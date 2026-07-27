/*
 * Copyright (c) 2026 Pritam
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * ratelimit.c — Per-IP connection rate limiting
 *
 * Uses a dynamic open-addressing hash table that grows as needed.
 * Thread safety is provided by a single mutex covering the table.
 */

#include "ratelimit.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"

#define RL_INITIAL_SIZE 256
#define RL_MAX_LOAD_FACTOR 0.75
#define RL_GROWTH_FACTOR 2
#define RL_STALE_TIMEOUT_SECS 60

typedef struct {
	char  *ip;   // Dynamically allocated IP string
	int   count;
	time_t last_activity;  // Last time this entry was touched
} RLEntry;

struct RateLimit {
	int             max_per_ip;
	RLEntry        *table;
	size_t          table_size;
	size_t          used;
	time_t          last_sweep;  // Last time stale entries were swept
	pthread_mutex_t lock;
};

// djb2 hash — simple, fast, good enough for IP strings
static unsigned long hash_ip(const char *ip)
{
	unsigned long h = 5381;
	int c;
	while ((c = *ip++))
		h = ((h << 5) + h) + (unsigned long)c;
	return h;
}

// Grow the hash table
static int ratelimit_grow(RateLimit *rl)
{
	size_t new_size = rl->table_size * RL_GROWTH_FACTOR;
	RLEntry *new_table = calloc(new_size, sizeof(RLEntry));
	if (!new_table) return -1;

	// Rehash all existing entries (skip stale zero-count entries)
	for (size_t i = 0; i < rl->table_size; i++) {
		if (rl->table[i].count > 0) {
			unsigned long idx = hash_ip(rl->table[i].ip) % new_size;
			for (size_t j = 0; j < new_size; j++) {
				size_t slot = (idx + j) % new_size;
				if (new_table[slot].count == 0) {
					new_table[slot] = rl->table[i];
					break;
				}
			}
		} else if (rl->table[i].ip) {
			free(rl->table[i].ip);
		}
	}

	free(rl->table);
	rl->table = new_table;
	rl->table_size = new_size;
	return 0;
}

// Evict entries with count == 0 that haven't been touched recently.
// Caller must hold rl->lock.
static void ratelimit_sweep_stale(RateLimit *rl)
{
	time_t now = time(NULL);
	for (size_t i = 0; i < rl->table_size; i++) {
		if (rl->table[i].count == 0 && rl->table[i].ip != NULL) {
			if (now - rl->table[i].last_activity > RL_STALE_TIMEOUT_SECS) {
				free(rl->table[i].ip);
				rl->table[i].ip = NULL;
				rl->used--;
			}
		}
	}
}

RateLimit *ratelimit_create(int max_conns_per_ip)
{
	if (max_conns_per_ip <= 0) return NULL;

	RateLimit *rl = calloc(1, sizeof(*rl));
	if (!rl) return NULL;

	rl->max_per_ip = max_conns_per_ip;
	rl->table_size = RL_INITIAL_SIZE;
	rl->used = 0;
	rl->last_sweep = time(NULL);
	rl->table = calloc(RL_INITIAL_SIZE, sizeof(RLEntry));
	if (!rl->table) {
		free(rl);
		return NULL;
	}

	pthread_mutex_init(&rl->lock, NULL);

	LOG_DEBUG("Rate limit created: max %d conns/IP, initial size %zu",
	          max_conns_per_ip, RL_INITIAL_SIZE);
	return rl;
}

int ratelimit_accept(RateLimit *rl, const char *ip)
{
	if (!rl) return 0;

	pthread_mutex_lock(&rl->lock);

	// Sweep stale entries periodically instead of on every accept
	time_t now = time(NULL);
	if (now - rl->last_sweep >= RL_STALE_TIMEOUT_SECS) {
		ratelimit_sweep_stale(rl);
		rl->last_sweep = now;
	}

	// Check load factor and grow if needed (using integer math to avoid float conversion)
	if (rl->used * 4 >= rl->table_size * 3) {
		if (ratelimit_grow(rl) != 0) {
			LOG_WARN("Rate limit table grow failed, continuing with current size");
		}
	}

	unsigned long idx = hash_ip(ip) % rl->table_size;

	// Linear probe: find either an empty slot or this IP's slot
	for (size_t i = 0; i < rl->table_size; i++) {
		size_t slot = (idx + i) % rl->table_size;
		if (rl->table[slot].count == 0 && rl->table[slot].ip == NULL) {
			// New entry (or reclaimed stale entry)
			rl->table[slot].ip = strdup(ip);
			if (!rl->table[slot].ip) {
				pthread_mutex_unlock(&rl->lock);
				return -1;
			}
			rl->table[slot].count = 1;
			rl->table[slot].last_activity = now;
			rl->used++;
			pthread_mutex_unlock(&rl->lock);
			return 0;
		}
		if (strcmp(rl->table[slot].ip, ip) == 0) {
			// Existing entry
			rl->table[slot].last_activity = now;
			if (rl->table[slot].count >= rl->max_per_ip) {
				LOG_WARN("Rate limit exceeded for %s (%d)", ip, rl->table[slot].count);
				pthread_mutex_unlock(&rl->lock);
				return -1;
			}
			rl->table[slot].count++;
			LOG_TRACE("Rate limit accept: %s (count=%d/%d)",
			          ip, rl->table[slot].count, rl->max_per_ip);
			pthread_mutex_unlock(&rl->lock);
			return 0;
		}
	}

	LOG_WARN("Rate limit table full, rejecting %s", ip);
	pthread_mutex_unlock(&rl->lock);
	return -1;
}

void ratelimit_leave(RateLimit *rl, const char *ip)
{
	if (!rl) return;

	pthread_mutex_lock(&rl->lock);

	unsigned long idx = hash_ip(ip) % rl->table_size;
	time_t now = time(NULL);

	for (size_t i = 0; i < rl->table_size; i++) {
		size_t slot = (idx + i) % rl->table_size;
		if (rl->table[slot].count > 0 && strcmp(rl->table[slot].ip, ip) == 0) {
			rl->table[slot].count--;
			rl->table[slot].last_activity = now;
			LOG_TRACE("Rate limit leave: %s (count=%d)", ip, rl->table[slot].count);
			// Don't free at count==0 — keep as dormant entry for reuse.
			// ratelimit_sweep_stale() handles cleanup of old dormant entries.
			break;
		}
	}

	pthread_mutex_unlock(&rl->lock);
}

void ratelimit_destroy(RateLimit *rl)
{
	if (!rl) return;

	// Free all IP strings
	for (size_t i = 0; i < rl->table_size; i++) {
		if (rl->table[i].ip) {
			free(rl->table[i].ip);
		}
	}

	free(rl->table);
	pthread_mutex_destroy(&rl->lock);
	free(rl);
	LOG_DEBUG("Rate limit destroyed");
}
