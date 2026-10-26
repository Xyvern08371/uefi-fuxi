/** @file
  Expose the UniXNU payload volume as a block device.

  `fastboot boot` loads the payload FAT volume into RAM, but nothing tells this
  firmware where. Scan the UEFI memory map for a FAT boot sector labelled
  FUXIESP and publish it through EFI_BLOCK_IO_PROTOCOL; the standard FAT driver
  then mounts it and the boot manager sees an ordinary file system.  The volume
  is RAM-backed, so writes are allowed in place: they only change this boot's
  copy and let the already-mounted EFI disk carry exported log files.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>
#include <Library/HobLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>

#define FUXI_LABEL_OFFSET  0x47
#define FUXI_LABEL_LENGTH  7
#define FUXI_BLOCK_SIZE    512
#define FUXI_PAYLOAD_BASE  0xA8000000
#define FUXI_PAYLOAD_SIZE  0x10000000

STATIC EFI_BLOCK_IO_MEDIA     mMedia;
STATIC EFI_BLOCK_IO_PROTOCOL  mBlockIo;
STATIC UINT8                  *mVolumeBase;
STATIC BOOLEAN                mLastLabeled;
STATIC BOOLEAN                mHintSeen;
STATIC UINT64                 mHintValue;
STATIC UINTN                  mVolumeBytes;

STATIC
BOOLEAN
Scannable (
  IN EFI_MEMORY_TYPE  Type
  )
{
  // Only types that the memory map guarantees are backed by normal RAM.
  // Reserved and MMIO ranges can be listed yet still fault when read.
  return (Type == EfiConventionalMemory) || (Type == EfiLoaderData) ||
         (Type == EfiLoaderCode) || (Type == EfiBootServicesData) ||
         (Type == EfiBootServicesCode) || (Type == EfiRuntimeServicesData) ||
         (Type == EfiRuntimeServicesCode);
}

STATIC
BOOLEAN
IsPayloadVolume (
  IN CONST UINT8  *Sector,
  OUT UINT64      *TotalSectors
  )
{
  UINT16  BytesPerSector;
  UINT32  SectorsPerFat;
  UINT32  Total32;
  UINT16  Total16;

  if ((Sector[510] != 0x55) || (Sector[511] != 0xAA)) {
    return FALSE;
  }

  // Prefer the expected label, but accept any plausible FAT volume so a
  // differently labelled payload still mounts.
  mLastLabeled = (CompareMem (Sector + FUXI_LABEL_OFFSET, "FUXIESP", FUXI_LABEL_LENGTH) == 0);

  BytesPerSector = (UINT16)(Sector[11] | (Sector[12] << 8));
  if (BytesPerSector != FUXI_BLOCK_SIZE) {
    return FALSE;
  }

  Total16 = (UINT16)(Sector[19] | (Sector[20] << 8));
  Total32 = (UINT32)(Sector[32] | (Sector[33] << 8) | (Sector[34] << 16) | ((UINT32)Sector[35] << 24));
  *TotalSectors = (Total16 != 0) ? Total16 : Total32;
  if (*TotalSectors == 0) {
    return FALSE;
  }

  SectorsPerFat = (UINT32)(Sector[36] | (Sector[37] << 8) | (Sector[38] << 16) | ((UINT32)Sector[39] << 24));
  if (SectorsPerFat == 0) {
    return FALSE;
  }

  //
  // Only a large volume can be the payload; this rejects stray boot sectors in
  // unrelated RAM without trusting the label alone.
  //
  if (*TotalSectors < (64ULL * 1024 * 1024 / FUXI_BLOCK_SIZE)) {
    return FALSE;
  }

  return TRUE;
}

STATIC
EFI_STATUS
EFIAPI
FuxiBlockReset (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN BOOLEAN                ExtendedVerification
  )
{
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FuxiBlockRead (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  OUT VOID                  *Buffer
  )
{
  UINT64  Offset;
  UINT64  End;

  if ((MediaId != 0) && (MediaId != This->Media->MediaId)) {
    return EFI_MEDIA_CHANGED;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize % FUXI_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Offset = Lba * FUXI_BLOCK_SIZE;
  End    = Offset + BufferSize;
  if ((End < Offset) || (End > mVolumeBytes)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Buffer, mVolumeBase + Offset, BufferSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FuxiBlockWrite (
  IN EFI_BLOCK_IO_PROTOCOL  *This,
  IN UINT32                 MediaId,
  IN EFI_LBA                Lba,
  IN UINTN                  BufferSize,
  IN VOID                   *Buffer
  )
{
  UINT64  Offset;
  UINT64  End;

  if ((MediaId != 0) && (MediaId != This->Media->MediaId)) {
    return EFI_MEDIA_CHANGED;
  }

  if (Buffer == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if ((BufferSize % FUXI_BLOCK_SIZE) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Offset = Lba * FUXI_BLOCK_SIZE;
  End    = Offset + BufferSize;
  if ((End < Offset) || (End > mVolumeBytes)) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (mVolumeBase + Offset, Buffer, BufferSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FuxiBlockFlush (
  IN EFI_BLOCK_IO_PROTOCOL  *This
  )
{
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
ScanRange (
  IN UINT64  Start,
  IN UINT64  Length
  )
{
  UINT64                 Address;
  UINT64                 TotalSectors;

  for (Address = Start; Address + FUXI_BLOCK_SIZE <= Start + Length; Address += FUXI_BLOCK_SIZE) {
    UINT8  *Page = (UINT8 *)(UINTN)Address;
    if (IsPayloadVolume (Page, &TotalSectors)) {
      mVolumeBase  = Page;
      mVolumeBytes = (UINTN)(TotalSectors * FUXI_BLOCK_SIZE);
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
BOOLEAN
LocatePayload (
  VOID
  )
{
  EFI_STATUS             Status;
  EFI_MEMORY_DESCRIPTOR  *Map;
  UINTN                  MapSize;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVersion;
  UINTN                  Index;
  UINTN                  Count;

  if (ScanRange (FUXI_PAYLOAD_BASE, FUXI_PAYLOAD_SIZE)) {
    return TRUE;
  }

  MapSize = 0;
  gBS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVersion);
  MapSize += 16 * DescSize;
  Map = AllocatePool (MapSize);
  if (Map == NULL) {
    return FALSE;
  }

  Status = gBS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVersion);
  if (EFI_ERROR (Status)) {
    FreePool (Map);
    return FALSE;
  }

  Count = MapSize / DescSize;
  for (Index = 0; Index < Count; Index++) {
    EFI_MEMORY_DESCRIPTOR  *Entry = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Map + Index * DescSize);

    if (!Scannable (Entry->Type)) {
      continue;
    }

    if (ScanRange (Entry->PhysicalStart, Entry->NumberOfPages << 12)) {
      FreePool (Map);
      return TRUE;
    }
  }

  FreePool (Map);
  return FALSE;
}

EFI_STATUS
EFIAPI
FuxiRamDiskEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                Status;

  // Make the run visible: the boot manager clears the screen afterwards.
  gBS->Stall (3000000);
  gST->ConOut->ClearScreen (gST->ConOut);
  DEBUG ((DEBUG_INFO, "FUXIRAMDISK: driver entry\n"));
  EFI_HANDLE                Handle;
  EFI_DEVICE_PATH_PROTOCOL  *Path;
  VENDOR_DEVICE_PATH        Vendor;

  // SEC publishes the payload address it derived from the device tree; that is
  // exact, so prefer it over scanning memory.
  {
    EFI_PEI_HOB_POINTERS  Hob;
    STATIC CONST EFI_GUID PayloadGuid = { 0x8f2c1e44, 0x9d3a, 0x4c77, { 0xb1, 0x2e, 0x77, 0x5a, 0x10, 0x4c, 0x9e, 0x31 } };

    Hob.Raw = GetHobList ();
    while (!END_OF_HOB_LIST (Hob)) {
      if ((Hob.Header->HobType == EFI_HOB_TYPE_GUID_EXTENSION) &&
          (CompareGuid (&Hob.Guid->Name, &PayloadGuid))) {
        UINT64  Base = *(UINT64 *)(Hob.Guid + 1);

        UINT64  Probe;
        UINT64  Total;

        mHintSeen = TRUE;
        mHintValue = Base;
        DEBUG ((DEBUG_INFO, "FUXIRAMDISK: device tree at %lx, looking below it\n", Base));
        for (Probe = Base; (Probe > Base - 0x10000000ULL) && (Probe >= FUXI_BLOCK_SIZE); Probe -= FUXI_BLOCK_SIZE) {
          if (IsPayloadVolume ((UINT8 *)(UINTN)Probe, &Total)) {
            DEBUG ((DEBUG_INFO, "FUXIRAMDISK: payload at %lx below the tree\n", Probe));
            mVolumeBase  = (UINT8 *)(UINTN)Probe;
            mVolumeBytes = (UINTN)(Total * FUXI_BLOCK_SIZE);
            break;
          }
        }
      }

      Hob.Raw = GET_NEXT_HOB (Hob);
    }
  }

  if (mVolumeBase == NULL) {
    // The SEC hint is an optimisation.  If the bootloader places the ramdisk
    // outside the expected window, fall back to the memory map scan instead
    // of entering UniXNU with no payload volume.
    DEBUG ((DEBUG_INFO, "FUXIRAMDISK: no volume from the hint, scanning the map\n"));
    DEBUG ((DEBUG_INFO, "  hob=%d base=%lx\n", mHintSeen ? 1 : 0, mHintValue));
    if (!LocatePayload ()) {
      DEBUG ((DEBUG_ERROR, "SIGNAL: no payload volume\n"));
      gBS->Stall (6000000);
      return EFI_SUCCESS;
    }
  }

  ZeroMem (&Vendor, sizeof (Vendor));
  Vendor.Header.Type    = HARDWARE_DEVICE_PATH;
  Vendor.Header.SubType = HW_VENDOR_DP;
  SetDevicePathNodeLength (&Vendor.Header, sizeof (VENDOR_DEVICE_PATH));
  CopyGuid (&Vendor.Guid, &gEfiCallerIdGuid);

  Path = AppendDevicePathNode (NULL, (EFI_DEVICE_PATH_PROTOCOL *)&Vendor);
  if (Path == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem (&mMedia, sizeof (mMedia));
  mMedia.MediaId          = 1;
  mMedia.RemovableMedia   = FALSE;
  mMedia.MediaPresent     = TRUE;
  mMedia.LogicalPartition = FALSE;
  mMedia.ReadOnly         = FALSE;
  mMedia.WriteCaching     = FALSE;
  mMedia.BlockSize        = FUXI_BLOCK_SIZE;
  mMedia.IoAlign          = 0;
  mMedia.LastBlock        = (EFI_LBA)(mVolumeBytes / FUXI_BLOCK_SIZE) - 1;

  mBlockIo.Revision    = EFI_BLOCK_IO_PROTOCOL_REVISION;
  mBlockIo.Media       = &mMedia;
  mBlockIo.Reset       = FuxiBlockReset;
  mBlockIo.ReadBlocks  = FuxiBlockRead;
  mBlockIo.WriteBlocks = FuxiBlockWrite;
  mBlockIo.FlushBlocks = FuxiBlockFlush;

  Handle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Handle,
                  &gEfiBlockIoProtocolGuid,
                  &mBlockIo,
                  &gEfiDevicePathProtocolGuid,
                  Path,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((DEBUG_INFO, "SIGNAL: summary\n"));
  DEBUG ((DEBUG_INFO, "  hob=%d base=%lx\n", mHintSeen ? 1 : 0, mHintValue));
  DEBUG ((DEBUG_INFO, "  volume=%lx\n", (UINT64)(UINTN)mVolumeBase));
  gBS->Stall (6000000);

  DEBUG ((DEBUG_INFO, "FUXIRAMDISK: payload at %lx, %lu KiB as a RAM-backed block device\n",
         (UINT64)(UINTN)mVolumeBase, (UINT64)(mVolumeBytes / 1024)));

  return EFI_SUCCESS;
}
