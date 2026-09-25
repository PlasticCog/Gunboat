"""Differential test: compiled C++ against the unmodified original DOS instructions.

Unicorn is a development-time oracle only; the native game does not link it.
Calls outside the tested kernels (cockpit drawing/crew messages) are skipped.
"""
import ctypes, json, os, pathlib, random, struct, subprocess, time
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import *

ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT / 'gunboat-port/out'
EXE = (ROOT / 'reverse_engineering/out/gunboat_unpacked_image.bin').read_bytes()
DS = 0x2b730
TRACKED = list(range(0xb808, 0xb80e)) + list(range(0xb816, 0xb82a)) + [0xd632, 0xd982, 0xd983]
TRACKED += [0xc12d, 0xc12e, 0xc8fd, 0xc8fe, 0xd521, 0xd522, 0xd503, 0xd504]

def main():
    compiler = pathlib.Path('C:/msys64/ucrt64/bin/g++.exe')
    os.environ['PATH'] = str(compiler.parent) + os.pathsep + os.environ['PATH']
    library = OUT / 'physics-verification.dll'
    subprocess.run([str(compiler), '-std=c++17', '-O2', '-Wall', '-Wextra', '-shared', '-static',
                    str(ROOT/'gunboat-port/tests/original_physics_bridge.cpp'),
                    str(ROOT/'gunboat-port/src/original_physics.cpp'), '-o', str(library)], check=True)
    dll = ctypes.CDLL(str(library))
    data = (ctypes.c_uint8 * len(EXE)).from_buffer_copy(EXE)
    dll.setup.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_uint]
    dll.setup(data, len(EXE))
    dll.run.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_int, ctypes.c_int]
    dll.run.restype = ctypes.c_uint32
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x100000)
    uc.mem_write(0x10000, EXE)
    # Skip only presentation/notification calls outside the measured kernels.
    skips = {0x9df3:5}
    def hook(uc, addr, size, _):
        offset = addr - 0x10000
        if offset in skips: uc.reg_write(UC_X86_REG_IP, (offset - 0x9190 + skips[offset]) & 65535)
        if offset == 0x9212: uc.reg_write(UC_X86_REG_IP, 0x9245 - 0x9190)
        if offset == 0x934f: uc.reg_write(UC_X86_REG_IP, 0x9382 - 0x9190)
    uc.hook_add(UC_HOOK_CODE, hook)
    rng = random.Random(7101989)
    counts = {}
    base = bytearray(65536)
    base[:min(65536, len(EXE)-0x1b730)] = EXE[0x1b730:0x2b730]
    def sample():
        m = bytearray(base)
        m[0xb808:0xb80a] = bytes(rng.choice([0,1,1,1,2,3,20,127,128,252,253,254,255]) for _ in range(2))
        m[0xb816:0xb818] = bytes(rng.randrange(8,104) for _ in range(2))
        m[0xb81c:0xb81e] = bytes([103,103])
        m[0xb818] = rng.randrange(126) | rng.choice([0,128])
        m[0xd632] = rng.randrange(3); m[0xd52c] = rng.randrange(4)
        m[0xb81e:0xb822] = bytes(rng.randrange(256) for _ in range(4))
        m[0xb822:0xb826] = bytes(rng.randrange(8) for _ in range(4))
        m[0xb826:0xb829] = bytes(rng.randrange(256) for _ in range(3))
        m[0xb819] = rng.randrange(16)
        m[0xb82a] = rng.randrange(256)
        struct.pack_into('<H',m,0xb82b,rng.randrange(65536))
        m[0xd967] = rng.randrange(1,10); m[0xd968] = rng.randrange(1,32); m[0xd969] = rng.randrange(4)
        struct.pack_into('<H',m,0xb4ff,rng.randrange(4))
        m[0x8a]=rng.randrange(256)
        m[0xd506:0xd508] = bytes(rng.randrange(4) for _ in range(2))
        m[0xd50a:0xd50c] = bytes(rng.randrange(4) for _ in range(2))
        for p in [0xb80a,0xb80c]: struct.pack_into('<H',m,p,rng.choice([0,1,32,1024,50500]))
        for p in [0xc12d,0xc8fd]: struct.pack_into('<H',m,p,rng.randrange(0x400,0x4001))
        struct.pack_into('<h',m,0xd982,rng.randrange(-200,201))
        m[0x86]=rng.randrange(1,5);m[0xd6b4]=rng.choice([0,0,1]);m[0x8b]=rng.randrange(256)
        return m
    def check(operation,arg,m):
        native = (ctypes.c_uint8 * 65536).from_buffer_copy(m)
        result = dll.run(native, operation, arg)
        uc.mem_write(DS,bytes(m))
        for r,v in [(UC_X86_REG_DS,0x2b73),(UC_X86_REG_CS,0x1919),(UC_X86_REG_SS,0x8000),
                    (UC_X86_REG_SP,0xff00),(UC_X86_REG_SI,arg*2 if operation==6 else arg),(UC_X86_REG_CX,arg),
                    (UC_X86_REG_BX,m[0xd52c]&3),(UC_X86_REG_EFLAGS,2)]:uc.reg_write(r,v)
        start,stop = [(0x9b69,0x9b80),(0xb6b9,0x19180),(0xb451,0xb4a8),
                      (0x11078,0x11164),(0x11270,0x1129c),(0x11270,0x19180),(0x10b71,0x19180),(0x9798,0x19180)][operation]
        uc.mem_write(0x8ff00,struct.pack('<H',0xfff0))
        uc.emu_start(0x10000+start,0x10000+stop,count=20000)
        if uc.reg_read(UC_X86_REG_IP) != ((stop-0x9190)&65535):
            raise AssertionError(('unfinished',operation,hex(uc.reg_read(UC_X86_REG_IP))))
        actual = uc.mem_read(DS,65536)
        tracked = TRACKED + (list(range(0xb82a,0xb82e)) + list(range(0xd967,0xd96a)) + [0xd193] if operation == 5 else [])
        bad=[(hex(i), m[i], native[i], actual[i]) for i in tracked if native[i]!=actual[i]]
        if bad:raise AssertionError((operation,arg,bad))
        if operation==1:
            actualThrust=uc.reg_read(UC_X86_REG_CX)|(uc.reg_read(UC_X86_REG_DX)<<16)
            assert actualThrust==result,('thrust',arg,result,actualThrust)
        if operation==6:
            actualContact=actual[0xd8ff]|(actual[0xd8fe]<<8)
            assert result==actualContact,('collision',arg,result,actualContact, m[0xd191],m[0xb81e],[(m[0x1096+j],hex(struct.unpack_from('<H',m,0x2c96+j*2)[0])) for j in range(arg-2,arg+3)])
    for operation,name in enumerate(['pilot-controls','engine-thrust-fuel','propulsion-turning','position-boundaries','pitch-decay','camera-pitch-bob']):
        for i in range(1200):
            m=sample(); arg=rng.randrange(32) if operation==0 else rng.randrange(2)
            check(operation,arg,m)
        counts[name]=1200
        print(name, '1200 matches',flush=True)
    for i in range(12000):
        m=sample();idx=rng.randrange(2,1022)
        m[0xd8fe]=m[0xd8ff]=0
        m[0xd191]=rng.randrange(256)
        for j in range(idx-2,idx+3):
            m[0x1096+j]=rng.choice([0,2,6,10,14,66,70,130,134,138])
            struct.pack_into('<H',m,0x2c96+j*2,rng.randrange(65536))
        check(6,idx,m)
    counts['shore-edge-contact']=12000
    print('shore-edge-contact 12000 matches',flush=True)
    for i in range(1200):
        m=sample();idx=rng.randrange(2)
        struct.pack_into('<H',m,0xb80a+idx*2,50000)
        m[0xd508+idx]=3
        m[0xd503+idx]=rng.choice([0x10,0x11,0x12,0x13,0x14,0x15,0x90])
        m[0xd521+idx]=rng.choice([0x10,0x11,0x14,0x15,0x90])
        check(7,idx,m)
    counts['engine-switch']=1200
    print('engine-switch 1200 matches',flush=True)
    report=dict(cases=sum(counts.values()),mismatches=0,groups=counts,
                oracle='Unmodified GB.EXE x86 kernels executed with Unicorn; native C++ built with GCC',
                exclusions=['Frame wall-clock pacing','Collision damage and renderer candidate integration','AI','wake rendering','crew messages and cockpit redraw'])
    (OUT/'physics-verification.json').write_text(json.dumps(report,indent=2))
    print(json.dumps(report,indent=2))

if __name__=='__main__': main()

