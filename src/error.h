/*
 * error.h — Centralized error handling API
 */

#ifndef _ERROR_H_
#define _ERROR_H_


// Check error categories
int error_is_client_error(int status);
int error_is_server_error(int status);

// Look up status code text from error table (O(1))
const char *error_find_code(int status);
const char *error_find_status_text(int status);


#endif  // _ERROR_H_
