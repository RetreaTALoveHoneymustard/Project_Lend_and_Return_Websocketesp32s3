/**
 * ESP32-S3 <-> PN532 over I2C, relayed through a cloud WebSocket backend.
 *
 * Wiring (I2C):
 *   PN532 VCC -> 3V3        PN532 SDA -> ESP32-S3 GPIO8 (PN532_SDA below)
 *   PN532 GND -> GND        PN532 SCL -> ESP32-S3 GPIO9 (PN532_SCL below)
 *   PN532 board switches set to I2C mode (check silkscreen: usually SEL0/SEL1 = off/off).
 *   Adjust PN532_SDA/PN532_SCL to whatever pins are labeled SDA/SCL (or A4/A5)
 *   on your specific "ESP32-S3 Uno-shape" board.
 *
 * Libraries (Arduino Library Manager):
 *   - Adafruit PN532        (by Adafruit)
 *   - Adafruit BusIO        (dependency of the above)
 *   - WebSockets             (by Markus Sattler / Links2004)
 *   - ArduinoJson  (v7)
 *
 * Board: "ESP32S3 Dev Module" (or your board's exact entry) in Arduino IDE.
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <Adafruit_PN532.h>

#include "secrets.h"

// ---- PN532 (I2C) ----------------------------------------------------------
#define PN532_SDA   8
#define PN532_SCL   9
#define PN532_IRQ   -1   // not used in polling mode
#define PN532_RESET -1   // tie PN532 RSTPDN high, or wire to a GPIO and set it here

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// ---- WebSocket client -------------------------------------------------
WebSocketsClient webSocket;

// ---- helpers ------------------------------------------------------------
String uidToHex(uint8_t *uid, uint8_t len) {
  String s;
  for (uint8_t i = 0; i < len; i++) {
    if (uid[i] < 0x10) s += "0";
    s += String(uid[i], HEX);
    if (i < len - 1) s += ":";
  }
  s.toUpperCase();
  return s;
}

void sendReply(const String &requestId, JsonDocument &extra) {
  JsonDocument doc;
  doc["requestId"] = requestId;
  for (JsonPair kv : extra.as<JsonObject>()) doc[kv.key()] = kv.value();
  String out;
  serializeJson(doc, out);
  webSocket.sendTXT(out);
}

void handleCommand(const String &requestId, const String &command, JsonObject params) {
  JsonDocument result;

  if (command == "ping") {
    result["status"] = "ok";
    result["type"] = "pong";

  } else if (command == "get_firmware_version") {
    uint32_t v = nfc.getFirmwareVersion();
    if (v) {
      result["status"] = "ok";
      result["chip"] = (v >> 24) & 0xFF;
      result["firmwareMajor"] = (v >> 16) & 0xFF;
      result["firmwareMinor"] = (v >> 8) & 0xFF;
    } else {
      result["status"] = "error";
      result["message"] = "PN532 not responding";
    }

  } else if (command == "read_tag") {
    uint8_t uid[7];
    uint8_t uidLength;
    uint16_t timeoutMs = params["timeoutMs"] | 3000;

    bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeoutMs);
    if (found) {
      result["status"] = "ok";
      result["uid"] = uidToHex(uid, uidLength);
      result["uidLength"] = uidLength;
    } else {
      result["status"] = "timeout";
      result["message"] = "no tag detected";
    }

  } else {
    result["status"] = "error";
    result["message"] = "unknown command: " + command;
  }

  sendReply(requestId, result);
}

void webSocketEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      Serial.println("[ws] connected to backend");
      break;

    case WStype_DISCONNECTED:
      Serial.println("[ws] disconnected, will auto-reconnect");
      break;

    case WStype_TEXT: {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, payload, length);
      if (err) {
        Serial.print("[ws] bad JSON: ");
        Serial.println(err.c_str());
        return;
      }
      String requestId = doc["requestId"] | "";
      String command = doc["command"] | "";
      JsonObject params = doc["params"];
      handleCommand(requestId, command, params);
      break;
    }

    default:
      break;
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  Wire.begin(PN532_SDA, PN532_SCL);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found — check I2C wiring / board switches");
  } else {
    Serial.println("PN532 found, ready.");
    nfc.SAMConfig(); // put the PN532 into normal reading mode
  }

  connectWiFi();

  String path = String("/esp32?deviceId=") + DEVICE_ID + "&token=" + DEVICE_TOKEN;
  if (BACKEND_USE_TLS) {
    webSocket.beginSSL(BACKEND_HOST, BACKEND_PORT, path);
  } else {
    webSocket.begin(BACKEND_HOST, BACKEND_PORT, path);
  }
  webSocket.onEvent(webSocketEvent);
  webSocket.setReconnectInterval(5000);
}

void loop() {
  webSocket.loop();
}
