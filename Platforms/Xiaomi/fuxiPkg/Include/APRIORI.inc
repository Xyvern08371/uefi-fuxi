## fuxiPkg/Include/APRIORI.inc
## Xiaomi 13 (SM8550) — derived from uefi-sheng shengPkg (branch: sheng).
##
## Minimal PEI/DXE dispatch order. With FUXI_UFS_STACK = 1 the nuwa UFS
## vendor chain is loaded; the USB/display/charger vendor chains stay out.
##
## TODO(阶段 2): FuxiBootMemoryDxe must declare 0xb8000000/0x02b00000 as boot
## services memory before SimpleFbDxe runs, otherwise the GOP has no backing.
## sheng does this in ShengBootMemoryDxe; fuxi has no equivalent yet.

APRIORI DXE {
  INF MdeModulePkg/Core/Dxe/DxeMain.inf
  INF MdeModulePkg/Universal/PCD/Dxe/Pcd.inf

  INF MdeModulePkg/Universal/ReportStatusCodeRouter/RuntimeDxe/ReportStatusCodeRouterRuntimeDxe.inf
  INF MdeModulePkg/Universal/StatusCodeHandler/RuntimeDxe/StatusCodeHandlerRuntimeDxe.inf
  INF MdeModulePkg/Core/RuntimeDxe/RuntimeDxe.inf
  INF ArmPkg/Drivers/CpuDxe/CpuDxe.inf
  INF ArmPkg/Drivers/ArmGicDxe/ArmGicDxe.inf
  INF EmbeddedPkg/MetronomeDxe/MetronomeDxe.inf
  INF ArmPkg/Drivers/TimerDxe/TimerDxe.inf

!if $(FUXI_UFS_STACK) == 1
  INF Binaries/nuwa/QcomPkg/Drivers/EnvDxe/EnvDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/SmemDxe/SmemDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/DALSYSDxe/DALSYSDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/HWIODxe/HWIODxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ChipInfoDxe/ChipInfoDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PlatformInfoDxe/PlatformInfoDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ProjectInfoDxe/ProjectInfoDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/HALIOMMUDxe/HALIOMMUDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ULogDxe/ULogDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/IPCCDxe/IPCCDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/GLinkDxe/GLinkDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/CmdDbDxe/CmdDbDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PwrUtilsDxe/PwrUtilsDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/NpaDxe/NpaDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/RpmhDxe/RpmhDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/VcsDxe/VcsDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ClockDxe/ClockDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ICBDxe/ICBDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ShmBridgeDxe/ShmBridgeDxeLA.inf
  INF Binaries/nuwa/QcomPkg/Drivers/TzDxe/ScmDxeCompat.inf
  INF Binaries/nuwa/QcomPkg/Drivers/TLMMDxe/TLMMDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/I2CDxe/I2CDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/SPMIDxe/SPMIDxe.inf
!endif

  INF MdeModulePkg/Universal/ResetSystemRuntimeDxe/ResetSystemRuntimeDxe.inf

  INF MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
  INF MdeModulePkg/Universal/Disk/PartitionDxe/PartitionDxe.inf
  INF MdeModulePkg/Universal/Disk/UnicodeCollation/EnglishDxe/EnglishDxe.inf
  INF FatPkg/EnhancedFatDxe/Fat.inf

!if $(FUXI_UFS_STACK) == 1
  INF Binaries/nuwa/QcomPkg/Drivers/SdccDxe/SdccDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UFSDxe/UFSDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/PmicGlinkDxe/PmicGlinkDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbPwrCtrlDxe/UsbPwrCtrlDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/QcomChargerDxe/QcomChargerDxeLA.inf
  INF Binaries/nuwa/QcomPkg/Drivers/ChargerExDxe/ChargerExDxe.inf
  INF Binaries/nuwa/QcomPkg/Drivers/UsbfnDwc3Dxe/UsbfnDwc3Dxe.inf
!endif

  INF MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf

  # 亮屏路径：上一级（ABL）留在 splash_region 的 framebuffer 继续扫描输出，
  # SimpleFbDxe 在其上发布 GOP。USE_CUSTOM_DISPLAY_DRIVER = 0。
  INF SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf

  INF MdeModulePkg/Universal/WatchdogTimerDxe/WatchdogTimer.inf
  INF MdeModulePkg/Universal/SecurityStubDxe/SecurityStubDxe.inf
  INF EmbeddedPkg/EmbeddedMonotonicCounter/EmbeddedMonotonicCounter.inf
  INF EmbeddedPkg/RealTimeClockRuntimeDxe/RealTimeClockRuntimeDxe.inf
  INF MdeModulePkg/Universal/PrintDxe/PrintDxe.inf
  INF MdeModulePkg/Universal/DevicePathDxe/DevicePathDxe.inf
  INF MdeModulePkg/Universal/CapsuleRuntimeDxe/CapsuleRuntimeDxe.inf
  INF MdeModulePkg/Universal/HiiDatabaseDxe/HiiDatabaseDxe.inf
  INF MdeModulePkg/Universal/BdsDxe/BdsDxe.inf

  INF MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbKbDxe/UsbKbDxe.inf
  INF MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf

  INF EmbeddedPkg/SimpleTextInOutSerial/SimpleTextInOutSerial.inf
  INF MdeModulePkg/Universal/Console/ConPlatformDxe/ConPlatformDxe.inf
  INF MdeModulePkg/Universal/Console/ConSplitterDxe/ConSplitterDxe.inf
  INF MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsoleDxe.inf
}
