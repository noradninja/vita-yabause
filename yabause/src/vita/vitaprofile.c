#include "vitaprofile.h"

#ifdef VITA_PROFILE
#include <psp2/kernel/processmgr.h>
#include <stdio.h>
#include <string.h>

#define PROFILE_PATH "ux0:data/yabause/profile.log"
#define PROFILE_INTERVAL 300

typedef struct {
   unsigned long long started;
   unsigned long long total;
   unsigned long long exclusive_total;
   unsigned int calls;
} ProfileCounter;

typedef struct {
   unsigned long long decoded_bytes;
   unsigned long long upload_bytes;
   unsigned int allocations;
   unsigned int upload_regions;
   unsigned int max_width;
   unsigned int max_height;
} Vdp2SourceCounter;

typedef struct {
   VitaProfileSection section;
   unsigned long long started;
   unsigned long long resumed;
} ProfileStackEntry;

typedef struct {
   unsigned long long decoded_bytes;
   unsigned long long upload_bytes;
   unsigned int dirty_regions;
   unsigned int merged_regions;
   unsigned int upload_calls;
   unsigned int upload_regions;
   unsigned int skipped_uploads;
   unsigned int fallback_uploads;
   unsigned int cache_hits;
   unsigned int cache_misses;
   unsigned int persistent_hits, persistent_misses, persistent_invalidations;
   unsigned int persistent_evictions, persistent_fallbacks;
   unsigned long long persistent_reused_bytes;
   unsigned int partial_refreshes, partial_rows;
   unsigned long long partial_bytes;
   unsigned int peak_height;
} AtlasCounter;

typedef struct {
   unsigned long long generated_bytes;
   unsigned long long selected_bytes;
   unsigned long long carried_bytes;
   unsigned int generated_regions;
   unsigned int selected_regions;
   unsigned int carried_regions;
   unsigned int buffer_uses[2];
   unsigned int resyncs;
   unsigned int overflows;
   unsigned int mode;
   unsigned int persistent_generated_regions;
   unsigned long long persistent_generated_bytes;
   unsigned int transient_generated_regions;
   unsigned long long transient_generated_bytes;
   unsigned int selected_persistent_regions;
   unsigned long long selected_persistent_bytes;
   unsigned int selected_transient_regions;
   unsigned long long selected_transient_bytes;
   unsigned int carried_persistent_regions;
   unsigned long long carried_persistent_bytes;
   unsigned int carried_transient_regions;
   unsigned long long carried_transient_bytes;
   unsigned int coalesce_input_regions;
   unsigned int coalesce_output_regions;
   unsigned long long coalesce_dirty_bytes;
   unsigned long long coalesce_upload_bytes;
   unsigned int pages_allocated;
   unsigned int page_switches;
   unsigned int overflow_pages;
   unsigned int stream_reloads;
   unsigned int page_generation[2];
   unsigned int page_live_area[2];
   unsigned int page_peak_live_area[2];
   unsigned int generation_increments;
   unsigned int stale_rejections;
   unsigned int persistent_demotions;
   unsigned int transient_redecodes;
} AtlasBufferCounter;

#define ATLAS_PHASE_STACK_MAX 4
#define PROFILE_STACK_MAX 12

static ProfileCounter counters[VITA_PROFILE_COUNT];
static AtlasCounter atlas_counters[VITA_PROFILE_ATLAS_COUNT];
static AtlasBufferCounter atlas_buffer_counter;
static Vdp2SourceCounter vdp2_source_counters[VITA_PROFILE_VDP2_SOURCE_COUNT];
static unsigned int vdp2_cache_reasons[VITA_PROFILE_VDP2_CACHE_REASON_COUNT];
static VitaProfileAtlasPhase atlas_phase_stack[ATLAS_PHASE_STACK_MAX];
static unsigned int atlas_phase_depth;
static ProfileStackEntry profile_stack[PROFILE_STACK_MAX];
static unsigned int profile_stack_depth;
static VitaProfileVdp2Source vdp2_source;
static unsigned int frames;

static VitaProfileAtlasPhase current_atlas_phase(void)
{
   return atlas_phase_depth ?
      atlas_phase_stack[atlas_phase_depth - 1] : VITA_PROFILE_ATLAS_NONE;
}

static unsigned long long profile_average(VitaProfileSection section)
{
   return counters[section].calls
      ? counters[section].total / counters[section].calls : 0;
}

static unsigned long long profile_exclusive_average(VitaProfileSection section)
{
   return counters[section].calls
      ? counters[section].exclusive_total / counters[section].calls : 0;
}

static void flush_profile(void)
{
   static const char *atlas_mode_names[] = {
      "wide", "square", "buffered_square"
   };
   const char *atlas_mode =
      atlas_buffer_counter.mode < 3 ?
         atlas_mode_names[atlas_buffer_counter.mode] : "unknown";
#ifdef VITA_ATLAS_UPLOAD_BANDS
   const char *atlas_upload = "bands";
#else
   const char *atlas_upload = "dirty";
#endif
   FILE *file;
   if (!frames)
      return;

   file = fopen(PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "frames=%u frame_avg_us=%llu "
              "vdp1_decode_avg_us=%llu vdp1_decode_calls=%u "
              "atlas_upload_avg_us=%llu atlas_upload_calls=%u "
              "vdp1_draw_avg_us=%llu vdp1_draw_calls=%u "
              "vdp2_decode_avg_us=%llu vdp2_decode_calls=%u "
              "vdp2_draw_avg_us=%llu vdp2_draw_calls=%u "
              "vdp1_submit_avg_us=%llu vdp1_submit_calls=%u "
              "vdp2_submit_avg_us=%llu vdp2_submit_calls=%u "
              "composition_avg_us=%llu composition_calls=%u "
              "present_avg_us=%llu audio_avg_us=%llu audio_calls=%u\n",
              frames,
              profile_average(VITA_PROFILE_FRAME),
              profile_average(VITA_PROFILE_VDP1_DECODE),
              counters[VITA_PROFILE_VDP1_DECODE].calls,
              profile_average(VITA_PROFILE_ATLAS_UPLOAD),
              counters[VITA_PROFILE_ATLAS_UPLOAD].calls,
              profile_average(VITA_PROFILE_VDP1_DRAW),
              counters[VITA_PROFILE_VDP1_DRAW].calls,
              profile_average(VITA_PROFILE_VDP2_DECODE),
              counters[VITA_PROFILE_VDP2_DECODE].calls,
              profile_average(VITA_PROFILE_VDP2_DRAW),
              counters[VITA_PROFILE_VDP2_DRAW].calls,
              profile_average(VITA_PROFILE_VDP1_SUBMIT),
              counters[VITA_PROFILE_VDP1_SUBMIT].calls,
              profile_average(VITA_PROFILE_VDP2_SUBMIT),
              counters[VITA_PROFILE_VDP2_SUBMIT].calls,
              profile_average(VITA_PROFILE_COMPOSITION),
              counters[VITA_PROFILE_COMPOSITION].calls,
              profile_average(VITA_PROFILE_PRESENT),
              profile_average(VITA_PROFILE_AUDIO),
              counters[VITA_PROFILE_AUDIO].calls);
      fprintf(file,
              "atlas_pack_avg_us=%llu atlas_pack_calls=%u "
              "atlas_transfer_avg_us=%llu atlas_transfer_calls=%u "
              "atlas_first_draw_avg_us=%llu atlas_first_draw_calls=%u\n",
              profile_average(VITA_PROFILE_ATLAS_PACK),
              counters[VITA_PROFILE_ATLAS_PACK].calls,
              profile_average(VITA_PROFILE_ATLAS_TRANSFER),
              counters[VITA_PROFILE_ATLAS_TRANSFER].calls,
              profile_average(VITA_PROFILE_ATLAS_FIRST_DRAW),
              counters[VITA_PROFILE_ATLAS_FIRST_DRAW].calls);
      fprintf(file,
              "atlas_mode=%s atlas_upload=%s atlas_buffer0_uses=%u atlas_buffer1_uses=%u "
              "atlas_generated_regions=%u atlas_generated_avg_bytes=%llu "
              "atlas_selected_regions=%u atlas_selected_avg_bytes=%llu "
              "atlas_carried_regions=%u atlas_carried_avg_bytes=%llu "
              "atlas_resyncs=%u atlas_journal_overflows=%u\n",
              atlas_mode,
              atlas_upload,
              atlas_buffer_counter.buffer_uses[0],
              atlas_buffer_counter.buffer_uses[1],
              atlas_buffer_counter.generated_regions,
              atlas_buffer_counter.generated_bytes / frames,
              atlas_buffer_counter.selected_regions,
              atlas_buffer_counter.selected_bytes / frames,
              atlas_buffer_counter.carried_regions,
              atlas_buffer_counter.carried_bytes / frames,
              atlas_buffer_counter.resyncs,
              atlas_buffer_counter.overflows);
      fprintf(file,
              "atlas_persistent_generated_regions=%u atlas_persistent_generated_avg_bytes=%llu "
              "atlas_transient_generated_regions=%u atlas_transient_generated_avg_bytes=%llu "
              "atlas_persistent_selected_regions=%u atlas_persistent_selected_avg_bytes=%llu "
              "atlas_transient_selected_regions=%u atlas_transient_selected_avg_bytes=%llu "
              "atlas_persistent_carried_regions=%u atlas_persistent_carried_avg_bytes=%llu "
              "atlas_transient_carried_regions=%u atlas_transient_carried_avg_bytes=%llu\n",
              atlas_buffer_counter.persistent_generated_regions,
              atlas_buffer_counter.persistent_generated_bytes / frames,
              atlas_buffer_counter.transient_generated_regions,
              atlas_buffer_counter.transient_generated_bytes / frames,
              atlas_buffer_counter.selected_persistent_regions,
              atlas_buffer_counter.selected_persistent_bytes / frames,
              atlas_buffer_counter.selected_transient_regions,
              atlas_buffer_counter.selected_transient_bytes / frames,
              atlas_buffer_counter.carried_persistent_regions,
              atlas_buffer_counter.carried_persistent_bytes / frames,
              atlas_buffer_counter.carried_transient_regions,
              atlas_buffer_counter.carried_transient_bytes / frames);
      fprintf(file,
              "atlas_coalesce_input_regions=%u atlas_coalesce_output_regions=%u "
              "atlas_coalesce_dirty_avg_bytes=%llu atlas_coalesce_upload_avg_bytes=%llu "
              "atlas_coalesce_overhead_avg_bytes=%llu\n",
              atlas_buffer_counter.coalesce_input_regions,
              atlas_buffer_counter.coalesce_output_regions,
              atlas_buffer_counter.coalesce_dirty_bytes / frames,
              atlas_buffer_counter.coalesce_upload_bytes / frames,
              (atlas_buffer_counter.coalesce_upload_bytes >= atlas_buffer_counter.coalesce_dirty_bytes ?
               atlas_buffer_counter.coalesce_upload_bytes - atlas_buffer_counter.coalesce_dirty_bytes : 0) / frames);
      fprintf(file,
              "atlas_pages_allocated=%u "
              "atlas_page_switches=%u atlas_overflow_frames=%u atlas_streaming_flushes=%u\n",
              atlas_buffer_counter.pages_allocated,
              atlas_buffer_counter.page_switches,
              atlas_buffer_counter.overflow_pages,
              atlas_buffer_counter.stream_reloads);
      fprintf(file,
              "atlas_page0_generation=%u atlas_page0_live_area=%u "
              "atlas_page0_peak_live_area=%u atlas_page1_generation=%u "
              "atlas_page1_live_area=%u atlas_page1_peak_live_area=%u "
              "atlas_generation_increments=%u atlas_stale_rejections=%u "
              "atlas_persistent_demotions=%u atlas_transient_redecodes=%u\n",
              atlas_buffer_counter.page_generation[0],
              atlas_buffer_counter.page_live_area[0],
              atlas_buffer_counter.page_peak_live_area[0],
              atlas_buffer_counter.page_generation[1],
              atlas_buffer_counter.page_live_area[1],
              atlas_buffer_counter.page_peak_live_area[1],
              atlas_buffer_counter.generation_increments,
              atlas_buffer_counter.stale_rejections,
              atlas_buffer_counter.persistent_demotions,
              atlas_buffer_counter.transient_redecodes);
      fprintf(file,
              "exclusive_atlas_upload_avg_us=%llu "
              "exclusive_vdp1_decode_avg_us=%llu exclusive_vdp1_draw_avg_us=%llu "
              "exclusive_vdp2_decode_avg_us=%llu exclusive_vdp2_draw_avg_us=%llu\n",
              profile_exclusive_average(VITA_PROFILE_ATLAS_UPLOAD),
              profile_exclusive_average(VITA_PROFILE_VDP1_DECODE),
              profile_exclusive_average(VITA_PROFILE_VDP1_DRAW),
              profile_exclusive_average(VITA_PROFILE_VDP2_DECODE),
              profile_exclusive_average(VITA_PROFILE_VDP2_DRAW));
      fprintf(file,
              "atlas_vdp1_peak_height=%u atlas_vdp1_dirty_regions=%u "
              "atlas_vdp1_decoded_avg_bytes=%llu atlas_vdp1_merged_regions=%u "
              "atlas_vdp1_upload_calls=%u atlas_vdp1_upload_regions=%u "
              "atlas_vdp1_upload_avg_bytes=%llu atlas_vdp1_skipped_uploads=%u "
              "atlas_vdp1_fallback_uploads=%u atlas_vdp1_cache_hits=%u "
              "atlas_vdp1_cache_misses=%u "
              "atlas_vdp2_peak_height=%u atlas_vdp2_dirty_regions=%u "
              "atlas_vdp2_decoded_avg_bytes=%llu atlas_vdp2_merged_regions=%u "
              "atlas_vdp2_upload_calls=%u atlas_vdp2_upload_regions=%u "
              "atlas_vdp2_upload_avg_bytes=%llu atlas_vdp2_skipped_uploads=%u "
              "atlas_vdp2_fallback_uploads=%u atlas_vdp2_cache_hits=%u "
              "atlas_vdp2_cache_misses=%u atlas_vdp2_persistent_hits=%u "
              "atlas_vdp2_persistent_misses=%u atlas_vdp2_persistent_invalidations=%u "
              "atlas_vdp2_persistent_evictions=%u atlas_vdp2_persistent_fallbacks=%u "
              "atlas_vdp2_persistent_reused_avg_bytes=%llu "
              "atlas_vdp2_partial_refreshes=%u atlas_vdp2_partial_rows=%u "
              "atlas_vdp2_partial_avg_bytes=%llu\n",
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].peak_height,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].dirty_regions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].decoded_bytes / frames,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].merged_regions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].upload_calls,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].upload_regions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].upload_bytes / frames,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].skipped_uploads,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].fallback_uploads,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].cache_hits,
              atlas_counters[VITA_PROFILE_ATLAS_VDP1].cache_misses,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].peak_height,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].dirty_regions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].decoded_bytes / frames,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].merged_regions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].upload_calls,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].upload_regions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].upload_bytes / frames,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].skipped_uploads,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].fallback_uploads,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].cache_hits,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].cache_misses,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].persistent_hits,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].persistent_misses,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].persistent_invalidations,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].persistent_evictions,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].persistent_fallbacks,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].persistent_reused_bytes / frames,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].partial_refreshes,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].partial_rows,
              atlas_counters[VITA_PROFILE_ATLAS_VDP2].partial_bytes / frames);
      fprintf(file,
              "vdp2_sources other=%u,%llu,%u,%llu,%ux%u "
              "rotation=%u,%llu,%u,%llu,%ux%u "
              "rotation_line=%u,%llu,%u,%llu,%ux%u "
              "nbg0_bitmap=%u,%llu,%u,%llu,%ux%u "
              "nbg1_bitmap=%u,%llu,%u,%llu,%ux%u "
              "pattern=%u,%llu,%u,%llu,%ux%u\n",
              vdp2_source_counters[0].allocations, vdp2_source_counters[0].decoded_bytes / frames,
              vdp2_source_counters[0].upload_regions, vdp2_source_counters[0].upload_bytes / frames,
              vdp2_source_counters[0].max_width, vdp2_source_counters[0].max_height,
              vdp2_source_counters[1].allocations, vdp2_source_counters[1].decoded_bytes / frames,
              vdp2_source_counters[1].upload_regions, vdp2_source_counters[1].upload_bytes / frames,
              vdp2_source_counters[1].max_width, vdp2_source_counters[1].max_height,
              vdp2_source_counters[2].allocations, vdp2_source_counters[2].decoded_bytes / frames,
              vdp2_source_counters[2].upload_regions, vdp2_source_counters[2].upload_bytes / frames,
              vdp2_source_counters[2].max_width, vdp2_source_counters[2].max_height,
              vdp2_source_counters[3].allocations, vdp2_source_counters[3].decoded_bytes / frames,
              vdp2_source_counters[3].upload_regions, vdp2_source_counters[3].upload_bytes / frames,
              vdp2_source_counters[3].max_width, vdp2_source_counters[3].max_height,
              vdp2_source_counters[4].allocations, vdp2_source_counters[4].decoded_bytes / frames,
              vdp2_source_counters[4].upload_regions, vdp2_source_counters[4].upload_bytes / frames,
              vdp2_source_counters[4].max_width, vdp2_source_counters[4].max_height,
              vdp2_source_counters[5].allocations, vdp2_source_counters[5].decoded_bytes / frames,
              vdp2_source_counters[5].upload_regions, vdp2_source_counters[5].upload_bytes / frames,
              vdp2_source_counters[5].max_width, vdp2_source_counters[5].max_height);
      fprintf(file,
              "vdp2_cache_reasons new=%u ram=%u cram=%u state=%u dimensions=%u "
              "eviction=%u fallback=%u\n",
              vdp2_cache_reasons[0], vdp2_cache_reasons[1],
              vdp2_cache_reasons[2], vdp2_cache_reasons[3],
              vdp2_cache_reasons[4], vdp2_cache_reasons[5],
              vdp2_cache_reasons[6]);
      fclose(file);
   }
   memset(counters, 0, sizeof(counters));
   memset(atlas_counters, 0, sizeof(atlas_counters));
   memset(&atlas_buffer_counter, 0, sizeof(atlas_buffer_counter));
   memset(vdp2_source_counters, 0, sizeof(vdp2_source_counters));
   memset(vdp2_cache_reasons, 0, sizeof(vdp2_cache_reasons));
   frames = 0;
}
#endif

void VitaProfileInit(void)
{
#ifdef VITA_PROFILE
   FILE *file;
   memset(counters, 0, sizeof(counters));
   memset(atlas_counters, 0, sizeof(atlas_counters));
   memset(&atlas_buffer_counter, 0, sizeof(atlas_buffer_counter));
   memset(vdp2_source_counters, 0, sizeof(vdp2_source_counters));
   memset(vdp2_cache_reasons, 0, sizeof(vdp2_cache_reasons));
   atlas_phase_depth = 0;
   profile_stack_depth = 0;
   vdp2_source = VITA_PROFILE_VDP2_SOURCE_OTHER;
   frames = 0;
   file = fopen(PROFILE_PATH, "w");
   if (file) {
      fputs("# Yabause Vita aggregated timings (microseconds)\n", file);
      fclose(file);
   }
#endif
}

void VitaProfileBegin(VitaProfileSection section)
{
#ifdef VITA_PROFILE
   unsigned long long now;
   if ((unsigned int)section >= VITA_PROFILE_COUNT)
      return;
   now = sceKernelGetProcessTimeWide();
   counters[section].started = now;
   if (profile_stack_depth && profile_stack_depth < PROFILE_STACK_MAX)
      counters[profile_stack[profile_stack_depth - 1].section].exclusive_total +=
         now - profile_stack[profile_stack_depth - 1].resumed;
   if (profile_stack_depth < PROFILE_STACK_MAX) {
      profile_stack[profile_stack_depth].section = section;
      profile_stack[profile_stack_depth].started = now;
      profile_stack[profile_stack_depth].resumed = now;
      profile_stack_depth++;
   }
#else
   (void)section;
#endif
}

void VitaProfileEnd(VitaProfileSection section)
{
#ifdef VITA_PROFILE
   unsigned long long now;
   if ((unsigned int)section >= VITA_PROFILE_COUNT ||
       !counters[section].started)
      return;
   now = sceKernelGetProcessTimeWide();
   if (profile_stack_depth &&
       profile_stack[profile_stack_depth - 1].section == section) {
      ProfileStackEntry *entry = &profile_stack[profile_stack_depth - 1];
      counters[section].total += now - entry->started;
      counters[section].exclusive_total += now - entry->resumed;
      profile_stack_depth--;
      if (profile_stack_depth)
         profile_stack[profile_stack_depth - 1].resumed = now;
   }
   else {
      counters[section].total += now - counters[section].started;
      counters[section].exclusive_total += now - counters[section].started;
   }
   counters[section].calls++;
   counters[section].started = 0;
#else
   (void)section;
#endif
}

void VitaProfilePushAtlasPhase(VitaProfileAtlasPhase phase)
{
#ifdef VITA_PROFILE
   if ((unsigned int)phase > VITA_PROFILE_ATLAS_NONE &&
       (unsigned int)phase < VITA_PROFILE_ATLAS_COUNT &&
       atlas_phase_depth < ATLAS_PHASE_STACK_MAX)
      atlas_phase_stack[atlas_phase_depth++] = phase;
#else
   (void)phase;
#endif
}

void VitaProfilePopAtlasPhase(void)
{
#ifdef VITA_PROFILE
   if (atlas_phase_depth)
      atlas_phase_depth--;
#endif
}

VitaProfileAtlasPhase VitaProfileCurrentAtlasPhase(void)
{
#ifdef VITA_PROFILE
   return current_atlas_phase();
#else
   return VITA_PROFILE_ATLAS_NONE;
#endif
}

void VitaProfileRecordAtlasAllocation(unsigned int width, unsigned int height,
                                      unsigned int atlas_height)
{
#ifdef VITA_PROFILE
   AtlasCounter *counter;
   VitaProfileAtlasPhase phase = current_atlas_phase();
   if (phase == VITA_PROFILE_ATLAS_NONE)
      return;
   counter = &atlas_counters[phase];
   counter->decoded_bytes +=
      (unsigned long long)width * (unsigned long long)height * 4ULL;
   counter->dirty_regions++;
   if (atlas_height > counter->peak_height)
      counter->peak_height = atlas_height;
#else
   (void)width;
   (void)height;
   (void)atlas_height;
#endif
}

void VitaProfileRecordAtlasUploadBatch(void)
{
#ifdef VITA_PROFILE
   VitaProfileAtlasPhase phase = current_atlas_phase();
   if (phase != VITA_PROFILE_ATLAS_NONE)
      atlas_counters[phase].upload_calls++;
#endif
}

void VitaProfileRecordAtlasUploadRegion(VitaProfileAtlasPhase producer,
                                        unsigned int width, unsigned int height)
{
#ifdef VITA_PROFILE
   if ((unsigned int)producer <= VITA_PROFILE_ATLAS_NONE ||
       (unsigned int)producer >= VITA_PROFILE_ATLAS_COUNT)
      producer = current_atlas_phase();
   if (producer == VITA_PROFILE_ATLAS_NONE)
      return;
   atlas_counters[producer].upload_bytes +=
      (unsigned long long)width * (unsigned long long)height * 4ULL;
   atlas_counters[producer].upload_regions++;
#else
   (void)producer;
   (void)width;
   (void)height;
#endif
}

void VitaProfileRecordAtlasRegionsMerged(VitaProfileAtlasPhase producer,
                                         unsigned int count)
{
#ifdef VITA_PROFILE
   if ((unsigned int)producer <= VITA_PROFILE_ATLAS_NONE ||
       (unsigned int)producer >= VITA_PROFILE_ATLAS_COUNT)
      producer = current_atlas_phase();
   if (producer != VITA_PROFILE_ATLAS_NONE)
      atlas_counters[producer].merged_regions += count;
#else
   (void)producer;
   (void)count;
#endif
}

void VitaProfileRecordAtlasUploadSkipped(void)
{
#ifdef VITA_PROFILE
   VitaProfileAtlasPhase phase = current_atlas_phase();
   if (phase != VITA_PROFILE_ATLAS_NONE)
      atlas_counters[phase].skipped_uploads++;
#endif
}

void VitaProfileRecordAtlasUploadFallback(void)
{
#ifdef VITA_PROFILE
   VitaProfileAtlasPhase phase = current_atlas_phase();
   if (phase != VITA_PROFILE_ATLAS_NONE)
      atlas_counters[phase].fallback_uploads++;
#endif
}

void VitaProfileRecordCacheResult(int hit)
{
#ifdef VITA_PROFILE
   if (current_atlas_phase() == VITA_PROFILE_ATLAS_NONE)
      return;
   if (hit)
      atlas_counters[current_atlas_phase()].cache_hits++;
   else
      atlas_counters[current_atlas_phase()].cache_misses++;
#else
   (void)hit;
#endif
}

void VitaProfileRecordVdp2PersistentCache(int event, unsigned int bytes)
{
#ifdef VITA_PROFILE
   AtlasCounter *c = &atlas_counters[VITA_PROFILE_ATLAS_VDP2];
   switch (event) {
      case 0: c->persistent_misses++; break;
      case 1: c->persistent_hits++; c->persistent_reused_bytes += bytes; break;
      case 2: c->persistent_invalidations++; break;
      case 3: c->persistent_evictions++; break;
      case 4: c->persistent_fallbacks++; break;
   }
#else
   (void)event;
   (void)bytes;
#endif
}

void VitaProfileRecordAtlasDirtyGenerated(unsigned int width,
                                          unsigned int height,
                                          int persistent)
{
#ifdef VITA_PROFILE
   unsigned long long bytes = (unsigned long long)width * height * 4ULL;
   atlas_buffer_counter.generated_regions++;
   atlas_buffer_counter.generated_bytes += bytes;
   if (persistent) {
      atlas_buffer_counter.persistent_generated_regions++;
      atlas_buffer_counter.persistent_generated_bytes += bytes;
   }
   else {
      atlas_buffer_counter.transient_generated_regions++;
      atlas_buffer_counter.transient_generated_bytes += bytes;
   }
#else
   (void)width;
   (void)height;
   (void)persistent;
#endif
}

void VitaProfileRecordAtlasJournalClasses(
   unsigned int selected_persistent_regions,
   unsigned long long selected_persistent_bytes,
   unsigned int selected_transient_regions,
   unsigned long long selected_transient_bytes,
   unsigned int carried_persistent_regions,
   unsigned long long carried_persistent_bytes,
   unsigned int carried_transient_regions,
   unsigned long long carried_transient_bytes)
{
#ifdef VITA_PROFILE
   atlas_buffer_counter.selected_persistent_regions += selected_persistent_regions;
   atlas_buffer_counter.selected_persistent_bytes += selected_persistent_bytes;
   atlas_buffer_counter.selected_transient_regions += selected_transient_regions;
   atlas_buffer_counter.selected_transient_bytes += selected_transient_bytes;
   atlas_buffer_counter.carried_persistent_regions += carried_persistent_regions;
   atlas_buffer_counter.carried_persistent_bytes += carried_persistent_bytes;
   atlas_buffer_counter.carried_transient_regions += carried_transient_regions;
   atlas_buffer_counter.carried_transient_bytes += carried_transient_bytes;
#else
   (void)selected_persistent_regions;
   (void)selected_persistent_bytes;
   (void)selected_transient_regions;
   (void)selected_transient_bytes;
   (void)carried_persistent_regions;
   (void)carried_persistent_bytes;
   (void)carried_transient_regions;
   (void)carried_transient_bytes;
#endif
}

void VitaProfileRecordAtlasBufferState(unsigned int mode,
                                       unsigned int active_buffer,
                                       unsigned int selected_regions,
                                       unsigned long long selected_bytes,
                                       unsigned int carried_regions,
                                       unsigned long long carried_bytes,
                                       int resync, int overflow)
{
#ifdef VITA_PROFILE
   atlas_buffer_counter.mode = mode;
   if (active_buffer < 2)
      atlas_buffer_counter.buffer_uses[active_buffer]++;
   atlas_buffer_counter.selected_regions += selected_regions;
   atlas_buffer_counter.selected_bytes += selected_bytes;
   atlas_buffer_counter.carried_regions += carried_regions;
   atlas_buffer_counter.carried_bytes += carried_bytes;
   if (resync)
      atlas_buffer_counter.resyncs++;
   if (overflow)
      atlas_buffer_counter.overflows++;
#else
   (void)mode;
   (void)active_buffer;
   (void)selected_regions;
   (void)selected_bytes;
   (void)carried_regions;
   (void)carried_bytes;
   (void)resync;
   (void)overflow;
#endif
}

void VitaProfileRecordAtlasCoalescing(unsigned int input_regions,
                                      unsigned int output_regions,
                                      unsigned long long dirty_bytes,
                                      unsigned long long upload_bytes)
{
#ifdef VITA_PROFILE
   atlas_buffer_counter.coalesce_input_regions += input_regions;
   atlas_buffer_counter.coalesce_output_regions += output_regions;
   atlas_buffer_counter.coalesce_dirty_bytes += dirty_bytes;
   atlas_buffer_counter.coalesce_upload_bytes += upload_bytes;
#else
   (void)input_regions; (void)output_regions;
   (void)dirty_bytes; (void)upload_bytes;
#endif
}

void VitaProfileRecordAtlasPaging(unsigned int pages_allocated,
                                  unsigned int page_switches,
                                  unsigned int overflow_pages,
                                  unsigned int stream_reloads)
{
#ifdef VITA_PROFILE
   atlas_buffer_counter.pages_allocated += pages_allocated;
   atlas_buffer_counter.page_switches += page_switches;
   atlas_buffer_counter.overflow_pages += overflow_pages;
   atlas_buffer_counter.stream_reloads += stream_reloads;
#else
   (void)pages_allocated; (void)page_switches;
   (void)overflow_pages; (void)stream_reloads;
#endif
}

void VitaProfileRecordAtlasPageState(unsigned int page,
                                     unsigned int generation,
                                     unsigned int live_area,
                                     unsigned int peak_live_area)
{
#ifdef VITA_PROFILE
   if (page >= 2)
      return;
   atlas_buffer_counter.page_generation[page] = generation;
   atlas_buffer_counter.page_live_area[page] = live_area;
   if (peak_live_area > atlas_buffer_counter.page_peak_live_area[page])
      atlas_buffer_counter.page_peak_live_area[page] = peak_live_area;
#else
   (void)page; (void)generation; (void)live_area; (void)peak_live_area;
#endif
}

void VitaProfileRecordAtlasPageEvents(unsigned int generation_increments,
                                      unsigned int stale_rejections,
                                      unsigned int persistent_demotions,
                                      unsigned int transient_redecodes)
{
#ifdef VITA_PROFILE
   atlas_buffer_counter.generation_increments += generation_increments;
   atlas_buffer_counter.stale_rejections += stale_rejections;
   atlas_buffer_counter.persistent_demotions += persistent_demotions;
   atlas_buffer_counter.transient_redecodes += transient_redecodes;
#else
   (void)generation_increments; (void)stale_rejections;
   (void)persistent_demotions; (void)transient_redecodes;
#endif
}

void VitaProfileRecordVdp2PartialRefresh(unsigned int rows,
                                         unsigned int bytes)
{
#ifdef VITA_PROFILE
   AtlasCounter *c = &atlas_counters[VITA_PROFILE_ATLAS_VDP2];
   c->partial_refreshes++;
   c->partial_rows += rows;
   c->partial_bytes += bytes;
#else
   (void)rows;
   (void)bytes;
#endif
}

void VitaProfileRecordVdp2CacheReason(VitaProfileVdp2CacheReason reason)
{
#ifdef VITA_PROFILE
   if ((unsigned int)reason < VITA_PROFILE_VDP2_CACHE_REASON_COUNT)
      vdp2_cache_reasons[reason]++;
#else
   (void)reason;
#endif
}

void VitaProfileSetVdp2Source(VitaProfileVdp2Source source)
{
#ifdef VITA_PROFILE
   if ((unsigned int)source < VITA_PROFILE_VDP2_SOURCE_COUNT)
      vdp2_source = source;
#else
   (void)source;
#endif
}

VitaProfileVdp2Source VitaProfileCurrentVdp2Source(void)
{
#ifdef VITA_PROFILE
   return vdp2_source;
#else
   return VITA_PROFILE_VDP2_SOURCE_OTHER;
#endif
}

void VitaProfileRecordVdp2SourceAllocation(VitaProfileVdp2Source source,
                                           unsigned int width,
                                           unsigned int height)
{
#ifdef VITA_PROFILE
   Vdp2SourceCounter *counter;
   if ((unsigned int)source >= VITA_PROFILE_VDP2_SOURCE_COUNT)
      return;
   counter = &vdp2_source_counters[source];
   counter->allocations++;
   counter->decoded_bytes += (unsigned long long)width * height * 4ULL;
   if (width * height > counter->max_width * counter->max_height) {
      counter->max_width = width;
      counter->max_height = height;
   }
#else
   (void)source;
   (void)width;
   (void)height;
#endif
}

void VitaProfileRecordVdp2SourceUpload(VitaProfileVdp2Source source,
                                       unsigned int width,
                                       unsigned int height)
{
#ifdef VITA_PROFILE
   if ((unsigned int)source < VITA_PROFILE_VDP2_SOURCE_COUNT) {
      vdp2_source_counters[source].upload_regions++;
      vdp2_source_counters[source].upload_bytes +=
         (unsigned long long)width * height * 4ULL;
   }
#else
   (void)source;
   (void)width;
   (void)height;
#endif
}

void VitaProfileFrameComplete(void)
{
#ifdef VITA_PROFILE
   if (++frames >= PROFILE_INTERVAL)
      flush_profile();
#endif
}

void VitaProfileShutdown(void)
{
#ifdef VITA_PROFILE
   flush_profile();
#endif
}
