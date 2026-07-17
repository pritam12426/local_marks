/*
 * error.h — Centralized error handling API
 */

#ifndef _ERROR_H_
#define _ERROR_H_


#include <stddef.h>

typedef struct Transport Transport;

// Send JSON error response (for API endpoints)
void error_send_json(Transport *t, int status, const char *detail);

// Send HTML error page (for browser clients)
void error_send_html(Transport *t, int status, const char *detail);

// Build error JSON string (caller must free)
char *error_build_json(int status, const char *detail, const char *request_id);

// Free JSON string from error_build_json
void error_free_json(char *json);

// Check error categories
int error_is_client_error(int status);
int error_is_server_error(int status);


#endif  // _ERROR_H_
