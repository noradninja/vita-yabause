#ifndef VITA_SCSP_STATE_DIAG_H
#define VITA_SCSP_STATE_DIAG_H

#include <stdint.h>

enum {
   VITA_SCSP_CHECKPOINT_SCSP_INIT = 1,
   VITA_SCSP_CHECKPOINT_YABAUSE_INIT = 2,
   VITA_SCSP_CHECKPOINT_FRAME_ENTER = 3,
   VITA_SCSP_CHECKPOINT_MASTER_BLOCK_ENTER = 4,
   VITA_SCSP_CHECKPOINT_MASTER_CYCLE_INTERRUPT = 5,
   VITA_SCSP_CHECKPOINT_SCSP_WRAPPER = 6,
   VITA_SCSP_CHECKPOINT_SCSP_ENTRY = 7
};

enum {
   VITA_SCSP_FAILURE_CORE_MISMATCH = 0x101,
   VITA_SCSP_FAILURE_GET_TARGET_MISMATCH = 0x102,
   VITA_SCSP_FAILURE_UPDATE_TARGET_MISMATCH = 0x103,
   VITA_SCSP_FAILURE_GET_TARGET_NOT_THUMB = 0x104,
   VITA_SCSP_FAILURE_UPDATE_TARGET_NOT_THUMB = 0x105,
   VITA_SCSP_FAILURE_CALLBACK_SP = 0x110,
   VITA_SCSP_FAILURE_CALLBACK_R4 = 0x114,
   VITA_SCSP_FAILURE_CALLBACK_R5 = 0x115,
   VITA_SCSP_FAILURE_CALLBACK_R6 = 0x116,
   VITA_SCSP_FAILURE_CALLBACK_R7 = 0x117,
   VITA_SCSP_FAILURE_CALLBACK_R8 = 0x118,
   VITA_SCSP_FAILURE_CALLBACK_R9 = 0x119,
   VITA_SCSP_FAILURE_CALLBACK_R10 = 0x11A,
   VITA_SCSP_FAILURE_CALLBACK_R11 = 0x11B,
   VITA_SCSP_FAILURE_SAMPLE_SP = 0x130,
   VITA_SCSP_FAILURE_SAMPLE_R4 = 0x134,
   VITA_SCSP_FAILURE_SAMPLE_R5 = 0x135,
   VITA_SCSP_FAILURE_SAMPLE_R6 = 0x136,
   VITA_SCSP_FAILURE_SAMPLE_R7 = 0x137,
   VITA_SCSP_FAILURE_SAMPLE_R8 = 0x138,
   VITA_SCSP_FAILURE_SAMPLE_R9 = 0x139,
   VITA_SCSP_FAILURE_SAMPLE_R10 = 0x13A,
   VITA_SCSP_FAILURE_SAMPLE_R11 = 0x13B
};

void **vita_scsp_state_diag_sound_core_slot(void);
void *vita_scsp_state_diag_expected_sound_core(void);
void vita_scsp_state_diag_log_reset(const char *sh2_core);
void vita_scsp_state_diag_set_frame(uint32_t frame);
void vita_scsp_state_diag_checkpoint(uint32_t checkpoint);
void vita_scsp_state_diag_checkpoint_host(uint32_t checkpoint,
                                          uint32_t host_address);
void vita_scsp_state_diag_log(const char *stage);
uint32_t vita_scsp_state_diag_get_audio_space(void);
void vita_scsp_state_diag_scsp_update(int32_t *left, int32_t *right,
                                      uint32_t length);

/* Called only by the diagnostic assembly trampoline after it has restored a
 * valid native stack and the caller's callee-saved registers. */
void vita_scsp_state_diag_callback_fail(uint32_t failure_site,
                                        uint32_t current,
                                        uint32_t expected,
                                        uint32_t detail);

#endif
