#ifndef _EMBD_FRONT_END_H_
#define _EMBD_FRONT_END_H_


#include <stddef.h>

typedef struct {
	const char          *file_path;
	const unsigned char *file_start;
	size_t               file_len;
} vfs_entry;

extern vfs_entry vfs_table[];


#endif  // _EMBD_FRONT_END_H_
