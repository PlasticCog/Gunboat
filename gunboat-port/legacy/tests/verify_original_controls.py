"""Compare native input masks to the original DOS keyboard interrupt routine.

Unicorn is an offline development oracle. Neither it nor GB.EXE instructions are
executed by the native game. Run after building out/controls-test.exe:

  python gunboat-port/legacy/tests/verify_original_controls.py
"""
from pathlib import Path
import hashlib
import json
import os
import struct
import subprocess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN
from unicorn.x86_const import (
    UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_CS, UC_X86_REG_SS,
    UC_X86_REG_SP,
)

ROOT = Path(__file__).resolve().parents[3]
IMAGE = ROOT / "reverse_engineering/out/gunboat_unpacked_image.bin"
data = IMAGE.read_bytes()
DS = 0x2B730


def oracle(sequence):
    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, 0x100000)
    uc.mem_write(0x10000, data)
    uc.mem_write(DS + 0xDA39, bytes(12))
    current = 0
    uc.hook_add(UC_HOOK_INSN,
                lambda u, port, size, _: current if port == 0x60 else 0,
                None, 1, 0, UC_X86_INS_IN)
    uc.hook_add(UC_HOOK_INSN, lambda *args: None,
                None, 1, 0, UC_X86_INS_OUT)
    masks = []
    for current in sequence:
        for register, value in [(UC_X86_REG_CS, 0x221B),
                                (UC_X86_REG_SS, 0x8000),
                                (UC_X86_REG_SP, 0xFF00)]:
            uc.reg_write(register, value)
        # Execute original ISR through its final IRET (stop immediately before).
        uc.emu_start(0x22C4C, 0x22E45, count=1000)
        masks.append(uc.mem_read(DS + 0xDA43, 1)[0])
    return masks


def main():
    # Every ordinary XT key emitted by the SDL host, including legacy diagonals
    # and the original alternate direction keys. Extended list is valid E0 keys.
    keys = list(range(1, 0x54)) + [0x57, 0x58]
    extended = [0x1C, 0x1D, 0x35, 0x37, 0x38, 0x47, 0x48, 0x49,
                0x4B, 0x4D, 0x4F, 0x50, 0x51, 0x52, 0x53]
    cases = [[key, key | 0x80] for key in keys]
    cases += [[0xE0, key, 0xE0, key | 0x80] for key in extended]
    # Ordinary simultaneous steering, throttle and neutral inputs.
    cases += [[0x48, 0x4B, 0x1C, 0xCB, 0x9C, 0xC8],
              [0x50, 0x4D, 0xCD, 0xD0]]
    env = dict(os.environ)
    env["PATH"] = r"C:\msys64\ucrt64\bin;" + env.get("PATH", "")
    run = subprocess.run([str(ROOT / "gunboat-port/out/controls-test.exe"), "--trace"],
                         input="\n".join(" ".join(f"{b:02x}" for b in case) for case in cases),
                         text=True, capture_output=True, check=True, env=env)
    native = [[int(x) for x in line.split(",")] for line in run.stdout.splitlines()]
    assert len(native) == len(cases)
    mismatches = []
    for sequence, actual in zip(cases, native):
        expected = oracle(sequence)
        if expected != actual:
            mismatches.append({"sequence": sequence, "native": actual, "original": expected})

    # The DOS command table is indexed by translated ASCII, not physical XT scan.
    dispatch = {
        "Tab": (9, 0x9A4E), "X": (ord("X"), 0x98F9),
        "Z": (ord("Z"), 0x98F2), "C": (ord("C"), 0x9900),
        "V": (ord("V"), 0x9950), "N": (ord("N"), 0x9986),
        "B": (ord("B"), 0x99C3), "M": (ord("M"), 0x98DA),
        "Slash": (ord("/"), 0x9A01), "Period": (ord("."), 0x98E6),
        "Equals": (ord("="), 0x9A0D), "Plus": (ord("+"), 0x9A0D),
        "D": (ord("D"), 0x9AC0), "Comma": (ord(","), 0x9A9D),
        "Minus": (ord("-"), 0x9693), "Underscore": (ord("_"), 0x9693),
    }
    for name, (character, target) in dispatch.items():
        original = struct.unpack_from("<H", data, 0x941E + character * 2)[0] + 0x9190
        assert original == target, (name, original, target)
    fkeys = [0x96B0, 0x973E, 0x96FB, 0x97F7, 0x97D9,
             0x97E8, 0x987E, 0x9839, 0x95F5, 0x95CC]
    for i, target in enumerate(fkeys):
        assert struct.unpack_from("<H", data, 0x28D45 + i*2)[0] + 0x9190 == target
    report = {
        "image_sha256": hashlib.sha256(data).hexdigest(),
        "keyboard_isr_image_offset": "0x12C4C",
        "held_mask_ds_offset": "0xDA43",
        "input_sequences": len(cases),
        "byte_transitions": sum(map(len,cases)),
        "native_original_mismatches": mismatches,
        "ascii_dispatch_entries_verified": len(dispatch),
        "function_dispatch_entries_verified": len(fkeys),
        "limits": ["Tests cover unmodified physical keys and ordinary simultaneous arrows.",
                   "Native input deliberately isolates Ctrl/Alt remaps and enhancement shortcuts.",
                   "Native input preserves a held direction when one of two equivalent keys is released; DOS clears the shared bit."],
    }
    path = ROOT / "gunboat-port/out/original-controls-verification.json"
    path.write_text(json.dumps(report,indent=2) + "\n")
    print(json.dumps(report,indent=2))
    assert not mismatches, "Native controls differ from original keyboard ISR"


if __name__ == "__main__":
    main()
