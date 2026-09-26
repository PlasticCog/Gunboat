"""Compare native projection and VGA triangle pixels with the original routines."""
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import *
ROOT=Path(__file__).resolve().parents[3]
PORT=ROOT/'gunboat-port'
image=(ROOT/'reverse_engineering/out/gunboat_unpacked_image.bin').read_bytes()
uc=Uc(UC_ARCH_X86,UC_MODE_16);uc.mem_map(0,0x100000);uc.mem_write(0x10000,image)
DS=0x2b730
def w(at,value):uc.mem_write(DS+at,struct.pack('<H',value&65535))
def run(start):
    for reg,value in [(UC_X86_REG_CS,0x1919),(UC_X86_REG_DS,0x2b73),(UC_X86_REG_ES,0x9000),(UC_X86_REG_SS,0x8000),(UC_X86_REG_SP,0xff00)]:uc.reg_write(reg,value)
    uc.mem_write(0x8ff00,b'\xf0\xff');uc.emu_start(0x10000+start,0x29180,count=100000)
    assert uc.reg_read(UC_X86_REG_IP)==0xfff0,hex(uc.reg_read(UC_X86_REG_IP))
random.seed(1923)
vertices=[]
for i in range(3000):
    x,y,cx,cy,heading=[random.randrange(65536) for _ in range(5)]
    if i<512:x=cx=32768;y=cy=32768
    if 512<=i<768:x=cx;y=cy+1
    vertices.append((x,random.randrange(256),y&65535,cx,cy,heading,random.randrange(61)))
triangles=[]
for i in range(1800):
    angles=[random.randrange(65536) for _ in range(3)]
    rows=[random.randrange(30,156) for _ in range(3)]
    if i%4==0:rows[1]=rows[2]
    if i%4==1:rows[0]=rows[1]
    if i%12==0:rows=[rows[0]]*3
    triangles.append(tuple(v for pair in zip(angles,rows) for v in pair)+(random.randrange(1,32),))
payload=struct.pack('<H',len(vertices))+b''.join(struct.pack('<7H',*v) for v in vertices)
payload+=struct.pack('<H',len(triangles))+b''.join(struct.pack('<7H',*v) for v in triangles)
inp=PORT/'out/renderer-input.bin';out=PORT/'out/renderer-output.bin';inp.write_bytes(payload)
subprocess.run([str(PORT/'build/gunboat_legacy.exe'),'--game-dir',str(ROOT/'Original DOS version'),'--renderer-probe',str(inp),str(out)],check=True)
got=out.read_bytes();position=0
for i,(x,h,y,cx,cy,heading,horizon) in enumerate(vertices):
    w(0x1c96,x);w(0x2496,y);w(0x1496,h);w(0x1498,0)
    w(0xd972,cx);w(0xd974,cy);w(0xd190,heading);uc.mem_write(DS+0xd953,bytes([horizon]))
    uc.reg_write(UC_X86_REG_AX,1);uc.reg_write(UC_X86_REG_SI,0);run(0x106b3)
    expected=b''.join(bytes(uc.mem_read(DS+p,2)) for p in [0x2c96,0x3496,0x4496])
    actual=got[position:position+6];position+=6
    assert actual==expected,('projection',i,vertices[i],struct.unpack('<3H',actual),struct.unpack('<3H',expected))
print(f'{len(vertices)} original projection cases match',flush=True)
for i,t in enumerate(triangles):
    for n in range(3):w(0x2c96+n*2,t[n*2]);w(0x3496+n*2,t[n*2+1])
    w(0xd954,t[6]*257);w(0xd8f8,0x10a1e-0x9190)
    uc.reg_write(UC_X86_REG_BX,0);uc.reg_write(UC_X86_REG_DX,0);uc.mem_write(0x90000,bytes(64000));run(0x1080a)
    expected=b''.join(bytes(uc.mem_read(0x90000+y*320+40,256)) for y in range(64,128))
    actual=got[position:position+16384];position+=16384
    assert actual==expected,('triangle',i,t,sum(a!=b for a,b in zip(actual,expected)))
assert position==len(got)
report=dict(projectionCases=len(vertices),trianglePixelCases=len(triangles),mismatches=0)
(PORT/'out/renderer-verification.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
