#include "stm32f10x.h"

#define SPIx SPI1
#define init_spi init_spi1
#define DMA_CH_RX DMA1_Channel2
#define DMA_CH_TX DMA1_Channel3
#define DMA_ISR_RX DMA_ISR_TCIF2
#define DMA_ISR_TX DMA_ISR_TCIF3

void init_spi1 (uint8_t ph);
void init_spi2 (uint8_t ph);
uint8_t xchg_spi (uint8_t dat);
void recv_spi_multi (uint8_t* buff,	uint32_t btr);
void send_spi_multi (const uint8_t *buff,	uint32_t btx);
//void send_spi_multi_2 (const uint8_t *buff);
void mem_cpy(uint8_t* to, const uint8_t* from, uint16_t count);
void delay(uint16_t ms2);
