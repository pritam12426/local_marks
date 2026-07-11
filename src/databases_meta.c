#include "databases_meta.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "log.h"
#include "project_config.h"
#include "server.h"

JSON_DB_meta_data g_db_meta[MAX_BOOKMARK_FILES];
int g_db_meta_count = 0;

// Portable timespec fill from struct stat
static void fill_timespec(struct timespec *out, const struct stat *st)
{
	out->tv_sec = st->st_mtime;
#if defined(__linux__)
	out->tv_nsec = st->st_mtim.tv_nsec;
#elif defined(__APPLE__)
	out->tv_nsec = st->st_mtimespec.tv_nsec;
#else
	out->tv_nsec = 0;
#endif
}

// Populate metadata for all configured bookmark files
void populate_db_meta_all(const ServerConfig *cfg)
{
	g_db_meta_count = 0;
	for (int i = 0; i < cfg->bookmark_file_count; i++) {
		const char *path = cfg->bookmark_files[i];
		if (!path) continue;

		struct stat st;
		if (stat(path, &st) != 0) {
			LOG_WARN("Failed to stat bookmark file '%s': %s", path, strerror(errno));
			continue;
		}

		JSON_DB_meta_data *meta = &g_db_meta[g_db_meta_count];
		meta->mode = st.st_mode & 0777;
		meta->absolute_path = path;
		meta->file_size = (size_t)st.st_size;
		meta->cTime = st.st_ctime;
		meta->bTime = 0;
#if defined(__APPLE__) && defined(__MACH__)
		meta->bTime = st.st_birthtime;
#endif
		meta->uid = st.st_uid;
		meta->gid = st.st_gid;
		fill_timespec(&meta->mTime, &st);

		// Extract file name from path
		const char *base = strrchr(path, '/');
		base = base ? base + 1 : path;
		strncpy(meta->file_name, base, sizeof(meta->file_name) - 1);
		meta->file_name[sizeof(meta->file_name) - 1] = '\0';

		LOG_INFO("Registered bookmark DB[%d]: %s (%zu bytes, mode=%04o, mtime=%ld)",
		         g_db_meta_count, path, (size_t)st.st_size, meta->mode, (long)st.st_mtime);

		g_db_meta_count++;
	}
	LOG_INFO("Loaded %d bookmark database(s)", g_db_meta_count);
}

// Build JSON array of database metadata
// Caller must free the returned string
char *build_databases_json(void)
{
	size_t buf_size = 1024 + (size_t)g_db_meta_count * 256;
	char *buf = malloc(buf_size);
	if (!buf) return NULL;

	size_t offset = 0;
	offset += snprintf(buf + offset, buf_size - offset,
	                   "{\"databases\":[");
	for (int i = 0; i < g_db_meta_count; i++) {
		const JSON_DB_meta_data *meta = &g_db_meta[i];

		if (i > 0) {
			offset += snprintf(buf + offset, buf_size - offset, ",");
		}
		offset += snprintf(buf + offset, buf_size - offset,
		                   "{\"mode\":\"%04o\","
		                   "\"absolute_path\":\"%s\","
		                   "\"file_name\":\"%s\","
		                   "\"file_size\":%zu,"
		                   "\"cTime\":%ld,"
		                   "\"bTime\":%ld,"
		                   "\"uid\":%u,"
		                   "\"gid\":%u,"
		                   "\"mTime_sec\":%ld,"
		                   "\"mTime_nsec\":%ld}",
		                   meta->mode,
		                   meta->absolute_path,
		                   meta->file_name,
		                   meta->file_size,
		                   (long)meta->cTime,
		                   (long)meta->bTime,
		                   (unsigned)meta->uid,
		                   (unsigned)meta->gid,
		                   (long)meta->mTime.tv_sec,
		                   (long)meta->mTime.tv_nsec);
	}
	offset += snprintf(buf + offset, buf_size - offset,
	                   "],\"count\":%d}", g_db_meta_count);

	return buf;
}

// Build JSON for a single database by index
// Caller must free the returned string
char *build_database_json(int index)
{
	if (index < 0 || index >= g_db_meta_count) return NULL;

	const JSON_DB_meta_data *meta = &g_db_meta[index];
	size_t buf_size = 512;
	char *buf = malloc(buf_size);
	if (!buf) return NULL;

	int len = snprintf(buf, buf_size,
	                   "{\"mode\":\"%04o\","
	                   "\"absolute_path\":\"%s\","
	                   "\"file_name\":\"%s\","
	                   "\"file_size\":%zu,"
	                   "\"cTime\":%ld,"
	                   "\"bTime\":%ld,"
	                   "\"uid\":%u,"
	                   "\"gid\":%u,"
	                   "\"mTime_sec\":%ld,"
	                   "\"mTime_nsec\":%ld}",
	                   meta->mode,
	                   meta->absolute_path,
	                   meta->file_name,
	                   meta->file_size,
	                   (long)meta->cTime,
	                   (long)meta->bTime,
	                   (unsigned)meta->uid,
	                   (unsigned)meta->gid,
	                   (long)meta->mTime.tv_sec,
	                   (long)meta->mTime.tv_nsec);

	if (len < 0 || (size_t)len >= buf_size) {
		free(buf);
		return NULL;
	}
	return buf;
}

void db_meta_cleanup(void)
{
	g_db_meta_count = 0;
}