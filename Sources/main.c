#include "stm32f10x.h"
#include "diskio.h"
#include "unix_time.h"
#include "tcpip.h"

//---------------TODO-------------------
// Stop on Eth or SD error ?
// Tamper
// Try sold pwr led on pin, led current ?
// Separate SPI or all on SPI1 ?
// Watchdog	?
// Status by bits	(94567)
// Try lower CPU speed
// Redo BB somwhere
// HW reset
// Error blink on C13 ?
// Program for pack fw on PC
// Pass sending with encryption
// Pause button
// Is get_data & ->data always the same ?
// __main & DMA_Irq @address ?
// Check diff from current fw in bldr or check min size
// Write in SD up to the end ?
// Rec length limit ?
// LED Pulse
// Check SD structure on PC
// separate pages update ?
// Aligned records - less net_buf ?
// Back to date in wif
// Save index at the end of writing ?
// Adjust mid-point
// 8kb send ? 4kb send ? ..

// TIM2 for diskio(maybe lan) TIM3 for ADC
#define iBUF_SIZE 508	//uint16_t					// also in lan.c
#define AVG 0x800
#define CRIT_THR 0x130
#define SEQ_LEN 16
#define CH_NUM 8
#define Buf ((uint32_t*)net_buf)
//#define _FRAME ((eth_frame_t*)net_buf)
//#define MAC_ADDR1	0x10224100
//#define MAC_ADDR2 0xE2E6
#define IP_ADDR	inet_addr(192,168,1,222)

static const uint32_t oki_steps [] =	// ~12-bit precision
{
			 16,   17,   19,   21,   23,   25,   28,
			 31,   34,   37,   41,   45,   50,   55,
			 60,   66,   73,   80,   88,   97,  107,
			118,  130,  143,  157,  173,  190,  209,
			230,  253,  279,  307,  337,  371,  408,
			449,  494,  544,  598,  658,  724,  796,
			876,  963, 1060, 1166, 1282, 1411, 1552
};

static const int32_t step_changes [] = { -1, -1, -1, -1, 2, 4, 6, 8 };

typedef struct {
	uint32_t	time;
	uint32_t	sector;
	//uint16_t length;
} RecordIndex;

extern uint32_t ip_addr;
extern uint8_t net_buf[];
extern const uint8_t def_names[141];
extern uint8_t ch_names[];
uint8_t mac_addr[6];

uint8_t dirInd, active, wr_flag;
volatile uint16_t oInd;
uint16_t fullWritten;
uint32_t curSector, indSector, indexAreaSize, cardSize1, Status, SeQ;

uint8_t ch_names[256];
RecordIndex indBuf[64 + CH_NUM - 1];
uint16_t iData[8*CH_NUM];
uint8_t oData[CH_NUM][1024];
int16_t crit[CH_NUM], crit_thr[CH_NUM];
uint8_t critCond[CH_NUM];
int16_t ind[CH_NUM], last[CH_NUM];


void Init_ADC1()
{
	//Timer init
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	TIM3->PSC = 149;										// 72MHz -> 480kHz
	TIM3->ARR = 59;										// 480kHz -> 8kHz
	TIM3->CR1 = TIM_CR1_URS;							// Only overflow generates UEV -> TRGO
	TIM3->CR2 = TIM_CR2_MMS_1;							// Enable TRGO
	TIM3->EGR = TIM_EGR_UG;								// Reload TIM registers

	//ADC Init
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
	GPIOA->CRH = 0x88888888;
	GPIOA->CRL = 0; 			        													// Configure PA0 - PA7 as ADC1 input
	RCC->APB2ENR &= ~RCC_APB2ENR_IOPAEN;											// GPIOA OFF for ADC
	RCC->CFGR    |= 3<<14;         													// ADC clk = PCLK2 / 8
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;												// Enable ADC1 peripheral clock
	ADC1->CR1 = ((CH_NUM - 1)<<13) | ADC_CR1_DISCEN;							// Channel count = CH_NUM, disc mode
	ADC1->SQR1 = (CH_NUM - 1)<<20;               								// Regular chn. Sequence length = CH_NUM
	ADC1->SQR2 = 6 | (7 << 5);															// 6->7
	ADC1->SQR3 = (1 << 5) | (2 << 10) | (3 << 15) |
						(4 << 20) | (5 << 25);											// 0->1->2->3->4->5
	ADC1->SMPR2 = 2 | (2<<3) | (2<<6) | (2<<9) | (2<<12) |
						(2<<15) | (2<<18) | (2<<21);									// sample time 1 -> 7,5 + 12,5 cyc.
	ADC1->CR2 = (4<<17) | (1<<20) | (1<<8);  										// TIM3 TRGO  as trigger, External trigger, DMA
	ADC1->CR2    |= (1<<0); 	  														// ADC enable
	ADC1->CR2    |=  1<<3;            												// Initialize calibration registers
	while (ADC1->CR2 & (1<<3));       												// Wait for init to finish
	ADC1->CR2    |=  1<<2;	           												// Start calibration
	while (ADC1->CR2 & (1<<2));       												// Wait for calibration to finish
	
	//DMA_Init For ADC
	//RCC->AHBENR |= RCC_AHBENR_DMA1EN;
	NVIC_EnableIRQ(DMA1_Channel1_IRQn);
	DMA1_Channel1->CPAR = (uint32_t)&(ADC1->DR);
	DMA1_Channel1->CMAR = (uint32_t)iData;
	DMA1_Channel1->CNDTR = 8*CH_NUM;
	//16to16, Memory increment, Circular, Transfer complete interrupt, DMA en
	DMA1_Channel1->CCR = DMA_CCR1_EN | DMA_CCR1_CIRC | DMA_CCR1_TCIE | DMA_CCR1_MSIZE_0 | DMA_CCR1_PSIZE_0 | DMA_CCR1_MINC;// | (2<<12);

}

void Init_ADC2()
{
	//Timer init
	RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
	TIM3->PSC = 149;										// 72MHz -> 480kHz
	TIM3->ARR = 59;										// 480kHz -> 8kHz
	TIM3->CR1 = TIM_CR1_URS;							// Only overflow generates UEV -> TRGO
	TIM3->CR2 = TIM_CR2_MMS_1;							// Enable TRGO
	TIM3->EGR = TIM_EGR_UG;								// Reload TIM registers

	//ADC Init			Try on single ADC - less noise?
	RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
	GPIOA->CRH = 0x88888888;
	GPIOA->CRL = 0; 			        													// Configure PA0 - PA7 as ADC1 input
	RCC->APB2ENR &= ~RCC_APB2ENR_IOPAEN;											// GPIOA OFF?
	RCC->CFGR    |= ( 3<<14);         												// ADC clk = PCLK2 / 8
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_ADC2EN;					// Enable ADC1 peripheral clock
	ADC1->CR1 = ((CH_NUM/2 - 1)<<13) | (6 << 16) | ADC_CR1_DISCEN;			// Channel count = CH_NUM/2, Dual regular, disc mode
	ADC1->SQR1 = (CH_NUM/2 - 1)<<20;               								// Regular chn. Sequence length = CH_NUM/2
	ADC1->SQR3 = (2 << 5) | (4 << 10) | (6 << 15) | (8 << 15);				// 0->2->4->6->8
	ADC1->SMPR2 = 2 | (2<<3) | (2<<6) | (2<<9);									// sample time 1 -> 7,5 + 12,5 cyc.
	ADC1->CR2 = (4<<17) | (1<<20) | (1<<8);  										// TIM3 TRGO  as trigger, External trigger, DMA
	ADC1->CR2    |= (1<<0); 	  														// ADC enable
	ADC1->CR2    |=  1<<3;            												// Initialize calibration registers
	while (ADC1->CR2 & (1<<3));       												// Wait for init to finish
	ADC1->CR2    |=  1<<2;	           												// Start calibration
	while (ADC1->CR2 & (1<<2));       												// Wait for calibration to finish
	
	ADC2->CR1 = ((CH_NUM/2 - 1)<<13) | ADC_CR1_DISCEN;							// Channel count = CH_NUM/2, disc mode
	ADC2->SQR1 = (CH_NUM/2 - 1)<<20;               								// Regular chn. Sequence length = CH_NUM/2
	ADC2->SQR3 = 1 | (3 << 5) | (5 << 10) | (7 << 15) | (9 << 15);			// 1->3->5->7->9
	ADC2->SMPR2 = 2 | (2<<3) | (2<<6) | (2<<9);									// sample time 1 -> 7,5 + 12,5 cyc.
	ADC2->CR2 = (4<<17) | (1<<20) | (1<<8);  										// TIM3 TRGO  as trigger, External trigger, DMA ? for 2nd ?
	ADC2->CR2    |= (1<<0); 	  														// ADC enable
	ADC2->CR2    |=  1<<3;            												// Initialize calibration registers
	while (ADC2->CR2 & (1<<3));       												// Wait for init to finish
	ADC2->CR2    |=  1<<2;  	         											// Start calibration
	while (ADC2->CR2 & (1<<2));       												// Wait for calibration to finish

	//DMA_Init For ADC
	//RCC->AHBENR |= RCC_AHBENR_DMA1EN;
	NVIC_EnableIRQ(DMA1_Channel1_IRQn);
	DMA1_Channel1->CPAR = (uint32_t)&(ADC1->DR);
	DMA1_Channel1->CMAR = (uint32_t)iData;
	DMA1_Channel1->CNDTR = 8*CH_NUM/2;
	//32to32, Memory increment, Circular, Transfer complete interrupt, DMA en
	DMA1_Channel1->CCR = DMA_CCR1_EN | DMA_CCR1_CIRC | DMA_CCR1_TCIE | DMA_CCR1_MSIZE_1 | DMA_CCR1_PSIZE_1 | DMA_CCR1_MINC;// | (2<<12);

}

void write_state(uint16_t correct)
{
	uint8_t s;
	
	Buf[0] = 0x43455230;
	Buf[1] = 0x43455231;
	Buf[2] = correct;
	Buf[3] = 0x43455232;
	Buf[4] = dirInd;
	Buf[5] = curSector;
	Buf[6] = fullWritten;
	Buf[7] = indSector;
	Buf[8] = indexAreaSize;
	Buf[9] = cardSize1;
	//Buf[10] = ip_addr;
	//Buf[11] = *(uint32_t*)mac_addr;
	//Buf[12] = *(uint16_t*)(mac_addr + 4);
	for (s = 0; s < 8; s++) Buf[13 + s] = crit_thr[s];
	mem_cpy(net_buf + 21*4, ch_names, ch_names[0] + 1);
}

void check_record_state() 
{
	int32_t t1, t2;
	uint8_t num;
	
	if (curSector  > indexAreaSize + 1) {
		t1 = curSector - 1;
		num = 8;
	}
	else {
		t1 = cardSize1;
		num = 16;		
	}
	while (num) {
		read_sector(net_buf, t1);
		t2 = *(uint32_t*)(net_buf + 508);
		t2 = curSector - t2;
		if (t2 < 0) t2 += cardSize1 - indexAreaSize;
		if (t2 > 24) {
			*(uint32_t*)(net_buf + 508) = 0;
			write_sector(net_buf, t1);
		}
		if (t1  > indexAreaSize + 1) t1--;
		else {
			t1 = cardSize1;
			num +=8;		
		}
	num--;
	}
}

uint8_t oki_encode(int16_t s, uint8_t ch)
{
	int16_t dd, t;
	uint8_t code = 0;

	s = s - last[ch];
	t = oki_steps[ind[ch]];				// Try remove t
	dd = t >> 3;
	if (s < 0) {	
		code = 8;
		s = -s;
	}
	if (s >= t)	{
		code |= 4;
		s = s - t;
		dd  += t;
	}
	t >>= 1;
	if (s >= t)	{
		code |= 2;
		s = s - t;
		dd  += t;
	}
	t >>= 1;
	if (s >= t) {
		code |= 1;
		dd  += t;
	}
	// Update encoder state
	if (code & 8) {
		last[ch] = last[ch] - dd;
		if (last[ch] < 0) last[ch] = 0;			// Try remove this here and there
	}
	else {		
		last[ch] = last[ch] + dd;
		if (last[ch] > 0xFFF) last[ch] = 0xFFF;		
	}
	ind[ch] += step_changes[code & 7];
	if (ind[ch] < 0) ind[ch] = 0;
	else if (ind[ch] > 48) ind[ch] = 48;

	return code;
}


void DMA1_Channel1_IRQHandler(void)			// __@address
{
	uint8_t	ch, t, i;
	
	//if (DMA1->ISR & DMA_ISR_TCIF1) {								// Is it needs ?
									//GPIOB->BSRR = 1<<05;					// for debug
		DMA1->IFCR = DMA_ISR_TCIF1; //Clear flag
		
		for (i = 0; i < 4; i++)
			for (ch = 0; ch < CH_NUM; ch++) {
				t = oki_encode(iData[CH_NUM*2*i + ch], ch);
				t = (t << 4) | oki_encode(iData[CH_NUM*(2*i + 1) + ch], ch);
				oData[ch][oInd + i] = t;
				crit[ch] += (((t>>4)^t)>>3)&1;
			}
		oInd += 4;
			
		if (oInd == 508 || oInd == 1020) {
			oInd = (oInd + 4) & 1023;
			if (wr_flag) {
				BB_RAM(&Status)[9] = 1;
				oInd = (oInd + 512) & 1023;
				wr_flag++;
				if (wr_flag > 20) {								// Hang 3sec
					GPIOB->BSRR = 1<<8;							// off per
					BB_REG(&TIM3->CR1)[0] = 0;					// off adc
					delay(10000);									// 5 sec					
					SCB->AIRCR = 0x05FA0004;					// reset
				}
				return;
			}
			for (ch = 0; ch < CH_NUM; ch++) {
				if (crit[ch] < crit_thr[ch]) {
					if (critCond[ch] == 0) {
						indBuf[dirInd].time = RTC->CNTH<<16 | RTC->CNTL;
						SeQ &= 0xFFFFFF >> (24 - 3*active);
						SeQ |= ch << (3*active);
						indBuf[dirInd].sector = (ch << 29) | (curSector + active);
						active++;
						dirInd++;
						GPIOB->BSRR = 1<<9;	// LED on
					}
					critCond[ch] = SEQ_LEN - 1;
				}
				else if (critCond[ch]) critCond[ch]--;
				else {
					last[ch] = AVG;
					ind[ch] = 0;
				}
			crit[ch] = 0;
			}
		if (active) wr_flag = 1;
		}
									//GPIOB->BSRR = 1<<(05+16);					// for debug	
	//}
}


void save_oData()
{
	uint8_t ch;
	//uint16_t surp = ~oInd & 512;
	uint32_t i, t, t2;
	
	//if (active) {
		i = SeQ;
		SeQ |= (active<<28);
		t = active;
		t2 = curSector;
		if (curSector + active + CH_NUM > cardSize1) {
			curSector = indexAreaSize + 1 - active;			// +active later
			fullWritten++;
			if(!fullWritten) fullWritten++;
		}
		for (ch = 0; ch < t; ch++) {
			if (critCond[(SeQ>>(3*ch))&7] != 0) 
					*((uint32_t*)&(oData[(SeQ>>(3*ch))&7][(~oInd & 512) + iBUF_SIZE])) = curSector + active + ch;
			else {
				*((uint32_t*)&(oData[(SeQ>>(3*ch))&7][(~oInd & 512) + iBUF_SIZE])) = 0;
				i = ((i>>3) & (0xFFFFFF<<(3*(ch + active - t)))) | (i & (0xFFFFFF>>(24 - 3*(ch + active - t))));
				active--;
			}
		}
		if (disk_write((uint8_t*)oData + (~oInd & 512), t2, SeQ)) BB_RAM(&Status)[4] = 1;
		wr_flag = 0;
		curSector += t;
		SeQ = i;
	//}
	if (!active) GPIOB->BSRR = 1<<(9 + 16);	// LED off
	
	if (dirInd > 63) {							// recheck all of this
		if (write_sector((uint8_t*)indBuf, indSector)) BB_RAM(&Status)[5] = 1;
		if (indSector == indexAreaSize) indSector = 1;
		else indSector++;
		dirInd = dirInd - 64;
		for (i = 0; i < dirInd; i++) indBuf[i] = indBuf[64 + i];	// mem_cpy ??
		write_state(active | (dirInd<<8));
		if (write_sector(net_buf, 0)) BB_RAM(&Status)[6] = 1;
	}	
}

int main(void)
{
	uint32_t i;

	Init_ADC1();
	
	//RTC
	RTC_Init();
	if (!RTC->CNTH) RTC_Set(date2sec(2018, 1, 1, 0, 0, 0));
	PWR->CR = PWR_CR_LPDS | PWR_CR_PVDE | PWR_CR_PLS_2V9;		// down to 2.5v
	SCB->SCR = 1<<2;			// SLEEPDEEP
	EXTI->FTSR = 1<<16;

	//SD card init
	RCC->CFGR |= 1<<13;
	init_spi(0);
	
	//LED & pw switch
	//delay(200);
	GPIOB->CRH &= ~0xFF;
	GPIOB->CRH |= 0x26;
	//GPIOB->BSRR = 1<<8;	// pw
	GPIOB->BSRR = 1<<9;		// LED
	// DBG
	//GPIOB->CRH &= ~(0xF<<8);
	//GPIOB->CRH |= (1<<8); 			//PB10 50MHz
	delay(10);
	if (disk_initialize()) return 1;
	//RCC->CFGR &= ~(1<<13);
	if (read_sector(net_buf, 0)) return 1; // make some signaling
	if ((Buf[0] == 0x43455230) && (Buf[1] == 0x43455231) && (Buf[3] == 0x43455232)
				&& (Buf[4] < 64) && (Buf[8] < Buf[9])) {
		dirInd = Buf[4];
		curSector = Buf[5];
		fullWritten = Buf[6];
		indSector = Buf[7];
		indexAreaSize = Buf[8];
		cardSize1 = Buf[9];
		for (i = 0; i < 8; i++) crit_thr[i] = Buf[13 + i];
		mem_cpy(ch_names, net_buf + 21*4, *(net_buf + 21*4) + 1);	
		if (Buf[2]) {
			dirInd = 0;
			if (Buf[2] & 0xFF) check_record_state();
		}
		else if (dirInd && read_sector((uint8_t*)indBuf, indSector)) return 1;
	}
	else {
		if(disk_ioctl(GET_SECTOR_COUNT, (void*)&cardSize1)) return 1;
		indSector = 1;
		indexAreaSize = cardSize1/(64*SEQ_LEN);
		cardSize1--;
		curSector = indexAreaSize + 1;
		dirInd = 0;
		ch_names[0] = sizeof(def_names);
		mem_cpy(ch_names + 1, def_names, sizeof(def_names));
		for (i = 0; i < CH_NUM; i++) crit_thr[i] = CRIT_THR;
	}
	
	if (BKP->DR1 == 0x1234 && BKP->DR7 == 0x5678) {
		//BB_REG(&PWR->CR)[8] = 1;
		ip_addr = BKP->DR2 | (BKP->DR3<<16);
		*(uint16_t*)mac_addr = BKP->DR4;
		*(uint16_t*)(mac_addr + 2) = BKP->DR5;
		*(uint16_t*)(mac_addr + 4) = BKP->DR6;
		//BB_REG(&PWR->CR)[8] = 0;
	}
	else {
		ip_addr = IP_ADDR;
		mac_addr[0] = 0;
		mac_addr[1] = 0xEA;
		RCC->AHBENR  |= RCC_AHBENR_CRCEN;
		CRC->DR = *(uint32_t*)(0x1FFFF7E8);
		CRC->DR = *(uint32_t*)(0x1FFFF7E8 + 4);
		CRC->DR = *(uint32_t*)(0x1FFFF7E8 + 8);
		*(uint32_t*)(mac_addr + 2) = CRC->DR;
		RCC->AHBENR  &= ~RCC_AHBENR_CRCEN;
		BB_REG(&PWR->CR)[8] = 1;
		BKP->DR1 = 0x1234;
		BKP->DR2 = ip_addr & 0xFFFF;
		BKP->DR3 = (ip_addr>>16) & 0xFFFF;
		BKP->DR4 = *(uint16_t*)mac_addr;
		BKP->DR5 = *(uint16_t*)(mac_addr + 2);
		BKP->DR6 = *(uint16_t*)(mac_addr + 4);
		BKP->DR7 = 0x5678;
		BB_REG(&PWR->CR)[8] = 0;
	}
	
	lan_init();
	
	for (i = 0; i < CH_NUM; i++) {
		//crit[i] = crit_thr;
		//critCond[i] = 0;
		last[i] = AVG;
	}
	//oInd = 0;
	
	BB_REG(&TIM3->CR1)[0] = 1;					//Start ADC
	
	while((crit_thr[0] > 0) || active) {
		
		if (wr_flag) save_oData();
				
		if (!(GPIOB->IDR & (1<<7))) process_packet();
		
		if (PWR->CSR & PWR_CSR_PVDO) for(i = 0; i < CH_NUM; i++) crit_thr[i] = -crit_thr[i];
		
	}
	
	BB_REG(&TIM3->CR1)[0] = 0;					//Stop ADC
	if (dirInd) write_sector((uint8_t*)indBuf, indSector);
	for (i = 0; i < 8; i++) crit_thr[i] = -crit_thr[i];
	write_state(0);
	write_sector(net_buf, 0);
	if (PWR->CSR & PWR_CSR_PVDO) {
		GPIOB->BSRR = 1<<8; 							// swith off per
		ADC1->CR2 = 0;
		ADC2->CR2 = 0;
		EXTI->EMR = 1<<16;							// Enable PVD Event
		__WFE();
		SCB->AIRCR = 0x05FA0004;
	}
						//enc28j60_soft_reset();
						//enc28j60_wcr16(ERXND, 7679);
						//enc28j60_bfs(ECON1, ECON1_RXEN);
	
	
	TIM2->ARR = 20000;	 						// 10 sec
	TIM2->EGR = 1;
	BB_REG(&TIM2->CR1)[0] = 1;					// TIM_CR1_CEN
	// start_pulse();
	// disable wdg
	while(BB_REG(&TIM2->CR1)[0]) if (!(GPIOB->IDR & (1<<7))) get_fw();
	//while(1) if (!(GPIOB->IDR & 1)) get_fw();											// no? ARP here
	//BB_REG(&TIM2->CR1)[0] = 0;
	
	SCB->AIRCR = 0x05FA0004;


}





