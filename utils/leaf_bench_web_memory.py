#!/usr/bin/env python3
"""Measure an idle Leaf and a repeatable Web App workload over USB and local Wi-Fi."""

import argparse
import hashlib
import json
import re
from datetime import datetime, timezone
from pathlib import Path
from statistics import median
from time import sleep
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

from leaf_bench_console import Console


def request(opener, base, path, body=None):
    req = Request(base + path, data=body, headers={"Content-Type": "application/json"})
    try:
        with opener.open(req, timeout=10) as response:
            return response.status, response.read()
    except HTTPError as error:
        with error:
            return error.code, error.read()


def capture(console, report, body_bytes):
    def press(button):
        console.request(f"press {button}")
        sleep(0.24)

    def sample(name):
        sleep(2)
        readings = [console.heap() for _ in range(5)]
        report["samples"][name] = {
            "raw": readings,
            "median_bytes": {key: median(row[key] for row in readings) for key in readings[0]},
        }

    def close_app():
        state = console.status()
        if state["page"] == "Menu" and state["modal_depth"] == 1:
            press("LEFT")
            state = console.status()
        if state["page"] == "Menu" and not state["modal_depth"]:
            press("LEFT")

    initial = console.status()
    if (initial["power"] != "On" or initial["page"] != "User" or initial["warning"]
            or initial["flight_running"] or initial["modal_depth"] or not initial["climb_ready"]
            or not initial["firmware_sd_access"] or not console.request("web").endswith(" off")):
        raise ValueError(f"Expected a warmed-up idle Leaf on the User page: {initial}")
    report["initial_status"] = initial
    report["configuration"] = console.request("config").split()[1:]
    sample("idle")

    for _ in range(8):
        press("RIGHT")
        if console.status()["page"] == "Menu":
            break
    else:
        raise ValueError("Could not reach the main menu")
    # The Developer and Logbook rows sit between Back and Web App when moving up.
    for _ in range(3 if report["configuration"][2] == "1" else 2):
        press("UP")
    if console.status()["cursor"] != 5:
        raise ValueError("Expected the Web App row")
    sleep(2)
    press("CENTER")

    try:
        _, url = console.request("web").split(maxsplit=1)
        if url == "off":
            raise ValueError("Web App did not start")
        base = url.removesuffix("/app")
        opener = build_opener(ProxyHandler({}))
        for path in ("/app", "/api/user/status", "/api/waypoints/files", "/api/debug/route-optimizer",
                     "/mac-address", "/api/debug/mac-address"):
            status, _ = request(opener, base, path)
            report["http"][path] = status
            if path not in ("/mac-address", "/api/debug/mac-address") and status != 200:
                raise ValueError(f"{path} returned HTTP {status}")
        sample("web_open")
        for _ in range(2):
            status, screenshot = request(opener, base, "/api/debug/raw-xbm")
            dimensions = re.findall(rb"#define \S+_(?:width|height) (\d+)", screenshot)
            pixels = re.findall(rb"0x[0-9a-fA-F]{2}", screenshot)
            if status != 200 or set(dimensions) != {b"192", b"96"} or len(pixels) != 2304:
                raise ValueError(f"Invalid 192×96 screenshot, HTTP {status}, {len(pixels)} bytes")
            report["http"]["screenshot"] = {"status": status, "bytes": len(screenshot), "bitmap_bytes": len(pixels)}
        sample("after_screenshots")

        route = b'{"name":"Bench Request Probe","points":[]}'.ljust(body_bytes)
        status, response = request(opener, base, "/api/routes/save", route)
        if status != 400 or b"at least one waypoint" not in response:
            raise ValueError(f"Unexpected route validation result. HTTP {status}: {response!r}")
        report["http"]["route_request"] = {"status": status, "bytes": len(route),
                                           "detail": json.loads(response)["detail"]}
        sample("after_route_request")
    except (OSError, ValueError):
        try:
            close_app()
        except (OSError, ValueError) as error:
            report["cleanup_error"] = str(error)
        raise
    close_app()

    if not console.request("web").endswith(" off"):
        raise ValueError("Web App stayed on after closing")
    report["final_status"] = console.status()
    sample("after_web_close")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--body-bytes", type=int, default=8000, metavar="64–18000")
    args = parser.parse_args()
    if not 64 <= args.body_bytes <= 18000:
        parser.error("Expected a request size in the range 64–18000 bytes")
    if args.output.exists():
        parser.error("Use a fresh output path")
    firmware = args.firmware.read_bytes()
    report = {"captured_at_utc": datetime.now(timezone.utc).isoformat(),
              "firmware_sha256": hashlib.sha256(firmware).hexdigest(),
              "samples": {}, "http": {}}
    try:
        with Console() as console:
            console.check_link()
            version = console.request("version").split()[1]
            if version.encode("ascii") + b"\0" not in firmware:
                raise ValueError(f"The device reports {version}, which is absent from the supplied firmware")
            report["version"] = version
            capture(console, report, args.body_bytes)
        report["passed"] = True
    except (OSError, ValueError) as error:
        report["passed"] = False
        report["error"] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as file:
        json.dump(report, file, indent=2)
        file.write("\n")
    print(json.dumps({key: value["median_bytes"] for key, value in report["samples"].items()}, indent=2))
    if not report["passed"]:
        parser.exit(1, report["error"] + "\n")


if __name__ == "__main__":
    main()
