# local-mark — Complete Codebase Reference for A new contributor

> **Purpose**: This document gives a new contributor a complete, accurate mental model of the codebase in one read. No speculation — only what exists in the repository as of the current commit.

---

## 1. Project Identity

| Attribute        | Value                                                                                                                                                                                              |
| ---------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Name**         | local-mark                                                                                                                                                                                         |
| **Language**     | C17 (strict: `-std=c17 -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wmissing-prototypes -Wshadow -Wconversion`)                                                                                   |
| **Platforms**    | Linux, macOS (also compiles on other POSIX)                                                                                                                                                        |
| **Dependencies** | Zero runtime deps. Build-time: `argp-standalone` on macOS (Homebrew), `pthread`, C library. Optional: `tlse` (TLS, via `make tls`)                                                                 |
| **Binary**       | Single executable `./local-mark` (~400 KB stripped)                                                                                                                                                |
| **License**      | MIT                                                                                                                                                                                                |
| **Philosophy**   | _Local-first bookmark browser._ Single binary serving embedded SPA + multi-database JSON API. No config files, no env vars, no external DB. All config via CLI flags. Optional TLS via `make tls`. |

**Not a framework.** Not a library. A self-contained tool for browsing bookmark databases locally.

---

## 2. High-Level Architecture

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                            main thread                                        │
│  argp CLI → log_init → vfs_hash_init → header_cache_init                      │
│       ↓                 ↓                  ↓                                  │
│  bookmark_cache_init → thread_pool_create                                     │
│       ↓                                                                       │
│  [if --tls-cert/--tls-key] tls_init → tls_create_context → tls_load_*        │
│       ↓                                                                       │
│  ratelimit_create (if --max-conns) → make_listener                            │
│       ↓                                                                       │
│  accept() loop ────────────────────────────────────────────────────────────┐  │
│       │                                                                    │  │
│       │                                                                    │  │
│  transport_new() + [if TLS] transport_set_tls()                            │  │
│  + ratelimit_accept() + thread_pool_submit()                               │  │
└─────────┼──────────────────────────────────────────────────────────────────┼──┘
          │                                                                  │
                                                                            
┌──────────────────────────┐                                  ┌─────────────────────────┐
│  thread pool (N workers) │                                  │  dedicated threads      │
│  ┌─────┐ ┌─────┐ ┌─────┐ │                                  │  ┌──────────────────┐   │
│  │ W1  │ │ W2  │ │ WN  │ │                                  │  │ log consumer     │   │
│  └─────┘ └─────┘ └─────┘ │                                  │  │ (drains ring buf)│   │
│        ▲        ▲        │                                  │  └──────────────────┘   │
│        │        │        │                                  └─────────────────────────┘
│  circular work queue     │
│  (4096 slots, mutex +    │
│   2 condvars)            │
└──────────┬───────────────┘
           │
           │ Worker request handling:
           
┌────────────────────────────────────────────────────────────────────────────────┐
│                         WORKER THREAD (per connection)                         │
│  ┌─────────────┐      ┌─────────────┐     ┌─────────────┐     ┌─────────────┐  │
│  │ http_parse  │───▶  │ auth_check? │───▶ │ api_handle  │───▶ │   DONE      │  │
│  │ _request()  │      │ (401/OK)    │     │ _request()  │     │ (if API)    │  │
│  └─────────────┘      └─────────────┘     └──────┬──────┘     └─────────────┘  │
│                                                  │                             │
│                                                   (not API)                   │
│                                        ┌─────────────────┐                     │
│                                        │  file_serve()   │                     │
│                                        │  (VFS lookup)   │                     │
│                                        │  vfs_lookup()   │                     │
│                                        │       │         │                     │
│                                        │                │                     │
│                                        │  vfs_entry*     │                     │
│                                        │  (gzip data)    │                     │
│                                        │       │         │                     │
│                                        │                │                     │
│                                        │ response_send() │                     │
│                                        │ (writev + ETag  │                     │
│                                        │  + Range +      │                     │
│                                        │  Content-       │                     │
│                                        │  Encoding)      │                     │
│                                        │                 │                     │
│                                        │ [if TLS] transport_write() chunks     │
│                                        │ plaintext 16KB, flushes per chunk     │
│                                        └─────────────────┘                     │
└────────────────────────────────────────────────────────────────────────────────┘
```

**Key invariants:**

- Main thread only accepts + dispatches. Never blocks on I/O.
- Worker threads handle full request lifecycle (parse → serve → keep-alive loop).
- Log consumer is single-threaded; workers are lock-free producers.
- VFS hash table built once at startup — O(1) file lookups.
- Embedded frontend files are gzip-compressed at build time, served from memory.
- API endpoints checked first (`/bookmarks*`, `/api/databases*`), then VFS fallback.

---

## 3. Complete Project File Structure

```
local_marks/
├── .clang-tidy                       # clang-tidy config (analyzer, readability, modernize, bugprone, etc.)
├── .editorconfig                     # EditorConfig for consistent formatting
├── .gitattributes                    # Git attributes
├── .gitignore                        # Git ignore rules
├── AGENTS.md                         # Agent instructions for this project
├── LICENSE                           # MIT License
├── Makefile                          # Build system (release, debug, install, clean, strip)
├── PROJECT_BRIEF.md                  # This document
├── README.md                         # Project overview
├── REFERENCES.md                     # External references & links
├── TODO.txt                          # Task list
├── marks2json.py                     # Python tool: create/update/find-dead bookmark DBs
├── local-mark                        # Built binary (after `make`)
│
├── front_end/                        # ── Embedded SPA Source ────────────────────────
│   ├── embed_frontend.bash           # Build script: gzip + xxd -i per file → C arrays
│   ├── favicon.ico                   # Favicon
│   ├── index.html                    # SPA entry point (hash routing)
│   │
│   ├── javascript/                   # ES Modules
│   │   ├── browse.js                 # Browse view: categories, search, tags, cards
│   │   ├── data.js                   # Shared: fetchBookmarks, IndexedDB, favorites, theme, layout
│   │   ├── databases.js              # Database selector: cards UI, switch DB, metadata display
│   │   ├── info.js                   # Info view: stats, charts, domain grid
│   │   ├── keyboard.js               # Keyboard shortcuts (?, j/k, /, etc.)
│   │   ├── main.js                   # Entry: hash router, init, DB selector
│   │   ├── panel.js                  # Bookmark panel rendering
│   │   ├── random.js                 # Random view: picker with filters
│   │   ├── search.js                 # Search logic (title, desc, tags, URL)
│   │   ├── sidebar.js                # Category sidebar rendering & events
│   │   └── tag_bar.js                # Tag filter pills UI
│   │
│   └── stylesheet/                   # CSS
│       ├── style.css                 # Main styles (dark/light via CSS vars)
│       └── themes/
│           └── light.css             # Light theme overrides
│
├── src/                              # ── C Source (flat, .c/.h pairs) ──────────────
│   ├── api.c / .h                    # API endpoints (/bookmarks*, /api/databases*)
│   ├── auth.c / .h                   # HTTP Basic Auth
│   ├── bookmark_cache.c / .h         # Multi-DB JSON cache (mtime invalidation)
│   ├── common.h                      # MAX_BOOKMARK_FILES = 10
│   ├── databases_meta.c / .h         # File metadata (stat, user/group, realpath)
│   ├── file.c / .h                   # VFS file serving (embedded frontend)
│   ├── gen_embedded_front_end_dir.h  # Auto-generated: vfs_entry[] + extern arrays
│   ├── header_cache.c / .h           # Pre-computed Date/Server/Connection headers
│   ├── http.c / .h                   # HTTP request parser (buffered, in-place)
│   ├── log.c / .h                    # Lock-free SPSC ring logger
│   ├── main.c                        # Entry: argp CLI, validation, startup
│   ├── mime.c / .h                   # Extension → MIME lookup
│   ├── project_config.h              # VERSION, BINARY_NAME, HOMEPAGE_URL
│   ├── ratelimit.c / .h              # Per-IP connection limit (1024-slot hash)
│   ├── response.c / .h               # Response builders (error, redirect, send)
│   ├── server.c / .h                 # Accept loop, thread pool dispatch, keep-alive
│   ├── thread_pool.c / .h            # Fixed-size pool (circular queue, mutex+condvars)
│   ├── transport.c / .h              # Opaque Transport (fd wrapper, writev, TLS, timeouts)
│   └── vfs_hash.c / .h               # O(1) VFS hash table (FNV-1a, linear probe)
│
└── third_party
```

---

## 4. Source Tree (src/ module summary)

```
src/
├── main.c                        # Entry point, argp parsing, ServerConfig construction
├── server.c / .h                 # Accept loop, signal handling, thread pool dispatch, keep-alive, TLS context
├── transport.c / .h              # Opaque Transport (fd wrapper, writev, TLS handshake/encrypt, timeouts)
├── http.c / .h                   # Request parser (4 KB buffered reads, HttpRequest struct)
├── response.c / .h               # High-level response builders (error, redirect, send)
├── header_cache.c / .h           # Pre-computed Date/Server/Connection headers (1 Hz update)
├── file.c / .h                   # VFS-based file serving (embedded frontend only)
├── vfs_hash.c / .h               # O(1) hash table for embedded files (FNV-1a, linear probe)
├── gen_embedded_front_end_dir.h  # Auto-generated: vfs_entry[] + extern C arrays
├── auth.c / .h                   # Basic Auth (Base64 decode, credential check)
├── thread_pool.c / .h            # Fixed-size pool, circular queue, mutex + 2 condvars
├── ratelimit.c / .h              # Per-IP connection limit (fixed 1024-slot hash table)
├── mime.c / .h                   # Extension → MIME lookup (static table, strcasecmp)
├── log.c / .h                    # Lock-free SPSC ring (4096 slots), consumer thread
├── api.c / .h                    # API endpoints (/bookmarks.json, /bookmarks/<idx>.json, /api/databases)
├── bookmark_cache.c / .h         # Per-database JSON cache with mtime invalidation
├── databases_meta.c / .h         # File metadata (stat, user/group names, absolute paths)
├── common.h                      # Shared constants (MAX_BOOKMARK_FILES = 10)
├── project_config.h              # VERSION, BINARY_NAME, HOMEPAGE_URL constants
```

**Auto-generated (build/):**

- `gen_embedded_front_end_dir.c` — Embedded frontend file data (gzip-compressed C arrays)
- `gen_embedded_front_end_dir.h` — Declarations for above

**Frontend source (front_end/):**

- `index.html` + `javascript/*.js` (10 modules: browse, data, databases, info, keyboard, main, panel, random, search, sidebar, tag_bar) + `stylesheet/*.css` + `favicon.ico` + `sw.js`
- `embed_frontend.bash` — `xxd -i` + gzip per file → C arrays + vfs_entry table

---

## 4. Module Deep Dive

### 4.1 `main.c` — Entry & CLI

- `argp_parse()` with `parse_opt()` callback → fills global `Arguments G_Args`.
- Positional args: `<DB_FILE(s)>...` (max 10, from `common.h`).
- Validates all bookmark files exist and are readable at parse time.
- TLS flags (`--tls-cert`, `--tls-key`): validates file exists and is readable at parse time (if TLS compiled in).
- Clamps: threads 1–256, port 1–65535, keep-alive 0–3600, max-conns 0–1000.
- Calls `log_init(NULL)` → starts consumer thread.
- Initializes VFS hash table: `vfs_hash_init()`.
- Initializes header cache: `header_cache_init()`.
- Initializes bookmark cache: `bookmark_cache_init()`.
- Registers all databases with cache: `bookmark_cache_add_db()`.
- Builds `ServerConfig` from `Arguments`, calls `server_run()`.

### 4.2 `server.c` — Server Core

- **Signals**: SIGINT/SIGTERM → `g_shutdown=1`; SIGPIPE → `SIG_IGN`.
- **TLS context** (optional): reads PEM cert+key into memory via `read_file_to_buffer()`, creates `tls_load_certificates()` + `tls_load_private_key()` on master context.
- **Listener**: `getaddrinfo()` → socket → `SO_REUSEADDR|SO_REUSEPORT` → `listen(128)`.
- **Accept loop**: blocking `accept()`, wraps fd in `Transport`, `transport_accept()` (performs TLS handshake if configured), `peer_addr()` for IP, `ratelimit_accept()`, `thread_pool_submit(ClientJob)`.
- **ClientJob**: owns `Transport*`, client IP/port, copied `ServerConfig`, `RateLimit*`.
- **Request handling** (`handle_client`):
  1. `http_parse_request()` → HttpRequest
  2. If auth configured → `auth_check()` → 401 or continue
  3. **API first**: `api_handle_request()` handles `/bookmarks.json`, `/bookmarks/<idx>.json`, `/api/databases*`
  4. **Else VFS file serving**: `file_serve()` → embedded frontend files
  5. Keep-alive loop via `wants_keep_alive()` + `transport_set_timeout()`
- **Shutdown**: closes listener → `thread_pool_destroy()` → `ratelimit_destroy()` → `bookmark_cache_cleanup()` → `db_meta_cleanup()`. TLS: destroys master TLS context.

### 4.3 `transport.c` — Socket Abstraction

```c
struct Transport {
    int fd;
#ifdef SUPPORT_TLS_E
    struct TLSContext *tls_ctx;  // NULL when plaintext
#endif
};
```

- `transport_write()`: handles partial writes, retries on `EINTR`. TLS mode: chunks plaintext into ≤16KB, calls `tls_write` per chunk, then `tls_get_write_buffer` + `send()` for ciphertext.
- `transport_writev()`: `writev()` for header+body in one syscall. TLS mode: writes each iov entry separately (concatenating into one buffer hits `tls_write` size limits).
- `transport_set_timeout()`: `SO_RCVTIMEO` for keep-alive.
- `transport_destroy()`: nullifies caller's pointer after `free()`. TLS mode: calls `tls_close_notify()` + `tls_destroy_context()`.
- `transport_accept()`: if TLS configured, performs TLS handshake via `tls_accept()`. Returns -1 on failure (logs error, closes fd).
- `transport_is_tls()`: returns `tls_ctx != NULL`.
- `TCP_NODELAY` set in `transport_new()`.

### 4.4 `http.c` — Request Parser

- **Buffered reads**: 4 KB chunks into `HttpRequest.raw[8192]` until `\r\n\r\n`.
- **Request line**: method (GET/HEAD/OTHER), URI split on `?`, URL-decode path (`%XX`, `+`).
- **Path traversal check**: rejects `..` in decoded path immediately.
- **Headers**: extracts Authorization, Connection, If-None-Match, Range (suffix `-N` supported).
- **No body parsing** — only GET/HEAD supported.
- **Optimization**: Parses `raw[]` in-place (no secondary buffer copy).

### 4.5 `vfs_hash.c` — Virtual Filesystem Hash Table

- **FNV-1a hash** + linear probing.
- Table size: next power of 2 ≥ 2× `VFS_MAX_FILES` (default 20 → 64 slots).
- Built once at startup: `vfs_hash_init()` iterates generated `vfs[]` array.
- Lookup: `vfs_lookup(path)` → `vfs_entry*` or NULL.
- Load factor warning at ≥ 50%.

### 4.6 `header_cache.c` — Static Header Components

- **Date**: `time()` + `gmtime_r()` + `strftime()` once/sec (double-checked locking).
- **Server**: `"Server: local-mark/1.1.0\r\n"` built once at init.
- **Connection**: two static strings (`keep-alive` / `close`).
- `header_cache_build()`: single `snprintf` merges all pieces.

### 4.7 `response.c` — Response Builders

- `response_send()`: calls `header_cache_build()` + `transport_writev()` (header + body) or `transport_write()` (header only).
- `response_error()`: styled HTML error page with `prefers-color-scheme` dark mode.
- `response_redirect()`: 301 with `Location`.

### 4.8 `file.c` — VFS File Serving

**Entry**: `file_serve(req, transport, client_ip, port, print_request, keep_alive)`

1. **Method check**: GET/HEAD only → 405.
2. **Path normalization**: strips leading `/`, defaults to `index.html`.
3. **VFS lookup**: `vfs_lookup(path)` → `vfs_entry*`.
4. **Conditional GET**: `If-None-Match` (ETag) → 304.
5. **ETag**: FNV-1a hash of content + size → `"<hash>-<size>"`.
6. **Range requests**: parses `bytes=N-M`, `N-`, `-N`; validates; serves partial.
7. **Gzip detection**: embedded files pre-compressed → adds `Content-Encoding: gzip`.
8. **MIME**: `mime_from_path()` from extension.
9. **Response**: `response_send()` with `writev()`.

### 4.9 `auth.c` — Basic Auth

- `auth_check()`: extracts `Authorization: Basic <b64>`, decodes (hand-rolled), splits on `:`, compares.
- `auth_send_challenge()`: 401 + `WWW-Authenticate: Basic realm="local-mark"`.
- No external deps.

### 4.10 `thread_pool.c` — Fixed-Size Pool

- Circular queue: 4096 `Task { void (*func)(void*), void* arg }`.
- Mutex + `not_empty` + `not_full` condvars.
- Workers: `lock → wait(not_empty) → dequeue → unlock → func(arg)`.
- `thread_pool_submit()`: blocks if queue full (backpressures accept loop).
- `thread_pool_destroy()`: `stop=1`, broadcast both condvars, join all.

### 4.11 `ratelimit.c` — Per-IP Connection Limit

- **Fixed open-addressing hash table** (1024 slots, djb2 hash).
- `RLEntry { char ip[64]; int count; }` — inline IP storage, no allocations.
- `ratelimit_accept()`: increments, returns 0/-1.
- `ratelimit_leave()`: decrements, clears entry at 0.
- Single mutex covers all ops.

### 4.12 `bookmark_cache.c` — Multi-Database JSON Cache

- Per-database entries: `bookmark_cache_entry_t { char* json; size_t len; time_t mtime; char path[256]; }`.
- Max `MAX_BOOKMARK_FILES` (10) entries.
- `bookmark_cache_add_db(path)`: registers path at startup.
- `get_cached_bookmark_json(path)`: loads/refreshes based on mtime.
- `get_cached_bookmark_json_by_index(idx)`: index-based access for API.
- Thread-safe with `pthread_mutex_t`.

### 4.13 `databases_meta.c` — Database Metadata

- Populates `g_db_meta[]` from `stat()` on each bookmark file.
- **Absolute path**: `realpath()` resolves full path.
- **User/Group names**: `getpwuid()` / `getgrgid()` → `"user"`, `"group"` strings (fallback to numeric).
- Exposes `/api/databases` (list) and `/api/databases/<idx>` (single).

### 4.14 `api.c` — API Endpoints

| Endpoint                    | Description                             |
| --------------------------- | --------------------------------------- |
| `GET /bookmarks.json`       | First database (backward compat)        |
| `GET /bookmarks/<idx>.json` | Specific database by index (0, 1, 2...) |
| `GET /api/databases`        | List all databases with metadata        |
| `GET /api/databases/<idx>`  | Single database metadata                |

All return `application/json; charset=utf-8` with `Cache-Control: no-cache`.

### 4.15 `databases.js` — Database Selector UI

- `initDatabaseSelector()` — caches DOM refs (`#db-select-list`, `#db-select-error`)
- `renderDatabaseSelector()` — fetches `/api/databases`, renders cards via `buildDbCard()`
- `buildDbCard(db, idx, isActive)` — creates clickable card with:
  - Icon + file_name + relative time (e.g. "2 hour ago")
  - Permissions string + `user:group` + absolute timestamp
  - "Current" badge for active database
- `selectDatabase(idx, isActive)` — saves to localStorage, navigates to browse (loads new DB)

Format helpers: `relativeTime()`, `absoluteTime()`, `permString(mode)`

### 4.16 `mime.c` — MIME Lookup

- Static `struct { const char* ext; const char* mime; } table[]`.
- Case-insensitive `strcasecmp()` on extension (after last `.`).
- Fallback: `application/octet-stream`.
- Covers: HTML, CSS, JS, JSON, images, fonts, audio, video, text, PDF, archives, WASM.

### 4.17 `log.c` — Lock-Free Ring Logger

- **Levels**: `LOG_LEVEL_ERROR` (0), `WARN` (1), `INFO` (2), `DEBUG` (3).
- **Ring**: 4096 `LogSlot { char buf[256]; int len; atomic_int ready; }`.
- **Producers (workers)**: `atomic_fetch_add(&head, 1)` → format full line → `atomic_store(&slot.ready, 1)`.
- **Consumer (dedicated thread)**: busy-waits `ready` flag → `fwrite()` batch → `fflush()`.
- **Timestamp**: captured at call site (accurate).
- **Colours**: ANSI auto-detected via `isatty(fileno(stderr))`. File output → no colours.
- **Macros**: `LOG_ERROR()`, `LOG_WARN()`, `LOG_INFO()`, `LOG_DEBUG()`, `LOG_PERROR()` (adds `strerror(errno)`).

---

## 5. Request Lifecycle (Happy Path)

```
1. main thread: accept() → cfd
2. transport_new(cfd) → Transport* (TCP_NODELAY set)
3. [if TLS] transport_set_tls(transport, tls_ctx) → TLS handshake via tls_accept()
4. peer_addr() → client_ip:port
5. ratelimit_accept(ip) → OK
6. ClientJob* malloc'd, filled
7. thread_pool_submit(handle_client, job)

Worker thread:
8. http_parse_request() → HttpRequest (4 KB buffered read; decrypted if TLS)
9. If auth configured → auth_check() → 401 or continue
10. api_handle_request():
   a. If /bookmarks.json or /bookmarks/<idx>.json → serve cached bookmark JSON
   b. If /api/databases* → serve metadata JSON
   c. Returns 1 if handled, 0 otherwise
11. If not API → file_serve():
    a. Method check (GET/HEAD)
    b. vfs_lookup(path) → vfs_entry*
    c. Conditional GET? → 304
    d. Range? → validate, serve partial
    e. response_send() → header_cache_build() + transport_writev()
       [if TLS] transport_write() chunks plaintext 16KB per tls_write call
12. keep_alive? → transport_set_timeout() → loop to step 8
13. cleanup: ratelimit_leave(), transport_destroy(), free(job)
```

---

## 6. Build System

### `Makefile` (top-level)

```make
CC = clang
CFLAGS = -Wall -Wextra -Wpedantic -Wstrict-prototypes -Wmissing-prototypes \
         -Wshadow -Wconversion -Wno-missing-field-initializers -std=c17 -Isrc
# Compile-time log features (always on):
CFLAGS += -DLOG_SHOW_TIME_STAMP -DLOG_SHOW_SOURCE_LOCATION

# macOS
LDFLAGS += -largp
# Linux
CFLAGS += -D_GNU_SOURCE

# TLS (optional, via `make tls`):
# Downloads tlse into third_party, adds -DSUPPORT_TLS_E, links tlse.c

# Debug target:
make debug O_DEBUG=1
# → -g3 -DDEBUG -fstack-usage -fsanitize=address -fsanitize=undefined -ffreestanding

# Release:
make            # -O3
make strip      # -O3 + strip symbols
make tls        # -O3 + TLS support (tlse linked in)
sudo make install            # /usr/local/bin
sudo make install PREFIX=~/.local  # ~/.local/bin
```

### Frontend Embedding

```bash
# Embedded by Makefile when any front_end/ file changes
FRONT_END_FILES = \
    front_end/javascript/browse.js \
    front_end/javascript/data.js \
    ... \
    front_end/index.html

# embed_frontend.bash does per-file:
gzip -9 -n -c front_end/xxx > gzip_stage/xxx.gz
xxd -n SYMBOL -i gzip_stage/xxx.gz
# Generates vfs_entry table with {path, data_ptr, size}
```

### No dependency tracking

- Headers not in `Makefile` deps. Relies on `.c → .o` timestamps. Run `make clean` after header changes.

---

## 7. Testing

| Suite    | Command                          | What It Covers                            |
| -------- | -------------------------------- | ----------------------------------------- |
| Manual   | `./local-mark db.json`           | Full server + frontend test               |
| Multi-DB | `./local-mark db1.json db2.json` | Database switching, API endpoints         |
| Build    | `make clean && make`             | Clean rebuild, no warnings (except known) |

---

## 8. Key Design Decisions (Rationale)

| Decision                        | Why                                                                            |
| ------------------------------- | ------------------------------------------------------------------------------ |
| Blocking I/O + thread pool      | Simpler than async; POSIX threads universal; pool prevents fork bomb           |
| 4 KB buffered header reads      | Reduces syscalls from O(header_bytes) to ~1; avoids partial-read complexity    |
| Embedded frontend (VFS)         | Single binary deployment; no filesystem access at runtime; gzip pre-compressed |
| VFS hash table (O(1) lookup)    | Fast embedded file serving; built once at startup                              |
| Multi-database cache with mtime | Supports multiple JSON DBs; auto-reloads on file change                        |
| User/group names in metadata    | Human-readable `/api/databases` output                                         |
| No directory listing            | Intentional. VFS only serves known embedded files.                             |
| No compression at runtime       | Files pre-gzipped at embed time; `Content-Encoding: gzip` header added         |
| No TLS                          | Zero-dep promise. Use reverse proxy (Caddy, nginx) for HTTPS                   |
| Opaque `Transport`              | Future TLS swap without touching callers                                       |
| Optional TLS via `make tls`     | `tlse` single-file lib, zero runtime deps without TLS. Chunks 16KB plaintext.  |
| Lock-free ring logger           | Eliminates mutex contention under load; consumer batches I/O                   |
| Pre-computed headers            | Date/Server/Connection formatted once/sec, not per-request                     |
| `writev()` header+body          | Single syscall, fewer TCP segments                                             |
| Hand-rolled ETag hex            | Avoids `snprintf` in hot path                                                  |

---

## 9. Performance Characteristics

| Metric                   | Mechanism                       | Impact                                                             |
| ------------------------ | ------------------------------- | ------------------------------------------------------------------ |
| Static file throughput   | Pre-gzipped VFS + writev        | Minimal CPU, fast delivery                                         |
| Per-request alloc        | Near-zero (VFS buffers static)  | Near-zero allocator contention                                     |
| Logging overhead         | Lock-free ring + batched fwrite | ~50 ns / call under load                                           |
| Header formatting        | Cached Date/Server/Connection   | Saves 3× `snprintf` + `time()` + `gmtime_r()` + `strftime()` / req |
| Syscall count (response) | `writev()` header+body          | 1 syscall vs 2+                                                    |
| Rate limiter scaling     | Fixed 1024-slot hash table      | O(1) per IP, no growth overhead                                    |
| Multi-DB cache           | mtime-based invalidation        | Instant reload on file change, no polling                          |

---

## 10. Known Limitations (By Design)

- **Optional HTTPS** — TLS available via `make tls` (tlse lib, ~100KB added to binary). Still no runtime deps.
- **No compression at runtime** — files pre-compressed at build.
- **No directory listing** — VFS serves only known embedded files.
- **No HTTP/2** — single-threaded accept + thread pool is HTTP/1.1 only.
- **Max 10 databases** — `MAX_BOOKMARK_FILES` constant (easy to increase).
- **No config file / env vars** — all CLI flags (explicit, reproducible).
- **No metrics endpoint** — not an observability target.
- **macOS argp** — requires `brew install argp-standalone`.

---

## 11. Version History (Current: 1.1.0)

| Version | Changes                                                                                                                                                                                |
| ------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1.1.0   | Multi-database support, database selector UI, optional TLS via `make tls` (tlse lib), transport_writev TLS-safe iov splitting, user/group names, live_server core optimizations merged |
| 1.0.0   | Initial: embedded frontend + single bookmark.json + basic HTTP server                                                                                                                  |

---

## 12. Files an Agent Might Need to Touch

| Task                          | Files                                                                                          |
| ----------------------------- | ---------------------------------------------------------------------------------------------- |
| Add CLI flag                  | `main.c` (argp options + `parse_opt`), `server.h` (ServerConfig), `server.c` (config plumbing) |
| New MIME type                 | `mime.c` (table entry)                                                                         |
| New log level                 | `log.h` (enum), `log.c` (macro + colour)                                                       |
| Change thread pool queue size | `thread_pool.c` (QUEUE_CAPACITY = 4096)                                                        |
| Adjust rate limiter size      | `ratelimit.c` (RL_TABLE_SIZE = 1024)                                                           |
| Add API endpoint              | `api.c` (new route in `api_handle_request`)                                                    |
| Increase max databases        | `common.h` (MAX_BOOKMARK_FILES), `bookmark_cache.c` (array size)                               |
| Modify embedded frontend      | `front_end/embed_frontend.bash`, `Makefile` (FRONT_END_FILES)                                  |
| Add response header           | `header_cache.c` (if static) or `response.c` (if dynamic)                                      |

---

## 13. Mental Model Checklist for Agents

- [ ] Single binary, no runtime deps, CLI-only config
- [ ] Main thread = accept + dispatch only
- [ ] Workers = full request lifecycle (parse → serve → keep-alive)
- [ ] Log consumer = 1 pthread, drain ring buffer
- [ ] VFS = embedded files only, hash table lookup, gzip pre-compressed
- [ ] Multi-database = per-DB cache with mtime invalidation
- [ ] API first, then VFS file serving
- [ ] All shared state protected by mutexes (ratelimit, header_cache, bookmark_cache, thread pool queue)
- [ ] Lock-free only in logger (SPSC ring)
- [ ] `Transport` is the **only** way to touch sockets
- [ ] `writev()` for all plaintext responses
- [ ] TLS (optional): `transport_write()` chunks plaintext 16KB, `transport_writev()` writes each iov separately
- [ ] No streaming request bodies (GET/HEAD only)
- [ ] No directory listing, no runtime compression, no HTTP/2

---

## 14. Quick Commands

```bash
# Build release
make

# Build with TLS
make tls

# Build debug (ASan+UBSan)
make debug O_DEBUG=1

# Rebuild frontend (if front_end/ files changed)
make clean && make

# Run with single database
./local-mark bookmarks.json

# Run with multiple databases
./local-mark db1.json db2.json db3.json

# Custom port/host/threads
./local-mark -P 3000 -H 0.0.0.0 -T 4 bookmarks.json

# With auth
./local-mark -u admin -p secret bookmarks.json

# With rate limit
./local-mark -M 10 bookmarks.json

# With TLS (requires make tls)
./local-mark --tls-cert cert.pem --tls-key key.pem bookmarks.json

# Install
sudo make install           # /usr/local/bin
sudo make install PREFIX=~/.local  # ~/.local/bin

# Create database from pipe-delimited .txt
./marks2json create *.txt -T bookmarks.json
./marks2json update *.txt -T bookmarks.json
./marks2json find-dead -T bookmarks.json
```

---

## 15. Frontend Architecture (Embedded SPA)

**Views (hash-routed):**

- `#browse` — Category sidebar + bookmark grid/list
- `#info` — Database stats (counts, categories, tags, domains)
- `#random` — Random link picker with filters
- `#databases` — Database selector page (cards with metadata, switch workspace)

### 15.1 JavaScript Modules (`front_end/javascript/`)

| Module         | Purpose                                                                        | Key Exports                                                                                                                                                                                                                                                                                                                   |
| -------------- | ------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `main.js`      | Entry point, hash router, boot sequence, DB indicator                          | `init()`, `renderRoute()`, `updateDbIndicator()`                                                                                                                                                                                                                                                                              |
| `data.js`      | Shared data layer: fetch, IndexedDB cache, favorites, theme, layout, active DB | `fetchBookmarks(idx?)`, `fetchDatabases()`, `getActiveDbIndex()`, `setActiveDbIndex()`, `getActiveDbName()`, `getFavorites()`, `toggleFavorite()`, `isFavorite()`, `initTheme()`, `toggleTheme()`, `getTheme()`, `setTheme()`, `getLayout()`, `setLayout()`, `getSidebarWidth()`, `setSidebarWidth()`, `buildCard()`, `esc()` |
| `browse.js`    | Browse view orchestration: sidebar, panel, search, tag bar, keyboard           | `initBrowse(data)`, `renderBrowse()`, `updateCategories(newData)`                                                                                                                                                                                                                                                             |
| `sidebar.js`   | Category sidebar rendering & events (click, keyboard, favorites)               | `initSidebar(cfg)`, `renderSidebar()`, `getActiveCategory()`, `setActiveCategory()`, `highlightSidebar()`                                                                                                                                                                                                                     |
| `panel.js`     | Main panel: renders bookmark cards for active category/favorites + tag bar     | `initPanel(cfg)`, `renderPanel()`, `getActiveCategory()`, `setActiveCategory()`, `clearActiveTags()`, `notifyCategoriesChanged()`                                                                                                                                                                                             |
| `search.js`    | Search index + results rendering (grouped by category)                         | `initSearch(cfg)`, `renderSearch(query)`, `clearSearch()`, `setCategories(cats)`, `rebuildIndex()`                                                                                                                                                                                                                            |
| `tag_bar.js`   | Tag filter pills UI (click to toggle, expand/collapse, clear all)              | `initTagBar(el)`, `renderTagBar(tags)`, `getActiveTags()`, `setActiveTags()`, `toggleExpanded()`, `isExpanded()`                                                                                                                                                                                                              |
| `keyboard.js`  | Vim-style shortcuts (j/k, h/l, gg/G, /, Enter, o, yy, p, ?, Esc)               | `initKeyboard(cfg)`, `refreshCards()`, `focusFirstCard()`, `focusLastCard()`, `focusNextCard()`, `focusPrevCard()`, `blurCards()`, `focusSidebar()`                                                                                                                                                                           |
| `databases.js` | Database selector page: cards with metadata, search, keyboard nav, switch DB   | `initDatabaseSelector()`, `renderDatabaseSelector()`, `selectDatabase(idx, isActive)`                                                                                                                                                                                                                                         |
| `info.js`      | Info view: stats strip, category chart, tag cloud, domain grid                 | `renderInfo(data)`                                                                                                                                                                                                                                                                                                            |
| `random.js`    | Random view: picker with count/category/tag filters, open all                  | `initRandom(data)`, `renderRandom()`                                                                                                                                                                                                                                                                                          |

### 15.2 CSS (`front_end/stylesheet/`)

| File               | Purpose                                                                     |
| ------------------ | --------------------------------------------------------------------------- |
| `style.css`        | Main styles (dark theme, CSS variables for theming), responsive breakpoints |
| `themes/light.css` | Light theme overrides via `[data-theme="light"]`                            |

### 15.3 HTML & Assets

| File          | Purpose                                                                               |
| ------------- | ------------------------------------------------------------------------------------- |
| `index.html`  | SPA shell: header (search, layout toggle, DB switcher, theme toggle), 4 view sections |
| `favicon.ico` | Site icon (served via VFS)                                                            |

### 15.4 JSON Data Schema

**Input to `marks2json`** (pipe-delimited `.txt`):

```
title | url | description | #tag1 #tag2
```

**Output (`bookmarks.json`):**

```json
{
  "book_Marks": [
    {
      "category": "Category Name",
      "bookmarks": [
        {
          "title": "Display Title",
          "url": "https://example.com",
          "description": "Optional description",
          "tags": ["#tag1", "#tag2"],
          "domain": "example.com",
          "icon": "https://..."   // YouTube channels only, with --icon
        }
      ]
    }
  ],
  "book_mark_domain_hash": { "example.com": 42 },
  "book_mark_tag_hash": { "#tag1": 10, "#tag2": 5 }
}
```

| Field                   | Type         | Description                                  |
| ----------------------- | ------------ | -------------------------------------------- |
| `book_Marks`            | `Category[]` | Array of categories (order preserved)        |
| `Category.category`     | `string`     | Category name (from filename)                |
| `Category.bookmarks`    | `Bookmark[]` | Bookmarks in this category                   |
| `Bookmark.title`        | `string`     | Display title (fallback: URL)                |
| `Bookmark.url`          | `string`     | Absolute URL (must have http/https)          |
| `Bookmark.description`  | `string`     | Optional description                         |
| `Bookmark.tags`         | `string[]`   | Tags without `#` prefix in JSON              |
| `Bookmark.domain`       | `string`     | Extracted hostname (no `www.`)               |
| `Bookmark.icon`         | `string?`    | YouTube channel avatar URL                   |
| `book_mark_domain_hash` | `object`     | Domain → count across all bookmarks         |
| `book_mark_tag_hash`    | `object`     | Tag (with `#`) → count across all bookmarks |

### 15.5 API Endpoints Consumed by Frontend

| Endpoint                | Method | Response                              | Used By                            |
| ----------------------- | ------ | ------------------------------------- | ---------------------------------- |
| `/api/databases`        | GET    | `{ databases: DBMeta[], count: int }` | `databases.js` → selector page    |
| `/api/databases/<idx>`  | GET    | `DBMeta`                              | (future: single DB detail)         |
| `/bookmarks.json`       | GET    | Full JSON of first DB                 | Backward compat                    |
| `/bookmarks/<idx>.json` | GET    | Full JSON of DB at index              | `data.js` → `fetchBookmarks(idx)` |

**DBMeta fields:**

```json
{
  "mode": "0644",
  "absolute_path": "/full/path/db.json",
  "file_name": "db.json",
  "file_size": 2410,
  "cTime": 1700000000,
  "bTime": 1690000000,
  "user": "pritam",
  "group": "staff",
  "mTime_sec": 1700000000,
  "mTime_nsec": 123456789
}
```

### 15.6 Persistence (localStorage)

| Key                    | Value                                           |
| ---------------------- | ----------------------------------------------- |
| `localmarks-favorites` | `string[]` — starred URLs                      |
| `localmarks-layout`    | `"single" \| "grid" \| "compact"`               |
| `localmarks-sidebar-w` | `number` — sidebar width in px                 |
| `localmarks-theme`     | `"dark" \| "light"`                             |
| `localmarks-active-db` | `string` — database index (e.g., `"0"`, `"1"`) |

### 15.7 IndexedDB Cache

- **Database**: `LocalMarksCache`
- **Store**: `bookmarks` (keyPath: `id`)
- **Keys**: `bookmarks:0`, `bookmarks:1`... per database index
- **Value**: `{ id, data, timestamp }`
- **Strategy**: Stale-while-revalidate — returns cached immediately, fetches fresh in background

### 15.8 Lazy DB Loading

On first visit, shows Database Selector (**no bookmark API call**). User picks a DB → saves index to localStorage → navigates to browse → **only then** fetches `/bookmarks/<idx>.json`. Switching DBs fetches new one in place (no full reload).

---

## 16. Database Switching Flow

1. User lands on `#databases` → `renderDatabaseSelector()` calls `fetchDatabases()` → shows cards
2. User clicks card → `selectDatabase(idx)` calls `setActiveDbIndex(idx)` + `location.hash = '#browse'`
3. Router (`renderRoute`) for `#browse` → calls `ensureDataLoaded()`
4. `ensureDataLoaded()` checks `getActiveDbIndex()` → calls `fetchBookmarks(idx)`
5. On success: `initBrowse(data)` / `updateCategories(data)` → renders view
6. Subsequent visits use cached IndexedDB (stale-while-revalidate)

---

_Generated from codebase inspection. Update when architecture changes._
