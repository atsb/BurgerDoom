#include "Doom.h"
#include <String.h>
#include <IntMath.h>
#include <stdio.h>

#define STRETCH(WIDTH,HEIGHT) (Fixed)((160.0/(float)WIDTH)*((float)HEIGHT/180.0)*2.2*65536)		

typedef struct {		/* Actual structure of TEXTURE1 */
	Word Count;			/* Count of entries */
	Word First;			/* Starting resource # */
	Word FlatCount;		/* Count of flats */
	Word FirstFlat;		/* Starting resource # for flats */
	texture_t Array[1];	/* Array of entries[Count] */
} Filemaptexture_t;

static Word ScreenWidths[6] = {280,256,224,192,160,128};
static Word ScreenHeights[6] = {160,144,128,112,96,80};
static Fixed Stretchs[6] = {
	STRETCH(280,160),
	STRETCH(256,144),
	STRETCH(224,128),
	STRETCH(192,112),
	STRETCH(160, 96),
	STRETCH(128, 80)
};
Word NumTextures;		/* Number of textures in the game */
Word FirstTexture;		/* First texture resource */
Word NumFlats;			/* Number of flats in the game */
Word FirstFlat;			/* Resource number to first flat texture */
texture_t *TextureInfo;	/* Array describing textures */
void ***FlatInfo;		/* Array describing flats */
texture_t **TextureTranslation;	/* Indexs to textures for global animation */
void ***FlatTranslation;		/* Indexs to textures for global animation */
texture_t *SkyTexture;		/* Pointer to the sky texture */

/**********************************

	Load in the "TextureInfo" array so that the game knows
	all about the wall and sky textures (Width,Height).
	Also initialize the texture translation table for wall animations.
	Called on powerup only.

**********************************/


static uint32_t D3DO_ReadBE32_RDATA(const Byte *p)
{
	return ((uint32_t)p[0] << 24) |
	       ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) |
	       (uint32_t)p[3];
}

void R_InitData(void)
{
	Word i;
	Byte *raw;
	uint32_t textureCount;
	uint32_t firstTextureNum;
	uint32_t flatCount;
	uint32_t firstFlatNum;

	raw = (Byte *)LoadAResource(rTEXTURE1);
	if (!raw)
		return;

	/*
	 * TEXTURE1 serialized header (big-endian):
	 *   u32 wall texture count
	 *   u32 first wall texture resource number
	 *   u32 flat texture count
	 *   u32 first flat texture resource number
	 */
	textureCount   = D3DO_ReadBE32_RDATA(raw + 0);
	firstTextureNum = D3DO_ReadBE32_RDATA(raw + 4);
	flatCount      = D3DO_ReadBE32_RDATA(raw + 8);
	firstFlatNum   = D3DO_ReadBE32_RDATA(raw + 12);
	raw += 16;

	if (textureCount > 0xFFFFu) textureCount = 0xFFFFu;
	if (flatCount > 0xFFFFu) flatCount = 0xFFFFu;
	NumTextures = (Word)textureCount;
	FirstTexture = (Word)firstTextureNum;
	NumFlats = (Word)flatCount;
	FirstFlat = (Word)firstFlatNum;

	TextureInfo = (texture_t *)AllocAPointer(
		(size_t)textureCount * sizeof(*TextureInfo));

	/*
	 * Each serialized texture entry is 12 bytes:
	 *   u32 width
	 *   u32 height
	 *   u32 unused
	 *
	 * Only the low host-width values are required by Rebecca's renderer.
	 */
	for (i = 0; i < (Word)textureCount; ++i, raw += 12) {
		uint32_t width = D3DO_ReadBE32_RDATA(raw + 0);
		uint32_t height = D3DO_ReadBE32_RDATA(raw + 4);

		TextureInfo[i].width = (Word)width;
		TextureInfo[i].height = (Word)height;
		TextureInfo[i].data = NULL;
	}

	ReleaseAResource(rTEXTURE1);

	TextureTranslation = (texture_t **)AllocAPointer(
		(size_t)textureCount * sizeof(*TextureTranslation));
	for (i = 0; i < (Word)textureCount; ++i)
		TextureTranslation[i] = &TextureInfo[i];

	FlatInfo = (void ***)AllocAPointer(
		(size_t)flatCount * sizeof(*FlatInfo));
	memset(FlatInfo, 0, (size_t)flatCount * sizeof(*FlatInfo));

	FlatTranslation = (void ***)AllocAPointer(
		(size_t)flatCount * sizeof(*FlatTranslation));
	memset(FlatTranslation, 0, (size_t)flatCount * sizeof(*FlatTranslation));

	IDivTable[0] = (Word)-1;
	i = 1;
	do {
		IDivTable[i] =
			IMFixDiv(512 << FRACBITS, i << FRACBITS);
	} while (++i < (sizeof(IDivTable) / sizeof(Word)));

	InitMathTables();
}

/**********************************

	Create all the math tables for the current screen size

**********************************/

void InitMathTables(void)
{
	Fixed j;
	Word i;
	
	ScreenWidth = ScreenWidths[ScreenSize];
	ScreenHeight = ScreenHeights[ScreenSize];
	CenterX = (ScreenWidth/2);
	CenterY = (ScreenHeight/2);
	ScreenXOffset = ((320-ScreenWidth)/2);
	ScreenYOffset = ((160-ScreenHeight)/2);
	GunXScale = (ScreenWidth*0x100000)/320;		/* Get the 3DO scale factor for the gun shape */
	GunYScale = (ScreenHeight*0x10000)/160;		/* And the y scale */

	Stretch = Stretchs[ScreenSize];
	StretchWidth = Stretch*((int)ScreenWidth/2);

	/* Create the viewangletox table */
	
	j = IMFixDiv(CenterX<<FRACBITS,finetangent[FINEANGLES/4+FIELDOFVIEW/2]);
	i = 0;
	do {
		Fixed t;
		if (finetangent[i]>FRACUNIT*2) {
			t = -1;
		} else if (finetangent[i]< -FRACUNIT*2) {
			t = ScreenWidth+1;
		} else {
			t = IMFixMul(finetangent[i],j);
			t = ((CenterX<<FRACBITS)-t+FRACUNIT-1)>>FRACBITS;
			if (t<-1) {
				t = -1;
			} else if (t>(int)ScreenWidth+1) {
				t = ScreenWidth+1;
			}
		}	
		viewangletox[i/2] = t;
		i+=2;
	} while (i<FINEANGLES/2);

	/* Using the viewangletox, create xtoviewangle table */
		
	i = 0;
	do {
		Word x;
		x = 0;
		while (viewangletox[x]>(int)i) {
			++x;
		}
		xtoviewangle[i] = (x<<(ANGLETOFINESHIFT+1))-ANG90;
	} while (++i<ScreenWidth+1);
	
	/* Set the minimums and maximums for viewangletox */
	i = 0;
	do {
		if (viewangletox[i]==-1) {
			viewangletox[i] = 0;
		} else if (viewangletox[i] == ScreenWidth+1) {
			viewangletox[i] = ScreenWidth;
		}
	} while (++i<FINEANGLES/4);
	
	/* Make the yslope table for floor and ceiling textures */
	
	i = 0;
	do {
		j = (((int)i-(int)ScreenHeight/2)*FRACUNIT)+FRACUNIT/2;
		j = IMFixDiv(StretchWidth,abs(j));
		j >>= 6;
		if (j>0xFFFF) {
			j = 0xFFFF;
		}
		yslope[i] = j;
	} while (++i<ScreenHeight);
	
	/* Create the distance scale table for floor and ceiling textures */
	
	i = 0;
	do {
		j = abs(finecosine[xtoviewangle[i]>>ANGLETOFINESHIFT]);
		distscale[i] = IMFixDiv(FRACUNIT,j)>>1;
	} while (++i<ScreenWidth);

	/* Create the lighting tables */
	
	i = 0;
	do {
		Fixed Range;
		j = i/3;
		lightmins[i] = j;	/* Save the light minimum factors */
		Range = i-j;
		lightsubs[i] = ((Fixed)ScreenWidth*Range)/(800-(Fixed)ScreenWidth);
		lightcoefs[i] = (Range<<16)/(800-(Fixed)ScreenWidth);
		planelightcoef[i] = Range*(0x140000/(800-(Fixed)ScreenWidth));
	} while (++i<256);
}
