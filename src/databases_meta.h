#ifndef _DATABASES_META_H_
#define _DATABASES_META_H_


#include <limits.h>
#include <stddef.h>
#include <sys/stat.h>

#include "common.h"
#include "server.h"

// Database metadata structure
typedef struct {
	mode_t          mode;
	char            absolute_path[PATH_MAX];
	char            file_name[256];
	size_t          file_size;  // bytes on disk

	// Optional: filesystem extras
	time_t          cTime;       // inode change time (metadata changes)
	time_t          bTime;       // birth/creation time (where supported)
	char            user[256];   // file owner name
	char            group[256];  // file group name
	struct timespec mTime;       // modification time
} JSON_DB_meta_data;

// Global array for database metadata (stack-allocated, max 10)
extern JSON_DB_meta_data g_db_meta[MAX_BOOKMARK_FILES];
extern int               g_db_meta_count;

// Populate metadata for all configured bookmark files
void populate_db_meta_all(const ServerConfig *cfg);

// Build JSON array of all databases metadata
// Caller must free the returned string
char *build_databases_json(void);

// Build JSON for a single database by index
// Caller must free the returned string
char *build_database_json(int index);

void db_meta_cleanup(void);


#endif  // _DATABASES_META_H_
