/*  Copyright 2004-2008, 2013 Theo Berkau
    Copyright 2005 Joost Peters
    Copyright 2005-2006 Guillaume Duhamel
    
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

/*! \file cdbase.c
    \brief Dummy and ISO, BIN/CUE, MDS, CCD CD Interfaces
*/

#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>
#include <wchar.h>
#include "cdbase.h"
#include "cs2.h"
#include "error.h"
#include "debug.h"

#ifndef HAVE_STRICMP
#ifdef HAVE_STRCASECMP
#define stricmp strcasecmp
#endif
#endif

#ifndef HAVE_WFOPEN
static char * wcsdupstr(const wchar_t * path)
{
   char * mbs;
   size_t len = wcstombs(NULL, path, 0);
   if (len == (size_t) -1) return NULL;

   mbs = malloc(len);
   len = wcstombs(mbs, path, len);
   if (len == (size_t) -1)
   {
      free(mbs);
      return NULL;
   }

   return mbs;
}

static FILE * _wfopen(const wchar_t *wpath, const wchar_t *wmode)
{
   FILE * fd;
   char * path;
   char * mode;

   path = wcsdupstr(wpath);
   if (path == NULL) return NULL;

   mode = wcsdupstr(wmode);
   if (mode == NULL)
   {
      free(path);
      return NULL;
   }

   fd = fopen(path, mode);

   free(path);
   free(mode);

   return fd;
}
#endif

//////////////////////////////////////////////////////////////////////////////

// Contains the Dummy and ISO CD Interfaces

static int DummyCDInit(const char *);
static void DummyCDDeInit(void);
static int DummyCDGetStatus(void);
static s32 DummyCDReadTOC(u32 *);
static s32 DummyCDReadTOC10(CDInterfaceToc10 *);
static int DummyCDReadSectorFAD(u32, void *);
static void DummyCDReadAheadFAD(u32);

CDInterface DummyCD = {
CDCORE_DUMMY,
"Dummy CD Drive",
DummyCDInit,
DummyCDDeInit,
DummyCDGetStatus,
DummyCDReadTOC,
DummyCDReadTOC10,
DummyCDReadSectorFAD,
DummyCDReadAheadFAD,
};

static int ISOCDInit(const char *);
static void ISOCDDeInit(void);
static int ISOCDGetStatus(void);
static s32 ISOCDReadTOC(u32 *);
static s32 ISOCDReadTOC10(CDInterfaceToc10 *);
static int ISOCDReadSectorFAD(u32, void *);
static void ISOCDReadAheadFAD(u32);

CDInterface ISOCD = {
CDCORE_ISO,
"ISO-File Virtual Drive",
ISOCDInit,
ISOCDDeInit,
ISOCDGetStatus,
ISOCDReadTOC,
ISOCDReadTOC10,
ISOCDReadSectorFAD,
ISOCDReadAheadFAD,
};

//////////////////////////////////////////////////////////////////////////////
// Dummy Interface
//////////////////////////////////////////////////////////////////////////////

static int DummyCDInit(UNUSED const char *cdrom_name)
{
	// Initialization function. cdrom_name can be whatever you want it to
	// be. Obviously with some ports(e.g. the dreamcast port) you probably
	// won't even use it.
	return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void DummyCDDeInit(void)
{
	// Cleanup function. Enough said.
}

//////////////////////////////////////////////////////////////////////////////

static int DummyCDGetStatus(void)
{
	// This function is called periodically to see what the status of the
	// drive is.
	//
	// Should return one of the following values:
	// 0 - CD Present, disc spinning
	// 1 - CD Present, disc not spinning
	// 2 - CD not present
	// 3 - Tray open
	//
	// If you really don't want to bother too much with this function, just
	// return status 0. Though it is kind of nice when the bios's cd
	// player, etc. recognizes when you've ejected the tray and popped in
	// another disc.

	return 0;
}

//////////////////////////////////////////////////////////////////////////////

static s32 DummyCDReadTOC(UNUSED u32 *TOC)
{
	// The format of TOC is as follows:
	// TOC[0] - TOC[98] are meant for tracks 1-99. Each entry has the
	// following format:
	// bits 0 - 23: track FAD address
	// bits 24 - 27: track addr
	// bits 28 - 31: track ctrl
	//
	// Any Unused tracks should be set to 0xFFFFFFFF
	//
	// TOC[99] - Point A0 information 
	// Uses the following format:
	// bits 0 - 7: PFRAME(should always be 0)
	// bits 7 - 15: PSEC(Program area format: 0x00 - CDDA or CDROM,
	//                   0x10 - CDI, 0x20 - CDROM-XA)
	// bits 16 - 23: PMIN(first track's number)
	// bits 24 - 27: first track's addr
	// bits 28 - 31: first track's ctrl
	//
	// TOC[100] - Point A1 information
	// Uses the following format:
	// bits 0 - 7: PFRAME(should always be 0)
	// bits 7 - 15: PSEC(should always be 0)
	// bits 16 - 23: PMIN(last track's number)
	// bits 24 - 27: last track's addr
	// bits 28 - 31: last track's ctrl
	//
	// TOC[101] - Point A2 information
	// Uses the following format:
	// bits 0 - 23: leadout FAD address
	// bits 24 - 27: leadout's addr
	// bits 28 - 31: leadout's ctrl
	//
	// Special Note: To convert from LBA/LSN to FAD, add 150.

	return 0;
}

//////////////////////////////////////////////////////////////////////////////

static s32 DummyCDReadTOC10(UNUSED CDInterfaceToc10 *TOC)
{
	return 0;
}

//////////////////////////////////////////////////////////////////////////////

static int DummyCDReadSectorFAD(UNUSED u32 FAD, void * buffer)
{
	// This function is supposed to read exactly 1 -RAW- 2352-byte sector
	// at the specified FAD address to buffer. Should return true if
	// successful, false if there was an error.
	//
	// Special Note: To convert from FAD to LBA/LSN, minus 150.
	//
	// The whole process needed to be changed since I need more control
	// over sector detection, etc. Not to mention it means less work for
	// the porter since they only have to implement raw sector reading as
	// opposed to implementing mode 1, mode 2 form1/form2, -and- raw
	// sector reading.

	memset(buffer, 0, 2352);

	return 1;
}

//////////////////////////////////////////////////////////////////////////////

static void DummyCDReadAheadFAD(UNUSED u32 FAD)
{
	// This function is called to tell the driver which sector (FAD
	// address) is expected to be read next. If the driver supports
	// read-ahead, it should start reading the given sector in the
	// background while the emulation continues, so that when the
	// sector is actually read with ReadSectorFAD() it'll be available
	// immediately. (Note that there's no guarantee this sector will
	// actually be requested--the emulated CD might be stopped before
	// the sector is read, for example.)
	//
	// This function should NOT block. If the driver can't perform
	// asynchronous reads (or you just don't want to bother handling
	// them), make this function a no-op and just read sectors
	// normally.
}

//////////////////////////////////////////////////////////////////////////////
// ISO Interface
//////////////////////////////////////////////////////////////////////////////

typedef struct
{
   u8 ctl_addr;
   u32 fad_start;
   u32 fad_end;
   u32 file_offset;
   u32 toc_fad_start;
   u32 sector_size;
   FILE *fp;
	FILE *sub_fp;
   int file_size;
   int file_id;
   int interleaved_sub;
} track_info_struct;

typedef struct
{
   u32 fad_start;
   u32 fad_end;
   track_info_struct *track;
   int track_num;
} session_info_struct;

typedef struct
{
   int session_num;
   session_info_struct *session;
} disc_info_struct;

#pragma pack(push, 1)
typedef struct
{
   u8 signature[16];
   u8 version[2];
   u16 medium_type;
   u16 session_count;
   u16 unused1[2];
   u16 bca_length;
   u32 unused2[2];
   u32 bca_offset;
   u32 unused3[6];
   u32 disk_struct_offset;
   u32 unused4[3];
   u32 sessions_blocks_offset;
   u32 dpm_blocks_offset;
   u32 enc_key_offset;
} mds_header_struct;

typedef struct
{
   s32 session_start;
   s32 session_end;
   u16 session_number;
   u8 total_blocks;
   u8 leadin_blocks;
   u16 first_track;
   u16 last_track;
   u32 unused;
   u32 track_blocks_offset;
} mds_session_struct;

typedef struct
{
   u8 mode;
   u8 subchannel_mode;
   u8 addr_ctl;
   u8 unused1;
   u8 track_num;
   u32 unused2;
   u8 m;
   u8 s;
   u8 f;
   u32 extra_offset;
   u16 sector_size;
   u8 unused3[18];
   u32 start_sector;
   u64 start_offset;
   u8 session;
   u8 unused4[3];
   u32 footer_offset;
   u8 unused5[24];
} mds_track_struct;

typedef struct
{
   u32 filename_offset;
   u32 is_widechar;
   u32 unused1;
   u32 unused2;
} mds_footer_struct;

#pragma pack(pop)

#define CCD_MAX_SECTION 20
#define CCD_MAX_NAME 30
#define CCD_MAX_VALUE 20

typedef struct
{
	char section[CCD_MAX_SECTION];
	char name[CCD_MAX_NAME];
	char value[CCD_MAX_VALUE];
} ccd_dict_struct;

typedef struct
{
	ccd_dict_struct *dict;
	int num_dict;
} ccd_struct;

static const s8 syncHdr[12] = { 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00 };
enum IMG_TYPE { IMG_NONE, IMG_ISO, IMG_BINCUE, IMG_MDS, IMG_CCD, IMG_NRG };
enum IMG_TYPE imgtype = IMG_ISO;
static u32 isoTOC[102];
static CDInterfaceToc10 isoTOC10[102];
int isoTOCnum=0;
static disc_info_struct disc;

#define MSF_TO_FAD(m,s,f) ((m * 4500) + (s * 75) + f)

//////////////////////////////////////////////////////////////////////////////

#define CUE_MAX_TRACKS 99
#define CUE_MAX_FILES 99
#define CUE_LINE_SIZE 2048
#define CUE_PATH_SIZE 1024

typedef struct {
   char name[CUE_PATH_SIZE], resolved[CUE_PATH_SIZE];
   FILE *fp;
   long size;
   u32 sectors;
   int first_track, track_count;
} cue_file_struct;

typedef struct {
   track_info_struct info;
   unsigned int number;
   int file_index;
   u32 index0, index1, pregap, postgap;
   int has_index0, has_index1;
} cue_track_struct;

static char *CueTrim(char *text)
{
   char *end;
   while (*text && isspace((unsigned char)*text)) text++;
   end = text + strlen(text);
   while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
   return text;
}

static int CuePrefix(const char *text, const char *prefix)
{
   while (*prefix)
      if (toupper((unsigned char)*text++) != toupper((unsigned char)*prefix++))
         return 0;
   return 1;
}

static int CueParseMSF(const char *text, u32 *sectors)
{
   unsigned int m, s, f;
   char extra;
   if (sscanf(text, "%u:%u:%u %c", &m, &s, &f, &extra) != 3 || s >= 60 || f >= 75)
      return -1;
   *sectors = MSF_TO_FAD(m, s, f);
   return 0;
}

static int CueError(int line, const char *detail)
{
   char message[512];
   if (line > 0) snprintf(message, sizeof(message), "Malformed CUE at line %d: %s", line, detail);
   else snprintf(message, sizeof(message), "Malformed CUE: %s", detail);
   YabSetError(YAB_ERR_OTHER, message);
   return -1;
}

static void CueNormalizePath(char *path)
{
   while (*path) {
      if (*path == '\\') *path = '/';
      path++;
   }
}

static int CueRelativePath(const char *cue, const char *name, char *out,
                           size_t out_size, int basename_only)
{
   const char *sep = strrchr(cue, '/'), *back = strrchr(cue, '\\');
   const char *part = name, *name_sep;
   size_t dirlen;
   if (!sep || (back && back > sep)) sep = back;
   if (basename_only && (name_sep = strrchr(name, '/'))) part = name_sep + 1;
   dirlen = sep ? (size_t)(sep - cue + 1) : 0;
   if (dirlen + strlen(part) + 1 > out_size) return -1;
   memcpy(out, cue, dirlen);
   strcpy(out + dirlen, part);
   CueNormalizePath(out);
   return 0;
}

static FILE *CueOpenFile(const char *cue, cue_file_struct *file)
{
   char name[CUE_PATH_SIZE];
   int absolute;
   strncpy(name, file->name, sizeof(name) - 1);
   name[sizeof(name) - 1] = '\0';
   CueNormalizePath(name);
   absolute = name[0] == '/' || strchr(name, ':') != NULL;
   if (!absolute && CueRelativePath(cue, name, file->resolved, sizeof(file->resolved), 0) == 0) {
      file->fp = fopen(file->resolved, "rb");
      if (file->fp) return file->fp;
   }
   strncpy(file->resolved, name, sizeof(file->resolved) - 1);
   file->resolved[sizeof(file->resolved) - 1] = '\0';
   file->fp = fopen(file->resolved, "rb");
   if (file->fp) return file->fp;
   if (CueRelativePath(cue, name, file->resolved, sizeof(file->resolved), 1) == 0)
      file->fp = fopen(file->resolved, "rb");
   return file->fp;
}

static void CueCloseFiles(cue_file_struct *files, int count)
{
   int i;
   if (!files) return;
   for (i = 0; i < count; i++)
      if (files[i].fp) { fclose(files[i].fp); files[i].fp = NULL; }
}

static int LoadBinCue(const char *cuefilename, FILE *iso_file)
{
   cue_file_struct *files = NULL;
   cue_track_struct *tracks = NULL;
   char line[CUE_LINE_SIZE];
   int line_no = 0, file_count = 0, track_count = 0;
   int current_file = -1, current_track = -1, i, j, result = -1;
   u32 disc_cursor = 150;

   files = (cue_file_struct *)calloc(CUE_MAX_FILES, sizeof(*files));
   tracks = (cue_track_struct *)calloc(CUE_MAX_TRACKS, sizeof(*tracks));
   if (!files || !tracks) { YabSetError(YAB_ERR_MEMORYALLOC, NULL); goto cleanup; }
   for (i = 0; i < CUE_MAX_FILES; i++) files[i].first_track = -1;

   fseek(iso_file, 0, SEEK_SET);
   while (fgets(line, sizeof(line), iso_file)) {
      char keyword[32], *text;
      line_no++;
      text = CueTrim(line);
      if (line_no == 1 && (unsigned char)text[0] == 0xEF &&
          (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) text += 3;
      if (!*text || sscanf(text, "%31s", keyword) != 1) continue;

      if (stricmp(keyword, "FILE") == 0) {
         char *q1 = strchr(text, '"'), *q2, type[32];
         size_t len;
         if (!q1 || !(q2 = strchr(q1 + 1, '"'))) { CueError(line_no, "FILE requires a quoted filename"); goto cleanup; }
         len = (size_t)(q2 - q1 - 1);
         if (!len || len >= CUE_PATH_SIZE) { CueError(line_no, "FILE filename is empty or too long"); goto cleanup; }
         if (sscanf(q2 + 1, "%31s", type) != 1) { CueError(line_no, "FILE type is missing"); goto cleanup; }
         if (stricmp(type, "BINARY") != 0) { CueError(line_no, "only BINARY track files are supported"); goto cleanup; }
         if (file_count >= CUE_MAX_FILES) { CueError(line_no, "too many FILE entries"); goto cleanup; }
         current_file = file_count++;
         memcpy(files[current_file].name, q1 + 1, len);
         files[current_file].name[len] = '\0';
         current_track = -1;
      } else if (stricmp(keyword, "TRACK") == 0) {
         unsigned int number, sector_size;
         char mode[32], *slash;
         if (current_file < 0) { CueError(line_no, "TRACK appears before FILE"); goto cleanup; }
         if (sscanf(text + strlen(keyword), "%u %31s", &number, mode) != 2) { CueError(line_no, "invalid TRACK declaration"); goto cleanup; }
         if (track_count >= CUE_MAX_TRACKS || number != (unsigned int)(track_count + 1)) { CueError(line_no, "tracks must be numbered consecutively from 01"); goto cleanup; }
         current_track = track_count++;
         tracks[current_track].number = number;
         tracks[current_track].file_index = current_file;
         if (stricmp(mode, "AUDIO") == 0) {
            tracks[current_track].info.sector_size = 2352;
            tracks[current_track].info.ctl_addr = 0x01;
         } else if (CuePrefix(mode, "MODE1/") || CuePrefix(mode, "MODE2/")) {
            slash = strchr(mode, '/');
            sector_size = slash ? (unsigned int)atoi(slash + 1) : 0;
            if (sector_size != 2048 && sector_size != 2336 && sector_size != 2352) { CueError(line_no, "unsupported data-track sector size"); goto cleanup; }
            tracks[current_track].info.sector_size = sector_size;
            tracks[current_track].info.ctl_addr = 0x41;
         } else { CueError(line_no, "unsupported TRACK mode"); goto cleanup; }
         if (files[current_file].first_track < 0) files[current_file].first_track = current_track;
         files[current_file].track_count++;
      } else if (stricmp(keyword, "INDEX") == 0) {
         unsigned int number;
         char timestamp[32];
         u32 sectors;
         if (current_track < 0) { CueError(line_no, "INDEX appears before TRACK"); goto cleanup; }
         if (sscanf(text + strlen(keyword), "%u %31s", &number, timestamp) != 2 ||
             CueParseMSF(timestamp, &sectors) != 0) { CueError(line_no, "invalid INDEX timestamp"); goto cleanup; }
         if (number == 0) { tracks[current_track].index0 = sectors; tracks[current_track].has_index0 = 1; }
         else if (number == 1) { tracks[current_track].index1 = sectors; tracks[current_track].has_index1 = 1; }
      } else if (stricmp(keyword, "PREGAP") == 0 || stricmp(keyword, "POSTGAP") == 0) {
         char timestamp[32];
         u32 sectors;
         if (current_track < 0) { CueError(line_no, "gap appears before TRACK"); goto cleanup; }
         if (sscanf(text + strlen(keyword), "%31s", timestamp) != 1 ||
             CueParseMSF(timestamp, &sectors) != 0) { CueError(line_no, "invalid gap timestamp"); goto cleanup; }
         if (stricmp(keyword, "PREGAP") == 0) tracks[current_track].pregap = sectors;
         else tracks[current_track].postgap = sectors;
      }
   }

   if (ferror(iso_file)) { YabSetError(YAB_ERR_FILEREAD, cuefilename); goto cleanup; }
   if (!file_count || !track_count) { CueError(0, "no binary files or tracks were found"); goto cleanup; }
   for (i = 0; i < track_count; i++) {
      if (!tracks[i].has_index1) { CueError(0, "every track must contain INDEX 01"); goto cleanup; }
      if (tracks[i].has_index0 && tracks[i].index0 > tracks[i].index1) { CueError(0, "INDEX 00 must not follow INDEX 01"); goto cleanup; }
   }

   for (i = 0; i < file_count; i++) {
      u32 sector_size, gaps = 0, previous_index = 0;
      if (files[i].first_track < 0) { CueError(0, "a FILE entry contains no tracks"); goto cleanup; }
      if (!CueOpenFile(cuefilename, &files[i])) { YabSetError(YAB_ERR_FILENOTFOUND, files[i].resolved[0] ? files[i].resolved : files[i].name); goto cleanup; }
      if (fseek(files[i].fp, 0, SEEK_END) != 0 || (files[i].size = ftell(files[i].fp)) <= 0) { YabSetError(YAB_ERR_FILEREAD, files[i].resolved); goto cleanup; }
      fseek(files[i].fp, 0, SEEK_SET);
      sector_size = tracks[files[i].first_track].info.sector_size;
      for (j = files[i].first_track; j < files[i].first_track + files[i].track_count; j++) {
         if (tracks[j].info.sector_size != sector_size) { CueError(0, "tracks sharing one BINARY file must use one sector size"); goto cleanup; }
         if (j > files[i].first_track && tracks[j].index1 <= previous_index) { CueError(0, "track indexes within a file must increase"); goto cleanup; }
         previous_index = tracks[j].index1;
      }
      if (files[i].size % sector_size) { CueError(0, "BIN size is not aligned to its sector size"); goto cleanup; }
      files[i].sectors = (u32)(files[i].size / sector_size);
      CDLOG("CUE file %d \"%s\" size=%ld sectors=%u", i + 1,
            files[i].resolved, files[i].size, files[i].sectors);
      for (j = files[i].first_track; j < files[i].first_track + files[i].track_count; j++) {
         if (tracks[j].index1 >= files[i].sectors) { CueError(0, "track index lies beyond its BIN file"); goto cleanup; }
         {
            u32 content_base = disc_cursor + gaps + tracks[j].pregap;
            u32 first_index = tracks[j].has_index0 ? tracks[j].index0 : tracks[j].index1;
            tracks[j].info.fad_start = content_base + first_index;
            tracks[j].info.toc_fad_start = content_base + tracks[j].index1;
            tracks[j].info.file_offset = first_index * tracks[j].info.sector_size;
         }
         tracks[j].info.fp = files[i].fp;
         tracks[j].info.file_size = (int)files[i].size;
         tracks[j].info.file_id = i + 1;
         gaps += tracks[j].pregap + tracks[j].postgap;
      }
      disc_cursor += files[i].sectors + gaps;
   }

   for (i = 0; i < track_count - 1; i++) {
      if (tracks[i + 1].info.fad_start <= tracks[i].info.fad_start) { CueError(0, "calculated track addresses are not increasing"); goto cleanup; }
      tracks[i].info.fad_end = tracks[i + 1].info.fad_start - 1;
   }
   tracks[track_count - 1].info.fad_end = disc_cursor;

   disc.session_num = 1;
   disc.session = (session_info_struct *)calloc(1, sizeof(session_info_struct));
   if (!disc.session) { YabSetError(YAB_ERR_MEMORYALLOC, NULL); goto cleanup; }
   disc.session[0].track = (track_info_struct *)malloc(sizeof(track_info_struct) * track_count);
   if (!disc.session[0].track) { YabSetError(YAB_ERR_MEMORYALLOC, NULL); free(disc.session); disc.session = NULL; goto cleanup; }
   disc.session[0].fad_start = 150;
   disc.session[0].fad_end = tracks[track_count - 1].info.fad_end;
   disc.session[0].track_num = track_count;
   for (i = 0; i < track_count; i++) {
      disc.session[0].track[i] = tracks[i].info;
      CDLOG("CUE track %02u file=%d sector=%u FAD=%u-%u offset=%u",
            tracks[i].number, tracks[i].file_index + 1, tracks[i].info.sector_size,
            tracks[i].info.fad_start, tracks[i].info.fad_end, tracks[i].info.file_offset);
   }
   CDLOG("CUE loaded files=%d tracks=%d leadout=%u", file_count, track_count, disc.session[0].fad_end);
   fclose(iso_file);
   result = 0;

cleanup:
   if (result != 0) CueCloseFiles(files, file_count);
   free(tracks);
   free(files);
   return result;
}

//////////////////////////////////////////////////////////////////////////////

int LoadMDSTracks(const char *mds_filename, FILE *iso_file, mds_session_struct *mds_session, session_info_struct *session)
{
   int i;
   int track_num=0;
   u32 fad_end = 0;

   session->track = malloc(sizeof(track_info_struct) * mds_session->last_track);
   if (session->track == NULL)
   {
      YabSetError(YAB_ERR_MEMORYALLOC, NULL);
      return -1;
   }
	memset(session->track, 0, sizeof(track_info_struct) * mds_session->last_track);

   for (i = 0; i < mds_session->total_blocks; i++)
   {
      mds_track_struct track;
      FILE *fp=NULL;
      int file_size = 0;

      fseek(iso_file, mds_session->track_blocks_offset + i * sizeof(mds_track_struct), SEEK_SET);
      if (fread(&track, 1, sizeof(mds_track_struct), iso_file) != sizeof(mds_track_struct))
      {
         YabSetError(YAB_ERR_FILEREAD, mds_filename);
         free(session->track);
         return -1;
      }

      if (track.track_num == 0xA2)
         fad_end = MSF_TO_FAD(track.m, track.s, track.f);
      if (!track.extra_offset)
         continue;

      if (track.footer_offset)
      {
         mds_footer_struct footer;
         int found_dupe=0;
         int j;

         // Make sure we haven't already opened file already
         for (j = 0; j < track_num; j++)
         {
            if (track.footer_offset == session->track[j].file_id)
            {
               found_dupe = 1;
               break;
            }
         }

         if (found_dupe)
         {
            fp = session->track[j].fp;
            file_size = session->track[j].file_size;
         }
         else
         {
            fseek(iso_file, track.footer_offset, SEEK_SET);
            if (fread(&footer, 1, sizeof(mds_footer_struct), iso_file) != sizeof(mds_footer_struct))
            {
               YabSetError(YAB_ERR_FILEREAD, mds_filename);
               free(session->track);
               return -1;
            }

            fseek(iso_file, footer.filename_offset, SEEK_SET);
            if (footer.is_widechar)
            {
               wchar_t filename[512];
               wchar_t img_filename[512];
               memset(img_filename, 0, 512 * sizeof(wchar_t));

               if (fwscanf(iso_file, L"%512c", img_filename) != 1)
               {
                  YabSetError(YAB_ERR_FILEREAD, mds_filename);
                  free(session->track);
                  return -1;
               }

               if (wcsncmp(img_filename, L"*.", 2) == 0)
               {
                  wchar_t *ext;
                  swprintf(filename, sizeof(filename)/sizeof(wchar_t), L"%S", mds_filename);
                  ext = wcsrchr(filename, '.');
                  wcscpy(ext, img_filename+1);
               }
               else
                  wcscpy(filename, img_filename);

               fp = _wfopen(filename, L"rb");
            }
            else
            {
               char filename[512];
               char img_filename[512];
               memset(img_filename, 0, 512);

               if (fscanf(iso_file, "%512c", img_filename) != 1)
               {
                  YabSetError(YAB_ERR_FILEREAD, mds_filename);
                  free(session->track);
                  return -1;
               }

               if (strncmp(img_filename, "*.", 2) == 0)
               {
                  char *ext;
                  size_t mds_filename_len = strlen(mds_filename);
                  if (mds_filename_len >= 512)
                  {
                     YabSetError(YAB_ERR_FILEREAD, mds_filename);
                     free(session->track);
                     return -1;
                  }
                  strcpy(filename, mds_filename);
                  ext = strrchr(filename, '.');
                  strcpy(ext, img_filename+1);
               }
               else
                  strcpy(filename, img_filename);

               fp = fopen(filename, "rb");
            }

            if (fp == NULL)
            {
               YabSetError(YAB_ERR_FILEREAD, mds_filename);
               free(session->track);
               return -1;
            }

            fseek(fp, 0, SEEK_END);
            file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
         }
      }

      session->track[track_num].ctl_addr = (((track.addr_ctl << 4) | (track.addr_ctl >> 4)) & 0xFF);
      session->track[track_num].fad_start = track.start_sector+150;
      if (track_num > 0)
         session->track[track_num-1].fad_end = session->track[track_num].fad_start;
      session->track[track_num].file_offset = track.start_offset;
      session->track[track_num].sector_size = track.sector_size;
      session->track[track_num].fp = fp;
      session->track[track_num].file_size = file_size;
      session->track[track_num].file_id = track.footer_offset;
      session->track[track_num].interleaved_sub = track.subchannel_mode != 0 ? 1 : 0;

      track_num++;
   }

   session->track[track_num-1].fad_end = fad_end;
   session->fad_start = session->track[0].fad_start;
   session->fad_end = fad_end;
   session->track_num = track_num;
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static int LoadMDS(const char *mds_filename, FILE *iso_file)
{
   s32 i;
   mds_header_struct header;

   fseek(iso_file, 0, SEEK_SET);

   if (fread((void *)&header, 1, sizeof(mds_header_struct), iso_file) != sizeof(mds_header_struct))
   {
      YabSetError(YAB_ERR_FILEREAD, mds_filename);
      return -1;
   }
   else if (memcmp(&header.signature,  "MEDIA DESCRIPTOR", sizeof(header.signature)))
   {
      YabSetError(YAB_ERR_OTHER, "Bad MDS header");
      return -1;
   }
   else if (header.version[0] > 1)
   {
      YabSetError(YAB_ERR_OTHER, "Unsupported MDS version");
      return -1;
   }

   if (header.medium_type & 0x10)
   {
      // DVD's aren't supported, not will they ever be
      YabSetError(YAB_ERR_OTHER, "DVD's aren't supported");
      return -1;
   }

   disc.session_num = header.session_count;
   disc.session = malloc(sizeof(session_info_struct) * disc.session_num);
   if (disc.session == NULL)
   {
      YabSetError(YAB_ERR_MEMORYALLOC, NULL);
      return -1;
   }

   for (i = 0; i < header.session_count; i++)
   {
      mds_session_struct session;

      fseek(iso_file, header.sessions_blocks_offset + i * sizeof(mds_session_struct), SEEK_SET);
      if (fread(&session, 1, sizeof(mds_session_struct), iso_file) != sizeof(mds_session_struct))
      {
         free(disc.session);
         YabSetError(YAB_ERR_FILEREAD, mds_filename);
         return -1;
      }

      if (LoadMDSTracks(mds_filename, iso_file, &session, &disc.session[i]) != 0)
         return -1;
   }

   fclose(iso_file);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static int LoadISO(FILE *iso_file)
{
   track_info_struct *track;

   disc.session_num = 1;
   disc.session = malloc(sizeof(session_info_struct) * disc.session_num);
   if (disc.session == NULL)
   {
      YabSetError(YAB_ERR_MEMORYALLOC, NULL);
      return -1;
   }

   disc.session[0].fad_start = 150;
   disc.session[0].track_num = 1;
   disc.session[0].track = malloc(sizeof(track_info_struct) * disc.session[0].track_num);
   if (disc.session[0].track == NULL)
   {
      YabSetError(YAB_ERR_MEMORYALLOC, NULL);
      free(disc.session);
      disc.session = NULL;
      return -1;
   }

	memset(disc.session[0].track, 0, sizeof(track_info_struct) * disc.session[0].track_num);

   track = disc.session[0].track;
   track->ctl_addr = 0x41;
   track->fad_start = 150;
   track->file_offset = 0;
   track->fp = iso_file;
   fseek(iso_file, 0, SEEK_END);
   track->file_size = ftell(iso_file);
   track->file_id = 0;

   if (0 == (track->file_size % 2048))
      track->sector_size = 2048;
   else if (0 == (track->file_size % 2352))
      track->sector_size = 2352;
   else
   {
      YabSetError(YAB_ERR_OTHER, "Unsupported CD image!\n");
      return -1;
   }

   disc.session[0].fad_end = track->fad_end = disc.session[0].fad_start + (track->file_size / track->sector_size);

   return 0;
}

//////////////////////////////////////////////////////////////////////////////

char* StripPreSuffixWhitespace(char* string)
{
	char* p;
	for (;;)
	{
		if (string[0] == 0 || !isspace(string[0]))
			break;
		string++;
	}

	if (strlen(string) == 0)
		return string;

	p = string+strlen(string)-1;
	for (;;)
	{
		if (p <= string || !isspace(p[0]))
		{
			p[1] = '\0';
			break;
		}
		p--;
	}

	return string;
}

//////////////////////////////////////////////////////////////////////////////

int LoadParseCCD(FILE *ccd_fp, ccd_struct *ccd)
{
	char text[60], section[CCD_MAX_SECTION], old_name[CCD_MAX_NAME] = "";
	char * start, *end, *name, *value;
	int lineno = 0, error = 0, max_size = 100;

	ccd->dict = (ccd_dict_struct *)malloc(sizeof(ccd_dict_struct)*max_size);
	if (ccd->dict == NULL) 
		return -1;

	ccd->num_dict = 0;

	// Read CCD file
	while (fgets(text, sizeof(text), ccd_fp) != NULL) 
	{
		lineno++;

		start = StripPreSuffixWhitespace(text);

		if (start[0] == '[') 
		{
			// Section
			end = strchr(start+1, ']');
			if (end == NULL) 
			{
				// ] missing from section
				error = lineno;
			}
			else
			{
				end[0] = '\0';
				memset(section, 0, sizeof(section));
				strncpy(section, start + 1, sizeof(section));
				old_name[0] = '\0';
			}
		}
		else if (start[0]) 
		{
			// Name/Value pair
			end = strchr(start, '=');
			if (end) 
			{
				end[0] = '\0';
				name = StripPreSuffixWhitespace(start);
				value = StripPreSuffixWhitespace(end + 1);

				memset(old_name, 0, sizeof(old_name));
				strncpy(old_name, name, sizeof(old_name));
				if (ccd->num_dict+1 > max_size)
				{
					max_size *= 2;
					ccd->dict = realloc(ccd->dict, sizeof(ccd_dict_struct)*max_size);
					if (ccd->dict == NULL)
					{
						free(ccd->dict);
						return -2;
					}
				}
				strcpy(ccd->dict[ccd->num_dict].section, section);
				strcpy(ccd->dict[ccd->num_dict].name, name);
				strcpy(ccd->dict[ccd->num_dict].value, value);
				ccd->num_dict++;
			}
			else
				error = lineno;
		}

		if (error)
			break;
	}

	if (error)
	{
		free(ccd->dict);
		ccd->num_dict = 0;
	}

	return error;
}

//////////////////////////////////////////////////////////////////////////////

static int GetIntCCD(ccd_struct *ccd, char *section, char *name)
{
	int i;
	for (i = 0; i < ccd->num_dict; i++)
	{
		if (stricmp(ccd->dict[i].section, section) == 0 &&
			 stricmp(ccd->dict[i].name, name) == 0)
			return strtol(ccd->dict[i].value, NULL, 0);
	}

	return -1;
}

//////////////////////////////////////////////////////////////////////////////

static int LoadCCD(const char *ccd_filename, FILE *iso_file)
{
	int i;
	ccd_struct ccd;
	int num_toc;
	char img_filename[512];
	char *ext;
	FILE *fp;
   size_t ccd_filename_len = strlen(ccd_filename);

   if (ccd_filename_len >= 512)
   {
      YabSetError(YAB_ERR_FILEREAD, ccd_filename);
      return -1;
   }

	strcpy(img_filename, ccd_filename);
	ext = strrchr(img_filename, '.');
	strcpy(ext, ".img");
	fp = fopen(img_filename, "rb");

	if (fp == NULL)
	{
		YabSetError(YAB_ERR_FILEREAD, img_filename);
		return -1;
	}

	fseek(iso_file, 0, SEEK_SET);

	// Load CCD file as dictionary
	if (LoadParseCCD(iso_file, &ccd))
	{
		fclose(fp);
		YabSetError(YAB_ERR_FILEREAD, ccd_filename);
		return -1;
	}

	num_toc = GetIntCCD(&ccd, "DISC", "TocEntries");
	disc.session_num = GetIntCCD(&ccd, "DISC", "Sessions");
	if (disc.session_num != 1)
	{
		fclose(fp);
		YabSetError(YAB_ERR_OTHER, "Sessions more than 1 are unsupported");
		return -1;
	}

	disc.session = malloc(sizeof(session_info_struct) * disc.session_num);
	if (disc.session == NULL)
	{
		fclose(fp);
		free(ccd.dict);
		YabSetError(YAB_ERR_MEMORYALLOC, NULL);
		return -1;
	}

	if (GetIntCCD(&ccd, "DISC", "DataTracksScrambled"))
	{
		fclose(fp);
		free(ccd.dict);
		free(disc.session);
		YabSetError(YAB_ERR_OTHER, "CCD Scrambled Tracks not supported");
		return -1;
	}

	isoTOCnum = num_toc;

	// Find track number and allocate
	for (i = 0; i < num_toc; i++)
	{
		char sect_name[64];
		int point;

		sprintf(sect_name, "Entry %d", i);

		isoTOC10[i].ctrladr = (GetIntCCD(&ccd, sect_name, "Control") << 4) | GetIntCCD(&ccd, sect_name, "ADR");
		isoTOC10[i].tno = GetIntCCD(&ccd, sect_name, "TrackNo");
		isoTOC10[i].point = GetIntCCD(&ccd, sect_name, "Point");
		isoTOC10[i].min = GetIntCCD(&ccd, sect_name, "AMin");
		isoTOC10[i].sec = 2;
		isoTOC10[i].frame = 0;
		isoTOC10[i].zero = GetIntCCD(&ccd, sect_name, "Zero");
		isoTOC10[i].pmin = GetIntCCD(&ccd, sect_name, "PMin");
		isoTOC10[i].psec = GetIntCCD(&ccd, sect_name, "PSec");
		isoTOC10[i].pframe = GetIntCCD(&ccd, sect_name, "PFrame");

		point = GetIntCCD(&ccd, sect_name, "Point");

		if (point == 0xA1)
		{
			int ses = GetIntCCD(&ccd, sect_name, "Session");

			disc.session[ses-1].fad_start = 150;
			disc.session[ses-1].track_num=GetIntCCD(&ccd, sect_name, "PMin");;
			disc.session[ses-1].track = (track_info_struct *)malloc(disc.session[ses-1].track_num * sizeof(track_info_struct));
			if (disc.session[ses-1].track == NULL)
			{
				fclose(fp);
				free(ccd.dict);
				free(disc.session);
				YabSetError(YAB_ERR_MEMORYALLOC, NULL);
				return -1;
			}
			memset(disc.session[ses-1].track, 0, disc.session[ses-1].track_num * sizeof(track_info_struct));
		}
	}

	// Load TOC
	for (i = 0; i < num_toc; i++)
	{
		char sect_name[64];
		int ses, point, adr, control, trackno, amin, asec, aframe;
		int alba, zero, pmin, psec, pframe, plba;

		sprintf(sect_name, "Entry %d", i);

		ses = GetIntCCD(&ccd, sect_name, "Session");
		point = GetIntCCD(&ccd, sect_name, "Point");
		adr = GetIntCCD(&ccd, sect_name, "ADR");
		control = GetIntCCD(&ccd, sect_name, "Control");
		trackno = GetIntCCD(&ccd, sect_name, "TrackNo");
		amin = GetIntCCD(&ccd, sect_name, "AMin");
		asec = GetIntCCD(&ccd, sect_name, "ASec");
		aframe = GetIntCCD(&ccd, sect_name, "AFrame");
		alba = GetIntCCD(&ccd, sect_name, "ALBA");
		zero = GetIntCCD(&ccd, sect_name, "Zero");
		pmin = GetIntCCD(&ccd, sect_name, "PMin");
		psec = GetIntCCD(&ccd, sect_name, "PSec");
		pframe = GetIntCCD(&ccd, sect_name, "PFrame");
		plba = GetIntCCD(&ccd, sect_name, "PLBA");

		if(point >= 1 && point <= 99)
		{
			track_info_struct *track=&disc.session[ses-1].track[point-1];
			track->ctl_addr = (control << 4) | adr;
			track->fad_start = MSF_TO_FAD(pmin, psec, pframe);
			if (point >= 2)
			   disc.session[ses-1].track[point-2].fad_end = track->fad_start-1;
			track->file_offset = plba*2352;
			track->sector_size = 2352;
			track->fp = fp;
			track->file_size = (track->fad_end+1-track->fad_start)*2352;
			track->file_id = 0;
			track->interleaved_sub = 0;
		}
		else if (point == 0xA2)
		{
			disc.session[ses-1].fad_end = MSF_TO_FAD(pmin, psec, pframe);
			disc.session[ses-1].track[disc.session[ses-1].track_num-1].fad_end = disc.session[ses-1].fad_end;
		}
	}

	fclose(iso_file);

	return 0;
}

//////////////////////////////////////////////////////////////////////////////

void BuildTOC()
{
   int i;
   session_info_struct *session=&disc.session[0];

   for (i = 0; i < session->track_num; i++)
   {
      track_info_struct *track=&disc.session[0].track[i];
      isoTOC[i] = (track->ctl_addr << 24) |
                  (track->toc_fad_start ? track->toc_fad_start : track->fad_start);
   }

   isoTOC[99] = (isoTOC[0] & 0xFF000000) | 0x010000;
   isoTOC[100] = (isoTOC[session->track_num - 1] & 0xFF000000) | (session->track_num << 16);
   isoTOC[101] = (isoTOC[session->track_num - 1] & 0xFF000000) | session->fad_end;
}

//////////////////////////////////////////////////////////////////////////////

void BuildTOC10()
{
   int i;
   session_info_struct *session=&disc.session[0];

   for (i = 0; i < session->track_num; i++)
   {
      isoTOC10[3+i].ctrladr = session->track[i].ctl_addr;
      isoTOC10[3+i].tno = 0;
      isoTOC10[3+i].point = i+1;
      isoTOC10[3+i].min = 0;
      isoTOC10[3+i].sec = 2;
      isoTOC10[3+i].frame = 0;
      isoTOC10[3+i].zero = 0;
      Cs2FADToMSF(session->track[i].toc_fad_start ? session->track[i].toc_fad_start : session->track[i].fad_start,
                  &isoTOC10[3+i].pmin, &isoTOC10[3+i].psec, &isoTOC10[3+i].pframe);
   }

   isoTOC10[0].ctrladr = isoTOC10[3].ctrladr;
   isoTOC10[0].tno = 0;
   isoTOC10[0].point = 0xA0;
   isoTOC10[0].min = 0;
   isoTOC10[0].sec = 2;
   isoTOC10[0].frame = 0;
   isoTOC10[0].zero = 0;
   isoTOC10[0].pmin = 1;
   isoTOC10[0].psec = 0;
   isoTOC10[0].pframe = 0;

   isoTOC10[1].ctrladr = isoTOC10[3+session->track_num-1].ctrladr;
   isoTOC10[1].tno = 0;
   isoTOC10[1].point = 0xA1;
   isoTOC10[1].min = 0;
   isoTOC10[1].sec = 2;
   isoTOC10[1].frame = 0;
   isoTOC10[1].zero = 0;
   isoTOC10[1].pmin = session->track_num;
   isoTOC10[1].psec = 0;
   isoTOC10[1].pframe = 0;

   isoTOC10[2].ctrladr = isoTOC10[1].ctrladr;
   isoTOC10[2].tno = 0;
   isoTOC10[2].point = 0xA2;
   isoTOC10[2].min = 0;
   isoTOC10[2].sec = 2;
   isoTOC10[2].frame = 0;
   isoTOC10[2].zero = 0;
   Cs2FADToMSF(session->fad_end, &isoTOC10[2].pmin, &isoTOC10[2].psec, &isoTOC10[2].pframe);
   isoTOCnum = 3+session->track_num;
}

//////////////////////////////////////////////////////////////////////////////

static int ISOCDInit(const char * iso) {
   char header[6];
   char *ext;
   int ret;
   FILE *iso_file;
   size_t num_read = 0;

   memset(isoTOC, 0xFF, 0xCC * 2);
   memset(&disc, 0, sizeof(disc));

   if (!iso)
      return -1;

   if (!(iso_file = fopen(iso, "rb")))
   {
      YabSetError(YAB_ERR_FILENOTFOUND, (char *)iso);
      return -1;
   }

   num_read = fread((void *)header, 1, 6, iso_file);
   ext = strrchr(iso, '.');

   // Figure out what kind of image format we're dealing with
   if (ext && stricmp(ext, ".CUE") == 0)
   {
      // It's a BIN/CUE
      imgtype = IMG_BINCUE;
      ret = LoadBinCue(iso, iso_file);
   }
   else if (stricmp(ext, ".MDS") == 0 && strncmp(header, "MEDIA ", sizeof(header)) == 0)
   {
      // It's a MDS
      imgtype = IMG_MDS;
      ret = LoadMDS(iso, iso_file);
   }
	else if (stricmp(ext, ".CCD") == 0)
	{
		// It's a CCD
		imgtype = IMG_CCD;
		ret = LoadCCD(iso, iso_file);
	}
   else
   {
      // Assume it's an ISO file
      imgtype = IMG_ISO;
      ret = LoadISO(iso_file);
   }

   if (ret != 0)
   {
      imgtype = IMG_NONE;

      if (iso_file)
         fclose(iso_file);
      iso_file = NULL;
      return -1;
   }   

   BuildTOC();
   if (imgtype != IMG_CCD)
      BuildTOC10();
   return 0;
}

//////////////////////////////////////////////////////////////////////////////

static void ISOCDDeInit(void) {
   int i, j, k;
   if (disc.session)
   {
      for (i = 0; i < disc.session_num; i++)
      {
         if (disc.session[i].track)
         {
            for (j = 0; j < disc.session[i].track_num; j++)
            {
               if (disc.session[i].track[j].fp)
               {
                  fclose(disc.session[i].track[j].fp);

                  // Make sure we don't close the same file twice
                  for (k = j+1; k < disc.session[i].track_num; k++)
                  {
                     if (disc.session[i].track[j].file_id == disc.session[i].track[k].file_id)
                        disc.session[i].track[k].fp = NULL;
                  }
               }
            }
            free(disc.session[i].track);
         }
      }
      free(disc.session);
   }
}

//////////////////////////////////////////////////////////////////////////////

static int ISOCDGetStatus(void) {
   return disc.session_num > 0 ? 0 : 2;
}

//////////////////////////////////////////////////////////////////////////////

static s32 ISOCDReadTOC(u32 * TOC) {
   memcpy(TOC, isoTOC, 0xCC * 2);

   return (0xCC * 2);
}

//////////////////////////////////////////////////////////////////////////////

static s32 ISOCDReadTOC10(CDInterfaceToc10 *TOC) {
   memcpy(TOC, isoTOC10, 102 * sizeof(CDInterfaceToc10));
   return isoTOCnum;
}

//////////////////////////////////////////////////////////////////////////////

static int ISOCDReadSectorFAD(u32 FAD, void *buffer) {
   int i,j;
   size_t num_read = 0;
   track_info_struct *track=NULL;

   assert(disc.session);

   memset(buffer, 0, 2448);

   for (i = 0; i < disc.session_num; i++)
   {
      for (j = 0; j < disc.session[i].track_num; j++)
      {
         if (FAD >= disc.session[i].track[j].fad_start &&
             FAD <= disc.session[i].track[j].fad_end)
         {             
            track = &disc.session[i].track[j];
            break;
         }
      }
   }

   if (track == NULL)
   {
      CDLOG("Warning: Sector not found in track list");
      return 0;
   }

   fseek(track->fp, track->file_offset + (FAD-track->fad_start) * track->sector_size, SEEK_SET);
	if (track->sub_fp)
		fseek(track->sub_fp, track->file_offset + (FAD-track->fad_start) * 96, SEEK_SET);
   if (track->sector_size == 2448)
   {
      if (!track->interleaved_sub)
		{
			if (track->sub_fp)
			{
            num_read = fread(buffer, 2352, 1, track->fp);
            num_read = fread((char *)buffer + 2352, 96, 1, track->sub_fp);
			}
			else
            num_read = fread(buffer, 2448, 1, track->fp);
		}
      else
      {
         const u16 deint_offsets[] = {
            0, 66, 125, 191, 100, 50, 150, 175, 8, 33, 58, 83, 
            108, 133, 158, 183, 16, 41, 25, 91, 116, 141, 166, 75, 
            24, 90, 149, 215, 124, 74, 174, 199, 32, 57, 82, 107, 
            132, 157, 182, 207, 40, 65, 49, 115, 140, 165, 190, 99, 
            48, 114, 173, 239, 148, 98, 198, 223, 56, 81, 106, 131, 
            156, 181, 206, 231, 64, 89, 73, 139, 164, 189, 214, 123, 
            72, 138, 197, 263, 172, 122, 222, 247, 80, 105, 130, 155, 
            180, 205, 230, 255, 88, 113, 97, 163, 188, 213, 238, 147
         };
         u8 subcode_buffer[96 * 3];

         num_read = fread(buffer, 2352, 1, track->fp);

         num_read = fread(subcode_buffer, 96, 1, track->fp);
         fseek(track->fp, 2352, SEEK_CUR);
         num_read = fread(subcode_buffer + 96, 96, 1, track->fp);
         fseek(track->fp, 2352, SEEK_CUR);
         num_read = fread(subcode_buffer + 192, 96, 1, track->fp);
         for (i = 0; i < 96; i++)
            ((u8 *)buffer)[2352+i] = subcode_buffer[deint_offsets[i]];
      }
   }
   else if (track->sector_size == 2352)
   {
      // Generate subcodes here
      num_read = fread(buffer, 2352, 1, track->fp);
   }
   else if (track->sector_size == 2048)
   {
      memcpy(buffer, syncHdr, 12);
      num_read = fread((char *)buffer + 0x10, 2048, 1, track->fp);
   }
   else if (track->sector_size == 2336)
   {
      memcpy(buffer, syncHdr, 12);
      num_read = fread((char *)buffer + 0x10, 2336, 1, track->fp);
   }
	return 1;
}

//////////////////////////////////////////////////////////////////////////////

static void ISOCDReadAheadFAD(UNUSED u32 FAD)
{
	// No-op
}

//////////////////////////////////////////////////////////////////////////////
