#include "pervita.h"

#include <psp2/ctrl.h>
#include <stdio.h>

static PerPad_struct *vita_pad;
static unsigned int previous_buttons;

typedef void (*PadAction)(PerPad_struct *pad);

typedef struct {
   unsigned int vita_button;
   PadAction press;
   PadAction release;
} VitaButtonMapping;

static const VitaButtonMapping mappings[] = {
   { SCE_CTRL_UP,       PerPadUpPressed,       PerPadUpReleased },
   { SCE_CTRL_RIGHT,    PerPadRightPressed,    PerPadRightReleased },
   { SCE_CTRL_DOWN,     PerPadDownPressed,     PerPadDownReleased },
   { SCE_CTRL_LEFT,     PerPadLeftPressed,     PerPadLeftReleased },
   { SCE_CTRL_SQUARE,   PerPadAPressed,        PerPadAReleased },
   { SCE_CTRL_CROSS,    PerPadBPressed,        PerPadBReleased },
   { SCE_CTRL_CIRCLE,   PerPadCPressed,        PerPadCReleased },
   { SCE_CTRL_TRIANGLE, PerPadYPressed,        PerPadYReleased },
   { SCE_CTRL_LTRIGGER, PerPadLTriggerPressed, PerPadLTriggerReleased },
   { SCE_CTRL_RTRIGGER, PerPadRTriggerPressed, PerPadRTriggerReleased },
   { SCE_CTRL_START,    PerPadStartPressed,    PerPadStartReleased },
};

static int PERVitaInit(void)
{
   sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
   PerPortReset();
   vita_pad = PerPadAdd(&PORTDATA1);
   previous_buttons = 0;
   return vita_pad ? 0 : -1;
}

static void PERVitaDeInit(void)
{
   vita_pad = NULL;
   previous_buttons = 0;
}

static int PERVitaHandleEvents(void)
{
   SceCtrlData state;
   unsigned int changed;
   unsigned int i;

   if (!vita_pad || sceCtrlPeekBufferPositive(0, &state, 1) <= 0)
      return 0;

   changed = previous_buttons ^ state.buttons;
   for (i = 0; i < sizeof(mappings) / sizeof(mappings[0]); ++i) {
      if (changed & mappings[i].vita_button) {
         if (state.buttons & mappings[i].vita_button)
            mappings[i].press(vita_pad);
         else
            mappings[i].release(vita_pad);
      }
   }

   previous_buttons = state.buttons;
   return 0;
}

static u32 PERVitaScan(u32 flags)
{
   return 0;
}

static void PERVitaFlush(void)
{
}

static void PERVitaKeyName(u32 key, char *name, int size)
{
   snprintf(name, size, "0x%08X", (unsigned int)key);
}

PerInterface_struct PERVita = {
   PERCORE_VITA,
   "PS Vita Controller",
   PERVitaInit,
   PERVitaDeInit,
   PERVitaHandleEvents,
   PERVitaScan,
   0,
   PERVitaFlush,
   PERVitaKeyName
};
