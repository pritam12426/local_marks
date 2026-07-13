// =============================================
// panel.js  —  Main panel rendering
// =============================================

'use strict';

import {buildCard} from './data.js';
import {getFavorites} from './data.js';
import {renderTagBar, getActiveTags, setActiveTags} from './tag_bar.js';

const state = {
	categories: [],
	activeCategory: 0,
	panelTitleEl: null,
	bookmarkListEl: null
};

export function initPanel(cfg)
{
	state.categories     = cfg.categories;
	state.activeCategory = cfg.activeCategory;
	state.panelTitleEl   = cfg.panelTitleEl;
	state.bookmarkListEl = cfg.bookmarkListEl;
}

export function getActiveCategory() { return state.activeCategory; }
export function setActiveCategory(index) { state.activeCategory = index; }
export function clearActiveTags() { setActiveTags(new Set()); }

export function renderPanel()
{
	const activeTags = getActiveTags();

	if (state.activeCategory === -1) {
		renderFavorites(activeTags);
		return;
	}

	const cat = state.categories[state.activeCategory];
	if (!cat)
		return;

	const seen      = new Set();
	const bookmarks = (cat.bookmarks || []).filter(bm => {
		if (seen.has(bm.url))
			return false;
		seen.add(bm.url);
		return true;
	});

	const filtered = activeTags.size ? bookmarks.filter(bm => [...activeTags].every(
	                                                        t => (bm.tags || []).includes(t)))
	                                 : bookmarks;

	const countLabel = filtered.length !== bookmarks.length
	                       ? `${filtered.length} of ${bookmarks.length}`
			               : filtered.length;

	state.panelTitleEl.innerHTML = `
		📋 ${cat.category}
		<span class="panel-count">(${countLabel})</span>
	`;

	const allTags = [...new Set(bookmarks.flatMap(bm => bm.tags || []))].sort();
	renderTagBar(allTags);
	renderCards(filtered);
}

function renderFavorites(activeTags)
{
	const favUrls   = new Set(getFavorites());
	let   bookmarks = state.categories.flatMap(
        cat => (cat.bookmarks || []).filter(bm => favUrls.has(bm.url)));

	const seen = new Set();
	bookmarks  = bookmarks.filter(bm => {
        if (seen.has(bm.url))
            return false;
        seen.add(bm.url);
        return true;
    });

	const filtered = activeTags.size ? bookmarks.filter(bm => [...activeTags].every(
	                                                        t => (bm.tags || []).includes(t)))
	                                 : bookmarks;

	state.panelTitleEl.innerHTML = `⭐ Favorites <span class="panel-count">(${
		filtered.length})</span>`;
	const allTags = [...new Set(bookmarks.flatMap(bm => bm.tags || []))].sort();
	renderTagBar(allTags);
	renderCards(filtered);
}

function renderCards(bookmarks)
{
	state.bookmarkListEl.innerHTML = '';

	if (!bookmarks.length) {
		state.bookmarkListEl.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">🔍</div>
				<p>No bookmarks match your filters.</p>
				<button class="state-action" onclick="document.querySelector('.tag-clear')?.click()">Clear all filters</button>
			</div>`;
		return;
	}

	const frag = document.createDocumentFragment();
	bookmarks.forEach(bm => frag.appendChild(buildCard(bm, {
		tagClickable: true,
		onTagClick: tag => {
		    const tags = getActiveTags();
		    tags.add(tag);
		    window.dispatchEvent(new CustomEvent('tag-filter-change'));
		}
	})));
	state.bookmarkListEl.appendChild(frag);

	window.dispatchEvent(new CustomEvent('cards-rendered'));
}
