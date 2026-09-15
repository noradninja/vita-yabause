#include "vita_scsp_state_diag.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>

#include <stddef.h>
#include <stdio.h>

#ifdef VITA_SH2_DYNAREC_RUNTIME
#include "vita_dynarec_vm.h"

extern int master_pc;
extern int slave_pc;
extern void *master_ip;
extern void *slave_ip;
#endif

#define VITA_SCSP_DIAG_MAGIC 0x53434447u /* "SCDG" */
#define VITA_SCSP_DIAG_VERSION 1u
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
   uint32_t write_depth;
   uint32_t runtime_patches;
   uint32_t invalidation_publications;
   uint32_t implicit_patch;
} VitaScspStateRecord;

typedef struct VitaScspStateRecorder {
   uint32_t magic;
   uint32_t version;
   uint32_t sequence;
   uint32_t last_checkpoint;
   uint32_t failure_checkpoint;
   uint32_t failure_current;
   uint32_t failure_expected;
   uint32_t failure_slot;
   VitaScspStateRecord records[VITA_SCSP_DIAG_RECORD_COUNT];
} VitaScspStateRecorder;

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
                                      uint32_t current,
                                      uint32_t expected,
                                      uint32_t slot)
{
   vita_scsp_state_recorder.failure_checkpoint = checkpoint;
   vita_scsp_state_recorder.failure_current = current;
   vita_scsp_state_recorder.failure_expected = expected;
   vita_scsp_state_recorder.failure_slot = slot;

   /* A fixed data-abort address makes this diagnostic recognizable while the
    * checkpoint and both pointer values remain available in BSS. */
   __asm__ volatile("str %0, [%1]"
                    :
                    : "r"(checkpoint), "r"((uintptr_t)0x18)
                    : "memory");
   __builtin_unreachable();
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
           "test=scsp-state revision=1 sh2=%s sound=dummy m68k=dummy\n",
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
   uint32_t sequence = vita_scsp_state_recorder.sequence + 1;
   uint32_t index = (sequence - 1) % VITA_SCSP_DIAG_RECORD_COUNT;

   record.sequence = sequence;
   record.checkpoint = checkpoint;
   record.frame = vita_scsp_state_frame;
   record.current = (uint32_t)(uintptr_t)current;
   record.expected = (uint32_t)(uintptr_t)expected;
   record.slot = (uint32_t)(uintptr_t)slot;
   vita_scsp_state_diag_context(&record);
   record.host_address = host_address ? host_address : record.master_ip;

   vita_scsp_state_recorder.magic = VITA_SCSP_DIAG_MAGIC;
   vita_scsp_state_recorder.version = VITA_SCSP_DIAG_VERSION;
   vita_scsp_state_recorder.records[index] = record;
   vita_scsp_state_recorder.last_checkpoint = checkpoint;
   vita_scsp_state_recorder.sequence = sequence;

   if (current != expected)
      vita_scsp_state_diag_fail(checkpoint, record.current,
                                record.expected, record.slot);
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

void vita_scsp_state_diag_log(const char *stage)
{
   FILE *file;
   void **slot = vita_scsp_state_diag_sound_core_slot();
   void *current = slot ? *slot : NULL;
   void *expected = vita_scsp_state_diag_expected_sound_core();

   sceIoMkdir("ux0:data/yabause", 0777);
   file = fopen(VITA_SCSP_DIAG_LOG_PATH, "a");
   if (!file)
      return;

   fprintf(file,
           "scsp_state stage=%s checkpoint=%u sequence=%u frame=%u current=%08x expected=%08x slot=%08x failure=%u",
           stage, (unsigned)vita_scsp_state_recorder.last_checkpoint,
           (unsigned)vita_scsp_state_recorder.sequence,
           (unsigned)vita_scsp_state_frame,
           (unsigned)(uintptr_t)current, (unsigned)(uintptr_t)expected,
           (unsigned)(uintptr_t)slot,
           (unsigned)vita_scsp_state_recorder.failure_checkpoint);
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
