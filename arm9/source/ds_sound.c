#include "DoomDef.h"
#include "P_local.h"
#include "soundst.h"
#include "map.h"
#include <string.h>
#include <stdint.h>

#ifdef WIN32
#define iprintf printf
#endif

int snd_LeftChannelId,
	snd_RightChannelId,
	snd_NextCleanup,
	snd_AmbChan,		//ambient sound channel
	snd_Channels,		// number of sounds that can play at once
	snd_MaxVolume,      // maximum volume for sound
	snd_MusicVolume;    // maximum volume for music

void mus_update_volume();
void mus_init_music();
void mus_play_music(char* name);


extern sfxinfo_t S_sfx[];
extern musicinfo_t S_music[];

extern angle_t		viewangle;
extern fixed_t		viewx, viewy, viewz;

extern void** lumpcache;

#define SND_SAMPLES (2048)
static int snd_Samples;
static int snd_Speed;

/*static*/ byte c_snd_Buffer_left[SND_SAMPLES];
/*static*/ byte c_snd_Buffer_right[SND_SAMPLES];
/*static*/ byte* snd_Buffer_left;
/*static*/ byte* snd_Buffer_right;

static byte soundCurve[MAX_SND_DIST];
static int snd_scaletable[32][256];

static channel_t channel[MAX_CHANNELS];
static unsigned int snd_PaintedTime;

#define S_CLIPPING_DIST (1200 * FRACUNIT)
#define S_CLOSE_DIST (160 * FRACUNIT)
#define S_ATTENUATOR ((S_CLIPPING_DIST - S_CLOSE_DIST) >> FRACBITS)
#define S_STEREO_SWING (96 * FRACUNIT)


unsigned int S_SoundTime() {
#ifdef ARM9
	u32 time2 = TIMER2_DATA;
	u32 time3 = TIMER3_DATA;

	u32 time = (time3 << 16) + time2;
#endif
#ifdef WIN32
	static uint32_t time = 0;
	time += 550;
#endif

	return time;
}


void S_StartSong(int song, boolean loop) {
	if (song < mus_e1m1 || song > NUMMUSIC)
	{
		return;
	}
	mus_play_music(S_music[song].name);
}

void S_SetMusicVolume(void) {
	mus_update_volume();
}

void S_GetChannelInfo(SoundInfo_t* s) {

}

int S_GetSfxLumpNum(sfxinfo_t* sound) {
	if (sound->name == 0) {
		return 0;
	}
	if (sound->link) {
		sound = sound->link;
	}

	return W_GetNumForName(sound->name);
}

int S_AdjustSoundParams(mobj_t* listener, mobj_t* source, int* vol, int* sep, int* dist) {
	fixed_t        approx_dist;
	fixed_t        adx;
	fixed_t        ady;
	angle_t        angle;

	// calculate the distance to sound origin
	//  and clip it if necessary
	adx = abs(listener->x - source->x);
	ady = abs(listener->y - source->y);

	// From _GG1_ p.428. Appox. eucledian distance fast.
	approx_dist = adx + ady - ((adx < ady ? adx : ady) >> 1);

	if (gamemap != 8 && approx_dist > S_CLIPPING_DIST) {
		return 0;
	}

	// angle of source to listener
	angle = R_PointToAngle2(
		listener->x,
		listener->y,
		source->x,
		source->y);

	if (angle > listener->angle) {
		angle = angle - listener->angle;
	}
	else {
		angle = angle + (0xffffffff - listener->angle);
	}

	angle >>= ANGLETOFINESHIFT;

	// stereo separation
	*sep = 128 - (FixedMul(S_STEREO_SWING, finesine[angle]) >> FRACBITS);
	*dist = approx_dist >> FRACBITS;

	// volume calculation
	if (approx_dist < S_CLOSE_DIST) {
		*vol = snd_MaxVolume;
	}
	else if (gamemap == 8) {
		if (approx_dist > S_CLIPPING_DIST) {
			approx_dist = S_CLIPPING_DIST;
		}

		*vol = 15 + ((snd_MaxVolume - 15)
			* ((S_CLIPPING_DIST - approx_dist) >> FRACBITS))
			/ S_ATTENUATOR;
	}
	else {
		// distance effect
		*vol = (snd_MaxVolume
			* ((S_CLIPPING_DIST - approx_dist) >> FRACBITS))
			/ S_ATTENUATOR;
	}

	return (*vol > 0);
}

int S_SoundChannel(mobj_t* origin, int sound_id, int priority) {
	static int sndcount = 0;
	int i, chan;

	for (i = 0; i < snd_Channels; i++) {
		if (origin->player) {
			i = snd_Channels;
			break; // let the player have more than one sound.
		}
		if (origin == channel[i].mo) { 
			// only allow other mobjs one sound
			S_StopSound(channel[i].mo);
			break;
		}
	}

	if (i >= snd_Channels) {
		//ambient sounds
		if (sound_id >= sfx_wind) {
			if (snd_AmbChan != -1 && S_sfx[sound_id].priority <= S_sfx[channel[snd_AmbChan].sound_id].priority) {
				return -1; //ambient channel already in use
			}
			else {
				snd_AmbChan = -1;
			}
		}
		//not attached to a mobj
		for (i = 0; i < snd_Channels; i++) {
			if (channel[i].mo == 0) {
				break;
			}
		}
		if (i >= snd_Channels) {
			//look for a lower priority sound to replace.
			sndcount++;
			if (sndcount >= snd_Channels) {
				sndcount = 0;
			}
			for (chan = 0; chan < snd_Channels; chan++) {
				i = (sndcount + chan) % snd_Channels;
				if (priority >= channel[i].priority) {
					chan = -1; //denote that sound should be replaced.
					break;
				}
			}
			if (chan != -1) {
				return -1; //no free channels.
			}
			else {
				//replace the lower priority sound.
				if (S_sfx[channel[i].sound_id].usefulness >= 0) {
					S_sfx[channel[i].sound_id].usefulness--;
				}

				if (snd_AmbChan == i) {
					snd_AmbChan = -1;
				}
			}
		}
	}

	return i;
}

boolean S_StopSoundID(int sound_id, int priority) {
	int i;
	int lp; //least priority
	int found;

	if (S_sfx[sound_id].numchannels == -1) {
		return true;
	}
	lp = -1; //denote the argument sound_id
	found = 0;
	for (i = 0; i < snd_Channels; i++) {
		if (channel[i].sound_id == sound_id && channel[i].mo) {
			found++; //found one.  Now, should we replace it??
			if (priority >= channel[i].priority) { 
				// if we're gonna kill one, then this'll be it
				lp = i;
				priority = channel[i].priority;
			}
		}
	}
	if (found < S_sfx[sound_id].numchannels) {
		return true;
	}
	else if (lp == -1) {
		return false; // don't replace any sounds
	}
	
	if (S_sfx[channel[lp].sound_id].usefulness >= 0) {
		S_sfx[channel[lp].sound_id].usefulness--;
	}
	channel[lp].mo = 0;

	return true;
}

void S_StartSoundAtVolume(mobj_t* origin, int sound_id, int volume) {
	int chan;

	if (sound_id == 0 || snd_MaxVolume == 0 || volume < 1) {
		return;
	}

	if (origin == 0) {
		origin = players[consoleplayer].mo;
	}

	volume = (volume * (snd_MaxVolume + 1) * 8) >> 7;
	if (volume > snd_MaxVolume) {
		volume = snd_MaxVolume;
	}

	// no priority checking, as ambient sounds would be the LOWEST.
	for (chan = 0; chan < snd_Channels; chan++) {
		if (channel[chan].mo == 0) {
			break;
		}
	}
	if (chan < 0 || chan >= snd_Channels) {
		return;
	}
	
	if (S_sfx[sound_id].lumpnum == 0) {
		S_sfx[sound_id].lumpnum = S_GetSfxLumpNum(&S_sfx[sound_id]);
	}
	if (S_sfx[sound_id].snd_ptr == 0) {
		S_sfx[sound_id].snd_ptr = W_CacheLumpNum(S_sfx[sound_id].lumpnum, PU_SOUND);
	}

	byte* data = (byte*)S_sfx[sound_id].snd_ptr;
	int lumplen = W_LumpLength(S_sfx[sound_id].lumpnum);

	if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
		return;
	}
	int len = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
	if (len > lumplen - 8 || len <= 48) {
		return;
	}
	len -= 32;

	channel[chan].mo = origin;
	channel[chan].sound_id = sound_id;
	channel[chan].priority = 1;
	channel[chan].end = len;
	channel[chan].pos = 0;
	channel[chan].left = volume;
	channel[chan].right = volume;

	S_sfx[sound_id].usefulness++;

}

void S_StartSound(mobj_t* origin, int sound_id) {
	int sep, dist, vol, priority;
	int chan;

	if (sound_id == 0 || snd_MaxVolume == 0) {
		return;
	}
	if (origin == 0) {
		origin = players[consoleplayer].mo;
	}

	if (origin == players[consoleplayer].mo) {
		sep = 128;
		dist = 0;
		vol = snd_MaxVolume;
	}
	else if (!S_AdjustSoundParams(players[consoleplayer].mo, origin, &vol, &sep, &dist)) {
		return;
	}
	priority = S_sfx[sound_id].priority;
	priority *= (10 - (dist / 160));

	if (!S_StopSoundID(sound_id, priority)) {
		return; // other sounds have greater priority
	}

	chan = S_SoundChannel(origin, sound_id, priority);
	if (chan < 0 || chan >= snd_Channels) {
		return;
	}

	if (S_sfx[sound_id].lumpnum == 0) {
		S_sfx[sound_id].lumpnum = S_GetSfxLumpNum(&S_sfx[sound_id]);
	}
	if (S_sfx[sound_id].snd_ptr == 0) {
		S_sfx[sound_id].snd_ptr = W_CacheLumpNum(S_sfx[sound_id].lumpnum, PU_SOUND);
	}

	byte *data = (byte*)S_sfx[sound_id].snd_ptr;
	int lumplen = W_LumpLength(S_sfx[sound_id].lumpnum);

	if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
		return;
	}
	int len = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
	if (len > lumplen - 8 || len <= 48) {
		return;
	}
	len -= 32;

	channel[chan].mo = origin;
	channel[chan].sound_id = sound_id;
	channel[chan].priority = priority;
	channel[chan].end = len;
	channel[chan].pos = 0;
	channel[chan].left = ((254 - sep) * vol) / 127;
	channel[chan].right = (sep*vol) / 127;
	
	if (sound_id >= sfx_wind) {
		snd_AmbChan = chan;
	}
	
	S_sfx[sound_id].usefulness++;
}

void S_StopSound(mobj_t* origin) {
	int i;

	for (i = 0; i < snd_Channels; i++) {
		if (channel[i].mo == origin) {
			if (S_sfx[channel[i].sound_id].usefulness >= 0) {
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].mo = 0;
			if (snd_AmbChan == i) {
				snd_AmbChan = -1;
			}
		}
	}
}

#define	PAINTBUFFER_SIZE	512
typedef struct
{
	int left;
	int right;
} portable_samplepair_t;
portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];

void S_TransferPaintBuffer(int samples) {
	int 	out_idx;
	int 	count;
	int 	out_mask;
	int		*p;
	int		val;
	int		vol = 255;
	byte* outl;
	byte* outr;

	p = (int*)paintbuffer;
	count = samples;
	out_mask = snd_Samples - 1;
	out_idx = snd_PaintedTime & out_mask;


	outl = snd_Buffer_left;
	outr = snd_Buffer_right;
	while (count--) {
		val = (*p * vol) >> 8;
		p += 1;
		if (val > 0x7fff) {
			val = 0x7fff;
		}
		else if (val < (short)0x8000) {
			val = (short)0x8000;
		}
		outl[out_idx] = (val >> 8);

		val = (*p * vol) >> 8;
		p += 1;
		if (val > 0x7fff) {
			val = 0x7fff;
		}
		else if (val < (short)0x8000) {
			val = (short)0x8000;
		}
		outr[out_idx] = (val >> 8);
		out_idx = (out_idx + 1) & out_mask;
	}
	DC_FlushRange(c_snd_Buffer_left, sizeof(snd_Buffer_left));
	DC_FlushRange(c_snd_Buffer_left, sizeof(snd_Buffer_left));

}

void S_PaintChannelFrom8(channel_t* ch, byte* sfx, int count) {
	int 	sample;
	int* lscale, * rscale;
	int		i;

	if (ch->left > 255) {
		ch->left = 255;
	}
	else if (ch->left < 0) {
		ch->left = 0;
	}
	if (ch->right > 255) {
		ch->right = 255;
	}
	else if (ch->right < 0) {
		ch->right = 0;
	}

	lscale = snd_scaletable[ch->left >> 3];
	rscale = snd_scaletable[ch->right >> 3];
	byte *data = sfx + 24 + ch->pos;


	for (i = 0; i < count; i++)
	{
		sample = (int)((unsigned char)(data[i] - 128));
		paintbuffer[i].left += lscale[sample];
		paintbuffer[i].right += rscale[sample];
	}

	ch->pos += count;
}

void S_PaintChannels(int samples) {
	int i, num, samp;
	while (samples > 0) {
		num = samples;
		if (num > PAINTBUFFER_SIZE) {
			num = PAINTBUFFER_SIZE;
		}
		memset(paintbuffer, 0, (num * sizeof(portable_samplepair_t)));

		channel_t* ch = channel;
		for (i = 0; i < snd_Channels; i++, ch++) {
			if (ch->mo == 0) {
				continue;
			}
			if (ch->left <= 0 && ch->right <= 0) {
				continue;
			}
			if (!S_sfx[ch->sound_id].snd_ptr) {
				continue;
			}
			samp = ch->end;
			if (samp > num) {
				samp = num;
			}
			S_PaintChannelFrom8(ch, (byte*)S_sfx[ch->sound_id].snd_ptr, samp);
			ch->end -= samp;
			if (ch->end <= 0) {
				if (S_sfx[channel[i].sound_id].usefulness >= 0) {
					S_sfx[channel[i].sound_id].usefulness--;
				}
				channel[i].mo = 0;
				channel[i].sound_id = 0;
				if (snd_AmbChan == i) {
					snd_AmbChan = -1;
				}
			}
		}
		S_TransferPaintBuffer(num);
		snd_PaintedTime += num;

		samples -= num;
	}
}

void S_Update_(void) {
	unsigned int current = S_SoundTime();
	unsigned int painted = snd_PaintedTime;
	if (current > 0x40000000) {
		current -= 0x40000000;
		painted -= 0x40000000;
	}

	unsigned int endtime = current + (snd_Speed >> 4);
	unsigned int samples = endtime - painted;

	if (samples > (SND_SAMPLES>>1)) {
		samples = (SND_SAMPLES>>1);
	}
	//if (painted < current) {
	//	iprintf("%08x %08x %08x\n", current, painted, samples);
	//}

	S_PaintChannels(samples);

}

void S_UpdateSounds(mobj_t* listener) {
	int i, dist, vol, sep;
	int priority;

	listener = players[consoleplayer].mo;
	if (snd_MaxVolume <= 0) {
		return;
	}
	for (i = 0; i < snd_Channels; i++) {
		if (S_sfx[channel[i].sound_id].usefulness < 0) {
			continue;
		}

		if (channel[i].mo == 0 || channel[i].sound_id == 0 || channel[i].mo == players[consoleplayer].mo) {
			continue;
		}
		else if (!S_AdjustSoundParams(players[consoleplayer].mo, channel[i].mo, &vol, &sep, &dist)) {
			S_StopSound(channel[i].mo);
			continue;
		}
		priority = S_sfx[channel[i].sound_id].priority;
		priority *= (10 - (dist / 160));
		channel[i].priority = priority;
		channel[i].left = ((254 - sep) * vol) / 127;
		channel[i].right = (sep*vol) / 127;
	}
	S_Update_();
}

void S_PauseSound(void) {
}

void S_ResumeSound(void) {
}


void S_SoundCleanup() {
	int i;
	if (snd_NextCleanup < gametic) {
		for (i = 0; i < NUMSFX; i++) {
			if (S_sfx[i].usefulness < 0 && S_sfx[i].snd_ptr) {
				if (lumpcache[S_sfx[i].lumpnum]) {
					if (((memblock_t*)((byte*)(lumpcache[S_sfx[i].lumpnum]) - sizeof(memblock_t)))->id == 0x1d4a11) {
						// taken directly from the Z_ChangeTag macro
						Z_ChangeTag2(lumpcache[S_sfx[i].lumpnum], PU_CACHE);
					}
				}
				S_sfx[i].usefulness = -1;
				S_sfx[i].snd_ptr = 0;
			}
		}
		snd_NextCleanup = gametic + 35; //CLEANUP DEBUG cleans every second
	}
}

void S_SetMaxVolume(boolean fullprocess) {
	int i;
	byte* data = (byte *)W_CacheLumpName("SNDCURVE", PU_CACHE);

	if (!fullprocess) {
		soundCurve[0] = ((*data) * (snd_MaxVolume * 8)) >> 7;
	}
	else {
		for (i = 0; i < MAX_SND_DIST; i++) {
			soundCurve[i] = (*(data + i) * (snd_MaxVolume * 8)) >> 7;
		}
	}
}

void S_InitTimer() {
#ifdef ARM9
	TIMER_DATA(1) = TIMER_FREQ(snd_Speed);// 0x10000 - (0x1000000 / snd_Speed) * 2;
	TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_1;
	TIMER_DATA(2) = 0;
	TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
	TIMER_DATA(3) = 0;
	TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;

	snd_Buffer_left = (byte*)memUncached(c_snd_Buffer_left);
	snd_Buffer_right = (byte*)memUncached(c_snd_Buffer_right);

	memset(snd_Buffer_left, 0, SND_SAMPLES);
	memset(snd_Buffer_right, 0, SND_SAMPLES);

	snd_LeftChannelId = soundPlaySample(c_snd_Buffer_left,
		SoundFormat_8Bit,
		snd_Samples,
		snd_Speed + 10,
		127,
		0,
		true,
		0);
	snd_RightChannelId = soundPlaySample(c_snd_Buffer_right,
		SoundFormat_8Bit,
		snd_Samples,
		snd_Speed + 10,
		127,
		127,
		true,
		0);
	snd_PaintedTime = S_SoundTime();
	//iprintf("started %d %d\n", snd_LeftChannelId, snd_RightChannelId);
#endif

#ifdef WIN32
	snd_Buffer_left = c_snd_Buffer_left;
	snd_Buffer_right = c_snd_Buffer_right;

	memset(snd_Buffer_left, 0, SND_SAMPLES);
	memset(snd_Buffer_right, 0, SND_SAMPLES);
#endif

}

void S_Start(void) {
	int i;

#ifdef ARM9
	if (snd_LeftChannelId >= 0) {
		soundKill(snd_LeftChannelId);
		//iprintf("stopped %d\n", snd_LeftChannelId);
		snd_LeftChannelId = -1;
	}
	if (snd_RightChannelId >= 0) {
		soundKill(snd_RightChannelId);
		//iprintf("stopped %d\n", snd_RightChannelId);
		snd_RightChannelId = -1;
	}
	TIMER_CR(1) = 0;
	TIMER_CR(2) = 0;
	TIMER_CR(3) = 0;
#endif


	//stop all sounds
	for (i = 0; i < snd_Channels; i++) {
		if (channel[i].mo) {
			S_StopSound(channel[i].mo);
		}
	}
	memset(channel, 0, sizeof(channel));
	memset(snd_Buffer_left, 0, SND_SAMPLES);
	memset(snd_Buffer_right, 0, SND_SAMPLES);

	S_StartSong((gameepisode - 1) * 9 + gamemap - 1, true);

	S_InitTimer();
}

void S_ShutDown(void) {

}

void S_InitScaletable(void) {
	int		i, j;

	for (i = 0; i < 32; i++) {
		for (j = 0; j < 256; j++) {
			snd_scaletable[i][j] = ((signed char)j) * i * 8;
		}
	}
}

void S_Init(void) {

#ifdef ARM9
	soundEnable();
#endif

	snd_Channels = MAX_CHANNELS;
	snd_Samples = SND_SAMPLES;
	snd_Speed = 11031;
	snd_AmbChan = -1;
	snd_NextCleanup = 0;
	snd_PaintedTime = 0;
	snd_LeftChannelId = -1;
	snd_RightChannelId = -1;

	S_InitScaletable();

	mus_init_music();

	S_InitTimer();
}
