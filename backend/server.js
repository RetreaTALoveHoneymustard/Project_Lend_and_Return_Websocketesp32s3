/**
 * ESP32-S3 / PN532 relay backend.
 *
 * - ESP32 devices connect OUTBOUND to  wss://<host>/esp32?deviceId=...&token=...
 *   and are kept open (they are behind NAT, so the backend can't dial them).
 * - Browser clients call plain HTTP:
 *     GET  /api/devices          -> which devices are currently online
 *     POST /api/command          -> { deviceId, command, params } forwarded to the device,
 *                                    response streamed back as the HTTP reply
 *     GET  /health                -> uptime check (open)
 *
 * /api/* requires the header  x-api-key: <API_KEY>  (set API_KEY as an env var).
 * If API_KEY is not configured the /api routes refuse every request.
 *
 * Deploy as-is to Render / Railway / Fly.io (git-based deploy). They set
 * process.env.PORT for you; everything else is read from env vars below.
 */

const http = require("http");
const crypto = require("crypto");
const express = require("express");
const cors = require("cors");
const { WebSocketServer } = require("ws");

// ---- config -----------------------------------------------------------
const PORT = process.env.PORT || 3000;
// Shared secret the ESP32 must present when it connects. Change this,
// and set the same value in the firmware's secrets.h.
const DEVICE_TOKEN = process.env.DEVICE_TOKEN || "change-me-device-token";
// How long the backend waits for a device to answer a command before
// giving the HTTP caller a 504.
const COMMAND_TIMEOUT_MS = Number(process.env.COMMAND_TIMEOUT_MS || 8000);
// Comma-separated list of allowed browser origins, or "*" for any.
const ALLOWED_ORIGIN = process.env.ALLOWED_ORIGIN || "*";

// Secret the browser client must send in the x-api-key header for /api/*.
// Generate one with:  openssl rand -hex 32
const API_KEY = process.env.API_KEY || "";

// ---- state --------------------------------------------------------------
/** @type {Map<string, import('ws').WebSocket>} deviceId -> live socket */
const devices = new Map();
/** @type {Map<string, {resolve: Function, reject: Function, timer: NodeJS.Timeout}>} */
const pending = new Map();

// ---- express app --------------------------------------------------------
const app = express();
app.use(express.json());
app.use(cors({ origin: ALLOWED_ORIGIN === "*" ? true : ALLOWED_ORIGIN.split(",") }));

// hash both sides so timingSafeEqual always compares equal-length buffers
const sha256 = (v) => crypto.createHash("sha256").update(String(v)).digest();

function requireApiKey(req, res, next) {
  if (!API_KEY) {
    return res.status(500).json({ error: "server has no API_KEY configured" });
  }
  const supplied = req.get("x-api-key") || "";
  if (!crypto.timingSafeEqual(sha256(supplied), sha256(API_KEY))) {
    return res.status(401).json({ error: "missing or invalid API key" });
  }
  next();
}

app.get("/health", (_req, res) => {
  res.json({ ok: true, uptimeSeconds: process.uptime(), devicesOnline: devices.size });
});

// everything under /api needs the key; /health stays open for uptime checks
app.use("/api", requireApiKey);

app.get("/api/devices", (_req, res) => {
  res.json({ devices: [...devices.keys()] });
});

app.post("/api/command", async (req, res) => {
  const { deviceId, command, params } = req.body || {};

  if (!deviceId || !command) {
    return res.status(400).json({ error: "deviceId and command are required" });
  }

  const socket = devices.get(deviceId);
  if (!socket || socket.readyState !== socket.OPEN) {
    return res.status(404).json({ error: `device '${deviceId}' is not connected` });
  }

  const requestId = crypto.randomUUID();
  const payload = JSON.stringify({ requestId, command, params: params || {} });

  try {
    const result = await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        pending.delete(requestId);
        reject(new Error("device did not respond in time"));
      }, COMMAND_TIMEOUT_MS);

      pending.set(requestId, { resolve, reject, timer });
      socket.send(payload);
    });

    res.json({ deviceId, ...result });
  } catch (err) {
    res.status(504).json({ error: err.message });
  }
});

// ---- http + websocket server --------------------------------------------
const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: "/esp32" });

wss.on("connection", (socket, req) => {
  const url = new URL(req.url, "http://localhost");
  const deviceId = url.searchParams.get("deviceId");
  const token = url.searchParams.get("token");

  if (!deviceId || token !== DEVICE_TOKEN) {
    socket.close(4001, "unauthorized");
    return;
  }

  // A second device with the same id replaces the stale connection.
  const stale = devices.get(deviceId);
  if (stale && stale !== socket) stale.close(4000, "replaced by new connection");

  devices.set(deviceId, socket);
  console.log(`[ws] device connected: ${deviceId}`);

  socket.on("message", (raw) => {
    let msg;
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      return; // ignore malformed frames
    }

    // Device replies carry the same requestId the backend sent out.
    if (msg.requestId && pending.has(msg.requestId)) {
      const { resolve, timer } = pending.get(msg.requestId);
      clearTimeout(timer);
      pending.delete(msg.requestId);
      const { requestId, ...rest } = msg;
      resolve(rest);
    }
  });

  socket.on("close", () => {
    if (devices.get(deviceId) === socket) {
      devices.delete(deviceId);
      console.log(`[ws] device disconnected: ${deviceId}`);
    }
  });

  socket.on("error", (err) => console.error(`[ws] error from ${deviceId}:`, err.message));
});

if (!API_KEY) {
  console.warn("[warn] API_KEY is not set — every /api request will be refused until you set it");
}

server.listen(PORT, () => {
  console.log(`HTTP + WebSocket relay listening on :${PORT}`);
});
