import hashlib
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import ProxyHandler, Request, build_opener

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "test-data/field-testing/sd-full/2026-11-19-3.2.8-fix"
OUT.mkdir(parents=True, exist_ok=True)
label, base, spec = sys.argv[1:]
steps = json.loads(Path(spec).read_text(encoding="utf-8"))
results = {"utc": datetime.now(timezone.utc).isoformat(), "base": base, "requests": []}
opener = build_opener(ProxyHandler({}))
for step in steps:
    method, path = step.get("method", "GET"), step["path"]
    headers = {}
    body = None
    if "json" in step:
        headers["Content-Type"] = "application/json"
        body = json.dumps(step["json"]).encode()
    if "padding" in step:
        body = body.ljust(step["padding"])
    if "upload" in step:
        upload = step["upload"]
        data = Path(upload["source"]).read_bytes()
        boundary = "leaf-sd-full-bench-boundary"
        headers["Content-Type"] = "multipart/form-data; boundary=" + boundary
        body = ("--" + boundary + '\r\nContent-Disposition: form-data; name="file"; filename="' + upload["name"] + '"\r\nContent-Type: application/octet-stream\r\n\r\n').encode() + data + ("\r\n--" + boundary + "--\r\n").encode()
    started = time.monotonic()
    result = {"method": method, "path": path, "request_bytes": len(body) if body else 0}
    try:
        try:
            response = opener.open(Request(base + path, data=body, headers=headers, method=method), timeout=18)
        except HTTPError as error:
            response = error
        with response:
            content = response.read()
            result.update(status=response.status, seconds=round(time.monotonic()-started, 3), bytes=len(content), sha256=hashlib.sha256(content).hexdigest())
            if "save" in step:
                (OUT / (label + "-" + step["save"])).write_bytes(content)
                result["saved_file"] = label + "-" + step["save"]
            if "xbm" in path:
                result["body"] = "Saved screen bitmap"
            else:
                try:
                    result["body"] = json.loads(content)
                except ValueError:
                    result["body"] = content.decode(errors="replace")[:300]
    except Exception as error:
        result["error"] = repr(error)
    results["requests"].append(result)
    (OUT / (label + ".json")).write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result), flush=True)
