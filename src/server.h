#ifndef _SERVER_H_
#define _SERVER_H_

#include <stdbool.h>

typedef struct {
	const char *host;
	int         port;

	const char *user;
	const char *pass;

	bool    print_request;
	const char *browser;

	const char *bookmark_file;  // Path to bookmark JSON file (first of DB_FILE(s))

	int thread_pool_size;
	int keep_alive_timeout;
	int max_conns_per_ip;
} ServerConfig;

int server_run(const ServerConfig *cfg);

#endif  // _SERVER_H_
