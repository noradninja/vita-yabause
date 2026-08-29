#include "vitagl_present.h"

#include <vitaGL.h>
#include <stdint.h>
#include <stdlib.h>

#define VITA_WIDTH 960
#define VITA_HEIGHT 544
#define VITAGL_LEGACY_POOL_SIZE (4 * 1024 * 1024)

static GLuint frame_texture;
static uint8_t *upload_pixels;
static size_t upload_capacity;
static int texture_width;
static int texture_height;
static int initialized;

static int ensure_upload_buffer(int width, int height)
{
   size_t required = (size_t)width * (size_t)height * 4;
   uint8_t *replacement;

   if (required <= upload_capacity)
      return 0;
   replacement = (uint8_t *)realloc(upload_pixels, required);
   if (!replacement)
      return -1;
   upload_pixels = replacement;
   upload_capacity = required;
   return 0;
}

int VitaGLPresenterInit(void)
{
   if (!vglInit(VITAGL_LEGACY_POOL_SIZE))
      return -1;

   vglWaitVblankStart(GL_TRUE);
   glDisable(GL_BLEND);
   glDisable(GL_DEPTH_TEST);
   glDisable(GL_CULL_FACE);
   glDisable(GL_SCISSOR_TEST);
   glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glOrtho(0.0, VITA_WIDTH, VITA_HEIGHT, 0.0, -1.0, 1.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();

   glGenTextures(1, &frame_texture);
   glBindTexture(GL_TEXTURE_2D, frame_texture);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
   glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
   glEnable(GL_TEXTURE_2D);

   initialized = 1;
   return 0;
}

void VitaGLPresenterShutdown(void)
{
   if (frame_texture)
      glDeleteTextures(1, &frame_texture);
   frame_texture = 0;
   free(upload_pixels);
   upload_pixels = NULL;
   upload_capacity = 0;
   texture_width = 0;
   texture_height = 0;
   initialized = 0;
}

int VitaGLPresenterPresent(const u32 *pixels, int width, int height)
{
   int scale_x;
   int scale_y;
   int scale;
   int output_width;
   int output_height;
   int origin_x;
   int origin_y;
   size_t pixel_count;
   size_t i;

   if (!initialized || !pixels || width <= 0 || height <= 0 ||
       width > VITA_WIDTH || height > VITA_HEIGHT)
      return -1;
   if (ensure_upload_buffer(width, height) < 0)
      return -1;

   pixel_count = (size_t)width * (size_t)height;
   for (i = 0; i < pixel_count; ++i) {
      u32 pixel = pixels[i];
      /* VIDSoft uses 0xAABBGGRR, which is RGBA byte order on little endian. */
      upload_pixels[i * 4 + 0] = (uint8_t)pixel;
      upload_pixels[i * 4 + 1] = (uint8_t)(pixel >> 8);
      upload_pixels[i * 4 + 2] = (uint8_t)(pixel >> 16);
      upload_pixels[i * 4 + 3] = 0xFF;
   }

   glBindTexture(GL_TEXTURE_2D, frame_texture);
   if (texture_width != width || texture_height != height) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                   GL_RGBA, GL_UNSIGNED_BYTE, upload_pixels);
      texture_width = width;
      texture_height = height;
   } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                      GL_RGBA, GL_UNSIGNED_BYTE, upload_pixels);
   }

   scale_x = VITA_WIDTH / width;
   scale_y = VITA_HEIGHT / height;
   scale = scale_x < scale_y ? scale_x : scale_y;
   if (scale < 1)
      scale = 1;
   output_width = width * scale;
   output_height = height * scale;
   origin_x = (VITA_WIDTH - output_width) / 2;
   origin_y = (VITA_HEIGHT - output_height) / 2;

   glClear(GL_COLOR_BUFFER_BIT);
   glColor4ub(255, 255, 255, 255);
   glBegin(GL_QUADS);
   glTexCoord2f(0.0f, 0.0f);
   glVertex3f((GLfloat)origin_x, (GLfloat)origin_y, 0.0f);
   glTexCoord2f(1.0f, 0.0f);
   glVertex3f((GLfloat)(origin_x + output_width), (GLfloat)origin_y, 0.0f);
   glTexCoord2f(1.0f, 1.0f);
   glVertex3f((GLfloat)(origin_x + output_width),
              (GLfloat)(origin_y + output_height), 0.0f);
   glTexCoord2f(0.0f, 1.0f);
   glVertex3f((GLfloat)origin_x, (GLfloat)(origin_y + output_height), 0.0f);
   glEnd();
   vglSwapBuffers(GL_FALSE);
   return 0;
}
