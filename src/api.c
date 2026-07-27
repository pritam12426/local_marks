#include "api.h"

#include <errno.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "bookmark_cache.h"
#include "databases_meta.h"
#include "log.h"
#include "response.h"

// Serve a JSON response with standard headers
static void send_json_response(Transport *t, const char *json, size_t json_len,
                               int keep_alive, int print_request,
                               const char *client_ip, int client_port,
                               const char *endpoint, const HttpRequest *req)
{
	char extra[128];
	snprintf(extra, sizeof extra, "Cache-Control: no-cache\r\n");
	response_send(t, 200, "OK",
	              "application/json; charset=utf-8",
	              extra, json, json_len,
	              keep_alive, 1);
	if (print_request)
		LOG_INFO("%s:%d \"GET %s %s\" 200 - (%zu bytes, application/json)",
		         client_ip, client_port, endpoint, req->version, json_len);
}

// Extract and URL-decode a single query parameter value by name.
// Returns length written to `out`, or -1 if param not found / buffer too small.
static int query_param(char *out, size_t out_size, const char *query, const char *name)
{
	const char *p = strstr(query, name);
	if (!p) return -1;
	p += strlen(name);
	if (*p != '=') return -1;
	p++;
	const char *end = strchr(p, '&');
	size_t raw_len = end ? (size_t)(end - p) : strlen(p);
	if (raw_len >= out_size) return -1;
	char raw[4096];
	memcpy(raw, p, raw_len);
	raw[raw_len] = '\0';
	http_url_decode(out, out_size, raw);
	return (int)raw_len;
}

// Handle API endpoints in handle_client()
// Returns 1 if request was handled by API, 0 otherwise (fall through to file_serve)
int api_handle_request(const HttpRequest *req,
                       Transport *t,
                       const char *client_ip,
                       int client_port,
                       const ServerConfig *cfg,
                       int keep_alive,
                       int print_request)
{
	// GET /redirect?url=...&db=0&title=... — log click and redirect to external URL
	if (req->method == HTTP_GET && strcmp(req->path, "/redirect") == 0) {
		char decoded_url[4096];
		if (query_param(decoded_url, sizeof decoded_url, req->query, "url") < 0) {
			response_error(t, 400, "Missing or invalid url parameter");
			return 1;
		}

		if (strncmp(decoded_url, "http://", 7) != 0 &&
		    strncmp(decoded_url, "https://", 8) != 0) {
			response_error(t, 400, "Only http/https URLs allowed");
			return 1;
		}

		int db_idx = -1;
		char db_buf[32];
		if (query_param(db_buf, sizeof db_buf, req->query, "db") >= 0)
			db_idx = atoi(db_buf);

		char decoded_title[512] = "";
		query_param(decoded_title, sizeof decoded_title, req->query, "title");

		LOG_INFO("%s:%d CLICK db=%d \"%s\" -> %s ",
		         client_ip, client_port,
		         db_idx, decoded_title, decoded_url);

		char extra[4200];
		snprintf(extra, sizeof extra, "Location: %s\r\n", decoded_url);
		response_send(t, 302, "Found", NULL, extra, NULL, 0, keep_alive, 0);
		return 1;
	}

	// GET /bookmarks/<index>.json - serve specific bookmark database by index
	if (req->method == HTTP_GET && strncmp(req->path, "/bookmarks/", 11) == 0) {
		const char *rest = req->path + 11;  // skip "/bookmarks/"
		const char *dot_json = strstr(rest, ".json");
		if (dot_json && dot_json > rest && strcmp(dot_json, ".json") == 0) {
			size_t idx_len = (size_t)(dot_json - rest);
			char idx_str[32];
			if (idx_len >= sizeof(idx_str)) {
				LOG_WARN("Bookmark index too long: %s", rest);
				response_error(t, 404, "Bookmark index out of range");
				return 1;
			}
			memcpy(idx_str, rest, idx_len);
			idx_str[idx_len] = '\0';

			char *endptr;
			long index = strtol(idx_str, &endptr, 10);

			if (*endptr != '\0' || index < 0 || index >= g_db_meta_count) {
				LOG_WARN("Invalid bookmark index requested: %s", idx_str);
				response_error(t, 404, "Bookmark index out of range");
				return 1;
			}

			size_t json_len = 0;
			const char *json = get_cached_bookmark_json_copy((int)index, &json_len);
			if (!json) {
				LOG_ERROR("Failed to load bookmark database index %ld", index);
				response_error(t, 500, "Cannot load bookmark database");
			} else {
				send_json_response(t, json, json_len, keep_alive, print_request,
				                   client_ip, client_port, req->path, req);
				free((void *)json);
			}
			return 1;
		}
	}

	// GET /bookmarks.json - serve first bookmark database
	const char *first_db = (cfg->bookmark_file_count > 0) ? cfg->bookmark_files[0] : NULL;
	if (req->method == HTTP_GET && first_db &&
	    strcmp(req->path, "/bookmarks.json") == 0) {
		size_t json_len = 0;
		char *json = get_cached_bookmark_json_copy(0, &json_len);
		if (!json) {
			LOG_ERROR("Failed to load bookmark file '%s'", first_db);
			response_error(t, 500, "Cannot load bookmark database");
		} else {
			send_json_response(t, json, json_len, keep_alive, print_request,
			                   client_ip, client_port, req->path, req);
			free(json);
		}
		return 1;
	}

	// GET /api/databases - list all databases
	if (req->method == HTTP_GET && strcmp(req->path, "/api/databases") == 0) {
		LOG_DEBUG("Serving database list: %d databases", g_db_meta_count);
		char *json = build_databases_json();
		if (!json) {
			LOG_ERROR("OOM building databases JSON");
			response_error(t, 500, "Out of memory");
		} else {
			size_t json_len = strlen(json);
			send_json_response(t, json, json_len, keep_alive, print_request,
			                   client_ip, client_port, req->path, req);
			free(json);
		}
		return 1;
	}

	// GET /api/databases/<index> - single database metadata
	if (req->method == HTTP_GET && strncmp(req->path, "/api/databases/", 15) == 0) {
		const char *rest = req->path + 15;  // skip "/api/databases/"

		// Reject trailing slash (e.g. /api/databases/)
		if (*rest == '\0') {
			response_error(t, 404, "Database index out of range");
			return 1;
		}

		// Parse index
		char *endptr;
		long index = strtol(rest, &endptr, 10);
		if (*endptr != '\0' || index < 0 || index >= g_db_meta_count) {
			LOG_WARN("Invalid database index requested: %s", rest);
			response_error(t, 404, "Database index out of range");
			return 1;
		}

		LOG_DEBUG("Serving database %ld metadata: %s", index, g_db_meta[index].file_name);
		char *json = build_database_json((int)index);
		if (!json) {
			LOG_ERROR("OOM building database JSON");
			response_error(t, 500, "Out of memory");
		} else {
			size_t json_len = strlen(json);
			send_json_response(t, json, json_len, keep_alive, print_request,
			                   client_ip, client_port, req->path, req);
			free(json);
		}
		return 1;
	}

	return 0;  // Not an API endpoint
}
