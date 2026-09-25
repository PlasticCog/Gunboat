"""Cross-check the exporter by executing the original 8086 transform in Unicorn."""
import json
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16
from unicorn.x86_const import *
from gunboat_formats import UNPACKED_IMAGE, ROOT, read_datac, asset_path
from export_original_world import decode_tiles, daytime_color


def main():
    image = UNPACKED_IMAGE.read_bytes()
    tile_bytes = asset_path(read_datac()[83]).read_bytes()
    tiles = decode_tiles(tile_bytes)
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x100000)
    uc.mem_write(0x10000, image)
    uc.mem_write(0x60000, tile_bytes)
    ds = 0x2b730
    count = 0
    for tile in tiles:
        for rotation in range(4):
            uc.mem_write(ds+0xb7e2, bytes([rotation]))
            for x, height, y in tile['vertices']:
                for origin_x, origin_y in [(0,0),(0x2400,0x1800)]:
                    uc.mem_write(ds+0xd9a9, struct.pack('<HH', origin_x, origin_y))
                    uc.reg_write(UC_X86_REG_DS, 0x2b73)
                    uc.reg_write(UC_X86_REG_CS, 0x1919)
                    uc.reg_write(UC_X86_REG_AX, x)
                    uc.reg_write(UC_X86_REG_CX, y)
                    uc.emu_start(0x218a1, 0x218e3, count=50)
                    expected = [(x,y),(y,128-x),(128-x,128-y),(128-y,x)][rotation]
                    expected = tuple((((v & 255)*8)+origin)&65535
                                     for v,origin in zip(expected,(origin_x,origin_y)))
                    actual=(uc.reg_read(UC_X86_REG_AX),uc.reg_read(UC_X86_REG_CX))
                    assert actual == expected, (tile['id'],rotation,x,y,actual,expected)
                    count += 1
    loader_cases = 0
    for tile in tiles:
        n = sum(tile['vertexGroups'])
        for rotation in range(4):
            start = 0
            uc.mem_write(ds+0xd951, bytes([14,10]))
            uc.mem_write(ds+0xd9af, bytes([0,24,16]))
            for group, group_count in enumerate(tile['vertexGroups']):
                if not group_count:
                    continue
                counter = 0xd960 + group*2
                uc.mem_write(ds+counter, b'\0\0')
                uc.mem_write(ds+0xb7e2, bytes([rotation]))
                uc.mem_write(ds+0xb7e5, bytes([n]))
                uc.mem_write(ds+0xd9a9, b'\0\0\0\0')
                uc.reg_write(UC_X86_REG_DS, 0x2b73)
                uc.reg_write(UC_X86_REG_ES, 0x6000)
                uc.reg_write(UC_X86_REG_CS, 0x1919)
                uc.reg_write(UC_X86_REG_SS, 0x8000)
                uc.reg_write(UC_X86_REG_SP, 0xff00)
                uc.mem_write(0x8ff00, struct.pack('<H', 0xfff0))
                uc.reg_write(UC_X86_REG_BX, counter)
                uc.reg_write(UC_X86_REG_CX, group_count)
                uc.reg_write(UC_X86_REG_DI, group*512)
                uc.reg_write(UC_X86_REG_SI, tile['offset']+4+start)
                uc.emu_start(0x217f5, 0x29180, count=30000)
                assert uc.reg_read(UC_X86_REG_IP)==0xfff0
                for i,(x,h,y) in enumerate(tile['vertices'][start:start+group_count]):
                    dst=(group*512+i)*2
                    actual_h=struct.unpack('<H',uc.mem_read(ds+0x1496+dst,2))[0]
                    actual_x=struct.unpack('<H',uc.mem_read(ds+0x1c96+dst,2))[0]
                    actual_y=struct.unpack('<H',uc.mem_read(ds+0x2496+dst,2))[0]
                    rx,ry=[(x,y),(y,128-x),(128-x,128-y),(128-y,x)][rotation]
                    assert (actual_x,actual_h,actual_y)==((rx&255)*32,h,(ry&255)*32)
                    actual_color=uc.mem_read(ds+0x1096+group*512+i,1)[0]&63
                    control=tile_bytes[tile['offset']+4+start+i]
                    assert actual_color==daytime_color(control,start+i)
                    loader_cases += 1
                start += group_count
    report = dict(transformCases=count, loaderVertexCases=loader_cases, mismatches=0,
                  codeStart='physical 0x218A1 / image offset 0x118A1',
                  codeStop='physical 0x218E3 (before RET)',
                  checked='Every tile vertex, four rotations, two world translations',
                  notChecked=['matched-location DOSBox geometry comparison','vertical scale calibration','other lighting-state palettes','map boundary behavior'])
    path=ROOT/'reverse_engineering/out/ghidra/transform-verification.json'
    path.write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))


if __name__=='__main__':
    main()
