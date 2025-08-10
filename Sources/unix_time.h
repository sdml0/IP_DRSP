#include "stm32f10x.h"

uint32_t date2sec (uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec);
void sec2date(uint32_t sec, char* str);
void RTC_Init(void);
void RTC_Set(uint32_t sec);
uint8_t int2str(uint32_t n, char* str);
