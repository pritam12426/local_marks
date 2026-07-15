// =============================================
// databases.js  —  Database selector view
// =============================================

'use strict';

import {esc} from './data.js';
import {fetchDatabases, getActiveDbIndex, setActiveDbIndex} from './data.js';

let elList  = null;
let elError = null;

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun',
                'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

export function initDatabaseSelector()
{
	elList  = document.getElementById('db-select-list');
	elError = document.getElementById('db-select-error');
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

	const databases = payload.databases || [];
	const activeIdx = getActiveDbIndex();

	if (!databases.length) {
		elList.innerHTML = `
			<div class="state-empty">
				<div class="state-icon">🛢️</div>
				<p>No databases registered on the server.</p>
			</div>`;
		return;
	}

	const frag = document.createDocumentFragment();
	databases.forEach((db, idx) => frag.appendChild(buildDbCard(db, idx, idx === activeIdx)));

	elList.innerHTML = '';
	elList.appendChild(frag);
}

function buildDbCard(db, idx, isActive)
{
	const card     = document.createElement('button');
	card.type      = 'button';
	card.className = 'db-card' + (isActive ? ' active' : '');
	card.setAttribute('aria-label', `Open ${db.file_name}${isActive ? ' (current)' : ''}`);

	const owner = db.user || db.uid;
	const group = db.group || db.gid;

	card.innerHTML = `
		<div class="db-card-top">
			<span class="db-card-name">🛢️ ${esc(db.file_name)}</span>
			<span class="db-card-time">${relativeTime(db.mTime_sec)}</span>
		</div>
		<div class="db-card-meta">
			<span class="db-card-perm">${permString(db.mode)} ${esc(String(owner))}:${esc(String(group))}</span>
			<span class="db-card-date">${absoluteTime(db.mTime_sec)}</span>
		</div>
		${isActive ? '<span class="db-card-badge">Current</span>' : ''}
	`;

	card.addEventListener('click', () => selectDatabase(idx, isActive));
	return card;
}

function selectDatabase(idx, isActive)
{
	setActiveDbIndex(idx);
	if (isActive) {
		// Already the active DB — just go browse it, no need to reload.
		location.hash = '#browse';
		return;
	}
	// Every view module caches the previously-loaded categories/state at
	// init time, so hot-swapping the dataset safely would mean threading a
	// reload through browse/search/sidebar/panel/random/info all at once.
	// A full reload is simpler and avoids a whole class of stale-state
	// bugs — this is a "switch workspace" action, not a frequent one.
	location.hash = '#browse';
	location.reload();
}

// ── Formatting helpers ──────────────────────

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
