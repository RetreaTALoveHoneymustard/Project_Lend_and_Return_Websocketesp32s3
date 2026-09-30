#pragma once

// --- Wi-Fi ---------------------------------------------------------------
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// --- Backend (the server.js you deployed) ---------------------------------
// Host only, no scheme, e.g. "esp32-pn532-backend.onrender.com"
#define BACKEND_HOST  "your-backend-host.onrender.com"
#define BACKEND_PORT  443     // 443 for wss (Render/Railway give you TLS), 3000 for local http test
#define BACKEND_USE_TLS true  // true -> beginSSL, false -> begin (e.g. testing on your LAN)

// --- Device identity -------------------------------------------------------
#define DEVICE_ID     "esp32-01"
// Must match DEVICE_TOKEN in the backend's environment variables.
#define DEVICE_TOKEN  "change-me-device-token"
