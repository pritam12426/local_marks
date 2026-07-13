// =============================================
// search.js  —  Search index & results
// =============================================

'use strict';

import {esc, buildCard} from './data.js';

const state = {
	categories: [],
	index: [],
	panelTitleEl: null,
	tagBarEl: null,
	bookmarkListEl: null,
	searchEl: null,
	clearEl: null,
	catListEl: null
};

export function initSearch(cfg)
{
	state.categories     = cfg.categories;
	state.panelTitleEl   = cfg.panelTitleEl;
	state.tagBarEl       = cfg.tagBarEl;
	state.bookmarkListEl = cfg.bookmarkListEl;
	state.searchEl       = cfg.searchEl;
	state.clearEl        = cfg.clearEl;
	state.catListEl      = cfg.catListEl;

	buildIndex();
}

function buildIndex()
{
	state.index = state.categories.flatMap(
	    cat => (cat.bookmarks ||
	            []).map(bm => ({
		                    category: cat.category,
		                    bookmark: bm,
		                    text: [bm.title, bm.description, bm.url, ...(bm.tags || [])]
		                              .filter(Boolean)
		                              .join(' ')
		                              .toLowerCase()
	                    })));
}

export function rebuildIndex()
{
	buildIndex();
}

export function renderSearch(query)
{
	const q       = query.toLowerCase();
	const results = state.index.filter(item => item.text.includes(q));

	state.panelTitleEl.innerHTML = `
		🔍 Results for <em style="color:var(--accent)">"${esc(query)}"</em>
		<span class="panel-count">(${results.length})</span>
	`;
	state.tagBarEl.innerHTML = '';

	if (!results.length) {
		state.bookmarkListEl.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">📭</div>
				<p>No bookmarks found for <strong>${esc(query)}</strong>.</p>
				<button class="state-action" onclick="document.getElementById('clear-search')?.click()">Clear search</button>
			</div>`;
		return;
	}

	const groups = new Map();
	results.forEach(({category, bookmark}) => {
		if (!groups.has(category))
			groups.set(category, []);
		groups.get(category).push(bookmark);
	});

	const frag = document.createDocumentFragment();
	groups.forEach((bms, catName) => {
		const header       = document.createElement('div');
		header.className   = 'search-group-header';
		header.textContent = `📋 ${catName}`;
		frag.appendChild(header);

		const seen = new Set();
		bms.forEach(bm => {
			if (seen.has(bm.url))
				return;
			seen.add(bm.url);
			frag.appendChild(buildCard(bm, {
				tagClickable: true,
				onTagClick: tag => {
				    window.dispatchEvent(
				        new CustomEvent('tag-filter-from-search', {detail: {tag}}));
				}
			}));
		});
	});

	state.bookmarkListEl.innerHTML = '';
	state.bookmarkListEl.appendChild(frag);

	window.dispatchEvent(new CustomEvent('cards-rendered'));
}

export function clearSearch()
{
	state.searchEl.value = '';
	state.clearEl.classList.remove('visible');

	state.catListEl.querySelectorAll('li').forEach(li => li.classList.remove('active'));
	const first = state.catListEl.firstElementChild;
	if (first)
		first.classList.add('active');

	window.dispatchEvent(new CustomEvent('search-cleared'));
}
