#pragma once

#if defined(ARM7) || defined (ARM9)
#include <nds/fifocommon.h>
#endif

#pragma pack(push,1)
typedef struct {
	char			ID[4];		// identifier "MUS" 0x1A
	unsigned short	scoreLen;
	unsigned short	scoreStart;
	unsigned short	channels;
	unsigned short	dummy1;
	unsigned short  instrCnt;
	unsigned short	dummy2;
	//	unsigned short	instruments[];
} mus_header_t;
#pragma pack(pop)

typedef enum {
	/*musMessageType_instruments = 1,
	musMessageType_play_song = 2,
	musMessageType_stop_song = 3,
	musMessageType_volume = 4,*/
	musMessageType_init = 5
} musMessageType;

#pragma pack(push,1)
typedef struct musMessage {
	unsigned int type;
	void* data;
} musMessage;
#pragma pack(pop)

#define FIFO_MUS FIFO_USER_04

typedef enum {
	MUS_IDLE,
	MUS_PLAYING,
	MUS_CHANGING,
	MUS_EXIT
} MUS_STATE;

typedef struct {
	MUS_STATE state;
	int volume;
	unsigned char *instruments;
	unsigned char* mix_buffer;
	unsigned char *mus;
} mus_state_t;
