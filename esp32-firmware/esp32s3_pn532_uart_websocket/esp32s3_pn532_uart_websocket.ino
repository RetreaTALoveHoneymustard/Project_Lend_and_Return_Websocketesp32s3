/**
 * ESP32-S3 <-> PN532 over UART (HSU), relayed through a cloud WebSocket backend.
 *
 * Wiring (UART / HSU):
 *   PN532 VCC -> 3V3           PN532 TX -> ESP32-S3 GPIO18 (PN532_RX_PIN below, ESP receives here)
 *   PN532 GND -> GND           PN532 RX -> ESP32-S3 GPIO17 (PN532_TX_PIN below, ESP transmits here)
 *   PN532 board switches set to HSU mode (check silkscreen — usually SEL1=on/SEL0=off,
 *   the opposite pairing from I2C mode). Adjust pins to match your board's labeled UART pins.
 *
 * Libraries (Arduino Library Manager):
 *   - "PN532" by Elechouse / Seeed Studio  (provides PN532.h + PN532_HSU.h; this is a
 *     different library from Adafruit's — Adafruit's own library does not do HSU on ESP32)
 *   - WebSockets   (by Markus Sattler / Links2004)
 *   - ArduinoJson  (v7)
 *
 * Board: "ESP32S3 Dev Module" (or your board's exact entry) in Arduino IDE.
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <PN532_HSU.h>
#include <PN532.h>

#include "secrets.h"

// ---- PN532 (UART / HSU) ----------------------------------------------------
#define PN532_RX_PIN 18   // ESP32-S3 RX  <- PN532 TX
#define PN532_TX_PIN 17   // ESP32-S3 TX  -> PN532 RX

PN532_HSU pn532hsu(Serial1);
PN532 nfc(pn532hsu);

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

    // NOTE: some versions of the Elechouse PN532 library don't accept a
    // timeout argument — if yours doesn't compile with it, drop the last
    // argument and instead wrap this call in your own millis()-based loop.
    int8_t found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeoutMs);
    if (found > 0) {
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

  Serial1.begin(115200, SERIAL_8N1, PN532_RX_PIN, PN532_TX_PIN);
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.println("PN532 not found — check UART wiring / board switches");
  } else {
    Serial.println("PN532 found, ready.");
    nfc.SAMConfig();
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
