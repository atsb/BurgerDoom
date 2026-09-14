# Burger Doom

## What is it?
Burger Doom is (now) an SDL3 direct modern source port, straight from the original source code of the 3DO Version.

This means a few things.

1. Demo Compatibility - yes, never before seen demos have perfect playback, no desyncs.
2. Absolute Faithfulness - Think 'Chocolate Doom for 3DO DOOM'.  Everything preserved, EXACTLY as it was.
3. Data is included.  Rebecca released the full package back in 2014.  I consider that a data release as well, so the REZFILE and all accompanying files are included.

I have added a few things:
1. PREZFILE support (for user created content)
2. MIDI support (WildMidi)
3. Mouse and Gamepad (SDL3) support

## Why is the screen so small?
Because 3DO Doom had a smaller window by default.  And no, the M2 cheat code isn't possible without a gamepad (faithfulness).

## Why no hi-res?
Because 3DO Doom has a very different rendering system.  VERY different.  In this port, I have simulated 'as much' as possible, the CCB, CEL, PLUT, CLIO and Madam (both worker threads) and the entire rendering pipeline to be 'as faithful' as humanly possible.  This was needed for demo compatibility.  The CCBs can only hand off a certain amount of data to the CEL, higher resolutions mean more data.  This would crash the game, the only way around this would be to rip out the renderer and slap SDL3 on it without caring for faithfulness.  NO thanks.  You'll see the framerate go down a bit in complex maps too (thanks to my simulation).

## What are the controls?
Keyboard:
E = Use
Space = Shoot
Shift = Run
Tab = Automap
Restart after Death = Also Tab (faithful)
Mouse support is also available (but disabled for demo recording, only keyboard or only gamepad).

Gamepads use the typical layout as best as possible.  The game is constrained by its limited controls due to the 3DO originally having a very simple gamepad.  Adding more controls would damage faithfulness and demo compatibility.

## Can I record demos?
Yes you can :) and I've done it plenty of times during testing.  3DO DOOM demos are now a reality.  But demo recording is only for Gamepads or Keyboards as the mouse code doesn't go through the 'Pad' controls, which would cause desyncs.  But just like Vanilla 3DO Doom, demo recording is capped at 15 frames per second.

## Just how faithful is it?
As faithful as Chocolate Doom is for PC Doom.

## What differences are there?
Projectiles are faster, enemy RNG is slightly 'dumber' because it was designed for a gamepad without strafing.  Limits are 'vastly' more reduced than Vanilla Doom (by roughly half).

## Are shaders included?
Yes!  Due to the low resolution, I implemented SDL 3.4 SPIR-V shader support and have one included (a crt filter).

## Why Burger Doom?
Because 'Burger Becky' was the original programmer of the 3DO version.  It uses her BurgerLib2, and is a way of crediting her.

