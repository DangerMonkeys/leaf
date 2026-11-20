import json
import sys
import time
import subprocess
from urllib.request import build_opener, ProxyHandler
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "utils"))
from leaf_bench_console import Console

out = ROOT / "test-data/field-testing/shutdown/2026-11-20-3.2.5-fanet"
out.mkdir(parents=True, exist_ok=True)
label, *commands = sys.argv[1:]
report = {"utc": datetime.now(timezone.utc).isoformat(), "steps": []}
raw = (out / (label + ".serial.txt")).open("w", encoding="utf-8")

class Tap:
    def __init__(self, port):
        self.port = port
    def __getattr__(self, name):
        return getattr(self.port, name)
    def read_until(self, *args, **kwargs):
        data = self.port.read_until(*args, **kwargs)
        raw.write(data.decode("utf-8", errors="replace"))
        raw.flush()
        return data

try:
    with Console() as unit:
        unit.port = Tap(unit.port)
        unit.check_link()
        report["initial"] = unit.status()
        for command in commands:
            if command == "handoff":
                break
            start = time.monotonic()
            raw.write("\nHOST " + command + "\n")
            raw.flush()
            if command.startswith("wait "):
                end = start + float(command.split()[1])
                while time.monotonic() < end:
                    unit.port.read_until(size=512)
                reply = "waited"
            else:
                reply = unit.request(command, timeout=12)
                if command.startswith("press "):
                    time.sleep(0.3)
                elif command.startswith("hold "):
                    time.sleep(int(command.split()[2]) / 1000 + 0.3)
            step = {"command": command, "reply": reply, "seconds": round(time.monotonic()-start, 3)}
            report["steps"].append(step)
            print(json.dumps(step), flush=True)
        if commands and commands[-1] == "handoff":
            report["final"] = "Expected USB transition; inspect in next capture"
        else:
            report["final"] = unit.status()
            report["heap"] = unit.heap()
            web = unit.request("web").split(maxsplit=1)[1]
            if web != "off":
                try:
                    xbm = out / (label + ".xbm")
                    with build_opener(ProxyHandler({})).open(web.removesuffix('/app')+'/api/debug/raw-xbm',timeout=4) as response:
                        xbm.write_bytes(response.read())
                    subprocess.run([sys.executable,str(Path(__file__).with_name('screen_png.py')),str(xbm)],check=True)
                    report['screen'] = xbm.with_suffix('.png').name
                except Exception as error:
                    report['screen_error'] = repr(error)
except Exception as error:
    report["error"] = repr(error)
    print(repr(error), flush=True)
finally:
    raw.close()
    (out / (label + ".json")).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
print(json.dumps({key: value for key, value in report.items() if key != "steps"}), flush=True)
sys.exit(1 if "error" in report else 0)
