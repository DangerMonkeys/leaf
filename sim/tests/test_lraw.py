import io
import struct
import unittest
import zlib

from sim import lraw


def crc_with_zero(data: bytes, offset: int) -> int:
    value = bytearray(data)
    value[offset : offset + 4] = b"\0" * 4
    return zlib.crc32(value) & 0xFFFFFFFF


def fixture() -> bytes:
    header_values = [
        b"LEAFRAW\0",
        1,
        lraw.FILE_HEADER_BYTES,
        lraw.CHUNK_BYTES,
        0,
        b"leaf_3_2_7",
        b"test-firmware",
        b"abc123",
        bytes.fromhex("001122334455"),
        0,
        1_700_000_000_000_000,
        1_000_000,
        1_000_000,
        0x1F,
        1,
        2,
        3,
        4,
        5,
        6,
        18,
        100,
        0,
        b"",
    ]
    header = bytearray(lraw.FILE_HEADER.pack(*header_values))
    struct.pack_into("<I", header, lraw.HEADER_CRC_OFFSET, crc_with_zero(header, lraw.HEADER_CRC_OFFSET))

    records = bytearray()
    pressure = struct.pack("<i", 101325)
    records += lraw.RECORD_HEADER.pack(lraw.BUS_PRESSURE, 0, len(pressure), 5_000) + pressure
    nmea = b"$GNRMC,example*00"
    records += lraw.RECORD_HEADER.pack(lraw.RAW_GPS_NMEA, 0, len(nmea), 6_000) + nmea
    ambient = struct.pack("<ff", 18.5, 47.25)
    records += lraw.RECORD_HEADER.pack(lraw.BUS_AMBIENT, 0, len(ambient), 7_000) + ambient
    motion = struct.pack("<BB2x6d", 1, 1, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6)
    records += lraw.RECORD_HEADER.pack(lraw.BUS_MOTION, 0, len(motion), 8_000) + motion
    chunk = bytearray(b"\xFF" * lraw.CHUNK_BYTES)
    lraw.CHUNK_HEADER.pack_into(chunk, 0, b"LRCH", 0, 1_000_000, len(records), 4, 0)
    chunk[lraw.CHUNK_HEADER.size : lraw.CHUNK_HEADER.size + len(records)] = records
    struct.pack_into("<I", chunk, lraw.CHUNK_CRC_OFFSET, crc_with_zero(chunk, lraw.CHUNK_CRC_OFFSET))
    return bytes(header + chunk)


class FlightTapeTest(unittest.TestCase):
    def test_parses_and_converts_canonical_records(self):
        stream = io.BytesIO(fixture())
        header = lraw.read_header(stream)
        records = list(lraw.iter_records(stream))
        self.assertEqual(header.hardware, "leaf_3_2_7")
        self.assertEqual(header.baro_calibration, (1, 2, 3, 4, 5, 6))
        self.assertEqual(
            [line for _, line in lraw.bus_lines(header, records)],
            [
                "P5,101325",
                "G6,$GNRMC,example*00",
                "A7,18.5,47.25",
                "M8,A,0.10000000000000001,0.20000000000000001,0.29999999999999999,"
                "Q,0.40000000000000002,0.5,0.59999999999999998",
            ],
        )

    def test_rejects_corrupt_chunk(self):
        data = bytearray(fixture())
        data[-1] ^= 1
        stream = io.BytesIO(data)
        lraw.read_header(stream)
        with self.assertRaisesRegex(lraw.TapeError, "CRC mismatch"):
            list(lraw.iter_records(stream))


if __name__ == "__main__":
    unittest.main()
