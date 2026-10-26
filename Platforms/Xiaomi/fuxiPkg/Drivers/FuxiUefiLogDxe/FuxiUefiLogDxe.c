/** @file
  Persist the UEFI Advanced Logger into the UFS userdata log area.

  The XNU BootLog writer owns the last 8192 4K blocks of the userdata UFS
  partition. This driver installs the UniXNU log-flush hook and, when
  DarwinBoot invokes it just before ExitBootServices, writes the Advanced
  Logger text into a small region immediately before that window.
  FuxiUfsLogDxe prepends the saved text to the XNU SHLOG window, giving one
  QR/OneSend export that covers UEFI, UniXNU, DarwinBoot and XNU.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/BlockIo.h>
#include <Protocol/UniXNULogFlush.h>
#include <Library/AdvancedLogViewLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define FUXI_UFS_LOG_BLOCK_SIZE   4096u
#define FUXI_UFS_LOG_BLOCKS       8192u
#define FUXI_UFS_LOG_BYTES        ((UINT64)FUXI_UFS_LOG_BLOCK_SIZE * FUXI_UFS_LOG_BLOCKS)

#define FUXI_UEFI_LOG_BLOCK_SIZE  4096u
#define FUXI_UEFI_LOG_BLOCKS      64u
#define FUXI_UEFI_LOG_BYTES       ((UINTN)FUXI_UEFI_LOG_BLOCK_SIZE * FUXI_UEFI_LOG_BLOCKS)
#define FUXI_UEFI_LOG_MAGIC       "SHUEFI01"

#pragma pack(1)
typedef struct {
  CHAR8   Magic[8];
  UINT32  Version;
  UINT32  HeaderBytes;
  UINT32  LogBytes;
  UINT32  Reserved[27];
} FUXI_UEFI_LOG_HEADER;
#pragma pack()

STATIC
EFI_STATUS
ReadBytes (
  IN  EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN  EFI_LBA                StartLba,
  IN  UINTN                  Bytes,
  OUT VOID                   *Buffer
  )
{
  UINTN       BlockSize;
  UINTN       Done;
  UINTN       Chunk;
  UINT8       *Out;
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
    Chunk = MIN (Bytes - Done, (UINTN)(1024u * 1024u));
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
EFI_STATUS
WriteBytes (
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo,
  IN EFI_LBA                StartLba,
  IN UINTN                  Bytes,
  IN CONST VOID             *Buffer
  )
{
  UINTN       BlockSize;
  UINTN       Done;
  UINTN       Chunk;
  CONST UINT8 *In;
  EFI_STATUS  Status;

  if ((BlockIo == NULL) || (BlockIo->Media == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  BlockSize = BlockIo->Media->BlockSize;
  if ((BlockSize == 0) || ((Bytes % BlockSize) != 0)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Done = 0;
  In   = Buffer;
  while (Done < Bytes) {
    Chunk = MIN (Bytes - Done, (UINTN)(1024u * 1024u));
    Chunk -= Chunk % BlockSize;
    if (Chunk == 0) {
      return EFI_BAD_BUFFER_SIZE;
    }

    Status = BlockIo->WriteBlocks (
                        BlockIo,
                        BlockIo->Media->MediaId,
                        StartLba + (Done / BlockSize),
                        Chunk,
                        (VOID *)In
                        );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Done += Chunk;
    In   += Chunk;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FindShlogTail (
  OUT EFI_BLOCK_IO_PROTOCOL  **BlockIo,
  OUT EFI_LBA                *StartLba
  )
{
  EFI_STATUS              Status;
  EFI_HANDLE              *Handles;
  UINTN                   Count;
  UINTN                   Index;
  EFI_BLOCK_IO_PROTOCOL   *Candidate;
  UINT32                  BlockSize;
  UINT64                  Sectors;
  UINT8                   Header[FUXI_UFS_LOG_BLOCK_SIZE];

  *BlockIo  = NULL;
  *StartLba = 0;
  Handles   = NULL;
  Count     = 0;

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
    if ((BlockSize == 0) ||
        ((FUXI_UFS_LOG_BYTES % BlockSize) != 0) ||
        ((FUXI_UFS_LOG_BLOCK_SIZE % BlockSize) != 0))
    {
      continue;
    }

    Sectors = FUXI_UFS_LOG_BYTES / BlockSize;
    if (Candidate->Media->LastBlock + 1 < Sectors) {
      continue;
    }

    Status = ReadBytes (
               Candidate,
               Candidate->Media->LastBlock + 1 - Sectors,
               FUXI_UFS_LOG_BLOCK_SIZE,
               Header
               );
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (CompareMem (Header, "SHLOG001", 8) == 0) {
      *BlockIo  = Candidate;
      *StartLba = Candidate->Media->LastBlock + 1 - Sectors;
      FreePool (Handles);
      return EFI_SUCCESS;
    }
  }

  FreePool (Handles);
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
UefiLogFlush (
  IN UNIXNU_LOG_FLUSH_PROTOCOL  *This
  )
{
  EFI_STATUS              Status;
  EFI_BLOCK_IO_PROTOCOL   *BlockIo;
  EFI_LBA                 StartLba;
  EFI_LBA                 UefiLba;
  UINTN                   UefiSectors;
  UINT32                  BlockSize;
  CHAR8                   *Log;
  UINTN                   LogLen;
  UINT8                   *Buffer;
  FUXI_UEFI_LOG_HEADER    *Header;

  (VOID)This;

  Log    = NULL;
  LogLen = 0;
  Status = AdvancedLogViewCollect (&Log, &LogLen);
  if (EFI_ERROR (Status) || (Log == NULL) || (LogLen == 0)) {
    if (Log != NULL) {
      FreePool (Log);
    }

    return EFI_NOT_FOUND;
  }

  Status = FindShlogTail (&BlockIo, &StartLba);
  if (EFI_ERROR (Status) || (BlockIo == NULL) || (BlockIo->Media == NULL)) {
    FreePool (Log);
    return Status;
  }

  BlockSize   = BlockIo->Media->BlockSize;
  UefiSectors = FUXI_UEFI_LOG_BYTES / BlockSize;
  if ((BlockSize == 0) || (UefiSectors == 0) || (StartLba < UefiSectors)) {
    FreePool (Log);
    return EFI_BAD_BUFFER_SIZE;
  }

  UefiLba = StartLba - UefiSectors;
  Buffer  = AllocateZeroPool (FUXI_UEFI_LOG_BYTES);
  if (Buffer == NULL) {
    FreePool (Log);
    return EFI_OUT_OF_RESOURCES;
  }

  Header = (FUXI_UEFI_LOG_HEADER *)Buffer;
  CopyMem (Header->Magic, FUXI_UEFI_LOG_MAGIC, sizeof (Header->Magic));
  Header->Version     = 1;
  Header->HeaderBytes = sizeof (*Header);
  Header->LogBytes    = (UINT32)MIN (
                         LogLen,
                         FUXI_UEFI_LOG_BYTES - FUXI_UEFI_LOG_BLOCK_SIZE
                         );
  CopyMem (Buffer + FUXI_UEFI_LOG_BLOCK_SIZE, Log, Header->LogBytes);

  Status = WriteBytes (BlockIo, UefiLba, FUXI_UEFI_LOG_BYTES, Buffer);
  FreePool (Buffer);
  FreePool (Log);
  return Status;
}

STATIC UNIXNU_LOG_FLUSH_PROTOCOL  mLogFlush = {
  UNIXNU_LOG_FLUSH_PROTOCOL_SIGNATURE,
  UNIXNU_LOG_FLUSH_PROTOCOL_VERSION,
  UefiLogFlush
};

//
// QcomXnuUsbDebug installs its own private translated SMMU domain and requires
// the USB SID 0x40 stream slot to be free. The nuwa USB device chain leaves a
// live stream route in the apps SMMU, which makes the XNU driver refuse with
// "USB stream exists". Release that exact route at the ExitBootServices
// boundary: halt the inherited DWC3 core, then clear the stream validity bit
// (SMR[31], or S2CR[10] with extended stream IDs). All other stream routes and
// the SMMU global controls are left untouched.
//

#define FUXI_USB_DWC3_BASE     0x0a600000ULL
#define FUXI_USB_SMMU_BASE     0x15000000ULL
#define FUXI_USB_STREAM_ID     0x40u

#define FUXI_USB_DWC3_DCTL     0xc704u
#define FUXI_USB_DWC3_DSTS     0xc70cu
#define FUXI_USB_DWC3_RUN_STOP     BIT31
#define FUXI_USB_DWC3_DEVCTRLHLT   BIT22

#define FUXI_USB_SMMU_SMR_BASE 0x800u
#define FUXI_USB_SMMU_S2CR_BASE 0xc00u
#define FUXI_USB_SMMU_CR0      0x000u
#define FUXI_USB_SMMU_ID0      0x020u

#define FUXI_USB_SMMU_SMR_VALID        BIT31
#define FUXI_USB_SMMU_S2CR_EXIDENABLE  BIT10
#define FUXI_USB_SMMU_CR0_EXIDS        BIT3

#define FUXI_USB_SMMU_MASK_SHIFT       16u
#define FUXI_USB_SMMU_ID_MASK_LEGACY   0x7fffu
#define FUXI_USB_SMMU_ID_MASK_EXIDS    0xffffu
#define FUXI_USB_SMMU_MAX_STREAMS      128u
#define FUXI_USB_HALT_POLLS            10000u

STATIC
VOID
FuxiHaltInheritedDwc3 (
  VOID
  )
{
  UINT32  Control;
  UINT32  Status;
  UINTN   Poll;

  Control = MmioRead32 ((UINTN)(FUXI_USB_DWC3_BASE + FUXI_USB_DWC3_DCTL));
  MmioWrite32 ((UINTN)(FUXI_USB_DWC3_BASE + FUXI_USB_DWC3_DCTL),
               Control & ~FUXI_USB_DWC3_RUN_STOP);

  for (Poll = 0; Poll < FUXI_USB_HALT_POLLS; Poll++) {
    Control = MmioRead32 ((UINTN)(FUXI_USB_DWC3_BASE + FUXI_USB_DWC3_DCTL));
    Status  = MmioRead32 ((UINTN)(FUXI_USB_DWC3_BASE + FUXI_USB_DWC3_DSTS));
    if ((Control & FUXI_USB_DWC3_RUN_STOP) == 0 &&
        (Status & FUXI_USB_DWC3_DEVCTRLHLT) != 0) {
      return;
    }

    gBS->Stall (1);
  }
}

STATIC
VOID
FuxiReleaseUsbSmmuStream (
  VOID
  )
{
  UINT32   Cr0;
  UINT32   Id0;
  UINT32   Smr;
  UINT32   S2cr;
  UINT32   Mask;
  UINT32   Id;
  UINT32   StreamCount;
  UINTN    Index;
  BOOLEAN  Exids;

  Cr0 = MmioRead32 ((UINTN)(FUXI_USB_SMMU_BASE + FUXI_USB_SMMU_CR0));
  Id0 = MmioRead32 ((UINTN)(FUXI_USB_SMMU_BASE + FUXI_USB_SMMU_ID0));

  Exids       = (Cr0 & FUXI_USB_SMMU_CR0_EXIDS) != 0;
  StreamCount = Id0 & 0xffu;
  if (StreamCount > FUXI_USB_SMMU_MAX_STREAMS) {
    StreamCount = FUXI_USB_SMMU_MAX_STREAMS;
  }

  for (Index = 0; Index < StreamCount; Index++) {
    Smr  = MmioRead32 ((UINTN)(FUXI_USB_SMMU_BASE + FUXI_USB_SMMU_SMR_BASE + 4u * Index));
    S2cr = MmioRead32 ((UINTN)(FUXI_USB_SMMU_BASE + FUXI_USB_SMMU_S2CR_BASE + 4u * Index));

    if (Exids) {
      if ((S2cr & FUXI_USB_SMMU_S2CR_EXIDENABLE) == 0) {
        continue;
      }

      Mask = (Smr >> FUXI_USB_SMMU_MASK_SHIFT) & FUXI_USB_SMMU_ID_MASK_EXIDS;
      Id   = Smr & FUXI_USB_SMMU_ID_MASK_EXIDS;
    } else {
      if ((Smr & FUXI_USB_SMMU_SMR_VALID) == 0) {
        continue;
      }

      Mask = (Smr >> FUXI_USB_SMMU_MASK_SHIFT) & FUXI_USB_SMMU_ID_MASK_LEGACY;
      Id   = Smr & FUXI_USB_SMMU_ID_MASK_LEGACY;
    }

    if ((FUXI_USB_STREAM_ID & ~Mask) != (Id & ~Mask)) {
      continue;
    }

    if (Exids) {
      MmioWrite32 ((UINTN)(FUXI_USB_SMMU_BASE + FUXI_USB_SMMU_S2CR_BASE + 4u * Index),
                   S2cr & ~FUXI_USB_SMMU_S2CR_EXIDENABLE);
    } else {
      MmioWrite32 ((UINTN)(FUXI_USB_SMMU_BASE + FUXI_USB_SMMU_SMR_BASE + 4u * Index),
                   Smr & ~FUXI_USB_SMMU_SMR_VALID);
    }
  }
}

STATIC
VOID
EFIAPI
FuxiUsbHandoffAtExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;
  (VOID)Context;

  FuxiHaltInheritedDwc3 ();
  FuxiReleaseUsbSmmuStream ();
}

EFI_STATUS
EFIAPI
FuxiUefiLogDxeEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   ExitBootEvent;

  (VOID)SystemTable;

  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gUniXNULogFlushProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mLogFlush
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ExitBootEvent = NULL;
  return gBS->CreateEventEx (
                EVT_NOTIFY_SIGNAL,
                TPL_CALLBACK,
                FuxiUsbHandoffAtExitBootServices,
                NULL,
                &gEfiEventExitBootServicesGuid,
                &ExitBootEvent
                );
}
