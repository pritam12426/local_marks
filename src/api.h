#ifndef _API_H_
#define _API_H_


#include "http.h"
#include "server.h"
#include "transport.h"

// Handle API requests (/bookmarks.json, /api/databases, /api/databases/<index>)
// Returns 1 if request was handled (API endpoint), 0 if not an API endpoint.
int api_handle_request(const HttpRequest *req, Transport *t,
                       const char *client_ip, int client_port,
                       const ServerConfig *cfg, int keep_alive, int print_request);


#endif  // _API_H_
