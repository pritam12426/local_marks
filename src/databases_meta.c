#include "databases_meta.h"

#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "log.h"
#include "project_config.h"
#include "server.h"

// Escape a string for safe inclusion in a JSON string value.
// Writes to `dst` with at most `dst_size` bytes (including NUL).
// Returns the number of bytes written (excluding NUL), or -1 on truncation.
static size_t json_escape_string(char *dst, size_t dst_size, const char *src)
{
	size_t di = 0;
	for (const char *p = src; *p; p++) {
		char esc = 0;
		switch (*p) {
		case '\"': esc = '"';  break;
		case '\\': esc = '\\'; break;
		case '\b': esc = 'b';  break;
		case '\f': esc = 'f';  break;
		case '\n': esc = 'n';  break;
		case '\r': esc = 'r';  break;
		case '\t': esc = 't';  break;
		default:
			if ((unsigned char)*p < 0x20) {
				// Control character → \u00XX
				if (di + 6 >= dst_size) return (size_t)-1;
				snprintf(dst + di, dst_size - di, "\\u%04x", (unsigned char)*p);
				di += 6;
				continue;
			}
			// Regular character
			if (di + 1 >= dst_size) return (size_t)-1;
			dst[di++] = *p;
			continue;
		}
		// Escaped character: \X
		if (di + 2 >= dst_size) return (size_t)-1;
		dst[di++] = '\\';
		dst[di++] = esc;
	}
	if (di >= dst_size) return (size_t)-1;
	dst[di] = '\0';
	return di;
}

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
#else  // __linux__
	out->tv_nsec = 0;
#endif
}

// Get user name from uid
static void get_user_name(uid_t uid, char *buf, size_t buf_size)
{
	struct passwd *pwd = getpwuid(uid);
	if (pwd) {
		strncpy(buf, pwd->pw_name, buf_size - 1);
		buf[buf_size - 1] = '\0';
	} else {
		snprintf(buf, buf_size, "%u", (unsigned)uid);
	}
}

// Get group name from gid
static void get_group_name(gid_t gid, char *buf, size_t buf_size)
{
	struct group *grp = getgrgid(gid);
	if (grp) {
		strncpy(buf, grp->gr_name, buf_size - 1);
		buf[buf_size - 1] = '\0';
	} else {
		snprintf(buf, buf_size, "%u", (unsigned)gid);
	}
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

		// Resolve absolute path
		char abs_path[PATH_MAX];
		if (!realpath(path, abs_path)) {
			LOG_WARN("Failed to resolve absolute path for '%s': %s", path, strerror(errno));
			strncpy(abs_path, path, sizeof(abs_path) - 1);
			abs_path[sizeof(abs_path) - 1] = '\0';
		}

		JSON_DB_meta_data *meta = &g_db_meta[g_db_meta_count];
		meta->mode = st.st_mode & 0777;
		strncpy(meta->absolute_path, abs_path, sizeof(meta->absolute_path) - 1);
		meta->absolute_path[sizeof(meta->absolute_path) - 1] = '\0';
		meta->file_size = (size_t)st.st_size;
		meta->cTime = st.st_ctime;
		meta->bTime = 0;
#if defined(__APPLE__)
		meta->bTime = st.st_birthtime;
#endif
		get_user_name(st.st_uid, meta->user, sizeof(meta->user));
		get_group_name(st.st_gid, meta->group, sizeof(meta->group));
		fill_timespec(&meta->mTime, &st);

		// Extract file name from path
		const char *base = strrchr(path, '/');
		base = base ? base + 1 : path;
		strncpy(meta->file_name, base, sizeof(meta->file_name) - 1);
		meta->file_name[sizeof(meta->file_name) - 1] = '\0';

		LOG_INFO("Registered bookmark DB[%d]: %s", g_db_meta_count, base);

		g_db_meta_count++;
	}
	LOG_INFO("Loaded %d bookmark database(s)", g_db_meta_count);
}

// Build JSON array of database metadata
// Caller must free the returned string
char *build_databases_json(void)
{
	// Each entry: escaped strings (up to 2x expansion) + JSON keys/numbers (~200) + overhead
	size_t buf_size = 1024 + (size_t)g_db_meta_count * (PATH_MAX * 2 + 2048);
	char *buf = malloc(buf_size);
	if (!buf) return NULL;

	char esc_path[PATH_MAX * 2];
	char esc_name[512];
	char esc_user[512];
	char esc_group[512];

	size_t offset = 0;
	int n;

	n = snprintf(buf + offset, buf_size - offset, "{\"databases\":[");
	if (n < 0) { free(buf); return NULL; }
	offset += (size_t)n;

	for (int i = 0; i < g_db_meta_count; i++) {
		const JSON_DB_meta_data *meta = &g_db_meta[i];

		// Escape all user-controlled strings
		json_escape_string(esc_path, sizeof esc_path, meta->absolute_path);
		json_escape_string(esc_name, sizeof esc_name, meta->file_name);
		json_escape_string(esc_user, sizeof esc_user, meta->user);
		json_escape_string(esc_group, sizeof esc_group, meta->group);

		if (i > 0) {
			n = snprintf(buf + offset, buf_size - offset, ",");
			if (n < 0 || offset + (size_t)n >= buf_size) { free(buf); return NULL; }
			offset += (size_t)n;
		}
		n = snprintf(buf + offset, buf_size - offset,
		                   "{\"mode\":\"%04o\","
		                   "\"absolute_path\":\"%s\","
		                   "\"file_name\":\"%s\","
		                   "\"file_size\":%zu,"
		                   "\"cTime\":%ld,"
		                   "\"bTime\":%ld,"
		                   "\"user\":\"%s\","
		                   "\"group\":\"%s\","
		                   "\"mTime_sec\":%ld,"
		                   "\"mTime_nsec\":%ld}",
		                   meta->mode,
		                   esc_path,
		                   esc_name,
		                   meta->file_size,
		                   (long)meta->cTime,
		                   (long)meta->bTime,
		                   esc_user,
		                   esc_group,
		                   (long)meta->mTime.tv_sec,
		                   (long)meta->mTime.tv_nsec);
		if (n < 0 || offset + (size_t)n >= buf_size) { free(buf); return NULL; }
		offset += (size_t)n;
	}

	n = snprintf(buf + offset, buf_size - offset, "],\"count\":%d}", g_db_meta_count);
	if (n < 0 || offset + (size_t)n >= buf_size) { free(buf); return NULL; }
	offset += (size_t)n;

	return buf;
}

// Build JSON for a single database by index
// Caller must free the returned string
char *build_database_json(int index)
{
	if (index < 0 || index >= g_db_meta_count) return NULL;

	const JSON_DB_meta_data *meta = &g_db_meta[index];
	size_t buf_size = 1024;
	char *buf = malloc(buf_size);
	if (!buf) return NULL;

	char esc_path[PATH_MAX * 2];
	char esc_name[512];
	char esc_user[512];
	char esc_group[512];

	json_escape_string(esc_path, sizeof esc_path, meta->absolute_path);
	json_escape_string(esc_name, sizeof esc_name, meta->file_name);
	json_escape_string(esc_user, sizeof esc_user, meta->user);
	json_escape_string(esc_group, sizeof esc_group, meta->group);

	int len = snprintf(buf, buf_size,
	                   "{\"mode\":\"%04o\","
	                   "\"absolute_path\":\"%s\","
	                   "\"file_name\":\"%s\","
	                   "\"file_size\":%zu,"
	                   "\"cTime\":%ld,"
	                   "\"bTime\":%ld,"
	                   "\"user\":\"%s\","
	                   "\"group\":\"%s\","
	                   "\"mTime_sec\":%ld,"
	                   "\"mTime_nsec\":%ld}",
	                   meta->mode,
	                   esc_path,
	                   esc_name,
	                   meta->file_size,
	                   (long)meta->cTime,
	                   (long)meta->bTime,
	                   esc_user,
	                   esc_group,
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
