// =============================================
// info.js  —  Database info view
// =============================================

'use strict';

import { esc } from './data.js';

export function renderInfo(data) {
	const categories = data.book_Marks || data.categories || [];
	const domainHash = data.book_mark_domain_hash || {};
	const tagHash    = data.book_mark_tag_hash    || {};

	renderStats(categories, domainHash, tagHash);
	renderCategoryChart(categories);
	renderTagCloud(tagHash);
	renderDomainGrid(domainHash);
}

// ── Stats strip ──

function renderStats(categories, domainHash, tagHash) {
	const allBookmarks = categories.flatMap(c => c.bookmarks || []);
	set('stat-total',   allBookmarks.length);
	set('stat-unique',  new Set(allBookmarks.map(b => b.url)).size);
	set('stat-cats',    categories.length);
	set('stat-domains', Object.keys(domainHash).length);
	set('stat-tags',    Object.keys(tagHash).length);
}

// ── Category breakdown ──

function renderCategoryChart(categories) {
	const container = document.getElementById('cat-breakdown');
	const counts    = categories.map(c => (c.bookmarks || []).length);
	const max       = Math.max(...counts, 1);

	const frag = document.createDocumentFragment();
	categories.forEach((cat, i) => {
		const pct = Math.round(counts[i] / max * 100);
		const row = document.createElement('div');
		row.className = 'cat-row';
		row.innerHTML = `
			<div class="cat-row-name" title="${esc(cat.category)}">📋 ${esc(cat.category)}</div>
			<div class="cat-row-track">
				<div class="cat-row-bar" style="width:${pct}%"></div>
			</div>
			<div class="cat-row-num">${counts[i]}</div>
		`;
		frag.appendChild(row);
	});

	container.innerHTML = '';
	container.appendChild(frag);
}

// ── Tag cloud ──

function renderTagCloud(tagHash) {
	const container = document.getElementById('tag-breakdown');
	const sorted    = Object.entries(tagHash).sort((a, b) => b[1] - a[1]);

	if (!sorted.length) {
		container.innerHTML = '<p style="color:var(--muted);font-size:12px">No tags found.</p>';
		return;
	}

	const INITIAL_COUNT = 35;
	let expanded = false;

	function render() {
		const visible     = expanded ? sorted : sorted.slice(0, INITIAL_COUNT);
		const hiddenCount = Math.max(0, sorted.length - INITIAL_COUNT);

		const frag  = document.createDocumentFragment();
		const cloud = document.createElement('div');
		cloud.className = 'tag-cloud';

		visible.forEach(([tag, count]) => {
			const item = document.createElement('div');
			item.className = 'tag-cloud-item';
			item.innerHTML = `${esc(tag)}<span class="tc-count">${count}</span>`;
			item.addEventListener('click', () => {
				window.location.href = `#browse?q=${encodeURIComponent(tag)}`;
			});
			cloud.appendChild(item);
		});

		frag.appendChild(cloud);

		if (hiddenCount > 0) {
			const toggle = document.createElement('div');
			toggle.className   = 'tag-cloud-toggle';
			toggle.textContent = expanded ? '▼ Show less' : `▶ +${hiddenCount} more tags`;
			toggle.addEventListener('click', () => { expanded = !expanded; render(); });
			frag.appendChild(toggle);
		}

		container.innerHTML = '';
		container.appendChild(frag);
	}

	render();
}

// ── Domain grid ──

function renderDomainGrid(domainHash) {
	const domains   = Object.entries(domainHash).sort((a, b) => b[1] - a[1]);
	const container = document.getElementById('domain-grid');
	const label     = document.getElementById('domain-count-label');

	if (label) label.textContent = domains.length;

	if (!domains.length) {
		container.innerHTML = '<p style="color:var(--muted);font-size:12px">No domain data in book_mark_domain_hash.</p>';
		return;
	}

	const frag = document.createDocumentFragment();
	domains.forEach(([domain, count]) => {
		const card = document.createElement('div');
		card.className = 'domain-card';
		card.title     = `Search all bookmarks from ${domain}`;
		card.innerHTML = `
			<div class="domain-card-header">
				<img src="https://www.google.com/s2/favicons?sz=32&domain=${esc(domain)}"
					alt="" loading="lazy" onerror="this.style.display='none'">
				<div class="domain-card-name">${esc(domain)}</div>
			</div>
			<div class="domain-card-count">${count}</div>
			<div class="domain-card-label">bookmark${count !== 1 ? 's' : ''}</div>
			<div class="domain-card-hint">🔍 search</div>
		`;
		card.addEventListener('click', () => {
			window.location.href = `#browse?q=${encodeURIComponent(domain)}`;
		});
		frag.appendChild(card);
	});

	container.innerHTML = '';
	container.appendChild(frag);
}

// ── Util ──

function set(id, val) {
	const el = document.getElementById(id);
	if (el) el.textContent = val;
}
