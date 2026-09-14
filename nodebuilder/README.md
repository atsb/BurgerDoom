# Burger Doom Nodebuilder

## A what?
A nodebuilder..  a tool that traverses the map and creates a playable level (in simple terms).

## Don't we already have Nodebuilders?
We do indeed.

## Why did you create this one?
Because 3DO DOOM 'DID NOT' have a publicly released nodebuilder..  ever (until now)

## How did you get it?
I didn't.  I created it :) via reverse engineering of the retail maps in the REZFILE, the formats of the wads, static binary analysis and reimplementing what was required.  Essentially, I got the original ID Software BSP tool, rewrote it from Objective-C to C (because IDBSP actually created slightly incorrect BSP traversals compared to the 3DO Maps) and then reverse engineered from the source code, map analysis and comparisons between the maps I built and the retail maps, until I got it right.  The nodebuilder now creates BYTE for BYTE identical maps compared to 3DO DOOM's retail maps.

Yes, another world first.  I really do rack them up.

## So this means we can have maps on the 3DO?
If someone wanted to go the 'original hardware route'.  Yes.  For me, I simply needed one to make a map, so I built it.

## How to build this?
gcc *.c -o nodebuilder3do -lm

It is fully multiplatform.

