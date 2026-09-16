#include "vita_scsp_state_diag.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stddef.h>
#include <stdio.h>

#include "scsp.h"

#ifdef VITA_SH2_DYNAREC_RUNTIME
#include "vita_dynarec_vm.h"

extern int master_pc;
extern int slave_pc;
extern void *master_ip;
extern void *slave_ip;
#endif

#define VITA_SCSP_DIAG_MAGIC 0x53434447u /* "SCDG" */
#define VITA_SCSP_DIAG_VERSION 2u
#define VITA_SCSP_DIAG_RECORD_COUNT 32u
#define VITA_SCSP_DIAG_LOG_PATH "ux0:data/yabause/dynarec-runtime.log"

typedef struct VitaScspStateRecord {
   uint32_t sequence;
   uint32_t checkpoint;
   uint32_t frame;
   uint32_t master_pc;
   uint32_t master_ip;
   uint32_t host_address;
   uint32_t slave_pc;
   uint32_t slave_ip;
   uint32_t current;
   uint32_t expected;
   uint32_t slot;
   uint32_t get_audio_space;
   uint32_t expected_get_audio_space;
   uint32_t update_audio;
   uint32_t expected_update_audio;
   uint32_t callback_thumb_bits;
   uint32_t write_depth;
   uint32_t runtime_patches;
   uint32_t invalidation_publications;
   uint32_t implicit_patch;
} VitaScspStateRecord;

/* Keep this layout synchronized with vita_scsp_callback_diag.S.  It lives
 * inside the main recorder so a crash dump contains the complete before/after
 * callback boundary without depending on log I/O. */
typedef struct VitaScspCallbackTrace {
   uint32_t target;
   uint32_t expected_target;
   uint32_t update_target;
   uint32_t expected_update_target;
   uint32_t thumb_bits;
   uint32_t return_value;
   uint32_t before_regs[8];
   uint32_t before_sp;
   uint32_t before_lr;
   uint32_t expected_return;
   uint32_t after_regs[8];
   uint32_t after_sp;
   uint32_t after_lr;
} VitaScspCallbackTrace;

typedef struct VitaScspStateRecorder {
   uint32_t magic;
   uint32_t version;
   uint32_t sequence;
   uint32_t last_checkpoint;
   uint32_t failure_checkpoint;
   uint32_t failure_site;
   uint32_t failure_current;
   uint32_t failure_expected;
   uint32_t failure_slot;
   uint32_t failure_detail;
   VitaScspCallbackTrace callback;
   VitaScspStateRecord records[VITA_SCSP_DIAG_RECORD_COUNT];
} VitaScspStateRecorder;

typedef char VitaScspRecorderCallbackOffsetMustBe40[
   offsetof(VitaScspStateRecorder, callback) == 40 ? 1 : -1];
typedef char VitaScspTraceBeforeRegsOffsetMustBe24[
   offsetof(VitaScspCallbackTrace, before_regs) == 24 ? 1 : -1];
typedef char VitaScspTraceBeforeSpOffsetMustBe56[
   offsetof(VitaScspCallbackTrace, before_sp) == 56 ? 1 : -1];
typedef char VitaScspTraceAfterRegsOffsetMustBe68[
   offsetof(VitaScspCallbackTrace, after_regs) == 68 ? 1 : -1];
typedef char VitaScspTraceAfterSpOffsetMustBe100[
   offsetof(VitaScspCallbackTrace, after_sp) == 100 ? 1 : -1];
typedef char VitaScspTraceSizeMustBe108[
   sizeof(VitaScspCallbackTrace) == 108 ? 1 : -1];

__attribute__((used, aligned(4)))
volatile VitaScspStateRecorder vita_scsp_state_recorder;

static volatile uint32_t vita_scsp_state_frame;

static void vita_scsp_state_diag_context(VitaScspStateRecord *record)
{
#ifdef VITA_SH2_DYNAREC_RUNTIME
   record->master_pc = (uint32_t)master_pc;
   record->master_ip = (uint32_t)(uintptr_t)master_ip;
   record->slave_pc = (uint32_t)slave_pc;
   record->slave_ip = (uint32_t)(uintptr_t)slave_ip;
   record->write_depth = vita_dynarec_vm_write_depth();
   record->runtime_patches = vita_dynarec_runtime_patch_count();
   record->invalidation_publications =
      vita_dynarec_invalidation_publish_count();
   record->implicit_patch = (uint32_t)vita_dynarec_implicit_patch_active();
#else
   record->master_pc = 0;
   record->master_ip = 0;
   record->slave_pc = 0;
   record->slave_ip = 0;
   record->write_depth = 0;
   record->runtime_patches = 0;
   record->invalidation_publications = 0;
   record->implicit_patch = 0;
#endif
}

__attribute__((noinline))
static void vita_scsp_state_diag_fail(uint32_t checkpoint,
                                      uint32_t failure_site,
                                      uint32_t current,
                                      uint32_t expected,
                                      uint32_t detail)
{
   extern void vita_scsp_state_diag_trap(uint32_t failure_site)
      __attribute__((noreturn));

   vita_scsp_state_recorder.failure_checkpoint = checkpoint;
   vita_scsp_state_recorder.failure_site = failure_site;
   vita_scsp_state_recorder.failure_current = current;
   vita_scsp_state_recorder.failure_expected = expected;
   vita_scsp_state_recorder.failure_slot =
      (uint32_t)(uintptr_t)vita_scsp_state_diag_sound_core_slot();
   vita_scsp_state_recorder.failure_detail = detail;

   /* The assembly trap fixes the abort address and guarantees that the unique
    * failure site is still in r0 when the crash context is captured. */
   vita_scsp_state_diag_trap(failure_site);
}

void vita_scsp_state_diag_callback_fail(uint32_t failure_site,
                                        uint32_t current,
                                        uint32_t expected,
                                        uint32_t detail)
{
   vita_scsp_state_diag_fail(vita_scsp_state_recorder.last_checkpoint,
                             failure_site, current, expected, detail);
}

void vita_scsp_state_diag_set_frame(uint32_t frame)
{
   vita_scsp_state_frame = frame;
}

void vita_scsp_state_diag_log_reset(const char *sh2_core)
{
   FILE *file;

   sceIoMkdir("ux0:data/yabause", 0777);
   file = fopen(VITA_SCSP_DIAG_LOG_PATH, "w");
   if (!file)
      return;

   fprintf(file,
           "test=scsp-callback-boundary revision=2 sh2=%s sound=dummy m68k=dummy\n",
           sh2_core);
   fclose(file);
}

static void vita_scsp_state_diag_record(uint32_t checkpoint,
                                        uint32_t host_address)
{
   VitaScspStateRecord record;
   void **slot = vita_scsp_state_diag_sound_core_slot();
   void *current = slot ? *slot : NULL;
   void *expected = vita_scsp_state_diag_expected_sound_core();
   SoundInterface_struct *current_core = (SoundInterface_struct *)current;
   SoundInterface_struct *expected_core = (SoundInterface_struct *)expected;
   uint32_t sequence = vita_scsp_state_recorder.sequence + 1;
   uint32_t index = (sequence - 1) % VITA_SCSP_DIAG_RECORD_COUNT;

   record.sequence = sequence;
   record.checkpoint = checkpoint;
   record.frame = vita_scsp_state_frame;
   record.current = (uint32_t)(uintptr_t)current;
   record.expected = (uint32_t)(uintptr_t)expected;
   record.slot = (uint32_t)(uintptr_t)slot;
   record.get_audio_space = current == expected && current_core
      ? (uint32_t)(uintptr_t)current_core->GetAudioSpace : 0;
   record.expected_get_audio_space = expected_core
      ? (uint32_t)(uintptr_t)expected_core->GetAudioSpace : 0;
   record.update_audio = current == expected && current_core
      ? (uint32_t)(uintptr_t)current_core->UpdateAudio : 0;
   record.expected_update_audio = expected_core
      ? (uint32_t)(uintptr_t)expected_core->UpdateAudio : 0;
   record.callback_thumb_bits = (record.get_audio_space & 1u) |
      ((record.update_audio & 1u) << 1);
   vita_scsp_state_diag_context(&record);
   record.host_address = host_address ? host_address : record.master_ip;

   vita_scsp_state_recorder.magic = VITA_SCSP_DIAG_MAGIC;
   vita_scsp_state_recorder.version = VITA_SCSP_DIAG_VERSION;
   vita_scsp_state_recorder.records[index] = record;
   vita_scsp_state_recorder.last_checkpoint = checkpoint;
   vita_scsp_state_recorder.sequence = sequence;

   if (current != expected)
      vita_scsp_state_diag_fail(checkpoint,
                                VITA_SCSP_FAILURE_CORE_MISMATCH,
                                record.current, record.expected, record.slot);
   if (!(record.get_audio_space & 1u))
      vita_scsp_state_diag_fail(checkpoint,
                                VITA_SCSP_FAILURE_GET_TARGET_NOT_THUMB,
                                record.get_audio_space,
                                record.expected_get_audio_space,
                                record.callback_thumb_bits);
   if (record.get_audio_space != record.expected_get_audio_space)
      vita_scsp_state_diag_fail(checkpoint,
                                VITA_SCSP_FAILURE_GET_TARGET_MISMATCH,
                                record.get_audio_space,
                                record.expected_get_audio_space,
                                record.callback_thumb_bits);
   if (!(record.update_audio & 1u))
      vita_scsp_state_diag_fail(checkpoint,
                                VITA_SCSP_FAILURE_UPDATE_TARGET_NOT_THUMB,
                                record.update_audio,
                                record.expected_update_audio,
                                record.callback_thumb_bits);
   if (record.update_audio != record.expected_update_audio)
      vita_scsp_state_diag_fail(checkpoint,
                                VITA_SCSP_FAILURE_UPDATE_TARGET_MISMATCH,
                                record.update_audio,
                                record.expected_update_audio,
                                record.callback_thumb_bits);
}

void vita_scsp_state_diag_checkpoint(uint32_t checkpoint)
{
   vita_scsp_state_diag_record(checkpoint, 0);
}

void vita_scsp_state_diag_checkpoint_host(uint32_t checkpoint,
                                          uint32_t host_address)
{
   vita_scsp_state_diag_record(checkpoint, host_address);
}

extern uint32_t vita_scsp_state_diag_call_get_audio_space(uint32_t target);

uint32_t vita_scsp_state_diag_get_audio_space(void)
{
   void **slot = vita_scsp_state_diag_sound_core_slot();
   SoundInterface_struct *current = slot
      ? (SoundInterface_struct *)*slot : NULL;
   SoundInterface_struct *expected =
      (SoundInterface_struct *)vita_scsp_state_diag_expected_sound_core();
   uint32_t get_target = current == expected && current
      ? (uint32_t)(uintptr_t)current->GetAudioSpace : 0;
   uint32_t expected_get_target = expected
      ? (uint32_t)(uintptr_t)expected->GetAudioSpace : 0;
   uint32_t update_target = current == expected && current
      ? (uint32_t)(uintptr_t)current->UpdateAudio : 0;
   uint32_t expected_update_target = expected
      ? (uint32_t)(uintptr_t)expected->UpdateAudio : 0;
   uint32_t thumb_bits = (get_target & 1u) | ((update_target & 1u) << 1);

   vita_scsp_state_recorder.callback.target = get_target;
   vita_scsp_state_recorder.callback.expected_target = expected_get_target;
   vita_scsp_state_recorder.callback.update_target = update_target;
   vita_scsp_state_recorder.callback.expected_update_target =
      expected_update_target;
   vita_scsp_state_recorder.callback.thumb_bits = thumb_bits;

   if (current != expected)
      vita_scsp_state_diag_fail(vita_scsp_state_recorder.last_checkpoint,
                                VITA_SCSP_FAILURE_CORE_MISMATCH,
                                (uint32_t)(uintptr_t)current,
                                (uint32_t)(uintptr_t)expected,
                                (uint32_t)(uintptr_t)slot);
   if (!(get_target & 1u))
      vita_scsp_state_diag_fail(vita_scsp_state_recorder.last_checkpoint,
                                VITA_SCSP_FAILURE_GET_TARGET_NOT_THUMB,
                                get_target, expected_get_target, thumb_bits);
   if (get_target != expected_get_target)
      vita_scsp_state_diag_fail(vita_scsp_state_recorder.last_checkpoint,
                                VITA_SCSP_FAILURE_GET_TARGET_MISMATCH,
                                get_target, expected_get_target, thumb_bits);
   if (!(update_target & 1u))
      vita_scsp_state_diag_fail(vita_scsp_state_recorder.last_checkpoint,
                                VITA_SCSP_FAILURE_UPDATE_TARGET_NOT_THUMB,
                                update_target, expected_update_target,
                                thumb_bits);
   if (update_target != expected_update_target)
      vita_scsp_state_diag_fail(vita_scsp_state_recorder.last_checkpoint,
                                VITA_SCSP_FAILURE_UPDATE_TARGET_MISMATCH,
                                update_target, expected_update_target,
                                thumb_bits);

   return vita_scsp_state_diag_call_get_audio_space(get_target);
}

void vita_scsp_state_diag_log(const char *stage)
{
   FILE *file;
   void **slot = vita_scsp_state_diag_sound_core_slot();
   void *current = slot ? *slot : NULL;
   void *expected = vita_scsp_state_diag_expected_sound_core();
   SoundInterface_struct *current_core = (SoundInterface_struct *)current;
   SoundInterface_struct *expected_core = (SoundInterface_struct *)expected;
   uint32_t get_target = current == expected && current_core
      ? (uint32_t)(uintptr_t)current_core->GetAudioSpace : 0;
   uint32_t expected_get_target = expected_core
      ? (uint32_t)(uintptr_t)expected_core->GetAudioSpace : 0;
   uint32_t update_target = current == expected && current_core
      ? (uint32_t)(uintptr_t)current_core->UpdateAudio : 0;
   uint32_t expected_update_target = expected_core
      ? (uint32_t)(uintptr_t)expected_core->UpdateAudio : 0;
   uint32_t thumb_bits = (get_target & 1u) | ((update_target & 1u) << 1);

   sceIoMkdir("ux0:data/yabause", 0777);
   file = fopen(VITA_SCSP_DIAG_LOG_PATH, "a");
   if (!file)
      return;

   fprintf(file,
           "scsp_state stage=%s checkpoint=%u sequence=%u frame=%u current=%08x expected=%08x slot=%08x get=%08x expected_get=%08x update=%08x expected_update=%08x thumb=%u failure_checkpoint=%u failure_site=%03x",
           stage, (unsigned)vita_scsp_state_recorder.last_checkpoint,
           (unsigned)vita_scsp_state_recorder.sequence,
           (unsigned)vita_scsp_state_frame,
           (unsigned)(uintptr_t)current, (unsigned)(uintptr_t)expected,
           (unsigned)(uintptr_t)slot,
           (unsigned)get_target, (unsigned)expected_get_target,
           (unsigned)update_target, (unsigned)expected_update_target,
           (unsigned)thumb_bits,
           (unsigned)vita_scsp_state_recorder.failure_checkpoint,
           (unsigned)vita_scsp_state_recorder.failure_site);
#ifdef VITA_SH2_DYNAREC_RUNTIME
   fprintf(file,
           " master_pc=%08x master_ip=%08x slave_pc=%08x slave_ip=%08x write_depth=%u runtime_patches=%u invalidation_publications=%u implicit=%d",
           (unsigned)master_pc, (unsigned)(uintptr_t)master_ip,
           (unsigned)slave_pc, (unsigned)(uintptr_t)slave_ip,
           vita_dynarec_vm_write_depth(), vita_dynarec_runtime_patch_count(),
           vita_dynarec_invalidation_publish_count(),
           vita_dynarec_implicit_patch_active());
#endif
   fputc('\n', file);
   fflush(file);
   fclose(file);
}
