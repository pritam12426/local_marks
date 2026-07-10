// =============================================
// data.js  —  Shared data layer & utilities
// =============================================

'use strict';

// ── IndexedDB cache ────────────────────────

const DB_NAME    = 'LocalMarksCache';
const DB_VERSION = 1;
const STORE_NAME = 'bookmarks';

function openDB() {
	return new Promise((resolve, reject) => {
		const req = indexedDB.open(DB_NAME, DB_VERSION);
		req.onupgradeneeded = e => {
			const db = e.target.result;
			if (!db.objectStoreNames.contains(STORE_NAME))
				db.createObjectStore(STORE_NAME, { keyPath: 'id' });
		};
		req.onsuccess = e => resolve(e.target.result);
		req.onerror   = () => reject(new Error('IndexedDB open failed'));
	});
}

async function getCached() {
	try {
		const db  = await openDB();
		const tx  = db.transaction(STORE_NAME, 'readonly');
		const store = tx.objectStore(STORE_NAME);
		const result = await new Promise((res, rej) => {
			const r = store.get('bookmarks');
			r.onsuccess = () => res(r.result || null);
			r.onerror   = () => rej(null);
		});
		db.close();
		return result ? result.data : null;
	} catch { return null; }
}

async function setCache(data) {
	try {
		const db  = await openDB();
		const tx  = db.transaction(STORE_NAME, 'readwrite');
		const store = tx.objectStore(STORE_NAME);
		store.put({ id: 'bookmarks', data, timestamp: Date.now() });
		db.close();
	} catch { /* cache is optional */ }
}

export async function fetchBookmarks() {
	const cached = await getCached();
	if (cached) {
		fetch('bookmarks.json')
			.then(r => r.json())
			.then(fresh => {
				setCache(fresh);
				window.dispatchEvent(new CustomEvent('bookmarks-refreshed', { detail: fresh }));
			})
			.catch(() => {});
		return cached;
	}

	const res = await fetch('bookmarks.json');
	if (!res.ok) throw new Error(`HTTP ${res.status}`);
	const data = await res.json();
	await setCache(data);
	return data;
}

// ── HTML escape ────────────────────────────

export function esc(str) {
	return String(str ?? '')
		.replace(/&/g, '&amp;')
		.replace(/</g, '&lt;')
		.replace(/>/g, '&gt;')
		.replace(/"/g, '&quot;');
}

// ── Favorites (localStorage) ───────────────

const FAVORITES_KEY = 'localmarks-favorites';

export function getFavorites() {
	try { return JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]'); }
	catch { return []; }
}

export function toggleFavorite(url) {
	let favs = getFavorites();
	const idx = favs.indexOf(url);
	idx === -1 ? favs.push(url) : favs.splice(idx, 1);
	localStorage.setItem(FAVORITES_KEY, JSON.stringify(favs));
	window.dispatchEvent(new CustomEvent('favorites-changed'));
}

export function isFavorite(url) { return getFavorites().includes(url); }

// ── Layout (localStorage) ──────────────────

const LAYOUT_KEY = 'localmarks-layout';

export function getLayout() { return localStorage.getItem(LAYOUT_KEY) || 'single'; }
export function setLayout(mode) { localStorage.setItem(LAYOUT_KEY, mode); }

// ── Bookmark card builder ──────────────────

export function buildCard(bm, { tagClickable, onTagClick } = {}) {
	const a = document.createElement('a');
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
		<span class="bm-star ${starred ? 'active' : ''}" data-url="${esc(bm.url)}">${starred ? '★' : '☆'}</span>
		<img class="bm-favicon" src="${esc(faviconSrc)}" alt=""
			onerror="this.__retried?this.style.display='none':(this.__retried=true,this.src='${esc(fallbackSrc)}')">
		<div class="bm-body">
			<div class="bm-title">${esc(displayTitle)}</div>
			${bm.description && bm.description !== displayTitle
				? `<div class="bm-desc">${esc(bm.description)}</div>`
				: ''}
			${(bm.tags || []).length
				? `<div class="bm-tags">${bm.tags.map(t =>
					`<span class="bm-tag" data-tag="${esc(t)}">${esc(t)}</span>`
				  ).join('')}</div>`
				: ''}
			<div class="bm-domain">${esc(domain)}</div>
		</div>`;

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
