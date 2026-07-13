// =============================================
// data.js  —  Shared data layer & utilities
// =============================================

'use strict';

// ── IndexedDB cache ────────────────────────

const DB_NAME    = 'LocalMarksCache';
const DB_VERSION = 1;
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

async function getCached()
{
	try {
		const db                = await openDB();
		const            tx     = db.transaction(STORE_NAME, 'readonly');
		const            store  = tx.objectStore(STORE_NAME);
		const            result = await new Promise((res, rej) => {
			const r     = store.get('bookmarks');
			r.onsuccess = () => res(r.result || null);
			r.onerror = () => rej(null);
		});
		db.close();
		return result ? result.data : null;
	} catch {
		return null;
	}
}

async function setCache(data)
{
	try {
		const            db    = await openDB();
		const            tx    = db.transaction(STORE_NAME, 'readwrite');
		const            store = tx.objectStore(STORE_NAME);
		store.put({id: 'bookmarks', data, timestamp: Date.now()});
		db.close();
	} catch { /* cache is optional */
	}
}

export async function fetchBookmarks()
{
	try {
		const res = await fetch('bookmarks.json', {cache: 'no-cache'});
		if (!res.ok)
			throw new Error(`HTTP ${res.status}`);
		const data = await res.json();
		setCache(data);
		return data;
	} catch (err) {
		const cached = await getCached();
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

export function getFavorites()
{
	try {
		return JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]');
	} catch {
		return [];
	}
}

export function toggleFavorite(url)
{
	let   favs = getFavorites();
	const idx  = favs.indexOf(url);
    idx === -1 ? favs.push(url) : favs.splice(idx, 1);
	localStorage.setItem(FAVORITES_KEY, JSON.stringify(favs));
	window.dispatchEvent(new CustomEvent('favorites-changed'));
}

export function isFavorite(url)
{
	return getFavorites().includes(url);
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
	const btn = document.getElementById('theme-toggle');
	if (btn) {
		btn.textContent = theme === 'dark' ? '☀️' : '🌙';
		btn.setAttribute('aria-label',
		                 theme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme');
	}
}

export function initTheme()
{
	const theme = getTheme();
	document.documentElement.setAttribute('data-theme', theme);
	updateThemeToggleIcon(theme);

	const toggle = document.getElementById('theme-toggle');
	if (toggle) {
		toggle.addEventListener('click', toggleTheme);
	}

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
		if (toggle) toggle.removeEventListener('click', toggleTheme);
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
	a.href      = bm.url;
	a.target    = '_blank';
	a.rel       = 'noopener noreferrer';

	const domain       = bm.domain;
	const displayTitle = bm.title || bm.description || bm.url;
	const faviconSrc   = bm.icon || `https://www.google.com/s2/favicons?sz=64&domain=${domain}`;
	const fallbackSrc  = `https://www.google.com/s2/favicons?sz=64&domain=${domain}`;
	const starred      = isFavorite(bm.url);

	a.innerHTML = `
		<span class="bm-star ${starred ? 'active' : ''}" data-url="${esc(bm.url)}">${
		starred ? '★' : '☆'}</span>
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

	// Lazy load favicon
	const faviconImg = a.querySelector('.bm-favicon');
	if (faviconImg) {
		const observer = new IntersectionObserver((entries) => {
			entries.forEach(entry => {
				if (entry.isIntersecting) {
					const img  = entry.target;
					img.src    = img.dataset.src;
					img.onload = () => img.classList.add('loaded');
					img.onerror = () => {
						img.src    = img.dataset.fallback;
						img.onload = () => img.classList.add('loaded');
					};
					observer.unobserve(img);
				}
			});
		}, {rootMargin: '100px'});
		observer.observe(faviconImg);
	}

	a.querySelector('.bm-star').addEventListener('click', e => {
		e.preventDefault();
		e.stopPropagation();
		const url = e.currentTarget.dataset.url;
		toggleFavorite(url);
		e.currentTarget.classList.toggle('active');
		e.currentTarget.textContent = isFavorite(url) ? '★' : '☆';
	});

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
