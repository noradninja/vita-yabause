#include "sndvita.h"
#include "vitaprofile.h"

#include <psp2/audioout.h>
#include <string.h>

#define VITA_AUDIO_RATE 44100
#define VITA_AUDIO_FRAMES 512

static int audio_port = -1;
static int muted;
static int volume = 100;
static unsigned int pending_frames;
static short audio_buffer[VITA_AUDIO_FRAMES * 2] __attribute__((aligned(64)));

static short convert_sample(s32 sample)
{
   sample = (sample * volume) / 100;
   if (sample > 32767)
      return 32767;
   if (sample < -32768)
      return -32768;
   return (short)sample;
}

static int SNDVitaInit(void)
{
   audio_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                    VITA_AUDIO_FRAMES,
                                    VITA_AUDIO_RATE,
                                    SCE_AUDIO_OUT_MODE_STEREO);
   muted = 0;
   volume = 100;
   pending_frames = 0;
   memset(audio_buffer, 0, sizeof(audio_buffer));
   return audio_port < 0 ? -1 : 0;
}

static void SNDVitaDeInit(void)
{
   if (audio_port >= 0) {
      if (pending_frames) {
         memset(&audio_buffer[pending_frames * 2], 0,
                (VITA_AUDIO_FRAMES - pending_frames) * 2 * sizeof(short));
         sceAudioOutOutput(audio_port, audio_buffer);
      }
      sceAudioOutReleasePort(audio_port);
   }
   audio_port = -1;
   pending_frames = 0;
}

static int SNDVitaReset(void)
{
   pending_frames = 0;
   memset(audio_buffer, 0, sizeof(audio_buffer));
   return 0;
}

static int SNDVitaChangeVideoFormat(int vertfreq)
{
   return 0;
}

static void SNDVitaUpdateAudio(u32 *left, u32 *right, u32 num_samples)
{
   u32 copied = 0;

   if (audio_port < 0)
      return;

   VitaProfileBegin(VITA_PROFILE_AUDIO);
   while (copied < num_samples) {
      unsigned int available = VITA_AUDIO_FRAMES - pending_frames;
      unsigned int count = num_samples - copied;
      unsigned int i;

      if (count > available)
         count = available;

      for (i = 0; i < count; ++i) {
         unsigned int dst = (pending_frames + i) * 2;
         if (muted) {
            audio_buffer[dst] = 0;
            audio_buffer[dst + 1] = 0;
         } else {
            audio_buffer[dst] = convert_sample((s32)left[copied + i]);
            audio_buffer[dst + 1] = convert_sample((s32)right[copied + i]);
         }
      }

      pending_frames += count;
      copied += count;

      if (pending_frames == VITA_AUDIO_FRAMES) {
         sceAudioOutOutput(audio_port, audio_buffer);
         pending_frames = 0;
      }
   }
   VitaProfileEnd(VITA_PROFILE_AUDIO);
}

static u32 SNDVitaGetAudioSpace(void)
{
   return audio_port < 0 ? 0 : VITA_AUDIO_FRAMES - pending_frames;
}

static void SNDVitaMuteAudio(void)
{
   muted = 1;
}

static void SNDVitaUnMuteAudio(void)
{
   muted = 0;
}

static void SNDVitaSetVolume(int new_volume)
{
   if (new_volume < 0)
      new_volume = 0;
   if (new_volume > 100)
      new_volume = 100;
   volume = new_volume;
}

SoundInterface_struct SNDVita = {
   SNDCORE_VITA,
   "PS Vita Audio",
   SNDVitaInit,
   SNDVitaDeInit,
   SNDVitaReset,
   SNDVitaChangeVideoFormat,
   SNDVitaUpdateAudio,
   SNDVitaGetAudioSpace,
   SNDVitaMuteAudio,
   SNDVitaUnMuteAudio,
   SNDVitaSetVolume
};
