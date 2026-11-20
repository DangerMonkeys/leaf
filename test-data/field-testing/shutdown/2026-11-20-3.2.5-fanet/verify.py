"""Verify preserved live-radio shutdown evidence; --live checks the local flashed artifacts."""
import csv
import hashlib
import json
import sys
from pathlib import Path

E = Path(__file__).resolve().parent
read = lambda name: json.loads((E / name).read_text(encoding="utf-8"))
sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
checked = 0
for manifest in E.glob("*-card.json"):
    label = manifest.name.removesuffix("-card.json")
    for item in json.loads(manifest.read_text())["files"]:
        path = E / "card-captures" / label / item["path"]
        assert sha(path) == item["sha256"] and path.stat().st_size == item["bytes"]
        checked += 1
first = {x["path"]: x["sha256"] for x in read("09-first-shutdown-card.json")["files"]}
last = {x["path"]: x["sha256"] for x in read("22-final-results-card.json")["files"]}
assert all(last[name] == digest for name, digest in first.items())
assert read("08-live-radio-shutdown.json")["final"]["power"] == "OffUSB"
second = read("18-second-shutdown.json")
assert second["initial"]["flight_running"] and second["initial"]["gps_fix"]
assert second["fanet_before"].startswith("fanet 1 RUNNING")
assert second["final"]["power"] == "OffUSB" and second["fanet_after"] == "fanet 1 UNINITIALIZED 0 0"
assert second["seconds_from_hold_command_to_charging"] < 8
assert second["heap_after"]["internal_min_since_boot"] <= second["heap_before"]["internal_min_since_boot"]
final = read("21-cancel-upload-for-readback.json")
assert final["final"]["power"] == "OffUSB" and not final["final"]["firmware_sd_access"]
assert any(s["reply"] == "config 1 1 1 0 0 0 0 0" for s in read("19-final-charging.json")["steps"])
a = read("analysis.json")
assert len(a["flights"]) == 2 and all(f["raw_g_matches"] for f in a["flights"])
assert all(f["strict_validator_output"] == "STATUS_IGC_FAILED" and f["strict_validator_error"] == "non-printable record" for f in a["flights"])
for flight in a["flights"]:
    lines = (E / flight["path"]).read_bytes().splitlines()
    assert lines[-1][:1] == b"G"
    assert hashlib.md5(b"".join(line + b"\n" for line in lines[:-1])).hexdigest().upper().encode() == lines[-1][1:]
    assert any(b"CAUSE=POWER_OFF" in line for line in lines)
    assert len(flight["non_ascii_records"]) == 1 and flight["non_ascii_records"][0]["line"] == 4
assert all(book["complete"] and book["track"]["saved"] for book in a["logbook"])
assert sum(f["observations"] for f in a["fanet_logs"]) == 125
assert all(f["neighbors"] == ["FB5F38", "FBD364"] and f["max_queue_drops"] == 0 for f in a["fanet_logs"])
assert all(int(row["instances"]) == 2 for row in a["stacks"][1]["rows"] if row["task"] != "loop")
web = read("14-web-readonly.json")["requests"]
assert all(x["status"] == 200 for x in web if x["path"] != "/api/debug/memory")
assert next(x for x in web if x["path"] == "/api/debug/memory")["status"] == 404
assert read("13-open-web-during-flight.json")["final"]["flight_running"]
assert read("15-close-web-resume.json")["final"]["flight_running"]
candidate = read("candidate.json")
assert candidate["artifacts"]["firmware.bin"]["sha256"] == read("flash.json")["firmware_sha256"]
assert read("flash.json")["attempts"][-1]["returncode"] == 0
if "--live" in sys.argv:
    root = E.parents[3]
    for name, artifact in candidate["artifacts"].items():
        assert sha(root / ".pio/build/leaf_3_2_5_bench" / name) == artifact["sha256"]
print(f"PASS: shutdown/lifecycle evidence, 125 peer observations, Stack restart minima and {checked} archived hashes. EXPECTED LIMIT: strict IGC validation fails Unicode pilot header; both raw checksums match.")
