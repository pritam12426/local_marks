#ifndef _VFS_HASH_H_
#define _VFS_HASH_H_


#include "embd_front_end.h"  // vfs_entry, vfs_table[]

/* Populates the hash table from the generated vfs[] array.
 * Call once at startup, before any vfs_lookup() calls. */
void vfs_hash_init(void);

/* O(1) amortized lookup by file_path, e.g. "javascript/main.js".
 * Returns NULL if not found. */
const vfs_entry *vfs_lookup(const char *path);


#endif  // _VFS_HASH_H_
