#ifndef VITAPROFILE_H
#define VITAPROFILE_H

typedef enum {
   VITA_PROFILE_FRAME = 0,
   VITA_PROFILE_VDP1_DECODE,
   VITA_PROFILE_ATLAS_UPLOAD,
   VITA_PROFILE_VDP1_DRAW,
   VITA_PROFILE_VDP2_DECODE,
   VITA_PROFILE_VDP2_DRAW,
   VITA_PROFILE_COMPOSITION,
   VITA_PROFILE_PRESENT,
   VITA_PROFILE_AUDIO,
   VITA_PROFILE_COUNT
} VitaProfileSection;

void VitaProfileInit(void);
void VitaProfileBegin(VitaProfileSection section);
void VitaProfileEnd(VitaProfileSection section);
void VitaProfileFrameComplete(void);
void VitaProfileShutdown(void);

#endif
