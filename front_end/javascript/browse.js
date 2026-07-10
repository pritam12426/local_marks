// =============================================
// browse.js  —  Bookmark browser view
// =============================================

'use strict';

import { esc, getFavorites, buildCard } from './data.js';

// ── State ──────────────────────────────────
let allCategories  = [];
let searchIndex    = [];
let activeCategory = 0;
let activeTags     = new Set();
let searchQuery    = '';
let tagBarExpanded = false;
let renderSidebar  = null; // filled below
let renderPanel    = null;

// ── DOM refs ──
let elSearch, elClear, elCatList, elSidebarCount,
    elPanelTitle, elTagBar, elBookmarkList, elHeaderTitle;

export function initBrowse(data) {
	elSearch       = document.getElementById('search-input');
	elClear        = document.getElementById('clear-search');
	elCatList      = document.getElementById('category-list');
	elSidebarCount = document.getElementById('sidebar-count');
	elPanelTitle   = document.getElementById('panel-title');
	elTagBar       = document.getElementById('tag-bar');
	elBookmarkList = document.getElementById('bookmark-list');
	elHeaderTitle  = document.getElementById('header-title');

	allCategories = data.book_Marks || data.categories || [];

	buildSearchIndex();
	updateHeaderCount();
	renderSidebar();
	bindEvents();
}

export function updateBrowseData(data) {
	allCategories = data.book_Marks || data.categories || [];
	buildSearchIndex();
	updateHeaderCount();
	renderSidebar();
	renderPanel();
}

export function renderBrowse() {
	const q = elSearch.value.trim();
	if (q) {
		searchQuery = q;
		elClear.classList.add('visible');
		elCatList.querySelectorAll('li').forEach(li => li.classList.remove('active'));
		renderSearch(q);
	} else if (searchQuery) {
		renderSearch(searchQuery);
	} else {
		renderPanel();
	}
}

// ── Data ───────────────────────────────────

function buildSearchIndex() {
	searchIndex = allCategories.flatMap(cat =>
		(cat.bookmarks || []).map(bm => ({
			category: cat.category,
			bookmark: bm,
			text: [bm.title, bm.description, bm.url, ...(bm.tags || [])]
				.filter(Boolean)
				.join(' ')
				.toLowerCase()
		}))
	);
}

function updateHeaderCount() {
	const total = allCategories.reduce((s, c) => s + (c.bookmarks || []).length, 0);
	if (elHeaderTitle) elHeaderTitle.textContent = `📚 ${total} Bookmarks`;
}

// ── Favorites count helper ──

function getFavoritesCount() {
	const favUrls = new Set(getFavorites());
	const seen    = new Set();
	let count     = 0;
	for (const cat of allCategories) {
		for (const bm of (cat.bookmarks || [])) {
			if (favUrls.has(bm.url) && !seen.has(bm.url)) {
				seen.add(bm.url);
				count++;
			}
		}
	}
	return count;
}

// ── Sidebar ────────────────────────────────

renderSidebar = function() {
	const favCount = getFavoritesCount();
	if (activeCategory === -1 && !favCount) activeCategory = 0;
	elSidebarCount.textContent = allCategories.length;

	const frag = document.createDocumentFragment();

	if (favCount) {
		const li = document.createElement('li');
		li.innerHTML = `<span class="cat-label">⭐ Favorites</span><span class="cat-badge">${favCount}</span>`;
		if (activeCategory === -1) li.classList.add('active');
		li.addEventListener('click', () => {
			if (searchQuery) clearSearch();
			activeCategory = -1;
			activeTags.clear();
			tagBarExpanded = false;
			highlightSidebar(-1);
			renderPanel();
		});
		frag.appendChild(li);
	}

	allCategories.forEach((cat, i) => {
		const li = document.createElement('li');
		li.innerHTML = `
			<span class="cat-label">📋 ${esc(cat.category)}</span>
			<span class="cat-badge">${(cat.bookmarks || []).length}</span>
		`;
		if (i === activeCategory) li.classList.add('active');

		li.addEventListener('click', () => {
			if (searchQuery) clearSearch();
			activeCategory = i;
			activeTags.clear();
			tagBarExpanded = false;
			highlightSidebar(i);
			renderPanel();
		});

		frag.appendChild(li);
	});

	elCatList.innerHTML = '';
	elCatList.appendChild(frag);
};

function highlightSidebar(index) {
	const hasFavs = getFavoritesCount() > 0;
	elCatList.querySelectorAll('li').forEach((li, i) => {
		const targetIdx = hasFavs ? i - 1 : i;
		li.classList.toggle('active', targetIdx === index);
	});
}

// ── Panel ──────────────────────────────────

renderPanel = function() {
	if (searchQuery) { renderSearch(searchQuery); return; }

	if (activeCategory === -1) {
		const favUrls = getFavorites();
		let bookmarks = allCategories.flatMap(cat =>
			(cat.bookmarks || []).filter(bm => favUrls.includes(bm.url))
		);
		const seen = new Set();
		bookmarks = bookmarks.filter(bm => {
			if (seen.has(bm.url)) return false;
			seen.add(bm.url);
			return true;
		});

		elPanelTitle.innerHTML = `⭐ Favorites <span class="panel-count">(${bookmarks.length})</span>`;
		const allTags = [...new Set(bookmarks.flatMap(bm => bm.tags || []))].sort();
		renderTagBar(allTags);
		renderCards(bookmarks, elBookmarkList);
		return;
	}

	const cat = allCategories[activeCategory];
	if (!cat) return;

	const seen = new Set();
	const bookmarks = (cat.bookmarks || []).filter(bm => {
		if (seen.has(bm.url)) return false;
		seen.add(bm.url);
		return true;
	});

	const filtered = activeTags.size
		? bookmarks.filter(bm =>
			[...activeTags].every(t => (bm.tags || []).includes(t))
		  )
		: bookmarks;

	const countLabel = filtered.length !== bookmarks.length
		? `${filtered.length} of ${bookmarks.length}`
		: filtered.length;

	elPanelTitle.innerHTML = `
		📋 ${esc(cat.category)}
		<span class="panel-count">(${countLabel})</span>
	`;

	const allTags = [...new Set(bookmarks.flatMap(bm => bm.tags || []))].sort();
	renderTagBar(allTags);
	renderCards(filtered, elBookmarkList);
};

// ── Tag bar ────────────────────────────────

function renderTagBar(tags) {
	elTagBar.innerHTML = '';
	if (!tags.length) return;

	const INITIAL_COUNT = 30;
	const frag = document.createDocumentFragment();

	const label = document.createElement('span');
	label.className   = 'tag-bar-label';
	label.textContent = 'Tags:';
	frag.appendChild(label);

	const visibleTags = tagBarExpanded ? tags : tags.slice(0, INITIAL_COUNT);

	visibleTags.forEach(tag => {
		const pill = document.createElement('span');
		pill.className   = 'tag-pill' + (activeTags.has(tag) ? ' active' : '');
		pill.textContent = tag;
		pill.addEventListener('click', () => {
			activeTags.has(tag) ? activeTags.delete(tag) : activeTags.add(tag);
			renderPanel();
		});
		frag.appendChild(pill);
	});

	if (tags.length > INITIAL_COUNT) {
		const toggle = document.createElement('button');
		toggle.className   = 'tag-clear';
		toggle.textContent = tagBarExpanded ? '▼ Show less' : `▶ +${tags.length - INITIAL_COUNT} more`;
		toggle.addEventListener('click', () => {
			tagBarExpanded = !tagBarExpanded;
			renderTagBar(tags);
		});
		frag.appendChild(toggle);
	}

	if (activeTags.size) {
		const clr = document.createElement('button');
		clr.className   = 'tag-clear';
		clr.textContent = 'Clear filters';
		clr.addEventListener('click', () => { activeTags.clear(); renderPanel(); });
		frag.appendChild(clr);
	}

	elTagBar.appendChild(frag);
}

// ── Cards ──────────────────────────────────

function renderCards(bookmarks, container) {
	container.innerHTML = '';

	if (!bookmarks.length) {
		container.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">🔍</div>
				<p>No bookmarks match your filters.</p>
			</div>`;
		return;
	}

	const frag = document.createDocumentFragment();
	bookmarks.forEach(bm => frag.appendChild(buildCard(bm, {
		tagClickable: true,
		onTagClick: tag => { activeTags.add(tag); renderPanel(); }
	})));
	container.appendChild(frag);
}

// ── Search ─────────────────────────────────

function renderSearch(query) {
	const q       = query.toLowerCase();
	const results = searchIndex.filter(item => item.text.includes(q));

	elPanelTitle.innerHTML = `
		🔍 Results for <em style="color:var(--accent)">"${esc(query)}"</em>
		<span class="panel-count">(${results.length})</span>
	`;
	elTagBar.innerHTML = '';

	if (!results.length) {
		elBookmarkList.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">📭</div>
				<p>No bookmarks found for <strong>${esc(query)}</strong>.</p>
			</div>`;
		return;
	}

	const groups = new Map();
	results.forEach(({ category, bookmark }) => {
		if (!groups.has(category)) groups.set(category, []);
		groups.get(category).push(bookmark);
	});

	const frag = document.createDocumentFragment();
	groups.forEach((bms, catName) => {
		const header = document.createElement('div');
		header.className   = 'search-group-header';
		header.textContent = `📋 ${catName}`;
		frag.appendChild(header);

		const seen = new Set();
		bms.forEach(bm => {
			if (seen.has(bm.url)) return;
			seen.add(bm.url);
			frag.appendChild(buildCard(bm, {
				tagClickable: true,
				onTagClick: tag => { activeTags.add(tag); renderPanel(); }
			}));
		});
	});

	elBookmarkList.innerHTML = '';
	elBookmarkList.appendChild(frag);
}

// ── Events ─────────────────────────────────

function bindEvents() {
	elSearch.addEventListener('input', () => {
		searchQuery = elSearch.value.trim();
		elClear.classList.toggle('visible', searchQuery.length > 0);

		if (!searchQuery) {
			highlightSidebar(activeCategory);
			renderPanel();
		} else {
			elCatList.querySelectorAll('li').forEach(li => li.classList.remove('active'));
			renderSearch(searchQuery);
		}
	});

	elClear.addEventListener('click', clearSearch);

	window.addEventListener('favorites-changed', () => {
		const wasFavView = activeCategory === -1;
		renderSidebar();
		if (wasFavView) renderPanel();
	});

	document.addEventListener('keydown', e => {
		if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
			e.preventDefault();
			elSearch.focus();
			elSearch.select();
		}
		if (e.key === 'Escape' && document.activeElement === elSearch) {
			clearSearch();
		}
	});
}

function clearSearch() {
	elSearch.value = '';
	searchQuery    = '';
	elClear.classList.remove('visible');
	activeTags.clear();
	if (activeCategory === -1) {
		elCatList.querySelectorAll('li').forEach(li => li.classList.remove('active'));
		const first = elCatList.firstElementChild;
		if (first) first.classList.add('active');
	} else {
		highlightSidebar(activeCategory);
	}
	renderPanel();
	elSearch.blur();
}
