"""Reconcile only this test card's optional FSINFO free-count hint with its verified FAT."""
import ctypes, hashlib, json, struct
from ctypes import wintypes as W
from pathlib import Path
k = ctypes.WinDLL('kernel32', use_last_error=True)
k.CreateFileW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,W.LPVOID,W.DWORD,W.DWORD,W.HANDLE];k.CreateFileW.restype=W.HANDLE
k.ReadFile.argtypes=[W.HANDLE,W.LPVOID,W.DWORD,ctypes.POINTER(W.DWORD),W.LPVOID]
k.WriteFile.argtypes=[W.HANDLE,W.LPCVOID,W.DWORD,ctypes.POINTER(W.DWORD),W.LPVOID]
k.SetFilePointer.argtypes=[W.HANDLE,ctypes.c_long,ctypes.POINTER(ctypes.c_long),W.DWORD]
k.DeviceIoControl.argtypes=[W.HANDLE,W.DWORD,W.LPVOID,W.DWORD,W.LPVOID,W.DWORD,ctypes.POINTER(W.DWORD),W.LPVOID]
k.FlushFileBuffers.argtypes=[W.HANDLE]; k.CloseHandle.argtypes=[W.HANDLE]
out=Path(__file__).resolve().parents[2]/'test-data/field-testing/sd-full/2026-11-19-3.2.8-fix/metadata-check'
out.mkdir(parents=True,exist_ok=True)
name=ctypes.create_unicode_buffer(261);serial=W.DWORD()
assert k.GetVolumeInformationW('F:/',name,261,ctypes.byref(serial),None,None,None,0)
assert name.value=='LEAF VARIO' and serial.value==0x01114FC5
h=k.CreateFileW('\\\\.\\F:',0xc0000000,3,None,3,0,None)
if h==ctypes.c_void_p(-1).value: raise ctypes.WinError(ctypes.get_last_error())
def ioctl(code):
 n=W.DWORD()
 if not k.DeviceIoControl(h,code,None,0,None,0,ctypes.byref(n),None):raise ctypes.WinError(ctypes.get_last_error())
def read(offset,size):
 assert 0<=offset<2**31 and offset%512==0 and size%512==0
 k.SetFilePointer(h,offset,None,0);b=ctypes.create_string_buffer(size);n=W.DWORD()
 if not k.ReadFile(h,b,size,ctypes.byref(n),None) or n.value!=size:raise ctypes.WinError(ctypes.get_last_error())
 return b.raw
locked=False
try:
 if not k.FlushFileBuffers(h):raise ctypes.WinError(ctypes.get_last_error())
 ioctl(0x90018);locked=True;ioctl(0x90020)
 boot=read(0,512)
 assert struct.unpack_from('<I',boot,67)[0]==0x01114FC5
 # FAT directory labels can differ from the boot-sector label; the mounted label was checked above.
 bps=struct.unpack_from('<H',boot,11)[0];spc=boot[13];reserved=struct.unpack_from('<H',boot,14)[0]
 fats=boot[16];fatsectors=struct.unpack_from('<I',boot,36)[0];sectors=struct.unpack_from('<I',boot,32)[0]
 fsisector=struct.unpack_from('<H',boot,48)[0];backup=struct.unpack_from('<H',boot,50)[0]
 assert bps==512 and fats==2 and spc==32 and fsisector==1 and backup==6
 fat=read(reserved*bps,fatsectors*bps)
 assert fat==read((reserved+fatsectors)*bps,fatsectors*bps),'FAT copies differ; refusing to change hints'
 clusters=(sectors-reserved-fats*fatsectors)//spc
 free=sum((entry&0xfffffff)==0 for (entry,) in struct.iter_unpack('<I',fat[8:4*(clusters+2)]))
 assert free==512,'Unexpected allocation count; stop for review'
 changes=[]
 backupInfo=read((backup+fsisector)*bps,bps)
 (out/"backup-fsinfo-unchanged.bin").write_bytes(backupInfo)
 for sector in (fsisector,):
  data=read(sector*bps,bps)
  assert data[:4]==struct.pack('<I',0x41615252) and data[484:488]==struct.pack('<I',0x61417272)
  assert data[508:512]==b'\x00\x00\x55\xaa'
  old=struct.unpack_from('<I',data,488)[0]
  assert old in (0,0xffffffff,free)
  (out/f'before-sector-{sector}.bin').write_bytes(data)
  changed=bytearray(data);struct.pack_into('<I',changed,488,free)
  assert changed[:488]==data[:488] and changed[492:]==data[492:]
  changes.append((sector,old,bytes(changed)))
 for sector,old,data in changes:
  k.SetFilePointer(h,sector*bps,None,0);n=W.DWORD()
  if not k.WriteFile(h,data,len(data),ctypes.byref(n),None) or n.value!=len(data):raise ctypes.WinError(ctypes.get_last_error())
 if not k.FlushFileBuffers(h):raise ctypes.WinError(ctypes.get_last_error())
 for sector,old,data in changes:assert read(sector*bps,bps)==data
 assert read(reserved*bps,fatsectors*bps)==fat
 assert read((reserved+fatsectors)*bps,fatsectors*bps)==fat
 assert read((backup+fsisector)*bps,bps)==backupInfo
 result={'volume_serial':'01114FC5','actual_free_clusters':free,'free_bytes':free*spc*bps,'fat_copies_identical_and_unchanged':True,'fat_sha256':hashlib.sha256(fat).hexdigest(),'hint_changes':[{'sector':sector,'old':old,'new':free} for sector,old,data in changes]}
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
finally:
 if locked:ioctl(0x9001c)
 k.CloseHandle(h)
