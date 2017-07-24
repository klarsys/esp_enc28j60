#ifndef GPIO_H_
#define GPIO_H_

enum {
  GPIO_INPUT,
  GPIO_OUTPUT
 };
enum {
  GPIO_DISABLED,
  GPIO_RISING,
  GPIO_FALLING,
  GPIO_CHANGE
};
typedef void (*GpioCb)(void);

void ICACHE_FLASH_ATTR gpioInit(void);
int ICACHE_FLASH_ATTR gpioRead(uint8 pin);
void ICACHE_FLASH_ATTR gpioWrite(uint8 pin, uint8 val);
void ICACHE_FLASH_ATTR gpioPinConfig(uint8 pin, uint8 mode);
void ICACHE_FLASH_ATTR gpioSetState(int cc, int val);
int ICACHE_FLASH_ATTR gpioGetState(int cc);
void ICACHE_FLASH_ATTR gpioIntrEn(uint8 pin, int mode);

#endif

