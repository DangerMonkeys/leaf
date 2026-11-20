import json,subprocess,sys,time,hashlib
from pathlib import Path
from serial.tools import list_ports
root=Path(__file__).resolve().parents[2]
out=root/'test-data/field-testing/shutdown/2026-11-19-3.2.8'
binary=root/'.pio/build/leaf_3_2_8_bench/firmware.bin'
esptool=Path('C:/Users/oxoth/.platformio/packages/tool-esptoolpy/esptool.py')
result={'firmware_sha256':hashlib.sha256(binary.read_bytes()).hexdigest(),'attempts':[]}
for attempt in range(2):
 ports=[p for p in list_ports.comports() if p.vid==0x303a]
 if len(ports)!=1: raise RuntimeError('Expected exactly one Espressif port: '+repr(ports))
 port=ports[0]
 cmd=[sys.executable,str(esptool),'--chip','esp32s3','--port',port.device,'--baud','460800','--before','default_reset','--after','hard_reset','write_flash','-z','--flash_mode','qio','--flash_freq','80m','--flash_size','detect','0x10000',str(binary),'0x340000',str(binary)]
 run=subprocess.run(cmd,capture_output=True,text=True)
 (out/f'flash-{attempt+1}.txt').write_text(run.stdout+run.stderr,encoding='utf-8')
 result['attempts'].append({'port':port.device,'vid':port.vid,'pid':port.pid,'serial_number':port.serial_number,'returncode':run.returncode})
 print(json.dumps(result['attempts'][-1]),flush=True)
 if run.returncode==0: break
 if port.pid==0x1001: break
 time.sleep(2)
(out/'flash.json').write_text(json.dumps(result,indent=2)+'\n')
assert result['attempts'][-1]['returncode']==0,'Flash failed; inspect saved esptool logs'
print('Both OTA slots flashed; see saved logs for verification',flush=True)
