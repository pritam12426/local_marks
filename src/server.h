#ifndef _SERVER_H_
#define _SERVER_H_

#include <stdbool.h>

#include "common.h"

typedef struct {
	const char *host;
	int         port;

	const char *user;
	const char *pass;

	bool        print_request;
	const char *browser;

	const char *bookmark_files[MAX_BOOKMARK_FILES];
	int         bookmark_file_count;

	int thread_pool_size;
	int keep_alive_timeout;
	int max_conns_per_ip;

#ifdef SUPPORT_TLS_E
	const char *tls_cert;
	const char *tls_key;
#endif  // SUPPORT_TLS_E

} ServerConfig;

int server_run(const ServerConfig *cfg);

#endif  // _SERVER_H_
