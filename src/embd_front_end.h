#ifndef _EMBD_FRONT_END__H_
#define _EMBD_FRONT_END__H_


#include <stddef.h>
#include <stdint.h>

typedef struct {
	const char          *file_path;
	const unsigned char *file_start;
	size_t               file_len;
	uint32_t             content_hash;  // FNV-1a hash, computed once at init
} vfs_entry;

extern vfs_entry vfs_table[];


#endif  // _EMBD_FRONT_END__H_
