#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include "../ygl.h"
#include "vitaprofile.h"

#define DIRECT_PROFILE_PATH "ux0:data/yabause/profile.log"
#define DIRECT_MAX_RECTS 1024U
#define DIRECT_MULTI_INPUT_LIMIT 48U
#define DIRECT_MAX_UPLOADS 12U
#define DIRECT_SOFT_NUMERATOR 3ULL
#define DIRECT_SOFT_DENOMINATOR 2ULL
#define DIRECT_HARD_NUMERATOR 2ULL
#define DIRECT_HARD_DENOMINATOR 1ULL
#define DIRECT_MIN_SAVINGS_NUMERATOR 9ULL
#define DIRECT_MIN_SAVINGS_DENOMINATOR 10ULL
#define DIRECT_REPORT_ATTEMPTS 300ULL

#define DIRECT_MODE_STOCK 0
#define DIRECT_MODE_BOUNDING 1
#define DIRECT_MODE_MULTI 2
#ifndef VITA_ATLAS_DIRECT_MODE
#define VITA_ATLAS_DIRECT_MODE DIRECT_MODE_MULTI
#endif

typedef struct {
   unsigned int x;
   unsigned int y;
   unsigned int w;
   unsigned int h;
   VitaProfileAtlasPhase producer;
} DirectRect;

typedef struct {
   DirectRect rects[DIRECT_MAX_RECTS];
   unsigned int count;
   int overflow;
   const unsigned int *cpu_base;
   int initialized;
} DirectJournal;

typedef struct {
   unsigned long long attempts;
   unsigned long long optimized;
   unsigned long long passthrough;
   unsigned long long fragmented_fallbacks;
   unsigned long long empty_fallbacks;
   unsigned long long exact_allocations;
   unsigned long long persistent_refreshes;
   unsigned long long partial_suppressed;
   unsigned long long original_bytes;
   unsigned long long submitted_bytes;
   unsigned long long submitted_calls;
   unsigned long long input_rects;
   unsigned long long output_rects;
   unsigned long long soft_merges;
   unsigned long long hard_merges;
   unsigned long long forced_merges;
   unsigned long long planner_us;
   unsigned long long packed_uploads;
   unsigned long long pack_failures;
   unsigned long long pack_us;
} DirectCounters;

static DirectJournal direct_journal[2];
static DirectRect direct_work[DIRECT_MAX_RECTS];
static DirectCounters direct_counters;
static unsigned int *direct_pack_buffer;
static size_t direct_pack_capacity_pixels;

#ifdef VITA_TEXTURE_CACHE
static unsigned int direct_partial_x;
static unsigned int direct_partial_y;
static int direct_partial_pending;
#endif

static unsigned long long DirectRectArea(const DirectRect *rect)
{
   return (unsigned long long)rect->w * rect->h;
}

static unsigned long long DirectRectBytes(const DirectRect *rect)
{
   return DirectRectArea(rect) * sizeof(unsigned int);
}

static void DirectClearJournal(unsigned int page)
{
   if (page >= 2U)
      return;
   direct_journal[page].count = 0;
   direct_journal[page].overflow = 0;
}

static void DirectClearAllJournals(void)
{
   DirectClearJournal(0);
   DirectClearJournal(1);
#ifdef VITA_TEXTURE_CACHE
   direct_partial_pending = 0;
#endif
}

static void DirectRefreshCpuBase(unsigned int page,
                                 const unsigned int *base)
{
   DirectJournal *journal;
   if (page >= 2U)
      return;
   journal = &direct_journal[page];
   if (journal->cpu_base != base) {
      journal->cpu_base = base;
      journal->initialized = 0;
      DirectClearJournal(page);
   }
}

static void DirectRecordRect(unsigned int page, unsigned int x,
                             unsigned int y, unsigned int w,
                             unsigned int h,
                             VitaProfileAtlasPhase producer)
{
   DirectJournal *journal;
   DirectRect *previous;
   DirectRect *rect;

   if (page >= 2U || !w || !h || !YglTM)
      return;
   if (x >= YglTM->width || y >= YglTM->height)
      return;
   if (w > YglTM->width - x)
      w = YglTM->width - x;
   if (h > YglTM->height - y)
      h = YglTM->height - y;
   if (!w || !h)
      return;

   journal = &direct_journal[page];
   if (journal->overflow)
      return;

   if (journal->count) {
      previous = &journal->rects[journal->count - 1U];
      if (previous->producer == producer && previous->y == y &&
          previous->h == h && previous->x + previous->w == x) {
         previous->w += w;
         return;
      }
   }

   if (journal->count >= DIRECT_MAX_RECTS) {
      journal->overflow = 1;
      return;
   }

   rect = &journal->rects[journal->count++];
   rect->x = x;
   rect->y = y;
   rect->w = w;
   rect->h = h;
   rect->producer = producer;
}

static int DirectGetPageBase(unsigned int page,
                             const unsigned int **base_out)
{
#ifdef VITA_ATLAS_PAGED
   if (!YglTM || !YglTM->pages || page >= YglTM->pageCount || page >= 2U ||
       !YglTM->pages[page].texture)
      return 0;
   *base_out = YglTM->pages[page].texture;
#else
   (void)page;
   if (!YglTM || !YglTM->texture)
      return 0;
   *base_out = YglTM->texture;
#endif
   return 1;
}

static int DirectFindPageForPixels(const unsigned int *pixels,
                                   unsigned int *page_out,
                                   const unsigned int **base_out)
{
#ifdef VITA_ATLAS_PAGED
   unsigned int page;
   if (!YglTM || !YglTM->pages)
      return 0;
   for (page = 0; page < YglTM->pageCount && page < 2U; page++) {
      const unsigned int *base = YglTM->pages[page].texture;
      const unsigned int *end;
      if (!base)
         continue;
      end = base + (size_t)YglTM->width * YglTM->height;
      if (pixels >= base && pixels < end) {
         *page_out = page;
         *base_out = base;
         return 1;
      }
   }
#else
   if (YglTM && YglTM->texture) {
      const unsigned int *base = YglTM->texture;
      const unsigned int *end =
         base + (size_t)YglTM->width * YglTM->height;
      if (pixels >= base && pixels < end) {
         *page_out = (_Ygl && _Ygl->activeAtlas < 2U) ?
                     _Ygl->activeAtlas : 0U;
         *base_out = base;
         return 1;
      }
   }
#endif
   return 0;
}

static void DirectRecordTextureAllocation(YglTexture *output,
                                          unsigned int w,
                                          unsigned int h)
{
   const unsigned int *base;
   const unsigned int *pixels;
   size_t offset;
   size_t capacity;
   unsigned int page;
   unsigned int x;
   unsigned int y;

   if (!YglTM || !output || !output->textdata || !w || !h)
      return;

#ifdef VITA_ATLAS_PAGED
   page = output->atlasPage;
#else
   page = (_Ygl && _Ygl->activeAtlas < 2U) ? _Ygl->activeAtlas : 0U;
#endif
   if (!DirectGetPageBase(page, &base))
      return;

   pixels = (const unsigned int *)output->textdata;
   capacity = (size_t)YglTM->width * YglTM->height;
   if (pixels < base || pixels >= base + capacity)
      return;

   offset = (size_t)(pixels - base);
   x = (unsigned int)(offset % YglTM->width);
   y = (unsigned int)(offset / YglTM->width);
   if (x + w > YglTM->width || y + h > YglTM->height)
      return;

   DirectRefreshCpuBase(page, base);

#ifdef VITA_TEXTURE_CACHE
   if (direct_partial_pending) {
      if (page == 0U && x == direct_partial_x && y == direct_partial_y) {
         direct_partial_pending = 0;
         direct_counters.partial_suppressed++;
         return;
      }
      direct_partial_pending = 0;
   }
#endif

   DirectRecordRect(page, x, y, w, h, VitaProfileCurrentAtlasPhase());
   direct_counters.exact_allocations++;
}

static void DirectBounds(const DirectRect *a, const DirectRect *b,
                         unsigned int *left, unsigned int *top,
                         unsigned int *right, unsigned int *bottom)
{
   unsigned int ar = a->x + a->w;
   unsigned int br = b->x + b->w;
   unsigned int ab = a->y + a->h;
   unsigned int bb = b->y + b->h;
   *left = a->x < b->x ? a->x : b->x;
   *top = a->y < b->y ? a->y : b->y;
   *right = ar > br ? ar : br;
   *bottom = ab > bb ? ab : bb;
}

static unsigned long long DirectMergedArea(const DirectRect *a,
                                           const DirectRect *b)
{
   unsigned int left, top, right, bottom;
   DirectBounds(a, b, &left, &top, &right, &bottom);
   return (unsigned long long)(right - left) * (bottom - top);
}

static void DirectMergePair(unsigned int *count, unsigned int a_index,
                            unsigned int b_index)
{
   DirectRect *a = &direct_work[a_index];
   DirectRect *b = &direct_work[b_index];
   unsigned int left, top, right, bottom;
   DirectBounds(a, b, &left, &top, &right, &bottom);
   a->x = left;
   a->y = top;
   a->w = right - left;
   a->h = bottom - top;
   if (a->producer != b->producer)
      a->producer = VITA_PROFILE_ATLAS_NONE;
   memmove(b, b + 1, (*count - b_index - 1U) * sizeof(*b));
   (*count)--;
}

static int DirectRectContained(const DirectRect *small,
                               const DirectRect *large)
{
   return small->x >= large->x && small->y >= large->y &&
          small->x + small->w <= large->x + large->w &&
          small->y + small->h <= large->y + large->h;
}

static unsigned int DirectRemoveContained(unsigned int count)
{
   unsigned int i = 0;
   while (i < count) {
      unsigned int j;
      int removed = 0;
      for (j = 0; j < count; j++) {
         if (i == j)
            continue;
         if (DirectRectContained(&direct_work[i], &direct_work[j])) {
            memmove(&direct_work[i], &direct_work[i + 1U],
                    (count - i - 1U) * sizeof(direct_work[0]));
            count--;
            removed = 1;
            break;
         }
      }
      if (!removed)
         i++;
   }
   return count;
}

static unsigned int DirectPlanRects(unsigned int count,
                                    unsigned long long *soft_merges,
                                    unsigned long long *hard_merges,
                                    unsigned long long *forced_merges)
{
   count = DirectRemoveContained(count);

   while (count > DIRECT_MAX_UPLOADS) {
      unsigned int i, j;
      unsigned int best_i = ~0U;
      unsigned int best_j = ~0U;
      unsigned long long best_extra = ~0ULL;
      int best_class = 3;

      for (i = 0; i < count; i++) {
         for (j = i + 1U; j < count; j++) {
            unsigned long long source_area =
               DirectRectArea(&direct_work[i]) +
               DirectRectArea(&direct_work[j]);
            unsigned long long merged_area =
               DirectMergedArea(&direct_work[i], &direct_work[j]);
            unsigned long long extra = merged_area > source_area ?
                                       merged_area - source_area : 0;
            int merge_class;

            if (merged_area * DIRECT_SOFT_DENOMINATOR <=
                source_area * DIRECT_SOFT_NUMERATOR)
               merge_class = 0;
            else if (merged_area * DIRECT_HARD_DENOMINATOR <=
                     source_area * DIRECT_HARD_NUMERATOR)
               merge_class = 1;
            else
               merge_class = 2;

            if (merge_class < best_class ||
                (merge_class == best_class && extra < best_extra)) {
               best_class = merge_class;
               best_extra = extra;
               best_i = i;
               best_j = j;
            }
         }
      }

      if (best_i == ~0U)
         break;
      if (best_class == 0)
         (*soft_merges)++;
      else if (best_class == 1)
         (*hard_merges)++;
      else
         (*forced_merges)++;
      DirectMergePair(&count, best_i, best_j);
   }

   return count;
}

static unsigned int DirectBoundingRect(unsigned int count)
{
   unsigned int i;
   DirectRect bounds;
   if (!count)
      return 0;
   bounds = direct_work[0];
   for (i = 1; i < count; i++) {
      unsigned int left, top, right, bottom;
      DirectBounds(&bounds, &direct_work[i], &left, &top, &right, &bottom);
      bounds.x = left;
      bounds.y = top;
      bounds.w = right - left;
      bounds.h = bottom - top;
      if (bounds.producer != direct_work[i].producer)
         bounds.producer = VITA_PROFILE_ATLAS_NONE;
   }
   direct_work[0] = bounds;
   return 1;
}

static int DirectEnsurePackCapacity(size_t pixels)
{
   unsigned int *resized;
   size_t capacity;

   if (pixels <= direct_pack_capacity_pixels)
      return 1;

   capacity = direct_pack_capacity_pixels ? direct_pack_capacity_pixels : 16384U;
   while (capacity < pixels) {
      size_t next = capacity * 2U;
      if (next <= capacity) {
         capacity = pixels;
         break;
      }
      capacity = next;
   }

   resized = (unsigned int *)realloc(
      direct_pack_buffer, capacity * sizeof(unsigned int));
   if (!resized)
      return 0;

   direct_pack_buffer = resized;
   direct_pack_capacity_pixels = capacity;
   return 1;
}

static int DirectPackBoundingRect(const unsigned int *base,
                                  const DirectRect *rect)
{
   unsigned int row;
   size_t pixels = (size_t)rect->w * rect->h;
   size_t row_bytes = (size_t)rect->w * sizeof(unsigned int);
   unsigned long long started;

   if (!base || !rect->w || !rect->h || !YglTM ||
       !DirectEnsurePackCapacity(pixels)) {
      direct_counters.pack_failures++;
      return 0;
   }

   started = sceKernelGetProcessTimeWide();
   for (row = 0; row < rect->h; row++) {
      const unsigned int *src =
         base + (size_t)(rect->y + row) * YglTM->width + rect->x;
      unsigned int *dst = direct_pack_buffer + (size_t)row * rect->w;
      memcpy(dst, src, row_bytes);
   }
   direct_counters.pack_us += sceKernelGetProcessTimeWide() - started;
   direct_counters.packed_uploads++;
   return 1;
}

static void DirectReportIfReady(void)
{
   FILE *file;
   if (direct_counters.attempts < DIRECT_REPORT_ATTEMPTS)
      return;
   file = fopen(DIRECT_PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "atlas_direct mode=%u cutoff=%u attempts=%llu optimized=%llu "
              "passthrough=%llu fragmented_fallbacks=%llu empty_fallbacks=%llu "
              "exact_allocations=%llu persistent_refreshes=%llu "
              "partial_suppressed=%llu original_avg_bytes=%llu "
              "submitted_avg_bytes=%llu submitted_calls_avg_x100=%llu "
              "input_rects_avg_x100=%llu output_rects_avg_x100=%llu "
              "soft_merges=%llu hard_merges=%llu forced_merges=%llu "
              "planner_avg_us=%llu packed_uploads=%llu pack_failures=%llu "
              "pack_avg_us=%llu\n",
              (unsigned int)VITA_ATLAS_DIRECT_MODE,
              (unsigned int)DIRECT_MULTI_INPUT_LIMIT,
              direct_counters.attempts,
              direct_counters.optimized,
              direct_counters.passthrough,
              direct_counters.fragmented_fallbacks,
              direct_counters.empty_fallbacks,
              direct_counters.exact_allocations,
              direct_counters.persistent_refreshes,
              direct_counters.partial_suppressed,
              direct_counters.original_bytes / direct_counters.attempts,
              direct_counters.submitted_bytes / direct_counters.attempts,
              direct_counters.submitted_calls * 100ULL /
                 direct_counters.attempts,
              direct_counters.input_rects * 100ULL /
                 direct_counters.attempts,
              direct_counters.output_rects * 100ULL /
                 direct_counters.attempts,
              direct_counters.soft_merges,
              direct_counters.hard_merges,
              direct_counters.forced_merges,
              direct_counters.planner_us / direct_counters.attempts,
              direct_counters.packed_uploads,
              direct_counters.pack_failures,
              direct_counters.packed_uploads ?
                 direct_counters.pack_us / direct_counters.packed_uploads : 0);
      fclose(file);
   }
   memset(&direct_counters, 0, sizeof(direct_counters));
}

extern void __real_glTexSubImage2D(GLenum target, GLint level,
                                   GLint xoffset, GLint yoffset,
                                   GLsizei width, GLsizei height,
                                   GLenum format, GLenum type,
                                   const GLvoid *pixels);

static void DirectPassthrough(GLenum target, GLint level,
                              GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height,
                              GLenum format, GLenum type,
                              const GLvoid *pixels,
                              unsigned long long original_bytes)
{
   __real_glTexSubImage2D(target, level, xoffset, yoffset,
                          width, height, format, type, pixels);
   direct_counters.passthrough++;
   direct_counters.submitted_bytes += original_bytes;
   direct_counters.submitted_calls++;
}

#ifdef VITA_PROFILE
extern float *VitaProfileWrappedYglQuad(YglSprite *input,
                                        YglTexture *output,
                                        YglCache *cache);
extern int VitaProfileWrappedYglQuadGrowShading(YglSprite *input,
                                                 YglTexture *output,
                                                 float *colors,
                                                 YglCache *cache);
extern void VitaProfileWrappedYglQuadOffset(YglSprite *input,
                                             YglTexture *output,
                                             YglCache *cache,
                                             int cx, int cy,
                                             float sx, float sy);
#else
extern float *__real_YglQuad(YglSprite *input, YglTexture *output,
                             YglCache *cache);
extern int __real_YglQuadGrowShading(YglSprite *input, YglTexture *output,
                                     float *colors, YglCache *cache);
extern void __real_YglQuadOffset(YglSprite *input, YglTexture *output,
                                 YglCache *cache, int cx, int cy,
                                 float sx, float sy);
#endif

float *__wrap_YglQuad(YglSprite *input, YglTexture *output, YglCache *cache)
{
   float *result;
#ifdef VITA_PROFILE
   result = VitaProfileWrappedYglQuad(input, output, cache);
#else
   result = __real_YglQuad(input, output, cache);
#endif
   if (input)
      DirectRecordTextureAllocation(output, input->w, input->h);
   return result;
}

int __wrap_YglQuadGrowShading(YglSprite *input, YglTexture *output,
                              float *colors, YglCache *cache)
{
   int result;
#ifdef VITA_PROFILE
   result = VitaProfileWrappedYglQuadGrowShading(input, output,
                                                  colors, cache);
#else
   result = __real_YglQuadGrowShading(input, output, colors, cache);
#endif
   if (input)
      DirectRecordTextureAllocation(output, input->w, input->h);
   return result;
}

void __wrap_YglQuadOffset(YglSprite *input, YglTexture *output,
                          YglCache *cache, int cx, int cy,
                          float sx, float sy)
{
#ifdef VITA_PROFILE
   VitaProfileWrappedYglQuadOffset(input, output, cache, cx, cy, sx, sy);
#else
   __real_YglQuadOffset(input, output, cache, cx, cy, sx, sy);
#endif
   if (input)
      DirectRecordTextureAllocation(output, input->w, input->h);
}

#ifdef VITA_TEXTURE_CACHE
extern void __real_YglVitaForcePersistentPartialAllocation(unsigned int x,
                                                            unsigned int y);
void __wrap_YglVitaForcePersistentPartialAllocation(unsigned int x,
                                                     unsigned int y)
{
   direct_partial_x = x;
   direct_partial_y = y;
   direct_partial_pending = 1;
   __real_YglVitaForcePersistentPartialAllocation(x, y);
}

extern void __real_YglVitaMarkPersistentDirty(unsigned int x, unsigned int y,
                                               unsigned int width,
                                               unsigned int height);
void __wrap_YglVitaMarkPersistentDirty(unsigned int x, unsigned int y,
                                       unsigned int width,
                                       unsigned int height)
{
   const unsigned int *base;
   __real_YglVitaMarkPersistentDirty(x, y, width, height);
#ifdef VITA_VGL_INPLACE_TEXTURE_UPDATES
   if (DirectGetPageBase(0, &base)) {
      DirectRefreshCpuBase(0, base);
      DirectRecordRect(0, x, y, width, height,
                       VitaProfileCurrentAtlasPhase());
      direct_counters.persistent_refreshes++;
   }
#endif
   direct_partial_pending = 0;
}
#endif

extern void __real_YglReset(void);
void __wrap_YglReset(void)
{
   DirectClearAllJournals();
   __real_YglReset();
}

void __wrap_glTexSubImage2D(GLenum target, GLint level,
                            GLint xoffset, GLint yoffset,
                            GLsizei width, GLsizei height,
                            GLenum format, GLenum type,
                            const GLvoid *pixels)
{
#ifdef VITA_VGL_INPLACE_TEXTURE_UPDATES
   const unsigned int *source = (const unsigned int *)pixels;
   const unsigned int *base = NULL;
   unsigned int page = 0;
   DirectJournal *journal;
   unsigned int i;
   unsigned int count = 0;
   unsigned int output_count = 0;
   unsigned long long planned_bytes = 0;
   unsigned long long original_bytes;
   unsigned long long soft_merges = 0;
   unsigned long long hard_merges = 0;
   unsigned long long forced_merges = 0;
   unsigned long long planner_started;

   if (target != GL_TEXTURE_2D || level != 0 || xoffset != 0 ||
       !YglTM || !pixels || width <= 0 || height <= 0 ||
       (unsigned int)width != YglTM->width || format != GL_RGBA ||
       type != GL_UNSIGNED_BYTE ||
       !DirectFindPageForPixels(source, &page, &base)) {
      __real_glTexSubImage2D(target, level, xoffset, yoffset,
                             width, height, format, type, pixels);
      return;
   }

   DirectRefreshCpuBase(page, base);
   journal = &direct_journal[page];
   original_bytes =
      (unsigned long long)width * height * sizeof(unsigned int);
   direct_counters.attempts++;
   direct_counters.original_bytes += original_bytes;

   if (VITA_ATLAS_DIRECT_MODE == DIRECT_MODE_STOCK ||
       !journal->initialized || journal->overflow || !journal->count) {
      if (!journal->count)
         direct_counters.empty_fallbacks++;
      DirectPassthrough(target, level, xoffset, yoffset,
                        width, height, format, type, pixels, original_bytes);
      journal->initialized = 1;
      DirectClearJournal(page);
      DirectReportIfReady();
      return;
   }

   planner_started = sceKernelGetProcessTimeWide();
   for (i = 0; i < journal->count && count < DIRECT_MAX_RECTS; i++) {
      DirectRect rect = journal->rects[i];
      unsigned int call_top = (unsigned int)yoffset;
      unsigned int call_bottom = call_top + (unsigned int)height;
      unsigned int rect_bottom = rect.y + rect.h;
      unsigned int top = rect.y > call_top ? rect.y : call_top;
      unsigned int bottom = rect_bottom < call_bottom ? rect_bottom : call_bottom;
      if (top >= bottom)
         continue;
      rect.y = top;
      rect.h = bottom - top;
      direct_work[count++] = rect;
   }
   direct_counters.input_rects += count;

   if (!count) {
      direct_counters.planner_us +=
         sceKernelGetProcessTimeWide() - planner_started;
      direct_counters.empty_fallbacks++;
      DirectPassthrough(target, level, xoffset, yoffset,
                        width, height, format, type, pixels, original_bytes);
      journal->initialized = 1;
      DirectClearJournal(page);
      DirectReportIfReady();
      return;
   }

   if (VITA_ATLAS_DIRECT_MODE == DIRECT_MODE_BOUNDING) {
      output_count = DirectBoundingRect(count);
   }
   else {
      if (count > DIRECT_MULTI_INPUT_LIMIT) {
         direct_counters.fragmented_fallbacks++;
         direct_counters.planner_us +=
            sceKernelGetProcessTimeWide() - planner_started;
         DirectPassthrough(target, level, xoffset, yoffset,
                           width, height, format, type, pixels, original_bytes);
         journal->initialized = 1;
         DirectClearJournal(page);
         DirectReportIfReady();
         return;
      }
      output_count = DirectPlanRects(count, &soft_merges,
                                     &hard_merges, &forced_merges);
   }

   direct_counters.planner_us +=
      sceKernelGetProcessTimeWide() - planner_started;
   direct_counters.soft_merges += soft_merges;
   direct_counters.hard_merges += hard_merges;
   direct_counters.forced_merges += forced_merges;

   for (i = 0; i < output_count; i++)
      planned_bytes += DirectRectBytes(&direct_work[i]);

   if (!output_count ||
       planned_bytes * DIRECT_MIN_SAVINGS_DENOMINATOR >=
          original_bytes * DIRECT_MIN_SAVINGS_NUMERATOR) {
      DirectPassthrough(target, level, xoffset, yoffset,
                        width, height, format, type, pixels, original_bytes);
   }
   else if (VITA_ATLAS_DIRECT_MODE == DIRECT_MODE_BOUNDING) {
      DirectRect *rect = &direct_work[0];
      if (!DirectPackBoundingRect(base, rect)) {
         DirectPassthrough(target, level, xoffset, yoffset,
                           width, height, format, type, pixels, original_bytes);
      }
      else {
         /* Diagnostic path: tightly pack the single bounding rectangle so
          * VitaGL sees ordinary contiguous RGBA input with no row stride. */
         glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
         __real_glTexSubImage2D(GL_TEXTURE_2D, 0,
                                (GLint)rect->x, (GLint)rect->y,
                                (GLsizei)rect->w, (GLsizei)rect->h,
                                GL_RGBA, GL_UNSIGNED_BYTE,
                                direct_pack_buffer);
         direct_counters.optimized++;
         direct_counters.submitted_bytes += planned_bytes;
         direct_counters.submitted_calls++;
         direct_counters.output_rects++;
      }
   }
   else {
      /* Multi mode intentionally retains the direct row-stride path so the
       * Bounding test isolates packing/stride behavior from journaling. */
      glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)YglTM->width);
      for (i = 0; i < output_count; i++) {
         const unsigned int *rect_pixels =
            base + (size_t)direct_work[i].y * YglTM->width +
            direct_work[i].x;
         __real_glTexSubImage2D(GL_TEXTURE_2D, 0,
                                (GLint)direct_work[i].x,
                                (GLint)direct_work[i].y,
                                (GLsizei)direct_work[i].w,
                                (GLsizei)direct_work[i].h,
                                GL_RGBA, GL_UNSIGNED_BYTE, rect_pixels);
      }
      glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

      direct_counters.optimized++;
      direct_counters.submitted_bytes += planned_bytes;
      direct_counters.submitted_calls += output_count;
      direct_counters.output_rects += output_count;
   }

   journal->initialized = 1;
   DirectClearJournal(page);
   DirectReportIfReady();
   return;
#else
   __real_glTexSubImage2D(target, level, xoffset, yoffset,
                          width, height, format, type, pixels);
#endif
}
