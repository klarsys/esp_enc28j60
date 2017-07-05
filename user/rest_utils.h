
#ifndef REST_UTILS_H_
#define REST_UTILS_H_

#include "httpd.h"
#include "debug.h"

int ICACHE_FLASH_ATTR sendOK(HttpdConnData *connData, const char *t);
int ICACHE_FLASH_ATTR sendJSON(HttpdConnData *connData);
int ICACHE_FLASH_ATTR sendTxt(HttpdConnData *connData, const char *t);
uint32 ICACHE_FLASH_ATTR toHex(const char *p);

#define HTTPD_SEND_STR(str)  httpdSend(connData, (str), strlen(str))

#define HTTPD_PRINTF(fmt, args...)  do {			\
		char buf[1024];								\
		rpl_snprintf(buf, sizeof(buf), fmt, ##args);\
		sendTxt(connData, buf);						\
	} while (0)

#define URL_IS(y) (os_strstr(connData->url, "/api/") \
			&& !os_strncmp(os_strstr(connData->url, "/api/"), (y), strlen(y)))

#define MAX2(x,y) (((x)<(y))?(y):(x))
#define MIN2(x,y) (((x)>(y))?(y):(x))

void ICACHE_FLASH_ATTR setTimeout(ETSTimer *timer, void *fxn, void *arg, uint32 t);

char * ICACHE_FLASH_ATTR itach_token(char **data, int *len);
char * ICACHE_FLASH_ATTR itach_rest(char **data, int *len);
int ICACHE_FLASH_ATTR itach_num(char **data, int *len);

#endif
