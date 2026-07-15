<h1 align="center">
	<br>
	<img src="assets/README_icon.png" width="200">
	<br>
	📚 LocalMarks
	<br>
	<br>
</h1>

Turn pipe-delimited bookmark files into a searchable, categorized local web UI. A single C17 binary serves a static frontend with tag/domain filters, no network required — just a browser.

<img src="./assets/home_page.png" alt="LocalMarks home page" width="100%">

---

## Table of Contents

- [Quick Start](#quick-start)
- [Prerequisites](#prerequisites)
- [Building from Source](#building-from-source)
- [Creating Your Bookmark Database](#creating-your-bookmark-database)
- [Running the Viewer](#running-the-viewer)
- [Multi-Database Support](#multi-database-support)
- [Configuration Options](#configuration-options)
- [Writing `.txt` Bookmark Files](#writing-txt-bookmark-files)
- [marks2json — The Converter](#marks2json--the-converter)
- [Features](#features)
- [Keyboard Shortcuts](#keyboard-shortcuts)
- [Project Structure](#project-structure)
- [Troubleshooting](#troubleshooting)
- [License](#license)

---

## Quick Start

```bash
# 1. Build
make

# 2. Create bookmark database from your .txt files
./marks2json create *.txt -T bookmarks.json

# 3. Start the server
./local-mark bookmarks.json

# 4. Open http://localhost:8080 in your browser
```

---

## Prerequisites

| Platform   | Requirements                                                                  |
| ---------- | ----------------------------------------------------------------------------- |
| **macOS**  | Xcode Command Line Tools + `argp-standalone` (`brew install argp-standalone`) |
| **Linux**  | `clang` or `gcc` + `make` + `libc6-dev`                                       |
| **Python** | 3.8+ (for `marks2json.py`)                                                    |

> **Note**: The frontend is embedded in the binary via gzip-compressed C arrays. Opening `front_end/index.html` as `file://` will not work — browsers block `fetch()` on local files.

---

## Building from Source

```bash
# Release build (optimized, stripped)
make

# Debug build (ASan + UBSan + stack usage + debug logs)
make debug -B O_DEBUG=1

# Clean build artifacts
make clean

# Install to /usr/local/bin (or PREFIX=~/.local)
sudo make install
```

**Output**: `./local-mark` (~400 KB static binary)

### Build Flags (controlled by Makefile)

| Flag                                       | Description                     |
| ------------------------------------------ | ------------------------------- |
| `-O3`                                      | Release optimization            |
| `-g3 -DDEBUG -fsanitize=address,undefined` | Debug build                     |
| `-DLOG_SHOW_TIME_STAMP`                    | Always on — timestamps in logs  |
| `-DLOG_SHOW_SOURCE_LOCATION`               | Debug only — file:line in logs  |
| `-largp`                                   | macOS only (argp from Homebrew) |

---

## Creating Your Bookmark Database

### 1. Write `.txt` files (one per category)

```bash
# Example: free_time.txt
# ── Games ───────────────────────────────────
akinator      | https://en.akinator.com          | Guess a celebrity     | #Game
invisiblecow  | https://findtheinvisiblecow.com/ | Find the Invisible Cow | #Game

# ── Reading ──────────────────────────────────
oddee         | https://www.oddee.com/           | Random interesting stuff | #Blog #Read

# ── Misc ─────────────────────────────────────
earthcam      | https://www.earthcam.com         | Live cameras worldwide   | #Cam #Media
```

### 2. Convert to JSON

```bash
# Create new database
./marks2json create *.txt -T bookmarks.json

# Update existing (adds new, skips duplicates)
./marks2json update new_stuff.txt -T bookmarks.json

# Force refresh existing entries
./marks2json update tools.txt refs.txt -T bookmarks.json --override

# Fetch YouTube channel icons (optional)
./marks2json create *.txt -T bookmarks.json --icon
```

### 3. Line format (pipe-delimited)

```
title | url | description | tags
```

| Field         | Required | Notes                                   |
| ------------- | -------- | --------------------------------------- |
| `title`       | no       | Display name; falls back to URL         |
| `url`         | **yes**  | Must contain `http://` or `https://`    |
| `description` | no       | Short note                              |
| `tags`        | no       | Space-separated, each prefixed with `#` |

### Rules enforced by `marks2json`

- Lines without `http://` or `https://` → skipped
- Lines with >3 pipes (>4 columns) → skipped
- Lines starting with `#` → treated as comments
- Empty lines → skipped
- Filename → Category name (`learning_python.txt` → "Learning Python")

### JSON Output Schema

```json
{
  "book_Marks": [
    {
      "category": "Free Time",
      "bookmarks": [
        {
          "title": "akinator",
          "url": "https://en.akinator.com",
          "description": "Guess a celebrity",
          "tags": ["#Game"],
          "domain": "en.akinator.com",
          "icon": "https://..."   // YouTube channels with --icon
        }
      ]
    }
  ],
  "book_mark_domain_hash": { "en.akinator.com": 1 },
  "book_mark_tag_hash":    { "#Game": 4, "#Dev": 12 }
}
```

---

## Running the Viewer

### Single Database

```bash
./local-mark bookmarks.json
# → http://localhost:8080
```

### Multiple Databases

```bash
./local-mark work.json personal.json learning.json
# Serves all three; switch via header dropdown
```

### Common Options

| Option         | Short | Default         | Description                                 |
| -------------- | ----- | --------------- | ------------------------------------------- |
| `FILE...`      | —     | **required**    | Bookmark JSON file(s) (max 10)              |
| `--port`       | `-P`  | `8080`          | TCP port                                    |
| `--host`       | `-H`  | `localhost`     | Bind address (`0.0.0.0` for all interfaces) |
| `--user`       | `-u`  | —               | Basic auth username                         |
| `--pass`       | `-p`  | —               | Basic auth password                         |
| `--max-conns`  | `-M`  | `0` (unlimited) | Max concurrent connections per IP           |
| `--browser`    | `-B`  | —               | Open browser on startup                     |
| `--log-level`  | `-L`  | `info`          | `error`, `warn`, `info`, `debug`            |
| `--log-file`   | `-F`  | stderr          | Append logs to file                         |
| `--threads`    | `-T`  | `2`             | Worker thread pool size                     |
| `--keep-alive` | `-K`  | `3`             | Keep-alive timeout (seconds, 0 = disable)   |

### Examples

```bash
# Public access with auth
./local-mark -u admin -p secret -H 0.0.0.0 bookmarks.json

# Custom port, more threads, open browser
./local-mark -P 3000 -T 4 -B bookmarks.json

# Rate limited, debug logs to file
./local-mark -M 10 -L debug -F server.log bookmarks.json
```

---

## Multi-Database Support

When you pass multiple `.json` files:

```
./local-mark db1.json db2.json db3.json
```

### API Endpoints

| Endpoint                      | Description                              |
| ----------------------------- | ---------------------------------------- |
| `GET /bookmarks.json`         | First database (backward compat)         |
| `GET /bookmarks/0.json`       | Database at index 0                      |
| `GET /bookmarks/1.json`       | Database at index 1                      |
| `GET /api/databases`          | List all databases with metadata         |
| `GET /api/databases/0`        | Metadata for database 0                  |

### Request Logging

Enable detailed request logging with these flags:

| Flag | Short | Description |
|------|-------|-------------|
| `--print-request` | `-R` | Log each request with method, path, headers |
| `--log-level=debug` | `-L debug` | Include request/response details |

```bash
# Log all requests to console
./local-mark -R bookmarks.json

# Log requests to file with timestamps
./local-mark -R -L debug -F access.log bookmarks.json
```

Example log output:
```
[INFO ] 127.0.0.1:54321 "GET /bookmarks.json HTTP/1.1" 200 - (2410 bytes, application/json)
[DEBUG] 127.0.0.1:54321 "GET /api/databases HTTP/1.1" 200 - (512 bytes, application/json)
[DEBUG] --- Request from 127.0.0.1:54321 ---
GET /bookmarks.json HTTP/1.1
Host: localhost:8080
User-Agent: curl/7.68.0
Accept: */*
---
```

> **Note**: Use `-R` sparingly in production — it logs every request including headers.

### Frontend Database Selector

1. **Header indicator** — shows current database name (🛢️ icon)
2. **Click indicator** or navigate to `#databases` — full selector page
3. **Database cards** show:
   - File name
   - Last modified (relative + absolute time)
   - Permissions + owner:group
   - "Current" badge on active database
4. **Click a card** → saves to `localStorage`, reloads page with new data
5. **Persistence** — your choice survives browser restarts

### Metadata Returned by `/api/databases`

```json
{
  "databases": [
    {
      "mode": "0644",
      "absolute_path": "/Users/you/work.json",
      "file_name": "work.json",
      "file_size": 2410,
      "cTime": 1700000000,
      "bTime": 1690000000,
      "user": "pritam",
      "group": "staff",
      "mTime_sec": 1700000000,
      "mTime_nsec": 123456789
    }
  ],
  "count": 1
}
```

> **Why `user`/`group` names?** Human-readable output from `getpwuid()`/`getgrgid()` instead of raw UID/GID.

---

## Configuration Options

### CLI Flags (Complete Reference)

```text
Usage: local-mark [OPTION...] <DB_FILE(s)>...

Logging:
  -L, --log-level=LEVEL     Log level: error|warn|info|debug (default: info)
  -F, --log-file=FILE       Append logs to FILE (default: stderr)
  -R, --print-request       Log each client request and headers

Authentication:
  -u, --user=USER           Enable Basic Auth with username
  -p, --pass=PASS           Enable Basic Auth with password

Connection:
  -H, --host=HOST           Listener host/IP (default: localhost)
  -P, --port=PORT           TCP port (default: 8080)
  -T, --threads=NUM         Thread pool size (default: 2)
  -K, --keep-alive=SECS     Keep-alive timeout (default: 3, 0 = disable)
  -M, --max-conns=NUM       Max concurrent conns per IP (0 = unlimited)

Browser:
  -B, --browser=BROWSER     Open browser on startup (e.g. firefox, chrome)

Help:
  -?, --help                Show this help
  --usage                   Show brief usage
  -V, --version             Print program version
```

### Environment Variables

None — all config via CLI flags for explicit, reproducible runs.

---

## Writing `.txt` Bookmark Files

### File → Category Mapping

```
work_tools.txt      → "Work Tools"
learning_rust.txt   → "Learning Rust"
my-links.txt        → "My Links"
```

### Complete Example

```txt
# free_time.txt
# ── Games ───────────────────────────────────
akinator      | https://en.akinator.com          | Guess a celebrity     | #Game #Web
invisiblecow  | https://findtheinvisiblecow.com/ | Find the Invisible Cow | #Game #Fun

# ── Reading ──────────────────────────────────
mdn           | https://developer.mozilla.org    | Web platform docs     | #Dev #Web #Reference
python_docs   | https://docs.python.org          | Python reference      | #Dev #Python

# ── YouTube ──────────────────────────────────
tsoding       | https://www.youtube.com/@tsoding | Live coding streams   | #YouTube #C #Rust
primagen      | https://www.youtube.com/@ThePrimeagen | Vim & productivity | #YouTube #Vim

# ── Misc ─────────────────────────────────────
earthcam      | https://www.earthcam.com         | Live cameras worldwide | #Cam #Media
oddee         | https://www.oddee.com/           | Random interesting stuff | #Blog #Read
```

### Tips

- Use comments (`# ...`) to organize sections within a file
- Keep descriptions concise — they appear on cards
- Tags enable filtering; use consistent prefixes (`#Dev`, `#Read`, `#Game`)
- Duplicate URLs within a category are deduplicated automatically

---

## marks2json — The Converter

```bash
# Create fresh database
marks2json create *.txt -T bookmarks.json

# Append new files (skips existing URLs)
marks2json update new_category.txt -T bookmarks.json

# Force refresh existing entries
marks2json update tools.txt refs.txt -T bookmarks.json --override

# Fetch YouTube channel avatars as icons
marks2json create *.txt -T bookmarks.json --icon
```

### All Options

| Flag                | Description                               |
| ------------------- | ----------------------------------------- |
| `-T, --target FILE` | Output JSON file (required)               |
| `-O, --override`    | Update existing entries (update only)     |
| `-I, --icon`        | Fetch YouTube channel icons (create only) |
| `-h, --help`        | Show help                                 |

---

## Features

### Browse View (`#browse`)

<img src="./assets/home_page.png" alt="Browse view" width="100%">

- **Sidebar**: Categories with counts; **Favorites** (★) appears at top when starred
- **Search**: Press `/` — searches title, description, tags, URL
- **Tag pills**: Click to filter; multi-select supported
- **Click tag on card** → instantly adds to filter
- **Collapsible tag bar** when >30 tags (shows active count when folded)
- **Layout toggle** (header): Single / Grid / Compact — persisted
- **Sidebar resize**: Drag handle (160–480px), double-click to reset — persisted
- **Favicons**: Google favicon service + YouTube thumbnail fallback
- **Keyboard nav**: `j/k` or `↑/↓`, `h/l` or `←/→`, `gg`/`G`, `Enter`/`o`, `yy`, `p`

### Info View (`#info`)

<img src="./assets/info_page.png" alt="Info view" width="100%">

- **Stats strip**: Total, unique URLs, categories, domains, tags
- **Category bar chart** (proportional)
- **Tag cloud** sorted by frequency (collapses >35 tags)
- **Domain grid** with favicon + count → click to filter browse view
- **Link health check**: Async HEAD requests with progress bar; categorizes 2xx/3xx/4xx/5xx/network errors; cancellable

### Random View (`#random`)

<img src="./assets/random_page.png" alt="Random view" width="100%">

- Pick N random links with optional category/tag filters
- "Open All" with 150ms staggered delays
- Shows match pool size

### Theme System

- **Dark** (default, `style.css`)
- **Light** (`stylesheet/themes/light.css`) — auto via `prefers-color-scheme: light`
- **Manual toggle** (☀️/🌙 in header) → persists to `localStorage` (`localmarks-theme`)

### Persistence (localStorage)

| Key                    | Purpose                         |
| ---------------------- | ------------------------------- |
| `localmarks-favorites` | Starred bookmark URLs           |
| `localmarks-layout`    | `single` \| `grid` \| `compact` |
| `localmarks-sidebar-w` | Sidebar width (px)              |
| `localmarks-theme`     | `dark` \| `light`               |
| `localmarks-active-db` | Active database index           |

### IndexedDB Cache

- Database: `LocalMarksCache` / `bookmarks` store
- Per-database keys: `bookmarks:0`, `bookmarks:1`...
- Stale cache returns immediately; fresh fetch in background
- Force reload: `indexedDB.deleteDatabase('LocalMarksCache')` in DevTools

---

## Keyboard Shortcuts (Browse View Only)

| Key             | Action                       |
| --------------- | ---------------------------- |
| `j` / `↓`       | Next bookmark                |
| `k` / `↑`       | Previous bookmark            |
| `h` / `←`       | Back to categories (sidebar) |
| `l` / `→`       | Into bookmarks (cards)       |
| `gg`            | Jump to first                |
| `G` (`Shift+G`) | Jump to last                 |
| `/`             | Focus search                 |
| `Enter`         | Open in new tab              |
| `o`             | Open in same tab             |
| `yy`            | Copy URL (domain toast)      |
| `p`             | Toggle pin/favorite          |
| `Esc`           | Clear search / close help    |
| `?`             | Toggle help modal            |
| `Ctrl/Cmd+K`    | Focus search                 |

---

## Project Structure

```
local_marks/
├── Makefile                    # Build system
├── marks2json.py               # .txt → JSON converter
│
├── front_end/                  # ── Embedded SPA ────────────────────
│   ├── embed_frontend.bash     # Build: gzip + xxd -i per file
│   ├── favicon.ico
│   ├── index.html              # SPA shell (hash routing)
│   ├── sw.js                   # Service Worker (offline)
│   │
│   ├── javascript/             # ES Modules
│   │   ├── browse.js           # Browse view
│   │   ├── data.js             # Shared: fetch, IndexedDB, favorites, theme
│   │   ├── databases.js        # Database selector page (cards, metadata)
│   │   ├── info.js             # Info view (stats, charts, health check)
│   │   ├── keyboard.js         # Vim-style shortcuts
│   │   ├── main.js             # Entry: router, init, DB indicator
│   │   ├── panel.js            # Bookmark panel rendering
│   │   ├── random.js           # Random picker
│   │   ├── search.js           # Search logic
│   │   ├── sidebar.js          # Category sidebar
│   │   └── tag_bar.js          # Tag filter pills
│   │
│   └── stylesheet/
│       ├── style.css           # Main (dark theme, CSS vars)
│       └── themes/light.css    # Light theme overrides
│
├── src/                        # ── C Source (flat .c/.h pairs) ─────
│   ├── api.c/.h                # /bookmarks*, /api/databases*
│   ├── auth.c/.h               # Basic Auth
│   ├── bookmark_cache.c/.h     # Multi-DB JSON cache (mtime)
│   ├── common.h                # MAX_BOOKMARK_FILES = 10
│   ├── databases_meta.c/.h     # stat(), realpath(), user/group names
│   ├── file.c/.h               # VFS file serving
│   ├── gen_embedded_front_end_dir.h  # Auto-gen: vfs_entry[]
│   ├── header_cache.c/.h       # Pre-computed Date/Server/Connection
│   ├── http.c/.h               # HTTP parser (buffered, in-place)
│   ├── log.c/.h                # Lock-free SPSC ring logger
│   ├── main.c                  # CLI (argp), startup
│   ├── mime.c/.h               # Extension → MIME
│   ├── project_config.h        # VERSION, BINARY_NAME
│   ├── ratelimit.c/.h          # Per-IP limit (1024-slot hash)
│   ├── response.c/.h           # Response builders
│   ├── server.c/.h             # Accept loop, thread pool, keep-alive
│   ├── thread_pool.c/.h        # Fixed pool (4096 queue, mutex+condvars)
│   ├── transport.c/.h          # Opaque fd wrapper, writev, timeouts
│   └── vfs_hash.c/.h           # FNV-1a + linear probe (O(1) lookup)
│
└── third_party/
    └── eduardsui_tlse-v1.0.7/  # TLS lib (not linked in current build)
```

---

## Troubleshooting

### `argp.h` not found (macOS)

```bash
brew install argp-standalone
# Makefile links -largp automatically
```

### Port already in use

```bash
./local-mark -P 3000 bookmarks.json
# or
lsof -ti:8080 | xargs kill -9
```

### Database not loading / stale data

```bash
# Clear browser IndexedDB
# In DevTools Console:
indexedDB.deleteDatabase('LocalMarksCache')

# Or force reload via CLI
./local-mark -L debug bookmarks.json
```

### Rate limit hitting

```bash
# Increase or disable
./local-mark -M 50 bookmarks.json
# or
./local-mark -M 0 bookmarks.json
```

### File permission errors

```bash
# Ensure JSON files are readable
chmod 644 *.json
```

### Build warnings

- `implicit conversion changes signedness` — harmless, from `snprintf` return
- `unused variable` — in debug paths, no runtime impact

---

## License

[MIT](LICENSE) — Copyright (c) 2024 Pritam

---

## See Also

- [PROJECT_BRIEF.md](PROJECT_BRIEF.md) — Architecture, module guide, mental model
- [DEV.md](DEV.md) — Development notes (from live_server reference)
- [marks2json.py](marks2json.py) — Converter source
- [AGENTS.md](AGENTS.md) — Agent instructions for this repo
