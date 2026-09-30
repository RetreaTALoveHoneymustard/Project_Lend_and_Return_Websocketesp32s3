# ESP32-S3 + PN532 + WebSocket backend

See `architecture.md` for the diagram and protocol. This file is the setup checklist.

```
esp32-pn532-project/
├── architecture.md
├── backend/                         # deploy this folder to Render/Railway/Fly.io
│   ├── server.js
│   ├── package.json
│   └── .env.example
├── client/                          # deploy this folder to Netlify/Vercel/GitHub Pages
│   └── index.html
└── esp32-firmware/                  # flash one of these to the ESP32-S3
    ├── esp32s3_pn532_i2c_websocket/     # PN532 over I2C (recommended default)
    ├── esp32s3_pn532_uart_websocket/    # PN532 over UART/HSU
    └── esp32s3_pn532_asyncweb_i2c/      # I2C + optional local ESPAsyncWebServer
```

## 1. Backend — git deploy to Render (or Railway/Fly.io, same idea)

1. `git init` inside `backend/`, commit, push to a new GitHub repo.
2. On Render: **New → Web Service** → connect the repo.
   - Build command: `npm install`
   - Start command: `npm start`
   - Add environment variable `DEVICE_TOKEN` = a secret string of your choosing.
   - Leave `PORT` alone — Render sets it for you.
3. Deploy. You'll get a URL like `https://esp32-pn532-backend.onrender.com`.
   - Your WebSocket endpoint is `wss://esp32-pn532-backend.onrender.com/esp32`.
   - Your HTTP API base is `https://esp32-pn532-backend.onrender.com`.
4. Sanity check: `curl https://esp32-pn532-backend.onrender.com/health`.

Local dev: `cp .env.example .env`, edit it, `npm install`, `npm start` → serves on `http://localhost:3000`.

## 2. ESP32-S3 firmware

1. Pick one folder under `esp32-firmware/` (start with `esp32s3_pn532_i2c_websocket` unless you specifically need UART).
2. In Arduino IDE, install the libraries listed at the top of the `.ino` file (Library Manager).
3. Open `secrets.h` in that same folder and fill in:
   - `WIFI_SSID` / `WIFI_PASSWORD`
   - `BACKEND_HOST` = your Render host, no scheme (e.g. `esp32-pn532-backend.onrender.com`)
   - `BACKEND_PORT` = `443` (Render/Railway/Fly all give you TLS on 443)
   - `DEVICE_TOKEN` = the exact same string you set as `DEVICE_TOKEN` on the backend
4. Wire the PN532 per the wiring comment at the top of the `.ino` file, select your board, flash it.
5. Open Serial Monitor at 115200 baud — you should see `PN532 found, ready.` then `[ws] connected to backend`.

## 3. Client — git deploy to Netlify/Vercel/GitHub Pages

1. `git init` inside `client/`, commit, push to a new repo.
2. Netlify/Vercel: **New site from Git** → connect the repo → no build command, publish directory `/`.
3. Open the deployed URL, set:
   - **backend url** → your Render URL from step 1
   - **device id** → `esp32-01` (or whatever `DEVICE_ID` you set in `secrets.h`)
4. Click **check devices online**, then try **ping** / **get firmware version** / **read tag**.

## Command protocol (for reference)

Browser → backend (`POST /api/command`):
```json
{ "deviceId": "esp32-01", "command": "read_tag", "params": { "timeoutMs": 3000 } }
```
Backend → browser (same call's response):
```json
{ "deviceId": "esp32-01", "status": "ok", "uid": "04:A2:24:5B:9C:6A:80", "uidLength": 7 }
```
Add new commands by adding a branch in the firmware's `handleCommand()` — no backend changes needed, it just relays whatever `command`/`params` you send.

## Troubleshooting

- **`device is not connected` (404)** — the ESP32 hasn't opened its WebSocket yet, or `DEVICE_TOKEN`/`DEVICE_ID` don't match between firmware and backend env var.
- **PN532 not found** at boot — double-check the mode switches on the PN532 board match the sketch (I2C vs HSU), and that VCC is 3.3V not 5V on 3.3V-only ESP32-S3 boards.
- **CORS errors in the browser console** — set `ALLOWED_ORIGIN` on the backend to your client's deployed URL instead of leaving it as `*` once you've confirmed it works.
