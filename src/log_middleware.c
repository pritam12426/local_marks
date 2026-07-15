/*
 * log_middleware.c — Request/Response logging middleware with request ID tracking
 */

#include "log_middleware.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "http.h"
#include "log.h"

static __thread uint64_t g_request_id = 0;

void log_middleware_set_request_id(uint64_t id)
{
	g_request_id = id;
}

uint64_t log_middleware_get_request_id(void)
{
	if (!g_request_id) {
		g_request_id = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)pthread_self();
	}
	return g_request_id;
}

uint64_t log_middleware_generate_request_id(void)
{
	static __thread uint64_t counter = 0;
	uint64_t id = ((uint64_t)time(NULL) << 32) ^ (uintptr_t)pthread_self() ^ ++counter;
	g_request_id = id;
	return id;
}

void log_middleware_request_start(const HttpRequest *req, const char *client_ip, int client_port)
{
	log_middleware_generate_request_id();

	LOG_INFO("req=%" PRIu64 " %s %s %s from %s:%d",
	         g_request_id,
	         http_method_str(req->method),
	         req->path,
	         req->version,
	         client_ip,
	         client_port);
}

void log_middleware_request_headers(const HttpRequest *req, const char *client_ip, int client_port)
{
	if (!g_request_id) log_middleware_generate_request_id();

	LOG_DEBUG("req=%" PRIu64 " Headers from %s:%d:", g_request_id, client_ip, client_port);
	if (req->host[0]) LOG_DEBUG("req=%" PRIu64 "  Host: %s", g_request_id, req->host);
	if (req->auth[0]) LOG_DEBUG("req=%" PRIu64 "  Authorization: Basic ****", g_request_id);
	if (req->connection[0]) LOG_DEBUG("req=%" PRIu64 "  Connection: %s", g_request_id, req->connection);
	if (req->if_none_match[0]) LOG_DEBUG("req=%" PRIu64 "  If-None-Match: %s", g_request_id, req->if_none_match);
	if (req->range_start != -1) {
		LOG_DEBUG("req=%" PRIu64 "  Range: bytes=%" PRId64 "-%" PRId64,
		          g_request_id, req->range_start, req->range_end);
	}
}

void log_middleware_response(int status, const char *status_text, size_t body_len, const char *mime)
{
	if (!g_request_id) return;

	LOG_INFO("req=%" PRIu64 " -> %d %s (%zu bytes, %s)",
	         g_request_id, status, status_text, body_len, mime ? mime : "unknown");
}

void log_middleware_error(const char *fmt, ...)
{
	if (!g_request_id) log_middleware_generate_request_id();

	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	LOG_ERROR("req=%" PRIu64 " %s", g_request_id, buf);
}

void log_middleware_debug(const char *fmt, ...)
{
	if (!g_request_id) return;

	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	LOG_DEBUG("req=%" PRIu64 " %s", g_request_id, buf);
}

void log_middleware_clear(void)
{
	g_request_id = 0;
}
