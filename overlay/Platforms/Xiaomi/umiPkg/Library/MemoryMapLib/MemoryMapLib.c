// SPDX-License-Identifier: BSD-2-Clause-Patent
#include <Library/MemoryMapLib.h>

// Source: this umi's extracted XBL RawFiles/uefiplat.cfg, SHA-256
// 29affca455090a26b223ebae2c02bf294547bb699f9b65a891af3c186d99c610.
// This is firmware configuration evidence, not a runtime memory map.
// Preserve stock regions, reserving the old FV/SEC/vector/page-table data;
// Keep the original DXE Heap as SEC/PEI's workspace. The separate 512 MiB
// Darwin RAM window below was qualified on this 12 GiB umi in attempt 18:
// matching runtime SMEM v3 and ABL FDT, two full address-pattern passes with
// cache maintenance, plus AllocatePages/read/write/FreePages of 256 MiB.
// This is a bounded device-specific window, not discovery of all installed RAM.
// Display Reserved is split at the observed DFPS boundary. IMEM's cookie
// page has its own descriptor rather than an overlapping MMU descriptor.
STATIC EFI_MEMORY_REGION_DESCRIPTOR gMemoryDescriptor[] = {
  {"IPC SHM", 0x805d0000, 0x20000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"MPSS EFS", 0x80600000, 0x100000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"BOOT INFO", 0x80700000, 0x100000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"AOP CMD DB", 0x80860000, 0x20000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"XBL Log Buffer", 0x80884000, 0x10000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"SMEM", 0x80900000, 0x200000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"TZApps Reserved", 0x82400000, 0x3a00000, HobOnlyNoCacheSetting, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"PIL Reserved", 0x86000000, 0xd200000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"DXE Heap", 0x98900000, 0x3300000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},
  {"Sched Heap", 0x9bc00000, 0x400000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"Display Reserved", 0x9c000000, 0x2300000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
  {"Display DFPS", 0x9e300000, 0x100000, AddMem, MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN},
  {"DBI Dump", 0x9e400000, 0xf00000, NoHob, MMAP_IO, INITIALIZED, Conv, UNCACHED_UNBUFFERED_XN},
  {"FV Region", 0x9f800000, 0x200000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"ABOOT FV", 0x9fa00000, 0x200000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"UEFI FD", 0x9fc00000, 0x300000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK},
  {"SEC Heap", 0x9ff00000, 0x8c000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"CPU Vectors", 0x9ff8c000, 0x1000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK},
  {"MMU PageTables", 0x9ff8d000, 0x3000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  {"UEFI Stack", 0x9ff90000, 0x40000, AddMem, SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK_XN},
  {"Log Buffer", 0x9fff7000, 0x8000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"Info Blk", 0x9ffff000, 0x1000, AddMem, SYS_MEM, SYS_MEM_CAP, RtData, WRITE_BACK_XN},
  {"Kernel", 0xa0000000, 0x10000000, AddMem, SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN},
  // Linux ramoops_memreserve=4M, confirmed in boot log and fs/pstore/ram.c.
  // Console is the first 2 MiB; pmsg is the second. Neither is allocatable.
  {"Ramoops", 0xb0000000, 0x400000, AddMem, MEM_RES, UNCACHEABLE, Reserv, UNCACHED_UNBUFFERED_XN},
  {"Darwin RAM", 0xc0000000, 0x20000000, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},
  // This 12 GiB umi: FDT banks cover 4..9 GiB without a gap; their fixed
  // reservations and all dynamic allocation ranges are below 4 GiB.
  // Recovery /proc/iomem independently reports this range as System RAM.
  // Keep the low qualified window for input images and allocate the 4.75 GiB
  // XNU destination in this separate 5 GiB run. First hardware use: attempt 47.
  {"Darwin High RAM", 0x100000000ULL, 0x140000000ULL, AddMem, SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN},
  {"IMEM Base", 0x14680000, 0x3f000, NoHob, MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
  {"IMEM Cookie Base", 0x146bf000, 0x1000, AddDev, MMAP_IO, INITIALIZED, Conv, NS_DEVICE},
  {"IPC_ROUTER_TOP", 0x400000, 0x100000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"SECURITY CONTROL", 0x780000, 0x7000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"QUPV3_2_GSI", 0x800000, 0xd0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"QUPV3_0_GSI", 0x900000, 0xd0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"QUPV3_1_GSI", 0xa00000, 0xd0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"PRNG_CFG_PRNG", 0x790000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"CRYPTO0 CRYPTO", 0x1dc0000, 0x40000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"TCSR_TCSR_REGS", 0x1fc0000, 0x30000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"GPU_GMU_CX_BLK", 0x2c7d000, 0x2000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"GPU_CC", 0x2c90000, 0xa000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"QUPV3_SSC_GSI", 0x5a00000, 0xd0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"PERIPH_SS", 0x8800000, 0x200000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"USB30_PRIM", 0xa600000, 0x11b000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"USB_RUMI", 0xa720000, 0x10000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"USB30_SEC", 0xa800000, 0x11b000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"AOSS", 0xb000000, 0x4000000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"TLMM_WEST", 0xf100000, 0x300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"TLMM_SOUTH", 0xf500000, 0x300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"TLMM_NORTH", 0xf900000, 0x300000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"SMMU", 0x15000000, 0xd0000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
  {"APSS_HM", 0x17800000, 0xd981000, AddDev, MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE},
};

VOID
GetMemoryMap (
  OUT EFI_MEMORY_REGION_DESCRIPTOR **MemoryDescriptor,
  OUT UINT8                         *MemoryDescriptorCount)
{
  *MemoryDescriptor      = gMemoryDescriptor;
  *MemoryDescriptorCount = ARRAY_SIZE (gMemoryDescriptor);
}
