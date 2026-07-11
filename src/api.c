#include "api.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "bookmark_cache.h"
#include "databases_meta.h"
#include "log.h"
#include "response.h"

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
	// GET /bookmarks.json - serve first bookmark database
	const char *first_db = (cfg->bookmark_file_count > 0) ? cfg->bookmark_files[0] : NULL;
	if (req->method == HTTP_GET && first_db &&
	    strcmp(req->path, "/bookmarks.json") == 0) {
		const char *json = get_cached_bookmark_json(first_db);
		if (!json) {
			LOG_ERROR("Failed to load bookmark file '%s'", first_db);
			response_error(t, 500, "Internal Server Error",
			               "Cannot load bookmark database");
		} else {
			char extra[128];
			snprintf(extra, sizeof extra, "Cache-Control: no-cache\r\n");
			response_send(t, 200, "OK",
			              "application/json; charset=utf-8",
			              extra, json, get_cached_bookmark_json_len(),
			              keep_alive, 1);
			if (print_request)
				LOG_INFO("%s:%d \"GET /bookmarks.json %s\" 200 - (%zu bytes, application/json)",
				         client_ip, client_port, req->version, get_cached_bookmark_json_len());
		}
		return 1;
	}

	// GET /api/databases - list all databases
	if (req->method == HTTP_GET && strcmp(req->path, "/api/databases") == 0) {
		LOG_DEBUG("Serving database list: %d databases", g_db_meta_count);
		char *json = build_databases_json();
		if (!json) {
			LOG_ERROR("OOM building databases JSON");
			response_error(t, 500, "Internal Server Error", "Out of memory");
		} else {
			char extra[128];
			snprintf(extra, sizeof extra, "Cache-Control: no-cache\r\n");
			response_send(t, 200, "OK",
			              "application/json; charset=utf-8",
			              extra, json, strlen(json),
			              keep_alive, 1);
			if (print_request)
				LOG_INFO("%s:%d \"GET /api/databases %s\" 200 - (%zu bytes, application/json)",
				         client_ip, client_port, req->version, strlen(json));
			free(json);
		}
		return 1;
	}

	// GET /api/databases/<index> - single database metadata
	if (req->method == HTTP_GET && strncmp(req->path, "/api/databases/", 15) == 0) {
		const char *idx_str = req->path + 15;  // skip "/api/databases/"
		char *endptr;
		long index = strtol(idx_str, &endptr, 10);
		if (*endptr != '\0' || index < 0 || index >= g_db_meta_count) {
			LOG_WARN("Invalid database index requested: %s", idx_str);
			response_error(t, 404, "Not Found", "Database index out of range");
			return 1;
		}

		LOG_DEBUG("Serving database %ld metadata: %s", index, g_db_meta[index].file_name);
		char *json = build_database_json((int)index);
		if (!json) {
			LOG_ERROR("OOM building database JSON");
			response_error(t, 500, "Internal Server Error", "Out of memory");
		} else {
			char extra[128];
			snprintf(extra, sizeof extra, "Cache-Control: no-cache\r\n");
			response_send(t, 200, "OK",
			              "application/json; charset=utf-8",
			              extra, json, strlen(json),
			              keep_alive, 1);
			if (print_request)
				LOG_INFO("%s:%d \"GET /api/databases/%ld %s\" 200 - (%zu bytes, application/json)",
				         client_ip, client_port, index, req->version, strlen(json));
			free(json);
		}
		return 1;
	}

	return 0;  // Not an API endpoint
}