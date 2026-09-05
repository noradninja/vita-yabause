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
   VITA_PROFILE_VDP1_SUBMIT,
   VITA_PROFILE_VDP2_SUBMIT,
   VITA_PROFILE_ATLAS_PACK,
   VITA_PROFILE_ATLAS_TRANSFER,
   VITA_PROFILE_ATLAS_FIRST_DRAW,
   VITA_PROFILE_COUNT
} VitaProfileSection;

typedef enum {
   VITA_PROFILE_ATLAS_NONE = 0,
   VITA_PROFILE_ATLAS_VDP1,
   VITA_PROFILE_ATLAS_VDP2,
   VITA_PROFILE_ATLAS_COUNT
} VitaProfileAtlasPhase;

typedef enum {
   VITA_PROFILE_VDP2_SOURCE_OTHER = 0,
   VITA_PROFILE_VDP2_SOURCE_ROTATION,
   VITA_PROFILE_VDP2_SOURCE_ROTATION_LINE,
   VITA_PROFILE_VDP2_SOURCE_NBG0_BITMAP,
   VITA_PROFILE_VDP2_SOURCE_NBG1_BITMAP,
   VITA_PROFILE_VDP2_SOURCE_PATTERN,
   VITA_PROFILE_VDP2_SOURCE_COUNT
} VitaProfileVdp2Source;

typedef enum {
   VITA_PROFILE_VDP2_CACHE_NEW = 0,
   VITA_PROFILE_VDP2_CACHE_RAM,
   VITA_PROFILE_VDP2_CACHE_CRAM,
   VITA_PROFILE_VDP2_CACHE_STATE,
   VITA_PROFILE_VDP2_CACHE_DIMENSIONS,
   VITA_PROFILE_VDP2_CACHE_EVICTION,
   VITA_PROFILE_VDP2_CACHE_FALLBACK,
   VITA_PROFILE_VDP2_CACHE_REASON_COUNT
} VitaProfileVdp2CacheReason;

void VitaProfileInit(void);
void VitaProfileBegin(VitaProfileSection section);
void VitaProfileEnd(VitaProfileSection section);
void VitaProfilePushAtlasPhase(VitaProfileAtlasPhase phase);
void VitaProfilePopAtlasPhase(void);
VitaProfileAtlasPhase VitaProfileCurrentAtlasPhase(void);
void VitaProfileRecordAtlasAllocation(unsigned int width, unsigned int height,
                                      unsigned int atlas_height);
void VitaProfileRecordAtlasUploadBatch(void);
void VitaProfileRecordAtlasUploadRegion(VitaProfileAtlasPhase producer,
                                        unsigned int width, unsigned int height);
void VitaProfileRecordAtlasRegionsMerged(VitaProfileAtlasPhase producer,
                                         unsigned int count);
void VitaProfileRecordAtlasUploadSkipped(void);
void VitaProfileRecordAtlasUploadFallback(void);
void VitaProfileRecordAtlasDirtyGenerated(unsigned int width,
                                          unsigned int height,
                                          int persistent);
void VitaProfileRecordAtlasJournalClasses(
   unsigned int selected_persistent_regions,
   unsigned long long selected_persistent_bytes,
   unsigned int selected_transient_regions,
   unsigned long long selected_transient_bytes,
   unsigned int carried_persistent_regions,
   unsigned long long carried_persistent_bytes,
   unsigned int carried_transient_regions,
   unsigned long long carried_transient_bytes);
void VitaProfileRecordAtlasBufferState(unsigned int mode,
                                       unsigned int active_buffer,
                                       unsigned int selected_regions,
                                       unsigned long long selected_bytes,
                                       unsigned int carried_regions,
                                       unsigned long long carried_bytes,
                                       int resync, int overflow);
void VitaProfileRecordCacheResult(int hit);
void VitaProfileRecordVdp2PersistentCache(int event, unsigned int bytes);
void VitaProfileRecordVdp2PartialRefresh(unsigned int rows,
                                         unsigned int bytes);
void VitaProfileRecordVdp2CacheReason(VitaProfileVdp2CacheReason reason);
void VitaProfileSetVdp2Source(VitaProfileVdp2Source source);
VitaProfileVdp2Source VitaProfileCurrentVdp2Source(void);
void VitaProfileRecordVdp2SourceAllocation(VitaProfileVdp2Source source,
                                           unsigned int width,
                                           unsigned int height);
void VitaProfileRecordVdp2SourceUpload(VitaProfileVdp2Source source,
                                       unsigned int width,
                                       unsigned int height);
void VitaProfileFrameComplete(void);
void VitaProfileShutdown(void);

#endif
