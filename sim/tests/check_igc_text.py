"""Check generated IGC text with the independent C validator and tamper controls."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


EXPECTED_HEADERS = [
    ("First pilot with a long IGC header name", "First glider with a long IGC header name"),
    ("Jose O'Neil  Jr.", "Creme - AEro 2"),
    ("Unknown", "Unknown"),
    ("Unknown", "Unknown"),
]


def run_validator(executable, path):
    result = subprocess.run(
        [str(executable), str(path)], capture_output=True, text=True, check=False
    )
    return {
        "exit_code": result.returncode,
        "stdout": result.stdout.strip(),
        "stderr": result.stderr.strip(),
    }


def check(executable, tracks):
    paths = sorted(tracks.glob("*.igc"))
    assert len(paths) == len(EXPECTED_HEADERS), f"Expected four consecutive flights, got {len(paths)}"
    results = []
    with tempfile.TemporaryDirectory(prefix="leaf-igc-check-") as scratch:
        for path, (pilot, glider) in zip(paths, EXPECTED_HEADERS):
            raw = path.read_bytes()
            lines = raw.splitlines()
            assert lines[-1].startswith(b"G")
            assert all(0x20 <= c <= 0x7D and c not in b"!$*\\^" for line in lines for c in line)
            assert b"HFPLTPILOTINCHARGE:" + pilot.encode("ascii") in lines
            assert b"HFGTYGLIDERTYPE:" + glider.encode("ascii") in lines
            digest = hashlib.md5(b"".join(line + b"\n" for line in lines[:-1])).hexdigest().upper()
            assert lines[-1][1:].decode("ascii") == digest
            validation = run_validator(executable, path)
            assert validation["exit_code"] == 0 and validation["stdout"] == "STATUS_IGC_PASSED", validation
            controls = {}
            offsets = {
                "pilot_header": raw.index(b"HFPLTPILOTINCHARGE:") + len(b"HFPLTPILOTINCHARGE:"),
                "gps_fix": raw.index(b"\nB") + 2 + 5,
            }
            for label, offset in offsets.items():
                altered = bytearray(raw)
                original = altered[offset]
                altered[offset] = (ord("0") + (original - ord("0") + 1) % 10
                                   if label == "gps_fix" else ord("X") if original != ord("X") else ord("Y"))
                assert altered != raw and bytes(altered).splitlines()[-1] == lines[-1]
                target = Path(scratch) / f"{path.stem}-{label}.igc"
                target.write_bytes(altered)
                result = run_validator(executable, target)
                assert result["exit_code"] == 1, result
                assert result["stdout"] == "STATUS_IGC_FAILED", result
                assert result["stderr"] == "G checksum mismatch", result
                controls[label] = result
            results.append({
                "file": path.name, "bytes": len(raw),
                "sha256": hashlib.sha256(raw).hexdigest(),
                "pilot_header": pilot, "glider_header": glider, "g_record": lines[-1].decode(),
                "validator": validation, "tamper_controls": controls,
            })
    return {
        "scope": "Unmodified simulator-produced files; independent C validator plus Python canonical-LF MD5. Not authenticated recorder signing or contest approval.",
        "validator_name": executable.name,
        "validator_binary_sha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
        "validator_source_sha256": hashlib.sha256(
            (Path(__file__).resolve().parents[2] / "utils/igc_validator/validate.c").read_bytes()
        ).hexdigest(),
        "files_passed": len(results), "tampered_copies_rejected": 2 * len(results),
        "files": results,
    }


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--tracks", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    result = check(args.validator.resolve(), args.tracks.resolve())
    text = json.dumps(result, indent=2) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8", newline="\n")
    print(text, end="")
