/*
 *   MIRACL compiler/hardware definitions - mirdef.h
 *   Copyright (c) 1988-1997 Shamus Software Ltd.
 *
 *   For typical 32-bit C-Only MIRACL library
 */


#define MIRACL 32
#define MR_LITTLE_ENDIAN      

/* or for most non-Intel processors
#define MR_BIG_ENDIAN      
*/

#define mr_utype int
#define MR_IBITS 32
#define MR_LBITS 64
#define mr_unsign32 unsigned int
#define mr_dltype long long

/* or for gcc and most Unix
#define mr_dltype long long
*/

#define MR_NOASM
#define MR_FLASH 52

#define MAXBASE ((mr_small)1<<(MIRACL-1))

