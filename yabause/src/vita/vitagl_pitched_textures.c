/* Yabause-owned VitaGL texture translation unit.
 *
 * Keep the upstream vitaGL submodule pinned and unmodified, but compile its
 * textures.c through this wrapper so we can expose one narrow helper for the
 * Vita YGL atlas. The helper below deliberately bypasses _glTexSubImage2D for
 * the optimized bounding upload and copies rows straight into the currently
 * bound linear RGBA8 texture backing store.
 */
#include "../../../third_party/vitaGL/source/textures.c"

int yabause_vglTexSubImage2DPitched(GLenum target, GLint level,
                                    GLint xoffset, GLint yoffset,
                                    GLsizei width, GLsizei height,
                                    GLenum format, GLenum type,
                                    const GLvoid *pixels,
                                    GLsizei src_row_length)
{
   texture_unit *tex_unit;
   texture *tex;
   uint32_t tex_w;
   uint32_t tex_h;
   uint32_t dst_stride;
   uint32_t src_stride;
   uint32_t row_bytes;
   uint8_t *dst;
   const uint8_t *src;
   unsigned int row;
   int texture2d_idx;

   THREAD_SAFE()

   /* This is intentionally a narrow Yabause atlas fast path, not a general
    * replacement for glTexSubImage2D. Fall back at the caller for anything
    * outside the exact format/layout used by the YGL RGBA atlas. */
   if (target != GL_TEXTURE_2D || level != 0 ||
       format != GL_RGBA || type != GL_UNSIGNED_BYTE ||
       !pixels || xoffset < 0 || yoffset < 0 ||
       width <= 0 || height <= 0 || src_row_length < width)
      return 0;

   tex_unit = &texture_units[server_texture_unit];
   resolve_tex_target(target, return 0);
   tex = &texture_slots[texture2d_idx];

   if (tex->status != TEX_VALID ||
       vglGetTexFormat(&tex->gxm_tex) !=
          SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR)
      return 0;

   vglGetTexSizes(&tex->gxm_tex, &tex_w, &tex_h);
   if ((uint32_t)xoffset + (uint32_t)width > tex_w ||
       (uint32_t)yoffset + (uint32_t)height > tex_h)
      return 0;

   dst = (uint8_t *)vglGetTexData(&tex->gxm_tex);
   if (!dst)
      return 0;

   /* VitaGL allocates ordinary linear 32-bit textures at an 8-pixel aligned
    * pitch. This is the same destination-stride calculation used by the
    * pinned _glTexSubImage2D implementation, but without its generic state,
    * format and source-row-length machinery. */
   dst_stride = VGL_ALIGN(tex_w, 8) * sizeof(uint32_t);
   src_stride = (uint32_t)src_row_length * sizeof(uint32_t);
   row_bytes = (uint32_t)width * sizeof(uint32_t);

   dst += (uint32_t)yoffset * dst_stride +
          (uint32_t)xoffset * sizeof(uint32_t);
   src = (const uint8_t *)pixels;

   for (row = 0; row < (unsigned int)height; row++) {
      vgl_fast_memcpy(dst, src, row_bytes);
      dst += dst_stride;
      src += src_stride;
   }

   return 1;
}
