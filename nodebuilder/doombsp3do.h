
/*
    doombsp3do.h - DOOM3DO Nodebuilder.
    Copyright (C) 2026  Gibbon.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
    */

#ifndef DOOMBSP3DO_H
#define DOOMBSP3DO_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>

typedef int8_t i8; typedef uint8_t u8; typedef int16_t i16; typedef uint16_t u16;
typedef int32_t i32; typedef uint32_t u32; typedef int64_t i64; typedef uint64_t u64;

#define NF_SUBSECTOR 0x8000u
#define ML_TWOSIDED 4
#define BOXTOP 0
#define BOXBOTTOM 1
#define BOXLEFT 2
#define BOXRIGHT 3
#define FRACBITS 16
#define FRACUNIT 65536
#define BLOCKSIZE 128
#define MAXINT32 0x7fffffff

struct WadDir { u32 start,length; char name[9]; };
struct Wad { char type[5]; u32 count,diroff; struct WadDir *dir; FILE *fp; };
struct Thing { i16 x,y,angle,type,options; };
struct Vertex { i16 x,y; };
struct LineDef { i16 v1,v2,flags,special,tag,sidenum[2]; };
struct SideDef { i16 xoff,yoff; char top[8],bottom[8],mid[8]; i16 sector; };
struct Sector { i16 floorh,ceilh; char floorpic[8],ceilpic[8]; i16 light,special,tag; };

struct WLine {
    float x1,y1,x2,y2;
    int linedef,side,offset;
    struct WLine *next;
};

struct DivLine { float x,y,dx,dy; };
struct BNode { struct WLine *lines; struct DivLine div; struct BNode *front,*back; };
struct OutSeg { u16 v1,v2; u32 angle; i32 offset; u16 linedef,side; };
struct OutSS { u16 num,first; };

struct OutNode {
    i32 x,y,dx,dy;
    i32 bbox[2][4];
    u32 child[2];
};

extern struct Wad g_wad;
extern struct Thing *g_things; extern size_t g_nthings;
extern struct Vertex *g_vertices; extern size_t g_nvertices;
extern struct LineDef *g_lines; extern size_t g_nlines;
extern struct SideDef *g_sides; extern size_t g_nsides;
extern struct Sector *g_sectors; extern size_t g_nsectors;
extern int *g_active_lines; extern size_t g_nactive;

void fatal(const char *fmt,...);
void *xmalloc(size_t n); void *xrealloc(void *p,size_t n);
void load_wad(const char *path);
void load_map(void);
void build_and_write(const char *outdir,const char *texmap,const char *manifest);

#endif
