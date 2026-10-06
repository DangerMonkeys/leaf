"""Check an IGC from the g-load-diagnostics scenario/script, including its GPS outage."""

import sys
from pathlib import Path


def check(path):
    lines = Path(path).read_text().splitlines()
    assert any(line.startswith("LXLFGLCFG V=1 ") for line in lines)
    groups = {}
    for line in lines:
        if not line.startswith(("LXLFGL ", "LXLFGA ", "LXLFGS ")):
            continue
        assert len(line) <= 76, line
        kind, *fields = line[4:].split()
        values = dict(field.split("=", 1) for field in fields)
        tick = int(values["T"])
        group = groups.setdefault(tick, {})
        assert kind not in group, (tick, kind)
        group[kind] = values

    assert len(groups) > 100, "Expected over 100 once-per-second diagnostic groups"
    ticks = sorted(groups)
    assert all(b - a == 1000 for a, b in zip(ticks, ticks[1:])), ticks
    air, ground = False, 0
    saw_landing = saw_relaunch = saw_outage = saw_committed = saw_values = False
    for tick in ticks:
        group = groups[tick]
        assert set(group) == {"GL", "GA", "GS"}, (tick, group)
        inputs, accel, state = (group[kind] for kind in ("GL", "GA", "GS"))
        look = int(inputs["L"])
        assert look == (int(inputs["FS"]) | int(inputs["VC"]))
        assert int(inputs["VC"]) == (int(inputs["CV"]) and abs(int(inputs["C"])) > 50)
        if not int(inputs["SV"]):
            assert inputs["S"] == "NA" and inputs["FS"] == "0"
            saw_outage = True
        if look:
            if saw_landing and not air:
                saw_relaunch = True
                assert state["I"] == "15000"
            air, ground = True, 0
        elif air:
            ground += 1
            if ground >= 5:
                air = False
                saw_landing = True
        assert int(state["ON"]) == 1
        assert int(state["A"]) == air and int(state["G"]) == ground, (tick, state)
        if not air:
            assert state["P"] == "0" and state["I"] == "0"
        saw_committed |= state["E"] == "1"
        saw_values |= accel["RV"] == accel["FV"] == "1" and accel["R"] != "NA" and accel["F"] != "NA"
    assert all((saw_landing, saw_relaunch, saw_outage, saw_committed, saw_values))
    print(f"Verified {len(groups)} complete 1 Hz groups, landing/relaunch, GPS outage and g values")
    for line in lines:
        if line.startswith(("LXLFGL ", "LXLFGA ", "LXLFGS ")):
            print(line)
            if line.startswith("LXLFGS "):
                break


if __name__ == "__main__":
    check(sys.argv[1])
