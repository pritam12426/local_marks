/*
 * error.h — Centralized error handling API
 */

#ifndef _ERROR__H_
#define _ERROR__H_


// Look up status code text from error table (O(1))
const char *error_find_code(int status);
const char *error_find_status_text(int status);


#endif  // _ERROR__H_
