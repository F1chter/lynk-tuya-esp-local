#include "mbedtls/aes.h"
#include "mbedtls/md.h"

enum TuyaProtocolVersion {
  TUYA_V34,
  TUYA_V35
};
#define V34_PORT 6668
#define RESPONSE_BUFFER_SIZE 256
#define AES_BLOCK 16

/* CLASS DEFINITION*/
template<TuyaProtocolVersion _VERSION>
class LynkTuyaDevice {
public:
  LynkTuyaDevice(IPAddress ipAdress, const char* localKey)
    : _ipAddress(ipAdress) {
    memcpy(_localKey, localKey, sizeof(_localKey));
    _prepareCmd03Request();  //prepare once, use in every handshake
  }


  // ---------- Networking ----------
  bool connectToPlug() {
    Serial.print("Connecting to plug ");
    Serial.print(_ipAddress);
    Serial.print(':');
    Serial.println(V34_PORT);

    if (!_client.connect(_ipAddress, V34_PORT)) {
      Serial.println("TCP connect failed");
      return false;
    }
    _client.setNoDelay(true);
    Serial.println("TCP connected");
    return true;
  }

  void handshake() {
    _sendCommand3();
    _lastReadOk = false;
    _readRawResponse();
    if (!_lastReadOk) return;
    _parseCommand3Response();
    _sendCommand5();
    _calculateSessionKey();
    _seqNo = 3;
  }

  void getStatus() {
    if (!_client.connected()) {
      connectToPlug();
      handshake();
      if (!_lastReadOk) return;
    }
    _sendCommand10();
    _lastReadOk = false;
    _readRawResponse();
    if (!_lastReadOk) return;
    _parseCommandResponse();
  }

  void enable(uint32_t timestamp) {
    String json = String(F("{\"protocol\":5,\"t\":"));
    char locbuf[10];
    ltoa(timestamp, locbuf, 10); //fun fact, it will broken in 2287 year;)
    json+=locbuf;
    //json += timestamp;
    json += F(",\"data\":{\"dps\":{\"1\":true}}}");
    
    
    if (!_client.connected()) {
      connectToPlug();
      handshake();
      if (!_lastReadOk) return;
    }
    
    _sendCommand0d(json);
    _lastReadOk = false;
    _readRawResponse();
    if (!_lastReadOk) return;
    _parseCommandResponse();
  }

  void disable(uint32_t timestamp) {
    String json = String(F("{\"protocol\":5,\"t\":"));
    json+= timestamp;
    json += F(",\"data\":{\"dps\":{\"1\":false}}}");
    if (!_client.connected()) {
      connectToPlug();
      handshake();
      if (!_lastReadOk) return;
    }
    _sendCommand0d(json);
    _lastReadOk = false;
    _readRawResponse();
    if (!_lastReadOk) return;
    _parseCommandResponse();
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

  void _readRawResponse() {
    memset(_response_buffer, 0, RESPONSE_BUFFER_SIZE);
    Serial.println("Reading raw response...");
    unsigned long start = millis();
    //unsigned long lastPrint = start;
    while (millis() - start < 5000) {
      if (_client.available()) {
        _responseSize = _client.read(_response_buffer, RESPONSE_BUFFER_SIZE);
        Serial.print("Got ");
        Serial.print(_responseSize);
        Serial.print(" bytes after ");
        Serial.print(millis() - start);
        Serial.println(" ms:");
        _lastReadOk = true;
        printHex(_response_buffer, _responseSize);

        return;
      }

      //if (millis() - lastPrint > 500) {
      //  Serial.print("...still waiting, connected=");
      //  Serial.println(client.connected());
      //  lastPrint = millis();
      //}
    }
    _lastReadOk = false;
    Serial.print(F("no response received within 5s, connected="));
    Serial.print(_client.connected());
    Serial.println();
  }

  void _calculateSessionKey() {
    uint8_t xorNonce[16];
    for (int i = 0; i < 16; i++) {
      xorNonce[i] = pgm_read_byte(LOCAL_NONCE_PADDED + i) ^ _deviceNonce[i];
    }
    Serial.println("xor nonce:");
    printHex(xorNonce, 16);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, _localKey, 128);
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, xorNonce, _sessionKey);
    Serial.println("Session Key:");
    printHex(_sessionKey, 16);

    mbedtls_aes_free(&aes);
  }

  /*====================================PREPARE REQUESTS====================================*/

  void _prepareCmd03Request() {
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

  /*====================================PARSE RESPONSES====================================*/
  void _parseCommand3Response() {  //tuya device sent packet with command 04 as response on packet with command 03
    uint8_t payload[64];
    memcpy(payload, _response_buffer + 20, 64);
    Serial.println("Encrypted response payload");
    printHex(payload, 64);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, _localKey, 128);
    uint8_t block[AES_BLOCK];
    for (size_t i = 0; i < 64; i += AES_BLOCK) {
      mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, payload + i, block);
      memcpy(payload + i, block, AES_BLOCK);
    }
    Serial.println("Decrypted response payload");
    printHex(payload, 64);
    //TODO decrypt only first block
    mbedtls_aes_free(&aes);

    memcpy(_deviceNonce, payload, 16);
    Serial.println("Device Nonce:");
    printHex(_deviceNonce, 16);
  }

  void _parseCommandResponse() {
    if (_responseSize < 56) {  //header 16 + retcode 4 + hmac 32 + suffix 4
      Serial.println("Response too short to parse");
      return;
    }

    uint32_t retcode = ((uint32_t)_response_buffer[16] << 24) | (_response_buffer[17] << 16) | (_response_buffer[18] << 8) | _response_buffer[19];
    Serial.print("retcode = ");
    Serial.println(retcode);

    if (_responseSize == 56) {
      Serial.println("No payload");
      return;
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
  }

  /*====================================COMMANDS====================================*/

  void _sendCommand3() {
    Serial.println("Sending command 3 ( 84 ) bytes");
    printHex(_cmd3_request, 84);
    size_t written = _client.write(_cmd3_request, 84);
    Serial.print("Bytes actually written: ");
    Serial.println(written);
  }

  void _sendCommand5() {
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

  void _sendCommand10() {
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

  void _sendCommand0d(String json) {
    // 1. Payload = local nonce, PKCS7-padded (16 -> 32 bytes)
    //String jsonBeforeDate = "{\"protocol\":5,\"t\":";
    //String jsonAfterDate = ",\"data\":{\"dps\":{\"1\":";
    //jsonAfterDate += enable ? F("true") : F("false");
    //jsonAfterDate += "}}}";
    uint8_t payload[256];
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

  /* VARIABLES*/
  IPAddress _ipAddress;
  uint8_t _localKey[16];

  //uint8_t _cmd3_payload[32];  //local nonce,PKCS7-padded, AES ECB encrypted with device's local key
  uint8_t _cmd3_request[84];
  uint8_t _response_buffer[RESPONSE_BUFFER_SIZE];
  bool _lastReadOk = true;
  int _responseSize = 0;
  uint8_t _deviceNonce[16];
  uint8_t _sessionKey[16];
  //TODO pass as init parameter
  WiFiClient _client;
  uint8_t _seqNo = 3;  //1-cmd3,2-cmd5

  const uint8_t V34_PREFIX_MAGIC[4] PROGMEM = { 0x00, 0x00, 0x55, 0xAA };
  const uint8_t V34_SUFFIX_MAGIC[4] PROGMEM = { 0x00, 0x00, 0xAA, 0x55 };
  const uint8_t V34_PAYLOAD_PREFIX_MAGIC[15] PROGMEM = { 0x33, 0x2e, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };  //3.4 00 00 00 ...
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