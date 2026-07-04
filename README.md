# lynk-tuya-esp-local
Control Tuya Devices from ESP32 over local network

Supported v3.4 and v3.5 devices

## Setup:
1. Use [TinyTuya](https://github.com/jasonacox/tinytuya/) to define device ip and Local Key

2. Create secrets.h and define next variables:
```
IPAddress plug1IP(192, 168, 1, 11); /*device ip, recommended to make it predefined on router */
#define PLUG1KEY "$`pfDH'nzZKIe]|]" /*local key from tinyTuya */
#define WIFI_SSID "MY_WIFI"
#define WIFI_PASS "MY_PASSWORD"

```

## `LynkTuya.h`

`LynkTuyaDevice` is a template class for controlling Tuya LAN devices that use the **3.4** or **3.5** protocol directly over TCP without relying on the Tuya cloud.

The class implements the complete LAN communication flow:

* TCP connection management
* Tuya authentication handshake
* Session key generation
* AES encryption/decryption
* HMAC-SHA256 authentication (v3.4)
* AES-GCM authenticated encryption (v3.5)
* Device status requests
* Power on/off commands

## Template Parameter

```cpp
template<TuyaProtocolVersion VERSION>
class LynkTuyaDevice;
```

Supported protocol versions:

```cpp
enum TuyaProtocolVersion {
    TUYA_V34,
    TUYA_V35
};
```

Example:

```cpp
LynkTuyaDevice<TUYA_V34> plug1(ip1, localKey1);
LynkTuyaDevice<TUYA_V35> plug2(ip2, localKey2);
```

| Parameter   | Description                                            |
| ----------- | ------------------------------------------------------ |
| `ipAddress` | IP address of the Tuya device.                         |
| `localKey`  | 16-byte Local Key obtained from tinyTuya. |

The constructor prepares the protocol-specific handshake packet, so it only needs to be generated once.

---

## Public API

```cpp
bool connectToPlug();
```

Opens a TCP connection to the device.

Returns `true` on success.

```cpp
bool closeConnection();
```

Closes the TCP connection.

```cpp
bool handshake();
```

Performs the Tuya authentication handshake.

This method:

1. Sends Command 3
2. Receives the device nonce
3. Sends Command 5
4. Generates the session key

Must be completed before encrypted commands can be sent.

Returns `true` on success.

```cpp
bool getStatus(bool autoCloseConnection = true);
```

Requests the current device status.

If no TCP connection exists, the class automatically:

1. Connects
2. Performs the handshake
3. Sends the status request

Returns `true` if a valid response is received.

```cpp
bool turnOn(uint32_t timestamp,
            bool autoCloseConnection = true);
```

Turns the device ON.

The timestamp should be the current Unix time in seconds.

```cpp
bool turnOff(uint32_t timestamp,
             bool autoCloseConnection = true);
```

Turns the device OFF.

```cpp
bool turn(uint32_t timestamp,
          bool on,
          bool autoCloseConnection = true);
```

Generic method for changing the relay state.

Parameters:

| Parameter             | Description                                                          |
| --------------------- | -------------------------------------------------------------------- |
| `timestamp`           | Current Unix timestamp (seconds).                                    |
| `on`                  | `true` to switch ON, `false` to switch OFF.                          |
| `autoCloseConnection` | Automatically closes the TCP connection after the command completes. |

---

## Connection Behaviour

By default every public command:

1. Opens a TCP connection (if necessary)
2. Performs the handshake
3. Executes the command
4. Closes the connection

To execute multiple commands without reconnecting:

```cpp
plug.connectToPlug();
plug.handshake();

plug.getStatus(false);
plug.turnOn(timestamp, false);
plug.turnOff(timestamp, false);

plug.closeConnection();
```

---

## Protocol Support

### Tuya v3.4

* AES-128 ECB encryption
* HMAC-SHA256 packet authentication
* PKCS#7 padding

### Tuya v3.5

* AES-128 GCM authenticated encryption
* No PKCS#7 padding
* Built-in authentication tag

The template selects the appropriate implementation at compile time.

---

## Example

```cpp
IPAddress ip(192,168,1,100);

LynkTuyaDevice<TUYA_V35> plug(
    ip,
    "0123456789abcdef"
);

plug.turnOn(now());

plug.getStatus();
```
