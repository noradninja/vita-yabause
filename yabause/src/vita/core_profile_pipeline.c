#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>

#include "../core.h"
#include "../cs2.h"
#include "../scu.h"
#include "../sh2core.h"
#include "../smpc.h"
#include "../vdp2.h"

#define CORE_PROFILE_PATH "ux0:data/yabause/profile.log"
#define CORE_PROFILE_INTERVAL 300U
#define CORE_PROFILE_SAMPLE_PERIOD 30U

typedef struct {
   unsigned long long exec_us;
   unsigned long long sampled_exec_us;
   unsigned long long msh2_us;
   unsigned long long ssh2_us;
   unsigned long long sh1_us;
   unsigned long long scu_us;
   unsigned long long smpc_us;
   unsigned long long cd_us;
   unsigned long long hblank_in_us;
   unsigned long long hblank_out_us;
   unsigned long long vblank_in_us;
   unsigned long long vblank_out_us;
   unsigned long long intback_us;
   unsigned long long msh2_calls;
   unsigned long long ssh2_calls;
   unsigned long long sh1_calls;
   unsigned long long scu_calls;
   unsigned long long smpc_calls;
   unsigned long long cd_calls;
   unsigned long long hblank_in_calls;
   unsigned long long hblank_out_calls;
   unsigned long long vblank_in_calls;
   unsigned long long vblank_out_calls;
   unsigned long long intback_calls;
   unsigned int frames;
   unsigned int samples;
} VitaCoreProfileCounter;

static VitaCoreProfileCounter core_profile;
static int core_profile_sample_active;

static unsigned long long sampled_average(unsigned long long value)
{
   return core_profile.samples ? value / core_profile.samples : 0;
}

static unsigned long long sampled_calls_x100(unsigned long long value)
{
   return core_profile.samples ? value * 100ULL / core_profile.samples : 0;
}

static void core_profile_flush(void)
{
   FILE *file;
   unsigned long long known_us;
   unsigned long long residual_us;

   if (!core_profile.frames)
      return;

   known_us = core_profile.msh2_us + core_profile.ssh2_us +
              core_profile.sh1_us + core_profile.scu_us +
              core_profile.smpc_us + core_profile.cd_us +
              core_profile.hblank_in_us + core_profile.hblank_out_us +
              core_profile.vblank_in_us + core_profile.vblank_out_us +
              core_profile.intback_us;
   residual_us = core_profile.sampled_exec_us > known_us ?
                 core_profile.sampled_exec_us - known_us : 0;

   file = fopen(CORE_PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "core_profile frames=%u samples=%u sample_period=%u "
              "exec_avg_us=%llu sampled_exec_avg_us=%llu residual_avg_us=%llu "
              "msh2_avg_us=%llu msh2_calls_avg_x100=%llu "
              "ssh2_avg_us=%llu ssh2_calls_avg_x100=%llu "
              "sh1_avg_us=%llu sh1_calls_avg_x100=%llu "
              "scu_avg_us=%llu scu_calls_avg_x100=%llu "
              "smpc_avg_us=%llu smpc_calls_avg_x100=%llu "
              "cd_avg_us=%llu cd_calls_avg_x100=%llu "
              "hblank_in_avg_us=%llu hblank_in_calls_avg_x100=%llu "
              "hblank_out_avg_us=%llu hblank_out_calls_avg_x100=%llu "
              "vblank_in_avg_us=%llu vblank_in_calls_avg_x100=%llu "
              "vblank_out_avg_us=%llu vblank_out_calls_avg_x100=%llu "
              "intback_avg_us=%llu intback_calls_avg_x100=%llu\n",
              core_profile.frames,
              core_profile.samples,
              (unsigned int)CORE_PROFILE_SAMPLE_PERIOD,
              core_profile.exec_us / core_profile.frames,
              sampled_average(core_profile.sampled_exec_us),
              sampled_average(residual_us),
              sampled_average(core_profile.msh2_us),
              sampled_calls_x100(core_profile.msh2_calls),
              sampled_average(core_profile.ssh2_us),
              sampled_calls_x100(core_profile.ssh2_calls),
              sampled_average(core_profile.sh1_us),
              sampled_calls_x100(core_profile.sh1_calls),
              sampled_average(core_profile.scu_us),
              sampled_calls_x100(core_profile.scu_calls),
              sampled_average(core_profile.smpc_us),
              sampled_calls_x100(core_profile.smpc_calls),
              sampled_average(core_profile.cd_us),
              sampled_calls_x100(core_profile.cd_calls),
              sampled_average(core_profile.hblank_in_us),
              sampled_calls_x100(core_profile.hblank_in_calls),
              sampled_average(core_profile.hblank_out_us),
              sampled_calls_x100(core_profile.hblank_out_calls),
              sampled_average(core_profile.vblank_in_us),
              sampled_calls_x100(core_profile.vblank_in_calls),
              sampled_average(core_profile.vblank_out_us),
              sampled_calls_x100(core_profile.vblank_out_calls),
              sampled_average(core_profile.intback_us),
              sampled_calls_x100(core_profile.intback_calls));
      fclose(file);
   }

   memset(&core_profile, 0, sizeof(core_profile));
}

extern int __real_YabauseExec(void);
int __wrap_YabauseExec(void)
{
   unsigned long long started = sceKernelGetProcessTimeWide();
   unsigned long long elapsed;
   int result;

   core_profile_sample_active =
      ((core_profile.frames % CORE_PROFILE_SAMPLE_PERIOD) == 0U);

   result = __real_YabauseExec();
   elapsed = sceKernelGetProcessTimeWide() - started;
   core_profile.exec_us += elapsed;
   if (core_profile_sample_active) {
      core_profile.sampled_exec_us += elapsed;
      core_profile.samples++;
   }

   core_profile.frames++;
   core_profile_sample_active = 0;
   if (core_profile.frames >= CORE_PROFILE_INTERVAL)
      core_profile_flush();
   return result;
}

#define PROFILE_VOID_U32(name, us_member, calls_member)                       \
   extern void __real_##name(u32 value);                                      \
   void __wrap_##name(u32 value)                                               \
   {                                                                            \
      unsigned long long started;                                               \
      if (!core_profile_sample_active) {                                        \
         __real_##name(value);                                                  \
         return;                                                                \
      }                                                                         \
      started = sceKernelGetProcessTimeWide();                                  \
      __real_##name(value);                                                     \
      core_profile.us_member += sceKernelGetProcessTimeWide() - started;         \
      core_profile.calls_member++;                                              \
   }

PROFILE_VOID_U32(ScuExec, scu_us, scu_calls)
PROFILE_VOID_U32(Cs2Exec, cd_us, cd_calls)

extern void __real_SmpcExec(s32 value);
void __wrap_SmpcExec(s32 value)
{
   unsigned long long started;
   if (!core_profile_sample_active) {
      __real_SmpcExec(value);
      return;
   }
   started = sceKernelGetProcessTimeWide();
   __real_SmpcExec(value);
   core_profile.smpc_us += sceKernelGetProcessTimeWide() - started;
   core_profile.smpc_calls++;
}

extern void FASTCALL __real_SH2Exec(SH2_struct *context, u32 cycles);
void FASTCALL __wrap_SH2Exec(SH2_struct *context, u32 cycles)
{
   unsigned long long started;
   unsigned long long elapsed;

   if (!core_profile_sample_active) {
      __real_SH2Exec(context, cycles);
      return;
   }

   started = sceKernelGetProcessTimeWide();
   __real_SH2Exec(context, cycles);
   elapsed = sceKernelGetProcessTimeWide() - started;

   if (context == MSH2) {
      core_profile.msh2_us += elapsed;
      core_profile.msh2_calls++;
   }
   else if (context == SSH2) {
      core_profile.ssh2_us += elapsed;
      core_profile.ssh2_calls++;
   }
   else if (context == SH1) {
      core_profile.sh1_us += elapsed;
      core_profile.sh1_calls++;
   }
}

#define PROFILE_VOID0(name, us_member, calls_member)                          \
   extern void __real_##name(void);                                            \
   void __wrap_##name(void)                                                     \
   {                                                                            \
      unsigned long long started;                                               \
      if (!core_profile_sample_active) {                                        \
         __real_##name();                                                       \
         return;                                                                \
      }                                                                         \
      started = sceKernelGetProcessTimeWide();                                  \
      __real_##name();                                                          \
      core_profile.us_member += sceKernelGetProcessTimeWide() - started;         \
      core_profile.calls_member++;                                              \
   }

PROFILE_VOID0(Vdp2HBlankIN, hblank_in_us, hblank_in_calls)
PROFILE_VOID0(Vdp2HBlankOUT, hblank_out_us, hblank_out_calls)
PROFILE_VOID0(Vdp2VBlankIN, vblank_in_us, vblank_in_calls)
PROFILE_VOID0(Vdp2VBlankOUT, vblank_out_us, vblank_out_calls)
PROFILE_VOID0(SmpcINTBACKEnd, intback_us, intback_calls)
