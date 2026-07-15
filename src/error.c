/*
 * error.c — Centralized error handling with structured error responses
 */

#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"
#include "project_config.h"
#include "transport.h"
#include "header_cache.h"
#include "response.h"

typedef struct {
	int         status;
	const char *code;
	const char *status_text;
} error_def_t;

static const error_def_t error_table[] = {
	{400, "BAD_REQUEST", "Bad Request"},
	{401, "UNAUTHORIZED", "Unauthorized"},
	{403, "FORBIDDEN", "Forbidden"},
	{404, "NOT_FOUND", "Not Found"},
	{405, "METHOD_NOT_ALLOWED", "Method Not Allowed"},
	{406, "NOT_ACCEPTABLE", "Not Acceptable"},
	{408, "REQUEST_TIMEOUT", "Request Timeout"},
	{413, "PAYLOAD_TOO_LARGE", "Payload Too Large"},
	{416, "RANGE_NOT_SATISFIABLE", "Range Not Satisfiable"},
	{429, "TOO_MANY_REQUESTS", "Too Many Requests"},
	{500, "INTERNAL_SERVER_ERROR", "Internal Server Error"},
	{501, "NOT_IMPLEMENTED", "Not Implemented"},
	{502, "BAD_GATEWAY", "Bad Gateway"},
	{503, "SERVICE_UNAVAILABLE", "Service Unavailable"},
	{504, "GATEWAY_TIMEOUT", "Gateway Timeout"},
	{0, NULL, NULL}
};

static const error_def_t *find_error(int status)
{
	for (const error_def_t *e = error_table; e->code; e++) {
		if (e->status == status) return e;
	}
	static const error_def_t fallback = {500, "INTERNAL_SERVER_ERROR", "Internal Server Error"};
	return &fallback;
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
	size_t i = 0, j = 0;
	while (src[i] && j + 6 < dst_size - 1) {
		switch (src[i]) {
			case '"':  dst[j++] = '\\'; dst[j++] = '"'; break;
			case '\\': dst[j++] = '\\'; dst[j++] = '\\'; break;
			case '\b': dst[j++] = '\\'; dst[j++] = 'b'; break;
			case '\f': dst[j++] = '\\'; dst[j++] = 'f'; break;
			case '\n': dst[j++] = '\\'; dst[j++] = 'n'; break;
			case '\r': dst[j++] = '\\'; dst[j++] = 'r'; break;
			case '\t': dst[j++] = '\\'; dst[j++] = 't'; break;
			default:
				if ((unsigned char)src[i] < 0x20) {
					if (j + 6 < dst_size - 1) {
						dst[j++] = '\\';
						dst[j++] = 'u';
						dst[j++] = '0';
						dst[j++] = '0';
						dst[j++] = "0123456789ABCDEF"[(src[i] >> 4) & 0xF];
						dst[j++] = "0123456789ABCDEF"[src[i] & 0xF];
					}
				} else {
					if (j < dst_size - 1) dst[j++] = src[i];
				}
				break;
		}
		i++;
	}
	dst[j] = '\0';
}

void error_send_json(Transport *t, int status, const char *detail)
{
	const error_def_t *e = find_error(status);
	char details[512] = "null";

	if (detail) {
		char escaped[512];
		json_escape(escaped, sizeof(escaped), detail);
		snprintf(details, sizeof(details), "\"%s\"", escaped);
	}

	char extra[128];
	snprintf(extra, sizeof(extra), "X-Error-Code: %s\r\n", e->code);

	char body[1024];
	int blen = snprintf(body, sizeof(body),
		"{\"error\":{\"code\":\"%s\",\"message\":\"%s\",\"status\":%d,\"details\":%s}}",
		e->code, e->status_text, status, details);

	if (blen < 0 || (size_t)blen >= sizeof(body)) blen = (int)sizeof(body) - 1;

	response_send(t, status, find_error(status)->status_text,
	              "application/json; charset=utf-8",
	              extra, body, (size_t)blen, 0, 1);
}

void error_send_html(Transport *t, int status, const char *detail)
{
	const error_def_t *e = find_error(status);
	char body[4096];

	int blen = snprintf(body, sizeof(body),
		"<!DOCTYPE html>"
		"<html lang='en'><head>"
		"<meta charset='utf-8'>"
		"<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
		"<title>%d %s</title>"
		"<style>"
		":root {--bg: #ffffff; --text: #222222; --muted: #555555; --accent: #0066cc; --border: #cccccc;}"
		"body.dark {--bg: #1a1a1a; --text: #dddddd; --muted: #aaaaaa; --accent: #66aaff; --border: #444444;}"
		"* { box-sizing: border-box; margin: 0; padding: 0; }"
		"body { font-family: Arial, Helvetica, sans-serif; background: var(--bg); color: var(--text); min-height: 100vh; display: flex; justify-content: center; align-items: center; padding: 20px; }"
		".card { width: 100%%; max-width: 600px; background: var(--bg); border: 1px solid var(--border); padding: 30px; }"
		".code { font-size: 2.5rem; font-weight: bold; color: var(--accent); }"
		".title { margin-top: 10px; font-size: 1.3rem; font-weight: bold; }"
		".detail { margin-top: 12px; color: var(--muted); line-height: 1.5; }"
		".footer { margin-top: 24px; padding-top: 14px; border-top: 1px solid var(--border); color: var(--muted); font-size: 0.85rem; }"
		"</style>"
		"<script>"
		"function updateTheme() { const dark = window.matchMedia('(prefers-color-scheme: dark)').matches; document.body.classList.toggle('dark', dark); }"
		"window.addEventListener('DOMContentLoaded', updateTheme);"
		"window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', updateTheme);"
		"</script>"
		"</head><body><div class='card'>"
		"<div class='code'>%d</div><div class='title'>%s</div><div class='detail'>%s</div>"
		"<div class='footer'>" MAIN_BINARY "/" PROJECT_VERSION "</div></div></body></html>",
		status, e->status_text, detail ? detail : "An unexpected error occurred.");

	if (blen < 0 || (size_t)blen >= sizeof(body)) blen = (int)sizeof(body) - 1;

	response_send(t, status, e->status_text,
	              "text/html; charset=utf-8", NULL, body, (size_t)blen, 0, 1);
}

int error_is_client_error(int status) { return status >= 400 && status < 500; }
int error_is_server_error(int status) { return status >= 500; }
