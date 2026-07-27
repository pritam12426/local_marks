// =============================================
// data.js  —  Shared data layer & utilities
// =============================================

'use strict';

import {getNote, setNote, deleteNote} from './notes.js';
import {openModal, closeModal} from './modal.js';

// ── Shared IntersectionObserver for lazy favicons ──

let faviconObserver = null;

function getFaviconObserver()
{
	if (!faviconObserver) {
		faviconObserver = new IntersectionObserver(entries => {
			entries.forEach(entry => {
				if (entry.isIntersecting) {
					const img = entry.target;
					img.src   = img.dataset.src;
					img.onload  = () => img.classList.add('loaded');
					img.onerror = () => {
						img.src    = img.dataset.fallback;
						img.onload = () => img.classList.add('loaded');
					};
					faviconObserver.unobserve(img);
				}
			});
		}, {rootMargin: '100px'});
	}
	return faviconObserver;
}

// ── IndexedDB cache (per-database) ─────────

const DB_NAME    = 'LocalMarksCache';
const DB_VERSION = 2;
const STORE_NAME = 'bookmarks';

function openDB()
{
	return new Promise((resolve, reject) => {
		const req           = indexedDB.open(DB_NAME, DB_VERSION);
		req.onupgradeneeded = e => {
			const db = e.target.result;
			if (!db.objectStoreNames.contains(STORE_NAME))
				db.createObjectStore(STORE_NAME, {keyPath: 'id'});
		};
		req.onsuccess = e => resolve(e.target.result);
		req.onerror = () => reject(new Error('IndexedDB open failed'));
	});
}

async function getCached(idx)
{
	try {
		const db                = await openDB();
		const            tx     = db.transaction(STORE_NAME, 'readonly');
		const            store  = tx.objectStore(STORE_NAME);
		const            result = await new Promise((res, rej) => {
            const r     = store.get(`bookmarks:${idx}`);
            r.onsuccess = () => res(r.result || null);
            r.onerror = () => rej(null);
        });
		db.close();
		return result ? result.data : null;
	} catch {
		return null;
	}
}

async function setCache(idx, data)
{
	try {
		const db               = await openDB();
		const            tx    = db.transaction(STORE_NAME, 'readwrite');
		const            store = tx.objectStore(STORE_NAME);
		store.put({id: `bookmarks:${idx}`, data, timestamp: Date.now()});
		db.close();
	} catch { /* cache is optional */
	}
}

// ── Active database (localStorage) ─────────

const ACTIVE_DB_KEY = 'localmarks-active-db';

export function getActiveDbIndex()
{
	const v = localStorage.getItem(ACTIVE_DB_KEY);
	return v ? parseInt(v, 10) : null;
}

export function setActiveDbIndex(idx)
{
	localStorage.setItem(ACTIVE_DB_KEY, String(idx));
}

export async function getActiveDbName()
{
	try {
		const {databases} = await fetchDatabases();
		const idx = getActiveDbIndex();
		if (databases && databases[idx]) {
			return databases[idx].file_name || 'bookmarks.json';
		}
	} catch {
		// ignore
	}
	return 'bookmarks.json';
}

// ── Database list & bookmark fetching ──────

let databasesCache    = null;
let databasesCacheTime = 0;
const DATABASES_CACHE_TTL = 30000; // 30 seconds

export async function fetchDatabases()
{
	const now = Date.now();
	if (databasesCache && (now - databasesCacheTime) < DATABASES_CACHE_TTL)
		return databasesCache;

	const res = await fetch('/api/databases', {cache: 'no-cache'});
	if (!res.ok)
		throw new Error(`HTTP ${res.status}`);
	databasesCache     = await res.json();
	databasesCacheTime = now;
	return databasesCache;
}

export function invalidateDatabasesCache()
{
	databasesCache = null;
}

export async function fetchBookmarks(idx = getActiveDbIndex())
{
	const url = idx > 0 ? `/bookmarks/${idx}.json` : '/bookmarks.json';
	try {
		const res = await fetch(url, {cache: 'no-cache'});
		if (!res.ok)
			throw new Error(`HTTP ${res.status}`);
		const data = await res.json();
		setCache(idx, data);
		return data;
	} catch (err) {
		const cached = await getCached(idx);
		if (cached) {
			console.warn('⚠️ Network fetch failed, using cached bookmarks:', err.message);
			return cached;
		}
		throw err;
	}
}

// ── HTML escape ────────────────────────────

export function esc(str)
{
	return String(str ?? '')
	    .replace(/&/g, '&amp;')
	    .replace(/</g, '&lt;')
	    .replace(/>/g, '&gt;')
	    .replace(/"/g, '&quot;');
}

// ── Favorites (localStorage) ───────────────

const FAVORITES_KEY = 'localmarks-favorites';

let favoritesCache = null;

function getFavoritesArray()
{
	try {
		return JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]');
	} catch {
		return [];
	}
}

export function getFavorites()
{
	return getFavoritesArray();
}

function getFavoritesSet()
{
	if (!favoritesCache) {
		favoritesCache = new Set(getFavoritesArray());
	}
	return favoritesCache;
}

export function toggleFavorite(url)
{
	let   favs = getFavoritesArray();
	const idx  = favs.indexOf(url);
    idx === -1 ? favs.push(url) : favs.splice(idx, 1);
	localStorage.setItem(FAVORITES_KEY, JSON.stringify(favs));
	favoritesCache = null; // invalidate
	window.dispatchEvent(new CustomEvent('favorites-changed'));
}

export function isFavorite(url)
{
	return getFavoritesSet().has(url);
}

// ── Theme (localStorage + system preference) ─────────────────

const THEME_KEY = 'localmarks-theme';

export function getTheme()
{
	const stored = localStorage.getItem(THEME_KEY);
	if (stored)
		return stored;
	return window.matchMedia('(prefers-color-scheme: light)').matches ? 'light' : 'dark';
}

export function setTheme(mode)
{
	localStorage.setItem(THEME_KEY, mode);
	document.documentElement.setAttribute('data-theme', mode);
	updateThemeToggleIcon(mode);
}

export function toggleTheme()
{
	const current = getTheme();
	const next    = current === 'dark' ? 'light' : 'dark';
	setTheme(next);
}

function updateThemeToggleIcon(theme)
{
	const btns = document.querySelectorAll('.theme-toggle');
	btns.forEach(btn => {
		btn.textContent = theme === 'dark' ? '☀️' : '🌙';
		btn.setAttribute('aria-label',
		                 theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme');
	});
}

export function initTheme()
{
	const theme = getTheme();
	document.documentElement.setAttribute('data-theme', theme);
	updateThemeToggleIcon(theme);

	const toggles = document.querySelectorAll('.theme-toggle');
	toggles.forEach(toggle => {
		toggle.addEventListener('click', toggleTheme);
	});

	// Listen for system preference changes
	const mediaQuery = window.matchMedia('(prefers-color-scheme: light)');
	const handler = (e) => {
		if (!localStorage.getItem(THEME_KEY)) {
			const newTheme = e.matches ? 'light' : 'dark';
			setTheme(newTheme);
		}
	};
	mediaQuery.addEventListener('change', handler);

	// Return cleanup function
	return () => {
		toggles.forEach(toggle => toggle.removeEventListener('click', toggleTheme));
		mediaQuery.removeEventListener('change', handler);
	};
}

// ── Layout (localStorage) ──────────────────

const LAYOUT_KEY = 'localmarks-layout';

export function getLayout()
{
	return localStorage.getItem(LAYOUT_KEY) || 'single';
}
export function setLayout(mode)
{
	localStorage.setItem(LAYOUT_KEY, mode);
}

// ── Sidebar width (localStorage) ───────────

const SIDEBAR_W_KEY = 'localmarks-sidebar-w';

export function getSidebarWidth()
{
	const v = localStorage.getItem(SIDEBAR_W_KEY);
	return v ? parseInt(v, 10) : null;  // null → fall back to the CSS default
}

export function setSidebarWidth(px)
{
	if (px == null)
		localStorage.removeItem(SIDEBAR_W_KEY);
	else
		localStorage.setItem(SIDEBAR_W_KEY, String(px));
}

// ── Bookmark card builder ──────────────────

export function buildCard(bm, {tagClickable, onTagClick} = {})
{
	const a     = document.createElement('a');
	a.className = 'bookmark-card';
	a.dataset.url = bm.url;
	a.href      = '/redirect?url=' + encodeURIComponent(bm.url)
	               + '&db=' + getActiveDbIndex()
	               + '&title=' + encodeURIComponent(bm.title || bm.url);
	a.target    = '_blank';
	a.rel       = 'noopener noreferrer';

	const domain       = bm.domain;
	const displayTitle = bm.title || bm.description || bm.url;
	const faviconSrc   = bm.icon || `https://www.google.com/s2/favicons?sz=64&domain=${domain}`;
	const fallbackSrc  = `https://www.google.com/s2/favicons?sz=64&domain=${domain}`;
	const starred      = isFavorite(bm.url);
	const hasNotes     = hasNote(bm.url);

	a.innerHTML = `
		<span class="bm-star ${starred ? 'active' : ''}" data-url="${esc(bm.url)}">${
		starred ? '★' : '☆'}</span>
		<span class="bm-note-indicator ${hasNotes ? 'has-note' : ''}" data-url="${esc(bm.url)}" title="Add note">📝</span>
		<img class="bm-favicon" data-lazy="true" data-src="${esc(faviconSrc)}" alt=""
			data-fallback="${esc(fallbackSrc)}">
		<div class="bm-body">
			<div class="bm-title">${esc(displayTitle)}</div>
			${
		bm.description && bm.description !== displayTitle
			? `<div class="bm-desc">${esc(bm.description)}</div>`
			: ''}
			${
		(bm.tags || []).length
			? `<div class="bm-tags">${
				  bm.tags.map(t => `<span class="bm-tag" data-tag="${esc(t)}">${esc(t)}</span>`)
					  .join('')}</div>`
			: ''}
			<div class="bm-domain">${esc(domain)}</div>
		</div>`;

	// Lazy load favicon using shared observer
	const faviconImg = a.querySelector('.bm-favicon');
	if (faviconImg) {
		getFaviconObserver().observe(faviconImg);
	}

	a.querySelector('.bm-star').addEventListener('click', e => {
		e.preventDefault();
		e.stopPropagation();
		const url = e.currentTarget.dataset.url;
		toggleFavorite(url);
		e.currentTarget.classList.toggle('active');
		e.currentTarget.textContent = isFavorite(url) ? '★' : '☆';
	});

	const noteBtn = a.querySelector('.bm-note-indicator');
	if (noteBtn) {
		noteBtn.addEventListener('click', e => {
			e.preventDefault();
			e.stopPropagation();
			openNoteModal(bm.url, bm.title || bm.url);
		});
	}

	if (tagClickable && onTagClick) {
		a.querySelectorAll('.bm-tag').forEach(el => {
			el.addEventListener('click', e => {
				e.preventDefault();
				e.stopPropagation();
				onTagClick(el.dataset.tag);
			});
		});
	}

	return a;
}

// ── Notes ──────────────────────────────────

let notesCache = null;

function invalidateNotesCache()
{
	notesCache = null;
}

function hasNote(url)
{
	if (!notesCache) {
		const dbIdx = getActiveDbIndex();
		const key   = `localmarks-notes-${dbIdx}`;
		try {
			notesCache = JSON.parse(localStorage.getItem(key) || '{}');
		} catch {
			notesCache = {};
		}
	}
	return !!notesCache[url];
}

function openNoteModal(url, title)
{
	const existing = getNote(url);
	const text     = existing ? existing.text : '';
	const updated  = existing ? new Date(existing.updated).toLocaleString() : '';

	const metaHtml = updated
		? `<div class="note-meta">Last edited: ${esc(updated)}</div>`
		: '';

	openModal(`
		<div class="note-modal">
			<div class="note-modal-header">
				<h3 class="note-modal-title">${esc(title)}</h3>
				<button class="modal-close" title="Close" aria-label="Close">&times;</button>
			</div>
			${metaHtml}
			<textarea class="note-textarea" rows="6" placeholder="Write a note…">${esc(text)}</textarea>
			<div class="note-modal-actions">
				<button class="note-btn note-btn-delete" ${text ? '' : 'hidden'}>Delete</button>
				<button class="note-btn note-btn-save">Save</button>
			</div>
		</div>
	`);

	const modal   = document.querySelector('.note-modal');
	const textarea = modal.querySelector('.note-textarea');
	const saveBtn  = modal.querySelector('.note-btn-save');
	const delBtn   = modal.querySelector('.note-btn-delete');
	const closeBtn = modal.querySelector('.modal-close');

	closeBtn.addEventListener('click', closeModal);

	saveBtn.addEventListener('click', () => {
		setNote(url, textarea.value);
		closeModal();
		invalidateNotesCache();
		window.dispatchEvent(new CustomEvent('note-changed', {detail: {url}}));
	});

	delBtn.addEventListener('click', () => {
		deleteNote(url);
		closeModal();
		invalidateNotesCache();
		window.dispatchEvent(new CustomEvent('note-changed', {detail: {url}}));
	});

	textarea.addEventListener('keydown', e => {
		if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') {
			e.preventDefault();
			saveBtn.click();
		}
	});
}

export {openNoteModal};
