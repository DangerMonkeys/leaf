"""Validate a closed compact-diagnostics IGC; historical V3 dumps use older tooling."""
import hashlib
import sys
from pathlib import Path


def check(path):
    lines = Path(path).read_bytes().splitlines()
    assert lines[-1].startswith(b"G")
    digest = hashlib.md5(b"".join(line + b"\n" for line in lines[:-1])).hexdigest().upper()
    assert lines[-1][1:].decode() == digest
    assert any(line.startswith(b"LXLVFDCFG V=1 ") for line in lines)
    assert any(line.startswith(b"LXLVSTART V=3 ") for line in lines)
    assert any(line.startswith(b"LXLVEND V=1 ") for line in lines)
    forbidden = (b"LXLVG", b"LXLVSTARTDATA", b"LXLVSTARTALT", b"LXLVSTARTPOS",
                 b"LXLVSTARTPATH", b"LXLVSTARTCFG", b"LXLVSTARTUNITS", b"LXLVSTARTFLAGS")
    assert not any(line.startswith(forbidden) for line in lines)
    events = []
    anchored = False
    for line in lines:
        if line.startswith(b"LXLVFDTIME "):
            anchor = dict(item.split("=", 1) for item in line.decode().split()[1:])
            assert 0 <= int(anchor["T"]) <= 2**32 - 1
            assert len(anchor["UTC"]) == 6 and anchor["UTC"].isdigit()
            anchored = True
        if not line.startswith(b"LXLVFD "):
            continue
        assert anchored
        assert len(line) <= 76
        fields = dict(item.split("=", 1) for item in line.decode().split()[1:])
        assert fields["K"] in {"IMU", "GRAVITY", "KALMAN", "FIFO", "MOTION", "PRESSURE", "WRITE"}
        assert 1 <= int(fields["N"]) <= 65535
        assert 0 <= int(fields["T"]) <= 2**32 - 1
        assert 0 <= int(fields["DT"]) <= 2**32 - 1
        events.append(fields)
    print(f"Verified compact diagnostics, {len(events)} event records and IGC checksum: {path}")


if __name__ == "__main__":
    check(sys.argv[1])
