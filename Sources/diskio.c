#include "diskio.h"

#define CS_HIGH()	GPIOB->BSRR = (1<<12)					//deselect SD
#define CS_LOW()	GPIOB->BSRR = (1<<(12+16))				//select SD
#define FCLK_SLOW() { SPIx->CR1 |= 0x38; }	// Set SCLK = PCLK/256
#define FCLK_FAST() { SPIx->CR1 &= ~0x30; }	// Set SCLK = PCLK/4		25MHz max

/* MMC/SD command */
#define CMD0	(0)				/* GO_IDLE_STATE */
#define CMD1	(1)				/* SEND_OP_COND (MMC) */
#define	ACMD41	(0x80+41)	/* SEND_OP_COND (SDC) */
#define CMD8	(8)				/* SEND_IF_COND */
#define CMD9	(9)				/* SEND_CSD */
#define CMD10	(10)				/* SEND_CID */
#define CMD12	(12)				/* STOP_TRANSMISSION */
#define ACMD13	(0x80+13)		/* SD_STATUS (SDC) */
#define CMD16	(16)				/* SET_BLOCKLEN */
#define CMD17	(17)				/* READ_SINGLE_BLOCK */
#define CMD18	(18)				/* READ_MULTIPLE_BLOCK */
#define CMD23	(23)				/* SET_BLOCK_COUNT (MMC) */
#define	ACMD23	(0x80+23)	/* SET_WR_BLK_ERASE_COUNT (SDC) */
#define CMD24	(24)				/* WRITE_BLOCK */
#define CMD25	(25)				/* WRITE_MULTIPLE_BLOCK */
#define CMD32	(32)				/* ERASE_ER_BLK_START */
#define CMD33	(33)				/* ERASE_ER_BLK_END */
#define CMD38	(38)				/* ERASE */
#define CMD55	(55)				/* APP_CMD */
#define CMD58	(58)				/* READ_OCR */

static uint8_t CardType;			/* Card type flags */

//extern uint8_t oData[8][512];

static uint8_t wait_ready (uint16_t wt)		// 1:Ready, 0:Timeout
{
	TIM2->ARR = wt; 						// wt/2 ms
	TIM2->EGR = 1;
	BB_REG(&TIM2->CR1)[0] = 1;					// TIM_CR1_CEN
	while ((xchg_spi(0xFF) != 0xFF) && (TIM2->CR1 & 1));	// Wait for card goes ready or timeout
	wt = TIM2->CR1 & 1;
	BB_REG(&TIM2->CR1)[0] = 0;
	return wt;
}

#define deselect() CS_HIGH(); xchg_spi(0xFF)		/* Dummy clock (force DO hi-z for multiple slave SPI) */

int select (void)	/* 1:OK, 0:Timeout */
{
	CS_LOW();		/* Set CS# low */
	//xchg_spi(0xFF);	/* Dummy clock (force DO enabled) */
	if (wait_ready(500)) return 1;	/* Wait for card ready */

	deselect();
	return 0;	/* Timeout */
}

static int rcvr_datablock (uint8_t *buff, uint32_t btr)	// 1:OK, 0:Error
{
	TIM2->ARR = 400; 						// 200 ms
	TIM2->EGR = 1;
	BB_REG(&TIM2->CR1)[0] = 1;					// TIM_CR1_CEN
	while ((xchg_spi(0xFF) == 0xFF) && (TIM2->CR1 & 1));		/* Wait for DataStart token in timeout of 200ms */
	if(!(TIM2->CR1 & 1)) return 0;									/* Function fails if invalid DataStart token or timeout */
	BB_REG(&TIM2->CR1)[0] = 0;
	recv_spi_multi(buff, btr);														/* Store trailing data to the buffer */
	xchg_spi(0xFF); xchg_spi(0xFF);												/* Discard CRC */

	return 1;
}

static int xmit_datablock (const uint8_t *buff,	uint8_t token)	// 1:OK, 0:Failed
{
	if (!wait_ready(500)) return 0;			/* Wait for card ready */
	xchg_spi(token);								/* Send token */
	send_spi_multi(buff, 512);						/* Data */
	xchg_spi(0xFF);								// Can it be ANY numbers? (Dummy CRC)
	xchg_spi(0xFF);
	return ((xchg_spi(0xFF) & 0x1F) == 0x05);				/* Function fails if the data packet was not accepted */
}

static uint8_t send_cmd (uint8_t cmd, uint32_t arg)		// Return value: R1 resp (bit7==1:Failed to send)
{
	if (cmd & 0x80) {	/* Send a CMD55 prior to ACMD<n> */
		cmd &= 0x7F;
		if (send_cmd(CMD55, 0) > 1) return 0xFF;
	}

	
	if (cmd != CMD12) {							/* Select the card and wait for ready except to stop multiple block read */
		deselect();
		if (!select()) return 0xFF;
	}

	/* Send command packet */
	xchg_spi(0x40 | cmd);					/* Start + command index */
	xchg_spi((uint8_t)(arg >> 24));		/* Argument[31..24] */
	xchg_spi((uint8_t)(arg >> 16));		/* Argument[23..16] */
	xchg_spi((uint8_t)(arg >> 8));		/* Argument[15..8] */
	xchg_spi((uint8_t)arg);					/* Argument[7..0] */
	//n = 0x01;									/* Dummy CRC + Stop */
	//if (cmd == CMD0) n = 0x95;			/* Valid CRC for CMD0(0) */
	if (cmd == CMD8) xchg_spi(0x87);			/* Valid CRC for CMD8(0x1AA) */
	else xchg_spi(0x95);

	/* Receive command resp */
	if (cmd == CMD12) xchg_spi(0xFF);	/* Diacard following one byte when CMD12 */
	cmd = 10;									/* Wait for response (10 bytes max) */
	do {
		arg = xchg_spi(0xFF);
	} while ((arg & 0x80) && --cmd);

	return arg;							/* Return received response */
}

DSTATUS disk_initialize ()
{
	uint8_t n;
	uint32_t 	ocr = 0;
	
	FCLK_SLOW();

	for (n = 10; n; n--) xchg_spi(0xFF);							// Send 80 dummy clocks
	CardType = 0;
	
	if (send_cmd(CMD0, 0) == 1) {													// Put the card SPI/Idle state
		if (send_cmd(CMD8, 0x1AA) == 1) {										// SDv2?
			for (n = 0; n < 4; n++) ocr = (ocr << 8) | xchg_spi(0xFF);	// Get 32 bit return value of R7 resp
			if (ocr == 0x1AA) {														// Is the card supports vcc of 2.7-3.6V?
				//TIM2->ARR = 10000; 												// 500 ms
				//TIM2->EGR = 1;
				//BB_REG(&TIM2->CR1)[0] = 1;					// TIM_CR1_CEN
				//while ((TIM2->CR1 & 1) && send_cmd(ACMD41, 1UL << 30));	// Wait for end of initialization with ACMD41(HCS)
				ocr = 0x1FFF;
				while (ocr && send_cmd(ACMD41, 1UL << 30)) ocr--;
				if (ocr && (send_cmd(CMD58, 0) == 0)) {			// Check CCS bit in the OCR
					ocr = 0;
					for (n = 0; n < 4; n++) ocr = (ocr << 8) | xchg_spi(0xFF);
					CardType = (__rev(ocr) & 0x40) ? CT_SD2 | CT_BLOCK : CT_SD2;					// Card id SDv2
				}
				//BB_REG(&TIM2->CR1)[0] = 0;
			}
		} else {														// Not SDv2 card
			if (send_cmd(ACMD41, 0) <= 1) {					// SDv1 or MMC?
				CardType = CT_SD1; ocr = ACMD41;						// SDv1 (ACMD41(0))
			} else {
				CardType = CT_MMC; ocr = CMD1;						// MMCv3 (CMD1(0))
			}
			n = 1;													// Count up to 256
			while (n && send_cmd(ocr, 0)) n++;				// Wait for end of initialization
			if (!n || send_cmd(CMD16, 512)) CardType = 0;		// Set block length: 512
		}
	}

	deselect();
	if (CardType) FCLK_FAST();						// OK	- Set fast clock
	
	//Timer = 0;
	//while (Timer < 100);			// ?????...
				
	return !CardType;
}

uint8_t disk_read (uint8_t *buff, uint32_t sector,	uint32_t count)
{
	if (!(CardType & CT_BLOCK)) sector *= 512;	// LBA ot BA conversion (byte addressing cards)

	if (count == 1) {
		if ((send_cmd(CMD17, sector) == 0) && rcvr_datablock(buff, 512)) count = 0;	// READ_SINGLE_BLOCK
	}
	else {
		if (send_cmd(CMD18, sector) == 0) {					// READ_MULTIPLE_BLOCK
			while (count--) {
				if (!rcvr_datablock(buff, 512)) break;
				buff += 512;
			}
			send_cmd(CMD12, 0);									// STOP_TRANSMISSION
		}
	}
	deselect();

	return count;
}

uint8_t disk_write (uint8_t* buf, uint32_t sector, uint32_t seq)
{
	uint8_t d;

	if (!(CardType & CT_BLOCK)) sector *= 512;							// LBA ==> BA conversion (byte addressing cards)

	d = seq>>28;
	if (d == 1) {
		if (!send_cmd(CMD24, sector)													// WRITE_BLOCK
			&& xmit_datablock(buf + 1024*(seq & 7), 0xFE)) d = 0;
	}
	else {
		if (CardType & CT_SDC) send_cmd(ACMD23, d);								// Predefine number of sectors
		if (!send_cmd(CMD25, sector)) {												// WRITE_MULTIPLE_BLOCK
			while (d && xmit_datablock(buf + 1024*(seq & 7), 0xFC)) {
				seq >>= 3;
				d--;
			}
			if (wait_ready(500)) xchg_spi(0xFD);									// STOP_TRAN token
		}
	}
	
	deselect();
	return d;
}

uint8_t read_sector(uint8_t* buff, uint32_t sector)		// 0 - OK
{
	uint8_t d;
	
	if (!(CardType & CT_BLOCK)) sector *= 512;	// LBA ot BA conversion (byte addressing cards)
	
	//deselect();
	CS_LOW();
	if (!wait_ready(500)) {deselect();return 10;}

	// Send command packet
	xchg_spi(0x40 | CMD17);						// Start + command index
	xchg_spi((uint8_t)(sector >> 24));		// Argument[31..24]
	xchg_spi((uint8_t)(sector >> 16));		// Argument[23..16]
	xchg_spi((uint8_t)(sector >> 8));		// Argument[15..8]
	xchg_spi((uint8_t)sector);					// Argument[7..0]
	xchg_spi(0x01);								// Dummy CRC

	d = 10;											// Wait for response (10 bytes max)
	while (xchg_spi(0xFF) && --d);
	if(!d) {deselect();return 11;}

	TIM2->ARR = 400; 								// 200 ms
	TIM2->EGR = 1;
	BB_REG(&TIM2->CR1)[0] = 1;					// TIM_CR1_CEN
	
	while ((xchg_spi(0xFF) == 0xFF) && (TIM2->CR1 & 1));	// Wait for DataStart token in timeout of 200ms
	if(!(TIM2->CR1 & 1)) {deselect();return 12;}			// Function fails if invalid DataStart token or timeout
	BB_REG(&TIM2->CR1)[0] = 0;
	
	recv_spi_multi (buff, 512);
	xchg_spi(0xFF); xchg_spi(0xFF);			// Discard CRC	- Also in DMA?
	
	deselect();
	return 0;						// Function succeeded
}

uint8_t write_sector (const uint8_t *buff,	uint32_t sector)	// 0 - OK
{
	uint8_t d;
	
	if (!(CardType & CT_BLOCK)) sector *= 512;	/* LBA ==> BA conversion (byte addressing cards) */
	
	//deselect();
	CS_LOW();
	if (!wait_ready(500)) {deselect();return 10;}

	// Send command packet
	xchg_spi(0x40 | CMD24);						// Start + command index
	xchg_spi((uint8_t)(sector >> 24));		// Argument[31..24]
	xchg_spi((uint8_t)(sector >> 16));		// Argument[23..16]
	xchg_spi((uint8_t)(sector >> 8));		// Argument[15..8]
	xchg_spi((uint8_t)sector);					// Argument[7..0]
	xchg_spi(0x01);								// Dummy CRC + Stop

	d = 10;											// Wait for response (10 bytes max)
	while ((xchg_spi(0xFF) & 0x80) && --d);
	if (!d) {deselect();return 11;}
	
	if(!wait_ready(500)) {deselect();return 12;}
	xchg_spi(0xFE);											// Send token
	
	send_spi_multi (buff, 512);
	xchg_spi(0xFF); xchg_spi(0xFF);					// Dummy CRC
	d = xchg_spi(0xFF);									// Receive data resp
	
	deselect();
	return ((d & 0x1F) != 0x05);						// Function OK if (resp & 0x1F) == 0x05
}


/*uint32_t read_lastInt(uint32_t sector)
{
	uint8_t n;
	uint32_t res;

	//if (Stat & STA_NOINIT) return 0xFFFFFFFF;	// Check if drive is ready
	if (!(CardType & CT_BLOCK)) sector *= 512;	// LBA ot BA conversion (byte addressing cards)

	// Select the card and wait for ready except to stop multiple block read
	deselect();
	if (!select()) return 0xFFFFFFFF;

	// Send command packet
	xchg_spi(0x40 | CMD17);				// Start + command index 
	xchg_spi((uint8_t)(sector >> 24));		// Argument[31..24]
	xchg_spi((uint8_t)(sector >> 16));		// Argument[23..16]
	xchg_spi((uint8_t)(sector >> 8));			// Argument[15..8]
	xchg_spi((uint8_t)sector);				// Argument[7..0]
	xchg_spi(1);									// Dummy CRC + Stop

	// Receive command resp
	n = 10;								// Wait for response (10 bytes max)
	do {
		res = xchg_spi(0xFF);
	} while ((res & 0x80) && --n);

	if (res) return 0xFFFFFFFF;
	
	Timer = 0;
	do {							// Wait for DataStart token in timeout of 200ms
		n = xchg_spi(0xFF);
	} while ((n == 0xFF) && (Timer < 200));
	if(n != 0xFE) return 0xFFFFFFFF;		// Function fails if invalid DataStart token or timeout

	n = 0xFF;
	DMA1_Channel4->CMAR = (uint32_t)&res;				// Receive here
	DMA1_Channel4->CNDTR = 4;
	DMA1_Channel4->CCR = DMA_CCR1_EN | DMA_CCR1_CIRC | DMA_CCR1_MINC | (1<<12);		// Memory increment, Circular, DMA en, Med prior
	DMA1_Channel5->CMAR = (uint32_t)&n;					// Send here
	DMA1_Channel5->CNDTR = 512;
	DMA1_Channel5->CCR = DMA_CCR1_DIR | DMA_CCR1_EN;		// From memory, DMA en
	DMA1->IFCR = DMA_ISR_TCIF5;
	SPIx->CR2 = SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
	while (!(DMA1->ISR & DMA_ISR_TCIF5));
	while (SPIx->SR & (1<<7));
	DMA1->IFCR = DMA_ISR_TCIF4;
	SPIx->CR2 = 0;
	DMA1_Channel4->CCR = 0;
	DMA1_Channel5->CCR = 0;
	
	xchg_spi(0xFF); xchg_spi(0xFF);			// Discard CRC

	deselect();

	return res;	// Return result
}*/

uint8_t disk_ioctl (
	uint8_t cmd,		/* Control command code */
	void *buff		/* Pointer to the conrtol data */
)
{
	DRESULT res;
	uint8_t n, csd[16];
	uint32_t *dp, st, ed, csize;

	res = RES_ERROR;

	switch (cmd) {
	case CTRL_SYNC :		/* Wait for end of internal write process of the drive */
		if (select()) res = RES_OK;
		break;

	case GET_SECTOR_COUNT :	/* Get drive capacity in unit of sector (uint32_t) */
		if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {
			if ((csd[0] >> 6) == 1) {	/* SDC ver 2.00 */
				csize = csd[9] + ((uint16_t)csd[8] << 8) + ((uint32_t)(csd[7] & 63) << 16) + 1;
				*(uint32_t*)buff = csize << 10;
			} else {					/* SDC ver 1.XX or MMC ver 3 */
				n = (csd[5] & 15) + ((csd[10] & 128) >> 7) + ((csd[9] & 3) << 1) + 2;
				csize = (csd[8] >> 6) + ((uint16_t)csd[7] << 2) + ((uint16_t)(csd[6] & 3) << 10) + 1;
				*(uint32_t*)buff = csize << (n - 9);
			}
			res = RES_OK;
		}
		break;

	case GET_BLOCK_SIZE :	/* Get erase block size in unit of sector (uint32_t) */
		if (CardType & CT_SD2) {	/* SDC ver 2.00 */
			if (send_cmd(ACMD13, 0) == 0) {	/* Read SD status */
				xchg_spi(0xFF);
				if (rcvr_datablock(csd, 16)) {				/* Read partial block */
					for (n = 64 - 16; n; n--) xchg_spi(0xFF);	/* Purge trailing data */
					*(uint32_t*)buff = 16UL << (csd[10] >> 4);
					res = RES_OK;
				}
			}
		} else {					/* SDC ver 1.XX or MMC */
			if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(csd, 16)) {	/* Read CSD */
				if (CardType & CT_SD1) {	/* SDC ver 1.XX */
					*(uint32_t*)buff = (((csd[10] & 63) << 1) + ((uint16_t)(csd[11] & 128) >> 7) + 1) << ((csd[13] >> 6) - 1);
				} else {					/* MMC */
					*(uint32_t*)buff = ((uint16_t)((csd[10] & 124) >> 2) + 1) * (((csd[11] & 3) << 3) + ((csd[11] & 224) >> 5) + 1);
				}
				res = RES_OK;
			}
		}
		break;

	case CTRL_TRIM :	/* Erase a block of sectors (used when _USE_ERASE == 1) */
		if (!(CardType & CT_SDC)) break;				/* Check if the card is SDC */
		if (disk_ioctl(MMC_GET_CSD, csd)) break;	/* Get CSD */
		if (!(csd[0] >> 6) && !(csd[10] & 0x40)) break;	/* Check if sector erase can be applied to the card */
		dp = buff; st = dp[0]; ed = dp[1];				/* Load sector block */
		if (!(CardType & CT_BLOCK)) {
			st *= 512; ed *= 512;
		}
		if (send_cmd(CMD32, st) == 0 && send_cmd(CMD33, ed) == 0 && send_cmd(CMD38, 0) == 0 && wait_ready(30000)) {	/* Erase sector block */
			res = RES_OK;	/* FatFs does not check result of this command */
		}
		break;
	case GET_CSD:
		if ((send_cmd(CMD9, 0) == 0) && rcvr_datablock(buff, 16)) res = RES_OK;
		break;
	default:
		res = RES_PARERR;
	}

	deselect();

	return res;
}

