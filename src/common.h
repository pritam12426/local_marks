#ifndef _COMMON_H_
#define _COMMON_H_


#define MAX_BOOKMARK_FILES 10


#if !defined(PATH_MAX) && defined(_POSIX_PATH_MAX)
#define PATH_MAX _POSIX_PATH_MAX
#elif !defined(PATH_MAX)
#define PATH_MAX 4096
#endif


#endif  // _COMMON_H_
