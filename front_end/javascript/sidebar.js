// =============================================
// sidebar.js  —  Category sidebar rendering & events
// =============================================

'use strict';

import {esc, fetchDatabases, getActiveDbIndex, getFavorites} from './data.js';

let state = {categories: [], activeCategory: 0, catListEl: null, sidebarCountEl: null, dbInfoEl: null};

// Cache for renderDbInfo per database index
let dbInfoCacheIdx = null;
let dbInfoCacheData = null;

export function initSidebar(cfg)
{
	state.categories     = cfg.categories;
	state.activeCategory = cfg.activeCategory;
	state.catListEl      = cfg.catListEl;
	state.sidebarCountEl = cfg.sidebarCountEl;
	state.dbInfoEl       = cfg.dbInfoEl || null;

	// Set up event delegation once on the container
	if (!state.catListEl.dataset.delegationAttached) {
		state.catListEl.addEventListener('click', onSidebarClick);
		state.catListEl.addEventListener('keydown', onSidebarKeydown);
		state.catListEl.dataset.delegationAttached = '1';
	}
}

function onSidebarClick(e)
{
	const li = e.target.closest('li');
	if (!li) return;
	const idx = Array.from(state.catListEl.children).indexOf(li);
	if (idx === -1) return;

	// Check if this is the Favorites item (first item when activeCategory was -1)
	const hasFavs = getFavoritesCount() > 0;
	if (hasFavs && idx === 0) {
		window.dispatchEvent(new CustomEvent('sidebar-fav-click'));
	} else {
		const catIdx = hasFavs ? idx - 1 : idx;
		window.dispatchEvent(new CustomEvent('sidebar-category-click', {detail: {index: catIdx}}));
	}
}

function onSidebarKeydown(e)
{
	if (e.key !== 'Enter' && e.key !== ' ') return;
	const li = e.target.closest('li');
	if (!li) return;
	e.preventDefault();
	li.click();
}

export function getActiveCategory()
{
	return state.activeCategory;
}
export function setActiveCategory(index)
{
	state.activeCategory = index;
	highlightSidebar(index);
}

export function renderSidebar()
{
	const favCount = getFavoritesCount();
	if (state.activeCategory === -1 && !favCount)
		state.activeCategory = 0;
	
	// Show total bookmark count in sidebar header
	const totalBookmarks = state.categories.reduce((s, c) => s + (c.bookmarks || []).length, 0);
	state.sidebarCountEl.textContent = totalBookmarks;

	const frag = document.createDocumentFragment();

	if (favCount) {
		const li     = document.createElement('li');
		li.innerHTML = `<span class="cat-label">⭐ Favorites</span><span class="cat-badge">${favCount}</span>`;
		if (state.activeCategory === -1)
			li.classList.add('active');
		li.tabIndex = 0;
		li.setAttribute('role', 'button');
		li.setAttribute('aria-label', `Favorites, ${favCount} bookmarks`);
		frag.appendChild(li);
	}

	state.categories.forEach((cat, i) => {
		const li     = document.createElement('li');
		li.innerHTML = `
			<span class="cat-label">📋 ${esc(cat.category)}</span>
			<span class="cat-badge">${(cat.bookmarks || []).length}</span>
		`;
		if (i === state.activeCategory)
			li.classList.add('active');
		li.tabIndex = 0;
		li.setAttribute('role', 'button');
		li.setAttribute('aria-label', `${cat.category}, ${(cat.bookmarks || []).length} bookmarks`);
		frag.appendChild(li);
	});

	state.catListEl.innerHTML = '';
	state.catListEl.appendChild(frag);

	renderDbInfo(totalBookmarks);
}

export function highlightSidebar(index)
{
	const hasFavs = getFavoritesCount() > 0;
	state.catListEl.querySelectorAll('li').forEach((li, i) => {
		const targetIdx = hasFavs ? i - 1 : i;
		li.classList.toggle('active', targetIdx === index);
	});
}

// ── Sidebar DB Info Bar ──

function renderDbInfo(totalBookmarks)
{
	if (!state.dbInfoEl) return;

	const dbIdx = getActiveDbIndex();
	if (dbIdx === null) return;

	// Use cached data if available for this DB
	if (dbInfoCacheIdx === dbIdx && dbInfoCacheData) {
		renderDbInfoHtml(dbInfoCacheData, totalBookmarks);
		return;
	}

	state.dbInfoEl.innerHTML = '<div class="sdbi-skeleton"></div>';

	fetchDatabases().then(payload => {
		const db = (payload.databases || [])[dbIdx];
		if (!db) { state.dbInfoEl.innerHTML = ''; return; }
		dbInfoCacheIdx  = dbIdx;
		dbInfoCacheData = db;
		renderDbInfoHtml(db, totalBookmarks);
	}).catch(() => {
		state.dbInfoEl.innerHTML = '';
	});
}

function renderDbInfoHtml(db, totalBookmarks)
{
	const name = db.file_name || 'bookmarks.json';
	const size = formatBytes(db.file_size);
	const age  = relativeTime(db.mTime_sec);

	state.dbInfoEl.innerHTML = `
		<span class="sdbi-name" title="${esc(db.absolute_path || name)}">🛢️ ${esc(name)}</span>
		<span class="sdbi-meta">${totalBookmarks} bookmarks · ${size} · ${age}</span>
	`;
}

function formatBytes(bytes)
{
	if (!bytes && bytes !== 0) return '—';
	const units = ['B', 'KB', 'MB', 'GB'];
	let i = 0;
	while (bytes >= 1024 && i < units.length - 1) { bytes /= 1024; i++; }
	return `${bytes.toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
}

function relativeTime(unixSec)
{
	if (!unixSec) return '';
	const diff = Math.max(0, Math.floor(Date.now() / 1000) - unixSec);
	if (diff < 60)   return `${diff}s ago`;
	if (diff < 3600)  return `${Math.floor(diff / 60)}m ago`;
	if (diff < 86400) return `${Math.floor(diff / 3600)}h ago`;
	const days = Math.floor(diff / 86400);
	if (days === 1) return '1 day ago';
	if (days < 30)  return `${days} days ago`;
	if (days < 365) return `${Math.floor(days / 30)}mo ago`;
	return `${Math.floor(days / 365)}y ago`;
}

function getFavoritesCount()
{
	const favUrls = new Set(getFavorites());
	const seen    = new Set();
	let   count   = 0;
	for (const cat of state.categories) {
		for (const bm of (cat.bookmarks || [])) {
			if (favUrls.has(bm.url) && !seen.has(bm.url)) {
				seen.add(bm.url);
				count++;
			}
		}
	}
	return count;
}
