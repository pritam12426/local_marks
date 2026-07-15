// =============================================
// main.js  —  Entry point with hash router
// =============================================

'use strict';

import {
	fetchBookmarks,
	getLayout,
	setLayout,
	initTheme,
	getSidebarWidth,
	setSidebarWidth,
	getActiveDbIndex,
	getActiveDbName
} from './data.js';

import {initBrowse, renderBrowse, updateCategories} from './browse.js';
import {renderInfo} from './info.js';
import {initRandom, renderRandom, updateRandomData} from './random.js';
import {initDatabaseSelector, renderDatabaseSelector} from './databases.js';

// ── Boot ───────────────────────────────────

async function init()
{
	try {
		initTheme();
		initLayoutToggle();
		initSidebarResizer();
	} catch (err) {
		console.error('❌ Init error:', err);
	}

	initDatabaseSelector();

	// Don't fetch bookmarks on boot - wait for user to select a database
	bootRouter();
}

function bootRouter()
{
	window.addEventListener('hashchange', renderRoute);
	if (!location.hash || location.hash === '#')
		location.hash = '#databases';
	renderRoute();
}

// ── Layout toggle ──────────────────────────

function initLayoutToggle()
{
	const toggle = document.getElementById('layout-toggle');
	const saved  = getLayout();
	toggle.querySelectorAll('.layout-btn')
	    .forEach(b => b.classList.toggle('active', b.dataset.layout === saved));

	// apply saved layout to bookmark list
	const list = document.getElementById('bookmark-list');
	if (list)
		list.className = 'bookmark-list' + (saved !== 'single' ? ` ${saved}` : '');

	toggle.addEventListener('click', e => {
		const btn = e.target.closest('.layout-btn');
		if (!btn)
			return;
		toggle.querySelectorAll('.layout-btn').forEach(b => b.classList.remove('active'));
		btn.classList.add('active');
		const mode = btn.dataset.layout;
		setLayout(mode);
		if (list)
			list.className = 'bookmark-list' + (mode !== 'single' ? ` ${mode}` : '');
	});
}

// ── Sidebar resize ─────────────────────────

function initSidebarResizer()
{
	const handle  = document.getElementById('sidebar-resizer');
	const sidebar = document.getElementById('sidebar');
	if (!handle || !sidebar)
		return;

	const MIN_W = 160;
	const MAX_W = 480;

	// Restore saved width (if any) — otherwise the CSS default (230px) stands.
	const saved = getSidebarWidth();
	if (saved)
		document.documentElement.style.setProperty('--sidebar-w', `${clamp(saved)}px`);

	function clamp(w)
	{
		return Math.min(MAX_W, Math.max(MIN_W, w));
	}

	let dragging    = false;
	let sidebarRect = null;

	function onMouseDown(e)
	{
		if (window.innerWidth <= 768)
			return;
		dragging    = true;
		sidebarRect = sidebar.getBoundingClientRect();  // read once
		handle.classList.add('dragging');
		document.body.classList.add('sidebar-resizing');
		e.preventDefault();
	}

	function onMouseMove(e)
	{
		if (!dragging || !sidebarRect)
			return;
		const width = clamp(e.clientX - sidebarRect.left);
		document.documentElement.style.setProperty('--sidebar-w', `${width}px`);
	}

	function onMouseUp()
	{
		if (!dragging)
			return;
		dragging    = false;
		sidebarRect = null;
		handle.classList.remove('dragging');
		document.body.classList.remove('sidebar-resizing');
		const width
		    = parseInt(getComputedStyle(document.documentElement).getPropertyValue('--sidebar-w'),
		               10);
		setSidebarWidth(width);
	}

	// Double-click resets to the default width
	function onDblClick()
	{
		document.documentElement.style.removeProperty('--sidebar-w');
		setSidebarWidth(null);
	}

	handle.addEventListener('mousedown', onMouseDown);
	window.addEventListener('mousemove', onMouseMove);
	window.addEventListener('mouseup', onMouseUp);
	handle.addEventListener('dblclick', onDblClick);

	// Return cleanup function
	return () => {
		handle.removeEventListener('mousedown', onMouseDown);
		window.removeEventListener('mousemove', onMouseMove);
		window.removeEventListener('mouseup', onMouseUp);
		handle.removeEventListener('dblclick', onDblClick);
	};
}



let data              = null;
let dataPromise        = null;
let loadedDbIdx        = null;  // which DB index `data` currently represents
let browseInitialized  = false; // has initBrowse()/initRandom() ever run?

// ── Router ─────────────────────────────────

async function ensureDataLoaded()
{
	const dbIdx = getActiveDbIndex();
	if (dbIdx === null) {
		location.hash = '#databases';
		return null;
	}

	// Already have fresh data for the currently-active database
	if (data && loadedDbIdx === dbIdx)
		return data;

	// First load, or the active database changed since we last fetched —
	// either way we need a new request.
	if (!dataPromise || loadedDbIdx !== dbIdx) {
		const requestedIdx = dbIdx;
		dataPromise = fetchBookmarks(dbIdx).then(d => {
			data       = d;
			loadedDbIdx = requestedIdx;
			return d;
		}).catch(err => {
			dataPromise = null;
			throw err;
		});
	}
	return dataPromise;
}

function renderRoute()
{
	const hash    = location.hash.replace(/^#/, '');
	const qIdx    = hash.indexOf('?');
	const route   = qIdx === -1 ? hash : hash.slice(0, qIdx);
	const qs      = qIdx === -1 ? '' : hash.slice(qIdx + 1);
	const qParams = new URLSearchParams(qs);

	document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
	document.body.className = `route-${route}`;

	const view = document.getElementById(`view-${route}`);
	if (view)
		view.classList.add('active');

	// Focus management on view switch
	const mainContent = document.getElementById('main-panel')
	                    || document.querySelector('.main-panel')
	                    || document.querySelector('.info-body')
	                    || document.querySelector('.random-body')
	                    || document.querySelector('.db-select-body');
	if (mainContent && document.activeElement !== document.getElementById('search-input')) {
		mainContent.setAttribute('tabindex', '-1');
		mainContent.focus({preventScroll: true});
	}

	// Update header title
	const h1 = document.getElementById('header-title');
	if (h1) {
		if (route === 'browse' || route === 'info' || route === 'random') {
			getActiveDbName().then(name => { h1.textContent = name; });
		} else {
			const titles = {
				browse: 'LocalMarks',
				info: 'Database Info',
				random: '🎲 Random Links',
				databases: '🛢️ Select Database'
			};
			h1.textContent = titles[route] || 'LocalMarks';
		}
	}

	// Update layout toggle visibility
	const lt = document.getElementById('layout-toggle');
	if (lt)
		lt.style.display = route === 'browse' ? '' : 'none';

	switch (route) {
	case 'browse':
		ensureDataLoaded().then(d => {
			if (!d) return; // redirected to databases
			if (qParams.has('q'))
				document.getElementById('search-input').value = qParams.get('q');
			if (!browseInitialized) {  // first load ever
				initBrowse(d);
				initRandom(d);
				browseInitialized = true;
			} else {
				updateCategories(d);
				updateRandomData(d);
			}
			renderBrowse();
		}).catch(err => {
			console.error('❌ Failed to load bookmarks:', err);
			document.getElementById('bookmark-list').innerHTML = `
				<div class="state-empty">
					<div class="state-icon">❌</div>
					<p>Could not load this database.</p>
					<button class="state-action" onclick="location.hash='#databases'">🛢️ Choose a different database</button>
				</div>`;
		});
		break;
	case 'info':
		ensureDataLoaded().then(d => {
			if (!d) return;
			renderInfo(d);
		});
		break;
	case 'random':
		ensureDataLoaded().then(d => {
			if (!d) return;
			renderRandom();
		});
		break;
	case 'databases':
		renderDatabaseSelector();
		break;
	}
}

init();
