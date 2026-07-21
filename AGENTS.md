# AGENTS.md

## Project

Local-first bookmark browser — C17 binary (`local-mark`) + embedded static web UI.
Reads `bookmarks.json` DB; serves frontend + API over HTTP using a thread pool.

## Build

```sh
make                           # release build → ./local-mark
make debug -B O_DEBUG=1        # debug build (-g3 -DDEBUG, sanitizers)
make clean
make install                   # installs to PREFIX (default /usr/local)
```

- Compiler: clang, `-std=c17`, source in `src/*.{c,h}`, objects → `build/`.
- macOS prerequisite: `brew install argp-standalone` (links `-largp`).
- Linux: `-D_GNU_SOURCE` is added automatically.
- `LOG_SHOW_TIME_STAMP` is always on. `LOG_SHOW_SOURCE_LOCATION` is debug-only (`-DDEBUG`).
- Debug builds enable `-fsanitize=address`, `-fsanitize=undefined`, `-fstack-usage`.
- Frontend embedding: `front_end/embed_frontend.bash` runs `xxd -i` per file → C arrays + `vfs_entry` table in `build/gen_embedded_front_end_dir.c` + `src/gen_embedded_front_end_dir.h`. The script gzip-compresses each file before embedding. The Makefile re-runs it when any `FRONT_END_FILES` or the script changes.

## CLI

Source of truth is `src/main.c` (differs from README):

| Flags             | Short | Value     | Description                                            |
| ----------------- | ----- | --------- | ------------------------------------------------------ |
| `--log-level`     | `-L`  | `LEVEL`   | Log level: error/warn/info/debug (default info)        |
| `--log-file`      | `-F`  | `FILE`    | Append logs to file (default stderr)                   |
| `--print-request` | `-R`  |           | Log each client request and its headers                |
| `--user`          | `-u`  | `USER`    | Basic auth username                                    |
| `--pass`          | `-p`  | `PASS`    | Basic auth password                                    |
| `--port`          | `-P`  | `PORT`    | TCP port (default 8080)                                |
| `--host`          | `-H`  | `HOST`    | Bind address (default localhost)                       |
| `--threads`       | `-T`  | `NUM`     | Thread pool size (default 2)                           |
| `--keep-alive`    | `-K`  | `SECS`    | Keep-alive timeout in seconds (default 3, 0 = disable) |
| `--max-conns`     | `-M`  | `NUM`     | Max concurrent conns per IP (0 = unlimited)            |
| `--browser`       | `-B`  | `BROWSER` | Browser to open on startup                             |

TLS flags (`--tls-cert`, `--tls-key`) are conditionally compiled (`-DO_TLS=1`).

Positional `<DB_FILE(s)>...` required (max 10, set in `common.h`).

## Source layout

| Path                                            | Purpose                                                         |
| ----------------------------------------------- | --------------------------------------------------------------- |
| `src/main.c`                                    | Entrypoint — CLI parsing (argp), validation, startup           |
| `src/server.c` / `src/server.h`                 | Accept loop, thread pool dispatch, keep-alive                   |
| `src/http.c` / `src/http.h`                     | HTTP request parser (GET/HEAD only, buffered)                   |
| `src/response.c` / `src/response.h`             | HTTP response builder (status, error, redirect)                 |
| `src/transport.c` / `src/transport.h`           | Opaque socket I/O wrapper (handles partial writes)              |
| `src/auth.c` / `src/auth.h`                     | HTTP Basic Authentication                                       |
| `src/ratelimit.c` / `src/ratelimit.h`           | Per-IP connection rate limiting (hash table)                    |
| `src/thread_pool.c` / `src/thread_pool.h`       | Fixed-size thread pool (circular buffer)                        |
| `src/mime.c` / `src/mime.h`                     | MIME type lookup by extension                                   |
| `src/log.c` / `src/log.h`                       | Thread-safe logger (mutex-based), timestamps, colors            |
| `src/error.c` / `src/error.h`                   | Centralized error handling (JSON + HTML error responses)        |
| `src/log_middleware.c` / `src/log_middleware.h` | Request/response logging with request ID tracking               |
| `src/file.c` / `src/file.h`                     | VFS-based static file serving (no filesystem access)            |
| `src/vfs_hash.c` / `src/vfs_hash.h`             | O(1) hash-table lookup for embedded frontend files              |
| `src/api.c` / `src/api.h`                       | API endpoints (`/bookmarks*`, `/api/databases*`)                |
| `src/bookmark_cache.c` / `src/bookmark_cache.h` | Multi-DB JSON cache (mtime invalidation)                        |
| `src/databases_meta.c` / `src/databases_meta.h` | File metadata (stat, user/group, realpath)                      |
| `src/header_cache.c` / `src/header_cache.h`     | Pre-computed Date/Server/Connection headers                     |
| `src/gen_embedded_front_end_dir.h`              | Auto-generated: `vfs_entry` struct + extern arrays              |
| `src/common.h`                                  | Shared constants (MAX_BOOKMARK_FILES)                           |
| `src/project_config.h`                          | Version, name, metadata                                         |
| `front_end/`                                    | Static SPA (`index.html`, `javascript/`, `stylesheet/`)         |
| `front_end/embed_frontend.bash`                 | Script: xxd per-file → C arrays + vfs_entry table              |
| `marks2json.py`                                 | Python converter: `create` / `update` / `find-dead` subcommands |

## Architecture

Blocking-accept loop + fixed-size thread pool. No event loop library.
Each accepted connection is dispatched as a `ClientJob` to the pool;
the worker thread reads the HTTP request, serves the file, and
optionally loops for keep-alive.

Key lookup: `vfs_lookup("index.html")` returns an embedded `vfs_entry`
from the hash table (no filesystem access for frontend files).

Request flow: `http_parse_request()` → `auth_check()` (if configured) →
`api_handle_request()` (API routes first) → `file_serve()` (VFS fallback).

## Database Loading Behavior (Current)

**Lazy loading** — no database is loaded at startup.

1. Server starts → shows **Database Selector page** (`#databases`)
2. User clicks a database card
3. Selection saved to `localStorage` (`localmarks-active-db`)
4. Navigates to `#browse` → **only then** fetches `/bookmarks/<idx>.json`
5. Switching databases fetches new one in place (no full reload)

## Workflow

```sh
marks2json create *.txt -T bookmarks.json          # create DB from pipe-delim .txt
marks2json update *.txt -T bookmarks.json           # append new (--override to refresh)

./local-mark bookmarks.json

# Check link health (HEAD requests) and print results to stdout
marks2json find-dead -T bookmarks.json

# Check link health and write new database with only healthy links
marks2json find-dead -T bookmarks.json --healthy healthy.json
```

## Logging

```c
LOG_INFO("started on port %d", port);
LOG_ERROR("failed: %s", strerror(errno));
LOG_DEBUG("bookmark count: %zu", count);
LOG_PERROR("bind failed");    // logs message + appends perror
```

- Call `log_init(file_path, level, flags)` before any LOG macro.
- Thread-safe (pthread_mutex). Writes to stderr or `--log-file`.
- Log macros are printf-style (format string + args).
- Levels: `LOG_LEVEL_FATAL`, `ERROR`, `WARN`, `INFO`, `DEBUG`, `TRACE`.
- Flags: `LOG_FLAG_SHOW_TIMESTAMP`, `LOG_FLAG_SHOW_SOURCE` (bitmask).

## Conventions

- `.clang-tidy` enforces clang-analyzer, readability, modernize, bugprone,
  misc-include-cleaner, llvm-header-guard.
- No test framework exists — verify with `make clean && make` (clean rebuild, no warnings).
- Header guard style: `_FILENAME_H_` (double underscore prefix/suffix).
- Tabs for C/Makefile, 4-space indentation for Python.
- Source copied from [live_server](https://github.com/pritam12426/live_server)
  (`server.c`, `http.c`, `response.c`, `transport.c`, `auth.c`, `thread_pool.c`,
  `ratelimit.c`, `mime.c`) — adapted to serve embedded VFS files instead of
  real filesystem files. `PROJECT_BRIEF.md` documents the original architecture.
