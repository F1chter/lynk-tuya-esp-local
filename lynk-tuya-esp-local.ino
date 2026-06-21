// ESP32 + Arduino IDE
// Tuya local protocol v3.4 - Step 1: open TCP connection, send Command 3
// (negotiate session key). This is the first message of the handshake
// described in GetKey.ino of FrBerger83/EspTuya, reimplemented from scratch
// using mbedtls (already bundled with the ESP32 Arduino core).

#include "secrets.h"
#include <WiFi.h>
#include "LynkTime.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"

//#define EMULATE_SEND 1

// ---------- WiFi ----------

const char* ntpServer = "pool.ntp.org";

// ---------- Device info (from tinytuya scan) ----------

const uint16_t plugPort = 6668;

// ---------- Protocol constants (3.4 / "55AA" messages) ----------
const uint8_t PREFIX_MAGIC[4] = { 0x00, 0x00, 0x55, 0xAA };
const uint8_t SUFFIX_MAGIC[4] = { 0x00, 0x00, 0xAA, 0x55 };
const uint8_t PAYLOAD_PREFIX_MAGIC[15] = {0x33,0x2e,0x34,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}; //3.4 00 00 00 ...
// Fixed local nonce used by tinytuya / EspTuya for the handshake
const uint8_t local_nonce[16] = {
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

WiFiClient client;

// ---------- Helpers ----------

void writeU32BE(uint8_t* buf, uint32_t v) {
  buf[0] = (v >> 24) & 0xFF;
  buf[1] = (v >> 16) & 0xFF;
  buf[2] = (v >> 8) & 0xFF;
  buf[3] = v & 0xFF;
}

// PKCS7 padding: always adds at least 1 byte, up to a full 16-byte block
// if the input is already a multiple of 16 (matches Tuya's own padding).
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

// ---------- Networking ----------

bool connectToPlug() {
  Serial.print("Connecting to plug ");
  Serial.print(plugIP);
  Serial.print(':');
  Serial.println(plugPort);

  if (!client.connect(plugIP, plugPort)) {
    Serial.println("TCP connect failed");
    return false;
  }
  client.setNoDelay(true);
  Serial.println("TCP connected");
  return true;
}

// ---------- Command 3: negotiate session key ----------
uint8_t seq = 1;
bool sendCommand3() {
  // 1. Payload = local nonce, PKCS7-padded (16 -> 32 bytes)
  uint8_t payload[32];
  memcpy(payload, local_nonce, 16);
  size_t payloadLen = pkcs7Pad(payload, 16);

  // 2. Encrypt payload with the device's local key
  aesEcbEncrypt(localKey, payload, payloadLen);

  // 3. Header: magic(4) + seqno(4) + command(4) + remaining length(4)
  uint8_t header[16];
  memcpy(header, PREFIX_MAGIC, 4);
  writeU32BE(header + 4, seq++);                     // sequence number
  writeU32BE(header + 8, 3);                     // command 3 = negotiate session key
  writeU32BE(header + 12, payloadLen + 32 + 4);  // payload + hmac(32) + suffix(4)

  // 4. HMAC-SHA256 over header+payload, keyed with the device's local key
  uint8_t hmacInput[16 + 32];
  memcpy(hmacInput, header, 16);
  memcpy(hmacInput + 16, payload, payloadLen);
  Serial.println("Raw header: ");
  printHex(header, 16);
  Serial.println("Raw payload: ");
  printHex(payload, payloadLen);

  uint8_t hmac[32];
  hmacSha256(localKey, 16, hmacInput, 16 + payloadLen, hmac);

  // 5. Assemble full message: header + encrypted payload + hmac + suffix
  uint8_t msg[16 + 32 + 32 + 4];
  size_t pos = 0;
  memcpy(msg + pos, header, 16);
  pos += 16;
  memcpy(msg + pos, payload, payloadLen);
  pos += payloadLen;
  memcpy(msg + pos, hmac, 32);
  pos += 32;
  memcpy(msg + pos, SUFFIX_MAGIC, 4);
  pos += 4;

  Serial.print("Sending command 3 (");
  Serial.print(pos);
  Serial.println(" bytes):");
  printHex(msg, pos);
#ifndef EMULATE_SEND
  size_t written = client.write(msg, pos);
  Serial.print("Bytes actually written: ");
  Serial.println(written);
#endif 
  return true;
}
uint8_t cmdResponse[256];// = {0x00,0x00,0x55,0xaa,0x00,0x00,0x3e,0x87,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x68,0x00,0x00,0x00,0x00,0x09,0x62,0xef,0x51,0x4b,0xa1,0xb6,0x5d,0x0e,0x50,0xc9,0x14,0xf0,0x61,0xf8,0x35,0x50,0x09,0x74,0x91,0x5d,0x34,0x51,0x90,0x66,0xe2,0x5a,0x15,0x91,0xda,0x40,0xc6,0xdd,0x14,0x2b,0x4e,0xaf,0x8c,0x17,0x54,0xb4,0x71,0x1c,0xab,0x04,0xa9,0x15,0xe0,0x75,0x43,0x4e,0xae,0xf8,0x4b,0x14,0xa2,0xf0,0x00,0x13,0x3f,0xb5,0x4f,0x09,0x7d,0x83,0x7a,0xdc,0xe9,0x7e,0x4c,0x05,0x97,0xb7,0xee,0x8c,0x67,0x15,0x56,0xe9,0x36,0xf1,0x2f,0xcf,0x25,0x0e,0xe8,0x8d,0xf6,0xcf,0xf2,0x85,0x08,0xb9,0xa0,0x6b,0xac,0x00,0x00,0xaa,0x55};
bool cmdResponseOK = false;

void readRawResponse() {
  memset(cmdResponse, 0, sizeof(cmdResponse));
  Serial.println("Raw response from plug:");
  unsigned long start = millis();
  unsigned long lastPrint = start;
  while (millis() - start < 5000) {
    if (client.available()) {
      //uint8_t buf[256];
      int n = client.read(cmdResponse, sizeof(cmdResponse));
      Serial.print("Got ");
      Serial.print(n);
      Serial.print(" bytes after ");
      Serial.print(millis() - start);
      Serial.println(" ms:");
      cmdResponseOK = true;
      printHex(cmdResponse, n);

      return;
    }
    if (millis() - lastPrint > 500) {
      Serial.print("...still waiting, connected=");
      Serial.println(client.connected());
      lastPrint = millis();
    }
  }
  Serial.print("(no response received within 5s, connected=");
  Serial.print(client.connected());
  Serial.println(")");
}

uint8_t encryptedPayloadCmd3[64];

uint32_t read_be32(const uint8_t* p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | ((uint32_t)p[3]);
}

void copyEncryptedPayloadCmd3() {
  uint32_t len = read_be32(cmdResponse + 12);

  // length includes:
  // 4 bytes retcode
  // payload
  // 32 bytes hmac
  // 4 magic???
  uint32_t payloadLen = len - 4 - 32 - 4;
  Serial.print("payloadLen=");
  Serial.println(payloadLen);
  memcpy(
    encryptedPayloadCmd3,
    cmdResponse + 20,
    payloadLen);
  Serial.println("Encrypted response payload");
  printHex(encryptedPayloadCmd3, 64);
}


#define AES_BLOCK 16
uint8_t decryptedPayloadCmd3[64];

void decryptCmd3Response() {
  //uint8_t key[16] = "OE47Z.)byJ'/~:]s";

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  mbedtls_aes_setkey_dec(&aes, localKey, 128);

  uint8_t block[AES_BLOCK];
  for (size_t i = 0; i < 64; i += AES_BLOCK) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT,
                          encryptedPayloadCmd3 + i, block);
    memcpy(decryptedPayloadCmd3 + i, block, AES_BLOCK);
  }

  Serial.println("Decrypted CMD3 Response");
  printHex(decryptedPayloadCmd3, 64);

  mbedtls_aes_free(&aes);
}

uint8_t remote_nonce[16]; //= {'a', '7', '4', '1', '5', '7', 'f', '3', '1', 'f', '4', '5', '3', '0', '6', 'd'};

void copyRemoteNonce() {
  memcpy(
    remote_nonce,
    decryptedPayloadCmd3,
    16);
  Serial.println("Remote Nonce:");
  printHex(remote_nonce, 16);
}


uint8_t xor_nonce[16];

void xorNonce() {
  for (int i = 0; i < 16; i++) {
    xor_nonce[i] = local_nonce[i] ^ remote_nonce[i];
  }
  Serial.println("XOR NONCE:");
  printHex(xor_nonce, 16);
}

uint8_t session_key[16];//= {0x5e,0xae,0xda,0x83,0x10,0x0d,0x21,0x30,0xa0,0x21,0xa2,0x7b,0x6e,0xe0,0x74,0xf7};

void deriveSessionKey() {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  mbedtls_aes_setkey_enc(&aes, localKey, 128);

  mbedtls_aes_crypt_ecb(
    &aes,
    MBEDTLS_AES_ENCRYPT,
    xor_nonce,
    session_key);

  Serial.println("Session Key:");
  printHex(session_key, 16);

  mbedtls_aes_free(&aes);
}



void _aes_encrypt_ecb(uint8_t* key, uint8_t* input, size_t len, uint8_t* output) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);

  mbedtls_aes_setkey_enc(&aes, key, 128);

  for (size_t i = 0; i < len; i += 16) {
    mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT,
                          input + i, output + i);
  }

  mbedtls_aes_free(&aes);
}

// ---------- Command 5: finalize session key negotiation ----------
// deviceNonce = the 16-byte nonce you extracted from the plug's Command 4
// (RESP) reply, after decrypting that reply with the LOCAL key (AES-ECB).
bool sendCommand5() {
  // 1. Payload = HMAC-SHA256(local_key, device_nonce), sent PLAINTEXT
  //    (like command 3, FINISH's payload is not AES-encrypted)
  uint8_t payload[48];
  hmacSha256(localKey, 16, remote_nonce, 16, payload);

  // 2. Header: magic(4) + seqno(4) + command(4) + remaining length(4)
  uint8_t header[16];
  memcpy(header, PREFIX_MAGIC, 4);
  writeU32BE(header + 4, seq++);                  // 2nd message the client sends
  writeU32BE(header + 8, 5);                  // command 5 = finalize session key
  writeU32BE(header + 12, 48 + 32 + 4);       // payload(48) + hmac(32) + footer(4)
  //payloadLen instead 48
  // 3. Outer frame HMAC over header+payload - still keyed with the LOCAL key.
  //    The session key only applies to messages sent AFTER this handshake
  //    completes, not to the handshake messages themselves.
  uint8_t hmacInput[16 + 48]; //TODO payloadLen
  memcpy(hmacInput, header, 16);
  //memcpy(hmacInput + 16, payload, 32);

  Serial.println("Raw header: ");
  printHex(header, 16);
  Serial.println("Raw payload: ");
  printHex(payload, 32);
  size_t payloadLen = pkcs7Pad(payload, 32);
  Serial.println("Pad payload: ");
  printHex(payload, payloadLen);
  aesEcbEncrypt(localKey, payload, payloadLen);
  Serial.println("Encrypted payload: ");
  printHex(payload, payloadLen);

  memcpy(hmacInput + 16, payload, payloadLen); //copy after payload encription
  uint8_t hmac[32];
  hmacSha256(localKey, 16, hmacInput, 16 + payloadLen, hmac);
  
  //aesEcbEncrypt(localKey, hmac, 16+32);
  // 4. Assemble: header + payload + hmac + footer
  uint8_t msg[16 + payloadLen + 32 + 4];
  size_t pos = 0;
  memcpy(msg + pos, header, 16);      pos += 16;
  memcpy(msg + pos, payload, payloadLen);     pos += payloadLen;
  memcpy(msg + pos, hmac, 32);        pos += 32;
  memcpy(msg + pos, SUFFIX_MAGIC, 4); pos += 4;

  Serial.print("Sending command 5 (");
  Serial.print(pos);
  Serial.println(" bytes):");
  printHex(msg, pos);
#ifndef EMULATE_SEND
  size_t written = client.write(msg, pos);
  Serial.print("Bytes actually written: ");
  Serial.println(written);
#endif
  // The plug does not reply to command 5 - the handshake is considered
  // complete once this is sent. The next message you send (a status query
  // or control command) should be encrypted/HMAC'd with the session key.
  return true;
}


// ---------- Command 5: get status ----------

bool sendCommand10() {
  // 1. Payload = local nonce, PKCS7-padded (16 -> 32 bytes)
  uint8_t payload[16];
  memset(payload, 0 , 16);
  payload[0] = 0x7b; //{
  payload[1] = 0x7d; //}
  //memcpy(payload, local_nonce, 16);
  size_t payloadLen = pkcs7Pad(payload, 2);

  // 2. Encrypt payload with the device's local key
  aesEcbEncrypt(session_key, payload, payloadLen);

  // 3. Header: magic(4) + seqno(4) + command(4) + remaining length(4)
  uint8_t header[16];
  memcpy(header, PREFIX_MAGIC, 4);
  writeU32BE(header + 4, seq++);                     // sequence number
  writeU32BE(header + 8, 0x10);                     // command 3 = negotiate session key
  writeU32BE(header + 12, payloadLen + 32 + 4);  // payload + hmac(32) + suffix(4)

  // 4. HMAC-SHA256 over header+payload, keyed with the device's local key
  uint8_t hmacInput[16 + 16];
  memcpy(hmacInput, header, 16);
  memcpy(hmacInput + 16, payload, payloadLen);

  uint8_t hmac[32];
  hmacSha256(session_key, 16, hmacInput, 16 + payloadLen, hmac);

  // 5. Assemble full message: header + encrypted payload + hmac + suffix
  uint8_t msg[16 + 16 + 32 + 4];
  size_t pos = 0;
  memcpy(msg + pos, header, 16); pos += 16;
  memcpy(msg + pos, payload, payloadLen); pos += payloadLen;
  memcpy(msg + pos, hmac, 32); pos += 32;
  memcpy(msg + pos, SUFFIX_MAGIC, 4); pos += 4;

  Serial.print("Sending command 10 (");
  Serial.print(pos);
  Serial.println(" bytes):");
  printHex(msg, pos);
#ifndef EMULATE_SEND
  size_t written = client.write(msg, pos);
  Serial.print("Bytes actually written: ");
  Serial.println(written);
#endif
  return true;

}

void readAndDecryptStatusResponse() {
  Serial.println("Waiting for status response...");
  unsigned long start = millis();
  while (millis() - start < 5000) {
    if (client.available()) {
      uint8_t buf[256];
      int n = client.read(buf, sizeof(buf));
      Serial.print("Got ");
      Serial.print(n);
      Serial.println(" bytes:");
      printHex(buf, n);

      if (n < 16 + 4 + 32 + 4) {
        Serial.println("Response too short to parse");
        return;
      }

      uint32_t retcode = ((uint32_t)buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
      Serial.print("retcode = ");
      Serial.println(retcode);

      // total - header(16) - retcode(4) - hmac(32) - footer(4)
      size_t encLen = n - 16 - 4 - 32 - 4;
      uint8_t payload[256];
      memcpy(payload, buf + 20, encLen);

      aesEcbDecrypt(session_key, payload, encLen);

      // Strip PKCS7 padding (last byte = padding length)
      uint8_t padLen = payload[encLen - 1];
      size_t jsonLen = (padLen <= encLen) ? encLen - padLen : encLen;

      Serial.print("Decrypted JSON: ");
      for (size_t i = 0; i < jsonLen; i++) Serial.print((char)payload[i]);
      Serial.println();
      cmdResponseOK = true;
      return;
    }
  }
  Serial.println("(no response received within 5s)");
}

bool sendCommand0d(bool enable) {
  // 1. Payload = local nonce, PKCS7-padded (16 -> 32 bytes)
  String jsonBeforeDate = "{\"protocol\":5,\"t\":";
  String jsonAfterDate = ",\"data\":{\"dps\":{\"1\":";
  jsonAfterDate += enable ? F("true") : F("false");
  jsonAfterDate += "}}}";
  uint8_t payload[128];
  memset(payload, 0 , 64);
  memcpy(payload, PAYLOAD_PREFIX_MAGIC, 15);
  memcpy(payload + 15, jsonBeforeDate.c_str(), jsonBeforeDate.length());
  memcpy(payload + 15 +  jsonBeforeDate.length(), timebuf, 10);
  memcpy(payload + 15 +  jsonBeforeDate.length() + 10, jsonAfterDate.c_str(), jsonAfterDate.length());
  size_t payloadLen = pkcs7Pad(payload, 15 +  jsonBeforeDate.length() + 10 + jsonAfterDate.length());
  
  Serial.println("Payload:");
  printHex(payload, payloadLen);
  // 2. Encrypt payload with the device's local key
  aesEcbEncrypt(session_key, payload, payloadLen);

  // 3. Header: magic(4) + seqno(4) + command(4) + remaining length(4)
  uint8_t header[16];
  memcpy(header, PREFIX_MAGIC, 4);
  writeU32BE(header + 4, seq++);                     // sequence number
  writeU32BE(header + 8, 0x0d);                     // command 13 = set
  writeU32BE(header + 12, payloadLen + 32 + 4);  // payload + hmac(32) + suffix(4)

  // 4. HMAC-SHA256 over header+payload, keyed with the device's local key
  uint8_t hmacInput[16 + payloadLen];
  memcpy(hmacInput, header, 16);
  memcpy(hmacInput + 16, payload, payloadLen);

  uint8_t hmac[32];
  hmacSha256(session_key, 16, hmacInput, 16 + payloadLen, hmac);

  // 5. Assemble full message: header + encrypted payload + hmac + suffix
  uint8_t msg[16 + payloadLen + 32 + 4];
  size_t pos = 0;
  memcpy(msg + pos, header, 16); pos += 16;
  memcpy(msg + pos, payload, payloadLen); pos += payloadLen;
  memcpy(msg + pos, hmac, 32); pos += 32;
  memcpy(msg + pos, SUFFIX_MAGIC, 4); pos += 4;

  Serial.print("Sending command 0d (");
  Serial.print(pos);
  Serial.println(" bytes):");
  printHex(msg, pos);
#ifndef EMULATE_SEND
  size_t written = client.write(msg, pos);
  Serial.print("Bytes actually written: ");
  Serial.println(written);
#endif
  
  return true;

}


// ---------- Arduino entry points ----------

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  WiFi.setSleep(false);  // power-save mode can delay/drop fast LAN replies

  configTime(0, 0, ntpServer);
  loctimestamp = getTime();
  lastupdate = millis();

  Serial.print("loctimestamp: ");
  Serial.println(loctimestamp);

  if (connectToPlug()) {
    Serial.println("Local Key:");
    printHex(localKey, 16);
    Serial.println("Local Nonce:");
    printHex(local_nonce, 16);
    sendCommand3();
#ifndef EMULATE_SEND
    readRawResponse();
    if (!cmdResponseOK) return;
    cmdResponseOK = false;
#endif
    copyEncryptedPayloadCmd3();
    decryptCmd3Response();
    copyRemoteNonce();
    xorNonce();
    
    sendCommand5();
#ifndef EMULATE_SEND
    //readRawResponse();
    //if (!cmdResponseOK) return;
    //cmdResponseOK = false;
#endif
    deriveSessionKey();
    sendCommand10();

#ifndef EMULATE_SEND
    readAndDecryptStatusResponse();
    if (!cmdResponseOK) return;
#endif
    updateTime();
    sendCommand0d(true);
    readAndDecryptStatusResponse();
    if (!cmdResponseOK) return;
    delay(5000);
    updateTime();
    sendCommand0d(false);
    readAndDecryptStatusResponse();
    //if (!cmdResponseOK) return;
  }
}

void loop() {
  // Next step: decrypt the response above to recover the plug's
  // "remote nonce", then send Command 5 to finalize the handshake.
}
