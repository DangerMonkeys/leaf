#!/usr/bin/env python3
"""Drive Leaf's bench firmware over USB CDC using pySerial."""

import argparse
import hashlib
import json
from datetime import datetime, timezone
from pathlib import Path
from statistics import median
from time import monotonic, sleep

import serial
from serial.tools.list_ports import comports


class Console:
    def __init__(self, port=None):
        self.port = serial.Serial(baudrate=115200, timeout=0.2, write_timeout=1)
        self.port.dtr = True
        self.port.rts = True
        if port is None:
            ports = [
                candidate.device for candidate in comports()
                if (candidate.vid, candidate.pid) == (0x303A, 0x0002)
            ]
            if len(ports) != 1:
                raise ValueError("Expected one Leaf application serial port. Use --port to select it.")
            port = ports[0]
        self.port.port = port
        self.port.open()
        # Allow USB CDC line-state notification to reach the device after reopening.
        sleep(0.25)

    def check_link(self):
        # Reopening retries the CDC line-state handshake. Only retry a read-only
        # request so a lost reply cannot repeat a button press.
        for _ in range(5):
            try:
                self.request("version", timeout=1.0)
                return
            except TimeoutError:
                self.port.close()
                sleep(0.25)
                self.port.open()
                sleep(0.25)
        self.request("version", timeout=2.0)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.port.close()

    def request(self, command, timeout=5.0):
        payload = command.encode("ascii")
        words = payload.split()
        if not words or len(payload) > 63 or any(not 32 <= byte < 127 for byte in payload):
            raise ValueError("Expected a single printable command of up to 63 bytes.")
        self.port.write(payload + b"\n")
        expected = b"ok" if words[0] in {b"press", b"hold"} else words[0]
        deadline = monotonic() + timeout
        pending = b""
        while monotonic() < deadline:
            pending += self.port.read_until(size=256)
            if not pending.endswith(b"\n"):
                # A diagnostic log can be arbitrarily long. Keep enough to read the reply.
                pending = pending[-512:]
                continue
            _, marker, reply = pending.rpartition(b"BENCH ")
            pending = b""
            if not marker:
                continue
            parts = reply.split()
            if parts and parts[0] == b"error":
                raise ValueError(reply.decode("ascii").strip())
            if parts and parts[0] == expected:
                return reply.decode("ascii").strip()
        raise TimeoutError(f"No response to {command!r} on {self.port.port}")

    def status(self):
        fields = self.request("status").split()[1:]
        power, page, cursor, depth, warning, climb_ready, gps_fix, flight, sd = fields
        return {
            "power": power, "page": page, "cursor": int(cursor), "modal_depth": int(depth),
            "warning": warning == "1", "climb_ready": climb_ready == "1", "gps_fix": gps_fix == "1",
            "flight_running": flight == "1", "firmware_sd_access": sd == "1",
        }

    def heap(self):
        keys = ("internal_free", "internal_min_since_boot", "internal_largest", "loop_stack_unused")
        return dict(zip(keys, map(int, self.request("heap").split()[1:]), strict=True))


def baseline(console, args):
    firmware = args.firmware.read_bytes()
    version = console.request("version").split(maxsplit=1)[1]
    if (version.encode("ascii") + b"\0") not in firmware:
        raise ValueError(f"Device firmware {version!r} does not match the provided binary.")
    web = console.request("web").split(maxsplit=1)[1]
    station, ap = console.request("ip").split()[1:]
    initial = console.status()
    configuration_fields = (
        "bluetooth_configured", "fanet_region", "developer_mode", "system_events",
        "network_events", "web_requests", "vario_log", "cpu_capture",
    )
    configuration = dict(
        zip(configuration_fields, map(int, console.request("config").split()[1:]), strict=True)
    )
    if initial["power"] != "On" or initial["flight_running"] or not initial["climb_ready"]:
        raise ValueError(f"Baseline requires an idle, running Leaf with a ready vario: {initial}")
    if initial["warning"] or initial["page"] in {"Menu", "Blank", "Charging"} or initial["modal_depth"]:
        raise ValueError(f"Baseline requires a primary screen without a warning or menu: {initial}")
    samples = []
    start = monotonic()
    for index in range(args.samples):
        if index:
            sleep(args.interval)
        samples.append({"at_seconds": round(monotonic() - start, 3), **console.heap()})
    final = console.status()
    final_web = console.request("web").split(maxsplit=1)[1]
    final_flags = list(map(int, console.request("config").split()[1:]))
    stable_fields = (
        "power", "page", "warning", "flight_running", "firmware_sd_access", "modal_depth"
    )
    state_changed = any(initial[field] != final[field] for field in stable_fields)
    if state_changed or final_web != web or list(configuration.values()) != final_flags:
        raise ValueError(f"Device state changed during the baseline: {initial} -> {final}")
    result = {
        "captured_at_utc": datetime.now(timezone.utc).isoformat(),
        "firmware_version": version,
        "firmware_sha256": hashlib.sha256(firmware).hexdigest(),
        "firmware_file": args.firmware.name,
        "port": console.port.port,
        "initial_status": initial,
        "final_status": final,
        "configuration": configuration,
        "web": web,
        "station_ipv4": station,
        "ap_ipv4": ap,
        "summary_bytes": {
            name: {
                "min": min(sample[name] for sample in samples),
                "median": median(sample[name] for sample in samples),
                "max": max(sample[name] for sample in samples),
            }
            for name in samples[0] if name != "at_seconds"
        },
        "samples": samples,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    print(json.dumps(result["summary_bytes"], indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Application serial port. Autodetect one Leaf by default.")
    commands = parser.add_subparsers(dest="action", required=True)
    send = commands.add_parser("send", help="Send commands in order, waiting for button release.")
    send.add_argument("commands", nargs="+")
    capture = commands.add_parser("baseline", help="Capture an idle on-state memory baseline.")
    capture.add_argument("--firmware", type=Path, required=True, help="The binary flashed to Leaf.")
    capture.add_argument("--output", type=Path, required=True)
    capture.add_argument("--samples", type=int, default=20)
    capture.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()
    if args.action == "baseline" and (args.samples < 1 or args.interval < 0):
        parser.error("Expected a positive sample count and nonnegative interval.")
    try:
        with Console(args.port) as console:
            console.check_link()
            if args.action == "baseline":
                baseline(console, args)
            else:
                for command in args.commands:
                    print(console.request(command))
                    if command.startswith("press "):
                        sleep(0.15)
                    elif command.startswith("hold "):
                        sleep(int(command.split()[2]) / 1000 + 0.1)
    except (ValueError, OSError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
