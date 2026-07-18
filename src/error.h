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

// Check error categories
int error_is_client_error(int status);
int error_is_server_error(int status);

// Look up status code text from error table (O(1))
const char *error_find_code(int status);
const char *error_find_status_text(int status);


#endif  // _ERROR_H_
