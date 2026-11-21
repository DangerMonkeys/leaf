"""Verify the preserved field data without importing or running the analyses."""

import ast
import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "source-manifest.json").read_text(encoding="utf-8"))
    failures = []
    igcs = 0
    for entry in manifest["files"] + manifest["additional_inputs"]:
        path = (root / entry["path"]).resolve()
        if not path.is_relative_to(root) or not path.is_file():
            failures.append(f"Missing or invalid path: {entry['path']}")
            continue
        data = path.read_bytes()
        expected = entry.get("archived_sha256", entry["sha256"])
        if hashlib.sha256(data).hexdigest() != expected:
            failures.append(f"Hash mismatch: {entry['path']}")
        if path.suffix.lower() == ".igc":
            igcs += 1
        if path.suffix == ".py":
            try:
                ast.parse(data, filename=str(path))
            except SyntaxError as error:
                failures.append(str(error))
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"Verified {len(manifest['files'])} archived source files and "
          f"{len(manifest['additional_inputs'])} additional replay input; {igcs} IGC files.")


if __name__ == "__main__":
    main()
