/**
 * PN532 over UART (HSU) + cloud WebSocket client + local AsyncWebServer.
 *
 * This is the UART twin of esp32s3_pn532_asyncweb_i2c. The only differences
 * are the PN532 library, the wiring, and how the PN532 is initialised.
 *
 * Wiring (UART / HSU):
 *   PN532 VCC -> 3V3           PN532 TX -> ESP32-S3 GPIO18 (PN532_RX_PIN)
 *   PN532 GND -> GND           PN532 RX -> ESP32-S3 GPIO17 (PN532_TX_PIN)
 *   PN532 board switches set to HSU mode (check the silkscreen).
 *   Adjust the pins to match your board's labeled UART pins.
 *
 * Libraries (Arduino Library Manager):
 *   - "PN532" by Elechouse / Seeed Studio (PN532.h + PN532_HSU.h)
 *   - WebSockets            (Markus Sattler / Links2004)
 *   - ESPAsyncWebServer     (ESP32Async fork)
 *   - AsyncTCP              (ESP32Async fork)
 *   - ArduinoJson (v7)
 *
 * Local endpoints (replace with your ESP32's IP):
 *   GET http://<esp32-ip>/status
 *   GET http://<esp32-ip>/read
 *
 * Caution: the /read handler and the WebSocket command handler both use the
 * PN532 and both block while scanning. Don't trigger both at the same moment.
 */

#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <PN532_HSU.h>
#include <PN532.h>

#include "secrets.h"

// ---- PN532 (UART / HSU) ----------------------------------------------------
#define PN532_RX_PIN 18   // ESP32-S3 RX <- PN532 TX
#define PN532_TX_PIN 17   // ESP32-S3 TX -> PN532 RX

PN532_HSU pn532hsu(Serial1);
PN532 nfc(pn532hsu);

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

// One place that talks to the PN532, shared by WS commands and /read.
bool scanForTag(uint16_t timeoutMs, JsonDocument &out) {
  uint8_t uid[7];
  uint8_t uidLength;
  // If your Elechouse PN532 version has no timeout argument, drop the last one.
  int8_t found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, timeoutMs);
  if (found > 0) {
    out["status"] = "ok";
    out["uid"] = uidToHex(uid, uidLength);
    out["uidLength"] = uidLength;
    return true;
  }
  out["status"] = "timeout";
  out["message"] = "no tag detected";
  return false;
}

// ---- cloud command handling (same protocol as the other sketches) --------
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

  Serial1.begin(115200, SERIAL_8N1, PN532_RX_PIN, PN532_TX_PIN);
  nfc.begin();
  if (!nfc.getFirmwareVersion()) {
    Serial.println("PN532 not found — check UART wiring / board switches");
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
  // AsyncWebServer runs in the background; only the cloud WS client needs pumping.
  webSocket.loop();
}
