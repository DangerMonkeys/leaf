"""Bench GPS only; physical pressure and motion sensors remain active. FANET must be off."""
import socket, time, sys, json
from pathlib import Path
from urllib.request import Request
from datetime import datetime, timezone
from urllib.request import build_opener, ProxyHandler
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
end = time.monotonic()+float(sys.argv[2])
opener = build_opener(ProxyHandler({}))
last_http = 0
started_utc=datetime.now(timezone.utc).isoformat()
packets_sent=0
def send(sentence):
    checksum = 0
    for byte in sentence.encode(): checksum ^= byte
    wire = f'G0,${sentence}*{checksum:02X}\r\n'.encode()
    sock.sendto(wire,(sys.argv[1],7431))
stop = Path(__file__).with_name('stop-replay')
print('Starting synthetic stationary GPS; physical sensors remain active',flush=True)
while time.monotonic()<end and not stop.exists():
    if time.monotonic()-last_http > 5:
        last_http = time.monotonic()
        try:
            with opener.open('http://'+sys.argv[1]+'/api/user/status',timeout=2) as response:
                response.read()
        except OSError:
            pass
    now=datetime.now(timezone.utc)
    utc=now.strftime('%H%M%S')+'.00'
    send(f'GNRMC,{utc},A,4730.0000,N,12212.0000,W,0.0,0.0,{now:%d%m%y},,,A')
    send(f'GNGGA,{utc},4730.0000,N,12212.0000,W,1,10,0.8,100.0,M,-17.0,M,,')
    packets_sent += 2
    time.sleep(0.2)

sock.close()
out=Path(__file__).resolve().parents[2]/'test-data/field-testing/shutdown/2026-11-19-3.2.8'
(out/(sys.argv[3]+'-producer.json')).write_text(json.dumps({'started_utc':started_utc,'stopped_utc':datetime.now(timezone.utc).isoformat(),'packets_sent':packets_sent,'stopped_by_marker':stop.exists(),'injection_disabled_by_host':False})+'\n')
print('Host replay stopped; host did not disable firmware injection',flush=True)
