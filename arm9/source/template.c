/*---------------------------------------------------------------------------------

	Simple console print demo
	-- dovoto

---------------------------------------------------------------------------------*/

#ifdef ARM9
#include <nds.h>
#include <stdio.h>

int ibm_main(int argc, char **argv);

u16		*ds_display_top; 
u16		*ds_display_bottom;
int		ds_display_bottom_height;
u16		*ds_display_menu; 

int ds_bg_sub = 0;
int ds_bg_main = 0;
int ds_bg_text = 0;

void Sys_Init()
{
	bool ret;
	int x, y;
	// Turn on everything
	//powerON(POWER_ALL);

	//put 3D on top
	lcdMainOnTop();
	
	videoSetModeSub(MODE_5_2D | DISPLAY_BG0_ACTIVE | DISPLAY_BG2_ACTIVE);

    vramSetMainBanks(VRAM_A_TEXTURE, VRAM_B_TEXTURE, VRAM_C_TEXTURE, VRAM_D_TEXTURE);
	vramSetBankE(VRAM_E_MAIN_BG);
	vramSetBankF(VRAM_F_TEX_PALETTE);
	vramSetBankG(VRAM_G_MAIN_BG_0x06010000);
	vramSetBankH(VRAM_H_SUB_BG); 
	vramSetBankI(VRAM_I_SUB_BG_0x06208000); 

	// Subscreen as a console
	//setup stdout on sub 0
	consoleInit(0,0, BgType_Text4bpp, BgSize_T_256x256, 4,0, false,true);
	
	//setup 8bit background on 2
	ds_bg_sub = bgInitSub(2,BgType_Bmp8,BgSize_B8_256x256,5,0);
	bgScroll(ds_bg_sub,0,-100);bgUpdate();
	ds_display_top = BG_MAP_RAM_SUB(5);
	/*for (y=0; y < 192; y++)
	{
		for (x=0; x < 128; x++)
		{
			u16 two_pixels = y | (y << 8);
			
			ds_display_top[(y * 128) + x] = two_pixels;
		}
	}*/
	/*for(x=0;x<(128*192);x++)
	{
		ds_display_top[x] = 0;
	}*/

	bgSetPriority(0+4,0);
	bgSetPriority(2+4,1);


	videoSetMode(MODE_3_3D | DISPLAY_BG0_ACTIVE | DISPLAY_BG2_ACTIVE | DISPLAY_BG3_ACTIVE);

	//setup text layer for fps, center print, and Con_Printf temp overlay
	ds_bg_text = bgInit(2,BgType_Text4bpp,BgSize_T_256x256,4,0);

	//setup top bitmap for console
	ds_bg_main = bgInit(3,BgType_Bmp8,BgSize_B8_256x256,0/*5*/,0);
	
	ds_display_bottom_height = 192;
	ds_display_bottom = bgGetGfxPtr(ds_bg_main);
	ds_display_menu = ds_display_bottom;

	bgSetPriority(3,0);
	bgSetPriority(0,1);
	bgSetPriority(2,2);
	
	BG_PALETTE[0x20] = RGB5(31,0,0);

	for(x=0;x<(128*192);x++)
	{
		ds_display_bottom[x] = 0;//0x2020;
	}
	


	//irqSet(IRQ_VBLANK, VID_loadPal);

#ifdef USE_WIFI
Wifi_InitDefault(true);
#endif

	//lcdMainOnBottom();
	
	glInit();
	//glDisable(GL_TEXTURE_2D);
	glEnable(GL_TEXTURE_2D);

#if 0
	glEnable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(1);
#endif

	// Specify the Clear Color and Depth 
	// setup the rear plane
	glClearColor(0,0,0,30); // BG must be opaque for AA to work
	glClearPolyID(63); // BG must have a unique polygon ID for AA to work
	//glClearDepth(0x7FFF);

	glCutoffDepth(0x7FFF);
	glEnable(GL_BLEND);
	glSetToonTableRange( 0, 15, RGB15(8,8,8) );
	glSetToonTableRange( 16, 31, RGB15(24,24,24) );

	printf("Initialing disk...");
	ret = fatInitDefault();
	printf("%s\n",ret ? "done\n" : "failed!\n");
	if(ret == 0)
	{
		do {
			swiWaitForVBlank();
		}while(1);
	}
	
	glColor3f(1,1,1);

	glViewport(0,0,255,191);

	
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glScalef(16.0f,16.0f,16.0f);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluPerspective(73.74, 256.0 / 192.0, 0.005, 40.0);
	
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();


	//setup timer for Sys_FloatTime;
#if 1
	TIMER0_CR = TIMER_ENABLE|TIMER_DIV_1024;
	TIMER1_CR = TIMER_ENABLE|TIMER_CASCADE;
#else
	TIMER_DATA(0) = 0x10000 - (0x1000000 / 11025) * 2;
	TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1;
	TIMER_DATA(1) = 0;
	TIMER_CR(1) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
	TIMER_DATA(2) = 0;
	TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;

#endif
	
	glEnable(GL_FOG);
	glFogShift(10);
	glFogColor(0,0,0,31);
	glFogDensity(0,0);
	for(x=1;x<16;x++)
		glFogDensity(x,x*8);
	for(;x<32;x++)
		glFogDensity(x,127);
	glFogOffset(0);

	soundEnable();

	defaultExceptionHandler();
	//cygprofile_begin();
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
	touchPosition touch;
	int ret;

	Sys_Init();

	//consoleDemoInit();  //setup the sub screen for printing

	iprintf("\n\n\tHello DS dev'rs\n");
	iprintf("\twww.drunkencoders.com\n");
	iprintf("\twww.devkitpro.org");

	//enable timers for keeping track of a normal time value every frame.
	TIMER0_CR = TIMER_ENABLE|TIMER_DIV_1024;
	TIMER1_CR = TIMER_ENABLE|TIMER_CASCADE;

	ret = ibm_main(argc,argv);

	printf("done: %d\n",ret);
	while(1) {

		//touchRead(&touch);
		//iprintf("\x1b[10;0HTouch x = %04i, %04i\n", touch.rawx, touch.px);
		//iprintf("Touch y = %04i, %04i\n", touch.rawy, touch.py);

		swiWaitForVBlank();
	}

	return 0;
}

#endif