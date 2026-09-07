#ifndef YABAUSE_VITA_HANG_H
#define YABAUSE_VITA_HANG_H

typedef enum {
   VITA_HANG_STAGE_IDLE = 0,
   VITA_HANG_STAGE_FRAME,
   VITA_HANG_STAGE_MSH2,
   VITA_HANG_STAGE_SSH2,
   VITA_HANG_STAGE_SCU,
   VITA_HANG_STAGE_SMPC,
   VITA_HANG_STAGE_CD,
   VITA_HANG_STAGE_M68K,
   VITA_HANG_STAGE_HBLANK_IN,
   VITA_HANG_STAGE_HBLANK_OUT,
   VITA_HANG_STAGE_VBLANK_IN,
   VITA_HANG_STAGE_VBLANK_OUT,
   VITA_HANG_STAGE_VDP1_DECODE,
   VITA_HANG_STAGE_VDP1_DRAW,
   VITA_HANG_STAGE_VDP2_PROCESS,
   VITA_HANG_STAGE_ATLAS_SYNC,
   VITA_HANG_STAGE_ATLAS_UPLOAD,
   VITA_HANG_STAGE_GPU_DRAW,
   VITA_HANG_STAGE_COMPOSITION,
   VITA_HANG_STAGE_PRESENT,
   VITA_HANG_STAGE_EVENTS
} VitaHangStage;

#ifdef VITA_HANG_DIAGNOSTICS
int VitaHangInit(void);
void VitaHangShutdown(void);
void VitaHangFrameBegin(void);
void VitaHangFrameComplete(void);
void VitaHangSetStage(VitaHangStage stage,
                      unsigned int detail0, unsigned int detail1,
                      unsigned int detail2, unsigned int detail3);
#else
static inline int VitaHangInit(void) { return 0; }
static inline void VitaHangShutdown(void) {}
static inline void VitaHangFrameBegin(void) {}
static inline void VitaHangFrameComplete(void) {}
static inline void VitaHangSetStage(VitaHangStage stage,
                                    unsigned int detail0, unsigned int detail1,
                                    unsigned int detail2, unsigned int detail3)
{
   (void)stage; (void)detail0; (void)detail1; (void)detail2; (void)detail3;
}
#endif

#endif
