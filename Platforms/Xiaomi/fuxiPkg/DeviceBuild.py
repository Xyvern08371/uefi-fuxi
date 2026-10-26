##
# fuxiPkg build settings — Xiaomi 13 (SM8550 / kalama)
# Derived from UniXNU/uefi-sheng shengPkg/DeviceBuild.py (branch: sheng).
#
# Copyright (c) Microsoft Corporation.
# SPDX-License-Identifier: BSD-2-Clause-Patent
##

import logging
import os

from edk2toolext.environment import shell_environment
from edk2toolext.environment.uefi_build import UefiBuilder
from edk2toolext.invocables.edk2_platform_build import BuildSettingsManager
from edk2toolext.invocables.edk2_pr_eval import PrEvalSettingsManager
from edk2toolext.invocables.edk2_setup import (RequiredSubmodule, SetupSettingsManager)
from edk2toolext.invocables.edk2_update import UpdateSettingsManager
from edk2toolext.invocables.edk2_parse import ParseSettingsManager


class CommonPlatform():
    PackagesSupported = ("fuxiPkg",)
    ArchSupported = ("AARCH64")
    TargetsSupported = ("DEBUG", "RELEASE")
    Scopes = ('fuxi', 'gcc_aarch64_linux', 'edk2-build')
    WorkspaceRoot = os.path.dirname(os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__)))))

    # UniXNU supplies two packages that edk2 has to see by name:
    #   package/            -> UniXNUPkg
    #   drivers/DarwinBoot  -> DarwinBootPkg
    # shengPkg lists only the first, which is why this platform never built in
    # uefi-sheng's own CI: Status.md marks sheng as a bring-up candidate, not an
    # active device, so build_active_devices.sh never touched it.
    UNIXNU = os.environ.get("UNIXNU_ROOT",
                            os.path.join(WorkspaceRoot, "..", "UniXNU"))

    PackagesPath = (
        os.path.join(UNIXNU, "package"),
        os.path.join(UNIXNU, "drivers", "DarwinBoot"),
        "Platforms/Xiaomi",
        "Common/Mu",
        "Common/Mu_OEM_Sample",
        "Mu_Basecore",
        "Silicon/Qualcomm",
        "Silicon/Silicium",
        "Silicium-ACPI",
        "Silicium-ACPI/Platforms/Xiaomi",
        "Silicium-ACPI/Silicon/Qualcomm",
    )


class SettingsManager(UpdateSettingsManager, SetupSettingsManager,
                      PrEvalSettingsManager, ParseSettingsManager):

    def GetPackagesSupported(self):
        return CommonPlatform.PackagesSupported

    def GetArchitecturesSupported(self):
        return CommonPlatform.ArchSupported

    def GetTargetsSupported(self):
        return CommonPlatform.TargetsSupported

    def GetRequiredSubmodules(self):
        # FUXI_UFS_STACK consumes Binaries/nuwa vendor DXE binaries.
        return [
            RequiredSubmodule("Binaries", True),
            RequiredSubmodule("Common/Mu", True),
            RequiredSubmodule("Common/Mu_OEM_Sample", True),
            RequiredSubmodule("Mu_Basecore", True),
            RequiredSubmodule("Silicium-ACPI", True),
            RequiredSubmodule("Silicon/Silicium/OpensslPkg/Library/OpensslLib/openssl", True),
        ]

    def SetArchitectures(self, list_of_requested_architectures):
        unsupported = set(list_of_requested_architectures) - set(self.GetArchitecturesSupported())
        if unsupported:
            raise Exception("Unsupported Architecture Requested: " + " ".join(unsupported))
        self.ActualArchitectures = list_of_requested_architectures

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def FilterPackagesToTest(self, changedFilesList, potentialPackagesList):
        return []

    def GetPlatformDscAndConfig(self):
        return ("fuxiPkg/fuxi.dsc", {})

    def GetName(self):
        return "fuxi"

    def GetPackagesPath(self):
        return CommonPlatform.PackagesPath


class PlatformBuilder(UefiBuilder, BuildSettingsManager):
    def __init__(self):
        UefiBuilder.__init__(self)

    def AddCommandLineOptions(self, parserObj):
        parserObj.add_argument('-a', "--arch", dest="build_arch", type=str,
                               default="AARCH64",
                               help="AARCH64 is the only valid option for this platform.")

    def RetrieveCommandLineOptions(self, args):
        if args.build_arch.upper() != "AARCH64":
            raise Exception("Invalid Arch Specified.")

    def GetWorkspaceRoot(self):
        return CommonPlatform.WorkspaceRoot

    def GetPackagesPath(self):
        result = [shell_environment.GetBuildVars().GetValue("FEATURE_CONFIG_PATH", "")]
        result.extend(CommonPlatform.PackagesPath)
        return result

    def GetActiveScopes(self):
        return CommonPlatform.Scopes

    def GetName(self):
        return "fuxiPkg"

    def GetLoggingLevel(self, loggerType):
        return logging.INFO

    def SetPlatformEnv(self):
        # FD_BASE / FD_SIZE / FD_BLOCKS come from the environment.
        #
        # Confirmed against this device's own firmware: the `uefiplatLA.cfg`
        # inside xbl_config_a declares
        #     0xA7000000, 0x00400000, "UEFI FD"
        # so fuxi uses the full 4 MiB window at 0xA7000000. sheng instead puts a
        # 3 MiB image at 0xA7100000 inside the same window; do not copy its
        # numbers.
        self.env.SetValue("PRODUCT_NAME", "fuxi", "Platform Hardcoded")
        self.env.SetValue("ACTIVE_PLATFORM", "fuxiPkg/fuxi.dsc", "Platform Hardcoded")
        self.env.SetValue("TARGET_ARCH", "AARCH64", "Platform Hardcoded")
        self.env.SetValue("TOOL_CHAIN_TAG", "CLANGPDB", "default")
        self.env.SetValue("EMPTY_DRIVE", "FALSE", "Default to false")
        self.env.SetValue("RUN_TESTS", "FALSE", "Default to false")
        self.env.SetValue("SHUTDOWN_AFTER_RUN", "FALSE", "Default to false")
        self.env.SetValue("BLD_*_BUILDID_STRING", "fuxi-bringup", "Default")
        self.env.SetValue("BUILDREPORTING", "TRUE", "Enabling build report")
        self.env.SetValue("BUILDREPORT_TYPES",
                          "PCD DEPEX FLASH BUILD_FLAGS LIBRARY FIXED_ADDRESS HASH",
                          "Setting build report types")
        self.env.SetValue("BLD_*_MEMORY_PROTECTION", "TRUE", "Default")
        self.env.SetValue("BLD_*_SHIP_MODE", "FALSE", "Default")
        self.env.SetValue("BLD_*_ENABLE_SECUREBOOT", self.env.GetValue("ENABLE_SECUREBOOT"), "Default")
        self.env.SetValue("BLD_*_DEVICE_MODEL", self.env.GetValue("DEVICE_MODEL"), "Default")
        self.env.SetValue("BLD_*_FD_BASE", self.env.GetValue("FD_BASE"), "Default")
        self.env.SetValue("BLD_*_FD_SIZE", self.env.GetValue("FD_SIZE"), "Default")
        self.env.SetValue("BLD_*_FD_BLOCKS", self.env.GetValue("FD_BLOCKS"), "Default")
        return 0

    def PlatformPreBuild(self):
        return 0

    def PlatformPostBuild(self):
        return 0

    def FlashRomImage(self):
        return 0


if __name__ == "__main__":
    import argparse
    import sys

    from edk2toolext.invocables.edk2_platform_build import Edk2PlatformBuild
    from edk2toolext.invocables.edk2_setup import Edk2PlatformSetup
    from edk2toolext.invocables.edk2_update import Edk2Update

    SCRIPT_PATH = os.path.relpath(__file__)
    parser = argparse.ArgumentParser(add_help=False)
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--update", "--UPDATE", action='store_true')
    group.add_argument("--setup", "--SETUP", action='store_true')
    args, remaining = parser.parse_known_args()

    sys.argv = ["stuart", "-c", SCRIPT_PATH] + remaining
    if args.setup:
        Edk2PlatformSetup().Invoke()
    elif args.update:
        Edk2Update().Invoke()
    else:
        Edk2PlatformBuild().Invoke()
