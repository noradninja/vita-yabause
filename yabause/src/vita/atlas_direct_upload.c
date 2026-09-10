#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include "../ygl.h"
#include "vitaprofile.h"

#define DIRECT_PROFILE_PATH "ux0:data/yabause/profile.log"
#define DIRECT_MAX_RECTS 1024U
#define DIRECT_MAX_PLANNED_INPUTS 128U
#define DIRECT_MAX_UPLOADS 12U
#define DIRECT_SOFT_NUMERATOR 3ULL
#define DIRECT_SOFT_DENOMINATOR 2ULL
#define DIRECT_HARD_NUMERATOR 2ULL
#define DIRECT_HARD_DENOMINATOR 1ULL
#define DIRECT_MIN_SAVINGS_NUMERATOR 9ULL
#define DIRECT_MIN_SAVINGS_DENOMINATOR 10ULL
#define DIRECT_REPORT_ATTEMPTS 300ULL

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
} DirectCounters;

static DirectJournal direct_journal[2];
static DirectRect direct_work[DIRECT_MAX_RECTS];
static DirectCounters direct_counters;

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
      /* Do not let a failed/mismatched partial allocation suppress an
       * unrelated later allocation. */
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

static void DirectReportIfReady(void)
{
   FILE *file;
   if (direct_counters.attempts < DIRECT_REPORT_ATTEMPTS)
      return;
   file = fopen(DIRECT_PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "atlas_direct attempts=%llu optimized=%llu passthrough=%llu "
              "fragmented_fallbacks=%llu empty_fallbacks=%llu "
              "exact_allocations=%llu persistent_refreshes=%llu "
              "partial_suppressed=%llu original_avg_bytes=%llu "
              "submitted_avg_bytes=%llu submitted_calls_avg_x100=%llu "
              "input_rects_avg_x100=%llu output_rects_avg_x100=%llu "
              "soft_merges=%llu hard_merges=%llu forced_merges=%llu "
              "planner_avg_us=%llu\n",
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
              direct_counters.planner_us / direct_counters.attempts);
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

/* The profiling build already owns the public YGL wrappers. CMake renames
 * those three wrapper functions and this layer calls them, preserving the
 * primitive counters while gaining the exact post-allocation texture pointer.
 */
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
   unsigned int output_count;
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

   /* A new backing store or a fragmented/incomplete journal always falls
    * back to YGL's original full-width upload. Correctness wins over savings. */
   if (!journal->initialized || journal->overflow || !journal->count ||
       journal->count > DIRECT_MAX_PLANNED_INPUTS) {
      if (!journal->count)
         direct_counters.empty_fallbacks++;
      if (journal->count > DIRECT_MAX_PLANNED_INPUTS)
         direct_counters.fragmented_fallbacks++;
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
      unsigned int bottom = rect_bottom < call_bottom ?
                            rect_bottom : call_bottom;
      if (top >= bottom)
         continue;
      rect.y = top;
      rect.h = bottom - top;
      direct_work[count++] = rect;
   }

   direct_counters.input_rects += count;
   output_count = DirectPlanRects(count, &soft_merges,
                                  &hard_merges, &forced_merges);
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
   else {
      /* Pinned VitaGL honors GL_UNPACK_ROW_LENGTH and copies synchronously
       * with TEXTURES_SPEEDHACK, so every sub-upload points directly at the
       * authoritative YGL atlas backing store. */
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
