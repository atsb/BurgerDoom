/*
 * DOOM 3DO - modern SDL3 hardware layer
 *
 * This file retains Rebecca Heineman's original threedo.c module boundary,
 * replacing only the 3DO Portfolio/CEL/audio hardware services with
 * portable SDL3/Linux implementations.
 */

#include "Doom.h"
#include "IntMath.h"
#include "String.h"

#define LIGHTSCALESHIFT 3

#define SKYSCALE(x) (Fixed)(1048576.0 * ((x) / 160.0))

static Fixed SkyScales[6] = {
    SKYSCALE(160.0),
    SKYSCALE(144.0),
    SKYSCALE(128.0),
    SKYSCALE(112.0),
    SKYSCALE(96.0),
    SKYSCALE(80.0)
};


#include "ResourceMgr.h"
#include "celutils.h"
#include "wildmidi_lib.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#include <time.h>

#if defined(_WIN32)
#include <string.h>
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif
#else
#include <strings.h>
#endif

/*
 * Host state used by the 3DO compatibility layer.
 */
Word WorkPage = 0;
LongWord LastTics = 1;

static SDL_Window *gWindow = NULL;
static SDL_Renderer *gRenderer = NULL;
static SDL_Texture *gTexture = NULL;
#if SDL_VERSION_ATLEAST(3,4,0)
static SDL_GPUDevice *gGPUDevice = NULL;
static SDL_GPUShader *gCRTShader = NULL;
static SDL_GPURenderState *gCRTRenderState = NULL;
#endif
static bool gCRTFilterEnabled = false;
static uint16_t *gFramebuffer = NULL;
static bool gFullscreen = false;
static int gWindowedWidth = 960;
static int gWindowedHeight = 600;
static _Thread_local uint16_t *gD3DO_RasterFramebuffer = NULL;
static uint32_t *gPresentPixels = NULL;
static uint16_t *gDisplayedFramebuffer = NULL;
static uint16_t *gWipeNewFramebuffer = NULL;
static uint16_t *gWipeOutputFramebuffer = NULL;
static Byte *VideoPointer = NULL;


Byte SpanArray[MAXSCREENWIDTH*MAXSCREENHEIGHT];
Byte *SpanPtr = SpanArray;

#define DOOM3DO_WIDTH  320
#define DOOM3DO_HEIGHT 200
#define DOOM3DO_VIEW_HEIGHT 160
#define D3DO_SYNTH_SPRITE_MAGIC 0x53525054u 

static uint64_t gLastTickCounter = 0;
static uint64_t gTickFrequency = 0;


#define D3DO_SYSTEM_HZ        60u
#define D3DO_STEPDOWN_HZ      30u
#define D3DO_STEPDOWN_VBLS    (D3DO_SYSTEM_HZ / D3DO_STEPDOWN_HZ)
static uint32_t gStepDownVBLAccum = 0;
static ResourceMgr gResourceMgr;
static int gResourceMgrReady = 0;
static ResourceMgr gPrezResourceMgr;
static int gPrezResourceMgrReady = 0;
static char gRezFilePath[PATH_MAX];
static char gPrezFilePath[PATH_MAX];
extern Word tx_x;
extern int tx_scale;

static Word gCurrentPad = 0;
static Word gPreviousPad = 0;
static Word gInputPulse = 0;
static int gKeyboardWeapon = -1;
static int gAutomapDirect = 0;
static bool gTabDown = false;

/*
 * Read big-endian 3DO scalar values from serialized host data.
 */
static uint16_t ReadBE16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static int16_t ReadBES16(const uint8_t *p)
{
    return (int16_t)ReadBE16(p);
}

static uint32_t ReadBE32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static int32_t ReadBES32(const uint8_t *p)
{
    return (int32_t)ReadBE32(p);
}

static void WriteBE32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static uint32_t ARGB1555ToARGB8888(uint16_t c)
{
    if ((c & 0x8000u) == 0 && c != 0)
        c |= 0x8000u;

    {
        uint32_t r = (c >> 10) & 31u;
        uint32_t g = (c >> 5) & 31u;
        uint32_t b = c & 31u;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

static uint16_t *D3DO_GetRasterFramebuffer(void)
{
    if (gD3DO_RasterFramebuffer)
        return gD3DO_RasterFramebuffer;
    return gFramebuffer;
}

static void SetPixel16(int x, int y, uint16_t color)
{
    uint16_t *framebuffer = D3DO_GetRasterFramebuffer();
    if (framebuffer && x >= 0 && x < DOOM3DO_WIDTH && y >= 0 && y < DOOM3DO_HEIGHT)
        framebuffer[y * DOOM3DO_WIDTH + x] = color;
}

static void FillRect16(int x, int y, int w, int h, uint16_t color)
{
    uint16_t *framebuffer = D3DO_GetRasterFramebuffer();
    int yy, xx;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w;
    int y1 = y + h;
    if (!framebuffer)
        return;
    if (x1 > DOOM3DO_WIDTH) x1 = DOOM3DO_WIDTH;
    if (y1 > DOOM3DO_HEIGHT) y1 = DOOM3DO_HEIGHT;
    for (yy = y0; yy < y1; ++yy)
        for (xx = x0; xx < x1; ++xx)
            framebuffer[yy * DOOM3DO_WIDTH + xx] = color;
}

static uint16_t D3DO_AutomapColor(Word color)
{
    static const uint16_t colors[] = {
        0x0000u, 0x6318u, 0x6800u, 0x0380u,
        0x001Eu, 0x7BC0u, 0x4102u, 0x6A9Eu
    };
    if (color < (Word)(sizeof(colors) / sizeof(colors[0])))
        return colors[color];
    return 0xFFFFu;
}

static void DrawLine16(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        SetPixel16(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = err * 2;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

static uint16_t D3DO_RGBA5551ToARGB1555(uint16_t c)
{
    const uint16_t r = (uint16_t)((c >> 11) & 31u);
    const uint16_t g = (uint16_t)((c >> 6) & 31u);
    const uint16_t b = (uint16_t)((c >> 1) & 31u);
    return (uint16_t)(0x8000u | (r << 10) | (g << 5) | b);
}


static SDL_Gamepad *gGamepad = NULL;
static SDL_JoystickID gGamepadID = 0;
static bool gMouseRelativeEnabled = false;
static double gMouseDXAccum = 0.0;


static angle_t gMouseTurnForTic = 0;

#define DOOM3DO_MOUSE_TURN_SCALE_NUM 1
#define DOOM3DO_MOUSE_TURN_SCALE_DEN 8
#define DOOM3DO_GAMEPAD_DEADZONE 8000
#define DOOM3DO_GAMEPAD_TRIGGER 16000

#define MAX_SOUND_CHANNELS 32
#define D3DO_AUDIO_RATE 48000u
typedef struct {
    const uint8_t *buffer;
    uint32_t numSamples;
    uint32_t sampleRate;
    uint32_t numChannels;
    uint32_t bitDepth;
    size_t bufferSize;
    uint8_t *ownedBuffer;
} D3DO_AudioData;

typedef struct {
    D3DO_AudioData *audio;
    uint64_t curSampleFrac;
    uint64_t stepFrac;
    float lVolume;
    float rVolume;
    int looped;
    int active;
} D3DO_AudioVoice;

static D3DO_AudioData gSamples[NUMSFX];
static D3DO_AudioData gMusicData;
static D3DO_AudioVoice gSoundVoices[MAX_SOUND_CHANNELS];
static D3DO_AudioVoice gMusicVoice;
static SDL_AudioStream *gAudioStream = NULL;
static SDL_Mutex *gAudioMutex = NULL;
static D3DO_AudioData gStartupMovieAudioData;
static D3DO_AudioVoice gStartupMovieAudioVoice;
static Word gSfxVolume = 15;
static Word gMusicVolume = 15;
static int gMusicPaused = 0;
static int gMusicPlaying = 0;
static midi *gMidiSong = NULL;
static int gMidiPlaying = 0;

Word SfxVolume = 15;
Word MusicVolume = 15;
Word LeftVolume = 255;
Word RightVolume = 255;

/*
 * Convert a 3DO automap colour and write one framebuffer pixel.
 */
void SetAPixel(Word x, Word y, Word color)
{
    
    SetPixel16((int)x, (int)y,
               D3DO_RGBA5551ToARGB1555(D3DO_AutomapColor(color)));
}

void D3DO_SetAutomapDirect(int enabled)
{
    gAutomapDirect = enabled ? 1 : 0;
}

static const ResourceMgr *D3DO_FindResourceManager(uint32_t number)
{
    if (gPrezResourceMgrReady &&
        ResourceMgr_Get(&gPrezResourceMgr, number))
        return &gPrezResourceMgr;

    if (gResourceMgrReady &&
        ResourceMgr_Get(&gResourceMgr, number))
        return &gResourceMgr;

    return NULL;
}

static ResourceMgr *D3DO_FindMutableResourceManager(uint32_t number)
{
    if (gPrezResourceMgrReady &&
        ResourceMgr_Get(&gPrezResourceMgr, number))
        return &gPrezResourceMgr;

    if (gResourceMgrReady &&
        ResourceMgr_Get(&gResourceMgr, number))
        return &gResourceMgr;

    return NULL;
}

void *LoadAResource(Word RezNum)
{
    ResourceMgr *manager = D3DO_FindMutableResourceManager((uint32_t)RezNum);
    const ResourceMgr_Resource *r;

    if (!manager)
        return NULL;

    r = ResourceMgr_Load(manager, (uint32_t)RezNum);
    return r ? r->data : NULL;
}

void **LoadAResourceHandle(Word RezNum)
{
    ResourceMgr *manager = D3DO_FindMutableResourceManager((uint32_t)RezNum);

    if (!manager)
        return NULL;

    return ResourceMgr_LoadHandle(manager, (uint32_t)RezNum);
}

void *LockAHandle(void *handle)
{
    void **h = (void **)handle;
    return h ? *h : NULL;
}

static LongWord D3DO_GetHandleSizeFromManager(const ResourceMgr *manager, void *handle)
{
    size_t i;

    if (!manager || !handle)
        return 0;

    for (i = 0; i < manager->count; ++i) {
        uint8_t *data = (uint8_t *)manager->resources[i].data;
        uint8_t *end;

        if (!data || !manager->resources[i].size)
            continue;

        end = data + manager->resources[i].size;

        if ((uint8_t *)handle >= data && (uint8_t *)handle < end)
            return (LongWord)(end - (uint8_t *)handle);

        if (manager->resources[i].handle &&
            handle == (void *)manager->resources[i].handle)
            return (LongWord)manager->resources[i].size;

        if (handle == (void *)&manager->resources[i].data)
            return (LongWord)manager->resources[i].size;
    }

    return 0;
}

LongWord GetAHandleSize(void *handle)
{
    LongWord size;

    if (!handle)
        return 0;

    size = D3DO_GetHandleSizeFromManager(&gPrezResourceMgr, handle);
    if (size)
        return size;

    return D3DO_GetHandleSizeFromManager(&gResourceMgr, handle);
}

LongWord GetAResourceSize(Word RezNum)
{
    const ResourceMgr *manager = D3DO_FindResourceManager((uint32_t)RezNum);
    const ResourceMgr_Resource *resource;

    if (!manager)
        return 0;

    resource = ResourceMgr_Get(manager, (uint32_t)RezNum);
    return resource ? (LongWord)resource->size : 0;
}

void ReleaseAResource(Word RezNum)
{
    ResourceMgr *manager = D3DO_FindMutableResourceManager((uint32_t)RezNum);
    if (manager)
        ResourceMgr_Free(manager, (uint32_t)RezNum);
}

void KillAResource(Word RezNum)
{
    ReleaseAResource(RezNum);
}

void *AllocAPointer(size_t size)
{
    return calloc(1, size ? size : 1);
}

void DeallocAPointer(void *ptr)
{
    free(ptr);
}

void PurgeHandles(Word flags)
{
    
    (void)flags;
}

void CompactHandles(void)
{
    
}

static uint16_t gRndSeed = 0;
static uint16_t gRndIndexI = 16;
static uint16_t gRndIndexJ = 4;

static uint16_t gRndArray[17] = {
    1,1,2,3,5,8,13,21,54,75,129,204,323,527,850,1377,2227
};

static const uint16_t gBaseRndArray[17] = {
    1,1,2,3,5,8,13,21,54,75,129,204,323,527,850,1377,2227
};

/*
 * Return the deterministic Burgerlib-compatible random value in 0..max.
 */
Word GetRandom(Word max)
{
    uint16_t NewVal;
    uint16_t i;
    uint16_t j;
    uint32_t product;

    if (!max) {
        return 0;
    }

    ++max;                         
    i = gRndIndexI;
    j = gRndIndexJ;

    NewVal = (uint16_t)(gRndArray[i] + gRndArray[j]);
    gRndArray[i] = NewVal;

    NewVal = (uint16_t)(NewVal + gRndSeed);
    gRndSeed = NewVal;

    --i;
    --j;

    if (i & 0x8000u) {
        i = 16;
    }
    if (j & 0x8000u) {
        j = 16;
    }

    gRndIndexI = i;
    gRndIndexJ = j;

    
    NewVal &= 0xFFFFu;
    max &= 0xFFFFu;

    if (!max) {
        return NewVal;
    }

    
    product = (uint32_t)NewVal * (uint32_t)max;
    return (Word)(product >> 16);
}

void Randomize(void)
{
    memcpy(gRndArray, gBaseRndArray, sizeof(gRndArray));
    gRndSeed = 0;
    gRndIndexI = 16;
    gRndIndexJ = 4;

    
    (void)GetRandom(255);
}

/*
 * Return elapsed time in the original 3DO tick domain.
 */
LongWord ReadTick(void)
{
    uint64_t now;
    if (!gTickFrequency)
        return 0;
    now = SDL_GetPerformanceCounter();
    return (LongWord)((now * TICKSPERSEC) / gTickFrequency);
}

static void UpdateElapsedTicks(void)
{
    uint64_t now;
    uint64_t elapsed;
    uint64_t ticks;
    uint32_t stepVBLS;

    
    do {
        now = SDL_GetPerformanceCounter();
        elapsed = now - gLastTickCounter;
        ticks = gTickFrequency ?
            (elapsed * (uint64_t)D3DO_SYSTEM_HZ) / gTickFrequency : 0;
    } while (ticks == 0);

    gLastTickCounter = now;
    gStepDownVBLAccum += (uint32_t)ticks;

    
    stepVBLS = gStepDownVBLAccum;
    if (stepVBLS < D3DO_STEPDOWN_VBLS) {
        
        LastTics = 0;
        return;
    }

    
    LastTics = (LongWord)stepVBLS;
    gStepDownVBLAccum = 0;
}

static void D3DO_OpenFirstGamepad(void)
{
    int count = 0;
    SDL_JoystickID *ids;

    if (gGamepad && SDL_GamepadConnected(gGamepad)) {
        return;
    }

    if (gGamepad) {
        SDL_CloseGamepad(gGamepad);
        gGamepad = NULL;
        gGamepadID = 0;
    }

    ids = SDL_GetGamepads(&count);
    if (!ids) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        if (!SDL_IsGamepad(ids[i])) {
            continue;
        }
        gGamepad = SDL_OpenGamepad(ids[i]);
        if (gGamepad) {
            gGamepadID = ids[i];
            break;
        }
    }
    SDL_free(ids);
}

static bool D3DO_SetFullscreen(bool fullscreen)
{
    if (!gWindow || fullscreen == gFullscreen)
        return true;

    if (fullscreen) {
        SDL_GetWindowSize(gWindow, &gWindowedWidth, &gWindowedHeight);
        if (!SDL_SetWindowFullscreen(gWindow, true)) {
            fprintf(stderr, "DOOM3DO: failed to enter fullscreen: %s\n", SDL_GetError());
            return false;
        }
    } else {
        if (!SDL_SetWindowFullscreen(gWindow, false)) {
            fprintf(stderr, "DOOM3DO: failed to leave fullscreen: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetWindowSize(gWindow, gWindowedWidth, gWindowedHeight);
    }

    gFullscreen = fullscreen;
    return true;
}

static void D3DO_ToggleFullscreen(void)
{
    if (D3DO_SetFullscreen(!gFullscreen))
        WritePrefsFile();
}

static void D3DO_ProcessInputEvents(void)
{
    SDL_Event e;

    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            exit(0);
            break;

        case SDL_EVENT_KEY_DOWN:
            if (!e.key.repeat && e.key.key == SDLK_ESCAPE) {
                exit(0);
            }
            if (!e.key.repeat && e.key.key == SDLK_F &&
                (e.key.mod & SDL_KMOD_CTRL)) {
                D3DO_ToggleFullscreen();
                break;
            }
            if (!e.key.repeat) {
                switch (e.key.scancode) {
                case SDL_SCANCODE_1:
                    gKeyboardWeapon = -1;
                    if (gamepaused) break;
                    gKeyboardWeapon = 0;
                    break;
                case SDL_SCANCODE_2: gKeyboardWeapon = 1; break;
                case SDL_SCANCODE_3: gKeyboardWeapon = 2; break;
                case SDL_SCANCODE_4: gKeyboardWeapon = 3; break;
                case SDL_SCANCODE_5: gKeyboardWeapon = 4; break;
                case SDL_SCANCODE_6: gKeyboardWeapon = 5; break;
                case SDL_SCANCODE_7: gKeyboardWeapon = 6; break;
                default: break;
                }
            }
            if (!e.key.repeat && e.key.scancode == SDL_SCANCODE_E) {
                
                gInputPulse |= PadUse;
            }
            break;

        case SDL_EVENT_GAMEPAD_ADDED:
            if (!gGamepad) {
                gGamepad = SDL_OpenGamepad(e.gdevice.which);
                if (gGamepad) {
                    gGamepadID = e.gdevice.which;
                }
            }
            break;

        case SDL_EVENT_GAMEPAD_REMOVED:
            if (gGamepad && gGamepadID == e.gdevice.which) {
                SDL_CloseGamepad(gGamepad);
                gGamepad = NULL;
                gGamepadID = 0;
                D3DO_OpenFirstGamepad();
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            
            if (gMouseRelativeEnabled) {
                gMouseDXAccum += (double)e.motion.xrel;
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            
            if (e.wheel.y > 0.0f) {
                gInputPulse |= (Word)(PadUse | PadRightShift);
            } else if (e.wheel.y < 0.0f) {
                gInputPulse |= (Word)(PadUse | PadLeftShift);
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_MIDDLE) {
                gInputPulse |= PadX;
            }
            if (e.button.button == SDL_BUTTON_RIGHT) {
                
                gInputPulse |= PadUse;
            }
            break;

        default:
            break;
        }
    }
}

static bool D3DO_DeadzonePositive(Sint16 value)
{
    return value > DOOM3DO_GAMEPAD_DEADZONE;
}

static bool D3DO_DeadzoneNegative(Sint16 value)
{
    return value < -DOOM3DO_GAMEPAD_DEADZONE;
}

static void D3DO_UpdateMouseMode(void)
{
    bool wantRelative;

    if (!gWindow) {
        return;
    }

    
    wantRelative = (players.mo != NULL) &&
                   !(players.AutomapFlags & AF_OPTIONSACTIVE);

    if (wantRelative != gMouseRelativeEnabled) {
        if (SDL_SetWindowRelativeMouseMode(gWindow, wantRelative)) {
            gMouseRelativeEnabled = wantRelative;
        }
    }
}

void D3DO_BeginTicInput(void)
{
    double dx;

    
    SDL_PumpEvents();
    D3DO_ProcessInputEvents();
    D3DO_OpenFirstGamepad();
    D3DO_UpdateMouseMode();

    if (DemoPlayback || DemoRecording) {
        gMouseDXAccum = 0.0;
        gMouseTurnForTic = 0;
        return;
    }

    dx = gMouseDXAccum;
    gMouseDXAccum = 0.0;
    if (dx > 64.0) dx = 64.0;
    if (dx < -64.0) dx = -64.0;

    
    gMouseTurnForTic = (angle_t)llround(
        -dx * (double)(600 << FRACBITS) *
        ((double)DOOM3DO_MOUSE_TURN_SCALE_NUM /
         (double)DOOM3DO_MOUSE_TURN_SCALE_DEN));
}

int D3DO_GetKeyboardWeapon(void)
{
    int weapon = gKeyboardWeapon;
    gKeyboardWeapon = -1;
    return weapon;
}


angle_t D3DO_GetMouseTurnForTic(void)
{
    angle_t turn = gMouseTurnForTic;
    gMouseTurnForTic = 0;
    return turn;
}

Word ReadJoyButtons(Word PadNum)
{
    const bool *keys;
    SDL_MouseButtonFlags mouseButtons;
    Sint16 leftX = 0;
    Sint16 leftY = 0;
    Sint16 rightX = 0;
    Sint16 leftTrigger = 0;
    Sint16 rightTrigger = 0;

    (void)PadNum;

    SDL_PumpEvents();
    D3DO_ProcessInputEvents();
    D3DO_OpenFirstGamepad();
    D3DO_UpdateMouseMode();

    keys = SDL_GetKeyboardState(NULL);

    gPreviousPad = gCurrentPad;
    gCurrentPad = gInputPulse;
    gInputPulse = 0;

    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) gCurrentPad |= PadUp;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) gCurrentPad |= PadDown;
    if (keys[SDL_SCANCODE_A]) gCurrentPad |= PadLeftShift;
    if (keys[SDL_SCANCODE_D]) gCurrentPad |= PadRightShift;

    if (keys[SDL_SCANCODE_LEFT])  gCurrentPad |= PadLeft;
    if (keys[SDL_SCANCODE_RIGHT]) gCurrentPad |= PadRight;

    if (keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT]) {
        gCurrentPad |= PadSpeed;
    }
    if (keys[SDL_SCANCODE_SPACE]) gCurrentPad |= PadAttack;
    if (keys[SDL_SCANCODE_E])     gCurrentPad |= PadUse;

    
    {
        const bool tabDown = keys[SDL_SCANCODE_TAB];
        if (tabDown && !gTabDown) {
            
            gInputPulse |= (Word)(PadUse | PadStart);
        }
        gTabDown = tabDown;
    }

    
    if (keys[SDL_SCANCODE_M]) gCurrentPad |= PadX;

    if (keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER]) {
        gCurrentPad |= PadStart;
    }

    if (keys[SDL_SCANCODE_Q]) {
        gCurrentPad |= (Word)(PadUse | PadLeftShift);
    }
    if (keys[SDL_SCANCODE_R]) {
        gCurrentPad |= (Word)(PadUse | PadRightShift);
    }

    if (keys[SDL_SCANCODE_LALT]) gCurrentPad |= PadLeftShift;
    if (keys[SDL_SCANCODE_RALT]) gCurrentPad |= PadRightShift;

    mouseButtons = SDL_GetRelativeMouseState(NULL, NULL);
    if (mouseButtons & SDL_BUTTON_LMASK) gCurrentPad |= PadAttack;
    if (mouseButtons & SDL_BUTTON_RMASK) gCurrentPad |= PadUse;

    {
        const bool rawUse = keys[SDL_SCANCODE_E] ||
            ((mouseButtons & SDL_BUTTON_RMASK) != 0);
        const bool wasUse = (gPreviousPad & PadUse) != 0;
        if (rawUse && !wasUse) {
            
            gInputPulse |= PadUse;
        }
    }
    if (mouseButtons & SDL_BUTTON_MMASK) gCurrentPad |= PadX;
    if (mouseButtons & SDL_BUTTON_X1MASK) gCurrentPad |= PadLeftShift;
    if (mouseButtons & SDL_BUTTON_X2MASK) gCurrentPad |= PadRightShift;

    if (gGamepad && SDL_GamepadConnected(gGamepad)) {
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_DPAD_UP)) {
            gCurrentPad |= PadUp;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) {
            gCurrentPad |= PadDown;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) {
            gCurrentPad |= PadLeft;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) {
            gCurrentPad |= PadRight;
        }

        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_SOUTH)) {
            gCurrentPad |= PadAttack;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_EAST)) {
            gCurrentPad |= PadUse;
            if (!(gPreviousPad & PadUse)) {
                gInputPulse |= PadUse;
            }
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_WEST)) {
            gCurrentPad |= PadSpeed;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_NORTH)) {
            gCurrentPad |= PadX;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_START)) {
            gCurrentPad |= PadStart;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_BACK)) {
            gCurrentPad |= PadX;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) {
            gCurrentPad |= PadLeftShift;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
            gCurrentPad |= PadRightShift;
        }
        if (SDL_GetGamepadButton(gGamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK)) {
            gCurrentPad |= PadSpeed;
        }

        leftX = SDL_GetGamepadAxis(gGamepad, SDL_GAMEPAD_AXIS_LEFTX);
        leftY = SDL_GetGamepadAxis(gGamepad, SDL_GAMEPAD_AXIS_LEFTY);
        rightX = SDL_GetGamepadAxis(gGamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        leftTrigger = SDL_GetGamepadAxis(gGamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        rightTrigger = SDL_GetGamepadAxis(gGamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

        
        if (D3DO_DeadzoneNegative(leftX)) {
            gCurrentPad |= PadLeftShift;
        } else if (D3DO_DeadzonePositive(leftX)) {
            gCurrentPad |= PadRightShift;
        }
        if (D3DO_DeadzoneNegative(leftY)) {
            gCurrentPad |= PadUp;
        } else if (D3DO_DeadzonePositive(leftY)) {
            gCurrentPad |= PadDown;
        }

        if (D3DO_DeadzoneNegative(rightX)) {
            gCurrentPad |= PadLeft;
        } else if (D3DO_DeadzonePositive(rightX)) {
            gCurrentPad |= PadRight;
        }

        if (leftTrigger > DOOM3DO_GAMEPAD_TRIGGER) {
            gCurrentPad |= PadUse;
        }
        if (rightTrigger > DOOM3DO_GAMEPAD_TRIGGER) {
            gCurrentPad |= PadAttack;
        }

    }

    return gCurrentPad;
}

/* ------------------------------------------------------------------------- */
/* 3DO CEL support / shapes                                                   */
/* ------------------------------------------------------------------------- */

Word GetShapeWidth(void *ShapePtr)
{
    if (!ShapePtr) return 0;
    return D3DO_CelWidth((const D3DO_CCB *)ShapePtr);
}

Word GetShapeHeight(void *ShapePtr)
{
    const uint8_t *ccb;
    uint32_t flags, next, source, plut;
    if (!ShapePtr) return 0;
    ccb=(const uint8_t *)ShapePtr;
    flags=ReadBE32(ccb+0u);
    next=ReadBE32(ccb+4u);
    source=ReadBE32(ccb+8u);
    plut=ReadBE32(ccb+12u);
    /* Our reconstructed world-sprite CEL is identifiable without changing
     * the real 3DO CCB representation.  The legacy row-major form exposes
     * its screen width in PRE1; the corrected sideways form exposes it in
     * CEL height, exactly as Rebecca's renderer expects. */
    if(flags==0x000007FFu && source==572u && plut==60u) {
        uint32_t pre0=ReadBE32(ccb+52u);
        uint32_t pre1=ReadBE32(ccb+56u);
        if(next==D3DO_SYNTH_SPRITE_MAGIC)
            return (Word)(((pre0>>6)&0x03FFu)+1u);
        return (Word)((pre1&0x07FFu)+1u);
    }
    return D3DO_CelHeight((const D3DO_CCB *)ShapePtr);
}

void DrawRezShape(Word x, Word y, Word RezNum)
{
    void *data = LoadAResource(RezNum);
    if (data) {
        DrawMShape(x, y, data);
    }
    ReleaseAResource(RezNum);
}

void *GetShapeIndexPtr(void *ShapePtr, Word index)
{
    uint32_t *offsets = (uint32_t *)ShapePtr;
    if (!ShapePtr) return NULL;
    return (uint8_t *)ShapePtr + (ReadBE32((const uint8_t *)&offsets[index]) & 0x3FFFFFFFu);
}

/*
 * -------------------------------------------------------------------------
 * 3DO Cel Engine software compatibility layer
 * -------------------------------------------------------------------------
 *
 * Rebecca's renderer intentionally feeds the Cel Engine rather than drawing
 * ordinary host pixels. The following code reproduces the relevant CCB
 * semantics in software:
 *
 *   - CCB coordinate formats
 *   - PRE0 4/8/16bpp selection
 *   - PRE1 source pixel count
 *   - packed 4bpp nibble ordering
 *   - CCB affine H/V deltas
 *   - BGND masking semantics
 *   - the exact 3DO sky/wall source addressing
 *   - the original DOOM3DO light-table values
 *
 * The host renderer preserves this ordered CCB submission boundary.  Three
 * render pages mirror Rebecca's original three-screen paging model while the
 * CLIO and MADAM worker threads process completed CCB batches asynchronously.
 */



/*
 * Render an already-decoded CEL according to its CCB affine transform.
 *
 * HDX/HDY are 12.20 values. VDX/VDY are 16.16 values.
 */
/*
 * Software equivalent of DrawMShape/DrawShape. Rebecca changes only CCB_BGND
 * at these call sites and then queues the CCB. We reproduce exactly that
 * distinction while resolving the transform from the serialized CCB.
 */

/*
 * -------------------------------------------------------------------------
 * TRUE 3DO CCB COMPATIBILITY LAYER
 * -------------------------------------------------------------------------
 * Rebecca's original renderer creates 3DO CCBs and submits them as an
 * ordered list to DrawCels().  This host layer preserves that boundary.
 */
#define D3DO_CCB_BGND   0x00000020u

/* ------------------------------------------------------------------------- */
/* Software representation of Rebecca's 60-byte 3DO CCB.                  */
/* The serialized CCB stays exactly 60 bytes; this runtime object is only   */
/* the resolved host-side command plus the hardware state needed by CEL.    */
/* ------------------------------------------------------------------------- */

#define CCB_SKIP      0x80000000u
#define CCB_LAST      0x40000000u
#define CCB_NPABS     0x20000000u
#define CCB_SPABS     0x10000000u
#define CCB_PPABS     0x08000000u
#define CCB_LDSIZE    0x04000000u
#define CCB_LDPRS     0x02000000u
#define CCB_LDPPMP    0x01000000u
#define CCB_LDPLUT    0x00800000u
#define CCB_CCBPRE    0x00400000u
#define CCB_YOXY      0x00200000u
#define CCB_ACSC      0x00100000u
#define CCB_ALSC      0x00080000u
#define CCB_ACW       0x00040000u
#define CCB_ACCW      0x00020000u
#define CCB_TWD       0x00010000u
#define CCB_LCE       0x00008000u
#define CCB_ACE       0x00004000u
#define CCB_MARIA     0x00001000u
#define CCB_PXOR      0x00000800u
#define CCB_USEAV     0x00000400u
#define CCB_PACKED    0x00000200u
#define CCB_POVERMASK 0x00000180u
#define CCB_PLUTPOS   0x00000040u
#define CCB_BGND      0x00000020u
#define CCB_NOBLK     0x00000010u

#define D3DO_CCB_TOTAL 0x200u

typedef struct {
    uint32_t flags;
    const uint8_t *ccbBase;
    const uint8_t *source;
    uint32_t sourceSize;
    const uint8_t *plut;
    int32_t xPos,yPos;
    int32_t hdx,hdy,vdx,vdy;
    int32_t hddx,hddy;
    uint32_t pixc,pre0,pre1;
    uint16_t color;
    uint8_t colorMode;
    uint8_t kind;
    uint8_t masked;
    int clipEnabled;
    int clipX1,clipY1,clipX2,clipY2;
    const Word *columnClip;
    int columnClipFirstX;
    /* Our reconstructed raw-8bpp sprite resources are a host-side
     * representation of a 3DO CEL.  These fields let MADAM consume the
     * row-major source without pretending it is a packed CEL. */
    uint8_t raw8;
    uint16_t raw8Width;
    uint16_t raw8Height;
    uint16_t raw8Stride;
    uint16_t raw8Column;
    uint8_t raw8Sideways;
} D3DO_CCBCommand;

enum {
    D3DO_CCB_GENERIC=0,
    D3DO_CCB_SKY=1,
    D3DO_CCB_WALL=2,
    D3DO_CCB_PLANE=3,
    D3DO_CCB_COLOR=4,
    D3DO_CCB_LINE=5,
    D3DO_CCB_RECT=6,
    D3DO_CCB_COPY16=7,
    /* A degenerate 3DO CEL used by Rebecca for a clipped sprite column.
     * It is not a new graphics primitive; it selects the MADAM host
     * emulation path that models the real one-column sideways CEL. */
    D3DO_CCB_SPRITE_COLUMN=8,
    D3DO_CCB_SPRITE_CLIPPED=9,
    D3DO_CCB_PAUSE_OVERLAY=10
};

/* ------------------------------------------------------------------------- */
/* Host-side CLIO and MADAM page pipeline                                   */
/* ------------------------------------------------------------------------- */

#define D3DO_RENDER_PAGE_COUNT        3u
#define D3DO_RENDER_BATCHES_PER_PAGE 32u
#define D3DO_RENDER_QUEUE_SIZE      128u


typedef struct {
    Word screenSize;
    Word width;
    Word height;
    Word xOffset;
    Word yOffset;
    Fixed skyScale;
} D3DO_ViewportState;

typedef struct {
    int32_t xPos,yPos;
    int32_t hdx,hdy,vdx,vdy;
    int32_t hddx,hddy;
    uint32_t pixc;
    uint32_t pre0,pre1;
    uint32_t ppFlags;
    uint16_t plut[32];
    int plutValid;
} D3DO_CELState;

typedef struct {
    D3DO_CCBCommand ccb;
    D3DO_CELState cel;
} D3DO_MADAMPacket;

typedef struct {
    uint32_t count;
    int finalBatch;
    D3DO_ViewportState viewport;
    D3DO_CELState startState;
    D3DO_CCBCommand commands[D3DO_CCB_TOTAL];
    D3DO_MADAMPacket packets[D3DO_CCB_TOTAL];
} D3DO_RenderBatch;

typedef struct D3DO_RenderPage D3DO_RenderPage;

typedef struct {
    D3DO_RenderPage *page;
    uint32_t batchIndex;
} D3DO_RenderJob;

enum {
    D3DO_PAGE_FREE = 0,
    D3DO_PAGE_BUILDING,
    D3DO_PAGE_INFLIGHT,
    D3DO_PAGE_COMPLETE
};

struct D3DO_RenderPage {
    uint16_t *framebuffer;
    Byte *spanArray;
    D3DO_RenderBatch *batches;
    uint32_t batchCount;
    uint32_t currentBatch;
    uint32_t spanOffset;
    Word *spriteClipArena;
    uint32_t spriteClipUsed;
    uint32_t spriteClipCapacity;
    D3DO_ViewportState viewport;
    D3DO_CELState buildState;
    uint64_t frameSerial;
    uint32_t completedBatches;
    int finalSubmitted;
    int state;
};

typedef struct {
    D3DO_RenderPage pages[D3DO_RENDER_PAGE_COUNT];
    int currentPage;
    uint64_t nextFrameSerial;
    uint64_t displayedFrameSerial;
    SDL_Mutex *mutex;
    SDL_Condition *clioCondition;
    SDL_Condition *madamCondition;
    SDL_Condition *pageCondition;
    SDL_Condition *queueCondition;
    D3DO_RenderJob clioQueue[D3DO_RENDER_QUEUE_SIZE];
    D3DO_RenderJob madamQueue[D3DO_RENDER_QUEUE_SIZE];
    uint32_t clioHead;
    uint32_t clioTail;
    uint32_t madamHead;
    uint32_t madamTail;
    SDL_Thread *clioThread;
    SDL_Thread *madamThread;
    int shutdown;
    int workersReady;
} D3DO_RenderSystem;

static D3DO_RenderSystem gD3DO_Render;
static _Thread_local D3DO_CELState gD3DO_CEL;
static _Thread_local D3DO_ViewportState gD3DO_WorkViewport;
static D3DO_CELState gD3DO_BuildCEL;
static uint32_t D3DO_CCBCount;

static D3DO_ViewportState D3DO_CaptureViewport(void)
{
    D3DO_ViewportState viewportState;
    viewportState.screenSize=ScreenSize;
    viewportState.width=ScreenWidth;
    viewportState.height=ScreenHeight;
    viewportState.xOffset=ScreenXOffset;
    viewportState.yOffset=ScreenYOffset;
    viewportState.skyScale=SkyScales[ScreenSize<6u?ScreenSize:5u];
    return viewportState;
}

static void FlushCCBs(void);
static int D3DO_StartupAssetPath(const char *filename, char *path, size_t pathSize);
#if SDL_VERSION_ATLEAST(3,4,0)
static int D3DO_CRTShaderPath(char *path, size_t pathSize);
static void D3DO_ShutdownCRTFilter(void);
#endif

static int PresentFramebuffer(const uint16_t *buffer);
static int PresentFrame(void);
static void D3DO_ApplyCCBState(D3DO_CELState *celState,
                                const D3DO_CCBCommand *command);

static int D3DO_QueueIsFull(uint32_t queueHead,uint32_t queueTail)
{
    return (queueHead-queueTail)>=D3DO_RENDER_QUEUE_SIZE;
}

static int D3DO_QueuePush(D3DO_RenderJob *renderQueue,uint32_t *queueHead,uint32_t queueTail,
                          const D3DO_RenderJob *renderJob)
{
    if (D3DO_QueueIsFull(*queueHead,queueTail))
        return 0;
    renderQueue[*queueHead%D3DO_RENDER_QUEUE_SIZE]=*renderJob;
    ++*queueHead;
    return 1;
}

static int D3DO_QueuePop(D3DO_RenderJob *renderQueue,uint32_t *queueTail,uint32_t queueHead,
                         D3DO_RenderJob *renderJob)
{
    if (*queueTail==queueHead)
        return 0;
    *renderJob=renderQueue[*queueTail%D3DO_RENDER_QUEUE_SIZE];
    ++*queueTail;
    return 1;
}

static D3DO_RenderPage *D3DO_CurrentPage(void)
{
    if (gD3DO_Render.currentPage<0)
        return NULL;
    return &gD3DO_Render.pages[gD3DO_Render.currentPage];
}

static const Word D3DO_LightTable[32] = {
    0x0000,0x0400,0x0800,0x0C00,0x1000,0x1400,0x1800,0x1C00,
    0x00D0,0x00D0,0x1300,0x1300,0x08D0,0x08D0,0x1700,0x1700,
    0x10D0,0x10D0,0x1B00,0x1B00,0x18D0,0x18D0,0x1F00,0x1F00,
    0x1F00,0x1F00,0x1F00,0x1F00,0x1F00,0x1F00,0x1F00,0x1F00
};

static void D3DO_ResetCEL(void)
{
    memset(&gD3DO_CEL,0,sizeof(gD3DO_CEL));
    gD3DO_CEL.hdx=1<<20;
    gD3DO_CEL.vdy=1<<16;
    gD3DO_CEL.pixc=0x1F00u;
    gD3DO_CEL.plutValid=0;
    gD3DO_BuildCEL=gD3DO_CEL;
}

static uint16_t D3DO_ApplyPIXC(uint16_t color,uint32_t pixc)
{
    uint32_t pixcValue=(uint32_t)(pixc&0xFFFFu);
    int sourceRed=(color>>10)&31;
    int sourceGreen=(color>>5)&31;
    int sourceBlue=color&31;

    /*
     * Rebecca's LightTable is not a linear table of arbitrary brightness
     * values.  It contains literal 3DO PIXC programs:
     *
     *   0x0000..0x1C00 : source * MF / DF
     *   0x00D0/08D0/...: source * MF / DF + source / 2
     *   0x1F00          : source unchanged
     *
     * These are the actual 3DO pixel-processor operations encoded by the
     * original renderer, so reproduce those operations rather than mapping
     * the table index to an invented RGB scale.
     */
    if(pixcValue==0x1F00u)
        return (uint16_t)(0x8000u|(sourceRed<<10)|(sourceGreen<<5)|sourceBlue);

    if(pixcValue==0x9C81u) {
        /*
         * Rebecca uses this PIXC for shadow cels.  In the original hardware
         * program the decoder is the primary source at 8/16 strength and the
         * frame buffer is the secondary source at 1/2 strength with subtract
         * selected.  The exact 3DO framebuffer interaction is handled by the
         * host rasterizer where the destination pixel is available; this
         * fallback preserves the source-side half-strength value.
         */
        sourceRed>>=1; sourceGreen>>=1; sourceBlue>>=1;
        return (uint16_t)(0x8000u|(sourceRed<<10)|(sourceGreen<<5)|sourceBlue);
    }

    {
        uint32_t multiplyFactor=((pixcValue>>10)&7u)+1u;
        uint32_t divideCode=(pixcValue>>8)&3u;
        uint32_t divideFactor=(divideCode==0u)?16u:(divideCode==1u)?2u:
                    (divideCode==2u)?4u:8u;
        uint32_t secondaryMode=(pixcValue>>6)&3u;
        uint32_t alphaValue=(pixcValue>>1)&31u;
        int resultRed=(sourceRed*(int)multiplyFactor)/(int)divideFactor;
        int resultGreen=(sourceGreen*(int)multiplyFactor)/(int)divideFactor;
        int resultBlue=(sourceBlue*(int)multiplyFactor)/(int)divideFactor;

        if(secondaryMode==3u && alphaValue==8u) {
            /* USEAV=1, SDV=2, add secondary decoder source. */
            resultRed += sourceRed>>1;
            resultGreen += sourceGreen>>1;
            resultBlue += sourceBlue>>1;
        } else if(secondaryMode==1u) {
            resultRed += (int)alphaValue;
            resultGreen += (int)alphaValue;
            resultBlue += (int)alphaValue;
        }

        if(resultRed>31)resultRed=31; if(resultGreen>31)resultGreen=31; if(resultBlue>31)resultBlue=31;
        return (uint16_t)(0x8000u|(resultRed<<10)|(resultGreen<<5)|resultBlue);
    }
}

static void D3DO_BeginBatch(D3DO_RenderPage *page)
{
    D3DO_RenderBatch *batch=&page->batches[page->currentBatch];
    memset(batch,0,sizeof(*batch));
    batch->viewport=page->viewport;
    batch->startState=page->buildState;
    D3DO_CCBCount=0;
}

static void D3DO_EnqueueCLIOJob(D3DO_RenderPage *page,uint32_t batchIndex)
{
    D3DO_RenderJob job;
    job.page=page;
    job.batchIndex=batchIndex;

    SDL_LockMutex(gD3DO_Render.mutex);
    while (D3DO_QueueIsFull(gD3DO_Render.clioHead,gD3DO_Render.clioTail) &&
           !gD3DO_Render.shutdown)
        SDL_WaitCondition(gD3DO_Render.queueCondition,gD3DO_Render.mutex);
    if (!gD3DO_Render.shutdown) {
        (void)D3DO_QueuePush(gD3DO_Render.clioQueue,&gD3DO_Render.clioHead,
                              gD3DO_Render.clioTail,&job);
        SDL_SignalCondition(gD3DO_Render.clioCondition);
    }
    SDL_UnlockMutex(gD3DO_Render.mutex);
}

static void D3DO_SubmitCurrentBatch(int finalBatch)
{
    D3DO_RenderPage *page=D3DO_CurrentPage();
    D3DO_RenderBatch *batch;

    if (!page || D3DO_CCBCount==0u) {
        if (page && finalBatch) {
            batch=&page->batches[page->currentBatch];
            memset(batch,0,sizeof(*batch));
            batch->viewport=page->viewport;
            batch->startState=gD3DO_BuildCEL;
            batch->finalBatch=1;
            ++page->batchCount;
            page->finalSubmitted=1;
            page->state=D3DO_PAGE_INFLIGHT;
            D3DO_EnqueueCLIOJob(page,page->currentBatch);
        }
        D3DO_CCBCount=0;
        return;
    }

    batch=&page->batches[page->currentBatch];
    batch->count=D3DO_CCBCount;
    batch->finalBatch=finalBatch;
    ++page->batchCount;
    D3DO_EnqueueCLIOJob(page,page->currentBatch);

    if (!finalBatch) {
        ++page->currentBatch;
        if(page->currentBatch>=D3DO_RENDER_BATCHES_PER_PAGE) {
            page->state=D3DO_PAGE_INFLIGHT;
            return;
        }
        D3DO_BeginBatch(page);
    } else {
        page->finalSubmitted=1;
        page->state=D3DO_PAGE_INFLIGHT;
        D3DO_CCBCount=0;
    }
}

static void D3DO_QueueCCB(const D3DO_CCBCommand *command)
{
    D3DO_RenderPage *renderPage=D3DO_CurrentPage();
    D3DO_RenderBatch *renderBatch;

    if(!command || !renderPage) return;
    if(D3DO_CCBCount>=D3DO_CCB_TOTAL)
        D3DO_SubmitCurrentBatch(FALSE);
    if(D3DO_CCBCount>=D3DO_CCB_TOTAL || renderPage->currentBatch>=D3DO_RENDER_BATCHES_PER_PAGE)
        return;

    renderBatch=&renderPage->batches[renderPage->currentBatch];
    renderBatch->commands[D3DO_CCBCount]=*command;
    D3DO_ApplyCCBState(&gD3DO_BuildCEL,command);
    renderPage->buildState=gD3DO_BuildCEL;
    ++D3DO_CCBCount;
}

/* Equivalent of Rebecca's AddCCB pointer munge. */
static int D3DO_ResolveCCB(
    D3DO_CCBCommand *outputCommand,const uint8_t *ccbData,uint32_t size)
{
    uint32_t ccbFlags,sourceOffset,plutOffset;

    if(!outputCommand || !ccbData || size<60u) return 0;

    memset(outputCommand,0,sizeof(*outputCommand));
    ccbFlags=ReadBE32(ccbData+0u);
    sourceOffset=ReadBE32(ccbData+8u);
    plutOffset=ReadBE32(ccbData+12u);

    outputCommand->ccbBase=ccbData;

    /*
     * Resource reconstruction note:
     *
     * makerez builds a deliberately simple sprite shape consisting of a
     * 60-byte CCB, a 512-byte PLUT and row-major 8bpp pixels.  Older REZFILEs
     * contain the CCB ccbFlags as the low 11-bit PACKED marker set (0x7ff),
     * rather than the actual 3DO flag word.  The old resolver consequently
     * rejected the CEL outright because no CCB load ccbFlags were present.
     *
     * Recognise that exact reconstructed representation and translate it to
     * the real CCB semantics at the host boundary.  This is not a second
     * renderer: it is decoding our serialized sprite resource into the same
     * runtime CCB state the original AddCCB() would consume.
     */
    if ((ccbFlags & 0xFFFFF800u) == 0u &&
        ccbFlags == 0x000007FFu && sourceOffset == 572u && plutOffset == 60u &&
        size >= 60u + 512u + 4u) {
        const uint32_t canonical =
            CCB_SPABS|CCB_PPABS|CCB_LDSIZE|CCB_LDPRS|CCB_LDPPMP|
            CCB_LDPLUT|CCB_CCBPRE|CCB_YOXY|CCB_ACW|CCB_ACCW|
            CCB_ACE|CCB_BGND|CCB_NOBLK;
        outputCommand->flags=canonical;
        outputCommand->source=ccbData+sourceOffset;
        outputCommand->plut=ccbData+plutOffset;
        outputCommand->sourceSize=size-sourceOffset;
        outputCommand->raw8=1;
        outputCommand->raw8Width=(uint16_t)((ReadBE32(ccbData+56u)&0x07FFu)+1u);
        outputCommand->raw8Height=(uint16_t)(((ReadBE32(ccbData+52u)>>6)&0x03FFu)+1u);
        outputCommand->raw8Stride=outputCommand->raw8Width;
        outputCommand->raw8Column=0;
        outputCommand->raw8Sideways=(ReadBE32(ccbData+4u)==D3DO_SYNTH_SPRITE_MAGIC);

        /* Corrected reconstructed resources are stored sideways like the
         * original 3DO sprite CELs: screen width == CEL height and screen
         * height == CEL width.  The source is already transposed in that
         * representation, so the existing Rebecca transform can be used. */

        /* Early reconstructed REZFILEs encoded only the 8bpp PRE0 mode
         * bits and therefore left the height field at its default value of
         * one.  The source payload is a contiguous row-major image with
         * only final 32-bit padding, so for the reconstructed sprite format
         * we can recover the authoritative height without changing the 3DO
         * CCB contract.  This also lets old REZFILEs remain usable while the
         * resource builder is corrected. */
        if(outputCommand->raw8Height<=1u && outputCommand->raw8Width>=4u &&
           outputCommand->sourceSize>(uint32_t)outputCommand->raw8Width*2u) {
            uint32_t inferred=outputCommand->sourceSize/(uint32_t)outputCommand->raw8Width;
            if(inferred>1u && inferred<=1024u)
                outputCommand->raw8Height=(uint16_t)inferred;
        }
    } else {
        outputCommand->flags=ccbFlags;
        if(ccbFlags&CCB_SPABS)
            outputCommand->source=(sourceOffset<size)?ccbData+sourceOffset:NULL;
        else
            outputCommand->source=(12u+sourceOffset<size)?ccbData+12u+sourceOffset:NULL;

        if(ccbFlags&CCB_LDPLUT) {
            if(ccbFlags&CCB_PPABS)
                outputCommand->plut=(plutOffset<size)?ccbData+plutOffset:NULL;
            else
                outputCommand->plut=(16u+plutOffset<size)?ccbData+16u+plutOffset:NULL;
        }

        outputCommand->sourceSize=outputCommand->source?
            size-(uint32_t)(outputCommand->source-ccbData):0u;
    }

    outputCommand->xPos=ReadBES32(ccbData+16u);
    outputCommand->yPos=ReadBES32(ccbData+20u);
    outputCommand->hdx=ReadBES32(ccbData+24u);
    outputCommand->hdy=ReadBES32(ccbData+28u);
    outputCommand->vdx=ReadBES32(ccbData+32u);
    outputCommand->vdy=ReadBES32(ccbData+36u);
    outputCommand->hddx=ReadBES32(ccbData+40u);
    outputCommand->hddy=ReadBES32(ccbData+44u);
    outputCommand->pixc=ReadBE32(ccbData+48u);
    outputCommand->pre0=ReadBE32(ccbData+52u);
    outputCommand->pre1=ReadBE32(ccbData+56u);

    outputCommand->masked=((outputCommand->flags&CCB_BGND)==0u);
    outputCommand->clipX1=0; outputCommand->clipY1=0;
    outputCommand->clipX2=DOOM3DO_WIDTH-1; outputCommand->clipY2=DOOM3DO_HEIGHT-1;
    return outputCommand->source!=NULL;
}

/* Apply only the CCB fields whose load bits are set, just as the CEL engine
 * does. This state persists across DrawCels() calls. */
static void D3DO_ApplyCCBState(D3DO_CELState *celState,
                                 const D3DO_CCBCommand *command)
{
    uint32_t i;

    if(!celState || !command) return;

    if(command->flags&CCB_LDSIZE) {
        celState->hdx=command->hdx; celState->hdy=command->hdy;
        celState->vdx=command->vdx; celState->vdy=command->vdy;
    }
    if(command->flags&CCB_LDPRS) {
        celState->hddx=command->hddx; celState->hddy=command->hddy;
    }
    if(command->flags&CCB_LDPPMP) {
        celState->pixc=command->pixc;
        celState->ppFlags=command->flags;
    }
    if(command->flags&CCB_LDPLUT) {
        if(command->plut) {
            for(i=0;i<32u;++i)
                celState->plut[i]=ReadBE16(command->plut+i*2u);
            celState->plutValid=1;
        }
    }
    if(command->flags&CCB_YOXY) {
        celState->xPos=command->xPos;
        celState->yPos=command->yPos;
    }
    if(command->flags&CCB_CCBPRE) {
        celState->pre0=command->pre0;
        celState->pre1=command->pre1;
    } else if(command->source && command->sourceSize>=4u) {
        /* Source-resident preambles. Packed CELs carry one preamble word;
         * unpacked CELs carry two. */
        celState->pre0=ReadBE32(command->source);
        if(!(command->flags&CCB_PACKED) && command->sourceSize>=8u)
            celState->pre1=ReadBE32(command->source+4u);
    }
}



static void D3DO_RasterWallCCB(const D3DO_CCBCommand *command)
{
    uint32_t sourcePixelSkip=(command->pre0>>24)&7u;
    uint32_t pixelCount=(command->pre1&0x0FFFu)+1u;
    uint32_t verticalStep=(uint32_t)gD3DO_CEL.hdy;
    int screenX=gD3DO_CEL.xPos>>16;
    uint32_t i;
    if(!command->source || !gD3DO_CEL.plutValid || !pixelCount || !verticalStep) return;
    for(i=0;i<pixelCount;++i){
        uint32_t sourcePixelIndex=sourcePixelSkip+i;
        uint8_t packedPixels=command->source[sourcePixelIndex>>1];
        uint32_t paletteIndex=(sourcePixelIndex&1u)?(packedPixels&0x0Fu):(packedPixels>>4);
        uint16_t pixelColor=gD3DO_CEL.plut[paletteIndex];
        int32_t topPixel=(int32_t)(((uint32_t)gD3DO_CEL.yPos+
            (uint32_t)(((uint64_t)i*verticalStep)>>4))>>16);
        int32_t bottomPixel=(int32_t)(((uint32_t)gD3DO_CEL.yPos+
            (uint32_t)(((uint64_t)(i+1u)*verticalStep)>>4))>>16);
        int32_t screenY;
        if(bottomPixel<=topPixel)bottomPixel=topPixel+1;
        pixelColor=D3DO_ApplyPIXC(pixelColor,gD3DO_CEL.pixc);
        for(screenY=topPixel;screenY<bottomPixel;++screenY)SetPixel16(screenX,screenY,pixelColor);
    }
}



static void D3DO_RasterSkyCCB(const D3DO_CCBCommand *command)
{
    uint32_t screenY;
    if(!command->source || !gD3DO_CEL.plutValid)return;
    for(screenY=0;screenY<128u;++screenY){
        uint32_t sourceY=(uint32_t)(((uint64_t)screenY*(uint32_t)gD3DO_CEL.hdy)>>20);
        uint8_t packedPixels; uint32_t paletteIndex; uint16_t pixelColor;
        if(sourceY>=128u)sourceY=127u;
        packedPixels=command->source[sourceY>>1];
        paletteIndex=(sourceY&1u)?(packedPixels&0x0Fu):(packedPixels>>4);
        pixelColor=D3DO_ApplyPIXC(gD3DO_CEL.plut[paletteIndex],gD3DO_CEL.pixc);
        SetPixel16(gD3DO_CEL.xPos>>16,
            (int)(gD3DO_CEL.yPos>>16)+(int)screenY,pixelColor);
    }
}



static void D3DO_RasterPlaneCCB(const D3DO_CCBCommand *command)
{
    uint32_t pixelCount=(command->pre1&0x0FFFu)+1u;
    uint32_t i;
    if(!command->source || !gD3DO_CEL.plutValid)return;
    for(i=0;i<pixelCount;++i){
        int screenX=(gD3DO_CEL.xPos>>16)+(int)i;
        int screenY=gD3DO_CEL.yPos>>16;
        if(screenX<0||screenX>=DOOM3DO_WIDTH||screenY<0||screenY>=DOOM3DO_HEIGHT)continue;
        SetPixel16(screenX,screenY,D3DO_ApplyPIXC(gD3DO_CEL.plut[command->source[i]],gD3DO_CEL.pixc));
    }
}



static void D3DO_FillQuad(int x0,int y0,int x1,int y1,
                          int x2,int y2,int x3,int y3,uint16_t color)
{
    int minY=y0,maxY=y0;
    int ys[4]={y1,y2,y3};
    int xs[4]={x0,x1,x2,x3};
    int i;

    for(i=0;i<3;++i){
        if(ys[i]<minY)minY=ys[i];
        if(ys[i]>maxY)maxY=ys[i];
    }
    if(minY<0)minY=0;
    if(maxY>=DOOM3DO_HEIGHT)maxY=DOOM3DO_HEIGHT-1;

    for(i=minY;i<=maxY;++i){
        double scan=(double)i+0.5;
        double left[4],right[4];
        int hits=0,j;
        int px[4]={x0,x1,x2,x3};
        int py[4]={y0,y1,y2,y3};

        for(j=0;j<4;++j){
            int k=(j+1)&3;
            if((py[j]<=scan && py[k]>scan) ||
               (py[k]<=scan && py[j]>scan)){
                double t=(scan-py[j])/(double)(py[k]-py[j]);
                double xx=px[j]+(px[k]-px[j])*t;
                left[hits]=xx; right[hits]=xx; ++hits;
            }
        }
        if(hits>=2){
            double a=left[0],bb=left[1];
            int lx=(int)ceil(a<bb?a:bb);
            int rx=(int)floor(a<bb?bb:a);
            if(lx<0)lx=0; if(rx>=DOOM3DO_WIDTH)rx=DOOM3DO_WIDTH-1;
            for(;lx<=rx;++lx) SetPixel16(lx,i,color);
        }
    }
}

static int D3DO_DecodeRaw8Sprite(const D3DO_CCBCommand *command,
                                  uint16_t **out, uint16_t *outputWidth, uint16_t *outputHeight)
{
    uint16_t *decodedPixels;
    uint32_t sourceRow;
    if (!command || !command->raw8 || !command->source || !command->plut || !out || !outputWidth || !outputHeight)
        return 0;
    if (!command->raw8Width || !command->raw8Height ||
        command->raw8Stride < command->raw8Column + command->raw8Width)
        return 0;
    if ((uint64_t)command->raw8Stride * command->raw8Height > command->sourceSize)
        return 0;
    decodedPixels=(uint16_t *)calloc((size_t)command->raw8Width*command->raw8Height,sizeof(*decodedPixels));
    if (!decodedPixels) return 0;
    for (sourceRow=0; sourceRow<command->raw8Height; ++sourceRow) {
        uint32_t sourceColumn;
        for (sourceColumn=0; sourceColumn<command->raw8Width; ++sourceColumn) {
            uint8_t paletteIndex=command->source[(size_t)sourceRow*command->raw8Stride + command->raw8Column + sourceColumn];
            decodedPixels[(size_t)sourceRow*command->raw8Width+sourceColumn]=
                ReadBE16(command->plut+(size_t)paletteIndex*2u);
        }
    }
    *out=decodedPixels; *outputWidth=command->raw8Width; *outputHeight=command->raw8Height;
    return 1;
}

static int D3DO_ColumnClipBounds(const D3DO_CCBCommand *command, int screenX,
                                   int *outTop, int *outBottom)
{
    int clipIndex;
    Word clipValue;
    int clipTop, clipBottom;

    if(!outTop || !outBottom)
        return 0;

    if(!command || !command->columnClip) {
        *outTop=ScreenYOffset;
        *outBottom=ScreenYOffset+(int)ScreenHeight;
        return *outTop<*outBottom;
    }

    clipIndex=screenX-command->columnClipFirstX;
    if(clipIndex<0 || clipIndex>=MAXSCREENWIDTH)
        return 0;

    clipValue=command->columnClip[clipIndex];
    if(clipValue==(Word)ScreenHeight) {
        *outTop=ScreenYOffset;
        *outBottom=ScreenYOffset+(int)ScreenHeight;
        return *outTop<*outBottom;
    }

    clipTop=(int)(clipValue>>8);
    clipBottom=(int)(clipValue&0xFFu);
    if(clipTop>=clipBottom)
        return 0;

    *outTop=clipTop+ScreenYOffset;
    *outBottom=clipBottom+ScreenYOffset;
    return *outTop<*outBottom;
}

static void D3DO_RasterClippedSpriteCCB(const D3DO_CCBCommand *command)
{
    uint16_t *decodedPixels=NULL,spriteWidth=0,spriteHeight=0;
    uint16_t sourceRow;
    const int32_t ox=gD3DO_CEL.xPos;
    const int32_t oy=gD3DO_CEL.yPos;
    const int32_t hdx=gD3DO_CEL.hdx;
    const int32_t hdy=gD3DO_CEL.hdy;
    const int32_t vdx=gD3DO_CEL.vdx;
    const int32_t vdy=gD3DO_CEL.vdy;
    const int32_t hddx=gD3DO_CEL.hddx;
    const int32_t hddy=gD3DO_CEL.hddy;

    if(!command || !command->source || !command->sourceSize)
        return;

    /*
     * CRITICAL: use the same decoder as the normal sprite path.  Genuine
     * retail 3DO CELs are not our synthetic row-major format and must be
     * decoded through D3DO_DecodeCelResolved().
     */
    if(command->raw8) {
        if(!D3DO_DecodeRaw8Sprite(command,&decodedPixels,&spriteWidth,&spriteHeight))
            return;
    } else {
        if(!D3DO_DecodeCelResolved(
                command->source,command->sourceSize,
                gD3DO_CEL.plutValid ? (const uint8_t *)gD3DO_CEL.plut : command->plut,
                gD3DO_CEL.pre0,gD3DO_CEL.pre1,command->flags,0,
                &decodedPixels,&spriteWidth,&spriteHeight))
            return;
    }

    /*
     * Rebecca's sprite clipping is defined in destination screen columns by
     * spropening[].  A scaled sprite source sample can cover several physical
     * X decodedPixels, so testing only its centre is wrong: a wall can clip one edge
     * of that sample while the other edge remains visible.  In the real 3DO
     * the CEL engine/painter path handles the projected geometry; our host
     * emulator must therefore test the actual destination X column for every
     * framebuffer pixel covered by the projected sample.
     *
     * World-sprite CCBs are axis-aligned (HDX/VDY zero in DrawVisSprite), but
     * the generic formula below preserves H/V deltas and remains valid for the
     * current renderer's sprite transform.
     */
    for(sourceRow=0;sourceRow<spriteHeight;++sourceRow){
        uint16_t sourceColumn;
        int64_t rowHdx=(int64_t)hdx+(int64_t)hddx*sourceRow;
        int64_t rowHdy=(int64_t)hdy+(int64_t)hddy*sourceRow;

        for(sourceColumn=0;sourceColumn<spriteWidth;++sourceColumn){
            uint16_t pixelColor=decodedPixels[(size_t)sourceRow*spriteWidth+sourceColumn];
            int64_t aX=ox+((rowHdx*(int64_t)sourceColumn)>>4)+(int64_t)vdx*sourceRow;
            int64_t aY=oy+((rowHdy*(int64_t)sourceColumn)>>4)+(int64_t)vdy*sourceRow;
            int64_t bX=ox+((rowHdx*(int64_t)(sourceColumn+1u))>>4)+(int64_t)vdx*sourceRow;
            int64_t bY=oy+((rowHdy*(int64_t)(sourceColumn+1u))>>4)+(int64_t)vdy*sourceRow;
            int64_t nextRowHdx=rowHdx+hddx;
            int64_t nextRowHdy=rowHdy+hddy;
            int64_t cX=ox+((nextRowHdx*(int64_t)sourceColumn)>>4)+(int64_t)vdx*(sourceRow+1u);
            int64_t cY=oy+((nextRowHdy*(int64_t)sourceColumn)>>4)+(int64_t)vdy*(sourceRow+1u);
            int64_t dX=ox+((nextRowHdx*(int64_t)(sourceColumn+1u))>>4)+(int64_t)vdx*(sourceRow+1u);
            int64_t dY=oy+((nextRowHdy*(int64_t)(sourceColumn+1u))>>4)+(int64_t)vdy*(sourceRow+1u);
            int minimumX=(int)(aX>>16), maximumX=(int)(aX>>16);
            int minimumY=(int)(aY>>16), maximumY=(int)(aY>>16);
            int screenX,screenY;

            if(bX<minimumX) minimumX=(int)(bX>>16);
            if(bX>maximumX) maximumX=(int)(bX>>16);
            if(cX<minimumX) minimumX=(int)(cX>>16);
            if(cX>maximumX) maximumX=(int)(cX>>16);
            if(dX<minimumX) minimumX=(int)(dX>>16);
            if(dX>maximumX) maximumX=(int)(dX>>16);
            if(bY<((int64_t)minimumY<<16)) minimumY=(int)(bY>>16); if(bY>((int64_t)maximumY<<16)) maximumY=(int)(bY>>16);
            if(cY<((int64_t)minimumY<<16)) minimumY=(int)(cY>>16); if(cY>((int64_t)maximumY<<16)) maximumY=(int)(cY>>16);
            if(dY<((int64_t)minimumY<<16)) minimumY=(int)(dY>>16); if(dY>((int64_t)maximumY<<16)) maximumY=(int)(dY>>16);

            /* Treat the projected sample as a half-open framebuffer rectangle. */
            if(maximumX<minimumX) maximumX=minimumX;
            if(maximumY<=minimumY) maximumY=minimumY+1;

            if(minimumX<0) minimumX=0;
            if(maximumX>=DOOM3DO_WIDTH) maximumX=DOOM3DO_WIDTH-1;
            if(minimumY<0) minimumY=0;
            if(maximumY>DOOM3DO_HEIGHT) maximumY=DOOM3DO_HEIGHT;
            if(minimumX>maximumX || minimumY>=maximumY)
                continue;

            if(command->masked && (pixelColor&0x7FFFu)==0u)
                continue;

            pixelColor=D3DO_ApplyPIXC(pixelColor,gD3DO_CEL.pixc);

            for(screenX=minimumX;screenX<=maximumX;++screenX){
                int topClip,bottomClip;
                int drawTop,drawBottom;

                if(!D3DO_ColumnClipBounds(command,screenX,&topClip,&bottomClip))
                    continue;

                drawTop=minimumY>topClip?minimumY:topClip;
                drawBottom=maximumY<bottomClip?maximumY:bottomClip;
                if(drawTop<drawBottom) {
                    for(screenY=drawTop;screenY<drawBottom;++screenY)
                        SetPixel16(screenX,screenY,pixelColor);
                }
            }
        }
    }
    free(decodedPixels);
}

static void D3DO_RasterGenericCCB(const D3DO_CCBCommand *command)
{
    uint16_t *decodedPixels=NULL,celWidth=0,celHeight=0;
    uint16_t sourceRow;
    const int32_t ox=gD3DO_CEL.xPos;
    const int32_t oy=gD3DO_CEL.yPos;
    const int32_t hdx=gD3DO_CEL.hdx;
    const int32_t hdy=gD3DO_CEL.hdy;
    const int32_t vdx=gD3DO_CEL.vdx;
    const int32_t vdy=gD3DO_CEL.vdy;
    const int32_t hddx=gD3DO_CEL.hddx;
    const int32_t hddy=gD3DO_CEL.hddy;
    const uint32_t flags=gD3DO_CEL.ppFlags;

    if(!command || !command->source || !command->sourceSize)
        return;

    if (command->raw8) {
        if (!D3DO_DecodeRaw8Sprite(command,&decodedPixels,&celWidth,&celHeight))
            return;
    } else {
        if(!D3DO_DecodeCelResolved(
                command->source,command->sourceSize,
                gD3DO_CEL.plutValid?(const uint8_t *)gD3DO_CEL.plut:command->plut,
                gD3DO_CEL.pre0,gD3DO_CEL.pre1,command->flags,
                command->kind==D3DO_CCB_PAUSE_OVERLAY,
                &decodedPixels,&celWidth,&celHeight))
            return;
    }

    for(sourceRow=0;sourceRow<celHeight;++sourceRow){
        uint16_t sourceColumn;
        int64_t rowHdx=(int64_t)hdx+(int64_t)hddx*sourceRow;
        int64_t rowHdy=(int64_t)hdy+(int64_t)hddy*sourceRow;

        for(sourceColumn=0;sourceColumn<celWidth;++sourceColumn){
            uint16_t pixelColor=decodedPixels[(size_t)sourceRow*celWidth+sourceColumn];
            int xA,yA,xB,yB,xC,yC,xD,yD;
            int64_t aX=ox+((rowHdx*(int64_t)sourceColumn)>>4)+(int64_t)vdx*sourceRow;
            int64_t aY=oy+((rowHdy*(int64_t)sourceColumn)>>4)+(int64_t)vdy*sourceRow;
            int64_t bX=ox+((rowHdx*(int64_t)(sourceColumn+1))>>4)+(int64_t)vdx*sourceRow;
            int64_t bY=oy+((rowHdy*(int64_t)(sourceColumn+1))>>4)+(int64_t)vdy*sourceRow;
            int64_t nextRowHdx=rowHdx+hddx;
            int64_t nextRowHdy=rowHdy+hddy;
            int64_t cX=ox+((nextRowHdx*(int64_t)sourceColumn)>>4)+(int64_t)vdx*(sourceRow+1);
            int64_t cY=oy+((nextRowHdy*(int64_t)sourceColumn)>>4)+(int64_t)vdy*(sourceRow+1);
            int64_t dX=ox+((nextRowHdx*(int64_t)(sourceColumn+1))>>4)+(int64_t)vdx*(sourceRow+1);
            int64_t dY=oy+((nextRowHdy*(int64_t)(sourceColumn+1))>>4)+(int64_t)vdy*(sourceRow+1);

            
            if(command->kind==D3DO_CCB_PAUSE_OVERLAY) {
                if(command->masked && (pixelColor & 0x8000u) == 0u)
                    continue;
            } else if(command->masked && (pixelColor & 0x7FFFu) == 0u) {
                continue;
            }

            xA=(int)(aX>>16); yA=(int)(aY>>16);
            xB=(int)(bX>>16); yB=(int)(bY>>16);
            xC=(int)(cX>>16); yC=(int)(cY>>16);
            xD=(int)(dX>>16); yD=(int)(dY>>16);
            if(command->clipEnabled) {
                int pixelX=(xA+xB+xC+xD)/4;
                int pixelY=(yA+yB+yC+yD)/4;
                if(pixelX<command->clipX1 || pixelX>command->clipX2 ||
                   pixelY<command->clipY1 || pixelY>command->clipY2)
                    continue;
            }

            pixelColor=D3DO_ApplyPIXC(pixelColor,gD3DO_CEL.pixc);

            
            if((flags&CCB_MARIA)==0)
                D3DO_FillQuad(xA,yA,xB,yB,xD,yD,xC,yC,pixelColor);
            else
                SetPixel16(xA,yA,pixelColor);
        }
    }

    free(decodedPixels);
}

static void D3DO_RasterCopy16CCB(const D3DO_CCBCommand *command)
{
    int pixelCount=(command->pre1&0x0FFFu)+1;
    int startX=command->xPos>>16;
    int screenY=command->yPos>>16;
    const uint16_t *sourcePixels=(const uint16_t *)command->source;
    int pixelIndex;
    if(!sourcePixels) return;
    for(pixelIndex=0;pixelIndex<pixelCount;++pixelIndex){
        int screenX=startX+pixelIndex;
        if(screenX>=0 && screenX<DOOM3DO_WIDTH && screenY>=0 && screenY<DOOM3DO_HEIGHT)
            SetPixel16(screenX,screenY,sourcePixels[pixelIndex]);
    }
}

static void D3DO_RasterColorCCB(const D3DO_CCBCommand *command)
{
    uint16_t *frameBuffer=D3DO_GetRasterFramebuffer();
    int startX=command->xPos>>16,startY=command->yPos>>16;
    int width=command->hdx>>20,height=command->vdy>>16;
    int screenX,screenY;

    if(!frameBuffer || width<=0 || height<=0)
        return;

    if(command->colorMode==1) {
        for(screenY=startY;screenY<startY+height;++screenY) {
            if(screenY<0 || screenY>=DOOM3DO_HEIGHT) continue;
            for(screenX=startX;screenX<startX+width;++screenX) {
                uint16_t pixelValue;
                int r,g,b;
                if(screenX<0 || screenX>=DOOM3DO_WIDTH) continue;
                pixelValue=frameBuffer[screenY*DOOM3DO_WIDTH+screenX];
                r=(pixelValue>>10)&31;
                g=(pixelValue>>5)&31;
                b=pixelValue&31;
                frameBuffer[screenY*DOOM3DO_WIDTH+screenX]=(uint16_t)(0x8000u |
                    ((31-r)<<10)|((31-g)<<5)|(31-b));
            }
        }
        return;
    }

    if(command->colorMode==2) {
        int red=(command->color>>10)&31;
        int green=(command->color>>5)&31;
        int blue=command->color&31;
        for(screenY=startY;screenY<startY+height;++screenY) {
            if(screenY<0 || screenY>=DOOM3DO_HEIGHT) continue;
            for(screenX=startX;screenX<startX+width;++screenX) {
                uint16_t pixelValue;
                int r,g,b;
                if(screenX<0 || screenX>=DOOM3DO_WIDTH) continue;
                pixelValue=frameBuffer[screenY*DOOM3DO_WIDTH+screenX];
                r=(pixelValue>>10)&31;
                g=(pixelValue>>5)&31;
                b=pixelValue&31;
                r=(r*(31+2*red)+15)/31;
                g=(g*(31+2*green)+15)/31;
                b=(b*(31+2*blue)+15)/31;
                if(r>31)r=31;
                if(g>31)g=31;
                if(b>31)b=31;
                frameBuffer[screenY*DOOM3DO_WIDTH+screenX]=(uint16_t)(0x8000u |
                    (r<<10)|(g<<5)|b);
            }
        }
        return;
    }

    for(screenY=startY;screenY<startY+height;++screenY) {
        for(screenX=startX;screenX<startX+width;++screenX)
            SetPixel16(screenX,screenY,command->color);
    }
}

static void D3DO_RasterLineCCB(const D3DO_CCBCommand *command)
{
    int startX=command->xPos>>16,startY=command->yPos>>16,endX=command->hdx,endY=command->hdy;
    int deltaX=abs(endX-startX),stepX=startX<endX?1:-1,deltaY=-abs(endY-startY),stepY=startY<endY?1:-1,error=deltaX+deltaY;
    for(;;){
        SetPixel16(startX,startY,command->color);
        if(startX==endX&&startY==endY)break;
        if(2*error>=deltaY){error+=deltaY;startX+=stepX;}
        if(2*error<=deltaX){error+=deltaX;startY+=stepY;}
    }
}
static void D3DO_RasterRectCCB(const D3DO_CCBCommand *command)
{
    int startX=command->xPos>>16,startY=command->yPos>>16,width=command->hdx>>20,height=command->vdy>>16,screenY;
    if(width<=0||height<=0)return;
    for(screenY=startY;screenY<startY+height;++screenY){
        int screenX;
        for(screenX=startX;screenX<startX+width;++screenX)SetPixel16(screenX,screenY,command->color);
    }
}

static void D3DO_RasterCCB(const D3DO_CCBCommand *command)
{
    
    if(command->kind==D3DO_CCB_WALL){
        D3DO_RasterWallCCB(command);
    } else if(command->kind==D3DO_CCB_SKY){
        D3DO_RasterSkyCCB(command);
    } else if(command->kind==D3DO_CCB_PLANE){
        D3DO_RasterPlaneCCB(command);
    } else if(command->kind==D3DO_CCB_COLOR){
        D3DO_RasterColorCCB(command);
    } else if(command->kind==D3DO_CCB_LINE){
        D3DO_RasterLineCCB(command);
    } else if(command->kind==D3DO_CCB_RECT){
        D3DO_RasterRectCCB(command);
    } else if(command->kind==D3DO_CCB_COPY16){
        D3DO_RasterCopy16CCB(command);
    } else if(command->kind==D3DO_CCB_SPRITE_CLIPPED){
        D3DO_RasterClippedSpriteCCB(command);
    } else {
        D3DO_RasterGenericCCB(command);
    }
}

static int D3DO_CLIOWorker(void *threadData)
{
    (void)threadData;

    for (;;) {
        D3DO_RenderJob renderJob;
        D3DO_RenderBatch *renderBatch;
        D3DO_CELState celState;
        uint32_t commandIndex;
        int jobAvailable;

        SDL_LockMutex(gD3DO_Render.mutex);
        while (!gD3DO_Render.shutdown &&
               gD3DO_Render.clioTail==gD3DO_Render.clioHead)
            SDL_WaitCondition(gD3DO_Render.clioCondition,gD3DO_Render.mutex);

        if (gD3DO_Render.shutdown) {
            SDL_UnlockMutex(gD3DO_Render.mutex);
            return 0;
        }

        jobAvailable=D3DO_QueuePop(gD3DO_Render.clioQueue,
                              &gD3DO_Render.clioTail,
                              gD3DO_Render.clioHead,&renderJob);
        SDL_SignalCondition(gD3DO_Render.queueCondition);
        SDL_UnlockMutex(gD3DO_Render.mutex);

        if (!jobAvailable)
            continue;

        renderBatch=&renderJob.page->batches[renderJob.batchIndex];
        celState=renderBatch->startState;

        for (commandIndex=0;commandIndex<renderBatch->count;++commandIndex) {
            D3DO_MADAMPacket *packet=&renderBatch->packets[commandIndex];
            packet->ccb=renderBatch->commands[commandIndex];
            D3DO_ApplyCCBState(&celState,&packet->ccb);
            packet->cel=celState;
        }

        SDL_LockMutex(gD3DO_Render.mutex);
        while (D3DO_QueueIsFull(gD3DO_Render.madamHead,
                                gD3DO_Render.madamTail) &&
               !gD3DO_Render.shutdown)
            SDL_WaitCondition(gD3DO_Render.queueCondition,gD3DO_Render.mutex);
        if (!gD3DO_Render.shutdown) {
            (void)D3DO_QueuePush(gD3DO_Render.madamQueue,
                                 &gD3DO_Render.madamHead,
                                 gD3DO_Render.madamTail,&renderJob);
            SDL_SignalCondition(gD3DO_Render.madamCondition);
        }
        SDL_UnlockMutex(gD3DO_Render.mutex);
    }
}

static int D3DO_MADAMWorker(void *threadData)
{
    (void)threadData;

    for (;;) {
        D3DO_RenderJob renderJob;
        D3DO_RenderBatch *renderBatch;
        uint32_t commandIndex;
        int jobAvailable;

        SDL_LockMutex(gD3DO_Render.mutex);
        while (!gD3DO_Render.shutdown &&
               gD3DO_Render.madamTail==gD3DO_Render.madamHead)
            SDL_WaitCondition(gD3DO_Render.madamCondition,gD3DO_Render.mutex);

        if (gD3DO_Render.shutdown) {
            SDL_UnlockMutex(gD3DO_Render.mutex);
            return 0;
        }

        jobAvailable=D3DO_QueuePop(gD3DO_Render.madamQueue,
                              &gD3DO_Render.madamTail,
                              gD3DO_Render.madamHead,&renderJob);
        SDL_SignalCondition(gD3DO_Render.queueCondition);
        SDL_UnlockMutex(gD3DO_Render.mutex);

        if (!jobAvailable)
            continue;

        renderBatch=&renderJob.page->batches[renderJob.batchIndex];
        gD3DO_RasterFramebuffer=renderJob.page->framebuffer;
        
        gD3DO_WorkViewport=renderBatch->viewport;

        for (commandIndex=0;commandIndex<renderBatch->count;++commandIndex) {
            gD3DO_CEL=renderBatch->packets[commandIndex].cel;
            D3DO_RasterCCB(&renderBatch->packets[commandIndex].ccb);
        }

        gD3DO_RasterFramebuffer=NULL;
        memset(&gD3DO_WorkViewport,0,sizeof(gD3DO_WorkViewport));

        SDL_LockMutex(gD3DO_Render.mutex);
        ++renderJob.page->completedBatches;
        if (renderBatch->finalBatch) {
            
            if(renderJob.page->frameSerial<=gD3DO_Render.displayedFrameSerial)
                renderJob.page->state=D3DO_PAGE_FREE;
            else
                renderJob.page->state=D3DO_PAGE_COMPLETE;
        }
        SDL_BroadcastCondition(gD3DO_Render.pageCondition);
        SDL_UnlockMutex(gD3DO_Render.mutex);
    }
}

static int D3DO_StartRenderWorkers(void)
{
    uint32_t pageIndex;

    memset(&gD3DO_Render,0,sizeof(gD3DO_Render));
    gD3DO_Render.currentPage=-1;

    gD3DO_Render.mutex=SDL_CreateMutex();
    gD3DO_Render.clioCondition=SDL_CreateCondition();
    gD3DO_Render.madamCondition=SDL_CreateCondition();
    gD3DO_Render.pageCondition=SDL_CreateCondition();
    gD3DO_Render.queueCondition=SDL_CreateCondition();
    if(!gD3DO_Render.mutex || !gD3DO_Render.clioCondition ||
       !gD3DO_Render.madamCondition || !gD3DO_Render.pageCondition ||
       !gD3DO_Render.queueCondition)
        return 0;

    for (pageIndex=0;pageIndex<D3DO_RENDER_PAGE_COUNT;++pageIndex) {
        D3DO_RenderPage *page=&gD3DO_Render.pages[pageIndex];
        page->framebuffer=(uint16_t *)calloc(
            DOOM3DO_WIDTH*DOOM3DO_HEIGHT,sizeof(uint16_t));
        page->spanArray=(Byte *)calloc(
            MAXSCREENWIDTH*MAXSCREENHEIGHT,sizeof(Byte));
        page->spriteClipCapacity=D3DO_CCB_TOTAL*MAXSCREENWIDTH;
        page->spriteClipArena=(Word *)calloc(
            page->spriteClipCapacity,sizeof(Word));
        page->batches=(D3DO_RenderBatch *)calloc(
            D3DO_RENDER_BATCHES_PER_PAGE,sizeof(D3DO_RenderBatch));
        if(!page->framebuffer || !page->spanArray || !page->batches)
            return 0;
        page->state=D3DO_PAGE_FREE;
    }

    gD3DO_Render.clioThread=
        SDL_CreateThread(D3DO_CLIOWorker,"DOOM3DO-CLIO",NULL);
    gD3DO_Render.madamThread=
        SDL_CreateThread(D3DO_MADAMWorker,"DOOM3DO-MADAM",NULL);
    if(!gD3DO_Render.clioThread || !gD3DO_Render.madamThread)
        return 0;

    gD3DO_Render.workersReady=1;
    return 1;
}

static void D3DO_StopRenderWorkers(void)
{
    uint32_t pageIndex;

    if(!gD3DO_Render.mutex)
        return;

    SDL_LockMutex(gD3DO_Render.mutex);
    gD3DO_Render.shutdown=1;
    SDL_BroadcastCondition(gD3DO_Render.clioCondition);
    SDL_BroadcastCondition(gD3DO_Render.madamCondition);
    SDL_BroadcastCondition(gD3DO_Render.pageCondition);
    SDL_BroadcastCondition(gD3DO_Render.queueCondition);
    SDL_UnlockMutex(gD3DO_Render.mutex);

    if(gD3DO_Render.clioThread)
        SDL_WaitThread(gD3DO_Render.clioThread,NULL);
    if(gD3DO_Render.madamThread)
        SDL_WaitThread(gD3DO_Render.madamThread,NULL);

    for(pageIndex=0;pageIndex<D3DO_RENDER_PAGE_COUNT;++pageIndex) {
        free(gD3DO_Render.pages[pageIndex].framebuffer);
        free(gD3DO_Render.pages[pageIndex].spanArray);
        free(gD3DO_Render.pages[pageIndex].spriteClipArena);
        free(gD3DO_Render.pages[pageIndex].batches);
        gD3DO_Render.pages[pageIndex].framebuffer=NULL;
        gD3DO_Render.pages[pageIndex].spanArray=NULL;
        gD3DO_Render.pages[pageIndex].batches=NULL;
    }

    SDL_DestroyCondition(gD3DO_Render.clioCondition);
    SDL_DestroyCondition(gD3DO_Render.madamCondition);
    SDL_DestroyCondition(gD3DO_Render.pageCondition);
    SDL_DestroyCondition(gD3DO_Render.queueCondition);
    SDL_DestroyMutex(gD3DO_Render.mutex);
    memset(&gD3DO_Render,0,sizeof(gD3DO_Render));
    gD3DO_Render.currentPage=-1;
}

static /*
 * Acquire a free render page for construction by the game thread.
 */
D3DO_RenderPage *D3DO_AcquireBuildPage(void)
{
    uint32_t pageIndex;
    D3DO_RenderPage *buildPage=NULL;

    SDL_LockMutex(gD3DO_Render.mutex);
    for (;;) {
        for(pageIndex=0;pageIndex<D3DO_RENDER_PAGE_COUNT;++pageIndex) {
            if(gD3DO_Render.pages[pageIndex].state==D3DO_PAGE_FREE) {
                buildPage=&gD3DO_Render.pages[pageIndex];
                buildPage->state=D3DO_PAGE_BUILDING;
                break;
            }
        }
        if(buildPage || gD3DO_Render.shutdown)
            break;
        SDL_WaitCondition(gD3DO_Render.pageCondition,gD3DO_Render.mutex);
    }
    SDL_UnlockMutex(gD3DO_Render.mutex);

    if(!buildPage)
        return NULL;

    buildPage->batchCount=0;
    buildPage->currentBatch=0;
    buildPage->spanOffset=0;
    buildPage->spriteClipUsed=0;
    buildPage->completedBatches=0;
    buildPage->finalSubmitted=0;
    buildPage->frameSerial=++gD3DO_Render.nextFrameSerial;
    
    buildPage->viewport=D3DO_CaptureViewport();
    memset(buildPage->framebuffer,0,
           DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));
    memset(buildPage->spanArray,0,
           MAXSCREENWIDTH*MAXSCREENHEIGHT*sizeof(Byte));
    buildPage->buildState=gD3DO_BuildCEL;
    gD3DO_Render.currentPage=(int)(buildPage-gD3DO_Render.pages);
    gFramebuffer=buildPage->framebuffer;
    SpanPtr=buildPage->spanArray;
    D3DO_BeginBatch(buildPage);
    return buildPage;
}

static void D3DO_SubmitFrame(void)
{
    D3DO_RenderPage *page=D3DO_CurrentPage();
    if(!page || page->state!=D3DO_PAGE_BUILDING)
        return;
    D3DO_SubmitCurrentBatch(TRUE);
}

static void D3DO_RetireObsoletePagesLocked(uint64_t displayedSerial,
                                               const D3DO_RenderPage *keep)
{
    uint32_t pageIndex;
    for(pageIndex=0;pageIndex<D3DO_RENDER_PAGE_COUNT;++pageIndex) {
        D3DO_RenderPage *renderPage=&gD3DO_Render.pages[pageIndex];
        if(renderPage==keep)
            continue;
        if(renderPage->state==D3DO_PAGE_COMPLETE &&
           renderPage->frameSerial<=displayedSerial) {
            
            renderPage->state=D3DO_PAGE_FREE;
        }
    }
}

static void D3DO_MarkPresentedPage(D3DO_RenderPage *page)
{
    if(!page || !gD3DO_Render.mutex)
        return;

    SDL_LockMutex(gD3DO_Render.mutex);
    if(page->frameSerial>gD3DO_Render.displayedFrameSerial)
        gD3DO_Render.displayedFrameSerial=page->frameSerial;
    page->state=D3DO_PAGE_FREE;
    D3DO_RetireObsoletePagesLocked(gD3DO_Render.displayedFrameSerial,NULL);
    SDL_BroadcastCondition(gD3DO_Render.pageCondition);
    SDL_UnlockMutex(gD3DO_Render.mutex);
}

static void D3DO_WaitForPage(D3DO_RenderPage *page)
{
    if(!page || !gD3DO_Render.mutex)
        return;
    SDL_LockMutex(gD3DO_Render.mutex);
    while(page->state!=D3DO_PAGE_COMPLETE && !gD3DO_Render.shutdown)
        SDL_WaitCondition(gD3DO_Render.pageCondition,gD3DO_Render.mutex);
    SDL_UnlockMutex(gD3DO_Render.mutex);
}

static void FlushCCBs(void)
{
    D3DO_RenderPage *page=D3DO_CurrentPage();
    if(!page) {
        SpanPtr=SpanArray;
        return;
    }

    if(D3DO_CCBCount)
        D3DO_SubmitCurrentBatch(FALSE);

    SpanPtr=page->spanArray+page->spanOffset;
}

void DrawMShape(Word screenX, Word screenY, void *ShapePtr)
{
    D3DO_CCBCommand command;
    uint32_t resourceSize;
    if(!ShapePtr) return;
    resourceSize=GetAHandleSize(ShapePtr); if(resourceSize<60u) return;
    if(!D3DO_ResolveCCB(&command,(const uint8_t *)ShapePtr,resourceSize)) return;
    command.flags&=~CCB_BGND; command.masked=1;
    command.xPos=((int32_t)screenX)<<16; command.yPos=((int32_t)screenY)<<16;
    command.clipEnabled=0;
    D3DO_QueueCCB(&command);
}
void DrawShape(Word screenX, Word screenY, void *ShapePtr)
{
    D3DO_CCBCommand command;
    uint32_t resourceSize;
    if(!ShapePtr) return;
    resourceSize=GetAHandleSize(ShapePtr); if(resourceSize<60u) return;
    if(!D3DO_ResolveCCB(&command,(const uint8_t *)ShapePtr,resourceSize)) return;
    command.flags|=CCB_BGND; command.masked=0;
    command.xPos=((int32_t)screenX)<<16; command.yPos=((int32_t)screenY)<<16;
    command.clipEnabled=0;
    D3DO_QueueCCB(&command);
}

void DrawWeaponShape(int screenX, int screenY, void *ShapePtr, LongWord Size,
                     Fixed ScaleX, Fixed ScaleY, Boolean Shadow)
{
    D3DO_CCBCommand command;
    if(!ShapePtr || Size<60u) return;
    if(!D3DO_ResolveCCB(&command,(const uint8_t *)ShapePtr,Size)) return;
    
    command.hdx=(int32_t)ScaleX;
    command.vdy=(int32_t)ScaleY;
    command.hddx=0;
    command.hddy=0;
    command.pixc=Shadow?0x9C81u:0x1F00u;
    command.flags&=~CCB_BGND; command.masked=1;
    command.xPos=((int32_t)screenX)<<16; command.yPos=((int32_t)screenY)<<16;
    command.clipEnabled=0;
    D3DO_QueueCCB(&command);
}

/*
 * Submit an unclipped world sprite using the 3DO sprite CCB projection.
 */
void DrawSpriteNoClip(vissprite_t *vis)
{
    uint8_t *resourceData;
    uint8_t *patchData;
    uint32_t resourceSize;
    D3DO_CCBCommand command;
    int screenX;

    if(!vis) return;

    resourceData=(uint8_t *)LoadAResource(vis->PatchLump);
    if(!resourceData) return;

    resourceSize=GetAResourceSize(vis->PatchLump);
    if(vis->PatchOffset>=resourceSize || resourceSize-vis->PatchOffset<64u){
        ReleaseAResource(vis->PatchLump);
        return;
    }

    
    patchData=resourceData+vis->PatchOffset;

    if(resourceSize-(uint32_t)(patchData+4u-resourceData)<60u ||
       !D3DO_ResolveCCB(&command,patchData+4u,
                        resourceSize-(uint32_t)(patchData+4u-resourceData))){
        ReleaseAResource(vis->PatchLump);
        return;
    }

    if(vis->colormap&0x8000u)
        command.pixc=0x9C81u;
    else
        command.pixc=D3DO_LightTable[
            ((uint32_t)vis->colormap&0xFFu)>>LIGHTSCALESHIFT];

    
    command.hdx=0;
    command.hdy=(int32_t)((uint32_t)vis->yscale<<4);
    command.vdy=0;
    if(vis->colormap&0x4000u){
        screenX=vis->x2;
        command.vdx=-(int32_t)vis->xscale;
    } else {
        screenX=vis->x1;
        command.vdx=(int32_t)vis->xscale;
    }

    command.xPos=((int32_t)(screenX+ScreenXOffset))<<16;
    command.yPos=((int32_t)(vis->y1+ScreenYOffset))<<16;
    command.flags&=~CCB_BGND;
    command.masked=1;
    command.kind=D3DO_CCB_GENERIC;

    D3DO_QueueCCB(&command);
    ReleaseAResource(vis->PatchLump);
}
/*
 * Submit a sprite with the wall-opening limits produced by the Doom renderer.
 */
void DrawSpriteClip(Word x1,Word x2,vissprite_t *vis)
{
    uint8_t *resourceData,*patchData;
    uint32_t resourceSize;
    D3DO_CCBCommand command;
    D3DO_RenderPage *renderPage;
    uint32_t columnCount;
    int columnOffset;

    if(!vis) return;
    resourceData=(uint8_t *)LoadAResource(vis->PatchLump);
    if(!resourceData) return;
    resourceSize=GetAResourceSize(vis->PatchLump);
    if(vis->PatchOffset>=resourceSize || resourceSize-vis->PatchOffset<64u){
        ReleaseAResource(vis->PatchLump); return;
    }
    patchData=resourceData+vis->PatchOffset;

    if(resourceSize-(uint32_t)(patchData+4u-resourceData)<60u ||
       !D3DO_ResolveCCB(&command,patchData+4u,
                        resourceSize-(uint32_t)(patchData+4u-resourceData))){
        ReleaseAResource(vis->PatchLump);
        return;
    }

    if(vis->colormap&0x8000u)
        command.pixc=0x9C81u;
    else
        command.pixc=D3DO_LightTable[
            ((uint32_t)vis->colormap&0xFFu)>>LIGHTSCALESHIFT];

    
    command.hdx=0;
    command.hdy=(int32_t)((uint32_t)vis->yscale<<4);
    command.vdy=0;
    if(vis->colormap&0x4000u){
        command.xPos=((int32_t)(vis->x2+ScreenXOffset))<<16;
        command.vdx=-(int32_t)vis->xscale;
        command.yPos=((int32_t)(vis->y1+ScreenYOffset))<<16;
    } else {
        command.xPos=((int32_t)(vis->x1+ScreenXOffset))<<16;
        command.vdx=(int32_t)vis->xscale;
        command.yPos=((int32_t)(vis->y1+ScreenYOffset))<<16;
    }
    command.flags&=~CCB_BGND;
    command.masked=1;
    
    command.kind=D3DO_CCB_SPRITE_CLIPPED;
    command.clipEnabled=0;

    renderPage=D3DO_CurrentPage();
    columnCount=(uint32_t)((x2>=x1)?(x2-x1+1):0);
    if(!renderPage || !renderPage->spriteClipArena ||
       renderPage->spriteClipUsed+columnCount>renderPage->spriteClipCapacity){
        ReleaseAResource(vis->PatchLump);
        return;
    }
    command.columnClip=&renderPage->spriteClipArena[renderPage->spriteClipUsed];
    command.columnClipFirstX=x1+ScreenXOffset;
    for(columnOffset=0;columnOffset<(int)columnCount;++columnOffset)
        renderPage->spriteClipArena[renderPage->spriteClipUsed+(uint32_t)columnOffset]=spropening[x1+columnOffset];
    renderPage->spriteClipUsed+=columnCount;

    D3DO_QueueCCB(&command);
    ReleaseAResource(vis->PatchLump);
}
void DrawSpriteCenter(Word SpriteNum)
{
    void *resourceData;
    uint32_t resourceSize,spriteOffset;
    uint8_t *patchData,*ccbData;
    int32_t screenX,screenY;
    D3DO_CCBCommand command;

    resourceData=LoadAResource((Word)(SpriteNum>>FF_SPRITESHIFT));
    if(!resourceData) return;
    resourceSize=GetAResourceSize((Word)(SpriteNum>>FF_SPRITESHIFT));
    spriteOffset=ReadBE32((const uint8_t *)resourceData+(uint32_t)(SpriteNum&FF_FRAMEMASK)*4u);
    if(spriteOffset&PT_NOROTATE){
        patchData=(uint8_t *)resourceData+(spriteOffset&0x3FFFFFFFu);
        spriteOffset=ReadBE32(patchData);
    }
    patchData=(uint8_t *)resourceData+(spriteOffset&0x3FFFFFFFu);
    screenX=80-(int32_t)(int16_t)ReadBE16(patchData+0u);
    screenY=90-(int32_t)(int16_t)ReadBE16(patchData+2u);
    ccbData=patchData+4u;
    memset(&command,0,sizeof(command));
    if(!D3DO_ResolveCCB(&command,ccbData,resourceSize-(uint32_t)(ccbData-(uint8_t *)resourceData))){
        ReleaseAResource((Word)(SpriteNum>>FF_SPRITESHIFT)); return;
    }
    command.hdx=0; command.hdy=(spriteOffset&PT_FLIP)?-(2<<20):(2<<20);
    if(spriteOffset&PT_FLIP) screenX+=(int32_t)GetShapeHeight(ccbData);
    command.vdx=2<<16; command.vdy=0;
    command.xPos=((int32_t)(screenX*2))<<16; command.yPos=((int32_t)(screenY*2))<<16;
    command.flags&=~CCB_BGND; command.masked=1; command.kind=D3DO_CCB_GENERIC;
    D3DO_QueueCCB(&command);
    ReleaseAResource((Word)(SpriteNum>>FF_SPRITESHIFT));
}

void EnableHardwareClipping(void)
{
    FlushCCBs();
}
void DisableHardwareClipping(void)
{
    FlushCCBs();
}

void DrawLine(Word x1,Word y1,Word x2,Word y2,Word color)
{
    if (gAutomapDirect) {
        
        const int ax1 = (int)(int16_t)x1 + 160;
        const int ay1 = 80 - (int)(int16_t)y1;
        const int ax2 = (int)(int16_t)x2 + 160;
        const int ay2 = 80 - (int)(int16_t)y2;
        DrawLine16(ax1, ay1, ax2, ay2, D3DO_RGBA5551ToARGB1555(D3DO_AutomapColor(color)));
        return;
    }

    D3DO_CCBCommand command; memset(&command,0,sizeof(command));
    command.kind=D3DO_CCB_LINE; command.color=(uint16_t)(0x8000u|(color&0x7FFFu));
    command.xPos=((int32_t)(x1+ScreenXOffset))<<16;
    command.yPos=((int32_t)(y1+ScreenYOffset))<<16;
    command.hdx=(int32_t)(x2+ScreenXOffset); command.hdy=(int32_t)(y2+ScreenYOffset);
    command.flags=CCB_LDSIZE|CCB_LDPRS|CCB_LDPPMP|CCB_CCBPRE|CCB_YOXY|CCB_ACW|CCB_ACCW|CCB_ACE|CCB_BGND|CCB_NOBLK;
    command.pixc=0x1F00u; command.pre0=0x40000016u; command.pre1=0x03FF1000u;
    D3DO_QueueCCB(&command);
}
void DrawARect(Word x1,Word y1,Word Width,Word Height,Word color)
{
    if (gAutomapDirect) {
        FillRect16((int)x1, (int)y1, (int)Width, (int)Height, D3DO_RGBA5551ToARGB1555(D3DO_AutomapColor(color)));
        return;
    }

    D3DO_CCBCommand command; memset(&command,0,sizeof(command));
    command.kind=D3DO_CCB_RECT; command.color=(uint16_t)(0x8000u|(color&0x7FFFu));
    command.xPos=((int32_t)(x1+ScreenXOffset))<<16;
    command.yPos=((int32_t)(y1+ScreenYOffset))<<16;
    command.hdx=((int32_t)Width)<<20; command.vdy=((int32_t)Height)<<16;
    command.flags=CCB_LDSIZE|CCB_LDPRS|CCB_LDPPMP|CCB_CCBPRE|CCB_YOXY|CCB_ACW|CCB_ACCW|CCB_ACE|CCB_BGND|CCB_NOBLK;
    command.pixc=0x1F00u; command.pre0=0x40000016u; command.pre1=0x03FF1000u;
    D3DO_QueueCCB(&command);
}

void DrawSkyLine(void)
{
    D3DO_CCBCommand command;
    uint32_t skyColumn;
    if(!SkyTexture || !SkyTexture->data || !*SkyTexture->data) return;
    memset(&command,0,sizeof(command));
    skyColumn=(((uint32_t)xtoviewangle[tx_x]+(uint32_t)viewangle)>>ANGLETOSKYSHIFT)&0xFFu;
    command.flags=CCB_SPABS|CCB_LDSIZE|CCB_LDPPMP|CCB_CCBPRE|CCB_YOXY|
        CCB_ACW|CCB_ACCW|CCB_ACE|CCB_BGND|CCB_NOBLK|CCB_PPABS|CCB_LDPLUT;
    command.source=(const uint8_t *)*SkyTexture->data+32u+skyColumn*64u;
    command.sourceSize=64u; command.plut=(const uint8_t *)*SkyTexture->data;
    
    command.xPos=((int32_t)(tx_x+ScreenXOffset))<<16;
    command.yPos=((int32_t)ScreenYOffset)<<16;
    command.hdx=0; command.hdy=SkyScales[ScreenSize]; command.vdx=1<<16; command.vdy=0;
    command.pixc=0x1F00u; command.pre0=0x03u; command.pre1=0x3E005000u|127u;
    command.kind=D3DO_CCB_WALL;
    D3DO_QueueCCB(&command);
}

void DrawWallColumn(Word y, Word Colnum, Byte *Source, Word Run)
{
    D3DO_CCBCommand command;
    Word sourcePixelRemainder,sourceByteOffset;
    if(!Source || !Run || tx_scale<=0) return;
    memset(&command,0,sizeof(command));
    sourcePixelRemainder=Colnum&7;
    sourceByteOffset=(Word)(((Colnum>>1)+32u)&~3u);
    command.flags=CCB_SPABS|CCB_LDSIZE|CCB_LDPRS|CCB_LDPPMP|CCB_CCBPRE|
        CCB_YOXY|CCB_ACW|CCB_ACCW|CCB_ACE|CCB_BGND|CCB_NOBLK|
        CCB_PPABS|CCB_LDPLUT|CCB_USEAV;
    command.source=Source+sourceByteOffset;
    command.plut=Source;
    command.xPos=((int32_t)(tx_x+ScreenXOffset))<<16;
    command.yPos=((int32_t)(y+ScreenYOffset)<<16)+0xFF00;
    command.hdx=0; command.hdy=(int32_t)((uint32_t)tx_scale<<11);
    command.vdx=1<<16; command.vdy=0; command.hddx=0; command.hddy=0;
    command.pixc=D3DO_LightTable[((uint32_t)tx_texturelight>>LIGHTSCALESHIFT)&31u];
    command.pre0=((uint32_t)sourcePixelRemainder<<24)|0x03u;
    command.pre1=0x3E005000u|((uint32_t)sourcePixelRemainder+(uint32_t)Run-1u);
    command.kind=D3DO_CCB_WALL;
    D3DO_QueueCCB(&command);
}

void DrawFloorColumn(Word ds_y, Word ds_x1, Word Count,
                     LongWord xfrac, LongWord yfrac,
                     Fixed ds_xstep, Fixed ds_ystep)
{
    D3DO_CCBCommand command;
    Byte *destinationBuffer;
    uint32_t paddedBytes;
    if(!PlaneSource || !Count) return;
    destinationBuffer=SpanPtr;
    DrawASpan(Count,xfrac,yfrac,ds_xstep,ds_ystep,destinationBuffer);
    memset(&command,0,sizeof(command));
    command.flags=CCB_SPABS|CCB_LDSIZE|CCB_LDPRS|CCB_LDPPMP|CCB_CCBPRE|
        CCB_YOXY|CCB_ACW|CCB_ACCW|CCB_ACE|CCB_BGND|CCB_NOBLK|
        CCB_PPABS|CCB_LDPLUT|CCB_USEAV;
    command.source=destinationBuffer; command.sourceSize=Count; command.plut=PlaneSource;
    command.xPos=((int32_t)(ds_x1+ScreenXOffset))<<16;
    command.yPos=((int32_t)(ds_y+ScreenYOffset))<<16;
    command.hdx=1<<20; command.hdy=0; command.vdx=0; command.vdy=1<<16;
    command.hddx=0; command.hddy=0;
    command.pixc=D3DO_LightTable[((uint32_t)tx_texturelight>>LIGHTSCALESHIFT)&31u];
    command.pre0=0x00000005u; command.pre1=0x3E005000u|((uint32_t)Count-1u);
    command.kind=D3DO_CCB_PLANE;
    D3DO_QueueCCB(&command);
    paddedBytes=((uint32_t)Count+3u)&~3u;
    SpanPtr+=paddedBytes;
    {
        D3DO_RenderPage *page=D3DO_CurrentPage();
        if(page) page->spanOffset=(uint32_t)(SpanPtr-page->spanArray);
    }
}

void DrawASpan(Word Count, LongWord xfrac, LongWord yfrac,
               Fixed ds_xstep, Fixed ds_ystep, Byte *Dest)
{
    extern Byte *PlaneSource;
    Word i;

    if (!Dest || !PlaneSource)
        return;

    for (i = 0; i < Count; ++i) {
        const uint32_t xIndex = (uint32_t)(xfrac >> 16) & 0x3Fu;
        const uint32_t yIndex = (uint32_t)(yfrac >> 10) & 0x0FC0u;
        const uint32_t texel = xIndex | yIndex;

        
        Dest[i] = PlaneSource[64u + texel];

        xfrac += ds_xstep;
        yfrac += ds_ystep;
    }
}


void DrawColors(void)
{
    Word red=(Word)players.damagecount;
    Word green=(Word)(players.bonuscount>>1);
    Word blue=0;
    uint32_t inv=(uint32_t)players.powers[pw_invulnerability];
    uint32_t rad=(uint32_t)players.powers[pw_ironfeet];
    uint32_t berserk=(uint32_t)players.powers[pw_strength];
    D3DO_CCBCommand command;

    memset(&command,0,sizeof(command));
    command.xPos=((int32_t)ScreenXOffset)<<16;
    command.yPos=((int32_t)ScreenYOffset)<<16;
    command.hdx=((int32_t)ScreenWidth)<<20;
    command.vdy=((int32_t)ScreenHeight)<<16;
    command.kind=D3DO_CCB_COLOR;
    command.colorMode=0;

    if(inv>4u*(uint32_t)TICKSPERSEC || (inv&0x10u)) {
        command.colorMode=1;
        D3DO_QueueCCB(&command);
        return;
    }

    if(rad>4u*(uint32_t)TICKSPERSEC || (rad&0x10u))
        green=15;

    if(berserk>0u && berserk<255u)
        red=(Word)(red+((255u-berserk)>>4));

    if(red>31u) red=31u;
    if(green>31u) green=31u;
    if(blue>31u) blue=31u;
    if(!red && !green && !blue)
        return;

    command.colorMode=2;
    command.color=(uint16_t)((((uint16_t)red)&31u)<<10 |
                       ((((uint16_t)green)&31u)<<5) |
                       (((uint16_t)blue)&31u));
    D3DO_QueueCCB(&command);
}


static void D3DO_DrawPlaque(Word RezNum, int background)
{
    void *resourceHandle = LoadAResource(RezNum);
    D3DO_CCBCommand command;
    uint32_t resourceSize;
    int plaqueWidth;
    int screenX,screenY;

    if (!resourceHandle) return;

    resourceSize=GetAHandleSize(resourceHandle);
    if (resourceSize>=60u && D3DO_ResolveCCB(&command,(const uint8_t *)resourceHandle,resourceSize)) {
        plaqueWidth=D3DO_CelWidth((const D3DO_CCB *)resourceHandle);
        screenX=(160-plaqueWidth/2);
        screenY=80;

        if (background) {
            command.flags|=CCB_BGND;
            command.masked=0;
        } else {
            command.flags&=~CCB_BGND;
            command.masked=1;
            command.kind=D3DO_CCB_PAUSE_OVERLAY;
        }
        command.xPos=((int32_t)screenX)<<16; command.yPos=((int32_t)screenY)<<16;
        command.clipEnabled=0;
        D3DO_QueueCCB(&command);
    }
    ReleaseAResource(RezNum);
}

void DrawPlaque(Word RezNum)
{
    D3DO_DrawPlaque(RezNum,1);
}

void D3DO_ShowPausePlaque(void)
{
    D3DO_RenderPage *page;

    page=D3DO_CurrentPage();
    if(!page || page->state!=D3DO_PAGE_BUILDING)
        page=D3DO_AcquireBuildPage();
    if(!page)
        return;

    memcpy(page->framebuffer,gDisplayedFramebuffer,
           DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));
    page->buildState=gD3DO_BuildCEL;
    gFramebuffer=page->framebuffer;
    SpanPtr=page->spanArray;
    D3DO_BeginBatch(page);

    D3DO_DrawPlaque(rPAUSED,0);
    D3DO_SubmitFrame();
    D3DO_WaitForPage(page);
    if(PresentFramebuffer(page->framebuffer))
        memcpy(gDisplayedFramebuffer,page->framebuffer,
               DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));
    D3DO_MarkPresentedPage(page);

    (void)D3DO_AcquireBuildPage();
}

static double ReadExtended80(const uint8_t p[10])
{
    uint16_t expRaw;
    uint64_t mantissa;
    int exponent;
    int sign;

    if (!p) return 0.0;

    
    expRaw = ReadBE16(p);
    sign = (expRaw & 0x8000u) != 0;
    exponent = (int)(expRaw & 0x7FFFu) - 16383;

    mantissa = ((uint64_t)p[2] << 56) |
               ((uint64_t)p[3] << 48) |
               ((uint64_t)p[4] << 40) |
               ((uint64_t)p[5] << 32) |
               ((uint64_t)p[6] << 24) |
               ((uint64_t)p[7] << 16) |
               ((uint64_t)p[8] << 8)  |
               (uint64_t)p[9];

    if (mantissa == 0) return 0.0;

    {
        double value = (double)mantissa / 9223372036854775808.0; 
        value = ldexp(value, exponent);
        return sign ? -value : value;
    }
}

static void FreeAudioData(D3DO_AudioData *a)
{
    if (!a) return;
    free(a->ownedBuffer);
    memset(a, 0, sizeof(*a));
}

static int ReadAudioFile(const char *path, uint8_t **outData, uint32_t *outSize)
{
    FILE *fp;
    long length;
    uint8_t *data;

    if (!path || !outData || !outSize) return 0;
    *outData = NULL; *outSize = 0;

    
    if (strcasecmp(path, "Sounds") == 0 || strncasecmp(path, "Sounds/", 7) == 0 ||
        strncasecmp(path, "Sounds\\", 8) == 0 ||
        strcasecmp(path, "Music") == 0 || strncasecmp(path, "Music/", 6) == 0 ||
        strncasecmp(path, "Music\\", 7) == 0) {
        fp = fopen(path, "rb");
        if (!fp) return 0;
        if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
        length = ftell(fp);
        if (length < 0 || (unsigned long)length > UINT32_MAX) { fclose(fp); return 0; }
        if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 0; }
        data = (uint8_t *)malloc(length ? (size_t)length : 1u);
        if (!data) { fclose(fp); return 0; }
        if (length && fread(data, 1, (size_t)length, fp) != (size_t)length) {
            free(data); fclose(fp); return 0;
        }
        fclose(fp);
        *outData = data;
        *outSize = (uint32_t)length;
        return 1;
    }

    return 0;
}

static int DecodeAIFF(const uint8_t *data, uint32_t size, D3DO_AudioData *out)
{
    uint32_t pos = 12, channels = 0, frames = 0, bits = 0, rate = 0, compression = 0;
    const uint8_t *sound = NULL;
    uint32_t soundSize = 0;
    int isAIFC = 0;
    if (!data || !out || size < 12 || memcmp(data, "FORM", 4) != 0) return 0;
    if (memcmp(data + 8, "AIFF", 4) == 0) isAIFC = 0;
    else if (memcmp(data + 8, "AIFC", 4) == 0) isAIFC = 1;
    else return 0;
    while (pos + 8 <= size) {
        uint32_t chunk = ReadBE32(data + pos + 4);
        uint32_t next = pos + 8u + chunk + (chunk & 1u);
        if (next > size || next < pos) return 0;
        if (memcmp(data + pos, "COMM", 4) == 0 && chunk >= (isAIFC ? 22u : 18u)) {
            channels = ReadBE16(data + pos + 8);
            frames = ReadBE32(data + pos + 10);
            bits = ReadBE16(data + pos + 14);
            rate = (uint32_t)(ReadExtended80(data + pos + 16) + 0.5);
            if (isAIFC) compression = ReadBE32(data + pos + 26);
        } else if (memcmp(data + pos, "SSND", 4) == 0 && chunk >= 8u) {
            uint32_t offset = ReadBE32(data + pos + 8);
            if (offset <= chunk - 8u) {
                sound = data + pos + 16u + offset;
                soundSize = chunk - 8u - offset;
            }
        }
        pos = next;
    }
    if (!sound || !channels || !frames || !rate || channels > 2 || (bits != 8 && bits != 16)) return 0;
    if (isAIFC && compression != 0x53445832u && compression != 0x4E4F4E45u) return 0;
    if (isAIFC && compression == 0x53445832u) {
        uint32_t total = frames * channels;
        int16_t *pcm = (int16_t *)malloc((size_t)total * sizeof(int16_t));
        int32_t prevL = 0, prevR = 0;
        if (!pcm) return 0;
        for (uint32_t i = 0; i < frames; ++i) {
            if (channels == 2) {
                if ((uint64_t)i * 2u + 1u >= soundSize) { free(pcm); return 0; }
                int8_t l8 = (int8_t)sound[i * 2u], r8 = (int8_t)sound[i * 2u + 1u];
                int16_t l16 = (int16_t)(((int32_t)l8 * abs((int)l8)) * 2 + prevL * (l8 & 1));
                int16_t r16 = (int16_t)(((int32_t)r8 * abs((int)r8)) * 2 + prevR * (r8 & 1));
                pcm[i * 2u] = l16;
                pcm[i * 2u + 1u] = r16;
                prevL = l16; prevR = r16;
            } else {
                if (i >= soundSize) { free(pcm); return 0; }
                int8_t s8 = (int8_t)sound[i];
                int16_t s16 = (int16_t)(((int32_t)s8 * abs((int)s8)) * 2 + prevL * (s8 & 1));
                pcm[i] = s16;
                prevL = s16;
            }
        }
        out->ownedBuffer = (uint8_t *)pcm; out->buffer = (const uint8_t *)pcm; out->bufferSize = (size_t)total * sizeof(int16_t);
        out->numSamples = frames; out->sampleRate = rate; out->numChannels = channels; out->bitDepth = 16;
        return 1;
    }
    {
        uint32_t bytes = bits / 8u;
        uint64_t needed = (uint64_t)frames * channels * bytes;
        uint8_t *pcm;
        if (needed > soundSize) return 0;
        pcm = (uint8_t *)malloc((size_t)needed);
        if (!pcm) return 0;
        if (bits == 8) memcpy(pcm, sound, (size_t)needed);
        else {
            int16_t *dst = (int16_t *)pcm;
            for (uint64_t i = 0; i < needed / 2u; ++i) dst[i] = ReadBES16(sound + i * 2u);
        }
        out->ownedBuffer = pcm; out->buffer = pcm; out->bufferSize = (size_t)needed;
        out->numSamples = frames; out->sampleRate = rate; out->numChannels = channels; out->bitDepth = bits;
        return 1;
    }
}

static float AudioSample(const D3DO_AudioData *a, uint32_t frame, uint32_t channel)
{
    if (a->bitDepth == 8) return (float)((const int8_t *)a->buffer)[frame * a->numChannels + channel] / 127.0f;
    return (float)((const int16_t *)a->buffer)[frame * a->numChannels + channel] / 32767.0f;
}

static void StopMidiMusicLocked(void)
{
    gMidiPlaying = 0;
    if (gMidiSong) {
        WildMidi_Close(gMidiSong);
        gMidiSong = NULL;
    }
}

static int LoadMidiMusic(const uint8_t *data, uint32_t size)
{
    midi *song;
    if (!data || size == 0) return 0;
    song = WildMidi_OpenBuffer(data, size);
    if (!song) return 0;
    if (WildMidi_SetOption(song, WM_MO_LOOP, 1) != 0) {
        WildMidi_Close(song);
        return 0;
    }
    SDL_LockMutex(gAudioMutex);
    StopMidiMusicLocked();
    gMidiSong = song;
    gMidiPlaying = 1;
    gMusicPaused = 0;
    SDL_UnlockMutex(gAudioMutex);
    return 1;
}

static void MixMidiMusic(float *out, int frames)
{
    int8_t *musicOut;
    int bytes;
    int result;
    int i;

    if (!gMidiPlaying || !gMidiSong || frames <= 0) return;

    bytes = frames * 4;
    musicOut = (int8_t *)malloc((size_t)bytes);
    if (!musicOut) return;

    result = WildMidi_GetOutput(gMidiSong, musicOut, (uint32_t)bytes);
    if (result <= 0) {
        free(musicOut);
        if (result < 0) gMidiPlaying = 0;
        return;
    }

    {
        const int16_t *samples = (const int16_t *)musicOut;
        float gain = ((float)gMusicVolume / 15.0f) * 0.7f;
        int sampleCount = result / (int)sizeof(int16_t);
        for (i = 0; i < sampleCount; ++i)
            out[i] += ((float)samples[i] / 32768.0f) * gain;
    }
    free(musicOut);
}

static void MixVoice(D3DO_AudioVoice *v, float *out, int frames, float master)
{
    if (!v->active || !v->audio || !v->audio->buffer || v->audio->numSamples == 0) return;
    for (int i = 0; i < frames && v->active; ++i) {
        uint32_t cur = (uint32_t)(v->curSampleFrac >> 16);
        if (cur >= v->audio->numSamples) {
            if (!v->looped) { v->active = 0; break; }
            v->curSampleFrac %= ((uint64_t)v->audio->numSamples << 16);
            cur = (uint32_t)(v->curSampleFrac >> 16);
        }
        uint32_t next = cur + 1u;
        if (next >= v->audio->numSamples) next = v->looped ? 0u : cur;
        float frac = (float)(v->curSampleFrac & 0xFFFFu) / 65536.0f;
        float l = AudioSample(v->audio, cur, 0) * (1.0f - frac) + AudioSample(v->audio, next, 0) * frac;
        float r = v->audio->numChannels > 1 ? AudioSample(v->audio, cur, 1) * (1.0f - frac) + AudioSample(v->audio, next, 1) * frac : l;
        out[i * 2] += l * v->lVolume * master;
        out[i * 2 + 1] += r * v->rVolume * master;
        v->curSampleFrac += v->stepFrac;
    }
}

static void AudioCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount)
{
    int frames = additional_amount / (int)(sizeof(float) * 2u);
    float *buffer;
    (void)userdata; (void)total_amount;
    if (!gAudioMutex || frames <= 0) return;
    buffer = (float *)calloc((size_t)frames * 2u, sizeof(float));
    if (!buffer) return;
    SDL_LockMutex(gAudioMutex);
    for (int i = 0; i < MAX_SOUND_CHANNELS; ++i) MixVoice(&gSoundVoices[i], buffer, frames, (float)gSfxVolume / 15.0f);
    MixVoice(&gStartupMovieAudioVoice, buffer, frames, 1.0f);
    if (!gMusicPaused) {
        MixVoice(&gMusicVoice, buffer, frames, (float)gMusicVolume / 15.0f);
        MixMidiMusic(buffer, frames);
    }
    for (int i = 0; i < frames * 2; ++i) buffer[i] = fmaxf(-1.0f, fminf(1.0f, buffer[i]));
    SDL_UnlockMutex(gAudioMutex);
    SDL_PutAudioStreamData(stream, buffer, frames * (int)(sizeof(float) * 2u));
    free(buffer);
}

static int LoadSamplesFromImage(void)
{
    int loaded = 0;
    for (int i = 1; i < NUMSFX; ++i) {
        char path[64]; uint8_t *data = NULL; uint32_t size = 0;
        snprintf(path, sizeof(path), "Sounds/Sound%02d.AIFF", i);
        if (!ReadAudioFile(path, &data, &size)) {
            fprintf(stderr, "DOOM3DO: audio SFX missing: %s\n", path);
            continue;
        }
        if (!DecodeAIFF(data, size, &gSamples[i])) {
            fprintf(stderr, "DOOM3DO: failed to decode %s (%u bytes)\n", path, (unsigned)size);
        } else {
            ++loaded;
        }
        free(data);
    }
    return loaded > 0;
}

static void FreeSamples(void)
{
    for (int i = 0; i < NUMSFX; ++i) FreeAudioData(&gSamples[i]);
    FreeAudioData(&gMusicData);
}

void PlaySound(Word sound)
{
    if (!gAudioMutex || !gAudioStream || sound == 0 || sound >= NUMSFX || !gSamples[sound].buffer) return;
    SDL_LockMutex(gAudioMutex);
    if (sound >= sfx_sawup && sound <= sfx_sawhit) {
        for (int i = 0; i < MAX_SOUND_CHANNELS; ++i) {
            if (gSoundVoices[i].active && gSoundVoices[i].audio == &gSamples[sound]) {
                gSoundVoices[i].active = 0;
                gSoundVoices[i].audio = NULL;
                break;
            }
        }
    }
    {
        int slot = -1;
        for (int i = 0; i < MAX_SOUND_CHANNELS; ++i) {
            if (!gSoundVoices[i].active) { slot = i; break; }
        }
        if (slot < 0) slot = 0;
        gSoundVoices[slot].audio = &gSamples[sound];
        gSoundVoices[slot].curSampleFrac = 0;
        gSoundVoices[slot].stepFrac = ((uint64_t)gSamples[sound].sampleRate << 16) / D3DO_AUDIO_RATE;
        gSoundVoices[slot].lVolume = (float)(LeftVolume > 255 ? 255 : LeftVolume) / 255.0f;
        gSoundVoices[slot].rVolume = (float)(RightVolume > 255 ? 255 : RightVolume) / 255.0f;
        gSoundVoices[slot].looped = 0; gSoundVoices[slot].active = 1;
    }
    LeftVolume = RightVolume = 255;
    SDL_UnlockMutex(gAudioMutex);
}

static void StopAllAudio(void)
{
    if (gAudioMutex) SDL_LockMutex(gAudioMutex);
    for (int i = 0; i < MAX_SOUND_CHANNELS; ++i) {
        gSoundVoices[i].active = 0;
        gSoundVoices[i].audio = NULL;
    }
    gMusicVoice.active = 0;
    gMusicVoice.audio = NULL;
    gMusicPlaying = 0;
    if (gAudioMutex) StopMidiMusicLocked();
    gMusicPaused = 0;
    if (gAudioMutex) SDL_UnlockMutex(gAudioMutex);
}

void StopSound(Word sound)
{
    if (!gAudioMutex || sound == 0 || sound >= NUMSFX) return;
    SDL_LockMutex(gAudioMutex);
    for (int i = 0; i < MAX_SOUND_CHANNELS; ++i) if (gSoundVoices[i].audio == &gSamples[sound]) gSoundVoices[i].active = 0;
    SDL_UnlockMutex(gAudioMutex);
}

void PlaySong(Word song)
{
    char path[64];
    const char *names[5];
    uint8_t *data = NULL;
    uint32_t size = 0;
    int i;

    if (song == 0) {
        SDL_LockMutex(gAudioMutex);
        gMusicVoice.active = 0;
        gMusicPlaying = 0;
        StopMidiMusicLocked();
        SDL_UnlockMutex(gAudioMutex);
        FreeAudioData(&gMusicData);
        return;
    }

    snprintf(path, sizeof(path), "Music/Song%u", (unsigned)song);
    {
        static char midiPath[64];
        static char midiPath2[64];
        static char aiffPath[64];
        static char aifPath[64];
        snprintf(midiPath, sizeof(midiPath), "Music/Song%u.mid", (unsigned)song);
        snprintf(midiPath2, sizeof(midiPath2), "Music/Song%u.midi", (unsigned)song);
        snprintf(aiffPath, sizeof(aiffPath), "Music/Song%u.aiff", (unsigned)song);
        snprintf(aifPath, sizeof(aifPath), "Music/Song%u.aif", (unsigned)song);
        names[0] = midiPath;
        names[1] = midiPath2;
        names[2] = path;
        names[3] = aiffPath;
        names[4] = aifPath;

        for (i = 0; i < 5; ++i) {
            if (ReadAudioFile(names[i], &data, &size)) {
                snprintf(path, sizeof(path), "%s", names[i]);
                break;
            }
        }
    }
    if (!data) {
        fprintf(stderr, "DOOM3DO: music file not found: Music/Song%u[.mid|.midi|.aiff|.aif]\n", (unsigned)song);
        return;
    }

    if (size >= 4 && memcmp(data, "MThd", 4) == 0) {
        if (!LoadMidiMusic(data, size))
            fprintf(stderr, "DOOM3DO: failed to load MIDI music: %s (%u bytes)\n", path, (unsigned)size);
        else {
            SDL_LockMutex(gAudioMutex);
            gMusicVoice.active = 0;
            gMusicPlaying = 0;
            SDL_UnlockMutex(gAudioMutex);
        }
        free(data);
        return;
    }

    {
        D3DO_AudioData decoded;
        memset(&decoded, 0, sizeof(decoded));
        if (!DecodeAIFF(data, size, &decoded)) {
            fprintf(stderr, "DOOM3DO: failed to decode music: %s (%u bytes)\n", path, (unsigned)size);
            free(data);
            return;
        }
        free(data);
        SDL_LockMutex(gAudioMutex);
        StopMidiMusicLocked();
        FreeAudioData(&gMusicData);
        gMusicData = decoded;
        memset(&gMusicVoice, 0, sizeof(gMusicVoice));
        gMusicVoice.audio = &gMusicData;
        gMusicVoice.curSampleFrac = 0;
        gMusicVoice.stepFrac = ((uint64_t)gMusicData.sampleRate << 16) / D3DO_AUDIO_RATE;
        gMusicVoice.lVolume = gMusicVoice.rVolume = 1.0f;
        gMusicVoice.looped = 1; gMusicVoice.active = 1;
        gMusicPaused = 0; gMusicPlaying = 1;
        SDL_UnlockMutex(gAudioMutex);
    }
}

void PauseMusic(void) { gMusicPaused = 1; }
void ResumeMusic(void) { gMusicPaused = 0; }
void SetSfxVolume(Word volume) { gSfxVolume = volume > 15 ? 15 : volume; SfxVolume = gSfxVolume; }
void SetMusicVolume(Word volume) { gMusicVolume = volume > 15 ? 15 : volume; MusicVolume = gMusicVolume; }

static const char *PrefsName(void)
{
    return "DoomPrefs";
}

int32 StdReadFile(char *fName, char *buf)
{
    FILE *f;
    long size;
    f = fopen(fName, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return -1; }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

int32 SaveAFile(Byte *name, void *data, LongWord size)
{
    FILE *f = fopen((const char *)name, "wb");
    if (!f) return -1;
    if (fwrite(data, 1, size, f) != size) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

void LongWordToAscii(LongWord value, Byte *text)
{
    sprintf((char *)text, "%u", (unsigned)value);
}


#define D3DO_CINE_WIDTH 280u
#define D3DO_CINE_HEIGHT 200u
#define D3DO_CINE_FPS 12u
#define D3DO_CINE_BLOCK_WIDTH (D3DO_CINE_WIDTH / 4u)
#define D3DO_CINE_BLOCK_HEIGHT (D3DO_CINE_HEIGHT / 4u)
#define D3DO_CINE_BLOCK_COUNT (D3DO_CINE_BLOCK_WIDTH * D3DO_CINE_BLOCK_HEIGHT)
#define D3DO_CINE_STREAM_HEADER_SIZE 20u
#define D3DO_CINE_CHUNK_HEADER_SIZE 24u
#define D3DO_CINE_FRAME_HEADER_SIZE 20u
#define D3DO_CINE_STRIP_HEADER_SIZE 12u
#define D3DO_CINE_FRAME_FLAG_KEEP_CODEBOOKS 0x01u

#define D3DO_CINE_KEYFRAME_V4_CODEBOOK 0x2000u
#define D3DO_CINE_DELTAFRAME_V4_CODEBOOK 0x2100u
#define D3DO_CINE_KEYFRAME_V1_CODEBOOK 0x2200u
#define D3DO_CINE_DELTAFRAME_V1_CODEBOOK 0x2300u
#define D3DO_CINE_KEYFRAME_VECTORS 0x3000u
#define D3DO_CINE_DELTAFRAME_VECTORS 0x3100u
#define D3DO_CINE_KEYFRAME_V1_VECTORS 0x3200u

typedef struct D3DO_CineReader {
    const uint8_t *data;
    size_t size;
    size_t position;
} D3DO_CineReader;

typedef struct D3DO_CineVector {
    uint8_t y0;
    uint8_t y1;
    uint8_t y2;
    uint8_t y3;
    int8_t u;
    int8_t v;
} D3DO_CineVector;

typedef struct D3DO_CineCodebook {
    D3DO_CineVector vectors[256];
} D3DO_CineCodebook;

typedef struct D3DO_CineDecoder {
    uint8_t *movieData;
    size_t movieDataSize;
    size_t frameOffset;
    uint32_t frameNumber;
    uint32_t frameCount;
    D3DO_CineCodebook codebooks[2];
    uint32_t pixels[D3DO_CINE_WIDTH * D3DO_CINE_HEIGHT];
} D3DO_CineDecoder;

static int D3DO_CineReadU8(D3DO_CineReader *reader, uint8_t *value)
{
    if (reader->position >= reader->size)
        return 0;
    *value = reader->data[reader->position++];
    return 1;
}

static int D3DO_CineReadBE16(D3DO_CineReader *reader, uint16_t *value)
{
    if (reader->size - reader->position < 2u)
        return 0;
    *value = ReadBE16(reader->data + reader->position);
    reader->position += 2u;
    return 1;
}

static int D3DO_CineReadBE32(D3DO_CineReader *reader, uint32_t *value)
{
    if (reader->size - reader->position < 4u)
        return 0;
    *value = ReadBE32(reader->data + reader->position);
    reader->position += 4u;
    return 1;
}

static int D3DO_CineReadVector(D3DO_CineReader *reader, D3DO_CineVector *vector)
{
    uint8_t value;

    if (!D3DO_CineReadU8(reader, &vector->y0) ||
        !D3DO_CineReadU8(reader, &vector->y1) ||
        !D3DO_CineReadU8(reader, &vector->y2) ||
        !D3DO_CineReadU8(reader, &vector->y3) ||
        !D3DO_CineReadU8(reader, &value))
        return 0;
    vector->u = (int8_t)value;
    if (!D3DO_CineReadU8(reader, &value))
        return 0;
    vector->v = (int8_t)value;
    return 1;
}

static uint32_t D3DO_CineYUVToARGB8888(uint8_t yValue, int8_t uValue, int8_t vValue)
{
    int32_t y = (int32_t)yValue;
    int32_t u = (int32_t)uValue;
    int32_t v = (int32_t)vValue;
    int32_t red = y + v * 2;
    int32_t green = y - u / 2 - v;
    int32_t blue = y + u * 2;

    if (red < 0)
        red = 0;
    else if (red > 255)
        red = 255;
    if (green < 0)
        green = 0;
    else if (green > 255)
        green = 255;
    if (blue < 0)
        blue = 0;
    else if (blue > 255)
        blue = 255;

    return 0xFF000000u | ((uint32_t)red << 16) |
           ((uint32_t)green << 8) | (uint32_t)blue;
}

static uint16_t D3DO_CineARGB8888ToARGB1555(uint32_t pixel)
{
    uint32_t red = (pixel >> 16) & 0xFFu;
    uint32_t green = (pixel >> 8) & 0xFFu;
    uint32_t blue = pixel & 0xFFu;

    return (uint16_t)(0x8000u | ((red >> 3) << 10) |
                      ((green >> 3) << 5) | (blue >> 3));
}

static void D3DO_CineDecodePixelBlock(D3DO_CineDecoder *decoder,
                                      uint32_t blockIndex,
                                      const D3DO_CineCodebook *codebook,
                                      uint8_t v0Index, uint8_t v1Index,
                                      uint8_t v2Index, uint8_t v3Index)
{
    uint32_t blockX = (blockIndex % D3DO_CINE_BLOCK_WIDTH) * 4u;
    uint32_t blockY = (blockIndex / D3DO_CINE_BLOCK_WIDTH) * 4u;
    D3DO_CineVector v0 = codebook->vectors[v0Index];
    D3DO_CineVector v1 = codebook->vectors[v1Index];
    D3DO_CineVector v2 = codebook->vectors[v2Index];
    D3DO_CineVector v3 = codebook->vectors[v3Index];
    uint32_t *row0 = decoder->pixels + (blockY + 0u) * D3DO_CINE_WIDTH + blockX;
    uint32_t *row1 = decoder->pixels + (blockY + 1u) * D3DO_CINE_WIDTH + blockX;
    uint32_t *row2 = decoder->pixels + (blockY + 2u) * D3DO_CINE_WIDTH + blockX;
    uint32_t *row3 = decoder->pixels + (blockY + 3u) * D3DO_CINE_WIDTH + blockX;

    row0[0] = D3DO_CineYUVToARGB8888(v0.y0, v0.u, v0.v);
    row0[1] = D3DO_CineYUVToARGB8888(v0.y1, v0.u, v0.v);
    row0[2] = D3DO_CineYUVToARGB8888(v1.y0, v1.u, v1.v);
    row0[3] = D3DO_CineYUVToARGB8888(v1.y1, v1.u, v1.v);
    row1[0] = D3DO_CineYUVToARGB8888(v0.y2, v0.u, v0.v);
    row1[1] = D3DO_CineYUVToARGB8888(v0.y3, v0.u, v0.v);
    row1[2] = D3DO_CineYUVToARGB8888(v1.y2, v1.u, v1.v);
    row1[3] = D3DO_CineYUVToARGB8888(v1.y3, v1.u, v1.v);
    row2[0] = D3DO_CineYUVToARGB8888(v2.y0, v2.u, v2.v);
    row2[1] = D3DO_CineYUVToARGB8888(v2.y1, v2.u, v2.v);
    row2[2] = D3DO_CineYUVToARGB8888(v3.y0, v3.u, v3.v);
    row2[3] = D3DO_CineYUVToARGB8888(v3.y1, v3.u, v3.v);
    row3[0] = D3DO_CineYUVToARGB8888(v2.y2, v2.u, v2.v);
    row3[1] = D3DO_CineYUVToARGB8888(v2.y3, v2.u, v2.v);
    row3[2] = D3DO_CineYUVToARGB8888(v3.y2, v3.u, v3.v);
    row3[3] = D3DO_CineYUVToARGB8888(v3.y3, v3.u, v3.v);
}

static int D3DO_CineDecodeCodebook(D3DO_CineReader *reader,
                                   D3DO_CineCodebook *codebook,
                                   int delta)
{
    uint32_t startIndex;

    if (!delta) {
        uint32_t entryCount = (uint32_t)(reader->size - reader->position) / 6u;
        if (entryCount > 256u)
            return 0;
        for (startIndex = 0; startIndex < entryCount; ++startIndex) {
            if (!D3DO_CineReadVector(reader, &codebook->vectors[startIndex]))
                return 0;
        }
        return 1;
    }

    if (reader->position == reader->size)
        return 1;

    for (startIndex = 0; startIndex < 256u; startIndex += 32u) {
        uint32_t updateFlags;
        uint32_t vectorIndex;

        if (!D3DO_CineReadBE32(reader, &updateFlags))
            return 0;
        for (vectorIndex = startIndex; vectorIndex < startIndex + 32u; ++vectorIndex) {
            if (updateFlags & 0x80000000u) {
                if (!D3DO_CineReadVector(reader, &codebook->vectors[vectorIndex]))
                    return 0;
            }
            updateFlags <<= 1;
        }
    }
    return 1;
}

static int D3DO_CineDecodeKeyFrameVectors(D3DO_CineDecoder *decoder,
                                     D3DO_CineReader *reader)
{
    uint32_t batchStart;

    for (batchStart = 0; batchStart < D3DO_CINE_BLOCK_COUNT; batchStart += 32u) {
        uint32_t flags;
        uint32_t blockIndex;
        uint32_t endBlock = batchStart + 32u;

        if (endBlock > D3DO_CINE_BLOCK_COUNT)
            endBlock = D3DO_CINE_BLOCK_COUNT;
        if (!D3DO_CineReadBE32(reader, &flags))
            return 0;

        for (blockIndex = batchStart; blockIndex < endBlock; ++blockIndex) {
            if (flags & 0x80000000u) {
                uint8_t v0, v1, v2, v3;
                if (!D3DO_CineReadU8(reader, &v0) ||
                    !D3DO_CineReadU8(reader, &v1) ||
                    !D3DO_CineReadU8(reader, &v2) ||
                    !D3DO_CineReadU8(reader, &v3))
                    return 0;
                D3DO_CineDecodePixelBlock(decoder, blockIndex,
                                          &decoder->codebooks[1], v0, v1, v2, v3);
            } else {
                uint8_t vectorIndex;
                if (!D3DO_CineReadU8(reader, &vectorIndex))
                    return 0;
                D3DO_CineDecodePixelBlock(decoder, blockIndex,
                                          &decoder->codebooks[0], vectorIndex,
                                          vectorIndex, vectorIndex, vectorIndex);
            }
            flags <<= 1;
        }
    }
    return 1;
}

static int D3DO_CineDecodeKeyFrameV1Vectors(D3DO_CineDecoder *decoder,
                                       D3DO_CineReader *reader)
{
    uint32_t blockIndex;

    for (blockIndex = 0; blockIndex < D3DO_CINE_BLOCK_COUNT; ++blockIndex) {
        uint8_t vectorIndex;
        if (!D3DO_CineReadU8(reader, &vectorIndex))
            return 0;
        D3DO_CineDecodePixelBlock(decoder, blockIndex,
                                  &decoder->codebooks[0], vectorIndex,
                                  vectorIndex, vectorIndex, vectorIndex);
    }
    return 1;
}

static int D3DO_CineDecodeDeltaFrameVectors(D3DO_CineDecoder *decoder,
                                       D3DO_CineReader *reader)
{
    uint32_t flags = 0;
    uint32_t flagBits = 0;
    uint32_t blockIndex;

    for (blockIndex = 0; blockIndex < D3DO_CINE_BLOCK_COUNT; ++blockIndex) {
        int updated;
        int v4Coded;

        if (!flagBits) {
            if (!D3DO_CineReadBE32(reader, &flags))
                return 0;
            flagBits = 32u;
        }

        updated = (flags & 0x80000000u) != 0;
        flags <<= 1;
        --flagBits;
        if (!updated)
            continue;

        if (!flagBits) {
            if (!D3DO_CineReadBE32(reader, &flags))
                return 0;
            flagBits = 32u;
        }

        v4Coded = (flags & 0x80000000u) != 0;
        flags <<= 1;
        --flagBits;
        if (v4Coded) {
            uint8_t v0, v1, v2, v3;
            if (!D3DO_CineReadU8(reader, &v0) ||
                !D3DO_CineReadU8(reader, &v1) ||
                !D3DO_CineReadU8(reader, &v2) ||
                !D3DO_CineReadU8(reader, &v3))
                return 0;
            D3DO_CineDecodePixelBlock(decoder, blockIndex,
                                      &decoder->codebooks[1], v0, v1, v2, v3);
        } else {
            uint8_t vectorIndex;
            if (!D3DO_CineReadU8(reader, &vectorIndex))
                return 0;
            D3DO_CineDecodePixelBlock(decoder, blockIndex,
                                      &decoder->codebooks[0], vectorIndex,
                                      vectorIndex, vectorIndex, vectorIndex);
        }
    }
    return 1;
}

static int D3DO_CineDecodeChunk(D3DO_CineDecoder *decoder,
                                D3DO_CineReader *reader)
{
    uint16_t chunkType;
    uint16_t chunkSize;
    D3DO_CineReader chunkReader;

    if (!D3DO_CineReadBE16(reader, &chunkType) ||
        !D3DO_CineReadBE16(reader, &chunkSize) ||
        chunkSize < 4u || (size_t)(chunkSize - 4u) > reader->size - reader->position)
        return 0;

    chunkReader.data = reader->data + reader->position;
    chunkReader.size = chunkSize - 4u;
    chunkReader.position = 0;
    reader->position += chunkReader.size;

    switch (chunkType) {
    case D3DO_CINE_KEYFRAME_V4_CODEBOOK:
        return D3DO_CineDecodeCodebook(&chunkReader, &decoder->codebooks[1], 0);
    case D3DO_CINE_DELTAFRAME_V4_CODEBOOK:
        return D3DO_CineDecodeCodebook(&chunkReader, &decoder->codebooks[1], 1);
    case D3DO_CINE_KEYFRAME_V1_CODEBOOK:
        return D3DO_CineDecodeCodebook(&chunkReader, &decoder->codebooks[0], 0);
    case D3DO_CINE_DELTAFRAME_V1_CODEBOOK:
        return D3DO_CineDecodeCodebook(&chunkReader, &decoder->codebooks[0], 1);
    case D3DO_CINE_KEYFRAME_VECTORS:
        return D3DO_CineDecodeKeyFrameVectors(decoder, &chunkReader);
    case D3DO_CINE_DELTAFRAME_VECTORS:
        return D3DO_CineDecodeDeltaFrameVectors(decoder, &chunkReader);
    case D3DO_CINE_KEYFRAME_V1_VECTORS:
        return D3DO_CineDecodeKeyFrameV1Vectors(decoder, &chunkReader);
    default:
        return 1;
    }
}

static int D3DO_CineExtractVideoStream(const uint8_t *fileData, size_t fileSize,
                                       uint8_t **outData, size_t *outSize)
{
    size_t position;
    size_t totalSize = 0;
    uint8_t *streamData;
    size_t streamPosition = 0;

    if (!fileData || fileSize < 244u || !outData || !outSize ||
        memcmp(fileData, "SHDR", 4) != 0 || ReadBE32(fileData + 4u) != 244u)
        return 0;

    position = 244u;
    while (position + D3DO_CINE_CHUNK_HEADER_SIZE <= fileSize) {
        uint32_t chunkSize = ReadBE32(fileData + position + 4u);
        if (chunkSize < D3DO_CINE_CHUNK_HEADER_SIZE ||
            position + chunkSize > fileSize)
            return 0;
        if (memcmp(fileData + position, "FILM", 4) == 0)
            totalSize += chunkSize - D3DO_CINE_CHUNK_HEADER_SIZE;
        position += chunkSize;
    }

    if (position != fileSize || totalSize <= D3DO_CINE_STREAM_HEADER_SIZE)
        return 0;

    streamData = (uint8_t *)malloc(totalSize);
    if (!streamData)
        return 0;

    position = 244u;
    while (position + D3DO_CINE_CHUNK_HEADER_SIZE <= fileSize) {
        uint32_t chunkSize = ReadBE32(fileData + position + 4u);
        if (memcmp(fileData + position, "FILM", 4) == 0) {
            size_t payloadSize = chunkSize - D3DO_CINE_CHUNK_HEADER_SIZE;
            memcpy(streamData + streamPosition,
                   fileData + position + D3DO_CINE_CHUNK_HEADER_SIZE,
                   payloadSize);
            streamPosition += payloadSize;
        }
        position += chunkSize;
    }

    *outData = streamData;
    *outSize = streamPosition;
    return 1;
}

static int D3DO_CineInitDecoder(D3DO_CineDecoder *decoder,
                                const uint8_t *fileData, size_t fileSize)
{
    uint8_t *streamData = NULL;
    size_t streamSize = 0;

    memset(decoder, 0, sizeof(*decoder));
    if (!D3DO_CineExtractVideoStream(fileData, fileSize, &streamData, &streamSize))
        return 0;
    if (memcmp(streamData, "cvid", 4) != 0 && memcmp(streamData, "CVID", 4) != 0) {
        free(streamData);
        return 0;
    }
    if (ReadBE32(streamData + 4u) != D3DO_CINE_HEIGHT ||
        ReadBE32(streamData + 8u) != D3DO_CINE_WIDTH ||
        ReadBE32(streamData + 16u) == 0u) {
        free(streamData);
        return 0;
    }

    decoder->movieData = streamData;
    decoder->movieDataSize = streamSize;
    decoder->frameOffset = D3DO_CINE_STREAM_HEADER_SIZE;
    decoder->frameNumber = 0;
    decoder->frameCount = ReadBE32(streamData + 16u);
    return 1;
}

static void D3DO_CineShutdownDecoder(D3DO_CineDecoder *decoder)
{
    free(decoder->movieData);
    memset(decoder, 0, sizeof(*decoder));
}

static int D3DO_CineDecodeNextFrame(D3DO_CineDecoder *decoder)
{
    const uint8_t *frameData;
    size_t frameEnd;
    uint32_t frameSize;
    uint8_t frameFlags;
    uint16_t width;
    uint16_t height;
    uint16_t stripCount;
    size_t stripPosition;
    uint16_t stripSize;
    uint16_t stripWidth;
    uint16_t stripHeight;
    D3DO_CineReader stripReader;

    if (decoder->frameNumber >= decoder->frameCount ||
        decoder->frameOffset + D3DO_CINE_FRAME_HEADER_SIZE > decoder->movieDataSize)
        return 0;

    frameData = decoder->movieData + decoder->frameOffset;
    frameSize = ReadBE32(frameData);
    frameFlags = frameData[4];
    width = ReadBE16(frameData + 8u);
    height = ReadBE16(frameData + 10u);
    stripCount = ReadBE16(frameData + 12u);
    frameEnd = decoder->frameOffset + 4u + frameSize;

    if (frameSize < D3DO_CINE_FRAME_HEADER_SIZE ||
        frameEnd > decoder->movieDataSize || width != D3DO_CINE_WIDTH ||
        height != D3DO_CINE_HEIGHT || stripCount != 1u)
        return 0;

    if (!(frameFlags & D3DO_CINE_FRAME_FLAG_KEEP_CODEBOOKS))
        memset(decoder->codebooks, 0, sizeof(decoder->codebooks));

    stripPosition = decoder->frameOffset + D3DO_CINE_FRAME_HEADER_SIZE;
    if (stripPosition + D3DO_CINE_STRIP_HEADER_SIZE > frameEnd)
        return 0;

    stripSize = ReadBE16(decoder->movieData + stripPosition + 2u);
    stripHeight = ReadBE16(decoder->movieData + stripPosition + 8u);
    stripWidth = ReadBE16(decoder->movieData + stripPosition + 10u);

    if (stripSize < D3DO_CINE_STRIP_HEADER_SIZE ||
        stripPosition + stripSize > frameEnd ||
        stripWidth != D3DO_CINE_WIDTH || stripHeight != D3DO_CINE_HEIGHT)
        return 0;

    stripReader.data = decoder->movieData + stripPosition + D3DO_CINE_STRIP_HEADER_SIZE;
    stripReader.size = stripSize - D3DO_CINE_STRIP_HEADER_SIZE;
    stripReader.position = 0;

    while (stripReader.position < stripReader.size) {
        if (!D3DO_CineDecodeChunk(decoder, &stripReader))
            return 0;
    }

    decoder->frameOffset = frameEnd;
    ++decoder->frameNumber;
    return 1;
}

static int D3DO_StartupAssetPath(const char *filename, char *path, size_t pathSize)
{
    char basePath[PATH_MAX];
    const char *exePath;
    FILE *file;

    if (!filename || !path || pathSize == 0u)
        return 0;

    file = fopen(filename, "rb");
    if (file) {
        fclose(file);
        snprintf(path, pathSize, "%s", filename);
        return 1;
    }

    snprintf(path, pathSize, "startup/%s", filename);
    file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return 1;
    }

    exePath = SDL_GetBasePath();
    if (!exePath)
        return 0;
    snprintf(basePath, sizeof(basePath), "%s", exePath);
    SDL_free((void *)exePath);

    snprintf(path, pathSize, "%sstartup/%s", basePath, filename);
    file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return 1;
    }

    snprintf(path, pathSize, "%s%s", basePath, filename);
    file = fopen(path, "rb");
    if (file) {
        fclose(file);
        return 1;
    }

    return 0;
}

static int D3DO_LoadStartupFile(const char *filename, uint8_t **data, size_t *size)
{
    char path[PATH_MAX];
    FILE *file;
    long fileSize;
    uint8_t *fileData;

    if (!D3DO_StartupAssetPath(filename, path, sizeof(path)))
        return 0;

    file = fopen(path, "rb");
    if (!file)
        return 0;
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }
    fileSize = ftell(file);
    if (fileSize <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    fileData = (uint8_t *)malloc((size_t)fileSize);
    if (!fileData) {
        fclose(file);
        return 0;
    }
    if (fread(fileData, 1, (size_t)fileSize, file) != (size_t)fileSize) {
        free(fileData);
        fclose(file);
        return 0;
    }
    fclose(file);
    *data = fileData;
    *size = (size_t)fileSize;
    return 1;
}

static int D3DO_LoadStartupCel(const char *filename, uint16_t **pixels,
                               uint16_t *width, uint16_t *height)
{
    uint8_t *fileData = NULL;
    size_t fileSize = 0;
    size_t position = 0;
    const uint8_t *ccbData = NULL;
    const uint8_t *pdatData = NULL;
    size_t pdatSize = 0;
    uint8_t paletteBytes[64];
    const uint8_t *palette = NULL;
    uint32_t ccbFlags;
    uint32_t pre0;
    uint32_t pre1;
    int result = 0;

    if (!pixels || !width || !height || !D3DO_LoadStartupFile(filename, &fileData, &fileSize))
        return 0;

    while (position + 8u <= fileSize) {
        uint32_t chunkSize = ReadBE32(fileData + position + 4u);
        if (chunkSize < 8u || position + chunkSize > fileSize)
            goto cleanup;
        if (memcmp(fileData + position, "CCB ", 4) == 0) {
            if (chunkSize < 80u)
                goto cleanup;
            ccbData = fileData + position + 12u;
            ccbFlags = ReadBE32(ccbData + 0u);
            pre0 = ReadBE32(ccbData + 52u);
            pre1 = ReadBE32(ccbData + 56u);
        } else if (memcmp(fileData + position, "PDAT", 4) == 0) {
            pdatData = fileData + position + 8u;
            pdatSize = chunkSize - 8u;
        } else if (memcmp(fileData + position, "PLUT", 4) == 0) {
            size_t paletteSize = chunkSize - 8u;
            uint32_t count;
            uint32_t entries;
            size_t index;
            if (paletteSize < 4u)
                goto cleanup;
            count = ReadBE32(fileData + position + 8u);
            entries = count > 32u ? 32u : count;
            if (paletteSize - 4u < (size_t)entries * 2u)
                goto cleanup;
            memset(paletteBytes, 0, sizeof(paletteBytes));
            for (index = 0; index < entries; ++index) {
                uint16_t color = ReadBE16(fileData + position + 12u + index * 2u);
                memcpy(paletteBytes + index * 2u, &color, sizeof(color));
            }
            palette = paletteBytes;
        }
        position += chunkSize;
    }

    if (!ccbData || !pdatData)
        goto cleanup;

    result = D3DO_DecodeCelResolved(
        pdatData, (uint32_t)pdatSize, palette, pre0, pre1, ccbFlags, 0,
        pixels, width, height);

cleanup:
    free(fileData);
    return result;
}

static uint16_t D3DO_StartupScalePixel(uint16_t pixel, uint32_t brightness)
{
    uint32_t red = (pixel >> 10) & 31u;
    uint32_t green = (pixel >> 5) & 31u;
    uint32_t blue = pixel & 31u;

    red = red * brightness / 255u;
    green = green * brightness / 255u;
    blue = blue * brightness / 255u;
    return (uint16_t)(0x8000u | (red << 10) | (green << 5) | blue);
}

static int D3DO_ProcessStartupEvents(void)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT)
            exit(0);
        if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
            continue;
        if (event.key.key == SDLK_W && (event.key.mod & SDL_KMOD_CTRL)) {
            D3DO_ToggleFullscreen();
            continue;
        }
        if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER ||
            event.key.key == SDLK_SPACE || event.key.key == SDLK_ESCAPE)
            return 1;
    }
    return 0;
}

static uint64_t D3DO_GetStartupTime(void)
{
    return SDL_GetPerformanceCounter();
}

static void D3DO_WaitStartupTime(uint64_t targetTime)
{
    uint64_t now = D3DO_GetStartupTime();
    if (now >= targetTime || gTickFrequency == 0u)
        return;
    {
        uint64_t remaining = targetTime - now;
        uint64_t milliseconds = (remaining * 1000u) / gTickFrequency;
        if (milliseconds > 1u)
            SDL_Delay((uint32_t)(milliseconds - 1u));
    }
}

static void D3DO_ShowStartupCel(const char *filename,
                                uint32_t fadeInMilliseconds,
                                uint32_t holdMilliseconds,
                                uint32_t fadeOutMilliseconds)
{
    uint16_t *pixels = NULL;
    uint16_t width = 0;
    uint16_t height = 0;
    uint64_t startTime;
    uint64_t endTime;
    uint64_t frameStep;
    int finished = 0;

    if (!D3DO_LoadStartupCel(filename, &pixels, &width, &height))
        return;

    startTime = D3DO_GetStartupTime();
    endTime = startTime +
        ((uint64_t)(fadeInMilliseconds + holdMilliseconds + fadeOutMilliseconds) * gTickFrequency) / 1000u;
    frameStep = gTickFrequency / 60u;
    if (frameStep == 0u)
        frameStep = 1u;

    while (!finished) {
        uint64_t now = D3DO_GetStartupTime();
        uint64_t elapsedMilliseconds = gTickFrequency ?
            ((now - startTime) * 1000u) / gTickFrequency : 0u;
        uint32_t brightness = 255u;
        int yStart = (DOOM3DO_HEIGHT - (int)height) / 2;
        int xStart = (DOOM3DO_WIDTH - (int)width) / 2;
        int y;
        int x;
        uint64_t nextFrame;

        if (D3DO_ProcessStartupEvents())
            break;

        if (elapsedMilliseconds < fadeInMilliseconds && fadeInMilliseconds)
            brightness = (uint32_t)((elapsedMilliseconds * 255u) / fadeInMilliseconds);
        else if (elapsedMilliseconds >= fadeInMilliseconds + holdMilliseconds && fadeOutMilliseconds) {
            uint64_t fadeElapsed = elapsedMilliseconds - fadeInMilliseconds - holdMilliseconds;
            if (fadeElapsed >= fadeOutMilliseconds)
                finished = 1;
            else
                brightness = 255u - (uint32_t)((fadeElapsed * 255u) / fadeOutMilliseconds);
        } else if (elapsedMilliseconds >= (uint64_t)fadeInMilliseconds + holdMilliseconds) {
            finished = 1;
        }

        if (finished)
            break;

        memset(gDisplayedFramebuffer, 0,
               (size_t)DOOM3DO_WIDTH * DOOM3DO_HEIGHT * sizeof(uint16_t));
        for (y = 0; y < (int)height; ++y) {
            int destinationY = yStart + y;
            if (destinationY < 0 || destinationY >= DOOM3DO_HEIGHT)
                continue;
            for (x = 0; x < (int)width; ++x) {
                int destinationX = xStart + x;
                if (destinationX < 0 || destinationX >= DOOM3DO_WIDTH)
                    continue;
                gDisplayedFramebuffer[destinationY * DOOM3DO_WIDTH + destinationX] =
                    D3DO_StartupScalePixel(pixels[y * width + x], brightness);
            }
        }
        PresentFramebuffer(gDisplayedFramebuffer);
        nextFrame = now + frameStep;
        D3DO_WaitStartupTime(nextFrame);
        if (D3DO_GetStartupTime() >= endTime)
            finished = 1;
    }

    free(pixels);
}

static int D3DO_LoadStartupMovieAudio(const uint8_t *fileData, size_t fileSize,
                                     D3DO_AudioData *audio)
{
    size_t position;
    uint32_t audioSize = 0;
    uint32_t sampleRate = 0;
    uint32_t channels = 0;
    uint32_t bitDepth = 0;
    uint8_t *buffer;
    size_t written = 0;

    if (!fileData || !audio || fileSize < 8u)
        return 0;

    memset(audio, 0, sizeof(*audio));
    position = ReadBE32(fileData + 4u);
    if (position < 8u || position > fileSize)
        return 0;

    while (position + 24u <= fileSize) {
        const uint8_t *chunk = fileData + position;
        uint32_t chunkSize = ReadBE32(chunk + 4u);

        if (chunkSize < 24u || position + chunkSize > fileSize)
            return 0;
        if (memcmp(chunk, "SNDS", 4) == 0) {
            if (memcmp(chunk + 16u, "SHDR", 4) == 0) {
                if (chunkSize < 64u)
                    return 0;
                bitDepth = ReadBE32(chunk + 40u);
                sampleRate = ReadBE32(chunk + 44u);
                channels = ReadBE32(chunk + 48u);
                audioSize = ReadBE32(chunk + 60u);
            } else if (memcmp(chunk + 16u, "SSMP", 4) == 0) {
                uint32_t blockSize = ReadBE32(chunk + 20u);
                if (blockSize > chunkSize - 24u || audioSize == 0u)
                    return 0;
            }
        }
        position += chunkSize;
    }

    if (!audioSize || !sampleRate || (channels != 1u && channels != 2u) ||
        (bitDepth != 8u && bitDepth != 16u))
        return 0;

    buffer = (uint8_t *)malloc(audioSize);
    if (!buffer)
        return 0;

    position = ReadBE32(fileData + 4u);
    while (position + 24u <= fileSize && written < audioSize) {
        const uint8_t *chunk = fileData + position;
        uint32_t chunkSize = ReadBE32(chunk + 4u);

        if (chunkSize < 24u || position + chunkSize > fileSize) {
            free(buffer);
            return 0;
        }
        if (memcmp(chunk, "SNDS", 4) == 0 && memcmp(chunk + 16u, "SSMP", 4) == 0) {
            uint32_t blockSize = ReadBE32(chunk + 20u);
            if ((size_t)blockSize > audioSize - written ||
                blockSize > chunkSize - 24u) {
                free(buffer);
                return 0;
            }
            memcpy(buffer + written, chunk + 24u, blockSize);
            written += blockSize;
        }
        position += chunkSize;
    }

    if (written != audioSize) {
        free(buffer);
        return 0;
    }

    if ((audioSize % ((bitDepth / 8u) * channels)) != 0u) {
        free(buffer);
        return 0;
    }

    audio->ownedBuffer = buffer;
    audio->buffer = buffer;
    audio->bufferSize = audioSize;
    audio->numSamples = audioSize / ((bitDepth / 8u) * channels);
    audio->sampleRate = sampleRate;
    audio->numChannels = channels;
    audio->bitDepth = bitDepth;
    return 1;
}

static void D3DO_StopStartupMovieAudio(void)
{
    if (gAudioMutex)
        SDL_LockMutex(gAudioMutex);

    gStartupMovieAudioVoice.active = 0;
    gStartupMovieAudioVoice.audio = NULL;
    FreeAudioData(&gStartupMovieAudioData);

    if (gAudioMutex)
        SDL_UnlockMutex(gAudioMutex);
}

static int D3DO_StartStartupMovieAudio(const uint8_t *fileData, size_t fileSize)
{
    D3DO_AudioData audio;

    if (!D3DO_LoadStartupMovieAudio(fileData, fileSize, &audio))
        return 0;

    D3DO_StopStartupMovieAudio();
    if (!gAudioMutex || !gAudioStream) {
        FreeAudioData(&audio);
        return 0;
    }

    SDL_LockMutex(gAudioMutex);
    gStartupMovieAudioData = audio;
    memset(&gStartupMovieAudioVoice, 0, sizeof(gStartupMovieAudioVoice));
    gStartupMovieAudioVoice.audio = &gStartupMovieAudioData;
    gStartupMovieAudioVoice.curSampleFrac = 0;
    gStartupMovieAudioVoice.stepFrac =
        ((uint64_t)audio.sampleRate << 16) / D3DO_AUDIO_RATE;
    gStartupMovieAudioVoice.lVolume = 1.0f;
    gStartupMovieAudioVoice.rVolume = 1.0f;
    gStartupMovieAudioVoice.looped = 0;
    gStartupMovieAudioVoice.active = 1;
    SDL_UnlockMutex(gAudioMutex);
    return 1;
}

static int D3DO_IsStartupMovieAudioPlaying(void)
{
    int active;

    if (!gAudioMutex)
        return 0;
    SDL_LockMutex(gAudioMutex);
    active = gStartupMovieAudioVoice.active;
    SDL_UnlockMutex(gAudioMutex);
    return active;
}

static int D3DO_PlayStartupMovie(const char *filename)
{
    uint8_t *fileData = NULL;
    size_t fileSize = 0;
    D3DO_CineDecoder decoder;
    uint64_t nextFrameTime;
    uint64_t frameStep;
    int result = 1;
    int movieAudioPlaying = 0;

    if (!D3DO_LoadStartupFile(filename, &fileData, &fileSize))
        return 0;
    if (!D3DO_CineInitDecoder(&decoder, fileData, fileSize)) {
        free(fileData);
        return 0;
    }

    movieAudioPlaying = D3DO_StartStartupMovieAudio(fileData, fileSize);
    free(fileData);

    frameStep = gTickFrequency / D3DO_CINE_FPS;
    if (frameStep == 0u)
        frameStep = 1u;
    nextFrameTime = D3DO_GetStartupTime();

    while (decoder.frameNumber < decoder.frameCount) {
        size_t index;
        uint16_t *destination;
        uint64_t now;

        destination = gDisplayedFramebuffer + 20u;

        if (D3DO_ProcessStartupEvents())
            break;

        D3DO_WaitStartupTime(nextFrameTime);
        now = D3DO_GetStartupTime();
        if (D3DO_ProcessStartupEvents())
            break;
        if (!D3DO_CineDecodeNextFrame(&decoder)) {
            result = 0;
            break;
        }

        memset(gDisplayedFramebuffer, 0,
               (size_t)DOOM3DO_WIDTH * DOOM3DO_HEIGHT * sizeof(uint16_t));
        for (index = 0; index < D3DO_CINE_WIDTH * D3DO_CINE_HEIGHT; ++index)
            destination[index + (index / D3DO_CINE_WIDTH) * (DOOM3DO_WIDTH - D3DO_CINE_WIDTH)] =
                D3DO_CineARGB8888ToARGB1555(decoder.pixels[index]);
        PresentFramebuffer(gDisplayedFramebuffer);

        nextFrameTime += frameStep;
        if (nextFrameTime < now)
            nextFrameTime = now + frameStep;
    }

    while (movieAudioPlaying && decoder.frameNumber >= decoder.frameCount &&
           D3DO_IsStartupMovieAudioPlaying()) {
        if (D3DO_ProcessStartupEvents())
            break;
        D3DO_WaitStartupTime(D3DO_GetStartupTime() + frameStep);
    }

    D3DO_StopStartupMovieAudio();
    D3DO_CineShutdownDecoder(&decoder);
    return result;
}

static void D3DO_RunStartupSequence(void)
{
    D3DO_ShowStartupCel("3do.logo.cel", 0u, 3000u, 1000u);
    D3DO_ShowStartupCel("IDLogo.cel", 1000u, 2000u, 1000u);
    (void)D3DO_PlayStartupMovie("logic.cine");
    (void)D3DO_PlayStartupMovie("AdiLogo.cine");
}

#if SDL_VERSION_ATLEAST(3,4,0)
static int D3DO_CRTShaderPath(const char *filename, char *path, size_t pathSize)
{
    const char *basePath;
    FILE *file;

    if (!filename || !path || pathSize == 0u)
        return 0;

    basePath = SDL_GetBasePath();
    if (!basePath)
        return 0;

    snprintf(path, pathSize, "%s%s", basePath, filename);
    file = fopen(path, "rb");
    if (!file)
        return 0;
    fclose(file);
    return 1;
}

static int D3DO_CRTShaderFilesAvailable(void)
{
    char path[PATH_MAX];

    if (D3DO_CRTShaderPath("doom3do.spv", path, sizeof(path)))
        return 1;
    return D3DO_CRTShaderPath("doom3do.dxil", path, sizeof(path));
}

static int D3DO_LoadCRTFilter(void)
{
    char path[PATH_MAX];
    const char *shaderFilename = NULL;
    const char *driver;
    FILE *file;
    long fileSize;
    Byte *shaderData;
    SDL_GPUShaderFormat formats;
    SDL_GPUShaderFormat shaderFormat = 0;
    SDL_GPUShaderCreateInfo shaderInfo;
    SDL_GPURenderStateCreateInfo stateInfo;
    struct {
        float resolutionX;
        float resolutionY;
        float sourceInvWidth;
        float sourceInvHeight;

        float scanlineStrength;
        float scanlineSharpness;
        float maskStrength;
        float maskBrightness;

        float bloomStrength;
        float bloomRadius;
        float chromaticAberration;
        float gamma;

        float outputBrightness;
        float vignetteStrength;
        float vignetteRadius;
        float padding0;
    } uniforms;

    gGPUDevice = SDL_GetGPURendererDevice(gRenderer);
    if (!gGPUDevice)
        return 0;

    formats = SDL_GetGPUShaderFormats(gGPUDevice);
    driver = SDL_GetGPUDeviceDriver(gGPUDevice);

    if (driver && strcmp(driver, "direct3d12") == 0) {
        if ((formats & SDL_GPU_SHADERFORMAT_DXIL) &&
            D3DO_CRTShaderPath("doom3do.dxil", path, sizeof(path))) {
            shaderFilename = "doom3do.dxil";
            shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
        }
    } else if (driver && strcmp(driver, "vulkan") == 0) {
        if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) &&
            D3DO_CRTShaderPath("doom3do.spv", path, sizeof(path))) {
            shaderFilename = "doom3do.spv";
            shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        }
    }

    if (!shaderFilename) {
        if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) &&
            D3DO_CRTShaderPath("doom3do.spv", path, sizeof(path))) {
            shaderFilename = "doom3do.spv";
            shaderFormat = SDL_GPU_SHADERFORMAT_SPIRV;
        } else if ((formats & SDL_GPU_SHADERFORMAT_DXIL) &&
                   D3DO_CRTShaderPath("doom3do.dxil", path, sizeof(path))) {
            shaderFilename = "doom3do.dxil";
            shaderFormat = SDL_GPU_SHADERFORMAT_DXIL;
        }
    }

    if (!shaderFilename) {
        SDL_Log("DOOM3DO: no compatible CRT shader for GPU backend '%s'",
                driver ? driver : "unknown");
        gGPUDevice = NULL;
        return 0;
    }

    file = fopen(path, "rb");
    if (!file) {
        SDL_Log("DOOM3DO: unable to open CRT shader '%s'", path);
        gGPUDevice = NULL;
        return 0;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        gGPUDevice = NULL;
        return 0;
    }
    fileSize = ftell(file);
    if (fileSize <= 0 ||
        (shaderFormat == SDL_GPU_SHADERFORMAT_SPIRV && (fileSize & 3) != 0) ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        gGPUDevice = NULL;
        return 0;
    }

    shaderData = (Byte *)malloc((size_t)fileSize);
    if (!shaderData) {
        fclose(file);
        gGPUDevice = NULL;
        return 0;
    }
    if (fread(shaderData, 1, (size_t)fileSize, file) != (size_t)fileSize) {
        free(shaderData);
        fclose(file);
        gGPUDevice = NULL;
        return 0;
    }
    fclose(file);

    SDL_zero(shaderInfo);
    shaderInfo.code = shaderData;
    shaderInfo.code_size = (size_t)fileSize;
    shaderInfo.entrypoint = "main";
    shaderInfo.format = shaderFormat;
    shaderInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    shaderInfo.num_samplers = 1;
    shaderInfo.num_uniform_buffers = 1;

    gCRTShader = SDL_CreateGPUShader(gGPUDevice, &shaderInfo);
    free(shaderData);
    if (!gCRTShader) {
        SDL_Log("DOOM3DO: SDL_CreateGPUShader(CRT) failed for %s: %s",
                shaderFilename, SDL_GetError());
        gGPUDevice = NULL;
        return 0;
    }

    SDL_zero(stateInfo);
    stateInfo.fragment_shader = gCRTShader;
    stateInfo.num_sampler_bindings = 0;
    stateInfo.sampler_bindings = NULL;
    stateInfo.num_storage_textures = 0;
    stateInfo.storage_textures = NULL;
    stateInfo.num_storage_buffers = 0;
    stateInfo.storage_buffers = NULL;
    stateInfo.props = 0;

    gCRTRenderState = SDL_CreateGPURenderState(gRenderer, &stateInfo);
    if (!gCRTRenderState) {
        SDL_Log("DOOM3DO: SDL_CreateGPURenderState(CRT) failed: %s", SDL_GetError());
        D3DO_ShutdownCRTFilter();
        return 0;
    }

    uniforms.resolutionX = (float)DOOM3DO_WIDTH;
    uniforms.resolutionY = (float)DOOM3DO_HEIGHT;
    uniforms.sourceInvWidth = 1.0f / (float)DOOM3DO_WIDTH;
    uniforms.sourceInvHeight = 1.0f / (float)DOOM3DO_HEIGHT;

    uniforms.scanlineStrength = 0.34f;
    uniforms.scanlineSharpness = 1.65f;
    uniforms.maskStrength = 0.18f;
    uniforms.maskBrightness = 0.92f;

    uniforms.bloomStrength = 0.11f;
    uniforms.bloomRadius = 1.25f;
    uniforms.chromaticAberration = 0.55f;
    uniforms.gamma = 2.20f;

    uniforms.outputBrightness = 1.04f;
    uniforms.vignetteStrength = 0.10f;
    uniforms.vignetteRadius = 0.82f;
    uniforms.padding0 = 0.0f;

    if (!SDL_SetGPURenderStateFragmentUniforms(gCRTRenderState, 0,
                                                &uniforms, sizeof(uniforms))) {
        SDL_Log("DOOM3DO: SDL_SetGPURenderStateFragmentUniforms(CRT) failed: %s", SDL_GetError());
        D3DO_ShutdownCRTFilter();
        return 0;
    }

    gCRTFilterEnabled = true;
    SDL_Log("DOOM3DO: CRT GPU filter enabled using %s render state",
            shaderFormat == SDL_GPU_SHADERFORMAT_DXIL ? "D3D12 DXIL" : "Vulkan SPIR-V");
    return 1;
}

static void D3DO_ShutdownCRTFilter(void)
{
    if (gCRTRenderState) {
        SDL_DestroyGPURenderState(gCRTRenderState);
        gCRTRenderState = NULL;
    }
    if (gCRTShader && gGPUDevice) {
        SDL_ReleaseGPUShader(gGPUDevice, gCRTShader);
        gCRTShader = NULL;
    }
    gGPUDevice = NULL;
    gCRTFilterEnabled = false;
}
#endif

static int PresentFrame(void)
{
    return PresentFramebuffer(gDisplayedFramebuffer);
}

static int PresentFramebuffer(const uint16_t *buffer)
{
    for (int y = 0; y < DOOM3DO_HEIGHT; ++y)
        for (int x = 0; x < DOOM3DO_WIDTH; ++x)
            gPresentPixels[y * DOOM3DO_WIDTH + x] =
                ARGB1555ToARGB8888(buffer[y * DOOM3DO_WIDTH + x]) | 0xFF000000u;

    if (!SDL_UpdateTexture(gTexture, NULL, gPresentPixels,
                           DOOM3DO_WIDTH * (int)sizeof(uint32_t)))
        return 0;
    if (!SDL_RenderClear(gRenderer))
        return 0;
#if SDL_VERSION_ATLEAST(3,4,0)
    if (gCRTFilterEnabled && gCRTRenderState) {
        if (SDL_SetGPURenderState(gRenderer, gCRTRenderState)) {
            if (SDL_RenderTexture(gRenderer, gTexture, NULL, NULL)) {
                if (!SDL_SetGPURenderState(gRenderer, NULL))
                    return 0;
            } else {
                SDL_Log("DOOM3DO: CRT SDL_RenderTexture failed, falling back: %s", SDL_GetError());
                (void)SDL_SetGPURenderState(gRenderer, NULL);
                gCRTFilterEnabled = false;
            }
        } else {
            SDL_Log("DOOM3DO: CRT SDL_SetGPURenderState failed, falling back: %s", SDL_GetError());
            gCRTFilterEnabled = false;
        }
    }
    if (!gCRTFilterEnabled) {
#endif
        if (!SDL_RenderTexture(gRenderer, gTexture, NULL, NULL))
            return 0;
#if SDL_VERSION_ATLEAST(3,4,0)
    }
#endif
    return SDL_RenderPresent(gRenderer);
}




/*
 * Draw the loading plaque over the displayed framebuffer.
 */
void D3DO_ShowLoadingPlaque(void)
{
    D3DO_RenderPage *page;
    LongWord mark;

    page=D3DO_CurrentPage();
    if(!page || page->state!=D3DO_PAGE_BUILDING)
        page=D3DO_AcquireBuildPage();
    if(page) {
        
        memcpy(page->framebuffer,gDisplayedFramebuffer,
               DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));
        page->buildState=gD3DO_BuildCEL;
        gFramebuffer=page->framebuffer;
        SpanPtr=page->spanArray;
        D3DO_BeginBatch(page);
    }
    DrawPlaque(rLOADING);
    D3DO_SubmitFrame();
    D3DO_WaitForPage(page);
    if(page) {
        if(PresentFramebuffer(page->framebuffer))
            memcpy(gDisplayedFramebuffer,page->framebuffer,
                   DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));
        D3DO_MarkPresentedPage(page);
    }

    page=D3DO_AcquireBuildPage();
    (void)page;
    mark=ReadTick();
    while((ReadTick()-mark)<(TICKSPERSEC/2)) {
    }
}

#define WIPEWIDTH 320		
#define WIPEHEIGHT 200

static void WipeDoom(LongWord *OldScreen,LongWord *NewScreen)
{
	LongWord Mark;	
	Word TimeCount;	
	Word i,x;
	Word Quit;		
	int delta;		
	LongWord *Screenad;		
	LongWord *SourcePtr;		
	int YDeltaTable[WIPEWIDTH/2];	

	delta = -GetRandom(15);	
	YDeltaTable[0] = delta;	
	x = 1;
	do {
		delta += (GetRandom(2)-1);	
		if (delta>0) {		
			delta = 0;
		}
		if (delta == -16) {	
			delta = -15;
		}
		YDeltaTable[x] = delta;	
	} while (++x<(WIPEWIDTH/2));	

	Mark = ReadTick()-2;	
	do {
		do {
			TimeCount = ReadTick()-Mark;	
		} while (TimeCount<(TICKSPERSEC/30));			
		Mark+=TimeCount;		
		TimeCount/=(TICKSPERSEC/30);	


		Quit = TRUE;		
		do {
			x = 0;		
			do {
				delta = YDeltaTable[x];		
				if (delta<WIPEHEIGHT) {	
					Quit = FALSE;		
					if (delta < 0) {
						++delta;					
					} else if (delta < 16) {
						delta = delta<<1;					
						++delta;
					} else {
						delta+=8;					
						if (delta>WIPEHEIGHT) {
							delta=WIPEHEIGHT;
						}
					}
					YDeltaTable[x] = delta;	
				}
			} while (++x<(WIPEWIDTH/2));	
		} while (--TimeCount);		

		x = 0;		
		do {
			Screenad = (LongWord *)&VideoPointer[x*8];	
			i = YDeltaTable[x];		
			if ((int)i<0) {	
				i = 0;		
			}
			i>>=1;		
			if (i) {
				TimeCount = i;
				SourcePtr = &NewScreen[x*2];	
				do {
					Screenad[0] = SourcePtr[0];	
					Screenad[1] = SourcePtr[1];
					Screenad+=WIPEWIDTH;
					SourcePtr+=WIPEWIDTH;
				} while (--TimeCount);
			}
			if (i<(WIPEHEIGHT/2)) {		
				i = (WIPEHEIGHT/2)-i;
				SourcePtr = &OldScreen[x*2];
				do {
					Screenad[0] = SourcePtr[0];	
					Screenad[1] = SourcePtr[1];
					Screenad+=WIPEWIDTH;
					SourcePtr+=WIPEWIDTH;
				} while (--i);
			}
		} while (++x<(WIPEWIDTH/2));
		PresentFramebuffer((uint16_t *)VideoPointer);
	} while (!Quit);		
}

static void D3DO_WipeDoom(void)
{
	VideoPointer = (Byte *)gWipeOutputFramebuffer;
	WipeDoom((LongWord *)gDisplayedFramebuffer,(LongWord *)gWipeNewFramebuffer);
	memcpy(gDisplayedFramebuffer, gWipeNewFramebuffer,
	       (size_t)DOOM3DO_WIDTH * DOOM3DO_HEIGHT * sizeof(uint16_t));
	DoWipe = FALSE;
}

void InitTools(void)
{
    int i;
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, 48000 };
    memset(gSamples, 0, sizeof(gSamples));
    memset(&gMusicData, 0, sizeof(gMusicData));
    memset(gSoundVoices, 0, sizeof(gSoundVoices));
    memset(&gMusicVoice, 0, sizeof(gMusicVoice));

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "DOOM3DO: SDL init failed: %s\n", SDL_GetError());
        exit(1);
    }

    gTickFrequency = SDL_GetPerformanceFrequency();

    gWindow = SDL_CreateWindow("DOOM 3DO", gWindowedWidth, gWindowedHeight, 0);
    if (!gWindow) {
        fprintf(stderr, "DOOM3DO: SDL window creation failed: %s\n", SDL_GetError());
        exit(1);
    }

    {
        int useCRTFilter = 0;
#if SDL_VERSION_ATLEAST(3,4,0)
        useCRTFilter = D3DO_CRTShaderFilesAvailable();
#endif
#if SDL_VERSION_ATLEAST(3,4,0)
        if (useCRTFilter)
            gRenderer = SDL_CreateGPURenderer(NULL, gWindow);
        if (!gRenderer)
#endif
            gRenderer = SDL_CreateRenderer(gWindow, NULL);
        if (!gRenderer) {
            fprintf(stderr, "DOOM3DO: SDL renderer creation failed: %s\n", SDL_GetError());
            exit(1);
        }
#if SDL_VERSION_ATLEAST(3,4,0)
        if (useCRTFilter && !D3DO_LoadCRTFilter()) {
            D3DO_ShutdownCRTFilter();
            SDL_DestroyRenderer(gRenderer);
            gRenderer = SDL_CreateRenderer(gWindow, NULL);
            if (!gRenderer) {
                fprintf(stderr, "DOOM3DO: SDL renderer fallback failed: %s\n", SDL_GetError());
                exit(1);
            }
        }
#endif
    }

    SDL_SetRenderLogicalPresentation(gRenderer, DOOM3DO_WIDTH, DOOM3DO_HEIGHT,
                                     SDL_LOGICAL_PRESENTATION_STRETCH);
    SDL_SetRenderVSync(gRenderer, 1);

    gTexture = SDL_CreateTexture(gRenderer, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 DOOM3DO_WIDTH, DOOM3DO_HEIGHT);
    if (!gTexture) {
        fprintf(stderr, "DOOM3DO: SDL texture creation failed: %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetTextureScaleMode(gTexture, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(gTexture, SDL_BLENDMODE_NONE);
    SDL_SetTextureAlphaMod(gTexture, 255);

    gPresentPixels = (uint32_t *)calloc(DOOM3DO_WIDTH * DOOM3DO_HEIGHT, sizeof(uint32_t));
    gDisplayedFramebuffer = (uint16_t *)calloc(DOOM3DO_WIDTH * DOOM3DO_HEIGHT, sizeof(uint16_t));
    gWipeNewFramebuffer = (uint16_t *)calloc(DOOM3DO_WIDTH * DOOM3DO_HEIGHT, sizeof(uint16_t));
    gWipeOutputFramebuffer = (uint16_t *)calloc(DOOM3DO_WIDTH * DOOM3DO_HEIGHT, sizeof(uint16_t));
    if (!gPresentPixels || !gDisplayedFramebuffer || !gWipeNewFramebuffer || !gWipeOutputFramebuffer) {
        fprintf(stderr, "DOOM3DO: framebuffer allocation failed\n");
        exit(1);
    }

    gAudioMutex = SDL_CreateMutex();
    if (!gAudioMutex) {
        fprintf(stderr, "DOOM3DO: audio mutex creation failed: %s\n", SDL_GetError());
    } else {
        if (WildMidi_Init("@opl3", (uint16_t)D3DO_AUDIO_RATE, 0) != 0) {
            fprintf(stderr, "DOOM3DO: WildMIDI OPL3 init failed: %s\n", WildMidi_GetError());
        } else {
            fprintf(stderr, "DOOM3DO: WildMIDI OPL3 MIDI backend initialized\n");
        }
        gAudioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, AudioCallback, NULL);
        if (!gAudioStream) {
            fprintf(stderr, "DOOM3DO: SDL3 audio stream creation failed: %s\n", SDL_GetError());
            SDL_DestroyMutex(gAudioMutex); gAudioMutex = NULL;
        } else {
            if (!SDL_ResumeAudioStreamDevice(gAudioStream))
                fprintf(stderr, "DOOM3DO: SDL3 audio resume failed: %s\n", SDL_GetError());
            LoadSamplesFromImage();
        }
    }

    D3DO_OpenFirstGamepad();

    D3DO_ResetCEL();
    D3DO_CCBCount = 0;
    if (!D3DO_StartRenderWorkers()) {
        fprintf(stderr, "DOOM3DO: failed to start CLIO/MADAM render workers\n");
        exit(1);
    }
    (void)D3DO_AcquireBuildPage();
    PresentFrame();

    gLastTickCounter = SDL_GetPerformanceCounter();
    gStepDownVBLAccum = 0;
}

static void D3DO_PresentSubmittedPage(D3DO_RenderPage *page)
{
    if(!page)
        return;

    
    D3DO_WaitForPage(page);
    if(gD3DO_Render.shutdown)
        return;

    if(PresentFramebuffer(page->framebuffer))
        memcpy(gDisplayedFramebuffer,page->framebuffer,
               DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));

    D3DO_MarkPresentedPage(page);
}

/*
 * Advance the 3DO timing model and present the completed render page.
 */
void UpdateAndPageFlip(void)
{
    D3DO_RenderPage *submitted;

    submitted=D3DO_CurrentPage();
    D3DO_SubmitFrame();

    if(DoWipe) {
        D3DO_WaitForPage(submitted);
        if(submitted) {
            memcpy(gWipeNewFramebuffer,submitted->framebuffer,
                   DOOM3DO_WIDTH*DOOM3DO_HEIGHT*sizeof(uint16_t));
            D3DO_WipeDoom();
            D3DO_MarkPresentedPage(submitted);
        }
    } else {
        
        D3DO_PresentSubmittedPage(submitted);
    }

    (void)D3DO_AcquireBuildPage();

    
    if (DemoPlayback || DemoRecording) {
        uint32_t demoVBLAccum=0;
        do {
            uint64_t now=SDL_GetPerformanceCounter();
            uint64_t elapsed=now-gLastTickCounter;
            uint64_t ticks=gTickFrequency ?
                (elapsed*(uint64_t)D3DO_SYSTEM_HZ)/gTickFrequency : 0;
            if(ticks==0)
                continue;
            gLastTickCounter=now;
            demoVBLAccum+=(uint32_t)ticks;
        } while(demoVBLAccum<4u);
        LastTics=4;
    } else {
        do {
            UpdateElapsedTicks();
            if(LastTics!=0)
                break;
        } while(1);
    }
}

void RunAProgram(char *ProgramName)
{
    
    (void)ProgramName;
}

void WritePrefsFile(void)
{
    const char *name = PrefsName();
    Word PrefFile[11];
    Word checksum;
    Word i;
    PrefFile[0] = 0x4C57;
    PrefFile[1] = (Word)StartSkill;
    PrefFile[2] = StartMap;
    PrefFile[3] = SfxVolume;
    PrefFile[4] = MusicVolume;
    PrefFile[5] = ControlType;
    PrefFile[6] = MaxLevel;
    PrefFile[7] = ScreenSize;
    PrefFile[8] = LowDetail;
    PrefFile[9] = gFullscreen ? 1 : 0;
    PrefFile[10] = 0;
    checksum = 12345;
    for (i = 0; i < 10; ++i) checksum = (Word)(checksum + PrefFile[i]);
    PrefFile[10] = checksum;
    {
        uint8_t raw[44];
        for (i = 0; i < 11; ++i) WriteBE32(raw + i * 4u, (uint32_t)PrefFile[i]);
        FILE *f = fopen(name, "wb");
        if (f) { fwrite(raw, 1, sizeof(raw), f); fclose(f); }
    }
}

void ReadPrefsFile(void)
{
    uint8_t raw[44];
    FILE *f = fopen(PrefsName(), "rb");
    Word PrefFile[11];
    Word i, checksum = 12345;
    long size;

    gFullscreen = false;
    if (!f) {
        ClearPrefsFile();
        return;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        ClearPrefsFile();
        return;
    }
    size = ftell(f);
    if (size != 40 && size != 44) {
        fclose(f);
        ClearPrefsFile();
        return;
    }
    if (fseek(f, 0, SEEK_SET) != 0 || fread(raw, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        ClearPrefsFile();
        return;
    }
    fclose(f);

    for (i = 0; i < 10; ++i) PrefFile[i] = (Word)ReadBE32(raw + i * 4u);
    if (size == 44) {
        PrefFile[10] = (Word)ReadBE32(raw + 40);
        if (PrefFile[9] > 1) {
            ClearPrefsFile();
            return;
        }
        checksum = 12345;
        for (i = 0; i < 10; ++i) checksum = (Word)(checksum + PrefFile[i]);
        if (checksum != PrefFile[10]) {
            ClearPrefsFile();
            return;
        }
        gFullscreen = PrefFile[9] ? true : false;
    } else {
        for (i = 0; i < 9; ++i) checksum = (Word)(checksum + PrefFile[i]);
        if (checksum != PrefFile[9] || PrefFile[0] != 0x4C57) {
            ClearPrefsFile();
            return;
        }
    }

    if (PrefFile[0] != 0x4C57) {
        ClearPrefsFile();
        return;
    }

    StartSkill = (skill_t)PrefFile[1];
    StartMap = PrefFile[2];
    SfxVolume = PrefFile[3];
    MusicVolume = PrefFile[4];
    ControlType = PrefFile[5];
    MaxLevel = PrefFile[6];
    ScreenSize = PrefFile[7];
    LowDetail = PrefFile[8];

    if (StartSkill >= sk_nightmare + 1 || StartMap >= 27 ||
        SfxVolume >= 16 || MusicVolume >= 16 ||
        ControlType >= 6 || MaxLevel >= 26 ||
        ScreenSize >= 6 || LowDetail >= 2)
        ClearPrefsFile();
}

void ClearPrefsFile(void)
{
    StartSkill = sk_medium;
    StartMap = 1;
    SfxVolume = 15;
    MusicVolume = 15;
    ControlType = 3;
    MaxLevel = 1;
    ScreenSize = 2;
    LowDetail = FALSE;
    WritePrefsFile();
}

int main(int argc, char **argv)
{
    const char *record_demo = NULL;
    const char *play_demo = NULL;
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-record") && i + 1 < argc) {
            record_demo = argv[++i];
        } else if (!strcmp(argv[i], "-playdemo") && i + 1 < argc) {
            play_demo = argv[++i];
        } else {
            fprintf(stderr, "DOOM3DO: unknown option '%s'\n", argv[i]);
            fprintf(stderr, "DOOM3DO: usage: doom3do [-record file] [-playdemo file]\n");
            return 1;
        }
    }

    if (record_demo && play_demo) {
        fprintf(stderr, "DOOM3DO: -record and -playdemo are mutually exclusive\n");
        return 1;
    }
    HostRecordDemoPath = record_demo;
    HostPlayDemoPath = play_demo;

    if (!SDL_SetAppMetadata("DOOM 3DO Modern Port", "1.0", "doom3do.modern"))
        return 1;

    snprintf(gRezFilePath, sizeof(gRezFilePath), "%s", "REZFILE");
    snprintf(gPrezFilePath, sizeof(gPrezFilePath), "%s", "PREZFILE");

    InitTools();

    if (!ResourceMgr_Init(&gResourceMgr, gRezFilePath)) {
        fprintf(stderr, "DOOM3DO: failed to open REZFILE in %s\n", gRezFilePath);
        return 1;
    }
    gResourceMgrReady = 1;

    /* Optional sparse PREZFILE overlay.  It is intentionally loaded only
     * when present; any resource IDs it contains take precedence over the
     * retail REZFILE, while all other IDs fall back to REZFILE. */
    {
        FILE *prezProbe = fopen(gPrezFilePath, "rb");
        if (prezProbe) {
            fclose(prezProbe);
            if (ResourceMgr_Init(&gPrezResourceMgr, gPrezFilePath)) {
                gPrezResourceMgrReady = 1;
                fprintf(stderr, "DOOM3DO: PREZFILE overlay loaded from %s (%zu resources)\n",
                        gPrezFilePath, gPrezResourceMgr.count);
            } else {
                fprintf(stderr, "DOOM3DO: warning: PREZFILE present but could not be opened; using REZFILE only\n");
            }
        }
    }

    ReadPrefsFile();
    if (gFullscreen) {
        gFullscreen = false;
        if (!D3DO_SetFullscreen(true))
            gFullscreen = false;
    }
    if (!record_demo && !play_demo) {
        D3DO_RunStartupSequence();
    }
    D_DoomMain();

    if (gGamepad) {
        SDL_CloseGamepad(gGamepad);
        gGamepad = NULL;
        gGamepadID = 0;
    }

    D3DO_StopRenderWorkers();

    if (gPrezResourceMgrReady) {
        ResourceMgr_Destroy(&gPrezResourceMgr);
        gPrezResourceMgrReady = 0;
    }
    ResourceMgr_Destroy(&gResourceMgr);
    gResourceMgrReady = 0;
    if (gAudioStream) {
        SDL_PauseAudioStreamDevice(gAudioStream);
        SDL_DestroyAudioStream(gAudioStream);
        gAudioStream = NULL;
    }
    StopAllAudio();
    if (gAudioMutex) {
        SDL_LockMutex(gAudioMutex);
        StopMidiMusicLocked();
        SDL_UnlockMutex(gAudioMutex);
    }
    WildMidi_Shutdown();
    if (gAudioMutex) {
        SDL_DestroyMutex(gAudioMutex);
        gAudioMutex = NULL;
    }
    FreeSamples();
    free(gPresentPixels);
    if (gTexture) SDL_DestroyTexture(gTexture);
#if SDL_VERSION_ATLEAST(3,4,0)
    D3DO_ShutdownCRTFilter();
#endif
    if (gRenderer) SDL_DestroyRenderer(gRenderer);
    if (gWindow) SDL_DestroyWindow(gWindow);
    SDL_Quit();
    return 0;
}