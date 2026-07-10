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
	{ "log-level",  'L', "LEVEL",  0,  "Set log level: [error|warn|info|debug] (default: info)",  1 },
	{ "log-file",   'F', "FILE",   0,  "Set logging file",                                        1 },


	{ 0, 0, 0, 0, "Authentication:", 2 },
	{ "user",         'u',  "USER",     0,  "Enable Basic-Auth with this username",  2 },
	{ "pass",         'p',  "PASS",     0,  "Enable Basic-Auth with this password",  2 },

	{ 0, 0, 0, 0, "Connection:", 3 },
	{ "max-conns",  'M', "NUM",   0,  "Max concurrent connections per IP (default: 0 = unlimited)", 3 },
	{ "port",     'P',  "PORT",     0,  "TCP port to listen on (default: 8080)",                    3 },
	{ "host",     'H',  "HOST",     0,  "Listener host / IP (default: localhost)",                  3 },
	{ "browser",  'B',  "BROWSER",  0,  "Open page in BROWSER on startup (e.g. firefox)",           3 },

#ifdef TLS_SUPPORT
	{ 0, 0, 0, 0, "HTTPS:", 4 },
	{ "tls-cert", 'T', "PATH",  0,  "TLS certificate file",  4 },
	{ "tls-key",  'K', "PATH",  0,  "TLS private key file",  4 },
#endif  // TLS_SUPPORT

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

#ifdef TLS_SUPPORT
	const char *tls_cert;         // -T: PEM certificate path
	const char *tls_key;          // -K: PEM private key path
#endif  // TLS_SUPPORT

	const char    *log_file;      // -F: Where it has to append logs instead of STDERR
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

#ifdef TLS_SUPPORT
	.tls_cert = NULL,
	.tls_key  = NULL,
#endif  // TLS_SUPPORT

	.log_file        = NULL,
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

#ifdef TLS_SUPPORT
	case 'T': G_Args.tls_cert = arg; break;
	case 'K': G_Args.tls_key  = arg; break;
#endif // TLS_SUPPORT

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

#ifdef TLS_SUPPORT
		// TLS requires both cert AND key (or neither)
		if ((G_Args.tls_cert && !G_Args.tls_key) || (!G_Args.tls_cert && G_Args.tls_key))
			argp_error(state, "Both --tls-cert and --tls-key must be provided for TLS.");
		if (G_Args.tls_cert && access(G_Args.tls_cert, R_OK) != 0)
			argp_error(state, "TLS certificate not readable: '%s'.", G_Args.tls_cert);
		if (G_Args.tls_key && access(G_Args.tls_key, R_OK) != 0)
			argp_error(state, "TLS key not readable: '%s'.", G_Args.tls_key);
#endif // TLS_SUPPORT

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
	if (argp_parse(&argp, argc, argv, 0, 0, 0) != 0)
		return 1;
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

	// Log bookmark files that will be loaded
	LOG_INFO("Loading %d bookmark file(s):", G_Args.bookmark_file_count);
	for (int i = 0; i < G_Args.bookmark_file_count; i++)
		LOG_INFO("  [%d/%d] %s", i + 1, G_Args.bookmark_file_count, G_Args.bookmark_files[i]);

	// TODO: load bookmark JSON, start HTTP server
	LOG_INFO("Startup complete (server logic not yet implemented)");

	return 0;
}
