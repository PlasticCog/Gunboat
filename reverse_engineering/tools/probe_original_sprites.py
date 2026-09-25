"""Run the original sprite-cache builder to inspect object graphics."""
import struct
from PIL import Image
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import *
from gunboat_formats import ROOT, UNPACKED_IMAGE, asset_path, read_datac

def render(kind, bank=77, scale=47, angle=0, screen=False):
    image=UNPACKED_IMAGE.read_bytes()
    records=read_datac()
    uc=Uc(UC_ARCH_X86,UC_MODE_16);uc.mem_map(0,0x100000)
    uc.mem_write(0x10000,image)
    ds=0x2b730
    uc.mem_write(ds+0x6e54,asset_path(records[bank]).read_bytes())
    uc.mem_write(ds+0x53a8,asset_path(records[bank+1]).read_bytes())
    uc.mem_write(ds+0xd883,struct.pack('<HH',0x7000,0x7000))
    uc.mem_write(ds+0xd863,bytes([1,scale,0]))
    uc.mem_write(ds+0xd868,bytes([kind]))
    uc.mem_write(ds+0xd86a,bytes([angle]))
    cache=struct.unpack_from('<H',image,0xe9cd)[0]
    uc.mem_write(0x70000+cache,b'\xff\xff\xff')
    # Give the scratch sprite cache enough space; this changes only the
    # allocation limit table, not sprite-building instructions or source data.
    uc.mem_write(0x10000+0xe9cd+2,struct.pack('<H',0xf000))
    for reg,val in [(UC_X86_REG_DS,0x2b73),(UC_X86_REG_CS,0x1919),(UC_X86_REG_ES,0x7000),
                    (UC_X86_REG_SS,0x8000),(UC_X86_REG_SP,0xff00)]:uc.reg_write(reg,val)
    uc.mem_write(0x8ff00,struct.pack('<H',0xfff0))
    uc.emu_start(0x1ec7b,0x29180,count=2000000)
    assert uc.reg_read(UC_X86_REG_IP)==0xfff0,hex(uc.reg_read(UC_X86_REG_IP))
    pointer=struct.unpack('<H',uc.mem_read(ds+0xd881,2))[0]
    raw=bytes(uc.mem_read(0x70000+pointer,60000))
    if screen:
        uc.mem_write(ds+0xd9b8,struct.pack('<H',0x9000))
        uc.mem_write(ds+0xeed2,struct.pack('<H',0x13))
        uc.mem_write(ds+0xd193,bytes([100]))
        uc.mem_write(ds+0xb82d,bytes([128]))
        uc.mem_write(ds+0x4e00,bytes([72]))
        uc.mem_write(ds+0x4eb5,b'\0')
        uc.mem_write(ds+0x4f6a,b'\0')
        uc.reg_write(UC_X86_REG_BX,0)
        uc.reg_write(UC_X86_REG_SP,0xff00)
        uc.mem_write(0x8ff00,struct.pack('<H',0xfff0))
        uc.emu_start(0x1ee01,0x29180,count=2000000)
        assert uc.reg_read(UC_X86_REG_IP)==0xfff0
        return bytes(uc.mem_read(0x90000,64000))
    return raw

if __name__=='__main__':
    for kind in (57,58,59):
        raw=render(kind)
        print(kind,list(raw[:30]),'nonzero',sum(x!=0 for x in raw[30:]))
        (ROOT/f'reverse_engineering/out/sprite-{kind}.bin').write_bytes(raw)
