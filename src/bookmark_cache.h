#ifndef _BOOKMARK_CACHE_H_
#define _BOOKMARK_CACHE_H_


#include <stddef.h>

// Initialize the bookmark cache (call once at startup)
void bookmark_cache_init(void);

// Cleanup the bookmark cache (call on shutdown)
void bookmark_cache_cleanup(void);

// Add a database path to the cache (call for each DB during startup)
void bookmark_cache_add_db(const char *path);

// Get cached bookmark JSON for a specific path, loading/refreshing based on mtime.
// Returns pointer to cached buffer (valid until next reload), or NULL on error.
const char *get_cached_bookmark_json(const char *path);

// Get length of currently cached bookmark JSON for the last accessed path
size_t get_cached_bookmark_json_len(void);

// Get cached bookmark JSON for a specific database by index.
// Returns pointer to cached buffer, or NULL on error.
const char *get_cached_bookmark_json_by_index(int index);

// Get length of cached bookmark JSON for a specific database index
size_t get_cached_bookmark_json_len_by_index(int index);


#endif  // _BOOKMARK_CACHE_H_