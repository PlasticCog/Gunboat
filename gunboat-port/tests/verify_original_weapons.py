"""Original-code oracle for station firing and integer projectile launch."""
from pathlib import Path
import csv
import json
import struct
import subprocess
import sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import *
ROOT = Path(__file__).resolve().parents[2]
IMAGE = ROOT / 'reverse_engineering/out/gunboat_unpacked_image.bin'
NATIVE = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'gunboat-port/out/original_weapons_trace.exe'
DS = 0x2b730


def compare(ticking):
    args = [str(NATIVE),str(IMAGE)] + (['ticks'] if ticking else [])
    rows = csv.reader(subprocess.check_output(args,text=True).splitlines())
    image = IMAGE.read_bytes()
    count = fired = 0
    for row in rows:
        actual = list(map(int,row)); i = actual[0]; station = 2+i%3
        uc=Uc(UC_ARCH_X86,UC_MODE_16); uc.mem_map(0,0x100000); uc.mem_write(0x10000,image)
        def b(p,v): uc.mem_write(DS+p,bytes([v&255]))
        def w(p,v): uc.mem_write(DS+p,struct.pack('<H',v&65535))
        def rb(p): return uc.mem_read(DS+p,1)[0]
        def rw(p): return struct.unpack('<H',uc.mem_read(DS+p,2))[0]
        for m in range(3):
            b([0xb804,0xb807,0xb806][m],(i//3+m)%3)
            b([0xb837,0xb836,0xb838][m],i*37+m*71)
            b([0xb81f,0xb820,0xb821][m],i*13+m*79)
            b([0xb823,0xb824,0xb825][m],i*11+m*3)
            b([0xd525,0xd529,0xd52e][m],i%17==0)
            b([0xd526,0xd52a,0xd52f][m],i%19==0)
        b(0xb830,8 if i%7 else i); b(0xb831,48 if i%11 else i)
        b(0xd644,i*3); b(0xb7f1,i%5); b(0xd70c,i)
        b(0xb82c,i*3); b(0xb82d,i*17); w(0xc12d,i*973); w(0xc8fd,i*829)
        uc.mem_write(DS+0xb839,bytes(4)); uc.mem_write(DS+0xd1dc,bytes(256))
        for p in range(32): b(0xd1bc+p,int(i%4==0 or p<i%32))
        # Sound effects are separately tested. Stub their entry here, record ID.
        sound=[]
        uc.mem_write(0x22ef5,b'\xcb')
        def hook(u,address,size,_):
            if address == 0x22ef5: sound.append(u.reg_read(UC_X86_REG_AX))
        uc.hook_add(UC_HOOK_CODE,hook,begin=0x22ef5,end=0x22ef5)
        for reg,value in [(UC_X86_REG_CS,0x1919),(UC_X86_REG_DS,0x2b73),
                          (UC_X86_REG_SS,0xa000),(UC_X86_REG_SP,0xff00)]:uc.reg_write(reg,value)
        uc.mem_write(0xaff00,struct.pack('<H',0xfff0))
        uc.emu_start(0x10000+[0x9f4d,0x9ed5,0x9e84][station-2],0x29180,count=100000)
        assert uc.reg_read(UC_X86_REG_IP)==0xfff0
        slot = 0
        if sound:
            for p in range(31,-1,-1):
                if not(i%4==0 or p<i%32):slot=p;break
        weapon = rb(0xd1dc+slot*8) if sound else 0
        if ticking:
            # HUD notification and the impact world changes are outside the
            # translated routines' scope. Isolate the original timer updates.
            uc.mem_write(0x191b7,b'\xc3')
            uc.mem_write(0x1ca7d,b'\xc3')
            b(0xd8bc,int(i%3==0))
            for entry in (0xae89,0x11388,0xca5c):
                uc.reg_write(UC_X86_REG_SP,0xff00)
                uc.mem_write(0xaff00,struct.pack('<H',0xfff0))
                uc.emu_start(0x10000+entry,0x29180,count=100000)
                assert uc.reg_read(UC_X86_REG_IP)==0xfff0
        expected=[i,int(bool(sound)),sound[0] if sound else 0,weapon,slot]
        expected += [rb(p) for p in (0xb830,0xb831,0xd644,0xb839,0xb83a,0xb83b,0xb83c)]
        for p in range(32):
            offset=0xd1dc+p*8
            expected += [rb(0xd1bc+p),rb(offset),rw(offset+1),rw(offset+3),rb(offset+5),rb(offset+6),rb(offset+7)]
        if expected != actual:
            raise AssertionError((i,[(p,a,b)for p,(a,b) in enumerate(zip(expected,actual))if a!=b][:20]))
        count+=1; fired+=bool(sound)
    return count, fired


def main():
    first, shots = compare(False)
    second, _ = compare(True)
    report={'cases':first+second,'shots':shots*2,'result':'exact',
            'scope':'station firing, reload/flash/projectile ticks and all 32 original projectile slots'}
    (ROOT/'gunboat-port/out/original-weapons-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))


if __name__=='__main__':main()
