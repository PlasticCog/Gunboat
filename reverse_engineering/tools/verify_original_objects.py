"""Check tile object positions against execution of Gunboat's original loader."""
import struct,json
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import *
from gunboat_formats import ROOT,UNPACKED_IMAGE,read_datac,asset_path
from export_original_world import decode_tiles

def main():
    image=UNPACKED_IMAGE.read_bytes();tile_bytes=asset_path(read_datac()[83]).read_bytes()
    uc=Uc(UC_ARCH_X86,UC_MODE_16);uc.mem_map(0,0x100000);uc.mem_write(0x10000,image)
    uc.mem_write(0x60000,tile_bytes);ds=0x2b730;cases=0
    for tile in decode_tiles(tile_bytes):
        for rotation in range(4):
            uc.mem_write(ds+0xf280,struct.pack('<HH',0,0x6000))
            uc.mem_write(ds+0xd960,b'\0'*4)
            uc.mem_write(ds+0xd9a4,b'\0'*3)
            uc.mem_write(ds+0xd9a9,struct.pack('<HH',3072,2048))
            uc.mem_write(ds+0xb959,struct.pack('<H',199))
            for r,v in [(UC_X86_REG_DS,0x2b73),(UC_X86_REG_CS,0x1919),
                        (UC_X86_REG_SS,0x8000),(UC_X86_REG_SP,0xff00),
                        (UC_X86_REG_AX,tile['id']|(rotation<<6))]:uc.reg_write(r,v)
            uc.mem_write(0x8ff00,struct.pack('<H',0xfff0))
            uc.emu_start(0x2171f,0x29180,count=100000)
            assert uc.reg_read(UC_X86_REG_IP)==0xfff0
            assert uc.mem_read(ds+0xd9a4,1)[0]==tile['objectCount']
            start=tile['offset']+4+4*len(tile['vertices'])
            for i in range(tile['objectCount']):
                kind,x,y,_=tile_bytes[start+4*i:start+4*i+4]
                j=(199+i)*2
                actual_type=struct.unpack('<H',uc.mem_read(ds+0xb95d+j,2))[0]
                actual_x=struct.unpack('<H',uc.mem_read(ds+0xc12d+j,2))[0]
                actual_y=struct.unpack('<H',uc.mem_read(ds+0xc8fd+j,2))[0]
                rx,ry=[(x,y),(y,128-x),(128-x,128-y),(128-y,x)][rotation]
                assert (actual_type&255,actual_x,actual_y)==(kind,3072+(rx&255)*8,2048+(ry&255)*8)
                assert actual_type>>8==((start+4*i)&7)
                cases+=1
    report=dict(tileObjectCases=cases,mismatches=0,loader='image 0x1171F',
        notes='Original loader writes type flags and rotated X/Y to the same three arrays used by authored map objects.')
    (ROOT/'reverse_engineering/out/object-placement-verification.json').write_text(json.dumps(report,indent=2))
    print(report)

if __name__=='__main__':main()
