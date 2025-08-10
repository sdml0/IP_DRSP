#include "stm32f10x.h"
#include "tinf.h"

extern void flSave16bit(uint16_t* pDest, uint16_t data);

#define pData ((TINF_DATA*)(*(uint32_t*)0x20000000))


/* --------------------------------------------------- *
 * -- uninitialized global data (static structures) -- *
 * --------------------------------------------------- */

/* extra bits and base tables for length codes */
static const uint8_t length_bits_const[30] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 
   0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03, 
   0x04, 0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x05, 0x00, 0x06, 
};

static const uint16_t length_base_const[30] = {
   0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0x0008, 0x0009, 0x000a, 0x000b, 0x000d, 
   0x000f, 0x0011, 0x0013, 0x0017, 0x001b, 0x001f, 0x0023, 0x002b, 0x0033, 0x003b, 
   0x0043, 0x0053, 0x0063, 0x0073, 0x0083, 0x00a3, 0x00c3, 0x00e3, 0x0102, 0x0143, 
};

/* extra bits and base tables for distance codes */
static const uint8_t dist_bits_const[30] = {
   0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 
   0x04, 0x04, 0x05, 0x05, 0x06, 0x06, 0x07, 0x07, 0x08, 0x08, 
   0x09, 0x09, 0x0a, 0x0a, 0x0b, 0x0b, 0x0c, 0x0c, 0x0d, 0x0d, 
};

static const uint16_t dist_base_const[30] = {
   0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0007, 0x0009, 0x000d, 0x0011, 0x0019, 
   0x0021, 0x0031, 0x0041, 0x0061, 0x0081, 0x00c1, 0x0101, 0x0181, 0x0201, 0x0301, 
   0x0401, 0x0601, 0x0801, 0x0c01, 0x1001, 0x1801, 0x2001, 0x3001, 0x4001, 0x6001, 
};

/* special ordering of code length codes */
static const uint8_t clcidx[] = {
   16, 17, 18, 0, 8, 7, 9, 6,
   10, 5, 11, 4, 12, 3, 13, 2,
   14, 1, 15
};

/* ----------------------- *
 * -- utility functions -- *
 * ----------------------- */

/*
 * put next byte to dest
 */
static void tinf_save_to_dest(uint8_t* buf, uint16_t len)
{
	if (pData->pDest - 1 == buf) {
		if ((uint32_t)pData->pDest & 1) flSave16bit((uint16_t*)(pData->pDest - 1), pData->sbyte | (pData->sbyte<<8));
		else pData->sbyte = *(pData->pDest - 1);
		pData->pDest++;
		len--;
		if (!len) return;
	}
	
	if ((uint32_t)pData->pDest & 1) {
		flSave16bit((uint16_t*)(pData->pDest - 1), pData->sbyte | ((*buf)<<8));
		buf++;
		len--;
		pData->pDest++;
	}
	while (len > 1) {
		flSave16bit((uint16_t*)pData->pDest, *(uint16_t*)buf);
		pData->pDest  += 2;
		buf +=2;
		len -=2;
	}
	if (len) {
		pData->sbyte = *buf;
		pData->pDest++;		
	}
}

/* build the fixed huffman trees */
static void tinf_build_fixed_trees(TINF_TREE *lt, TINF_TREE *dt)
{
   uint8_t i;

   /* build fixed length tree */
   for (i = 0; i < 7; ++i) lt->table[i] = 0;

   lt->table[7] = 24;
   lt->table[8] = 152;
   lt->table[9] = 112;

   for (i = 0; i < 24; ++i) lt->trans[i] = 256 + i;
   for (i = 0; i < 144; ++i) lt->trans[24 + i] = i;
   for (i = 0; i < 8; ++i) lt->trans[24 + 144 + i] = 280 + i;
   for (i = 0; i < 112; ++i) lt->trans[24 + 144 + 8 + i] = 144 + i;

   /* build fixed distance tree */
   for (i = 0; i < 5; ++i) dt->table[i] = 0;

   dt->table[5] = 32;

   for (i = 0; i < 32; ++i) dt->trans[i] = i;
}

/* given an array of code lengths, build a tree */
static void tinf_build_tree(TINF_TREE *t, const uint8_t *lengths, uint16_t num)
{
   uint16_t offs[16], i;																		// 36 bytes
   uint16_t sum;

   /* clear code length count table */
   for (i = 0; i < 16; ++i) t->table[i] = 0;

   /* scan symbol lengths, and sum code length counts */
   for (i = 0; i < num; ++i) t->table[lengths[i]]++;

   t->table[0] = 0;

   /* compute offset table for distribution sort */
   for (sum = 0, i = 0; i < 16; ++i)
   {
      offs[i] = sum;
      sum += t->table[i];
   }

   /* create code->symbol translation table (symbols sorted by code) */
   for (i = 0; i < num; ++i) if (lengths[i]) t->trans[offs[lengths[i]]++] = i;
   
}

/* ---------------------- *
 * -- decode functions -- *
 * ---------------------- */

/* read a num bit value from a stream */
static uint16_t tinf_getbits(uint8_t num)
{
   uint16_t val;
	
	if (num <= pData->bitcount) {
		val = pData->tag & ~(0xFF<<num);
		pData->bitcount -= num;
		pData->tag >>= num;
	}
	else {
		val = pData->tag;
		num -= pData->bitcount;
		val |= (*(uint16_t*)(pData->pSource) & ~(0xFFFF<<num))<<pData->bitcount;

		pData->bitcount = (num + 7)>>3;
		pData->pSource += pData->bitcount;
		pData->pSrcIndex += pData->bitcount;
		
		pData->bitcount = ((num - 1) & 7) + 1;
		pData->tag = *(pData->pSource - 1) >> pData->bitcount;				
		pData->bitcount = 8 - pData->bitcount;
	}

	return val;
}

/* given a data stream and a tree, decode a symbol */
static uint16_t tinf_decode_symbol(TINF_TREE *t)
{
   int32_t sum, cur, len;

	sum = 0;cur = 0;len = 0;
   while (cur >= 0) {		/* get more bits while code value is above sum */
		cur = (cur<<1) | tinf_getbits(1);
      len++;
      sum += t->table[len];
      cur -= t->table[len];
   } 

   return t->trans[sum + cur];
}

/* given a data stream, decode dynamic trees from it */
static void tinf_decode_trees(TINF_TREE *lt, TINF_TREE *dt)
{
   uint16_t hlit, hdist, hclen, i, num;
	uint8_t prev;

	i = tinf_getbits(14);
   /* get 5 bits HLIT (257-286) */
   hlit = 257 + (i & 0x1F);
	i >>= 5;
   /* get 5 bits HDIST (1-32) */
   hdist = 1 + (i & 0x1F);
	i >>= 5;
   /* get 4 bits HCLEN (4-19) */
   hclen = 4 + i;

   for (i = 0; i < 19; ++i) pData->pLengths[i] = 0;

   /* read code lengths for code length alphabet */
   for (i = 0; i < hclen; ++i) pData->pLengths[clcidx[i]] = tinf_getbits(3);		/* get 3 bits code length (0-7) */
     
   /* build code length tree */
   tinf_build_tree(lt, pData->pLengths, 19);

   /* decode code lengths for the dynamic trees */
   for (num = 0; num < hlit + hdist; ) {
      i = tinf_decode_symbol(lt);

      switch (i) {
			case 16:	/* copy previous code length 3-6 times (read 2 bits) */
				prev = pData->pLengths[num - 1];
				for (i = 3 + tinf_getbits(2); i > 0; --i) pData->pLengths[num++] = prev;
				break;
			case 17:	/* repeat code length 0 for 3-10 times (read 3 bits) */
				for (i = 3 + tinf_getbits(3); i > 0; --i) pData->pLengths[num++] = 0;
				break;
			case 18:	/* repeat code length 0 for 11-138 times (read 7 bits) */
				for (i = 11 + tinf_getbits(7); i > 0; --i) pData->pLengths[num++] = 0;
				break;
			default:	 /* values 0-15 represent the actual code lengths */
				pData->pLengths[num++] = i;
				break;
      }
   }
   /* build dynamic trees */
   tinf_build_tree(lt, pData->pLengths, hlit);
   tinf_build_tree(dt, pData->pLengths + hlit, hdist);
}

/* ----------------------------- *
 * -- block inflate functions -- *
 * ----------------------------- */

/* given a stream and two trees, inflate a block of data */
static void tinf_inflate_block_data(TINF_TREE *lt, TINF_TREE *dt)
{
	uint16_t sym, length;

   while (1) {
      sym = tinf_decode_symbol(lt);
      if (sym == 256) return;											/* end of block */
      if (sym < 256) tinf_save_to_dest((uint8_t*)&sym, 1);	/* next unpacked byte */
		else {																/* copy match from already unpacked data */
			sym -= 257;
			length = length_base_const[sym] + tinf_getbits(length_bits_const[sym]);	/* possibly get more bits from length code */
			sym = tinf_decode_symbol(dt);
			sym = dist_base_const[sym] + tinf_getbits(dist_bits_const[sym]);		/* possibly get more bits from distance code */
			
			tinf_save_to_dest(pData->pDest - sym, length);
      }
   }
}

/* inflate an uncompressed block of data */
static T_TINF_RES tinf_inflate_uncompressed_block()
{
	uint16_t length, invlength;

	length = *(uint16_t*)(pData->pSource);	 /* get length */
	pData->pSource += 2;
	invlength = *(uint16_t*)(pData->pSource);	/* get one's complement of length */
	pData->pSource += 2;
	pData->pSrcIndex += 4;

	/* check length */
	if (length != (~invlength & 0xffff)) return TINF_RES_ERR_LENGTH_OF_UNCOMPRESSED_BLOCK_DOESNT_MATCH;
	
	/* copy block */		
   tinf_save_to_dest(pData->pSource, length);
	pData->pSource += length;
	pData->pSrcIndex += length;
	
   /* make sure we start next block on a byte boundary */
   pData->bitcount = 0;

   return TINF_RES_OK;
}

/* ---------------------- *
 * -- public functions -- *
 * ---------------------- */

uint8_t tinf_uncompress()
{
   uint8_t btype;
	T_TINF_RES res;
		
	pData->tag = 0;
	pData->bitcount = 0;
	
	pData->pDestIndex = 0;
	pData->pSrcIndex = 0;
	
   res = TINF_RES_OK;
	
	do {
      btype = tinf_getbits(3);	/* read final & block type (1 + 2 bits) */ 
      switch (btype>>1)	/* decompress block */
      {
         case 0:	/* decompress uncompressed block */
            res = tinf_inflate_uncompressed_block();
            break;
         case 1:	/* decompress block with fixed huffman trees */
            tinf_build_fixed_trees(&pData->ltree, &pData->dtree);	/* build fixed huffman trees */
  				tinf_inflate_block_data(&pData->ltree, &pData->dtree);	/* decode block using fixed trees */
            break;
         case 2:	/* decompress block with dynamic huffman trees */
				tinf_decode_trees(&pData->ltree, &pData->dtree);	/* decode trees from stream */
				tinf_inflate_block_data(&pData->ltree, &pData->dtree);	/* decode block using decoded trees */
            break;
         default:
            res = TINF_RES_ERR_DATA_ERROR;
            break;
      }

      if (res != TINF_RES_OK) return res;

   } while (!(btype & 1));
	
		// All data is unpacked. Now we should to call pSaveUnpackedBuf last time
   if ((res == TINF_RES_OK) && ((uint32_t)pData->pDest & 1))  tinf_save_to_dest(&btype, 1);
      
	return res;
}

