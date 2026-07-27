/*
 * fnv1a.h — FNV-1a hash (inline, header-only)
 *
 * Two variants:
 *   fnv1a_str(s)           — hash a NUL-terminated string
 *   fnv1a_data(data, len)  — hash an arbitrary byte buffer
 */

#ifndef _FNV1A__H_
#define _FNV1A__H_

#include <stddef.h>
#include <stdint.h>

static inline uint32_t fnv1a_str(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++) {
		h ^= (unsigned char)*s;
		h *= 16777619u;
	}
	return h;
}

static inline uint32_t fnv1a_data(const unsigned char *data, size_t len)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < len; i++) {
		h ^= data[i];
		h *= 16777619u;
	}
	return h;
}

#endif  // _FNV1A__H_
