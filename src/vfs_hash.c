#include "vfs_hash.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

/* ---- The one knob: set this to (at least) how many files you embed. ----
 * Everything else below derives from it automatically. */
#define VFS_MAX_FILES 10

/* Rounds x up to the next power of two, as a compile-time constant
 * expression (standard bit-smearing trick — no compiler-specific
 * builtins, so it's portable to any C17 compiler). Used only to turn
 * VFS_MAX_FILES into a valid power-of-two table size. */
#define VFS__PO2_1(x)    ((x) | ((x) >> 1))
#define VFS__PO2_2(x)    (VFS__PO2_1(x) | (VFS__PO2_1(x) >> 2))
#define VFS__PO2_4(x)    (VFS__PO2_2(x) | (VFS__PO2_2(x) >> 4))
#define VFS__PO2_8(x)    (VFS__PO2_4(x) | (VFS__PO2_4(x) >> 8))
#define VFS__PO2_16(x)   (VFS__PO2_8(x) | (VFS__PO2_8(x) >> 16))
#define VFS_NEXT_POW2(x) (VFS__PO2_16((x) - 1) + 1)

/* Keep load factor under 50% for good average-case probe lengths, so we
 * size the table for 2x the expected file count, rounded up to a power
 * of two. At VFS_MAX_FILES=20 this comes out to 64 —
 * but now it scales automatically if you raise VFS_MAX_FILES. */
#define VFS_HASH_CAP VFS_NEXT_POW2(VFS_MAX_FILES * 2)

static const vfs_entry *slots[VFS_HASH_CAP];

static uint32_t fnv1a(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++) {
		h ^= (unsigned char) *s;
		h *= 16777619u;
	}
	return h;
}

void vfs_hash_init(void)
{
	memset(slots, 0, sizeof slots);

	size_t inserted = 0;
	for (size_t i = 0; vfs[i].file_path; i++) {
		if (inserted >= VFS_HASH_CAP) {
			LOG_ERROR("vfs_hash: %zu embedded files exceed VFS_HASH_CAP=%d; "
			          "raise VFS_MAX_FILES in vfs_hash.c",
			          inserted + 1,
			          VFS_HASH_CAP);
			abort();
		}

		uint32_t idx    = fnv1a(vfs[i].file_path) & (VFS_HASH_CAP - 1);
		size_t   probes = 0;
		while (slots[idx] != NULL) {
			idx = (idx + 1) & (VFS_HASH_CAP - 1); /* linear probing */
			probes++;
			if (probes >= VFS_HASH_CAP) {
				/* Table is completely full: without this bound, the loop
				 * above would spin forever since no empty slot exists. */
				LOG_ERROR("vfs_hash: hash table full while inserting \"%s\"; "
				          "raise VFS_MAX_FILES in vfs_hash.c",
				          vfs[i].file_path);
				abort();
			}
		}
		slots[idx] = &vfs[i];
		LOG_DEBUG("vfs_hash: inserted \"%s\" at slot %u (%zu probes)",
		          vfs[i].file_path,
		          idx,
		          probes);
		inserted++;
	}

	if (inserted * 2 >= VFS_HASH_CAP) {
		LOG_WARN("vfs_hash: load factor at or above 50%% (%zu files in %d "
		         "slots); consider raising VFS_MAX_FILES for better lookup performance",
		         inserted,
		         VFS_HASH_CAP);
	}

	LOG_DEBUG("vfs_hash: initialized with %zu files in %d slots (load factor %.0f%%)",
	          inserted,
	          VFS_HASH_CAP,
	          (double) inserted * 100.0 / VFS_HASH_CAP);
}

const vfs_entry *vfs_lookup(const char *path)
{
	uint32_t idx = fnv1a(path) & (VFS_HASH_CAP - 1);
	for (size_t i = 0; i < VFS_HASH_CAP; i++) {
		const vfs_entry *e = slots[idx];
		if (e == NULL) {
			LOG_DEBUG("vfs_hash: lookup \"%s\" -> miss (%zu probes)", path, i);
			return NULL; /* empty slot: definitely not present (no deletions occur) */
		}
		if (strcmp(e->file_path, path) == 0) {
			LOG_DEBUG("vfs_hash: lookup \"%s\" -> hit (%zu probes)", path, i);
			return e;
		}
		idx = (idx + 1) & (VFS_HASH_CAP - 1);
	}

	LOG_DEBUG("vfs_hash: lookup \"%s\" -> miss (table exhausted)", path);
	return NULL;
}
