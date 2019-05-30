#include "burger.h"
#include <time.h>
#include <string.h>

// DC: 3DO specific headers - remove
#if 0
    #include <kernel.h>
    #include <task.h>
    #include <io.h>
#endif

/**********************************

    Read the current timer value

**********************************/

static volatile LongWord TickValue;
static bool TimerInited;

/******************************

    This is my IRQ task executed 60 times a second
    This is spawned as a background thread and modifies a
    global timer variable.

******************************/

// DC: FIXME: reimplement/replace
#if 0
static void Timer60Hz(void)
{
    Item devItem;       /* Item referance for a timer device */
    Item IOReqItem;     /* IO Request item */
    IOInfo ioInfo;      /* IO Info struct */
    struct timeval tv;  /* Timer time struct */

    devItem = OpenNamedDevice("timer",0);       /* Open the timer */
    IOReqItem = CreateIOReq(0,0,devItem,0);     /* Create a IO request */

    for (;;) {  /* Stay forever */
        tv.tv_sec = 0;
        tv.tv_usec = 16667;                     /* 60 times per second */
        memset(&ioInfo,0,sizeof(ioInfo));       /* Init the struct */
        ioInfo.ioi_Command = TIMERCMD_DELAY;    /* Sleep for some time */
        ioInfo.ioi_Unit = TIMER_UNIT_USEC;
        ioInfo.ioi_Send.iob_Buffer = &tv;       /* Pass the time struct */
        ioInfo.ioi_Send.iob_Len = sizeof(tv);
        DoIO(IOReqItem,&ioInfo);                /* Perform the task sleep */
        ++TickValue;                            /* Inc at 60 hz */
    }
}
#endif

LongWord ReadTick(void)
{
    if (TimerInited) {      /* Was the timer started? */
        return TickValue;
    }
    
    TimerInited = true;     /* Mark as started */

    // DC: FIXME: reimplement/replace
    #if 0
        CreateThread("Timer60Hz",KernelBase->kb_CurrentTask->t.n_Priority+10,Timer60Hz,512);
    #endif
    
    return TickValue;       /* Return the tick value */
}
