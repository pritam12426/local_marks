// =============================================
// browse.js  —  Bookmark browser view (composed from modules)
// =============================================

'use strict';

import {
	initSidebar,
	renderSidebar     as renderSidebarFn,
	getActiveCategory as sidebarGetActive,
	setActiveCategory as sidebarSetActive,
	highlightSidebar
} from './sidebar.js';

import {
	initPanel,
	renderPanel,
	getActiveCategory as panelGetActive,
	setActiveCategory as panelSetActive,
	clearActiveTags
} from './panel.js';

import {initSearch, renderSearch, clearSearch as searchClear} from './search.js';
import {initTagBar, getActiveTags, setActiveTags} from './tag_bar.js';
import {initKeyboard, refreshCards, focusFirstCard} from './keyboard.js';
import {getFavorites, buildCard} from './data.js';

let allCategories  = [];
let searchQuery    = '';
let activeCategory = 0;

// DOM refs
let elSearch, elClear, elCatList, elSidebarCount, elPanelTitle, elTagBar, elBookmarkList,
    elHeaderTitle;

export function initBrowse(data)
{
	elSearch       = document.getElementById('search-input');
	elClear        = document.getElementById('clear-search');
	elCatList      = document.getElementById('category-list');
	elSidebarCount = document.getElementById('sidebar-count');
	elPanelTitle   = document.getElementById('panel-title');
	elTagBar       = document.getElementById('tag-bar');
	elBookmarkList = document.getElementById('bookmark-list');
	elHeaderTitle  = document.getElementById('header-title');

	allCategories = data.book_Marks || data.categories || [];

	// Initialize submodules
	initSidebar({
		categories: allCategories,
		activeCategory,
		catListEl: elCatList,
		sidebarCountEl: elSidebarCount
	});

	initPanel({
		categories: allCategories,
		activeCategory,
		panelTitleEl: elPanelTitle,
		bookmarkListEl: elBookmarkList
	});

	initSearch({
		categories: allCategories,
		panelTitleEl: elPanelTitle,
		tagBarEl: elTagBar,
		bookmarkListEl: elBookmarkList,
		searchEl: elSearch,
		clearEl: elClear,
		catListEl: elCatList
	});

	initTagBar(elTagBar);

	initKeyboard({
		bookmarkListEl: elBookmarkList,
		catListEl: elCatList,
		searchEl: elSearch,
		clearEl: elClear
	});

	updateHeaderCount();
	renderSidebarFn();
	bindEvents();
}

export function renderBrowse()
{
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

function updateHeaderCount()
{
	const total = allCategories.reduce((s, c) => s + (c.bookmarks || []).length, 0);
	if (elHeaderTitle)
		elHeaderTitle.textContent = `📚 ${total} Bookmarks`;
}

function bindEvents()
{
	elClear.addEventListener('click', onClearSearch);

	window.addEventListener('favorites-changed', () => {
		const wasFavView = sidebarGetActive() === -1;
		renderSidebarFn();
		if (wasFavView)
			renderPanel();
		updateHeaderCount();
	});

	// Sidebar events
	window.addEventListener('sidebar-fav-click', () => {
		if (searchQuery) {
			searchClear();
			searchQuery = '';
			elClear.classList.remove('visible');
		}
		activeCategory = -1;
		sidebarSetActive(-1);
		panelSetActive(-1);
		setActiveTags(new Set());
		highlightSidebar(-1);
		renderPanel();
	});

	window.addEventListener('sidebar-category-click', e => {
		if (searchQuery) {
			searchClear();
			searchQuery = '';
			elClear.classList.remove('visible');
		}
		activeCategory = e.detail.index;
		sidebarSetActive(activeCategory);
		panelSetActive(activeCategory);
		setActiveTags(new Set());
		highlightSidebar(activeCategory);
		renderPanel();
	});

	// Tag filter changes
	window.addEventListener('tag-filter-change', () => { renderPanel(); });

	window.addEventListener('tag-bar-toggle', () => { renderPanel(); });

	// Search events from keyboard.js
	window.addEventListener('search-query-changed', e => {
		searchQuery = e.detail.query;
		elCatList.querySelectorAll('li').forEach(li => li.classList.remove('active'));
		renderSearch(searchQuery);
	});

	window.addEventListener('search-query-empty', () => {
		highlightSidebar(activeCategory);
		renderPanel();
	});

	window.addEventListener('search-cleared', () => {
		searchQuery = '';
		highlightSidebar(activeCategory);
		renderPanel();
	});

	// Refresh cards after render
	window.addEventListener('cards-rendered', () => { refreshCards(); });

	// Tag filter from search results
	window.addEventListener('tag-filter-from-search', e => {
		const tags = getActiveTags();
		tags.add(e.detail.tag);
		// Switch to browse view with that category
		// For simplicity, just re-render panel with new tag
		renderPanel();
	});

	// Global shortcuts
	document.addEventListener('keydown', e => {
		if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'k') {
			e.preventDefault();
			elSearch.focus();
			elSearch.select();
		}
		if (e.key === 'Escape' && document.activeElement === elSearch) {
			onClearSearch();
		}
	});
}

function onClearSearch()
{
	searchClear();
	searchQuery = '';
	setActiveTags(new Set());
	if (activeCategory === -1) {
		elCatList.querySelectorAll('li').forEach(li => li.classList.remove('active'));
		const first = elCatList.firstElementChild;
		if (first)
			first.classList.add('active');
	} else {
		renderSidebarFn();
	}
	renderPanel();
	elSearch.blur();
}
