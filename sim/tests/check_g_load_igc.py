"""Check the V3 g-load scenario: shared launch replay, intervals and sensor outage.

This replays detector checks, not the full-rate g filter. Groups are ordered, since the
final stop group may share a device timestamp with the preceding periodic check.
"""
import sys
from pathlib import Path


def check(path):
    lines = Path(path).read_text().splitlines()
    config = next(line for line in lines if line.startswith("LXLVGLCFG V=3 "))
    cfg = dict(field.split("=", 1) for field in config.split()[1:])
    kinds = {"GL", "GA", "GS", "GQ", "GX", "GR", "GF", "GC", "GP", "GE", "GD"}
    groups = []
    for line in lines:
        if not line.startswith("LXLV"):
            continue
        kind, *fields = line[4:].split()
        if kind not in kinds:
            continue
        assert len(line) <= 76, line
        values = dict(field.split("=", 1) for field in fields)
        if kind == "GL":
            groups.append({})
        group = groups[-1]
        assert kind not in group, (kind, line)
        group[kind] = values
    assert len(groups) > 100
    air = False
    ground = 0
    saw = set()
    previous = None
    for group in groups:
        assert set(group) == kinds, group
        inputs, accel, state, quality = (group[k] for k in ("GL", "GA", "GS", "GQ"))
        tick = int(inputs["T"])
        assert all(int(v["T"]) == tick for v in group.values())
        if previous is not None and not int(inputs["Z"]):
            assert 900 <= (tick - previous) % 2**32 <= 1200
        previous = tick
        look = int(inputs["L"])
        assert look == (int(inputs["FS"]) | int(inputs["VC"]))
        assert int(inputs["VC"]) == (int(inputs["CV"]) and abs(int(inputs["C"])) > 50)
        if not int(inputs["SV"]):
            assert inputs["S"] == "NA" and inputs["FS"] == "0"
            saw.add("outage")
        if int(inputs["Z"]):
            air = False
            ground = 0
            assert state["ON"] == "0" and state["D"] == "5"
            saw.add("stop")
        elif not air:
            if int(group["GX"]["J"]):
                air = True
                ground = 0
                assert state["I"] == cfg["IGNORE"] and state["D"] == "1"
                if "landing" in saw:
                    saw.add("relaunch")
        elif not look and not (int(inputs["SV"]) and int(inputs["CV"])):
            ground = 0
            assert state["D"] == "3"
        elif look:
            ground = 0
        else:
            if air:
                ground += 1
                if ground >= int(cfg["LAND"]):
                    air = False
                    saw.add("landing")
        assert int(state["A"]) == air and int(state["G"]) == ground, (tick, state)
        assert int(state["H"]) == 0  # Reserved; launch already includes confirmation.
        if not air:
            assert state["P"] == "0" and state["I"] == "0"
        if state["E"] == "1":
            saw.add("committed")
        if accel["FV"] == "1":
            assert (tick - int(accel["FT"])) % 2**32 <= 500
            saw.add("values")
        else:
            assert accel["F"] == "NA"
            saw.add("stale")
        for kind, count in (("GR", "N"), ("GF", "F")):
            r = group[kind]
            assert (r["LO"] == "NA") == (int(quality[count]) == 0)
        for kind in ("GR", "GF", "GC", "GP", "GE", "GD"):
            r = group[kind]
            assert (r["LO"] == "NA") == (r["HI"] == "NA")
            if r["LO"] != "NA":
                assert 0 <= float(r["LO"]) <= float(r["HI"]) <= 14
        if group["GD"]["LO"] != "NA":
            saw.add("discard")
        if int(quality["GP"]):
            saw.add("gap")
            assert int(group["GX"]["RM"]) & 256
            assert int(group["GX"]["DT"]) > 500
    assert {"landing", "relaunch", "outage", "committed", "values", "stale", "gap", "discard"} <= saw, saw
    print(f"Verified {len(groups)} complete V3 groups, shared launch, freshness, gaps and ranges")


if __name__ == "__main__":
    check(sys.argv[1])
