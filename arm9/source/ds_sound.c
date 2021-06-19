#include "DoomDef.h"
#include "P_local.h"
#include "soundst.h"
#include "map.h"

#ifdef ARM9
#include <maxmod9.h>
#endif

#ifdef WIN32
#define iprintf printf
#endif
void S_Update_(void);
void SND_PaintChannelFrom8(channel_t* ch, byte* sfx, int count);

/*
===============================================================================

		MUSIC & SFX API

===============================================================================
*/
static int		snd_scaletable[32][256];

static channel_t channel[MAX_CHANNELS];

int mus_song = -1;
int mus_lumpnum;

int snd_Samples,
	snd_Speed,
	snd_MaxVolume,      // maximum volume for sound
	snd_MusicVolume;    // maximum volume for music
int AmbChan;
int snd_Channels;

int			soundtime;		// sample PAIRS
int   		paintedtime; 	// sample PAIRS

#define SND_SAMPLES (2048)
byte c_snd_Buffer_left[SND_SAMPLES];
byte c_snd_Buffer_right[SND_SAMPLES];
byte *snd_Buffer_left;
byte *snd_Buffer_right;

byte *soundCurve;

extern sfxinfo_t S_sfx[];
extern musicinfo_t S_music[];

extern angle_t		viewangle;
extern fixed_t		viewx, viewy, viewz;

extern void **lumpcache;


void S_InitScaletable (void)
{
	int		i, j;
	
	for (i=0 ; i<32 ; i++)
		for (j=0 ; j<256 ; j++)
			snd_scaletable[i][j] = ((signed char)j) * i * 8;
}

#define	PAINTBUFFER_SIZE	512
typedef struct
{
	int left;
	int right;
} portable_samplepair_t;
portable_samplepair_t paintbuffer[PAINTBUFFER_SIZE];

#ifdef ARM9

long long ds_sound_start;

long long ds_time()
{
	static u16 last;
	static long long t;
	u16 time1 = TIMER2_DATA;
	u16 time = TIMER3_DATA;
	if(time < last) {
		t += (1<<32);
	}
	last = time;
	return (t + (time << 16) + time1);
}

void on_stream_request_transfer(byte *dest,int len)
{
	int 	out_idx;
	int 	count,count1,count2,pos;
	int 	out_mask;
	int 	*p;
	int 	step;
	int		val;
	int		snd_vol;
	byte *outl;
	byte *outr;
	
	p = (int *) paintbuffer;
	count = len;
	snd_vol = 255;//volume.value*256;


	//outl = snd_Buffer_left;
	//outr = snd_Buffer_right;
	while (count--) {
		val = (*p * snd_vol) >> 8;
		p+= 1;//step;
		if (val > 0x7fff)
			val = 0x7fff;
		else if (val < (short)0x8000)
			val = (short)0x8000;
		*dest++ = (val>>8);// + 128;

		val = (*p * snd_vol) >> 8;
		p+= 1;//step;
		if (val > 0x7fff)
			val = 0x7fff;
		else if (val < (short)0x8000)
			val = (short)0x8000;
		*dest++ = (val>>8);// + 128;
	}

}

//void S_PaintChannels(int endtime)
mm_word on_stream_request( mm_word length, mm_addr pdest, mm_stream_formats format ) {
	int 	i;
	int 	end;
	channel_t *ch;
	//channel_t	*sc;
	int		ltime, count,mixed=0;
	byte *dest = (byte *)pdest;
	int endtime;

	if(length > 1200) return;
	//paintedtime = mmStreamGetPosition();
	endtime = paintedtime + length;

	iprintf("onstream: %d %d %d\n",length,paintedtime,endtime);

	while (paintedtime < endtime)
	{
	// if paintbuffer is smaller than DMA buffer
		end = endtime;
		if (endtime - paintedtime > PAINTBUFFER_SIZE)
			end = paintedtime + PAINTBUFFER_SIZE;

	// clear the paint buffer
		memset(paintbuffer, 0, (end - paintedtime) * sizeof(portable_samplepair_t));

	// paint in the channels.
		ch = channel;
		for (i=0; i<snd_Channels ; i++, ch++)
		{
			//if(mixed) break;
			if (!ch->handle) {
				//iprintf("no handle\n");
				continue;
			}
			if (!ch->mo) {
				iprintf("no mo\n");
				continue;
			}
		/*if(!I_SoundIsPlaying(channel[i].handle))
		{
			if(S_sfx[channel[i].sound_id].usefulness > 0)
			{
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].handle = 0;
			channel[i].mo = NULL;
			channel[i].sound_id = 0;
			if(AmbChan == i)
			{
				AmbChan = -1;
			}
		}*/
			if (!ch->left < 0 && !ch->right)
				continue;
			//sc = S_LoadSound (ch->sfx);
			//if (!sc)
			if(!S_sfx[ch->sound_id].snd_ptr) {
				iprintf("no snd_ptr\n");
				continue;
			}

			ltime = paintedtime;

			while (ltime < end)
			{	// paint up to end
				if (ch->end < end)
					count = ch->end - ltime;
				else
					count = end - ltime;

				if (count > 0)
				{	
					//if (sc->width == 1)
						SND_PaintChannelFrom8(ch, (byte *)S_sfx[ch->sound_id].snd_ptr, count);
					//else
					//	SND_PaintChannelFrom16(ch, sc, count);
					mixed++;
					ltime += count;
				}

			// if at end of loop, restart
				if (ltime >= ch->end)
				{
					if(S_sfx[channel[i].sound_id].usefulness > 0)
					{
						S_sfx[channel[i].sound_id].usefulness--;
					}
					channel[i].handle = 0;
					channel[i].mo = NULL;
					channel[i].sound_id = 0;
					if(AmbChan == i)
					{
						AmbChan = -1;
					}
					break;
				}
			}
															  
		}

	// transfer out according to DMA format
		on_stream_request_transfer(dest,end-paintedtime);
		dest += (end-paintedtime);
		iprintf("sound: %d %d %d\n",mixed,paintedtime,end);
		paintedtime = end;
	}
	printf("end\n");
}
void S_Init(void)
{

	S_InitScaletable();

	snd_Samples = SND_SAMPLES;
	snd_Speed = 11031;
	paintedtime = 0;
#ifndef USE_MAXMOD
	TIMER_DATA(1) = TIMER_FREQ(snd_Speed);// 0x10000 - (0x1000000 / snd_Speed) * 2;
	TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_1;
	TIMER_DATA(2) = 0;
	TIMER_CR(2) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
	TIMER_DATA(3) = 0;
	TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;

	//TIMER_DATA(2) = TIMER_FREQ(snd_Speed);//0x10000 - (0x1000000 / snd_Speed) * 2;
	//TIMER_CR(2) = TIMER_ENABLE | TIMER_DIV_1;
	//TIMER_DATA(3) = 0;
	//TIMER_CR(3) = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;

	snd_Buffer_left  = (byte *) memUncached(c_snd_Buffer_left);
	snd_Buffer_right = (byte *) memUncached(c_snd_Buffer_right);

	memset(snd_Buffer_left,0,SND_SAMPLES);
	memset(snd_Buffer_right,0,SND_SAMPLES);
	soundPlaySample(c_snd_Buffer_left,
		SoundFormat_8Bit,
		snd_Samples,
		snd_Speed+10,
		127,
		0,
		true,
		0);
	soundPlaySample(c_snd_Buffer_right,
		SoundFormat_8Bit,
		snd_Samples,
		snd_Speed+10,
		127,
		127,
		true,
		0);
	ds_sound_start = ds_time();
#else
	//----------------------------------------------------------------
	// initialize maxmod without any soundbank (unusual setup)
	//----------------------------------------------------------------
	mm_ds_system sys;
	sys.mod_count 			= 0;
	sys.samp_count			= 0;
	sys.mem_bank			= 0;
	sys.fifo_channel		= FIFO_MAXMOD;
	mmInit( &sys );
	
	//----------------------------------------------------------------
	// open stream
	//----------------------------------------------------------------
	mm_stream mystream;
	mystream.sampling_rate	= snd_Speed;					// sampling rate = 25khz
	mystream.buffer_length	= 1200;						// buffer length = 1200 samples
	mystream.callback		= on_stream_request;		// set callback function
	mystream.format			= MM_STREAM_8BIT_STEREO;	// format = stereo 8-bit
	mystream.timer			= MM_TIMER2;				// use hardware timer 0
	mystream.manual			= false;						// use manual filling
	mmStreamOpen( &mystream );
#endif
	soundCurve = (byte *)Z_Malloc(MAX_SND_DIST, PU_STATIC, NULL);
	snd_Channels = MAX_CHANNELS;
	//snd_MaxVolume = 127;
	soundEnable();
	mus_init_music();
}
#else
void S_Init(void)
{
	S_InitScaletable();

	snd_Samples = SND_SAMPLES;
	snd_Speed = 11025;
	snd_Buffer_left  = (byte *) (c_snd_Buffer_left);
	snd_Buffer_right = (byte *) (c_snd_Buffer_right);
	snd_Channels = MAX_CHANNELS;
	snd_MaxVolume = 127;
}
#endif

void S_Start(void)
{
	int i;

	S_StartSong((gameepisode-1)*9 + gamemap-1, true);

	//stop all sounds
	for(i=0; i < snd_Channels; i++)
	{
		if(channel[i].handle)
		{
			S_StopSound(channel[i].mo);
		}
	}
	memset(channel, 0, sizeof(channel));
	memset(snd_Buffer_left, 0, SND_SAMPLES);
	memset(snd_Buffer_right, 0, SND_SAMPLES);
	S_Update_();
}

void mus_play_music(char *name);

void S_StartSong(int song, boolean loop)
{
	//printf("song: %d\n",song);
	if(song < mus_e1m1 || song > NUMMUSIC)
	{
		return;
	}
	mus_play_music(S_music[song].name );
}

// Gets lump nums of the named sound.  Returns pointer which will be
// passed to I_StartSound() when you want to start an SFX.  Must be
// sure to pass this to UngetSoundEffect() so that they can be
// freed!


int I_GetSfxLumpNum(sfxinfo_t *sound)
{
  char namebuf[9];

  if(sound->name == 0)
		return 0;
  if (sound->link) sound = sound->link;
//  sprintf(namebuf, "d%c%s", snd_prefixen[snd_SfxDevice], sound->name);
  return W_GetNumForName(sound->name);

}

boolean S_StopSoundID(int sound_id, int priority)
{
	int i;
	int lp; //least priority
	int found;

	if(S_sfx[sound_id].numchannels == -1)
	{
		return(true);
	}
	lp = -1; //denote the argument sound_id
	found = 0;
	for(i=0; i<snd_Channels; i++)
	{
		if(channel[i].sound_id == sound_id && channel[i].mo)
		{
			found++; //found one.  Now, should we replace it??
			if(priority >= channel[i].priority)
			{ // if we're gonna kill one, then this'll be it
				lp = i;
				priority = channel[i].priority;
			}
		}
	}
	if(found < S_sfx[sound_id].numchannels)
	{
		return(true);
	}
	else if(lp == -1)
	{
		return(false); // don't replace any sounds
	}
	if(channel[lp].handle)
	{
		//if(I_SoundIsPlaying(channel[lp].handle))
		//{
		//	I_StopSound(channel[lp].handle);
		//}
		if(S_sfx[channel[i].sound_id].usefulness > 0)
		{
			S_sfx[channel[i].sound_id].usefulness--;
		}
		channel[lp].mo = NULL;
		channel[lp].handle = 0;
	}
	return(true);
}


// when to clip out sounds
// Does not fit the large outdoor areas.

#define S_CLIPPING_DIST (1200 * FRACUNIT)

// Distance tp origin when sounds should be maxed out.
// This should relate to movement clipping resolution
// (see BLOCKMAP handling).
// In the source code release: (160*FRACUNIT).  Changed back to the 
// Vanilla value of 200 (why was this changed?)

#define S_CLOSE_DIST (160 * FRACUNIT)

// The range over which sound attenuates

#define S_ATTENUATOR ((S_CLIPPING_DIST - S_CLOSE_DIST) >> FRACBITS)

// Stereo separation

#define S_STEREO_SWING (96 * FRACUNIT)

#define NORM_PITCH 128
#define NORM_PRIORITY 64
#define NORM_SEP 128

//
// Changes volume and stereo-separation variables
//  from the norm of a sound effect to be played.
// If the sound is not audible, returns a 0.
// Otherwise, modifies parameters and returns 1.
//

static int S_AdjustSoundParams(mobj_t *listener, mobj_t *source,
                               int *vol, int *sep,int *dist)
{
    fixed_t        approx_dist;
    fixed_t        adx;
    fixed_t        ady;
    angle_t        angle;

    // calculate the distance to sound origin
    //  and clip it if necessary
    adx = abs(listener->x - source->x);
    ady = abs(listener->y - source->y);

    // From _GG1_ p.428. Appox. eucledian distance fast.
    approx_dist = adx + ady - ((adx < ady ? adx : ady)>>1);
    
    if (gamemap != 8 && approx_dist > S_CLIPPING_DIST)
    {
        return 0;
    }
    
    // angle of source to listener
    angle = R_PointToAngle2(listener->x,
                            listener->y,
                            source->x,
                            source->y);

    if (angle > listener->angle)
    {
        angle = angle - listener->angle;
    }
    else
    {
        angle = angle + (0xffffffff - listener->angle);
    }

    angle >>= ANGLETOFINESHIFT;

    // stereo separation
    *sep = 128 - (FixedMul(S_STEREO_SWING, finesine[angle]) >> FRACBITS);
	*dist = approx_dist>>FRACBITS;

    // volume calculation
    if (approx_dist < S_CLOSE_DIST)
    {
        *vol = snd_MaxVolume;
    }
    else if (gamemap == 8)
    {
        if (approx_dist > S_CLIPPING_DIST)
        {
            approx_dist = S_CLIPPING_DIST;
        }

        *vol = 15+ ((snd_MaxVolume-15)
                    *((S_CLIPPING_DIST - approx_dist)>>FRACBITS))
            / S_ATTENUATOR;
    }
    else
    {
        // distance effect
        *vol = (snd_MaxVolume
                * ((S_CLIPPING_DIST - approx_dist)>>FRACBITS))
            / S_ATTENUATOR; 
    }
    
    return (*vol > 0);
}

void S_StartSound(mobj_t *origin, int sound_id)
{
	int dist, vol;
	int i;
	int sound;
	int priority;
	int sep;
	int angle;
	int absx;
	int absy;
	int len;
	int lumplen;
	byte *data;

	static int sndcount = 0;
	int chan;

	//iprintf("1\n");
	if(sound_id==0 || snd_MaxVolume == 0)
		return;
	if(origin == NULL)
	{
		origin = players[consoleplayer].mo;
	}

#if 0
// calculate the distance before other stuff so that we can throw out
// sounds that are beyond the hearing range.
	absx = abs(origin->x-players[consoleplayer].mo->x);
	absy = abs(origin->y-players[consoleplayer].mo->y);
	dist = absx+absy-(absx > absy ? absy>>1 : absx>>1);
	dist >>= FRACBITS;
//  dist = P_AproxDistance(origin->x-viewx, origin->y-viewy)>>FRACBITS;

	if(dist >= MAX_SND_DIST)
	{
//      dist = MAX_SND_DIST - 1;
	  return; //sound is beyond the hearing range...
	}
	if(dist < 0)
	{
		dist = 0;
	}
#else
	if(origin == players[consoleplayer].mo)
	{
		sep = 128;
		dist = 0;
		vol = snd_MaxVolume;
	} else if(!S_AdjustSoundParams(players[consoleplayer].mo,origin,&vol,&sep,&dist)) {
		return;
	}
#endif
	priority = S_sfx[sound_id].priority;
	priority *= (10 - (dist/160));
	if(!S_StopSoundID(sound_id, priority))
	{
		return; // other sounds have greater priority
	}
	//iprintf("2\n");
	for(i=0; i<snd_Channels; i++)
	{
		if(origin->player)
		{
			i = snd_Channels;
			break; // let the player have more than one sound.
		}
		if(origin == channel[i].mo)
		{ // only allow other mobjs one sound
			S_StopSound(channel[i].mo);
			break;
		}
	}
	//iprintf("3\n");
	if(i >= snd_Channels)
	{
		if(sound_id >= sfx_wind)
		{
			if(AmbChan != -1 && S_sfx[sound_id].priority <=
				S_sfx[channel[AmbChan].sound_id].priority)
			{
				return; //ambient channel already in use
			}
			else
			{
				AmbChan = -1;
			}
		}
		for(i=0; i<snd_Channels; i++)
		{
			if(channel[i].mo == NULL)
			{
				break;
			}
		}
		if(i >= snd_Channels)
		{
			//look for a lower priority sound to replace.
			sndcount++;
			if(sndcount >= snd_Channels)
			{
				sndcount = 0;
			}
			for(chan=0; chan < snd_Channels; chan++)
			{
				i = (sndcount+chan)%snd_Channels;
				if(priority >= channel[i].priority)
				{
					chan = -1; //denote that sound should be replaced.
					break;
				}
			}
			if(chan != -1)
			{
				return; //no free channels.
			}
			else //replace the lower priority sound.
			{
				if(channel[i].handle)
				{
					//if(I_SoundIsPlaying(channel[i].handle))
					//{
					//	I_StopSound(channel[i].handle);
					//}
					if(S_sfx[channel[i].sound_id].usefulness > 0)
					{
						S_sfx[channel[i].sound_id].usefulness--;
					}

					if(AmbChan == i)
					{
						AmbChan = -1;
					}
				}
			}
		}
	}
	//iprintf("4\n");
	if(S_sfx[sound_id].lumpnum == 0)
	{
		S_sfx[sound_id].lumpnum = I_GetSfxLumpNum(&S_sfx[sound_id]);
	}
	if(S_sfx[sound_id].snd_ptr == NULL)
	{
		S_sfx[sound_id].snd_ptr = W_CacheLumpNum(S_sfx[sound_id].lumpnum,PU_SOUND);
	}

#if 0
	// calculate the volume based upon the distance from the sound origin.
//      vol = (snd_MaxVolume*16 + dist*(-snd_MaxVolume*16)/MAX_SND_DIST)>>9;
	vol = soundCurve[dist];

	if(origin == players[consoleplayer].mo)
	{
		sep = 128;
	}
	else
	{
		angle = R_PointToAngle2(players[consoleplayer].mo->x,
			players[consoleplayer].mo->y, channel[i].mo->x, channel[i].mo->y);
		angle = (angle-viewangle)>>24;
		sep = angle*2-128;
		if(sep < 64)
			sep = -sep;
		if(sep > 192)
			sep = 512-sep;
	}
#endif
	data = (byte *)S_sfx[sound_id].snd_ptr;
	lumplen = W_LumpLength(S_sfx[sound_id].lumpnum);
	if (lumplen < 8 || data[0] != 0x03 || data[1] != 0x00) {
		return;
	}
	len = (data[7] << 24) | (data[6] << 16) | (data[5] << 8) | data[4];
	if (len > lumplen - 8 || len <= 48) {
		return;
	}
	len -= 32;

	channel[i].pitch = (byte)(127+(M_Random()&7)-(M_Random()&7));
	channel[i].handle = 1;//I_StartSound(sound_id, S_sfx[sound_id].snd_ptr, vol, sep, channel[i].pitch, 0);
	channel[i].mo = origin;
	channel[i].sound_id = sound_id;
	channel[i].priority = priority;
	channel[i].end = paintedtime + len;
	channel[i].pos = 0;
	channel[i].left = ((254 - sep) * vol) / 127;
	channel[i].right = ((sep) * vol) / 127;
	//iprintf("play: %d v:%d %d %8s\n",sound_id,channel[i].left,channel[i].right,S_sfx[sound_id].name);
	//for(i=0;i<16;i++) {
	//	iprintf("%2x",data[24+i]);
	//}
	//iprintf("\n");
	if(sound_id >= sfx_wind)
	{
		AmbChan = i;
	}
	if(S_sfx[sound_id].usefulness == -1)
	{
		S_sfx[sound_id].usefulness = 1;
	}
	else
	{
		S_sfx[sound_id].usefulness++;
	}
}

void S_StartSoundAtVolume(mobj_t *origin, int sound_id, int volume)
{
	int dist;
	int i;
	int sep;

	static int sndcount;
	int chan;

	//return;

	if(sound_id == 0 || snd_MaxVolume == 0)
		return;
	if(origin == NULL)
	{
		origin = players[consoleplayer].mo;
	}

	if(volume < 1)
	{
		return;
	}
	volume = (volume*(snd_MaxVolume+1)*8)>>7;
	if(volume > snd_MaxVolume) {
		volume = snd_MaxVolume;
	}

// no priority checking, as ambient sounds would be the LOWEST.
	for(i=0; i<snd_Channels; i++)
	{
		if(channel[i].mo == NULL)
		{
			break;
		}
	}
	if(i >= snd_Channels)
	{
		return;
	}
	if(S_sfx[sound_id].lumpnum == 0)
	{
		S_sfx[sound_id].lumpnum = I_GetSfxLumpNum(&S_sfx[sound_id]);
	}
	if(S_sfx[sound_id].snd_ptr == NULL)
	{
		S_sfx[sound_id].snd_ptr = W_CacheLumpNum(S_sfx[sound_id].lumpnum,
			PU_SOUND);
	}
	channel[i].pitch = (byte)(127-(M_Random()&3)+(M_Random()&3));
	channel[i].handle = 1;//I_StartSound(sound_id, S_sfx[sound_id].snd_ptr, volume, 128, channel[i].pitch, 0);
	channel[i].mo = origin;
	channel[i].sound_id = sound_id;
	channel[i].priority = 1; //super low priority.
	channel[i].left = volume;
	channel[i].right = volume;
	//iprintf("play: %d v:%d %8s\n",sound_id,volume,S_sfx[sound_id].name);
	if(S_sfx[sound_id].usefulness == -1)
	{
		S_sfx[sound_id].usefulness = 1;
	}
	else
	{
		S_sfx[sound_id].usefulness++;
	}
}

void S_StopSound(mobj_t *origin)
{
	int i;

	for(i=0;i<snd_Channels;i++)
	{
		if(channel[i].mo == origin)
		{
			//I_StopSound(channel[i].handle);
			if(S_sfx[channel[i].sound_id].usefulness > 0)
			{
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].handle = 0;
			channel[i].mo = NULL;
			if(AmbChan == i)
			{
				AmbChan = -1;
			}
		}
	}
}

void S_SoundLink(mobj_t *oldactor, mobj_t *newactor)
{
}

void S_PauseSound(void)
{
}

void S_ResumeSound(void)
{
}

static int nextcleanup;

void S_SoundCleanup() {
	int i;
	if (nextcleanup < gametic)
	{
		for (i = 0; i < NUMSFX; i++)
		{
			if (S_sfx[i].usefulness == 0 && S_sfx[i].snd_ptr)
			{
				if (lumpcache[S_sfx[i].lumpnum])
				{
					if (((memblock_t*)((byte*)(lumpcache[S_sfx[i].lumpnum]) -
						sizeof(memblock_t)))->id == 0x1d4a11)
					{ // taken directly from the Z_ChangeTag macro
						Z_ChangeTag2(lumpcache[S_sfx[i].lumpnum], PU_CACHE);
					}
				}
				S_sfx[i].usefulness = -1;
				S_sfx[i].snd_ptr = NULL;
			}
		}
		nextcleanup = gametic + 35; //CLEANUP DEBUG cleans every second
	}
}

void S_UpdateSounds(mobj_t *listener)
{
	int i, dist, vol;
	int angle;
	int sep;
	int priority;
	int absx;
	int absy;

	listener = players[consoleplayer].mo;
	if(snd_MaxVolume == 0)
	{
		return;
	}
	/*if(nextcleanup < gametic)
	{
		for(i=0; i < NUMSFX; i++)
		{
			if(S_sfx[i].usefulness == 0 && S_sfx[i].snd_ptr)
			{
				if(lumpcache[S_sfx[i].lumpnum])
				{
					if(((memblock_t *)((byte *)(lumpcache[S_sfx[i].lumpnum])-
						sizeof(memblock_t)))->id == 0x1d4a11)
					{ // taken directly from the Z_ChangeTag macro
						Z_ChangeTag2(lumpcache[S_sfx[i].lumpnum], PU_CACHE);
					}
				}
				S_sfx[i].usefulness = -1;
				S_sfx[i].snd_ptr = NULL;
			}
		}
		nextcleanup = gametic+35; //CLEANUP DEBUG cleans every second
	}*/
	for(i=0;i<snd_Channels;i++)
	{
		if(!channel[i].handle || S_sfx[channel[i].sound_id].usefulness == -1)
		{
			continue;
		}
		/*if(!I_SoundIsPlaying(channel[i].handle))
		{
			if(S_sfx[channel[i].sound_id].usefulness > 0)
			{
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].handle = 0;
			channel[i].mo = NULL;
			channel[i].sound_id = 0;
			if(AmbChan == i)
			{
				AmbChan = -1;
			}
		}*/
		if(channel[i].mo == NULL || channel[i].sound_id == 0
			|| channel[i].mo == players[consoleplayer].mo)
		{
			continue;
		}
#if 1
		else if(!S_AdjustSoundParams(players[consoleplayer].mo,channel[i].mo,&vol,&sep,&dist)) {
			S_StopSound(channel[i].mo);
			continue;
		}
		priority = S_sfx[channel[i].sound_id].priority;
		priority *= (10 - (dist>>8));
		channel[i].priority = priority;
		channel[i].left = ((254 - sep) * vol) / 127;
		channel[i].right = ((sep) * vol) / 127;
#else
		else
		{
			absx = abs(channel[i].mo->x-players[consoleplayer].mo->x);
			absy = abs(channel[i].mo->y-players[consoleplayer].mo->y);
			dist = absx+absy-(absx > absy ? absy>>1 : absx>>1);
			dist >>= FRACBITS;
//          dist = P_AproxDistance(channel[i].mo->x-listener->x, channel[i].mo->y-listener->y)>>FRACBITS;

			if(dist >= MAX_SND_DIST)
			{
				S_StopSound(channel[i].mo);
				continue;
			}
			if(dist < 0)
				dist = 0;

// calculate the volume based upon the distance from the sound origin.
//          vol = (*((byte *)W_CacheLumpName("SNDCURVE", PU_CACHE)+dist)*(snd_MaxVolume*8))>>7;
			vol = soundCurve[dist];

			angle = R_PointToAngle2(players[consoleplayer].mo->x,
				players[consoleplayer].mo->y, channel[i].mo->x, channel[i].mo->y);
			angle = (angle-viewangle)>>24;
			sep = angle*2-128;
			if(sep < 64)
				sep = -sep;
			if(sep > 192)
				sep = 512-sep;
			//I_UpdateSoundParams(channel[i].handle, vol, sep, channel[i].pitch);
			priority = S_sfx[channel[i].sound_id].priority;
			priority *= (10 - (dist>>8));
			channel[i].priority = priority;
		}
#endif
	}
	S_Update_();
}

void S_GetChannelInfo(SoundInfo_t *s)
{
}

void S_SetMaxVolume(boolean fullprocess)
{
	int i;

	if(!fullprocess)
	{
		soundCurve[0] = (*((byte *)W_CacheLumpName("SNDCURVE", PU_CACHE))*(snd_MaxVolume*8))>>7;
	}
	else
	{
		for(i = 0; i < MAX_SND_DIST; i++)
		{
			soundCurve[i] = (*((byte *)W_CacheLumpName("SNDCURVE", PU_CACHE)+i)*(snd_MaxVolume*8))>>7;
		}
	}
}

void mus_update_volume();

void S_SetMusicVolume(void) {
	mus_update_volume();
}

void S_ShutDown(void)
{
}


int SND_SamplePos() {
#ifdef ARM9
	static long long v;

	v = (ds_time() - ds_sound_start);
	
	return (int)v;
#else
	return 0;
#endif
}

void GetSoundtime(void)
{
	soundtime = SND_SamplePos();
}


void S_TransferPaintBuffer(int endtime)
{
	int 	out_idx;
	int 	count,count1,count2,pos;
	int 	out_mask;
	int 	*p;
	int 	step;
	int		val;
	int		snd_vol;
	byte *outl;
	byte *outr;
	
	p = (int *) paintbuffer;
	count = (endtime - paintedtime);// * shm->channels;
	out_mask = snd_Samples - 1; 
	out_idx = paintedtime & out_mask;
	step = 3 - 1;//shm->channels;
	snd_vol = 255;//volume.value*256;


	outl = snd_Buffer_left;
	outr = snd_Buffer_right;
	while (count--)
	{
		val = (*p * snd_vol) >> 8;
		p+= 1;//step;
		if (val > 0x7fff)
			val = 0x7fff;
		else if (val < (short)0x8000)
			val = (short)0x8000;
		outl[out_idx] = (val>>8);// + 128;

		val = (*p * snd_vol) >> 8;
		p+= 1;//step;
		if (val > 0x7fff)
			val = 0x7fff;
		else if (val < (short)0x8000)
			val = (short)0x8000;
		outr[out_idx] = (val>>8);// + 128;
		out_idx = (out_idx + 1) & out_mask;
	}

}

void SND_PaintChannelFrom8__ (channel_t *ch,  byte *sfx, int count)
{
	int data;
	int left, right;
	int leftvol, rightvol;
	int	i;

	leftvol = ch->left;
	rightvol = ch->right;
	sfx = sfx + 24 + ch->pos;

	for (i=0 ; i<count ; i++)
	{
		data = sfx[i]-128;
		left = (data * leftvol) >> 8;
		right = (data * rightvol) >> 8;
		paintbuffer[i].left += left;
		paintbuffer[i].right += right;
	}

	ch->pos += count;
}

void SND_PaintChannelFrom8 (channel_t *ch, byte *sfx, int count)
{
	int 	data;
	int		*lscale, *rscale;
	int		i;

	if (ch->left > 255)
		ch->left = 255;
	else if (ch->left < 0)
		ch->left = 0;
	if (ch->right > 255)
		ch->right = 255;
	else if (ch->right < 0)
		ch->right = 0;
		
	lscale = snd_scaletable[ch->left >> 3];
	rscale = snd_scaletable[ch->right >> 3];
	sfx = sfx + 24 + ch->pos;

	/*if(ch->left < 0) {
		iprintf("================\n%s: %d\n",S_sfx[ch->sound_id].name,ch->left);
	}
	if(ch->right < 0) {
		iprintf("================\n%s: %d\n",S_sfx[ch->sound_id].name,ch->right);
	}

	if(ch->left < 8) {
		iprintf("left(%d): ",ch->left);
		for (i=0 ; i<32 ; i++)
			iprintf("%d ",lscale[i]);
		iprintf("\n");
	}
	if(ch->right < 8) {
		iprintf("right(%d): ",ch->right);
		for (i=0 ; i<32 ; i++)
			iprintf("%d ",rscale[i]);
		iprintf("\n");
	}*/

	for (i=0 ; i<count ; i++)
	{
		//data = ((int)sfx[i]) - 128;
		data = (int)( (unsigned char)(sfx[i] - 128));
		/*if(data < 0 || data > 255) {
			printf("\n%s: (%d %d) %02x %d\n",
				S_sfx[ch->sound_id].name,
				ch->left,
				ch->right,
				sfx[i],data);
		}*/
		//data = (int)(sfx[i]-128);
		paintbuffer[i].left += lscale[data];
		paintbuffer[i].right += rscale[data];
		/*if(ch->left < 8 && i < 16) {
			iprintf("left(%d): %d %d\n",ch->left,lscale[data],paintbuffer[i].left);
		}
		if(ch->right < 8) {
			iprintf("right(%d): %d %d\n",ch->right,rscale[data],paintbuffer[i].right);
		}*/
	}
	
	ch->pos += count;
}

void S_PaintChannels(int endtime)
{
	int 	i;
	int 	end;
	channel_t *ch;
	//channel_t	*sc;
	int		ltime, count,mixed=0;

	while (paintedtime < endtime)
	{
	// if paintbuffer is smaller than DMA buffer
		end = endtime;
		if (endtime - paintedtime > PAINTBUFFER_SIZE)
			end = paintedtime + PAINTBUFFER_SIZE;

	// clear the paint buffer
		memset(paintbuffer, 0, (end - paintedtime) * sizeof(portable_samplepair_t));

	// paint in the channels.
		ch = channel;
		for (i=0; i<snd_Channels ; i++, ch++)
		{
			//if(mixed) break;
			if (!ch->handle) {
				//iprintf("no handle\n");
				continue;
			}
			if (!ch->mo) {
				iprintf("no mo\n");
				continue;
			}
		/*if(!I_SoundIsPlaying(channel[i].handle))
		{
			if(S_sfx[channel[i].sound_id].usefulness > 0)
			{
				S_sfx[channel[i].sound_id].usefulness--;
			}
			channel[i].handle = 0;
			channel[i].mo = NULL;
			channel[i].sound_id = 0;
			if(AmbChan == i)
			{
				AmbChan = -1;
			}
		}*/
			if (ch->left <= 0 && ch->right <= 0)
				continue;
			//sc = S_LoadSound (ch->sfx);
			//if (!sc)
			if(!S_sfx[ch->sound_id].snd_ptr) {
				iprintf("no snd_ptr\n");
				continue;
			}

			ltime = paintedtime;

			while (ltime < end)
			{	// paint up to end
				if (ch->end < end)
					count = ch->end - ltime;
				else
					count = end - ltime;

				if (count > 0)
				{	
					//if (sc->width == 1)
						SND_PaintChannelFrom8(ch, (byte *)S_sfx[ch->sound_id].snd_ptr, count);
					//else
					//	SND_PaintChannelFrom16(ch, sc, count);
					mixed++;
					ltime += count;
				}

			// if at end of loop, restart
				if (ltime >= ch->end)
				{
					if(S_sfx[channel[i].sound_id].usefulness > 0)
					{
						S_sfx[channel[i].sound_id].usefulness--;
					}
					channel[i].handle = 0;
					channel[i].mo = NULL;
					channel[i].sound_id = 0;
					if(AmbChan == i)
					{
						AmbChan = -1;
					}
					break;
				}
			}
															  
		}

	// transfer out according to DMA format
		S_TransferPaintBuffer(end);
		//Con_Printf("sound: %d %d %d\n",mixed,paintedtime,end);
		paintedtime = end;
	}
}


void S_Update_(void)
{
#ifdef USE_MAXMOD
	//paintedtime = mmStreamGetPosition();
	//mmStreamUpdate();
#else
	unsigned        endtime;
	int				samps;
	

// Updates DMA time
	GetSoundtime();

// check to make sure that we haven't overshot
	if (paintedtime < soundtime)
	{
		//iprintf ("S_Update_ : overflow\n");
		paintedtime = soundtime;
	}

// mix ahead of current position
	endtime = soundtime + snd_Speed/10;//_snd_mixahead.value * shm->speed;
	samps = snd_Samples;//shm->samples >> (shm->channels-1);
	if (endtime - soundtime > samps)
		endtime = soundtime + samps;

	//iprintf("paint: %d\n",endtime);
	S_PaintChannels (endtime);
#endif
}
