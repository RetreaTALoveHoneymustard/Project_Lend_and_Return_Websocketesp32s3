/**
 * OPTIONAL variant — same cloud relay as esp32s3_pn532_i2c_websocket, PLUS a
 * local AsyncWebServer for LAN debugging (curl the ESP32 directly on your
 * network without going through the backend at all).
 *
 * Why both: the cloud backend can't reach the ESP32 directly (it's behind
 * NAT), so the outbound WebSocketsClient connection is still required for
 * remote/internet control. AsyncWebServer here is a *local* convenience —
 * useful while developing on your bench, or for a LAN-only mode.
 *
 * Wiring: identical to esp32s3_pn532_i2c_websocket (I2C — see that file's
 * header comment for pins and PN532 switch settings).
 *
 * Libraries (Arduino Library Manager):
 *   - Adafruit PN532 (+ Adafruit BusIO)
 *   - WebSockets            (Markus Sattler / Links2004) — cloud client
 *   - ESPAsyncWebServer     (use the "ESP32Async/ESPAsyncWebServer" fork —
 *                             it's the one kept up to date for ESP32-S3 and
 *                             the newer Arduino-ESP32 core 3.x)
 *   - AsyncTCP              (use "ESP32Async/AsyncTCP", same reason)
 *   - ArduinoJson (v7)
 *
 * Local endpoints once flashed (replace with your ESP32's IP):
 *   GET http://<esp32-ip>/status   -> { deviceId, wsConnected, uptimeMs }
 *   GET http://<esp32-ip>/read     -> scans for a tag, returns its UID
 */

#include <Wire.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Adafruit_PN532.h>

#include "secrets.h"

// ---- PN532 (I2C) ----------------------------------------------------------
#define PN532_SDA 8
#define PN532_SCL 9
#define PN532_IRQ   -1
#define PN532_RESET -1

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// ---- cloud WebSocket client + local async server -------------------------
WebSocketsClient webSocket;
AsyncWebServer localServer(80);

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

// Shared by both the WS command handler and the local /read endpoint so
// there's only one place that actually talks to the PN532.
bool scanForTag(uint16_t timeoutMs, JsonDocument &out) {
  uint8_t uid[7];
  uint8_t uidLength;
  bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeoutMs);
  if (found) {
    out["status"] = "ok";
    out["uid"] = uidToHex(uid, uidLength);
    out["uidLength"] = uidLength;
  } else {
    out["status"] = "timeout";
    out["message"] = "no tag detected";
  }
  return found;
}

// ---- cloud command handling (same protocol as the base I2C sketch) -------
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
    uint16_t timeoutMs = params["timeoutMs"] | 3000;
    scanForTag(timeoutMs, result);
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
      if (deserializeJson(doc, payload, length)) return;
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

// ---- local AsyncWebServer routes ------------------------------------------
void setupLocalServer() {
  localServer.on("/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    doc["deviceId"] = DEVICE_ID;
    doc["wsConnected"] = webSocket.isConnected();
    doc["uptimeMs"] = millis();
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  localServer.on("/read", HTTP_GET, [](AsyncWebServerRequest *request) {
    JsonDocument doc;
    scanForTag(3000, doc);
    String out;
    serializeJson(doc, out);
    request->send(200, "application/json", out);
  });

  localServer.begin();
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
  if (!nfc.getFirmwareVersion()) {
    Serial.println("PN532 not found — check I2C wiring / board switches");
  } else {
    Serial.println("PN532 found, ready.");
    nfc.SAMConfig();
  }

  connectWiFi();
  setupLocalServer();

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
  // AsyncWebServer runs in the background on its own — nothing needed here
  // for it. The cloud WS client still needs its loop() pumped.
  webSocket.loop();
}
