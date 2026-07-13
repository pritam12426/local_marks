// =============================================
// sidebar.js  —  Category sidebar rendering & events
// =============================================

'use strict';

import {esc} from './data.js';
import {getFavorites} from './data.js';

let state = {categories: [], activeCategory: 0, catListEl: null, sidebarCountEl: null};

export function initSidebar(cfg)
{
	state.categories     = cfg.categories;
	state.activeCategory = cfg.activeCategory;
	state.catListEl      = cfg.catListEl;
	state.sidebarCountEl = cfg.sidebarCountEl;
}

export function getActiveCategory()
{
	return state.activeCategory;
}
export function setActiveCategory(index)
{
	state.activeCategory = index;
}

export function renderSidebar()
{
	const favCount = getFavoritesCount();
	if (state.activeCategory === -1 && !favCount)
		state.activeCategory = 0;
	state.sidebarCountEl.textContent = state.categories.length;

	const frag = document.createDocumentFragment();

	if (favCount) {
		const li     = document.createElement('li');
		li.innerHTML = `<span class="cat-label">⭐ Favorites</span><span class="cat-badge">${
			favCount}</span>`;
		if (state.activeCategory === -1)
			li.classList.add('active');
		li.tabIndex = 0;
		li.setAttribute('role', 'button');
		li.setAttribute('aria-label', `Favorites, ${favCount} bookmarks`);
		li.addEventListener('click',
		                    () => window.dispatchEvent(new CustomEvent('sidebar-fav-click')));
		li.addEventListener('keydown', e => {
			if (e.key === 'Enter' || e.key === ' ') {
				e.preventDefault();
				li.click();
			}
		});
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
		li.addEventListener('click',
		                    () => window.dispatchEvent(
		                        new CustomEvent('sidebar-category-click', {detail: {index: i}})));
		li.addEventListener('keydown', e => {
			if (e.key === 'Enter' || e.key === ' ') {
				e.preventDefault();
				li.click();
			}
		});
		frag.appendChild(li);
	});

	state.catListEl.innerHTML = '';
	state.catListEl.appendChild(frag);
}

export function highlightSidebar(index)
{
	const hasFavs = getFavoritesCount() > 0;
	state.catListEl.querySelectorAll('li').forEach((li, i) => {
		const targetIdx = hasFavs ? i - 1 : i;
		li.classList.toggle('active', targetIdx === index);
	});
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
