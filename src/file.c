#include "file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "mime.h"
#include "project_config.h"
#include "response.h"
#include "transport.h"
#include "vfs_hash.h"

static void build_etag(uint32_t content_hash, size_t file_len, char *buf, size_t len)
{
	snprintf(buf, len, "\"%08x-%zx\"", content_hash, file_len);
}

static uint32_t fnv1a_hash(const unsigned char *data, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
}

static void log_request(const char        *client_ip,
                        int                client_port,
                        const HttpRequest *req,
                        int                status,
                        long long          bytes,
                        const char        *mime)
{
	const char *vfs_path = req->path;
	if (*vfs_path == '/') vfs_path++;
	if (*vfs_path == '\0') vfs_path = "index.html";

	if (bytes >= 0 && mime) {
		LOG_INFO("%s:%d \"%s VFS:%s %s\" %d - (%lld bytes, %s)",
		         client_ip,
		         client_port,
		         http_method_str(req->method),
		         vfs_path,
		         req->version,
		         status,
		         bytes,
		         mime);
	} else {
		LOG_INFO("%s:%d \"%s VFS:%s %s\" %d",
		         client_ip,
		         client_port,
		         http_method_str(req->method),
		         vfs_path,
		         req->version,
		         status);
	}
}

int file_serve(const HttpRequest *req,
               Transport         *t,
               const char        *client_ip,
               int                client_port,
               int                print_request,
               int                keep_alive)
{
	if (req->method != HTTP_GET && req->method != HTTP_HEAD) {
		response_error(t, 405, "Only GET and HEAD are supported.");
		if (print_request)
			log_request(client_ip, client_port, req, 405, -1, NULL);
		return 405;
	}

	/* Strip leading '/' for VFS lookup: "/" → "index.html", "/foo" → "foo" */
	const char *path = req->path;
	if (*path == '/')
		path++;

	/* Default to index.html for root */
	if (*path == '\0')
		path = "index.html";

	const char *vfs_path = path;  // Path used for VFS lookup

	const vfs_entry *entry = vfs_lookup(path);
	if (!entry) {
		LOG_DEBUG("VFS miss: \"%s\"", path);
		response_error(t, 404, "File not found.");
		if (print_request)
			log_request(client_ip, client_port, req, 404, -1, NULL);
		return 404;
	}

	const unsigned char *data = entry->file_start;
	size_t               len  = entry->file_len;

	const char *mime = mime_from_path(entry->file_path);
	int         is_gzipped = (len >= 2 && data[0] == 0x1f && data[1] == 0x8b);

	/* Generate ETag from content hash + size */
	uint32_t content_hash = fnv1a_hash(data, len);
	char     etag[64];
	build_etag(content_hash, len, etag, sizeof etag);

	/* Conditional GET: If-None-Match */
	if (req->if_none_match[0] && strcmp(req->if_none_match, etag) == 0) {
		LOG_INFO("%s:%d \"VFS:%s\" 304 (ETag match)", client_ip, client_port, req->path);
		char extra[256];
		snprintf(extra, sizeof extra, "ETag: %s\r\n", etag);
		response_send(t, 304, "Not Modified", NULL, extra, NULL, 0, keep_alive, 1);
		if (print_request)
			log_request(client_ip, client_port, req, 304, -1, NULL);
		return 304;
	}

	LOG_INFO("%s:%d \"%s VFS:%s\" (%s, %zu bytes%s)",
	         client_ip, client_port, http_method_str(req->method), vfs_path, mime, len,
	         is_gzipped ? ", gzip" : "");

	/* Range request handling */
	long range_first = 0, range_last = (long) len - 1;
	int  is_range = 0;

	if (req->range_start != -1) {
		if (req->range_start < 0) {
			/* Suffix range: bytes=-500 → last 500 bytes */
			range_first = (long) len + (long) req->range_start;
			if (range_first < 0)
				range_first = 0;
			range_last = (long) len - 1;
		} else {
			range_first = (long) req->range_start;
			range_last  = (req->range_end >= 0) ? (long) req->range_end : (long) len - 1;
		}

		if (range_first < 0)
			range_first = 0;
		if (range_last >= (long) len)
			range_last = (long) len - 1;

		if (range_first > range_last || range_first >= (long) len) {
			LOG_WARN("Range not satisfiable: %ld-%ld (file size %zu)",
			         range_first, range_last, len);
			char extra[128];
			snprintf(extra, sizeof extra, "Content-Range: bytes */%zu\r\n", len);
			response_send(t, 416, "Range Not Satisfiable", NULL, extra, NULL, 0, keep_alive, 1);
			if (print_request)
				log_request(client_ip, client_port, req, 416, -1, NULL);
			return 416;
		}
		LOG_DEBUG("Range request: bytes=%ld-%ld of %zu", range_first, range_last, len);
		is_range = 1;
	}

	const char *body    = (const char *)(data + range_first);
	size_t      body_len = (size_t)(range_last - range_first + 1);

	char extra[512];
	if (is_range) {
		snprintf(extra,
		         sizeof extra,
		         "ETag: %s\r\n"
		         "Accept-Ranges: bytes\r\n"
		         "Content-Range: bytes %ld-%ld/%zu\r\n",
		         etag,
		         range_first,
		         range_last,
		         len);
	} else {
		snprintf(extra,
		         sizeof extra,
		         "ETag: %s\r\n"
		         "Accept-Ranges: bytes\r\n",
		         etag);
	}

	/* Add Content-Encoding for gzip-compressed embedded files */
	if (is_gzipped) {
		size_t cur = strlen(extra);
		snprintf(extra + cur, sizeof extra - cur,
		         "Content-Encoding: gzip\r\n");
	}

	int status = is_range ? 206 : 200;
	response_send(t,
	              status,
	              is_range ? "Partial Content" : "OK",
	              mime,
	              extra,
	              body,
	              body_len,
	              keep_alive,
	              req->method != HTTP_HEAD);
	if (print_request)
		log_request(client_ip, client_port, req, status, (long long) body_len, mime);
	return status;
}
