#ifdef ARM9
#include <nds/fifocommon.h>
#endif
#include <stdint.h>
#include "musfifo.h"
#include "DoomDef.h"

#ifdef WIN32
#define iprintf printf
void DC_FlushRange(void* p, int len) {
}
#endif

void mus_play_timer(void) {

}
extern int snd_MusicVolume;

volatile mus_state_t mus_state;

void mus_update_volume() {
	mus_state.volume = snd_MusicVolume;
	DC_FlushRange(&mus_state, sizeof(mus_state));

	//DC_InvalidateAll();

	/*musMessage msg;
	msg.type = musMessageType_volume;
	msg.data = (void *)snd_MusicVolume;

	DC_InvalidateAll();
	
	iprintf("mus_update_volume\n");
	fifoSendDatamsg(FIFO_MUS, sizeof(msg), (u8*)&msg);
	iprintf("mus_update_volume sent\n");

	while (!fifoCheckValue32(FIFO_MUS));

	int result = (int)fifoGetValue32(FIFO_MUS);
	if (result) {
		iprintf("mus_update_volume: arm7 no likey\n");
	}
	else {
		iprintf("mus_update_volume: arm7 do likey\n");
	}*/
}

static byte* last_mus_lump = 0;

void mus_play_music(char* name) {
	musMessage msg;
	byte* lump = W_CacheLumpName(name, PU_MUSIC);
	if (!lump) {
		iprintf("mus_play_music: failed\n");
		return;
	}

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

	mus_state.mus = lump + header->scoreStart;
	mus_state.state = MUS_CHANGING;
	DC_FlushRange(&mus_state, sizeof(mus_state));

	/*msg.type = musMessageType_play_song;
	msg.data = lump + header->scoreStart;

	DC_InvalidateAll();

	fifoSendDatamsg(FIFO_MUS, sizeof(msg), (u8*)&msg);
	while (!fifoCheckValue32(FIFO_MUS));

	int result = (int)fifoGetValue32(FIFO_MUS);
	if (result) {
		iprintf("mus_play_music: arm7 no likey\n");
	}
	else {
		iprintf("mus_play_music: arm7 do likey\n");
	}*/
}

volatile byte *mus_init_music_lump = 0;

uint32_t *mix_buffer = 0;

void mus_init_music() {
	musMessage msg;
	byte* lump = (byte*)W_CacheLumpName("GENMIDI", PU_STATIC);
	if (!lump) {
		iprintf("mus_init_music: GENMIDI not found\n");
		return;
	}

	iprintf("mus_init_music: %08x\n", lump);
	mix_buffer = malloc(11025 * sizeof(uint32_t));

	mus_state.volume = snd_MusicVolume;
	mus_state.instruments = lump;
	mus_state.mus = 0;
	mus_state.mix_buffer = mix_buffer;
	mus_state.state = MUS_IDLE;

	msg.type = musMessageType_init;
	msg.data = &mus_state;

	DC_FlushRange(&mus_state,sizeof(mus_state));

#ifdef ARM9
	fifoSendDatamsg(FIFO_MUS, sizeof(msg), (u8*)&msg);
	while (!fifoCheckValue32(FIFO_MUS));

	int result = (int)fifoGetValue32(FIFO_MUS);
	if (result) {
		iprintf("mus_init_music: arm7 no likey\n");
	}
	else {
		iprintf("mus_init_music: arm7 do likey\n");
	}
#endif
	/* ---------------------------------------------- 
	mus_init_music_lump = lump;
	msg.type = musMessageType_instruments;
	msg.data = lump;

	DC_InvalidateAll();

	mus_init_music_lump = lump;

	fifoSendDatamsg(FIFO_MUS, sizeof(msg), (u8*)&msg);
	while (!fifoCheckValue32(FIFO_MUS));

	result = (int)fifoGetValue32(FIFO_MUS);
	if (result) {
		iprintf("mus_init_music: arm7 no likey\n");
	}
	else {
		iprintf("mus_init_music: arm7 do likey\n");
	}*/
}
