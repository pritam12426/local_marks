// =============================================
// keyboard.js  —  Vim-style keyboard navigation
// =============================================

'use strict';

const state = {
	focusedCardIndex: -1,
	cards: [],
	bookmarkListEl: null,
	catListEl: null,
	searchEl: null,
	clearEl: null,
	helpModal: null
};

export function initKeyboard(cfg)
{
	state.bookmarkListEl = cfg.bookmarkListEl;
	state.catListEl      = cfg.catListEl;
	state.searchEl       = cfg.searchEl;
	state.clearEl        = cfg.clearEl;

	document.addEventListener('keydown', handleGlobalKeys);
	state.bookmarkListEl.addEventListener('keydown', handleListKeys);
	state.catListEl.addEventListener('keydown', handleSidebarKeys);
	state.searchEl.addEventListener('input', handleSearchInput);
	state.searchEl.addEventListener('keydown', handleSearchKeydown);
}

export function refreshCards()
{
	state.cards = Array.from(state.bookmarkListEl.querySelectorAll('.bookmark-card'));
	if (state.focusedCardIndex >= state.cards.length)
		state.focusedCardIndex = state.cards.length - 1;
	if (state.focusedCardIndex < -1)
		state.focusedCardIndex = -1;
	updateCardFocus();
}

export function focusFirstCard()
{
	refreshCards();
	if (state.cards.length) {
		state.focusedCardIndex = 0;
		updateCardFocus();
		state.cards[0].focus({preventScroll: true});
		state.cards[0].scrollIntoView({behavior: 'smooth', block: 'nearest'});
	}
}

export function focusLastCard()
{
	refreshCards();
	if (state.cards.length) {
		state.focusedCardIndex = state.cards.length - 1;
		updateCardFocus();
		state.cards[state.focusedCardIndex].focus({preventScroll: true});
		state.cards[state.focusedCardIndex].scrollIntoView({behavior: 'smooth', block: 'nearest'});
	}
}

export function focusNextCard()
{
	refreshCards();
	if (state.cards.length === 0)
		return;
	if (state.focusedCardIndex < state.cards.length - 1) {
		state.focusedCardIndex++;
		updateCardFocus();
		state.cards[state.focusedCardIndex].focus({preventScroll: true});
		state.cards[state.focusedCardIndex].scrollIntoView({behavior: 'smooth', block: 'nearest'});
	}
}

export function focusPrevCard()
{
	refreshCards();
	if (state.cards.length === 0)
		return;
	if (state.focusedCardIndex > 0) {
		state.focusedCardIndex--;
		updateCardFocus();
		state.cards[state.focusedCardIndex].focus({preventScroll: true});
		state.cards[state.focusedCardIndex].scrollIntoView({behavior: 'smooth', block: 'nearest'});
	} else if (state.focusedCardIndex === 0) {
		blurCards();
		focusSidebar();
	}
}

export function blurCards()
{
	state.focusedCardIndex = -1;
	updateCardFocus();
}

function updateCardFocus()
{
	state.cards.forEach((card, i) => {
		const isFocused = i === state.focusedCardIndex;
		card.classList.toggle('focused', isFocused);
		if (isFocused) {
			card.setAttribute('aria-selected', 'true');
			card.tabIndex = 0;
		} else {
			card.removeAttribute('aria-selected');
			card.tabIndex = -1;
		}
	});
}

function focusSidebar()
{
	blurCards();
	const activeLi = state.catListEl.querySelector('li.active');
	if (activeLi) {
		activeLi.focus();
		activeLi.scrollIntoView({behavior: 'smooth', block: 'nearest'});
	}
}

function isHelpOpen()
{
	return !!(state.helpModal && state.helpModal.classList.contains('open'));
}

function ensureHelpModal()
{
	if (state.helpModal)
		return state.helpModal;

	const overlay     = document.createElement('div');
	overlay.className = 'modal-overlay';
	overlay.setAttribute('role', 'dialog');
	overlay.setAttribute('aria-modal', 'true');
	overlay.setAttribute('aria-label', 'Keyboard shortcuts');

	overlay.innerHTML = `
		<div class="modal-box">
			<button class="modal-close" aria-label="Close">✕</button>
			<div class="keyboard-help">
				<h3>⌨️ Keyboard Shortcuts</h3>
				<table>
					<tr><td><kbd>j</kbd> / <kbd>↓</kbd></td><td>Next bookmark</td></tr>
					<tr><td><kbd>k</kbd> / <kbd>↑</kbd></td><td>Previous bookmark</td></tr>
					<tr><td><kbd>h</kbd> / <kbd>←</kbd></td><td>Back to categories</td></tr>
					<tr><td><kbd>l</kbd> / <kbd>→</kbd></td><td>Into bookmarks</td></tr>
					<tr><td><kbd>gg</kbd></td><td>Jump to first bookmark</td></tr>
					<tr><td><kbd>G</kbd> (<kbd>Shift+G</kbd>)</td><td>Jump to last bookmark</td></tr>
					<tr><td><kbd>/</kbd></td><td>Focus search</td></tr>
					<tr><td><kbd>Enter</kbd></td><td>Open focused bookmark (new tab)</td></tr>
					<tr><td><kbd>o</kbd></td><td>Open focused bookmark (same tab)</td></tr>
					<tr><td><kbd>yy</kbd></td><td>Copy URL to clipboard</td></tr>
					<tr><td><kbd>p</kbd></td><td>Pin/unpin bookmark</td></tr>
					<tr><td><kbd>Esc</kbd></td><td>Clear search / close this window</td></tr>
					<tr><td><kbd>?</kbd></td><td>Toggle this help window</td></tr>
					<tr><td><kbd>Ctrl/Cmd+K</kbd></td><td>Focus search</td></tr>
				</table>
			</div>
		</div>
	`;

	overlay.addEventListener('click', e => {
		if (e.target === overlay)
			closeKeyboardHelp();
	});
	overlay.querySelector('.modal-close').addEventListener('click', closeKeyboardHelp);
	overlay.addEventListener('keydown', e => {
		if (e.key === 'Escape') {
			e.stopPropagation();
			closeKeyboardHelp();
		}
	});

	document.body.appendChild(overlay);
	state.helpModal = overlay;
	return overlay;
}

function showKeyboardHelp()
{
	const modal = ensureHelpModal();
	modal.classList.add('open');
	modal.querySelector('.modal-close').focus();
}

function closeKeyboardHelp()
{
	if (!state.helpModal)
		return;
	state.helpModal.classList.remove('open');
	if (state.focusedCardIndex >= 0 && state.cards[state.focusedCardIndex]) {
		state.cards[state.focusedCardIndex].focus({preventScroll: true});
	} else {
		focusSidebar();
	}
}

// Destroy keyboard module: remove event listeners and modal
function destroyKeyboard()
{
	document.removeEventListener('keydown', handleGlobalKeys);
	if (state.bookmarkListEl)
		state.bookmarkListEl.removeEventListener('keydown', handleListKeys);
	if (state.catListEl)
		state.catListEl.removeEventListener('keydown', handleSidebarKeys);
	if (state.searchEl) {
		state.searchEl.removeEventListener('input', handleSearchInput);
		state.searchEl.removeEventListener('keydown', handleSearchKeydown);
	}
	if (state.helpModal) {
		state.helpModal.remove();
		state.helpModal = null;
	}
}

function handleGlobalKeys(e)
{
	if (isHelpOpen()) {
		if (e.key === 'Escape' || e.key === '?') {
			e.preventDefault();
			closeKeyboardHelp();
		}
		return;
	}

	const inField = e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA'
	                || e.target.isContentEditable;

	if (e.key === '/' && !inField) {
		e.preventDefault();
		state.searchEl.focus();
		state.searchEl.select();
		return;
	}

	if (!document.body.classList.contains('route-browse'))
		return;

	if (inField) {
		// Escape-while-search-focused is handled by browse.js's own listener
		// (onClearSearch does the full reset: tags, sidebar, panel). Don't
		// also clear it here, or both fire and renderPanel() runs twice.
		return;
	}

	switch (e.key.toLowerCase()) {
	case 'j':
	case 'arrowdown':
		e.preventDefault();
		focusNextCard();
		break;
	case 'k':
	case 'arrowup':
		e.preventDefault();
		focusPrevCard();
		break;
	case 'h':
	case 'arrowleft':
		e.preventDefault();
		focusSidebar();
		break;
	case 'l':
	case 'arrowright':
		e.preventDefault();
		if (state.focusedCardIndex === -1)
			focusFirstCard();
		break;
	case 'g':
		if (e.shiftKey) {
			e.preventDefault();
			focusLastCard();
		} else if (state.lastKey === 'g') {
			e.preventDefault();
			focusFirstCard();
		}
		break;
	case '?':
		e.preventDefault();
		showKeyboardHelp();
		break;
	case 'o':
		if (state.focusedCardIndex >= 0 && state.cards[state.focusedCardIndex]) {
			e.preventDefault();
			window.open(state.cards[state.focusedCardIndex].href, '_blank', 'noopener,noreferrer');
		}
		break;
	case 'p':
		if (state.focusedCardIndex >= 0 && state.cards[state.focusedCardIndex]) {
			e.preventDefault();
			const star = state.cards[state.focusedCardIndex].querySelector('.bm-star');
			if (star)
				star.click();
		}
		break;
	case 'y':
		if (state.lastKey === 'y') {
			e.preventDefault();
			if (state.focusedCardIndex >= 0 && state.cards[state.focusedCardIndex]) {
				const url = state.cards[state.focusedCardIndex].href;
				navigator.clipboard.writeText(url).then(
				    () => { showToast(`Copied: ${getDomain(url)}`); });
			}
		}
		break;
	case 'enter':
		if (state.focusedCardIndex >= 0 && state.cards[state.focusedCardIndex]) {
			e.preventDefault();
			state.cards[state.focusedCardIndex].click();
		}
		break;
	case 'escape':
		clearSearch();
		blurCards();
		break;
	}
	state.lastKey = e.key.toLowerCase();
}

let searchDebounceTimer = null;

function handleSearchInput()
{
	clearTimeout(searchDebounceTimer);
	searchDebounceTimer = setTimeout(() => {
		const query = state.searchEl.value.trim();
		state.clearEl.classList.toggle('visible', query.length > 0);

		if (query && !document.body.classList.contains('route-browse')) {
			location.hash = '#browse';
		}

		if (!query) {
			window.dispatchEvent(new CustomEvent('search-query-empty'));
		} else {
			window.dispatchEvent(new CustomEvent('search-query-changed', {detail: {query}}));
		}
	}, 300);
}

function handleSearchKeydown(e)
{
	if (e.key === 'Enter') {
		e.preventDefault();
		clearTimeout(searchDebounceTimer);
		const query = state.searchEl.value.trim();
		state.clearEl.classList.toggle('visible', query.length > 0);

		if (query && !document.body.classList.contains('route-browse')) {
			location.hash = '#browse';
		}

		if (!query) {
			window.dispatchEvent(new CustomEvent('search-query-empty'));
		} else {
			window.dispatchEvent(new CustomEvent('search-query-changed', {detail: {query}}));
		}

		state.searchEl.blur();
		setTimeout(() => focusFirstCard(), 50);
	}
}

function handleListKeys(e)
{
	if (e.target.closest('.bm-star') || e.target.closest('.bm-tag'))
		return;

	switch (e.key) {
	case 'j':
	case 'ArrowDown':
		e.preventDefault();
		e.stopPropagation();
		focusNextCard();
		break;
	case 'k':
	case 'ArrowUp':
		e.preventDefault();
		e.stopPropagation();
		focusPrevCard();
		break;
	case 'Enter':
		if (state.focusedCardIndex >= 0 && state.cards[state.focusedCardIndex]) {
			e.preventDefault();
			e.stopPropagation();
			state.cards[state.focusedCardIndex].click();
		}
		break;
	case 'Escape':
		e.stopPropagation();
		blurCards();
		state.searchEl.focus();
		break;
	}
}

function handleSidebarKeys(e)
{
	if (e.key !== 'j' && e.key !== 'k' && e.key !== 'ArrowDown' && e.key !== 'ArrowUp')
		return;

	const items = Array.from(state.catListEl.querySelectorAll('li'));
	if (!items.length)
		return;

	const current = items.indexOf(document.activeElement);
	const step    = (e.key === 'j' || e.key === 'ArrowDown') ? 1 : -1;
	const next    = current + step;

	if (next < 0 || next >= items.length)
		return;

	e.preventDefault();
	e.stopPropagation();

	items[next].focus();
	items[next].scrollIntoView({behavior: 'smooth', block: 'nearest'});
	items[next].click();
}

function clearSearch()
{
	state.searchEl.value = '';
	state.clearEl.classList.remove('visible');
	window.dispatchEvent(new CustomEvent('search-cleared'));
}

function getDomain(url)
{
	try {
		return new URL(url).hostname.replace('www.', '');
	} catch {
		return url;
	}
}

function showToast(message)
{
	// Remove existing toast if any
	const existing = document.querySelector('.kb-toast');
	if (existing)
		existing.remove();

	const toast       = document.createElement('div');
	toast.className   = 'kb-toast';
	toast.textContent = message;
	document.body.appendChild(toast);

	// Force reflow for animation
	toast.offsetHeight;
	toast.classList.add('show');

	setTimeout(() => {
		toast.classList.remove('show');
		setTimeout(() => toast.remove(), 200);
	}, 1500);
}
