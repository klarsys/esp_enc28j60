#include <esp8266.h>
#include "debug.h"

static ETSTimer dbgReportTimer;
static int dbgLvl = DLVL_DEBUG, dbgHeap = 0, dbgSerial = 1;
static char *dbgLn = NULL;;
static int dbgLnLen = 1024;

int ICACHE_FLASH_ATTR debugSetLevel(int level)
{
	if (level > DLVL_DEBUG)
		level = DLVL_DEBUG;
	if (level < DLVL_ERROR)
		level = DLVL_ERROR;
	dbgLvl = level;
	return dbgLvl;
}

static void ICACHE_FLASH_ATTR dbgReportTimerCb()
{
	char *flashMap[] = {
		[FLASH_SIZE_4M_MAP_256_256] "4M_MAP_256_256",
		[FLASH_SIZE_2M] "2M",
		[FLASH_SIZE_8M_MAP_512_512] "8M_MAP_512_512",
		[FLASH_SIZE_16M_MAP_512_512] "16M_MAP_512_512",
		[FLASH_SIZE_32M_MAP_512_512] "32M_MAP_512_512",
		[FLASH_SIZE_16M_MAP_1024_1024] "16M_MAP_1024_1024",
		[FLASH_SIZE_32M_MAP_1024_1024] "32M_MAP_1024_1024"
	};
	INFO("--------------------------------------------");
    INFO("SDK: v%s", system_get_sdk_version());
    INFO("Free Heap: %d", system_get_free_heap_size());
    INFO("CPU Frequency: %d MHz", system_get_cpu_freq());
    INFO("System Chip ID: 0x%x", system_get_chip_id());
    INFO("SPI Flash ID: 0x%x", spi_flash_get_id());
    INFO("SPI Flash Size: %d", (1 << ((spi_flash_get_id() >> 16) & 0xff)));
    INFO("Flash Size Map: %d=>%s", system_get_flash_size_map(), flashMap[system_get_flash_size_map()]);
	struct softap_config ap_config;
	wifi_softap_get_config(&ap_config);
	INFO("AP SSID : %s", (char *)ap_config.ssid);

	struct ip_info ipconfig;
	wifi_get_ip_info(STATION_IF, &ipconfig);
	if (wifi_station_get_connect_status() == STATION_GOT_IP && ipconfig.ip.addr != 0) {
		INFO("ip: %d.%d.%d.%d, mask: %d.%d.%d.%d, gw: %d.%d.%d.%d",
			IP2STR(&ipconfig.ip), IP2STR(&ipconfig.netmask), IP2STR(&ipconfig.gw));
	} else {
		INFO("network status: %d", wifi_station_get_connect_status());
	}
	INFO("--------------------------------------------");

}


static const char dbgLvlTok[] = {
	[DLVL_ERROR] 'E',
	[DLVL_WARN] 'W',
	[DLVL_INFO] 'I',
	[DLVL_DEBUG] 'D',
};

void ICACHE_FLASH_ATTR debugPrint(int level, const char *file, int line, const char *fmt, ...)
{
	if (level > dbgLvl || !dbgLn)
		return;
	int i, tlen, hlen;
	va_list ap;

	if ((i = os_strlen(file)) > 10)
		file = file + i - 10;
	hlen = rpl_snprintf(dbgLn, dbgLnLen-1, "%10u %c %10s:%-4d ", system_get_time(),
				dbgLvlTok[level], file, line);

	va_start(ap, fmt);
	tlen = rpl_vsnprintf(dbgLn+hlen, dbgLnLen-hlen-3, fmt, ap);
	va_end(ap);

	if (tlen < 0)
		tlen = rpl_snprintf(dbgLn+hlen, dbgLnLen-hlen, "sprintf error!");
	dbgLn[hlen+tlen+0] = '\r';
	dbgLn[hlen+tlen+1] = '\n';
	dbgLn[hlen+tlen+2] = 0;

	os_printf(dbgLn);
}

void ICACHE_FLASH_ATTR debugInit(void)
{
	if (!(dbgLn = MALLOC(dbgLnLen)))
		goto err;
	INFO("Debug initialized. lnLen=%d dbgHeap=%d", dbgLnLen, dbgHeap);
err:
	return;
}

typedef struct MREC_ {
	char file[64];
	int nalloc;
	int nacalls;
	int nfcalls;
	int max;
	int min;
	struct MREC_ *next;
} MREC;
static MREC *mrecs = NULL;

static void ICACHE_FLASH_ATTR mrec_alloc(const char *file, int n)
{
	MREC *p = mrecs, *q;
	q = p;
	do {
		if (!q) {
			if (!(q = os_malloc(sizeof(MREC)))) {
				ERROR("Running out of mem");
				return;
			}
			SAFE_STRCPY(q->file, file);
			q->nalloc = q->max = q->min = n;
			q->nacalls = 1;
			q->nfcalls = 0;
			q->next = NULL;
			if (p)
				p->next = q;
			else
				mrecs = q;
			return;
		} else if (!os_strncmp(q->file, file, sizeof(q->file)-1)) {
			q->nalloc += n;
			++(q->nacalls);
			if (n > q->max)
				q->max = n;
			if (n < q->min)
				q->min = n;
			return;
		} else {
			p = q;
			q = q->next;
		}
	} while (p);
}
static void ICACHE_FLASH_ATTR mrec_free(const char *file, int n)
{
	MREC *p = mrecs;
	while (p) {
		if (!os_strncmp(p->file, file, sizeof(p->file)-1)) {
			p->nalloc -= n;
			++(p->nfcalls);
			return;
		}
		p = p->next;
	}
	ERROR("Unable to locate allocation for %s[%s]", file, n);
}
void ICACHE_FLASH_ATTR debugMemInfo(char *s, int len)
{
	MREC *p = mrecs;
	int n, r = (os_random()&0xFFFF);
	n = os_sprintf(s, "meminfo,%d\n", r);
	while (p && n < len - 32) {
		n += os_sprintf(s+n, "mem,%s,%d,%d,%d,%d,%d\n", p->file, p->nalloc, p->nacalls, p->nfcalls, p->min, p->max);
		p = p->next;
	}
	os_sprintf(s+n, "endmeminfo,%d\n", r);
}
typedef struct FENCE_ {
	uint32 magic1;
	const char *file;
	uint16 line;
	uint16 size;
	uint32 magic2;
} FENCE;
const uint32 magic[] = {
	0xdeadbeef,
	0xba5eba11,
	0xbedabb1e,
	0xbe5077ed,
	0xb0a710ad,
	0xb01dface,
	0xcab005e,
	0xca11ab1e,
	0xca55e77e,
	0xdeadbea7,
	0xdefec8,
	0xf01dab1e,
	0xf005ba11,
	0x0ddba11,
	0x5ca1ab1e,
	0x7e1eca57
};
void * ICACHE_FLASH_ATTR debugHeap(const char *file, int line, int op, int sz)
{
	if (!dbgHeap) {
		void *ret;
		if (op == DHP_FREE) {
			os_free((void *)sz);
			return NULL;
		} else {
			ret = (op == DHP_ZALLOC)?(os_zalloc(sz)):(os_malloc(sz));
			if (!ret)
				DEBUG("[%s:%d] Mem alloc failed for %d bytes", file, line, sz);
			return ret;
		}
	}
	FENCE *p;
	uint32 *ret, asz;
	#define MULT4(n) (((n)+3u)&(~3u))
	switch (op) {
	case DHP_ALLOC:
	case DHP_ZALLOC:
		asz = MULT4(sz) + 2*sizeof(FENCE);
		DEBUG("Allocating %d (%d) bytes (%s:%d). Rem=%d", sz, asz, file, line, system_get_free_heap_size());
		if (!(p = os_zalloc(asz))) {
			ERROR("Failed to alloc %d (%d) bytes", asz, sz);
			return NULL;
		}
		mrec_alloc(file, sz);
		p->magic1 = magic[line&0xF];
		p->file = file;
		p->line = line;
		p->size = sz;
		p->magic2 = magic[sz&0xF];
		ret = (uint32 *)&p[1];
		os_memcpy(ret + (MULT4(sz)>>2), p, sizeof(FENCE));
		DEBUG("Allocated %d (%d) bytes (%s:%d). Rem=%d", sz, asz, file, line, system_get_free_heap_size());
		return ret;
	case DHP_FREE:
		ret = (void *)(sz);
		p = (FENCE *)(ret - (sizeof(FENCE)>>2));
		if (p->magic1 != magic[p->line&0xF] || p->magic2 != magic[p->size&0xF])
			ERROR("Top fence fail (%s:%d)", file, line);
		else if (os_memcmp(p, ret + (MULT4(p->size)>>2), sizeof(FENCE)))
			ERROR("Bottom fence fail(%s:%d)", file, line);
		else {
			DEBUG("Free call at %s:%d from %s:%d for %d bytes. Rem=%d", file, line, p->file, p->line, p->size, system_get_free_heap_size());
		}
		os_free(p);
		mrec_free(p->file, p->size);
		DEBUG("AFter free rem=%d", system_get_free_heap_size());
		return NULL;
	}
	return NULL;
}
