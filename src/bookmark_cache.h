#ifndef _BOOKMARK_CACHE_H_
#define _BOOKMARK_CACHE_H_


#include <stddef.h>

// Initialize the bookmark cache (call once at startup)
void bookmark_cache_init(void);

// Cleanup the bookmark cache (call on shutdown)
void bookmark_cache_cleanup(void);

// Add a database path to the cache (call for each DB during startup)
void bookmark_cache_add_db(const char *path);

// Get a malloc'd copy of cached bookmark JSON and its length.
// Caller must free() the returned pointer.
// Returns NULL on error.
char *get_cached_bookmark_json_copy(int index, size_t *out_len);


#endif  // _BOOKMARK_CACHE_H_