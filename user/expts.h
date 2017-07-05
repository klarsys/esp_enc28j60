#ifndef EXPTS_H
#define EXPTS_H

#include "httpd.h"

int exptGetUUID(HttpdConnData *connData);
int exptSetGPIO(HttpdConnData *connData);
int exptGetGPIO(HttpdConnData *connData);
int exptBlast(HttpdConnData *connData);

typedef void (*irSendCb)(void *arg, char *err);

int ICACHE_FLASH_ATTR irSend(char *mod, char **cmd, irSendCb cb, void *arg);
int ICACHE_FLASH_ATTR irSendStop(void);
enum {
	irLearnFmtSendir,
	irLearnFmtDecode
};
int ICACHE_FLASH_ATTR irLearn(int fmt, void (*cb)(void *, char *), void *arg);
int ICACHE_FLASH_ATTR irLearnStop(void);

void ICACHE_FLASH_ATTR irInit(void);

#endif
