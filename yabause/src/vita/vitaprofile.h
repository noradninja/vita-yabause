#ifndef VITAPROFILE_H
#define VITAPROFILE_H

typedef enum {
   VITA_PROFILE_FRAME = 0,
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
