// =============================================
// databases.js  —  Database selector view
// =============================================

'use strict';

import {esc} from './data.js';
import {fetchDatabases, getActiveDbIndex, setActiveDbIndex} from './data.js';

let elList       = null;
let elError      = null;
let elSearch     = null;
let elClearBtn   = null;
let allDatabases = [];

export function initDatabaseSelector()
{
	elList    = document.getElementById('db-select-list');
	elError   = document.getElementById('db-select-error');
	elSearch  = document.getElementById('db-select-search-input');
	elClearBtn = document.getElementById('db-select-clear-search');

	if (elSearch) {
		elSearch.addEventListener('input', () => {
			const q = elSearch.value.toLowerCase();
			filterDatabases(q);
			elClearBtn.hidden = !q;
		});
		elSearch.addEventListener('keydown', e => {
			if (e.key === 'Escape') {
				elSearch.value = '';
				filterDatabases('');
				elClearBtn.hidden = true;
			}
		});
	}
	if (elClearBtn) {
		elClearBtn.addEventListener('click', () => {
			elSearch.value = '';
			filterDatabases('');
			elClearBtn.hidden = true;
			elSearch.focus();
		});
	}

	initDbKeyboardNavigation();
}

export async function renderDatabaseSelector()
{
	if (!elList)
		return;

	elList.innerHTML  = `<div class="db-card-skeleton"></div><div class="db-card-skeleton"></div>`;
	if (elError)
		elError.hidden = true;

	let payload;
	try {
		payload = await fetchDatabases();
	} catch (err) {
		elList.innerHTML = '';
		if (elError) {
			elError.hidden     = false;
			elError.textContent = `Couldn't reach /api/databases — ${err.message}`;
		}
		return;
	}

	allDatabases = payload.databases || [];
	const activeIdx = getActiveDbIndex();

	if (!allDatabases.length) {
		elList.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">🛢️</div>
				<p>No databases registered on the server.</p>
			</div>`;
		return;
	}

	renderCards(allDatabases);
}

function filterDatabases(query)
{
	const filtered = allDatabases.filter(db => {
		const name = (db.file_name || '').toLowerCase();
		const path = (db.absolute_path || '').toLowerCase();
		return name.includes(query) || path.includes(query);
	});
	renderCards(filtered);
}

function renderCards(databases)
{
	if (!elList) return;

	if (!databases.length) {
		elList.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">🔍</div>
				<p>No databases match your filter.</p>
			</div>`;
		return;
	}

	const activeIdx = getActiveDbIndex();
	const frag = document.createDocumentFragment();
	databases.forEach((db, idx) => {
		const originalIdx = allDatabases.indexOf(db);
		frag.appendChild(buildDbCard(db, originalIdx, originalIdx === activeIdx));
	});

	elList.innerHTML = '';
	elList.appendChild(frag);

	// Update dbCards reference for keyboard navigation
	dbCards = Array.from(elList.querySelectorAll('.db-card'));
	dbFocusedIndex = -1;
}

// ── Helpers ──────────────────────────────────

function buildDbCard(db, idx, isActive)
{
	const card     = document.createElement('button');
	card.type      = 'button';
	card.className = 'db-card' + (isActive ? ' active' : '');
	card.setAttribute('aria-label', `Open ${db.file_name}${isActive ? ' (current)' : ''}`);
	card.tabIndex  = -1;

	const owner = db.user || db.uid;
	const group = db.group || db.gid;

	card.innerHTML = `
		<div class="db-card-top">
			<span class="db-card-name">🛢️ ${esc(db.file_name)}</span>
			<span class="db-card-time">${relativeTime(db.mTime_sec)}</span>
		</div>
		<div class="db-card-meta">
			<span class="db-card-perm">${permString(db.mode)} ${esc(String(owner))}:${esc(String(group))}</span>
			<span class="db-card-size">${esc(formatBytes(db.file_size))}</span>
			<span class="db-card-date">${absoluteTime(db.mTime_sec)}</span>
		</div>
		${isActive ? '<span class="db-card-badge">Current</span>' : ''}
	`;

	card.addEventListener('click', () => selectDatabase(idx, isActive));
	return card;
}

function formatBytes(bytes)
{
	if (!bytes && bytes !== 0)
		return '—';
	const units = ['B', 'KB', 'MB', 'GB', 'TB'];
	let i = 0;
	while (bytes >= 1024 && i < units.length - 1) {
		bytes /= 1024;
		i++;
	}
	return `${bytes.toFixed(i === 0 ? 0 : 1)} ${units[i]}`;
}

function relativeTime(unixSec)
{
	if (!unixSec)
		return '';
	const diff = Math.max(0, Math.floor(Date.now() / 1000) - unixSec);
	if (diff < 60)
		return `${diff} sec ago`;
	if (diff < 3600)
		return `${Math.floor(diff / 60)} min ago`;
	if (diff < 86400)
		return `${Math.floor(diff / 3600)} hour ago`;
	if (diff < 2592000)
		return `${Math.floor(diff / 86400)} day ago`;
	if (diff < 31536000)
		return `${Math.floor(diff / 2592000)} month ago`;
	return `${Math.floor(diff / 31536000)} year ago`;
}

function absoluteTime(unixSec)
{
	if (!unixSec)
		return '—';
	const d = new Date(unixSec * 1000);
	let   h = d.getHours();
	const ampm = h >= 12 ? 'PM' : 'AM';
	h = h % 12;
	if (h === 0)
		h = 12;
	const pad2 = n => String(n).padStart(2, '0');
	return `${d.getFullYear()}-${MONTHS[d.getMonth()]}-${pad2(d.getDate())} `
	       + `${pad2(h)}:${pad2(d.getMinutes())} ${ampm}`;
}

const MONTHS = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];

function permString(mode)
{
	const m = parseInt(mode, 8) & 0o777;
	const bits = [
		(m & 0o400) ? 'r' : '-', (m & 0o200) ? 'w' : '-', (m & 0o100) ? 'x' : '-',
		(m & 0o040) ? 'r' : '-', (m & 0o020) ? 'w' : '-', (m & 0o010) ? 'x' : '-',
		(m & 0o004) ? 'r' : '-', (m & 0o002) ? 'w' : '-', (m & 0o001) ? 'x' : '-'
	];
	return '-' + bits.join('');
}

let dbFocusedIndex = -1;
let dbCards = [];

function focusDbCard()
{
	if (!dbCards.length) return;
	if (dbFocusedIndex >= 0 && dbFocusedIndex < dbCards.length) {
		dbCards[dbFocusedIndex].focus({preventScroll: true});
		dbCards[dbFocusedIndex].scrollIntoView({behavior: 'smooth', block: 'nearest'});
	}
}

function blurCards()
{
	dbFocusedIndex = -1;
	dbCards.forEach(card => {
		card.classList.remove('focused');
		card.removeAttribute('aria-selected');
		card.tabIndex = -1;
	});
}

function focusFirstDbCard()
{
	dbFocusedIndex = 0;
	focusDbCard();
}

// ── Vim-style keyboard navigation for database selector ─────
function initDbKeyboardNavigation()
{
	if (!elList) return;

	// Make cards focusable and add keyboard handlers
	elList.addEventListener('keydown', handleDbListKeys);

	// Also handle keys on the search input when focused
	if (elSearch) {
		elSearch.addEventListener('keydown', e => {
			if (e.key === 'ArrowDown' || e.key === 'j') {
				e.preventDefault();
				focusFirstDbCard();
			}
			if (e.key === 'Escape') {
				elSearch.value = '';
				filterDatabases('');
				elClearBtn.hidden = true;
			}
		});
	}
}

function handleDbListKeys(e)
{
	const cards = Array.from(elList.querySelectorAll('.db-card'));
	if (!cards.length) return;

	switch (e.key) {
	case 'j':
	case 'ArrowDown':
		e.preventDefault();
		e.stopPropagation();
		if (dbFocusedIndex < cards.length - 1) {
			dbFocusedIndex++;
			focusDbCard();
		}
		break;
	case 'k':
	case 'ArrowUp':
		e.preventDefault();
		e.stopPropagation();
		if (dbFocusedIndex > 0) {
			dbFocusedIndex--;
			focusDbCard();
		} else if (dbFocusedIndex === 0) {
			dbFocusedIndex = -1;
			blurCards();
			if (elSearch) elSearch.focus();
		}
		break;
	case 'Enter':
		if (dbFocusedIndex >= 0) {
			e.preventDefault();
			e.stopPropagation();
			cards[dbFocusedIndex].click();
		}
		break;
	case 'Escape':
		blurCards();
		if (elSearch) {
			elSearch.value = '';
			filterDatabases('');
			elClearBtn.hidden = true;
			elSearch.focus();
		}
		break;
	}
}

function selectDatabase(idx, isActive)
{
	if (isActive) {
		// Already the active DB — just go browse it.
		location.hash = '#browse';
		return;
	}
	// Set the active DB index and navigate to browse.
	// main.js's renderRoute will handle loading the new database.
	setActiveDbIndex(idx);
	location.hash = '#browse';
}
