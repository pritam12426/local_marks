// =============================================
// sw.js  —  Service Worker for offline support
// =============================================

const CACHE_NAME = 'localmarks-v1';
const STATIC_ASSETS = [
	'/',
	'/index.html',
	'/bookmarks.json',
	'/favicon.ico',
	'/javascript/main.js',
	'/javascript/data.js',
	'/javascript/browse.js',
	'/javascript/sidebar.js',
	'/javascript/panel.js',
	'/javascript/tag_bar.js',
	'/javascript/search.js',
	'/javascript/keyboard.js',
	'/javascript/info.js',
	'/javascript/random.js',
	'/javascript/health.js',
	'/stylesheet/style.css',
	'/stylesheet/themes/light.css'
];

// Install - cache static assets
self.addEventListener('install', event => {
	event.waitUntil(
		caches.open(CACHE_NAME)
			.then(cache => cache.addAll(STATIC_ASSETS))
			.then(() => self.skipWaiting())
	);
});

// Activate - clean old caches
self.addEventListener('activate', event => {
	event.waitUntil(
		caches.keys()
			.then(keys => Promise.all(
				keys.filter(key => key !== CACHE_NAME)
					.map(key => caches.delete(key))
			))
			.then(() => self.clients.claim())
	);
});

// Fetch - cache-first for static, network-first for bookmarks.json
self.addEventListener('fetch', event => {
	const {request} = event;
	const url = new URL(request.url);

	// Only handle same-origin requests
	if (url.origin !== location.origin) return;

	// Network-first for bookmarks.json (always want fresh data)
	if (url.pathname === '/bookmarks.json') {
		event.respondWith(networkFirst(request));
		return;
	}

	// Cache-first for everything else
	event.respondWith(cacheFirst(request));
});

async function networkFirst(request) {
	const cache = await caches.open(CACHE_NAME);
	try {
		const response = await fetch(request);
		if (response.ok) {
			cache.put(request, response.clone());
		}
		return response;
	} catch (err) {
		const cached = await cache.match(request);
		return cached || new Response('Offline', {status: 503, statusText: 'Offline'});
	}
}

async function cacheFirst(request) {
	const cache = await caches.open(CACHE_NAME);
	const cached = await cache.match(request);
	if (cached) return cached;

	try {
		const response = await fetch(request);
		if (response.ok) {
			cache.put(request, response.clone());
		}
		return response;
	} catch (err) {
		return new Response('Offline', {status: 503, statusText: 'Offline'});
	}
}

// Listen for messages from main thread
self.addEventListener('message', event => {
	if (event.data === 'skipWaiting') {
		self.skipWaiting();
	}
});
