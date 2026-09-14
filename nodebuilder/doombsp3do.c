/*
    doombsp3do.c - DOOM3DO Nodebuilder.
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

/* This actually has some history (and why its so messy).
This is a straight C conversion of ID Software's original Nodebuilder
from Objective-C (thank me being a Mac user).

IDBSP contained a lot of coding errors and created an incorrect BSP traversal
compared to IDs original nodebuilder (and the one Rebecca used for the 3DO port).

So I ported it myself, straight to C, as strictly as possible.

I then reverse engineered the 3DO Nodebuilder from the 3DO source, analysing the 
3DO maps and comparing the built BSPs, until I got an exact Byte-for-Byte match.

This Nodebuilder therefore creates 'exact duplicate BSPs' as Rebecca's 3DO Nodebuilder.
*/

#include "doombsp3do.h"
#include <stdarg.h>

#ifndef M_PI
#define M_PI 3.141592657
#endif

// BEWARE ADVENTURER!  FOR BEYOND..  IS DANGEROUS!  Arrgghh..
// Why all in one file?  Because when you're doing Reverse Engineering, it is
// easiesr to 'get it done' then spending years 'making it perfect'.
// We didn't need all of IDBSP so its just taken, used, et voila!
// Appreciate that we have a 3DO Nodebuilder now, you're welcome!

/* --------------------- WAD I/O --------------------- */
struct Wad g_wad;
struct Thing *g_things;
size_t g_nthings;
struct Vertex *g_vertices;
size_t g_nvertices;
struct LineDef *g_lines;
size_t g_nlines;
struct SideDef *g_sides;
size_t g_nsides;
struct Sector *g_sectors;
size_t g_nsectors;
int *g_active_lines;
size_t g_nactive;
static int g_cuts;

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        fatal("out of memory");
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        fatal("out of memory");
    return q;
}

static u16 rd16(const u8 *p) { return (u16)p[0] | ((u16)p[1] << 8); }
static i16 rds16(const u8 *p) { return (i16)rd16(p); }
static u32 rd32(const u8 *p) { return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24); }
static int g_big_endian = 1;

static void wr32(FILE *f, u32 v)
{
    if (g_big_endian)
    {
        for (int i = 3; i >= 0; i--)
            fputc((v >> (8 * i)) & 255, f);
    }
    else
    {
        for (int i = 0; i < 4; i++)
            fputc((v >> (8 * i)) & 255, f);
    }
}

static void seekread(void *p, size_t n, u32 off)
{
    if (fseek(g_wad.fp, (long)off, SEEK_SET) || fread(p, 1, n, g_wad.fp) != n)
        fatal("short WAD read");
}

static void canon(char out[9], const char *in)
{
    memset(out, 0, 9);
    size_t n = strlen(in);
    if (n > 8)
        n = 8;
    memcpy(out, in, n);
    for (int i = 7; i >= 0 && out[i] == ' '; i--)
        out[i] = 0;
    for (int i = 0; i < 8; i++)
        if (out[i] >= 'a' && out[i] <= 'z')
            out[i] = (char)(out[i] - 'a' + 'A');
}

static int lump(const char *n)
{
    char w[9];
    canon(w, n);
    for (u32 i = 0; i < g_wad.count; i++)
    {
        char x[9];
        canon(x, g_wad.dir[i].name);
        if (!strcmp(w, x))
            return (int)i;
    }
    return -1;
}

static u8 *getlump(const char *n, size_t *len)
{
    int i = lump(n);
    if (i < 0)
        fatal("missing lump %s", n);
    *len = g_wad.dir[i].length;
    u8 *p = xmalloc(*len);
    seekread(p, *len, g_wad.dir[i].start);
    return p;
}

void load_wad(const char *path)
{
    memset(&g_wad, 0, sizeof g_wad);
    g_wad.fp = fopen(path, "rb");
    if (!g_wad.fp)
        fatal("cannot open %s: %s", path, strerror(errno));
    u8 h[12];
    if (fread(h, 1, 12, g_wad.fp) != 12)
        fatal("not a WAD");
    memcpy(g_wad.type, h, 4);
    g_wad.type[4] = 0;
    g_wad.count = rd32(h + 4);
    g_wad.diroff = rd32(h + 8);
    if (strcmp(g_wad.type, "IWAD") && strcmp(g_wad.type, "PWAD"))
        fatal("not IWAD/PWAD");
    g_wad.dir = xmalloc((size_t)g_wad.count * sizeof *g_wad.dir);
    for (u32 i = 0; i < g_wad.count; i++)
    {
        u8 d[16];
        seekread(d, 16, g_wad.diroff + i * 16);
        g_wad.dir[i].start = rd32(d);
        g_wad.dir[i].length = rd32(d + 4);
        memcpy(g_wad.dir[i].name, d + 8, 8);
        g_wad.dir[i].name[8] = 0;
    }
}

void load_map(void)
{
    size_t len;
    u8 *p;
    p = getlump("THINGS", &len);
    if (len % 10)
        fatal("bad THINGS");
    g_nthings = len / 10;
    g_things = xmalloc(g_nthings * sizeof *g_things);

    for (size_t i = 0; i < g_nthings; i++)
    {
        g_things[i].x = rds16(p + 10 * i);
        g_things[i].y = rds16(p + 10 * i + 2);
        g_things[i].angle = rds16(p + 10 * i + 4);
        g_things[i].type = rds16(p + 10 * i + 6);
        g_things[i].options = rds16(p + 10 * i + 8);
    }
    free(p);

    p = getlump("VERTEXES", &len);
    if (len % 4)
        fatal("bad VERTEXES");
    g_nvertices = len / 4;
    g_vertices = xmalloc(g_nvertices * sizeof *g_vertices);

    for (size_t i = 0; i < g_nvertices; i++)
    {
        g_vertices[i].x = rds16(p + 4 * i);
        g_vertices[i].y = rds16(p + 4 * i + 2);
    }
    free(p);

    p = getlump("LINEDEFS", &len);
    if (len % 14)
        fatal("bad LINEDEFS");
    g_nlines = len / 14;
    g_lines = xmalloc(g_nlines * sizeof *g_lines);

    for (size_t i = 0; i < g_nlines; i++)
    {
        u8 *q = p + 14 * i;
        g_lines[i].v1 = rds16(q);
        g_lines[i].v2 = rds16(q + 2);
        g_lines[i].flags = rds16(q + 4);
        g_lines[i].special = rds16(q + 6);
        g_lines[i].tag = rds16(q + 8);
        g_lines[i].sidenum[0] = rds16(q + 10);
        g_lines[i].sidenum[1] = rds16(q + 12);
    }
    free(p);

    p = getlump("SIDEDEFS", &len);
    if (len % 30)
        fatal("bad SIDEDEFS");
    g_nsides = len / 30;
    g_sides = xmalloc(g_nsides * sizeof *g_sides);

    for (size_t i = 0; i < g_nsides; i++)
    {
        u8 *q = p + 30 * i;
        g_sides[i].xoff = rds16(q);
        g_sides[i].yoff = rds16(q + 2);
        memcpy(g_sides[i].top, q + 4, 8);
        memcpy(g_sides[i].bottom, q + 12, 8);
        memcpy(g_sides[i].mid, q + 20, 8);
        g_sides[i].sector = rds16(q + 28);
    }
    free(p);

    p = getlump("SECTORS", &len);
    if (len % 26)
        fatal("bad SECTORS");
    g_nsectors = len / 26;
    g_sectors = xmalloc(g_nsectors * sizeof *g_sectors);

    for (size_t i = 0; i < g_nsectors; i++)
    {
        u8 *q = p + 26 * i;
        g_sectors[i].floorh = rds16(q);
        g_sectors[i].ceilh = rds16(q + 2);
        memcpy(g_sectors[i].floorpic, q + 4, 8);
        memcpy(g_sectors[i].ceilpic, q + 12, 8);
        g_sectors[i].light = rds16(q + 20);
        g_sectors[i].special = rds16(q + 22);
        g_sectors[i].tag = rds16(q + 24);
    }
    free(p);

    g_active_lines = xmalloc(g_nlines * sizeof *g_active_lines);
    g_nactive = 0;
    for (size_t i = 0; i < g_nlines; i++)
    {
        if (g_lines[i].v1 == g_lines[i].v2)
            continue;
        g_active_lines[g_nactive++] = (int)i;
    }
}

/* --------------------- Original DOOMBSP BSP core --------------------- */
// Thanks Carmack!
static void div_from(const struct WLine *w, struct DivLine *d)
{
    d->x = w->x1;
    d->y = w->y1;
    d->dx = w->x2 - w->x1;
    d->dy = w->y2 - w->y1;
}

static int point_side(float x, float y, const struct DivLine *l)
{
    float dx, dy, left, right, a, b, c, d;
    if (!l->dx)
    {
        if (x > l->x - 2 && x < l->x + 2)
            return -1;
        if (x < l->x)
            return l->dy > 0;
        return l->dy < 0;
    }
    if (!l->dy)
    {
        if (y > l->y - 2 && y < l->y + 2)
            return -1;
        if (y < l->y)
            return l->dx < 0;
        return l->dx > 0;
    }
    dx = l->x - x;
    dy = l->y - y;
    a = l->dx * l->dx + l->dy * l->dy;
    b = 2 * (l->dx * dx + l->dy * dy);
    c = dx * dx + dy * dy - 4;
    d = b * b - 4 * a * c;
    if (d > 0)
        return -1;
    dx = x - l->x;
    dy = y - l->y;
    left = l->dy * dx;
    right = dy * l->dx;
    if (fabs(left - right) < 0.5)
        return -1;
    return right < left ? 0 : 1;
}

static int sgn(float x) { return x < 0 ? -1 : x > 0 ? 1
                                                    : 0; }
static int line_side(const struct WLine *w, const struct DivLine *l)
{
    int a = point_side(w->x1, w->y1, l), b = point_side(w->x2, w->y2, l);
    if (a == b)
    {
        if (a == -1)
        {
            float dx = w->x2 - w->x1, dy = w->y2 - w->y1;
            return (sgn(dx) == sgn(l->dx) && sgn(dy) == sgn(l->dy)) ? 0 : 1;
        }
        return a;
    }
    if (a == -1)
        return b;
    if (b == -1)
        return a;
    return -2;
}

static float intercept(const struct DivLine *v2, const struct DivLine *v1)
{
    float den = v1->dy * v2->dx - v1->dx * v2->dy;
    if (den == 0)
        fatal("InterceptVector: parallel");
    float f = ((v1->x - v2->x) * v1->dy + (v2->y - v1->y) * v1->dx) / den;
    if (f <= 0 || f >= 1)
        fatal("InterceptVector: intersection outside line");
    return f;
}

static float id_round(float x)
{
    if (x > 0)
    {
        float r = x - (int)x;
        if (r < .1f)
            return (int)x;
        if (r > .9f)
            return (int)x + 1;
        return x;
    }
    float r = (int)x - x;
    if (r < .1f)
        return (int)x;
    if (r > .9f)
        return (int)x - 1;
    return x;
}

static struct WLine *clone_w(const struct WLine *w)
{
    struct WLine *n = xmalloc(sizeof *n);
    *n = *w;
    n->next = NULL;
    return n;
}

static void free_w(struct WLine *l)
{
    while (l)
    {
        struct WLine *n = l->next;
        free(l);
        l = n;
    }
}

static void append_w(struct WLine **h, struct WLine **t, struct WLine *l)
{
    l->next = NULL;
    if (*t)
        (*t)->next = l;
    else
        *h = l;
    *t = l;
}

static struct WLine *cut_line(struct WLine *w, const struct DivLine *b)
{
    g_cuts++;
    struct DivLine d;
    div_from(w, &d);
    struct WLine *n = clone_w(w);
    float f = intercept(&d, b);
    float ix = d.x + id_round(d.dx * f), iy = d.y + id_round(d.dy * f);
    int side = point_side(w->x1, w->y1, b);
    int off = w->offset + (int)id_round(f * sqrt(d.dx * d.dx + d.dy * d.dy));
    if (side == 0)
    {
        w->x2 = ix;
        w->y2 = iy;
        n->x1 = ix;
        n->y1 = iy;
        n->offset = off;
    }
    else
    {
        w->x1 = ix;
        w->y1 = iy;
        w->offset = off;
        n->x2 = ix;
        n->y2 = iy;
    }
    return n;
}

static int eval_split(struct WLine *h, struct WLine *split, int best)
{
    int f = 0, b = 0, c = 0, g = 0;
    for (struct WLine *l = h; l; l = l->next)
        c++;
    struct DivLine d;
    div_from(split, &d);
    for (struct WLine *l = h; l; l = l->next)
    {
        int s = l == split ? 0 : line_side(l, &d);
        if (s == 0)
            f++;
        else if (s == 1)
            b++;
        else if (s == -2)
        {
            f++;
            b++;
        }
        int mx = f > b ? f : b, n = (f + b) - c;
        g = mx + n * 8;
        if (g > best)
            return g;
    }
    if (!f || !b)
        return INT_MAX;
    return g;
}

static struct BNode *bsp(struct WLine *list)
{
    int c = 0;
    for (struct WLine *l = list; l; l = l->next)
        c++;
    int step = c / 40 + 1, best = INT_MAX;
    struct WLine *pick = NULL;
research:
    for (int i = 0; i < c; i += step)
    {
        struct WLine *l = list;
        for (int j = 0; j < i; j++)
            l = l->next;
        int v = eval_split(list, l, best);
        if (v < best)
        {
            best = v;
            pick = l;
        }
    }
    if (best == INT_MAX)
    {
        if (step > 1)
        {
            step = 1;
            goto research;
        }
        struct BNode *n = xmalloc(sizeof *n);
        memset(n, 0, sizeof *n);
        n->lines = list;
        return n;
    }
    struct DivLine d;
    div_from(pick, &d);
    struct WLine *fh = NULL, *ft = NULL, *bh = NULL, *bt = NULL;
    for (struct WLine *l = list; l; l = l->next)
    {
        if (l == pick)
        {
            append_w(&fh, &ft, clone_w(l));
            continue;
        }
        int s = line_side(l, &d);
        if (s == -2)
        {
            struct WLine *n = cut_line(l, &d);
            append_w(&fh, &ft, clone_w(l));
            append_w(&bh, &bt, clone_w(n));
            free(n);
        }
        else if (s == 0)
            append_w(&fh, &ft, clone_w(l));
        else if (s == 1)
            append_w(&bh, &bt, clone_w(l));
        else
            fatal("ExecuteSplit: bad side");
    }
    free_w(list);
    struct BNode *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->div = d;
    n->front = bsp(fh);
    n->back = bsp(bh);
    return n;
}


static struct WLine *make_base(void)
{
    struct WLine *h = NULL, *t = NULL;
    for (size_t ai = 0; ai < g_nactive; ai++)
    {
        int i = g_active_lines[ai];
        struct LineDef *l = &g_lines[i];
        struct WLine *w = xmalloc(sizeof *w);
        memset(w, 0, sizeof *w);
        w->x1 = g_vertices[l->v1].x;
        w->y1 = g_vertices[l->v1].y;
        w->x2 = g_vertices[l->v2].x;
        w->y2 = g_vertices[l->v2].y;
        w->linedef = i;
        w->side = 0;
        append_w(&h, &t, w);
        if (l->flags & ML_TWOSIDED)
        {
            w = xmalloc(sizeof *w);
            memset(w, 0, sizeof *w);
            w->x1 = g_vertices[l->v2].x;
            w->y1 = g_vertices[l->v2].y;
            w->x2 = g_vertices[l->v1].x;
            w->y2 = g_vertices[l->v1].y;
            w->linedef = i;
            w->side = 1;
            append_w(&h, &t, w);
        }
    }
    return h;
}

static struct OutSeg *osegs;
static size_t nosegs;
static struct OutSS *oss;
static size_t noss;
static struct OutNode *onodes;
static size_t nonodes;
static struct Vertex *outverts;
static size_t noutverts;
static u16 *line_v1;
static u16 *line_v2;

/*
 * Do not regroup sectors, the 3DO
 * runtime GroupLines() builds its runtime sector line lists after loading.
 */
struct OutSide
{
    i32 xoff, yoff;
    char top[8], bottom[8], mid[8];
    u32 sector;
};

struct OutSector
{
    i16 floorh, ceilh;
    char floorpic[8], ceilpic[8];
    u32 light, special, tag;
};

static struct OutSide *osides;
static size_t nosides;
static struct OutSector *osectors;
static size_t nosect;
static u16 *line_s1, *line_s2;

static void build_output_defs(void)
{
    osides = NULL;
    nosides = 0;
    osectors = NULL;
    nosect = 0;

    line_s1 = xmalloc(g_nlines * sizeof(*line_s1));
    line_s2 = xmalloc(g_nlines * sizeof(*line_s2));

    /* The 3DO SIDEDEFS resource preserves the PC WAD sidedef table,
    one record for each input sidedef, same index. */
    for (size_t i = 0; i < g_nsides; i++)
    {
        if (g_sides[i].sector < 0 || (size_t)g_sides[i].sector >= g_nsectors)
            fatal("SIDEDEFS[%zu] has invalid sector %d", i, g_sides[i].sector);
        osides = xrealloc(osides, (nosides + 1) * sizeof(*osides));
        struct OutSide *o = &osides[nosides++];
        o->xoff = g_sides[i].xoff;
        o->yoff = g_sides[i].yoff;
        memcpy(o->top, g_sides[i].top, 8);
        memcpy(o->bottom, g_sides[i].bottom, 8);
        memcpy(o->mid, g_sides[i].mid, 8);
        o->sector = (u32)(u16)g_sides[i].sector;
    }

    /* The 3DO SECTORS resource preserves the source sector table. */
    for (size_t i = 0; i < g_nsectors; i++)
    {
        osectors = xrealloc(osectors, (nosect + 1) * sizeof(*osectors));
        struct OutSector *o = &osectors[nosect++];
        o->floorh = g_sectors[i].floorh;
        o->ceilh = g_sectors[i].ceilh;
        memcpy(o->floorpic, g_sectors[i].floorpic, 8);
        memcpy(o->ceilpic, g_sectors[i].ceilpic, 8);
        o->light = (u32)(u16)g_sectors[i].light;
        o->special = (u32)(u16)g_sectors[i].special;
        o->tag = (u32)(u16)g_sectors[i].tag;
    }

    /* LINEDEFS keep the original SIDEDEF indices. */
    for (size_t i = 0; i < g_nlines; i++)
    {
        int a = g_lines[i].sidenum[0];
        int b = g_lines[i].sidenum[1];
        if (a < 0 || (size_t)a >= g_nsides)
            fatal("LINEDEFS[%zu] has invalid front sidedef %d", i, a);
        line_s1[i] = (u16)a;
        if (b < 0)
        {
            line_s2[i] = 0xffff;
        }
        else
        {
            if ((size_t)b >= g_nsides)
                fatal("LINEDEFS[%zu] has invalid back sidedef %d", i, b);
            line_s2[i] = (u16)b;
        }
    }
}

/* --------------------- OUTPUT <3 --------------------- */
static u16 out_vertex(float x, float y)
{
    i16 ix = (i16)x, iy = (i16)y;
    for (size_t i = 0; i < noutverts; i++)
        if (outverts[i].x == ix && outverts[i].y == iy)
            return (u16)i;
    if (noutverts >= 65535)
        fatal("too many output vertices");
    outverts = xrealloc(outverts, (noutverts + 1) * sizeof(*outverts));
    outverts[noutverts].x = ix;
    outverts[noutverts].y = iy;
    return (u16)noutverts++;
}

/* Original DOOMBSP emits a 16-bit mapseg angle.  The 3DO converter widens
   that value to angle_t by shifting it left 16 bits. */
static u32 out_angle(float dx, float dy)
{
    const double PI_ID = 3.141592657;
    float ff = (float)atan2((double)dy, (double)dx);
    double v = ((double)ff / (PI_ID * 2.0)) * 65536.0;
    i16 a = (i16)(int)v; /* C truncation toward zero, matching original */
    return ((u32)(u16)a) << 16;
}

static void bbox_tree(struct BNode *n, i32 *b)
{
    if (n->lines)
    {
        b[0] = INT_MIN;
        b[1] = INT_MAX;
        b[2] = INT_MAX;
        b[3] = INT_MIN;
        for (struct WLine *l = n->lines; l; l = l->next)
        {
            int x1 = (int)l->x1, y1 = (int)l->y1, x2 = (int)l->x2, y2 = (int)l->y2;
            if (y1 > b[0])
                b[0] = y1;
            if (y2 > b[0])
                b[0] = y2;
            if (y1 < b[1])
                b[1] = y1;
            if (y2 < b[1])
                b[1] = y2;
            if (x1 < b[2])
                b[2] = x1;
            if (x2 < b[2])
                b[2] = x2;
            if (x1 > b[3])
                b[3] = x1;
            if (x2 > b[3])
                b[3] = x2;
        }
        return;
    }
    i32 a[4], c[4];
    bbox_tree(n->front, a);
    bbox_tree(n->back, c);
    b[0] = a[0] > c[0] ? a[0] : c[0];
    b[1] = a[1] < c[1] ? a[1] : c[1];
    b[2] = a[2] < c[2] ? a[2] : c[2];
    b[3] = a[3] > c[3] ? a[3] : c[3];
}

static void seed_output_vertices(void)
{
    /* Preserve the source VERTEXES order. */
    line_v1 = xmalloc(g_nlines * sizeof(*line_v1));
    line_v2 = xmalloc(g_nlines * sizeof(*line_v2));
    outverts = xmalloc(g_nvertices * sizeof(*outverts));
    noutverts = g_nvertices;
    for (size_t i = 0; i < g_nvertices; i++)
    {
        outverts[i] = g_vertices[i];
    }
    for (size_t i = 0; i < g_nlines; i++)
    {
        if (g_lines[i].v1 < 0 || (size_t)g_lines[i].v1 >= g_nvertices ||
            g_lines[i].v2 < 0 || (size_t)g_lines[i].v2 >= g_nvertices)
            fatal("LINEDEFS[%zu] has invalid vertex reference", i);
        line_v1[i] = (u16)g_lines[i].v1;
        line_v2[i] = (u16)g_lines[i].v2;
    }
}

static void emit_tree(struct BNode *n, u32 *idx)
{
    if (n->lines)
    {
        u16 first = (u16)nosegs, c = 0;
        for (struct WLine *l = n->lines; l; l = l->next)
        {
            if (nosegs >= 65535)
                fatal("too many SEGS");
            osegs = xrealloc(osegs, (nosegs + 1) * sizeof *osegs);
            struct OutSeg *s = &osegs[nosegs++];
            s->v1 = out_vertex(l->x1, l->y1);
            s->v2 = out_vertex(l->x2, l->y2);
            s->angle = out_angle(l->x2 - l->x1, l->y2 - l->y1);
            s->offset = l->offset;
            s->linedef = (u16)l->linedef;
            s->side = (u16)l->side;
            c++;
        }
        if (noss >= 65535)
            fatal("too many SSECTORS");
        oss = xrealloc(oss, (noss + 1) * sizeof *oss);
        oss[noss].num = c;
        oss[noss].first = first;
        *idx = NF_SUBSECTOR | (u32)noss;
        noss++;
        return;
    }
    u32 a, b;
    emit_tree(n->front, &a);
    emit_tree(n->back, &b);
    struct OutNode o;
    memset(&o, 0, sizeof o);
    o.x = (i32)n->div.x;
    o.y = (i32)n->div.y;
    o.dx = (i32)n->div.dx;
    o.dy = (i32)n->div.dy;
    bbox_tree(n->front, o.bbox[0]);
    bbox_tree(n->back, o.bbox[1]);
    onodes = xrealloc(onodes, (nonodes + 1) * sizeof *onodes);
    onodes[nonodes] = o;
    onodes[nonodes].child[0] = a;
    onodes[nonodes].child[1] = b;
    *idx = (u32)nonodes++;
}

/* --------------------- IDBSP SAVECNCT (NOT MINE) :P --------------------- */
typedef struct
{
    int x, y;
} BPoint;

typedef struct
{
    int xl, xh, yl, yh;
} BBox;

typedef struct
{
    BPoint p1, p2;
} BLine;

typedef struct
{
    int numpoints;
    BBox bounds;
    BPoint *points;
} BChain;

typedef struct
{
    int x, y, dx, dy;
} BDivLine;

static BBox *secboxes;
static BChain *chains;
static size_t numchains;
static BLine *blines;
static size_t numblines;
static BDivLine ends[2], sides[2];
static int end0out, end1out, side0out, side1out;
static BBox sweptarea;

static void cb(BBox *b)
{
    b->xl = b->yl = INT_MAX;
    b->xh = b->yh = INT_MIN;
}

static void ab(BBox *b, int x, int y)
{
    if (x < b->xl)
        b->xl = x;
    if (x > b->xh)
        b->xh = x;
    if (y < b->yl)
        b->yl = y;
    if (y > b->yh)
        b->yh = y;
}

static int bp(const BPoint *p, const BDivLine *l)
{
    int dx, dy, left, right;
    if (!l->dx)
    {
        if (p->x < l->x)
            return l->dy > 0;
        return l->dy < 0;
    }
    if (!l->dy)
    {
        if (p->y < l->y)
            return l->dx < 0;
        return l->dx > 0;
    }
    dx = p->x - l->x;
    dy = p->y - l->y;
    left = l->dy * dx;
    right = dy * l->dx;
    return right < left ? 0 : 1;
}

static int chain_blocks(const BChain *c)
{
    if (sweptarea.xl > c->bounds.xh || sweptarea.xh < c->bounds.xl || sweptarea.yl > c->bounds.yh || sweptarea.yh < c->bounds.yl)
        return 0;
    int start = -1;
    for (int p = 0; p < c->numpoints; p++)
    {
        const BPoint *pt = &c->points[p];
        if (bp(pt, &ends[0]) == end0out)
        {
            start = -1;
            continue;
        }
        if (bp(pt, &ends[1]) == end1out)
        {
            start = -1;
            continue;
        }
        int side;
        if (bp(pt, &sides[0]) == side0out)
            side = 0;
        else if (bp(pt, &sides[1]) == side1out)
            side = 1;
        else
            continue;
        if (start == -1 || start == side)
        {
            start = side;
            continue;
        }
        return 1;
    }
    return 0;
}

static void build_chains(void)
{
    int *used = xmalloc(numblines * sizeof *used);
    memset(used, 0, numblines * sizeof *used);
    chains = NULL;
    numchains = 0;
    for (size_t i = 0; i < numblines; i++)
    {
        if (used[i])
            continue;
        used[i] = 1;
        BPoint *tmp = xmalloc((numblines + 1) * sizeof *tmp);
        size_t np = 0;
        int cx = blines[i].p1.x, cy = blines[i].p1.y;
        tmp[np++] = (BPoint){cx, cy};
        cx = blines[i].p2.x;
        cy = blines[i].p2.y;
        tmp[np++] = (BPoint){cx, cy};
        BChain ch;
        cb(&ch.bounds);
        ab(&ch.bounds, tmp[0].x, tmp[0].y);
        ab(&ch.bounds, cx, cy);
        for (;;)
        {
            size_t j;
            for (j = i + 1; j < numblines; j++)
                if (!used[j] && blines[j].p1.x == cx && blines[j].p1.y == cy)
                    break;
            if (j == numblines)
                break;
            used[j] = 1;
            cx = blines[j].p2.x;
            cy = blines[j].p2.y;
            tmp[np++] = (BPoint){cx, cy};
            ab(&ch.bounds, cx, cy);
        }
        ch.numpoints = (int)np;
        ch.points = xmalloc(np * sizeof *ch.points);
        memcpy(ch.points, tmp, np * sizeof *ch.points);
        free(tmp);
        chains = xrealloc(chains, (numchains + 1) * sizeof *chains);
        chains[numchains++] = ch;
    }
    free(used);
}

static void build_reject(unsigned char **out, size_t *outlen)
{
    /* Direct translation of SAVECNCT.M.  This operates on the
       source WAD's sector/sidedef/line tables, not on BSP generated segs. */
    size_t ns = g_nsectors;
    *outlen = (ns * ns + 7) / 8;
    *out = calloc(*outlen, 1);
    if (!*out)
        fatal("REJECT OOM");

    secboxes = xmalloc(ns * sizeof *secboxes);
    for (size_t i = 0; i < ns; i++)
        cb(&secboxes[i]);

    /* SAVECNCT's ProcessConnections() scans only side 0. */
    for (size_t i = 0; i < g_nlines; i++)
    {
        if (g_lines[i].v1 == g_lines[i].v2)
            continue; /* DOOMBSP removes it */
        int sd = g_lines[i].sidenum[0];
        if (sd < 0 || (size_t)sd >= g_nsides)
            continue;
        int sec = g_sides[sd].sector;
        if (sec < 0 || (size_t)sec >= ns)
            continue;
        ab(&secboxes[sec], g_vertices[g_lines[i].v1].x, g_vertices[g_lines[i].v1].y);
        ab(&secboxes[sec], g_vertices[g_lines[i].v2].x, g_vertices[g_lines[i].v2].y);
    }

    for (size_t i = 0; i < g_nlines; i++)
    {
        if (g_lines[i].v1 == g_lines[i].v2)
            continue; /* DOOMBSP removes it but WE need it (REVERSE ENGINEERED)*/
        if (g_lines[i].flags & ML_TWOSIDED)
            continue;
        blines = xrealloc(blines, (numblines + 1) * sizeof *blines);
        blines[numblines].p1 = (BPoint){g_vertices[g_lines[i].v1].x, g_vertices[g_lines[i].v1].y};
        blines[numblines].p2 = (BPoint){g_vertices[g_lines[i].v2].x, g_vertices[g_lines[i].v2].y};
        numblines++;
    }
    build_chains();

    size_t blocked = 0;
    for (size_t i = 0; i + 1 < ns; i++)
    {
        BBox *b0 = &secboxes[i];
        if (b0->xl == INT_MAX || b0->yl == INT_MAX)
            continue;
        if (b0->xh - b0->xl < 64 || b0->yh - b0->yl < 64)
            continue;
        for (size_t j = i + 1; j < ns; j++)
        {
            BBox *b1 = &secboxes[j];
            if (b1->xl == INT_MAX || b1->yl == INT_MAX)
                continue;
            if (b1->xh - b1->xl < 64 || b1->yh - b1->yl < 64)
                continue;
            if (b1->xl <= b0->xh && b1->xh >= b0->xl && b1->yl <= b0->yh && b1->yh >= b0->yl)
                continue;

            sweptarea.xl = b0->xl < b1->xl ? b0->xl : b1->xl;
            sweptarea.xh = b0->xh > b1->xh ? b0->xh : b1->xh;
            sweptarea.yl = b0->yl < b1->yl ? b0->yl : b1->yl;
            sweptarea.yh = b0->yh > b1->yh ? b0->yh : b1->yh;

            BPoint points[2][2];
            BBox *bb[2] = {b0, b1};
            for (int bn = 0; bn < 2; bn++)
            {
                int walls[4] = {0, 0, 0, 0};
                if (bb[bn]->xl <= bb[1 - bn]->xl)
                    walls[3] = 1; /* west */
                if (bb[bn]->xh >= bb[1 - bn]->xh)
                    walls[1] = 1; /* east */
                if (bb[bn]->yl <= bb[1 - bn]->yl)
                    walls[2] = 1; /* south */
                if (bb[bn]->yh >= bb[1 - bn]->yh)
                    walls[0] = 1; /* north */
                for (int ss = 0; ss < 5; ss++)
                {
                    int x, y;
                    switch (ss & 3)
                    {
                    case 0:
                        x = bb[bn]->xl;
                        y = bb[bn]->yh;
                        break;
                    case 1:
                        x = bb[bn]->xh;
                        y = bb[bn]->yh;
                        break;
                    case 2:
                        x = bb[bn]->xh;
                        y = bb[bn]->yl;
                        break;
                    default:
                        x = bb[bn]->xl;
                        y = bb[bn]->yl;
                        break;
                    }
                    if (!walls[(ss - 1) & 3] && walls[ss & 3])
                        points[bn][0] = (BPoint){x, y};
                    if (walls[(ss - 1) & 3] && !walls[ss & 3])
                        points[bn][1] = (BPoint){x, y};
                }
                ends[bn] = (BDivLine){points[bn][0].x, points[bn][0].y,
                                      points[bn][1].x - points[bn][0].x, points[bn][1].y - points[bn][0].y};
            }
            sides[0] = (BDivLine){points[0][0].x, points[0][0].y,
                                  points[1][1].x - points[0][0].x, points[1][1].y - points[0][0].y};
            sides[1] = (BDivLine){points[0][1].x, points[0][1].y,
                                  points[1][0].x - points[0][1].x, points[1][0].y - points[0][1].y};
            end0out = !bp(&points[1][0], &ends[0]);
            end1out = !bp(&points[0][0], &ends[1]);
            side0out = !bp(&points[0][1], &sides[0]);
            side1out = !bp(&points[0][0], &sides[1]);

            for (size_t k = 0; k < numchains; k++)
            {
                if (!chain_blocks(&chains[k]))
                    continue;
                size_t bit = i * ns + j;
                (*out)[bit >> 3] |= (u8)(1u << (bit & 7));
                bit = j * ns + i;
                (*out)[bit >> 3] |= (u8)(1u << (bit & 7));
                blocked++;
                break;
            }
        }
    }
    fprintf(stdout, "REJECT: %zu bytes, %zu blocked pairs, %zu chains\n", *outlen, blocked, numchains);
    for (size_t i = 0; i < numchains; i++)
        free(chains[i].points);
    free(chains);
    free(blines);
    free(secboxes);
    chains = NULL;
    blines = NULL;
    secboxes = NULL;
    numchains = numblines = 0;
}

/* --------------------- 3DO RESOURCES OUTPUT --------------------- */
struct TexMap
{
    char name[9];
    u16 id;
    int kind;
};

static struct TexMap *tm;
static size_t ntm;
static u16 null_wall_id = 0xffff;
static u16 wall_first_rez = 0xffff, flat_first_rez = 0xffff;
static u16 wall_last_rez = 0xffff, flat_last_rez = 0xffff;

static int ieq(const char *a, const char *b)
{
    while (*a && *b)
    {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z')
            x = (char)(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z')
            y = (char)(y - 'A' + 'a');
        if (x != y)
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

static void add_tm(const char *name, u16 id, int kind)
{
    tm = xrealloc(tm, (ntm + 1) * sizeof(*tm));
    struct TexMap *t = &tm[ntm++];
    memset(t, 0, sizeof(*t));
    for (size_t i = 0; i < 8 && name[i]; i++)
    {
        char c = name[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        t->name[i] = c;
    }
    t->id = id;
    t->kind = kind;
}

/* Parse extracted REZ manifest.  In the retail data the
   wall resources occupy IDs 2 to 75 and the flat resources 76 to 144.  Map
   resources start at 145.  The runtime map stores the zero based index into
   those two resource runs, not the global REZ resource number. */
static void load_manifest(const char *path)
{
    typedef struct
    {
        u16 rid;
        char name[9]; // Don't ask me why its here, as whoever did it in the 90's :P (probably Carmack)
    } RawRez;

    RawRez *raw = NULL;
    size_t nraw = 0;
    u16 first_flat = 0xffff, first_map = 0xffff;
    FILE *f = fopen(path, "r");
    if (!f)
        fatal("cannot open REZ manifest %s", path);
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        unsigned rid, size, off;
        char name[128], type[32];
        if (sscanf(line, "%u %127s type=%31s size=%u offset=%x", &rid, name, type, &size, &off) < 4)
            continue;
        if (name[0] != 'r')
            continue;
        const char *n = name + 1;
        if (!strcmp(n, "FLAT14") && first_flat == 0xffff)
            first_flat = (u16)rid;
        if (!strcmp(n, "MAP01_THINGS") && first_map == 0xffff)
            first_map = (u16)rid;
        if (rid >= 2 && rid < first_map)
        {
            raw = xrealloc(raw, (nraw + 1) * sizeof(*raw));
            raw[nraw].rid = (u16)rid;
            memset(raw[nraw].name, 0, sizeof raw[nraw].name);
            memcpy(raw[nraw].name, n, 8);
            raw[nraw].name[8] = 0;
            nraw++;
        }
    }
    fclose(f);
    if (first_flat == 0xffff || first_map == 0xffff || first_flat >= first_map)
        fatal("could not locate rFLAT14 and rMAP01_THINGS in REZ manifest");
    wall_first_rez = 2;
    flat_first_rez = first_flat;
    wall_last_rez = (u16)(first_flat - 1);
    flat_last_rez = (u16)(first_map - 1);

    for (size_t i = 0; i < nraw; i++)
    {
        u16 rid = raw[i].rid;
        if (rid >= wall_first_rez && rid < flat_first_rez)
            add_tm(raw[i].name, (u16)(rid - wall_first_rez), 1);
        else if (rid >= flat_first_rez && rid < first_map)
            add_tm(raw[i].name, (u16)(rid - flat_first_rez), 2);
    }
    free(raw);

    null_wall_id = 0xffff;
    for (size_t i = 0; i < ntm; i++)
        if (tm[i].kind == 1 && !strcmp(tm[i].name, "ASH01"))
            null_wall_id = tm[i].id;
    if (null_wall_id == 0xffff)
        fatal("REZ manifest has no ASH01 null-texture resource");
    fprintf(stdout, "REZ IDs: walls %u..%u => 0..%u, flats %u..%u => 0..%u, null wall ASH01=%u\n",
            wall_first_rez, wall_last_rez, (unsigned)(wall_last_rez - wall_first_rez),
            flat_first_rez, flat_last_rez, (unsigned)(flat_last_rez - flat_first_rez), null_wall_id);
}

static void load_tm(const char *path)
{
    if (!path)
        return;
    FILE *f = fopen(path, "r");
    if (!f)
        fatal("cannot open texture map %s", path);
    char line[512];
    while (fgets(line, sizeof line, f))
    {
        char a[64], n[64];
        unsigned id;
        int kind = 0;
        if (line[0] == '#' || sscanf(line, "%63s", a) != 1)
            continue;
        int c = sscanf(line, "%63s %63s %u", a, n, &id);
        if (c == 2)
        {
            strncpy(n, a, sizeof n - 1);
            n[sizeof n - 1] = 0;
            if (sscanf(line, "%*s %u", &id) != 1)
                continue;
        }
        else if (c >= 3)
        {
            if (ieq(a, "texture") || ieq(a, "wall"))
                kind = 1;
            else if (ieq(a, "flat"))
                kind = 2;
            else
                continue;
        }
        add_tm(n, (u16)id, kind);
    }
    fclose(f);
}

static u32 texid(const char raw[8], const char *kind)
{
    char n[9];
    canon(n, raw);
    int want = !strcmp(kind, "flat") ? 2 : 1;
    if (!n[0] || !strcmp(n, "-"))
        return want == 1 ? (u32)null_wall_id : 0xffffffffu;
    if (want == 2 && !strcmp(n, "F_SKY1"))
        return 0xffffffffu;
    for (size_t i = 0; i < ntm; i++)
        if ((tm[i].kind == 0 || tm[i].kind == want) && !strncmp(n, tm[i].name, 8))
            return tm[i].id;
    char *e;
    unsigned long v = strtoul(n, &e, 10);
    if (e && *e == 0 && v <= 65535)
        return (u32)v;
    fatal("missing 3DO resource ID for %s '%s'", kind, n);
    return 0;
}

static void count(FILE *f, size_t n)
{
    if (n > 65535)
        fatal("resource count > 65535");
    wr32(f, (u32)n);
}

static FILE *openo(const char *d, const char *n)
{
    char p[4096];
    snprintf(p, sizeof p, "%s/%s", d, n);
    FILE *f = fopen(p, "wb");
    if (!f)
        fatal("cannot create %s", p);
    return f;
}

static i16 as_i16(int v)
{
    if (v < -32768 || v > 32767)
        fatal("value %d does not fit signed 16-bit DOOM map field", v);
    return (i16)v;
}

// Compacted functions when they are small is cleaner.
static u32 fixed_from_i16(int v) { return (u32)((u32)(u16)as_i16(v) << 16); }
static u32 thing_angle(i16 degrees)
{
    double a = (double)degrees;
    while (a < 0)
        a += 360.0;
    a = fmod(a, 360.0);
    return (u32)((double)a * (4294967296.0 / 360.0));
}

static void write_things(const char *d)
{
    FILE *f = openo(d, "THINGS");
    count(f, g_nthings);
    for (size_t i = 0; i < g_nthings; i++)
    {
        wr32(f, fixed_from_i16(g_things[i].x));
        wr32(f, fixed_from_i16(g_things[i].y));
        wr32(f, thing_angle(g_things[i].angle));
        wr32(f, (u32)(u16)g_things[i].type);
        wr32(f, (u32)(u16)g_things[i].options);
    }
    fclose(f);
}

static void write_vertices(const char *d)
{
    FILE *f = openo(d, "VERTEXES");
    for (size_t i = 0; i < noutverts; i++)
    {
        wr32(f, fixed_from_i16(outverts[i].x));
        wr32(f, fixed_from_i16(outverts[i].y));
    }
    fclose(f);
}

static void write_lines(const char *d)
{
    FILE *f = openo(d, "LINEDEFS");
    count(f, g_nlines);
    for (size_t i = 0; i < g_nlines; i++)
    {
        wr32(f, line_v1[i]);
        wr32(f, line_v2[i]);
        wr32(f, (u32)(i32)(i16)(g_lines[i].flags & ~256));
        wr32(f, (u32)(u16)g_lines[i].special);
        wr32(f, (u32)(u16)g_lines[i].tag);
        wr32(f, line_s1[i]);
        wr32(f, g_lines[i].sidenum[1] < 0 ? 0xffffffffu : (u32)line_s2[i]);
    }
    fclose(f);
}

static void write_sides(const char *d)
{
    FILE *f = openo(d, "SIDEDEFS");
    count(f, nosides);
    for (size_t i = 0; i < nosides; i++)
    {
        struct OutSide *s = &osides[i];
        wr32(f, fixed_from_i16(s->xoff));
        wr32(f, fixed_from_i16(s->yoff));
        wr32(f, texid(s->top, "wall"));
        wr32(f, texid(s->bottom, "wall"));
        wr32(f, texid(s->mid, "wall"));
        wr32(f, s->sector);
    }
    fclose(f);
}

static void write_sectors(const char *d)
{
    FILE *f = openo(d, "SECTORS");
    count(f, nosect);
    for (size_t i = 0; i < nosect; i++)
    {
        struct OutSector *s = &osectors[i];
        wr32(f, fixed_from_i16(s->floorh));
        wr32(f, fixed_from_i16(s->ceilh));
        wr32(f, texid(s->floorpic, "flat"));
        wr32(f, texid(s->ceilpic, "flat"));
        wr32(f, s->light);
        wr32(f, s->special);
        wr32(f, s->tag);
    }
    fclose(f);
}

static void write_segs(const char *d)
{
    FILE *f = openo(d, "SEGS");
    count(f, nosegs);
    for (size_t i = 0; i < nosegs; i++)
    {
        struct OutSeg *s = &osegs[i];
        wr32(f, s->v1);
        wr32(f, s->v2);
        wr32(f, s->angle);
        wr32(f, fixed_from_i16(s->offset));
        wr32(f, s->linedef);
        wr32(f, s->side);
    }
    fclose(f);
}

static void write_ssectors(const char *d)
{
    FILE *f = openo(d, "SSECTORS");
    count(f, noss);
    for (size_t i = 0; i < noss; i++)
    {
        wr32(f, oss[i].num);
        wr32(f, oss[i].first);
    }
    fclose(f);
}

static void write_nodes(const char *d)
{
    FILE *f = openo(d, "NODES");
    count(f, nonodes);
    for (size_t i = 0; i < nonodes; i++)
    {
        struct OutNode *n = &onodes[i];
        wr32(f, fixed_from_i16(n->x));
        wr32(f, fixed_from_i16(n->y));
        wr32(f, fixed_from_i16(n->dx));
        wr32(f, fixed_from_i16(n->dy));
        for (int c = 0; c < 2; c++)
            for (int k = 0; k < 4; k++)
                wr32(f, fixed_from_i16(n->bbox[c][k]));
        wr32(f, n->child[0]);
        wr32(f, n->child[1]);
    }
    fclose(f);
}

static void write_reject(const char *d, const u8 *r, size_t len)
{
    FILE *f = openo(d, "REJECT");
    if (fwrite(r, 1, len, f) != len)
        fatal("REJECT write");
    fclose(f);
}

/* Original DOOMBSP blockmap geometry, reverse engineered to 3DO LongWord lists. */
static int line_contact(int idx, float xl, float xh, float yl, float yh)
{
    struct LineDef *l = &g_lines[idx];
    float x1 = g_vertices[l->v1].x, y1 = g_vertices[l->v1].y, x2 = g_vertices[l->v2].x, y2 = g_vertices[l->v2].y;
    float lxl = fminf(x1, x2), lxh = fmaxf(x1, x2), lyl = fminf(y1, y2), lyh = fmaxf(y1, y2);
   
    if (lxl >= xh || lxh < xl || lyl >= yh || lyh < yl)
        return 0;

    struct DivLine d = {x1, y1, x2 - x1, y2 - y1};
    struct DivLine p1, p2;

    if (d.dy / d.dx > 0)
    {
        p1 = (struct DivLine){xl, yh, 0, 0};
        p2 = (struct DivLine){xh, yl, 0, 0};
    }
    else
    {
        p1 = (struct DivLine){xh, yh, 0, 0};
        p2 = (struct DivLine){xl, yl, 0, 0};
    }
    return point_side(p1.x, p1.y, &d) != point_side(p2.x, p2.y, &d);
}

static void write_blockmap(const char *d)
{
    int minx = INT_MAX, maxx = INT_MIN, miny = INT_MAX, maxy = INT_MIN;
    for (size_t i = 0; i < g_nlines; i++)
    {
        if (g_lines[i].v1 == g_lines[i].v2)
            continue; /* DOOMBSP removes it */
        int x1 = g_vertices[g_lines[i].v1].x, y1 = g_vertices[g_lines[i].v1].y;
        int x2 = g_vertices[g_lines[i].v2].x, y2 = g_vertices[g_lines[i].v2].y;
        if (x1 < minx)
            minx = x1;
        if (x2 < minx)
            minx = x2;
        if (x1 > maxx)
            maxx = x1;
        if (x2 > maxx)
            maxx = x2;
        if (y1 < miny)
            miny = y1;
        if (y2 < miny)
            miny = y2;
        if (y1 > maxy)
            maxy = y1;
        if (y2 > maxy)
            maxy = y2;
    }
    if (minx == INT_MAX)
        fatal("cannot build BLOCKMAP for empty map");
    int orgx = minx - 8, orgy = miny - 8;

    /* Match IDrect/BoundLineStore from the original DOOMBSP. The
       rectangle extents are inclusive, so its width/height are
       (max-min+1), followed by the 8 unit margin on each side. */
    int blockwidth = (maxx - minx + 17 + BLOCKSIZE - 1) / BLOCKSIZE;
    int blockheight = (maxy - miny + 17 + BLOCKSIZE - 1) / BLOCKSIZE;

    if (blockwidth <= 0 || blockheight <= 0 || blockwidth > 65535 || blockheight > 65535)
        fatal("invalid BLOCKMAP dimensions %d x %d", blockwidth, blockheight);
    size_t entries = (size_t)blockwidth * (size_t)blockheight;


    /* REVERSE ENGINEERED: Build the original DOOMBSP PC block list first, a zero thing chain
       slot, contacted linedef numbers, then -1.  The 3DO converter retains
       that as LongWords, but internalises identical lists and
       collapses duplicate zeroes (MAP01 block 338 proves this edge case). */
    typedef struct
    {
        int32_t *v;
        size_t n;
        size_t cap;
    } IVec;

    IVec *lists = xmalloc(entries * sizeof(*lists));
    memset(lists, 0, entries * sizeof(*lists));
    for (size_t bi = 0; bi < entries; bi++)
    {
        int x = (int)(bi % (size_t)blockwidth), y = (int)(bi / (size_t)blockwidth);
        float xl = (float)(orgx + x * BLOCKSIZE), xh = xl + BLOCKSIZE;
        float yl = (float)(orgy + y * BLOCKSIZE), yh = yl + BLOCKSIZE;
        IVec *iv = &lists[bi];
        iv->cap = 16;
        iv->v = xmalloc(iv->cap * sizeof(*iv->v));
        iv->n = 0;
#define PUSHVAL(V)                                             \
    do                                                         \
    {                                                          \
        if (iv->n == iv->cap)                                  \
        {                                                      \
            iv->cap *= 2;                                      \
            iv->v = xrealloc(iv->v, iv->cap * sizeof(*iv->v)); \
        }                                                      \
        iv->v[iv->n++] = (V);                                  \
    } while (0)
        PUSHVAL(0); /* thing chain slot */
        for (size_t li = 0; li < g_nlines; li++)
        {
            if (g_lines[li].v1 == g_lines[li].v2)
                continue; /* DOOMBSP removes it */
            struct LineDef *wl = &g_lines[li];
            float x1 = g_vertices[wl->v1].x, y1 = g_vertices[wl->v1].y;
            float x2 = g_vertices[wl->v2].x, y2 = g_vertices[wl->v2].y;
            int hit = 0;
            if (x1 == x2)
            {
                if (x1 >= xl && x1 < xh)
                {
                    if (y1 < y2)
                        hit = !(y1 >= yh || y2 < yl);
                    else
                        hit = !(y2 >= yh || y1 < yl);
                }
            }
            else if (y1 == y2)
            {
                if (y1 >= yl && y1 < yh)
                {
                    if (x1 < x2)
                        hit = !(x1 >= xh || x2 < xl);
                    else
                        hit = !(x2 >= xh || x1 < xl);
                }
            }
            else if (line_contact((int)li, xl, xh, yl, yh))
                hit = 1;
            if (hit)
            {
                /* Preserve original GenerateBlockList behaviour, including
                   duplicate line 0, then collapse consecutive duplicates. */
                if (iv->n == 0 || iv->v[iv->n - 1] != (int32_t)li)
                    PUSHVAL((int32_t)li);
            }
        }
        PUSHVAL(-1);
#undef PUSHVAL
    }

    /* Internalise identical lists, preserving first seen order. */
    typedef struct
    {
        int32_t *v;
        size_t n;
        u32 offset;
    } UList;

    UList *uniq = NULL;
    size_t nu = 0;
    u32 *offsets = xmalloc(entries * sizeof(*offsets));
    size_t list_words = 0;
    for (size_t bi = 0; bi < entries; bi++)
    {
        IVec *iv = &lists[bi];
        size_t found = (size_t)-1;
        for (size_t j = 0; j < nu; j++)
            if (uniq[j].n == iv->n && !memcmp(uniq[j].v, iv->v, iv->n * sizeof(int32_t)))
            {
                found = j;
                break;
            }
        if (found == (size_t)-1)
        {
            uniq = xrealloc(uniq, (nu + 1) * sizeof(*uniq));
            uniq[nu].v = iv->v;
            uniq[nu].n = iv->n;
            uniq[nu].offset = 0;
            found = nu++;
            list_words += iv->n;
        }
        else
            free(iv->v);
        offsets[bi] = (u32)found;
    }

    u32 header_words = 4, table_words = (u32)entries;
    u32 data_start = (header_words + table_words) * 4;
    u32 cursor = data_start;

    for (size_t i = 0; i < nu; i++)
    {
        uniq[i].offset = cursor;
        cursor += (u32)(uniq[i].n * 4);
    }

    FILE *f = openo(d, "BLOCKMAP");
    wr32(f, fixed_from_i16(orgx));
    wr32(f, (u32)fixed_from_i16(orgy));
    wr32(f, (u32)blockwidth);
    wr32(f, (u32)blockheight);
    for (size_t bi = 0; bi < entries; bi++)
        wr32(f, uniq[offsets[bi]].offset);
    for (size_t i = 0; i < nu; i++)
        for (size_t j = 0; j < uniq[i].n; j++)
            wr32(f, (u32)uniq[i].v[j]);
    fclose(f);

    fprintf(stdout, "BLOCKMAP: %d x %d, %zu blocks, %zu unique lists, %u bytes\n", blockwidth, blockheight, entries, nu, cursor);
    for (size_t i = 0; i < nu; i++)
        free(uniq[i].v);
    free(uniq);
    free(offsets);
    free(lists);
}

static void free_tree(struct BNode *n)
{
    if (!n)
        return;
    if (n->lines)
        free_w(n->lines);
    else
    {
        free_tree(n->front);
        free_tree(n->back);
    }
    free(n);
}

void build_and_write(const char *outdir, const char *texmap, const char *manifest)
{
#ifdef _WIN32
    _mkdir(outdir);
#else
    mkdir(outdir, 0777);
#endif
    if (manifest)
        load_manifest(manifest);
    else
        load_tm(texmap);
    struct WLine *base = make_base();
    struct BNode *root = bsp(base);
    osegs = NULL;
    oss = NULL;
    onodes = NULL;
    nosegs = noss = nonodes = 0;
    noutverts = 0;
    outverts = NULL;
    line_v1 = NULL;
    line_v2 = NULL;
    seed_output_vertices();
    u32 rootidx;
    emit_tree(root, &rootidx);

    if (rootidx != nonodes - 1)
        fatal("internal root ordering error");
        
    build_output_defs();
    u8 *rej;
    size_t rejlen;
    build_reject(&rej, &rejlen);
    fprintf(stdout, "BSP: cuts=%d vertices=%zu segs=%zu subsectors=%zu nodes=%zu output-sides=%zu output-sectors=%zu\n", g_cuts, noutverts, nosegs, noss, nonodes, nosides, nosect);
    write_things(outdir);
    write_vertices(outdir);
    write_lines(outdir);
    write_sides(outdir);
    write_sectors(outdir);
    write_segs(outdir);
    write_ssectors(outdir);
    write_nodes(outdir);
    write_reject(outdir, rej, rejlen);
    write_blockmap(outdir);
    free(rej);
    free_tree(root);
    free(line_v1);
    free(line_v2);
    free(outverts);
    free(osegs);
    free(oss);
    free(onodes);
    free(osides);
    free(osectors);
    free(line_s1);
    free(line_s2);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "DOOMBSP3DO\nUsage: %s input.wad output-dir [--manifest manifest.txt] [--texture-map texture-map.txt] [--little-endian]\n", argv[0]);
        return 2;
    }
    const char *texmap = NULL, *manifest = NULL;
    for (int i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "--little-endian"))
            g_big_endian = 0;
        else if (!strcmp(argv[i], "--manifest"))
        {
            if (++i >= argc)
                fatal("--manifest needs a path");
            manifest = argv[i];
        }
        else if (!strcmp(argv[i], "--texture-map"))
        {
            if (++i >= argc)
                fatal("--texture-map needs a path");
            texmap = argv[i];
        }
        else
            fatal("unknown option %s", argv[i]);
    }
    if (manifest && texmap)
        fatal("use either --manifest or --texture-map, not both");
    load_wad(argv[1]);
    load_map();
    printf("Input %s: %u lumps, %zu things, %zu lines, %zu vertices, %zu sides, %zu sectors\n", g_wad.type, g_wad.count, g_nthings, g_nlines, g_nvertices, g_nsides, g_nsectors);
    printf("3DO serializer: exact retail-derived 32-bit resource layouts; byte order=%s.\n", g_big_endian ? "big-endian" : "little-endian");
    build_and_write(argv[2], texmap, manifest);
    return 0;
}
