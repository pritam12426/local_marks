# AGENTS.md

## Project

Fast, local-first bookmark browser — C17 binary (`local_mark`) + static web UI.
Reads `bookmarks.json` DB; serves frontend + API over HTTP.

**Current state**: server logic not yet implemented. `main.c` parses CLI args,
validates them, and exits.

## Build

```sh
make                           # release build → ./local_mark
make debug -B O_DEBUG=1        # debug build (-g3 -DDEBUG, sanitizers)
make clean
make O_TLS=1                   # TLS via OpenSSL (macOS: brew install openssl; needs pkg-config)
```

- Compiler: clang, `-std=c17`, source in `src/*.{c,h}`, objects → `build/`.
- Deps: `-lpthread`; macOS `brew install argp-standalone` → `-largp`.
- Build flags always on: `LOG_SHOW_TIME_STAMP`, `LOG_SHOW_SOURCE_LOCATION`.
- Frontend is embedded into the binary: `make` tars `front_end/` → `build/front_end.tar` → linked `.o`. GNU `ld -r -b binary` on Linux; macOS uses `xxd -i` + `$(CC)` (Apple `ld` does not support `-b binary`).

## CLI

Source of truth is `src/main.c` (differs from README):

| Flags         | Short  | Value    | Description                                         |
| ------------- | ------ | -------- | --------------------------------------------------- |
| `--log-level` | `-L`   | `LEVEL`  | Log level: error/warn/info/debug (default info)      |
| `--log-file`  | `-F`   | `FILE`   | Append logs to file (default stderr)                 |
| `--user`      | `-u`   | `USER`   | Basic auth username                                  |
| `--pass`      | `-p`   | `PASS`   | Basic auth password                                  |
| `--max-conns` | `-M`   | `NUM`    | Max concurrent conns per IP (0 = unlimited)          |
| `--port`      | `-P`   | `PORT`   | TCP port (default 8080)                              |
| `--host`      | `-H`   | `HOST`   | Bind address (default localhost)                     |
| `--browser`   | `-B`   | `BROWSER`| Browser to open on startup (e.g. firefox)           |
| `--tls-cert`  | `-T`   | `PATH`   | TLS cert (requires `O_TLS=1` build)                 |
| `--tls-key`   | `-K`   | `PATH`   | TLS key (requires `O_TLS=1` build)                  |

Positional `<DB_FILE(s)>...` required (max 10, set in `common.h`).

## Source layout

| Path                      | Purpose                                                    |
| ------------------------- | ---------------------------------------------------------- |
| `src/main.c`              | Entrypoint — CLI parsing, validation, startup              |
| `src/log.h` / `src/log.c` | Thread-safe logger (rwlock), timestamps, colors             |
| `src/vfs.h` / `src/vfs.c` | Virtual filesystem — parses embedded tar at runtime         |
| `src/common.h`            | Shared constants (MAX_BOOKMARK_FILES)                      |
| `src/project_config.h`    | Version, name, metadata                                     |
| `third_party/tlse.h`      | Symlink to eduardsui/tlse (unused by Makefile; gitignored) |
| `front_end/`              | Static SPA (`index.html`, `javascript/`, `stylesheet/`)    |
| `marks2json.py`           | Python converter: `create`/`update` subcommands            |

## Workflow

```sh
marks2json create *.txt -T bookmarks.json          # create DB from pipe-delim .txt
marks2json update *.txt -T bookmarks.json           # append new (--override to refresh)
./local_mark bookmarks.json
```

## Logging

```c
LOG_INFO("started on port ", port);
LOG_ERROR("failed: ", strerror(errno));
LOG_DEBUG("bookmark count: ", count);
LOG_PERROR("bind failed");    // logs message + appends perror
```

- Call `log_init()` before any LOG macro.
- Thread-safe (pthread_rwlock). Writes to stderr or `--log-file`.

## Conventions

- `.clang-tidy` enforces clang-analyzer, readability, modernize, bugprone,
  misc-include-cleaner, llvm-header-guard.
- No test framework exists.
- Single-threaded (logger is thread-safe but app is not concurrent).
