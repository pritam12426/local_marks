// =============================================
// random.js  —  Random bookmark picker view
// =============================================

'use strict';

import {esc, buildCard} from './data.js';

let allBookmarks = [];
let lastPicked   = [];
let elCount, elCat, elTag, elGo, elResults, elInfo, elOpenAll;

export function initRandom(data)
{
	elCount   = document.getElementById('random-count');
	elCat     = document.getElementById('random-cat');
	elTag     = document.getElementById('random-tag');
	elGo      = document.getElementById('random-go');
	elResults = document.getElementById('random-results');
	elInfo    = document.getElementById('random-info');
	elOpenAll = document.getElementById('random-open-all');

	const cats   = data.book_Marks || data.categories || [];
	allBookmarks = cats.flatMap(
	    c => (c.bookmarks || []).map(bm => ({...bm, _category: c.category || ''})));

	const allTags  = [...new Set(allBookmarks.flatMap(b => b.tags || []))].sort();
	const datalist = document.createElement('datalist');
	datalist.id    = 'tag-suggestions';
	const frag     = document.createDocumentFragment();
	allTags.forEach(t => {
		const opt = document.createElement('option');
		opt.value = t;
		frag.appendChild(opt);
	});
	datalist.appendChild(frag);
	document.body.appendChild(datalist);
	elTag.setAttribute('list', 'tag-suggestions');

	const catNames = [...new Set(allBookmarks.map(b => b._category).filter(Boolean))].sort();
	const catFrag  = document.createDocumentFragment();
	catNames.forEach(name => {
		const opt       = document.createElement('option');
		opt.value       = name;
		opt.textContent = name;
		catFrag.appendChild(opt);
	});
	elCat.appendChild(catFrag);

	elGo.addEventListener('click', render);
	elCount.addEventListener('keydown', e => {
		if (e.key === 'Enter')
			render();
	});
	elCat.addEventListener('change', render);
	elTag.addEventListener('keydown', e => {
		if (e.key === 'Enter')
			render();
	});
	elOpenAll.addEventListener('click', openAll);
}

export function renderRandom()
{
	render();
}

// ── Pick logic ──

function pickRandom(arr, k)
{
	const copy   = [...arr];
	const result = [];
	const count  = Math.min(k, copy.length);
	for (let i = 0; i < count; i++) {
		const j            = i + Math.floor(Math.random() * (copy.length - i));
		[copy[i], copy[j]] = [copy[j], copy[i]];
		result.push(copy[i]);
	}
	return result;
}

// ── Render ──

function render()
{
	const n = parseInt(elCount.value, 10);
	if (!n || n < 1)
		return;
	elGo.textContent = '🎲 Reroll';

	const catFilter = elCat.value;
	const tagFilter = elTag.value.trim().toLowerCase();

	let pool = allBookmarks;
	if (catFilter)
		pool = pool.filter(bm => bm._category === catFilter);
	if (tagFilter)
		pool = pool.filter(bm => (bm.tags || []).some(t => t.toLowerCase().includes(tagFilter)));

	const picked        = pickRandom(pool, n);
	lastPicked          = picked;
	elResults.innerHTML = '';

	if (!picked.length) {
		elInfo.textContent = '';
		elOpenAll.hidden   = true;
		const parts        = [];
		if (catFilter)
			parts.push(`in "${esc(catFilter)}"`);
		if (tagFilter)
			parts.push(`tagged "${esc(elTag.value.trim())}"`);
		elResults.innerHTML
		    = `<div class="state-empty"><div class="state-icon">📭</div><p>No bookmarks ${
			    parts.join(' ')}.</p></div>`;
		return;
	}

	elInfo.textContent = `🎲 ${picked.length} of ${pool.length} bookmark${
		pool.length === 1 ? '' : 's'}`;
	elOpenAll.hidden = false;

	const frag = document.createDocumentFragment();
	picked.forEach(bm => frag.appendChild(buildCard(bm)));
	elResults.appendChild(frag);
}

function openAll()
{
	lastPicked.forEach((bm, i) => { setTimeout(() => window.open(bm.url, '_blank'), i * 150); });
}
