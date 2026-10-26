// SPDX-License-Identifier: BSD-3-Clause
//
// fuxiPkg/Library/MemoryMapLib — Xiaomi 13 (SM8550 / kalama)
//
// Source of truth: `uefiplatLA.cfg`, extracted from this device's
// `xbl_config_a` partition (HyperOS OS3.0.319.0.WPCCNXM). The file carries
// `MaxMemoryRegions = 74` and a `[MemoryMap]` section; the 28 regions below
// are that section verbatim. Unlike sheng's map, nothing here is reconstructed
// from Linux DTS reservations — it is the vendor's own firmware table.
//
// The UEFI allocation block is identical to sheng's at 0xA7000000, confirming
// it is a platform-wide SM8550 convention rather than a per-board choice:
//
//   0xA7000000  1 MiB  FD Reserved
//   0xA7400000  2 MiB  UEFI FD Reserved
//   0xA7100000  3 MiB  UEFI FD          <- FD_BASE / FD_SIZE
//   0xA7600000  4 KiB  CPU Vectors
//   0xA7601000  ...    Info Blk / MMU PageTables / Log Buffer
//   0xA760D000  ...    UEFI Stack   (matches fuxi.dsc PcdCPUCoresStackBase)
//
// This file covers 0x80000000..0x100000000 plus the high DRAM banks the live
// FDT /memory node exposes: 0x8C0000000 + 3 GiB and 0x980000000 + 6 GiB.
// DarwinBoot needs one contiguous conventional run for the owned window, and
// the 4 GiB request is satisfied by the 6 GiB bank at 0x980000000. The
// 0x880000000 + 925 MiB bank is left out on purpose: sheng hit a synchronous
// external abort while clearing around 0x8BB300000 in that bank, and fuxi's
// entry spans the same address.
//
// SPDX header above; original table from Qualcomm's uefiplatLA.cfg.
#include <Library/MemoryMapLib.h>

STATIC EFI_MEMORY_REGION_DESCRIPTOR gMemoryDescriptor[] = {
  // The general-purpose pool must come first: SEC allocates the FV-image
  // decompression buffer from the TCB before it writes DXE heap HOBs, and
  // an allocator that hands out the FD region would overwrite the very data
  // it is decoding (that surfaced as 'Decompress Failed - Invalid Parameter').
  {"DXE Heap",          0xBAB00000, 0x09500000, AddMem,   SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

  // ---- firmware-owned reservations ----
  {"NOMAP",             0x80000000, 0x01200000, HobOnlyNoCacheSetting,    MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"RSRV0",             0x81200000, 0x00800000, AddMem,   SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"XBL DT",            0x81A00000, 0x00040000, AddMem,   SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"XBL Ramdump",       0x81A40000, 0x001C0000, AddMem,   MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"AOP",               0x81C00000, 0x000A0000, AddMem,   MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"SMEM",              0x81D00000, 0x00200000, AddMem,   MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"RSRV1",             0x86E00000, 0x02A00000, AddMem,   MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"PIL Reserved",      0x89800000, 0x19280000, AddMem,   MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"Display Demura",    0xA2A80000, 0x02B00000, AddMem,   MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
  {"RSRV2",             0xA5580000, 0x00B80000, AddMem,   MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"DBI Dump",          0xA6100000, 0x00F00000, NoHob,    MMAP_IO, INITIALIZED, Conv,  UNCACHED_UNBUFFERED_XN},
  {"TZApps Reserved",   0xD8800000, 0x08A00000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"RSRV3",             0xEEB00000, 0x04B00000, AddMem,   MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"NOMAP",             0xFF800000, 0x00800000, HobOnlyNoCacheSetting,    MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},

  // ---- UEFI allocation block ----
  {"FD Reserved",       0xA7000000, 0x00100000, AddMem,   SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"UEFI FD",           0xA7100000, 0x00300000, AddMem,   SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"UEFI FD Reserved",  0xA7400000, 0x00200000, AddMem,   SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"CPU Vectors",       0xA7600000, 0x00001000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"Info Blk",          0xA7601000, 0x00001000, AddMem,   SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"MMU PageTables",    0xA7602000, 0x00003000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"Log Buffer",        0xA7605000, 0x00008000, AddMem,   SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"UEFI Stack",        0xA760D000, 0x00040000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"SEC Heap",          0xA764D000, 0x0008C000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"Sched Heap",        0xA76D9000, 0x00400000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"FV Region",         0xA7AD9000, 0x00400000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"UEFI RESV",         0xA7ED9000, 0x00127000, AddMem,   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},

  // ---- BootShim relocation window ----
  // 0xA8000000..0xB8000000 is where BootShim relocates the firmware volume and
  // decompresses the next stage. Keep it reserved so the DXE allocator never
  // hands it out.
  {"BootShim Window",   0xA8000000, 0x10000000, AddMem,   SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},

  // ---- DarwinBoot kernel window ----
  // DarwinBoot needs one contiguous 256 MiB run (WINDOW_SIZE = SIZE_256MB,
  // aligned to 2 MiB, plus headroom for the kernel's 0x7c000 base offset).
  // The DXE heap above is too fragmented by the time it runs, so hand this
  // 320 MiB DRAM gap over as conventional memory and keep it after the heap:
  // AllocatePages (AllocateAddress) lands here. Ends at 0xD8000000, clear of
  // TZApps Reserved at 0xD8800000.
  {"DarwinBoot Window", 0xC4000000, 0x14000000, AddMem,   SYS_MEM, SYS_MEM_CAP, Conv,   WRITE_BACK_XN},

  // ---- high DRAM banks (live FDT /memory) ----
  // Both are category-SDRAM usable RAM. They are mapped as conventional so the
  // DXE allocator and DarwinBoot can use them; DarwinBoot's 4 GiB owned window
  // lands in the 6 GiB bank at 0x980000000.
  {"DRAM bank 0x8C0000000", 0x8C0000000, 0xC0000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},
  {"DRAM bank 0x980000000", 0x980000000, 0x180000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},

  // ---- the ABL frame buffer fuxi keeps scanning out ----
  // Identical to /reserved-memory/splash_region in the live device tree, and to
  // sheng's hand-patched dtb_node. SimpleFbDxe publishes a GOP on top of it.
  {"Display Reserved",  0xB8000000, 0x02B00000, AddMem,   MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},

  // ---- 149 MiB of general-purpose RAM for DXE ----
  // ---- MMIO register regions ----
  // uefiplatLA.cfg only lists DDR in [MemoryMap]; the register apertures live
  // in a separate section and have to be mapped here, or every device driver
  // takes a translation fault on its first register read. FuxiKeyInputDxe hit
  // exactly that: SPMI sits at 0x0C400000, inside the AOSS aperture.
  {"IPC_ROUTER_TOP",    0x00400000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"SECURITY CONTROL",  0x00780000, 0x00007000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"QUP",               0x00800000, 0x00300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"PRNG_CFG_PRNG",     0x010C0000, 0x0000C000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"CRYPTO0 CRYPTO",    0x01DC0000, 0x00040000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"CORE_TOP_CSR",      0x01F00000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"PERIPH_SS",         0x08800000, 0x00100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"USB",               0x0A600000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"AOSS",              0x0B000000, 0x04000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"TLMM",              0x0F000000, 0x01000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"SMMU",              0x15000000, 0x00200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"APSS_HM",           0x17000000, 0x02000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"IMEM Base",         0x14680000, 0x0002A000, NoHob,  MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},
  {"IMEM Cookie Base",  0x146AA000, 0x00016000, AddDev, MMAP_IO, INITIALIZED, Conv,   NS_DEVICE},
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
