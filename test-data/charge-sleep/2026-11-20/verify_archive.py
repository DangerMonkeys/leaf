"""Check the preserved charge-sleep captures without third-party dependencies."""

import hashlib
import json
from pathlib import Path


def main():
    root = Path(__file__).resolve().parent
    manifest = json.loads((root / "source-manifest.json").read_text())
    expected = set()
    for record in manifest["files"]:
        path = root / record["path"]
        assert path.resolve().is_relative_to(root), record["path"]
        payload = path.read_bytes()
        assert len(payload) == record["bytes"], f"Size mismatch: {record['path']}"
        assert hashlib.sha256(payload).hexdigest() == record["sha256"], record["path"]
        expected.add(record["path"])
    actual = {p.relative_to(root).as_posix()
              for folder in (root / "captures", root / "scope")
              for p in folder.rglob("*") if p.is_file()}
    assert actual == expected, "Missing or unlisted capture files"
    print(f"Verified {len(expected)} byte-preserved charge-sleep capture files")


if __name__ == "__main__":
    main()
