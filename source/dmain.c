#include "Doom.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/**********************************

	Grow a box if needed to encompass a point

**********************************/

void AddToBox(Fixed *box,Fixed x,Fixed y)
{
	if (x<box[BOXLEFT]) {		/* Off the left side? */
		box[BOXLEFT] = x;		/* Increase the left */
	} else if (x>box[BOXRIGHT]) {	/* Off the right side? */
		box[BOXRIGHT] = x;		/* Increase the right */
	}
	if (y<box[BOXBOTTOM]) {		/* Off the top of the box? */
		box[BOXBOTTOM] = y;		/* Move the top */
	} else if (y>box[BOXTOP]) {	/* Off the bottom of the box? */
		box[BOXTOP] = y;
	}
}

/**********************************

	Convert a joypad response into a network joypad record.
	This is to compensate the fact that differant computers
	have differant hot keys for motion control.

**********************************/

static Word LocalToNet(Word cmd)
{
	Word NewCmd;

	NewCmd = 0;
	if (cmd & PadSpeed) {		/* Was speed true? */
		NewCmd = PadA;	/* Save it */
	}
	if (cmd & PadAttack) {		/* Was attack true? */
		NewCmd |= PadB;	/* Save it */
	}
	if (cmd & PadUse) {			/* Was use true? */
		NewCmd |= PadC;	/* Save it */
	}
	return (cmd&~(PadA|PadB|PadC))|NewCmd;			/* Return the network compatible response */
}

/**********************************

	Convert a network response into a local joypad record.
	This is to compensate the fact that differant computers
	have differant hot keys for motion control.

**********************************/

static Word NetToLocal(Word cmd)
{
	Word NewCmd;
	
	NewCmd = 0;
	if (cmd & PadA) {
		NewCmd = PadSpeed;	/* Set the speed bit */
	}
	if (cmd & PadB) {
		NewCmd |= PadAttack;	/* Set the attack bit */
	}
	if (cmd & PadC) {
		NewCmd |= PadUse;		/* Set the use bit */
	}
	return (cmd&~(PadA|PadB|PadC))|NewCmd;		/* Return the localized joypad setting */
}

/**********************************

	Read a 32-bit joypad command Word from the demo data

**********************************/

Word ReadDemoWord(const Word *p)
{
	const Byte *b = (const Byte *)p;
	return (Word)(((uint32_t)b[0] << 24) |
		((uint32_t)b[1] << 16) |
		((uint32_t)b[2] << 8) |
		(uint32_t)b[3]);
}

void WriteDemoWord(Word *p, Word value)
{
	Byte *b = (Byte *)p;
	b[0] = (Byte)(value >> 24);
	b[1] = (Byte)(value >> 16);
	b[2] = (Byte)(value >> 8);
	b[3] = (Byte)value;
}

static Word GetDemoCmd(void)
{
	Word cmd;

	cmd = ReadDemoWord(DemoDataPtr);	/* Demo resources are big-endian 3DO words */
	++DemoDataPtr;
	return NetToLocal(cmd);	/* Convert the network command to local */
}

/**********************************

	Main loop processing for the game system

**********************************/

Word MiniLoop(void(*start)(void),void(*stop)(void),
	Word(*ticker)(void),void(*drawer)(void))
{
	Word exit;
	Word buttons;

/* Setup (cache graphics,etc) */

	DoWipe = TRUE;
	start();			/* Prepare the background task (Load data etc.) */
	exit = 0;			/* I am running */
	gameaction = ga_nothing;	/* Game is not in progress */
	TotalGameTicks = 0;		/* No vbls processed during game */
	ElapsedTime = 0;	/* No time has elapsed yet */

	/* Init the joypad states */
	JoyPadButtons = PrevJoyPadButtons = NewJoyPadButtons = 0;

	do {		/* Run the tic immediately */
		TotalGameTicks += ElapsedTime;		/* Add to the VBL count */
		exit = ticker();			/* Process the keypad commands */

/* adaptive timing based on previous frame */

		if (DemoPlayback || DemoRecording) {
			ElapsedTime = 4;		/* Original demo cadence: 4 x 60 Hz VBLs = 15 Hz */
		} else {
			ElapsedTime = (Word)LastTics;		/* Get the true time count */
			if (ElapsedTime >= 9) {		/* Too slow? */
				ElapsedTime = 8;		/* Make 7.5 fps as my mark */
			}
		}

/* Get buttons for next tic */

		PrevJoyPadButtons = JoyPadButtons;		/* Pass through the latest keypad info */

		buttons = ReadJoyButtons(0);			/* Read the controller */
		JoyPadButtons = buttons;	/* Save it */
		
		if (DemoPlayback) {					/* Playing back a demo? */
			if (buttons & (PadA|PadB|PadC|PadD) ) {		/* Abort? */
				exit = ga_exitdemo;			/* Exit the demo */
				break;
			}
			/* Get a joypad from demo data */
			JoyPadButtons = buttons = GetDemoCmd();
		}
		NewJoyPadButtons = (buttons^PrevJoyPadButtons)&buttons;	/* Get the joypad downs... */

		if (DemoRecording) {		/* Am I recording a demo? */
			if (DemoDataPtr < DemoBuffer + (0x8000u / sizeof(Word))) {
				/* Demo files use serialized 3DO big-endian words. */
				WriteDemoWord(DemoDataPtr, LocalToNet(buttons));
				++DemoDataPtr;
			} else {
				/* Prevent the fixed 32 KiB original buffer from overflowing. */
				fprintf(stderr, "DOOM3DO: demo buffer full; stopping recording\n");
				exit = ga_completed;
			}
		}

		/* Am I recording a demo? */
		if ((DemoRecording || DemoPlayback) && (buttons & PadStart) ) {
			exit = ga_completed;		/* End the game right now! */
		}

		if (gameaction == ga_warped) {		/* Did I end the level? */
			exit = ga_warped;	/* Exit after drawing */
			break;			/* Exit */
		}

/* sync up with the refresh */

		drawer();			/* Draw the screen */
	} while (!exit);		/* Is the loop finished? */
	stop();					/* Release resources */
	S_Clear();				/* Kill sounds */
	players.mo = 0;	/* For net consistancy checks */
	return exit;			/* Return the abort code from ticker */
}

/**********************************

	If key's A,B or C was pressed or 8 seconds of demo was shown
	then abort the demo.

**********************************/

static Word TIC_Abortable (void)
{
	if (TotalGameTicks >= (8*TICKSPERSEC)) {	/* Time up? */
		return ga_died;		/* Go on to next demo */
	}
	if (NewJoyPadButtons&(PadA|PadB|PadC|PadD)) {	/* Pressed A B or C? */
		return ga_exitdemo;			/* Exit the demo right now! */
	}
	return ga_nothing;			/* Continue the demo */
}

/**********************************

	Load the title picture into memory

**********************************/

static Boolean OnlyOnce;

static void START_Title(void)
{
	if (!OnlyOnce) {
		OnlyOnce = TRUE;
		DoWipe = FALSE;		/* On power up, don't wipe the screen */
	}
	S_StartSong(Song_intro,TRUE);		/* Intro music, no looping */
}

/**********************************

	Release the memory for the title picture

**********************************/

static void STOP_Title(void)
{
//	S_StopSong();			/* Stop the music */
}

/**********************************

	Draw the title page

**********************************/

static void DRAW_Title(void)
{
	DrawRezShape(0,0,rTITLE);		/* Draw the doom logo */
	UpdateAndPageFlip();					/* Page flip */
}

/**********************************

	Load resources for the credits page

**********************************/

static Word CreditRezNum;

static void START_Credits(void)
{
	CreditRezNum = rIDCREDITS;
}

/**********************************

	Release memory for credits

**********************************/

static void STOP_Credits(void)
{
}

/**********************************

	Ticker code for the credits page

**********************************/

static Word TIC_Credits(void)
{
	if (TotalGameTicks >= (30*TICKSPERSEC)) {	/* Time up? */
		return ga_died;		/* Go on to next demo */
	}
	if (NewJoyPadButtons&(PadA|PadB|PadC|PadD)) {	/* Pressed A,B or C? */
		return ga_exitdemo;		/* Abort the demo */
	}
	return ga_nothing;		/* Don't stop! */
}

/**********************************

	Draw the credits page

**********************************/

static void DRAW_Credits(void)
{
	switch (CreditRezNum) {
	case rIDCREDITS:
		if (TotalGameTicks>=(10*TICKSPERSEC)) {
			CreditRezNum = rCREDITS;
			DoWipe = TRUE;
		}
		break;
	case rCREDITS:
		if (TotalGameTicks>=(20*TICKSPERSEC)) {
			CreditRezNum = rLOGCREDITS;
			DoWipe = TRUE;
		}
	}
	DrawRezShape(0,0,CreditRezNum);	/* Draw the credits */
	UpdateAndPageFlip();				/* Page flip */
}

/**********************************

	Execute the main menu

**********************************/

static void RunMenu(void)
{
	if (MiniLoop(M_Start,M_Stop,M_Ticker,M_Drawer)==ga_completed) {		/* Process the menu */
		S_StopSong();
		if (HostRecordDemoPath) {
			if (G_RecordDemoToFile(HostRecordDemoPath) != 0) {
				fprintf(stderr, "DOOM3DO: demo recording failed\n");
			}
		} else {
			G_InitNew(StartSkill,StartMap);		/* Init the new game */
			G_RunGame();					/* Play the game */
		}
	}
}


/**********************************

	Run the title page

**********************************/

static void RunTitle(void)
{
	if (MiniLoop(START_Title,STOP_Title,TIC_Abortable,DRAW_Title)==ga_exitdemo) {
		RunMenu();			/* Process the main menu */
	}
}

/**********************************

	Show the game credits

**********************************/

static void RunCredits (void)
{
	if (MiniLoop(START_Credits,STOP_Credits,TIC_Credits,DRAW_Credits)== ga_exitdemo) {	/* Did you quit? */
		RunMenu();		/* Process the main menu */
	}
}

/**********************************

	Run the game demo

**********************************/

static const char *ExternalDemoName(Word demoname)
{
	if (demoname == rDEMO1) {
		return "rDEMO1";
	}
	if (demoname == rDEMO2) {
		return "rDEMO2";
	}
	return NULL;
}

static int GetExternalDemoPath(Word demoname, char *path, size_t pathSize)
{
	const char *name = ExternalDemoName(demoname);
	char *base;
	size_t baseLen;

	if (!name || !path || pathSize == 0) {
		return 0;
	}

	base = SDL_GetBasePath();
	if (!base) {
		return 0;
	}

	baseLen = strlen(base);
	if (baseLen + strlen(name) + 1 > pathSize) {
		SDL_free(base);
		return 0;
	}

	memcpy(path, base, baseLen);
	memcpy(path + baseLen, name, strlen(name) + 1);
	SDL_free(base);
	return 1;
}

static int ExternalDemoExists(const char *path)
{
	FILE *fp;

	if (!path) {
		return 0;
	}
	fp = fopen(path, "rb");
	if (!fp) {
		return 0;
	}
	fclose(fp);
	return 1;
}

static void RunDemo(Word demoname)
{
	Word *demo;
	Word exit;
	LongWord demoSize;

	/*
	 * The original 3DO startup path plays the demo directly from the
	 * corresponding REZ resource.  Do not silently substitute a host file
	 * with the same name: the resource itself is the compatibility baseline.
	 */
	demo = (Word *)LoadAResource(demoname);	/* Load the demo */
	demoSize = GetAResourceSize(demoname);
	if (!demo || demoSize < (2u * sizeof(Word)) ||
		(demoSize & (sizeof(Word) - 1u)) != 0u) {
		fprintf(stderr, "DOOM3DO: invalid demo resource %u (size=%lu)\n",
			(unsigned)demoname, (unsigned long)demoSize);
		if (demo) {
			ReleaseAResource(demoname);
		}
		return;
	}

	exit = G_PlayDemoPtr(demo);		/* Play the demo; header supplies skill/map */
	ReleaseAResource(demoname);			/* Release the demo data */
	if (exit == ga_exitdemo) {		/* Quit? */
		RunMenu();
	}
}

/**********************************

	Main entry point for DOOM!!!!

**********************************/

void D_DoomMain(void)
{
	BigNumFont = LoadAResource(rBIGNUMB);
	/* Cache the large numeric font (Needed always) */
	R_Init();			/* Init refresh system */
	P_Init();			/* Init main code */
	O_Init();			/* Init controls */
	if (HostRecordDemoPath) {
		RunTitle();
		return;
	}
	if (HostPlayDemoPath) {
		if (G_PlayDemoFile(HostPlayDemoPath) != 0) {
			fprintf(stderr, "DOOM3DO: demo playback failed\n");
		}
		return;
	}

	for (;;) {
		RunTitle();			/* Show the title page */
		RunDemo(rDEMO1);	/* Run the first demo */
		RunCredits();		/* Show the credits page */
		RunDemo(rDEMO2);	/* Run the second demo */
	}
}
