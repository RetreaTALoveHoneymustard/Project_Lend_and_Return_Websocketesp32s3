<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>Admin Console</title>

<style>
  :root {
    --bg: #14110d;
    --panel: #1c1712;
    --line: #3a3126;
    --text: #e9dfc8;
    --dim: #8f8368;
    --amber: #e8a33d;
    --ok: #7fb56a;
    --err: #c9634a;
  }
  * { box-sizing: border-box; }
  html, body {
    margin: 0;
    background: var(--bg);
    color: var(--text);
    font-family: "JetBrains Mono", "SFMono-Regular", Consolas, Menlo, monospace;
    font-size: 14px;
  }
  body { padding: 24px; max-width: 720px; margin: 0 auto; }

  .title { font-size: 15px; color: var(--amber); margin: 0 0 2px; }
  .subtitle { color: var(--dim); margin: 0 0 20px; font-size: 12px; }

  fieldset { border: 1px solid var(--line); padding: 14px; margin: 0 0 16px; }
  legend { color: var(--dim); padding: 0 6px; font-size: 12px; }

  label { display: block; color: var(--dim); font-size: 12px; margin: 8px 0 4px; }
  label:first-child { margin-top: 0; }
  input {
    width: 100%;
    background: var(--panel);
    border: 1px solid var(--line);
    color: var(--text);
    padding: 7px 8px;
    font: inherit;
  }
  input:focus, button:focus-visible { outline: 1px solid var(--amber); }

  .row { display: flex; gap: 8px; margin-top: 14px; flex-wrap: wrap; }
  button {
    background: var(--panel);
    border: 1px solid var(--line);
    color: var(--text);
    padding: 8px 14px;
    font: inherit;
    cursor: pointer;
  }
  button:hover { border-color: var(--amber); color: var(--amber); }
  button:disabled { opacity: 0.4; cursor: default; }
  button:disabled:hover { border-color: var(--line); color: var(--text); }

  .status-line { color: var(--dim); font-size: 12px; margin-top: 10px; }
  .status-line .on { color: var(--ok); }
  .status-line .off { color: var(--err); }

  .log {
    border: 1px solid var(--line);
    height: 320px;
    overflow-y: auto;
    padding: 10px 12px;
    background: #100d09;
  }
  .entry { margin-bottom: 10px; white-space: pre-wrap; word-break: break-word; }
  .entry .meta { color: var(--dim); font-size: 11px; }
  .entry .ok { color: var(--ok); }
  .entry .err { color: var(--err); }
  .empty { color: var(--dim); }
</style>
</head>
<body>

  <p class="title">Admin console pn532</p>
  <p class="subtitle">sends commands over HTTP to the backend, which relays them to the ESP32-S3 over WebSocket</p>

  <!-- connection -->
  <fieldset>
    <legend>connection</legend>
    <label for="backendUrl">backend url</label>
    <input id="backendUrl" value="https://your-backend-host.onrender.com" placeholder="eg. https://my-app.onrender.com" />

    <label for="deviceId">device id</label>
    <input id="deviceId" value="esp32-01" placeholder="eg. esp32-01" />

    <label for="apiKey">api key</label>
    <input id="apiKey" type="password" autocomplete="off" placeholder="eg. secret_api_key_123" />

    <div class="row">
      <button id="refreshBtn">check devices online</button>
    </div>
    <div class="status-line" id="statusLine">not checked yet</div>
  </fieldset>

  <!-- commands -->
  <fieldset>
    <legend>commands</legend>
    <div class="row" style="margin-top:0">
      <button data-cmd="ping">ping</button>
      <button data-cmd="get_firmware">get firmware &amp; mac</button>
      <button data-cmd="read_tag">read tag</button>
    </div>
    <div class="status-line" id="busyLine"></div>
  </fieldset>

  <!-- log -->
  <fieldset>
    <legend>log</legend>
    <div class="log" id="log"><div class="empty">nothing sent yet</div></div>
  </fieldset>

<script>
  const $ = (id) => document.getElementById(id);
  const logEl = $("log");

  // ---- remember connection fields between visits ----
  function loadSaved() {
    try {
      for (const k of ["backendUrl", "deviceId", "apiKey"]) {
        const v = localStorage.getItem("pn532." + k);
        if (v) $(k).value = v;
      }
    } catch (_) {}
  }
  function saveField(k) {
    try { localStorage.setItem("pn532." + k, $(k).value); } catch (_) {}
  }

  function baseUrl() { return $("backendUrl").value.trim().replace(/\/$/, ""); }
  function apiHeaders(extra) {
    return { "x-api-key": $("apiKey").value.trim(), ...extra };
  }
  function timestamp() { return new Date().toLocaleTimeString(); }

  // ---- log (textContent only: device replies are untrusted) ----
  function logEntry(meta, body, cls) {
    if (logEl.querySelector(".empty")) logEl.textContent = "";
    const div = document.createElement("div");
    div.className = "entry";
    const m = document.createElement("div");
    m.className = "meta";
    m.textContent = meta;
    const b = document.createElement("div");
    b.className = cls;
    b.textContent = body;
    div.append(m, b);
    logEl.prepend(div);
  }

  function setBusy(on) {
    document.querySelectorAll("button").forEach((b) => (b.disabled = on));
    $("busyLine").textContent = on
      ? "working… if the command needs a tag, hold it on the reader now"
      : "";
  }

  // ---- backend calls ----
  async function checkDevices() {
    const line = $("statusLine");
    line.textContent = "checking…";
    try {
      const res = await fetch(`${baseUrl()}/api/devices`, { headers: apiHeaders() });
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || `HTTP ${res.status}`);
      const list = data.devices || [];
      const wanted = $("deviceId").value.trim();
      const online = list.includes(wanted);
      line.textContent = `online devices: [${list.join(", ") || "none"}] — target "${wanted}" is `;
      const state = document.createElement("span");
      state.className = online ? "on" : "off";
      state.textContent = online ? "online" : "offline";
      line.append(state);
    } catch (e) {
      line.textContent = "";
      const state = document.createElement("span");
      state.className = "off";
      state.textContent = `could not reach backend: ${e.message}`;
      line.append(state);
    }
  }

  async function sendCommand(command, params = {}) {
    const deviceId = $("deviceId").value.trim();
    const meta = `${timestamp()} — ${command} → ${deviceId}`;

    setBusy(true);
    try {
      const res = await fetch(`${baseUrl()}/api/command`, {
        method: "POST",
        headers: apiHeaders({ "Content-Type": "application/json" }),
        body: JSON.stringify({ deviceId, command, params }),
      });
      const data = await res.json();
      const ok = res.ok && data.status === "ok";
      logEntry(meta, JSON.stringify(data), ok ? "ok" : "err");
    } catch (e) {
      logEntry(meta, `request failed: ${e.message}`, "err");
    } finally {
      setBusy(false);
    }
  }

  // ---- wiring ----
  $("refreshBtn").addEventListener("click", checkDevices);
  $("backendUrl").addEventListener("input", () => saveField("backendUrl"));
  $("deviceId").addEventListener("input", () => saveField("deviceId"));
  $("apiKey").addEventListener("input", () => saveField("apiKey"));

  document.querySelectorAll("button[data-cmd]").forEach((btn) => {
    btn.addEventListener("click", () => sendCommand(btn.dataset.cmd));
  });

  loadSaved();
</script>
</body>
</html>
