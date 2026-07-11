#include "bookmark_cache.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "log.h"
#include "project_config.h"

// Global cache state (single entry for first DB file)
static char *g_bookmark_json = NULL;
static size_t g_bookmark_json_len = 0;
static pthread_mutex_t g_bookmark_cache_mutex = PTHREAD_MUTEX_INITIALIZER;
static time_t g_bookmark_mtime = 0;
static char g_bookmark_cache_path[256] = {0};

// Load bookmark JSON file into memory
static int load_bookmark_json(const char *path)
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

	g_bookmark_json = malloc((size_t)size + 1);
	if (!g_bookmark_json) {
		LOG_ERROR("Failed to allocate %ld bytes for bookmark JSON", size);
		fclose(f);
		return -1;
	}

	size_t read = fread(g_bookmark_json, 1, (size_t)size, f);
	fclose(f);

	if (read != (size_t)size) {
		LOG_ERROR("Short read on '%s': expected %ld, got %zu", path, size, read);
		free(g_bookmark_json);
		g_bookmark_json = NULL;
		return -1;
	}

	g_bookmark_json[read] = '\0';
	g_bookmark_json_len = read;
	LOG_INFO("Loaded bookmark database: %s (%zu bytes)", path, g_bookmark_json_len);
	return 0;
}

void bookmark_cache_init(void)
{
	pthread_mutex_init(&g_bookmark_cache_mutex, NULL);
	g_bookmark_json = NULL;
	g_bookmark_json_len = 0;
	g_bookmark_mtime = 0;
	g_bookmark_cache_path[0] = '\0';
}

void bookmark_cache_cleanup(void)
{
	pthread_mutex_lock(&g_bookmark_cache_mutex);
	free(g_bookmark_json);
	g_bookmark_json = NULL;
	g_bookmark_json_len = 0;
	g_bookmark_mtime = 0;
	g_bookmark_cache_path[0] = '\0';
	pthread_mutex_unlock(&g_bookmark_cache_mutex);
	pthread_mutex_destroy(&g_bookmark_cache_mutex);
}

const char *get_cached_bookmark_json(const char *path)
{
	if (!path) return NULL;

	pthread_mutex_lock(&g_bookmark_cache_mutex);

	// First load or path changed
	if (!g_bookmark_json || strcmp(g_bookmark_cache_path, path) != 0) {
		LOG_INFO("Loading bookmark DB (first load): %s", path);
	} else {
		// Check mtime for cache invalidation
		struct stat st;
		if (stat(path, &st) == 0 && st.st_mtime != g_bookmark_mtime) {
			LOG_INFO("Reloading bookmark DB (mtime changed): %s (old: %ld, new: %ld)",
			         path, (long)g_bookmark_mtime, (long)st.st_mtime);
		} else {
			// Cache hit - same mtime
			pthread_mutex_unlock(&g_bookmark_cache_mutex);
			return g_bookmark_json;
		}
	}

	// Load new file
	if (load_bookmark_json(path) == 0) {
		strncpy(g_bookmark_cache_path, path, sizeof(g_bookmark_cache_path) - 1);
		g_bookmark_cache_path[sizeof(g_bookmark_cache_path) - 1] = '\0';
		// Get actual mtime
		struct stat st;
		if (stat(path, &st) == 0) {
			g_bookmark_mtime = st.st_mtime;
		}
		pthread_mutex_unlock(&g_bookmark_cache_mutex);
		return g_bookmark_json;
	}

	pthread_mutex_unlock(&g_bookmark_cache_mutex);
	return NULL;
}

size_t get_cached_bookmark_json_len(void)
{
	return g_bookmark_json_len;
}