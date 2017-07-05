#ifndef CONSOLE_H_
#define CONSOLE_H_


#if 0
	#define DEBUG(fmt, args...) os_printf("DEBUG[%s,%d]: " fmt "\r\n", __FILE__, __LINE__, ##args)
	#define INFO(fmt, args...) os_printf("INFO[%s,%d]: " fmt "\r\n", __FILE__, __LINE__, ##args)
	#define WARN(fmt, args...) os_printf("WARN[%s,%d]: " fmt "\r\n", __FILE__, __LINE__, ##args)
	#define ERROR(fmt, args...) os_printf("ERROR[%s,%d]: " fmt "\r\n", __FILE__, __LINE__, ##args)
//#else
	#define DEBUG(fmt, args...) ((void)0)
	#define INFO DEBUG
	#define WARN DEBUG
	#define ERROR DEBUG
#endif

enum {
	DLVL_ERROR,
	DLVL_WARN,
	DLVL_INFO,
	DLVL_DEBUG,
	DLVL_TRACE // Prints outside the firmware proper (i.e., from libraries) come only at this level
};

void ICACHE_FLASH_ATTR debugInit(void);
int ICACHE_FLASH_ATTR debugSetLevel(int level);
void ICACHE_FLASH_ATTR debugPrint(int level, const char *file, int line, const char *fmt, ...);
void ICACHE_FLASH_ATTR debugMemInfo(char *s, int len);

//#define DEBUG(fmt, args...) debugPrint(DLVL_DEBUG, __FILE__, __LINE__, fmt, ##args)
#define DEBUG(fmt, args...) ((void)0)
#define INFO(fmt, args...) debugPrint(DLVL_INFO, __FILE__, __LINE__, fmt, ##args)
#define WARN(fmt, args...) debugPrint(DLVL_WARN, __FILE__, __LINE__, fmt, ##args)
#define ERROR(fmt, args...) debugPrint(DLVL_ERROR, __FILE__, __LINE__, fmt, ##args)

enum {
	DHP_FREE,
	DHP_ALLOC,
	DHP_ZALLOC
};
void * ICACHE_FLASH_ATTR debugHeap(const char *file, int line, int op, int sz);

#define MALLOC(n)  debugHeap(__FILE__, __LINE__, DHP_ALLOC, (n))
#define ZALLOC(n)  debugHeap(__FILE__, __LINE__, DHP_ZALLOC, (n))
#define FREE(p)  debugHeap(__FILE__, __LINE__, DHP_FREE, (int)(p))

#include <stdarg.h>
int rpl_vsnprintf(char *, size_t, const char *, va_list);
int rpl_snprintf(char *, size_t, const char *, ...);

#define SAFE_STRCPY(dst, src)  (dst[sizeof(dst)-1] = 0, os_strncpy(dst, src?src:"", sizeof(dst)-1))

#endif
