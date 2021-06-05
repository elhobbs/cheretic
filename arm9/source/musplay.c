#include <nds/fifocommon.h>
#include "musfifo.h"
#include "DoomDef.h"

void mus_play_timer(void) {

}

void mus_update_volume() {

}

static byte* last_mus_lump = 0;

void mus_play_music(char* name) {
	musMessage msg;
	byte* lump = W_CacheLumpName(name, PU_MUSIC);
	if (!lump) {
		iprintf("mus_play_music: failed\n");
		return;
	}
	DC_InvalidateAll();

	if (last_mus_lump != 0 && last_mus_lump != lump) {
		Z_ChangeTag(last_mus_lump, PU_CACHE);
	}
	last_mus_lump = lump;

	//printf("mus: %s\n",name);

	mus_header_t* header = (mus_header_t*)lump;


	if (header->ID[0] != 'M' ||
		header->ID[1] != 'U' ||
		header->ID[2] != 'S' ||
		header->ID[3] != 0x1A)
	{
		iprintf("mus_play_music: not MUS\n");
		return;
	}

	msg.type = musMessageType_play_song;
	msg.data = lump + header->scoreStart;

	fifoSendDatamsg(FIFO_MUS, sizeof(msg), (u8*)&msg);
	while (!fifoCheckValue32(FIFO_MUS));

	int result = (int)fifoGetValue32(FIFO_MUS);
	if (result) {
		iprintf("mus_play_music: arm7 no likey\n");
	}
	else {
		iprintf("mus_play_music: arm7 do likey\n");
	}
}

void mus_init_music() {
	musMessage msg;
	byte* lump = (byte*)W_CacheLumpName("GENMIDI", PU_STATIC);
	if (!lump) {
		iprintf("mus_init_music: GENMIDI not found\n");
		return;
	}
	DC_InvalidateAll();

	iprintf("mus_init_music: %p\n", lump);

	msg.type = musMessageType_instruments;
	msg.data = lump;

	fifoSendDatamsg(FIFO_MUS, sizeof(msg), (u8*)&msg);
	while (!fifoCheckValue32(FIFO_MUS));

	int result =  (int)fifoGetValue32(FIFO_MUS);
	if (result) {
		iprintf("mus_init_music: arm7 no likey\n");
	}
	else {
		iprintf("mus_init_music: arm7 do likey\n");
	}
}
