## fuxiPkg/Include/DXE.inc
## Xiaomi 13 (SM8550) — derived from uefi-sheng shengPkg (branch: sheng).
##
## FUXI_VENDOR_HW = 0: no `Binaries/fuxi/*` entries. The vendor stack
## (SMEM, clocks, RPMh, UFS, USB, charger, PmicDxe) is what turns the panel
## black on sheng; fuxi is untested, so it stays out of the first build.

  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf

  # XNU 加载器必须编进固件卷：从外部卷加载的驱动会被本 build 的镜像校验拒绝。
  # DarwinBoot is the XNU loader; stage 1 only needs the boot manager UI.
  # It is also the driver that walks the handed-over device tree, which the
  # SEC MMU never maps (the tree sits at 0xC5B1D0, outside every
  # MemoryMapLib descriptor), so touching it faults at FAR 0xC5B1D48.
  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF fuxiPkg/Drivers/FuxiRamDiskDxe/FuxiRamDiskDxe.inf
!if $(TARGET) == DEBUG
  INF UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf
!endif
  INF FatPkg/EnhancedFatDxe/Fat.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

  # 板级驱动
  #   FuxiBootMemoryDxe 必须在 DXE Core 起来之后、任何大额分配之前加载，
  #   它会把 DTB 描述的可用 DRAM 补进 GCD。
  # FuxiBootMemoryDxe disabled for bring-up: it derives DRAM 'holes' from the
  # appended tree, which on this board is the Android live FDT (35 reserved
  # ranges, Android /chosen), and the ranges it computes make DXE touch MMIO.
  # That aborted with a second-level translation fault at 0xC5B1D48.
  # MemoryMapLib already gives DXE a 209 MiB heap, which is enough for the menu.
  #INF fuxiPkg/Drivers/FuxiBootMemoryDxe/FuxiBootMemoryDxe.inf

  #   把电源/音量键发布成 SimpleTextIn，并把自己挂进 ConIn；
  #   没有它 UniXNU 菜单能显示、不能导航。
  INF fuxiPkg/Drivers/FuxiKeyInputDxe/FuxiKeyInputDxe.inf

  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
