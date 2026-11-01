"""Validate a closed compact-diagnostics IGC; historical V3 dumps use older tooling."""
import hashlib
import json
import re
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
    if b"LXLVSENSOR V=1 G=MG T=DECI_C HEX=1" in lines:
        check_sensors(lines, path)
    print(f"Verified compact diagnostics, {len(events)} event records and IGC checksum: {path}")


def check_sensors(lines, path):
    assert not any(line.startswith((b"LXLVX", b"LXLVP", b"LXLVC", b"LXLVD")) for line in lines)
    g_values = []
    samples = 0
    anchored = False
    pending_fix = False
    climb_values = []

    for line in lines:
        if line.startswith(b"B"):
            assert not pending_fix, "B record without associated sensor record"
            pending_fix = True
            climb_values.append(int(line[50:53]) / 10)
        elif re.fullmatch(rb"LXLVA[0-9A-F]{8}[0-9]{14}", line):
            anchored = True
        elif line.startswith(b"LXLVS") and not line.startswith(b"LXLVSENSOR") and not line.startswith(b"LXLVSTART"):
            assert re.fullmatch(rb"LXLVS[0-9A-F]{15}", line), line
            assert anchored
            g = int(line[13:17], 16)
            temperature = int(line[17:20], 16)
            assert g == 0xFFFF or 0 <= g <= 14000
            assert 0 <= temperature <= 4095
            if g != 0xFFFF:
                g_values.append(g)
            pending_fix = False
            samples += 1
    assert samples and not pending_fix
    # Generated simulator tracks have paired logbook entries. Check reconstruction against
    # the actual persisted JSON as well as validating the wire format and checksum.
    # IGC now leaves flight-window classification to the reader: handling/landing peaks
    # can exceed the device extrema, but every device extreme must still be represented.
    card = next((parent for parent in Path(path).parents if parent.name == "card"), None)
    if card:
        matched = False
        for entry in (card / "logbook").rglob("*.json"):
            data = json.loads(entry.read_text())
            if not str(data.get("track", {}).get("path", "")).endswith(Path(path).name):
                continue
            matched = True
            metrics = data["metrics"]
            for key in ("min_accel_g", "max_accel_g"):
                value = metrics[key]
                if value is not None:
                    assert any(abs(g / 1000 - value) <= .00051 for g in g_values)
            if climb_values:
                assert abs(max(0, max(climb_values)) - min(99.9, metrics["max_climb_rate_mps"])) <= .051
                assert abs(min(0, min(climb_values)) - max(-9.9, metrics["max_sink_rate_mps"])) <= .051
        assert matched, "No paired simulator logbook JSON found"
    print(f"Verified {samples} independent sensor records without eligibility metadata")


if __name__ == "__main__":
    check(sys.argv[1])
