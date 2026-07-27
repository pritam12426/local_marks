// =============================================
// modal.js  —  Reusable modal overlay
// =============================================

'use strict';

let overlay = null;

function ensureOverlay()
{
	if (overlay) return overlay;

	overlay = document.createElement('div');
	overlay.className = 'modal-overlay';
	overlay.setAttribute('role', 'dialog');
	overlay.setAttribute('aria-modal', 'true');
	overlay.innerHTML = `<div class="modal-box"></div>`;

	overlay.addEventListener('click', e => {
		if (e.target === overlay)
			closeModal();
	});

	document.addEventListener('keydown', e => {
		if (e.key === 'Escape' && overlay && overlay.classList.contains('open'))
			closeModal();
	});

	document.body.appendChild(overlay);
	return overlay;
}

export function openModal(html)
{
	const el   = ensureOverlay();
	const box  = el.querySelector('.modal-box');
	box.innerHTML = html;
	el.classList.add('open');

	const firstInput = box.querySelector('textarea, input, button');
	if (firstInput) firstInput.focus();

	return box;
}

export function closeModal()
{
	if (overlay) {
		overlay.classList.remove('open');
		const box = overlay.querySelector('.modal-box');
		if (box) box.innerHTML = '';
	}
}

export function isModalOpen()
{
	return overlay && overlay.classList.contains('open');
}
