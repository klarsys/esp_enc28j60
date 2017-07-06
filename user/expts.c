#include <esp8266.h>
#include "expts.h"
#include "rest_utils.h"
#include "debug.h"
#include "gpio.h"

static int ntx = 2, tx_gpio[8] = {2, 2, 2, 2, 2, 2, 2, 2};
static uint32 tx_mask = 0, // All the pins that need to be SET to turn ON
				tx_inv_mask = 0; // All the pins that need to be CLEARED to turn ON
static int rx_gpio = 3;

typedef struct IrRxRepeat_ {
	char preamble[32];
	char *cmd; // Temp placeholder
	struct IrRxRepeat_ *next;
} IrRxRepeat;
static IrRxRepeat *irRepeat = NULL;
static void ICACHE_FLASH_ATTR repeatIR(char *cmd);

// #define DEBUG_IR_TX
enum {
	RPT_COUNT,
	RPT_START,
	RPT_END
};
typedef struct IrCode_ {
	struct IrCode_ *next;
	uint32 pinMask; // All pins that are to be pulsed for this code
	uint32 period; // Actually period/2 in Q16
	// These are indices into the seq[] table
	uint16 n; 		// seq count for first transmit
	uint16 alloc; 	// number allocated for seq[]
	sint16 repeat[3]; 	// count, start and end; if count is negative, repeat forever or until stop
	// State variables
	uint16 isRepeat; 	// Set when entering repeat mode
	uint16 cur; 	// Current index
	uint16 stop; 	// Flag to cause abort

	ETSTimer timer; // Timer for longer gaps. FIXME: worth making dynamic?

#ifdef DEBUG_IR_TX
	uint32 startTime;
	uint32 np;
#endif
	// Actual sequence
	uint16 seq[]; // flexible array member: https://gcc.gnu.org/onlinedocs/gcc/Zero-Length.html
} IrCode;

static bool irRxEnable = false, irTxEnable = true;
static uint32 latchedIntPins;


// Tx related globals
static int txNow = 0; // Index into txArray
static IrCode **txArray = NULL;  // txArray is NULL terminated
static ETSTimer txAbortTimer; // Needed when mutex is unavailable to abort
static irSendCb sendCb = NULL;
static void *sendCbArg = NULL;
static bool rxOldTrigger = false;

// Config variables read at startup (FIXME not done yet)
static uint32 maxBusyWait = 10000, /* us; timer is used beyond this */
	rxTimeout = 300, /* ms; any gap beyond this is an indication of a new code sequence start */
	nRecvCodes = 512 /* Number of codes to capture */;
static int maxRepeat = 20;

static IrCode ** ICACHE_FLASH_ATTR parseIRSend(uint32 pinMask, char **cmds)
{	int i, j, ncmds, n = 0, alloc;
	char *p;
	IrCode **pCode = NULL;

	// Count the number of commands
	for (ncmds = 0; cmds[ncmds]; ncmds++)
		;
	if (ncmds <= 0)
		goto err;
	DEBUG("parseIR ncmds=%d", ncmds);
	// Allocate
	if (!(pCode = ZALLOC((ncmds+1)*sizeof(IrCode *))))
		goto err;
	for (i = 0; cmds[i]; i++) {
		if (strstr(cmds[i], "delay,")) {
			if (!(pCode[i] = ZALLOC(sizeof(*pCode[i]) + sizeof(pCode[i]->seq[0]))))
				goto err;
			pCode[i]->n = 1; // Indicates this is a delay command
			pCode[i]->seq[0] = atoi(cmds[i]+os_strlen("delay,"));
			continue;
		}
		// COunt the number of commas
		n = 0;
		p = cmds[i];
		//while (*p && (p = strchr(p, ','))) {
		//	++p;
		//	++n;
		//}
		int inNum = 1;
		// 		"36000,2,1,32,32A32,32",
		while (*p) {
			if (*p == ',')
				++n;
			else if (*p >= 'A' && *p <= 'Z') {
				n += 2;
				inNum = 0;
			} else {
				if (!inNum)
					++n;
				inNum = 1;
			}
			++p;
		}
		alloc = n - 2;
		if (alloc&1)
			++alloc;
		DEBUG("command[%d]: n=%d alloc=%d", i, n, alloc);
		if (!(pCode[i] = ZALLOC(sizeof(*pCode[i]) + alloc*sizeof(pCode[i]->seq[0]))))
			goto err;
		pCode[i]->alloc = alloc;
		pCode[i]->pinMask = pinMask;
		// Format of the string is
		//	frequency,repeatN,repeatStart,ON,OFF,ON,OFF
		p = cmds[i];
		pCode[i]->period = atoi(p);
		DEBUG("command[%d]: freq=%d", i, pCode[i]->period);
		if (pCode[i]->period < 10000 || pCode[i]->period > 100000) {
			ERROR("frequency out of limits");
			goto err;
		}
		// we need to do 1e6/freq/2*65536
		// we try to preserve precision while sticking with integer arithmetic
		// we lose out on just 2 bits
		pCode[i]->period = ((1000000u*0x1000u)/(pCode[i]->period))<<3u;
		DEBUG("command[%d]: period=%d",i,  pCode[i]->period);
		pCode[i]->n = alloc;
		pCode[i]->alloc = alloc;
		p = strchr(p, ',') + 1;
		pCode[i]->repeat[0] = atoi(p) - 1;
		p = strchr(p, ',') + 1;
		pCode[i]->repeat[1] = atoi(p) - 1;
		pCode[i]->repeat[2] = alloc - pCode[i]->repeat[1];
		p = strchr(p, ',') + 1; // to start of seq

		int dict[27][2], ndx = 0;
		for (j = 0; j < n - 2; ) {
			if (*p >= 'A' && *p <= 'Z') {
				pCode[i]->seq[j++] = dict[*p - 'A'][0];
				pCode[i]->seq[j++] = dict[*p - 'A'][1];
				++p;
			} else if (*p >= '0' && *p <= '9') {
				pCode[i]->seq[j++] =
					dict[ndx>>1][ndx&1] = atoi(p);
				++ndx;
				if (ndx == 27*2)
					ndx = 27*2 - 1;
				while (*p >= '0' && *p <= '9')
					++p;
				if (*p == ',')
					++p;
			} else if (*p == 0 || *p == '\r' || *p == '\n') {
				break;
			} else if (*p == ',') {
				// Ignore extra commas
				++p;
			} else {
				ERROR("error parsing irsend >>%c<< %d", *p, *p);
				goto err;
			}
		}
		if (j != n - 2) {
			WARN("Allocated %d codes. Got only %d.  Extra commas?", n-2, j);
			pCode[i]->n =  j;
		}
	}
	return pCode;
err:
	ERROR("mem or other error in commands");
	if (pCode) {
		for (i = 0; pCode[i]; i++)
			FREE(pCode[i]);
		FREE(pCode);
	}
	return NULL;
}


#define ESP8266_REG(addr) (*((volatile uint32_t *)(0x60000000+(addr))))
#define GPOS   ESP8266_REG(0x304) //GPIO_OUT_SET WO
#define GPOC   ESP8266_REG(0x308) //GPIO_OUT_CLR WO
// CCOUNT reg runs at 80 MHz
//	1 tick == 12.5 ns
// See: http://bbs.espressif.com/viewtopic.php?t=200
#define US_TO_CCOUNT(x)  (((x)*80)>>16)
//#define READ_CCOUNT()  asm volatile ("rsr %0, ccount" : "=r"(r))
static inline unsigned get_ccount(void)
{
	unsigned r;
	asm volatile ("rsr %0, ccount" : "=r"(r));
	return r;
}
static void txOn(uint32 pinMask, int pulse, int n)
{
	uint32 next;
	int i = 0;
	uint32 swp, on_set = (pinMask&tx_mask), on_clr = (pinMask&tx_inv_mask);

	pulse = US_TO_CCOUNT(pulse);
	next  = get_ccount() + pulse;
	while (i < 2*n) {
		GPOS = on_set;
		GPOC = on_clr;
		if (get_ccount() > next) {
			next  += pulse;
			swp = on_set;
			on_set = on_clr;
			on_clr = swp;
			++i;
		}
	}
}

static int ICACHE_FLASH_ATTR checkFinished(IrCode *code)
{
	int n = (code->isRepeat?code->repeat[RPT_END]:code->n);
	int isEnd = (n <= code->cur);
	//INFO("checkFinish cur=%d n=%d end=%d", code->cur, n, isEnd);
#ifdef DEBUG_IR_TX
	if (!code->startTime)
		code->startTime = system_get_time();
#endif
	if (code->stop)
		goto finish;
	if (!isEnd)
		return 0; // Not finished
	if (/*!code->isRepeat && */code->repeat[RPT_COUNT])
		code->isRepeat = 1; // Enter repeat mode
	if (!code->isRepeat)
		goto finish; // Have reached end of non-repeat code
	if (code->repeat[RPT_COUNT] != 0) {
		if (code->repeat[RPT_COUNT] > 0)
			--code->repeat[RPT_COUNT];
		code->cur = code->repeat[RPT_START];
		return 0;  // Not finished; restart at repeat point
	}
	// Definitely finished or stopped by this point
finish:
#ifdef DEBUG_IR_TX
	{
		uint32 end = system_get_time();
		end = end - code->startTime;
		code->np = 2*((code->np*code->period)>>16);
		DEBUG("IR TX: Sending took %u us instead of %u (err=%u)", end, code->np, end-code->np);
	}
#endif
	INFO("IR TX: Finished");
	FREE(code);
	txArray[txNow++] = NULL;
	return 1;
}

static void ICACHE_FLASH_ATTR finishIRSend(void *err)
{
	if (txArray[txNow])
		FREE(txArray[txNow]);
	if (txArray)
		FREE(txArray);
	txArray = NULL;
	if (sendCb)
		sendCb(sendCbArg, err);
	sendCb = NULL;
}

static int ICACHE_FLASH_ATTR txCode(IrCode *code)
{
	uint32 gap;
	//DEBUG("Sending code %u\n", system_get_time());
	if (!code) {
		finishIRSend(NULL);
		return 2;
	}

	if (code->n == 1) { // Delay code
		IrCode *next = txArray[txNow+1];

		if (next) { // If there's a next command, we use its timer.
			txArray[txNow++] = NULL;
			setTimeout(&(next->timer), txCode, next, code->seq[0]);
			FREE(code); // Also means we can de-allocate this code
		} else { // If there's no next comand, we can't dealloc now.  We need that timer
			// NULL==code signals this kludge
			setTimeout(&(code->timer), txCode, NULL, code->seq[0]);
		}
		return 1;
	}
	while (!checkFinished(code))  {
		txOn(code->pinMask, code->period, code->seq[code->cur]);
		//gap = (code->seq[code->cur+1]*code->period*2)>>16; // FIXME gap needs to be adjusted by acc error
		gap = 2*((code->seq[code->cur+1]*(code->period>>16)) +
			((code->seq[code->cur+1]*(code->period&0xFFFF))>>16));
#ifdef DEBUG_IR_TX
		code->np += code->seq[code->cur] + code->seq[code->cur+1];
#endif
		code->cur += 2;
		if (gap > maxBusyWait) {
			os_timer_disarm(&(code->timer));
			os_timer_setfn(&(code->timer), (os_timer_func_t *)txCode, code);
			os_timer_arm_us(&(code->timer), gap, 0);
			return 1;
		} else if (gap)
			os_delay_us(gap); // Busy wait for smaller times
	}
	if (txArray[txNow])
		txCode(txArray[txNow]);
	else {
		finishIRSend(NULL);
	}
	return 2;
}


int ICACHE_FLASH_ATTR irSend(char *mod, char **cmds, irSendCb cb, void *arg)
{
	uint32 i, pinMask = 0;

	if (txArray || !irTxEnable) {
		WARN("irSend busy");
		return -1;
	}
	if (!(mod = os_strchr(mod, ':'))) {
		ERROR("Bad module specification");
		pinMask = 1;
	} else {
		// Module specification is 1:N
		// If N is less than 256, it refers to a single transmitter number (iTach convention)
		//		- If n is greater than available transmitter, transmitter 0 is used
		//			(this is for backwards compatibility)
		// If N is  greater than 256, it is interpreted as a mask of all requested transmitters (after a rshft by 8)
		++mod; // skip over ':'
		int rem = os_strlen(mod), mno = itach_num(&mod, &rem);
		if (mno > 256)
			mno >>= 8; 
		else {
			if (mno >= ntx)
				mno = 0;
			mno = (1u << mno);
		}
		for (i = 0; i < ntx; i++)
			if (mno&(1u<<i))
				pinMask |= (1u << tx_gpio[i]);
		DEBUG("mno=%d ntx=%d tx_gpio[0]=%d pinMask=%x", mno, ntx, tx_gpio[0], pinMask);
		DEBUG("tx_mask=%x tx_inv_mask=%x", tx_mask, tx_inv_mask);
	}

	if (!(txArray = parseIRSend(pinMask, cmds))) {
		ERROR("bad irSend format");
		return -1;
	}
	sendCb = cb;
	sendCbArg = arg;
	//printCode(txArray);
	txNow = 0;
	os_timer_disarm(&(txArray[0]->timer));
	os_timer_setfn(&(txArray[0]->timer), (os_timer_func_t *)txCode, txArray[0]);
	os_timer_arm(&(txArray[0]->timer), 1, 0);
	return 1;

	//return txCode(txArray[0]);
}
void ICACHE_FLASH_ATTR irInit(void)
{
}

int exptGetUUID(HttpdConnData *connData)
{
	os_printf("UUID\r\n");
	HTTPD_PRINTF("CI%08x\n", system_get_chip_id());
	return HTTPD_CGI_DONE;
}
static u8 gpio = 0;
//#define ESP8266_REG(addr) *((volatile uint32_t *)(0x60000000+(addr)))
#define GP16O  ESP8266_REG(0x768)
//#define GPOS   ESP8266_REG(0x304) //GPIO_OUT_SET WO
//#define GPOC   ESP8266_REG(0x308) //GPIO_OUT_CLR WO

int exptSetGPIO(HttpdConnData *connData)
{
	int i;
	char *p = strrchr(connData->url, '/')+1;
	gpio = strtol(p, NULL, 0);
	os_printf("Write URL %s ==> 0x%02x\r\n", connData->url, gpio);
	#if 1
	for (i = 7; i >= 0; i--) {
		
		// DS -- GPIO16
		if (gpio&(1<<i))
			GP16O |= 1;
		else
			GP16O &= ~1;
	//asm volatile ("nop");
	//
	//asm volatile ("nop");
		// SHCP -- GPIO0
		GPOS = 1;
		GPOC = 1; 
	}
	GP16O &= ~1;
	// STCP -- GPIO2
	GPOS = (1 << 2);
	GPOC = (1 << 2);
	#else
	gpioWrite(2, 0);
	for (i = 7; i >= 0; i--) {
		gpioWrite(0, 0);
		gpioWrite(16, gpio&(1<<i));
		gpioWrite(0, 1);
	}
	gpioWrite(2, 1);
	#endif
	HTTPD_PRINTF("0x%02x\n", gpio);
	return HTTPD_CGI_DONE;
}
int exptGetGPIO(HttpdConnData *connData)
{
	HTTPD_PRINTF("0x%02x\n", gpio);
	return HTTPD_CGI_DONE;
}
int exptBlast(HttpdConnData *connData)
{
	return HTTPD_CGI_DONE;
}


void ICACHE_FLASH_ATTR exptInit(void)
{
	gpioPinConfig(0, GPIO_OUTPUT);
	gpioPinConfig(2, GPIO_OUTPUT);
	gpioPinConfig(16, GPIO_OUTPUT);
	gpioWrite(0, 0);
	gpioWrite(2, 0);
	gpioWrite(16, 0);
}
