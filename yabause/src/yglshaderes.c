/*  Copyright 2005-2006 Guillaume Duhamel
    Copyright 2005-2006 Theo Berkau
    Copyright 2011-2015 Shinya Miyamoto(devmiyax)

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifdef HAVE_LIBGL

//#ifdef __ANDROID__
#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "ygl.h"
#include "yui.h"
#include "vidshared.h"
#ifdef VITA
#include "vita/vitagl_present.h"
#endif

extern float vdp1wratio;
extern float vdp1hratio;
extern int GlHeight;
extern int GlWidth;

static void Ygl_printShaderError( GLuint shader )
{
  GLsizei bufSize;

  glGetShaderiv(shader, GL_INFO_LOG_LENGTH , &bufSize);

  if (bufSize > 1) {
    GLchar *infoLog;

    infoLog = (GLchar *)malloc(bufSize);
    if (infoLog != NULL) {
      GLsizei length;
      glGetShaderInfoLog(shader, bufSize, &length, infoLog);
      YGLLOG("Shaderlog:\n%s\n", infoLog);
      free(infoLog);
    }
  }
}

#ifdef VITA
static void YglVitaLogProgramText(int id, const char *text)
{
  const char *cursor = text;

  while (cursor != NULL && *cursor != '\0') {
    char message[224];
    size_t length = 0;

    while (*cursor == '\r' || *cursor == '\n')
      ++cursor;
    if (*cursor == '\0')
      break;

    while (cursor[length] != '\0' &&
           cursor[length] != '\r' &&
           cursor[length] != '\n' &&
           length < 160)
      ++length;

    snprintf(message, sizeof(message),
             "renderer: shader %d link log: %.*s",
             id, (int)length, cursor);
    VitaGLPresenterLog(message);
    cursor += length;
  }
}
#endif

static void Ygl_printProgramError(GLuint program, int id)
{
  GLsizei bufSize;

  glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufSize);

  if (bufSize > 1) {
    GLchar *infoLog = (GLchar *)malloc(bufSize);
    if (infoLog != NULL) {
      GLsizei length;
      glGetProgramInfoLog(program, bufSize, &length, infoLog);
      YGLLOG("Program log:\n%s\n", infoLog);
#ifdef VITA
      YglVitaLogProgramText(id, infoLog);
#else
      (void)id;
#endif
      free(infoLog);
    }
  }
}

static GLuint _prgid[PG_MAX] ={0};

/*------------------------------------------------------------------------------------
 *  Normal Draw
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_normal_v[] =
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "uniform mat4 u_mvpMatrix;    \n"
      "uniform mat4 u_texMatrix;    \n"
      "layout (location = 0) in vec4 a_position;   \n"
      "layout (location = 1) in vec4 a_texcoord;   \n"
      "out  highp vec4 v_texcoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "   v_texcoord  = a_texcoord/*u_texMatrix*/; \n"
      "   v_texcoord.s  = v_texcoord.s; ///2048.0; \n"
      "   v_texcoord.t  = v_texcoord.t; ///1024.0; \n"
      "} ";
const GLchar * pYglprg_normal_v[] = {Yglprg_normal_v, NULL};

const GLchar Yglprg_normal_f[] =
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "precision highp float;                            \n"
      "in highp vec4 v_texcoord;                            \n"
      "uniform vec4 u_color_offset;    \n"
      "uniform sampler2D s_texture;                        \n"
      "out vec4 fragColor;            \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  ivec2 addr; \n"
      "  addr.x = int(v_texcoord.x);                        \n"
      "  addr.y = int(v_texcoord.y);                        \n"
      "  vec4 txcol = texelFetch( s_texture, addr,0 );         \n"
      "  if(txcol.a > 0.0)\n                                 "
      "     fragColor = clamp(txcol+u_color_offset,vec4(0.0),vec4(1.0));\n                         "
      "  else \n                                            "
      "     discard;\n                                      "
      "}                                                   \n";
const GLchar * pYglprg_normal_f[] = {Yglprg_normal_f, NULL};
static int id_normal_s_texture = -1;

int Ygl_uniformNormal(void * p )
{

   YglProgram * prg;
   prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   glUniform1i(id_normal_s_texture, 0);
   glUniform4fv(prg->color_offset,1,prg->color_offset_val);
   return 0;
}

int Ygl_cleanupNormal(void * p )
{
   YglProgram * prg;
   prg = p;
   return 0;
}

int ShaderDrawTest()
{

   GLuint vertexp         = glGetAttribLocation(_prgid[PG_NORMAL],(const GLchar *)"a_position");
   GLuint texcoordp       = glGetAttribLocation(_prgid[PG_NORMAL],(const GLchar *)"a_texcoord");
   GLuint mtxModelView    = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_mvpMatrix");
   GLuint mtxTexture      = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_texMatrix");

   GLfloat vec[]={ 0.0f,0.0f,-0.5f, 100.0f,  0.0f,-0.5f,100.0f,100.0f,-0.5f,
                   0.0f,0.0f,-0.5f, 100.0f,100.0f,-0.5f,  0.0f,100.0f,-0.5f };

/*
   GLfloat vec[]={ 0.0f,0.0f,-0.5f,
                   -1.0f,1.0f,-0.5f,
                   1.0f,1.0f,-0.5f,
                   0.0f,0.0f,-0.5f,
                   -1.0f,-1.0f,-0.5f,
                   1.0f,-1.0f,-0.5f,
   };
*/
   GLfloat tex[]={ 0.0f,0.0f, 2048.0f,0.0f,2048.0f,1024.0f,0.0f,0.0f,2048.0f,1024.0f,0.0f,1024.0f };

//   GLfloat tex[]={ 0.0f,0.0f,1.0f,0.0f,1.0f,1.0f,
//                   0.0f,0.0f,1.0f,1.0f,0.0f,1.0f };

   YglMatrix mtx;
   YglMatrix mtxt;
   GLuint id = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"s_texture");

   YglLoadIdentity(&mtx);
   YglLoadIdentity(&mtxt);

   YglOrtho(&mtx,0.0f,100.0f,100.0f,0.0f,1.0f,0.0f);

   glUseProgram(_prgid[PG_NORMAL]);
   glUniform1i(id, 0);

   glEnableVertexAttribArray(vertexp);
   glEnableVertexAttribArray(texcoordp);

   glUniformMatrix4fv( mtxModelView, 1, GL_FALSE, (GLfloat*) &_Ygl->mtxModelView/*mtx*/.m[0][0] );

   glVertexAttribPointer(vertexp,3, GL_FLOAT,GL_FALSE, 0, (const GLvoid*) vec );
   glVertexAttribPointer(texcoordp,2, GL_FLOAT,GL_FALSE, 0, (const GLvoid*)tex );

   glDrawArrays(GL_TRIANGLES, 0, 6);

   return 0;

}

/*------------------------------------------------------------------------------------
 *  Window Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_window_v[] =
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "uniform mat4 u_mvpMatrix;    \n"
      "layout (location = 0) in vec4 a_position;               \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "} ";
const GLchar * pYglprg_window_v[] = {Yglprg_window_v, NULL};

const GLchar Yglprg_window_f[] =
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "precision highp float;                            \n"
      "out vec4 fragColor;            \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  fragColor = vec4( 1.0,1.0,1.0,1.0 );\n"
      "}                                                   \n";
const GLchar * pYglprg_window_f[] = {Yglprg_window_f, NULL};

int Ygl_uniformWindow(void * p )
{
   YglProgram * prg;
   prg = p;
   glUseProgram(prg->prgid );
   glUniform1i(prg->tex0, 0);
   glEnableVertexAttribArray(0);
   glDisableVertexAttribArray(1);
   glDisableVertexAttribArray(2);
   return 0;
}

int Ygl_cleanupWindow(void * p )
{
   YglProgram * prg;
   prg = p;
   return 0;
}

/*------------------------------------------------------------------------------------
 *  VDP1 Normal Draw
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_normal_v[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform mat4 u_mvpMatrix;\n"
      "attribute vec4 a_position;\n"
      "attribute vec4 a_texcoord;\n"
      "varying vec4 v_texcoord;\n"
      "void main() {\n"
      "   vec4 position = a_position * u_mvpMatrix;\n"
      "   gl_Position = position * a_texcoord.w;\n"
      "   v_texcoord = a_texcoord;\n"
      "   v_texcoord.s = v_texcoord.s / 2048.0;\n"
      "   v_texcoord.t = v_texcoord.t / 1024.0;\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "uniform mat4 u_mvpMatrix;    \n"
      "layout (location = 0) in vec4 a_position;   \n"
      "layout (location = 1) in vec4 a_texcoord;   \n"
      "out   vec4 v_texcoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "   v_texcoord  = a_texcoord; \n"
      "   v_texcoord.s  = v_texcoord.s/2048.0; \n"
      "   v_texcoord.t  = v_texcoord.t/1024.0; \n"
      "} ";
#endif
const GLchar * pYglprg_vdp1_normal_v[] = {Yglprg_vdp1_normal_v, NULL};

const GLchar Yglprg_vpd1_normal_f[] =
#ifdef VITA
      "precision highp float;\n"
      "varying vec4 v_texcoord;\n"
      "uniform sampler2D s_texture;\n"
      "void main() {\n"
      "   vec4 spriteColor = texture2D(s_texture, (floor(v_texcoord.st * vec2(2048.0, 1024.0)) + vec2(0.5)) / vec2(2048.0, 1024.0));\n"
      "   if (spriteColor.a == 0.0) discard;\n"
      "   gl_FragColor = spriteColor;\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "precision highp float;                            \n"
      "in vec4 v_texcoord;                            \n"
      "uniform sampler2D s_texture;                        \n"
      "out vec4 fragColor;            \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  vec2 addr = v_texcoord.st;                        \n"
      "  addr.s = addr.s / (v_texcoord.q);                 \n"
      "  addr.t = addr.t / (v_texcoord.q);                 \n"
      "  vec4 FragColor = texture( s_texture, addr );      \n"
      "  /*if( FragColor.a == 0.0 ) discard;*/                \n"
      "  fragColor = FragColor;\n "
      "}                                                   \n";
#endif
const GLchar * pYglprg_vdp1_normal_f[] = {Yglprg_vpd1_normal_f, NULL};
static int id_vdp1_normal_s_texture = -1;

int Ygl_uniformVdp1Normal(void * p )
{
   YglProgram * prg;
   prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   glUniform1i(id_vdp1_normal_s_texture, 0);
   return 0;
}

int Ygl_cleanupVdp1Normal(void * p )
{
   YglProgram * prg;
   prg = p;
   return 0;
}



/*------------------------------------------------------------------------------------
 *  VDP1 GlowShading Operation
 * ----------------------------------------------------------------------------------*/
const GLchar Yglprg_vdp1_gouraudshading_v[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform mat4 u_mvpMatrix;\n"
      "attribute vec4 a_position;\n"
      "attribute vec4 a_texcoord;\n"
      "attribute vec4 a_grcolor;\n"
      "varying vec4 v_texcoord;\n"
      "varying vec4 v_vtxcolor;\n"
      "void main() {\n"
      "   vec4 position = a_position * u_mvpMatrix;\n"
      "   gl_Position = position * a_texcoord.w;\n"
      "   v_texcoord = a_texcoord;\n"
      "   v_texcoord.s = v_texcoord.s / 2048.0;\n"
      "   v_texcoord.t = v_texcoord.t / 1024.0;\n"
      "   v_vtxcolor = a_grcolor * a_texcoord.w;\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "uniform mat4 u_mvpMatrix;                \n"
      "uniform mat4 u_texMatrix;                \n"
      "layout (location = 0) in vec4 a_position;               \n"
      "layout (location = 1) in vec4 a_texcoord;               \n"
      "layout (location = 2) in vec4 a_grcolor;                \n"
      "out  vec4 v_texcoord;               \n"
      "out  vec4 v_vtxcolor;               \n"
      "void main() {                            \n"
      "   v_vtxcolor  = a_grcolor;              \n"
      "   v_texcoord  = a_texcoord/*u_texMatrix*/; \n"
      "   v_texcoord.s  = v_texcoord.s/2048.0; \n"
      "   v_texcoord.t  = v_texcoord.t/1024.0; \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "}\n";
#endif
const GLchar * pYglprg_vdp1_gouraudshading_v[] = {Yglprg_vdp1_gouraudshading_v, NULL};

const GLchar Yglprg_vdp1_gouraudshading_f[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform sampler2D u_sprite;\n"
      "varying vec4 v_texcoord;\n"
      "varying vec4 v_vtxcolor;\n"
      "void main() {\n"
      "   vec4 spriteColor = texture2D(u_sprite, (floor(v_texcoord.st * vec2(2048.0, 1024.0)) + vec2(0.5)) / vec2(2048.0, 1024.0));\n"
      "   if (spriteColor.a == 0.0) discard;\n"
      "   vec3 gouraudTable = (v_vtxcolor.rgb / max(v_texcoord.w, 0.000001)) * 31.0;\n"
"   vec3 source5 = min(floor(spriteColor.rgb * (255.0 / 8.0) + vec3(0.0001)), vec3(31.0));\n"
"   vec3 table5 = floor(gouraudTable + vec3(0.0001));\n"
"   spriteColor.rgb = clamp(source5 + table5 - vec3(16.0), vec3(0.0), vec3(31.0)) / 31.0;\n"
"   gl_FragColor = spriteColor;\n"
      "   gl_FragColor.a = spriteColor.a;\n"
      "}\n";
#else
#if defined(_OGLES3_)
"#version 300 es \n"
#else
"#version 330 \n"
#endif
"precision highp float;                                                 \n"
"uniform sampler2D u_sprite;                                              \n"
"in vec4 v_texcoord;                                                 \n"
"in vec4 v_vtxcolor;                                                 \n"
"out vec4 fragColor;            \n"
"void main() {                                                            \n"
"  vec2 addr = v_texcoord.st;                                             \n"
"  addr.s = addr.s / (v_texcoord.q);                                      \n"
"  addr.t = addr.t / (v_texcoord.q);                                      \n"
"  vec4 spriteColor = texture(u_sprite,addr);                           \n"
"  if( spriteColor.a == 0.0 ) discard;                                      \n"
"  fragColor   = spriteColor;                                          \n"
"  fragColor  = clamp(spriteColor+v_vtxcolor,vec4(0.0),vec4(1.0));     \n"
      "  fragColor.a = spriteColor.a;                                        \n"
      "}\n";
#endif
const GLchar * pYglprg_vdp1_gouraudshading_f[] = {Yglprg_vdp1_gouraudshading_f, NULL};
static int id_vdp1_normal_s_sprite = -1;

int Ygl_uniformGlowShading(void * p )
{
   YglProgram * prg;
   prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   glUniform1i(id_vdp1_normal_s_sprite, 0);
   if( prg->vertexAttribute != NULL )
   {
      glEnableVertexAttribArray(prg->vaid);
   }
   return 0;
}

int Ygl_cleanupGlowShading(void * p )
{
   YglProgram * prg;
   prg = p;
   glDisableVertexAttribArray(prg->vaid);
   return 0;
}

/*------------------------------------------------------------------------------------
 *  VDP1 GlowShading and Half Trans Operation
 * ----------------------------------------------------------------------------------*/
static int id_sprite;
static int id_fbo;
static int id_fbowidth;
static int id_fboheight;
static int id_gouraud_half_mode;

const GLchar Yglprg_vdp1_gouraudshading_hf_v[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform mat4 u_mvpMatrix;\n"
      "attribute vec4 a_position;\n"
      "attribute vec4 a_texcoord;\n"
      "attribute vec4 a_grcolor;\n"
      "varying vec4 v_texcoord;\n"
      "varying vec4 v_vtxcolor;\n"
      "void main() {\n"
      "   vec4 position = a_position * u_mvpMatrix;\n"
      "   gl_Position = position * a_texcoord.w;\n"
      "   v_texcoord = a_texcoord;\n"
      "   v_texcoord.s = v_texcoord.s / 2048.0;\n"
      "   v_texcoord.t = v_texcoord.t / 1024.0;\n"
      "   v_vtxcolor = a_grcolor * a_texcoord.w;\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "uniform mat4 u_mvpMatrix;                \n"
      "uniform mat4 u_texMatrix;                \n"
      "layout (location = 0) in vec4 a_position;               \n"
      "layout (location = 1) in vec4 a_texcoord;               \n"
      "layout (location = 2) in vec4 a_grcolor;                \n"
      "out  vec4 v_texcoord;               \n"
      "out  vec4 v_vtxcolor;               \n"
      "void main() {                            \n"
      "   v_vtxcolor  = a_grcolor;              \n"
      "   v_texcoord  = a_texcoord/*u_texMatrix*/; \n"
      "   v_texcoord.s  = v_texcoord.s/2048.0; \n"
      "   v_texcoord.t  = v_texcoord.t/1024.0; \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "}\n";
#endif
const GLchar * pYglprg_vdp1_gouraudshading_hf_v[] = {Yglprg_vdp1_gouraudshading_hf_v, NULL};

const GLchar Yglprg_vdp1_gouraudshading_hf_f[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform sampler2D u_sprite;\n"
      "uniform sampler2D u_fbo;\n"
      "uniform float u_fbowidth;\n"
      "uniform float u_fbohegiht;\n"
      "uniform float u_half_mode;\n"
      "varying vec4 v_texcoord;\n"
      "varying vec4 v_vtxcolor;\n"
      "void main() {\n"
      "   vec4 spriteColor = texture2D(u_sprite, (floor(v_texcoord.st * vec2(2048.0, 1024.0)) + vec2(0.5)) / vec2(2048.0, 1024.0));\n"
      "   vec2 faddr = vec2(gl_FragCoord.x / u_fbowidth, gl_FragCoord.y / u_fbohegiht);\n"
      "   if (spriteColor.a == 0.0) discard;\n"
      "   vec3 gouraudTable = (v_vtxcolor.rgb / max(v_texcoord.w, 0.000001)) * 31.0;\n"
"   vec3 source5 = min(floor(spriteColor.rgb * (255.0 / 8.0) + vec3(0.0001)), vec3(31.0));\n"
"   vec3 table5 = floor(gouraudTable + vec3(0.0001));\n"
"   spriteColor.rgb = clamp(source5 + table5 - vec3(16.0), vec3(0.0), vec3(31.0)) / 31.0;\n"
      "   if (u_half_mode < 0.5) {\n"
      "      vec4 fboColor = texture2D(u_fbo, faddr);\n"
      "      if (fboColor.a > 0.0)\n"
      "         gl_FragColor = vec4((spriteColor.rgb + fboColor.rgb) * 0.5, fboColor.a);\n"
      "      else\n"
      "         gl_FragColor = spriteColor;\n"
      "   } else {\n"
      "      gl_FragColor = spriteColor;\n"
      "      if (u_half_mode < 1.5) gl_FragColor.a = 0.5;\n"
      "   }\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "precision highp float;                                                                     \n"
      "uniform sampler2D u_sprite;                                                                  \n"
      "uniform sampler2D u_fbo;                                                                     \n"
      "uniform int u_fbowidth;                                                                      \n"
      "uniform int u_fbohegiht;                                                                     \n"
      "in vec4 v_texcoord;                                                                     \n"
      "in vec4 v_vtxcolor;                                                                     \n"
      "out vec4 fragColor; \n "
      "void main() {                                                                                \n"
      "  vec2 addr = v_texcoord.st;                                                                 \n"
      "  vec2 faddr = vec2( gl_FragCoord.x/float(u_fbowidth), gl_FragCoord.y/float(u_fbohegiht));   \n"
      "  addr.s = addr.s / (v_texcoord.q);                                                          \n"
      "  addr.t = addr.t / (v_texcoord.q);                                                          \n"
      "  vec4 spriteColor = texture(u_sprite,addr);                                               \n"
      "  if( spriteColor.a == 0.0 ) discard;                                                          \n"
      "  vec4 fboColor    = texture(u_fbo,faddr);                                                 \n"
      "  spriteColor += vec4(v_vtxcolor.r,v_vtxcolor.g,v_vtxcolor.b,0.0);                           \n"
      "  if( fboColor.a > 0.0 && spriteColor.a > 0.0 )                                              \n"
      "  {                                                                                          \n"
      "    fragColor = spriteColor*0.5 + fboColor*0.5;                                           \n"
      "    fragColor.a = fboColor.a;                                                             \n"
      "  }else{                                                                                     \n"
      "    fragColor = spriteColor;                                                              \n"
      "  }                                                                                          \n"
      "}\n";
#endif
const GLchar * pYglprg_vdp1_gouraudshading_hf_f[] = {Yglprg_vdp1_gouraudshading_hf_f, NULL};

int Ygl_uniformGlowShadingHalfTrans(void * p )
{
   YglProgram * prg;
   prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   if( prg->vertexAttribute != NULL )
   {
      glEnableVertexAttribArray(prg->vaid);
   }

   glUniform1i(id_sprite, 0);
#ifdef VITA
   glUniform1f(prg->halftrans_mode, 0.0f);
#endif
   glUniform1i(id_fbo, 1);
   glActiveTexture(GL_TEXTURE1);
#ifdef VITA
   glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe]);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
   glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe]);
#endif
#ifdef VITA
   glUniform1f(id_fbowidth, (GLfloat)GlWidth);
   glUniform1f(id_fboheight, (GLfloat)GlHeight);
#else
   glUniform1i(id_fbowidth, GlWidth);
   glUniform1i(id_fboheight, GlHeight);
#endif
   glActiveTexture(GL_TEXTURE0);
#if !defined(_OGLES3_)
   glTextureBarrierNV();
#endif
   return 0;
}

int Ygl_cleanupGlowShadingHalfTrans(void * p )
{
   YglProgram * prg;
   prg = p;
   glDisableVertexAttribArray(prg->vaid);
   return 0;
}


/*------------------------------------------------------------------------------------
 *  VDP1 Half Trans Operation
 * ----------------------------------------------------------------------------------*/
static int id_hf_sprite;
static int id_hf_fbo;
static int id_hf_fbowidth;
static int id_hf_fboheight;
static int id_half_mode;

const GLchar Yglprg_vdp1_halftrans_v[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform mat4 u_mvpMatrix;\n"
      "attribute vec4 a_position;\n"
      "attribute vec4 a_texcoord;\n"
      "varying vec4 v_texcoord;\n"
      "void main() {\n"
      "   vec4 position = a_position * u_mvpMatrix;\n"
      "   gl_Position = position * a_texcoord.w;\n"
      "   v_texcoord = a_texcoord;\n"
      "   v_texcoord.s = v_texcoord.s / 2048.0;\n"
      "   v_texcoord.t = v_texcoord.t / 1024.0;\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
        "uniform mat4 u_mvpMatrix;                \n"
        "layout (location = 0) in vec4 a_position;               \n"
        "layout (location = 1) in vec4 a_texcoord;               \n"
        "layout (location = 2) in vec4 a_grcolor;                \n"
        "out  vec4 v_texcoord;               \n"
        "out  vec4 v_vtxcolor;               \n"
        "void main() {                            \n"
        "   v_vtxcolor  = a_grcolor;              \n"
        "   v_texcoord  = a_texcoord/*u_texMatrix*/; \n"
        "   v_texcoord.s  = v_texcoord.s/2048.0; \n"
        "   v_texcoord.t  = v_texcoord.t/1024.0; \n"
        "   gl_Position = a_position*u_mvpMatrix; \n"
        "}\n";

#endif
const GLchar * pYglprg_vdp1_halftrans_v[] = {Yglprg_vdp1_halftrans_v, NULL};

const GLchar Yglprg_vdp1_halftrans_f[] =
#ifdef VITA
      "precision highp float;\n"
      "uniform sampler2D u_sprite;\n"
      "uniform sampler2D u_fbo;\n"
      "uniform float u_fbowidth;\n"
      "uniform float u_fbohegiht;\n"
      "uniform float u_half_mode;\n"
      "varying vec4 v_texcoord;\n"
      "void main() {\n"
      "   vec4 spriteColor = texture2D(u_sprite, (floor(v_texcoord.st * vec2(2048.0, 1024.0)) + vec2(0.5)) / vec2(2048.0, 1024.0));\n"
      "   vec2 faddr = vec2(gl_FragCoord.x / u_fbowidth, gl_FragCoord.y / u_fbohegiht);\n"
      "   if (spriteColor.a == 0.0) discard;\n"
      "   if (u_half_mode < 0.5) {\n"
      "      vec4 fboColor = texture2D(u_fbo, faddr);\n"
      "      if (fboColor.a > 0.0)\n"
      "         gl_FragColor = vec4((spriteColor.rgb + fboColor.rgb) * 0.5, fboColor.a);\n"
      "      else\n"
      "         gl_FragColor = spriteColor;\n"
      "   } else {\n"
      "      gl_FragColor = spriteColor;\n"
      "      if (u_half_mode < 1.5) gl_FragColor.a = 0.5;\n"
      "   }\n"
      "}\n";
#else
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "precision highp float;                                                                     \n"
      "uniform sampler2D u_sprite;                                                                  \n"
      "uniform sampler2D u_fbo;                                                                     \n"
      "uniform int u_fbowidth;                                                                      \n"
      "uniform int u_fbohegiht;                                                                     \n"
      "in vec4 v_texcoord;                                                                     \n"
      "out vec4 fragColor; \n "
      "void main() {                                                                                \n"
      "  vec2 addr = v_texcoord.st;                                                                 \n"
      "  vec2 faddr = vec2( gl_FragCoord.x/float(u_fbowidth), gl_FragCoord.y/float(u_fbohegiht));   \n"
      "  addr.s = addr.s / (v_texcoord.q);                                                          \n"
      "  addr.t = addr.t / (v_texcoord.q);                                                          \n"
      "  vec4 spriteColor = texture(u_sprite,addr);                                               \n"
      "  if( spriteColor.a == 0.0 ) discard;                                                          \n"
      "  vec4 fboColor    = texture(u_fbo,faddr);                                                 \n"
      "  if( fboColor.a > 0.0 && spriteColor.a > 0.0 )                                              \n"
      "  {                                                                                          \n"
      "    fragColor = spriteColor*0.5 + fboColor*0.5;                                           \n"
      "    fragColor.a = fboColor.a;                                                             \n"
      "  }else{                                                                                     \n"
      "    fragColor = spriteColor;                                                              \n"
      "  }                                                                                          \n"
      "}\n";
#endif
const GLchar * pYglprg_vdp1_halftrans_f[] = {Yglprg_vdp1_halftrans_f, NULL};

int Ygl_uniformHalfTrans(void * p )
{
   YglProgram * prg;
   prg = p;

   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);

   glUniform1i(id_hf_sprite, 0);
#ifdef VITA
   glUniform1f(prg->halftrans_mode, 0.0f);
#endif
   glUniform1i(id_hf_fbo, 1);
   glActiveTexture(GL_TEXTURE1);
#ifdef VITA
   glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe]);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
   glBindTexture(GL_TEXTURE_2D, _Ygl->vdp1FrameBuff[_Ygl->drawframe]);
#endif
#ifdef VITA
   glUniform1f(id_hf_fbowidth, (GLfloat)GlWidth);
   glUniform1f(id_hf_fboheight, (GLfloat)GlHeight);
#else
   glUniform1i(id_hf_fbowidth, GlWidth);
   glUniform1i(id_hf_fboheight, GlHeight);
#endif
   glActiveTexture(GL_TEXTURE0);
#if !defined(_OGLES3_)
   glTextureBarrierNV();
#endif
   return 0;
}

int Ygl_cleanupHalfTrans(void * p )
{
   YglProgram * prg;
   prg = p;
   return 0;
}

/*------------------------------------------------------------------------------------
 *  VDP1 UserClip Operation
 * ----------------------------------------------------------------------------------*/
int Ygl_uniformStartUserClip(void * p )
{
   YglProgram * prg;
   prg = p;

   glEnableVertexAttribArray(0);
   glDisableVertexAttribArray(1);

   if( prg->ux1 != -1 )
   {

      GLint vertices[12];
      glColorMask( GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE );
#ifdef VITA
      /* Clear only low clip bits; bit 0x80 tracks VDP1 occupancy. */
      glStencilMask(YGL_VDP1_STENCIL_WINDOW_MASK);
#else
      glStencilMask(0xffffffff);
#endif
      glClearStencil(0);
      glClear(GL_STENCIL_BUFFER_BIT);
      glEnable(GL_STENCIL_TEST);
      glStencilFunc(GL_ALWAYS,0x1,0x01);
      glStencilOp(GL_REPLACE,GL_REPLACE,GL_REPLACE);
      //glDisable(GL_TEXTURE_2D);

      // render
      vertices[0] = (int)((float)prg->ux1 * vdp1wratio);
      vertices[1] = (int)((float)prg->uy1 * vdp1hratio);
      vertices[2] = (int)((float)(prg->ux2+1) * vdp1wratio);
      vertices[3] = (int)((float)prg->uy1 * vdp1hratio);
      vertices[4] = (int)((float)(prg->ux2+1) * vdp1wratio);
      vertices[5] = (int)((float)(prg->uy2+1) * vdp1hratio);

      vertices[6] = (int)((float)prg->ux1 * vdp1wratio);
      vertices[7] = (int)((float)prg->uy1 * vdp1hratio);
      vertices[8] = (int)((float)(prg->ux2+1) * vdp1wratio);
      vertices[9] = (int)((float)(prg->uy2+1) * vdp1hratio);
      vertices[10] = (int)((float)prg->ux1 * vdp1wratio);
      vertices[11] = (int)((float)(prg->uy2+1) * vdp1hratio);

      glUniformMatrix4fv( prg->mtxModelView, 1, GL_FALSE, (GLfloat*) &_Ygl->mtxModelView.m[0][0]  );
      glVertexAttribPointer(prg->vertexp,2, GL_INT,GL_FALSE, 0, (GLvoid*)vertices );

      glDrawArrays(GL_TRIANGLES, 0, 6);

      glColorMask( GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE );
      glStencilFunc(GL_ALWAYS,0,0x0);
      glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
      glDisable(GL_STENCIL_TEST);
   }

   glEnable(GL_STENCIL_TEST);
   glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);
   if( prg->uClipMode == 0x02 )
   {
      glStencilFunc(GL_EQUAL,0x1,0xFF);
   }else if( prg->uClipMode == 0x03 )
   {
      glStencilFunc(GL_EQUAL,0x0,0xFF);
   }else{
      glStencilFunc(GL_ALWAYS,0,0xFF);
   }

   glEnableVertexAttribArray(0);
   glEnableVertexAttribArray(1);

   //glDisable(GL_STENCIL_TEST);

   return 0;
}

int Ygl_cleanupStartUserClip(void * p ){return 0;}

int Ygl_uniformEndUserClip(void * p )
{

   YglProgram * prg;
   prg = p;
   glDisable(GL_STENCIL_TEST);
   glStencilFunc(GL_ALWAYS,0,0xFF);

   return 0;
}

int Ygl_cleanupEndUserClip(void * p ){return 0;}


int Ygl_uniformStartVDP2Window(void * p )
{
   YglProgram * prg;
   prg = p;

   glEnable(GL_STENCIL_TEST);
   glStencilOp(GL_KEEP,GL_KEEP,GL_KEEP);

   //glEnableVertexAttribArray(0);
   //glDisableVertexAttribArray(1);


   if( prg->bwin0 && !prg->bwin1 )
   {
      if( prg->logwin0 )
      {
         glStencilFunc(GL_EQUAL,0x01,0x01);
      }else{
         glStencilFunc(GL_NOTEQUAL,0x01,0x01);
      }
   }else if( !prg->bwin0 && prg->bwin1 ) {

      if( prg->logwin1 )
      {
         glStencilFunc(GL_EQUAL,0x02,0x02);
      }else{
         glStencilFunc(GL_NOTEQUAL,0x02,0x02);
      }
   }else if( prg->bwin0 && prg->bwin1 ) {
       // and
      if( prg->winmode == 0x0 )
      {
		  if (prg->logwin0 == 1 && prg->logwin0 == 1){
			  glStencilFunc(GL_EQUAL, 0x03, 0x03);
		  }
		  else if (prg->logwin0 == 0 && prg->logwin0 == 0){ 
			  glStencilFunc(GL_NOTEQUAL, 0x03, 0x03);
		  }else{
			  glStencilFunc(GL_ALWAYS, 0, 0xFF);
		  }
      // OR
      }else if( prg->winmode == 0x01 )
      {
		  if (prg->logwin0 == 1 && prg->logwin0 == 1){
			  glStencilFunc(GL_LEQUAL, 0x01, 0x03);
		  }
		  else if (prg->logwin0 == 0 && prg->logwin0 == 0){
			  glStencilFunc(GL_GREATER, 0x01, 0x03);
		  }
		  else{
			  glStencilFunc(GL_ALWAYS, 0, 0xFF);
		  }
      }
   }

   return 0;
}

int Ygl_cleanupStartVDP2Window(void * p ){return 0;}

int Ygl_uniformEndVDP2Window(void * p )
{

   YglProgram * prg;
   prg = p;
   glDisable(GL_STENCIL_TEST);
   glStencilFunc(GL_ALWAYS,0,0xFF);

   return 0;
}

int Ygl_cleanupEndVDP2Window(void * p ){return 0;}


/*------------------------------------------------------------------------------------
 *  VDP2 Draw Frame buffer Operation
 * ----------------------------------------------------------------------------------*/
static int idvdp1FrameBuffer;
static int idfrom;
static int idto;
static int idcoloroffset;

const GLchar Yglprg_vdp1_drawfb_v[] =
#if defined(_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
      "uniform mat4 u_mvpMatrix;                \n"
      "layout (location = 0) in vec4 a_position;               \n"
      "layout (location = 1) in vec2 a_texcoord;               \n"
      "out vec2 v_texcoord;                 \n"
      "void main() {                            \n"
      "   v_texcoord  = a_texcoord;             \n"
      "   gl_Position = a_position*u_mvpMatrix; \n"
      "}\n";
const GLchar * pYglprg_vdp2_drawfb_v[] = {Yglprg_vdp1_drawfb_v, NULL};

const GLchar Yglprg_vdp2_drawfb_f[] =
#if defined(_OGLES3_)
"#version 300 es \n"
#else
"#version 330 \n"
#endif
"precision highp float;\n"
"in vec2 v_texcoord;\n"
"uniform sampler2D s_vdp1FrameBuffer;\n"
"uniform float u_from;\n"
"uniform float u_to;\n"
"uniform vec4 u_coloroffset;\n"
"out vec4 fragColor;\n"
"void main()\n"
"{\n"
"  vec2 addr = v_texcoord;\n"
"  highp vec4 fbColor = texture(s_vdp1FrameBuffer,addr);\n"
"  int additional = int(fbColor.a * 255.0);\n"
"  highp float alpha = float((additional/8)*8)/255.0;\n"
"  highp float depth = (float(additional&0x07)/10.0) + 0.05;\n"
"  if( depth < u_from || depth > u_to ){ discard;return;}\n"
"  if( alpha > 0.0){\n"
"     fragColor = fbColor;\n"
"     fragColor += u_coloroffset;  \n"
"     fragColor.a = alpha + 7.0/255.0;\n"
"     gl_FragDepth =  (depth+1.0)/2.0;\n"
"  } else { \n"
"     discard;\n"
"  }\n"
"}\n";

const GLchar * pYglprg_vdp2_drawfb_f[] = {Yglprg_vdp2_drawfb_f, NULL};

void Ygl_uniformVDP2DrawFramebuffer( void * p, float from, float to , float * offsetcol )
{
   YglProgram * prg;
   prg = p;

   glUseProgram(_prgid[PG_VDP2_DRAWFRAMEBUFF]);
   glUniform1i(idvdp1FrameBuffer, 0);
   glActiveTexture(GL_TEXTURE0);
   glUniform1f(idfrom,from);
   glUniform1f(idto,to);
   glUniform4fv(idcoloroffset,1,offsetcol);
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   _Ygl->renderfb.mtxModelView = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF], (const GLchar *)"u_mvpMatrix");
}

/*------------------------------------------------------------------------------------
*  VDP2 Draw Frame buffer Operation( with Line color insert )
* ----------------------------------------------------------------------------------*/
static int idvdp1FrameBuffer_linecolor;
static int idfrom_linecolor;
static int idto_linecolor;
static int idcoloroffset_linecolor;
static int id_fblinecol_s_line;
static int id_fblinecol_emu_height;
static int id_fblinecol_vheight;

const GLchar * pYglprg_vdp2_drawfb_linecolor_v[] = { Yglprg_vdp1_drawfb_v, NULL };

const GLchar Yglprg_vdp2_drawfb_linecolor_f[] =
#if defined(_OGLES3_)
"#version 300 es \n"
#else
"#version 330 \n"
#endif
"precision highp float;                             \n"
"in vec2 v_texcoord;                             \n"
"uniform sampler2D s_vdp1FrameBuffer;                 \n"
"uniform float u_from;                                  \n"
"uniform float u_to;                                    \n"
"uniform vec4 u_coloroffset;                            \n"
"uniform float u_emu_height;    \n"
"uniform sampler2D s_line;                        \n"
"uniform float u_vheight; \n"
"out vec4 fragColor;            \n"
"void main()                                          \n"
"{                                                    \n"
"  vec2 addr = v_texcoord;                         \n"
"  highp vec4 fbColor = texture(s_vdp1FrameBuffer,addr);  \n"
"  int additional = int(fbColor.a * 255.0);           \n"
"  highp float alpha = float((additional/8)*8)/255.0;  \n"
"  highp float depth = (float(additional&0x07)/10.0) + 0.05; \n"
"  if( depth < u_from || depth > u_to ){ discard;return;} \n"
"  ivec2 linepos; \n "
"  linepos.y = 0; \n "
"  linepos.x = int((u_vheight - gl_FragCoord.y) * u_emu_height);\n"
"  vec4 lncol = texelFetch( s_line, linepos,0 );      \n"
"  if( alpha > 0.0){ \n"
"     fragColor = fbColor;                            \n"
"     fragColor += u_coloroffset;  \n"
"     fragColor += lncol; \n"
"     fragColor.a = 1.0; \n"
"     gl_FragDepth =  (depth+1.0)/2.0;\n"
"  } else { \n"
"     discard;\n"
"  }\n"
"}                                                    \n";

const GLchar * pYglprg_vdp2_drawfb_linecolor_f[] = { Yglprg_vdp2_drawfb_linecolor_f, NULL };

void Ygl_uniformVDP2DrawFramebuffer_linecolor(void * p, float from, float to, float * offsetcol)
{
  YglProgram * prg;
  prg = p;

  glUseProgram(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR]);
  glUniform1i(idvdp1FrameBuffer_linecolor, 0);
  glActiveTexture(GL_TEXTURE0);
  glUniform1f(idfrom_linecolor, from);
  glUniform1f(idto_linecolor, to);
  glUniform4fv(idcoloroffset_linecolor, 1, offsetcol);
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);

  glUniform1i(id_fblinecol_s_line, 1);
  glUniform1f(id_fblinecol_emu_height, (float)_Ygl->rheight/(float)_Ygl->height);
  glUniform1f(id_fblinecol_vheight, (float)_Ygl->height);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _Ygl->lincolor_tex);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_BLEND);
  _Ygl->renderfb.mtxModelView = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"u_mvpMatrix");

}

const GLchar Yglprg_vdp2_drawfb_addcolor_f[] =
#if defined(_OGLES3_)
"#version 300 es \n"
#else
"#version 330 \n"
#endif
"precision highp float;\n"
"in vec2 v_texcoord;\n"
"uniform sampler2D s_vdp1FrameBuffer;\n"
"uniform float u_from;\n"
"uniform float u_to;\n"
"uniform vec4 u_coloroffset;\n"
"out vec4 fragColor;\n"
"void main()\n"
"{\n"
"  vec2 addr = v_texcoord;\n"
"  highp vec4 fbColor = texture(s_vdp1FrameBuffer,addr);\n"
"  int additional = int(fbColor.a * 255.0);\n"
"  highp float alpha = float((additional/8)*8)/255.0;\n"
"  highp float depth = (float(additional&0x07)/10.0) + 0.05;\n"
"  if( depth < u_from || depth > u_to ){ discard;return;}\n"
"  if( alpha <= 0.0){\n"
"     discard;\n"
"  }else if( alpha >= 0.75){\n"
"     fragColor = fbColor;\n"
"     fragColor += u_coloroffset;  \n"
"     fragColor.a = 0.0;\n"
"     gl_FragDepth =  (depth+1.0)/2.0;\n"
"  }else{\n"
"     fragColor = fbColor;\n"
"     fragColor += u_coloroffset;\n"
"     fragColor.a = 1.0;\n"
"     gl_FragDepth =  (depth+1.0)/2.0;\n"
"  }\n " 
"}\n";

const GLchar * pYglprg_vdp2_drawfb_addcolor_f[] = { Yglprg_vdp2_drawfb_addcolor_f, NULL };

/*------------------------------------------------------------------------------------
*  VDP2 Draw Frame buffer Operation( with add color operation )
* ----------------------------------------------------------------------------------*/
static int idvdp1FrameBuffer_addcolor;
static int idfrom_addcolor;
static int idto_addcolor;
static int idcoloroffset_addcolor;

int Ygl_uniformVDP2DrawFramebuffer_addcolor(void * p, float from, float to, float * offsetcol)
{
	YglProgram * prg;
	prg = p;

	glUseProgram(_prgid[PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR]);
	glUniform1i(idvdp1FrameBuffer_addcolor, 0);
	glActiveTexture(GL_TEXTURE0);
	glUniform1f(idfrom_addcolor, from);
	glUniform1f(idto_addcolor, to);
	glUniform4fv(idcoloroffset_addcolor, 1, offsetcol);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	_Ygl->renderfb.mtxModelView = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR], (const GLchar *)"u_mvpMatrix");

	glBlendFunc(GL_ONE, GL_SRC_ALPHA);

   return 0;
}

int Ygl_cleanupVDP2DrawFramebuffer_addcolor(void * p){
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

   return 0;
}

/*------------------------------------------------------------------------------------
 *  VDP2 Add Blend operaiotn
 * ----------------------------------------------------------------------------------*/
int Ygl_uniformAddBlend(void * p )
{
   glBlendFunc(GL_ONE,GL_ONE);
   return 0;
}

int Ygl_cleanupAddBlend(void * p )
{
   glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
   return 0;
}


/*------------------------------------------------------------------------------------
*  11.3 Line Color Insertion
* ----------------------------------------------------------------------------------*/
const GLchar * pYglprg_linecol_v[] = { Yglprg_normal_v, NULL };

const GLchar Yglprg_linecol_f[] =
#if defined(_OGLES3_)
"#version 300 es \n"
#else
"#version 330 \n"
#endif
"precision highp float;                            \n"
"in highp vec4 v_texcoord;                            \n"
"uniform vec4 u_color_offset;    \n"
"uniform float u_emu_height;    \n"
"uniform float u_vheight; \n"
"uniform sampler2D s_texture;                        \n"
"uniform sampler2D s_line;                        \n"
"out vec4 fragColor;            \n"
"void main()                                         \n"
"{                                                   \n"
"  ivec2 addr; \n"
"  addr.x = int(v_texcoord.x);                        \n"
"  addr.y = int(v_texcoord.y);                        \n"
"  ivec2 linepos; \n "
"  linepos.y = 0; \n "
"  linepos.x = int( (u_vheight-gl_FragCoord.y) * u_emu_height);\n"
"  vec4 txcol = texelFetch( s_texture, addr,0 );      \n"
"  vec4 lncol = texelFetch( s_line, linepos,0 );      \n"
"  if(txcol.a > 0.0){\n                                 "
"     fragColor = txcol+u_color_offset+lncol;\n       "
"     fragColor.a = 1.0;\n                             "
"  }else{ \n                                            "
"     discard;\n                                      "
"  }                                                   \n"
"}                                                   \n";
const GLchar * pYglprg_linecol_f[] = { Yglprg_linecol_f, NULL };
static int id_linecol_s_texture = -1;
static int id_linecol_s_line = -1;
static int id_linecol_color_offset = -1;
static int id_linecol_emu_height = -1;
static int id_linecol_vheight = -1;

int Ygl_uniformLinecolorInsert(void * p)
{

  YglProgram * prg;
  prg = p;
  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glUniform1i(id_linecol_s_texture, 0);
  glUniform1i(id_linecol_s_line, 1);
  glUniform4fv(id_linecol_color_offset, 1, prg->color_offset_val);
  glUniform1f(id_linecol_emu_height, (float)_Ygl->rheight / (float)_Ygl->height);
  glUniform1f(id_linecol_vheight, (float)_Ygl->height);

  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, _Ygl->lincolor_tex);

  glActiveTexture(GL_TEXTURE0);

  glDisable(GL_BLEND);

  return 0;
}

int Ygl_cleanupLinecolorInsert(void * p)
{
  YglProgram * prg;
  prg = p;
  
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  glActiveTexture(GL_TEXTURE0);
  glEnable(GL_BLEND);

  return 0;
}


int YglGetProgramId( int prg )
{
   return _prgid[prg];
}

#ifdef VITA
static int YglVitaReplace(char **text, const char *find, const char *replacement)
{
   char *source = *text;
   char *match;
   char *output;
   char *write;
   const char *read;
   size_t find_length = strlen(find);
   size_t replacement_length = strlen(replacement);
   size_t count = 0;
   size_t source_length = strlen(source);

   match = strstr(source, find);
   while (match != NULL) {
      ++count;
      match = strstr(match + find_length, find);
   }
   if (count == 0)
      return 0;

   output = (char *)malloc(source_length + 1 +
      count * (replacement_length > find_length ?
               replacement_length - find_length : 0));
   if (output == NULL)
      return -1;

   read = source;
   write = output;
   while ((match = strstr(read, find)) != NULL) {
      size_t prefix_length = (size_t)(match - read);
      memcpy(write, read, prefix_length);
      write += prefix_length;
      memcpy(write, replacement, replacement_length);
      write += replacement_length;
      read = match + find_length;
   }
   strcpy(write, read);
   free(source);
   *text = output;
   return 0;
}

static char *YglVitaShaderSource(const GLchar *source, int fragment)
{
   char *translated = (char *)malloc(strlen(source) + 1);
   if (translated == NULL)
      return NULL;
   strcpy(translated, source);

   if (YglVitaReplace(&translated, "layout (location = 0) in ", "attribute ") < 0 ||
       YglVitaReplace(&translated, "layout (location = 1) in ", "attribute ") < 0 ||
       YglVitaReplace(&translated, "layout (location = 2) in ", "attribute ") < 0)
      goto failed;

   if (fragment) {
      if (YglVitaReplace(&translated, "out vec4 fragColor;", "") < 0 ||
          YglVitaReplace(&translated, "in highp ", "varying highp ") < 0 ||
          YglVitaReplace(&translated, "in vec", "varying vec") < 0 ||
          YglVitaReplace(&translated, "fragColor", "gl_FragColor") < 0)
         goto failed;
   } else {
      if (YglVitaReplace(&translated, "out  highp ", "varying highp ") < 0 ||
          YglVitaReplace(&translated, "out   ", "varying ") < 0 ||
          YglVitaReplace(&translated, "out  ", "varying ") < 0 ||
          YglVitaReplace(&translated, "out vec", "varying vec") < 0)
         goto failed;
   }

   if (YglVitaReplace(&translated, "texture(", "texture2D(") < 0 ||
       YglVitaReplace(&translated, "addr.s = addr.s / (v_texcoord.q);", "") < 0 ||
       YglVitaReplace(&translated, "addr.t = addr.t / (v_texcoord.q);", "") < 0 ||
       YglVitaReplace(
          &translated,
          "int additional = int(fbColor.a * 255.0);",
          "float additional = floor(fbColor.a * 255.0 + 0.5);") < 0 ||
       YglVitaReplace(
          &translated,
          "float((additional/8)*8)/255.0",
          "floor(additional / 8.0) * 8.0 / 255.0") < 0 ||
       YglVitaReplace(
          &translated,
          "float(additional&0x07)/10.0",
          "mod(additional, 8.0) / 10.0") < 0 ||
       YglVitaReplace(&translated, "discard;return;", "discard;") < 0 ||
       YglVitaReplace(
          &translated,
          "texelFetch( s_texture, addr,0 )",
          "texture2D(s_texture, (vec2(addr) + vec2(0.5)) / vec2(2048.0, 1024.0))") < 0 ||
       YglVitaReplace(
          &translated,
          "texelFetch( s_line, linepos,0 )",
          "texture2D(s_line, (vec2(linepos) + vec2(0.5)) / vec2(512.0, 1.0))") < 0)
      goto failed;

   return translated;

failed:
   free(translated);
   return NULL;
}

static void YglVitaLogShader(int id, const char *phase)
{
   char message[96];
   snprintf(message, sizeof(message), "renderer: shader %d %s", id, phase);
   VitaGLPresenterLog(message);
}
#endif

int YglInitShader( int id, const GLchar * vertex[], const GLchar * frag[] )
{
    GLint compiled,linked;
    GLuint vshader;
    GLuint fshader;
#ifdef VITA
    char *vita_vertex = YglVitaShaderSource(vertex[0], 0);
    char *vita_fragment = YglVitaShaderSource(frag[0], 1);
    const GLchar *vita_vertex_array[1];
    const GLchar *vita_fragment_array[1];
    if (vita_vertex == NULL || vita_fragment == NULL) {
       free(vita_vertex);
       free(vita_fragment);
       VitaGLPresenterLog("renderer: shader translation allocation failed");
       return -1;
    }
    vita_vertex_array[0] = vita_vertex;
    vita_fragment_array[0] = vita_fragment;
#endif

   _prgid[id] = glCreateProgram();
    if (_prgid[id] == 0 ) return -1;

    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

#ifdef VITA
    YglVitaLogShader(id, "compile vertex");
    glShaderSource(vshader, 1, vita_vertex_array, NULL);
#else
    glShaderSource(vshader, 1, vertex, NULL);
#endif
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
       YGLLOG( "Compile error in vertex shader.\n");
       Ygl_printShaderError(vshader);
       _prgid[id] = 0;
       return -1;
    }

#ifdef VITA
    YglVitaLogShader(id, "compile fragment");
    glShaderSource(fshader, 1, vita_fragment_array, NULL);
#else
    glShaderSource(fshader, 1,frag, NULL);
#endif
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
       YGLLOG( "Compile error in fragment shader.\n");
       Ygl_printShaderError(fshader);
       _prgid[id] = 0;
       return -1;
     }

    glAttachShader(_prgid[id], vshader);
    glAttachShader(_prgid[id], fshader);
#ifdef VITA
    /*
     * vitaGL resolves attributes through the attached vertex shader. YGL
     * submits them by fixed index, so bind after attaching and before link.
     * Bindings for attributes absent from a program are ignored.
     */
    YglVitaLogShader(id, "bind attributes");
    glBindAttribLocation(_prgid[id], 0, "a_position");
    glBindAttribLocation(_prgid[id], 1, "a_texcoord");
    glBindAttribLocation(_prgid[id], 2, "a_grcolor");
    YglVitaLogShader(id, "link");
#endif
    glLinkProgram(_prgid[id]);
#ifdef VITA
    YglVitaLogShader(id, "link returned");
    YglVitaLogShader(id, "query link status");
#endif
    glGetProgramiv(_prgid[id], GL_LINK_STATUS, &linked);
#ifdef VITA
    YglVitaLogShader(id, linked == GL_FALSE ?
                     "link status failed" : "link status passed");
#endif
    if (linked == GL_FALSE) {
       YGLLOG("Link error..\n");
#ifdef VITA
       YglVitaLogShader(id, "read program log");
#endif
       Ygl_printProgramError(_prgid[id], id);
#ifdef VITA
       YglVitaLogShader(id, "program log complete");
       free(vita_vertex);
       free(vita_fragment);
#endif
       _prgid[id] = 0;
       return -1;
    }
#ifdef VITA
    free(vita_vertex);
    free(vita_fragment);
    YglVitaLogShader(id, "ready");
#endif
    return 0;
}

#ifdef VITA
static int id_stencil_half_sprite = -1;
static int id_stencil_half_mode = -1;
static int id_stencil_gouraud_sprite = -1;
static int id_stencil_gouraud_mode = -1;

const GLchar Yglprg_vdp1_halftrans_stencil_f[] =
      "precision highp float;\n"
      "uniform sampler2D u_sprite;\n"
      "uniform float u_half_mode;\n"
      "varying vec4 v_texcoord;\n"
      "void main() {\n"
      "   vec4 spriteColor = texture2D(u_sprite, (floor(v_texcoord.st * vec2(2048.0, 1024.0)) + vec2(0.5)) / vec2(2048.0, 1024.0));\n"
      "   if (spriteColor.a == 0.0) discard;\n"
      "   gl_FragColor = spriteColor;\n"
      "   if (u_half_mode < 1.5) gl_FragColor.a = 0.5;\n"
      "}\n";
const GLchar *pYglprg_vdp1_halftrans_stencil_f[] =
      {Yglprg_vdp1_halftrans_stencil_f, NULL};

const GLchar Yglprg_vdp1_gouraud_halftrans_stencil_f[] =
      "precision highp float;\n"
      "uniform sampler2D u_sprite;\n"
      "uniform float u_half_mode;\n"
      "varying vec4 v_texcoord;\n"
      "varying vec4 v_vtxcolor;\n"
      "void main() {\n"
      "   vec4 spriteColor = texture2D(u_sprite, (floor(v_texcoord.st * vec2(2048.0, 1024.0)) + vec2(0.5)) / vec2(2048.0, 1024.0));\n"
      "   if (spriteColor.a == 0.0) discard;\n"
      "   vec3 gouraudTable = (v_vtxcolor.rgb / max(v_texcoord.w, 0.000001)) * 31.0;\n"
"   vec3 source5 = min(floor(spriteColor.rgb * (255.0 / 8.0) + vec3(0.0001)), vec3(31.0));\n"
"   vec3 table5 = floor(gouraudTable + vec3(0.0001));\n"
"   spriteColor.rgb = clamp(source5 + table5 - vec3(16.0), vec3(0.0), vec3(31.0)) / 31.0;\n"
      "   gl_FragColor = spriteColor;\n"
      "   if (u_half_mode < 1.5) gl_FragColor.a = 0.5;\n"
      "}\n";
const GLchar *pYglprg_vdp1_gouraud_halftrans_stencil_f[] =
      {Yglprg_vdp1_gouraud_halftrans_stencil_f, NULL};

static int Ygl_uniformStencilHalfTrans(void *p)
{
   YglProgram *prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   glUniform1i(id_stencil_half_sprite, 0);
   return 0;
}

static int Ygl_uniformStencilGouraudHalfTrans(void *p)
{
   YglProgram *prg = p;
   glEnableVertexAttribArray(prg->vertexp);
   glEnableVertexAttribArray(prg->texcoordp);
   glEnableVertexAttribArray(prg->vaid);
   glUniform1i(id_stencil_gouraud_sprite, 0);
   return 0;
}

static int Ygl_cleanupStencilGouraudHalfTrans(void *p)
{
   YglProgram *prg = p;
   glDisableVertexAttribArray(prg->vaid);
   return 0;
}
#endif

int YglProgramInit()
{
   YGLLOG("PG_NORMAL\n");
   //
   if( YglInitShader( PG_NORMAL, pYglprg_normal_v, pYglprg_normal_f ) != 0 )
      return -1;

    id_normal_s_texture = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"s_texture");
   //

   _prgid[PG_VFP1_ENDUSERCLIP] = _prgid[PG_NORMAL];
   _prgid[PG_VDP2_ADDBLEND] = _prgid[PG_NORMAL];

   YGLLOG("PG_VDP1_NORMAL\n");
   //
   if( YglInitShader( PG_VDP1_NORMAL, pYglprg_vdp1_normal_v, pYglprg_vdp1_normal_f ) != 0 )
      return -1;

   id_vdp1_normal_s_texture = glGetUniformLocation(_prgid[PG_VDP1_NORMAL], (const GLchar *)"s_texture");


   YGLLOG("PG_VFP1_GOURAUDSAHDING\n");

   //
   if( YglInitShader( PG_VFP1_GOURAUDSAHDING, pYglprg_vdp1_gouraudshading_v, pYglprg_vdp1_gouraudshading_f ) != 0 )
      return -1;

   id_vdp1_normal_s_sprite = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING], (const GLchar *)"u_sprite");

   YGLLOG("PG_VDP2_DRAWFRAMEBUFF --START--\n");

   //
   if( YglInitShader( PG_VDP2_DRAWFRAMEBUFF, pYglprg_vdp2_drawfb_v, pYglprg_vdp2_drawfb_f ) != 0 )
      return -1;

   YGLLOG("PG_VDP2_DRAWFRAMEBUFF --END--\n");

   idvdp1FrameBuffer = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF], (const GLchar *)"s_vdp1FrameBuffer");
   idfrom = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF], (const GLchar *)"u_from");
   idto   = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF], (const GLchar *)"u_to");
   idcoloroffset = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF], (const GLchar *)"u_coloroffset");



   _Ygl->renderfb.prgid=_prgid[PG_VDP2_DRAWFRAMEBUFF];
   _Ygl->renderfb.setupUniform    = Ygl_uniformNormal;
   _Ygl->renderfb.cleanupUniform  = Ygl_cleanupNormal;
   _Ygl->renderfb.vertexp         = glGetAttribLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF],(const GLchar *)"a_position");
   _Ygl->renderfb.texcoordp       = glGetAttribLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF],(const GLchar *)"a_texcoord");
   _Ygl->renderfb.mtxModelView    = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF],(const GLchar *)"u_mvpMatrix");

   YGLLOG("PG_VFP1_HALFTRANS\n");

   //
   if( YglInitShader( PG_VFP1_HALFTRANS, pYglprg_vdp1_halftrans_v, pYglprg_vdp1_halftrans_f ) != 0 )
      return -1;

   id_hf_sprite = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS], (const GLchar *)"u_sprite");
   id_hf_fbo = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS], (const GLchar *)"u_fbo");
   id_hf_fbowidth = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS], (const GLchar *)"u_fbowidth");
   id_hf_fboheight = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS], (const GLchar *)"u_fbohegiht");
   id_half_mode = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS], (const GLchar *)"u_half_mode");

   YGLLOG("PG_VFP1_GOURAUDSAHDING_HALFTRANS\n");

   if( YglInitShader( PG_VFP1_GOURAUDSAHDING_HALFTRANS, pYglprg_vdp1_gouraudshading_hf_v, pYglprg_vdp1_gouraudshading_hf_f ) != 0 )
      return -1;

   id_sprite = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS], (const GLchar *)"u_sprite");
   id_fbo = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS], (const GLchar *)"u_fbo");
   id_fbowidth = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS], (const GLchar *)"u_fbowidth");
   id_fboheight = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS], (const GLchar *)"u_fbohegiht");
   id_gouraud_half_mode = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS], (const GLchar *)"u_half_mode");

#ifdef VITA
   YGLLOG("PG_VFP1_HALFTRANS_STENCIL\n");
   if (YglInitShader(PG_VFP1_HALFTRANS_STENCIL,
                     pYglprg_vdp1_halftrans_v,
                     pYglprg_vdp1_halftrans_stencil_f) != 0)
      return -1;
   id_stencil_half_sprite = glGetUniformLocation(
      _prgid[PG_VFP1_HALFTRANS_STENCIL], "u_sprite");
   id_stencil_half_mode = glGetUniformLocation(
      _prgid[PG_VFP1_HALFTRANS_STENCIL], "u_half_mode");

   YGLLOG("PG_VFP1_GOURAUD_HALFTRANS_STENCIL\n");
   if (YglInitShader(PG_VFP1_GOURAUD_HALFTRANS_STENCIL,
                     pYglprg_vdp1_gouraudshading_hf_v,
                     pYglprg_vdp1_gouraud_halftrans_stencil_f) != 0)
      return -1;
   id_stencil_gouraud_sprite = glGetUniformLocation(
      _prgid[PG_VFP1_GOURAUD_HALFTRANS_STENCIL], "u_sprite");
   id_stencil_gouraud_mode = glGetUniformLocation(
      _prgid[PG_VFP1_GOURAUD_HALFTRANS_STENCIL], "u_half_mode");
#endif

   YGLLOG("PG_WINDOW\n");
   //
   if( YglInitShader( PG_WINDOW, pYglprg_window_v, pYglprg_window_f ) != 0 )
      return -1;

   _Ygl->windowpg.prgid=_prgid[PG_WINDOW];
   _Ygl->windowpg.setupUniform    = Ygl_uniformNormal;
   _Ygl->windowpg.cleanupUniform  = Ygl_cleanupNormal;
   _Ygl->windowpg.vertexp         = glGetAttribLocation(_prgid[PG_WINDOW],(const GLchar *)"a_position");
   _Ygl->windowpg.mtxModelView    = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"u_mvpMatrix");

   _prgid[PG_VFP1_STARTUSERCLIP] = _prgid[PG_WINDOW];

   YGLLOG("PG_LINECOLOR_INSERT\n");
   //
   if (YglInitShader(PG_LINECOLOR_INSERT, pYglprg_linecol_v, pYglprg_linecol_f) != 0)
     return -1;

   id_linecol_s_texture = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"s_texture");
   id_linecol_s_line = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"s_line");
   id_linecol_color_offset = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_color_offset");
   id_linecol_emu_height =   glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_emu_height");
   id_linecol_vheight = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_vheight");

   //
   if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_LINECOLOR, pYglprg_vdp2_drawfb_linecolor_v, pYglprg_vdp2_drawfb_linecolor_f) != 0)
     return -1;

   idvdp1FrameBuffer_linecolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"s_vdp1FrameBuffer");;
   idfrom_linecolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"u_from");
   idto_linecolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"u_to");
   idcoloroffset_linecolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"u_coloroffset");
   id_fblinecol_s_line = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"s_line");
   id_fblinecol_emu_height = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_LINECOLOR], (const GLchar *)"u_emu_height");
   id_fblinecol_vheight = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_vheight");

   //
   if (YglInitShader(PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR, pYglprg_vdp2_drawfb_v, pYglprg_vdp2_drawfb_addcolor_f) != 0)
	   return -1;

   idvdp1FrameBuffer_addcolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR], (const GLchar *)"s_vdp1FrameBuffer");;
   idfrom_addcolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR], (const GLchar *)"u_from");
   idto_addcolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR], (const GLchar *)"u_to");
   idcoloroffset_addcolor = glGetUniformLocation(_prgid[PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR], (const GLchar *)"u_coloroffset");

   return 0;
}



int YglProgramChange( YglLevel * level, int prgid )
{
   YglProgram* tmp;
   YglProgram* current;
#if  USEVBO
   int maxsize;
#endif

   level->prgcurrent++;

   if( level->prgcurrent >= level->prgcount)
   {
      level->prgcount++;
      tmp = (YglProgram*)malloc(sizeof(YglProgram)*level->prgcount);
      if( tmp == NULL ) return -1;
      memset(tmp,0,sizeof(YglProgram)*level->prgcount);
      memcpy(tmp,level->prg,sizeof(YglProgram)*(level->prgcount-1));
      level->prg = tmp;

      level->prg[level->prgcurrent].currentQuad = 0;
#if  USEVBO
       level->prg[level->prgcurrent].maxQuad = 14692;
      maxsize = level->prg[level->prgcurrent].maxQuad;
      if( YglGetVertexBuffer(maxsize,
                             (void**)&level->prg[level->prgcurrent].quads,
                             (void**)&level->prg[level->prgcurrent].textcoords,
                             (void**)&level->prg[level->prgcurrent].vertexAttribute  ) != 0 ) {
          return -1;
      }
      if( level->prg[level->prgcurrent].quads == 0 )
      {
          int a=0;
      }
#else
      level->prg[level->prgcurrent].maxQuad = 12*64;
      if ((level->prg[level->prgcurrent].quads = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float))) == NULL)
         return -1;

      if ((level->prg[level->prgcurrent].textcoords = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float) * 2)) == NULL)
         return -1;

       if ((level->prg[level->prgcurrent].vertexAttribute = (float *) malloc(level->prg[level->prgcurrent].maxQuad * sizeof(float)*2)) == NULL)
         return -1;
#endif
   }

   current = &level->prg[level->prgcurrent];
   level->prg[level->prgcurrent].prgid=prgid;
   level->prg[level->prgcurrent].prg=_prgid[prgid];
   level->prg[level->prgcurrent].vaid = 0;

   if( prgid == PG_NORMAL )
   {
      current->setupUniform    = Ygl_uniformNormal;
      current->cleanupUniform  = Ygl_cleanupNormal;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_texMatrix");
      current->color_offset    = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"u_color_offset");
      current->tex0 = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"s_texture");

   }else if( prgid == PG_VDP1_NORMAL )
   {
      current->setupUniform    = Ygl_uniformVdp1Normal;
      current->cleanupUniform  = Ygl_cleanupVdp1Normal;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VDP1_NORMAL],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VDP1_NORMAL],(const GLchar *)"u_texMatrix");
      current->tex0 = glGetUniformLocation(_prgid[PG_VDP1_NORMAL], (const GLchar *)"s_texture");

   }else if( prgid == PG_VFP1_GOURAUDSAHDING )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformGlowShading;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupGlowShading;
      current->vertexp = 0; 
      current->texcoordp = 1; 
      level->prg[level->prgcurrent].vaid = 2;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING],(const GLchar *)"u_texMatrix");
      current->tex0 = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING], (const GLchar *)"s_texture");
   }
   else if( prgid == PG_VFP1_STARTUSERCLIP )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformStartUserClip;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupStartUserClip;
      current->vertexp         = 0;
      current->texcoordp       = -1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_WINDOW],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = -1; //glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_texMatrix");
      
   }
   else if( prgid == PG_VFP1_ENDUSERCLIP )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformEndUserClip;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupEndUserClip;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_texMatrix");
      current->tex0 = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"s_texture");
   }
#ifdef VITA
   else if (prgid == PG_VFP1_HALFTRANS_STENCIL)
   {
      current->setupUniform = Ygl_uniformStencilHalfTrans;
      current->cleanupUniform = Ygl_cleanupHalfTrans;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView = glGetUniformLocation(
         _prgid[PG_VFP1_HALFTRANS_STENCIL], "u_mvpMatrix");
      current->halftrans_mode = id_stencil_half_mode;
   }
   else if (prgid == PG_VFP1_GOURAUD_HALFTRANS_STENCIL)
   {
      current->setupUniform = Ygl_uniformStencilGouraudHalfTrans;
      current->cleanupUniform = Ygl_cleanupStencilGouraudHalfTrans;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->vaid = 2;
      current->mtxModelView = glGetUniformLocation(
         _prgid[PG_VFP1_GOURAUD_HALFTRANS_STENCIL], "u_mvpMatrix");
      current->halftrans_mode = id_stencil_gouraud_mode;
   }
#endif
   else if( prgid == PG_VFP1_HALFTRANS )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformHalfTrans;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupHalfTrans;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VFP1_HALFTRANS],(const GLchar *)"u_texMatrix");
      current->halftrans_mode  = id_half_mode;

   }
   else if( prgid == PG_VFP1_GOURAUDSAHDING_HALFTRANS )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformGlowShadingHalfTrans;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupGlowShadingHalfTrans;
      current->vertexp = 0;
      current->texcoordp = 1;
      level->prg[level->prgcurrent].vaid = 2;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_VFP1_GOURAUDSAHDING_HALFTRANS],(const GLchar *)"u_texMatrix");
      current->halftrans_mode  = id_gouraud_half_mode;


   }else if( prgid == PG_VDP2_ADDBLEND )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformAddBlend;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupAddBlend;
      current->vertexp = 0;
      current->texcoordp = 1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_texMatrix");
   }else if( prgid == PG_VDP2_STARTWINDOW )
   {
      level->prg[level->prgcurrent].setupUniform = Ygl_uniformStartVDP2Window;
      level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupStartVDP2Window;
      current->vertexp         = 0;
      current->texcoordp       = -1;
      current->mtxModelView    = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_mvpMatrix");
      current->mtxTexture      = glGetUniformLocation(_prgid[PG_NORMAL],(const GLchar *)"u_texMatrix");
   }
   else if (prgid == PG_VDP2_ENDWINDOW)
   {
     level->prg[level->prgcurrent].setupUniform = Ygl_uniformEndVDP2Window;
     level->prg[level->prgcurrent].cleanupUniform = Ygl_cleanupEndVDP2Window;
     current->vertexp = 0;
     current->texcoordp = 1;
     current->mtxModelView = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"u_mvpMatrix");
     current->mtxTexture = glGetUniformLocation(_prgid[PG_NORMAL], (const GLchar *)"u_texMatrix");
   }
   else if (prgid == PG_LINECOLOR_INSERT)
   {
	   current->setupUniform = Ygl_uniformLinecolorInsert;
	   current->cleanupUniform = Ygl_cleanupLinecolorInsert;
	   current->vertexp = 0;
	   current->texcoordp = 1;
	   current->mtxModelView = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_mvpMatrix");
	   current->mtxTexture = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_texMatrix");
	   current->color_offset = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"u_color_offset");
	   current->tex0 = glGetUniformLocation(_prgid[PG_LINECOLOR_INSERT], (const GLchar *)"s_texture");
      
   }else{
      level->prg[level->prgcurrent].setupUniform = NULL;
      level->prg[level->prgcurrent].cleanupUniform = NULL;
   }
   return 0;

}

static int blit_prg = -1;
static int u_w;
static int u_h;

static const char vblit_img[] =
#if defined (_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
"layout (location = 0) in vec2 a_Position; \n"
"layout (location = 1) in vec2 a_Uv;       \n"
"uniform float u_w; \n"
"uniform float u_h; \n"
"out vec2	v_Uv;                    \n"  
"void main()                              \n"
"{                                        \n"
"   gl_Position = vec4((a_Position.x*u_w)-1.0, (a_Position.y*u_h)-1.0, 0.0, 1.0);  \n" 
"   v_Uv = a_Uv;                          \n"
"}";

static const char fblit_img[] =
#if defined (_OGLES3_)
      "#version 300 es \n"
#else
      "#version 330 \n"
#endif
"precision mediump float;     \n"
"uniform sampler2D u_Src; \n"
"in vec2  v_Uv;          \n"
"out vec4 fragColor;            \n"
"void main () \n"
"{ \n"
"  vec4 src = texture2D( u_Src, v_Uv ); \n"
"  fragColor = src; \n"
"}\n";




int YglBlitFramebuffer(u32 srcTexture, u32 targetFbo, float w, float h) {

  float vb[] = { 0, 0, 
    2.0, 0.0, 
    2.0, 2.0, 
    0, 2.0, };

  float tb[] = { 0.0, 0.0,
    1.0, 0.0,
    1.0, 1.0,
    0.0, 1.0 };

  glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);

  if (blit_prg == -1){
    GLuint vshader;
    GLuint fshader;
    GLint compiled, linked;

    const GLchar * vblit_img_v[] = { vblit_img, NULL };
    const GLchar * fblit_img_v[] = { fblit_img, NULL };

    blit_prg = glCreateProgram();
    if (blit_prg == 0) return -1;

    glUseProgram(blit_prg);
    vshader = glCreateShader(GL_VERTEX_SHADER);
    fshader = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vshader, 1, vblit_img_v, NULL);
    glCompileShader(vshader);
    glGetShaderiv(vshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in vertex shader.\n");
      Ygl_printShaderError(vshader);
      blit_prg = -1;
      return -1;
    }

    glShaderSource(fshader, 1, fblit_img_v, NULL);
    glCompileShader(fshader);
    glGetShaderiv(fshader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
      YGLLOG("Compile error in fragment shader.\n");
      Ygl_printShaderError(fshader);
      blit_prg = -1;
      return -1;
    }

    glAttachShader(blit_prg, vshader);
    glAttachShader(blit_prg, fshader);
    glLinkProgram(blit_prg);
    glGetProgramiv(blit_prg, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
      YGLLOG("Link error..\n");
      Ygl_printShaderError(blit_prg);
      blit_prg = -1;
      return -1;
    }

     glUniform1i(glGetUniformLocation(blit_prg, "u_Src"), 0);
     u_w = glGetUniformLocation(blit_prg, "u_w");
     u_h = glGetUniformLocation(blit_prg, "u_h");

  }
  else{
    glUseProgram(blit_prg);
  }


  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);

  glEnableVertexAttribArray(0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vb);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, tb);
  glUniform1f(u_w, w);
  glUniform1f(u_h, h);

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTexture);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  // Clean up
  glActiveTexture(GL_TEXTURE0);
  glDisableVertexAttribArray(0);
  glDisableVertexAttribArray(1);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);

  return 0;
}

#endif
