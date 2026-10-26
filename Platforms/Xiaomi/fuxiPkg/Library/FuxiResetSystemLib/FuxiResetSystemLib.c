/** @file
  Fuxi reset support.

  Normal reset/shutdown uses PSCI. Recovery and bootloader reboots write the
  PMIC SDAM reboot-reason cell through the SPMI arbiter before PSCI reset.
  The cell is the same one the stock ABL reads (SM8550 PMK8550 SDAM 0x7100,
  restart@0x48, bits 1..7): recovery = 1, bootloader = 2.

  Copyright (c) 2026, UniXNU contributors. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>

#include <IndustryStandard/ArmStdSmc.h>

#include <Library/ArmMonitorLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/ResetSystemLib.h>

#define FUXI_SPMI_CORE    0xC400000ULL
#define FUXI_SPMI_CNFG    0xC42D000ULL
#define FUXI_SPMI_OBSRVR  0xC440000ULL
#define FUXI_SPMI_CHNLS   0xC500000ULL

#define FUXI_SPMI_EE      0

#define FUXI_SPMI_VERSION_V7_MIN  0x70000000U
#define FUXI_SPMI_VERSION_V8_MIN  0x80000000U
#define FUXI_SPMI_FEATURES        0x04
#define FUXI_SPMI_FEATURES_APIDS_MASK  0x7FFU
#define FUXI_SPMI_APID_MAP        0x2000
#define FUXI_SPMI_CNFG_OWNER      0x00
#define FUXI_SPMI_OBS_EE_STRIDE   0x8000
#define FUXI_SPMI_OBS_APID_STRIDE 0x20
#define FUXI_SPMI_CHNL_APID_STRIDE 0x1000

#define FUXI_SPMI_CMD             0x00
#define FUXI_SPMI_STATUS          0x08
#define FUXI_SPMI_WDATA0          0x10
#define FUXI_SPMI_RDATA0          0x18

#define FUXI_SPMI_OP_EXT_WRITEL   0
#define FUXI_SPMI_OP_EXT_READL    1
#define FUXI_SPMI_STATUS_DONE     BIT0
#define FUXI_SPMI_STATUS_ERRORS   0x0EU

#define FUXI_SDAM_PPID            0x071
#define FUXI_REBOOT_CELL          0x48
#define FUXI_REBOOT_MASK          0xFE
#define FUXI_REBOOT_SHIFT         1
#define FUXI_REBOOT_RECOVERY      1
#define FUXI_REBOOT_BOOTLOADER    2
#define FUXI_SPMI_POLLS           100000

STATIC
UINT32
FuxiSpmiRead (
  IN UINT64  Base,
  IN UINTN   Offset
  )
{
  return MmioRead32 ((UINTN)(Base + Offset));
}

STATIC
VOID
FuxiSpmiWrite (
  IN UINT64  Base,
  IN UINTN   Offset,
  IN UINT32  Value
  )
{
  MmioWrite32 ((UINTN)(Base + Offset), Value);
}

STATIC
UINT32
FuxiSpmiCommand (
  IN UINT8  Op,
  IN UINT8  ByteAddress,
  IN UINT8  ByteCount
  )
{
  return ((UINT32)Op << 27) | ((UINT32)ByteAddress << 4) | ((UINT32)ByteCount & 7U);
}

STATIC
EFI_STATUS
FuxiSpmiFindApid (
  OUT UINT32  *Version,
  OUT UINT32  *ApidCount,
  OUT INT32   *Apid,
  OUT INT32   *Owner
  )
{
  UINT32  Index;

  *Version   = FuxiSpmiRead (FUXI_SPMI_CORE, 0);
  *ApidCount = FuxiSpmiRead (FUXI_SPMI_CORE, FUXI_SPMI_FEATURES) & FUXI_SPMI_FEATURES_APIDS_MASK;
  *Apid      = -1;
  *Owner     = -1;

  if ((*Version < FUXI_SPMI_VERSION_V7_MIN) || (*Version >= FUXI_SPMI_VERSION_V8_MIN)) {
    return EFI_UNSUPPORTED;
  }

  if ((*ApidCount == 0) || (*ApidCount > 1024)) {
    return EFI_DEVICE_ERROR;
  }

  for (Index = 0; Index < *ApidCount; Index++) {
    UINT32  Map = FuxiSpmiRead (FUXI_SPMI_CORE, FUXI_SPMI_APID_MAP + (4 * Index));
    INT32   CurrentOwner;

    if ((Map == 0) || (((Map >> 8) & 0xFFFU) != FUXI_SDAM_PPID)) {
      continue;
    }

    CurrentOwner = (INT32)(FuxiSpmiRead (FUXI_SPMI_CNFG, FUXI_SPMI_CNFG_OWNER + (4 * Index)) & 7U);
    if ((*Apid < 0) || (CurrentOwner == FUXI_SPMI_EE)) {
      *Apid  = (INT32)Index;
      *Owner = CurrentOwner;
    }
  }

  return (*Apid < 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

STATIC
EFI_STATUS
FuxiSpmiWait (
  IN UINT64  Base,
  IN UINTN   Offset
  )
{
  UINTN  Index;

  for (Index = 0; Index < FUXI_SPMI_POLLS; Index++) {
    UINT32  Status = FuxiSpmiRead (Base, Offset + FUXI_SPMI_STATUS);

    if ((Status & FUXI_SPMI_STATUS_DONE) != 0) {
      return ((Status & FUXI_SPMI_STATUS_ERRORS) != 0) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
    }
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
FuxiSdamRead8 (
  IN  INT32  Apid,
  IN  UINT8  ByteAddress,
  OUT UINT8  *Value
  )
{
  UINT64      Channel;
  EFI_STATUS  Status;

  Channel = FUXI_SPMI_OBSRVR + (FUXI_SPMI_OBS_EE_STRIDE * FUXI_SPMI_EE) + (FUXI_SPMI_OBS_APID_STRIDE * (UINT32)Apid);
  FuxiSpmiWrite (Channel, FUXI_SPMI_CMD, FuxiSpmiCommand (FUXI_SPMI_OP_EXT_READL, ByteAddress, 0));
  Status = FuxiSpmiWait (Channel, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Value = (UINT8)FuxiSpmiRead (Channel, FUXI_SPMI_RDATA0);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FuxiSdamWrite8 (
  IN INT32  Apid,
  IN UINT8  ByteAddress,
  IN UINT8  Value
  )
{
  UINT64  Channel;

  Channel = FUXI_SPMI_CHNLS + (FUXI_SPMI_CHNL_APID_STRIDE * (UINT32)Apid);
  FuxiSpmiWrite (Channel, FUXI_SPMI_WDATA0, Value);
  FuxiSpmiWrite (Channel, FUXI_SPMI_CMD, FuxiSpmiCommand (FUXI_SPMI_OP_EXT_WRITEL, ByteAddress, 0));
  return FuxiSpmiWait (Channel, 0);
}

STATIC
EFI_STATUS
FuxiSetRebootReason (
  IN UINT8  Mode
  )
{
  UINT32      Version;
  UINT32      ApidCount;
  INT32       Apid;
  INT32       Owner;
  UINT8       Before;
  UINT8       After;
  UINT8       Next;
  EFI_STATUS  Status;

  Status = FuxiSpmiFindApid (&Version, &ApidCount, &Apid, &Owner);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot find SDAM APID: %r\n", __FUNCTION__, Status));
    return Status;
  }

  if (Owner != FUXI_SPMI_EE) {
    DEBUG ((DEBUG_ERROR, "%a: SDAM APID %d is owned by EE %d\n", __FUNCTION__, Apid, Owner));
    return EFI_ACCESS_DENIED;
  }

  Status = FuxiSdamRead8 (Apid, FUXI_REBOOT_CELL, &Before);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot read reboot cell: %r\n", __FUNCTION__, Status));
    return Status;
  }

  Next = (UINT8)((Before & ~FUXI_REBOOT_MASK) | (Mode << FUXI_REBOOT_SHIFT));
  Status = FuxiSdamWrite8 (Apid, FUXI_REBOOT_CELL, Next);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot write reboot cell: %r\n", __FUNCTION__, Status));
    return Status;
  }

  Status = FuxiSdamRead8 (Apid, FUXI_REBOOT_CELL, &After);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: cannot verify reboot cell: %r\n", __FUNCTION__, Status));
    return Status;
  }

  if (After != Next) {
    DEBUG ((DEBUG_ERROR, "%a: reboot cell verify failed (%02x != %02x)\n", __FUNCTION__, After, Next));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
FuxiPsciReset (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_RESET;
  ArmMonitorCall (&Args);
}

/**
  This function causes a system-wide reset (cold reset).
**/
VOID
EFIAPI
ResetCold (
  VOID
  )
{
  FuxiPsciReset ();
}

/**
  This function causes a system-wide initialization (warm reset).
**/
VOID
EFIAPI
ResetWarm (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  Args.Arg0 = ARM_SMC_ID_PSCI_FEATURES;
  Args.Arg1 = ARM_SMC_ID_PSCI_SYSTEM_RESET2_AARCH64;
  ArmMonitorCall (&Args);
  if (Args.Arg0 == ARM_SMC_PSCI_RET_SUCCESS) {
    Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_RESET2_AARCH64;
    Args.Arg1 = 0;
    ArmMonitorCall (&Args);
  } else {
    ResetCold ();
  }
}

/**
  This function causes the system to power off.
**/
VOID
EFIAPI
ResetShutdown (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;

  Args.Arg0 = ARM_SMC_ID_PSCI_SYSTEM_OFF;
  ArmMonitorCall (&Args);
}

/**
  Recovery/bootloader are carried as an ASCII reason in ResetData.
**/
VOID
EFIAPI
ResetPlatformSpecific (
  IN UINTN  DataSize,
  IN VOID   *ResetData
  )
{
  CHAR8  *Reason;
  UINTN  Length;
  UINT8  Mode;

  if ((ResetData == NULL) || (DataSize == 0)) {
    ResetCold ();
    return;
  }

  Reason = (CHAR8 *)ResetData;
  Length = AsciiStrnLenS (Reason, DataSize);
  if ((Length == 8) && (AsciiStrnCmp (Reason, "recovery", 8) == 0)) {
    Mode = FUXI_REBOOT_RECOVERY;
  } else if ((Length == 10) && (AsciiStrnCmp (Reason, "bootloader", 10) == 0)) {
    Mode = FUXI_REBOOT_BOOTLOADER;
  } else {
    DEBUG ((DEBUG_WARN, "%a: unknown reset reason '%a', falling back to cold reset\n", __FUNCTION__, Reason));
    ResetCold ();
    return;
  }

  if (EFI_ERROR (FuxiSetRebootReason (Mode))) {
    ResetCold ();
    return;
  }

  FuxiPsciReset ();
}

/**
  Dispatch reset requests to the matching implementation.
**/
VOID
EFIAPI
ResetSystem (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  switch (ResetType) {
    case EfiResetWarm:
      ResetWarm ();
      break;

    case EfiResetCold:
      ResetCold ();
      break;

    case EfiResetShutdown:
      ResetShutdown ();
      return;

    case EfiResetPlatformSpecific:
      ResetPlatformSpecific (DataSize, ResetData);
      return;

    default:
      return;
  }
}
