/*  Copyright 2005 Guillaume Duhamel
    Copyright 2005-2006 Theo Berkau

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

#if defined(HAVE_LIBGL) || defined(__ANDROID__)

#if defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl3ext.h>
    #include <EGL/egl.h>

#elif defined(VITA)
    #include <vitaGL.h>
    /*
     * vitaGL exposes buffer mapping but not the desktop PBO target tokens.
     * The Vita paths avoid PBO uploads; these values keep the legacy
     * framebuffer-read fallback buildable until it is replaced.
     */
    #ifndef GL_PIXEL_PACK_BUFFER
    #define GL_PIXEL_PACK_BUFFER 0x88EB
    #endif
    #ifndef GL_PIXEL_UNPACK_BUFFER
    #define GL_PIXEL_UNPACK_BUFFER 0x88EC
    #endif
    #ifndef GL_DITHER
    #define GL_DITHER 0x0BD0
    #endif

#elif defined(_WIN32)

#include <windows.h>
  #if defined(_USEGLEW_)
    #include <GL/glew.h>
    #include <GL/gl.h>
    #include "glext.h"
#else
    #include <GL/gl.h>
    #include "glext.h"
    extern PFNGLACTIVETEXTUREPROC glActiveTexture;
  #endif

#elif  defined(__APPLE__)
    #define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
    #include <OpenGL/gl.h>
    #include <OpenGL/gl3.h>

#else // Linux?
    #if defined(_OGLES3_)||defined(_OGL3_)
        #define GL_GLEXT_PROTOTYPES 1
        #define GLX_GLXEXT_PROTOTYPES 1
        #include <GL/glew.h>
        #include <GL/gl.h>
    #else
        #include <GL/gl.h>
    #endif
#endif

#include <stdarg.h>
#include <string.h>

#ifndef YGL_H
#define YGL_H

#include "core.h"
#include "threads.h"
#include "vidshared.h"

typedef struct {
	float vertices[8];
	int w;
	int h;
	int flip;
	int priority;
	int dst;
    int uclipmode;
    int blendmode;
    s32 cor;
    s32 cog;
    s32 cob;
    int linescreen;

} YglSprite;

#define YGL_VDP1_HALF_TRANSPARENT 0x80
#define YGL_VDP1_MESH_LEGACY 0x100
#define YGL_VDP1_STENCIL_WINDOW_MASK 0x7F
#define YGL_VDP1_STENCIL_OCCUPIED 0x80

typedef struct {
	float x;
	float y;
#ifdef VITA_ATLAS_PAGED
	unsigned int atlasPage;
	unsigned int atlasGeneration;
#endif
} YglCache;

typedef struct {
	unsigned int * textdata;
	unsigned int w;
#ifdef VITA_ATLAS_PAGED
	unsigned int atlasPage;
	unsigned int atlasGeneration;
#endif
} YglTexture;

#ifdef VITA_ATLAS_PAGED
typedef struct {
	unsigned int *texture;
	unsigned int currentX, currentY, yMax, generation;
} YglAtlasPage;
#endif


typedef struct {
	unsigned int currentX;
	unsigned int currentY;
	unsigned int yMax;
	unsigned int * texture;
	unsigned int width;
	unsigned int height;
#ifdef VITA_ATLAS_PAGED
	YglAtlasPage *pages;
	unsigned int pageCount, pageCapacity, activePage;
#endif
} YglTextureManager;

extern YglTextureManager * YglTM;

void YglTMInit(unsigned int, unsigned int);
void YglTMDeInit(void);
void YglTMReset(void);
void YglTMAllocate(YglTexture *, unsigned int, unsigned int, unsigned int *, unsigned int *);
#ifdef VITA_TEXTURE_CACHE
void YglVitaForcePersistentAllocation(unsigned int x, unsigned int y);
void YglVitaForcePersistentPartialAllocation(unsigned int x, unsigned int y);
void YglVitaMarkPersistentDirty(unsigned int x, unsigned int y,
                                unsigned int width, unsigned int height);
#endif

enum
{
   PG_NORMAL=1,
   PG_VDP1_NORMAL,
   PG_VFP1_GOURAUDSAHDING,
   PG_VFP1_STARTUSERCLIP,
   PG_VFP1_ENDUSERCLIP,
   PG_VFP1_HALFTRANS,    
   PG_VFP1_GOURAUDSAHDING_HALFTRANS, 
   PG_VDP2_ADDBLEND,
   PG_VDP2_DRAWFRAMEBUFF,    
   PG_VDP2_STARTWINDOW,
   PG_VDP2_ENDWINDOW,
   PG_WINDOW,
   PG_LINECOLOR_INSERT,
   PG_VDP2_DRAWFRAMEBUFF_LINECOLOR,
   PG_VDP2_DRAWFRAMEBUFF_ADDCOLOR,
#ifdef VITA
   PG_VFP1_HALFTRANS_STENCIL,
   PG_VFP1_GOURAUD_HALFTRANS_STENCIL,
#endif
   PG_MAX,
};

typedef struct {
   int prgid;
   GLuint prg;
   GLuint vertexBuffer;
   float * quads;
   float * textcoords;
   float * vertexAttribute;
   int currentQuad;
   int maxQuad;
   int vaid;
   char uClipMode;
   short ux1,uy1,ux2,uy2;
   int blendmode;
   int bwin0,logwin0,bwin1,logwin1,winmode;
   GLuint vertexp;
   GLuint texcoordp;
   GLuint mtxModelView;
   GLuint mtxTexture;
   GLuint color_offset;
   GLuint tex0;
   GLuint tex1;
   GLint halftrans_mode;
   float color_offset_val[4];
   int (*setupUniform)(void *);
   int (*cleanupUniform)(void *);
#ifdef VITA_ATLAS_PAGED
   unsigned int atlasPage;
   unsigned int atlasGeneration;
#endif
} YglProgram;

typedef struct {
   int prgcount;
   int prgcurrent;
   int uclipcurrent;
   short ux1,uy1,ux2,uy2;
   int blendmode;
   YglProgram * prg;
} YglLevel;

typedef struct
{
    GLfloat   m[4][4];
} YglMatrix;

typedef struct {
   GLuint texture;
#ifdef VITA
   GLuint atlasTextures[2];
   unsigned int atlasTextureCount;
   unsigned int activeAtlas;
#ifdef VITA_ATLAS_PAGED
   unsigned int loadedOverflowPage;
   unsigned int loadedOverflowGeneration;
#endif
#endif
   GLuint pixelBufferID;
   int st;
   char message[512];
   int msglength;
   unsigned int width;
   unsigned int height;
   unsigned int depth;

   float clear_r;
   float clear_g;
   float clear_b;
   
   // VDP1 Info
   int vdp1_maxpri;
   int vdp1_minpri;
   
   // VDP1 Framebuffer
   int rwidth;
   int rheight;
   int drawframe;
   int readframe;
   GLuint rboid_depth;
   GLuint rboid_stencil;
   GLuint vdp1fbo;
   GLuint vdp1FrameBuff[2];
   /* Ping-pong target used by VDP1 half-transparency. */
   GLuint vdp1FeedbackTexture;
   GLuint vdp1FeedbackFbo;
   GLuint smallfbo;
   GLuint smallfbotex;
   GLuint vdp1pixelBufferID;
   void * pFrameBuffer;

   // Message Layer
   int msgwidth;
   int msgheight;
   GLuint msgtexture;
   u32 * messagebuf;

   int bUpdateWindow;
   int win0v[512*4];
   int win0_vertexcnt;
   int win1v[512*4];
   int win1_vertexcnt;

   YglMatrix mtxModelView;
   YglMatrix mtxTexture;

   YglProgram windowpg;
   YglProgram renderfb;

   YglLevel * levels;
   
   u32 lincolor_tex;
   u32 linecolor_pbo;
   u32 * lincolor_buf;

}  Ygl;

extern Ygl * _Ygl;


int YglGLInit(int, int);
int YglInit(int, int, unsigned int);
void YglDeInit(void);
float * YglQuad(YglSprite *, YglTexture *,YglCache * c);
void YglQuadOffset(YglSprite * input, YglTexture * output, YglCache * c, int cx, int cy, float sx, float sy);
void YglCachedQuadOffset(YglSprite * input, YglCache * cache, int cx, int cy, float sx, float sy);
void YglCachedQuad(YglSprite *, YglCache *);
void YglRender(void);
void YglReset(void);
void YglShowTexture(void);
void YglChangeResolution(int, int);
void YglCacheQuadGrowShading(YglSprite * input, float * colors, YglCache * cache);
int YglQuadGrowShading(YglSprite * input, YglTexture * output, float * colors,YglCache * c);
void YglSetClearColor(float r, float g, float b);
void YglStartWindow( vdp2draw_struct * info, int win0, int logwin0, int win1, int logwin1, int mode );
void YglEndWindow( vdp2draw_struct * info );

void YglCacheInit(void);
void YglCacheDeInit(void);
int YglIsCached(u32,YglCache *);
void YglCacheAdd(u32,YglCache *);
void YglCacheReset(void);

// 0.. no belnd, 1.. Alpha, 2.. Add 
int YglSetLevelBlendmode( int pri, int mode );

void Ygl_uniformVDP2DrawFramebuffer_linecolor(void * p, float from, float to, float * offsetcol);
int Ygl_uniformVDP2DrawFramebuffer_addcolor(void * p, float from, float to, float * offsetcol);
void Ygl_uniformVDP2DrawFramebuffer( void * p,float from, float to , float * offsetcol );

void YglNeedToUpdateWindow();

void YglScalef(YglMatrix *result, GLfloat sx, GLfloat sy, GLfloat sz);
void YglTranslatef(YglMatrix *result, GLfloat tx, GLfloat ty, GLfloat tz);
void YglRotatef(YglMatrix *result, GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void YglFrustum(YglMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ);
void YglPerspective(YglMatrix *result, float fovy, float aspect, float nearZ, float farZ);
void YglOrtho(YglMatrix *result, float left, float right, float bottom, float top, float nearZ, float farZ);
void YglLoadIdentity(YglMatrix *result);
void YglMatrixMultiply(YglMatrix *result, YglMatrix *srcA, YglMatrix *srcB);

int YglInitVertexBuffer( int initsize );
void YglDeleteVertexBuffer();
int YglUnMapVertexBuffer();
int YglMapVertexBuffer();
int YglUserDirectVertexBuffer();
int YglUserVertexBuffer();
int YglGetVertexBuffer( int size, void ** vpos, void **tcpos, void **vapos );
int YglExpandVertexBuffer( int addsize, void ** vpos, void **tcpos, void **vapos );
intptr_t YglGetOffset( void* address );
int YglBlitFramebuffer(u32 srcTexture, u32 targetFbo, float w, float h);

void YglRenderVDP1(void);
#ifdef VITA_ATLAS_BUFFERED
void YglVitaBeginAtlasFrame(void);
#endif
u32 * YglGetLineColorPointer();
void YglSetLineColor(u32 * pbuf, int size);

int Ygl_uniformWindow(void * p );
int YglProgramInit();
int YglProgramChange( YglLevel * level, int prgid );

#if !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_USEGLEW_) && !defined(_OGLES3_) && !defined(VITA)

extern GLuint (STDCALL *glCreateProgram)(void);
extern GLuint (STDCALL *glCreateShader)(GLenum);
extern void (STDCALL *glShaderSource)(GLuint,GLsizei,const GLchar **,const GLint *);
extern void (STDCALL *glCompileShader)(GLuint);
extern void (STDCALL *glAttachShader)(GLuint,GLuint);
extern void (STDCALL *glLinkProgram)(GLuint);
extern void (STDCALL *glUseProgram)(GLuint);
extern GLint (STDCALL *glGetUniformLocation)(GLuint,const GLchar *);
extern void (STDCALL *glUniform1i)(GLint,GLint);
extern void (STDCALL *glGetShaderInfoLog)(GLuint,GLsizei,GLsizei *,GLchar *);
extern void (STDCALL *glVertexAttribPointer)(GLuint index,GLint size, GLenum type, GLboolean normalized, GLsizei stride,const void *pointer);
extern void (STDCALL *glBindAttribLocation)( GLuint program, GLuint index, const GLchar * name);
extern void (STDCALL *glGetProgramiv)( GLuint    program, GLenum pname, GLint * params);
extern void (STDCALL *glGetShaderiv)(GLuint shader,GLenum pname,GLint *    params);
extern GLint (STDCALL *glGetAttribLocation)(GLuint program,const GLchar *    name);

extern void (STDCALL *glEnableVertexAttribArray)(GLuint index);
extern void (STDCALL *glDisableVertexAttribArray)(GLuint index);


//GL_ARB_framebuffer_object
extern PFNGLISRENDERBUFFERPROC glIsRenderbuffer;
extern PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer;
extern PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers;
extern PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers;
extern PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage;
extern PFNGLGETRENDERBUFFERPARAMETERIVPROC glGetRenderbufferParameteriv;
extern PFNGLISFRAMEBUFFERPROC glIsFramebuffer;
extern PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
extern PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
extern PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
extern PFNGLFRAMEBUFFERTEXTURE1DPROC glFramebufferTexture1D;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
extern PFNGLFRAMEBUFFERTEXTURE3DPROC glFramebufferTexture3D;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
extern PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC glGetFramebufferAttachmentParameteriv;
extern PFNGLGENERATEMIPMAPPROC glGenerateMipmap;
extern PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC glRenderbufferStorageMultisample;
extern PFNGLFRAMEBUFFERTEXTURELAYERPROC glFramebufferTextureLayer;

extern PFNGLUNIFORM4FPROC glUniform4f;
extern PFNGLUNIFORM1FPROC glUniform1f;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv;

#endif // !defined(__APPLE__) && !defined(__ANDROID__) && !defined(_USEGLEW_)

#endif // YGL_H

#endif // defined(HAVE_LIBGL) || defined(__ANDROID__)
