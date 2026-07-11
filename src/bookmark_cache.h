#ifndef _BOOKMARK_CACHE_H_
#define _BOOKMARK_CACHE_H_


#include <stddef.h>

// Initialize the bookmark cache (call once at startup)
void bookmark_cache_init(void);

// Cleanup the bookmark cache (call on shutdown)
void bookmark_cache_cleanup(void);

// Get cached bookmark JSON, loading/refreshing based on mtime.
// Returns pointer to cached buffer (valid until next reload), or NULL on error.
const char *get_cached_bookmark_json(const char *path);

// Get length of currently cached bookmark JSON
size_t get_cached_bookmark_json_len(void);


#endif  // _BOOKMARK_CACHE_H_