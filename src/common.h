#ifndef _COMMON__H_
#define _COMMON__H_


#define MAX_BOOKMARK_FILES 10


#if !defined(PATH_MAX) && defined(_POSIX_PATH_MAX)
#define PATH_MAX _POSIX_PATH_MAX
#elif !defined(PATH_MAX)
#define PATH_MAX 4096
#endif


#endif  // _COMMON__H_
