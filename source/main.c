/*

Elf/Dol FOrwarder -- loads an elf or dol specified in the code.

 * Copyright (c) 2008 SpaceJump
 * Copyright (c) 2009 WiiPower

Thanks to svpe, the creator of Front SD Loader and TCPLoad for giving me(SpaceJump) permission for using some of his
functions.

This software is provided 'as-is', without any express or implied warranty.
In no event will the authors be held liable for any damages arising from
the use of this software.

*/

#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <string.h>

#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>

#include "elf_abi.h"
#include "processor.h"
#include "dol.h"

static void *xfb = NULL;
static GXRModeObj *rmode = NULL;

static int device = -1;
static char filename[128];
	
u32 load_dol_image (void *dolstart);
s32 valid_elf_image (void *addr);
u32 load_elf_image (void *addr);
extern void __exception_closeall();

void init_video_and_wpad()
{
	// Initialise the video system
	VIDEO_Init();
	
	// Obtain the preferred video mode from the system
	// This will correspond to the settings in the Wii menu
	rmode = VIDEO_GetPreferredMode(NULL);

	// Allocate memory for the display in the uncached region
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	
	// Set up the video registers with the chosen mode
	VIDEO_Configure(rmode);
	
	// Tell the video hardware where our display memory is
	VIDEO_SetNextFramebuffer(xfb);
	
	// Make the display visible
	VIDEO_SetBlack(FALSE);

	// Flush the video register changes to the hardware
	VIDEO_Flush();

	// Wait for Video setup to complete
	VIDEO_WaitVSync();
	if(rmode->viTVMode&VI_NON_INTERLACE) VIDEO_WaitVSync();

	// Set console parameters
    int x = 24, y = 32, w, h;
    w = rmode->fbWidth - (32);
    h = rmode->xfbHeight - (48);

    // Initialize the console - CON_InitEx works after VIDEO_ calls
	CON_InitEx(rmode, x, y, w, h);

	// Clear the garbage around the edges of the console
    VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
	
	// This function initialises the attached controllers
	WPAD_Init();
	WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
}

void release_storage()
{
	if (device == 0)
	{
		fatUnmount("sd");
		__io_wiisd.shutdown();
	} else
	{
		fatUnmount("usb");
		__io_usbstorage.shutdown();
	}
}

void press_button_to_reboot()
{
	s32 pressed = 0;
	while (1) 
	{
		WPAD_ScanPads();
		pressed = WPAD_ButtonsDown(0);
		if (pressed) 
		{
			release_storage();
			SYS_ResetSystem(SYS_RESTART,0,0);
		}
	}
}

void mount_storage()
{
	// Determine from the filepath if sd or usb is used
	if (filename[0] == 115 && filename[1] == 100)
	{
		device = 0;	
	} else
	{
		device = 1;
	}

	if (device == 0)
	{
		// initialize sd storage
		__io_wiisd.startup();
		
		if (!fatMountSimple("sd", &__io_wiisd)) 
		{
			init_video_and_wpad();
			printf("SD storage could not be initialized!\n");
			printf("Press any button to reboot Wii...\n");
			press_button_to_reboot();
		}		
	}
	else
	{
		// initialize usb storage
		__io_usbstorage.startup();
		
		if (!fatMountSimple("usb", &__io_usbstorage)) 
		{
			init_video_and_wpad();
			printf("USB storage could not be initialized!\n");
			printf("Press any button to reboot Wii...\n");
			press_button_to_reboot();
		}
	}
}


//#define USB

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------

	//create a buffer for the elf/dol content
	void* myBuffer;
	
	//read elf/dol from given path:
	FILE* inputFile;

#ifndef USB
	snprintf(filename, 128, "sd:/apps/usbloader_cfg/boot.dol");
#endif
	
#ifdef USB
	snprintf(filename, 128, "usb:/apps/usbloader_cfg/boot.dol");
#endif

	mount_storage();
	inputFile = fopen( filename, "rb");
	
	if(inputFile == NULL) 
	{
		init_video_and_wpad();
		printf("Error: Couldn't open the file: '");
		printf(filename);
		printf("'.\n");
		printf("Press any button to reboot Wii...\n");
		press_button_to_reboot();
	}
	
	int pos = ftell(inputFile);
	fseek(inputFile, 0, SEEK_END);
	int size = ftell(inputFile);
	fseek(inputFile, pos, SEEK_SET); //return to previous position

	myBuffer = malloc(size);
	fread( myBuffer, 1, size, inputFile);

	fclose(inputFile);
	
	release_storage();
	
	//Check if valid elf file:
	s32 res;
	res = valid_elf_image(myBuffer);
    if(res == 1) 
	{
		//elf ok! -> Load entry point of elf file:
		void (*ep)();
		ep = (void(*)())load_elf_image(myBuffer);

		// code from geckoloader
		u32 level;
		__IOS_ShutdownSubsystems ();
		//printf("IOS_ShutdownSubsystems() done\n");
		_CPU_ISR_Disable (level);
		//printf("_CPU_ISR_Disable() done\n");
		__exception_closeall ();
		//printf("__exception_closeall() done. Jumping to ep now...\n");
		
		ep();
		_CPU_ISR_Restore (level);
	} else 
	{
		//Elf not valid, load dol:
		
		//Stuff for arguments
		struct __argv argv;
		bzero(&argv, sizeof(argv));
		argv.argvMagic = ARGV_MAGIC;
		argv.length = strlen(filename) + 2;
		argv.commandLine = malloc(argv.length);
		if (!argv.commandLine)
		{
			init_video_and_wpad();
			printf("Error creating arguments, could not allocate memory for commandLine\n");
			printf("Press any button to reboot Wii...\n");
			press_button_to_reboot();
		}
		strcpy(argv.commandLine, filename);
		argv.commandLine[argv.length - 1] = '\x00';
		argv.argc = 1;
		argv.argv = &argv.commandLine;
		argv.endARGV = argv.argv + 1;

		run_dol(myBuffer, &argv);		
	}
	
	return 0;
}
