#include "bookmark_cache.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "log.h"
#include "project_config.h"

// Per-database cache entry
typedef struct {
	char *json;
	size_t json_len;
	time_t mtime;
	char path[256];
} bookmark_cache_entry_t;

static bookmark_cache_entry_t g_db_cache[MAX_BOOKMARK_FILES];
static int g_db_cache_count = 0;
static pthread_mutex_t g_bookmark_cache_mutex = PTHREAD_MUTEX_INITIALIZER;

// Load bookmark JSON file into a cache entry
static int load_bookmark_json(const char *path, bookmark_cache_entry_t *entry)
{
	if (!path) {
		LOG_INFO("No bookmark file configured");
		return 0;
	}

	FILE *f = fopen(path, "rb");
	if (!f) {
		LOG_ERROR("Failed to open bookmark file '%s': %s", path, strerror(errno));
		return -1;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	if (size < 0) {
		LOG_ERROR("Failed to determine size of '%s'", path);
		fclose(f);
		return -1;
	}

	entry->json = malloc((size_t)size + 1);
	if (!entry->json) {
		LOG_ERROR("Failed to allocate %ld bytes for bookmark JSON", size);
		fclose(f);
		return -1;
	}

	size_t read = fread(entry->json, 1, (size_t)size, f);
	fclose(f);

	if (read != (size_t)size) {
		LOG_ERROR("Short read on '%s': expected %ld, got %zu", path, size, read);
		free(entry->json);
		entry->json = NULL;
		return -1;
	}

	entry->json[read] = '\0';
	entry->json_len = read;
	LOG_INFO("Loaded bookmark database: %s (%zu bytes)", path, entry->json_len);
	return 0;
}

void bookmark_cache_init(void)
{
	pthread_mutex_init(&g_bookmark_cache_mutex, NULL);
	for (int i = 0; i < MAX_BOOKMARK_FILES; i++) {
		g_db_cache[i].json = NULL;
		g_db_cache[i].json_len = 0;
		g_db_cache[i].mtime = 0;
		g_db_cache[i].path[0] = '\0';
	}
	g_db_cache_count = 0;
}

void bookmark_cache_cleanup(void)
{
	pthread_mutex_lock(&g_bookmark_cache_mutex);
	for (int i = 0; i < MAX_BOOKMARK_FILES; i++) {
		free(g_db_cache[i].json);
		g_db_cache[i].json = NULL;
		g_db_cache[i].json_len = 0;
		g_db_cache[i].mtime = 0;
		g_db_cache[i].path[0] = '\0';
	}
	g_db_cache_count = 0;
	pthread_mutex_unlock(&g_bookmark_cache_mutex);
	pthread_mutex_destroy(&g_bookmark_cache_mutex);
}

// Add a database path to the cache (call during startup for each DB)
void bookmark_cache_add_db(const char *path)
{
	if (!path) return;

	pthread_mutex_lock(&g_bookmark_cache_mutex);
	if (g_db_cache_count < MAX_BOOKMARK_FILES) {
		bookmark_cache_entry_t *entry = &g_db_cache[g_db_cache_count];
		strncpy(entry->path, path, sizeof(entry->path) - 1);
		entry->path[sizeof(entry->path) - 1] = '\0';
		entry->json = NULL;
		entry->json_len = 0;
		entry->mtime = 0;
		g_db_cache_count++;
	}
	pthread_mutex_unlock(&g_bookmark_cache_mutex);
}

// Find or create cache entry for a path
static bookmark_cache_entry_t *get_or_create_entry(const char *path)
{
	for (int i = 0; i < g_db_cache_count; i++) {
		if (strcmp(g_db_cache[i].path, path) == 0) {
			return &g_db_cache[i];
		}
	}
	if (g_db_cache_count < MAX_BOOKMARK_FILES) {
		bookmark_cache_entry_t *entry = &g_db_cache[g_db_cache_count];
		strncpy(entry->path, path, sizeof(entry->path) - 1);
		entry->path[sizeof(entry->path) - 1] = '\0';
		g_db_cache_count++;
		return entry;
	}
	return NULL;
}

const char *get_cached_bookmark_json(const char *path)
{
	if (!path) return NULL;

	pthread_mutex_lock(&g_bookmark_cache_mutex);

	bookmark_cache_entry_t *entry = get_or_create_entry(path);
	if (!entry) {
		pthread_mutex_unlock(&g_bookmark_cache_mutex);
		return NULL;
	}

	// First load or path changed
	if (!entry->json || strcmp(entry->path, path) != 0) {
		LOG_INFO("Loading bookmark DB (first load): %s", path);
	} else {
		// Check mtime for cache invalidation
		struct stat st;
		if (stat(path, &st) == 0 && st.st_mtime != entry->mtime) {
			LOG_INFO("Reloading bookmark DB (mtime changed): %s (old: %ld, new: %ld)",
			         path, (long)entry->mtime, (long)st.st_mtime);
		} else {
			// Cache hit - same mtime
			pthread_mutex_unlock(&g_bookmark_cache_mutex);
			return entry->json;
		}
	}

	// Load new file
	if (load_bookmark_json(path, entry) == 0) {
		strncpy(entry->path, path, sizeof(entry->path) - 1);
		entry->path[sizeof(entry->path) - 1] = '\0';
		struct stat st;
		if (stat(path, &st) == 0) {
			entry->mtime = st.st_mtime;
		}
		pthread_mutex_unlock(&g_bookmark_cache_mutex);
		return entry->json;
	}

	pthread_mutex_unlock(&g_bookmark_cache_mutex);
	return NULL;
}

size_t get_cached_bookmark_json_len(void)
{
	// Return length of the last accessed database (index 0 if available)
	if (g_db_cache_count > 0 && g_db_cache[0].json) {
		return g_db_cache[0].json_len;
	}
	return 0;
}

const char *get_cached_bookmark_json_by_index(int index)
{
	if (index < 0 || index >= g_db_cache_count) return NULL;

	pthread_mutex_lock(&g_bookmark_cache_mutex);
	bookmark_cache_entry_t *entry = &g_db_cache[index];

	if (!entry->json) {
		// Try to load if not loaded yet
		if (load_bookmark_json(entry->path, entry) == 0) {
			struct stat st;
			if (stat(entry->path, &st) == 0) {
				entry->mtime = st.st_mtime;
			}
			pthread_mutex_unlock(&g_bookmark_cache_mutex);
			return entry->json;
		}
		pthread_mutex_unlock(&g_bookmark_cache_mutex);
		return NULL;
	}

	// Check mtime for cache invalidation
	struct stat st;
	if (stat(entry->path, &st) == 0 && st.st_mtime != entry->mtime) {
		LOG_INFO("Reloading bookmark DB (mtime changed): %s (old: %ld, new: %ld)",
		         entry->path, (long)entry->mtime, (long)st.st_mtime);
		if (load_bookmark_json(entry->path, entry) == 0) {
			entry->mtime = st.st_mtime;
			pthread_mutex_unlock(&g_bookmark_cache_mutex);
			return entry->json;
		}
		pthread_mutex_unlock(&g_bookmark_cache_mutex);
		return NULL;
	}

	pthread_mutex_unlock(&g_bookmark_cache_mutex);
	return entry->json;
}

size_t get_cached_bookmark_json_len_by_index(int index)
{
	if (index < 0 || index >= g_db_cache_count) return 0;
	return g_db_cache[index].json_len;
}