#!/usr/bin/env python3
import subprocess
import csv
import re
import sys
import argparse
from pathlib import Path
import tempfile

CSV_FILE = "fanet_addresses.csv"
PREFIX = "0C"  # fixed prefix

MAC_RE = re.compile(r"MAC:\s*([0-9a-f]{2}(?::[0-9a-f]{2}){5})", re.I)
ADDR_RE = re.compile(r"^0C([0-9A-F]{4})$")

def run_esptool_read_mac(port=None):
    cmd = ["esptool.py"]
    if port:
        cmd += ["--port", port]
    cmd += ["read_mac"]
    res = subprocess.run(cmd, capture_output=True, text=True, check=True)
    match = MAC_RE.search(res.stdout)
    if not match:
        raise RuntimeError("Failed to parse MAC from esptool.py output:\n" + res.stdout)
    return match.group(1).lower()

def normalize_mac(mac: str) -> str:
    hexchars = re.sub(r'[^0-9A-Fa-f]', '', mac)
    return ':'.join(hexchars[i:i+2].lower() for i in range(0, 12, 2))

def load_csv(path: Path):
    if not path.exists():
        return []
    with open(path, newline='', encoding='utf-8') as f:
        return list(csv.DictReader(f))

def find_existing(mac, rows):
    for row in rows:
        if row.get("mac_address", "").lower() == mac.lower():
            return row.get("fanet_address")
    return None

def next_fanet(rows):
    max_id = 0
    for row in rows:
        fa = row.get("fanet_address", "").upper()
        m = ADDR_RE.match(fa)
        if m:
            val = int(m.group(1), 16)
            max_id = max(max_id, val)
    next_id = max_id + 1
    if next_id > 0xFFFF:
        raise RuntimeError("FANET ID overflow (exceeded 0xFFFF)")
    return f"{PREFIX}{next_id:04X}"

def append_csv(path: Path, mac, fanet):
    write_header = not path.exists()
    with open(path, "a", newline='', encoding="utf-8") as f:
        writer = csv.writer(f)
        if write_header:
            writer.writerow(["mac_address", "fanet_address"])
        writer.writerow([mac, fanet])

def write_nvm_csv(fanet_address: str) -> Path:
    tmpfile = Path(tempfile.gettempdir()) / "nvm.csv"
    with open(tmpfile, "w", newline='', encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["key", "type", "encoding", "value"])
        writer.writerow(["fanet", "namespace", "", ""])
        writer.writerow(["address", "data", "string", fanet_address])
    return tmpfile

def generate_nvs_bin(esp_idf_dir: Path, nvm_csv: Path) -> Path:
    bin_file = Path(tempfile.gettempdir()) / "flash.bin"
    nvs_gen = esp_idf_dir / "components" / "nvs_flash" / "nvs_partition_generator" / "nvs_partition_gen.py"
    if not nvs_gen.exists():
        raise RuntimeError(f"Cannot find nvs_partition_gen.py at {nvs_gen}")
    cmd = [
        sys.executable,
        str(nvs_gen),
        "generate",
        str(nvm_csv),
        str(bin_file),
        "0x5000"  # partition start offset for generation
    ]
    subprocess.run(cmd, check=True)
    print(f"Created NVS binary: {bin_file}")
    return bin_file

def flash_bin(bin_file: Path, port=None):
    cmd = ["esptool.py"]
    if port:
        cmd += ["--port", port]
    cmd += ["write_flash", "0x9000", str(bin_file)]
    subprocess.run(cmd, check=True)
    print(f"Flashed {bin_file} to device at offset 0x9000")

def main():
    p = argparse.ArgumentParser()
    p.add_argument("--csv", default=CSV_FILE, help="CSV file path")
    p.add_argument("--port", default=None, help="ESP serial port")
    p.add_argument("--esp-idf", required=True, type=Path, help="ESP-IDF root directory")
    args = p.parse_args()

    mac = normalize_mac(run_esptool_read_mac(args.port))
    rows = load_csv(Path(args.csv))

    existing = find_existing(mac, rows)
    if existing:
        print(f"MAC {mac} already has FANET {existing}")
        fanet = existing
    else:
        fanet = next_fanet(rows)
        append_csv(Path(args.csv), mac, fanet)
        print(f"Assigned {fanet} to MAC {mac}")

    nvm_csv = write_nvm_csv(fanet)
    print(f"NVM CSV written to {nvm_csv}")

    bin_file = generate_nvs_bin(args.esp_idf, nvm_csv)
    flash_bin(bin_file, args.port)

if __name__ == "__main__":
    main()
