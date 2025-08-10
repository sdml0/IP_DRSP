#include "unix_time.h"

static const uint16_t week_day[] = {0xCDCF, 0xD2C2, 0xD0D1, 0xD2D7, 0xD2CF, 0xC1D1, 0xD1C2};
static const uint32_t months[] = {0xC2CDDF2D, 0xC2C5D42D,0xD0C0CC2D, 0xD0CFC02D, 0xC9C0CC2D, 0xCDDEC82D, 0xCBDEC82D, 0xC3C2C02D, 0xCDC5D12D, 0xD2CACE2D, 0xDFCECD2D, 0xCAC5C42D};
#define SEC_A_DAY 86400

uint32_t date2sec(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec)
{
	uint8_t a;
	uint32_t time;

	a = (14 - month)/12;
	year += 4800 - a;
	month += 12*a - 3;
	time = day + (153*month + 2)/5 + 365*year + year/4 - year/100 + year/400 - 32045 - 2440588;
	time *= SEC_A_DAY;
	time += sec + min*60 + hour*3600;
	
	return time;
}

void sec2date(uint32_t sec, char* str)
{
	uint32_t a;
	uint8_t b, c, d;

	a = (((sec + 43200)/(86400/2)) + (2440587*2) + 1) >> 1;
	*(uint16_t*)str = week_day[a % 7];
	str[2] = ' ';
	a += 32044;
	b = (4*a + 3)/146097;
	a = a - ((146097*b)>>2);
	c = (4*a + 3)/1461;
	a = a - ((1461*c)>>2);
	d = (5*a + 2)/153;
	
	a = a - (153*d + 2)/5 + 1;			// day
	str[3] = a/10;
	a = a - 10*str[3];
	str[4] = a;
	*(uint32_t*)(str + 5) = months[d + 2 - 12*(d/10)];
	a = 100*b + c - 4800 + (d/10);	// year
	str[9] = '-';
	str[10] = a/1000;
	a = a - str[10]*1000;
	str[11] = a/100;
	a = a - str[11]*100;
	str[12] = a/10;
	a = a - str[12]*10;
	str[13] = a;
	str[14] = ' ';
	sec = sec % SEC_A_DAY;
	b = sec/3600;							// hour
	str[15] = b/10;
	c = b - 10*str[15];
	str[16] = c;
	str[17] = ':';
	sec = sec - 3600*b;					// min + sec
	str[18] = sec/600;
	sec = sec - 600*str[18];
	str[19] = sec/60;
	str[20] = ':';
	sec = sec - 60*str[19];				// sec
	str[21] = sec/10;
	sec = sec - 10*str[21];
	str[22] = sec;
	*(uint16_t*)(str + 3) += 0x3030;
	*(uint32_t*)(str + 10) += 0x30303030;
	*(uint16_t*)(str + 15) += 0x3030;
	*(uint16_t*)(str + 18) += 0x3030;
	*(uint16_t*)(str + 21) += 0x3030;
}

void RTC_Init(void)
{
	//разрешить тактирование модулей управления питанием и управлением резервной областью
	RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;

	//если часы выключены - инициализировать их
	if (!(RCC->BDCR & RCC_BDCR_RTCEN)) {
		//разрешить доступ к области резервных данных
		BB_REG(&PWR->CR)[8] = 1;
		//выполнить сброс области резервных данных
		RCC->BDCR |=  RCC_BDCR_BDRST;
		RCC->BDCR &= ~RCC_BDCR_BDRST;
	 	//выбрать источником тактовых импульсов LSI(40kHz) и подать тактирование
		RCC->BDCR |=  RCC_BDCR_RTCEN | RCC_BDCR_RTCSEL_LSI;
		BB_REG(&PWR->CR)[8] = 0;
	}
	RTC->CRL |= RTC_CRL_CNF;		//Allow edit
	RTC->PRLL = 39971;				//регистр деления на 40000
	RTC->CRL &= ~RTC_CRL_CNF;
	 
	//установить бит разрешения работы и дождаться установки бита готовности
	RCC->CSR |= RCC_CSR_LSION;
	while (!(RCC->CSR & RCC_CSR_LSIRDY));
	 
	RTC->CRL &= (uint16_t)~RTC_CRL_RSF;		//Registers synchronize
	while(!(RTC->CRL & RTC_CRL_RSF));
}

void RTC_Set(uint32_t sec)                                                    //Записать новое значение счетчика
{
 //RCC->APB1ENR |= RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN;  							//включить тактирование PWR и Backup
 BB_REG(&PWR->CR)[8] = 1;                                                     //разрешить доступ к Backup области
 while (!(RTC->CRL & RTC_CRL_RTOFF));                                         //проверить закончены ли изменения регистров RTC
 RTC->CRL |= RTC_CRL_CNF;                                                     //Разрешить Запись в регистры RTC
 RTC->CNTH = sec>>16;                                                         //записать новое значение счетного регистра
 RTC->CNTL = sec;
 RTC->CRL &= ~RTC_CRL_CNF;                                                    //Запретить запись в регистры RTC
 while (!(RTC->CRL & RTC_CRL_RTOFF));                                         //Дождаться окончания записи
 BB_REG(&PWR->CR)[8] = 0;                                                     //запретить доступ к Backup области
}

/*void timer2cal (uint32_t timer, unix_cal * unix_time)
{
	unsigned long a;
	char b;
	char c;
	char d;
	unsigned long time;

	time = timer % SEC_A_DAY;
	a = ((timer + 43200)/(86400>>1)) + (2440587<<1) + 1;
	a >>= 1;
	unix_time->wday = a % 7;
	a += 32044;
	b = (4*a + 3)/146097;
	a = a - (146097*b)/4;
	c = (4*a + 3)/1461;
	a = a - (1461*c)/4;
	d = (5*a + 2)/153;
	unix_time->mday = a - (153*d + 2)/5 + 1;
	unix_time->mon = d + 3 - 12*(d/10);
	unix_time->year = 100*b + c - 4800+(d/10);
	unix_time->hour = time/3600;
	unix_time->min = (time % 3600)/60;
	unix_time->sec = (time % 3600) % 60;
}

void sec2Fname (uint32_t sec, char* fn)
{
	fn[6] = sec % 10 + '0';
	sec = sec/10;
	fn[5] = sec % 6 + '0';
	sec = sec/6;
	fn[4] = sec % 10 + '0';
	sec = sec/10;
	fn[3] = sec % 6 + '0';
	sec = (sec/6) % 24;
	fn[2] = sec % 10 + '0';
	fn[1] = sec/10 + '0';
}*/

uint8_t int2str(uint32_t n, char* str)
{
	uint32_t m;
	uint8_t d = 0;
	
	if (n == 0) {
		str[0] = '0';
		return 1;
	}
	while(n) {
		m = n;
		n /= 10;
		str[d] = m - 10*n + '0';
		d++;
	}
	for (m = 0; m < d/2; m++) {
		str[d] = str[m];
		str[m] = str[d - m - 1];
		str[d - m - 1] = str[d];
	}

	return d;
}

