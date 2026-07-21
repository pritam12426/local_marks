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
// #include "header_cache.h"
#include "response.h"

static const struct {
	const char *code;
	const char *status_text;
} error_table[] = {
	/* 1xx – Informational */
	[100] = { "CONTINUE",                         "Continue"                         },
	[101] = { "SWITCHING_PROTOCOLS",              "Switching Protocols"              },
	[102] = { "PROCESSING",                       "Processing"                       },
	[103] = { "EARLY_HINTS",                      "Early Hints"                      },

	/* 2xx – Success */
	[200] = { "OK",                               "OK"                               },
	[201] = { "CREATED",                          "Created"                          },
	[202] = { "ACCEPTED",                         "Accepted"                         },
	[203] = { "NON_AUTHORITATIVE_INFORMATION",    "Non-Authoritative Information"    },
	[204] = { "NO_CONTENT",                       "No Content"                       },
	[205] = { "RESET_CONTENT",                    "Reset Content"                    },
	[206] = { "PARTIAL_CONTENT",                  "Partial Content"                  },
	[207] = { "MULTI_STATUS",                     "Multi-Status"                     },
	[208] = { "ALREADY_REPORTED",                 "Already Reported"                 },
	[226] = { "IM_USED",                          "IM Used"                          },

	/* 3xx – Redirection */
	[301] = { "MOVED_PERMANENTLY",                "Moved Permanently"                },
	[302] = { "FOUND",                            "Found"                            },
	[303] = { "SEE_OTHER",                        "See Other"                        },
	[300] = { "MULTIPLE_CHOICES",                 "Multiple Choices"                 },
	[304] = { "NOT_MODIFIED",                     "Not Modified"                     },
	[305] = { "USE_PROXY",                        "Use Proxy"                        },
	[307] = { "TEMPORARY_REDIRECT",               "Temporary Redirect"               },
	[308] = { "PERMANENT_REDIRECT",               "Permanent Redirect"               },

	/* 4xx – Client Errors */
	[400] = { "BAD_REQUEST",                      "Bad Request"                      },
	[401] = { "UNAUTHORIZED",                     "Unauthorized"                     },
	[402] = { "PAYMENT_REQUIRED",                 "Payment Required"                 },
	[403] = { "FORBIDDEN",                        "Forbidden"                        },
	[404] = { "NOT_FOUND",                        "Not Found"                        },
	[405] = { "METHOD_NOT_ALLOWED",               "Method Not Allowed"               },
	[406] = { "NOT_ACCEPTABLE",                   "Not Acceptable"                   },
	[407] = { "PROXY_AUTHENTICATION_REQUIRED",    "Proxy Authentication Required"    },
	[408] = { "REQUEST_TIMEOUT",                  "Request Timeout"                  },
	[409] = { "CONFLICT",                         "Conflict"                         },
	[410] = { "GONE",                             "Gone"                             },
	[411] = { "LENGTH_REQUIRED",                  "Length Required"                  },
	[412] = { "PRECONDITION_FAILED",              "Precondition Failed"              },
	[413] = { "PAYLOAD_TOO_LARGE",                "Payload Too Large"                },
	[414] = { "URI_TOO_LONG",                     "URI Too Long"                     },
	[415] = { "UNSUPPORTED_MEDIA_TYPE",           "Unsupported Media Type"           },
	[416] = { "RANGE_NOT_SATISFIABLE",            "Range Not Satisfiable"            },
	[417] = { "EXPECTATION_FAILED",               "Expectation Failed"               },
	[418] = { "IM_A_TEAPOT",                      "I'm a Teapot"                     },
	[421] = { "MISDIRECTED_REQUEST",              "Misdirected Request"              },
	[422] = { "UNPROCESSABLE_CONTENT",            "Unprocessable Content"            },
	[423] = { "LOCKED",                           "Locked"                           },
	[424] = { "FAILED_DEPENDENCY",                "Failed Dependency"                },
	[425] = { "TOO_EARLY",                        "Too Early"                        },
	[426] = { "UPGRADE_REQUIRED",                 "Upgrade Required"                 },
	[428] = { "PRECONDITION_REQUIRED",            "Precondition Required"            },
	[429] = { "TOO_MANY_REQUESTS",                "Too Many Requests"                },
	[431] = { "REQUEST_HEADER_FIELDS_TOO_LARGE",  "Request Header Fields Too Large"  },
	[451] = { "UNAVAILABLE_FOR_LEGAL_REASONS",    "Unavailable For Legal Reasons"    },

	/* 5xx – Server Errors */
	[500] = { "INTERNAL_SERVER_ERROR",            "Internal Server Error"            },
	[501] = { "NOT_IMPLEMENTED",                  "Not Implemented"                  },
	[502] = { "BAD_GATEWAY",                      "Bad Gateway"                      },
	[503] = { "SERVICE_UNAVAILABLE",              "Service Unavailable"              },
	[504] = { "GATEWAY_TIMEOUT",                  "Gateway Timeout"                  },
	[505] = { "HTTP_VERSION_NOT_SUPPORTED",       "HTTP Version Not Supported"       },
	[506] = { "VARIANT_ALSO_NEGOTIATES",          "Variant Also Negotiates"          },
	[507] = { "INSUFFICIENT_STORAGE",             "Insufficient Storage"             },
	[508] = { "LOOP_DETECTED",                    "Loop Detected"                    },
	[510] = { "NOT_EXTENDED",                     "Not Extended"                     },
	[511] = { "NETWORK_AUTHENTICATION_REQUIRED",  "Network Authentication Required"  }
};

const char *error_find_code(int status)
{
	if (status >= 100 && status <= 511 && error_table[status].code)
		return error_table[status].code;
	return "INTERNAL_SERVER_ERROR";
}

const char *error_find_status_text(int status)
{
	if (status >= 100 && status <= 511 && error_table[status].status_text)
		return error_table[status].status_text;
	return "Internal Server Error";
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
	const char *code = error_find_code(status);
	const char *status_text = error_find_status_text(status);
	char details[512] = "null";

	if (detail) {
		char escaped[512];
		json_escape(escaped, sizeof(escaped), detail);
		snprintf(details, sizeof(details), "\"%s\"", escaped);
	}

	char extra[128];
	snprintf(extra, sizeof(extra), "X-Error-Code: %s\r\n", code);

	char body[1024];
	int blen = snprintf(body, sizeof(body),
		"{\"error\":{\"code\":\"%s\",\"message\":\"%s\",\"status\":%d,\"details\":%s}}",
		code, status_text, status, details);

	if (blen < 0 || (size_t)blen >= sizeof(body)) blen = (int)sizeof(body) - 1;

	response_send(t, status, status_text,
	              "application/json; charset=utf-8",
	              extra, body, (size_t)blen, 0, 1);
}

void error_send_html(Transport *t, int status, const char *detail)
{
	const char *status_text = error_find_status_text(status);
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
		"<div class='code'>%d</div>"
		"<div class='title'>%s</div>"
		"<div class='detail'>%s</div>"
		"<div class='footer'>" MAIN_BINARY "/" PROJECT_VERSION "</div></div></body></html>",
		status, status_text,
		status, status_text,
		detail ? detail : "An unexpected error occurred.");

	if (blen < 0 || (size_t)blen >= sizeof(body)) blen = (int)sizeof(body) - 1;

	response_send(t, status, status_text,
	              "text/html; charset=utf-8", NULL, body, (size_t)blen, 0, 1);
}

int error_is_client_error(int status) { return status >= 400 && status < 500; }
int error_is_server_error(int status) { return status >= 500; }
