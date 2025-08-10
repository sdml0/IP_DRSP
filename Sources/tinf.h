#ifndef TINF_H_INCLUDED
#define TINF_H_INCLUDED

    typedef enum E_TINF_RES {
        TINF_RES_OK,

        TINF_RES_ERR_NOT_GZIP_FORMAT,
        TINF_RES_ERR_NOT_DEFLATE_METHOD,
        TINF_RES_ERR_GZIP_RES_FLAGS_NOT_ZERO,
        TINF_RES_ERR_GZIP_UNPACKED_DATA_LENGTH_NOT_MATCH,
        TINF_RES_ERR_GZIP_CRC32_NOT_MATCH,
        TINF_RES_ERR_SRC_BUF_IS_NULL,
        TINF_RES_ERR_SRC_BUF_LEN_IS_ZERO,
        TINF_RES_ERR_NOT_ALL_DEST_FUNC_DEFINED,
        TINF_RES_ERR_LENGTH_OF_UNCOMPRESSED_BLOCK_DOESNT_MATCH,
        TINF_RES_ERR_DEST_BUF_LEN_IS_ZERO,
        TINF_RES_ERR_DEST_BUF_IS_NULL,
        TINF_RES_ERR_NO_SPACE_IN_DEST_BUF,
        TINF_RES_ERR_OUT_OF_SOURCE_BUF,
        
        TINF_RES_ERR_CALLBACK_RETURNED_FALSE,
        TINF_RES_ERR_SOURCE_BUF_GET_RETURNED_FALSE,
        TINF_RES_ERR_MEMCPY_FROM_UNPACKED_RETURNED_FALSE,


        TINF_RES_ERR_DATA_ERROR,

    } T_TINF_RES;


	/*
	 * 608 bytes
	 */
	typedef struct {
		uint16_t table[16];  /* table of code length counts      */
		uint16_t trans[288]; /* code -> symbol translation table */
	} TINF_TREE;

	/*
	 * 1552 bytes
	 */
	typedef struct {
		uint8_t tag;
		uint8_t bitcount;
		uint8_t sbyte;

		uint8_t *pSource;
		uint8_t *pDest;	
		uint16_t pSrcIndex; 
		uint16_t pDestIndex;
		
		uint8_t pLengths[288 + 32];
		TINF_TREE ltree;                            /* dynamic length/symbol tree                    */
		TINF_TREE dtree;                            /* dynamic distance tree                         */

	} TINF_DATA;

    /* function prototypes */


    uint8_t tinf_uncompress(void);


#endif /* TINF_H_INCLUDED */
