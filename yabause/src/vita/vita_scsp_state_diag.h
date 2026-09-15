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

void **vita_scsp_state_diag_sound_core_slot(void);
void *vita_scsp_state_diag_expected_sound_core(void);
void vita_scsp_state_diag_log_reset(const char *sh2_core);
void vita_scsp_state_diag_set_frame(uint32_t frame);
void vita_scsp_state_diag_checkpoint(uint32_t checkpoint);
void vita_scsp_state_diag_checkpoint_host(uint32_t checkpoint,
                                          uint32_t host_address);
void vita_scsp_state_diag_log(const char *stage);

#endif
