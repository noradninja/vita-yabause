#include "vitahang.h"

#ifdef VITA_HANG_DIAGNOSTICS
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <stdio.h>
#include <string.h>

#define HANG_LOG_PATH "ux0:data/yabause/hang.log"
#define HANG_POLL_US 250000
#define HANG_FIRST_REPORT_US 2000000ULL
#define HANG_REPEAT_REPORT_US 5000000ULL

typedef struct {
   volatile unsigned int sequence;
   unsigned int frame;
   unsigned int stage;
   unsigned int detail[4];
   unsigned long long stage_started;
   unsigned long long frame_completed;
} VitaHangState;

typedef struct {
   unsigned int frame;
   unsigned int stage;
   unsigned int detail[4];
   unsigned long long stage_started;
   unsigned long long frame_completed;
} VitaHangSnapshot;

static VitaHangState hang_state;
static volatile int hang_running;
static SceUID hang_thread = -1;

static unsigned long long hang_now(void)
{
   return sceKernelGetProcessTimeWide();
}

static const char *hang_stage_name(unsigned int stage)
{
   static const char *names[] = {
      "idle", "frame", "msh2", "ssh2", "scu", "smpc", "cd", "m68k",
      "hblank_in", "hblank_out", "vblank_in", "vblank_out",
      "vdp1_decode", "vdp1_draw", "vdp2_process", "atlas_sync",
      "atlas_upload", "gpu_draw", "composition", "present", "events"
   };
   return stage < sizeof(names) / sizeof(names[0]) ? names[stage] : "unknown";
}

static void hang_write(const char *text, int truncate)
{
   int flags = SCE_O_WRONLY | SCE_O_CREAT |
      (truncate ? SCE_O_TRUNC : SCE_O_APPEND);
   SceUID file = sceIoOpen(HANG_LOG_PATH, flags, 0777);
   if (file >= 0) {
      sceIoWrite(file, text, strlen(text));
      sceIoClose(file);
   }
}

static int hang_read_snapshot(VitaHangSnapshot *snapshot)
{
   unsigned int before;
   unsigned int after;
   int attempts;

   for (attempts = 0; attempts < 8; attempts++) {
      before = hang_state.sequence;
      __sync_synchronize();
      if (before & 1)
         continue;
      snapshot->frame = hang_state.frame;
      snapshot->stage = hang_state.stage;
      snapshot->detail[0] = hang_state.detail[0];
      snapshot->detail[1] = hang_state.detail[1];
      snapshot->detail[2] = hang_state.detail[2];
      snapshot->detail[3] = hang_state.detail[3];
      snapshot->stage_started = hang_state.stage_started;
      snapshot->frame_completed = hang_state.frame_completed;
      __sync_synchronize();
      after = hang_state.sequence;
      if (before == after && !(after & 1))
         return 1;
   }
   return 0;
}

static int hang_watchdog(SceSize args, void *argp)
{
   unsigned long long last_report = 0;
   (void)args;
   (void)argp;

   while (hang_running) {
      VitaHangSnapshot snapshot;
      unsigned long long now;
      sceKernelDelayThread(HANG_POLL_US);
      if (!hang_running || !hang_read_snapshot(&snapshot))
         continue;
      now = hang_now();
      if (now - snapshot.frame_completed < HANG_FIRST_REPORT_US) {
         last_report = 0;
         continue;
      }
      if (!last_report || now - last_report >= HANG_REPEAT_REPORT_US) {
         char line[320];
         snprintf(line, sizeof(line),
                  "frame=%u stalled_us=%llu stage=%s stage_us=%llu "
                  "detail0=%u detail1=%u detail2=%u detail3=%u\n",
                  snapshot.frame,
                  now - snapshot.frame_completed,
                  hang_stage_name(snapshot.stage),
                  now - snapshot.stage_started,
                  snapshot.detail[0], snapshot.detail[1],
                  snapshot.detail[2], snapshot.detail[3]);
         hang_write(line, 0);
         last_report = now;
      }
   }
   return 0;
}

void VitaHangSetStage(VitaHangStage stage,
                      unsigned int detail0, unsigned int detail1,
                      unsigned int detail2, unsigned int detail3)
{
   hang_state.sequence++;
   __sync_synchronize();
   hang_state.stage = (unsigned int)stage;
   hang_state.detail[0] = detail0;
   hang_state.detail[1] = detail1;
   hang_state.detail[2] = detail2;
   hang_state.detail[3] = detail3;
   hang_state.stage_started = hang_now();
   __sync_synchronize();
   hang_state.sequence++;
}

int VitaHangInit(void)
{
   unsigned long long now = hang_now();
   memset(&hang_state, 0, sizeof(hang_state));
   hang_state.stage_started = now;
   hang_state.frame_completed = now;
   hang_write("Vita hang diagnostics started\n", 1);
   hang_running = 1;
   hang_thread = sceKernelCreateThread(
      "Yabause hang watchdog", hang_watchdog, 0x10000100, 0x4000, 0, 0, NULL);
   if (hang_thread < 0 || sceKernelStartThread(hang_thread, 0, NULL) < 0) {
      hang_running = 0;
      if (hang_thread >= 0) {
         sceKernelDeleteThread(hang_thread);
         hang_thread = -1;
      }
      hang_write("watchdog thread creation failed\n", 0);
      return -1;
   }
   return 0;
}

void VitaHangShutdown(void)
{
   if (hang_thread < 0)
      return;
   hang_running = 0;
   sceKernelWaitThreadEnd(hang_thread, NULL, NULL);
   sceKernelDeleteThread(hang_thread);
   hang_thread = -1;
}

void VitaHangFrameBegin(void)
{
   hang_state.sequence++;
   __sync_synchronize();
   hang_state.frame++;
   hang_state.stage = VITA_HANG_STAGE_FRAME;
   hang_state.detail[0] = 0;
   hang_state.detail[1] = 0;
   hang_state.detail[2] = 0;
   hang_state.detail[3] = 0;
   hang_state.stage_started = hang_now();
   __sync_synchronize();
   hang_state.sequence++;
}

void VitaHangFrameComplete(void)
{
   unsigned long long now = hang_now();
   hang_state.sequence++;
   __sync_synchronize();
   hang_state.stage = VITA_HANG_STAGE_IDLE;
   hang_state.stage_started = now;
   hang_state.frame_completed = now;
   __sync_synchronize();
   hang_state.sequence++;
}
#endif
