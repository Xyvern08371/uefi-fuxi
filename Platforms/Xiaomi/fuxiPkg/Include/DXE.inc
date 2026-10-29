## fuxiPkg/Include/DXE.inc
## Xiaomi 13 (SM8550) — derived from uefi-sheng shengPkg (branch: sheng).
##
## FUXI_UFS_STACK = 1 loads the nuwa vendor services required by UFSDxe.
## Display, USB device and charger vendor chains remain out of this build.

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

!if $(FUXI_UFS_STACK) == 1
  #
  # UFS 前链：只接 nuwa UFSDxe 需要的 vendor 服务，不接 Display/USB 设备链。
  # 顺序沿用 nuwaPkg/Include/DXE.inc。
  #
  INF Binaries/nuwa/QcomPkg/Drivers/EnvDxe/EnvDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/nuwa/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/nuwa/QcomPkg/Drivers/TzDxe/TzDxeLA.inf
  INF Binaries/nuwa/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ProjectInfoDxe/ProjectInfoDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PmicDxe/PmicDxeLa.inf
!endif

  # XNU 加载器必须编进固件卷：从外部卷加载的驱动会被本 build 的镜像校验拒绝。
  # DarwinBoot is the XNU loader; stage 1 only needs the boot manager UI.
  # It is also the driver that walks the handed-over device tree, which the
  # SEC MMU never maps (the tree sits at 0xC5B1D0, outside every
  # MemoryMapLib descriptor), so touching it faults at FAR 0xC5B1D48.
  INF DarwinBootPkg/Driver/DarwinBoot.inf
  INF fuxiPkg/Drivers/FuxiBootMemoryDxe/FuxiBootMemoryDxe.inf
  INF fuxiPkg/Drivers/FuxiRamDiskDxe/FuxiRamDiskDxe.inf
!if $(TARGET) == DEBUG
  INF UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf
!endif
  INF FatPkg/EnhancedFatDxe/Fat.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf

!if $(FUXI_UFS_STACK) == 1
  INF Binaries/nuwa/QcomPkg/Drivers/SdccDxe/SdccDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
!endif

  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf

  # 板级驱动
  #   FuxiBootMemoryDxe 必须在 DXE Core 起来之后、任何大额分配之前加载。
  #   当前启用它的原因是：sheng 用 DTB 的 /chosen linux,initrd-start/end
  #   直接发布 payload FAT 卷；FuxiRamDiskDxe 的内存盲扫只是兜底。
  #   SEC 现在会保留 handed-over DTB，避免 DarwinBoot 覆盖它。

  #   把电源/音量键发布成 SimpleTextIn，并把自己挂进 ConIn；
  #   没有它 UniXNU 菜单能显示、不能导航。
  INF fuxiPkg/Drivers/FuxiKeyInputDxe/FuxiKeyInputDxe.inf

  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbDeviceDxe/UsbDeviceDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbConfigDxe/UsbConfigDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbInitDxe/UsbInitDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UCDxe/UCDxe.inf
