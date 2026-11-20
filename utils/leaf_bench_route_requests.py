#!/usr/bin/env python3
"""Verify the Leaf Web App's route requests and capture heap checkpoints."""

import argparse
import hashlib
import http.client
import json
import re
import socket
from datetime import datetime, timezone
from pathlib import Path
from time import monotonic, sleep
from urllib.parse import urlencode, urlsplit
from urllib.request import ProxyHandler, build_opener

from leaf_bench_console import Console
from leaf_bench_web_memory import request


def json_body(payload, size):
    body = json.dumps(payload, separators=(",", ":")).encode()
    if len(body) > size:
        raise ValueError("Route fixture exceeds the target request size")
    return body.ljust(size)


def open_web_app(unit):
    for _ in range(8):
        if unit.status()["page"] == "Menu":
            break
        press(unit, "RIGHT")
    else:
        raise ValueError("Could not reach the main menu")
    for _ in range(3 if unit.request("config").split()[3] == "1" else 2):
        press(unit, "UP")
    if unit.status()["cursor"] != 5:
        raise ValueError("Expected the Web App row")
    for _ in range(24):
        if unit.request("ip").split()[1] != "0.0.0.0":
            break
        sleep(0.25)
    press(unit, "CENTER")
    _, url = unit.request("web").split(maxsplit=1)
    if url == "off":
        raise ValueError("Web App did not start")
    return url.removesuffix("/app")


def press(unit, button):
    unit.request(f"press {button}")
    sleep(0.26)


def close_web_app(unit):
    state = unit.status()
    if state["page"] == "Menu" and state["modal_depth"] == 1:
        press(unit, "LEFT")
    if unit.status()["page"] == "Menu":
        press(unit, "LEFT")
    if unit.status()["page"] == "Navigate":
        press(unit, "LEFT")


def run_requests(unit, base, args, report):
    opener = build_opener(ProxyHandler({}))
    edit_path = "/api/routes/save"
    import_path = "/api/routes/import"
    points = [{"name": f"Bench {i:02}", "lat": 47.5 + i / 1000, "lon": -122.2 - i / 1000,
               "alt_m": 100 + i, "radius_m": [0, 3, 100, 25000][i % 4],
               "discard_point": {"value": list(range(30))}} for i in range(20)]
    edit = {"name": args.prefix + "-edit", "activate": False, "points": points,
            "discard_root": {"source": "bench"}}
    task = {"taskType": "CLASSIC", "version": 1, "goal": {"type": "CYLINDER"},
            "turnpoints": [{"waypoint": {"name": p["name"], "lat": p["lat"], "lon": p["lon"],
                                         "altSmoothed": p["alt_m"]}, "radius": 400} for p in points]}
    import_payload = json.dumps(task, separators=(",", ":")).ljust(8192)
    imported = {"name": args.prefix + "-import", "activate": False, "data": import_payload}

    def check(label, path, body, expected_status, detail=None):
        before = unit.heap()
        start = monotonic()
        status, raw = request(opener, base, path, body)
        elapsed = round((monotonic() - start) * 1000)
        response = json.loads(raw)
        report["requests"][label] = {
            "status": status, "bytes": len(body), "response": response, "elapsed_ms": elapsed,
            "before": before, "after": unit.heap(),
        }
        try:
            values = map(int, unit.request("routeheap").split()[1:])
            report["requests"][label]["route_heap_bytes"] = dict(zip(
                ("receipt_start", "receipt_minimum", "normalization_start", "normalization_minimum"),
                values, strict=True))
        except ValueError as error:
            if "unknown_command" not in str(error):
                raise
        if status != expected_status or (detail and detail not in response.get("detail", "")):
            raise ValueError(f"{label} returned HTTP {status}: {response}")
        if expected_status == 200 and (not response["saved"] or response["active"]
                                      or response["points"] != 20):
            raise ValueError(f"{label} did not save the expected inactive 20-point route")

    edit_body = json_body(edit, 18000)
    import_body = json_body(imported, 10000)
    if args.only != "import":
        check("editor_limit", edit_path, edit_body, 200)
    if args.only != "edit":
        # The handler trims the nested JSON string, so the outer request reaches
        # 10,000 bytes and the existing 8,192-byte decoded route limit is preserved.
        check("import_limit", import_path, import_body, 200)
    if args.only != "both":
        return

    check("editor_oversize", edit_path, edit_body + b" ", 400, "request is invalid")
    check("import_oversize", import_path, import_body + b" ", 400, "request is invalid")
    check("editor_bad_json", edit_path, b'{"name":', 400, "request is not JSON")
    check("editor_21_points", edit_path, json_body({**edit, "points": points + points[:1]}, 18000),
          400, "20-point")
    bad_points = [{**points[0], "lat": 91}, *points[1:]]
    check("editor_bad_coordinates", edit_path, json_body({**edit, "points": bad_points}, 18000),
          400, "invalid waypoint coordinate")
    host = urlsplit(base)
    # Give the server time to receive at least one chunk before dropping the
    # connection. The complete substituted route would target the existing name.
    interrupted = json_body({**edit, "points": [{**p, "alt_m": 999} for p in points]}, 18000)
    with socket.create_connection((host.hostname, host.port or 80), timeout=4) as client:
        headers = (f"POST {edit_path} HTTP/1.1\r\nHost: {host.hostname}\r\n"
                   f"Content-Type: application/json\r\nContent-Length: {len(interrupted)}\r\n"
                   "Connection: close\r\n\r\n")
        client.sendall(headers.encode("ascii") + interrupted[:3000])
        sleep(0.35)
        client.shutdown(socket.SHUT_WR)
        client.settimeout(12)
        interrupted_reply = client.recv(256)
        if interrupted_reply and b" 400 " not in interrupted_reply.split(b"\r\n", 1)[0]:
            raise ValueError("An incomplete request received an unexpected response")
    status, _ = request(opener, base, "/api/user/status")
    if status != 200:
        raise ValueError(f"Web App did not recover after the interrupted upload ({status})")
    report["interrupted_request"] = {"declared_bytes": len(interrupted), "sent_bytes": 3000,
                                     "response": "400" if interrupted_reply else "closed",
                                     "recovery_http_status": status}

    form_body = urlencode({"plain": '{"name":'}).encode("ascii")
    client = http.client.HTTPConnection(host.hostname, host.port or 80, timeout=10)
    try:
        client.request("POST", edit_path, form_body,
                       {"Content-Type": "application/x-www-form-urlencoded"})
        form_reply = client.getresponse()
        detail = json.loads(form_reply.read()).get("detail")
    finally:
        client.close()
    if form_reply.status != 400 or detail != "Route save request is not JSON.":
        raise ValueError(f"The legacy form body was not decoded: {form_reply.status} {detail}")
    report["legacy_urlencoded_form"] = {"status": form_reply.status, "detail": detail}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--prefix", required=True, help="Unused prefix for two disposable routes")
    parser.add_argument("--only", choices=("both", "edit", "import"), default="both")
    args = parser.parse_args()
    if not re.fullmatch(r"bench-[a-z0-9-]{1,16}", args.prefix) or args.output.exists():
        parser.error("Use an unused bench- prefix and a fresh output path")
    image = args.firmware.read_bytes()
    report = {"captured_at_utc": datetime.now(timezone.utc).isoformat(),
              "firmware_sha256": hashlib.sha256(image).hexdigest(), "prefix": args.prefix,
              "requests": {}, "passed": False}
    try:
        with Console() as unit:
            unit.check_link()
            report["version"] = unit.request("version").split()[1]
            if report["version"].encode() + b"\0" not in image:
                raise ValueError("Firmware image and device version do not match")
            if (unit.status()["page"] != "User" or unit.status()["flight_running"]
                    or not unit.request("web").endswith(" off")):
                raise ValueError("Start on the idle User page")
            try:
                base = open_web_app(unit)
                report["web_open_heap"] = unit.heap()
                run_requests(unit, base, args, report)
            finally:
                try:
                    close_web_app(unit)
                except (OSError, ValueError) as error:
                    report["cleanup_error"] = str(error)
        report["passed"] = True
    except (OSError, ValueError, http.client.HTTPException) as error:
        report["error"] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps(report, indent=2))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
