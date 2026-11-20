"""Verify archived storage-fix evidence; --live also checks the current source/build."""
import csv
import hashlib
import json
import sys
from pathlib import Path

E = Path(__file__).resolve().parent
read = lambda name: json.loads((E / name).read_text())
def files(name):
    return {x["path"].replace(chr(92), "/"): {k: x[k] for k in ("bytes", "sha256")} for x in read(name)["files"]}

before, after = files("04-full-fixtures-card.json"), files("19-full-results-card.json")
assert before == after and len(before) == 6
assert read("19-full-results-card.json")["free_after"] == 0
full = read("12-full-writes.json")["requests"]
recovery = read("37-recovery-confirmed.json")["requests"]
assert all(x["status"] == 200 for x in recovery)
assert [x["status"] for x in full if x["method"] in ("POST", "PUT")] == [500, 500, 500, 400, 200]
assert files("sd-before.json") == files("45-originals-restored-card.json")
checked = 0
for manifest in E.glob("*-card.json"):
    for f in json.loads(manifest.read_text())["files"]:
        raw = (E / "card-captures" / manifest.name.removesuffix("-card.json") / f["path"]).read_bytes()
        assert len(raw) == f["bytes"] and hashlib.sha256(raw).hexdigest() == f["sha256"]
        checked += 1
stack = E / "card-captures/44-recovery-results/diagnostics/stack_1.csv"
rows = list(csv.DictReader(stack.open()))
assert len(rows) == 4 and int(rows[0]["min_unused_bytes"]) == 7644
space = read("final-space-check.json")
assert space["actual_free_clusters"] == space["fsinfo_free_clusters"] == 519
assert space["fat_copies_identical"]
if "--live" in sys.argv:
    root = E.parents[3]
    candidate = read("candidate.json")
    for name, digest in candidate["source_changes"].items():
        assert hashlib.sha256((root / name).read_bytes()).hexdigest() == digest, name
    for name, entry in candidate["artifacts"].items():
        assert hashlib.sha256((root / ".pio/build/leaf_3_2_8_bench" / name).read_bytes()).hexdigest() == entry["sha256"]
print(f"PASS: six full-card files preserved; recovery HTTP 200; Stack CSV; originals restored; {checked} archived file hashes; final free-space hints match.")
