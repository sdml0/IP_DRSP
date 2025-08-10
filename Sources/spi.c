#include "spi.h"

// Initialize SPI interface
void init_spi1 (uint8_t ph)		//SPI1	36MHz			SCK-B3 MISO-B4 MOSI-B5
{
		//RCC->AHBENR  |= RCC_AHBENR_DMA1EN;
	RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
	RCC->APB2ENR |= RCC_APB2ENR_SPI1EN | RCC_APB2ENR_IOPBEN | RCC_APB2ENR_AFIOEN;
	
	AFIO->MAPR = (2<<24) | 1; 									// Disable JTAG & remap SPI1
	RCC->APB2ENR &= ~RCC_APB2ENR_AFIOEN;
	GPIOB->CRL = 0x40000444 | (0xBu<<12) | (0xBu<<20);	// PB03 PB05 AltPP 50MHz
	GPIOB->BSRR = 1<<04;											// PB04 Pull-up
	GPIOB->CRL |= (8<<16) | (1<<24);							// PB04 InPul, PB06 OutPP 10MHz (CS for Eth)
	GPIOB->CRH = 0x44404444 | (1<<16);						// PB12 OutPP 10MHz (CS for SD)
	
	SPI1->CR1 = (1<<9)|(1<<8)|(1<<6)|(1<<2)|0x38|ph;	// Enable SPI1 slow & phase
	GPIOB->BSRR = (1<<06) | (1<<12);							// Set CS high
	// For delay
	TIM2->PSC = 36000 - 1;										// 72MHz -> 2kHz = 0,5ms
	TIM2->CR1 = TIM_CR1_OPM;									// One pulse
	// For DMA
	DMA_CH_RX->CPAR = (uint32_t)&(SPI1->DR);
	DMA_CH_TX->CPAR = (uint32_t)&(SPI1->DR);
}

void init_spi2 (uint8_t ph)		//SPI2	18MHz			SCK-B13 MISO-B14 MOSI-B15
{
		//RCC->AHBENR  |= RCC_AHBENR_DMA1EN;
	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN | RCC_APB1ENR_TIM2EN;
	RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
		
	GPIOB->CRH = 0x4444 | (0xBu<<20) | (0xBu<<28);		// PB13 PB15 AltPP 50MHz
	GPIOB->CRL = 0x40444444 | (1<<24);						// PB06 OutPP 10MHz (CS for Eth)
	GPIOB->CRH |= 1<<16;											// PB12 OutPP 10MHz (CS for SD)
	GPIOB->BSRR = 1<<14;
	GPIOB->CRH |= 8<<24;											// PB14 InPul
	SPI2->CR1 = (1<<9)|(1<<8)|(1<<6)|(1<<2)|0x38|ph;	// Enable SPI2 slow & phase
	GPIOB->BSRR = (1<<06) | (1<<12);							// Set CS high
	// For delay
	TIM2->PSC = 36000 - 1;										// 72MHz -> 2kHz = 0,5ms
	TIM2->CR1 = TIM_CR1_OPM;									// One pulse
	// For DMA
	DMA_CH_RX->CPAR = (uint32_t)&(SPI2->DR);
	DMA_CH_TX->CPAR = (uint32_t)&(SPI2->DR);
}

// Exchange a byte
uint8_t xchg_spi (uint8_t dat)
{
	SPIx->DR = dat;
	while (!(SPIx->SR & SPI_SR_RXNE));
	//while (SPIx->SR & (1<<7));
	return (uint8_t)SPIx->DR;
}

void recv_spi_multi (uint8_t* buff,	uint32_t btr)
{
	DMA_CH_RX->CMAR = (uint32_t)buff;
	DMA_CH_RX->CNDTR = btr;
	DMA_CH_RX->CCR = DMA_CCR1_EN | DMA_CCR1_MINC;		// Memory increment, DMA en, Med prior
	DMA_CH_TX->CMAR = (uint32_t)&btr;
	DMA_CH_TX->CNDTR = btr;
	btr = 0xFF;
	DMA_CH_TX->CCR = DMA_CCR1_DIR | DMA_CCR1_EN;		// From memory, DMA en
	SPIx->CR2 = SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
	while (!(DMA1->ISR & DMA_ISR_RX));
	DMA1->IFCR = DMA_ISR_RX | DMA_ISR_TX;
	SPIx->CR2 = 0;
	DMA_CH_RX->CCR = 0;
	DMA_CH_TX->CCR = 0;
}

void send_spi_multi (const uint8_t *buff,	uint32_t btx)
{
	DMA_CH_TX->CMAR = (uint32_t)buff;
	DMA_CH_TX->CNDTR = btx;
	DMA_CH_TX->CCR = DMA_CCR1_DIR | DMA_CCR1_MINC | DMA_CCR1_EN;		// From memory, Memory increment, DMA en
	SPIx->CR2 = SPI_CR2_TXDMAEN;
	while (!(DMA1->ISR & DMA_ISR_TX));
	DMA1->IFCR = DMA_ISR_TX;
	DMA_CH_TX->CCR = 0;
	while (!(SPIx->SR & SPI_SR_TXE));
	while (SPIx->SR & SPI_SR_BSY);
	SPIx->CR2 = 0;
	SPIx->DR;
}

void delay(uint16_t ms2)
{
	TIM2->ARR = ms2; 								// ms2/2 ms
	TIM2->EGR = 1;
	BB_REG(&TIM2->CR1)[0] = 1;					// TIM_CR1_CEN
	while(TIM2->CR1 & 1);
}

void mem_cpy(uint8_t* to, const uint8_t* from, uint16_t count)
{	
	DMA1_Channel7->CMAR = (uint32_t)to;
	DMA1_Channel7->CPAR = (uint32_t)from;
	DMA1_Channel7->CNDTR = count;
	DMA1_Channel7->CCR = DMA_CCR1_MINC | DMA_CCR1_PINC | DMA_CCR1_MEM2MEM | DMA_CCR1_EN;	// 8-8
	while (!(DMA1->ISR & DMA_ISR_TCIF7));
	DMA1_Channel7->CCR = 0;
	DMA1->IFCR = DMA_ISR_TCIF7;
}

/*void mem_cpy(uint8_t* to, const uint8_t* from, uint16_t count)
{	
	DMA1_Channel2->CMAR = (uint32_t)to;
	DMA1_Channel2->CPAR = (uint32_t)from;
	DMA1_Channel2->CNDTR = count/2;
	DMA1_Channel2->CCR = DMA_CCR1_MSIZE_0 | DMA_CCR1_PSIZE_0 | DMA_CCR1_MINC | DMA_CCR1_PINC | DMA_CCR1_MEM2MEM | DMA_CCR1_EN;	// 16-16
	while (!(DMA1->ISR & DMA_ISR_TCIF2));
	DMA1_Channel2->CCR = 0;
	DMA1->IFCR = DMA_ISR_TCIF2;
	if (!(--count & 1)) to[count] = from[count];
}*/

