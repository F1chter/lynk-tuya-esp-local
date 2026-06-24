#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/gcm.h"

enum TuyaProtocolVersion {
  TUYA_V34,
  TUYA_V35
};
#define V34_PORT 6668
#define V35_PORT 6668
#define RESPONSE_BUFFER_SIZE 256
#define PAYLOAD_BUFFER_SIZE 256
#define AES_BLOCK 16

/* CLASS DEFINITION*/
template<TuyaProtocolVersion _VERSION>
class LynkTuyaDevice {
public:
  LynkTuyaDevice(IPAddress ipAdress, const char* localKey)
    : _ipAddress(ipAdress) {
    memcpy(_localKey, localKey, sizeof(_localKey));
    if (_VERSION == TUYA_V34)
      _prepareCmd03Request();  //prepare once, use in every handshake
    else if (_VERSION == TUYA_V35)
      _prepareCmd03RequestV35();
  }

  // ---------- Networking ----------
  bool connectToPlug() {
    Serial.print("Connecting to plug ");
    Serial.print(_ipAddress);
    Serial.print(':');
    Serial.print(V34_PORT);
    if (!_client.connect(_ipAddress, V34_PORT)) {
      Serial.println(" - failed");
      return false;
    }
    _client.setNoDelay(true);
    Serial.println(" - connected");
    return true;
  }

  bool closeConnection() {
    _client.stop();
    return true;
  }

  bool handshake() {
    _sendCommand3();
    if (!_readRawResponse() || !_parseCommand3Response())
      return false;
    _sendCommand5();
    _calculateSessionKey();
    _seqNo = 3;
  }

  bool getStatus(bool autoCloseConnection = true) {
    if (!_client.connected())
      if (!connectToPlug() || !handshake())
        if (autoCloseConnection || closeConnection())
          return false;
    _sendCommand10();
    if (!_readRawResponse() || !_parseCommandResponse())
      if (autoCloseConnection || closeConnection())
        return false;
    return true;
  }

  bool turnOn(uint32_t timestamp, bool autoCloseConnection = true) {
    String json = String(F("{\"protocol\":5,\"t\":"));
    char locbuf[10];
    ltoa(timestamp, locbuf, 10);  //fun fact, it will broken in 2287 year;)
    json += locbuf;
    //json += timestamp;
    json += F(",\"data\":{\"dps\":{\"1\":true}}}");

    if (!_client.connected())
      if (!connectToPlug() || !handshake())
        if (autoCloseConnection || closeConnection())
          return false;

    _sendCommand0d(json);
    if (!_readRawResponse() || !_parseCommandResponse())
      if (autoCloseConnection || closeConnection())
        return false;
  }

  bool turnOff(uint32_t timestamp, bool autoCloseConnection = true) {
    String json = String(F("{\"protocol\":5,\"t\":"));
    json += timestamp;
    json += F(",\"data\":{\"dps\":{\"1\":false}}}");
    if (!_client.connected())
      if (!connectToPlug() || !handshake())
        if (autoCloseConnection || closeConnection())
          return false;

    _sendCommand0d(json);
    if (!_readRawResponse() || !_parseCommandResponse())
      if (autoCloseConnection || closeConnection())
        return false;
  }

private:
  /*====================================HELPERS====================================*/
  void writeU32BE(uint8_t* buf, uint32_t v) {
    buf[0] = (v >> 24) & 0xFF;
    buf[1] = (v >> 16) & 0xFF;
    buf[2] = (v >> 8) & 0xFF;
    buf[3] = v & 0xFF;
  }

  // PKCS7 padding: always adds at least 1 byte, up to a full 16-byte block
  // if the input is already a multiple of 16 (matches Tuya's own padding) add more 16 bytes
  size_t pkcs7Pad(uint8_t* buf, size_t len) {
    uint8_t padLen = 16 - (len % 16);
    for (uint8_t i = 0; i < padLen; i++) buf[len + i] = padLen;
    return len + padLen;
  }

  /*ECB for v34*/

  // AES-128-ECB encrypt in place, len must be a multiple of 16
  void aesEcbEncrypt(const uint8_t* key, uint8_t* buf, size_t len) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, key, 128);
    for (size_t i = 0; i < len; i += 16) {
      mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, buf + i, buf + i);
    }
    mbedtls_aes_free(&aes);
  }

  // AES-128-ECB decrypt in place, len must be a multiple of 16
  void aesEcbDecrypt(const uint8_t* key, uint8_t* buf, size_t len) {
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, key, 128);
    for (size_t i = 0; i < len; i += 16) {
      mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, buf + i, buf + i);
    }
    mbedtls_aes_free(&aes);
  }

  /*GCM for v35*/

  // AES-128-GCM encrypt
  void aesGcmEncrypt(const uint8_t* key,
                     const uint8_t* plaintext,
                     size_t plaintextLen,
                     const uint8_t* iv,
                     size_t ivLen,
                     const uint8_t* authD,
                     size_t authDLen,
                     uint8_t* ciphertext,
                     uint8_t* tag,
                     size_t tagLen) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    int rc = mbedtls_gcm_crypt_and_tag(
      &gcm,
      MBEDTLS_GCM_ENCRYPT,
      plaintextLen,
      iv,
      ivLen,
      authD,
      authDLen,
      plaintext,
      ciphertext,
      tagLen,
      tag);
    mbedtls_gcm_free(&gcm);
    Serial.print("Encrypt result code:");
    Serial.println(rc);
  }

  // AES-128-GCM decrypt
  void aesGcmDecrypt(const uint8_t* key,
                     const uint8_t* ciphertext,
                     size_t ciphertextLen,
                     const uint8_t* iv,
                     size_t ivLen,
                     const uint8_t* authD,
                     size_t authDLen,
                     const uint8_t* tag,
                     size_t tagLen,
                     uint8_t* plaintext) {
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128);
    int rc = mbedtls_gcm_auth_decrypt(
      &gcm,
      ciphertextLen,
      iv,
      ivLen,
      authD,
      authDLen,
      tag,
      tagLen,
      ciphertext,
      plaintext);
    mbedtls_gcm_free(&gcm);
    Serial.print("Decrypt result code:");
    Serial.println(rc);
  }


  //return 32 byte signature hash
  void hmacSha256(const uint8_t* key, size_t keyLen,
                  const uint8_t* data, size_t dataLen, uint8_t* out32) {
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, key, keyLen);
    mbedtls_md_hmac_update(&ctx, data, dataLen);
    mbedtls_md_hmac_finish(&ctx, out32);
    mbedtls_md_free(&ctx);
  }

  void printHex(const uint8_t* buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
      if (buf[i] < 0x10) Serial.print('0');
      Serial.print(buf[i], HEX);
      //Serial.print(' ');
    }
    Serial.println();
  }

  bool _readRawResponse(uint32_t timeoutMs = 5000L) {
    memset(_response_buffer, 0, RESPONSE_BUFFER_SIZE);
    Serial.println("Reading raw response...");
    unsigned long start = millis();
    //unsigned long lastPrint = start;
    while (millis() - start < timeoutMs) {
      if (_client.available()) {
        _responseSize = _client.read(_response_buffer, RESPONSE_BUFFER_SIZE);
        Serial.print("Got ");
        Serial.print(_responseSize);
        Serial.print(" bytes after ");
        Serial.print(millis() - start);
        Serial.println(" ms:");
        printHex(_response_buffer, _responseSize);
        return true;
      }
      //if (millis() - lastPrint > 500) {
      //  Serial.print("...still waiting, connected=");
      //  Serial.println(client.connected());
      //  lastPrint = millis();
      //}
      return false;
    }
    Serial.print(F("no response received within timeout, connected="));
    Serial.println(_client.connected());
  }

  void _calculateSessionKey() {
    uint8_t xorNonce[16];
    for (int i = 0; i < 16; i++) {
      xorNonce[i] = pgm_read_byte(LOCAL_NONCE_PADDED + i) ^ _deviceNonce[i];
    }
    Serial.println("xor nonce:");
    printHex(xorNonce, 16);

    if (_VERSION == TUYA_V34) {
      memcpy(_sessionKey, xorNonce, 16);
      aesEcbEncrypt(_localKey, _sessionKey, 16);
    } else if (_VERSION == TUYA_V35) {
      uint8_t iv[12];
      memcpy_P(iv, LOCAL_NONCE_PADDED, 12);
      uint8_t tag[16];
      aesGcmEncrypt(_localKey, xorNonce, 16, iv, 12, nullptr, 0, _sessionKey, tag, 16);
    }
    Serial.println("Session Key:");
    printHex(_sessionKey, 16);
  }

  /*====================================PREPARE REQUESTS====================================*/
  void _prepareCmd03Request() {
    if (_VERSION == TUYA_V34) {
      _prepareCmd03RequestV34();
    } else if (_VERSION == TUYA_V35) {
      _prepareCmd03RequestV35();
    }
  }

  void _prepareCmd03RequestV34() {
    uint8_t payload[32];
    memcpy_P(payload, LOCAL_NONCE_PADDED, 32);  //Payload = local nonce, PKCS7-padded (16 -> 32 bytes)
    aesEcbEncrypt(_localKey, payload, 32);      //encrypt with localKey
    Serial.println("Encrypted payload: ");
    printHex(payload, 32);

    uint8_t hmacInput[48];  // header 16 + payload 32
    memcpy_P(hmacInput, V34_CMD3_HEADER, 16);
    memcpy(hmacInput + 16, payload, 32);
    uint8_t hmac[32];
    hmacSha256(_localKey, 16, hmacInput, 48, hmac);

    //Assemble full message: header + encrypted payload + hmac + suffix
    memcpy_P(_cmd3_request, V34_CMD3_HEADER, 16);
    memcpy(_cmd3_request + 16, payload, 32);
    memcpy(_cmd3_request + 48, hmac, 32);
    memcpy_P(_cmd3_request + 80, V34_SUFFIX_MAGIC, 4);
  }

  void _prepareCmd03RequestV35() {
    uint8_t payload[16];
    memcpy_P(payload, LOCAL_NONCE_PADDED, 16);  //without pad
    uint8_t iv[12];
    memcpy(iv, payload, 12);
    uint8_t authD[14];
    memcpy_P(authD, V35_CMD3_HEADER, 14);
    //payload, _localKey, iv, 16, V35_MAGIC, 14
    uint8_t tag[16];
    memset(tag, 0, 16);
    uint8_t encBuf[16];
    memset(encBuf, 0, 16);
    aesGcmEncrypt(_localKey, payload, 16, iv, 12, authD, 14, encBuf, tag, 16);
    //Serial.println("Encrypted Payload GCM:");
    //printHex(encBuf, 16);

    memcpy_P(_cmd3_request, V35_PREFIX_MAGIC, 4);
    memcpy_P(_cmd3_request + 4, V35_CMD3_HEADER, 14);
    memcpy(_cmd3_request + 18, iv, 12);
    memcpy(_cmd3_request + 30, encBuf, 16);
    memcpy(_cmd3_request + 46, tag, 16);
    memcpy_P(_cmd3_request + 62, V35_SUFFIX_MAGIC, 4);

    printHex(_cmd3_request, 66);
  }
  /*====================================PARSE RESPONSES====================================*/
  bool _parseCommand3Response() {
    if (_VERSION == TUYA_V34) {
      return _parseCommand3ResponseV34();
    } else if (_VERSION == TUYA_V35) {
      return _parseCommand3ResponseV35();
    }
    return false;
  }

  bool _parseCommand3ResponseV34() {  //tuya device sent packet with command 04 as response on packet with command 03
    uint8_t payload[16];              //use only 16/64 that contain remote nonce
    //TODO check response prefix, suffix, crc
    if (_responseSize < 36)  //header + payload
      return false;
    memcpy(payload, _response_buffer + 20, 16);
    Serial.println("Encrypted response payload");
    printHex(payload, 16);
    //decrypt only first block, that contain remote nonce
    aesEcbDecrypt(_localKey, payload, 16);
    Serial.println("Decrypted response payload");
    printHex(payload, 16);

    memcpy(_deviceNonce, payload, 16);
    Serial.println("Device Nonce:");
    printHex(_deviceNonce, 16);
    return true;
  }

  bool _parseCommand3ResponseV35() {  //tuya device sent packet with command 04 as response on packet with command 03
    if (_responseSize < 98)           //header + iv 12 + payload 52 + tag +16
      return false;
    uint8_t iv[12];
    memcpy(iv, _response_buffer + 18, 12);
    uint8_t payload[52];
    memcpy(payload, _response_buffer + 30, 52);
    uint8_t tag[16];
    memcpy(tag, _response_buffer + 82, 16);
    uint8_t authD[14];
    memcpy(authD, _response_buffer + 4, 14);
    uint8_t decPayload[52];
    memset(decPayload, 0, 52);

    aesGcmDecrypt(_localKey, payload, 52, iv, 12,
                  authD, 14, tag, 16,
                  decPayload);
    Serial.print("Decrypted Payload: ");
    printHex(decPayload, 52);

    memcpy(_deviceNonce, decPayload + 4, 16);
    Serial.println("Device Nonce:");
    printHex(_deviceNonce, 16);
    return true;
  }

  bool _parseCommandResponse() {
    if (_VERSION == TUYA_V34) {
      return _parseCommandResponseV34();
    } else if (_VERSION == TUYA_V35) {
      return _parseCommandResponseV35();
    }
    return false;
  }

  bool _parseCommandResponseV34() {
    if (_responseSize < 56) {  //header 16 + retcode 4 + hmac 32 + suffix 4
      Serial.println("Response too short to parse");
      return false;
    }

    uint32_t retcode = ((uint32_t)_response_buffer[16] << 24) | (_response_buffer[17] << 16) | (_response_buffer[18] << 8) | _response_buffer[19];
    Serial.print("retcode = ");
    Serial.println(retcode);

    if (_responseSize == 56) {
      Serial.println("No payload");
      return false;
    }

    uint8_t pLen = _responseSize - 56;
    uint8_t payload[RESPONSE_BUFFER_SIZE - 56];
    memcpy(payload, _response_buffer + 20, pLen);
    aesEcbDecrypt(_sessionKey, payload, pLen);

    // Strip PKCS7 padding (last byte = padding length)
    uint8_t padLen = payload[pLen - 1];
    uint8_t jsonLen = (padLen <= pLen) ? pLen - padLen : pLen;

    Serial.print("Decrypted JSON: ");
    for (uint8_t i = 0; i < jsonLen; i++) Serial.print((char)payload[i]);
    Serial.println();
    return true;
  }

  bool _parseCommandResponseV35() {
    if (_responseSize < 18) {  //no header
      Serial.println("Response with invalid header");
      return false;
    }
    uint32_t length = ((uint32_t)_response_buffer[14] << 24) | (_response_buffer[15] << 16) | (_response_buffer[16] << 8) | _response_buffer[17];
    if (_responseSize < 18 + length || length < 29) {  //header + iv + payload + tag
      Serial.println("Response with invalid payload");
      return false;
    }
    uint8_t iv[12];
    memcpy(iv, _response_buffer + 18, 12);
    uint32_t pLen = length - 12 - 16;
    uint8_t payload[pLen];
    memcpy(payload, _response_buffer + 30, pLen);
    uint8_t tag[16];
    memcpy(tag, _response_buffer + 30 + pLen, 16);

    uint8_t authD[14];
    memcpy(authD, _response_buffer + 4, 14);
    uint8_t decPayload[pLen];
    memset(decPayload, 0, pLen);

    aesGcmDecrypt(_sessionKey, payload, pLen, iv, 12,
                  authD, 14, tag, 16,
                  decPayload);

    Serial.print("Decrypted Payload: ");
    printHex(decPayload, pLen);

    if (pLen < 4) return false;
    uint32_t retcode = ((uint32_t)decPayload[0] << 24) | (decPayload[1] << 16) | (decPayload[2] << 8) | decPayload[3];
    Serial.print("Return code: ");
    Serial.println(retcode);

    Serial.print("Decrypted JSON: ");
    for (uint8_t i = 4; i < pLen; i++) Serial.print((char)decPayload[i]);
    Serial.println();

    return true;
  }

  /*====================================COMMANDS====================================*/

  void _sendCommand3() {
    if (_VERSION == TUYA_V34) {
      Serial.println("Sending command 3 ( 84 ) bytes");
      printHex(_cmd3_request, 84);
    } else {
      Serial.println("Sending command 3 ( 66 ) bytes");
      printHex(_cmd3_request, 66);
    }
    size_t written = _client.write(_cmd3_request, _VERSION == TUYA_V34 ? 84 : 66);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
  }

  void _sendCommand5() {
    if (_VERSION == TUYA_V34) {
      _sendCommand5V34();
    } else if (_VERSION == TUYA_V35) {
      _sendCommand5V35();
    }
  }

  void _sendCommand5V34() {
    uint8_t payload[48];
    hmacSha256(_localKey, 16, _deviceNonce, 16, payload);  //fill first 32byte
    pkcs7Pad(payload, 32);                                 //pad to 48bytes
    aesEcbEncrypt(_localKey, payload, 48);
    Serial.println("Encrypted payload: ");
    printHex(payload, 48);

    uint8_t hmacInput[64];  //16 + 48
    memcpy_P(hmacInput, V34_CMD5_HEADER, 16);
    memcpy(hmacInput + 16, payload, 48);
    uint8_t hmac[32];
    hmacSha256(_localKey, 16, hmacInput, 64, hmac);

    uint8_t _cmd5_request[100];
    memcpy_P(_cmd5_request, V34_CMD5_HEADER, 16);
    memcpy(_cmd5_request + 16, payload, 48);
    memcpy(_cmd5_request + 64, hmac, 32);
    memcpy_P(_cmd5_request + 96, V34_SUFFIX_MAGIC, 4);

    Serial.println("Sending command 5 ( 100 ) bytes");
    printHex(_cmd5_request, 100);
    size_t written = _client.write(_cmd5_request, 100);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
    // The plug does not reply to command 5 - the handshake is considered
    // complete once this is sent. The next message (a status query
    // or control command) should be encrypted/HMAC'd with the session key.
  }

  void _sendCommand5V35() {
    uint8_t payload[32];
    hmacSha256(_localKey, 16, _deviceNonce, 16, payload);
    uint8_t iv[12];
    memcpy_P(iv, LOCAL_NONCE_PADDED, 12);
    uint8_t authD[14];
    memcpy_P(authD, V35_CMD5_HEADER, 14);
    uint8_t tag[16];
    memset(tag, 0, 16);
    uint8_t encBuf[32];
    aesGcmEncrypt(_localKey, payload, 32, iv, 12, authD, 14, encBuf, tag, 16);
    Serial.println("Encrypted CMD5 Payload GCM:");
    printHex(encBuf, 32);
    uint8_t _cmd5_request[82];
    memcpy_P(_cmd5_request, V35_PREFIX_MAGIC, 4);
    memcpy_P(_cmd5_request + 4, V35_CMD5_HEADER, 14);
    memcpy(_cmd5_request + 18, iv, 12);
    memcpy(_cmd5_request + 30, encBuf, 32);
    memcpy(_cmd5_request + 62, tag, 16);
    memcpy_P(_cmd5_request + 78, V35_SUFFIX_MAGIC, 4);

    Serial.println("Sending command 5 ( 82 ) bytes");
    printHex(_cmd5_request, 82);
    size_t written = _client.write(_cmd5_request, 82);
    Serial.print("Bytes actually written: ");
    Serial.println(written);

    // The plug does not reply to command 5 - the handshake is considered
    // complete once this is sent. The next message (a status query
    // or control command) should be encrypted/HMAC'd with the session key.
  }

  void _sendCommand10() {
    if (_VERSION == TUYA_V34) {
      _sendCommand10V34();
    } else if (_VERSION == TUYA_V35) {
      _sendCommand10V35();
    }
  }

  void _sendCommand10V34() {
    uint8_t payload[16];
    memcpy_P(payload, CMD10_PAYLOAD_PADDED, 16);
    aesEcbEncrypt(_sessionKey, payload, 16);
    uint8_t seq = _seqNo++;

    uint8_t hmacInput[32];
    memcpy_P(hmacInput, V34_CMD10_HEADER, 16);
    writeU32BE(hmacInput + 4, seq);  // override sequence number
    memcpy(hmacInput + 16, payload, 16);
    uint8_t hmac[32];
    hmacSha256(_sessionKey, 16, hmacInput, 32, hmac);

    uint8_t _cmd10_request[68];
    memcpy_P(_cmd10_request, V34_CMD10_HEADER, 16);
    writeU32BE(_cmd10_request + 4, seq);  // override sequence number
    memcpy(_cmd10_request + 16, payload, 16);
    memcpy(_cmd10_request + 32, hmac, 32);
    memcpy_P(_cmd10_request + 64, V34_SUFFIX_MAGIC, 4);

    Serial.println("Sending command 10 (68 bytes):");
    printHex(_cmd10_request, 68);

    size_t written = _client.write(_cmd10_request, 68);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
  }

  void _sendCommand10V35() {
    uint8_t payload[2];
    memcpy_P(payload, CMD10_PAYLOAD_PADDED, 2);
    uint8_t iv[12];
    memcpy_P(iv, LOCAL_NONCE_PADDED, 12);
    uint8_t header[14];
    memcpy_P(header, V35_CMD10_HEADER, 14);
    writeU32BE(header + 4, _seqNo++);  // override sequence number
    uint8_t tag[16];
    memset(tag, 0, 16);
    uint8_t encBuf[2];
    aesGcmEncrypt(_sessionKey, payload, 2, iv, 12, header, 14, encBuf, tag, 16);

    uint8_t _cmd10_request[52];
    memcpy_P(_cmd10_request, V35_PREFIX_MAGIC, 4);
    memcpy(_cmd10_request + 4, header, 14);
    memcpy(_cmd10_request + 18, iv, 12);
    memcpy(_cmd10_request + 30, encBuf, 2);
    memcpy(_cmd10_request + 32, tag, 16);
    memcpy_P(_cmd10_request + 48, V35_SUFFIX_MAGIC, 4);

    Serial.println("Sending command 10 ( 52 ) bytes");
    printHex(_cmd10_request, 52);
    size_t written = _client.write(_cmd10_request, 52);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
  }

  void _sendCommand0d(String json) {
    if (_VERSION == TUYA_V34) {
      _sendCommand0dV34(json);
    } else if (_VERSION == TUYA_V35) {
      _sendCommand0dV35(json);
    }
  }

  void _sendCommand0dV34(String json) {
    if (json.length() > (PAYLOAD_BUFFER_SIZE - 31)) {  //15 - v3.4000... prefix and up to 16 pkcs7Pad
      Serial.println("Payload buffer is not enough to store json, increase PAYLOAD_BUFFER_SIZE");
    }
    uint8_t payload[PAYLOAD_BUFFER_SIZE];
    memset(payload, 0, 256);
    memcpy_P(payload, V34_PAYLOAD_PREFIX_MAGIC, 15);
    memcpy(payload + 15, json.c_str(), json.length());
    size_t payloadLen = pkcs7Pad(payload, 15 + json.length());

    Serial.println("Payload:");
    printHex(payload, payloadLen);
    aesEcbEncrypt(_sessionKey, payload, payloadLen);

    uint8_t header[16];
    memcpy_P(header, V34_PREFIX_MAGIC, 4);
    writeU32BE(header + 4, _seqNo++);          // sequence number
    writeU32BE(header + 8, 0x0d);              // command 13 = set
    writeU32BE(header + 12, payloadLen + 36);  // payload + hmac(32) + suffix(4)

    uint8_t hmacInput[16 + payloadLen];
    memcpy(hmacInput, header, 16);
    memcpy(hmacInput + 16, payload, payloadLen);
    uint8_t hmac[32];
    hmacSha256(_sessionKey, 16, hmacInput, 16 + payloadLen, hmac);

    uint8_t _cmd0d_request[payloadLen + 52];  //header(16) +  payload + hmac(32) + suffix(4)
    memcpy(_cmd0d_request, header, 16);
    memcpy(_cmd0d_request + 16, payload, payloadLen);
    memcpy(_cmd0d_request + payloadLen + 16, hmac, 32);
    memcpy_P(_cmd0d_request + payloadLen + 48, V34_SUFFIX_MAGIC, 4);

    Serial.print("Sending command 0d (");
    Serial.print(payloadLen + 52);
    Serial.println(" bytes):");
    printHex(_cmd0d_request, payloadLen + 52);

    size_t written = _client.write(_cmd0d_request, payloadLen + 52);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
  }

  void _sendCommand0dV35(String json) {
    if (json.length() > (PAYLOAD_BUFFER_SIZE - 15)) {
      Serial.println("Payload buffer is not enough to store json, increase PAYLOAD_BUFFER_SIZE");
    }
    uint8_t payload[PAYLOAD_BUFFER_SIZE];
    memset(payload, 0, 256);
    memcpy_P(payload, V35_PAYLOAD_PREFIX_MAGIC, 15);
    memcpy(payload + 15, json.c_str(), json.length());
    size_t payloadLen = 15 + json.length();
    //{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x1e };
    uint8_t header[14];
    memset(header, 0, 2);
    writeU32BE(header + 2, _seqNo++);          // sequence number
    writeU32BE(header + 6, 0x0d);              // command 13 = set
    writeU32BE(header + 10, payloadLen + 28);  // iv(12)+payload+tag(16)

    uint8_t iv[12];
    memcpy_P(iv, LOCAL_NONCE_PADDED, 12);
    uint8_t tag[16];
    memset(tag, 0, 16);
    uint8_t encBuf[payloadLen];
    aesGcmEncrypt(_sessionKey, payload, payloadLen, iv, 12, header, 14, encBuf, tag, 16);

    uint8_t _cmd0d_request[payloadLen + 50];
    memcpy_P(_cmd0d_request, V35_PREFIX_MAGIC, 4);
    memcpy(_cmd0d_request + 4, header, 14);
    memcpy(_cmd0d_request + 18, iv, 12);
    memcpy(_cmd0d_request + 30, encBuf, payloadLen);
    memcpy(_cmd0d_request + 30 + payloadLen, tag, 16);
    memcpy_P(_cmd0d_request + 46 + payloadLen, V35_SUFFIX_MAGIC, 4);

    Serial.print("Sending command 0d ( ");
    Serial.print(payloadLen + 50);
    Serial.println(" ) bytes");
    printHex(_cmd0d_request, 52);
    size_t written = _client.write(_cmd0d_request, payloadLen + 50);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
  }

  /* VARIABLES*/
  IPAddress _ipAddress;
  uint8_t _localKey[16];

  //uint8_t _cmd3_payload[32];  //local nonce,PKCS7-padded, AES ECB encrypted with device's local key
  uint8_t _cmd3_request[84];  //84 v34; 66v35
  uint8_t _response_buffer[RESPONSE_BUFFER_SIZE];
  //bool _lastReadOk = true;
  int _responseSize = 0;
  uint8_t _deviceNonce[16];
  uint8_t _sessionKey[16];
  //TODO pass as init parameter
  WiFiClient _client;
  uint8_t _seqNo = 3;  //1-cmd3,2-cmd5
  const uint8_t V35_CMD3_HEADER[14] PROGMEM = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x2c };
  const uint8_t V35_PREFIX_MAGIC[4] PROGMEM = { 0x00, 0x00, 0x66, 0x99 };
  const uint8_t V35_SUFFIX_MAGIC[4] PROGMEM = { 0x00, 0x00, 0x99, 0x66 };
  const uint8_t V35_CMD5_HEADER[14] PROGMEM = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x3c };
  const uint8_t V35_CMD10_HEADER[14] PROGMEM = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x1e };
  const uint8_t V34_PREFIX_MAGIC[4] PROGMEM = { 0x00, 0x00, 0x55, 0xAA };
  const uint8_t V34_SUFFIX_MAGIC[4] PROGMEM = { 0x00, 0x00, 0xAA, 0x55 };
  const uint8_t V34_PAYLOAD_PREFIX_MAGIC[15] PROGMEM = { 0x33, 0x2e, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  //3.4 00 00 00 ...
  const uint8_t V35_PAYLOAD_PREFIX_MAGIC[15] PROGMEM = { 0x33, 0x2e, 0x35, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  //3.5 00 00 00 ...

  //const uint8_t _localNonce[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f' };  //Fixed local nonce used by tinytuya for the handshake
  const uint8_t LOCAL_NONCE_PADDED[32] PROGMEM = { 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                                                   0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
                                                   0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10,
                                                   0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10 };  //local nonce, PKCS7-padded (16 -> 32 bytes)
  const uint8_t V34_CMD3_HEADER[16] PROGMEM = { 0x00, 0x00, 0x55, 0xaa, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x44 };
  const uint8_t V34_CMD5_HEADER[16] PROGMEM = { 0x00, 0x00, 0x55, 0xaa, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x54 };
  const uint8_t V34_CMD10_HEADER[16] PROGMEM = { 0x00, 0x00, 0x55, 0xaa, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x34 };
  const uint8_t CMD10_PAYLOAD_PADDED[16] PROGMEM = { 0x7b, 0x7d, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e };  // empty json {}
};