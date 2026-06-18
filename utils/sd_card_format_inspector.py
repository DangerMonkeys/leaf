#!/usr/bin/env python3
r"""Read-only SD card FAT32 layout inspector for Windows PhysicalDrive devices.

Run PowerShell as Administrator, then:

    python utils\sd_card_format_inspector.py --disk 2

The script does not write to the card. It reads the MBR, chooses the first FAT
partition by default, decodes the FAT boot sector, and checks FAT32 FSInfo and
backup boot-sector metadata.
"""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass


SECTOR_SIZE = 512
MBR_PARTITION_TABLE_OFFSET = 446
MBR_PARTITION_ENTRY_SIZE = 16
MBR_PARTITION_ENTRY_COUNT = 4
FAT_PARTITION_TYPES = {
    0x01: "FAT12",
    0x04: "FAT16 <32M",
    0x06: "FAT16",
    0x0B: "FAT32 CHS",
    0x0C: "FAT32 LBA",
    0x0E: "FAT16 LBA",
}


def u8(data: bytes, offset: int) -> int:
    return data[offset]


def u16(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 2], "little")


def u32(data: bytes, offset: int) -> int:
    return int.from_bytes(data[offset : offset + 4], "little")


def ascii_field(data: bytes, offset: int, length: int) -> str:
    return data[offset : offset + length].decode("ascii", errors="replace").rstrip()


def format_hex(value: int, width: int = 2) -> str:
    return f"0x{value:0{width}X}"


def print_field(name: str, value: object) -> None:
    print(f"{name:<34} {value}")


@dataclass
class PartitionEntry:
    index: int
    status: int
    partition_type: int
    lba_start: int
    sector_count: int

    @property
    def type_name(self) -> str:
        return FAT_PARTITION_TYPES.get(self.partition_type, "Unknown")

    @property
    def is_empty(self) -> bool:
        return self.partition_type == 0 and self.lba_start == 0 and self.sector_count == 0

    @property
    def is_fat(self) -> bool:
        return self.partition_type in FAT_PARTITION_TYPES


@dataclass
class FatBootSector:
    raw: bytes
    partition_start: int

    @property
    def jump(self) -> str:
        return self.raw[0:3].hex(" ")

    @property
    def oem_name(self) -> str:
        return ascii_field(self.raw, 3, 8)

    @property
    def bytes_per_sector(self) -> int:
        return u16(self.raw, 0x0B)

    @property
    def sectors_per_cluster(self) -> int:
        return u8(self.raw, 0x0D)

    @property
    def reserved_sectors(self) -> int:
        return u16(self.raw, 0x0E)

    @property
    def fat_count(self) -> int:
        return u8(self.raw, 0x10)

    @property
    def root_entry_count(self) -> int:
        return u16(self.raw, 0x11)

    @property
    def total_sectors(self) -> int:
        total16 = u16(self.raw, 0x13)
        return total16 if total16 else u32(self.raw, 0x20)

    @property
    def media_descriptor(self) -> int:
        return u8(self.raw, 0x15)

    @property
    def fat_size_sectors(self) -> int:
        fat16 = u16(self.raw, 0x16)
        return fat16 if fat16 else u32(self.raw, 0x24)

    @property
    def sectors_per_track(self) -> int:
        return u16(self.raw, 0x18)

    @property
    def head_count(self) -> int:
        return u16(self.raw, 0x1A)

    @property
    def hidden_sectors(self) -> int:
        return u32(self.raw, 0x1C)

    @property
    def ext_flags(self) -> int:
        return u16(self.raw, 0x28)

    @property
    def fs_version(self) -> int:
        return u16(self.raw, 0x2A)

    @property
    def root_cluster(self) -> int:
        return u32(self.raw, 0x2C)

    @property
    def fsinfo_sector(self) -> int:
        return u16(self.raw, 0x30)

    @property
    def backup_boot_sector(self) -> int:
        return u16(self.raw, 0x32)

    @property
    def drive_number(self) -> int:
        return u8(self.raw, 0x40)

    @property
    def boot_signature(self) -> int:
        return u8(self.raw, 0x42)

    @property
    def volume_id(self) -> int:
        return u32(self.raw, 0x43)

    @property
    def volume_label(self) -> str:
        return ascii_field(self.raw, 0x47, 11)

    @property
    def filesystem_type(self) -> str:
        return ascii_field(self.raw, 0x52, 8)

    @property
    def has_boot_sector_signature(self) -> bool:
        return self.raw[510:512] == b"\x55\xaa"

    @property
    def cluster_size_bytes(self) -> int:
        return self.bytes_per_sector * self.sectors_per_cluster

    @property
    def fat1_start(self) -> int:
        return self.partition_start + self.reserved_sectors

    @property
    def fat2_start(self) -> int | None:
        if self.fat_count < 2:
            return None
        return self.fat1_start + self.fat_size_sectors

    @property
    def data_start(self) -> int:
        return self.partition_start + self.reserved_sectors + self.fat_count * self.fat_size_sectors

    @property
    def data_sectors(self) -> int:
        return self.total_sectors - self.reserved_sectors - self.fat_count * self.fat_size_sectors

    @property
    def data_clusters(self) -> int:
        if self.sectors_per_cluster == 0:
            return 0
        return self.data_sectors // self.sectors_per_cluster


def read_sector(device, sector: int, count: int = 1) -> bytes:
    device.seek(sector * SECTOR_SIZE)
    data = device.read(count * SECTOR_SIZE)
    expected = count * SECTOR_SIZE
    if len(data) != expected:
        raise OSError(f"Expected {expected} bytes at sector {sector}, got {len(data)}")
    return data


def parse_mbr(mbr: bytes) -> list[PartitionEntry]:
    partitions: list[PartitionEntry] = []
    for index in range(MBR_PARTITION_ENTRY_COUNT):
        offset = MBR_PARTITION_TABLE_OFFSET + index * MBR_PARTITION_ENTRY_SIZE
        entry = mbr[offset : offset + MBR_PARTITION_ENTRY_SIZE]
        partitions.append(
            PartitionEntry(
                index=index + 1,
                status=u8(entry, 0),
                partition_type=u8(entry, 4),
                lba_start=u32(entry, 8),
                sector_count=u32(entry, 12),
            )
        )
    return partitions


def choose_partition(partitions: list[PartitionEntry], requested_index: int | None) -> PartitionEntry | None:
    if requested_index is not None:
        matches = [partition for partition in partitions if partition.index == requested_index]
        return matches[0] if matches else None

    for partition in partitions:
        if partition.is_fat and partition.sector_count:
            return partition

    for partition in partitions:
        if not partition.is_empty:
            return partition

    return None


def print_mbr(mbr: bytes, partitions: list[PartitionEntry]) -> None:
    print("MBR")
    print_field("Boot signature", "valid 0x55AA" if mbr[510:512] == b"\x55\xaa" else "INVALID")
    print_field("Disk signature", format_hex(u32(mbr, 440), 8))
    print()
    print("Partition table")
    for partition in partitions:
        if partition.is_empty:
            print(f"  {partition.index}: empty")
            continue
        print(
            "  "
            f"{partition.index}: "
            f"status={format_hex(partition.status)} "
            f"type={format_hex(partition.partition_type)} ({partition.type_name}) "
            f"start={partition.lba_start} "
            f"sectors={partition.sector_count} "
            f"size_bytes={partition.sector_count * SECTOR_SIZE}"
        )
    print()


def print_boot_sector(boot: FatBootSector, partition_sector_count: int | None) -> None:
    print("FAT boot sector")
    print_field("Boot sector signature", "valid 0x55AA" if boot.has_boot_sector_signature else "INVALID")
    print_field("Jump instruction", boot.jump)
    print_field("OEM name", repr(boot.oem_name))
    print_field("Filesystem type string", repr(boot.filesystem_type))
    print_field("Volume label", repr(boot.volume_label))
    print_field("Volume serial", format_hex(boot.volume_id, 8))
    print_field("Bytes per sector", boot.bytes_per_sector)
    print_field("Sectors per cluster", boot.sectors_per_cluster)
    print_field("Cluster size bytes", boot.cluster_size_bytes)
    print_field("Reserved sectors", boot.reserved_sectors)
    print_field("Number of FATs", boot.fat_count)
    print_field("Sectors per FAT", boot.fat_size_sectors)
    print_field("Total sectors in filesystem", boot.total_sectors)
    if partition_sector_count is not None:
        delta = partition_sector_count - boot.total_sectors
        print_field("Partition sectors", partition_sector_count)
        print_field("Partition minus filesystem", delta)
    print_field("Media descriptor", format_hex(boot.media_descriptor))
    print_field("Root entry count", boot.root_entry_count)
    print_field("Root cluster", boot.root_cluster)
    print_field("Hidden sectors", boot.hidden_sectors)
    print_field("FSInfo sector rel", boot.fsinfo_sector)
    print_field("Backup boot sector rel", boot.backup_boot_sector)
    print_field("Ext flags", format_hex(boot.ext_flags, 4))
    print_field("FS version", format_hex(boot.fs_version, 4))
    print_field("Drive number", format_hex(boot.drive_number))
    print_field("Boot signature byte", format_hex(boot.boot_signature))
    print_field("Sectors per track", boot.sectors_per_track)
    print_field("Heads", boot.head_count)
    print()


def print_derived_layout(boot: FatBootSector) -> None:
    print("Derived layout")
    print_field("Partition start sector", boot.partition_start)
    print_field("FAT1 start sector", boot.fat1_start)
    print_field("FAT2 start sector", boot.fat2_start if boot.fat2_start is not None else "n/a")
    print_field("Data start sector", boot.data_start)
    print_field("Data sectors", boot.data_sectors)
    print_field("Data clusters", boot.data_clusters)
    print_field("Approx FAT type by clusters", "FAT32" if boot.data_clusters >= 65525 else "FAT12/16 range")
    print()


def print_fsinfo(device, boot: FatBootSector) -> None:
    print("FAT32 FSInfo")
    if boot.fsinfo_sector in (0, 0xFFFF):
        print("  No usable FSInfo sector pointer in boot sector.")
        print()
        return

    sector = boot.partition_start + boot.fsinfo_sector
    fsinfo = read_sector(device, sector)
    print_field("FSInfo raw sector", sector)
    print_field("Lead signature", format_hex(u32(fsinfo, 0), 8))
    print_field("Struct signature", format_hex(u32(fsinfo, 484), 8))
    print_field("Trail signature", format_hex(u32(fsinfo, 508), 8))
    print_field("Free cluster hint", u32(fsinfo, 488))
    print_field("Next free cluster hint", u32(fsinfo, 492))
    valid = (
        u32(fsinfo, 0) == 0x41615252
        and u32(fsinfo, 484) == 0x61417272
        and u32(fsinfo, 508) == 0xAA550000
    )
    print_field("FSInfo signatures valid", valid)
    print()


def print_backup_boot_sector(device, boot: FatBootSector) -> None:
    print("Backup boot sector")
    if boot.backup_boot_sector in (0, 0xFFFF):
        print("  No usable backup boot-sector pointer in boot sector.")
        print()
        return

    sector = boot.partition_start + boot.backup_boot_sector
    backup = read_sector(device, sector)
    print_field("Backup raw sector", sector)
    print_field("Backup signature", "valid 0x55AA" if backup[510:512] == b"\x55\xaa" else "INVALID")

    ignored_offsets = set(range(0x43, 0x47)) | set(range(0x47, 0x52))
    mismatches = [
        index
        for index, (primary_byte, backup_byte) in enumerate(zip(boot.raw, backup))
        if primary_byte != backup_byte and index not in ignored_offsets
    ]
    print_field("Matches primary boot sector", not mismatches)
    if mismatches:
        preview = ", ".join(format_hex(index, 4) for index in mismatches[:12])
        if len(mismatches) > 12:
            preview += ", ..."
        print_field("Differing byte offsets", preview)
    print()


def print_first_fat_entries(device, boot: FatBootSector) -> None:
    print("First FAT entries")
    fat = read_sector(device, boot.fat1_start)
    entries = [u32(fat, offset) for offset in range(0, 16, 4)]
    for index, value in enumerate(entries):
        print_field(f"FAT[{index}]", format_hex(value, 8))
    print()


def main() -> int:
    parser = argparse.ArgumentParser(description="Inspect MBR/FAT32 parameters from a Windows raw disk.")
    parser.add_argument("--disk", type=int, required=True, help="Windows disk number, e.g. 2 for \\\\.\\PhysicalDrive2")
    parser.add_argument("--partition", type=int, help="MBR partition number to inspect. Defaults to first FAT partition.")
    parser.add_argument(
        "--boot-sector",
        type=int,
        help="Override boot sector LBA. Useful for superfloppy cards or manual probing.",
    )
    args = parser.parse_args()

    path = rf"\\.\PhysicalDrive{args.disk}"
    try:
        with open(path, "rb") as device:
            print(f"Device: {path}")
            print()

            mbr = read_sector(device, 0)
            partitions = parse_mbr(mbr)
            print_mbr(mbr, partitions)

            selected = choose_partition(partitions, args.partition)
            if args.boot_sector is not None:
                partition_start = args.boot_sector
                partition_sector_count = None
                print(f"Inspecting explicit boot sector {partition_start}")
                print()
            elif selected is not None:
                partition_start = selected.lba_start
                partition_sector_count = selected.sector_count
                print(f"Inspecting partition {selected.index} at sector {partition_start}")
                print()
            else:
                partition_start = 0
                partition_sector_count = None
                print("No partition found; inspecting sector 0 as a superfloppy boot sector.")
                print()

            boot = FatBootSector(read_sector(device, partition_start), partition_start)
            print_boot_sector(boot, partition_sector_count)
            print_derived_layout(boot)
            print_fsinfo(device, boot)
            print_backup_boot_sector(device, boot)
            print_first_fat_entries(device, boot)

            if boot.hidden_sectors != partition_start:
                print("WARNING: Hidden sectors does not match partition start sector.")
            if boot.bytes_per_sector != SECTOR_SIZE:
                print(f"WARNING: Script assumes 512-byte sectors; boot sector reports {boot.bytes_per_sector}.")

    except PermissionError:
        print(
            f"Permission denied opening {path}. Run PowerShell as Administrator and try again.",
            file=sys.stderr,
        )
        return 1
    except OSError as exc:
        print(f"Failed reading {path}: {exc}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
