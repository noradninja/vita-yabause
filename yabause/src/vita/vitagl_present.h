#ifndef VITAGL_PRESENT_H
#define VITAGL_PRESENT_H

#include "../core.h"

int VitaGLPresenterInit(void);
void VitaGLPresenterShutdown(void);
int VitaGLPresenterPresent(const u32 *pixels, int width, int height);

#endif
