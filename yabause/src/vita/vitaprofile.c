#include "vitaprofile.h"

#ifdef VITA_PROFILE
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

#define PROFILE_PATH "ux0:data/yabause/profile.log"
#define PROFILE_INTERVAL 300

typedef struct {
   unsigned long long started;
   unsigned long long total;
   unsigned int calls;
} ProfileCounter;

static ProfileCounter counters[VITA_PROFILE_COUNT];
static unsigned int frames;

static void flush_profile(void)
{
   FILE *file;
   if (!frames)
      return;

   file = fopen(PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "frames=%u frame_avg_us=%llu present_avg_us=%llu "
              "audio_avg_us=%llu audio_calls=%u\n",
              frames,
              counters[VITA_PROFILE_FRAME].calls
                 ? counters[VITA_PROFILE_FRAME].total /
                      counters[VITA_PROFILE_FRAME].calls : 0,
              counters[VITA_PROFILE_PRESENT].calls
                 ? counters[VITA_PROFILE_PRESENT].total /
                      counters[VITA_PROFILE_PRESENT].calls : 0,
              counters[VITA_PROFILE_AUDIO].calls
                 ? counters[VITA_PROFILE_AUDIO].total /
                      counters[VITA_PROFILE_AUDIO].calls : 0,
              counters[VITA_PROFILE_AUDIO].calls);
      fclose(file);
   }
   memset(counters, 0, sizeof(counters));
   frames = 0;
}
#endif

void VitaProfileInit(void)
{
#ifdef VITA_PROFILE
   FILE *file;
   memset(counters, 0, sizeof(counters));
   frames = 0;
   file = fopen(PROFILE_PATH, "w");
   if (file) {
      fputs("# Yabause Vita aggregated timings (microseconds)\n", file);
      fclose(file);
   }
#endif
}

void VitaProfileBegin(VitaProfileSection section)
{
#ifdef VITA_PROFILE
   if ((unsigned int)section < VITA_PROFILE_COUNT)
      counters[section].started = sceKernelGetProcessTimeWide();
#else
   (void)section;
#endif
}

void VitaProfileEnd(VitaProfileSection section)
{
#ifdef VITA_PROFILE
   unsigned long long now;
   if ((unsigned int)section >= VITA_PROFILE_COUNT ||
       !counters[section].started)
      return;
   now = sceKernelGetProcessTimeWide();
   counters[section].total += now - counters[section].started;
   counters[section].calls++;
   counters[section].started = 0;
#else
   (void)section;
#endif
}

void VitaProfileFrameComplete(void)
{
#ifdef VITA_PROFILE
   if (++frames >= PROFILE_INTERVAL)
      flush_profile();
#endif
}

void VitaProfileShutdown(void)
{
#ifdef VITA_PROFILE
   flush_profile();
#endif
}
