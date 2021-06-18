#include <stdio.h> 
#include <string.h>
#include <malloc.h>
#include <nds/arm7/audio.h>
#include <nds/timers.h>
#include "opl.h"
#include "musplay.h"

volatile mus_state_t *mus_state;
mus_state_t last_mus_state = {
	0,0,0,0
};


#define MUS_MIX_CHANNEL 1
#define MUS_PLAYBACK_RATE 11031

/* Flags: */
#define CH_SECONDARY	0x01
#define CH_SUSTAIN	0x02
#define CH_VIBRATO	0x04		/* set if modulation >= MOD_MIN */
#define CH_FREE		0x80
#define MOD_MIN		40		/* vibrato threshold */
#define CHANNEL_ID(ch) (*(ushort *)&(ch))
#define MAKE_ID(ch, mus) ((ch)|((mus)<<8))

//int snd_MusicVolume;    // maximum volume for music 0-15

volatile MUS_STATE musState = MUS_IDLE;
volatile uint OPLchannels = OPL2CHANNELS;
volatile OP2instrEntry* OPLinstruments = 0;
volatile ulong	MLtime = 0;
volatile uint	playingChannels = 0;
volatile musicBlock _mus;
static int mus_initialized = 0;

/* OPL channel (voice) data */
static  channelEntry channels[MAXCHANNELS];

volatile byte* MUSdata = 0;
volatile byte* score = 0;


void OPLwriteReg(byte reg, byte data) {
	OPL_WriteRegister(reg, data);
	//fifoSendValue32(FIFO_USER_01, (reg << 8) | data);
}

/*
* Write to an operator pair. To be used for register bases of 0x20, 0x40,
* 0x60, 0x80 and 0xE0.
*/
void OPLwriteChannel(uint regbase, uint channel, uchar data1, uchar data2)
{
	static uint op_num[] = {
		0x000, 0x001, 0x002, 0x008, 0x009, 0x00A, 0x010, 0x011, 0x012,
		0x100, 0x101, 0x102, 0x108, 0x109, 0x10A, 0x110, 0x111, 0x112 };

	register uint reg = regbase + op_num[channel];
	OPLwriteReg(reg, data1);
	OPLwriteReg(reg + 3, data2);
}

/*
* Write to channel a single value. To be used for register bases of
* 0xA0, 0xB0 and 0xC0.
*/
void OPLwriteValue(uint regbase, uint channel, uchar value)
{
	static uint reg_num[] = {
		0x000, 0x001, 0x002, 0x003, 0x004, 0x005, 0x006, 0x007, 0x008,
		0x100, 0x101, 0x102, 0x103, 0x104, 0x105, 0x106, 0x107, 0x108 };

	OPLwriteReg(regbase + reg_num[channel], value);
}

/*
* Write frequency/octave/keyon data to a channel
*/
void OPLwriteFreq(uint channel, uint freq, uint octave, uint keyon)
{
	OPLwriteValue(0xA0, channel, (BYTE)freq);
	OPLwriteValue(0xB0, channel, (freq >> 8) | (octave << 2) | (keyon << 5));
}

/*
* Adjust volume value (register 0x40)
*/
uint OPLconvertVolume(uint data, uint volume)
{
	static uchar volumetable[128] = {
		0, 1, 3, 5, 6, 8, 10, 11,
		13, 14, 16, 17, 19, 20, 22, 23,
		25, 26, 27, 29, 30, 32, 33, 34,
		36, 37, 39, 41, 43, 45, 47, 49,
		50, 52, 54, 55, 57, 59, 60, 61,
		63, 64, 66, 67, 68, 69, 71, 72,
		73, 74, 75, 76, 77, 79, 80, 81,
		82, 83, 84, 84, 85, 86, 87, 88,
		89, 90, 91, 92, 92, 93, 94, 95,
		96, 96, 97, 98, 99, 99, 100, 101,
		101, 102, 103, 103, 104, 105, 105, 106,
		107, 107, 108, 109, 109, 110, 110, 111,
		112, 112, 113, 113, 114, 114, 115, 115,
		116, 117, 117, 118, 118, 119, 119, 120,
		120, 121, 121, 122, 122, 123, 123, 123,
		124, 124, 125, 125, 126, 126, 127, 127 };

#if 0
	uint n;

	if (volume > 127)
		volume = 127;
	n = 0x3F - (data & 0x3F);
	n = (n * (uint)volumetable[volume]) >> 7;
	return (0x3F - n) | (data & 0xC0);
#else
	return 0x3F - (((0x3F - data) *
		(uint)volumetable[volume <= 127 ? volume : 127]) >> 7);
#endif
}

uint OPLpanVolume(uint volume, int pan)
{
	if (pan >= 0)
		return volume;
	else
		return (volume * (pan + 64)) / 64;
}

/*
* Write volume data to a channel
*/
void OPLwriteVolume(uint channel, OPL2instrument* instr, uint volume)
{
	OPLwriteChannel(0x40, channel, ((instr->feedback & 1) ?
		OPLconvertVolume(instr->level_1, volume) : instr->level_1) | instr->scale_1,
		OPLconvertVolume(instr->level_2, volume) | instr->scale_2);
}

/*
* Write pan (balance) data to a channel
*/
void OPLwritePan(uint channel, OPL2instrument* instr, int pan)
{
	uchar bits;
	if (pan < -36) bits = 0x10;		// left
	else if (pan > 36) bits = 0x20;	// right
	else bits = 0x30;			// both

	OPLwriteValue(0xC0, channel, instr->feedback | bits);
}

/*
* Write an instrument to a channel
*
* Instrument layout:
*
*   Operator1  Operator2  Descr.
*    data[0]    data[7]   reg. 0x20 - tremolo/vibrato/sustain/KSR/multi
*    data[1]    data[8]   reg. 0x60 - attack rate/decay rate
*    data[2]    data[9]   reg. 0x80 - sustain level/release rate
*    data[3]    data[10]  reg. 0xE0 - waveform select
*    data[4]    data[11]  reg. 0x40 - key scale level
*    data[5]    data[12]  reg. 0x40 - output level (bottom 6 bits only)
*          data[6]        reg. 0xC0 - feedback/AM-FM (both operators)
*/
void OPLwriteInstrument(uint channel, OPL2instrument* instr)
{
	OPLwriteChannel(0x40, channel, 0x3F, 0x3F);		// no volume
	OPLwriteChannel(0x20, channel, instr->trem_vibr_1, instr->trem_vibr_2);
	OPLwriteChannel(0x60, channel, instr->att_dec_1, instr->att_dec_2);
	OPLwriteChannel(0x80, channel, instr->sust_rel_1, instr->sust_rel_2);
	OPLwriteChannel(0xE0, channel, instr->wave_1, instr->wave_2);
	OPLwriteValue(0xC0, channel, instr->feedback | 0x30);
}

/*
* Stop all sounds
*/
void OPLshutup(void)
{
	uint i;

	for (i = 0; i < OPLchannels; i++)
	{
		OPLwriteChannel(0x40, i, 0x3F, 0x3F);	// turn off volume
		OPLwriteChannel(0x60, i, 0xFF, 0xFF);	// the fastest attack, decay
		OPLwriteChannel(0x80, i, 0x0F, 0x0F);	// ... and release
		OPLwriteValue(0xB0, i, 0);		// KEY-OFF
	}
}

/*
* Initialize hardware upon startup
*/
void OPLinit(void)
{
	OPLchannels = OPL2CHANNELS;
	OPLwriteReg(0x01, 0x20);		// enable Waveform Select
	OPLwriteReg(0x08, 0x40);		// turn off CSW mode
	OPLwriteReg(0xBD, 0x00);		// set vibrato/tremolo depth to low, set melodic mode

	OPLshutup();
}

static WORD freqtable[] = {					 /* note # */
	345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 615, 651,  /*  0 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 12 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 24 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 36 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 48 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 60 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 72 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 84 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651,  /* 96 */
	690, 731, 774, 820, 869, 921, 975, 517, 547, 580, 615, 651, /* 108 */
	690, 731, 774, 820, 869, 921, 975, 517 };		    /* 120 */

static BYTE octavetable[] = {					 /* note # */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,			     /*  0 */
	0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,			     /* 12 */
	1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2,			     /* 24 */
	2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3,			     /* 36 */
	3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4,			     /* 48 */
	4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5,			     /* 60 */
	5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,			     /* 72 */
	6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7,			     /* 84 */
	7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8,			     /* 96 */
	8, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9,			    /* 108 */
	9, 9, 9, 9, 9, 9, 9, 10 };				    /* 120 */

												//#define HIGHEST_NOTE 102
#define HIGHEST_NOTE 127

static WORD pitchtable[] = {				    /* pitch wheel */
	29193U, 29219U, 29246U, 29272U, 29299U, 29325U, 29351U, 29378U,  /* -128 */
	29405U, 29431U, 29458U, 29484U, 29511U, 29538U, 29564U, 29591U,  /* -120 */
	29618U, 29644U, 29671U, 29698U, 29725U, 29752U, 29778U, 29805U,  /* -112 */
	29832U, 29859U, 29886U, 29913U, 29940U, 29967U, 29994U, 30021U,  /* -104 */
	30048U, 30076U, 30103U, 30130U, 30157U, 30184U, 30212U, 30239U,  /*  -96 */
	30266U, 30293U, 30321U, 30348U, 30376U, 30403U, 30430U, 30458U,  /*  -88 */
	30485U, 30513U, 30541U, 30568U, 30596U, 30623U, 30651U, 30679U,  /*  -80 */
	30706U, 30734U, 30762U, 30790U, 30817U, 30845U, 30873U, 30901U,  /*  -72 */
	30929U, 30957U, 30985U, 31013U, 31041U, 31069U, 31097U, 31125U,  /*  -64 */
	31153U, 31181U, 31209U, 31237U, 31266U, 31294U, 31322U, 31350U,  /*  -56 */
	31379U, 31407U, 31435U, 31464U, 31492U, 31521U, 31549U, 31578U,  /*  -48 */
	31606U, 31635U, 31663U, 31692U, 31720U, 31749U, 31778U, 31806U,  /*  -40 */
	31835U, 31864U, 31893U, 31921U, 31950U, 31979U, 32008U, 32037U,  /*  -32 */
	32066U, 32095U, 32124U, 32153U, 32182U, 32211U, 32240U, 32269U,  /*  -24 */
	32298U, 32327U, 32357U, 32386U, 32415U, 32444U, 32474U, 32503U,  /*  -16 */
	32532U, 32562U, 32591U, 32620U, 32650U, 32679U, 32709U, 32738U,  /*   -8 */
	32768U, 32798U, 32827U, 32857U, 32887U, 32916U, 32946U, 32976U,  /*    0 */
	33005U, 33035U, 33065U, 33095U, 33125U, 33155U, 33185U, 33215U,  /*    8 */
	33245U, 33275U, 33305U, 33335U, 33365U, 33395U, 33425U, 33455U,  /*   16 */
	33486U, 33516U, 33546U, 33576U, 33607U, 33637U, 33667U, 33698U,  /*   24 */
	33728U, 33759U, 33789U, 33820U, 33850U, 33881U, 33911U, 33942U,  /*   32 */
	33973U, 34003U, 34034U, 34065U, 34095U, 34126U, 34157U, 34188U,  /*   40 */
	34219U, 34250U, 34281U, 34312U, 34343U, 34374U, 34405U, 34436U,  /*   48 */
	34467U, 34498U, 34529U, 34560U, 34591U, 34623U, 34654U, 34685U,  /*   56 */
	34716U, 34748U, 34779U, 34811U, 34842U, 34874U, 34905U, 34937U,  /*   64 */
	34968U, 35000U, 35031U, 35063U, 35095U, 35126U, 35158U, 35190U,  /*   72 */
	35221U, 35253U, 35285U, 35317U, 35349U, 35381U, 35413U, 35445U,  /*   80 */
	35477U, 35509U, 35541U, 35573U, 35605U, 35637U, 35669U, 35702U,  /*   88 */
	35734U, 35766U, 35798U, 35831U, 35863U, 35895U, 35928U, 35960U,  /*   96 */
	35993U, 36025U, 36058U, 36090U, 36123U, 36155U, 36188U, 36221U,  /*  104 */
	36254U, 36286U, 36319U, 36352U, 36385U, 36417U, 36450U, 36483U,  /*  112 */
	36516U, 36549U, 36582U, 36615U, 36648U, 36681U, 36715U, 36748U }; /*  120 */


static void writeFrequency(uint slot, uint note, int pitch, uint keyOn)
{
	uint freq = freqtable[note];
	uint octave = octavetable[note];

	if (pitch)
	{
#ifdef DEBUG
		printf("DEBUG: pitch: N: %d  P: %d           \n", note, pitch);
#endif
		if (pitch > 127) pitch = 127;
		else if (pitch < -128) pitch = -128;
		freq = ((ulong)freq * pitchtable[pitch + 128]) >> 15;
		if (freq >= 1024)
		{
			freq >>= 1;
			octave++;
		}
	}
	if (octave > 7)
		octave = 7;
	OPLwriteFreq(slot, freq, octave, keyOn);
}

static void writeModulation(uint slot, OPL2instrument* instr, int state)
{
	if (state)
		state = 0x40;	/* enable Frequency Vibrato */
	OPLwriteChannel(0x20, slot,
		(instr->feedback & 1) ? (instr->trem_vibr_1 | state) : instr->trem_vibr_1,
		instr->trem_vibr_2 | state);
}

static uint calcVolume(uint channelVolume, uint MUSvolume, uint noteVolume)
{
	noteVolume = ((ulong)channelVolume * MUSvolume * noteVolume) / (127 * 127);
	if (noteVolume > 127)
		return 127;
	else
		return noteVolume;
}

static int occupyChannel(musicBlock* mus, uint slot, uint channel,
	int note, int volume, OP2instrEntry* instrument, uchar secondary)
{
	OPL2instrument* instr;
	volatile OPLdata* data = &mus->data;
	channelEntry* ch = &channels[slot];

	playingChannels++;

	ch->channel = channel;
	ch->musnumber = mus->number;
	ch->note = note;
	ch->flags = secondary ? CH_SECONDARY : 0;
	if (data->channelModulation[channel] >= MOD_MIN) {
		ch->flags |= CH_VIBRATO;
	}
	ch->time = MLtime;
	if (volume == -1) {
		volume = data->channelLastVolume[channel];
	}
	else {
		data->channelLastVolume[channel] = volume;
	}
	ch->realvolume = calcVolume(data->channelVolume[channel], mus->volume, ch->volume = volume);
	if (instrument->flags & FL_FIXED_PITCH) {
		note = instrument->note;
	}
	else if (channel == PERCUSSION) {
		note = 60;			// C-5
	}
	if (secondary && (instrument->flags & FL_DOUBLE_VOICE)) {
		ch->finetune = instrument->finetune - 0x80;
	}
	else {
		ch->finetune = 0;
	}
	ch->pitch = ch->finetune + data->channelPitch[channel];
	if (secondary) {
		instr = &instrument->instr[1];
	}
	else {
		instr = &instrument->instr[0];
	}
	ch->instr = instr;
	if ((note += instr->basenote) < 0) {
		while ((note += 12) < 0);
	}
	else if (note > HIGHEST_NOTE) {
		while ((note -= 12) > HIGHEST_NOTE);
	}
	ch->realnote = note;

	OPLwriteInstrument(slot, instr);
	if (ch->flags & CH_VIBRATO) {
		writeModulation(slot, instr, 1);
	}
	OPLwritePan(slot, instr, data->channelPan[channel]);
	OPLwriteVolume(slot, instr, ch->realvolume);
	writeFrequency(slot, note, ch->pitch, 1);
	return slot;
}

static int releaseChannel(musicBlock* mus, uint slot, uint killed)
{
	channelEntry* ch = &channels[slot];
#ifdef DEBUG
	printf("\nDEBUG: Release  Ch: %d  Adl: %d  %04X\n", channel, slot, Adlibchannel[slot]);
#endif
	playingChannels--;
	writeFrequency(slot, ch->realnote, ch->pitch, 0);
	ch->channel |= CH_FREE;
	ch->flags = CH_FREE;
	if (killed)
	{
		OPLwriteChannel(0x80, slot, 0x0F, 0x0F);  // release rate - fastest
		OPLwriteChannel(0x40, slot, 0x3F, 0x3F);  // no volume
	}
	return slot;
}

static int releaseSustain(musicBlock* mus, uint channel)
{
	uint i;
	uint id = MAKE_ID(channel, mus->number);

	for (i = 0; i < OPLchannels; i++)
	{
		if (CHANNEL_ID(channels[i]) == id && channels[i].flags & CH_SUSTAIN)
			releaseChannel(mus, i, 0);
	}
	return 0;
}

volatile int last_returned_channel = -1;

static int findFreeChannel(musicBlock* mus, uint flag)
{
	static uint last = 0xffffffff;
	uint i;
	uint oldest = 0xffffffff;
	ulong oldesttime = MLtime;

	/* find free channel */
	for (i = 0; i < OPLchannels; i++)
	{
		if (++last == OPLchannels)	/* use cyclic `Next Fit' algorithm */
			last = 0;
		if (channels[last].flags & CH_FREE)
			return last_returned_channel = last;
	}

	if (flag & 1)
		return -1;			/* stop searching if bit 0 is set */

							/* find some 2nd-voice channel and determine the oldest */
	for (i = 0; i < OPLchannels; i++)
	{
		if (channels[i].flags & CH_SECONDARY)
		{
#ifdef DEBUG
			printf("\nDEBUG: Kill 2nd %04X\n", Adlibchannel[i]);
#endif
			releaseChannel(mus, i, -1);
			return i;
		}
		else
			if (channels[i].time < oldesttime)
			{
				oldesttime = channels[i].time;
				oldest = i;
			}
	}

	/* if possible, kill the oldest channel */
	if (!(flag & 2) && oldest != 0xffffffff)
	{
#ifdef DEBUG
		printf("DEBUG: Kill %04X !!!\n", Adlibchannel[oldest]);
#endif
		releaseChannel(mus, oldest, 0xffffffff);
		return last_returned_channel = oldest;
	}

	/* can't find any free channel */
#ifdef DEBUG
	printf("DEBUG: Full!!!\n");
#endif
	return last_returned_channel = -1;
}

volatile uint opl_get_intr = -1;

static OP2instrEntry* getInstrument(musicBlock* mus, uint channel, uchar note)
{
	uint instrnumber;

	if (mus->percussMask & (1 << channel))
	{
		if (note < 35 || note > 81)
			return NULL;		/* wrong percussion number */
		instrnumber = note + (128 - 35);
	}
	else
		instrnumber = mus->data.channelInstr[channel];

	opl_get_intr = instrnumber;

	if (OPLinstruments)
		return &OPLinstruments[instrnumber];
	else
		return NULL;
}

volatile int opl_play_note = -1;
volatile int opl_play_note_instr = -1;

// code 1: play note
void OPLplayNote(musicBlock* mus, uint channel, uchar note, int volume)
{
	int i;
	OP2instrEntry* instr;
	opl_play_note++;

	if ((instr = getInstrument(mus, channel, note)) == NULL)
		return;
	opl_play_note_instr++;
#ifdef DEBUG
	cprintf("\rDEBUG: play: Ch: %d  N: %d  V: %d (%d)  I: %d (%s)  Fi: %d           \r\n",
		channel, note, volume, data->channelVolume[channel], instrnumber,
		instrumentName[instrnumber], (instr->flags & FL_DOUBLE_VOICE) ?
		(instr->finetune - 0x80) : 0);
#endif

	if ((i = findFreeChannel(mus, (channel == PERCUSSION) ? 2 : 0)) != -1)
	{
		occupyChannel(mus, i, channel, note, volume, instr, 0);
		if (/*!OPLsinglevoice && */instr->flags == FL_DOUBLE_VOICE)
		{
			if ((i = findFreeChannel(mus, (channel == PERCUSSION) ? 3 : 1)) != -1)
				occupyChannel(mus, i, channel, note, volume, instr, 1);
		}
	}
}

// code 0: release note
void OPLreleaseNote(musicBlock* mus, uint channel, uchar note)
{
	uint i;
	uint id = MAKE_ID(channel, mus->number);
	volatile OPLdata* data = &mus->data;
	uint sustain = data->channelSustain[channel];

#ifdef DEBUG
	printf("DEBUG: release: Ch: %d  N: %d           \n", channel, note);
#endif
	for (i = 0; i < OPLchannels; i++)
		if (CHANNEL_ID(channels[i]) == id && channels[i].note == note)
		{
			if (sustain < 0x40)
				releaseChannel(mus, i, 0);
			else
				channels[i].flags |= CH_SUSTAIN;
		}
}

// code 2: change pitch wheel (bender)
void OPLpitchWheel(musicBlock* mus, uint channel, int pitch)
{
	uint i;
	uint id = MAKE_ID(channel, mus->number);
	volatile OPLdata* data = &mus->data;

#ifdef DEBUG
	printf("DEBUG: pitch: Ch: %d  P: %d\n", channel, pitch);
#endif
	data->channelPitch[channel] = pitch;
	for (i = 0; i < OPLchannels; i++)
	{
		channelEntry* ch = &channels[i];
		if (CHANNEL_ID(*ch) == id)
		{
			ch->time = MLtime;
			ch->pitch = ch->finetune + pitch;
			writeFrequency(i, ch->realnote, ch->pitch, 1);
		}
	}
}

// code 4: change control
void OPLchangeControl(musicBlock* mus, uint channel, uchar controller, int value)
{
	uint i;
	uint id = MAKE_ID(channel, mus->number);
	volatile OPLdata* data = &mus->data;
#ifdef DEBUG
	printf("DEBUG: ctrl: Ch: %d  C: %d  V: %d\n", channel, controller, value);
#endif

	switch (controller) {
	case ctrlPatch:			/* change instrument */
		data->channelInstr[channel] = value;
		break;
	case ctrlModulation:
		data->channelModulation[channel] = value;
		for (i = 0; i < OPLchannels; i++)
		{
			channelEntry* ch = &channels[i];
			if (CHANNEL_ID(*ch) == id)
			{
				uchar flags = ch->flags;
				ch->time = MLtime;
				if (value >= MOD_MIN)
				{
					ch->flags |= CH_VIBRATO;
					if (ch->flags != flags)
						writeModulation(i, ch->instr, 1);
				}
				else {
					ch->flags &= ~CH_VIBRATO;
					if (ch->flags != flags)
						writeModulation(i, ch->instr, 0);
				}
			}
		}
		break;
	case ctrlVolume:		/* change volume */
		data->channelVolume[channel] = value;
		for (i = 0; i < OPLchannels; i++)
		{
			channelEntry* ch = &channels[i];
			if (CHANNEL_ID(*ch) == id)
			{
				ch->time = MLtime;
				ch->realvolume = calcVolume(value, mus->volume, ch->volume);
				OPLwriteVolume(i, ch->instr, ch->realvolume);
			}
		}
		break;
	case ctrlPan:			/* change pan (balance) */
		data->channelPan[channel] = value -= 64;
		for (i = 0; i < OPLchannels; i++)
		{
			channelEntry* ch = &channels[i];
			if (CHANNEL_ID(*ch) == id)
			{
				ch->time = MLtime;
				OPLwritePan(i, ch->instr, value);
			}
		}
		break;
	case ctrlSustainPedal:		/* change sustain pedal (hold) */
		data->channelSustain[channel] = value;
		if (value < 0x40)
			releaseSustain(mus, channel);
		break;
	}
}

void OPLplayMusic(musicBlock* mus)
{
	uint i;
	volatile OPLdata* data = &mus->data;

	for (i = 0; i < CHANNELS; i++)
	{
		data->channelVolume[i] = 127;	/* default volume 127 (full volume) */
		data->channelSustain[i] = data->channelLastVolume[i] = 0;
	}
}

void OPLstopMusic(musicBlock* mus)
{
	uint i;
	for (i = 0; i < OPLchannels; i++)
		if (channels[i].musnumber == mus->number && !(channels[i].flags & CH_FREE))
			releaseChannel(mus, i, -1);
}

void OPLchangeVolume(musicBlock* mus, uint volume)
{
	//    struct OPLdata *data = mus->driverdata;
	volatile uchar* channelVolume = mus->data.channelVolume;
	uint i;
	for (i = 0; i < OPLchannels; i++)
	{
		channelEntry* ch = &channels[i];
		if (ch->musnumber == mus->number)
		{
			ch->realvolume = calcVolume(channelVolume[ch->channel & 0xF], volume, ch->volume);
			if (mus->state == ST_PLAYING)
				OPLwriteVolume(i, ch->instr, ch->realvolume);
		}
	}
}

void OPLpauseMusic(musicBlock* mus)
{
	uint i;
	for (i = 0; i < OPLchannels; i++)
	{
		channelEntry* ch = &channels[i];
		if (ch->musnumber == mus->number)
		{
			OPL2instrument* instr = ch->instr;
			OPLwriteVolume(i, instr, 0);
			OPLwriteChannel(0x60, i, 0, 0);	// attack, decay
			OPLwriteChannel(0x80, i, instr->sust_rel_1 & 0xF0,
				instr->sust_rel_2 & 0xF0);	// sustain, release
		}
	}
}

void OPLunpauseMusic(musicBlock* mus)
{
	uint i;
	for (i = 0; i < OPLchannels; i++)
	{
		channelEntry* ch = &channels[i];
		if (ch->musnumber == mus->number)
		{
			OPL2instrument* instr = ch->instr;

			OPLwriteChannel(0x60, i, instr->att_dec_1, instr->att_dec_2);
			OPLwriteChannel(0x80, i, instr->sust_rel_1, instr->sust_rel_2);
			OPLwriteVolume(i, instr, ch->realvolume);
		}
	}
}


void mus_stop_music() {
	volatile musicBlock* mus = &_mus;

	if (!mus_initialized) {
		return;
	}
	//only stop if playing
	//?should this happen if paused?
	if (mus->state != ST_PLAYING) {
		return;
	}
	mus->state = ST_STOPPED;
	OPLstopMusic(mus);
	musState = MUS_IDLE;
	//printf("stopping music...");
	/*svcWaitSynchronization(musResponse, U64_MAX);
	svcClearEvent(musResponse);*/
	//printf(" done\n");
}


void mus_play_music(u8* data) {
	volatile musicBlock* mus = &_mus;

	if (!mus_initialized) {
		return;
	}

	switch (mus->state) {
	case ST_EMPTY:
	case ST_STOPPED:
		break;
	case ST_PLAYING:
	case ST_PAUSED:
	default:
		mus_stop_music();
		OPLstopMusic(mus);
	}

	MUSdata = score = data;
	if (score == 0) {
		mus->state = ST_EMPTY;
		musState = MUS_IDLE;
		return;
	}
	OPLplayMusic(mus);
	mus->state = ST_PLAYING;
	musState = MUS_PLAYING;
	//printf("starting music...");
	/*svcSignalEvent(musRequest);
	svcWaitSynchronization(musResponse, U64_MAX);
	svcClearEvent(musResponse);*/
	//printf(" done\n");
}

void mus_update_volume(int volume) {
	volatile musicBlock* mus = &_mus;
	if (!mus_initialized) {
		return;
	}
	mus->volume = volume;
	OPLchangeVolume(mus, volume);
}

volatile byte* opl_mus_inst_data = (byte*)-1L;

int mus_load_instruments(byte *lump) {
	static byte masterhdr[8] = "#OPL_II#";
	byte hdr[8];

	opl_mus_inst_data = lump;

	// Check header
	if (memcmp((char*)lump, masterhdr, sizeof(hdr)) != 0)
	{
		//W_ReleaseLumpName("GENMIDI");
		return -1;
	}

	OPLinstruments = (OP2instrEntry*)(lump + 8);
	return 0;
}

static ulong delayTicks(musicBlock* mus)
{
	ulong time = 0;
	uchar data;

	do {
		time <<= 7;
		data = *MUSdata++;
		time += data & 0x7F;
	} while (data & 0x80);

	return time;
}

static int playTick(musicBlock* mus)
{

	for (;;)
	{
		uint data = *MUSdata++;
		uint command = (data >> 4) & 7;
		uint channel = data & 0x0F;
		uint last = data & 0x80;
		byte temp;

		switch (command) {
		case 0:	// release note
			mus->playingcount--;
			temp = *MUSdata++;
			OPLreleaseNote(mus, channel, temp);
			break;
		case 1: {	// play note
			uchar note = *MUSdata++;
			mus->playingcount++;
			if (note & 0x80)	// note with volume
				OPLplayNote(mus, channel, note & 0x7F, *MUSdata++);
			else
				OPLplayNote(mus, channel, note, -1);
		} break;
		case 2:	// pitch wheel
			temp = *MUSdata++;
			OPLpitchWheel(mus, channel, temp - 0x80);
			break;
		case 3:	// system event (valueless controller)
			OPLchangeControl(mus, channel, *MUSdata++, 0);
			break;
		case 4: {	// change control
			uchar ctrl = *MUSdata++;
			uchar value = *MUSdata++;
			OPLchangeControl(mus, channel, ctrl, value);
		} break;
		case 6:	// end
			return -1;
		case 5:	// ???
		case 7:	// ???
			break;
		}
		if (last)
			break;
	}
	return 0;
}

void mus_play_timer(void) {
	volatile musicBlock* mus = &_mus;
	
	if (mus_state->state == MUS_CHANGING && last_mus_state.state != MUS_CHANGING) {
		last_mus_state.state = MUS_CHANGING;
		last_mus_state.mus = 0;
		if (mus) {
			mus->state = ST_PAUSED;
		}
	}
	//printf("tick: %x %d %d %d\n", MUSdata, mus->state, mus->ticks, MLtime);
	if (mus && mus->state == ST_PLAYING) {
		if (!mus->ticks) {
			if (playTick(mus)) {
				MUSdata = score;
			}
			mus->time += mus->ticks = delayTicks(mus);
		}
		mus->ticks--;
	}
	//printf("tock: %x %d %d %d\n", MUSdata, mus->state, mus->ticks, MLtime);
	MLtime++;
}

#define SND_SAMPLES (2048)
#define SND_MASK (2048-1)

ushort snd_Buffer[SND_SAMPLES];

volatile uint filled_mus_pos = 0; //filled to this point in playback
volatile uint mus_buffer_pos = 0; //the current playback position
//volatile uint mus_buffer_last = 0; //the current playback position

uint mus_pos() {
	u16 time1 = TIMER1_DATA;
	u16 time2 = TIMER2_DATA;
	u16 time3 = TIMER3_DATA;

	mus_buffer_pos = (((uint)time3) << 16) + (uint)time2;
	//mus_buffer_pos += MUS_PLAYBACK_RATE / 60;

	return mus_buffer_pos;
}

short adlib_data[2048];

static uint samples_to_ticks(uint samples) {
	return (samples * 140) / MUS_PLAYBACK_RATE;
}

static uint ticks_to_samples(uint ticks) {
	return (ticks * MUS_PLAYBACK_RATE) / 140;
}

void mus_update(short* buffer, int cnt, uint fpos) {
	int i;
	uint pos = fpos;
	for (i = 0; i < cnt; i++, pos++) {
		snd_Buffer[pos & SND_MASK] = buffer[i];
	}
}

//uint mus_frame_ticks[9] = { 0,0,0,0,0,0,0 };

void mus_frame() {
	if (last_mus_state.volume != mus_state->volume) {
		last_mus_state.volume = mus_state->volume;
		mus_update_volume(last_mus_state.volume);
	}
	if (last_mus_state.mus == 0 && mus_state->mus != 0) {
		mus_play_music(mus_state->mus);
		last_mus_state.mus = mus_state->mus;
		last_mus_state.state = mus_state->state = MUS_PLAYING;
	}
	/*
	if (last_mus_state.state != mus_state->state) {
		last_mus_state.state = mus_state->state;
		last_mus_state.mus = 0;
		if (mus) {
			mus->state = ST_PAUSED;
		}
	}
	*/

	if (musState != MUS_PLAYING) {
		return;
	}
	uint cpos = mus_pos();
	uint fpos = filled_mus_pos;
	if (cpos > 0x40000000) {
		cpos -= 0x40000000;
		fpos -= 0x40000000;
	}
	
	//mus_frame_ticks[5] = fpos;
	//mus_frame_ticks[6] = mus_buffer_last;
	
	//if we get behind then push the filled position forward
	if (fpos < cpos) {
		fpos = cpos;
	}

	uint end_ticks = samples_to_ticks(cpos) + 14;
	uint filled_ticks = samples_to_ticks(fpos);
	uint ticks = end_ticks - filled_ticks;

	//mus_frame_ticks[0] = ticks;
	//mus_frame_ticks[1] = end_ticks;
	//mus_frame_ticks[2] = filled_ticks;
	//mus_frame_ticks[3] = cpos;
	//mus_frame_ticks[4] = fpos;

	//render samples here
	if (ticks) {
#if 0
		volatile musicBlock* mus = &_mus;
		uint samples = ticks_to_samples(1);
		mus_frame_ticks[8] = samples;
		while (ticks) {
			//playTick(mus);
			ticks--;
			OPL_Render_Samples(adlib_data, samples);
			mus_update(adlib_data, samples, fpos);
			fpos += samples;
		}
		filled_mus_pos = fpos;
#else
		uint samples = ticks_to_samples(ticks);
		//mus_frame_ticks[8] = samples;
		
		OPL_Render_Samples(adlib_data, samples);
		mus_update(adlib_data, samples, fpos);
		fpos += samples;

		filled_mus_pos = fpos;
#endif
	}
	//mus_buffer_last = cpos;
	//mus_frame_ticks[7] = filled_mus_pos;
}

int mus_setup_timer();
//volatile byte* mus_handler_instr = (byte*)-1L;
//volatile int mus_handler_instr_count = 0;
//volatile musMessage mus_msg = { 2,3 };
extern int* mix_buffer;

void musDataHandler(int bytes, void* user_data) {
	//---------------------------------------------------------------------------------
	musMessage msg;
	byte* data;
	u32 result = 0;
	mus_state_t* state;

	fifoGetDatamsg(FIFO_MUS, bytes, (u8*)&msg);

	switch (msg.type) {
	case musMessageType_init:
		state = (mus_state_t *)msg.data;
		mus_state = state;
		mix_buffer = state->mix_buffer;
		//mus_handler_instr = 
		data = state->instruments;
		mus_init();
		mus_load_instruments(data);
		break;
	/*case musMessageType_instruments:
		data = msg.data;
		mus_handler_instr = data;
		mus_handler_instr_count++;
		memcpy(&mus_msg, &msg, sizeof(musMessage));
		mus_load_instruments(data);
		break;
	case musMessageType_play_song:
		data = msg.data;
		mus_play_music(data);
		break;
	case musMessageType_stop_song:
		break;
	case musMessageType_volume:
		int volume = (int)msg.data;
		mus_update_volume(volume);
		break;*/
	default:
		break;
	}
	fifoSendValue32(FIFO_MUS, result);

}

void mus_init() {
	//if (!audio_initialized) {
	//	return;
	//}


	if (!OPL_Init(MUS_PLAYBACK_RATE))
	{
		//printf("Dude.  The Adlib isn't responding.\n");
		return;
	}
	memset(channels, 0xFF, sizeof channels);
	//OPLinstruments = 0;
	OPLinit();

	/*if (mus_load_instruments(instr)) {
		OPL_Shutdown();
		return;
	}*/
	volatile musicBlock* mus = &_mus;
	memset(mus, 0, sizeof(musicBlock));
	mus->state = ST_EMPTY;
	mus->number = 0;
	mus->volume = 256;
	mus->channelMask = 0xffffffff;
	mus->percussMask = 1 << PERCUSSION;

	if (mus_setup_timer()) {
		mus_initialized = 0;
	}


	mus_initialized = 1;

}

int mus_setup_timer() {
	int channel = 15;

	memset(snd_Buffer, 0, SND_SAMPLES*2);
	snd_Buffer[4] = snd_Buffer[5] = 0x7fff;	// force a pop for debugging


	SCHANNEL_SOURCE(channel) = (u32)snd_Buffer;
	SCHANNEL_REPEAT_POINT(channel) = 0;
	SCHANNEL_LENGTH(channel) = SND_SAMPLES/2;
	SCHANNEL_TIMER(channel) = (-(33513982 / 2) / MUS_PLAYBACK_RATE);// SOUND_FREQ(11030);
	SCHANNEL_CR(channel) = SCHANNEL_ENABLE | SOUND_VOL(127) | SOUND_PAN(64) | (/*SoundFormat_16Bit*/1 << 29) | SOUND_REPEAT;

	timerStart(0, ClockDivider_1024, TIMER_FREQ_1024(140), mus_play_timer);
	
	TIMER_DATA(1) = TIMER_FREQ(MUS_PLAYBACK_RATE);
	TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_1;
	TIMER_DATA(2) = 0;
	TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
	TIMER_DATA(3) = 0;
	TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;

	return 0;
}
