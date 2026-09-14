#include "Doom.h"
#include <IntMath.h>
#include <String.h>
#include <stdlib.h>
#include <stdio.h>

static uint16_t D3DO_ReadBE16(const Byte *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t D3DO_ReadBE32(const Byte *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static node_t *D3DO_NodesBuffer;
static Word D3DO_NumSubsectors;
static uint32_t D3DO_NumVertexes;
static line_t **D3DO_BlockMapLineStorage;

/* lump order in a map wad */
enum {
	ML_THINGS,ML_LINEDEFS,ML_SIDEDEFS,ML_VERTEXES,ML_SEGS,
	ML_SSECTORS,ML_SECTORS,ML_NODES,ML_REJECT,ML_BLOCKMAP,
	ML_TOTAL
};

static vertex_t *vertexes;	/* Only needed during load, then discarded before game play */
static Word LoadedLevel;	/* Resource number of the loaded level */
static Word numsides;	/* Number of sides loaded */
static side_t *sides;		/* Pointer to array of loaded sides */
static line_t **LineArrayBuffer;	/* Pointer to array of line_t pointers used by sectors */
static Word PreLoadTable[] = {
	rSPR_ZOMBIE,			/* Zombiemen */
	rSPR_SHOTGUY,			/* Shotgun guys */
	rSPR_IMP,				/* Imps */
	rSPR_DEMON,				/* Demons */
	rSPR_CACODEMON,			/* Cacodemons */
	rSPR_LOSTSOUL,			/* Lost souls */
	rSPR_BARON,				/* Baron of Hell */
	rSPR_OURHEROBDY,		/* Our dead hero */
	rSPR_BARREL,			/* Exploding barrel */
	rSPR_SHOTGUN,			/* Shotgun on floor */
	rSPR_CLIP,				/* Clip of bullets */
	rSPR_SHELLS,			/* 4 shotgun shells */
	rSPR_STIMPACK,			/* Stimpack */
	rSPR_MEDIKIT,			/* Med-kit */
	rSPR_GREENARMOR,		/* Normal armor */
	rSPR_BLUEARMOR,			/* Mega armor */
	rSPR_HEALTHBONUS,		/* Health bonus */
	rSPR_ARMORBONUS,		/* Armor bonus */
	rSPR_BLUD,				/* Blood from bullet hit */
	rSPR_PUFF,				/* Gun sparks on wall */
	-1
};

seg_t *segs;				/* Pointer to array of loaded segs */
Word numsectors;			/* Number of sectors loaded */
sector_t *sectors;			/* Pointer to array of loaded sectors */
subsector_t *subsectors;	/* Pointer to array of loaded subsectors */
node_t *FirstBSPNode;	/* First BSP entry */
Word numlines;		/* Number of lines loaded */
line_t *lines;		/* Pointer to array of loaded lines */
line_t ***BlockMapLines;	/* Pointer to line lists based on blockmap */
Word BlockMapWidth,BlockMapHeight;	/* Size of blockmap in blocks */
Fixed BlockMapOrgX,BlockMapOrgY;	/* Origin of block map */
mobj_t **BlockLinkPtr;		/* Starting link for thing chains */
Byte *RejectMatrix;			/* For fast sight rejection */
mapthing_t deathmatchstarts[10],*deathmatch_p;	/* Deathmatch starts */
mapthing_t playerstarts;	/* Starting position for players */

/**********************************

	Load the sector data, this is loaded in first since it
	doesn't require the presence of any other data type.

**********************************/

typedef	struct {		/* Map sector loaded from disk */
	Fixed floorheight;	/* Floor and ceiling height */
	Fixed ceilingheight;
	Word floorpic;		/* Floor image */
	Word ceilingpic;	/* Ceiling image */
	Word lightlevel;	/* Light level */
	Word special;		/* Special flags */
	Word tag;			/* Tag ID */
} mapsector_t;

static void LoadSectors(Word lump)
{
	Byte *raw = (Byte *)LoadAResource(lump);
	sector_t *ss;
	uint32_t count, i;

	if (!raw)
		return;

	count = D3DO_ReadBE32(raw);
	numsectors = (Word)count;

	ss = (sector_t *)AllocAPointer((size_t)count * sizeof(*ss));
	memset(ss, 0, (size_t)count * sizeof(*ss));
	sectors = ss;
	raw += 4;

	for (i = 0; i < count; ++i, ++ss, raw += 28) {
		ss->floorheight = (Fixed)(int32_t)D3DO_ReadBE32(raw + 0);
		ss->ceilingheight = (Fixed)(int32_t)D3DO_ReadBE32(raw + 4);
		ss->FloorPic = (Word)D3DO_ReadBE32(raw + 8);
		ss->CeilingPic = (Word)D3DO_ReadBE32(raw + 12);
		ss->lightlevel = (Word)D3DO_ReadBE32(raw + 16);
		ss->special = (Word)D3DO_ReadBE32(raw + 20);
		ss->tag = (Word)D3DO_ReadBE32(raw + 24);
	}

	KillAResource(lump);
}




/**********************************

	Load in the wall side definitions
	Requires that the sectors are loaded.

**********************************/

typedef struct {		/* Map sidedef loaded from disk */
	Fixed textureoffset;
	Fixed rowoffset;
	Word toptexture,bottomtexture,midtexture;
	Word sector;		/* on viewer's side */
} mapsidedef_t;

static void LoadSideDefs(Word lump)
{
	Byte *raw = (Byte *)LoadAResource(lump);
	side_t *sd;
	uint32_t count, i;

	if (!raw)
		return;

	count = D3DO_ReadBE32(raw);
	numsides = (Word)count;

	sd = (side_t *)AllocAPointer((size_t)count * sizeof(*sd));
	memset(sd, 0, (size_t)count * sizeof(*sd));
	sides = sd;
	raw += 4;

	for (i = 0; i < count; ++i, ++sd, raw += 24) {
		sd->textureoffset = (Fixed)(int32_t)D3DO_ReadBE32(raw + 0);
		sd->rowoffset = (Fixed)(int32_t)D3DO_ReadBE32(raw + 4);
		sd->toptexture = (Word)D3DO_ReadBE32(raw + 8);
		sd->bottomtexture = (Word)D3DO_ReadBE32(raw + 12);
		sd->midtexture = (Word)D3DO_ReadBE32(raw + 16);
		{
			uint32_t sector = D3DO_ReadBE32(raw + 20);
			if (sector >= count || sector >= (uint32_t)numsectors)
				sd->sector = NULL;
			else
				sd->sector = &sectors[sector];
		}
	}

	KillAResource(lump);
}




/**********************************

	Load in all the line definitions.
	Requires vertexes,sectors and sides to be loaded.
	I also calculate the line's slope for quick processing by line
	slope comparisons.
	I also calculate the line's bounding box in Fixed pixels.

**********************************/

typedef struct {
	Word v1,v2;		/* Indexes to the vertex table */
	Word flags;		/* Line flags */
	Word special;	/* Special event type */
	Word tag;		/* ID tag for external trigger */
	Word sidenum[2];	/* sidenum[1] will be -1 if one sided */
} maplinedef_t;

static void LoadLineDefs(Word lump)
{
	/* 3DO MapLine is 7 big-endian uint32 fields = 28 bytes. */
	Byte *raw = (Byte *)LoadAResource(lump);
	line_t *ld;
	uint32_t count, i;

	if (!raw)
		return;

	count = D3DO_ReadBE32(raw);
	{
		LongWord resourceSize = GetAResourceSize(lump);
		if (resourceSize < 4 || count > ((uint32_t)resourceSize - 4u) / 28u) {
			fprintf(stderr,
				"invalid LINEDEFS resource %u: count=%u size=%u\n",
				(unsigned)lump, (unsigned)count, (unsigned)resourceSize);
			return;
		}
	}
	numlines = (Word)count;

	ld = (line_t *)AllocAPointer(
		(size_t)count * sizeof(*ld));
	if (!ld) {
		fprintf(stderr, "DOOM3DO: unable to allocate %u line definitions\n",
			(unsigned)count);
		return;
	}
	memset(ld, 0, (size_t)count * sizeof(*ld));
	lines = ld;
	raw += 4;

	for (i = 0; i < count; ++i, ++ld, raw += 28) {
		Fixed dx, dy;
		uint32_t side0, side1;
		uint32_t v1, v2;

		v1 = D3DO_ReadBE32(raw + 0);
		v2 = D3DO_ReadBE32(raw + 4);

		if (v1 >= D3DO_NumVertexes ||
			v2 >= D3DO_NumVertexes) {
			fprintf(stderr,
				"invalid line vertex index: v1=%u v2=%u vertexCount=%u\n",
				(unsigned)v1, (unsigned)v2, (unsigned)D3DO_NumVertexes);
			fflush(stderr);
			abort();
		}

		ld->v1 = vertexes[v1];
		ld->v2 = vertexes[v2];

		ld->flags = (Word)D3DO_ReadBE32(raw + 8);
		ld->special = (Word)D3DO_ReadBE32(raw + 12);
		ld->tag = (Word)D3DO_ReadBE32(raw + 16);

		dx = ld->v2.x - ld->v1.x;
		dy = ld->v2.y - ld->v1.y;

		if (!dx)
			ld->slopetype = ST_VERTICAL;
		else if (!dy)
			ld->slopetype = ST_HORIZONTAL;
		else
			ld->slopetype =
				((dy ^ dx) >= 0) ? ST_POSITIVE : ST_NEGATIVE;

		if (dx >= 0) {
			ld->bbox[BOXLEFT] = ld->v1.x;
			ld->bbox[BOXRIGHT] = ld->v2.x;
		} else {
			ld->bbox[BOXLEFT] = ld->v2.x;
			ld->bbox[BOXRIGHT] = ld->v1.x;
		}

		if (dy >= 0) {
			ld->bbox[BOXBOTTOM] = ld->v1.y;
			ld->bbox[BOXTOP] = ld->v2.y;
		} else {
			ld->bbox[BOXBOTTOM] = ld->v2.y;
			ld->bbox[BOXTOP] = ld->v1.y;
		}

		side0 = D3DO_ReadBE32(raw + 20);
		side1 = D3DO_ReadBE32(raw + 24);

		if (side0 >= (uint32_t)numsides)
			abort();

		ld->SidePtr[0] = &sides[side0];
		ld->frontsector = ld->SidePtr[0]->sector;

		if (side1 != UINT32_MAX) {
			if (side1 >= (uint32_t)numsides)
				abort();

			ld->SidePtr[1] = &sides[side1];
			ld->backsector = ld->SidePtr[1]->sector;
		}
	}

	KillAResource(lump);
}





/**********************************

	Load in the block map
	I need to have the lines preloaded before I can process the blockmap
	The data has 4 longwords at the beginning which will have an array of offsets
	to a series of line #'s. These numbers will be turned into pointers into the line
	array for quick compares to lines.
	
**********************************/

static void LoadBlockMap(Word lump)
{
	void **BlockHandle;
	Byte *raw;
	LongWord size;
	uint32_t entries;
	uint32_t i;
	uint32_t numLineListEntries;
	uint32_t lineListEntriesStartU32Idx;

	BlockHandle = LoadAResourceHandle(lump);
	raw = (Byte *)LockAHandle(BlockHandle);
	size = GetAHandleSize(BlockHandle);

	if (!raw || size < 16)
		return;

	BlockMapOrgX = (Fixed)(int32_t)D3DO_ReadBE32(raw + 0);
	BlockMapOrgY = (Fixed)(int32_t)D3DO_ReadBE32(raw + 4);
	BlockMapWidth = D3DO_ReadBE32(raw + 8);
	BlockMapHeight = D3DO_ReadBE32(raw + 12);
	entries = BlockMapWidth * BlockMapHeight;

	if (entries == 0 || 16u + entries * 4u > size)
		return;

	numLineListEntries =
		(size / 4u) - 4u - entries;
	lineListEntriesStartU32Idx = 4u + entries;

	BlockMapLines = (line_t ***)AllocAPointer(
		(size_t)entries * sizeof(*BlockMapLines));

	D3DO_BlockMapLineStorage = (line_t **)AllocAPointer(
		(size_t)numLineListEntries * sizeof(*D3DO_BlockMapLineStorage));

	{
		uint32_t k;
		uint32_t storage = 0;

		for (i = 0; i < entries; ++i) {
			uint32_t lineListByteOffset =
				D3DO_ReadBE32(raw + 16u + i * 4u);
			uint32_t lineListIdx;

			lineListIdx =
				(lineListByteOffset / 4u) -
				lineListEntriesStartU32Idx;

			if (lineListIdx >= numLineListEntries) {
				BlockMapLines[i] = NULL;
				continue;
			}

			BlockMapLines[i] =
				D3DO_BlockMapLineStorage + lineListIdx;

			(void)k;
		}

		/* Convert the serialized line numbers to native pointers. */
		for (i = 0; i < numLineListEntries; ++i) {
			uint32_t rawWord =
				D3DO_ReadBE32(raw +
					(lineListEntriesStartU32Idx + i) * 4u);

			if (rawWord == UINT32_MAX)
				D3DO_BlockMapLineStorage[i] = NULL;
			else if (rawWord < (uint32_t)numlines)
				D3DO_BlockMapLineStorage[i] = &lines[rawWord];
			else
				D3DO_BlockMapLineStorage[i] = NULL;
		}
	}

	BlockLinkPtr = (mobj_t **)AllocAPointer(
		(size_t)entries * sizeof(*BlockLinkPtr));
	memset(BlockLinkPtr, 0, (size_t)entries * sizeof(*BlockLinkPtr));
}



/**********************************

	Load the line segments structs for rendering
	Requires vertexes,sides and lines to be preloaded

**********************************/

typedef struct {
	Word v1,v2;			/* Index to the vertexs */
	angle_t	angle;		/* Angle of the line segment */
	Fixed offset;		/* Texture offset */
	Word linedef;		/* Line definition */
	Word side;			/* Side of the line segment */
} mapseg_t;

static void LoadSegs(Word lump)
{
	Byte *raw = (Byte *)LoadAResource(lump);
	seg_t *li;
	uint32_t count, i;

	if (!raw)
		return;

	count = D3DO_ReadBE32(raw);
	li = (seg_t *)AllocAPointer((size_t)count * sizeof(*li));
	memset(li, 0, (size_t)count * sizeof(*li));
	segs = li;
	raw += 4;

	for (i = 0; i < count; ++i, ++li, raw += 24) {
		uint32_t v1 = D3DO_ReadBE32(raw + 0);
		uint32_t v2 = D3DO_ReadBE32(raw + 4);
		uint32_t line = D3DO_ReadBE32(raw + 16);
		uint32_t side = D3DO_ReadBE32(raw + 20);

		li->v1 = vertexes[v1];
		li->v2 = vertexes[v2];
		li->angle = (angle_t)D3DO_ReadBE32(raw + 8);
		li->offset = (Fixed)(int32_t)D3DO_ReadBE32(raw + 12);

		li->linedef = &lines[line];
		li->sidedef = li->linedef->SidePtr[side & 1u];
		li->frontsector = li->sidedef->sector;

		if (li->linedef->flags & ML_TWOSIDED)
			li->backsector =
				li->linedef->SidePtr[(side ^ 1u) & 1u]->sector;

		if (li->linedef->v1.x == li->v1.x &&
			li->linedef->v1.y == li->v1.y)
			li->linedef->fineangle =
				li->angle >> ANGLETOFINESHIFT;
	}

	KillAResource(lump);
}




/**********************************

	Load in all the subsectors
	Requires segs, sectors, sides

**********************************/

typedef struct {		/* Loaded map subsectors */
	Word numlines;		/* Number of line segments */
	Word firstline;		/* Segs are stored sequentially */
} mapsubsector_t;

static void LoadSubsectors(Word lump)
{
	Byte *raw = (Byte *)LoadAResource(lump);
	subsector_t *ss;
	uint32_t count, i;

	if (!raw)
		return;

	count = D3DO_ReadBE32(raw);
	D3DO_NumSubsectors = (Word)count;
	ss = (subsector_t *)AllocAPointer((size_t)count * sizeof(*ss));
	memset(ss, 0, (size_t)count * sizeof(*ss));
	subsectors = ss;
	raw += 4;

	for (i = 0; i < count; ++i, ++ss, raw += 8) {
		uint32_t first = D3DO_ReadBE32(raw + 4);
		ss->numsublines = (Word)D3DO_ReadBE32(raw + 0);
		ss->firstline = &segs[first];
		ss->sector = ss->firstline->sidedef->sector;
	}

	KillAResource(lump);
}




/**********************************

	Load in the BSP tree and convert the indexs into pointers
	to either the node list or the subsector array.
	I require that the subsectors are loaded in.

**********************************/

#define	NF_SUBSECTOR 0x8000

typedef struct {
	Fixed x,y,dx,dy;	/* Partition vector */
	Fixed bbox[2][4];	/* Bounding box for each child */
	LongWord children[2];	/* if NF_SUBSECTOR it's a subsector index else node index */
} mapnode_t;

static void LoadNodes(Word lump)
{
	Byte *raw = (Byte *)LoadAResource(lump);
	node_t *nodes;
	uint32_t count, i, j;

	if (!raw)
		return;

	count = D3DO_ReadBE32(raw);
	nodes = (node_t *)AllocAPointer((size_t)count * sizeof(*nodes));
	memset(nodes, 0, (size_t)count * sizeof(*nodes));
	D3DO_NodesBuffer = nodes;
	raw += 4;

	for (i = 0; i < count; ++i, raw += 56) {
		nodes[i].Line.x = (Fixed)(int32_t)D3DO_ReadBE32(raw + 0);
		nodes[i].Line.y = (Fixed)(int32_t)D3DO_ReadBE32(raw + 4);
		nodes[i].Line.dx = (Fixed)(int32_t)D3DO_ReadBE32(raw + 8);
		nodes[i].Line.dy = (Fixed)(int32_t)D3DO_ReadBE32(raw + 12);

		for (j = 0; j < 2; ++j) {
			uint32_t child =
				D3DO_ReadBE32(raw + 48 + j * 4);

			nodes[i].bbox[j][BOXTOP] =
				(Fixed)(int32_t)D3DO_ReadBE32(raw + 16 + j * 16 + 0);
			nodes[i].bbox[j][BOXBOTTOM] =
				(Fixed)(int32_t)D3DO_ReadBE32(raw + 16 + j * 16 + 4);
			nodes[i].bbox[j][BOXLEFT] =
				(Fixed)(int32_t)D3DO_ReadBE32(raw + 16 + j * 16 + 8);
			nodes[i].bbox[j][BOXRIGHT] =
				(Fixed)(int32_t)D3DO_ReadBE32(raw + 16 + j * 16 + 12);

			if (child & (uint32_t)NF_SUBSECTOR) {
				child &= ~(uint32_t)NF_SUBSECTOR;
				nodes[i].Children[j] =
					(void *)((uintptr_t)&subsectors[child] | (uintptr_t)1u);
			} else {
				nodes[i].Children[j] = &nodes[child];
			}
		}
	}

	FirstBSPNode = count ? &nodes[count - 1] : NULL;
	KillAResource(lump);
}




/**********************************

	Builds sector line lists and subsector sector numbers
	Finds block bounding boxes for sectors

**********************************/

static void GroupLines(void)
{
	line_t **linebuffer;	/* Pointer to linebuffer array */
	Word total;		/* Number of entries needed for linebuffer array */
	line_t *li;		/* Pointer to a work line record */
	Word i,j;
	sector_t *sector;	/* Work sector pointer */
	Fixed block;	/* Clipped bounding box value */
	Fixed bbox[4];

/* count number of lines in each sector */

	li = lines;		/* Init pointer to line array */
	total = 0;		/* How many line pointers are needed for sector line array */
	i = numlines;	/* How many lines to process */
	do {
		li->frontsector->linecount++;	/* Inc the front sector's line count */
		if (li->backsector && li->backsector != li->frontsector) {	/* Two sided line? */
			li->backsector->linecount++;	/* Add the back side referance */
			++total;	/* Inc count */
		}
		++total;	/* Inc for the front */
		++li;		/* Next line down */
	} while (--i);

/* Build line tables for each sector */

	linebuffer = (line_t **)AllocAPointer(total*sizeof(line_t*));
	LineArrayBuffer = linebuffer;	/* Save in global for later disposal */
	sector = sectors;		/* Init the sector pointer */
	i = numsectors;		/* Get the sector count */
	do {
		bbox[BOXTOP] = bbox[BOXRIGHT] = MININT;	/* Invalidate the rect */
		bbox[BOXBOTTOM] = bbox[BOXLEFT] = MAXINT;
		sector->lines = linebuffer;	/* Get the current list entry */
		li = lines;		/* Init the line array pointer */
		j = numlines;
		do {
			if (li->frontsector == sector || li->backsector == sector) {
				linebuffer[0] = li;	/* Add the pointer to the entry list */
				++linebuffer;		/* Add to the count */
				AddToBox(bbox,li->v1.x,li->v1.y);	/* Adjust the bounding box */
				AddToBox(bbox,li->v2.x,li->v2.y);	/* Both points */
			}
			++li;
		} while (--j);		/* All done? */

		/* Set the sound origin to the center of the bounding box */

		sector->SoundX = (bbox[BOXRIGHT]+bbox[BOXLEFT])/2;	/* Get average */
		sector->SoundY = (bbox[BOXTOP]+bbox[BOXBOTTOM])/2;	/* This is SIGNED! */

		/* Adjust bounding box to map blocks and clip to unsigned values */

		block = (bbox[BOXTOP]-BlockMapOrgY+MAXRADIUS)>>MAPBLOCKSHIFT;
		++block;
		block = (block > (int)BlockMapHeight) ? BlockMapHeight : block;
		sector->blockbox[BOXTOP]=block;		/* Save the topmost point */

		block = (bbox[BOXBOTTOM]-BlockMapOrgY-MAXRADIUS)>>MAPBLOCKSHIFT;
		block = (block < 0) ? 0 : block;
		sector->blockbox[BOXBOTTOM]=block;	/* Save the bottommost point */

		block = (bbox[BOXRIGHT]-BlockMapOrgX+MAXRADIUS)>>MAPBLOCKSHIFT;
		++block;
		block = (block > (int)BlockMapWidth) ? BlockMapWidth : block;
		sector->blockbox[BOXRIGHT]=block;	/* Save the rightmost point */

		block = (bbox[BOXLEFT]-BlockMapOrgX-MAXRADIUS)>>MAPBLOCKSHIFT;
		block = (block < 0) ? 0 : block;
		sector->blockbox[BOXLEFT]=block;	/* Save the leftmost point */
		++sector;
	} while (--i);
}

/**********************************

	Spawn items and critters

**********************************/

static void LoadThings(Word lump)
{
	Byte *raw;
	uint32_t count;
	uint32_t i;

	raw = (Byte *)LoadAResource(lump);
	if (!raw)
		return;

	/*
	 * 3DO serialized MapThing:
	 *   Fixed    x          4
	 *   Fixed    y          4
	 *   angle_t  angle      4
	 *   uint32  type        4
	 *   uint32  ThingFlags  4
	 *                         = 20 bytes
	 *
	 * The resource begins with a 32-bit big-endian count.  Do not cast
	 * this buffer to Rebecca's native mapthing_t: on modern hosts its
	 * representation is intentionally different.
	 */
	count = D3DO_ReadBE32(raw);
	raw += 4;

	for (i = 0; i < count; ++i, raw += 20) {
		mapthing_t mt;

		mt.x = (Fixed)(int32_t)D3DO_ReadBE32(raw + 0);
		mt.y = (Fixed)(int32_t)D3DO_ReadBE32(raw + 4);
		mt.angle = (angle_t)D3DO_ReadBE32(raw + 8);
		mt.type = (Word)D3DO_ReadBE32(raw + 12);
		mt.ThingFlags = (Word)D3DO_ReadBE32(raw + 16);

		SpawnMapThing(&mt);
	}

	KillAResource(lump);
}

/**********************************

	Draw the word "Loading" on the screen

**********************************/

static void LoadingPlaque(void)
{
	D3DO_ShowLoadingPlaque();
}

/**********************************

	Preload all the wall and flat shapes

**********************************/

static void PreloadWalls(void)
{
	Word i;				/* Index */
	texture_t *TexPtr;
	Boolean TextureLoadFlags[100];		/* Which textures should I load? */

    memset(TextureLoadFlags,0,sizeof(TextureLoadFlags));		/* Set to zilch */

	i = numsides;		/* How many side do I have? */
	if (i) {
		Word Tex;		/* Temp */
		side_t *sd;		/* Pointer to sidedef table */

		sd = sides;		/* Init the pointer */
		do {
			Tex = sd->toptexture;		/* Is there a top texture? */
			if (Tex<NumTextures) {
				TextureLoadFlags[Tex] = TRUE;	/* Load it in */
			}
			Tex = sd->midtexture;
			if (Tex<NumTextures) {
				TextureLoadFlags[Tex] = TRUE;
			}
			Tex = sd->bottomtexture;
			if (Tex<NumTextures) {
				TextureLoadFlags[Tex] = TRUE;
			}
			++sd;		/* Next side def */
		} while (--i);	/* All done? */
	}

	/* Now, scan the walls for switches */
	
	i = NumSwitches;
	if (i) {
		do {
			--i;
			if (TextureLoadFlags[SwitchList[i]]) {		/* Found a switch? */
				TextureLoadFlags[SwitchList[i^1]] = TRUE;	/* Get the alternate */
			}
		} while (i);	/* Any more? */
	}

	/* Now load in the walls */
	
	i = 0;			/* Init index */
	TexPtr = TextureInfo;		/* Init texture table */
	do {
		if (TextureLoadFlags[i]) {	/* Load it in? */
			TexPtr->data = LoadAResourceHandle(i+FirstTexture);	/* Get it */
		}
		++TexPtr;
	} while (++i<NumTextures);

	/* Now scan for the flats */
	
	memset(TextureLoadFlags,0,sizeof(TextureLoadFlags));		/* Set to zilch */
	i = numsectors;
	if (i) {
		Word Tex;		/* Temp */
		sector_t *sd;		/* Pointer to sidedef table */

		sd = sectors;		/* Init the pointer */
		do {
			TextureLoadFlags[sd->FloorPic] = TRUE;	/* Load it in */
			Tex = sd->CeilingPic;
			if (Tex<NumFlats) {		/* Make sure it's ok */
				TextureLoadFlags[Tex] = TRUE;
			}
			++sd;		/* Next side def */
		} while (--i);	/* All done? */
	}
	i = NumFlatAnims;
	if (i) {
		anim_t *sd;
		Word j;
		sd = FlatAnims;
		do {
			if (TextureLoadFlags[sd->LastPicNum]) {
				j = sd->BasePic;
				do {
					TextureLoadFlags[j] = TRUE;
				} while (++j<=sd->LastPicNum);
			}
			++sd;
		} while (--i);
	}
	
	i = 0;			/* Init index */
	do {
		if (TextureLoadFlags[i]) {	/* Load it in? */
			FlatInfo[i] = LoadAResourceHandle(i+FirstFlat);	/* Get it */
		}
	} while (++i<NumFlats);
	memcpy(FlatTranslation,FlatInfo,sizeof(Byte *)*NumFlats);
	i = 0;
	do {
		LoadAResourceHandle(PreLoadTable[i]);
		ReleaseAResource(PreLoadTable[i]);
		++i;
	} while (PreLoadTable[i] != 0xFFFFu);
}

/**********************************

	Load and prepare the game level

**********************************/

void SetupLevel(Word map)
{
	Word lumpnum;
	player_t *p;

	Randomize();			/* Reset the random number generator */
	LoadingPlaque();		/* Display "Loading" */
	PurgeHandles(0);		/* Purge memory */
	CompactHandles();		/* Pack remaining memory */
	TotalKillsInLevel = ItemsFoundInLevel = SecretsFoundInLevel = 0;
	p = &players;
	p->killcount = 0;		/* Nothing killed */
	p->secretcount = 0;		/* No secrets found */
	p->itemcount = 0;		/* No items found */
	
	InitThinkers();			/* Zap the think logics */

	if (map < 1 || map > 24) {
		fprintf(stderr, "DOOM3DO: invalid map number %u\n", (unsigned)map);
		return;
	}

	lumpnum = ((map-1)*ML_TOTAL)+rMAP01;	/* Get the map number */
	LoadedLevel = lumpnum;		/* Save the loaded resource number */

/* Note: most of this ordering is important */

	{
		const Word vertexResourceNum =
			(Word)(lumpnum + ML_VERTEXES);
		Byte *rawVertexes =
			(Byte *)LoadAResource(vertexResourceNum);
		LongWord vertexBytes =
			GetAResourceSize(vertexResourceNum);
		uint32_t vertexCount =
			(uint32_t)vertexBytes / 8u;
		uint32_t vertexIndex;

		D3DO_NumVertexes = vertexCount;

		vertexes = (vertex_t *)AllocAPointer(
			(size_t)vertexCount * sizeof(*vertexes));

		if (rawVertexes) {
			for (vertexIndex = 0;
				 vertexIndex < vertexCount;
				 ++vertexIndex, rawVertexes += 8) {
				vertexes[vertexIndex].x =
					(Fixed)(int32_t)D3DO_ReadBE32(rawVertexes + 0);
				vertexes[vertexIndex].y =
					(Fixed)(int32_t)D3DO_ReadBE32(rawVertexes + 4);
			}
		}
	}
	LoadSectors(lumpnum+ML_SECTORS);	/* Needs nothing */
	LoadSideDefs(lumpnum+ML_SIDEDEFS);	/* Needs sectors */
	LoadLineDefs(lumpnum+ML_LINEDEFS);	/* Needs vertexes,sectors and sides */
	LoadBlockMap(lumpnum+ML_BLOCKMAP);	/* Needs lines */
	LoadSegs(lumpnum+ML_SEGS);		/* Needs vertexes,lines,sides */
	LoadSubsectors(lumpnum+ML_SSECTORS);	/* Needs sectors and segs and sides */
	LoadNodes(lumpnum+ML_NODES);		/* Needs subsectors */
	KillAResource(lumpnum+ML_VERTEXES);		/* Release the map vertexes */
	RejectMatrix = (Byte *)LoadAResource(lumpnum+ML_REJECT);	/* Get the reject matrix */
	GroupLines();			/* Final last minute data arranging */
	deathmatch_p = deathmatchstarts;
	LoadThings(lumpnum+ML_THINGS);	/* Spawn all the items */
	SpawnSpecials();		/* Spawn all sector specials */
	PreloadWalls();			/* Load all the wall textures and sprites */

/* if deathmatch, randomly spawn the active players */

	gamepaused = FALSE;		/* Game in progress */
}

/**********************************

	Dispose of all memory allocated by loading a level

**********************************/

void ReleaseMapMemory(void)
{
	Word i;
	
	DeallocAPointer(sectors);		/* Dispose of the sectors */
	DeallocAPointer(sides);			/* Dispose of the side defs */
	DeallocAPointer(lines);			/* Dispose of the lines */
	KillAResource(LoadedLevel+ML_BLOCKMAP);	/* Make sure it's discarded since I modified it */
	DeallocAPointer(BlockLinkPtr);	/* Discard the block map mobj linked list */
	DeallocAPointer(BlockMapLines); BlockMapLines = 0;
	DeallocAPointer(D3DO_BlockMapLineStorage); D3DO_BlockMapLineStorage = 0;
	DeallocAPointer(segs);		/* Release the line segment memory */
	DeallocAPointer(subsectors);	/* Release the sub sectors */
	DeallocAPointer(D3DO_NodesBuffer); D3DO_NodesBuffer=0;	/* Release the native BSP tree */
	KillAResource(LoadedLevel+ML_REJECT);	/* Release the quick reject matrix */	
	DeallocAPointer(LineArrayBuffer);
	sectors = 0;		/* Zap the pointers */
	sides = 0;			/* May cause a memory fault, but this will aid in debugging! */
	lines = 0;
	BlockMapLines = 0;		/* Force zero for resource */
	BlockLinkPtr = 0;
	segs = 0;
	subsectors = 0;
	FirstBSPNode = 0;
	RejectMatrix = 0;
	
	i = 0;					/* Start at the first wall texture */
	do {
		ReleaseAResource(i+FirstTexture);	/* Release all wall textures */
	} while (++i<NumTextures);			/* All released? */
	i = 0;					/* Start at the first flat texture */
	do {
		ReleaseAResource(i+FirstFlat);
	} while (++i<NumFlats);
	memset(FlatInfo,0,NumFlats*sizeof(void *));	/* Kill the cached flat table */
	InitThinkers();			/* Dispose of all remaining memory */
}

/**********************************

	Init the machine independant code

**********************************/

void P_Init(void)
{
	P_InitSwitchList();		/* Init the switch picture lookup list */
	P_InitPicAnims();		/* Init the picture animation scripts */
}
