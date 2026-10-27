/** @file
  UFS Log export tool: choose a source, then render it as OneSend QR codes.

  Volume keys pick UEFI Log, Userdata Boot Log, Data Crash Log or Clear
  Userdata Log. The first short power press arms the selected source; the
  second one converts it to the AdvancedLogViewLib QR stream. UEFI Log and
  Userdata Boot Log come from the UFS tail windows owned by FuxiUefiLogDxe and
  the XNU BootLog writer. Data Crash Log reads the newest CrashReporter .ips
  snapshot that the XNU BootLog writer copied into the SHCRASH01 area at the
  tail of its export region. Clear Userdata Log invalidates the SHLOG001
  header block so the next read reports "no log".

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/SimpleTextIn.h>
#include <Protocol/UniXNUMenuPlugin.h>
#include <Library/AdvancedLogViewLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define FUXI_UFS_LOG_BLOCK_SIZE   4096u
#define FUXI_UFS_LOG_BLOCKS       8192u
#define FUXI_UFS_LOG_BYTES        ((UINT64)FUXI_UFS_LOG_BLOCK_SIZE * FUXI_UFS_LOG_BLOCKS)
#define FUXI_UFS_READ_CHUNK       (1024u * 1024u)
#define FUXI_UFS_NO_LOG_STALL_MS  3000u

#define FUXI_UFS_CRASH_BLOCKS     4096u
#define FUXI_UFS_CRASH_MAX_BYTES  (4u * 1024u * 1024u)
#define FUXI_UFS_CRASH_MAGIC      "SHCRASH01"
#define FUXI_UFS_HEADER_V3_BYTES  140u

#define FUXI_UEFI_LOG_BLOCK_SIZE  4096u
#define FUXI_UEFI_LOG_BLOCKS      64u
#define FUXI_UEFI_LOG_BYTES       ((UINT64)FUXI_UEFI_LOG_BLOCK_SIZE * FUXI_UEFI_LOG_BLOCKS)
#define FUXI_UEFI_LOG_MAGIC       "SHUEFI01"

#pragma pack(1)
typedef struct {
  CHAR8   Magic[8];
  UINT32  Version;
  UINT32  HeaderBytes;
  UINT64  BootId;
  UINT64  FirstLba;
  UINT32  WindowBlocks;
  UINT32  MsgFirst;
  UINT32  MsgBlocks;
  UINT32  MsgBytes;
  UINT32  SlotFirst;
  UINT32  SlotBlocks;
  UINT32  SlotCount;
  UINT32  SlotsWritten;
  UINT32  UptimeS;
  UINT32  Rounds;
  UINT32  WriteErrors;
  UINT32  GptOpen;
  UINT32  GptLast;
  UINT32  State;
  CHAR8   Tag[32];
  UINT32  ExportFirst;
  UINT32  ExportBlocks;
  UINT32  ExportBytes;
  UINT32  ExportGeneration;
  UINT32  ExportBelow;
  UINT32  CrashBlocks;
  UINT32  CrashBytes;
  UINT32  CrashGeneration;
  UINT32  CrashNameLen;
} FUXI_UFS_LOG_HEADER;

typedef struct {
  CHAR8   Magic[8];
  UINT32  Version;
  UINT32  HeaderBytes;
  UINT32  Generation;
  UINT32  NameLen;
  UINT32  FileBytes;
} FUXI_UFS_CRASH_HEADER;

typedef struct {
  CHAR8   Magic[8];
  UINT32  Version;
  UINT32  HeaderBytes;
  UINT32  LogBytes;
  UINT32  Reserved[27];
} FUXI_UEFI_LOG_HEADER;
#pragma pack()

STATIC EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mBackground = { 0x0F, 0x0C, 0x0B, 0 };

typedef enum {
  FuxiLogSourceUefi = 0,
  FuxiLogSourceUserdata,
  FuxiLogSourceDataCrash,
  FuxiLogSourceClearUserdata,
  FuxiLogSourceCount,
  FuxiLogSourceBack
} FUXI_LOG_SOURCE;

STATIC CONST CHAR16  *mSourceTitles[FuxiLogSourceCount] = {
  L"UEFI Log",
  L"Userdata Boot Log",
  L"Data Crash Log",
  L"Clear Userdata Log"
};

STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuTextColor         = { 0xF2, 0xF3, 0xF5, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuDimColor          = { 0x7C, 0x83, 0x8D, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuAccentColor       = { 0x2A, 0x5B, 0xD7, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuRowColor          = { 0x17, 0x1A, 0x20, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuRuleColor         = { 0x26, 0x2A, 0x31, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuMarkerColor       = { 0xFF, 0xFF, 0xFF, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuBadgeColor        = { 0x26, 0x2A, 0x31, 0 };
STATIC CONST EFI_GRAPHICS_OUTPUT_BLT_PIXEL  mMenuBadgeOnColor      = { 0x1B, 0x44, 0xA6, 0 };

STATIC
VOID
Fill (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL  *Gop,
  IN UINT32                        X,
  IN UINT32                        Y,
  IN UINT32                        Width,
  IN UINT32                        Height,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color
  )
{
  Gop->Blt (Gop, &Color, EfiBltVideoFill, 0, 0, X, Y, Width, Height, 0);
}

// 8x19 bitmap font for printable ASCII (0x20..0x7E), copied verbatim from
// UniXNU UniXNULib/Font.c (EDK II GraphicsConsoleDxe narrow font) so the
// plugin menu matches the UniXNU main menu glyph for glyph.
#define FUXI_MENU_GLYPH_WIDTH   8u
#define FUXI_MENU_GLYPH_HEIGHT  19u
#define FUXI_MENU_GLYPH_ADVANCE 8u

STATIC CONST UINT8  mCaFontGlyphs[95][FUXI_MENU_GLYPH_HEIGHT] = {
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // ' '
  { 0x00, 0x00, 0x00, 0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 },  // '!'
  { 0x00, 0x00, 0x00, 0x6C, 0x6C, 0x6C, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '"'
  { 0x00, 0x00, 0x00, 0x00, 0x6C, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0x6C, 0xFE, 0x6C, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00 },  // '#'
  { 0x00, 0x00, 0x18, 0x18, 0x7C, 0xC6, 0xC6, 0x60, 0x38, 0x0C, 0x06, 0xC6, 0xC6, 0x7C, 0x18, 0x18, 0x00, 0x00, 0x00 },  // '$'
  { 0x00, 0x00, 0x00, 0xC6, 0xC6, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x60, 0x60, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // '%'
  { 0x00, 0x00, 0x00, 0x78, 0xCC, 0xCC, 0xCC, 0x78, 0x76, 0xDC, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00 },  // '&'
  { 0x00, 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // "'"
  { 0x00, 0x00, 0x00, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00 },  // '('
  { 0x00, 0x00, 0x00, 0xC0, 0x60, 0x60, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x60, 0x60, 0xC0, 0x00, 0x00, 0x00, 0x00 },  // ')'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x6C, 0x38, 0xFE, 0x38, 0x6C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '*'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '+'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00 },  // ','
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '-'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '.'
  { 0x00, 0x00, 0x00, 0x06, 0x06, 0x0C, 0x0C, 0x18, 0x18, 0x30, 0x30, 0x60, 0x60, 0xC0, 0xC0, 0x00, 0x00, 0x00, 0x00 },  // '/'
  { 0x00, 0x00, 0x00, 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x00, 0x00, 0x00, 0x00 },  // '0'
  { 0x00, 0x00, 0x00, 0x18, 0x38, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, 0x00, 0x00, 0x00 },  // '1'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xC2, 0xFE, 0x00, 0x00, 0x00, 0x00 },  // '2'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0x06, 0x06, 0x06, 0x3C, 0x06, 0x06, 0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // '3'
  { 0x00, 0x00, 0x00, 0x1C, 0x1C, 0x3C, 0x3C, 0x6C, 0x6C, 0xCC, 0xFE, 0x0C, 0x0C, 0x0C, 0x1E, 0x00, 0x00, 0x00, 0x00 },  // '4'
  { 0x00, 0x00, 0x00, 0xFE, 0xC0, 0xC0, 0xC0, 0xC0, 0xFC, 0x06, 0x06, 0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // '5'
  { 0x00, 0x00, 0x00, 0x3C, 0x60, 0xC0, 0xC0, 0xC0, 0xFC, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // '6'
  { 0x00, 0x00, 0x00, 0xFE, 0xC6, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 },  // '7'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // '8'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x06, 0x06, 0x06, 0x0C, 0x78, 0x00, 0x00, 0x00, 0x00 },  // '9'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },  // ':'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00 },  // ';'
  { 0x00, 0x00, 0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00 },  // '<'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '='
  { 0x00, 0x00, 0x00, 0x00, 0xC0, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x00, 0x00, 0x00, 0x00 },  // '>'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0x0C, 0x0C, 0x18, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 },  // '?'
  { 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xDE, 0xDE, 0xDE, 0xDC, 0xC0, 0xC0, 0x7E, 0x00, 0x00, 0x00, 0x00 },  // '@'
  { 0x00, 0x00, 0x00, 0x10, 0x38, 0x6C, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // 'A'
  { 0x00, 0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0xFC, 0x00, 0x00, 0x00, 0x00 },  // 'B'
  { 0x00, 0x00, 0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC2, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00 },  // 'C'
  { 0x00, 0x00, 0x00, 0xF8, 0x6C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00, 0x00, 0x00, 0x00 },  // 'D'
  { 0x00, 0x00, 0x00, 0xFE, 0x66, 0x62, 0x60, 0x68, 0x78, 0x68, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00, 0x00, 0x00, 0x00 },  // 'E'
  { 0x00, 0x00, 0x00, 0xFE, 0x66, 0x62, 0x60, 0x64, 0x7C, 0x64, 0x60, 0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00 },  // 'F'
  { 0x00, 0x00, 0x00, 0x3C, 0x66, 0xC2, 0xC0, 0xC0, 0xC0, 0xDE, 0xC6, 0xC6, 0xC6, 0x66, 0x3C, 0x00, 0x00, 0x00, 0x00 },  // 'G'
  { 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xFE, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // 'H'
  { 0x00, 0x00, 0x00, 0xFC, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0xFC, 0x00, 0x00, 0x00, 0x00 },  // 'I'
  { 0x00, 0x00, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00, 0x00, 0x00, 0x00 },  // 'J'
  { 0x00, 0x00, 0x00, 0xE6, 0x66, 0x6C, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x6C, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00 },  // 'K'
  { 0x00, 0x00, 0x00, 0xF0, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00, 0x00, 0x00, 0x00 },  // 'L'
  { 0x00, 0x00, 0x00, 0xC6, 0xEE, 0xEE, 0xFE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // 'M'
  { 0x00, 0x00, 0x00, 0xC6, 0xE6, 0xF6, 0xF6, 0xF6, 0xDE, 0xCE, 0xCE, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // 'N'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'O'
  { 0x00, 0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00 },  // 'P'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0x7C, 0x1C, 0x0E, 0x00, 0x00 },  // 'Q'
  { 0x00, 0x00, 0x00, 0xFC, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x6C, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00 },  // 'R'
  { 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0x60, 0x38, 0x0C, 0x06, 0x06, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'S'
  { 0x00, 0x00, 0x00, 0xFC, 0xFC, 0xB4, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00 },  // 'T'
  { 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'U'
  { 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x6C, 0x38, 0x10, 0x00, 0x00, 0x00, 0x00 },  // 'V'
  { 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0xD6, 0xFE, 0x6C, 0x6C, 0x00, 0x00, 0x00, 0x00 },  // 'W'
  { 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0x6C, 0x6C, 0x38, 0x6C, 0x6C, 0xC6, 0xC6, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // 'X'
  { 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00 },  // 'Y'
  { 0x00, 0x00, 0x00, 0xFE, 0xC6, 0x86, 0x0C, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xC2, 0xC6, 0xFE, 0x00, 0x00, 0x00, 0x00 },  // 'Z'
  { 0x00, 0x00, 0x00, 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00, 0x00, 0x00, 0x00 },  // '['
  { 0x00, 0x00, 0x00, 0xC0, 0xC0, 0x60, 0x60, 0x30, 0x30, 0x18, 0x18, 0x0C, 0x0C, 0x06, 0x06, 0x00, 0x00, 0x00, 0x00 },  // '\\'
  { 0x00, 0x00, 0x00, 0xF0, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0xF0, 0x00, 0x00, 0x00, 0x00 },  // ']'
  { 0x00, 0x00, 0x10, 0x38, 0x6C, 0xC6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '^'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00 },  // '_'
  { 0x00, 0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '`'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x78, 0x0C, 0x0C, 0x7C, 0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00 },  // 'a'
  { 0x00, 0x00, 0x00, 0xE0, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'b'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC0, 0xC0, 0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'c'
  { 0x00, 0x00, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x3C, 0x6C, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x7E, 0x00, 0x00, 0x00, 0x00 },  // 'd'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xFE, 0xC0, 0xC0, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'e'
  { 0x00, 0x00, 0x00, 0x1E, 0x33, 0x30, 0x30, 0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x30, 0x78, 0x00, 0x00, 0x00, 0x00 },  // 'f'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0xCC, 0x78, 0x00 },  // 'g'
  { 0x00, 0x00, 0x00, 0xE0, 0x60, 0x60, 0x60, 0x7C, 0x76, 0x66, 0x66, 0x66, 0x66, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00 },  // 'h'
  { 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00 },  // 'i'
  { 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x0C, 0x00, 0x1C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x6C, 0x38, 0x00 },  // 'j'
  { 0x00, 0x00, 0x00, 0xE0, 0x60, 0x60, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x6C, 0x66, 0xE6, 0x00, 0x00, 0x00, 0x00 },  // 'k'
  { 0x00, 0x00, 0x00, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, 0x00, 0x00, 0x00 },  // 'l'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xEC, 0xEE, 0xFE, 0xD6, 0xD6, 0xD6, 0xD6, 0xD6, 0x00, 0x00, 0x00, 0x00 },  // 'm'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00 },  // 'n'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 'o'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00 },  // 'p'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x76, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x7C, 0x0C, 0x0C, 0x1E, 0x00 },  // 'q'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDC, 0x66, 0x60, 0x60, 0x60, 0x60, 0x60, 0xF0, 0x00, 0x00, 0x00, 0x00 },  // 'r'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7C, 0xC6, 0xC0, 0x7C, 0x06, 0x06, 0xC6, 0x7C, 0x00, 0x00, 0x00, 0x00 },  // 's'
  { 0x00, 0x00, 0x00, 0x10, 0x30, 0x30, 0x30, 0xFC, 0x30, 0x30, 0x30, 0x30, 0x30, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00 },  // 't'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x76, 0x00, 0x00, 0x00, 0x00 },  // 'u'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0x78, 0x30, 0x00, 0x00, 0x00, 0x00 },  // 'v'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xD6, 0xD6, 0xFE, 0xEE, 0x6C, 0x00, 0x00, 0x00, 0x00 },  // 'w'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x6C, 0x38, 0x38, 0x6C, 0x6C, 0xC6, 0xC6, 0x00, 0x00, 0x00, 0x00 },  // 'x'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0xF8, 0x00 },  // 'y'
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x86, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0xFE, 0x00, 0x00, 0x00, 0x00 },  // 'z'
  { 0x00, 0x00, 0x00, 0x0E, 0x18, 0x18, 0x18, 0x18, 0x30, 0x18, 0x18, 0x18, 0x18, 0x18, 0x0E, 0x00, 0x00, 0x00, 0x00 },  // '{'
  { 0x00, 0x00, 0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00 },  // '|'
  { 0x00, 0x00, 0x00, 0xE0, 0x30, 0x30, 0x30, 0x30, 0x18, 0x30, 0x30, 0x30, 0x30, 0x30, 0xE0, 0x00, 0x00, 0x00, 0x00 },  // '}'
  { 0x00, 0x00, 0x00, 0x76, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '~'
};

STATIC
VOID
MenuFillRect (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Buffer,
  IN     UINT32                          Width,
  IN     UINT32                          Height,
  IN     UINT32                          X,
  IN     UINT32                          Y,
  IN     UINT32                          RectWidth,
  IN     UINT32                          RectHeight,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL   Color
  )
{
  UINT32  Row;
  UINT32  Col;
  UINT32  EndX;
  UINT32  EndY;

  if ((Buffer == NULL) || (RectWidth == 0) || (RectHeight == 0)) {
    return;
  }

  EndX = (X > Width - RectWidth) ? Width : X + RectWidth;
  EndY = (Y > Height - RectHeight) ? Height : Y + RectHeight;
  for (Row = Y; Row < EndY; Row++) {
    for (Col = X; Col < EndX; Col++) {
      Buffer[Row * Width + Col] = Color;
    }
  }
}

STATIC
UINT32
MenuTextWidth (
  IN CONST CHAR16  *Text,
  IN UINT32        Scale
  )
{
  return (UINT32)StrLen (Text) * FUXI_MENU_GLYPH_ADVANCE * Scale;
}

STATIC
VOID
MenuDrawText (
  IN OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Buffer,
  IN     UINT32                          Width,
  IN     UINT32                          Height,
  IN     UINT32                          X,
  IN     UINT32                          Y,
  IN     CONST CHAR16                    *Text,
  IN     UINT32                          Scale,
  IN     EFI_GRAPHICS_OUTPUT_BLT_PIXEL   Color
  )
{
  UINTN        Row;
  UINTN        Col;
  CONST UINT8  *Glyph;
  CHAR16       C;

  if ((Buffer == NULL) || (Text == NULL) || (Scale == 0)) {
    return;
  }

  while (*Text != L'\0') {
    C     = *Text;
    Glyph = mCaFontGlyphs[((C >= 0x20) && (C <= 0x7E)) ? (C - 0x20) : ('?' - 0x20)];
    for (Row = 0; Row < FUXI_MENU_GLYPH_HEIGHT; Row++) {
      for (Col = 0; Col < FUXI_MENU_GLYPH_WIDTH; Col++) {
        if ((Glyph[Row] & (0x80 >> Col)) == 0) {
          continue;
        }

        MenuFillRect (
          Buffer,
          Width,
          Height,
          X + (UINT32)Col * Scale,
          Y + (UINT32)Row * Scale,
          Scale,
          Scale,
          Color
          );
      }
    }

    X += FUXI_MENU_GLYPH_ADVANCE * Scale;
    Text++;
  }
}

STATIC
VOID
DrawSelector (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN CONST UNIXNU_PLUGIN_RECT       *Content,
  IN UINTN                          Selected,
  IN BOOLEAN                        Confirmed
  )
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *Buffer;
  UINT32                          Longest;
  UINT32                          Scale;
  UINT32                          GlyphHeight;
  UINT32                          Pad;
  UINT32                          Margin;
  UINT32                          CardWidth;
  UINT32                          CardHeight;
  UINT32                          Gap;
  UINT32                          TitleY;
  UINT32                          RuleY;
  UINT32                          RuleHeight;
  UINT32                          StartY;
  UINT32                          Y;
  UINT32                          Badge;
  UINT32                          BadgeX;
  UINT32                          BadgeY;
  UINT32                          TextX;
  UINT32                          TextY;
  UINT32                          TextWidth;
  UINTN                           Index;
  CHAR16                          Number[2];
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   CardColor;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   BadgeColor;

  if ((Gop == NULL) || (Content == NULL) || (Content->Width == 0) || (Content->Height == 0)) {
    return;
  }

  Buffer = AllocatePool ((UINTN)Content->Width * Content->Height * sizeof (*Buffer));
  if (Buffer == NULL) {
    return;
  }

  MenuFillRect (Buffer, Content->Width, Content->Height, 0, 0, Content->Width, Content->Height, mBackground);

  Longest = 0;
  for (Index = 0; Index < FuxiLogSourceCount; Index++) {
    Longest = MAX (Longest, StrLen (mSourceTitles[Index]));
  }
  Scale      = MIN (5u, MAX (2u, Content->Width / (Longest * FUXI_MENU_GLYPH_ADVANCE + 32u)));
  GlyphHeight = FUXI_MENU_GLYPH_HEIGHT * Scale;
  Pad         = GlyphHeight / 2u;
  Margin      = MAX (Scale * 3u, 16u);
  CardWidth   = (Content->Width > 2u * Margin) ? Content->Width - 2u * Margin : Content->Width;
  CardHeight  = GlyphHeight + 2u * Pad;
  Gap         = Scale * 3u;
  TitleY      = Scale * 2u;
  RuleY       = TitleY + FUXI_MENU_GLYPH_HEIGHT * 2u + Scale;
  RuleHeight  = MAX (1u, Scale / 2u);
  StartY      = RuleY + RuleHeight + Scale * 2u;

  while ((Scale > 2u) &&
         (StartY + FuxiLogSourceCount * CardHeight + (FuxiLogSourceCount - 1u) * Gap > Content->Height))
  {
    Scale--;
    GlyphHeight = FUXI_MENU_GLYPH_HEIGHT * Scale;
    Pad         = GlyphHeight / 2u;
    Margin      = MAX (Scale * 3u, 16u);
    CardWidth   = (Content->Width > 2u * Margin) ? Content->Width - 2u * Margin : Content->Width;
    CardHeight  = GlyphHeight + 2u * Pad;
    Gap         = Scale * 3u;
    TitleY      = Scale * 2u;
    RuleY       = TitleY + FUXI_MENU_GLYPH_HEIGHT * 2u + Scale;
    RuleHeight  = MAX (1u, Scale / 2u);
    StartY      = RuleY + RuleHeight + Scale * 2u;
  }

  MenuDrawText (
    Buffer,
    Content->Width,
    Content->Height,
    (Content->Width - MenuTextWidth (L"Select export source", 2u)) / 2u,
    TitleY,
    L"Select export source",
    2u,
    mMenuDimColor
    );
  MenuFillRect (
    Buffer,
    Content->Width,
    Content->Height,
    Margin,
    RuleY,
    CardWidth,
    RuleHeight,
    mMenuRuleColor
    );

  for (Index = 0; Index < FuxiLogSourceCount; Index++) {
    Y = StartY + (UINT32)Index * (CardHeight + Gap);
    if (Y + CardHeight > Content->Height) {
      break;
    }

    CardColor  = (Index == Selected) ? mMenuAccentColor : mMenuRowColor;
    BadgeColor = (Index == Selected) ? mMenuBadgeOnColor : mMenuBadgeColor;
    MenuFillRect (Buffer, Content->Width, Content->Height, Margin, Y, CardWidth, CardHeight, CardColor);
    if (Index == Selected) {
      MenuFillRect (Buffer, Content->Width, Content->Height, Margin, Y, Scale * 2u, CardHeight, mMenuMarkerColor);
    }

    Badge  = GlyphHeight * 3u / 2u;
    BadgeX = Margin + Pad;
    BadgeY = Y + (CardHeight - Badge) / 2u;
    MenuFillRect (Buffer, Content->Width, Content->Height, BadgeX, BadgeY, Badge, Badge, BadgeColor);

    Number[0] = (CHAR16)(L'0' + Index + 1u);
    Number[1] = L'\0';
    TextWidth = MenuTextWidth (Number, Scale);
    MenuDrawText (
      Buffer,
      Content->Width,
      Content->Height,
      BadgeX + (Badge - TextWidth) / 2u,
      Y + (CardHeight - GlyphHeight) / 2u,
      Number,
      Scale,
      mMenuTextColor
      );

    TextX = BadgeX + Badge + Pad;
    TextY = Y + (CardHeight - GlyphHeight) / 2u;
    MenuDrawText (
      Buffer,
      Content->Width,
      Content->Height,
      TextX,
      TextY,
      mSourceTitles[Index],
      Scale,
      mMenuTextColor
      );
  }

  if (Confirmed) {
    MenuFillRect (Buffer, Content->Width, Content->Height, 0, 0, Content->Width, 2u, mMenuAccentColor);
    MenuFillRect (
      Buffer,
      Content->Width,
      Content->Height,
      0,
      Content->Height - 2u,
      Content->Width,
      2u,
      mMenuAccentColor
      );
  }

  Gop->Blt (
         Gop,
         Buffer,
         EfiBltBufferToVideo,
         0,
         0,
         Content->X,
         Content->Y,
         Content->Width,
         Content->Height,
         0
         );
  FreePool (Buffer);
}

STATIC
FUXI_LOG_SOURCE
SelectExportSource (
  IN UNIXNU_MENU_PLUGIN_PROTOCOL    *This,
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN CONST UNIXNU_PLUGIN_RECT       *Content,
  IN UNIXNU_MENU_PLUGIN_DRAW_STATUS DrawStatus
  )
{
  EFI_STATUS     Status;
  UINTN          Signaled;
  EFI_INPUT_KEY  Key;
  UINTN          Selected;
  BOOLEAN        Confirmed;
  CHAR16         StatusLine[96];

  Selected  = FuxiLogSourceUefi;
  Confirmed = FALSE;
  DrawSelector (Gop, Content, Selected, Confirmed);
  if (DrawStatus != NULL) {
    DrawStatus (This, 0, L"Selected: UEFI Log");
    DrawStatus (This, 1, L"Volume keys: choose    Power key: select");
  }

  for ( ; ;) {
    if (gST->ConIn == NULL) {
      return FuxiLogSourceBack;
    }

    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Signaled);
    if (EFI_ERROR (Status)) {
      return FuxiLogSourceBack;
    }

    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'k')) {
      if (Confirmed) {
        Confirmed = FALSE;
      } else {
        Selected = (Selected + FuxiLogSourceCount - 1) % FuxiLogSourceCount;
      }
    } else if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'j')) {
      if (Confirmed) {
        Confirmed = FALSE;
      } else {
        Selected = (Selected + 1) % FuxiLogSourceCount;
      }
    } else if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) ||
               (Key.UnicodeChar == CHAR_LINEFEED))
    {
      if (Confirmed) {
        return (FUXI_LOG_SOURCE)Selected;
      }

      Confirmed = TRUE;
    } else if (Key.ScanCode == SCAN_ESC) {
      if (Confirmed) {
        Confirmed = FALSE;
      } else {
        return FuxiLogSourceBack;
      }
    } else {
      continue;
    }

    DrawSelector (Gop, Content, Selected, Confirmed);
    if (DrawStatus != NULL) {
      if (Confirmed) {
        UnicodeSPrint (StatusLine, sizeof (StatusLine), L"Ready: %s", mSourceTitles[Selected]);
        DrawStatus (This, 0, StatusLine);
        DrawStatus (This, 1, L"Power key: export    long press: choose again");
      } else {
        UnicodeSPrint (StatusLine, sizeof (StatusLine), L"Selected: %s", mSourceTitles[Selected]);
        DrawStatus (This, 0, StatusLine);
        DrawStatus (This, 1, L"Volume keys: choose    Power key: select");
      }
    }
  }
}

STATIC
VOID
UpdateStatus (
  IN UNIXNU_MENU_PLUGIN_PROTOCOL    *This,
  IN UNIXNU_MENU_PLUGIN_DRAW_STATUS DrawStatus,
  IN CONST CHAR16                   *SourceName,
  IN UINTN                          Page,
  IN UINTN                          PageCount
  )
{
  CHAR16  StatusLine[96];

  if (DrawStatus == NULL) {
    return;
  }

  UnicodeSPrint (
    StatusLine,
    sizeof (StatusLine),
    L"%s  %u/%u  left %u",
    SourceName,
    (UINT32)(Page + 1),
    (UINT32)PageCount,
    (UINT32)(PageCount - Page - 1)
    );
  DrawStatus (This, 0, StatusLine);
}

STATIC
EFI_STATUS
ReadBytes (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                StartLba,
  IN  UINTN                  Bytes,
  OUT VOID                   *Buffer
  )
{
  UINTN   BlockSize;
  UINTN   Done;
  UINTN   Chunk;
  UINT8   *Out;
  EFI_STATUS  Status;

  if ((BlockIo == NULL) || (BlockIo->Media == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if ((BlockSize == 0) || ((Bytes % BlockSize) != 0)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Done = 0;
  Out  = Buffer;
  while (Done < Bytes) {
    Chunk = MIN ((UINTN)FUXI_UFS_READ_CHUNK, Bytes - Done);
    Chunk -= Chunk % BlockSize;
    if (Chunk == 0) {
      return EFI_BAD_BUFFER_SIZE;
    }

    Status = BlockIo->ReadBlocks (
                        BlockIo,
                        BlockIo->Media->MediaId,
                        StartLba + (Done / BlockSize),
                        Chunk,
                        Out
                        );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Done += Chunk;
    Out  += Chunk;
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
ValidHeader (
  IN CONST FUXI_UFS_LOG_HEADER  *Header
  )
{
  if (CompareMem (Header->Magic, "SHLOG001", sizeof (Header->Magic)) != 0) {
    return FALSE;
  }

  if ((Header->HeaderBytes < FUXI_UFS_HEADER_V3_BYTES) ||
      (Header->HeaderBytes > FUXI_UFS_LOG_BLOCK_SIZE))
  {
    return FALSE;
  }

  return (Header->WindowBlocks > 0) &&
         (Header->WindowBlocks <= FUXI_UFS_LOG_BLOCKS);
}

STATIC
EFI_STATUS
FindLogWindow (
  OUT EFI_BLOCK_IO_PROTOCOL  **BlockIo,
  OUT EFI_LBA                *StartLba,
  OUT UINTN                  *BlockDeviceCount
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  EFI_BLOCK_IO_PROTOCOL     *Candidate;
  UINT32                    BlockSize;
  UINT64                    Sectors;
  EFI_LBA                   Start;
  UINT8                     Header[FUXI_UFS_LOG_BLOCK_SIZE];

  *BlockIo         = NULL;
  *StartLba        = 0;
  *BlockDeviceCount = 0;
  Handles = NULL;
  Count   = 0;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *BlockDeviceCount = Count;
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&Candidate
                    );
    if (EFI_ERROR (Status) || (Candidate->Media == NULL)) {
      continue;
    }

    BlockSize = Candidate->Media->BlockSize;
    if ((BlockSize == 0) || ((FUXI_UFS_LOG_BYTES % BlockSize) != 0) ||
        ((FUXI_UFS_LOG_BLOCK_SIZE % BlockSize) != 0))
    {
      continue;
    }

    Sectors = FUXI_UFS_LOG_BYTES / BlockSize;
    if (Candidate->Media->LastBlock + 1 < Sectors) {
      continue;
    }

    Start = Candidate->Media->LastBlock + 1 - Sectors;
    Status = ReadBytes (Candidate, Start, FUXI_UFS_LOG_BLOCK_SIZE, Header);
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (ValidHeader ((CONST FUXI_UFS_LOG_HEADER *)Header)) {
      *BlockIo  = Candidate;
      *StartLba = Start;
      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);
  return EFI_NOT_FOUND;
}

STATIC
VOID
AppendAscii (
  IN OUT CHAR8        *Buffer,
  IN     UINTN        Capacity,
  IN OUT UINTN        *Length,
  IN     CONST CHAR8  *Text
  )
{
  UINTN  TextLength;
  UINTN  Remaining;

  if ((Buffer == NULL) || (Length == NULL) || (*Length >= Capacity)) {
    return;
  }

  TextLength = AsciiStrLen (Text);
  Remaining  = Capacity - *Length - 1;
  if (TextLength > Remaining) {
    TextLength = Remaining;
  }

  CopyMem (Buffer + *Length, Text, TextLength);
  *Length += TextLength;
}

STATIC
VOID
AppendBytes (
  IN OUT CHAR8        *Buffer,
  IN     UINTN        Capacity,
  IN OUT UINTN        *Length,
  IN     CONST VOID   *Data,
  IN     UINTN        DataLength
  )
{
  UINTN  Remaining;

  if ((Buffer == NULL) || (Length == NULL) || (Data == NULL) || (*Length >= Capacity)) {
    return;
  }

  Remaining = Capacity - *Length - 1;
  if (DataLength > Remaining) {
    DataLength = Remaining;
  }

  CopyMem (Buffer + *Length, Data, DataLength);
  *Length += DataLength;
}

STATIC
EFI_STATUS
AppendUefiLog (
  IN     EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN     EFI_LBA                ShlogStartLba,
  IN OUT CHAR8                  *Buffer,
  IN     UINTN                  Capacity,
  IN OUT UINTN                  *Length
  )
{
  EFI_STATUS              Status;
  UINT32                  BlockSize;
  UINT64                  Sectors;
  EFI_LBA                 StartLba;
  UINT8                   *Window;
  CONST FUXI_UEFI_LOG_HEADER  *Header;
  UINTN                   DataBytes;

  if ((BlockIo == NULL) || (BlockIo->Media == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if ((BlockSize == 0) ||
      ((FUXI_UEFI_LOG_BYTES % BlockSize) != 0) ||
      ((FUXI_UEFI_LOG_BLOCK_SIZE % BlockSize) != 0))
  {
    return EFI_UNSUPPORTED;
  }

  Sectors = FUXI_UEFI_LOG_BYTES / BlockSize;
  if (ShlogStartLba < Sectors) {
    return EFI_NOT_FOUND;
  }

  StartLba = ShlogStartLba - Sectors;
  Window = AllocatePages (EFI_SIZE_TO_PAGES ((UINTN)FUXI_UEFI_LOG_BYTES));
  if (Window == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ReadBytes (BlockIo, StartLba, (UINTN)FUXI_UEFI_LOG_BYTES, Window);
  if (EFI_ERROR (Status)) {
    FreePages (Window, EFI_SIZE_TO_PAGES ((UINTN)FUXI_UEFI_LOG_BYTES));
    return Status;
  }

  Header = (CONST FUXI_UEFI_LOG_HEADER *)Window;
  if ((CompareMem (Header->Magic, FUXI_UEFI_LOG_MAGIC, sizeof (Header->Magic)) != 0) ||
      (Header->HeaderBytes < sizeof (*Header)) ||
      (Header->HeaderBytes > FUXI_UEFI_LOG_BLOCK_SIZE) ||
      (Header->LogBytes > FUXI_UEFI_LOG_BYTES - FUXI_UEFI_LOG_BLOCK_SIZE))
  {
    FreePages (Window, EFI_SIZE_TO_PAGES ((UINTN)FUXI_UEFI_LOG_BYTES));
    return EFI_VOLUME_CORRUPTED;
  }

  AppendAscii (Buffer, Capacity, Length, "[uefi log]\n");
  DataBytes = Header->LogBytes;
  AppendBytes (Buffer, Capacity, Length, Window + FUXI_UEFI_LOG_BLOCK_SIZE, DataBytes);
  AppendAscii (Buffer, Capacity, Length, "\n");

  FreePages (Window, EFI_SIZE_TO_PAGES ((UINTN)FUXI_UEFI_LOG_BYTES));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
BuildUefiLogText (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                StartLba,
  OUT CHAR8                  **Text,
  OUT UINTN                  *TextLength
  )
{
  EFI_STATUS  Status;
  CHAR8       *Out;
  UINTN       Capacity;
  UINTN       Length;

  *Text       = NULL;
  *TextLength = 0;
  Capacity    = (UINTN)FUXI_UEFI_LOG_BYTES + 4096u;
  Out         = AllocatePool (Capacity);
  if (Out == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Length = 0;
  Status = AppendUefiLog (BlockIo, StartLba, Out, Capacity, &Length);
  if (EFI_ERROR (Status)) {
    FreePool (Out);
    return Status;
  }

  Out[Length] = '\0';
  *Text       = Out;
  *TextLength = Length;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
BuildLogText (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                StartLba,
  OUT CHAR8                  **Text,
  OUT UINTN                  *TextLength
  )
{
  EFI_STATUS              Status;
  UINT8                   *Window;
  CONST FUXI_UFS_LOG_HEADER  *Header;
  CHAR8                   *Out;
  UINTN                   Capacity;
  UINTN                   Length;
  CHAR8                   Line[192];
  UINT64                  MsgStart;
  UINT64                  MsgEnd;
  UINT64                  SlotStart;
  UINT64                  SlotArea;
  UINTN                   FirstSlot;
  UINTN                   Slot;
  UINTN                   SlotIndex;
  UINTN                   SlotLength;
  UINTN                   Index;
  UINTN                   CopyLength;

  *Text       = NULL;
  *TextLength = 0;
  Window = AllocatePages (EFI_SIZE_TO_PAGES (FUXI_UFS_LOG_BYTES));
  if (Window == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ReadBytes (BlockIo, StartLba, (UINTN)FUXI_UFS_LOG_BYTES, Window);
  if (EFI_ERROR (Status)) {
    FreePages (Window, EFI_SIZE_TO_PAGES (FUXI_UFS_LOG_BYTES));
    return Status;
  }

  Header = (CONST FUXI_UFS_LOG_HEADER *)Window;
  if (!ValidHeader (Header)) {
    FreePages (Window, EFI_SIZE_TO_PAGES (FUXI_UFS_LOG_BYTES));
    return EFI_NOT_FOUND;
  }

  MsgStart  = (UINT64)Header->MsgFirst * FUXI_UFS_LOG_BLOCK_SIZE;
  MsgEnd    = MsgStart + (UINT64)Header->MsgBlocks * FUXI_UFS_LOG_BLOCK_SIZE;
  SlotStart = (UINT64)Header->SlotFirst * FUXI_UFS_LOG_BLOCK_SIZE;
  SlotArea  = (UINT64)Header->SlotBlocks * Header->SlotCount * FUXI_UFS_LOG_BLOCK_SIZE;

  if ((Header->MsgBlocks > FUXI_UFS_LOG_BLOCKS) ||
      (Header->SlotBlocks > FUXI_UFS_LOG_BLOCKS) ||
      (Header->SlotCount > FUXI_UFS_LOG_BLOCKS) ||
      (MsgEnd > FUXI_UFS_LOG_BYTES) ||
      (Header->MsgBytes > (UINT64)Header->MsgBlocks * FUXI_UFS_LOG_BLOCK_SIZE) ||
      ((SlotStart + SlotArea) > FUXI_UFS_LOG_BYTES))
  {
    FreePages (Window, EFI_SIZE_TO_PAGES (FUXI_UFS_LOG_BYTES));
    return EFI_VOLUME_CORRUPTED;
  }

  Capacity = (UINTN)FUXI_UFS_LOG_BYTES + 65536u;
  Out = AllocatePool (Capacity);
  if (Out == NULL) {
    FreePages (Window, EFI_SIZE_TO_PAGES (FUXI_UFS_LOG_BYTES));
    return EFI_OUT_OF_RESOURCES;
  }

  Length = 0;
  AppendAscii (Out, Capacity, &Length, "[header]\n");

  AsciiSPrint (Line, sizeof (Line), "magic=%.8a\n", Header->Magic);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "version=%u\n", Header->Version);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "header_bytes=%u\n", Header->HeaderBytes);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "boot_id=0x%Lx\n", Header->BootId);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "first_lba=0x%Lx\n", Header->FirstLba);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "window_blocks=%u\n", Header->WindowBlocks);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (
    Line,
    sizeof (Line),
    "msg_first=%u msg_blocks=%u msg_bytes=%u\n",
    Header->MsgFirst,
    Header->MsgBlocks,
    Header->MsgBytes
    );
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (
    Line,
    sizeof (Line),
    "slot_first=%u slot_blocks=%u slot_count=%u slots_written=%u\n",
    Header->SlotFirst,
    Header->SlotBlocks,
    Header->SlotCount,
    Header->SlotsWritten
    );
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "uptime_s=%u rounds=%u write_errors=%u\n", Header->UptimeS, Header->Rounds, Header->WriteErrors);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "gpt_open=0x%x gpt_last=0x%x state=%u\n", Header->GptOpen, Header->GptLast, Header->State);
  AppendAscii (Out, Capacity, &Length, Line);
  AsciiSPrint (Line, sizeof (Line), "tag=%.32a\n", Header->Tag);
  AppendAscii (Out, Capacity, &Length, Line);

  AppendAscii (Out, Capacity, &Length, "[msgbuf]\n");
  CopyLength = (UINTN)Header->MsgBytes;
  AppendBytes (Out, Capacity, &Length, Window + (UINTN)MsgStart, CopyLength);
  AppendAscii (Out, Capacity, &Length, "\n[slots]\n");

  FirstSlot = 0;
  if (Header->SlotsWritten > Header->SlotCount) {
    FirstSlot = Header->SlotsWritten - Header->SlotCount;
  }

  for (Slot = FirstSlot; Slot < Header->SlotsWritten; Slot++) {
    SlotIndex  = Slot % Header->SlotCount;
    SlotLength = Header->SlotBlocks * FUXI_UFS_LOG_BLOCK_SIZE;
    if (SlotLength == 0) {
      continue;
    }

    SlotStart = (UINT64)Header->SlotFirst * FUXI_UFS_LOG_BLOCK_SIZE;
    SlotStart += (UINT64)SlotIndex * SlotLength;
    for (Index = 0; (Index < SlotLength) && (Window[(UINTN)SlotStart + Index] != 0); Index++) {
    }

    AppendBytes (Out, Capacity, &Length, Window + (UINTN)SlotStart, Index);
    AppendAscii (Out, Capacity, &Length, "\n");
  }

  Out[Length] = '\0';
  FreePages (Window, EFI_SIZE_TO_PAGES (FUXI_UFS_LOG_BYTES));
  *Text       = Out;
  *TextLength = Length;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
BuildDataCrashLogText (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                StartLba,
  OUT CHAR8                  **Text,
  OUT UINTN                  *TextLength
  )
{
  EFI_STATUS              Status;
  UINT8                   *WindowHeader;
  UINT8                   *CrashHeader;
  UINT8                   *CrashData;
  CONST FUXI_UFS_LOG_HEADER   *Header;
  CONST FUXI_UFS_CRASH_HEADER *Crash;
  CHAR8                   *Out;
  UINTN                   Capacity;
  UINTN                   Length;
  UINTN                   BlockSize;
  UINTN                   SectorsPerBlock;
  EFI_LBA                 CrashStart;
  UINTN                   DataBytes;
  UINTN                   DataReadBytes;
  CHAR8                   Line[160];

  *Text       = NULL;
  *TextLength = 0;
  if ((BlockIo == NULL) || (BlockIo->Media == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if ((BlockSize == 0) || ((FUXI_UFS_LOG_BLOCK_SIZE % BlockSize) != 0)) {
    return EFI_UNSUPPORTED;
  }

  WindowHeader = AllocatePool (FUXI_UFS_LOG_BLOCK_SIZE);
  if (WindowHeader == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ReadBytes (BlockIo, StartLba, FUXI_UFS_LOG_BLOCK_SIZE, WindowHeader);
  if (EFI_ERROR (Status)) {
    FreePool (WindowHeader);
    return Status;
  }

  Header = (CONST FUXI_UFS_LOG_HEADER *)WindowHeader;
  if (!ValidHeader (Header) ||
      (Header->Version < 4) ||
      (Header->HeaderBytes < sizeof (*Header)))
  {
    FreePool (WindowHeader);
    Status = EFI_NOT_FOUND;
    goto NoLog;
  }

  SectorsPerBlock = FUXI_UFS_LOG_BLOCK_SIZE / BlockSize;
  if (StartLba < (EFI_LBA)((UINT64)FUXI_UFS_CRASH_BLOCKS * SectorsPerBlock)) {
    FreePool (WindowHeader);
    Status = EFI_NOT_FOUND;
    goto NoLog;
  }

  CrashStart = StartLba - (EFI_LBA)((UINT64)FUXI_UFS_CRASH_BLOCKS * SectorsPerBlock);
  CrashHeader = AllocatePool (FUXI_UFS_LOG_BLOCK_SIZE);
  if (CrashHeader == NULL) {
    FreePool (WindowHeader);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = ReadBytes (BlockIo, CrashStart, FUXI_UFS_LOG_BLOCK_SIZE, CrashHeader);
  if (EFI_ERROR (Status)) {
    FreePool (CrashHeader);
    FreePool (WindowHeader);
    return Status;
  }

  Crash = (CONST FUXI_UFS_CRASH_HEADER *)CrashHeader;
  if ((CompareMem (Crash->Magic, FUXI_UFS_CRASH_MAGIC, sizeof (Crash->Magic)) != 0) ||
      (Crash->HeaderBytes < sizeof (*Crash)) ||
      (Crash->FileBytes > FUXI_UFS_CRASH_MAX_BYTES) ||
      (Crash->NameLen > 255u))
  {
    FreePool (CrashHeader);
    FreePool (WindowHeader);
    Status = EFI_NOT_FOUND;
    goto NoLog;
  }

  DataBytes = Crash->FileBytes;
  DataReadBytes = DataBytes;
  if (DataReadBytes != 0) {
    DataReadBytes = ((DataReadBytes + BlockSize - 1) / BlockSize) * BlockSize;
  }

  Capacity = FUXI_UFS_CRASH_MAX_BYTES + 4096u;
  Out = AllocatePool (Capacity);
  if (Out == NULL) {
    FreePool (CrashHeader);
    FreePool (WindowHeader);
    return EFI_OUT_OF_RESOURCES;
  }

  CrashData = NULL;
  if (DataReadBytes != 0) {
    CrashData = AllocatePool (DataReadBytes);
    if (CrashData == NULL) {
      FreePool (Out);
      FreePool (CrashHeader);
      FreePool (WindowHeader);
      return EFI_OUT_OF_RESOURCES;
    }

    Status = ReadBytes (BlockIo, CrashStart + (EFI_LBA)SectorsPerBlock, DataReadBytes, CrashData);
    if (EFI_ERROR (Status)) {
      FreePool (CrashData);
      FreePool (Out);
      FreePool (CrashHeader);
      FreePool (WindowHeader);
      return Status;
    }
  }

  Length = 0;
  AppendAscii (Out, Capacity, &Length, "[data crash log]\n");
  AsciiSPrint (Line, sizeof (Line), "generation=%u name_len=%u file_bytes=%u\n", Crash->Generation, Crash->NameLen, Crash->FileBytes);
  AppendAscii (Out, Capacity, &Length, Line);
  AppendAscii (Out, Capacity, &Length, "path=/private/var/mobile/Library/Logs/CrashReporter\n");
  AppendAscii (Out, Capacity, &Length, "[file]\n");
  if (DataBytes != 0) {
    AppendBytes (Out, Capacity, &Length, CrashData, DataBytes);
    AppendAscii (Out, Capacity, &Length, "\n");
  }

  if (CrashData != NULL) {
    FreePool (CrashData);
  }

  FreePool (CrashHeader);
  FreePool (WindowHeader);
  Out[Length] = '\0';
  *Text       = Out;
  *TextLength = Length;
  return EFI_SUCCESS;

NoLog:
  Capacity = 1024u;
  Out = AllocatePool (Capacity);
  if (Out == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Length = 0;
  AppendAscii (Out, Capacity, &Length, "[data crash log]\n");
  AppendAscii (Out, Capacity, &Length, "status=no-log\n");
  AppendAscii (Out, Capacity, &Length, "reason=the XNU CrashReporter exporter has not written SHCRASH01 yet; sheng-log=1 and a completed .ips are required.\n");
  AppendAscii (Out, Capacity, &Length, "path=/private/var/mobile/Library/Logs/CrashReporter\n");
  Out[Length] = '\0';
  *Text       = Out;
  *TextLength = Length;
  return EFI_SUCCESS;
}

STATIC
VOID
AppendUnicodeAscii (
  IN OUT CHAR8        *Buffer,
  IN     UINTN        Capacity,
  IN OUT UINTN        *Length,
  IN     CONST CHAR16 *Text
  )
{
  CHAR8  Ascii[320];

  Ascii[0] = '\0';
  UnicodeStrToAsciiStrS (Text, Ascii, sizeof (Ascii));
  Ascii[sizeof (Ascii) - 1] = '\0';
  AppendAscii (Buffer, Capacity, Length, Ascii);
}

STATIC
EFI_STATUS
BuildDeviceReport (
  OUT CHAR8  **Text,
  OUT UINTN  *TextLength
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     Count;
  UINTN                     Index;
  EFI_BLOCK_IO_PROTOCOL     *BlockIo;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  CHAR16                    *Path;
  CHAR16                    Line[320];
  CHAR8                     *Out;
  UINTN                     Capacity;
  UINTN                     Length;
  UINT32                    BlockSize;
  UINT64                    Sectors;
  EFI_LBA                   Start;
  UINT8                     Header[FUXI_UFS_LOG_BLOCK_SIZE];

  *Text       = NULL;
  *TextLength = 0;
  Handles = NULL;
  Count   = 0;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Capacity = Count * 384u + 4096u;
  Out = AllocatePool (Capacity);
  if (Out == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Length = 0;
  AppendAscii (Out, Capacity, &Length, "[block devices]\n");
  AppendAscii (Out, Capacity, &Length, "count=");
  UnicodeSPrint (Line, sizeof (Line), L"%u\n", (UINT32)Count);
  AppendUnicodeAscii (Out, Capacity, &Length, Line);

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&BlockIo
                    );
    if (EFI_ERROR (Status) || (BlockIo->Media == NULL)) {
      continue;
    }

    Path = NULL;
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiDevicePathProtocolGuid,
                    (VOID **)&DevicePath
                    );
    if (!EFI_ERROR (Status)) {
      Path = ConvertDevicePathToText (DevicePath, FALSE, TRUE);
    }

    BlockSize = BlockIo->Media->BlockSize;
    UnicodeSPrint (
      Line,
      sizeof (Line),
      L"[%u] %s block=%u last=0x%Lx bytes=0x%Lx\n",
      (UINT32)Index,
      (Path != NULL) ? Path : L"<no path>",
      BlockSize,
      (UINT64)BlockIo->Media->LastBlock,
      ((UINT64)BlockIo->Media->LastBlock + 1) * BlockSize
      );
    AppendUnicodeAscii (Out, Capacity, &Length, Line);
    if (Path != NULL) {
      FreePool (Path);
    }

    if ((BlockSize == 0) ||
        ((FUXI_UFS_LOG_BYTES % BlockSize) != 0) ||
        ((FUXI_UFS_LOG_BLOCK_SIZE % BlockSize) != 0))
    {
      AppendAscii (Out, Capacity, &Length, "  tail=skip bad-block-size\n");
      continue;
    }

    Sectors = FUXI_UFS_LOG_BYTES / BlockSize;
    if (BlockIo->Media->LastBlock + 1 < Sectors) {
      AppendAscii (Out, Capacity, &Length, "  tail=skip too-small\n");
      continue;
    }

    Start  = BlockIo->Media->LastBlock + 1 - Sectors;
    Status = ReadBytes (BlockIo, Start, FUXI_UFS_LOG_BLOCK_SIZE, Header);
    if (EFI_ERROR (Status)) {
      UnicodeSPrint (Line, sizeof (Line), L"  tail=read-error %r\n", Status);
      AppendUnicodeAscii (Out, Capacity, &Length, Line);
      continue;
    }

    AppendAscii (Out, Capacity, &Length, "  tail=ok magic=");
    AppendBytes (Out, Capacity, &Length, Header, 8);
    AppendAscii (Out, Capacity, &Length, "\n");
  }

  FreePool (Handles);
  Out[Length] = '\0';
  *Text       = Out;
  *TextLength = Length;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ShowPayload (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN CONST UNIXNU_PLUGIN_RECT       *Content,
  IN UNIXNU_MENU_PLUGIN_PROTOCOL    *This,
  IN UNIXNU_MENU_PLUGIN_DRAW_STATUS DrawStatus,
  IN CONST CHAR16                   *SourceName,
  IN UINT8                          *Payload,
  IN UINTN                          PayloadLength,
  IN UINTN                          PageCount,
  IN UINT32                         PayloadChecksum
  )
{
  EFI_STATUS    Status;
  UINT32        SessionId;
  UINTN         Page;
  UINTN         Gear;
  EFI_EVENT     Timer;
  EFI_EVENT     Events[2];
  UINTN         EventCount;
  UINTN         Signaled;
  EFI_INPUT_KEY Key;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  *BackBuffer;

  SessionId = 0x9E3779B9u ^ (UINT32)(UINTN)Gop ^ (UINT32)PayloadLength;
  if (SessionId == 0) {
    SessionId = 1;
  }

  Page = 0;
  BackBuffer = AllocatePool ((UINTN)Content->Width * Content->Height * sizeof (*BackBuffer));
  if (BackBuffer == NULL) {
    FreePool (Payload);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &Timer);
  if (EFI_ERROR (Status)) {
    FreePool (BackBuffer);
    FreePool (Payload);
    return Status;
  }

  Gear = ADVANCED_LOG_VIEW_GEAR_INITIAL;
  gBS->SetTimer (Timer, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS (AdvancedLogViewGearInterval (Gear)));
  EventCount = 0;
  Events[EventCount++] = Timer;
  if (gST->ConIn != NULL) {
    Events[EventCount++] = gST->ConIn->WaitForKey;
  }

  AdvancedLogViewDrawPage (Gop, Content, BackBuffer, Payload, PayloadLength, Page, PageCount, SessionId, PayloadChecksum, Gear + 1);
  UpdateStatus (This, DrawStatus, SourceName, Page, PageCount);
  for ( ; ;) {
    Status = gBS->WaitForEvent (EventCount, Events, &Signaled);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Signaled == 0) {
      Page = (Page + 1) % PageCount;
      AdvancedLogViewDrawPage (Gop, Content, BackBuffer, Payload, PayloadLength, Page, PageCount, SessionId, PayloadChecksum, Gear + 1);
      UpdateStatus (This, DrawStatus, SourceName, Page, PageCount);
      continue;
    }

    if (gST->ConIn == NULL) {
      continue;
    }

    if (EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key))) {
      continue;
    }

    if ((Key.UnicodeChar == CHAR_CARRIAGE_RETURN) ||
        (Key.UnicodeChar == CHAR_LINEFEED) ||
        (Key.ScanCode == SCAN_ESC))
    {
      break;
    }

    if ((Key.ScanCode == SCAN_UP) || (Key.UnicodeChar == L'k')) {
      if (Gear < ADVANCED_LOG_VIEW_GEAR_COUNT - 1) {
        Gear++;
        gBS->SetTimer (Timer, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS (AdvancedLogViewGearInterval (Gear)));
      }

      AdvancedLogViewDrawPage (Gop, Content, BackBuffer, Payload, PayloadLength, Page, PageCount, SessionId, PayloadChecksum, Gear + 1);
      UpdateStatus (This, DrawStatus, SourceName, Page, PageCount);
    } else if ((Key.ScanCode == SCAN_DOWN) || (Key.UnicodeChar == L'j')) {
      if (Gear > 0) {
        Gear--;
        gBS->SetTimer (Timer, TimerPeriodic, EFI_TIMER_PERIOD_MILLISECONDS (AdvancedLogViewGearInterval (Gear)));
      }

      AdvancedLogViewDrawPage (Gop, Content, BackBuffer, Payload, PayloadLength, Page, PageCount, SessionId, PayloadChecksum, Gear + 1);
      UpdateStatus (This, DrawStatus, SourceName, Page, PageCount);
    }
  }

  gBS->SetTimer (Timer, TimerCancel, 0);
  gBS->CloseEvent (Timer);
  FreePool (BackBuffer);
  FreePool (Payload);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ExportText (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN CONST UNIXNU_PLUGIN_RECT       *Content,
  IN UNIXNU_MENU_PLUGIN_PROTOCOL    *This,
  IN UNIXNU_MENU_PLUGIN_DRAW_STATUS DrawStatus,
  IN CONST CHAR16                   *SourceName,
  IN CHAR8                          *Text,
  IN UINTN                          TextLength
  )
{
  EFI_STATUS  Status;
  UINT8       *Payload;
  UINTN       PayloadLength;
  UINTN       PageCount;
  UINT32      PayloadChecksum;

  Status = AdvancedLogViewPrepare (
             (CONST UINT8 *)Text,
             TextLength,
             &Payload,
             &PayloadLength,
             &PageCount,
             &PayloadChecksum
             );
  FreePool (Text);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return ShowPayload (
           Gop,
           Content,
           This,
           DrawStatus,
           SourceName,
           Payload,
           PayloadLength,
           PageCount,
           PayloadChecksum
           );
}

STATIC
VOID
ReadPowerKey (
  OUT BOOLEAN  *ExitRequested
  )
{
  EFI_INPUT_KEY  Key;

  *ExitRequested = FALSE;
  if ((gST->ConIn == NULL) ||
      EFI_ERROR (gST->ConIn->ReadKeyStroke (gST->ConIn, &Key)))
  {
    return;
  }

  *ExitRequested =
    (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) ||
    (Key.UnicodeChar == CHAR_LINEFEED) ||
    (Key.ScanCode == SCAN_ESC);
}

STATIC
EFI_STATUS
ClearUserdataLog (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN CONST UNIXNU_PLUGIN_RECT       *Content,
  IN UNIXNU_MENU_PLUGIN_PROTOCOL    *This,
  IN UNIXNU_MENU_PLUGIN_DRAW_STATUS DrawStatus
  )
{
  EFI_STATUS              Status;
  EFI_BLOCK_IO_PROTOCOL   *BlockIo;
  EFI_LBA                 StartLba;
  UINTN                   BlockDeviceCount;
  UINTN                   BlockSize;
  UINT8                   *Zero;
  CHAR16                  StatusLine[96];
  BOOLEAN                 ExitRequested;
  UINTN                   Signaled;

  Fill (Gop, Content->X, Content->Y, Content->Width, Content->Height, mBackground);

  Status = FindLogWindow (&BlockIo, &StartLba, &BlockDeviceCount);
  if (EFI_ERROR (Status)) {
    if (DrawStatus != NULL) {
      DrawStatus (This, 0, L"Clear Userdata Log: no log window");
      DrawStatus (This, 1, L"Power key: back");
    }
  } else if ((BlockIo->Media == NULL) || BlockIo->Media->ReadOnly) {
    Status = EFI_WRITE_PROTECTED;
    if (DrawStatus != NULL) {
      DrawStatus (This, 0, L"Clear Userdata Log: medium is read-only");
      DrawStatus (This, 1, L"Power key: back");
    }
  } else {
    BlockSize = BlockIo->Media->BlockSize;
    if ((BlockSize == 0) || ((FUXI_UFS_LOG_BLOCK_SIZE % BlockSize) != 0)) {
      Status = EFI_BAD_BUFFER_SIZE;
    } else {
      Zero = AllocateZeroPool (FUXI_UFS_LOG_BLOCK_SIZE);
      if (Zero == NULL) {
        Status = EFI_OUT_OF_RESOURCES;
      } else {
        Status = BlockIo->WriteBlocks (
                           BlockIo,
                           BlockIo->Media->MediaId,
                           StartLba,
                           FUXI_UFS_LOG_BLOCK_SIZE,
                           Zero
                           );
        FreePool (Zero);
      }
    }

    if (DrawStatus != NULL) {
      if (EFI_ERROR (Status)) {
        UnicodeSPrint (StatusLine, sizeof (StatusLine), L"Clear Userdata Log failed (%r)", Status);
        DrawStatus (This, 0, StatusLine);
      } else {
        DrawStatus (This, 0, L"Userdata log cleared");
      }
      DrawStatus (This, 1, L"Power key: back");
    }
  }

  ExitRequested = FALSE;
  while (!ExitRequested) {
    if (gST->ConIn != NULL) {
      Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Signaled);
      if (!EFI_ERROR (Status) && (Signaled == 0)) {
        ReadPowerKey (&ExitRequested);
      }
    } else {
      gBS->Stall (1000000);
      break;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FuxiUfsLogRun (
  IN UNIXNU_MENU_PLUGIN_PROTOCOL    *This,
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL   *Gop,
  IN CONST UNIXNU_PLUGIN_RECT       *Content,
  IN UNIXNU_MENU_PLUGIN_DRAW_STATUS DrawStatus
  )
{
  EFI_STATUS              Status;
  FUXI_LOG_SOURCE         Source;
  EFI_BLOCK_IO_PROTOCOL   *BlockIo;
  EFI_LBA                 StartLba;
  UINTN                   BlockDeviceCount;
  CHAR8                   *Text;
  UINTN                   TextLength;

  if ((Gop == NULL) || (Content == NULL) || (Content->Width == 0) || (Content->Height == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Source = SelectExportSource (This, Gop, Content, DrawStatus);
  if (Source == FuxiLogSourceBack) {
    Fill (Gop, Content->X, Content->Y, Content->Width, Content->Height, mBackground);
    return EFI_SUCCESS;
  }

  if (Source == FuxiLogSourceClearUserdata) {
    return ClearUserdataLog (Gop, Content, This, DrawStatus);
  }

  BlockIo  = NULL;
  StartLba = 0;
  Text     = NULL;
  TextLength = 0;

  Status = FindLogWindow (&BlockIo, &StartLba, &BlockDeviceCount);
  if (EFI_ERROR (Status)) {
    Status = BuildDeviceReport (&Text, &TextLength);
  } else if (Source == FuxiLogSourceDataCrash) {
    Status = BuildDataCrashLogText (BlockIo, StartLba, &Text, &TextLength);
  } else if (Source == FuxiLogSourceUefi) {
    Status = BuildUefiLogText (BlockIo, StartLba, &Text, &TextLength);
  } else {
    Status = BuildLogText (BlockIo, StartLba, &Text, &TextLength);
  }

  if (EFI_ERROR (Status)) {
    CHAR16   StatusLine[96];
    BOOLEAN  ExitRequested;
    UINTN    Signaled;

    Fill (Gop, Content->X, Content->Y, Content->Width, Content->Height, mBackground);
    if (DrawStatus != NULL) {
      UnicodeSPrint (StatusLine, sizeof (StatusLine), L"%s: export failed (%r)", mSourceTitles[Source], Status);
      DrawStatus (This, 0, StatusLine);
      DrawStatus (This, 1, L"Power key: back");
    }

    ExitRequested = FALSE;
    while (!ExitRequested) {
      if (gST->ConIn != NULL) {
        Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Signaled);
        if (!EFI_ERROR (Status) && (Signaled == 0)) {
          ReadPowerKey (&ExitRequested);
        }
      } else {
        gBS->Stall (1000000);
        break;
      }
    }

    if (Text != NULL) {
      FreePool (Text);
    }

    return Status;
  }

  return ExportText (
           Gop,
           Content,
           This,
           DrawStatus,
           mSourceTitles[Source],
           Text,
           TextLength
           );
}

STATIC UNIXNU_MENU_PLUGIN_PROTOCOL  mFuxiUfsLogPlugin = {
  UNIXNU_MENU_PLUGIN_PROTOCOL_SIGNATURE,
  UNIXNU_MENU_PLUGIN_PROTOCOL_VERSION,
  L"UFS Log",
  FuxiUfsLogRun
};

EFI_STATUS
EFIAPI
FuxiUfsLogDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)SystemTable;

  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gUniXNUMenuPluginProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mFuxiUfsLogPlugin
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}
