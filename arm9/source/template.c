/*---------------------------------------------------------------------------------

	Simple console print demo
	-- dovoto

---------------------------------------------------------------------------------*/

#ifdef ARM9
#include <nds.h>
#include <stdio.h>
#include "keyboard.h"

int ibm_main(int argc, char **argv);

u16		*ds_display_top; 
u16		*ds_display_bottom;
int		ds_display_bottom_height;
u16		*ds_display_menu; 

int ds_bg_sub = 0;
int ds_bg_main = 0;
int ds_bg_text = 0;
byte* ds_bottom_screen = 0;

volatile int ds_timer_ticks = 0;
void mus_play_timer(void);
volatile int ds_intimer = 0;

void ds_140_timer() {
	//int oldIME = enterCriticalSection();
	//if (ds_intimer) {
		//leaveCriticalSection(oldIME);
	//	return;
	//}
	//ds_intimer = 1;
	//leaveCriticalSection(oldIME);
	ds_timer_ticks++;
	mus_play_timer();
	//oldIME = enterCriticalSection();
	//ds_intimer = 0;
	//leaveCriticalSection(oldIME);
	//iprintf(".");
}

#ifdef ARM9
void memcpy32(void *dst, const void *src, uint wdcount) ITCM_CODE;
#endif
volatile int hblank_timer_ticks = 0;
void hblank_timer() {
	hblank_timer_ticks++;
}

//byte hblank_buffer[256*256*2] ALIGN(32);
void Sys_Init()
{
	bool ret;
	int x, y;
	// Turn on everything
	//powerON(POWER_ALL);

	//put 3D on top
	lcdMainOnTop();

    vramSetMainBanks(VRAM_A_TEXTURE, VRAM_B_TEXTURE, VRAM_C_TEXTURE, VRAM_D_TEXTURE);
	vramSetBankE(VRAM_E_MAIN_BG);
	vramSetBankF(VRAM_F_TEX_PALETTE);
	vramSetBankG(VRAM_G_MAIN_BG_0x06010000);
	vramSetBankH(VRAM_H_SUB_BG); 
	vramSetBankI(VRAM_I_SUB_BG_0x06208000); 
	
	videoSetModeSub(MODE_5_2D | DISPLAY_BG2_ACTIVE);


	// Subscreen as a console
	//setup stdout on main 0
	
	//setup 8bit background on 2
	ds_bg_sub = bgInitSub(2,BgType_Bmp8,BgSize_B8_256x256,0,0);
	//bgScroll(ds_bg_sub,0,-100);bgUpdate();
	ds_display_top = BG_MAP_RAM_SUB(0);
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

	//bgSetPriority(0+4,1);
	bgSetPriority(2+4,0);


	videoSetMode(MODE_3_3D | DISPLAY_BG1_ACTIVE | DISPLAY_BG3_ACTIVE);
	consoleInit(0,1, BgType_Text4bpp, BgSize_T_256x256, 31,3, true,true);

	//setup text layer for fps, center print, and Con_Printf temp overlay
	//ds_bg_text = bgInit(2,BgType_Text4bpp,BgSize_T_256x256,4,0);

	//setup top bitmap for console
	ds_bg_main = bgInit(3,BgType_Bmp8,BgSize_B8_256x256,0,0);
	
	ds_display_bottom_height = 192;
	ds_display_bottom = bgGetGfxPtr(ds_bg_main);
	ds_display_menu = ds_display_bottom;
	ds_bottom_screen = (byte*)malloc(256 * 192);
	memset(ds_bottom_screen, 0, 256 * 192);

	bgSetPriority(0, 2);
	bgSetPriority(3, 1);
	bgSetPriority(1, 3);
	//bgSetPriority(2,2);
	bgUpdate();
	
	BG_PALETTE[0x20] = RGB5(31,0,0);

	for(x=0;x<(128*192);x++)
	{
		ds_display_bottom[x] = 0;//0x2020;
	}
	keyboard_init();



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
	glClearColor(0,0,0,1); // BG must be opaque for AA to work
	glClearPolyID(63); // BG must have a unique polygon ID for AA to work
	//glClearDepth(0x7FFF);

	glCutoffDepth(0x7FFF);
	glEnable(GL_BLEND);
	glSetToonTableRange( 0, 15, RGB15(8,8,8) );
	glSetToonTableRange( 16, 31, RGB15(24,24,24) );

	printf("Initialing disk...");
	ret = fatInitDefault();
	//ret = nitroFSInit(NULL);
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
	timerStart( 0, ClockDivider_1024, TIMER_FREQ_1024( 140 ), ds_140_timer );
	//TIMER0_CR = TIMER_ENABLE|TIMER_DIV_1024;
	//TIMER1_CR = TIMER_ENABLE|TIMER_CASCADE;
#else
	TIMER_DATA(0) = 0x10000 - (0x1000000 / 11025) * 2;
	TIMER_CR(0) = TIMER_ENABLE | TIMER_DIV_1;
	TIMER_DATA(1) = 0;
	TIMER_CR(1) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
	TIMER_DATA(2) = 0;
	TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;

#endif
	
#if 0
	if (0) {
		int t1,t2;
		int v1,v2;

		for(x=0;x<(256*256*2);x++) {
			hblank_buffer[x] = (byte)(x & 0x000000ff);
		}
		vramSetBankA(VRAM_A_LCD);
		irqSet(IRQ_HBLANK, hblank_timer);
		irqEnable(IRQ_HBLANK);

		swiWaitForVBlank();
		v1 = REG_VCOUNT;
		t1 = ds_timer_ticks;
		x = hblank_timer_ticks;
		dmaCopyWords(2,(uint32*)hblank_buffer, (uint32*)VRAM_A , (256*256));
		v2 = REG_VCOUNT;
		t2 = ds_timer_ticks;
		y = hblank_timer_ticks;

		printf("dmaCopyWords ticks: %d %d %d\n",y-x,t2-t1,v2 - v1);
		printf("%d %d %d %d %d %d\n",x,y,t1,t2,v1,v2);

		swiWaitForVBlank();
		v1 = REG_VCOUNT;
		t1 = ds_timer_ticks;
		x = hblank_timer_ticks;
		//memcpy32(VRAM_A,hblank_buffer,(256*256*2)/4);
		memcpy32(VRAM_A,hblank_buffer,(256*256)/4);
		v2 = REG_VCOUNT;
		t2 = ds_timer_ticks;
		y = hblank_timer_ticks;
		printf("memcpy32 ticks: %d %d %d\n",y-x,t2-t1,v2-v1);
		printf("%d %d %d %d %d %d\n",x,y,t1,t2,v1,v2);
		
		while(1);
	}
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

	//defaultExceptionHandler();
	//cygprofile_begin();
}

static const char* registerNames[] =
{ "r0","r1","r2","r3","r4","r5","r6","r7",
	"r8 ","r9 ","r10","r11","r12","sp ","lr ","pc " };

extern const char __itcm_start[];

u32 getExceptionAddress(u32 opcodeAddress, u32 thumbState);

//---------------------------------------------------------------------------------
void exceptionDump() {
	//---------------------------------------------------------------------------------
	/*consoleDemoInit();

	BG_PALETTE_SUB[0] = RGB15(31, 0, 0);
	BG_PALETTE_SUB[255] = RGB15(31, 31, 31);*/
	
	bgSetPriority(1, 0);
	bgUpdate();

	iprintf("\x1b[5CGuru Meditation Error!\n");
	u32	currentMode = getCPSR() & 0x1f;
	u32 thumbState = ((*(u32*)0x02FFFD90) & 0x20);

	u32 codeAddress, exceptionAddress = 0;

	int offset = 8;

	if (currentMode == 0x17) {
		iprintf("\x1b[10Cdata abort!\n\n");
		codeAddress = exceptionRegisters[15] - offset;
		if ((codeAddress > 0x02000000 && codeAddress < 0x02400000) ||
			(codeAddress > (u32)__itcm_start && codeAddress < (u32)(__itcm_start + 32768)))
			exceptionAddress = getExceptionAddress(codeAddress, thumbState);
		else
			exceptionAddress = codeAddress;

	}
	else {
		if (thumbState)
			offset = 2;
		else
			offset = 4;
		iprintf("\x1b[5Cundefined instruction!\n\n");
		codeAddress = exceptionRegisters[15] - offset;
		exceptionAddress = codeAddress;
	}

	iprintf("  pc: %08lX addr: %08lX\n\n", codeAddress, exceptionAddress);

	int i;
	for (i = 0; i < 8; i++) {
		iprintf("  %s: %08lX   %s: %08lX\n",
			registerNames[i], exceptionRegisters[i],
			registerNames[i + 8], exceptionRegisters[i + 8]);
	}
	iprintf("\n");
	u32* stack = (u32*)exceptionRegisters[13];
	for (i = 0; i < 10; i++) {
		iprintf("\x1b[%d;2H%08lX:  %08lX %08lX", i + 14, (u32)&stack[i * 2], stack[i * 2], stack[(i * 2) + 1]);
	}
}

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
	touchPosition touch;
	int ret;

	Sys_Init();

	setExceptionHandler(exceptionDump);

	//enable timers for keeping track of a normal time value every frame.
	//TIMER0_CR = TIMER_ENABLE|TIMER_DIV_1024;
	//TIMER1_CR = TIMER_ENABLE|TIMER_CASCADE;

	ret = ibm_main(argc,argv);

	return 0;
}

#endif