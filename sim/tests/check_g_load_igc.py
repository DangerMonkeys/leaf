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
    buckets = {}
    sample_bucket = 0
    samples = 0
    anchored = False
    last_fix = None
    climb_values = []

    def add(bucket, g):
        if not bucket or g == 0xFFFF:
            return
        values, disposition = buckets.setdefault(bucket, ([], None))
        assert disposition is None
        assert 0 <= g <= 14000
        values.append(g)

    previous = b""
    for line in lines:
        if line.startswith(b"B"):
            assert last_fix is None, "B record without associated sensor record"
            last_fix = int(line[50:53]) / 10
        elif re.fullmatch(rb"LXLVA[0-9A-F]{8}[0-9]{14}", line):
            anchored = True
        elif line.startswith(b"LXLVS") and not line.startswith(b"LXLVSENSOR") and not line.startswith(b"LXLVSTART"):
            assert re.fullmatch(rb"LXLVS[0-9A-F]{22}", line), line
            assert anchored and last_fix is not None
            sample_bucket = int(line[13:19], 16)
            g = int(line[19:23], 16)
            temperature = int(line[23:26], 16)
            flags = int(line[26:27], 16)
            assert bool(flags & 1) == (g != 0xFFFF)
            assert bool(flags & 2) == (temperature != 0x800)
            if flags & 4:
                assert flags & 1 and sample_bucket
            if flags & 8:
                climb_values.append(last_fix)
            add(sample_bucket, g)
            last_fix = None
            samples += 1
        elif line.startswith(b"LXLVX"):
            assert re.fullmatch(rb"LXLVX[0-9A-F]{12}", line), line
            assert previous.startswith(b"LXLVS") and len(previous) == 27
            add(sample_bucket, int(line[13:17], 16))
        elif line.startswith(b"LXLVP"):
            assert re.fullmatch(rb"LXLVP[0-9A-F]{18}", line), line
            add(int(line[13:19], 16), int(line[19:23], 16))
        elif re.fullmatch(rb"LXLV[CD][0-9A-F]{6}", line):
            bucket = int(line[5:11], 16)
            assert bucket in buckets and buckets[bucket][1] is None
            buckets[bucket] = (buckets[bucket][0], chr(line[4]))
        previous = line
    assert samples and last_fix is None
    assert all(disposition in {"C", "D"} for _, disposition in buckets.values())
    accepted = [g for values, disposition in buckets.values() if disposition == "C" for g in values]
    # Generated simulator tracks have paired logbook entries. Check reconstruction against
    # the actual persisted JSON as well as validating the wire format and checksum.
    card = next((parent for parent in Path(path).parents if parent.name == "card"), None)
    if card:
        matched = False
        for entry in (card / "logbook").rglob("*.json"):
            data = json.loads(entry.read_text())
            if not str(data.get("track", {}).get("path", "")).endswith(Path(path).name):
                continue
            matched = True
            metrics = data["metrics"]
            for key, fn in (("min_accel_g", min), ("max_accel_g", max)):
                value = metrics[key]
                assert (value is not None) == bool(accepted)
                if accepted:
                    assert abs(fn(accepted) / 1000 - value) <= .00051
            if climb_values:
                assert abs(max(0, max(climb_values)) - min(99.9, metrics["max_climb_rate_mps"])) <= .051
                assert abs(min(0, min(climb_values)) - max(-9.9, metrics["max_sink_rate_mps"])) <= .051
        assert matched, "No paired simulator logbook JSON found"
    print(f"Verified {samples} sensor records and {len(buckets)} resolved eligibility buckets")


if __name__ == "__main__":
    check(sys.argv[1])
