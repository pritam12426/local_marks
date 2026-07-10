#!/usr/bin/env python3

import argparse
import json
import re
import sys
from pathlib import Path
from urllib.parse import urlparse


def excepthook(exc_type, exc_value, exc_tb):
	if exc_type is KeyboardInterrupt:
		print("\nInterrupted by user.", end="")
		return
	sys.__excepthook__(exc_type, exc_value, exc_tb)

sys.excepthook = excepthook

# NOTE: icon re-fetching is skipped for unchanged entries during `update`
# (see parse_bookmark_line's existing_by_url / icon_stats["skipped"] logic).
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


# ── Parsing ────────────────────────────────────────────────────────────────────
def parse_bookmark_line(
	line: str,
	fetch_icons: bool,
	domain_counter: dict[str, int],
	tag_counter: dict[str, int],
	icon_stats: dict[str, int] | None = None,
	existing_by_url: dict[str, dict] | None = None,
) -> dict | None:
	"""Parse one pipe-separated bookmark line. Returns None if the line should be skipped."""
	if not line or line.strip().startswith("#"):
		return None

	if "http://" not in line and "https://" not in line:
		return None

	parts = [x.strip() for x in line.strip().split("|") if x.strip()]

	if len(parts) > 4:
		return None

	title       = ""
	url         = ""
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

	# Update tag counters
	for tag in tags:
		tag_counter[tag] = tag_counter.get(tag, 0) + 1

	# Update domain counters
	domain = get_domain(url)
	if domain:
		domain_counter[domain] = domain_counter.get(domain, 0) + 1

	entry: dict = {
		"title":       title,
		"url":         url,
		"description": description,
		"tags":        tags,
		"domain":      domain,
	}

	if fetch_icons and url.startswith("https://www.youtube.com/@"):
		# If this exact entry already exists in the database (same title/description/
		# tags), there's nothing new to fetch — reuse the icon it already has instead
		# of hitting the network again.
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
		else:
			print(f"     🛜  Fetching icon for '{url}'")
			try:
				icon_url = get_channel_icon_url(url)
				if icon_url:
					entry["icon"] = icon_url
					if icon_stats is not None:
						icon_stats["fetched"] = icon_stats.get("fetched", 0) + 1
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
				line, fetch_icons, domain_counter, tag_counter,
				icon_stats=icon_stats, existing_by_url=existing_by_url,
			)
			if bookmark:
				bookmarks.append(bookmark)

		if bookmarks:
			categories.append({"category": category_name, "bookmarks": bookmarks})
			print(f"   📄 {file_path.name}  ({len(bookmarks)} bookmarks)")

	return categories


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
		failed  = icon_stats.get("failed", 0)
		if fetched or skipped or failed:
			print(f"   🖼️  Icons      : {fetched} fetched, {skipped} skipped (unchanged), {failed} failed")
	print(f"   💾 Saved to   : {output_file}")

def bookmark_sort_key(bm: dict) -> str:
	return (
		bm.get("title")
		or bm.get("description")
		or bm.get("url")
		or ""
	).strip().lower()


def sort_database(book_marks: list[dict]) -> None:
	"""Sort bookmarks within each category and categories by name, in-place."""
	for category in book_marks:
		category["bookmarks"].sort(key=bookmark_sort_key)

	book_marks.sort(
		key=lambda cat: cat.get("category", "").strip().lower()
	)

# ── Subcommands ────────────────────────────────────────────────────────────────

def cmd_create(args: argparse.Namespace) -> None:
	"""Build a fresh database from the given .txt files."""
	output: Path = args.to
	output.parent.mkdir(parents=True, exist_ok=True)

	print("🚀 Creating bookmark database…")

	domain_counter: dict[str, int] = {}
	tag_counter:    dict[str, int] = {}
	icon_stats:     dict[str, int] = {}

	book_marks = process_files(args.files, args.icon, domain_counter, tag_counter, icon_stats)

	sort_database(book_marks)

	final_data = {
		"book_Marks":            book_marks,
		"book_mark_domain_hash": domain_counter,
		"book_mark_tag_hash":    tag_counter,
	}

	output.write_text(json.dumps(final_data, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
	print_summary(book_marks, domain_counter, tag_counter, output, icon_stats)


def cmd_update(args: argparse.Namespace) -> None:
	"""Append bookmarks from the given .txt files into an existing database.

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

	book_marks:     list[dict]     = existing.get("book_Marks", [])
	domain_counter: dict[str, int] = existing.get("book_mark_domain_hash", {})
	tag_counter:    dict[str, int] = existing.get("book_mark_tag_hash", {})

	def bookmark_key(bookmark: dict) -> str:
		return json.dumps(bookmark, sort_keys=True, ensure_ascii=False)

	# Index existing bookmarks by URL (used both to skip unnecessary icon
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
	# Scratch counters: parse_bookmark_line needs somewhere to tally domain/tag
	# occurrences while scanning, but we don't want duplicates or unchanged
	# entries inflating the real counters — those get adjusted below, only for
	# bookmarks we actually add or update.
	scratch_domain_counter: dict[str, int] = {}
	scratch_tag_counter:    dict[str, int] = {}
	new_categories = process_files(
		args.files, args.icon, scratch_domain_counter, scratch_tag_counter,
		icon_stats=icon_stats, existing_by_url=existing_by_url,
	)

	def _adjust_counters(bm: dict, delta: int) -> None:
		domain = bm.get("domain")
		if domain:
			domain_counter[domain] = domain_counter.get(domain, 0) + delta
			if domain_counter[domain] <= 0:
				domain_counter.pop(domain, None)
		for tag in bm.get("tags", []):
			tag_counter[tag] = tag_counter.get(tag, 0) + delta
			if tag_counter[tag] <= 0:
				tag_counter.pop(tag, None)

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
				_adjust_counters(old, -1)   # remove old domain/tag contribution
				old.update(bm)
				_adjust_counters(old, +1)   # add refreshed contribution
				known_bookmarks.add(bookmark_key(old))
				updated_total += 1
				print(f"   ✏️  Updated: {bm.get('title') or bm['url']}")
				continue

			# Brand-new URL
			_adjust_counters(bm, +1)
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
			book_marks.append({
				"category": new_cat["category"],
				"bookmarks": added_bms,
			})
			print(f"   🆕 New category '{new_cat['category']}': {len(added_bms)} bookmark(s)")

	if added_total == 0 and updated_total == 0:
		print("   ℹ️  No changes (all bookmarks already exist and are up to date).")
		return

	sort_database(book_marks)

	final_data = {
		"book_Marks":            book_marks,
		"book_mark_domain_hash": domain_counter,
		"book_mark_tag_hash":    tag_counter,
	}

	output.write_text(
		json.dumps(final_data, indent="\t", ensure_ascii=False) + "\n",
		encoding="utf-8",
	)

	print_summary(book_marks, domain_counter, tag_counter, output, icon_stats, updated_total)

parser = argparse.ArgumentParser(prog="marks2json", description="Convert bookmark .txt files into a JSON database")
parser.add_argument("-I", "--icon", action="store_true", help="Fetch channel icons (requires network)")

subparsers = parser.add_subparsers(dest="command", required=True)

# ── create ──
create_parser = subparsers.add_parser("create", help="Create a new bookmark database from .txt files")
create_parser.add_argument("files", type=Path, nargs="+", metavar="FILE", help="One or more .txt bookmark files")
create_parser.add_argument("-T", "--to", type=Path, default=Path("bookmarks.json"), metavar="DB", help="Output JSON file (default: bookmarks.json)")
create_parser.set_defaults(func=cmd_create)

# ── append ──
append_parser = subparsers.add_parser("update", help="Append bookmarks from .txt files into an existing database")
append_parser.add_argument("files", type=Path, nargs="+", metavar="FILE", help="One or more .txt bookmark files")
append_parser.add_argument("-T", "--to", type=Path, required=True, metavar="DB", help="Path to the existing JSON database")
append_parser.add_argument("-O", "--override", action="store_true",help="Update existing bookmarks (matched by URL) if the incoming line differs, instead of skipping them")
append_parser.set_defaults(func=cmd_update)


args   = parser.parse_args()
args.func(args)
