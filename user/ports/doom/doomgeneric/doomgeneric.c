#include <stdio.h>
#include <stdlib.h>

#include "m_argv.h"

#include "doomgeneric.h"

pixel_t* DG_ScreenBuffer = NULL;

void M_FindResponseFile(void);
void D_DoomMain (void);


void doomgeneric_Create(int argc, char **argv)
{
	// save arguments
    myargc = argc;
    myargv = argv;

	M_FindResponseFile();

	DG_ScreenBuffer = malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * 4);
	if (DG_ScreenBuffer == NULL)
	{
		fprintf(stderr, "doomgeneric: failed to allocate screen buffer\n");
		exit(1);
	}

	DG_Init();

	D_DoomMain ();
}
