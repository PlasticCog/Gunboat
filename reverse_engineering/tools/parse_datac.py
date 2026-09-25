#!/usr/bin/env python3
"""Parse and optionally extract Gunboat's DATAA/DATAB chunks."""

from __future__ import annotations

import argparse
import pathlib
import struct


def records(datac: bytes):
    for index in range(0, len(datac), 14):
        chunk = datac[index : index + 14]
        if len(chunk) < 14:
            break
        meta0, meta1, file_word, off_lo, off_hi, len_lo, len_hi = struct.unpack("<7H", chunk)
        if not any(chunk):
            break
        yield {
            "index": index // 14,
            "meta0": meta0,
            "meta1": meta1,
            "file_key": chr(file_word & 0xFF),
            "offset": off_lo + (off_hi << 16),
            "length": len_lo + (len_hi << 16),
        }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=pathlib.Path, default=pathlib.Path("Original DOS version"))
    parser.add_argument("--extract", action="store_true")
    parser.add_argument("--out", type=pathlib.Path, default=pathlib.Path("reverse_engineering/out/assets"))
    args = parser.parse_args()

    datac = (args.base / "DATAC.DAT").read_bytes()
    data_files = {
        "a": (args.base / "DATAA.DAT").read_bytes(),
        "b": (args.base / "DATAB.DAT").read_bytes(),
    }

    if args.extract:
        args.out.mkdir(parents=True, exist_ok=True)

    for rec in records(datac):
        end = rec["offset"] + rec["length"]
        print(
            f"{rec['index']:02d} file={rec['file_key']} "
            f"meta=({rec['meta0']:04X},{rec['meta1']:04X}) "
            f"offset=0x{rec['offset']:06X} length=0x{rec['length']:05X} end=0x{end:06X}"
        )
        if args.extract:
            data = data_files[rec["file_key"]][rec["offset"] : end]
            out_path = args.out / f"{rec['index']:02d}_{rec['file_key']}_{rec['offset']:06X}_{rec['length']:05X}.bin"
            out_path.write_bytes(data)


if __name__ == "__main__":
    main()
