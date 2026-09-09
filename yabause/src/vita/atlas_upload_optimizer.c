#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vitaGL.h>
#ifdef VITA_PROFILE
#include <psp2/kernel/processmgr.h>
#endif

#include "../ygl.h"

#define PROFILE_PATH "ux0:data/yabause/profile.log"
#define ATLAS_OPT_TILE_WIDTH 32U
#define ATLAS_OPT_MAX_RECTS 12U
#define ATLAS_OPT_MAX_CANDIDATES 2048U
#define ATLAS_OPT_MERGE_NUMERATOR 7ULL
#define ATLAS_OPT_MERGE_DENOMINATOR 4ULL
#define ATLAS_OPT_REPORT_ATTEMPTS 300U

typedef struct {
   unsigned int x;
   unsigned int y;
   unsigned int w;
   unsigned int h;
} AtlasOptRect;

typedef struct {
   GLuint texture;
   unsigned int width;
   unsigned int height;
   unsigned int tiles_per_row;
   unsigned char *shadow;
   unsigned char *valid;
   size_t shadow_size;
   size_t valid_size;
} AtlasOptState;

typedef struct {
   unsigned long long attempts;
   unsigned long long optimized;
   unsigned long long passthrough;
   unsigned long long original_bytes;
   unsigned long long submitted_bytes;
   unsigned long long submitted_calls;
   unsigned long long candidate_rects;
   unsigned long long forced_merges;
#ifdef VITA_PROFILE
   unsigned long long scan_us;
   unsigned long long pack_us;
#endif
} AtlasOptCounters;

static AtlasOptState atlas_states[2];
static AtlasOptRect atlas_rects[ATLAS_OPT_MAX_CANDIDATES];
static unsigned char *atlas_scratch;
static size_t atlas_scratch_size;
static GLuint atlas_bound_texture;
static AtlasOptCounters atlas_counters;

extern void __real_glBindTexture(GLenum target, GLuint texture);
extern void __real_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                                GLsizei width, GLsizei height, GLint border,
                                GLenum format, GLenum type, const GLvoid *pixels);
extern void __real_glTexSubImage2D(GLenum target, GLint level,
                                   GLint xoffset, GLint yoffset,
                                   GLsizei width, GLsizei height,
                                   GLenum format, GLenum type,
                                   const GLvoid *pixels);

static unsigned long long rect_bytes(const AtlasOptRect *rect)
{
   return (unsigned long long)rect->w * rect->h * 4ULL;
}

static void invalidate_state(AtlasOptState *state)
{
   if (state->valid && state->valid_size)
      memset(state->valid, 0, state->valid_size);
}

static AtlasOptState *find_state(GLuint texture)
{
   unsigned int i;

   for (i = 0; i < 2; i++) {
      if (atlas_states[i].texture == texture && texture != 0)
         return &atlas_states[i];
   }
   return NULL;
}

static AtlasOptState *get_state(GLuint texture, unsigned int width,
                                unsigned int height)
{
   AtlasOptState *state;
   unsigned int slot;
   size_t shadow_size;
   size_t valid_size;
   unsigned int tiles_per_row;
   unsigned char *shadow;
   unsigned char *valid;

   if (!texture || !width || !height)
      return NULL;

   state = find_state(texture);
   if (!state) {
      slot = atlas_states[0].texture == 0 ? 0U :
             (atlas_states[1].texture == 0 ? 1U : 0U);
      state = &atlas_states[slot];
      free(state->shadow);
      free(state->valid);
      memset(state, 0, sizeof(*state));
      state->texture = texture;
   }

   tiles_per_row = (width + ATLAS_OPT_TILE_WIDTH - 1U) / ATLAS_OPT_TILE_WIDTH;
   shadow_size = (size_t)width * height * 4U;
   valid_size = (size_t)tiles_per_row * height;

   if (state->width == width && state->height == height &&
       state->shadow_size == shadow_size && state->valid_size == valid_size &&
       state->shadow && state->valid)
      return state;

   shadow = (unsigned char *)realloc(state->shadow, shadow_size);
   if (!shadow)
      return NULL;
   state->shadow = shadow;

   valid = (unsigned char *)realloc(state->valid, valid_size);
   if (!valid)
      return NULL;
   state->valid = valid;

   state->width = width;
   state->height = height;
   state->tiles_per_row = tiles_per_row;
   state->shadow_size = shadow_size;
   state->valid_size = valid_size;
   memset(state->valid, 0, valid_size);
   return state;
}

static int ensure_scratch(size_t bytes)
{
   unsigned char *scratch;

   if (bytes <= atlas_scratch_size)
      return 0;
   scratch = (unsigned char *)realloc(atlas_scratch, bytes);
   if (!scratch)
      return -1;
   atlas_scratch = scratch;
   atlas_scratch_size = bytes;
   return 0;
}

static int is_atlas_texture(GLuint texture)
{
   unsigned int i;

   if (!_Ygl || !texture)
      return 0;
   for (i = 0; i < _Ygl->atlasTextureCount && i < 2U; i++) {
      if (_Ygl->atlasTextures[i] == texture)
         return 1;
   }
   return 0;
}

static int same_horizontal_span(const AtlasOptRect *a,
                                unsigned int x, unsigned int w,
                                unsigned int y)
{
   return a->x == x && a->w == w && a->y + a->h == y;
}

static unsigned int build_candidate_rects(AtlasOptState *state,
                                          GLint xoffset, GLint yoffset,
                                          GLsizei width, GLsizei height,
                                          const unsigned char *pixels,
                                          int *overflow)
{
   unsigned int row;
   unsigned int rect_count = 0;
   unsigned int first_tile;
   unsigned int last_tile;

   *overflow = 0;
   first_tile = (unsigned int)xoffset / ATLAS_OPT_TILE_WIDTH;
   last_tile = ((unsigned int)xoffset + (unsigned int)width - 1U) /
               ATLAS_OPT_TILE_WIDTH;

   for (row = 0; row < (unsigned int)height; row++) {
      unsigned int tile = first_tile;
      unsigned int dest_y = (unsigned int)yoffset + row;

      while (tile <= last_tile) {
         unsigned int tile_x = tile * ATLAS_OPT_TILE_WIDTH;
         unsigned int begin = tile_x < (unsigned int)xoffset ?
                              (unsigned int)xoffset : tile_x;
         unsigned int end = tile_x + ATLAS_OPT_TILE_WIDTH;
         unsigned int call_end = (unsigned int)xoffset + (unsigned int)width;
         unsigned int span_begin;
         unsigned int span_end;
         unsigned int span_tile_begin;
         unsigned int span_tile_end;
         const unsigned char *src;
         unsigned char *shadow;
         size_t bytes;
         int dirty;

         if (end > call_end)
            end = call_end;
         bytes = (size_t)(end - begin) * 4U;
         src = pixels + ((size_t)row * width +
                         (begin - (unsigned int)xoffset)) * 4U;
         shadow = state->shadow +
                  ((size_t)dest_y * state->width + begin) * 4U;
         dirty = !state->valid[(size_t)dest_y * state->tiles_per_row + tile] ||
                 memcmp(src, shadow, bytes) != 0;
         if (!dirty) {
            tile++;
            continue;
         }

         span_begin = begin;
         span_end = end;
         span_tile_begin = tile;
         span_tile_end = tile;
         tile++;

         while (tile <= last_tile) {
            tile_x = tile * ATLAS_OPT_TILE_WIDTH;
            begin = tile_x < (unsigned int)xoffset ?
                    (unsigned int)xoffset : tile_x;
            end = tile_x + ATLAS_OPT_TILE_WIDTH;
            if (end > call_end)
               end = call_end;
            bytes = (size_t)(end - begin) * 4U;
            src = pixels + ((size_t)row * width +
                            (begin - (unsigned int)xoffset)) * 4U;
            shadow = state->shadow +
                     ((size_t)dest_y * state->width + begin) * 4U;
            dirty = !state->valid[(size_t)dest_y * state->tiles_per_row + tile] ||
                    memcmp(src, shadow, bytes) != 0;
            if (!dirty)
               break;
            span_end = end;
            span_tile_end = tile;
            tile++;
         }

         (void)span_tile_begin;
         (void)span_tile_end;
         if (rect_count &&
             same_horizontal_span(&atlas_rects[rect_count - 1],
                                  span_begin, span_end - span_begin, dest_y)) {
            atlas_rects[rect_count - 1].h++;
         }
         else {
            if (rect_count >= ATLAS_OPT_MAX_CANDIDATES) {
               *overflow = 1;
               return rect_count;
            }
            atlas_rects[rect_count].x = span_begin;
            atlas_rects[rect_count].y = dest_y;
            atlas_rects[rect_count].w = span_end - span_begin;
            atlas_rects[rect_count].h = 1;
            rect_count++;
         }
      }
   }

   return rect_count;
}

static void merge_pair(unsigned int *count, unsigned int a_index,
                       unsigned int b_index)
{
   AtlasOptRect *a = &atlas_rects[a_index];
   AtlasOptRect *b = &atlas_rects[b_index];
   unsigned int left = a->x < b->x ? a->x : b->x;
   unsigned int top = a->y < b->y ? a->y : b->y;
   unsigned int right_a = a->x + a->w;
   unsigned int right_b = b->x + b->w;
   unsigned int bottom_a = a->y + a->h;
   unsigned int bottom_b = b->y + b->h;
   unsigned int right = right_a > right_b ? right_a : right_b;
   unsigned int bottom = bottom_a > bottom_b ? bottom_a : bottom_b;

   a->x = left;
   a->y = top;
   a->w = right - left;
   a->h = bottom - top;
   memmove(b, b + 1, (*count - b_index - 1U) * sizeof(*b));
   (*count)--;
}

static unsigned int reduce_rects(unsigned int count,
                                 unsigned long long *forced_merges)
{
   while (count > ATLAS_OPT_MAX_RECTS) {
      unsigned int i;
      unsigned int j;
      unsigned int best_i = ~0U;
      unsigned int best_j = ~0U;
      unsigned long long best_extra = ~0ULL;
      int budgeted = 0;

      for (i = 0; i < count; i++) {
         for (j = i + 1; j < count; j++) {
            const AtlasOptRect *a = &atlas_rects[i];
            const AtlasOptRect *b = &atlas_rects[j];
            unsigned int left = a->x < b->x ? a->x : b->x;
            unsigned int top = a->y < b->y ? a->y : b->y;
            unsigned int right_a = a->x + a->w;
            unsigned int right_b = b->x + b->w;
            unsigned int bottom_a = a->y + a->h;
            unsigned int bottom_b = b->y + b->h;
            unsigned int right = right_a > right_b ? right_a : right_b;
            unsigned int bottom = bottom_a > bottom_b ? bottom_a : bottom_b;
            unsigned long long pair_bytes = rect_bytes(a) + rect_bytes(b);
            unsigned long long merged_bytes =
               (unsigned long long)(right - left) * (bottom - top) * 4ULL;
            unsigned long long extra = merged_bytes > pair_bytes ?
                                       merged_bytes - pair_bytes : 0;
            int within_budget =
               merged_bytes * ATLAS_OPT_MERGE_DENOMINATOR <=
               pair_bytes * ATLAS_OPT_MERGE_NUMERATOR;

            if (within_budget) {
               if (!budgeted || extra < best_extra) {
                  budgeted = 1;
                  best_extra = extra;
                  best_i = i;
                  best_j = j;
               }
            }
            else if (!budgeted && extra < best_extra) {
               best_extra = extra;
               best_i = i;
               best_j = j;
            }
         }
      }

      if (best_i == ~0U)
         break;
      if (!budgeted)
         (*forced_merges)++;
      merge_pair(&count, best_i, best_j);
   }
   return count;
}

static void update_shadow_rect(AtlasOptState *state,
                               const AtlasOptRect *rect,
                               GLint xoffset, GLint yoffset,
                               GLsizei call_width,
                               const unsigned char *pixels)
{
   unsigned int row;
   unsigned int first_tile = rect->x / ATLAS_OPT_TILE_WIDTH;
   unsigned int last_tile = (rect->x + rect->w - 1U) / ATLAS_OPT_TILE_WIDTH;

   for (row = 0; row < rect->h; row++) {
      unsigned int dest_y = rect->y + row;
      unsigned int src_y = dest_y - (unsigned int)yoffset;
      const unsigned char *src = pixels +
         ((size_t)src_y * call_width +
          (rect->x - (unsigned int)xoffset)) * 4U;
      unsigned char *dst = state->shadow +
         ((size_t)dest_y * state->width + rect->x) * 4U;
      unsigned int tile;

      memcpy(dst, src, (size_t)rect->w * 4U);
      for (tile = first_tile; tile <= last_tile; tile++)
         state->valid[(size_t)dest_y * state->tiles_per_row + tile] = 1;
   }
}

static void update_shadow_full_call(AtlasOptState *state,
                                    GLint xoffset, GLint yoffset,
                                    GLsizei width, GLsizei height,
                                    const unsigned char *pixels)
{
   AtlasOptRect rect;
   rect.x = (unsigned int)xoffset;
   rect.y = (unsigned int)yoffset;
   rect.w = (unsigned int)width;
   rect.h = (unsigned int)height;
   update_shadow_rect(state, &rect, xoffset, yoffset, width, pixels);
}

#ifdef VITA_PROFILE
static void report_optimizer_if_ready(void)
{
   FILE *file;
   unsigned long long attempts;
   unsigned long long saved;
   unsigned long long savings_x100;

   if (atlas_counters.attempts < ATLAS_OPT_REPORT_ATTEMPTS)
      return;
   attempts = atlas_counters.attempts;
   saved = atlas_counters.original_bytes > atlas_counters.submitted_bytes ?
           atlas_counters.original_bytes - atlas_counters.submitted_bytes : 0;
   savings_x100 = atlas_counters.original_bytes ?
      saved * 10000ULL / atlas_counters.original_bytes : 0;

   file = fopen(PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "atlas_optimizer attempts=%llu optimized=%llu passthrough=%llu "
              "original_avg_bytes=%llu submitted_avg_bytes=%llu "
              "savings_pct_x100=%llu submitted_calls_avg_x100=%llu "
              "candidate_rects_avg_x100=%llu forced_merges=%llu "
              "scan_avg_us=%llu pack_avg_us=%llu\n",
              attempts,
              atlas_counters.optimized,
              atlas_counters.passthrough,
              atlas_counters.original_bytes / attempts,
              atlas_counters.submitted_bytes / attempts,
              savings_x100,
              atlas_counters.submitted_calls * 100ULL / attempts,
              atlas_counters.candidate_rects * 100ULL / attempts,
              atlas_counters.forced_merges,
              atlas_counters.scan_us / attempts,
              atlas_counters.pack_us / attempts);
      fclose(file);
   }
   memset(&atlas_counters, 0, sizeof(atlas_counters));
}
#endif

void __wrap_glBindTexture(GLenum target, GLuint texture)
{
   if (target == GL_TEXTURE_2D)
      atlas_bound_texture = texture;
   __real_glBindTexture(target, texture);
}

void __wrap_glTexImage2D(GLenum target, GLint level, GLint internalformat,
                         GLsizei width, GLsizei height, GLint border,
                         GLenum format, GLenum type, const GLvoid *pixels)
{
   AtlasOptState *state;

   __real_glTexImage2D(target, level, internalformat, width, height, border,
                       format, type, pixels);
   if (target == GL_TEXTURE_2D) {
      state = find_state(atlas_bound_texture);
      if (state)
         invalidate_state(state);
   }
}

void __wrap_glTexSubImage2D(GLenum target, GLint level,
                            GLint xoffset, GLint yoffset,
                            GLsizei width, GLsizei height,
                            GLenum format, GLenum type,
                            const GLvoid *pixels)
{
   AtlasOptState *state;
   const unsigned char *source = (const unsigned char *)pixels;
   unsigned int rect_count;
   unsigned int original_rect_count;
   unsigned int i;
   unsigned long long original_bytes;
   unsigned long long submitted_bytes = 0;
   unsigned long long forced_merges = 0;
   int overflow;
#ifdef VITA_PROFILE
   unsigned long long scan_started;
   unsigned long long pack_started;
#endif

   if (target != GL_TEXTURE_2D || level != 0 || !pixels ||
       format != GL_RGBA || type != GL_UNSIGNED_BYTE ||
       xoffset < 0 || yoffset < 0 || width <= 0 || height <= 0 ||
       !_Ygl || !YglTM || !is_atlas_texture(atlas_bound_texture) ||
       (unsigned int)xoffset + (unsigned int)width > YglTM->width ||
       (unsigned int)yoffset + (unsigned int)height > YglTM->height) {
      __real_glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                             format, type, pixels);
      return;
   }

   state = get_state(atlas_bound_texture, YglTM->width, YglTM->height);
   if (!state) {
      __real_glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                             format, type, pixels);
      return;
   }

   original_bytes = (unsigned long long)width * height * 4ULL;
   atlas_counters.attempts++;
   atlas_counters.original_bytes += original_bytes;

#ifdef VITA_PROFILE
   scan_started = sceKernelGetProcessTimeWide();
#endif
   rect_count = build_candidate_rects(state, xoffset, yoffset,
                                      width, height, source, &overflow);
#ifdef VITA_PROFILE
   atlas_counters.scan_us += sceKernelGetProcessTimeWide() - scan_started;
#endif
   original_rect_count = rect_count;
   atlas_counters.candidate_rects += original_rect_count;

   if (!overflow && rect_count == 0) {
      atlas_counters.optimized++;
#ifdef VITA_PROFILE
      report_optimizer_if_ready();
#endif
      return;
   }

   if (overflow) {
      atlas_counters.passthrough++;
      atlas_counters.submitted_bytes += original_bytes;
      atlas_counters.submitted_calls++;
      __real_glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                             format, type, pixels);
      update_shadow_full_call(state, xoffset, yoffset, width, height, source);
#ifdef VITA_PROFILE
      report_optimizer_if_ready();
#endif
      return;
   }

   rect_count = reduce_rects(rect_count, &forced_merges);
   atlas_counters.forced_merges += forced_merges;
   for (i = 0; i < rect_count; i++)
      submitted_bytes += rect_bytes(&atlas_rects[i]);

   if (rect_count > ATLAS_OPT_MAX_RECTS || submitted_bytes >= original_bytes) {
      atlas_counters.passthrough++;
      atlas_counters.submitted_bytes += original_bytes;
      atlas_counters.submitted_calls++;
      __real_glTexSubImage2D(target, level, xoffset, yoffset, width, height,
                             format, type, pixels);
      update_shadow_full_call(state, xoffset, yoffset, width, height, source);
#ifdef VITA_PROFILE
      report_optimizer_if_ready();
#endif
      return;
   }

   atlas_counters.optimized++;
   atlas_counters.submitted_bytes += submitted_bytes;
   atlas_counters.submitted_calls += rect_count;

   for (i = 0; i < rect_count; i++) {
      AtlasOptRect *rect = &atlas_rects[i];
      unsigned int row;
      size_t bytes = (size_t)rect->w * rect->h * 4U;

      if (ensure_scratch(bytes) != 0) {
         atlas_counters.passthrough++;
         __real_glTexSubImage2D(target, level, xoffset, yoffset,
                                width, height, format, type, pixels);
         update_shadow_full_call(state, xoffset, yoffset,
                                 width, height, source);
#ifdef VITA_PROFILE
         report_optimizer_if_ready();
#endif
         return;
      }
#ifdef VITA_PROFILE
      pack_started = sceKernelGetProcessTimeWide();
#endif
      for (row = 0; row < rect->h; row++) {
         unsigned int src_y = rect->y + row - (unsigned int)yoffset;
         const unsigned char *src = source +
            ((size_t)src_y * width +
             (rect->x - (unsigned int)xoffset)) * 4U;
         memcpy(atlas_scratch + (size_t)row * rect->w * 4U,
                src, (size_t)rect->w * 4U);
      }
#ifdef VITA_PROFILE
      atlas_counters.pack_us += sceKernelGetProcessTimeWide() - pack_started;
#endif
      __real_glTexSubImage2D(target, level,
                             (GLint)rect->x, (GLint)rect->y,
                             (GLsizei)rect->w, (GLsizei)rect->h,
                             format, type, atlas_scratch);
      update_shadow_rect(state, rect, xoffset, yoffset, width, source);
   }

#ifdef VITA_PROFILE
   report_optimizer_if_ready();
#endif
}
