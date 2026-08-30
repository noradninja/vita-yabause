#ifndef VITAGL_PRESENT_H
#define VITAGL_PRESENT_H

#include "../core.h"

void VitaGLPresenterLog(const char *message);
int VitaGLPresenterInit(void);
void VitaGLPresenterShutdown(void);
int VitaGLPresenterPresent(const u32 *pixels, int width, int height);
int VitaGLPresenterPrepareNative(int width, int height);
void VitaGLPresenterSwapNative(void);

#endif
