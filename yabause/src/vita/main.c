#include <psp2/display.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../cdbase.h"
#include "../cs0.h"
#include "../m68kcore.h"
#include "../peripheral.h"
#include "../sh2core.h"
#include "../sh2int.h"
#include "../vidsoft.h"
#include "../yabause.h"
#include "../yui.h"
#include "pervita.h"
#include "sndvita.h"
#include "vitaprofile.h"
#ifdef VITA_USE_VITAGL
#include "vitagl_present.h"
#include "vidvitagl.h"
#endif

#define BIOS_PATH "ux0:data/yabause/bios.bin"
#define BACKUP_PATH "ux0:data/yabause/backup.bin"
#define GAME_PATH "ux0:data/yabause/bin/game.cue"
#define BIOS_SIZE (512 * 1024)

#define DISPLAY_WIDTH 960
#define DISPLAY_HEIGHT 544
#define DISPLAY_PITCH 960
#define SHADER_COMPILER_PATH "ur0:/data/libshacccg.suprx"
#define STARTUP_LOG_PATH "ux0:data/yabause/startup.log"

extern int vdp2width;
extern int vdp2height;

static SceUID framebuffer_blocks[2] = { -1, -1 };
static u32 *framebuffers[2];
static unsigned int draw_buffer;
static int software_display_active;
#ifdef VITA_USE_VITAGL
static int vitagl_active;
static int first_vitagl_frame_logged;
#endif

static void startup_log_reset(const char *message)
{
   FILE *file = fopen(STARTUP_LOG_PATH, "w");
   if (file) {
      fprintf(file, "%s\n", message);
      fclose(file);
   }
}

static void startup_log(const char *message)
{
   FILE *file = fopen(STARTUP_LOG_PATH, "a");
   if (file) {
      fprintf(file, "%s\n", message);
      fclose(file);
   }
}

M68K_struct *M68KCoreList[] = {
   &M68KDummy,
#ifdef HAVE_Q68
   &M68KQ68,
#endif
   NULL
};

SH2Interface_struct *SH2CoreList[] = {
   &SH2Interpreter,
   &SH2DebugInterpreter,
   NULL
};

PerInterface_struct *PERCoreList[] = {
   &PERDummy,
   &PERVita,
   NULL
};

CDInterface *CDCoreList[] = {
   &DummyCD,
   &ISOCD,
   NULL
};

SoundInterface_struct *SNDCoreList[] = {
   &SNDDummy,
   &SNDVita,
   NULL
};

VideoInterface_struct *VIDCoreList[] = {
   &VIDDummy,
#ifdef VITA_USE_VITAGL
   &VIDVitaGL,
#endif
   &VIDSoft,
   NULL
};

static const unsigned char *glyph_for(char character)
{
   static const unsigned char blank[5] = { 0, 0, 0, 0, 0 };
   static const unsigned char unknown[5] = { 0x02, 0x01, 0x51, 0x09, 0x06 };
   static const unsigned char digits[10][5] = {
      { 0x3E, 0x51, 0x49, 0x45, 0x3E }, { 0x00, 0x42, 0x7F, 0x40, 0x00 },
      { 0x42, 0x61, 0x51, 0x49, 0x46 }, { 0x21, 0x41, 0x45, 0x4B, 0x31 },
      { 0x18, 0x14, 0x12, 0x7F, 0x10 }, { 0x27, 0x45, 0x45, 0x45, 0x39 },
      { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, { 0x01, 0x71, 0x09, 0x05, 0x03 },
      { 0x36, 0x49, 0x49, 0x49, 0x36 }, { 0x06, 0x49, 0x49, 0x29, 0x1E }
   };
   static const unsigned char letters[26][5] = {
      { 0x7E, 0x11, 0x11, 0x11, 0x7E }, { 0x7F, 0x49, 0x49, 0x49, 0x36 },
      { 0x3E, 0x41, 0x41, 0x41, 0x22 }, { 0x7F, 0x41, 0x41, 0x22, 0x1C },
      { 0x7F, 0x49, 0x49, 0x49, 0x41 }, { 0x7F, 0x09, 0x09, 0x09, 0x01 },
      { 0x3E, 0x41, 0x49, 0x49, 0x7A }, { 0x7F, 0x08, 0x08, 0x08, 0x7F },
      { 0x00, 0x41, 0x7F, 0x41, 0x00 }, { 0x20, 0x40, 0x41, 0x3F, 0x01 },
      { 0x7F, 0x08, 0x14, 0x22, 0x41 }, { 0x7F, 0x40, 0x40, 0x40, 0x40 },
      { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, { 0x7F, 0x04, 0x08, 0x10, 0x7F },
      { 0x3E, 0x41, 0x41, 0x41, 0x3E }, { 0x7F, 0x09, 0x09, 0x09, 0x06 },
      { 0x3E, 0x41, 0x51, 0x21, 0x5E }, { 0x7F, 0x09, 0x19, 0x29, 0x46 },
      { 0x46, 0x49, 0x49, 0x49, 0x31 }, { 0x01, 0x01, 0x7F, 0x01, 0x01 },
      { 0x3F, 0x40, 0x40, 0x40, 0x3F }, { 0x1F, 0x20, 0x40, 0x20, 0x1F },
      { 0x3F, 0x40, 0x38, 0x40, 0x3F }, { 0x63, 0x14, 0x08, 0x14, 0x63 },
      { 0x07, 0x08, 0x70, 0x08, 0x07 }, { 0x61, 0x51, 0x49, 0x45, 0x43 }
   };
   static const unsigned char colon[5] = { 0, 0x36, 0x36, 0, 0 };
   static const unsigned char slash[5] = { 0x20, 0x10, 0x08, 0x04, 0x02 };
   static const unsigned char period[5] = { 0, 0x60, 0x60, 0, 0 };
   static const unsigned char dash[5] = { 0x08, 0x08, 0x08, 0x08, 0x08 };

   if (character >= 'a' && character <= 'z')
      character -= 'a' - 'A';
   if (character >= 'A' && character <= 'Z')
      return letters[character - 'A'];
   if (character >= '0' && character <= '9')
      return digits[character - '0'];
   if (character == ' ')
      return blank;
   if (character == ':')
      return colon;
   if (character == '/')
      return slash;
   if (character == '.')
      return period;
   if (character == '-')
      return dash;
   return unknown;
}

static void draw_character(u32 *buffer, int x, int y, char character, u32 color)
{
   const unsigned char *glyph = glyph_for(character);
   int column;
   int row;

   for (column = 0; column < 5; ++column) {
      for (row = 0; row < 7; ++row) {
         if (glyph[column] & (1u << row)) {
            int px;
            int py;
            for (py = 0; py < 2; ++py)
               for (px = 0; px < 2; ++px)
                  buffer[(y + row * 2 + py) * DISPLAY_PITCH + x + column * 2 + px] = color;
         }
      }
   }
}

static void draw_text(u32 *buffer, int x, int y, const char *text, u32 color)
{
   int origin_x = x;

   while (*text) {
      if (*text == '\n' || x + 12 >= DISPLAY_WIDTH) {
         x = origin_x;
         y += 18;
         if (*text == '\n') {
            ++text;
            continue;
         }
      }
      if (y + 14 >= DISPLAY_HEIGHT)
         return;
      draw_character(buffer, x, y, *text++, color);
      x += 12;
   }
}

static void present_framebuffer(unsigned int index)
{
   SceDisplayFrameBuf frame;

   memset(&frame, 0, sizeof(frame));
   frame.size = sizeof(frame);
   frame.base = framebuffers[index];
   frame.pitch = DISPLAY_PITCH;
   frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
   frame.width = DISPLAY_WIDTH;
   frame.height = DISPLAY_HEIGHT;
   sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
   sceDisplayWaitVblankStart();
}

static int display_init(void)
{
   int i;
   const unsigned int framebuffer_size = DISPLAY_PITCH * DISPLAY_HEIGHT * sizeof(u32);
   const unsigned int allocation_size = (framebuffer_size + 0x3FFFFu) & ~0x3FFFFu;

   for (i = 0; i < 2; ++i) {
      framebuffer_blocks[i] = sceKernelAllocMemBlock(
         i == 0 ? "Yabause framebuffer 0" : "Yabause framebuffer 1",
         SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
         allocation_size,
         NULL);
      if (framebuffer_blocks[i] < 0 ||
          sceKernelGetMemBlockBase(framebuffer_blocks[i], (void **)&framebuffers[i]) < 0)
         return -1;
      memset(framebuffers[i], 0, framebuffer_size);
   }

   draw_buffer = 0;
   software_display_active = 1;
   present_framebuffer(draw_buffer);
   return 0;
}

static void display_deinit(void)
{
   int i;
   if (!software_display_active)
      return;
   sceDisplaySetFrameBuf(NULL, SCE_DISPLAY_SETBUF_IMMEDIATE);
   for (i = 0; i < 2; ++i) {
      if (framebuffer_blocks[i] >= 0)
         sceKernelFreeMemBlock(framebuffer_blocks[i]);
      framebuffer_blocks[i] = -1;
      framebuffers[i] = NULL;
   }
   software_display_active = 0;
}

static void show_error(const char *message)
{
   u32 *buffer;
#ifdef VITA_USE_VITAGL
   if (vitagl_active) {
      buffer = (u32 *)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(u32));
      if (!buffer)
         return;
      memset(buffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(u32));
      draw_text(buffer, 48, 48, "YABAUSE FOR PS VITA", 0xFFFFFFFF);
      draw_text(buffer, 48, 90, message, 0xFF8080FF);
      draw_text(buffer, 48, 180, "CLOSE THE APPLICATION AND CORRECT THE FILE.", 0xFFC0C0C0);
      VitaGLPresenterPresent(buffer, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      free(buffer);
      return;
   }
#endif
   buffer = framebuffers[draw_buffer];
   if (!buffer)
      return;

   memset(buffer, 0, DISPLAY_PITCH * DISPLAY_HEIGHT * sizeof(u32));
   draw_text(buffer, 48, 48, "YABAUSE FOR PS VITA", 0xFFFFFFFF);
   draw_text(buffer, 48, 90, message, 0xFF8080FF);
   draw_text(buffer, 48, 180, "CLOSE THE APPLICATION AND CORRECT THE FILE.", 0xFFC0C0C0);
   present_framebuffer(draw_buffer);
}

static int prepare_startup_files(void)
{
   SceIoStat stat;

   sceIoMkdir("ux0:data/yabause", 0777);
   memset(&stat, 0, sizeof(stat));
   if (sceIoGetstat(BIOS_PATH, &stat) < 0)
      return -1;
   if (stat.st_size != BIOS_SIZE)
      return -2;
#ifdef VITA_BOOT_GAME
   memset(&stat, 0, sizeof(stat));
   if (sceIoGetstat(GAME_PATH, &stat) < 0)
      return -3;
#endif
   return 0;
}

void YuiErrorMsg(const char *message)
{
   startup_log(message ? message : "UNKNOWN EMULATION ERROR");
   show_error(message ? message : "UNKNOWN EMULATION ERROR");
}

void YuiSwapBuffers(void)
{
   int source_width = vdp2width;
   int source_height = vdp2height;

#ifdef VITA_USE_VITAGL
   if (vitagl_active && VIDCore == &VIDVitaGL) {
      VitaProfileBegin(VITA_PROFILE_PRESENT);
      if (!first_vitagl_frame_logged) {
         startup_log("first VIDVitaGL frame presented");
         first_vitagl_frame_logged = 1;
      }
      VitaGLPresenterSwapNative();
      VitaProfileEnd(VITA_PROFILE_PRESENT);
      return;
   }
#endif

   if (!dispbuffer || source_width <= 0 || source_width > 704 ||
       source_height <= 0 || source_height > 512)
      return;

   VitaProfileBegin(VITA_PROFILE_PRESENT);
#ifdef VITA_USE_VITAGL
   if (vitagl_active) {
      if (!first_vitagl_frame_logged) {
         startup_log("first VIDSoft frame presented");
         first_vitagl_frame_logged = 1;
      }
      VitaGLPresenterPresent(dispbuffer, source_width, source_height);
   }
#else
   {
      u32 *destination;
      int scale_x = DISPLAY_WIDTH / source_width;
      int scale_y = DISPLAY_HEIGHT / source_height;
      int scale = scale_x < scale_y ? scale_x : scale_y;
      int output_width;
      int output_height;
      int origin_x;
      int origin_y;
      int x;
      int y;

      if (scale < 1)
         scale = 1;
      output_width = source_width * scale;
      output_height = source_height * scale;
      origin_x = (DISPLAY_WIDTH - output_width) / 2;
      origin_y = (DISPLAY_HEIGHT - output_height) / 2;

      draw_buffer ^= 1;
      destination = framebuffers[draw_buffer];
      memset(destination, 0, DISPLAY_PITCH * DISPLAY_HEIGHT * sizeof(u32));

      for (y = 0; y < output_height; ++y) {
         const u32 *source = dispbuffer + (y / scale) * source_width;
         u32 *line = destination + (origin_y + y) * DISPLAY_PITCH + origin_x;
         for (x = 0; x < output_width; ++x)
            line[x] = 0xFF000000u | source[x / scale];
      }
      present_framebuffer(draw_buffer);
   }
#endif
   VitaProfileEnd(VITA_PROFILE_PRESENT);
}

int main(void)
{
   yabauseinit_struct init;
   int bios_status;
   int result;

   if (display_init() < 0)
      sceKernelExitProcess(1);

#ifdef VITA_USE_VITAGL
   {
      SceIoStat shader_stat;
      memset(&shader_stat, 0, sizeof(shader_stat));
      if (sceIoGetstat(SHADER_COMPILER_PATH, &shader_stat) < 0) {
         show_error("VITAGL NEEDS LIBSHACCCG.SUPRX AT:\n" SHADER_COMPILER_PATH);
         sceKernelDelayThread(10 * 1000 * 1000);
         display_deinit();
         sceKernelExitProcess(1);
      }
   }
#endif

   bios_status = prepare_startup_files();
   if (bios_status != 0) {
      if (bios_status == -1)
         show_error("BIOS NOT FOUND. COPY A 512 KIB SATURN BIOS TO:\n" BIOS_PATH);
      else if (bios_status == -2)
         show_error("INVALID BIOS SIZE. BIOS.BIN MUST BE EXACTLY 512 KIB.");
      else
         show_error("GAME CUE NOT FOUND. COPY THE DISC IMAGE TO:\n" GAME_PATH);
      sceKernelDelayThread(10 * 1000 * 1000);
      display_deinit();
      sceKernelExitProcess(1);
   }

#ifdef VITA_BOOT_GAME
   startup_log_reset("BIOS and game CUE validated");
   startup_log("boot mode: game via ISOCD at " GAME_PATH);
#else
   startup_log_reset("BIOS validated");
   startup_log("boot mode: BIOS via DummyCD");
#endif

#ifdef VITA_USE_VITAGL
   startup_log("starting vitaGL initialization");
   display_deinit();
   if (VitaGLPresenterInit() < 0) {
      display_init();
      show_error("VITAGL COULD NOT INITIALIZE.");
      sceKernelDelayThread(10 * 1000 * 1000);
      display_deinit();
      sceKernelExitProcess(1);
   }
   vitagl_active = 1;
   startup_log("vitaGL initialized and first swap submitted");
#endif
   VitaProfileInit();

   memset(&init, 0, sizeof(init));
   init.percoretype = PERCORE_VITA;
   init.sh1coretype = SH2CORE_INTERPRETER;
   init.sh2coretype = SH2CORE_INTERPRETER;
#ifdef VITA_USE_VITAGL
   init.vidcoretype = VIDCORE_VITAGL;
#else
   init.vidcoretype = VIDCORE_SOFT;
#endif
#ifdef VITA_DISABLE_AUDIO
   init.sndcoretype = SNDCORE_DUMMY;
   init.m68kcoretype = M68KCORE_DUMMY;
   startup_log("runtime: audio output and 68K sound CPU disabled");
#else
   init.sndcoretype = SNDCORE_VITA;
   init.m68kcoretype = M68KCORE_Q68;
#endif
#ifdef VITA_BOOT_GAME
   init.cdcoretype = CDCORE_ISO;
#else
   init.cdcoretype = CDCORE_DUMMY;
#endif
   init.carttype = CART_NONE;
   init.regionid = REGION_AUTODETECT;
   init.biospath = BIOS_PATH;
#ifdef VITA_BOOT_GAME
   init.cdpath = GAME_PATH;
#else
   init.cdpath = NULL;
#endif
   init.buppath = BACKUP_PATH;
   init.frameskip = 0;
   init.videoformattype = VIDEOFORMATTYPE_NTSC;
   init.clocksync = 0;
   init.usethreads = 0;
   init.numthreads = 1;
   init.skip_load = 0;
   init.use_new_scsp = 0;
   init.use_cd_block_lle = 0;
   init.use_scsp_dsp_dynarec = 0;
   init.use_scu_dsp_jit = 0;

   startup_log("starting YabauseInit");
   result = YabauseInit(&init);
   if (result != 0) {
#ifdef VITA_BOOT_GAME
      show_error("YABAUSE COULD NOT INITIALIZE THE GAME. CHECK GAME.CUE AND ITS BIN TRACKS.");
#else
      show_error("YABAUSE COULD NOT INITIALIZE. CHECK THE BIOS FILE.");
#endif
      sceKernelDelayThread(10 * 1000 * 1000);
      display_deinit();
      sceKernelExitProcess(1);
   }

   startup_log("YabauseInit completed");
   do {
      VitaProfileBegin(VITA_PROFILE_FRAME);
      result = YabauseExec();
      VitaProfileEnd(VITA_PROFILE_FRAME);
      VitaProfileFrameComplete();
      if (result == 0)
         PERCore->HandleEvents();
   } while (result == 0);

   YabauseDeInit();
   VitaProfileShutdown();
#ifdef VITA_USE_VITAGL
   VitaGLPresenterShutdown();
   vitagl_active = 0;
#else
   display_deinit();
#endif
   sceKernelExitProcess(result == 0 ? 0 : 1);
   return 0;
}
