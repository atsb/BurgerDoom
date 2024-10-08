#pragma once

#include "Base/Fixed.h"

//------------------------------------------------------------------------------------------------------------------------------------------
// Global defines for Doom
//------------------------------------------------------------------------------------------------------------------------------------------

// Game version string
#define GAME_VERSION_STR "1.0.0"

// These affect the folder that save data and config preferences go into:
static constexpr const char* const SAVE_FILE_PRODUCT = "BurgerDoom";

// View related
static constexpr uint32_t   MAXSCREENHEIGHT = 160;                  // Maximum height allowed
static constexpr uint32_t   MAXSCREENWIDTH  = 280;                  // Maximum width allowed
static constexpr Fixed      VIEWHEIGHT      = 41 * FRACUNIT;        // Height to render from

// Gameplay/simulation related constants
static constexpr uint32_t   TICKSPERSEC     = 60;                           // The game timebase (ticks per second)
static constexpr float      SECS_PER_TICK   = 1.0f / (float) TICKSPERSEC;   // The number of seconds per tick
static constexpr uint32_t   MAPBLOCKSHIFT   = FRACBITS + 7;                 // Shift value to convert Fixed to 128 pixel blocks
static constexpr Fixed      ONFLOORZ        = FRACMIN;                      // Attach object to floor with this z
static constexpr Fixed      ONCEILINGZ      = FRACMAX;                      // Attach object to ceiling with this z
static constexpr Fixed      GRAVITY         = 4 * FRACUNIT;                 // Rate of fall
static constexpr Fixed      MAXMOVE         = 16 * FRACUNIT;                // Maximum velocity
static constexpr Fixed      MAXRADIUS       = 32 << FRACBITS;               // Largest radius of any critter
static constexpr Fixed      MELEERANGE      = 70 << FRACBITS;               // Range of hand to hand combat
static constexpr Fixed      MISSILERANGE    = 32 * 64 << FRACBITS;          // Range of guns targeting
static constexpr Fixed      FLOATSPEED      = 8 * FRACUNIT;                 // Speed an object can float vertically
static constexpr Fixed      SKULLSPEED      = 40 * FRACUNIT;                // Speed of the skull to attack

// Graphics
static constexpr float  MF_SHADOW_ALPHA         = 0.5f;     // Alpha to render things with that have the 'MF_SHADOW' map thing flag applied
static constexpr float  MF_SHADOW_COLOR_MULT    = 0.1f;     // Color multiply to render things with that have the 'MF_SHADOW' map thing flag applied

// Misc
static constexpr uint32_t   SKY_CEILING_PIC = UINT32_MAX;           // Texture number that indicates a sky texture

//------------------------------------------------------------------------------------------------------------------------------------------
// Globa enums
//------------------------------------------------------------------------------------------------------------------------------------------

// Index for a bounding box coordinate
enum {
    BOXTOP,
    BOXBOTTOM,
    BOXLEFT,
    BOXRIGHT,
    BOXCOUNT
};

//------------------------------------------------------------------------------------------------------------------------------------------
// A utility to convert a tick count from PC Doom's original 35Hz timebase to the timebase used by this game version.
// Tries to round so the answer is as close as possible.
//------------------------------------------------------------------------------------------------------------------------------------------
inline constexpr uint32_t convertPcTicks(const uint32_t ticks35Hz) noexcept {
    constexpr uint32_t scaleFactor = (TICKSPERSEC << 1) / 35;
    const uint32_t tickCountFixed = ticks35Hz * scaleFactor;
    return (tickCountFixed + 1) >> 1;
}

//------------------------------------------------------------------------------------------------------------------------------------------
// Convert an uint32 speed defined in the PC 35Hz timebase to the 60Hz timebase used by used by this game version.
// Tries to round so the answer is as close as possible.
//------------------------------------------------------------------------------------------------------------------------------------------
inline constexpr uint32_t convertPcUintSpeed(const uint32_t speed35Hz) noexcept {
    constexpr uint32_t scaleFactor = (35 << 1) / TICKSPERSEC;
    const uint32_t speedFixed = speed35Hz * scaleFactor;
    return (speedFixed + 1) >> 1;
}
