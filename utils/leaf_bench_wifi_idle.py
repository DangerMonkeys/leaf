#!/usr/bin/env python3
"""Compare Leaf heap after Wi-Fi menu and Web App sessions."""

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from statistics import median
from time import monotonic, sleep
from urllib.error import URLError
from urllib.request import ProxyHandler, build_opener

from leaf_bench_console import Console
from leaf_bench_route_requests import close_web_app, open_web_app, press
from leaf_bench_web_memory import request


def wait_for_ip(unit, station=True):
    start = monotonic()
    for _ in range(40):
        addresses = unit.request("ip").split()
        if addresses[1 if station else 2] != "0.0.0.0":
            return round((monotonic() - start) * 1000)
        sleep(0.25)
    raise ValueError("Leaf did not acquire the expected IPv4 address")


def sample(unit):
    sleep(2)
    samples = [unit.heap() for _ in range(5)]
    result = {"heap": {key: median(row[key] for row in samples) for key in samples[0]},
              "station_connected": unit.request("ip").split()[1] != "0.0.0.0",
              "ap_running": unit.request("ip").split()[2] != "0.0.0.0",
              "status": unit.status()}
    try:
        result["wifi_mode"] = unit.request("wifi").split()[1]
    except ValueError as error:
        if "unknown_command" not in str(error):
            raise
    return result


def exercise(unit, report):
    checks = report["checkpoints"]
    checks["idle"] = sample(unit)
    opener = build_opener(ProxyHandler({}))
    for cycle in range(1, 3):
        start = monotonic()
        base = open_web_app(unit)
        if unit.request("ip").split()[1] == "0.0.0.0":
            raise ValueError("Web App reopened without the saved network")
        status, _ = request(opener, base, "/api/user/status")
        if status != 200:
            raise ValueError(f"Reopened Web App returned HTTP {status}")
        report[f"reopen_{cycle}_ms"] = round((monotonic() - start) * 1000)
        checks[f"station_web_{cycle}"] = sample(unit)

        if cycle == 2:
            # Select Use Leaf AP, then close it. The host remains on its normal
            # network. Verify Leaf's AP and the subsequent saved STA reconnect.
            press(unit, "DOWN")
            press(unit, "CENTER")
            wait_for_ip(unit, station=False)
            checks["access_point"] = sample(unit)
            press(unit, "LEFT")
            report["saved_network_reconnect_ms"] = wait_for_ip(unit)
            checks["menu_after_ap_close"] = sample(unit)

        close_web_app(unit)
        checks[f"idle_after_{cycle}"] = sample(unit)
        if unit.request("web") != "web off" or unit.status()["page"] != "User":
            raise ValueError("Could not return Leaf to the User screen with the Web App closed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a fresh output path")
    image = args.firmware.read_bytes()
    report = {"captured_at_utc": datetime.now(timezone.utc).isoformat(),
              "firmware_sha256": hashlib.sha256(image).hexdigest(),
              "checkpoints": {}, "passed": False}
    try:
        with Console() as unit:
            unit.check_link()
            report["version"] = unit.request("version").split()[1]
            if report["version"].encode() + b"\0" not in image:
                raise ValueError("Firmware image and device version do not match")
            state = unit.status()
            if state["power"] != "On" or state["page"] != "User" or state["flight_running"]:
                raise ValueError("Start on the idle User screen")
            try:
                exercise(unit, report)
                report["passed"] = True
            finally:
                if unit.status()["page"] == "Menu":
                    close_web_app(unit)
    except (OSError, ValueError, URLError) as error:
        report["error"] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
