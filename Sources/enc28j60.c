#include "enc28j60.h"

#define enc28j60_release()	GPIOB->BSRR = 1<<06			//SPI1
#define enc28j60_select()	GPIOB->BSRR = 1<<(06+16)			//SPI1

extern uint32_t Status;

void enc28j60_set_bank(uint8_t adr);

// Generic SPI read command
uint8_t enc28j60_read_op(uint8_t cmd, uint8_t adr)
{
	uint8_t data;

	enc28j60_select();
	xchg_spi(cmd | (adr & ENC28J60_ADDR_MASK));
	if(adr & 0x80) xchg_spi(0xFF); 	// throw out dummy byte when reading MII/MAC register
	data = xchg_spi(0xFF);
	enc28j60_release();
	
	return data;
}

// Generic SPI write command
void enc28j60_write_op(uint8_t cmd, uint8_t adr, uint8_t data)
{
	enc28j60_select();
	xchg_spi(cmd | (adr & ENC28J60_ADDR_MASK));
	xchg_spi(data);
	enc28j60_release();
}

// Initiate software reset
void enc28j60_soft_reset()
{
	enc28j60_select();
	xchg_spi(ENC28J60_SPI_SC);
	enc28j60_release();

	enc28j60_set_bank(0);
	delay(10);	// Wait until device initializes
}


/*
 * Memory access
 */

// Set register bank
void enc28j60_set_bank(uint8_t adr)
{
	static uint8_t bank;

	adr = (adr >> 5) & 0x03;
	if (bank & ~adr) enc28j60_write_op(ENC28J60_SPI_BFC, ECON1, bank & ~adr);
	if (~bank & adr) enc28j60_write_op(ENC28J60_SPI_BFS, ECON1, ~bank & adr);
	bank = adr;
}

// Read register
uint8_t enc28j60_rcr(uint8_t adr)
{
	enc28j60_set_bank(adr);
	return enc28j60_read_op(ENC28J60_SPI_RCR, adr);
}

// Read register pair
uint16_t enc28j60_rcr16(uint8_t adr)
{
	enc28j60_set_bank(adr);
	return enc28j60_read_op(ENC28J60_SPI_RCR, adr) | (enc28j60_read_op(ENC28J60_SPI_RCR, adr+1) << 8);
}

// Write register
void enc28j60_wcr(uint8_t adr, uint8_t arg)
{
	enc28j60_set_bank(adr);
	enc28j60_select();
	xchg_spi(ENC28J60_SPI_WCR | (adr & ENC28J60_ADDR_MASK));
	xchg_spi(arg);
	enc28j60_release();
}

// Write register pair
void enc28j60_wcr16(uint8_t adr, uint16_t arg)
{
	enc28j60_set_bank(adr);
		
	enc28j60_select();
	xchg_spi(ENC28J60_SPI_WCR | (adr & ENC28J60_ADDR_MASK));
	xchg_spi((uint8_t)arg);
	xchg_spi(arg>>8);
	enc28j60_release();
	enc28j60_release(); // I Dont know why... It was so pain
}

// Clear bits in register (reg &= ~mask)
void enc28j60_bfc(uint8_t adr, uint8_t mask)
{
	enc28j60_set_bank(adr);
	enc28j60_write_op(ENC28J60_SPI_BFC, adr, mask);
}

// Set bits in register (reg |= mask)
void enc28j60_bfs(uint8_t adr, uint8_t mask)
{
	enc28j60_set_bank(adr);
	enc28j60_write_op(ENC28J60_SPI_BFS, adr, mask);
}

// Read PHY register
uint16_t enc28j60_read_phy(uint8_t adr)
{
	enc28j60_wcr(MIREGADR, adr);
	enc28j60_bfs(MICMD, MICMD_MIIRD);
	while(enc28j60_rcr(MISTAT) & MISTAT_BUSY);
	
	enc28j60_bfc(MICMD, MICMD_MIIRD);
	return enc28j60_rcr16(MIRD);
}

// Write PHY register
void enc28j60_write_phy(uint8_t adr, uint16_t data)
{
	enc28j60_wcr(MIREGADR, adr);
	enc28j60_wcr16(MIWR, data);
	while(enc28j60_rcr(MISTAT) & MISTAT_BUSY);
}

void enc28j60_set_mac(uint8_t* macadr)
{
	enc28j60_wcr(MAADR5, macadr[0]);
	enc28j60_wcr(MAADR4, macadr[1]);
	enc28j60_wcr(MAADR3, macadr[2]);
	enc28j60_wcr(MAADR2, macadr[3]);
	enc28j60_wcr(MAADR1, macadr[4]);
	enc28j60_wcr(MAADR0, macadr[5]);	
}

void enc28j60_init(uint8_t *macadr)
{
	// Initialize SPI
	//init_spi();
	enc28j60_release();

	// Reset ENC28J60
	enc28j60_soft_reset();

	// Setup Rx/Tx buffer/
	enc28j60_wcr16(ERXST, ENC28J60_RXSTART);	// 0
	enc28j60_wcr16(ERXRDPT, 0);
	enc28j60_wcr16(ERXND, ENC28J60_RXEND);		// ENC28J60_RXSIZE-1

	// Setup MAC
	enc28j60_wcr(MACON1, MACON1_TXPAUS| 					// Enable flow control
		MACON1_RXPAUS|MACON1_MARXEN); 						// Enable MAC Rx
	//enc28j60_wcr(MACON2, 0); 									// Clear reset
	enc28j60_wcr(MACON3, MACON3_PADCFG0 | 					// Enable padding,
		MACON3_TXCRCEN | MACON3_FRMLNEN | MACON3_FULDPX); 	// Enable crc & frame len chk
	enc28j60_wcr16(MAMXFL, ENC28J60_MAXFRAME);
	enc28j60_wcr(MABBIPG, 0x15); 								// Set inter-frame gap
	enc28j60_wcr(MAIPGL, 0x12);
	//enc28j60_wcr(MAIPGH, 0x0c);
	enc28j60_set_mac(macadr);
	//enc28j60_wcr(MAADR5, macadr[0]); 						// Set MAC address
	//enc28j60_wcr(MAADR4, macadr[1]);
	//enc28j60_wcr(MAADR3, macadr[2]);
	//enc28j60_wcr(MAADR2, macadr[3]);
	//enc28j60_wcr(MAADR1, macadr[4]);
	//enc28j60_wcr(MAADR0, macadr[5]);
	
	enc28j60_wcr(ERXFCON, ERXFCON_CRCEN | ERXFCON_UCEN | ERXFCON_BCEN);	// Discard if CRC error
	enc28j60_wcr(EIE, EIE_INTIE | EIE_PKTIE); // INT Init

	// Setup PHY
	enc28j60_write_phy(PHCON1, PHCON1_PDPXMD); // Force full-duplex mode
	enc28j60_write_phy(PHCON2, PHCON2_HDLDIS); // Disable loopback
	enc28j60_write_phy(PHLCON, PHLCON_LACFG2| // Configure LED ctrl
		PHLCON_LBCFG2|PHLCON_LBCFG1|PHLCON_LBCFG0|
		PHLCON_LFRQ0|PHLCON_STRCH);

	// Enable Rx packets
	enc28j60_bfs(ECON1, ECON1_RXEN);
}

void enc28j60_send_packet(uint8_t *data, uint16_t len)
{
					//GPIOB->BSRR = 1<<05;
	while(enc28j60_read_op(ENC28J60_SPI_RCR, ECON1) & ECON1_TXRTS) {		// bank any
		// TXRTS may not clear - ENC28J60 bug. We must reset
		// transmit logic in case of Tx error
		if(enc28j60_read_op(ENC28J60_SPI_RCR, EIR) & EIR_TXERIF) {
			enc28j60_write_op(ENC28J60_SPI_BFS, ECON1, ECON1_TXRST);
			enc28j60_write_op(ENC28J60_SPI_BFC, ECON1, ECON1_TXRST);	// 2 times ?
		}
	}
	enc28j60_wcr16(EWRPT, ENC28J60_TXSTART);
	enc28j60_select();
	xchg_spi(ENC28J60_SPI_WBM);
	xchg_spi(0);
	
	send_spi_multi(data, len);
	enc28j60_release();
	
	enc28j60_wcr16(ETXST, ENC28J60_TXSTART);	// bank 0
	enc28j60_wcr16(ETXND, ENC28J60_TXSTART + len);
	enc28j60_write_op(ENC28J60_SPI_BFS, ECON1, ECON1_TXRTS);	// Request packet send
						//GPIOB->BSRR = 1<<(05+16);
}


uint16_t enc28j60_recv_packet(uint8_t *buf)
{
	static uint16_t rxrdpt;
	uint16_t rxlen = 0, status;

	enc28j60_set_bank(EPKTCNT);		// bank 1
	if(enc28j60_read_op(ENC28J60_SPI_RCR, EPKTCNT)) {
		enc28j60_wcr16(ERDPT, rxrdpt);

			enc28j60_select();
			xchg_spi(ENC28J60_SPI_RBM);
			rxrdpt = xchg_spi(0xFF);
			rxrdpt |= xchg_spi(0xFF) << 8;
			rxlen = xchg_spi(0xFF);
			rxlen |= xchg_spi(0xFF) << 8;
			status = xchg_spi(0xFF);
			status |= xchg_spi(0xFF) << 8;

			if ((rxlen < 0x40) || (rxlen > ENC28J60_MAXFRAME)) {
				BB_RAM(&Status)[7] = 1;
				status = 0;
			}
			
			if(status & 0x80) {
				rxlen = rxlen - 4;  // throw out crc
				recv_spi_multi(buf, rxlen);
			}
			else rxlen = 0;
			enc28j60_release();
		
		// Set Rx read pointer to next packet
		enc28j60_wcr16(ERXRDPT, (rxrdpt - 1) & ENC28J60_BUFEND);
		// Decrement packet counter
		enc28j60_write_op(ENC28J60_SPI_BFS, ECON2, ECON2_PKTDEC);
	}
	
	return rxlen;
}
