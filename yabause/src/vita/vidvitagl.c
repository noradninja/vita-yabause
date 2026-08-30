#include "vidvitagl.h"

#include "../vidogl.h"

int VIDOGLInit(void);
void VIDOGLDeInit(void);
void VIDOGLResize(unsigned int width, unsigned int height, int fullscreen);
int VIDOGLIsFullscreen(void);
int VIDOGLVdp1Reset(void);
void VIDOGLVdp1DrawStart(void);
void VIDOGLVdp1DrawEnd(void);
void VIDOGLVdp1NormalSpriteDraw(u8 *ram, Vdp1 *regs, u8 *framebuffer);
void VIDOGLVdp1ScaledSpriteDraw(u8 *ram, Vdp1 *regs, u8 *framebuffer);
void VIDOGLVdp1DistortedSpriteDraw(u8 *ram, Vdp1 *regs, u8 *framebuffer);
void VIDOGLVdp1PolygonDraw(u8 *ram, Vdp1 *regs, u8 *framebuffer);
void VIDOGLVdp1PolylineDraw(u8 *ram, Vdp1 *regs, u8 *framebuffer);
void VIDOGLVdp1LineDraw(u8 *ram, Vdp1 *regs, u8 *framebuffer);
void VIDOGLVdp1UserClipping(u8 *ram, Vdp1 *regs);
void VIDOGLVdp1SystemClipping(u8 *ram, Vdp1 *regs);
void VIDOGLVdp1LocalCoordinate(u8 *ram, Vdp1 *regs);
void VIDOGLVdp1ReadFrameBuffer(u32 type, u32 addr, void *out);
int VIDOGLVdp2Reset(void);
void VIDOGLVdp2DrawStart(void);
void VIDOGLVdp2DrawEnd(void);
void VIDOGLVdp2DrawScreens(void);
void YglGetGlSize(int *width, int *height);
void VIDOGLGetNativeResolution(int *width, int *height, int *interlace);
void VIDOGLVdp2DispOff(void);

VideoInterface_struct VIDVitaGL = {
   VIDCORE_VITAGL,
   "vitaGL Video Interface",
   VIDOGLInit,
   VIDOGLDeInit,
   VIDOGLResize,
   VIDOGLIsFullscreen,
   VIDOGLVdp1Reset,
   VIDOGLVdp1DrawStart,
   VIDOGLVdp1DrawEnd,
   VIDOGLVdp1NormalSpriteDraw,
   VIDOGLVdp1ScaledSpriteDraw,
   VIDOGLVdp1DistortedSpriteDraw,
   VIDOGLVdp1PolygonDraw,
   VIDOGLVdp1PolylineDraw,
   VIDOGLVdp1LineDraw,
   VIDOGLVdp1UserClipping,
   VIDOGLVdp1SystemClipping,
   VIDOGLVdp1LocalCoordinate,
   VIDOGLVdp1ReadFrameBuffer,
   NULL,
   VIDOGLVdp2Reset,
   VIDOGLVdp2DrawStart,
   VIDOGLVdp2DrawEnd,
   VIDOGLVdp2DrawScreens,
   YglGetGlSize,
   VIDOGLGetNativeResolution,
   VIDOGLVdp2DispOff
};
