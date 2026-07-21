#include "server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "api.h"
#include "auth.h"
#include "bookmark_cache.h"
#include "databases_meta.h"
#include "file.h"
#include "http.h"
#include "log.h"
#include "ratelimit.h"
#include "response.h"
#include "thread_pool.h"
#include "transport.h"

#ifdef SUPPORT_TLS_E
#include "tlse.h"
#endif  // SUPPORT_TLS_E

static atomic_int g_shutdown = 0;

static void handle_signal(int sig)
{
	LOG_INFO("Signal %d received, initiating graceful shutdown", sig);
	atomic_store_explicit(&g_shutdown, 1, memory_order_relaxed);
}

typedef struct {
	Transport      *t;
	char            client_ip[INET6_ADDRSTRLEN];
	int             client_port;
	ServerConfig    cfg;
	RateLimit      *rl;
} ClientJob;

static int wants_keep_alive(const HttpRequest *req, int keep_alive_timeout)
{
	if (keep_alive_timeout <= 0)
		return 0;
	int http11 = (strcmp(req->version, "HTTP/1.1") == 0);
	int conn_close = (strcasestr(req->connection, "close") != NULL);
	int conn_keep_alive = (strcasestr(req->connection, "keep-alive") != NULL);
	int ka = http11 ? !conn_close : conn_keep_alive;
	LOG_DEBUG("Keep-alive decision: %s (http11=%d, close=%d, keep-alive=%d, timeout=%d) -> %s",
	          req->version, http11, conn_close, conn_keep_alive,
	          keep_alive_timeout, ka ? "yes" : "no");
	return ka;
}

static void handle_client(void *arg)
{
	ClientJob *job = arg;
	Transport *t    = job->t;
	const char *client_ip  = job->client_ip;
	int         client_port = job->client_port;
	ServerConfig cfg        = job->cfg;

	int keep_alive = 0;
	do {
		HttpRequest req;
		if (http_parse_request(t, &req) != 0) {
			http_send_status(t, 400, "Bad Request",
			                 "<h1>400 Bad Request</h1>");
			break;
		}

		if (cfg.print_request) {
			LOG_CUSTOM(LOG_LEVEL_DEBUG, false,
			           "--- Request from %s:%d ---\n%.*s---\n",
			           client_ip, client_port,
			           (int)req.raw_len, req.raw);
		}

		if (cfg.user || cfg.pass) {
			if (!auth_check(&req, cfg.user, cfg.pass)) {
				LOG_INFO("%s:%d \"%s %s %s\" 401",
				         client_ip, client_port,
				         http_method_str(req.method),
				         req.path, req.version);
				auth_send_challenge(t);
				break;
			}
		}

		// Compute keep-alive for this request
		keep_alive = wants_keep_alive(&req, cfg.keep_alive_timeout);

		// Handle API endpoints first
		if (api_handle_request(&req, t, client_ip, client_port,
		                       &cfg, keep_alive, cfg.print_request)) {
			// API handled the request
		} else {
			// Fall through to file serving
			file_serve(&req, t, client_ip, client_port,
			           cfg.print_request, keep_alive);
		}

		if (!keep_alive) {
			LOG_DEBUG("Connection %s:%d closed (no keep-alive)", client_ip, client_port);
			break;
		}

		LOG_DEBUG("%s:%d keep-alive: reusing connection for next request", client_ip, client_port);
		transport_set_timeout(t, cfg.keep_alive_timeout);
	} while (!atomic_load_explicit(&g_shutdown, memory_order_relaxed));

	if (job->rl)
		ratelimit_leave(job->rl, client_ip);
	transport_destroy(&t);
	free(job);
}

static int make_listener(const char *host, int port)
{
	struct addrinfo hints = {
		.ai_family   = AF_UNSPEC,
		.ai_socktype = SOCK_STREAM,
		.ai_flags    = AI_PASSIVE,
	};
	char port_str[16];
	snprintf(port_str, sizeof port_str, "%d", port);

	struct addrinfo *res = NULL;
	int rc = getaddrinfo(host, port_str, &hints, &res);
	if (rc != 0) {
		LOG_ERROR("getaddrinfo(%s:%d): %s", host, port, gai_strerror(rc));
		return -1;
	}

	int lfd = -1;
	for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
		lfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (lfd < 0) continue;

		int one = 1;
		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);

		if (bind(lfd, ai->ai_addr, ai->ai_addrlen) == 0) break;

		close(lfd);
		lfd = -1;
	}
	freeaddrinfo(res);

	if (lfd < 0) {
		LOG_FATAL("Could not bind to %s:%d", host, port);
		return -1;
	}

	if (listen(lfd, 128) < 0) {
		LOG_PERROR("listen");
		close(lfd);
		return -1;
	}
	return lfd;
}

static void peer_addr(int fd, char *ip_buf, size_t ip_len, int *port_out)
{
	struct sockaddr_storage ss;
	socklen_t               sl = sizeof ss;

	if (getpeername(fd, (struct sockaddr *)&ss, &sl) != 0) {
		snprintf(ip_buf, ip_len, "?");
		*port_out = 0;
		return;
	}
	if (ss.ss_family == AF_INET) {
		struct sockaddr_in *s = (struct sockaddr_in *)&ss;
		inet_ntop(AF_INET, &s->sin_addr, ip_buf, (socklen_t)ip_len);
		*port_out = ntohs(s->sin_port);
	} else {
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)&ss;
		inet_ntop(AF_INET6, &s->sin6_addr, ip_buf, (socklen_t)ip_len);
		*port_out = ntohs(s->sin6_port);
	}
}

static void open_browser(const char *browser, const char *host, int port, bool tls)
{
	char url[256];
	snprintf(url, sizeof url, "http%s://%s:%d", tls ? "s" : "", host, port);
	LOG_INFO("Opening browser: %s %s", browser, url);

	pid_t pid = fork();
	if (pid == 0) {
		// Child: avoid LOG_* (inherits parent's locked mutexes → deadlock)
		execlp(browser, browser, url, (char *)NULL);
#if defined(__linux__)
		execlp("xdg-open", "xdg-open", url, (char *)NULL);
#elif defined(__APPLE__)
		execlp("open", "open", url, (char *)NULL);
#endif
		_exit(1);
	}

	if (pid < 0) {
		LOG_PERROR("fork for browser open");
	}
}

#ifdef SUPPORT_TLS_E
// Read a file into a malloc'd buffer. Caller must free().
static char *read_file_to_buffer(const char *path, size_t *out_len)
{
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;

	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	if (len < 0) { fclose(f); return NULL; }
	rewind(f);

	char *buf = malloc((size_t)len + 1);
	if (!buf) { fclose(f); return NULL; }

	size_t nread = fread(buf, 1, (size_t)len, f);
	fclose(f);

	buf[nread] = '\0';
	*out_len = nread;
	return buf;
}
#endif  // SUPPORT_TLS_E

int server_run(const ServerConfig *cfg)
{
	struct sigaction sa = { .sa_handler = handle_signal };
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

	int lfd = make_listener(cfg->host, cfg->port);
	if (lfd < 0) return -1;

	ThreadPool *pool = thread_pool_create(cfg->thread_pool_size);
	if (!pool) {
		LOG_FATAL("Failed to create thread pool");
		close(lfd);
		return -1;
	}

	RateLimit *rl = NULL;
	if (cfg->max_conns_per_ip > 0) {
		rl = ratelimit_create(cfg->max_conns_per_ip);
	}

	// Populate metadata for all bookmark database files
	populate_db_meta_all(cfg);

	// Initialize bookmark cache
	bookmark_cache_init();

	// Register all databases with the cache
	for (int i = 0; i < cfg->bookmark_file_count; i++) {
		if (cfg->bookmark_files[i]) {
			bookmark_cache_add_db(cfg->bookmark_files[i]);
		}
	}

#ifdef SUPPORT_TLS_E
	// TLS master context — created once, used to produce per-connection child contexts
	struct TLSContext *tls_master = NULL;
	if (cfg->tls_cert && cfg->tls_key) {
		tls_init();
		tls_master = tls_create_context(1, TLS_V12);
		if (!tls_master) {
			LOG_ERROR("Failed to create TLS context");
			goto fail_cleanup;
		}

		// Load certificate chain
		size_t cert_len = 0;
		char *cert_pem = read_file_to_buffer(cfg->tls_cert, &cert_len);
		if (!cert_pem) {
			LOG_ERROR("Failed to read TLS certificate: %s", cfg->tls_cert);
			tls_destroy_context(tls_master);
			goto fail_cleanup;
		}
		if (tls_load_certificates(tls_master, (unsigned char *)cert_pem, (int)cert_len) != 1) {
			LOG_ERROR("Failed to load TLS certificate from: %s", cfg->tls_cert);
			free(cert_pem);
			tls_destroy_context(tls_master);
			goto fail_cleanup;
		}
		free(cert_pem);

		// Load private key
		size_t key_len = 0;
		char *key_pem = read_file_to_buffer(cfg->tls_key, &key_len);
		if (!key_pem) {
			LOG_ERROR("Failed to read TLS private key: %s", cfg->tls_key);
			tls_destroy_context(tls_master);
			goto fail_cleanup;
		}
		if (tls_load_private_key(tls_master, (unsigned char *)key_pem, (int)key_len) != 1) {
			LOG_ERROR("Failed to load TLS private key from: %s", cfg->tls_key);
			free(key_pem);
			tls_destroy_context(tls_master);
			goto fail_cleanup;
		}
		free(key_pem);

		LOG_INFO("TLS enabled (cert: %s)", cfg->tls_cert);
	}
#endif  // SUPPORT_TLS_E

	bool tls_enabled = false;
#ifdef SUPPORT_TLS_E
	tls_enabled = (tls_master != NULL);
#endif  // SUPPORT_TLS_E

	LOG_INFO("Serving on http%s://%s:%d", tls_enabled ? "s" : "", cfg->host, cfg->port);
	LOG_INFO("Thread pool: %d workers", cfg->thread_pool_size);
	if (cfg->keep_alive_timeout > 0)
		LOG_INFO("Keep-alive: %ds timeout", cfg->keep_alive_timeout);
	if (cfg->max_conns_per_ip > 0)
		LOG_INFO("Rate limit: %d conns/IP", cfg->max_conns_per_ip);

	if (cfg->browser)
		open_browser(cfg->browser, cfg->host, cfg->port, tls_enabled);

	while (!atomic_load_explicit(&g_shutdown, memory_order_relaxed)) {
		struct sockaddr_storage client_addr;
		socklen_t               client_len = sizeof client_addr;

		int cfd = accept(lfd, (struct sockaddr *)&client_addr, &client_len);
		if (cfd < 0) {
			if (errno == EINTR) continue;
			if (atomic_load_explicit(&g_shutdown, memory_order_relaxed)) break;
			LOG_PERROR("accept");
			continue;
		}

		if (atomic_load_explicit(&g_shutdown, memory_order_relaxed)) {
			close(cfd);
			break;
		}

		Transport *t = transport_new(cfd);
		if (!t) {
			LOG_WARN("transport_new failed for fd=%d", cfd);
			close(cfd);
			continue;
		}

#ifdef SUPPORT_TLS_E
		// Attach TLS context if configured — transport_accept() will do the handshake
		if (tls_master)
			transport_set_tls(t, tls_master);
#endif  // SUPPORT_TLS_E

		if (transport_accept(t) != 0) {
			LOG_WARN("transport_accept failed for fd=%d", cfd);
			transport_destroy(&t);
			continue;
		}

		ClientJob *job = malloc(sizeof(*job));
		if (!job) {
			transport_destroy(&t);
			continue;
		}
		job->t           = t;
		job->client_port = 0;
		job->cfg         = *cfg;
		job->rl          = rl;
		peer_addr(cfd, job->client_ip, sizeof(job->client_ip), &job->client_port);

		if (rl && ratelimit_accept(rl, job->client_ip) != 0) {
			LOG_WARN("Rate limit exceeded for %s, rejecting", job->client_ip);
			http_send_status(t, 429, "Too Many Requests",
			                 "<h1>429 Too Many Requests</h1>");
			transport_destroy(&t);
			free(job);
			continue;
		}

		LOG_DEBUG("Accepted connection from %s:%d (fd=%d)",
		          job->client_ip, job->client_port, cfd);

		thread_pool_submit(pool, handle_client, job);
	}

	LOG_INFO("Shutting down....");

	close(lfd);
	thread_pool_destroy(pool);

	if (rl) ratelimit_destroy(rl);

#ifdef SUPPORT_TLS_E
	if (tls_master)
		tls_destroy_context(tls_master);
#endif  // SUPPORT_TLS_E

	// Cleanup
	bookmark_cache_cleanup();
	db_meta_cleanup();

	LOG_INFO("Goodbye.");
	return 0;

#ifdef SUPPORT_TLS_E
fail_cleanup:
	close(lfd);
	thread_pool_destroy(pool);

	if (rl) ratelimit_destroy(rl);

	if (tls_master)
		tls_destroy_context(tls_master);

	bookmark_cache_cleanup();
	db_meta_cleanup();

	return -1;
#endif  // SUPPORT_TLS_E
}
