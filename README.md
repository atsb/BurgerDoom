# BURGER DOOM

A backport & remaster of 3DO DOOM for Windows, MacOS and Linux.

## R.I.P Becky

I had the privilege in talking to her about many of the technical 3DO'isms and unique aspects about the system, that would, inevitably, lead to a better port here.  The world has lost another great developer and person.

## About This Project

BURGER DOOM is a project derived from the [3DO DOOM source code release by Rebecca Ann Heineman](https://github.com/Olde-Skuul/doom3do) and the Phoenix Doom project. The aim of this project is to allow the original 3DO version of DOOM to be played on modern operating systems.

## What makes this different from PheonixDoom?

Well for one, it is actually playable :)

PheonixDoom had so many issues..  it was a quick and dirty port.  It was not simulating the 3DO internal ticrate, obeying 3DO logic or even bothering to ensure it was a playable experience.

What I've done is:

Wrote a 3DO internal ticrate of 60hz and according to 3DO specs, most games will auto-clamp to a 28/30hz tic inside of the 3DO's tic.  So this means logic is 60/2 and this is why the 3DO Doom logic is inherently 'faster', to make up for the slower tic..  otherwise it'll be too fast since we need to cleanly divide 60.  Clamping at 35 would result in logic mismatch.

The 3DO itself never clamped tics itself, it would run strictly on a 1 tick per frame, thus keeping itself synched every new frame, if we did it like this on PC, it would run at 60fps and so would all game logic.  It is 
one of the few things that cannot be 'exactly' emulated.

Fixed doomguy's face!  the 3DO version uses a 16bit integer calculation for doomguy's facial expressions, not 32bit.  So this was restructured.

Fixed all internal logic to cleanly divide by 3DO tic to game tic, so doors, plats, ceilings, etc..  will all run at the tic they were designed for.

Experimental PWAD loading (CRASHES DOES NOT WORK) but I did get some loading working, however it was fighting against the REZFILE.

You may experience a slightly 'faster' or 'slower' game than traditional DOOM, and that's because we're simulating the 3DO here.  It'll randomly flip between 28/30 tics as that is what most games did..  so the pistol will be a bit slower, but doors will be slightly faster..  gamers would have noticed this too, if the 3DO ran Doom at this framerate.

All very fun and I enjoyed it!

-Gibbon
