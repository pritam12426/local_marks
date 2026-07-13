// =============================================
// health.js  —  Bookmark link health checker
// =============================================

'use strict';

let abortController = null;

export async function checkAllBookmarks(categories, options = {})
{
	const {concurrency = 5, progress, complete} = options;
	const allBookmarks                          = categories.flatMap(
        c => (c.bookmarks || []).map(b => ({...b, category: c.category})));
	const urls = [...new Set(allBookmarks.map(b => b.url))];

	abortController = new AbortController();
	const signal    = abortController.signal;

	const results = [];
	const queue   = [...urls];
	let   checked = 0;

	async function worker()
	{
		while (queue.length > 0) {
			if (signal.aborted)
				break;
			const url = queue.shift();
			if (!url)
				continue;

			const result = await checkUrl(url, signal);
			results.push(result);
			checked++;
			progress?.(url, checked, urls.length);
		}
	}

	const workers = Array.from({length: Math.min(concurrency, urls.length)}, () => worker());
	await Promise.all(workers);

	if (complete)
		complete(results);
	return results;
}

export function cancelCheck()
{
	if (abortController) {
		abortController.abort();
		abortController = null;
	}
}

async function checkUrl(url, signal)
{
	if (isSameOrigin(url))
		return await checkSameOrigin(url, signal);
	return await checkCrossOrigin(url, signal);
}

function isSameOrigin(url)
{
	try {
		return new URL(url, location.href).origin === location.origin;
	} catch {
		return false;
	}
}

// Same-origin: a normal HEAD isn't subject to CORS, so we get a real,
// readable status code and can genuinely tell OK from broken.
async function checkSameOrigin(url, signal)
{
	try {
		const controller = new AbortController();
		const timeout    = setTimeout(() => controller.abort(), 10000);

		const response = await fetch(url, {
			method: 'HEAD',
			signal: AbortSignal.any([signal, controller.signal])
		});

		clearTimeout(timeout);

		const status = response.status;
		let   category;
		if (status >= 200 && status < 300)
			category = 'ok';
		else if (status >= 300 && status < 400)
			category = 'redirect';
		else if (status >= 400 && status < 500)
			category = 'client-error';
		else if (status >= 500)
			category = 'server-error';
		else
			category = 'ok';

		return {url, category, status};
	} catch (err) {
		if (err.name === 'AbortError' || signal.aborted)
			return {url, category: 'cancelled'};
		return {url, category: 'error', error: err.message || 'Network error'};
	}
}

// Cross-origin: almost no third-party site sends Access-Control-Allow-Origin
// for an anonymous request, so a normal 'cors' HEAD is guaranteed to fail —
// it would only spam devtools with CORS warnings we can't act on, for zero
// benefit. Go straight to a no-cors HEAD instead. This can only tell us
// "the network round-trip completed" vs. "it didn't" — a 404 or 500 error
// page still completes and looks identical to a working page in no-cors
// mode, since the response body/status are opaque to JS. So a success here
// is honestly labeled 'reachable', not a fabricated 200 OK.
async function checkCrossOrigin(url, signal)
{
	try {
		const controller = new AbortController();
		const timeout    = setTimeout(() => controller.abort(), 10000);

		await fetch(url, {
			method: 'HEAD',
			mode: 'no-cors',
			signal: AbortSignal.any([signal, controller.signal])
		});

		clearTimeout(timeout);
		return {url, category: 'reachable', status: null};
	} catch (err) {
		if (err.name === 'AbortError' || signal.aborted)
			return {url, category: 'cancelled'};
		// Network error, domain unreachable, blocked by an extension, etc.
		return {url, category: 'error', error: err.message || 'Network error'};
	}
}
