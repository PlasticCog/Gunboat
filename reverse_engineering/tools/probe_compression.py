#!/usr/bin/env python3
"""Probe Gunboat's packed graphics/compression formats.

This is intentionally an investigation tool.  It records what the original
draw code implies about graphic records, tries common DOS-era LZSS variants,
and writes visual artifacts that make false positives easy to reject.
"""

from __future__ import annotations

import argparse
import math
import pathlib
from dataclasses import dataclass

from PIL import Image, ImageDraw

from gunboat_formats import (
    ASSET_DIR,
    ORIGINAL_DIR,
    OUT_DIR,
    AssetRecord,
    apply_orientation,
    asset_path,
    ensure_assets,
    entropy,
    grayscale_palette,
    indexed_palette,
    read_datac,
    render_indexed,
    td3_lzw_rle_decode,
)


PROBE_DIR = OUT_DIR / "compression_probe"


@dataclass(frozen=True)
class GraphicRecordHeader:
    prefix: int
    width: int
    height: int
    secondary_width: int
    secondary_height: int
    secondary_stride: int

    @property
    def plausible(self) -> bool:
        return (
            0 < self.width <= 320
            and 0 < self.height <= 200
            and 0 <= self.secondary_width <= 255
            and 0 <= self.secondary_height <= 200
        )


@dataclass(frozen=True)
class LzssResult:
    name: str
    start: int
    out: bytes
    score: float
    note: str


@dataclass(frozen=True)
class RleResult:
    name: str
    start: int
    out: bytes
    score: float
    note: str


@dataclass(frozen=True)
class Td3Result:
    start: int
    width: int
    lzw: bytes
    pixels: bytes
    score: float
    note: str


def parse_graphic_header(data: bytes, base: int = 0) -> GraphicRecordHeader | None:
    """Parse the header shape used by the draw routine at 0xEE01.

    That routine does `si = record_base + 1`, then reads:
    - byte 0 at si: width
    - byte 1 at si: height
    - word at si+2: optional secondary dimensions
    - byte at si+4: secondary stride/count

    Therefore the first byte at record_base is a cache/control byte, not width.
    """

    if base + 6 > len(data):
        return None
    return GraphicRecordHeader(
        prefix=data[base],
        width=data[base + 1],
        height=data[base + 2],
        secondary_width=data[base + 3],
        secondary_height=data[base + 4],
        secondary_stride=data[base + 5],
    )


def image_from_record_payload(data: bytes, header: GraphicRecordHeader, palette: str = "indexed") -> Image.Image:
    """Render the byte layout consumed by the direct VGA blitter.

    The original code treats bytes base+6..base+29 as line repeat/control bytes
    and starts pixel bytes at base+30.  Zero pixels are transparent in-game; for
    this probe we keep them black.
    """

    width = max(1, header.width)
    height = max(1, header.height)
    payload_offset = 30
    payload = data[payload_offset : payload_offset + width * height]
    payload = payload + bytes(max(0, width * height - len(payload)))
    image = Image.frombytes("P", (width, height), payload)
    image.putpalette(grayscale_palette() if palette == "gray" else indexed_palette())
    return image.convert("RGB")


def lzss_decode(
    data: bytes,
    *,
    start: int,
    literal_bit: int,
    bit_order: str,
    offset_mode: str,
    init_value: int,
    max_output: int,
) -> bytes:
    """Decode a small family of common 4K-window LZSS layouts.

    This is not asserted to be Gunboat's codec; it is a falsification harness
    for the usual Okumura/Nintendo/installer-style variants.
    """

    n = 4096
    f = 18
    threshold = 2
    ring = bytearray([init_value] * n)
    r = n - f
    out = bytearray()
    pos = start
    flags = 0
    mask = 0

    while pos < len(data) and len(out) < max_output:
        if mask == 0:
            flags = data[pos]
            pos += 1
            mask = 0x01 if bit_order == "lsb" else 0x80
        is_literal = (flags & mask) != 0
        if literal_bit == 0:
            is_literal = not is_literal

        if is_literal:
            if pos >= len(data):
                break
            value = data[pos]
            pos += 1
            out.append(value)
            ring[r] = value
            r = (r + 1) & (n - 1)
        else:
            if pos + 1 >= len(data):
                break
            b1 = data[pos]
            b2 = data[pos + 1]
            pos += 2
            if offset_mode == "lohi":
                offset = b1 | ((b2 & 0xF0) << 4)
                length = (b2 & 0x0F) + threshold + 1
            elif offset_mode == "hilo":
                offset = ((b1 & 0xF0) << 4) | b2
                length = (b1 & 0x0F) + threshold + 1
            elif offset_mode == "lohi_inverted_len":
                offset = b1 | ((b2 & 0x0F) << 8)
                length = ((b2 >> 4) & 0x0F) + threshold + 1
            else:
                raise ValueError(offset_mode)
            for i in range(length):
                value = ring[(offset + i) & (n - 1)]
                out.append(value)
                ring[r] = value
                r = (r + 1) & (n - 1)
                if len(out) >= max_output:
                    break

        if bit_order == "lsb":
            mask <<= 1
            if mask > 0x80:
                mask = 0
        else:
            mask >>= 1

    return bytes(out)


def image_score(data: bytes) -> tuple[float, str]:
    """Score whether bytes look like indexed/paletted image output."""

    if len(data) < 128:
        return -100.0, "too short"
    ent = entropy(data)
    zero_ratio = data.count(0) / len(data)
    ff_ratio = data.count(0xFF) / len(data)
    printable = sum(32 <= b < 127 for b in data) / len(data)
    adjacency = sum(1 for a, b in zip(data, data[1:]) if a == b) / max(1, len(data) - 1)
    low_color = sum(b < 64 for b in data) / len(data)
    score = 0.0
    score += adjacency * 28.0
    score += zero_ratio * 10.0
    score += low_color * 5.0
    score -= max(0.0, ent - 7.2) * 10.0
    score -= printable * 4.0
    score -= ff_ratio * 2.0
    note = f"entropy={ent:.3f} zero={zero_ratio:.2%} adjacent={adjacency:.2%} low<64={low_color:.2%}"
    return score, note


def try_lzss_family(data: bytes, cap: int) -> list[LzssResult]:
    results: list[LzssResult] = []
    for start in range(0, min(8, len(data))):
        for literal_bit in (0, 1):
            for bit_order in ("lsb", "msb"):
                for offset_mode in ("lohi", "hilo", "lohi_inverted_len"):
                    for init_value in (0x00, 0x20):
                        try:
                            out = lzss_decode(
                                data,
                                start=start,
                                literal_bit=literal_bit,
                                bit_order=bit_order,
                                offset_mode=offset_mode,
                                init_value=init_value,
                                max_output=cap,
                            )
                        except Exception as exc:
                            results.append(
                                LzssResult(
                                    name=f"{bit_order}/{literal_bit}/{offset_mode}/init{init_value:02x}",
                                    start=start,
                                    out=b"",
                                    score=-999.0,
                                    note=str(exc),
                                )
                            )
                            continue
                        score, note = image_score(out)
                        results.append(
                            LzssResult(
                                name=f"{bit_order}/literal{literal_bit}/{offset_mode}/init{init_value:02x}",
                                start=start,
                                out=out,
                                score=score,
                                note=note,
                            )
                        )
    return sorted(results, key=lambda r: r.score, reverse=True)


def decode_packbits(data: bytes, start: int, max_output: int) -> bytes:
    out = bytearray()
    pos = start
    while pos < len(data) and len(out) < max_output:
        control = int.from_bytes(data[pos : pos + 1], "little", signed=True)
        pos += 1
        if 0 <= control <= 127:
            count = control + 1
            out.extend(data[pos : pos + count])
            pos += count
        elif -127 <= control <= -1:
            if pos >= len(data):
                break
            out.extend(bytes([data[pos]]) * (1 - control))
            pos += 1
        else:
            pass
    return bytes(out[:max_output])


def decode_pcx_rle(data: bytes, start: int, max_output: int) -> bytes:
    out = bytearray()
    pos = start
    while pos < len(data) and len(out) < max_output:
        value = data[pos]
        pos += 1
        if value >= 0xC0 and pos < len(data):
            count = value & 0x3F
            out.extend(bytes([data[pos]]) * count)
            pos += 1
        else:
            out.append(value)
    return bytes(out[:max_output])


def decode_pair_rle(data: bytes, start: int, max_output: int, count_first: bool, count_bias: int) -> bytes:
    out = bytearray()
    pos = start
    while pos + 1 < len(data) and len(out) < max_output:
        a = data[pos]
        b = data[pos + 1]
        pos += 2
        count, value = (a, b) if count_first else (b, a)
        count += count_bias
        if count <= 0 or count > 256:
            break
        out.extend(bytes([value]) * count)
    return bytes(out[:max_output])


def try_rle_family(data: bytes, cap: int) -> list[RleResult]:
    decoders = []
    decoders.append(("packbits", lambda d, s: decode_packbits(d, s, cap)))
    decoders.append(("pcx-c0", lambda d, s: decode_pcx_rle(d, s, cap)))
    for count_first in (True, False):
        for bias in (0, 1):
            label = f"{'count,value' if count_first else 'value,count'}+{bias}"
            decoders.append((label, lambda d, s, cf=count_first, b=bias: decode_pair_rle(d, s, cap, cf, b)))

    results: list[RleResult] = []
    for start in range(0, min(8, len(data))):
        for name, decoder in decoders:
            try:
                out = decoder(data, start)
                score, note = image_score(out)
                expansion = len(out) / max(1, len(data) - start)
                if expansion < 1.15:
                    score -= 8.0
                    note += f" expansion={expansion:.2f}x"
                else:
                    note += f" expansion={expansion:.2f}x"
                results.append(RleResult(name, start, out, score, note))
            except Exception as exc:
                results.append(RleResult(name, start, b"", -999.0, str(exc)))
    return sorted(results, key=lambda r: r.score, reverse=True)


def try_td3_pipeline(data: bytes, cap: int) -> list[Td3Result]:
    widths = [320, 208, 168, 160, 112, 96, 80, 72, 64, 45, 40, 32]
    results: list[Td3Result] = []
    for start in range(0, min(12, len(data))):
        try:
            lzw, pixels = td3_lzw_rle_decode(data, skip=start, max_lzw=cap, max_pixels=cap * 4)
        except Exception as exc:
            results.append(Td3Result(start, 0, b"", b"", -999.0, str(exc)))
            continue

        base_score, image_note = image_score(pixels)
        lzw_expansion = len(lzw) / max(1, len(data) - start)
        pixel_expansion = len(pixels) / max(1, len(data) - start)
        if len(lzw) < 16 or len(pixels) < 128:
            base_score -= 100.0
        if 0.5 <= lzw_expansion <= 8.0:
            base_score += 3.0
        if 2.0 <= pixel_expansion <= 40.0:
            base_score += 3.0

        for width in widths:
            score = base_score
            height = len(pixels) // width if width else 0
            remainder = len(pixels) % width if width else len(pixels)
            if height:
                score += 1.0
            if remainder == 0 and height:
                score += 5.0
            if width <= 320 and 8 <= height <= 240:
                score += 3.0
            note = (
                f"lzw={len(lzw)} ({lzw_expansion:.2f}x) pixels={len(pixels)} "
                f"({pixel_expansion:.2f}x) height={height} rem={remainder}; {image_note}"
            )
            results.append(Td3Result(start, width, lzw, pixels, score, note))
    return sorted(results, key=lambda r: r.score, reverse=True)


def render_bytes_as_strip(data: bytes, width: int = 160, height: int | None = None) -> Image.Image:
    if height is None:
        height = max(1, math.ceil(len(data) / width))
    payload = data[: width * height] + bytes(max(0, width * height - len(data)))
    image = Image.frombytes("P", (width, height), payload)
    image.putpalette(indexed_palette())
    return image.convert("RGB")


def make_montage(entries: list[tuple[str, Image.Image]], path: pathlib.Path) -> None:
    if not entries:
        return
    thumb_w, thumb_h = 160, 100
    label_h = 24
    cols = 4
    rows = math.ceil(len(entries) / cols)
    montage = Image.new("RGB", (cols * thumb_w, rows * (thumb_h + label_h)), "#181818")
    draw = ImageDraw.Draw(montage)
    for i, (label, image) in enumerate(entries):
        x = (i % cols) * thumb_w
        y = (i // cols) * (thumb_h + label_h)
        thumb = image.copy()
        thumb.thumbnail((thumb_w, thumb_h), Image.Resampling.NEAREST)
        montage.paste(thumb, (x, y))
        draw.text((x + 3, y + thumb_h + 4), label[:25], fill="#eeeeee")
    montage.save(path)


def probe_record(rec: AssetRecord, limit_lzss: bool) -> list[str]:
    data = asset_path(rec).read_bytes()
    header = parse_graphic_header(data)
    lines = [
        f"## Record {rec.index:02d} {rec.candidate_name or rec.extracted_name}",
        "",
        f"- Source: `{rec.source_file}` offset `0x{rec.offset:06X}` length `{rec.length}`",
        f"- Entropy: `{entropy(data):.3f}`",
        f"- First 16 bytes: `{data[:16].hex(' ')}`",
    ]
    if header is None:
        lines.append("- Code-derived graphic header: not enough bytes")
        return lines

    lines.append(
        "- Code-derived graphic header: "
        f"prefix `{header.prefix:02X}`, primary `{header.width}x{header.height}`, "
        f"secondary `{header.secondary_width}x{header.secondary_height}`, stride/count `{header.secondary_stride}`"
    )

    if header.plausible and rec.length >= 30:
        image = image_from_record_payload(data, header)
        out_path = PROBE_DIR / f"record_{rec.index:02d}_direct_blit.png"
        image.save(out_path)
        lines.append(f"- Direct-blit preview: `{out_path.relative_to(OUT_DIR.parent)}`")
    else:
        lines.append("- Direct-blit preview: skipped; header is not plausible as a post-depack image record")

    if limit_lzss and rec.length > 1024 and entropy(data) > 7.5:
        cap = max(32768, min(65536, rec.length * 6))
        td3_results = try_td3_pipeline(data, cap)[:8]
        lines.append("- Test Drive 3 LZW+RLE pipeline trials:")
        td3_thumbs: list[tuple[str, Image.Image]] = []
        for result in td3_results:
            lines.append(
                f"  - start `{result.start}` width `{result.width}` -> score `{result.score:.2f}` "
                f"({result.note})"
            )
            label = f"td3 s{result.start} w{result.width}"
            td3_image = apply_orientation(render_indexed(result.pixels[:24000], max(1, result.width or 160)), "flip vertical")
            td3_thumbs.append((label, td3_image))
        make_montage(td3_thumbs, PROBE_DIR / f"record_{rec.index:02d}_td3_lzw_rle_trials.png")

        rle_results = try_rle_family(data, cap)[:6]
        lines.append("- Simple RLE trials:")
        rle_thumbs: list[tuple[str, Image.Image]] = []
        for result in rle_results:
            lines.append(
                f"  - `{result.name}` start `{result.start}` -> {len(result.out)} bytes, "
                f"score `{result.score:.2f}` ({result.note})"
            )
            rle_thumbs.append((f"{result.name} s{result.start}", render_bytes_as_strip(result.out[:16000])))
        make_montage(rle_thumbs, PROBE_DIR / f"record_{rec.index:02d}_rle_trials.png")

        results = try_lzss_family(data, cap)[:8]
        lines.append("- Common LZSS trials:")
        thumbs: list[tuple[str, Image.Image]] = []
        for result in results:
            lines.append(
                f"  - `{result.name}` start `{result.start}` -> {len(result.out)} bytes, "
                f"score `{result.score:.2f}` ({result.note})"
            )
            thumbs.append((f"{result.name} s{result.start}", render_bytes_as_strip(result.out[:16000])))
        make_montage(thumbs, PROBE_DIR / f"record_{rec.index:02d}_lzss_trials.png")
    return lines


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--records", nargs="*", type=int, default=[0, 7, 8, 9, 24, 26, 33, 56, 83])
    parser.add_argument("--all", action="store_true", help="Probe every DATAC record.")
    parser.add_argument("--lzss", action="store_true", help="Run slower common-LZSS falsification trials.")
    args = parser.parse_args()

    ensure_assets()
    PROBE_DIR.mkdir(parents=True, exist_ok=True)
    records = read_datac()
    chosen = records if args.all else [rec for rec in records if rec.index in set(args.records)]

    report: list[str] = [
        "# Gunboat Compression Probe",
        "",
        "This report captures current compression findings and generated previews.",
        "",
        "Confirmed from the original draw routine at `0xEE01`: packed graphic records have a one-byte control/cache prefix, then a primary width/height at bytes 1 and 2. The direct VGA blitter treats bytes 6..29 as row-control data and bytes 30 onward as pixel bytes.",
        "",
        "The high-entropy `.LZ` chunks in DATAA/DATAB do not look like plain post-depack image records; their direct-blit previews are mostly diagnostic noise. Test Drive 3's LZW+RLE image pipeline is currently the strongest match for the loader-time decompressor and produces recognizable Gunboat graphics.",
        "",
    ]

    for rec in chosen:
        report.extend(probe_record(rec, args.lzss))
        report.append("")

    standalone = ORIGINAL_DIR / "FOOTIT1F.LZ"
    if standalone.exists():
        data = standalone.read_bytes()
        header = parse_graphic_header(data)
        report.extend(
            [
                "## Standalone FOOTIT1F.LZ",
                "",
                f"- Length: `{len(data)}`",
                f"- Entropy: `{entropy(data):.3f}`",
                f"- First 16 bytes: `{data[:16].hex(' ')}`",
            ]
        )
        if header:
            report.append(
                "- Code-derived graphic header: "
                f"prefix `{header.prefix:02X}`, primary `{header.width}x{header.height}`, "
                f"secondary `{header.secondary_width}x{header.secondary_height}`, stride/count `{header.secondary_stride}`"
            )
        if args.lzss:
            report.append("- Test Drive 3 LZW+RLE pipeline trials:")
            td3_thumbs = []
            for result in try_td3_pipeline(data, 65536)[:8]:
                report.append(
                    f"  - start `{result.start}` width `{result.width}` -> score `{result.score:.2f}` "
                    f"({result.note})"
                )
                td3_thumbs.append(
                    (
                        f"td3 s{result.start} w{result.width}",
                        apply_orientation(render_indexed(result.pixels[:24000], max(1, result.width or 160)), "flip vertical"),
                    )
                )
            make_montage(td3_thumbs, PROBE_DIR / "standalone_footit1f_td3_lzw_rle_trials.png")

            report.append("- Simple RLE trials:")
            rle_thumbs = []
            for result in try_rle_family(data, 65536)[:6]:
                report.append(
                    f"  - `{result.name}` start `{result.start}` -> {len(result.out)} bytes, "
                    f"score `{result.score:.2f}` ({result.note})"
                )
                rle_thumbs.append((f"{result.name} s{result.start}", render_bytes_as_strip(result.out[:16000])))
            make_montage(rle_thumbs, PROBE_DIR / "standalone_footit1f_rle_trials.png")

            report.append("- Common LZSS trials:")
            thumbs = []
            for result in try_lzss_family(data, 65536)[:8]:
                report.append(
                    f"  - `{result.name}` start `{result.start}` -> {len(result.out)} bytes, "
                    f"score `{result.score:.2f}` ({result.note})"
                )
                thumbs.append((f"{result.name} s{result.start}", render_bytes_as_strip(result.out[:16000])))
            make_montage(thumbs, PROBE_DIR / "standalone_footit1f_lzss_trials.png")

    report_path = PROBE_DIR / "compression_probe.md"
    report_path.write_text("\n".join(report), encoding="utf-8")
    print(f"Wrote {report_path}")


if __name__ == "__main__":
    main()
