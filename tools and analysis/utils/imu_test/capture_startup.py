"""Analyze archived v3.2.8 startup-probe logs or capture older firmware.

Current release firmware no longer emits these probes. Live capture is only useful
with historical firmware that contained the retired dev startup tracing.
Use --analyze LOG to regenerate CSV/JSON without connecting to hardware.
"""

import argparse
import csv
import json
from pathlib import Path
import time


def analyze(path):
    sessions = []
    columns = None
    rows = []
    session = None
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        # Other firmware diagnostics can share a serial line with the probe.
        start = line.find("IMU_STARTUP_")
        if start < 0:
            continue
        parts = line[start:].split(",")
        try:
            if parts[0] == "IMU_STARTUP_EVENT":
                _, stamp, elapsed, event = parts
                if event == "configure_begin":
                    session = {"start_ms": int(stamp), "events": {}, "samples": 0}
                    sessions.append(session)
                if session is not None:
                    session["events"][event] = {
                        "t_ms": int(stamp), "elapsed_ms": int(elapsed)
                    }
            elif parts[0] == "IMU_STARTUP_HEADER":
                columns = parts[1:]
            elif parts[0] == "IMU_STARTUP_SAMPLE" and columns:
                if len(parts) != len(columns) + 1:
                    continue  # Incomplete serial line, not a measurement.
                row = dict(zip(columns, parts[1:]))
                # A late attachment must not invent a configuration event.
                row["session"] = len(sessions) if session else 0
                rows.append(row)
                if session is not None:
                    session["samples"] += 1
                    if row["baro_ready"] == "1":
                        session.setdefault("first_observed_baro_ready_ms", int(row["t_ms"]))
            elif parts[0] == "IMU_STARTUP_UI" and session is not None:
                session["splash_complete_ms"] = int(parts[1])
        except (ValueError, IndexError):
            continue
    if rows:
        with path.with_suffix(".csv").open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=["session", *columns])
            writer.writeheader()
            writer.writerows(rows)
    summary = {"sessions": sessions, "sample_rows": len(rows)}
    path.with_suffix(".json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    return summary


def capture(path, port, seconds):
    import serial
    from serial.tools import list_ports

    if port is None:
        ports = [item.device for item in list_ports.comports() if item.vid == 0x303A]
        if len(ports) != 1:
            raise SystemExit(f"Expected one Espressif port; found {ports}. Use --port if needed.")
        port = ports[0]
    path.parent.mkdir(parents=True, exist_ok=True)
    device = serial.Serial(port=None, baudrate=115200, timeout=0.25)
    device.rts = False
    device.dtr = True
    device.port = port
    print(f"Capturing {port} for {seconds:g}s to {path}; wake Leaf to begin a trial.", flush=True)
    try:
        device.open()
        with path.open("xb") as stream:
            deadline = time.monotonic() + seconds
            while time.monotonic() < deadline:
                data = device.read(device.in_waiting or 1)
                if data:
                    stream.write(data)
                    stream.flush()
    except KeyboardInterrupt:
        pass
    finally:
        device.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="Default: auto-detect a single Espressif VID 303A port")
    parser.add_argument("--seconds", type=float, default=60)
    parser.add_argument("--output", type=Path, help="New log path (existing files are preserved)")
    parser.add_argument("--analyze", type=Path, help="Analyze an existing log without serial access")
    args = parser.parse_args()
    if args.analyze:
        analyze(args.analyze)
        return
    if args.seconds <= 0:
        parser.error("--seconds must be positive")
    path = args.output or Path(".pio/imu-startup") / time.strftime("startup-%Y%m%d-%H%M%S.log")
    if path.exists():
        parser.error(f"Refusing to replace {path}")
    capture(path, args.port, args.seconds)
    if path.exists():
        analyze(path)


if __name__ == "__main__":
    main()
