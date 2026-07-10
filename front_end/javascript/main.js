// =============================================
// main.js  —  Entry point with hash router
// =============================================

'use strict';

import { fetchBookmarks, getLayout, setLayout } from './data.js';
import { initBrowse, renderBrowse, updateBrowseData } from './browse.js';
import { renderInfo } from './info.js';
import { initRandom, renderRandom, updateRandomData } from './random.js';
let data = null;

// ── Boot ───────────────────────────────────

async function init() {
	initLayoutToggle();

	try {
		data = await fetchBookmarks();
	} catch (err) {
		console.error('❌ Failed to load bookmarks:', err);
		document.getElementById('bookmark-list').innerHTML = `
			<div class="state-empty">
				<div class="state-icon">❌</div>
				<p>Could not load <code>bookmarks.json</code>.</p>
			</div>`;
		return;
	}

	initBrowse(data);
	initRandom(data);

	window.addEventListener('bookmarks-refreshed', e => {
		data = e.detail;
		updateBrowseData(data);
		updateRandomData(data);
		renderRoute();
	});

	window.addEventListener('hashchange', renderRoute);
	if (!location.hash || location.hash === '#') location.hash = '#browse';
	renderRoute();
}

// ── Layout toggle ──────────────────────────

function initLayoutToggle() {
	const toggle = document.getElementById('layout-toggle');
	const saved  = getLayout();
	toggle.querySelectorAll('.layout-btn').forEach(b =>
		b.classList.toggle('active', b.dataset.layout === saved)
	);

	// apply saved layout to bookmark list
	const list = document.getElementById('bookmark-list');
	if (list) list.className = 'bookmark-list' + (saved !== 'single' ? ` ${saved}` : '');

	toggle.addEventListener('click', e => {
		const btn = e.target.closest('.layout-btn');
		if (!btn) return;
		toggle.querySelectorAll('.layout-btn').forEach(b => b.classList.remove('active'));
		btn.classList.add('active');
		const mode = btn.dataset.layout;
		setLayout(mode);
		if (list) list.className = 'bookmark-list' + (mode !== 'single' ? ` ${mode}` : '');
	});
}

// ── Router ─────────────────────────────────

function renderRoute() {
	const hash    = location.hash.replace(/^#/, '');
	const qIdx    = hash.indexOf('?');
	const route   = qIdx === -1 ? hash : hash.slice(0, qIdx);
	const qs      = qIdx === -1 ? '' : hash.slice(qIdx + 1);
	const qParams = new URLSearchParams(qs);

	document.querySelectorAll('.view').forEach(v => v.classList.remove('active'));
	document.body.className = `route-${route}`;

	const view = document.getElementById(`view-${route}`);
	if (view) view.classList.add('active');

	// Update header title
	const titles = { browse: 'LocalMarks', info: 'Database Info', random: '🎲 Random Links' };
	const h1 = document.getElementById('header-title');
	if (h1) h1.textContent = titles[route] || 'LocalMarks';

	// Update layout toggle visibility
	const lt = document.getElementById('layout-toggle');
	if (lt) lt.style.display = route === 'browse' ? '' : 'none';

	switch (route) {
		case 'browse':
			if (qParams.has('q'))
				document.getElementById('search-input').value = qParams.get('q');
			renderBrowse();
			break;
		case 'info':
			renderInfo(data);
			break;
		case 'random':
			renderRandom();
			break;
	}
}

init();
