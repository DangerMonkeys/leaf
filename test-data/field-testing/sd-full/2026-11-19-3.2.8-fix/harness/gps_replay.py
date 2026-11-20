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
    time.sleep(0.2)
# The host producer has stopped. Disable admission before any power-off button event.
with opener.open(Request('http://'+sys.argv[1]+'/api/debug/session',data=b'{"udp_sensor_injection":false}',headers={'Content-Type':'application/json'},method='POST'),timeout=5) as response:
    state=json.loads(response.read())
assert state['udp_sensor_injection'] is False
out=Path(__file__).resolve().parents[2]/'test-data/field-testing/sd-full/2026-11-19-3.2.8-fix'
label=sys.argv[3] if len(sys.argv)>3 else 'replay'
(out/(label+'-stopped.json')).write_text(json.dumps({'producer_stopped':True,'injection_disabled':True})+'\n')
print('Synthetic GPS stopped; injection disabled; safe to request shutdown',flush=True)
