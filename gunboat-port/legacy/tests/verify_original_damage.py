"""Isolate the original weapon-to-object damage transition, excluding world side effects."""
from pathlib import Path
import csv, json, struct, subprocess, sys
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import *
ROOT=Path(__file__).resolve().parents[3]
IMAGE=ROOT/'reverse_engineering/out/gunboat_unpacked_image.bin'
NATIVE=Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'gunboat-port/out/original_damage_trace.exe'
DS=0x2b730

def main():
    rows=csv.reader(subprocess.check_output([str(NATIVE),str(IMAGE)],text=True).splitlines())
    image=IMAGE.read_bytes();count=0
    for row in rows:
        kind,flags,classification,weapon,newkind,newflags,sound=map(int,row)
        u=Uc(UC_ARCH_X86,UC_MODE_16);u.mem_map(0,0x100000);u.mem_write(0x10000,image)
        def byte(p,v):u.mem_write(DS+p,bytes([v]))
        byte(0xb95d+0x100,kind);byte(0xb95e+0x100,flags)
        byte(0x5401+kind,0);byte(0x541b+kind*2,classification)
        byte(0xb7f8,flags&0x38);byte(0xb7ea,weapon);byte(0xd74c,kind)
        # Isolate this transition from tile changes, object allocation,
        # radio/score callbacks and the separately tested sound sequencer.
        for p in (0xcde1,0xcd2f,0xcce1,0xa6f5):u.mem_write(0x10000+p,b'\xc3')
        u.mem_write(0x200ba,bytes.fromhex('bb4400c3'))
        u.mem_write(0x22ef5,b'\xcb')
        sounds=[]
        def hook(uc,address,size,_):sounds.append(uc.reg_read(UC_X86_REG_AX))
        u.hook_add(UC_HOOK_CODE,hook,begin=0x22ef5,end=0x22ef5)
        for reg,value in [(UC_X86_REG_CS,0x1919),(UC_X86_REG_DS,0x2b73),(UC_X86_REG_SS,0xa000),
                          (UC_X86_REG_SP,0xff00),(UC_X86_REG_AX,kind),(UC_X86_REG_SI,0x100),
                          (UC_X86_REG_BX,0)]:u.reg_write(reg,value)
        u.mem_write(0xaff00,struct.pack('<H',0xfff0))
        u.emu_start(0x1cb8c,0x29180,count=10000)
        assert u.reg_read(UC_X86_REG_IP)==0xfff0
        actual=list(u.mem_read(DS+0xba5d,2))+[sounds[0] if sounds else 0]
        if actual != [newkind,newflags,sound]:raise AssertionError((row,actual))
        count+=1
    report={'cases':count,'result':'exact','scope':'object kind/flags and sound trigger; world side effects excluded'}
    (ROOT/'gunboat-port/out/original-damage-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report))
if __name__=='__main__':main()
