// =============================================
// info.js  —  Database info view + health check
// =============================================

'use strict';

import {esc} from './data.js';
import {checkAllBookmarks, cancelCheck} from './health.js';

let healthResults         = [];
let healthCheckInProgress = false;

export function renderInfo(data)
{
	const categories = data.book_Marks || data.categories || [];
	const domainHash = data.book_mark_domain_hash || {};
	const tagHash    = data.book_mark_tag_hash || {};

	renderStats(categories, domainHash, tagHash);
	renderCategoryChart(categories);
	renderTagCloud(tagHash);
	renderDomainGrid(domainHash);
	renderHealthCheck(categories);
}

function renderHealthCheck(categories)
{
	const container = document.getElementById('health-check');
	if (!container)
		return;

	const allBookmarks = categories.flatMap(c => c.bookmarks || []);
	const uniqueUrls   = new Set(allBookmarks.map(b => b.url)).size;

	container.innerHTML = `
		<section class="info-section full-width">
			<h2 class="section-title">Link Health <span id="health-status" class="section-count"></span></h2>
			<div class="health-controls">
				<button id="health-run" class="health-btn primary" ${
		healthCheckInProgress ? 'disabled' : ''}>
					${healthCheckInProgress ? '⏳ Checking...' : '🔍 Check All Links'}
				</button>
				<button id="health-cancel" class="health-btn secondary" ${
		!healthCheckInProgress ? 'hidden' : ''}>
					✕ Cancel
				</button>
				<div id="health-progress" class="health-progress" hidden>
					<div class="health-progress-bar"><div class="health-progress-fill" style="width: 0%"></div></div>
					<span class="health-progress-text">0 / ${uniqueUrls}</span>
				</div>
			</div>
			<div id="health-summary" class="health-summary" hidden></div>
			<div id="health-details" class="health-details"></div>
		</section>
	`;

	if (healthResults.length) {
		renderHealthResults();
	}

	bindHealthEvents(categories);
}

function bindHealthEvents(categories)
{
	const runBtn    = document.getElementById('health-run');
	const cancelBtn = document.getElementById('health-cancel');

	runBtn?.addEventListener('click', () => runHealthCheck(categories));
	cancelBtn?.addEventListener('click', cancelHealthCheck);
}

async function runHealthCheck(categories)
{
	healthCheckInProgress = true;
	healthResults         = [];
	const allBookmarks    = categories.flatMap(c => c.bookmarks || []);
	const uniqueUrls      = new Set(allBookmarks.map(b => b.url)).size;

	updateHealthUI(true, 0, uniqueUrls);

	try {
		healthResults = await checkAllBookmarks(categories, {
			concurrency: 5,
			progress: (url, checked, total) => updateHealthUI(true, checked, total, url),
			complete: (results)             => {
			    healthCheckInProgress = false;
			    renderHealthResults();
			    updateHealthUI(false, results.length, uniqueUrls);
			}
		});
	} catch (err) {
		healthCheckInProgress = false;
		updateHealthUI(false, 0, uniqueUrls);
	}
}

function cancelHealthCheck()
{
	cancelCheck();
	healthCheckInProgress = false;
	const uniqueUrls      = healthResults.length;
	updateHealthUI(false, 0, uniqueUrls);
}

function updateHealthUI(inProgress, checked, total, currentUrl = '')
{
	const runBtn       = document.getElementById('health-run');
	const cancelBtn    = document.getElementById('health-cancel');
	const progressEl   = document.getElementById('health-progress');
	const progressFill = progressEl?.querySelector('.health-progress-fill');
	const progressText = progressEl?.querySelector('.health-progress-text');
	const statusEl     = document.getElementById('health-status');

	if (runBtn) {
		runBtn.disabled    = inProgress;
		runBtn.textContent = inProgress ? '⏳ Checking...' : '🔍 Check All Links';
	}
	if (cancelBtn)
		cancelBtn.hidden = !inProgress;
	if (progressEl)
		progressEl.hidden = !inProgress;

	if (progressFill)
		progressFill.style.width = total ? `${(checked / total) * 100}%` : '0%';
	if (progressText)
		progressText.textContent = `${checked} / ${total}`;
	if (statusEl)
		statusEl.textContent = inProgress ? ` (${checked}/${total})` : '';
}

function renderHealthResults()
{
	const summaryEl = document.getElementById('health-summary');
	const detailsEl = document.getElementById('health-details');

	const summary = {
		ok: healthResults.filter(r => r.category === 'ok').length,
		reachable: healthResults.filter(r => r.category === 'reachable').length,
		redirect: healthResults.filter(r => r.category === 'redirect').length,
		clientError: healthResults.filter(r => r.category === 'client-error').length,
		serverError: healthResults.filter(r => r.category === 'server-error').length,
		error: healthResults.filter(r => r.category === 'error').length,
		total: healthResults.length
	};

	summaryEl.hidden    = false;
	summaryEl.innerHTML = `
		<div class="health-stats">
			<span class="health-stat ok">✅ ${summary.ok} OK</span>
			<span class="health-stat reachable" title="Cross-origin sites don't expose their real status to browser JS — we can only confirm the server answered.">🌐 ${
		summary.reachable} Reachable</span>
			<span class="health-stat redirect">↪️ ${summary.redirect} Redirect</span>
			<span class="health-stat client-error">⚠️ ${summary.clientError} Client Error</span>
			<span class="health-stat server-error">❌ ${summary.serverError} Server Error</span>
			<span class="health-stat error">💥 ${summary.error} Failed</span>
			<span class="health-stat total">📊 ${summary.total} Total</span>
		</div>
	`;

	// Group by category
	const byCategory = {};
	for (const result of healthResults) {
		byCategory[result.category] = byCategory[result.category] || [];
		byCategory[result.category].push(result);
	}

	const order = ['error', 'server-error', 'client-error', 'redirect', 'ok', 'reachable'];
	let   html
	    = '<table class="health-table"><thead><tr><th>Status</th><th>URL</th><th>Details</th></tr></thead><tbody>';
	for (const cat of order) {
		if (!byCategory[cat])
			continue;
		for (const r of byCategory[cat]) {
			const statusIcon = {
				ok: '✅',
				reachable: '🌐',
				redirect: '↪️',
				'client-error': '⚠️',
				'server-error': '❌',
				error: '💥',
				cancelled: '⏹️'
			}[r.category] || '❓';
			const details  = r.status ? `HTTP ${r.status}`
				             : r.category === 'reachable'
				                 ? 'Server answered — cross-origin, real status unavailable'
				                 : (r.error || '—');
            html += `<tr class="health-row ${r.category}"><td>${statusIcon} ${
                r.category}</td><td><a href="${esc(r.url)}" target="_blank" rel="noopener">${
                esc(r.url)}</a></td><td>${details}</td></tr>`;
		}
	}
	html                += '</tbody></table>';
	detailsEl.innerHTML  = html;
}

function renderStats(categories, domainHash, tagHash)
{
	const allBookmarks = categories.flatMap(c => c.bookmarks || []);
	set('stat-total', allBookmarks.length);
	set('stat-unique', new Set(allBookmarks.map(b => b.url)).size);
	set('stat-cats', categories.length);
	set('stat-domains', Object.keys(domainHash).length);
	set('stat-tags', Object.keys(tagHash).length);
}

function renderCategoryChart(categories)
{
	const container = document.getElementById('cat-breakdown');
	const counts    = categories.map(c => (c.bookmarks || []).length);
	const max       = Math.max(...counts, 1);

	const frag = document.createDocumentFragment();
	categories.forEach((cat, i) => {
		const pct     = Math.round(counts[i] / max * 100);
		const row     = document.createElement('div');
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

function renderTagCloud(tagHash)
{
	const container = document.getElementById('tag-breakdown');
	const sorted    = Object.entries(tagHash).sort((a, b) => b[1] - a[1]);

	if (!sorted.length) {
		container.innerHTML = '<p style="color:var(--muted);font-size:12px">No tags found.</p>';
		return;
	}

	const INITIAL_COUNT = 35;
	let   expanded      = false;

	function render()
	{
		const visible     = expanded ? sorted : sorted.slice(0, INITIAL_COUNT);
		const hiddenCount = Math.max(0, sorted.length - INITIAL_COUNT);

		const frag      = document.createDocumentFragment();
		const cloud     = document.createElement('div');
		cloud.className = 'tag-cloud';

		visible.forEach(([tag, count]) => {
			const item     = document.createElement('div');
			item.className = 'tag-cloud-item';
			item.innerHTML = `${esc(tag)}<span class="tc-count">${count}</span>`;
			item.addEventListener('click', () => {
				window.location.href = `#browse?q=${encodeURIComponent(tag)}`;
			});
			cloud.appendChild(item);
		});

		frag.appendChild(cloud);

		if (hiddenCount > 0) {
			const toggle       = document.createElement('div');
			toggle.className   = 'tag-cloud-toggle';
			toggle.textContent = expanded ? '▼ Show less' : `▶ +${hiddenCount} more tags`;
			toggle.addEventListener('click', () => {
				expanded = !expanded;
				render();
			});
			frag.appendChild(toggle);
		}

		container.innerHTML = '';
		container.appendChild(frag);
	}

	render();
}

function renderDomainGrid(domainHash)
{
	const domains   = Object.entries(domainHash).sort((a, b) => b[1] - a[1]);
	const container = document.getElementById('domain-grid');
	const label     = document.getElementById('domain-count-label');

	if (label)
		label.textContent = domains.length;

	if (!domains.length) {
		container.innerHTML
		    = '<p style="color:var(--muted);font-size:12px">No domain data in book_mark_domain_hash.</p>';
		return;
	}

	const frag = document.createDocumentFragment();
	domains.forEach(([domain, count]) => {
		const card     = document.createElement('div');
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

function set(id, val)
{
	const el = document.getElementById(id);
	if (el)
		el.textContent = val;
}
