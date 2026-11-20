"""Validate preserved shutdown evidence. --live checks the current source/build too."""
import csv
import hashlib
import json
import sys
from datetime import datetime
from pathlib import Path

E = Path(__file__).resolve().parent
read = lambda name: json.loads((E / name).read_text(encoding="utf-8"))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
def files(name):
    return {x["path"].replace(chr(92), "/"): (x["bytes"], x["sha256"]) for x in read(name)["files"]}
assert files("02-card-before-card.json") == files("27-originals-restored-card.json")
assert read("27-originals-restored-card.json")["free_after"] == 8503296
checked = 0
for manifest in E.glob("*-card.json"):
    label = manifest.name.removesuffix("-card.json")
    for f in json.loads(manifest.read_text(encoding="utf-8"))["files"]:
        path = E / "card-captures" / label / f["path"]
        assert path.stat().st_size == f["bytes"] and sha(path) == f["sha256"]
        checked += 1
for label, producer in (("13-power-off-result", "power-off"), ("20-second-cycle-result", "wake-cycle")):
    result = read(label + ".json")
    assert result["final"]["power"] == "OffUSB" and result["final"]["page"] == "Charging"
    host = read(producer + "-producer.json")
    assert not host["injection_disabled_by_host"] and host["packets_sent"] > 0
    assert datetime.fromisoformat(host["started_utc"]) < datetime.fromisoformat(result["utc"]) < datetime.fromisoformat(host["stopped_utc"])
assert read("18-injection-resumed.json")["requests"][0]["body"]["udp_sensor_injection"]
assert read("25-final-charging.json")["final"]["power"] == "OffUSB"
assert any(x["reply"] == "config 1 0 0 0 0 0 0 0" for x in read("25-final-charging.json")["steps"])
assert read("23-session-restored.json")["requests"][0]["body"]["udp_sensor_injection"] is False
capture = E / "card-captures/14-writable-results"
rows = list(csv.DictReader((capture / "diagnostics/stack_1.csv").open()))
assert len(rows) == 4 and rows[0]["elapsed_ms"] == "203373" and rows[0]["min_unused_bytes"] == "7868"
assert "CAUSE=POWER_OFF" in (capture / "tracks/2026-11-19-XLV-000-03.igc").read_text(encoding="ascii")
assert all(json.loads(f.read_text())["complete"] for f in (capture / "logbook").glob("*.json"))
validated = read("igc-validation.json")
assert len(validated) == 3
for record in validated:
    assert record["output"] == "STATUS_IGC_PASSED" and sha(E / record["path"]) == record["sha256"]
log = (E / "shutdown-fix-sim-passed.log").read_text(encoding="utf-8")
for marker in ("Unserviced/repeated shutdown requests retain the original deadline", "A stuck admitted publisher forced guarded recovery", "Deferred shutdown released the bus", "Thermal stop, auto-stop", "Checked storage:", "Leaf Log sync regression checks passed"):
    assert marker in log, marker
if "--live" in sys.argv:
    root = E.parents[3]
    candidate = read("candidate.json")
    for name, digest in candidate["source_changes"].items():
        assert sha(root / name) == digest, name
    for name, entry in candidate["artifacts"].items():
        assert sha(root / ".pio/build/leaf_3_2_8_bench" / name) == entry["sha256"]
print(f"PASS: two replay-active shutdowns, retained wake opt-in, three validated IGCs, Stack CSV, original settings/files and {checked} archived file hashes.")
