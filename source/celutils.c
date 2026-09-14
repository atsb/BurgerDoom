#include "celutils.h"

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static uint16_t be16(const uint8_t *bytes){
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t be32(const uint8_t *bytes){
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t bitPos;
} BitReader;


static uint32_t read_bits(BitReader *reader,unsigned bitCount,int *success){
    uint32_t v = 0;
    unsigned i;
    if(bitCount > 32 || reader->bitPos + bitCount > reader->size * 8u){
        *success = 0;
        return 0;
    }
    for(i = 0; i < bitCount; ++i){
        uint32_t p = reader->bitPos++;
        v = (v << 1) | ((reader->data[p >> 3] >> (7u - (p & 7u))) & 1u);
    }
    return v;
}

static void align64(BitReader *reader){
    reader->bitPos = (reader->bitPos + 63u) & ~63u;
}

static uint8_t ccb_bpp(const uint8_t *ccbData){
    switch(be32(ccbData + 52u) & 7u){
        case 1: return 1;
        case 2: return 2;
        case 3: return 4;
        case 4: return 6;
        case 5: return 8;
        case 6: return 16;
        default: return 0;
    }
}

static uint16_t ccb_width(const uint8_t *ccbData){
    return (uint16_t)((be32(ccbData + 56u) & 0x07FFu) + 1u);
}

static uint16_t ccb_height(const uint8_t *ccbData){
    return (uint16_t)(((be32(ccbData + 52u) >> 6) & 0x03FFu) + 1u);
}

uint16_t D3DO_CelWidth(const D3DO_CCB *ccb){
    return ccb ? ccb_width((const uint8_t *)ccb) : 0;
}

uint16_t D3DO_CelHeight(const D3DO_CCB *ccb){
    return ccb ? ccb_height((const uint8_t *)ccb) : 0;
}

static int decode_unpacked(
    const uint8_t *image,uint32_t imageSize,const uint16_t *plut,uint16_t width,uint16_t height,uint8_t bpp,int indexed,uint16_t *out,uint32_t initialPixelSkip,int transparentZero){
    BitReader reader = { image,imageSize,initialPixelSkip * (uint32_t)bpp };
    uint32_t rowBits = (uint32_t)bpp * width;
    uint32_t alignedBits = (rowBits + 63u) & ~63u;
    uint32_t alignedBytes = alignedBits / 8u;
    int doAlign = (initialPixelSkip == 0u &&
                   (uint64_t)alignedBytes * height <= imageSize);

    for(uint16_t row = 0; row < height; ++row){
        if(doAlign) align64(&reader);
        for(uint16_t column = 0; column < width; ++column){
            int success = 1;
            uint16_t pixelColor;
            uint32_t value = read_bits(&reader,bpp,&success);
            if(!success) return 0;
            if(indexed){
                if(!plut || value >= 32u) return 0;
                pixelColor = (transparentZero && value == 0u) ? 0u : (uint16_t)(plut[value] | 0x8000u);
            } else {
                if(bpp != 16) return 0;
                pixelColor = (uint16_t)(value | 0x8000u);
            }
            out[(size_t)row*width+column] = pixelColor;
        }
    }
    return 1;
}


static int decode_packed(
    const uint8_t *image,uint32_t imageSize,const uint16_t *plut,uint16_t width,uint16_t height,uint8_t bpp,int indexed,uint16_t *out,int transparentZero){
    const uint8_t *lineData = image;

    if(!image || !imageSize || !out || !width || !height)
        return 0;

    for(uint16_t y = 0; y < height; ++y){
        uint32_t remainingBytes = (uint32_t)(lineData - image);
        uint32_t lineBytes;
        uint32_t headerBytes;
        BitReader headerReader;
        BitReader reader;
        int success = 1;
        uint16_t column = 0;

        if(remainingBytes >= imageSize)
            return 0;

        
        headerReader.data = lineData;
        headerReader.size = imageSize - remainingBytes;
        headerReader.bitPos = 0;

        if(bpp >= 8u)
            lineBytes = ((uint32_t)read_bits(&headerReader,16,&success) + 2u) * 4u;
        else
            lineBytes = ((uint32_t)read_bits(&headerReader,8,&success) + 2u) * 4u;

        if(!success || lineBytes < (bpp >= 8u ? 8u : 8u))
            return 0;
        if(lineBytes > imageSize - remainingBytes)
            return 0;

        headerBytes = (bpp >= 8u) ? 2u : 1u;
        reader.data = lineData;
        reader.size = lineBytes;
        reader.bitPos = headerBytes * 8u;

        while(column < width){
            uint32_t packMode;
            uint32_t pixelCount;

            packMode = read_bits(&reader,2,&success);
            if(!success)
                return 0;

            
            if(packMode == 0u)
                break;

            pixelCount = read_bits(&reader,6,&success) + 1u;
            if(!success || pixelCount > (uint32_t)(width - column))
                return 0;

            if(packMode == 2u){
                
                column = (uint16_t)(column + pixelCount);
                continue;
            }

            if(packMode == 3u){
                
                uint32_t value = read_bits(&reader,bpp,&success);
                if(!success)
                    return 0;

                {
                    uint16_t pixelColor;
                    if(indexed){
                        if(!plut || value >= 32u)
                            return 0;
                        pixelColor = (transparentZero && value == 0u) ? 0u : (uint16_t)(plut[value] | 0x8000u);
                    } else {
                        if(bpp != 16u)
                            return 0;
                        pixelColor = (uint16_t)(value | 0x8000u);
                    }
                    for(uint32_t repeatIndex = 0; repeatIndex < pixelCount; ++repeatIndex)
                        out[(size_t)y * width + column++] = pixelColor;
                }
                continue;
            }

            if(packMode == 1u){
                
                for(uint32_t repeatIndex = 0; repeatIndex < pixelCount; ++repeatIndex){
                    uint32_t value = read_bits(&reader,bpp,&success);
                    uint16_t pixelColor;
                    if(!success)
                        return 0;

                    if(indexed){
                        if(!plut || value >= 32u)
                            return 0;
                        pixelColor = (transparentZero && value == 0u) ? 0u : (uint16_t)(plut[value] | 0x8000u);
                    } else {
                        if(bpp != 16u)
                            return 0;
                        pixelColor = (uint16_t)(value | 0x8000u);
                    }
                    out[(size_t)y * width + column++] = pixelColor;
                }
                continue;
            }

            return 0;
        }

        lineData += lineBytes;
    }

    return 1;
}


int D3DO_DecodeCelResolved(const uint8_t *source,uint32_t sourceSize,const uint8_t *plutBytes,uint32_t pre0,uint32_t pre1,uint32_t ccbFlags,int transparentZero,uint16_t **out,uint16_t *outWidth,uint16_t *outHeight){
    uint16_t width = (uint16_t)((pre1 & 0x07FFu) + 1u);
    uint16_t height = (uint16_t)(((pre0 >> 6) & 0x03FFu) + 1u);
    uint8_t bitsPerPixel = 0;
    int indexedColors;
    const uint16_t *palette = (const uint16_t *)plutBytes;
    uint16_t *decodedPixels;

    if(!source || !sourceSize || !out || !outWidth || !outHeight || !width || !height)
        return 0;

    switch(pre0 & 7u){
        case 1: bitsPerPixel=1; break;
        case 2: bitsPerPixel=2; break;
        case 3: bitsPerPixel=4; break;
        case 4: bitsPerPixel=6; break;
        case 5: bitsPerPixel=8; break;
        case 6: bitsPerPixel=16; break;
        default: return 0;
    }

    if(width > 2048u || height > 1024u)
        return 0;

    {
        uint32_t initialPixelSkip = (pre0 >> 24) & 7u;
        if(initialPixelSkip && bitsPerPixel == 4u && height == 1u){
            if(width <= initialPixelSkip) return 0;
            width = (uint16_t)(width - initialPixelSkip);
        }
    }

    indexedColors = (bitsPerPixel != 16u);
    if(indexedColors && !palette)
        return 0;

    decodedPixels=(uint16_t *)calloc((size_t)width*height,sizeof(*decodedPixels));
    if(!decodedPixels)
        return 0;

    if(ccbFlags & 0x00000200u){
        if(!decode_packed(source,sourceSize,palette,width,height,bitsPerPixel,indexedColors,decodedPixels,transparentZero)){
            free(decodedPixels);
            return 0;
        }
    } else {
        {
            uint32_t initialPixelSkip = (pre0 >> 24) & 7u;
            if(!decode_unpacked(source,sourceSize,palette,width,height,bitsPerPixel,indexedColors,decodedPixels,initialPixelSkip,transparentZero)){
                free(decodedPixels);
                return 0;
            }
        }
    }

    *out=decodedPixels; *outWidth=width; *outHeight=height;
    return 1;
}

int D3DO_DecodeCel(const uint8_t *data,uint32_t size,uint16_t **out,uint16_t *w,uint16_t *h){
    uint32_t ccbFlags,sourceOffset,plutOffset;
    const uint8_t *sourceData,*plutData;

    if(!data || size < 60u || !out || !w || !h)
        return 0;

    ccbFlags=be32(data+0u);
    sourceOffset=be32(data+8u);
    plutOffset=be32(data+12u);

    if(ccbFlags & 0x10000000u)
        sourceData=(sourceOffset<size)?data+sourceOffset:NULL;
    else
        sourceData=(12u+sourceOffset<size)?data+12u+sourceOffset:NULL;

    if(ccbFlags & 0x08000000u)
        plutData=(plutOffset<size)?data+plutOffset:NULL;
    else
        plutData=(16u+plutOffset<size)?data+16u+plutOffset:NULL;

    if(!sourceData)
        return 0;

    return D3DO_DecodeCelResolved(
        sourceData,size-(uint32_t)(sourceData-data),plutData,be32(data+52u),be32(data+56u),ccbFlags,0,out,w,h);
}
