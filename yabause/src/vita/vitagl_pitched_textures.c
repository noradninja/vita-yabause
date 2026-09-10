/* Yabause-owned VitaGL texture translation unit.
 *
 * Keep the upstream vitaGL submodule pinned and unmodified, but compile its
 * textures.c through this wrapper so we can expose one narrow helper for
 * explicit-pitch atlas uploads.
 */
#include "../../../third_party/vitaGL/source/textures.c"

int yabause_vglTexSubImage2DPitched(GLenum target, GLint level,
                                    GLint xoffset, GLint yoffset,
                                    GLsizei width, GLsizei height,
                                    GLenum format, GLenum type,
                                    const GLvoid *pixels,
                                    GLsizei src_row_length)
{
   THREAD_SAFE()

   texture_unit *tex_unit = &texture_units[server_texture_unit];
   int texture2d_idx;
   int previous_unpack_row_len;

   if (!pixels || width <= 0 || height <= 0 || src_row_length < width)
      return 0;

   resolve_tex_target(target, return 0);
   texture *tex = &texture_slots[texture2d_idx];

   previous_unpack_row_len = unpack_row_len;
   unpack_row_len = src_row_length;
   _glTexSubImage2D(tex, target, level, xoffset, yoffset,
                    width, height, format, type, pixels);
   unpack_row_len = previous_unpack_row_len;
   return 1;
}
