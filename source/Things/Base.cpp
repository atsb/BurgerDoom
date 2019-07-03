#include "Base.h"

#include "Enemy.h"
#include "Game/Data.h"
#include "Game/Tick.h"
#include "Info.h"
#include "Map/MapData.h"
#include "Map/MapUtil.h"
#include "Map/Sight.h"
#include "MapObj.h"

static mobj_t*          gCheckThingMo;          // Used for PB_CheckThing 
static Fixed            gTestX;
static Fixed            gTestY;
static Fixed            gTestFloorZ;
static Fixed            gTestCeilingZ;
static Fixed            gTestDropoffZ;
static subsector_t*     gTestSubSec;
static line_t*          gCeilingLine;
static mobj_t*          gHitThing;
static Fixed            gTestBBox[4];           // Bounding box for tests 
static int              gTestFlags;

/**********************************

    Float up or down at a set speed, used by flying monsters
    
**********************************/

static void FloatChange(mobj_t *mo)
{
    mobj_t *Target;
    Fixed dist, delta;
    
    Target = mo->target;        // Get the target object  
    delta = (Target->z + (mo->height>>1)) - mo->z;  // Get the height differance 

    dist = GetApproxDistance(Target->x - mo->x,Target->y - mo->y);  // Distance to target 
    delta = delta*3;        // Mul by 3 for a fudge factor 
    
    if (delta<0) {      // Delta is signed... 
        if (dist < (-delta) ) {     // Negate 
            mo->z -= FLOATSPEED;    // Adjust the height 
        }
        return;
    }    
    if (dist < delta) {     // Normal compare 
        mo->z += FLOATSPEED;    // Adjust the height 
    }
}


/**********************************

    Move a critter in the Z axis
    
**********************************/
static uint32_t P_ZMovement(mobj_t *mo)
{
    // DC: Note: added a division by 2 here to account for the move from a 35Hz timebase (PC) to a 60Hz timebase (3DO).
    // This fixes issues in the 3DO version with certain projectiles like imps fireballs moving too quickly.

    mo->z += fixedDiv(mo->momz, 2 * FRACUNIT);      // Apply basic z motion

    if ( (mo->flags & MF_FLOAT) && mo->target) {    // float down towards target if too close 
        FloatChange(mo);
    }

//
// clip movement
//
    if (mo->z <= mo->floorz) {  // hit the floor
        if (mo->momz < 0) {
            mo->momz = 0;
        }
        mo->z = mo->floorz;
        if (mo->flags & MF_MISSILE) {
            ExplodeMissile(mo);
            return true;
        }
    } else if (! (mo->flags & MF_NOGRAVITY) ) {
        if (!mo->momz) {        // Not falling 
            mo->momz = -GRAVITY*2;  // Fall faster 
        } else {
            mo->momz -= GRAVITY;
        }
    }

    if (mo->z + mo->height > mo->ceilingz) {    // hit the ceiling
        if (mo->momz > 0) {
            mo->momz = 0;
        }
        mo->z = mo->ceilingz - mo->height;
        if (mo->flags & MF_MISSILE) {
            ExplodeMissile(mo);
            return true;
        }
    }
    return false;
}


/*
=================
=
= PB_BoxCrossLine
=
=================
*/

static bool PB_BoxCrossLine(line_t *ld)
{
    Fixed x1, x2;
    Fixed lx, ly;
    Fixed ldx, ldy;
    Fixed dx1,dy1;
    Fixed dx2,dy2;
    bool side1, side2;

    if (gTestBBox[BOXRIGHT] <= ld->bbox[BOXLEFT]
    ||  gTestBBox[BOXLEFT] >= ld->bbox[BOXRIGHT]
    ||  gTestBBox[BOXTOP] <= ld->bbox[BOXBOTTOM]
    ||  gTestBBox[BOXBOTTOM] >= ld->bbox[BOXTOP] )
        return false;

    if (ld->slopetype == ST_POSITIVE) {
        x1 = gTestBBox[BOXLEFT];
        x2 = gTestBBox[BOXRIGHT];
    } else {
        x1 = gTestBBox[BOXRIGHT];
        x2 = gTestBBox[BOXLEFT];
    }

    lx = ld->v1.x;
    ly = ld->v1.y;
    ldx = (ld->v2.x-ld->v1.x)>>16;
    ldy = (ld->v2.y-ld->v1.y)>>16;

    dx1 = (x1 - lx)>>16;
    dy1 = (gTestBBox[BOXTOP] - ly)>>16;
    dx2 = (x2 - lx)>>16;
    dy2 = (gTestBBox[BOXBOTTOM] - ly)>>16;

    side1 = ldy*dx1 < dy1*ldx;
    side2 = ldy*dx2 < dy2*ldx;

    return side1 != side2;
}


/*
==================
=
= PB_CheckLine
=
= Adjusts testfloorz and testceilingz as lines are contacted
==================
*/

static bool PB_CheckLine (line_t *ld)
{
    Fixed opentop, openbottom;
    Fixed lowfloor;
    sector_t    *front, *back;

/*
=
= The moving thing's destination position will cross the given line.
= If this should not be allowed, return FALSE.
*/
    if (!ld->backsector)
        return false;       // one sided line

    if ( !(gTestFlags & MF_MISSILE)
    && (ld->flags & (ML_BLOCKING | ML_BLOCKMONSTERS) ) )
            return false;       // explicitly blocking


    front = ld->frontsector;
    back = ld->backsector;

    if (front->ceilingheight < back->ceilingheight)
        opentop = front->ceilingheight;
    else
        opentop = back->ceilingheight;
    if (front->floorheight > back->floorheight)
    {
        openbottom = front->floorheight;
        lowfloor = back->floorheight;
    }
    else
    {
        openbottom = back->floorheight;
        lowfloor = front->floorheight;
    }

// adjust floor / ceiling heights
    if (opentop < gTestCeilingZ)
    {
        gTestCeilingZ = opentop;
        gCeilingLine = ld;
    }
    if (openbottom > gTestFloorZ)
        gTestFloorZ = openbottom;
    if (lowfloor < gTestDropoffZ)
        gTestDropoffZ = lowfloor;

    return true;
}

static uint32_t PB_CrossCheck(line_t *ld) 
{
    if (PB_BoxCrossLine(ld)) {
        if (!PB_CheckLine(ld)) {
            return false;
        }
    }
    return true;
}

/*
==================
=
= PB_CheckThing
=
==================
*/

static uint32_t PB_CheckThing (mobj_t *thing)
{
    Fixed       blockdist;
    int         delta;
    mobj_t *mo;

    if (!(thing->flags & MF_SOLID )) {
        return true;
    }
    mo = gCheckThingMo;
    blockdist = thing->radius + mo->radius;

    delta = thing->x - gTestX;
    if (delta < 0)
        delta = -delta;
    if (delta >= blockdist)
        return true;        // didn't hit it
    delta = thing->y - gTestY;
    if (delta < 0)
        delta = -delta;
    if (delta >= blockdist)
        return true;        // didn't hit it

    if (thing == mo)
        return true;        // don't clip against self

//
// check for skulls slamming into things
//
    if (gTestFlags & MF_SKULLFLY) {
        gHitThing = thing;
        return false;       // stop moving
    }


//
// missiles can hit other things
//
    if (gTestFlags & MF_MISSILE) {
    // see if it went over / under
        if (mo->z > thing->z + thing->height)
            return true;        // overhead
        if (mo->z+mo->height < thing->z)
            return true;        // underneath
        if (mo->target->InfoPtr == thing->InfoPtr) {    // don't hit same species as originator
            if (thing == mo->target)
                return true;    // don't explode on shooter
            if (thing->InfoPtr != &gMObjInfo[MT_PLAYER])
                return false;   // explode, but do no damage
            // let players missile other players
        }
        if (! (thing->flags & MF_SHOOTABLE) )
            return !(thing->flags & MF_SOLID);      // didn't do any damage

    // damage / explode
        gHitThing = thing;
        return false;           // don't traverse any more
    }

    return !(thing->flags & MF_SOLID);
}

/*
==================
=
= PB_CheckPosition
=
= This is purely informative, nothing is modified (except things picked up)

in:
basething       a mobj_t
testx,testy     a position to be checked (doesn't need relate to the mobj_t->x,y)

out:

testsubsec
floorz
ceilingz
testdropoffz        the lowest point contacted (monsters won't move to a dropoff)
hitthing

==================
*/
static uint32_t PB_CheckPosition(mobj_t *mo) {
    gTestFlags = mo->flags;

    int r = mo->radius;
    gTestBBox[BOXTOP] = gTestY + r;
    gTestBBox[BOXBOTTOM] = gTestY - r;
    gTestBBox[BOXRIGHT] = gTestX + r;
    gTestBBox[BOXLEFT] = gTestX - r;
    
    // The base floor / ceiling is from the subsector that contains the point.
    // Any contacted lines the step closer together will adjust them.
    gTestSubSec = PointInSubsector(gTestX, gTestY);
    gTestFloorZ = gTestDropoffZ = gTestSubSec->sector->floorheight;
    gTestCeilingZ = gTestSubSec->sector->ceilingheight;
    
    ++gValidCount;
    
    gCeilingLine = 0;
    gHitThing = 0;

    // The bounding box is extended by MAXRADIUS because mobj_ts are grouped
    // into mapblocks based on their origin point, and can overlap into adjacent
    // blocks by up to MAXRADIUS units.
    int xl = (gTestBBox[BOXLEFT] - gBlockMapOriginX - MAXRADIUS) >> MAPBLOCKSHIFT;
    int xh = (gTestBBox[BOXRIGHT] - gBlockMapOriginX + MAXRADIUS) >> MAPBLOCKSHIFT;
    int yl = (gTestBBox[BOXBOTTOM] - gBlockMapOriginY - MAXRADIUS) >> MAPBLOCKSHIFT;
    int yh = (gTestBBox[BOXTOP] - gBlockMapOriginY + MAXRADIUS) >> MAPBLOCKSHIFT;
    
    if (xl < 0) {
        xl = 0;
    }
    
    if (yl < 0) {
        yl = 0;
    }
    
    if (xh >= (int32_t) gBlockMapWidth) {
        xh = (int32_t) gBlockMapWidth - 1;
    }
    
    if (yh >= (int32_t) gBlockMapHeight) {
        yh = (int32_t) gBlockMapHeight - 1;
    }
    
    gCheckThingMo = mo;  // Store for PB_CheckThing
    
    for (int bx = xl; bx <= xh; bx++) {
        for (int by = yl; by <= yh; by++) {
            if (!BlockThingsIterator(bx, by, PB_CheckThing))
                return false;
            if (!BlockLinesIterator(bx, by, PB_CrossCheck))
                return false;
        }
    }

    return true;
}

/*
===================
=
= PB_TryMove
=
= Attempt to move to a new position
=
===================
*/

static bool PB_TryMove(int tryx,int tryy,mobj_t *mo)
{
    gTestX = tryx;
    gTestY = tryy;

    if (!PB_CheckPosition(mo))
        return false;       // solid wall or thing

    if (gTestCeilingZ - gTestFloorZ < mo->height)
        return false;           // doesn't fit
    if (gTestCeilingZ - mo->z < mo->height)
        return false;           // mobj must lower itself to fit
    if (gTestFloorZ - mo->z > 24*FRACUNIT )
        return false;           // too big a step up
    if (!(gTestFlags&(MF_DROPOFF|MF_FLOAT))
        && gTestFloorZ - gTestDropoffZ > 24*FRACUNIT )
        return false;           // don't stand over a dropoff

//
// the move is ok, so link the thing into its new position
//

    UnsetThingPosition(mo);
    mo->floorz = gTestFloorZ;
    mo->ceilingz = gTestCeilingZ;
    mo->x = tryx;
    mo->y = tryy;
    SetThingPosition(mo);
    return true;
}

/*
===================
=
= P_XYMovement
=
===================
*/

#define STOPSPEED       0x1000
#define FRICTION        0xd240

static uint32_t P_XYMovement(mobj_t* mo) {
    Fixed xleft, yleft;
    Fixed xuse, yuse;

    // Cut the move into chunks if too large
    xleft = xuse = mo->momx & ~7;
    yleft = yuse = mo->momy & ~7;

    // DC: Note: added a division by 2 here to account for the move from a 35Hz timebase (PC) to a 60Hz timebase (3DO).
    // This fixes issues in the 3DO version with certain projectiles like imps fireballs moving too quickly.
    xleft = xuse = fixedDiv(mo->momx, 2 * FRACUNIT);
    yleft = yuse = fixedDiv(mo->momy, 2 * FRACUNIT);

    while (xuse > MAXMOVE || xuse < -MAXMOVE
    || yuse > MAXMOVE || yuse < -MAXMOVE)
    {
        xuse >>= 1;
        yuse >>= 1;
    }

    while (xleft || yleft) {
        xleft -= xuse;
        yleft -= yuse;
        if (!PB_TryMove(mo->x + xuse, mo->y + yuse,mo)) {
        // blocked move
            if (mo->flags & MF_SKULLFLY) {
                L_SkullBash(mo, gHitThing);
                return true;
            }

            if (mo->flags & MF_MISSILE) {   // explode a missile
                if (gCeilingLine && gCeilingLine->backsector &&
                    gCeilingLine->backsector->CeilingPic==-1) {  // hack to prevent missiles exploding against the sky
                    P_RemoveMobj(mo);
                    return true;
                }
                L_MissileHit(mo, gHitThing);
                return true;
            }

            mo->momx = mo->momy = 0;
            return false;
        }
    }

//
// slow down
//
    if (mo->flags & (MF_MISSILE | MF_SKULLFLY ) )
        return false;       // no friction for missiles ever

    if (mo->z > mo->floorz)
        return false;       // no friction when airborne

    if (mo->flags & MF_CORPSE)
        if (mo->floorz != mo->subsector->sector->floorheight)
            return false;           // don't stop halfway off a step

    if (mo->momx > -STOPSPEED && mo->momx < STOPSPEED
    && mo->momy > -STOPSPEED && mo->momy < STOPSPEED) {
        mo->momx = 0;
        mo->momy = 0;
    } else {
        mo->momx = (mo->momx>>8)*(FRICTION>>8);
        mo->momy = (mo->momy>>8)*(FRICTION>>8);
    }
    return false;
}


/**********************************

    Process all the critter logic
    
**********************************/

static void P_MobjThinker(mobj_t *mobj)
{
    uint32_t Time;
    uint32_t SightFlag;

    if (mobj->momx || mobj->momy) {     // Any horizontal momentum? 
        if (P_XYMovement(mobj)) {   // Move it 
            return;         // Exit if state change 
        }
    }

    if ( (mobj->z != mobj->floorz) || mobj->momz) { // Any z momentum? 
        if (P_ZMovement(mobj)) {        // Move it (Or fall) 
            return;         // Exit if state change 
        }
    }

    Time = gElapsedTime;
    SightFlag = true;
    do {
        if (mobj->tics == -1) {     // Never time out? 
            return;
        }
        if (mobj->tics>Time) {
            mobj->tics-=Time;   // Count down 
            return;
        }
        if (SightFlag && mobj->flags & MF_COUNTKILL) {  // Can it be killed? 
            SightFlag = false;
            mobj->flags &= ~MF_SEETARGET;   // Assume I don't see a target 
            if (mobj->target) { // Do I have a target? 
                if (CheckSight(mobj,mobj->target)) {
                    mobj->flags |= MF_SEETARGET;
                }
            }
        }
        Time-=mobj->tics;
        if (!SetMObjState(mobj,mobj->state->nextstate)) {   // Next object state 
            return;
        }
    } while (Time);
}


/**********************************

    Execute base think logic for the critters every tic
    
**********************************/

void P_RunMobjBase(void)
{
    mobj_t *mo;
    mo = gMObjHead.next;
    if (mo!=&gMObjHead) {
        do {    // End of the list? 
            mobj_t *NextMo;     // In case it's deleted! 
            NextMo = mo->next;
            if (!mo->player) {      // Don't handle players 
                P_MobjThinker(mo);  // Execute the code 
            }
            mo = NextMo;            // Next in the list 
        } while (mo!=&gMObjHead);
    }
}
