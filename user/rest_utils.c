#include <esp8266.h>
#include "rest_utils.h"

static void ICACHE_FLASH_ATTR sendHeaders(HttpdConnData *connData, int status, const char *ctype)
{
	if (!connData->conn)
		return; // MQTT response using faked connData object
	DEBUG("sending headers %s", ctype);
	httpdStartResponse(connData, status);
	httpdHeader(connData, "Access-Control-Allow-Origin",  "*");
	httpdHeader(connData, "Access-Control-Allow-Credentials",  "true");
	httpdHeader(connData, "Access-Control-Allow-Methods", "PUT, POST, GET, OPTIONS");
	httpdHeader(connData, "Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
	httpdHeader(connData, "Content-Type", ctype);
	httpdEndHeaders(connData);
}
int ICACHE_FLASH_ATTR sendOK(HttpdConnData *connData, const char *t)
{
	sendHeaders(connData, 200, "application/json");
	HTTPD_PRINTF("{\"status\":\"%s\"}", t);
	return 1;

}
int ICACHE_FLASH_ATTR sendTxt(HttpdConnData *connData, const char *t)
{
	sendHeaders(connData, 200, "text/plain; charset=us-ascii");
	if (t[0])
		HTTPD_SEND_STR(t);
	return 1;

}
int ICACHE_FLASH_ATTR sendJSON(HttpdConnData *connData)
{
	sendHeaders(connData, 200, "application/json");
	return 1;
}
uint32 ICACHE_FLASH_ATTR toHex(const char *p)
{
	int ret = 0;
	if (!os_strncmp(p, "0x", 2) || !os_strncmp(p, "0X", 2))
		p += 2;
	while (*p) {
		if (*p >= '0' && *p <= '9')
			ret = (ret << 4) + (*p - '0');
		else if  (*p >= 'a' && *p <= 'f')
			ret = (ret << 4) + (*p - 'a') + 10;
		else if  (*p >= 'A' && *p <= 'F')
			ret = (ret << 4) + (*p - 'A') + 10;
		else
			return ret;
		++p;
	}
	return ret;
}

void ICACHE_FLASH_ATTR setTimeout(ETSTimer *timer, void *fxn, void *arg, uint32 t)
{
	os_timer_disarm(timer);
	os_timer_setfn(timer, (os_timer_func_t *)fxn, arg);
	os_timer_arm(timer, t, 0);
}
char * ICACHE_FLASH_ATTR itach_token(char **data, int *len)
{
        char *ret = (*len > 0)?*data:NULL;
        while ((*len > 0) && (**data)) {
                if (**data == ',' || **data == '\r' || **data == '\n') {
                        **data = 0;
                        (*data)++;
                        (*len)--;
                        break;
                }
                (*data)++;
                (*len)--;
        }
        return ret;
}
char * ICACHE_FLASH_ATTR itach_rest(char **data, int *len)
{
        char *ret = (*len > 0)?*data:NULL;
        while ((*len > 0)) {
                if (**data == '\r' || **data == '\n' || **data == '\0') {
                        **data = '\0';
                        (*data)++;
                        (*len)--;
                        return ret;
                }
                (*data)++;
                (*len)--;
        }
        return ret;
}

int ICACHE_FLASH_ATTR itach_num(char **data, int *len)
{
        char *p = itach_token(data, len);
        if (*len > 2 && p[0] == '0' && (p[1] == 'b' || p[1] == 'B'))
                return strtol(p+2, NULL, 2);
        return strtol(p, NULL, 0);
}
