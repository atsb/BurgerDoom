#pragma once

#include <cstdint>

// All the songs in the game
enum musicnum_e : uint8_t { 
    Song_None,
    Song_intro,
    Song_final,
    Song_bunny,
    Song_intermission,
    Song_e1m1,
    NUMMUSIC = Song_e1m1 + 24   // Song count
};

// All the sound effects in the game
enum sfxenum_e : uint8_t {
    sfx_None,
    sfx_pistol,
    sfx_shotgn,
    sfx_sgcock,
    sfx_plasma,
    sfx_bfg,
    sfx_sawup,
    sfx_sawidl,
    sfx_sawful,
    sfx_sawhit,
    sfx_rlaunc,
    sfx_rfly,
    sfx_rxplod,
    sfx_firsht,
    sfx_firbal,
    sfx_firxpl,
    sfx_pstart,
    sfx_pstop,
    sfx_doropn,
    sfx_dorcls,
    sfx_stnmov,
    sfx_swtchn,
    sfx_swtchx,
    sfx_plpain,
    sfx_dmpain,
    sfx_popain,
    sfx_slop,
    sfx_itemup,
    sfx_wpnup,
    sfx_oof,
    sfx_telept,
    sfx_posit1,
    sfx_posit2,
    sfx_posit3,
    sfx_bgsit1,
    sfx_bgsit2,
    sfx_sgtsit,
    sfx_cacsit,
    sfx_brssit,
    sfx_cybsit,
    sfx_spisit,
    sfx_sklatk,
    sfx_sgtatk,
    sfx_claw,
    sfx_pldeth,
    sfx_podth1,
    sfx_podth2,
    sfx_podth3,
    sfx_bgdth1,
    sfx_bgdth2,
    sfx_sgtdth,
    sfx_cacdth,
    sfx_skldth,
    sfx_brsdth,
    sfx_cybdth,
    sfx_spidth,
    sfx_posact,
    sfx_bgact,
    sfx_dmact,
    sfx_noway,
    sfx_barexp,
    sfx_punch,
    sfx_hoof,
    sfx_metal,
    sfx_itmbk,
    NUMSFX      // SFX count
};
