#ifndef DOOM3DO_BURGER_H
#define DOOM3DO_BURGER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Exact-width replacements for the scalar types used by the 3DO SDK. */
typedef uint8_t  Byte;
typedef uint8_t  byte;
/*
 * The original 3DO/Burgerlib ABI defines a machine Word as 32 bits.
 * Do not confuse this with serialized 16-bit file/pixel quantities; those
 * are represented explicitly with uint16_t/int16_t where required.
 */
typedef uint32_t Word;
typedef int16_t  Short;
typedef int32_t  Long;
typedef uint32_t LongWord;
typedef int32_t  Fixed;
typedef uint32_t Boolean;
typedef int32_t  int32;
typedef uint32_t uint32;
typedef uint32_t ulong;
typedef void    *Item;
typedef void    *Handle;
typedef int32_t   Err;

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
_Static_assert(sizeof(Byte) == 1, "3DO ABI: Byte must be 8 bits");
_Static_assert(sizeof(Short) == 2, "3DO ABI: Short must be 16 bits");
_Static_assert(sizeof(Word) == 4, "3DO ABI: Word must be 32 bits");
_Static_assert(sizeof(Long) == 4, "3DO ABI: Long must be 32 bits");
_Static_assert(sizeof(LongWord) == 4, "3DO ABI: LongWord must be 32 bits");
_Static_assert(sizeof(Fixed) == 4, "3DO ABI: Fixed must be 32 bits");
_Static_assert(sizeof(Boolean) == 4, "3DO ABI: Boolean must be 32 bits");
#endif

#ifndef TRUE
#define TRUE  1u
#endif
#ifndef FALSE
#define FALSE 0u
#endif

#ifndef TICKSPERSEC
#define TICKSPERSEC 60u
#endif

/* 3DO controller bit masks. These are the physical bits; DOOM remaps them
 * through PadAttack/PadUse/PadSpeed in O_Init(). */
/*
 * Portfolio event.h ControlPad values.
 *
 * These are 3DO event-system Word masks, not compact host-local bit
 * positions.  The original Doom3DO demo resources contain these values
 * verbatim (for example 0x00100000 for Up and 0x02000000 for B).
 *
 * Keep the exact 32-bit masks so recorded/replayed demos and the original
 * NetToLocal/LocalToNet translation remain compatible with the 3DO build.
 */
#define PadUp          ((Word)0x00100000u)
#define PadDown        ((Word)0x00200000u)
#define PadLeft        ((Word)0x00400000u)
#define PadRight       ((Word)0x00800000u)
#define PadA           ((Word)0x01000000u)
#define PadB           ((Word)0x02000000u)
#define PadC           ((Word)0x04000000u)
#define PadD           ((Word)0x08000000u)
#define PadStart       ((Word)0x10000000u)
#define PadX           ((Word)0x20000000u)
#define PadLeftShift   ((Word)0x40000000u)
#define PadRightShift  ((Word)0x80000000u)

extern Word PadAttack;
extern Word PadUse;
extern Word PadSpeed;

void InitTools(void);
void UpdateAndPageFlip(void);
LongWord ReadTick(void);
Word ReadJoyButtons(Word PadNum);

void *LoadAResource(Word RezNum);
void **LoadAResourceHandle(Word RezNum);
void *LockAHandle(void *handle);
LongWord GetAHandleSize(void *handle);
LongWord GetAResourceSize(Word RezNum);
void ReleaseAResource(Word RezNum);
void KillAResource(Word RezNum);

void *AllocAPointer(size_t size);
void DeallocAPointer(void *ptr);
void PurgeHandles(Word flags);
void CompactHandles(void);

void *GetShapeIndexPtr(void *ShapePtr, Word index);
Word GetShapeWidth(void *ShapePtr);
Word GetShapeHeight(void *ShapePtr);

void DrawMShape(Word x, Word y, void *ShapePtr);
void DrawWeaponShape(int x, int y, void *ShapePtr, LongWord Size, Fixed ScaleX, Fixed ScaleY, Boolean Shadow);
void DrawRezShape(Word x, Word y, Word RezNum);
void DrawShape(Word x, Word y, void *ShapePtr);
void DrawARect(Word x1, Word y1, Word Width, Word Height, Word color);
void DrawSkyLine(void);
void DrawWallColumn(Word y, Word Colnum, Byte *Source, Word Run);
void DrawFloorColumn(Word ds_y, Word ds_x1, Word Count,
                     LongWord xfrac, LongWord yfrac, Fixed ds_xstep, Fixed ds_ystep);
void DrawSpriteCenter(Word SpriteNum);
void DrawColors(void);
void DrawPlaque(Word RezNum);
void D3DO_ShowPausePlaque(void);
void EnableHardwareClipping(void);
void DisableHardwareClipping(void);
void DrawASpan(Word Count, LongWord xfrac, LongWord yfrac,
               Fixed ds_xstep, Fixed ds_ystep, Byte *Dest);
void SetAPixel(Word x, Word y, Word color);
void LongWordToAscii(LongWord value, Byte *text);

void PlaySound(Word sound);
void StopSound(Word sound);
void PlaySong(Word song);
void PauseMusic(void);
void ResumeMusic(void);
void SetSfxVolume(Word volume);
void SetMusicVolume(Word volume);
extern Word SfxVolume;
extern Word MusicVolume;
extern Word LeftVolume;
extern Word RightVolume;

Word GetRandom(Word max);
void Randomize(void);

int32 StdReadFile(char *fName, char *buf);
int32 SaveAFile(Byte *name, void *data, LongWord size);

#endif


#ifndef DOOM3DO_COLORS
#define DOOM3DO_COLORS
#define BLACK 0u
#define LIGHTGREY 1u
#define RED 2u
#define GREEN 3u
#define BLUE 4u
#define YELLOW 5u
#define BROWN 6u
#define LILAC 7u
#endif
