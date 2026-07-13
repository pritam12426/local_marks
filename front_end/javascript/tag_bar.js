// =============================================
// tag_bar.js  —  Tag filter bar
// =============================================

'use strict';

import {esc} from './data.js';

const state = {
	activeTags: new Set(),
	expanded: false,
	el: null
};

export function initTagBar(el) { state.el = el; }

export function getActiveTags() { return state.activeTags; }
export function setActiveTags(tags) { state.activeTags = tags; }
export function toggleExpanded() { state.expanded = !state.expanded; }
export function isExpanded() { return state.expanded; }

export function renderTagBar(tags)
{
	if (!state.el)
		return;
	state.el.innerHTML = '';
	if (!tags.length)
		return;

	const INITIAL_COUNT = 30;
	const frag          = document.createDocumentFragment();

	const label       = document.createElement('span');
	label.className   = 'tag-bar-label';
	label.textContent = 'Tags:';
	frag.appendChild(label);

	const visibleTags = state.expanded ? tags : tags.slice(0, INITIAL_COUNT);

	visibleTags.forEach(tag => {
		const pill       = document.createElement('span');
		pill.className   = 'tag-pill' + (state.activeTags.has(tag) ? ' active' : '');
		pill.textContent = tag;
		pill.addEventListener('click', () => {
			state.activeTags.has(tag) ? state.activeTags.delete(tag) : state.activeTags.add(tag);
			window.dispatchEvent(new CustomEvent('tag-filter-change'));
		});
		frag.appendChild(pill);
	});

	if (tags.length > INITIAL_COUNT) {
		const toggle       = document.createElement('button');
		toggle.className   = 'tag-clear';
		toggle.textContent = state.expanded ? '▼ Show less'
		                                    : `▶ +${tags.length - INITIAL_COUNT} more`;
		toggle.addEventListener('click', () => {
			state.expanded = !state.expanded;
			window.dispatchEvent(new CustomEvent('tag-bar-toggle'));
		});
		frag.appendChild(toggle);
	}

	if (state.activeTags.size) {
		const clr       = document.createElement('button');
		clr.className   = 'tag-clear';
		clr.textContent = 'Clear filters';
		clr.addEventListener('click', () => {
			state.activeTags.clear();
			window.dispatchEvent(new CustomEvent('tag-filter-change'));
		});
		frag.appendChild(clr);
	}

	state.el.appendChild(frag);
}
