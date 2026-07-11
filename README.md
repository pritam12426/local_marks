<h1 align="center">
	<br>
	<img src="assets/README_icon.png" width="200">
	<br>
	📚 LocalMarks
	<br>
	<br>
</h1>

Turn pipe-delimited bookmark files into a searchable, categorized local web UI. A single C17 binary serves a static frontend with tag/domain filters, no network required — just a browser.

## Table of Contents

- [How it works](#how-it-works)
- [Project structure](#project-structure)
- [Getting started](#getting-started)
- [Building from source](#building-from-source)
- [Writing your .txt bookmark files](#writing-your-txt-bookmark-files)
- [marks2json &mdash; the converter](#marks2json--the-converter)
- [Running the viewer](#running-the-viewer)
- [Features](#features)
- [Bonus — bookmarkfmt](#bonus--bookmarkfmt)

---

## How it works

```
your .txt files  ──►  marks2json  ──►  bookmarks.json  ──►  local-mark  ──►  browser
```

1. You keep bookmarks as human-readable pipe-separated `.txt` files
2. `marks2json` converts them into a structured `bookmarks.json` database
3. `local-mark` (a single C17 binary) serves the JSON and the static frontend over HTTP
4. No Electron, no Node, no Docker, no cloud

---

## Project structure

```
local_marks/
├── src/
│   ├── main.c                       # Entrypoint — CLI parsing, startup
│   ├── log.h / log.c                # Logger with level gating, colors, timestamps
│   ├── common.h                     # Shared constants (MAX_BOOKMARK_FILES)
│   ├── project_config.h             # Version, name, metadata
│   ├── vfs_hash.h / vfs_hash.c      # Hash-table lookup for embedded frontend files
│   ├── temp_hash_lookup.c           # Hash-table helpers
│   └── gen_embedded_front_end_dir.h # Auto-generated: vfs_entry struct + extern arrays
├── front_end/
│   ├── embed_frontend.bash          # Script: xxd per-file → C arrays + vfs_entry table
│   ├── index.html                   # SPA shell (browse/info/random views via hash routing)
│   ├── javascript/
│   │   ├── main.js                  # Entry point, bootstraps data, hash router
│   │   ├── data.js                  # Shared helpers (fetch, card builder, layout, favorites)
│   │   ├── browse.js                # Browse view (sidebar, search, tag filters, cards)
│   │   ├── info.js                  # Info view (stats, category chart, tag cloud, domain grid)
│   │   └── random.js                # Random picker (category/tag filters, open all)
│   ├── stylesheet/
│   │   ├── style.css                # All visual styles
│   │   └── themes/                  # Color themes
│   └── favicon.ico
├── marks2json.py                    # Python converter: .txt → JSON
└── Makefile                         # Pure Makefile build (no CMake)
```

---

## Getting started

### 1. Download or build `local-mark`

**build from source** (see [Building from source](#building-from-source)).

### 2. Create your bookmark database

```bash
./marks2json create ~/bookmarks/*.txt -T bookmarks.json
```

### 3. Start the viewer

```bash
./local-mark bookmarks.json
```

Open [http://localhost:8080](http://localhost:8080).

---

## Writing your .txt bookmark files

Each `.txt` file becomes one **category** in the viewer. The filename (minus extension) becomes the category name — underscores become spaces, each word capitalised.

```
free_time.txt      →  "Free Time"
learning_python.txt  →  "Learning Python"
```

### Line format

One bookmark per line, fields separated by `|`:

```
title | url | description | tags
```

| Field         | Required | Notes                                   |
| ------------- | -------- | --------------------------------------- |
| `title`       | no       | Display name                            |
| `url`         | **yes**  | Must contain `http://` or `https://`    |
| `description` | no       | Short note                              |
| `tags`        | no       | Space-separated, each prefixed with `#` |

### Rules `marks2json` enforces

- Lines **without** `http://` or `https://` are skipped
- Lines with **more than 3 pipes** (more than 4 columns) are skipped
- Lines starting with `#` are treated as **comments** and skipped
- Empty lines are skipped

### Comments

```
# ── Learning ────────────────────────────────
MDN      | https://developer.mozilla.org | Web platform docs | #Dev #Web
Python   | https://docs.python.org       | Python reference  | #Dev #Python

# ── YouTube channels ────────────────────────
Tsoding  | https://www.youtube.com/@tsoding | Live coding | #YouTube #C
```

### Full example — `free_time.txt`

```
# ── Games ───────────────────────────────────
akinator      | https://en.akinator.com          | Guess a celebrity     | #Game
invisiblecow  | https://findtheinvisiblecow.com/ | Find the Invisible Cow | #Game

# ── Reading ──────────────────────────────────
oddee         | https://www.oddee.com/           | Random interesting stuff | #Blog #Read

# ── Misc ─────────────────────────────────────
earthcam      | https://www.earthcam.com         | Live cameras worldwide   | #Cam #Media
```

---

## marks2json &mdash; the converter

Two subcommands: `create` and `update`.

### Create a new database

```bash
marks2json create *.txt -T bookmarks.json
marks2json create *.txt -T bookmarks.json --icon   # fetch YouTube channel icons
```

### Update an existing database

Skips URLs already present (no duplicates). Use `--override` to refresh existing entries.

```bash
marks2json update new_category.txt -T bookmarks.json
marks2json update tools.txt references.txt -T bookmarks.json --override
```

### All options

```
marks2json [-I/--icon] {create,update} ...

create  FILE... [-T DB]         Build fresh database
update  FILE... -T DB [-O]      Append/override into existing database
```

### JSON schema

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
                    "icon": "https://..."  // YouTube channels with --icon
                }
            ]
        }
    ],
    "book_mark_domain_hash": { "en.akinator.com": 1, "oddee.com": 3 },
    "book_mark_tag_hash":    { "#Game": 4, "#Dev": 12 }
}
```

---

## Running the viewer

```sh
./local-mark first_bookmarks_DB.json second_bookmarks_DB.json
```

### Options

| Argument      | Short | Default         | Description                       |
| ------------- | ----- | --------------- | --------------------------------- |
| `FILE`        |       | required        | Bookmark JSON file(s)             |
| `--port`      | `-P`  | `8080`          | TCP port                          |
| `--host`      | `-H`  | `localhost`     | Listener address                  |
| `--user`      | `-u`  | —              | Basic auth username               |
| `--pass`      | `-p`  | —              | Basic auth password               |
| `--max-conns` | `-M`  | `0` (unlimited) | Max concurrent connections per IP |
| `--browser`   | `-B`  | —              | Open browser on startup           |
| `--log-level` | `-L`  | `info`          | `error`, `warn`, `info`, `debug`  |
| `--log-file`  | `-F`  | stderr          | Append logs to file               |

> The frontend is embedded in the binary and served over HTTP — opening `front_end/index.html` as `file://` will not work (browsers block `fetch()` on local files).

---

## Building from source

### Requirements

- C17 compiler (Clang)
- macOS: `-largp` (handled by Makefile)

### Build

```sh
make                     # release build → ./local-mark
make debug -B O_DEBUG=1  # debug build (-g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION)
make clean
```

No CMake — pure Makefile. Objects go to `build/`.

---

## Features

**Bookmark browser (`#browse`)**

- Sidebar with categories and bookmark counts per category
- Full-text search across title, description, tags, and URL (`Ctrl+K` / `Cmd+K` to focus)
- Tag filter bar per category — multi-select tag pills
- Click a tag on any bookmark card to instantly filter
- Collapsible tag bar (auto-folds past 30 tags)
- Duplicate URLs deduplicated within each category
- Favorites view (⭐ star bookmarks, stored in localStorage)
- Three layouts: single-column, grid, compact

**Database info (`#info`)**

- Total bookmarks, unique URLs, categories, domains, and tags
- Per-category bar chart
- Tag cloud sorted by frequency
- Domain grid with favicon and count — click any domain to search

**Random picker (`#random`)**

- Pick N random bookmarks from all / a category / matching a tag
- Tag autocomplete from database
- "Open all" opens each result in a new tab

**Performance**

- IndexedDB cache — loads instantly on repeat visits, background-refreshes
- No framework, no dependencies at runtime

---

### Logging

```c
LOG_INFO("started on port ", port);
LOG_ERROR("failed: ", strerror(errno));
LOG_DEBUG("bookmark count: ", count);
LOG_PERROR("bind");   // appends perror
```

Compile-time flags: `-DLOG_SHOW_TIME_STAMP` (on by default), `-DLOG_SHOW_SOURCE_LOCATION` (debug build).

---

## Bonus — bookmarkfmt

If you want your `.txt` files to stay neatly column-aligned (so they are easier to read and edit by hand), there is a companion formatter called **`bookmarkfmt`**.

It is not part of this repository. It lives in the author's dotfiles:

**[bookmarkfmt.py — pritam12426/dotfiles](https://github.com/pritam12426/dotfiles/blob/main/unix/bin_scripts/bookmarkfmt.py)**

```sh
mkdir -p ~/.local/bin/
curl -fsSL "https://raw.githubusercontent.com/pritam12426/dotfiles/refs/heads/main/unix/bin_scripts/bookmarkfmt.py" -o ~/.local/bin/bookmarkfmt
chmod +x ~/.local/bin/bookmarkfmt
```

```bash
# Format one file in-place
bookmarkfmt free_time.txt

# Preview without writing
bookmarkfmt --dry-run *.txt

# CI / git pre-commit hook — exits with code 1 if any file needs formatting
bookmarkfmt --check *.txt
```

Before:

```
earthcam | https://www.earthcam.com | See random places | #Cam #Media
oddee | https://www.oddee.com/ | Read stuff | #Blog #Read
akinator | https://en.akinator.com | Guess celebs | #Game
```

After:

```
earthcam | https://www.earthcam.com | See random places | #Cam #Media
oddee    | https://www.oddee.com/   | Read stuff        | #Blog #Read
akinator | https://en.akinator.com  | Guess celebs      | #Game
```

---

## License

[MIT](LICENSE)
