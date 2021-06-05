/*---------------------------------------------------------------------------------

	default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
#ifdef ARM7
#include <nds.h>
#ifdef USE_WIFI
#include <dswifi7.h>
#endif
//#include <maxmod7.h>

#ifdef MUSPLAY
#define FIFO_ADLIB FIFO_USER_01

extern void 	PutAdLibBuffer(int);
extern void 	AdlibEmulator();

void mus_init();

//---------------------------------------------------------------------------------
void AdLibHandler(u32 value, void* userdata) {
	//---------------------------------------------------------------------------------
	PutAdLibBuffer(value);
}
#endif

//---------------------------------------------------------------------------------
void VblankHandler(void) {
	//---------------------------------------------------------------------------------
#ifdef USE_WIFI
	Wifi_Update();
#endif
}


//---------------------------------------------------------------------------------
void VcountHandler() {
	//---------------------------------------------------------------------------------
	inputGetAndSend();
}

volatile bool exitflag = false;

//---------------------------------------------------------------------------------
void powerButtonCB() {
	//---------------------------------------------------------------------------------
	exitflag = true;
}

int arm7_frame_count = 0;

//---------------------------------------------------------------------------------
int main() {
	//---------------------------------------------------------------------------------
		// clear sound registers
	dmaFillWords(0, (void*)0x04000400, 0x100);

	REG_SOUNDCNT |= SOUND_ENABLE;
	writePowerManagement(PM_CONTROL_REG, (readPowerManagement(PM_CONTROL_REG) & ~PM_SOUND_MUTE) | PM_SOUND_AMP);
	powerOn(POWER_SOUND);

	readUserSettings();
	ledBlink(0);

	irqInit();
	// Start the RTC tracking IRQ
	initClockIRQ();
	fifoInit();
	touchInit();

	//mmInstall(FIFO_MAXMOD);

	SetYtrigger(80);

#ifdef USE_WIFI
	installWifiFIFO();
#endif
	installSoundFIFO();

	installSystemFIFO();

#ifdef MUSPLAY
	fifoSetValue32Handler(FIFO_ADLIB, AdLibHandler, 0);
#endif

	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);

#ifdef USE_WIFI
	irqEnable(IRQ_VBLANK | IRQ_VCOUNT | IRQ_NETWORK);
#else
	irqEnable(IRQ_VBLANK | IRQ_VCOUNT);
#endif

	setPowerButtonCB(powerButtonCB);

#ifdef MUSPLAY
	AdlibEmulator();		// We never return from here
#endif

	mus_init();

	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if (0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R))) {
			exitflag = true;
		}
		void mus_frame();
		mus_frame();
		arm7_frame_count++;
		//swiWaitForVBlank();
	}
	return 0;
}
#endif

