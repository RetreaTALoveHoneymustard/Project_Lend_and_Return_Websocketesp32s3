# Smart Lending Console

A web page for browsing the Smart Lending database: **loans, students, items and item copies**. Each table opens in its own popup window with stat cards, search, filters and paging.

The same backend also relays commands to an ESP32-S3 + PN532 NFC reader over WebSocket. This guide covers the console only.

## How it works

```
Browser  ──►  Backend (Node, port 3000)  ──►  Smart Lending API (FastAPI on the Raspberry Pi)
 page            login + read-only proxy            the database
```

- The **backend** serves the web page, asks for a login, and passes read-only requests to the Pi.
- The **page** never needs a URL or API key. It talks to the server that served it.
- Only `GET` requests for `loans`, `students`, `items` and `item-copies` are allowed through, so the console cannot change any data.

## What you need

| Item | Notes |
|---|---|
| Node.js 18 or newer | Download the LTS version from nodejs.org. Check with `node -v` |
| Network access to the Pi | The address `https://rpi5.tailb8495f.ts.net` must open from the computer running the backend |
| This project folder | Contains `backend/` and `client/` |

## Folder layout

```
Project_Lend_and_Return_Websocketesp32s3/
├── backend/
│   ├── server.js          login, proxy, serves the page
│   └── package.json
├── client/
│   └── index.html         the console page
└── esp32-firmware/        ESP32 code (not needed for the console)
```

`client/` must sit next to `backend/`, because the backend serves `client/index.html`.

## First-time setup

Open PowerShell in the `backend` folder and install the packages once:

```powershell
cd "<your project folder>\backend"
npm install
```

Check that `node -v` prints a version. If `npm` is not recognized, install Node.js and open a new PowerShell window.

## How to open it

Do this every time you want to use the console.

**1. Start the backend.** In PowerShell, inside the `backend` folder:

```powershell
$env:SITE_USER = "shop"
$env:SITE_PASSWORD = "choose-a-strong-password"
$env:API_KEY = "another-long-secret"
npm start
```

You should see:

```
HTTP + WebSocket relay listening on :3000
```

Leave this window open. Closing it stops the console.

**2. Open the page.** In your browser, go to:

```
http://localhost:3000
```

**3. Sign in.** The browser shows a login box. Enter the `SITE_USER` and `SITE_PASSWORD` you set above. The browser remembers them after the first time.

**4. Use it.** Click a tile (Loans, Students, Items or Item copies) to open its window.

**To stop:** press `Ctrl + C` in the PowerShell window.

> The variables set with `$env:` only last for that PowerShell window. Set them again each time you open a new window, or use the start script below.

### Optional: start script

Save this as `start.ps1` in the project folder, then run it with `.\start.ps1`:

```powershell
Start-Process powershell -ArgumentList '-NoExit','-Command','cd backend; $env:SITE_USER="shop"; $env:SITE_PASSWORD="choose-a-strong-password"; $env:API_KEY="another-long-secret"; npm start'
Start-Sleep -Seconds 3
Start-Process "http://localhost:3000"
```

If PowerShell blocks the script, run `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned` once.

## Using the console

**Home page**
- Four tiles show the number of rows in each table. Click one to open it.
- The pill at the top right shows **database online** or **database offline**.
- **Refresh data** reloads all four tables.

**Popup window**
- Tabs at the top switch between the four tables without closing the window.
- Stat cards summarise the table, such as total loans, borrowed today, or copies available.
- **Search** looks in every column. The dropdown filters by status or type. Loans also have a date range.
- The table shows 10 rows per page. Use the buttons at the bottom to move between pages.
- **↻** reloads the current table. **×**, the `Esc` key, or a click outside the window closes it.

The `password` column is never displayed, even if the database returns it.

## Settings

Set these as environment variables before `npm start`.

| Variable | Default | What it does |
|---|---|---|
| `SITE_USER` | `admin` | Username for the browser login |
| `SITE_PASSWORD` | *(empty)* | Password for the browser login. **Required**: with no password, nobody can sign in |
| `API_KEY` | *(empty)* | Secret for tools such as `curl` and for the ESP32 command routes |
| `LENDING_API` | `https://rpi5.tailb8495f.ts.net` | Address of the Smart Lending API |
| `PORT` | `3000` | Port the backend listens on |
| `DEVICE_TOKEN` | `change-me-device-token` | Secret the ESP32 uses to connect. Must match `secrets.h` |
| `ALLOWED_ORIGIN` | `*` | Websites allowed to call the backend from a browser |

## Testing from the command line

With the backend running:

```powershell
curl.exe http://localhost:3000/health
curl.exe -u shop:choose-a-strong-password http://localhost:3000/api/db/loans
```

The first should return `{"ok":true,...}`. The second should return the loans as JSON.

## Troubleshooting

| What you see | Likely cause and fix |
|---|---|
| `npm is not recognized` | Node.js is not installed, or PowerShell was opened before installing it. Install it, then open a new window |
| `EADDRINUSE ... port 3000` | An old backend is still running. Close its window, or run `Stop-Process -Id (Get-NetTCPConnection -LocalPort 3000 -State Listen).OwningProcess -Force` |
| `Cannot GET /` | `client/` is not next to `backend/`, or `index.html` is missing |
| Login box keeps coming back | Wrong username or password, or `SITE_PASSWORD` was not set in that PowerShell window |
| Page says "Failed to fetch" | You opened the file directly or from another port. Use `http://localhost:3000` and keep the backend window open |
| Tile shows `failed: path not allowed` | The `/api/db/*` route in `server.js` uses the wrong variable. It must use `tablePath`, not `path` |
| Tile shows `failed: fetch failed (ECONNRESET)` | The backend cannot reach the Pi. Check that `https://rpi5.tailb8495f.ts.net/loans` opens in a browser on the same computer, and that the Pi and Tailscale are running |
| Tile shows `failed: not logged in` | Reload `http://localhost:3000` and sign in again |
| Status pill says **database offline** | Same as the `ECONNRESET` row: the Pi cannot be reached from this computer |

## Letting other people use it

| Setup | How |
|---|---|
| **Same Wi-Fi or office network** | Run the backend on a computer that stays on. Others open `http://<its-ip-address>:3000`. Find the address with `ipconfig`, and allow port 3000 in Windows Firewall |
| **On the Raspberry Pi itself** (best) | Put the project on the Pi and set `LENDING_API=http://localhost:8000`. No tunnel is needed between the backend and the database |
| **Over the internet** | Use HTTPS only, because the browser login is not encrypted over plain `http`. Tailscale Funnel or a hosting service provides HTTPS |

A hosting service such as Render cannot reach the Pi's `ts.net` address unless the Pi is made public with Tailscale Funnel. Without that, run the backend on a computer or Pi that can reach it.

## Security notes

- Use a strong `SITE_PASSWORD` and give it only to people who may see student data.
- Never commit passwords, API keys or `secrets.h` to a public repository. Change any key that has been shared in a chat or screenshot.
- The Smart Lending API has no login of its own. Do not expose it to the internet without adding an API-key check.
- The API currently stores and returns student passwords in plain text. Hash them, and remove the `password` field from the `/students` response.

## Changing the console

All changes are in `client/index.html`.

- **Windows:** the `TABLES` list near the top of the script defines each window's title, description, stat cards and filters. Add, remove or edit entries there.
- **Rows per page:** change `PAGE_SIZE`.
- **Hidden columns:** add names to `HIDDEN_COLUMNS`.
- **Colors:** the variables at the top of the CSS (`--accent` for the orange, `--bg` for the cream background).
- **Text size:** the `font` line on `body`, and the sizes in the popup styles.

To allow another table through the backend, add its name to the `ALLOWED` pattern in `server.js`.
