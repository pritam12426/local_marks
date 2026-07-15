/*
 * log_middleware.h — Request/Response logging middleware with request ID tracking
 */

#ifndef _LOG_MIDDLEWARE_H_
#define _LOG_MIDDLEWARE_H_


#include <stddef.h>
#include <stdint.h>

#include "http.h"

void log_middleware_set_request_id(uint64_t id);
uint64_t log_middleware_get_request_id(void);
uint64_t log_middleware_generate_request_id(void);

void log_middleware_request_start(const HttpRequest *req, const char *client_ip, int client_port);
void log_middleware_request_headers(const HttpRequest *req, const char *client_ip, int client_port);
void log_middleware_response(int status, const char *status_text, size_t body_len, const char *mime);
void log_middleware_error(const char *fmt, ...);
void log_middleware_debug(const char *fmt, ...);
void log_middleware_clear(void);


#endif  // _LOG_MIDDLEWARE_H_
