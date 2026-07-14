#!/usr/bin/env python3

import argparse
import json
import os
import re
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
from urllib.parse import urlparse


def excepthook(exc_type, exc_value, exc_tb):
	if exc_type is KeyboardInterrupt:
		print("\nInterrupted by user.", end="")
		return
	sys.__excepthook__(exc_type, exc_value, exc_tb)


sys.excepthook = excepthook

# Persistent on-disk cache of already-fetched YouTube channel icons, keyed by
# channel URL. Avoids re-hitting the network for a channel we've seen before,
# even across separate `create`/`update` runs or different .txt files.
ICON_CACHE_FILE = (
	Path(os.environ["LOCALAPPDATA"]) / "marks2json" / "icons.json"
	if os.name == "nt"
	else Path.home() / ".cache" / "marks2json_icons.json"
)


def load_icon_cache() -> dict[str, str]:
	try:
		return json.loads(ICON_CACHE_FILE.read_text(encoding="utf-8"))
	except Exception:
		return {}


def save_icon_cache(cache: dict[str, str]) -> None:
	try:
		ICON_CACHE_FILE.parent.mkdir(parents=True, exist_ok=True)
		ICON_CACHE_FILE.write_text(
			json.dumps(cache, indent="\t", ensure_ascii=False) + "\n",
			encoding="utf-8",
		)
	except Exception as exc:
		print(f"     ⚠️  Could not save icon cache: {exc}")


def get_channel_icon_url(channel_url: str) -> str | None:
	import requests

	headers = {
		"User-Agent": (
			"Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
			"AppleWebKit/537.36 (KHTML, like Gecko) "
			"Chrome/120.0.0.0 Safari/537.36"
		),
	}
	response = requests.get(channel_url, headers=headers, timeout=5)
	html = response.text

	match = re.search(r"ytInitialData\s*=\s*(\{.*?\});</script>", html, re.DOTALL)
	if not match:
		raise ValueError("Could not find ytInitialData in page")

	data = json.loads(match.group(1))

	avatar = (
		data["header"]
		["pageHeaderRenderer"]
		["content"]
		["pageHeaderViewModel"]
		["image"]
		["decoratedAvatarViewModel"]
		["avatar"]
		["avatarViewModel"]
		["image"]
		["sources"]
	)

	return avatar[-1]["url"]


# ── Helpers ────────────────────────────────────────────────────────────────────

def format_category_name(filename: str) -> str:
	"""Format a filename stem into a readable category name."""
	name = Path(filename).stem.replace("_", " ")
	return " ".join(word.capitalize() for word in name.split())


def get_domain(url: str) -> str:
	"""Extract the bare domain from a URL."""
	try:
		host = urlparse(url).hostname or ""
		return host.removeprefix("www.").lower()
	except Exception:
		return ""


def bookmark_sort_key(bm: dict) -> str:
	return (
		bm.get("title") or bm.get("description") or bm.get("url") or ""
	).strip().lower()


def sort_database(book_marks: list[dict]) -> None:
	"""Sort bookmarks within each category and categories by name, in-place."""
	for category in book_marks:
		category["bookmarks"].sort(key=bookmark_sort_key)
	book_marks.sort(key=lambda cat: cat.get("category", "").strip().lower())


def adjust_counters(
	bm: dict,
	domain_counter: dict[str, int],
	tag_counter: dict[str, int],
	delta: int,
) -> None:
	"""Adjust domain/tag counters by delta (+1 or -1)."""
	domain = bm.get("domain")
	if domain:
		domain_counter[domain] = domain_counter.get(domain, 0) + delta
		if domain_counter[domain] <= 0:
			domain_counter.pop(domain, None)
	for tag in bm.get("tags", []):
		tag_counter[tag] = tag_counter.get(tag, 0) + delta
		if tag_counter[tag] <= 0:
			tag_counter.pop(tag, None)


def recalc_counters(categories: list[dict]) -> tuple[dict[str, int], dict[str, int]]:
	"""Recalculate domain and tag counters from categories."""
	domain_counter: dict[str, int] = {}
	tag_counter: dict[str, int] = {}
	for cat in categories:
		for bm in cat["bookmarks"]:
			domain = bm.get("domain")
			if domain:
				domain_counter[domain] = domain_counter.get(domain, 0) + 1
			for tag in bm.get("tags", []):
				tag_counter[tag] = tag_counter.get(tag, 0) + 1
	return domain_counter, tag_counter


def print_summary(
	book_marks: list[dict],
	domain_counter: dict,
	tag_counter: dict,
	output_file: Path,
	icon_stats: dict[str, int] | None = None,
	updated_total: int | None = None,
) -> None:
	total = sum(len(c["bookmarks"]) for c in book_marks)
	print("\n✅  Done!")
	print(f"   📊 Categories : {len(book_marks)}")
	print(f"   📎 Bookmarks  : {total}")
	print(f"   🌐 Domains    : {len(domain_counter)}")
	print(f"   🏷️  Tags       : {len(tag_counter)}")
	if updated_total is not None:
		print(f"   ✏️  Updated    : {updated_total}")
	if icon_stats:
		fetched = icon_stats.get("fetched", 0)
		skipped = icon_stats.get("skipped", 0)
		cached = icon_stats.get("cached", 0)
		failed = icon_stats.get("failed", 0)
		if fetched or skipped or cached or failed:
			print(
				f"   🖼️  Icons      : {fetched} fetched, {cached} from cache, "
				f"{skipped} skipped (unchanged), {failed} failed"
			)
	print(f"   💾 Saved to   : {output_file}")


# ── Parsing ────────────────────────────────────────────────────────────────────

def parse_bookmark_line(
	line: str,
	fetch_icons: bool,
	domain_counter: dict[str, int],
	tag_counter: dict[str, int],
	icon_stats: dict[str, int] | None = None,
	existing_by_url: dict[str, dict] | None = None,
	icon_cache: dict[str, str] | None = None,
) -> dict | None:
	"""Parse one pipe-separated bookmark line. Returns None if the line should be skipped."""
	if not line or line.strip().startswith("#"):
		return None

	if "http://" not in line and "https://" not in line:
		return None

	parts = [x.strip() for x in line.strip().split("|") if x.strip()]

	if len(parts) > 4:
		return None

	title = ""
	url = ""
	description = ""
	tags: list[str] = []

	for part in parts:
		if part.startswith(("http://", "https://")):
			url = part
		elif part.startswith("#"):
			tags.extend(
				t.strip()
				for t in part.split()
				if t.startswith("#") and t.removeprefix("#").strip()
			)
		elif not title:
			title = part
		elif not description:
			description = part

	if not url:
		return None

	for tag in tags:
		tag_counter[tag] = tag_counter.get(tag, 0) + 1

	domain = get_domain(url)
	if domain:
		domain_counter[domain] = domain_counter.get(domain, 0) + 1

	entry: dict = {
		"title": title,
		"url": url,
		"description": description,
		"tags": tags,
		"domain": domain,
	}

	if fetch_icons and url.startswith("https://www.youtube.com/@"):
		old = (existing_by_url or {}).get(url)
		unchanged = (
			old is not None
			and old.get("title") == title
			and old.get("description") == description
			and old.get("tags") == tags
		)

		if unchanged and old.get("icon"):
			entry["icon"] = old["icon"]
			if icon_stats is not None:
				icon_stats["skipped"] = icon_stats.get("skipped", 0) + 1
		elif icon_cache is not None and url in icon_cache:
			entry["icon"] = icon_cache[url]
			if icon_stats is not None:
				icon_stats["cached"] = icon_stats.get("cached", 0) + 1
			print(f"     💾  Using cached icon for '{url}'")
		else:
			print(f"     🛜  Fetching icon for '{url}'")
			try:
				icon_url = get_channel_icon_url(url)
				if icon_url:
					entry["icon"] = icon_url
					if icon_cache is not None:
						icon_cache[url] = icon_url
					if icon_stats is not None:
						icon_stats["fetched"] = icon_stats.get("fetched", 0) + 1
					print(f"     ✅  Fetched icon for '{url}'")
			except Exception as exc:
				print(f"     ⚠️  Could not fetch icon: {exc}")
				if icon_stats is not None:
					icon_stats["failed"] = icon_stats.get("failed", 0) + 1

	return entry


def process_files(
	files: list[Path],
	fetch_icons: bool,
	domain_counter: dict[str, int],
	tag_counter: dict[str, int],
	icon_stats: dict[str, int] | None = None,
	existing_by_url: dict[str, dict] | None = None,
	icon_cache: dict[str, str] | None = None,
) -> list[dict]:
	"""Read a list of .txt files and return a list of category dicts."""
	categories: list[dict] = []

	for file_path in sorted(files):
		if not file_path.exists():
			print(f"   ⚠️  File not found, skipping: {file_path}")
			continue

		category_name = format_category_name(file_path.name)
		bookmarks: list[dict] = []

		for line in file_path.read_text(encoding="utf-8").splitlines():
			bookmark = parse_bookmark_line(
				line,
				fetch_icons,
				domain_counter,
				tag_counter,
				icon_stats=icon_stats,
				existing_by_url=existing_by_url,
				icon_cache=icon_cache,
			)
			if bookmark:
				bookmarks.append(bookmark)

		if bookmarks:
			categories.append({"category": category_name, "bookmarks": bookmarks})
			print(f"   📄 {file_path.name}  ({len(bookmarks)} bookmarks)")

	return categories


# ── Subcommands: create / update ───────────────────────────────────────────────

def cmd_create(args: argparse.Namespace) -> None:
	"""Build a fresh database from the given .txt files."""
	output: Path = args.to
	output.parent.mkdir(parents=True, exist_ok=True)

	print("🚀 Creating bookmark database…")

	domain_counter: dict[str, int] = {}
	tag_counter: dict[str, int] = {}
	icon_stats: dict[str, int] = {}
	icon_cache: dict[str, str] = load_icon_cache() if args.icon else {}

	book_marks = process_files(
		args.files, args.icon, domain_counter, tag_counter, icon_stats, icon_cache=icon_cache
	)

	sort_database(book_marks)

	final_data = {
		"book_Marks": book_marks,
		"book_mark_domain_hash": domain_counter,
		"book_mark_tag_hash": tag_counter,
	}

	output.write_text(json.dumps(final_data, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
	if args.icon:
		save_icon_cache(icon_cache)
	print_summary(book_marks, domain_counter, tag_counter, output, icon_stats)


def cmd_update(args: argparse.Namespace) -> None:
	"""Append bookmarks from .txt files into an existing database.

	By default, only brand-new bookmarks (not already present, matched by URL)
	are added. With --override, bookmarks whose URL already exists in the
	database get their title/description/tags/icon refreshed if the incoming
	line differs from what's stored.
	"""
	output: Path = args.to

	if not output.exists():
		raise SystemExit(f"❌ Database not found: {output}\n   Use 'create' to start a new one.")

	print(f"📂 Loading existing database: {output}")
	existing = json.loads(output.read_text(encoding="utf-8"))

	book_marks: list[dict] = existing.get("book_Marks", [])
	domain_counter: dict[str, int] = existing.get("book_mark_domain_hash", {})
	tag_counter: dict[str, int] = existing.get("book_mark_tag_hash", {})

	def bookmark_key(bm: dict) -> str:
		return json.dumps(bm, sort_keys=True, ensure_ascii=False)

	# Index existing bookmarks by URL (used to skip unnecessary icon
	# re-fetches and, with --override, to update entries in place).
	existing_by_url: dict[str, dict] = {
		bm["url"]: bm
		for cat in book_marks
		for bm in cat.get("bookmarks", [])
		if bm.get("url")
	}
	known_bookmarks: set[str] = {bookmark_key(bm) for bm in existing_by_url.values()}

	print("📖 Scanning new files…")
	icon_stats: dict[str, int] = {}
	icon_cache: dict[str, str] = load_icon_cache() if args.icon else {}
	scratch_domain_counter: dict[str, int] = {}
	scratch_tag_counter: dict[str, int] = {}
	new_categories = process_files(
		args.files,
		args.icon,
		scratch_domain_counter,
		scratch_tag_counter,
		icon_stats=icon_stats,
		existing_by_url=existing_by_url,
		icon_cache=icon_cache,
	)

	added_total = 0
	updated_total = 0

	for new_cat in new_categories:
		added_bms: list[dict] = []

		for bm in new_cat["bookmarks"]:
			if bookmark_key(bm) in known_bookmarks:
				continue  # exact duplicate, nothing changed

			old = existing_by_url.get(bm["url"])

			if old is not None:
				if not args.override:
					continue  # URL already known, and we're not allowed to touch it
				adjust_counters(old, domain_counter, tag_counter, -1)
				old.update(bm)
				adjust_counters(old, domain_counter, tag_counter, +1)
				known_bookmarks.add(bookmark_key(old))
				updated_total += 1
				print(f"   ✏️  Updated: {bm.get('title') or bm['url']}")
				continue

			# Brand-new URL
			adjust_counters(bm, domain_counter, tag_counter, +1)
			added_bms.append(bm)
			existing_by_url[bm["url"]] = bm
			known_bookmarks.add(bookmark_key(bm))

		if not added_bms:
			continue

		added_total += len(added_bms)

		existing_cat = next(
			(c for c in book_marks if c["category"] == new_cat["category"]),
			None,
		)

		if existing_cat:
			existing_cat["bookmarks"].extend(added_bms)
			print(f"   ➕ {new_cat['category']}: added {len(added_bms)} bookmark(s)")
		else:
			book_marks.append({"category": new_cat["category"], "bookmarks": added_bms})
			print(f"   🆕 New category '{new_cat['category']}': {len(added_bms)} bookmark(s)")

	if added_total == 0 and updated_total == 0:
		if args.icon:
			save_icon_cache(icon_cache)
		print("   ℹ️  No changes (all bookmarks already exist and are up to date).")
		return

	sort_database(book_marks)

	final_data = {
		"book_Marks": book_marks,
		"book_mark_domain_hash": domain_counter,
		"book_mark_tag_hash": tag_counter,
	}

	output.write_text(
		json.dumps(final_data, indent="\t", ensure_ascii=False) + "\n",
		encoding="utf-8",
	)

	if args.icon:
		save_icon_cache(icon_cache)

	print_summary(book_marks, domain_counter, tag_counter, output, icon_stats, updated_total)


# ── Subcommand: find-dead ──────────────────────────────────────────────────────

def categorize_status(status_code: int | None, error: str | None = None) -> str:
	"""Categorize HTTP status or error into health category."""
	if error or status_code is None:
		return "error"
	if 200 <= status_code < 300:
		return "ok"
	if 300 <= status_code < 400:
		return "redirect"
	if 400 <= status_code < 500:
		return "4xx"
	if 500 <= status_code < 600:
		return "5xx"
	return "error"


def check_url(url: str, timeout: int = 10) -> dict:
	"""Check a single URL with HEAD request, return result dict."""
	import requests

	try:
		headers = {"User-Agent": "Mozilla/5.0 (compatible; marks2json/1.0)"}
		resp = requests.head(url, headers=headers, timeout=timeout, allow_redirects=True)
		return {"url": url, "status": resp.status_code, "category": categorize_status(resp.status_code), "error": None}
	except requests.exceptions.Timeout:
		return {"url": url, "status": None, "category": "error", "error": "Timeout"}
	except requests.exceptions.ConnectionError:
		return {"url": url, "status": None, "category": "error", "error": "Connection error"}
	except requests.exceptions.TooManyRedirects:
		return {"url": url, "status": None, "category": "error", "error": "Too many redirects"}
	except requests.exceptions.RequestException as e:
		return {"url": url, "status": None, "category": "error", "error": str(e)}


def check_all_urls(urls: list[str], concurrency: int = 5, timeout: int = 10, progress_cb=None) -> list[dict]:
	"""Check multiple URLs concurrently with progress callback."""
	results = []
	with ThreadPoolExecutor(max_workers=concurrency) as executor:
		future_to_url = {executor.submit(check_url, url, timeout): url for url in urls}
		for i, future in enumerate(as_completed(future_to_url), 1):
			results.append(future.result())
			if progress_cb:
				progress_cb(future_to_url[future], i, len(urls))
	return results


def print_health_table(results: list[dict], dead_statuses: set[str]) -> None:
	"""Print a formatted health check table to stdout."""
	cats = {"ok": 0, "redirect": 0, "4xx": 0, "5xx": 0, "error": 0}
	for r in results:
		cats[r["category"]] = cats.get(r["category"], 0) + 1

	print(f"\n📊  Link Health Summary: {len(results)} URLs checked")
	print(f"   ✅ OK:          {cats.get('ok', 0)}")
	print(f"   🔄 Redirect:    {cats.get('redirect', 0)}")
	print(f"   ⚠️  Client Err:  {cats.get('4xx', 0)}")
	print(f"   💥 Server Err:  {cats.get('5xx', 0)}")
	print(f"   ❌ Network Err: {cats.get('error', 0)}")

	dead_results = [r for r in results if r["category"] in dead_statuses]
	if dead_results:
		print(f"\n💀 Dead links ({len(dead_results)}):")
		for r in dead_results:
			status_str = f"HTTP {r['status']}" if r.get("status") else r.get("error", "error")
			print(f"   ❌ {r['url']}  →  {status_str}")


def cmd_find_dead(args: argparse.Namespace) -> None:
	"""Check link health in database and optionally create healthy-only database."""
	import requests  # noqa: F401 (used in check_url)

	output: Path = args.to

	if not output.exists():
		raise SystemExit(f"❌ Database not found: {output}")

	print(f"📂 Loading database: {output}")
	existing = json.loads(output.read_text(encoding="utf-8"))
	book_marks: list[dict] = existing.get("book_Marks", [])

	# Collect all unique URLs
	url_to_cats: dict[str, list[str]] = {}
	for category in book_marks:
		for bm in category.get("bookmarks", []):
			url = bm.get("url", "")
			if url:
				url_to_cats.setdefault(url, []).append(category["category"])

	urls = list(url_to_cats.keys())
	if not urls:
		print("   ℹ️  No URLs found in database.")
		return

	print(f"🔍 Checking {len(urls)} unique URLs (concurrency={args.concurrency}, timeout={args.timeout}s)...")

	dead_statuses = {s.strip().lower() for s in args.status.split(",") if s.strip()}

	def progress(url: str, checked: int, total: int) -> None:
		if checked % 10 == 0 or checked == total:
			print(f"   [{checked}/{total}] {url[:80]}")

	results = check_all_urls(urls, concurrency=args.concurrency, timeout=args.timeout, progress_cb=progress)
	print_health_table(results, dead_statuses)

	# If --healthy specified, write new database with only healthy links
	if args.healthy:
		healthy_urls = {r["url"] for r in results if r["category"] not in dead_statuses}

		healthy_categories = []
		for category in book_marks:
			kept = [bm for bm in category.get("bookmarks", []) if bm.get("url", "") in healthy_urls]
			if kept:
				healthy_categories.append({"category": category["category"], "bookmarks": kept})

		domain_counter, tag_counter = recalc_counters(healthy_categories)
		sort_database(healthy_categories)

		final_data = {
			"book_Marks": healthy_categories,
			"book_mark_domain_hash": domain_counter,
			"book_mark_tag_hash": tag_counter,
		}

		healthy_path = args.healthy
		healthy_path.write_text(json.dumps(final_data, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
		total_healthy = sum(len(c["bookmarks"]) for c in healthy_categories)
		print(f"\n✅  Healthy database written to {healthy_path} ({len(healthy_categories)} categories, {total_healthy} bookmarks)")


# ── CLI ────────────────────────────────────────────────────────────────────────

parser = argparse.ArgumentParser(prog="marks2json", description="Convert bookmark .txt files into a JSON database")
parser.add_argument("-I", "--icon", action="store_true", help="Fetch channel icons (requires network)")

subparsers = parser.add_subparsers(dest="command", required=True)

# create
create_parser = subparsers.add_parser("create", help="Create a new bookmark database from .txt files")
create_parser.add_argument("files", type=Path, nargs="+", metavar="FILE", help="One or more .txt bookmark files")
create_parser.add_argument("-T", "--to", type=Path, default=Path("bookmarks.json"), metavar="DB", help="Output JSON file (default: bookmarks.json)")
create_parser.set_defaults(func=cmd_create)

# update
append_parser = subparsers.add_parser("update", help="Append bookmarks from .txt files into an existing database")
append_parser.add_argument("files", type=Path, nargs="+", metavar="FILE", help="One or more .txt bookmark files")
append_parser.add_argument("-T", "--to", type=Path, required=True, metavar="DB", help="Path to the existing JSON database")
append_parser.add_argument("-O", "--override", action="store_true", help="Update existing bookmarks (matched by URL) if the incoming line differs, instead of skipping them")
append_parser.set_defaults(func=cmd_update)

# find-dead
find_dead_parser = subparsers.add_parser("find-dead", help="Check link health in database (HEAD requests)")
find_dead_parser.add_argument("-T", "--to", type=Path, required=True, metavar="DB", help="Path to the existing JSON database")
find_dead_parser.add_argument("--status", default="4xx,5xx,error", metavar="LIST", help="Comma-separated statuses to treat as dead (default: 4xx,5xx,error). Available: ok, redirect, 4xx, 5xx, error")
find_dead_parser.add_argument("--concurrency", type=int, default=5, metavar="N", help="Concurrent requests (default: 5)")
find_dead_parser.add_argument("--timeout", type=int, default=10, metavar="SEC", help="Request timeout in seconds (default: 10)")
find_dead_parser.add_argument("--healthy", type=Path, metavar="FILE", help="Write new database with only healthy links to this file")
find_dead_parser.set_defaults(func=cmd_find_dead)


args = parser.parse_args()
args.func(args)
