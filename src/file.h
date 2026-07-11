/*
 * file.h — VFS-based file serving interface
 *
 * Serves files from the embedded virtual filesystem (vfs_lookup),
 * not from the real filesystem.
 */

#ifndef _FILE_H_
#define _FILE_H_


#include "http.h"

typedef struct Transport Transport;

// Serve an embedded file based on the HTTP request.
// Handles conditional requests (ETag), range requests, and keep-alive.
// Returns the HTTP status code sent.
int file_serve(const HttpRequest *req,
               Transport         *t,
               const char        *client_ip,
               int                client_port,
               int                print_request,
               int                keep_alive);


#endif  // _FILE_H_
