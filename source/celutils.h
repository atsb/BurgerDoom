#ifndef DOOM3DO_CELUTILS_H
#define DOOM3DO_CELUTILS_H

#include <stdint.h>

/*
 * Serialized 3DO Cel Control Block.
 *
 * This is the exact 60-byte layout Rebecca used in threedo.c.  In particular,
 * the CCB does NOT contain host pointers or width/height fields.  All fields
 * in a resource CEL are big-endian 32-bit values.
 *
 * Host code must never treat sourcePtr/plutPtr/nextPtr as native pointers;
 * they are serialized 32-bit values/offsets until explicitly resolved.
 */
typedef struct D3DO_CCB {
    uint32_t flags;       /* 00 */
    uint32_t nextPtr;     /* 04 */
    uint32_t sourcePtr;   /* 08 */
    uint32_t plutPtr;     /* 0C */
    int32_t  xPos;        /* 10, 16.16 */
    int32_t  yPos;        /* 14, 16.16 */
    int32_t  hdx;         /* 18, 12.20 */
    int32_t  hdy;         /* 1C, 12.20 */
    int32_t  vdx;         /* 20, 16.16 */
    int32_t  vdy;         /* 24, 16.16 */
    int32_t  hddx;        /* 28, 12.20 */
    int32_t  hddy;        /* 2C, 12.20 */
    uint32_t pixc;        /* 30 */
    uint32_t pre0;        /* 34 */
    uint32_t pre1;        /* 38 */
} D3DO_CCB;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(D3DO_CCB) == 60, "D3DO_CCB must remain 60 bytes");
#endif

uint16_t D3DO_CelWidth(const D3DO_CCB *ccb);
uint16_t D3DO_CelHeight(const D3DO_CCB *ccb);

/* Decode a serialized CEL into host-native ARGB1555 pixels. */
int D3DO_DecodeCelResolved(const uint8_t *source, uint32_t sourceSize,
                           const uint8_t *plutBytes,
                           uint32_t pre0, uint32_t pre1, uint32_t ccbFlags,
                           int transparentZero,
                           uint16_t **out, uint16_t *w, uint16_t *h);

int D3DO_DecodeCel(const uint8_t *data, uint32_t size,
                   uint16_t **pixels, uint16_t *width, uint16_t *height);

#endif
