"""Package student test builds with filenames accepted by the existing SD updater."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess


def package(project: Path, environment: str, output: Path) -> Path:
    match = re.fullmatch(r"leaf_student_(3_2_[567])_training", environment)
    if not match:
        raise ValueError("Expected a supported student training environment")
    hardware = match.group(1).replace("_", ".")
    variant = "leaf_student_" + match.group(1)
    source = project / ".pio" / "build" / environment / "firmware.bin"
    data = source.read_bytes()
    if not 4096 <= len(data) <= 3342336 or data[0] != 0xE9:
        raise ValueError("Unexpected ESP32 application image size/header")
    if ("LEAF_HARDWARE_VARIANT=" + variant + "\0").encode() not in data:
        raise ValueError("Student hardware marker does not match the requested environment")
    if ("+h" + hardware + "\0").encode() not in data:
        raise ValueError("Firmware version does not contain the matching hardware marker")
    # Normal firmware accepts the dotted token; existing student firmware accepts its variant.
    name = f"firmware-{variant}-training-{hardware}.bin"
    output.mkdir(parents=True, exist_ok=True)
    destination = output / name
    shutil.copyfile(source, destination)
    digest = hashlib.sha256(data).hexdigest()
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=project, text=True).strip()
    (output / "SHA256SUMS.txt").write_text(f"{digest}  {name}\n", encoding="utf-8")
    (output / "build-info.json").write_text(json.dumps({
        "environment": environment, "hardware": hardware, "source_commit": commit,
        "filename": name, "sha256": digest, "size_bytes": len(data),
        "qualification": "Experimental training test build; not flight-qualified",
    }, indent=2) + "\n", encoding="utf-8")
    (output / "README.txt").write_text(
        f"Experimental Leaf student training firmware for hardware {hardware}.\n"
        "This is an application-only SD update, not a complete image for an erased board.\n"
        f"Copy only {name} into /firmware on the matching Leaf SD card.\n"
        "Keep its filename unchanged and remove any other .bin files from /firmware.\n"
        "Safely eject the card/USB storage and restart the Leaf.\n"
        "Update results are written to /firmware/update_status.txt.\n"
        "After updating: Settings > Leaf Labs > Training; enable and pair with the instructor.\n"
        "Prototype build: not flight-qualified. Existing settings/pairings are preserved.\n",
        encoding="utf-8")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment", required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    arguments = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    print(package(root, arguments.environment, arguments.output_dir.resolve()))
