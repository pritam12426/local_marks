#include <argp.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "common.h"
#include "project_config.h"
#include "server.h"
#include "vfs_hash.h"
#include "header_cache.h"

/* ── argp global strings ──────────────────────────────────────────────────── */
// These are used by --version and --help automatically
// project_config.h defines VERSION, HOMEPAGE_URL, and AUTH_MESSAGE
const char *argp_program_version     = MAIN_BINARY " " PROJECT_VERSION;
const char *argp_program_bug_address = PROJECT_HOMEPAGE_URL "/issues" "\n" AUTH_MESSAGE;
static char doc[]                    = MAIN_BINARY " - " PROJECT_DESC;

/* ── CLI option table ─────────────────────────────────────────────────────── */
// Each option group has a section number for grouping in --help output
static struct argp_option options[] = {
	{ 0, 0, 0, 0, "Logging:", 1 },
	{ "log-level",      'L', "LEVEL",  0,  "Set log level: [error|warn|info|debug] (default: info)",  1 },
	{ "log-file",       'F', "FILE",   0,  "Set logging file",                                        1 },
	{ "print-request",  'R', 0,        0,  "Log each client request and its headers",                 1 },

	{ 0, 0, 0, 0, "Authentication:", 2 },
	{ "user",         'u',  "USER",     0,  "Enable Basic-Auth with this username",  2 },
	{ "pass",         'p',  "PASS",     0,  "Enable Basic-Auth with this password",  2 },

	{ 0, 0, 0, 0, "Connection:", 3 },
	{ "port",       'P', "PORT",  0,  "TCP port to listen on (default: 8080)",                      3 },
	{ "host",       'H', "HOST",  0,  "Listener host / IP (default: localhost)",                    3 },
	{ "threads",    'T', "NUM",   0,  "Thread pool size (default: 2)",                              3 },
	{ "keep-alive", 'K', "SECS",  0,  "Keep-alive timeout in seconds (default: 3, 0 = disable)",   3 },
	{ "max-conns",  'M', "NUM",   0,  "Max concurrent connections per IP (default: 0 = unlimited)", 3 },
	{ "browser", 'B', "BROWSER", 0,  "Open page in BROWSER on startup (e.g. firefox)",              3 },

	{ 0 }
};

/* ── Arguments struct (mirrors ServerConfig) ──────────────────────────────── */
// Stored as globals so parse_opt() can fill them; later copied into ServerConfig
typedef struct {
	const char  *user;             // -u: Basic-Auth username (NULL = disabled)
	const char  *pass;             // -p: Basic-Auth password (NULL = disabled)
	const char  *browser;          // -B: browser to open on start
	const char  *host;             // -H: bind address (default: localhost)

	int          port;             // -P: listen port (default: 8080)
	int          threads;          // -T: thread pool size (default: 2)
	int          keep_alive;       // -K: keep-alive timeout (default: 3)
	int          max_conns;        // -M: max conns per IP (default: 0 = unlimited)

	const char    *log_file;      // -F: Where it has to append logs instead of STDERR
	bool           print_request; // -R: log every request
	Log_level_t    log_level;     // -L: verbosity threshold

	const char   *bookmark_files[MAX_BOOKMARK_FILES];  // positional: JSON DB file(s)
	int           bookmark_file_count;                 // number of bookmark files provided
} Arguments;

static Arguments G_Args = {
	.user            = NULL,    // NULL = auth disabled by default
	.pass            = NULL,
	.browser         = NULL,
	.host            = "localhost",
	.port            = 8080,
	.threads         = 2,
	.keep_alive      = 3,
	.max_conns       = 0,

	.log_file        = NULL, // NULL => stderr
	.print_request   = false,
	.browser         = NULL,
	.log_level       = LOG_LEVEL_INFO,

	.bookmark_file_count = 0,
};

/* ── Option parser ────────────────────────────────────────────────────────── */
// Called by argp for each CLI flag; key is the short-option character
static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	switch (key) {
	case 'P': {
		// Parse port number, validate range 1–65535
		char *end;
		long  port = strtol(arg, &end, 10);
		if (*arg == '\0' || *end != '\0')
			argp_error(state, "Invalid port: '%s'.", arg);
		if (port < 1 || port > 65535)
			argp_error(state, "Port out of range: %ld.", port);
		G_Args.port = (int)port;
		break;
	}
	case 'L':
		// Map string-level to internal enum
		if      (strcmp(arg, "error") == 0) log_set_level(LOG_LEVEL_ERROR);
		else if (strcmp(arg, "warn")  == 0) log_set_level(LOG_LEVEL_WARN);
		else if (strcmp(arg, "info")  == 0) log_set_level(LOG_LEVEL_INFO);
		else if (strcmp(arg, "debug") == 0) log_set_level(LOG_LEVEL_DEBUG);
		else     argp_error(state, "Invalid log level: '%s'. Use: error, warn, info, debug.", arg);
		G_Args.log_level = log_get_level();
		break;
	case 'H': G_Args.host      = arg;  break;
	case 'F': G_Args.log_file  = arg;  break;
	case 'p': G_Args.pass      = arg;  break;
	case 'u': G_Args.user      = arg;  break;
	case 'B': G_Args.browser   = arg; break;
	case 'T': {
		char *end;
		long  val = strtol(arg, &end, 10);
		if (*arg == '\0' || *end != '\0')
			argp_error(state, "Invalid thread count: '%s'.", arg);
		if (val < 1 || val > 256)
			argp_error(state, "Thread count out of range: %ld (1-256).", val);
		G_Args.threads = (int)val;
		break;
	}
	case 'K': {
		char *end;
		long  val = strtol(arg, &end, 10);
		if (*arg == '\0' || *end != '\0')
			argp_error(state, "Invalid keep-alive timeout: '%s'.", arg);
		if (val < 0 || val > 3600)
			argp_error(state, "Keep-alive timeout out of range: %ld (0-3600).", val);
		G_Args.keep_alive = (int)val;
		break;
	}
	case 'M': {
		char *end;
		long  val = strtol(arg, &end, 10);
		if (*arg == '\0' || *end != '\0')
			argp_error(state, "Invalid max connections: '%s'.", arg);
		if (val < 0 || val > 1000)
			argp_error(state, "Max connections out of range: %ld (0-1000).", val);
		G_Args.max_conns = (int)val;
		break;
	}
	case 'R':
		G_Args.print_request = true;
		break;

	case ARGP_KEY_ARG:
		// Collect positional bookmark file paths
		if (G_Args.bookmark_file_count >= MAX_BOOKMARK_FILES)
			argp_error(state, "Too many bookmark files (max %d).", MAX_BOOKMARK_FILES);
		G_Args.bookmark_files[G_Args.bookmark_file_count++] = arg;
		break;

	case ARGP_KEY_END:
		// Validate argument combinations after all flags are parsed
		if (G_Args.bookmark_file_count == 0)
			argp_error(state, "At least one bookmark JSON file is required.");

		if (G_Args.user != NULL && G_Args.pass == NULL)
			argp_error(state, "A password must be provided when a username is specified.");

		// Verify all bookmark files exist and are readable
		for (int i = 0; i < G_Args.bookmark_file_count; i++) {
			if (access(G_Args.bookmark_files[i], R_OK) != 0)
				argp_error(state, "Cannot read bookmark file: '%s'.", G_Args.bookmark_files[i]);
		}
		break;

	default: return ARGP_ERR_UNKNOWN;
	}
	return 0;
}

static struct argp argp = {
	.options = options,
	.parser  = parse_opt,
	.doc     = doc,
	.args_doc = "<DB_FILE(s)>...",
};

/* ── main ─────────────────────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
	// Parse CLI args; argp calls parse_opt() for each flag
	argp_parse(&argp, argc, argv, 0, 0, 0);
	log_init(G_Args.log_file);  // Initialise logger (outputs to stderr)


	// Dump parsed CLI args when in debug mode — useful for troubleshooting
	if (log_get_level() == LOG_LEVEL_DEBUG) {
		LOG_CUSTOM(LOG_LEVEL_DEBUG,false, "Command-line args: [");
		for (int i = 0; i < argc; i++) {
			fprintf(log_get_file(), "\"%s\"", argv[i]);
			if (i != argc - 1) fputs(", ", log_get_file());
		}
		fputs("]\n", log_get_file());
	}

	vfs_hash_init();
	header_cache_init();

	// Log bookmark files that will be loaded
	LOG_INFO("Loading %d bookmark file(s):", G_Args.bookmark_file_count);
	for (int i = 0; i < G_Args.bookmark_file_count; i++)
		LOG_INFO("  [%d/%d] %s", i + 1, G_Args.bookmark_file_count, G_Args.bookmark_files[i]);

ServerConfig cfg = {
		.host              = G_Args.host,
		.port              = G_Args.port,
		.user              = G_Args.user,
		.pass              = G_Args.pass,
		.print_request     = G_Args.print_request,
		.browser           = G_Args.browser,
		.bookmark_file_count = G_Args.bookmark_file_count,
		.thread_pool_size  = G_Args.threads,
		.keep_alive_timeout = G_Args.keep_alive,
		.max_conns_per_ip  = G_Args.max_conns,
	};

	for (int i = 0; i < G_Args.bookmark_file_count; i++) {
		cfg.bookmark_files[i] = G_Args.bookmark_files[i];
	}

	// Start HTTP server (blocks until SIGINT/SIGTERM)
	server_run(&cfg);

	return 0;
}
