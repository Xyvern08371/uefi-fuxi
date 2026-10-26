##
#  fuxiPkg — Xiaomi 13 (SM8550 / kalama)
#
#  Derived from UniXNU/uefi-sheng shengPkg/sheng.dsc (branch: sheng).
#  参考基线：Mu-Silicium 的 Platforms/Xiaomi/nuwaPkg（小米 13 Pro，同 SM8550）。
#
#  STATUS: 骨架（scaffold）。除 dsc 本身外，以下文件尚未建立：
#    - fuxiPkg/fuxi.fdf
#    - fuxiPkg/DeviceBuild.py
#    - fuxiPkg/Library/MemoryMapLib/  (必须按 device-facts.md 第 6 节重写)
#    - fuxiPkg/Library/ConfigurationMapLib/
#    - fuxiPkg/Include/{APRIORI,DXE,RAW}.inc
#    - fuxiPkg/FdtBlob/sm8550-xiaomi-fuxi.dtb
#  在该目录可用前，本文件不能参与构建。
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
##

################################################################################
#
# Defines Section
#
################################################################################
[Defines]
  PLATFORM_NAME                  = fuxi
  PLATFORM_GUID                  = 6F1C8E42-3D57-4B0A-9C21-8E4D5A7B1F30
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/fuxiPkg
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = RELEASE|DEBUG
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = fuxiPkg/fuxi.fdf

  #
  # 与 sheng 首启相同的取舍：0 = 只用 Mu 驱动。
  # 此时上一级（ABL）留在 splash_region 的 framebuffer 继续扫描输出，
  # SimpleFbDxe 直接在其上发布 GOP，UniXNU 菜单可见，不加载任何 vendor 驱动。
  #
  # fuxi 的 stock DT 已自带 /reserved-memory/splash_region
  #   reg = <0x0 0xb8000000 0x0 0x2b00000>
  # 因此不像 sheng 那样需要在 board.toml 里补 dtb_node。
  #
  # 1 = 加载 stock vendor 驱动（SMEM/时钟/RPMh/UFS/USB/充电…）。
  # sheng 上的经验是这条链会把面板点黑，fuxi 未知，留作后续阶段。
  #
  USE_CUSTOM_DISPLAY_DRIVER      = 0
  FUXI_VENDOR_HW                 = 0

  #
  # 1 = load only the vendor stack needed for UFS, without Display/USB/charger.
  # UFSDxe is a stock nuwa binary; this keeps the panel on the ABL scan-out
  # path while still publishing a UFS BlockIo for FuxiUfsLogDxe.
  #
  FUXI_UFS_STACK                 = 1

  #
  # 0 = SM8550-AB
  # 1 = SM8550-AC
  # fuxi (2211133C) 为 SM8550-AB
  #
  SOC_TYPE                       = 0

!include KailuaPkg/KailuaPkg.dsc.inc

!if ($(USE_CUSTOM_DISPLAY_DRIVER) == 1) && ($(FUXI_VENDOR_HW) == 0)
  !error The stock DisplayDxe chain needs FUXI_VENDOR_HW = 1
!endif

[PcdsFixedAtBuild]
  #
  # DDR Memory — 实测：fuxi live FDT 的 memory 基址为 0x80000000
  #
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x80000000

  #
  # UEFI Stack
  # TODO: 0xA760D000 是 sheng/nuwa 的值，fuxi 需按 MemoryMapLib 实测重新落位。
  #
  gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0xA760D000
  gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000

  #
  # SMBIOS
  #
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemManufacturer|"Xiaomi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemModel|"13"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailModel|"fuxi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemRetailSku|"13_fuxi"
  gSiliciumPkgTokenSpaceGuid.PcdSmbiosSystemBoardModel|"13"

  #
  # UniXNU menu plugins
  #
  gUniXNUPkgTokenSpaceGuid.PcdUniXNUDeviceModel|"fuxi"

  #
  # Simple Frame Buffer
  # 面板 m3_38_0c_0a，1080x2400，DSC command mode。
  # 宽度必须与 ABL 的 scan-out stride 一致，否则画面错位。
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1080
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2400
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
  #
  # 1080/8/2 = 67 列，2400/16/2 = 75 行；sheng 用 3（3048 宽）。
  #
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferFontScale|2
  gSiliciumPkgTokenSpaceGuid.PcdFrameBufferDelay|30000

  #
  # Platform PEI
  #
  gQcomPkgTokenSpaceGuid.PcdPlatformType|"LA"
!if ($(FUXI_VENDOR_HW) == 0) && ($(FUXI_UFS_STACK) == 0)
  # 沿用 sheng 的规避：PlatformPeim 会去扫 stock UEFI 残留的 FD 窗口并调用其
  # scheduler protocol，在没有 vendor 驱动读这些 HOB 时会随机卡死。
  gQcomPkgTokenSpaceGuid.PcdBuildXblHobs|FALSE
!else
  gQcomPkgTokenSpaceGuid.PcdBuildXblHobs|TRUE
!endif

  #
  # Advanced Logger
  # DXE-only 配置：DXE Core 从保留内存分配日志缓冲区，不依赖 SEC。
  #
!if $(TARGET) == DEBUG
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerPages|64
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerHdwPortDebugPrintErrorLevel|0xFFFFFFFF
!endif

[PcdsFeatureFlag]
!if $(TARGET) == DEBUG
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerFixedInRAM|FALSE
  gAdvLoggerPkgTokenSpaceGuid.PcdAdvancedLoggerLocator|FALSE
!endif

[LibraryClasses]
  #
  # Memory Libraries
  #
  MemoryMapLib|fuxiPkg/Library/MemoryMapLib/MemoryMapLib.inf
  ConfigurationMapLib|fuxiPkg/Library/ConfigurationMapLib/ConfigurationMapLib.inf

  #
  # Restart / Recovery / Bootloader
  #
  ResetSystemLib|fuxiPkg/Library/FuxiResetSystemLib/FuxiResetSystemLib.inf
  HwResetSystemLib|fuxiPkg/Library/FuxiResetSystemLib/FuxiResetSystemLib.inf

  UniXNULib|UniXNUPkg/Library/UniXNULib/UniXNULib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf

  AdvancedLoggerAccessLib|AdvLoggerPkg/Library/AdvancedLoggerAccessLib/AdvancedLoggerAccessLib.inf
  AdvancedLogViewLib|UniXNUPkg/Library/AdvancedLogViewLib/AdvancedLogViewLib.inf
  QrEncoderLib|MsGraphicsPkg/Library/QrEncoderLib/QrEncoderLib.inf

!if $(TARGET) == DEBUG
  AdvancedLoggerHdwPortLib|AdvLoggerPkg/Library/AdvancedLoggerHdwPortLib/AdvancedLoggerHdwPortLib.inf

[LibraryClasses.common.DXE_CORE]
  DebugLib|AdvLoggerPkg/Library/BaseDebugLibAdvancedLogger/BaseDebugLibAdvancedLogger.inf
  AssertLib|AdvLoggerPkg/Library/AssertLib/AssertLib.inf
  AdvancedLoggerLib|AdvLoggerPkg/Library/AdvancedLoggerLib/DxeCore/AdvancedLoggerLib.inf

[LibraryClasses.common.DXE_DRIVER, LibraryClasses.common.UEFI_DRIVER, LibraryClasses.common.UEFI_APPLICATION]
  DebugLib|AdvLoggerPkg/Library/BaseDebugLibAdvancedLogger/BaseDebugLibAdvancedLogger.inf
  AssertLib|AdvLoggerPkg/Library/AssertLib/AssertLib.inf
  AdvancedLoggerLib|AdvLoggerPkg/Library/AdvancedLoggerLib/Dxe/AdvancedLoggerLib.inf

[LibraryClasses.common.DXE_RUNTIME_DRIVER]
  DebugLib|AdvLoggerPkg/Library/BaseDebugLibAdvancedLogger/BaseDebugLibAdvancedLogger.inf
  AssertLib|AdvLoggerPkg/Library/AssertLib/AssertLib.inf
  AdvancedLoggerLib|AdvLoggerPkg/Library/AdvancedLoggerLib/Runtime/AdvancedLoggerLib.inf
!endif

[Components]
  #
  # UniXNU 启动管理器
  #
  UniXNUPkg/Application/UniXNU/UniXNU.inf
  UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf

  #
  # DarwinBoot（本阶段不加载内核，保留以便第二阶段的 FV 结构一致）
  #
  # disabled for stage 1, see Include/DXE.inc
  DarwinBootPkg/Driver/DarwinBoot.inf
  fuxiPkg/Drivers/FuxiRamDiskDxe/FuxiRamDiskDxe.inf
  fuxiPkg/Drivers/FuxiUfsLogDxe/FuxiUfsLogDxe.inf
  fuxiPkg/Drivers/FuxiUefiLogDxe/FuxiUefiLogDxe.inf
!if $(TARGET) == DEBUG
  UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf
!endif
  FatPkg/EnhancedFatDxe/Fat.inf

  #
  # 板级驱动
  #
  #   FuxiBootMemoryDxe：把 DTB 里 /memory 描述的、GCD 还不认识的 DRAM 减去
  #   /reserved-memory 后加入系统内存，并把 /chosen 的 initrd 发布成只读 BlockIo。
  #   没有它，DXE 只有 fuxiPkg MemoryMapLib 那几条固定区可用。
  #
  #   FuxiKeyInputDxe：电源/音量键 → SimpleTextIn，菜单导航必需。
  #
  # disabled for bring-up, see Include/DXE.inc
  #fuxiPkg/Drivers/FuxiBootMemoryDxe/FuxiBootMemoryDxe.inf
  fuxiPkg/Drivers/FuxiKeyInputDxe/FuxiKeyInputDxe.inf
