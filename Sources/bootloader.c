#include "stm32f10x.h"
#include "tinf.h"

#define RAM_ADDR 0x20000000
#define FW_ADDR 0x08007000

												//	0	  1  2  3  4  5  6    7  8  9  A  B    C    D  E    F
static const uint8_t rep_tbl[16] = { 0xC, 0xB, 5, 1, 9, 7, 0, 0xF, 6, 2, 4, 3, 0xD, 0xA, 8, 0xE };

// ALL MUST BE ON STACK !!!

void flSave16bit(uint16_t* pDest, uint16_t data)
{
	if (!((uint32_t)pDest & 0x3FF)) {
		FLASH->CR &= ~FLASH_CR_PG;
		FLASH->CR |= FLASH_CR_PER;
		FLASH->AR = (uint32_t)pDest;
		FLASH->CR |= FLASH_CR_STRT;
		while (!(FLASH->SR & FLASH_SR_EOP));
		FLASH->SR = FLASH_SR_EOP;
		FLASH->CR &= ~FLASH_CR_PER;
		FLASH->CR |= FLASH_CR_PG;
	}
	*pDest = data;
	while (!(FLASH->SR & FLASH_SR_EOP));
	FLASH->SR = FLASH_SR_EOP;
}

void bootloader()
{   
	uint8_t t;
	uint32_t i;
	TINF_DATA pData_M;
	
	/*if (!(FLASH->OBR & FLASH_OBR_RDPRT)) {		// read lock
		FLASH->KEYR = 0x45670123;
		FLASH->KEYR = 0xCDEF89AB;
		FLASH->OPTKEYR = 0x45670123;
		FLASH->OPTKEYR = 0xCDEF89AB;
		FLASH->CR |= FLASH_CR_OPTER;
		FLASH->CR |= FLASH_CR_STRT;
		while (!(FLASH->SR & FLASH_SR_EOP));
		SCB->AIRCR = 0x05FA0004;
	}*/
	RCC->AHBENR  |= RCC_AHBENR_DMA1EN;
	//LED
	//RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
	//GPIOC->CRH = (2<<20);		//	PC13
	// DBg
	//RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
	//GPIOB->CRL &= ~(0xF<<20);
	//GPIOB->CRL |= (3<<20);	// PB5
	
	if ((((uint32_t*)RAM_ADDR)[0] == 0x70555766) && ((*(uint16_t*)(RAM_ADDR + 6)) < 4609)) {
					//GPIOB->BSRR = 1<<(05);		//DBug
		
		for (i = RAM_ADDR + 12; i < RAM_ADDR + 16; i++) {
			t = *(uint8_t*)i;
			t = rep_tbl[t & 0xF] | (rep_tbl[t>>4]<<4);
			*(uint8_t*)i = t;
		}
		RCC->AHBENR  |= RCC_AHBENR_CRCEN;
		DMA1_Channel2->CMAR = RAM_ADDR + 12;
		DMA1_Channel2->CPAR = (uint32_t)&(CRC->DR);
		DMA1_Channel2->CNDTR = *(uint16_t*)(RAM_ADDR + 6);		//Nr of words
		DMA1_Channel2->CCR = (10<<8) | DMA_CCR1_MINC | DMA_CCR1_MEM2MEM | DMA_CCR1_DIR | DMA_CCR1_EN;	// 32-32
		while (!(DMA1->ISR & DMA_ISR_TCIF2));
		DMA1->IFCR = DMA_ISR_TCIF2;
		DMA1_Channel2->CCR = 0;
		((uint32_t*)RAM_ADDR)[0] = CRC->DR;
		RCC->AHBENR  &= ~RCC_AHBENR_CRCEN;
		if (((uint32_t*)RAM_ADDR)[0] != ((uint32_t*)RAM_ADDR)[2]) return;
		// LED
		//GPIOC->BSRR = 1<<13;
		
		FLASH->KEYR = 0x45670123;		// Unlock FLASH
		FLASH->KEYR = 0xCDEF89AB;
		
		pData_M.pSource =	(uint8_t*)(RAM_ADDR + 12);
		pData_M.pDest = 	(uint8_t*)FW_ADDR;
		
		((uint32_t*)RAM_ADDR)[0] = (uint32_t)&pData_M;
 
		t = tinf_uncompress();
		//FLASH->SR = FLASH_SR_EOP;
		FLASH->CR &= ~FLASH_CR_PG;
		FLASH->CR |= FLASH_CR_LOCK;
		
		// LED
		//GPIOC->BSRR = 1<<(13+16);
					//GPIOB->BSRR = 1<<(05+16);		//DBug
	}
}

