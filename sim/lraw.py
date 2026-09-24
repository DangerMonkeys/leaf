#!/usr/bin/env python3
"""Inspect Leaf flight tapes and replay their canonical records through the bus-log injector."""

from __future__ import annotations

import argparse
import dataclasses
import socket
import struct
import sys
import time
import zlib
from collections import Counter
from pathlib import Path
from typing import BinaryIO, Iterator

FILE_HEADER_BYTES = 512
CHUNK_BYTES = 4096
FORMAT_VERSION = 1
FILE_HEADER = struct.Struct("<8sHHHH16s32s41s6sBqQII6HHHI356s")
CHUNK_HEADER = struct.Struct("<4sIQHHI")
RECORD_HEADER = struct.Struct("<BBHI")
HEADER_CRC_OFFSET = 152
CHUNK_CRC_OFFSET = 20

RAW_GPS_NMEA = 7
DATA_LOSS = 12
BUS_PRESSURE = 64
BUS_MOTION = 65
BUS_AMBIENT = 66

RECORD_NAMES = {
    1: "raw_baro_pressure",
    2: "raw_baro_temperature",
    3: "raw_imu_accel",
    4: "raw_imu_gyro",
    5: "raw_imu_mag",
    6: "raw_imu_quaternion",
    7: "raw_gps_nmea",
    8: "raw_ambient",
    9: "raw_power",
    10: "time_sync",
    11: "event",
    12: "data_loss",
    64: "bus_pressure",
    65: "bus_motion",
    66: "bus_ambient",
}


class TapeError(RuntimeError):
    pass


@dataclasses.dataclass(frozen=True)
class TapeHeader:
    version: int
    chunk_bytes: int
    hardware: str
    firmware: str
    git_revision: str
    device_mac: str
    start_utc_us: int
    start_monotonic_us: int
    timebase_hz: int
    sensor_mask: int
    baro_calibration: tuple[int, ...]
    imu_rate_hz: int
    baro_rate_hz: int


@dataclasses.dataclass(frozen=True)
class TapeRecord:
    type: int
    flags: int
    timestamp_us: int
    payload: bytes
    chunk_sequence: int


def _cstring(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def _crc_with_zeroed_field(data: bytes, offset: int) -> int:
    mutable = bytearray(data)
    mutable[offset : offset + 4] = b"\0\0\0\0"
    return zlib.crc32(mutable) & 0xFFFFFFFF


def read_header(stream: BinaryIO) -> TapeHeader:
    data = stream.read(FILE_HEADER_BYTES)
    if len(data) != FILE_HEADER_BYTES:
        raise TapeError("truncated flight-tape header")
    values = FILE_HEADER.unpack(data)
    if values[0] != b"LEAFRAW\0":
        raise TapeError("not a Leaf flight tape")
    if values[1] != FORMAT_VERSION:
        raise TapeError(f"unsupported flight-tape version {values[1]}")
    if values[2] != FILE_HEADER_BYTES:
        raise TapeError(f"unsupported header size {values[2]}")
    if values[3] != CHUNK_BYTES:
        raise TapeError(f"unsupported chunk size {values[3]}")
    stored_crc = values[-2]
    calculated_crc = _crc_with_zeroed_field(data, HEADER_CRC_OFFSET)
    if stored_crc != calculated_crc:
        raise TapeError(f"header CRC mismatch: stored={stored_crc:08x} calculated={calculated_crc:08x}")
    return TapeHeader(
        version=values[1],
        chunk_bytes=values[3],
        hardware=_cstring(values[5]),
        firmware=_cstring(values[6]),
        git_revision=_cstring(values[7]),
        device_mac=values[8].hex(":"),
        start_utc_us=values[10],
        start_monotonic_us=values[11],
        timebase_hz=values[12],
        sensor_mask=values[13],
        baro_calibration=tuple(values[14:20]),
        imu_rate_hz=values[20],
        baro_rate_hz=values[21],
    )


def iter_records(stream: BinaryIO) -> Iterator[TapeRecord]:
    expected_sequence = 0
    while True:
        chunk = stream.read(CHUNK_BYTES)
        if not chunk:
            return
        if len(chunk) != CHUNK_BYTES:
            raise TapeError(f"truncated chunk {expected_sequence}: {len(chunk)} bytes")
        magic, sequence, base_us, payload_bytes, record_count, stored_crc = CHUNK_HEADER.unpack_from(chunk)
        if magic != b"LRCH":
            raise TapeError(f"bad chunk magic at sequence {expected_sequence}")
        if sequence != expected_sequence:
            raise TapeError(f"chunk sequence gap: expected {expected_sequence}, got {sequence}")
        calculated_crc = _crc_with_zeroed_field(chunk, CHUNK_CRC_OFFSET)
        if stored_crc != calculated_crc:
            raise TapeError(
                f"chunk {sequence} CRC mismatch: stored={stored_crc:08x} calculated={calculated_crc:08x}"
            )
        payload_end = CHUNK_HEADER.size + payload_bytes
        if payload_end > CHUNK_BYTES:
            raise TapeError(f"chunk {sequence} payload exceeds chunk")
        offset = CHUNK_HEADER.size
        seen = 0
        while offset < payload_end:
            if offset + RECORD_HEADER.size > payload_end:
                raise TapeError(f"chunk {sequence} has a truncated record header")
            record_type, flags, size, delta_us = RECORD_HEADER.unpack_from(chunk, offset)
            offset += RECORD_HEADER.size
            if offset + size > payload_end:
                raise TapeError(f"chunk {sequence} has a truncated record payload")
            yield TapeRecord(record_type, flags, base_us + delta_us, chunk[offset : offset + size], sequence)
            offset += size
            seen += 1
        if seen != record_count:
            raise TapeError(f"chunk {sequence} says {record_count} records but contains {seen}")
        expected_sequence += 1


def open_tape(path: Path) -> tuple[TapeHeader, list[TapeRecord]]:
    with path.open("rb") as stream:
        header = read_header(stream)
        records = list(iter_records(stream))
    return header, records


def bus_line(record: TapeRecord, start_monotonic_us: int) -> str | None:
    elapsed_ms = max(0, (record.timestamp_us - start_monotonic_us) // 1000)
    if record.type == RAW_GPS_NMEA:
        return f"G{elapsed_ms},{record.payload.decode('ascii', errors='replace')}"
    if record.type == BUS_PRESSURE and len(record.payload) == 4:
        (pressure,) = struct.unpack("<i", record.payload)
        return f"P{elapsed_ms},{pressure}"
    if record.type == BUS_AMBIENT and len(record.payload) == 8:
        temperature, humidity = struct.unpack("<ff", record.payload)
        return f"A{elapsed_ms},{temperature:.7g},{humidity:.7g}"
    if record.type == BUS_MOTION and len(record.payload) == 52:
        has_accel, has_orientation, ax, ay, az, qx, qy, qz = struct.unpack("<BB2x6d", record.payload)
        return (
            f"M{elapsed_ms},{'A' if has_accel else 'a'},{ax:.17g},{ay:.17g},{az:.17g},"
            f"{'Q' if has_orientation else 'q'},{qx:.17g},{qy:.17g},{qz:.17g}"
        )
    return None


def bus_lines(header: TapeHeader, records: list[TapeRecord]) -> Iterator[tuple[int, str]]:
    for record in records:
        line = bus_line(record, header.start_monotonic_us)
        if line is not None:
            yield max(0, (record.timestamp_us - header.start_monotonic_us) // 1000), line


def write_buslog(path: Path, header: TapeHeader, records: list[TapeRecord]) -> None:
    with path.open("w", encoding="utf-8", newline="\n") as output:
        output.write(f"V{header.firmware}\n")
        output.write(f"#0,source={header.hardware},format={header.version}\n")
        for _, line in bus_lines(header, records):
            output.write(line + "\n")


def replay_udp(host: str, port: int, speed: float, header: TapeHeader, records: list[TapeRecord]) -> None:
    if speed <= 0:
        raise TapeError("replay speed must be greater than zero")
    address = socket.getaddrinfo(host, port, type=socket.SOCK_DGRAM)[0]
    family, socktype, protocol, _, destination = address
    with socket.socket(family, socktype, protocol) as sock:
        for command in ("!Disconnect sensors", "!Reset reference time"):
            sock.sendto(command.encode(), destination)
        started = time.monotonic()
        try:
            for elapsed_ms, line in bus_lines(header, records):
                target = started + elapsed_ms / 1000.0 / speed
                delay = target - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                sock.sendto(line.encode(), destination)
        finally:
            sock.sendto(b"!Reconnect sensors", destination)


def print_summary(path: Path, header: TapeHeader, records: list[TapeRecord]) -> None:
    counts = Counter(RECORD_NAMES.get(record.type, f"unknown_{record.type}") for record in records)
    duration_us = records[-1].timestamp_us - header.start_monotonic_us if records else 0
    losses = 0
    for record in records:
        if record.type == DATA_LOSS and len(record.payload) == 8:
            losses = max(losses, struct.unpack("<II", record.payload)[0])
    print(f"File: {path}")
    print(f"Format: {header.version}; hardware: {header.hardware}; firmware: {header.firmware}")
    print(f"Device: {header.device_mac}; duration: {duration_us / 1_000_000:.3f}s")
    print(f"Rates: barometer {header.baro_rate_hz}Hz; IMU {header.imu_rate_hz}Hz")
    print(f"Records: {len(records)}; reported losses: {losses}")
    for name, count in sorted(counts.items()):
        print(f"  {name}: {count}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tape", type=Path)
    parser.add_argument("--buslog", type=Path, help="write canonical records as a simulator bus log")
    parser.add_argument("--udp", metavar="HOST", help="replay canonical records to the firmware UDP injector")
    parser.add_argument("--port", type=int, default=7431)
    parser.add_argument("--speed", type=float, default=1.0, help="replay speed multiplier")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        header, records = open_tape(args.tape)
        print_summary(args.tape, header, records)
        if args.buslog:
            write_buslog(args.buslog, header, records)
            print(f"Wrote {args.buslog}")
        if args.udp:
            replay_udp(args.udp, args.port, args.speed, header, records)
    except (OSError, TapeError) as error:
        print(f"lraw: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
