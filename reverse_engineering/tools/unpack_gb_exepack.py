#!/usr/bin/env python3
"""Unpack Gunboat's EXEPACK-style DOS executable image.

This emulates only the DOS MZ load state and the packer stub. It stops at the
stub's final far jump, then dumps the already-relocated game image.
"""

from __future__ import annotations

import argparse
import pathlib
import struct

from unicorn import UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_INTR, UC_MODE_16, Uc, UcError
from unicorn.x86_const import (
    UC_X86_REG_BX,
    UC_X86_REG_CS,
    UC_X86_REG_DS,
    UC_X86_REG_EFLAGS,
    UC_X86_REG_ES,
    UC_X86_REG_IP,
    UC_X86_REG_SP,
    UC_X86_REG_SS,
)


def word(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def parse_mz(data: bytes) -> dict[str, int]:
    if data[:2] != b"MZ":
        raise ValueError("not a DOS MZ executable")
    return {
        "cblp": word(data, 2),
        "cp": word(data, 4),
        "cparhdr": word(data, 8),
        "ss": word(data, 14),
        "sp": word(data, 16),
        "ip": word(data, 20),
        "cs": word(data, 22),
    }


def unpack(exe_path: pathlib.Path, out_dir: pathlib.Path, dump_size: int) -> None:
    data = exe_path.read_bytes()
    hdr = parse_mz(data)
    header_bytes = hdr["cparhdr"] * 16
    image = data[header_bytes:]

    psp = 0x0FF0
    loadseg = psp + 0x10
    base = loadseg << 4
    memsize = 0x80000

    uc = Uc(UC_ARCH_X86, UC_MODE_16)
    uc.mem_map(0, memsize)
    uc.mem_write(base, image)
    uc.mem_write(psp << 4, b"\xcd\x20" + bytes(254))

    uc.reg_write(UC_X86_REG_CS, loadseg + hdr["cs"])
    uc.reg_write(UC_X86_REG_IP, hdr["ip"])
    uc.reg_write(UC_X86_REG_DS, psp)
    uc.reg_write(UC_X86_REG_ES, psp)
    uc.reg_write(UC_X86_REG_SS, loadseg + hdr["ss"])
    uc.reg_write(UC_X86_REG_SP, hdr["sp"])
    uc.reg_write(UC_X86_REG_EFLAGS, 0x202)

    state: dict[str, int | bool] = {"count": 0}

    def hook_code(uc: Uc, _address: int, size: int, state: dict[str, int | bool]) -> None:
        state["count"] = int(state["count"]) + 1
        cs = uc.reg_read(UC_X86_REG_CS)
        ip = uc.reg_read(UC_X86_REG_IP)
        linear = (cs << 4) + ip
        op = bytes(uc.mem_read(linear, min(size, 8)))

        # EXEPACK final handoff: CS: ljmp far ptr [BX], encoded as 2E FF 2F.
        if op[:3] == b"\x2e\xff\x2f":
            bx = uc.reg_read(UC_X86_REG_BX)
            ptr = (cs << 4) + bx
            target_ip = struct.unpack("<H", uc.mem_read(ptr, 2))[0]
            target_cs = struct.unpack("<H", uc.mem_read(ptr + 2, 2))[0]
            state.update(
                {
                    "done": True,
                    "handoff_cs": cs,
                    "handoff_ip": ip,
                    "target_cs": target_cs,
                    "target_ip": target_ip,
                    "runtime_ss": uc.reg_read(UC_X86_REG_SS),
                    "runtime_sp": uc.reg_read(UC_X86_REG_SP),
                }
            )
            uc.emu_stop()

        if int(state["count"]) > 2_000_000:
            state["too_many"] = True
            uc.emu_stop()

    def hook_intr(uc: Uc, intno: int, state: dict[str, int | bool]) -> None:
        state["interrupt"] = intno
        uc.emu_stop()

    uc.hook_add(UC_HOOK_CODE, hook_code, state)
    uc.hook_add(UC_HOOK_INTR, hook_intr, state)

    try:
        uc.emu_start(((loadseg + hdr["cs"]) << 4) + hdr["ip"], memsize - 1)
    except UcError as err:
        raise RuntimeError(f"emulation failed: {err}") from err

    if not state.get("done"):
        raise RuntimeError(f"unpacker did not reach handoff: {state}")

    out_dir.mkdir(parents=True, exist_ok=True)
    image_out = out_dir / "gunboat_unpacked_image.bin"
    rough_exe_out = out_dir / "gunboat_unpacked_rough.exe"
    image_dump = bytes(uc.mem_read(base, dump_size))
    image_out.write_bytes(image_dump)

    total_size = 0x200 + len(image_dump)
    pages = (total_size + 511) // 512
    last_page = total_size % 512
    mz = bytearray(0x200)
    struct.pack_into(
        "<2sHHHHHHHHHHHHH",
        mz,
        0,
        b"MZ",
        last_page,
        pages,
        0,
        0x20,
        0,
        0xFFFF,
        int(state["runtime_ss"]) - loadseg,
        int(state["runtime_sp"]),
        0,
        int(state["target_ip"]),
        int(state["target_cs"]) - loadseg,
        0x1E,
        0,
    )
    rough_exe_out.write_bytes(mz + image_dump)

    print(f"entry: {(int(state['target_cs']) - loadseg):04X}:{int(state['target_ip']):04X}")
    print(f"stack: {(int(state['runtime_ss']) - loadseg):04X}:{int(state['runtime_sp']):04X}")
    print(f"wrote: {image_out}")
    print(f"wrote: {rough_exe_out}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("exe", type=pathlib.Path, nargs="?", default=pathlib.Path("Original DOS version/GB.EXE"))
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("reverse_engineering/out"))
    parser.add_argument("--dump-size", type=lambda value: int(value, 0), default=0x30000)
    args = parser.parse_args()
    unpack(args.exe, args.out, args.dump_size)


if __name__ == "__main__":
    main()
