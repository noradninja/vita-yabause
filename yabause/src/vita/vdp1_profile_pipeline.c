#include <stdio.h>
#include <string.h>

#include <psp2/kernel/processmgr.h>
#include <vitaGL.h>

#include "../vdp1.h"
#include "../ygl.h"
#include "vitaprofile.h"

#define PROFILE_PATH "ux0:data/yabause/profile.log"
#define PIPELINE_INTERVAL 300

typedef struct {
   unsigned long long commands;
   unsigned long long draw_commands;
   unsigned long long state_commands;
   unsigned long long normal;
   unsigned long long scaled;
   unsigned long long distorted;
   unsigned long long polygon;
   unsigned long long polyline;
   unsigned long long line;
   unsigned long long primitives;
   unsigned long long batches;
   unsigned long long draw_calls;
   unsigned long long program_switches;
   unsigned long long stencil_draws;
   unsigned long long half_trans_passes;
   unsigned long long fbo_switches;
   unsigned long long feedback_blits;

   unsigned long long atlas_dirty_bytes[VITA_PROFILE_ATLAS_COUNT];
   unsigned long long atlas_dirty_regions[VITA_PROFILE_ATLAS_COUNT];
   unsigned long long atlas_upload_bytes;
   unsigned long long atlas_upload_regions;
   unsigned long long atlas_coalesce_dirty_bytes;
   unsigned long long atlas_coalesce_upload_bytes;
   unsigned long long atlas_coalesce_collateral_bytes;

   unsigned long long gpu_wait_calls;
   unsigned long long gpu_wait_us;
   unsigned long long gpu_wait_max_us;
   unsigned long long prewait_draws;
   unsigned long long prewait_vdp1_draws;
   unsigned long long prewait_draws_max;
   unsigned long long prewait_vdp1_draws_max;
} Vdp1PipelineCounter;

static Vdp1PipelineCounter pipeline;
static unsigned int pipeline_frames;
static unsigned int vdp1_submit_depth;
static int stencil_enabled;
static int blend_enabled;
static GLenum stencil_depth_pass = GL_KEEP;
static GLuint framebuffer_binding = ~0U;
static GLuint read_framebuffer_binding = ~0U;
static GLuint draw_framebuffer_binding = ~0U;
static unsigned long long draws_since_finish;
static unsigned long long vdp1_draws_since_finish;

static int vdp1_submit_active(void)
{
   return vdp1_submit_depth != 0;
}

static unsigned long long average_x100(unsigned long long value)
{
   return pipeline_frames ? value * 100ULL / pipeline_frames : 0;
}

static unsigned long long average_per_wait_x100(unsigned long long value)
{
   return pipeline.gpu_wait_calls ? value * 100ULL / pipeline.gpu_wait_calls : 0;
}

static void reset_submit_state(void)
{
   stencil_enabled = 0;
   blend_enabled = 0;
   stencil_depth_pass = GL_KEEP;
   framebuffer_binding = ~0U;
   read_framebuffer_binding = ~0U;
   draw_framebuffer_binding = ~0U;
}

static void flush_pipeline(void)
{
   FILE *file;

   if (!pipeline_frames)
      return;

   file = fopen(PROFILE_PATH, "a");
   if (file) {
      fprintf(file,
              "vdp1_pipeline frames=%u "
              "commands=%llu commands_avg_x100=%llu "
              "draw_commands=%llu state_commands=%llu "
              "normal=%llu scaled=%llu distorted=%llu polygon=%llu polyline=%llu line=%llu "
              "primitives=%llu primitives_avg_x100=%llu "
              "batches=%llu batches_avg_x100=%llu "
              "draw_calls=%llu draw_calls_avg_x100=%llu "
              "program_switches=%llu program_switches_avg_x100=%llu "
              "stencil_draws=%llu stencil_draws_avg_x100=%llu "
              "half_trans_passes=%llu half_trans_passes_avg_x100=%llu "
              "fbo_switches=%llu fbo_switches_avg_x100=%llu "
              "feedback_blits=%llu feedback_blits_avg_x100=%llu\n",
              pipeline_frames,
              pipeline.commands, average_x100(pipeline.commands),
              pipeline.draw_commands, pipeline.state_commands,
              pipeline.normal, pipeline.scaled, pipeline.distorted,
              pipeline.polygon, pipeline.polyline, pipeline.line,
              pipeline.primitives, average_x100(pipeline.primitives),
              pipeline.batches, average_x100(pipeline.batches),
              pipeline.draw_calls, average_x100(pipeline.draw_calls),
              pipeline.program_switches, average_x100(pipeline.program_switches),
              pipeline.stencil_draws, average_x100(pipeline.stencil_draws),
              pipeline.half_trans_passes, average_x100(pipeline.half_trans_passes),
              pipeline.fbo_switches, average_x100(pipeline.fbo_switches),
              pipeline.feedback_blits, average_x100(pipeline.feedback_blits));

      fprintf(file,
              "atlas_producers frames=%u "
              "none_dirty_regions=%llu none_dirty_avg_bytes=%llu "
              "vdp1_dirty_regions=%llu vdp1_dirty_avg_bytes=%llu "
              "vdp2_dirty_regions=%llu vdp2_dirty_avg_bytes=%llu "
              "upload_regions=%llu upload_avg_bytes=%llu "
              "coalesce_dirty_avg_bytes=%llu coalesce_upload_avg_bytes=%llu "
              "coalesce_collateral_avg_bytes=%llu\n",
              pipeline_frames,
              pipeline.atlas_dirty_regions[VITA_PROFILE_ATLAS_NONE],
              pipeline.atlas_dirty_bytes[VITA_PROFILE_ATLAS_NONE] / pipeline_frames,
              pipeline.atlas_dirty_regions[VITA_PROFILE_ATLAS_VDP1],
              pipeline.atlas_dirty_bytes[VITA_PROFILE_ATLAS_VDP1] / pipeline_frames,
              pipeline.atlas_dirty_regions[VITA_PROFILE_ATLAS_VDP2],
              pipeline.atlas_dirty_bytes[VITA_PROFILE_ATLAS_VDP2] / pipeline_frames,
              pipeline.atlas_upload_regions,
              pipeline.atlas_upload_bytes / pipeline_frames,
              pipeline.atlas_coalesce_dirty_bytes / pipeline_frames,
              pipeline.atlas_coalesce_upload_bytes / pipeline_frames,
              pipeline.atlas_coalesce_collateral_bytes / pipeline_frames);

      fprintf(file,
              "gpu_wait frames=%u calls=%llu calls_avg_x100=%llu "
              "wait_avg_us=%llu wait_frame_avg_us=%llu wait_max_us=%llu "
              "prewait_draws_avg_x100=%llu prewait_vdp1_draws_avg_x100=%llu "
              "prewait_draws_max=%llu prewait_vdp1_draws_max=%llu\n",
              pipeline_frames,
              pipeline.gpu_wait_calls,
              average_x100(pipeline.gpu_wait_calls),
              pipeline.gpu_wait_calls ? pipeline.gpu_wait_us / pipeline.gpu_wait_calls : 0,
              pipeline.gpu_wait_us / pipeline_frames,
              pipeline.gpu_wait_max_us,
              average_per_wait_x100(pipeline.prewait_draws),
              average_per_wait_x100(pipeline.prewait_vdp1_draws),
              pipeline.prewait_draws_max,
              pipeline.prewait_vdp1_draws_max);
      fclose(file);
   }

   memset(&pipeline, 0, sizeof(pipeline));
   pipeline_frames = 0;
}

/* ------------------------------------------------------------------------- */
/* Existing Vita profiler entry points.  Linker --wrap keeps the renderer    */
/* source untouched while allowing the pipeline counters to follow the same   */
/* 300-frame windows as vitaprofile.c.                                        */
/* ------------------------------------------------------------------------- */

extern void __real_VitaProfileBegin(VitaProfileSection section);
extern void __real_VitaProfileEnd(VitaProfileSection section);
extern void __real_VitaProfileFrameComplete(void);
extern void __real_VitaProfileShutdown(void);

void __wrap_VitaProfileBegin(VitaProfileSection section)
{
   __real_VitaProfileBegin(section);
   if (section == VITA_PROFILE_VDP1_SUBMIT) {
      if (vdp1_submit_depth++ == 0)
         reset_submit_state();
   }
}

void __wrap_VitaProfileEnd(VitaProfileSection section)
{
   __real_VitaProfileEnd(section);
   if (section == VITA_PROFILE_VDP1_SUBMIT && vdp1_submit_depth)
      --vdp1_submit_depth;
}

void __wrap_VitaProfileFrameComplete(void)
{
   __real_VitaProfileFrameComplete();
   if (++pipeline_frames >= PIPELINE_INTERVAL)
      flush_pipeline();
}

void __wrap_VitaProfileShutdown(void)
{
   __real_VitaProfileShutdown();
   flush_pipeline();
}

/* ------------------------------------------------------------------------- */
/* Atlas producer attribution.  The old upload-phase counter answers where    */
/* an upload happened.  These counters preserve which renderer actually       */
/* generated each dirty region before it is coalesced/uploaded later.          */
/* ------------------------------------------------------------------------- */

extern void __real_VitaProfileRecordAtlasDirtyGenerated(unsigned int width,
                                                         unsigned int height,
                                                         int persistent);
void __wrap_VitaProfileRecordAtlasDirtyGenerated(unsigned int width,
                                                  unsigned int height,
                                                  int persistent)
{
   VitaProfileAtlasPhase producer = VitaProfileCurrentAtlasPhase();
   unsigned long long bytes = (unsigned long long)width * height * 4ULL;

   if ((unsigned int)producer >= VITA_PROFILE_ATLAS_COUNT)
      producer = VITA_PROFILE_ATLAS_NONE;
   ++pipeline.atlas_dirty_regions[producer];
   pipeline.atlas_dirty_bytes[producer] += bytes;

   __real_VitaProfileRecordAtlasDirtyGenerated(width, height, persistent);
}

extern void __real_VitaProfileRecordAtlasUploadRegion(
   VitaProfileAtlasPhase producer, unsigned int width, unsigned int height);
void __wrap_VitaProfileRecordAtlasUploadRegion(VitaProfileAtlasPhase producer,
                                                unsigned int width,
                                                unsigned int height)
{
   ++pipeline.atlas_upload_regions;
   pipeline.atlas_upload_bytes +=
      (unsigned long long)width * height * 4ULL;
   __real_VitaProfileRecordAtlasUploadRegion(producer, width, height);
}

extern void __real_VitaProfileRecordAtlasCoalescing(
   unsigned int input_regions, unsigned int output_regions,
   unsigned long long dirty_bytes, unsigned long long upload_bytes);
void __wrap_VitaProfileRecordAtlasCoalescing(
   unsigned int input_regions, unsigned int output_regions,
   unsigned long long dirty_bytes, unsigned long long upload_bytes)
{
   pipeline.atlas_coalesce_dirty_bytes += dirty_bytes;
   pipeline.atlas_coalesce_upload_bytes += upload_bytes;
   if (upload_bytes > dirty_bytes)
      pipeline.atlas_coalesce_collateral_bytes += upload_bytes - dirty_bytes;
   __real_VitaProfileRecordAtlasCoalescing(
      input_regions, output_regions, dirty_bytes, upload_bytes);
}

/* ------------------------------------------------------------------------- */
/* VDP1 command callbacks.  These count actual non-skipped renderer callbacks */
/* emitted by Vdp1DrawCommands(), including clip/local-coordinate state.      */
/* ------------------------------------------------------------------------- */

#define DEFINE_DRAW_COMMAND_WRAPPER(name, member)                              \
   extern void __real_##name(u8 *ram, Vdp1 *regs, u8 *back_framebuffer);      \
   void __wrap_##name(u8 *ram, Vdp1 *regs, u8 *back_framebuffer)              \
   {                                                                           \
      ++pipeline.commands;                                                      \
      ++pipeline.draw_commands;                                                 \
      ++pipeline.member;                                                        \
      __real_##name(ram, regs, back_framebuffer);                              \
   }

DEFINE_DRAW_COMMAND_WRAPPER(VIDOGLVdp1NormalSpriteDraw, normal)
DEFINE_DRAW_COMMAND_WRAPPER(VIDOGLVdp1ScaledSpriteDraw, scaled)
DEFINE_DRAW_COMMAND_WRAPPER(VIDOGLVdp1DistortedSpriteDraw, distorted)
DEFINE_DRAW_COMMAND_WRAPPER(VIDOGLVdp1PolygonDraw, polygon)
DEFINE_DRAW_COMMAND_WRAPPER(VIDOGLVdp1PolylineDraw, polyline)
DEFINE_DRAW_COMMAND_WRAPPER(VIDOGLVdp1LineDraw, line)

extern void __real_VIDOGLVdp1UserClipping(u8 *ram, Vdp1 *regs);
extern void __real_VIDOGLVdp1SystemClipping(u8 *ram, Vdp1 *regs);
extern void __real_VIDOGLVdp1LocalCoordinate(u8 *ram, Vdp1 *regs);

void __wrap_VIDOGLVdp1UserClipping(u8 *ram, Vdp1 *regs)
{
   ++pipeline.commands;
   ++pipeline.state_commands;
   __real_VIDOGLVdp1UserClipping(ram, regs);
}

void __wrap_VIDOGLVdp1SystemClipping(u8 *ram, Vdp1 *regs)
{
   ++pipeline.commands;
   ++pipeline.state_commands;
   __real_VIDOGLVdp1SystemClipping(ram, regs);
}

void __wrap_VIDOGLVdp1LocalCoordinate(u8 *ram, Vdp1 *regs)
{
   ++pipeline.commands;
   ++pipeline.state_commands;
   __real_VIDOGLVdp1LocalCoordinate(ram, regs);
}

/* ------------------------------------------------------------------------- */
/* YGL primitive emission.  The atlas phase filters out VDP2 calls sharing   */
/* the same helper functions.                                                 */
/* ------------------------------------------------------------------------- */

static void record_primitive(void)
{
   if (VitaProfileCurrentAtlasPhase() == VITA_PROFILE_ATLAS_VDP1)
      ++pipeline.primitives;
}

extern float *__real_YglQuad(YglSprite *input, YglTexture *output, YglCache *cache);
float *__wrap_YglQuad(YglSprite *input, YglTexture *output, YglCache *cache)
{
   record_primitive();
   return __real_YglQuad(input, output, cache);
}

extern int __real_YglQuadGrowShading(YglSprite *input, YglTexture *output,
                                     float *colors, YglCache *cache);
int __wrap_YglQuadGrowShading(YglSprite *input, YglTexture *output,
                              float *colors, YglCache *cache)
{
   record_primitive();
   return __real_YglQuadGrowShading(input, output, colors, cache);
}

extern void __real_YglCacheQuadGrowShading(YglSprite *input, float *colors,
                                            YglCache *cache);
void __wrap_YglCacheQuadGrowShading(YglSprite *input, float *colors,
                                     YglCache *cache)
{
   record_primitive();
   __real_YglCacheQuadGrowShading(input, colors, cache);
}

extern void __real_YglCachedQuad(YglSprite *input, YglCache *cache);
void __wrap_YglCachedQuad(YglSprite *input, YglCache *cache)
{
   record_primitive();
   __real_YglCachedQuad(input, cache);
}

extern void __real_YglQuadOffset(YglSprite *input, YglTexture *output,
                                 YglCache *cache, int cx, int cy,
                                 float sx, float sy);
void __wrap_YglQuadOffset(YglSprite *input, YglTexture *output,
                          YglCache *cache, int cx, int cy,
                          float sx, float sy)
{
   record_primitive();
   __real_YglQuadOffset(input, output, cache, cx, cy, sx, sy);
}

extern void __real_YglCachedQuadOffset(YglSprite *input, YglCache *cache,
                                       int cx, int cy, float sx, float sy);
void __wrap_YglCachedQuadOffset(YglSprite *input, YglCache *cache,
                                int cx, int cy, float sx, float sy)
{
   record_primitive();
   __real_YglCachedQuadOffset(input, cache, cx, cy, sx, sy);
}

/* ------------------------------------------------------------------------- */
/* GL/VitaGL submission hooks.  These measure the work that reaches vitaGL,  */
/* not the number of Saturn commands that requested it.                       */
/* ------------------------------------------------------------------------- */

extern void __real_glDrawArrays(GLenum mode, GLint first, GLsizei count);
void __wrap_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
   ++draws_since_finish;
   if (vdp1_submit_active()) {
      ++vdp1_draws_since_finish;
      ++pipeline.draw_calls;
      if (stencil_enabled)
         ++pipeline.stencil_draws;
      if (stencil_enabled && (blend_enabled || stencil_depth_pass == GL_INVERT))
         ++pipeline.half_trans_passes;
   }
   __real_glDrawArrays(mode, first, count);
}

extern void __real_glFinish(void);
void __wrap_glFinish(void)
{
   unsigned long long started = sceKernelGetProcessTimeWide();
   unsigned long long elapsed;

   __real_glFinish();
   elapsed = sceKernelGetProcessTimeWide() - started;

   ++pipeline.gpu_wait_calls;
   pipeline.gpu_wait_us += elapsed;
   if (elapsed > pipeline.gpu_wait_max_us)
      pipeline.gpu_wait_max_us = elapsed;

   pipeline.prewait_draws += draws_since_finish;
   pipeline.prewait_vdp1_draws += vdp1_draws_since_finish;
   if (draws_since_finish > pipeline.prewait_draws_max)
      pipeline.prewait_draws_max = draws_since_finish;
   if (vdp1_draws_since_finish > pipeline.prewait_vdp1_draws_max)
      pipeline.prewait_vdp1_draws_max = vdp1_draws_since_finish;

   draws_since_finish = 0;
   vdp1_draws_since_finish = 0;
}

extern void __real_glUseProgram(GLuint program);
void __wrap_glUseProgram(GLuint program)
{
   if (vdp1_submit_active())
      ++pipeline.program_switches;
   __real_glUseProgram(program);
}

extern void __real_glUniformMatrix4fv(GLint location, GLsizei count,
                                      GLboolean transpose,
                                      const GLfloat *value);
void __wrap_glUniformMatrix4fv(GLint location, GLsizei count,
                               GLboolean transpose, const GLfloat *value)
{
   if (vdp1_submit_active())
      ++pipeline.batches;
   __real_glUniformMatrix4fv(location, count, transpose, value);
}

extern void __real_glEnable(GLenum capability);
void __wrap_glEnable(GLenum capability)
{
   if (vdp1_submit_active()) {
      if (capability == GL_STENCIL_TEST)
         stencil_enabled = 1;
      else if (capability == GL_BLEND)
         blend_enabled = 1;
   }
   __real_glEnable(capability);
}

extern void __real_glDisable(GLenum capability);
void __wrap_glDisable(GLenum capability)
{
   if (vdp1_submit_active()) {
      if (capability == GL_STENCIL_TEST)
         stencil_enabled = 0;
      else if (capability == GL_BLEND)
         blend_enabled = 0;
   }
   __real_glDisable(capability);
}

extern void __real_glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass);
void __wrap_glStencilOp(GLenum sfail, GLenum dpfail, GLenum dppass)
{
   if (vdp1_submit_active())
      stencil_depth_pass = dppass;
   __real_glStencilOp(sfail, dpfail, dppass);
}

static void record_framebuffer_binding(GLenum target, GLuint framebuffer)
{
   GLuint *binding;

   if (!vdp1_submit_active())
      return;

   if (target == GL_READ_FRAMEBUFFER)
      binding = &read_framebuffer_binding;
   else if (target == GL_DRAW_FRAMEBUFFER)
      binding = &draw_framebuffer_binding;
   else
      binding = &framebuffer_binding;

   if (*binding != framebuffer) {
      ++pipeline.fbo_switches;
      *binding = framebuffer;
   }

   if (target == GL_FRAMEBUFFER) {
      read_framebuffer_binding = framebuffer;
      draw_framebuffer_binding = framebuffer;
   }
}

extern void __real_glBindFramebuffer(GLenum target, GLuint framebuffer);
void __wrap_glBindFramebuffer(GLenum target, GLuint framebuffer)
{
   record_framebuffer_binding(target, framebuffer);
   __real_glBindFramebuffer(target, framebuffer);
}

extern void __real_glBlitFramebuffer(GLint srcX0, GLint srcY0,
                                     GLint srcX1, GLint srcY1,
                                     GLint dstX0, GLint dstY0,
                                     GLint dstX1, GLint dstY1,
                                     GLbitfield mask, GLenum filter);
void __wrap_glBlitFramebuffer(GLint srcX0, GLint srcY0,
                              GLint srcX1, GLint srcY1,
                              GLint dstX0, GLint dstY0,
                              GLint dstX1, GLint dstY1,
                              GLbitfield mask, GLenum filter)
{
   if (vdp1_submit_active())
      ++pipeline.feedback_blits;
   __real_glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                            dstX0, dstY0, dstX1, dstY1,
                            mask, filter);
}
