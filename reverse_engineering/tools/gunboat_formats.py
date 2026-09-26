#!/usr/bin/env python3
"""Shared helpers for exploring the DOS Gunboat data files."""

from __future__ import annotations

import math
import pathlib
import re
import struct
from dataclasses import dataclass

from collections.abc import Sequence

from PIL import Image, ImageDraw


ROOT = pathlib.Path(__file__).resolve().parents[2]
ORIGINAL_DIR = ROOT / "Game"
OUT_DIR = ROOT / "reverse_engineering" / "out"
ASSET_DIR = OUT_DIR / "assets"
UNPACKED_IMAGE = OUT_DIR / "gunboat_unpacked_image.bin"


@dataclass(frozen=True)
class AssetRecord:
    index: int
    meta0: int
    meta1: int
    file_key: str
    offset: int
    length: int
    candidate_name: str = ""

    @property
    def source_file(self) -> str:
        return "DATAA.DAT" if self.file_key == "a" else "DATAB.DAT"

    @property
    def end(self) -> int:
        return self.offset + self.length

    @property
    def extracted_name(self) -> str:
        return f"{self.index:02d}_{self.file_key}_{self.offset:06X}_{self.length:05X}.bin"


TITLE_BANK_NAMES = [
    "DAT6.DAT",
    "TITLCOLR.BIN",
    "COPY.LZ",
    "ACCO.LZ",
    "TITLE1A.LZ",
    "TITLE1B.LZ",
    "TITLE1C.LZ",
    "TITLE1D.LZ",
    "FOOTIT1E.LZ",
    "FOOTIT1F.LZ",
    "TITLE3C.LZ",
    "TITLE3B.LZ",
    "TITLE3A.LZ",
    "TIT1COLR.BIN",
    "TITLE2A.LZ",
    "TITLE2B.LZ",
    "TITLE2C.LZ",
    "TITLE2D.LZ",
    "TIT2COLR.BIN",
    "TITLE3C.LZ",
    "TITLE3B.LZ",
    "TITLE3A.LZ",
    "TIT3COLR.BIN",
    "VALK/TITLE?",
    "TITLE/FOOTER?",
]

GAME_BANK_NAMES = [
    "QUIZCOLR.BIN",
    "HQ1.LZ",
    "HQ2.LZ",
    "HQ3.LZ",
    "HQARM.LZ",
    "PENCIL.MPP",
    "DAT5.DAT",
    "GENCOLR.BIN",
    "GENB.LZ",
    "GENC.LZ",
    "FOLDER.LZ",
    "SPEC1.LZ",
    "SPEC2.LZ",
    "SPEC3.LZ",
    "ADHEAD.LZ",
    "SPEC4.LZ",
    "SPEC5.LZ",
    "SPEC6.LZ",
    "SMALL.LZ",
    "INSIG.LZ",
    "MP5A.LZ",
    "MP5B.LZ",
    "MP2A.LZ",
    "MP2B.LZ",
    "MP3A.LZ",
    "MP3B.LZ",
    "MP4A.LZ",
    "MP4B.LZ",
    "MP5A.LZ",
    "MP5B.LZ",
    "GENA.LZ",
    "DAT1.DAT",
    "DAT2.DAT",
    "DAT3.DAT",
    "DAT4.DAT",
    "DAT10.DAT",
    "DAT11.DAT",
    "DAT2A.DAT",
    "DAT2B.DAT",
    "DAT3A.DAT",
    "DAT3B.DAT",
    "DAT4A.DAT",
    "DAT4B.DAT",
    "DAT1A.DAT",
    "DAT1B.DAT",
    "TILE.BIN",
    "BD1.LZ",
    "BD2.LZ",
    "BD3.LZ",
    "BD4.LZ",
    "BD5.LZ",
    "CLIP.LZ",
    "BG1.LZ",
    "BG2.LZ",
    "BF1.LZ",
    "BF2.LZ",
    "B61/B62/BG/BR/BM/MAP?",
    "TACTICAL/MAP?",
    "MAP.LZ?",
]


def filename_key(name: bytes) -> tuple[int, int]:
    """Original lookup at image 0xEF2; helpers at 0xE62 and 0xEAC."""
    return (sum(i * c for i, c in enumerate(name[:-1])) & 0xFFFF,
            sum(c * pow(257, i, 65536) for i, c in enumerate(name)) & 0xFFFF)


def read_datac(base: pathlib.Path = ORIGINAL_DIR) -> list[AssetRecord]:
    datac = (base / "DATAC.DAT").read_bytes()
    records: list[AssetRecord] = []
    names = {}
    if UNPACKED_IMAGE.exists():
        for name in set(re.findall(rb"[A-Z0-9]+\.(?:DAT|BIN|LZ|MPP)", UNPACKED_IMAGE.read_bytes())):
            names.setdefault(filename_key(name), []).append(name.decode("ascii"))
    title_i = 0
    game_i = 0
    for raw_index in range(0, len(datac), 14):
        chunk = datac[raw_index : raw_index + 14]
        if len(chunk) < 14 or not any(chunk):
            break
        meta0, meta1, file_word, off_lo, off_hi, len_lo, len_hi = struct.unpack("<7H", chunk)
        key = chr(file_word & 0xFF)
        name = ""
        if key == "a" and title_i < len(TITLE_BANK_NAMES):
            name = TITLE_BANK_NAMES[title_i]
            title_i += 1
        elif key == "b" and game_i < len(GAME_BANK_NAMES):
            name = GAME_BANK_NAMES[game_i]
            game_i += 1
        records.append(
            AssetRecord(
                index=raw_index // 14,
                meta0=meta0,
                meta1=meta1,
                file_key=key,
                offset=off_lo + (off_hi << 16),
                length=len_lo + (len_hi << 16),
                candidate_name=" / ".join(sorted(names.get((meta0, meta1), []))) or name,
            )
        )
    return records


def ensure_assets(base: pathlib.Path = ORIGINAL_DIR, out_dir: pathlib.Path = ASSET_DIR) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    banks = {"a": (base / "DATAA.DAT").read_bytes(), "b": (base / "DATAB.DAT").read_bytes()}
    for rec in read_datac(base):
        path = out_dir / rec.extracted_name
        if not path.exists():
            path.write_bytes(banks[rec.file_key][rec.offset : rec.end])


def asset_path(rec: AssetRecord, asset_dir: pathlib.Path = ASSET_DIR) -> pathlib.Path:
    return asset_dir / rec.extracted_name


def entropy(data: bytes) -> float:
    if not data:
        return 0.0
    counts = [0] * 256
    for value in data:
        counts[value] += 1
    return -sum((count / len(data)) * math.log2(count / len(data)) for count in counts if count)


def adjacent_equal_ratio(data: bytes) -> float:
    if len(data) < 2:
        return 0.0
    return sum(1 for left, right in zip(data, data[1:]) if left == right) / (len(data) - 1)


def looks_like_palette_record(rec: AssetRecord, data: bytes) -> bool:
    return rec.length in (189, 225, 768)


def looks_like_compressed_graphics(data: bytes) -> bool:
    return len(data) > 512 and entropy(data) > 7.25 and adjacent_equal_ratio(data) < 0.03


def graphic_record_header(data: bytes, base: int = 0) -> tuple[int, int, int, int, int, int] | None:
    """Return the expanded drawing-record header used by the 0xEE01 blitter."""

    if base + 6 > len(data):
        return None
    return (data[base], data[base + 1], data[base + 2], data[base + 3], data[base + 4], data[base + 5])


def ascii_strings(data: bytes, min_len: int = 4) -> list[tuple[int, str]]:
    pattern = rb"[ -~]{" + str(min_len).encode("ascii") + rb",}"
    return [(m.start(), m.group().decode("ascii", "replace")) for m in re.finditer(pattern, data)]


def embedded_asset_names(image_path: pathlib.Path = UNPACKED_IMAGE) -> list[tuple[int, str]]:
    if not image_path.exists():
        return []
    data = image_path.read_bytes()
    pattern = rb"[A-Z0-9_]+\.(?:LZ|BIN|MUS|DAT|MPP|DRV|CFG|EXE)"
    seen: set[str] = set()
    names: list[tuple[int, str]] = []
    for m in re.finditer(pattern, data):
        name = m.group().decode("ascii", "replace")
        if name not in seen:
            names.append((m.start(), name))
            seen.add(name)
    return names


def indexed_palette() -> list[int]:
    palette: list[int] = []
    for index in range(256):
        palette += [(index * 47) & 0xFF, (index * 83) & 0xFF, (index * 131) & 0xFF]
    return palette


def grayscale_palette() -> list[int]:
    palette: list[int] = []
    for index in range(256):
        palette += [index, index, index]
    return palette


def ega_palette() -> list[int]:
    base = [
        (0, 0, 0),
        (0, 0, 170),
        (0, 170, 0),
        (0, 170, 170),
        (170, 0, 0),
        (170, 0, 170),
        (170, 85, 0),
        (170, 170, 170),
        (85, 85, 85),
        (85, 85, 255),
        (85, 255, 85),
        (85, 255, 255),
        (255, 85, 85),
        (255, 85, 255),
        (255, 255, 85),
        (255, 255, 255),
    ]
    palette = []
    for index in range(256):
        palette += list(base[index % len(base)])
    return palette


def map_material_palette() -> list[int]:
    """Temporary tactical-map material palette.

    Records 39..46 decode to a very small set of material indices rather than a
    normal title-screen palette.  The exact VGA palette/remap is still being
    traced; this palette is tuned from reference screenshots so the terrain
    layer is inspectable without the false bright-cyan banding caused by other
    palettes.
    """

    colors = tactical_map_palette_colors()
    palette: list[int] = []
    for index in range(256):
        material_index = tactical_map_material(index)
        palette += list(colors.get(material_index, (0, 0, 0)))
    return palette


def tactical_map_palette_colors() -> dict[int, tuple[int, int, int]]:
    """Material colors for the low-resolution tactical sector-map layer."""

    return {
        0: (18, 78, 30),
        2: (34, 122, 42),
        3: (12, 52, 28),
        9: (34, 75, 190),
        10: (64, 208, 86),
        14: (226, 226, 116),
        15: (242, 242, 188),
    }


def tactical_map_preview_palette(show_relief_hatch: bool = True) -> list[int]:
    """Palette for tactical-map previews.

    Material value 3 is a hard horizontal relief hatch in the decoded map layer.
    The original assignment map appears to composite that layer more softly than
    our raw indexed preview.  Keep the raw hatch available, but allow the GUI's
    framed map preview to collapse it into the base terrain green.
    """

    colors = tactical_map_palette_colors().copy()
    if not show_relief_hatch:
        colors[0] = colors[2]
        colors[3] = colors[2]

    palette: list[int] = []
    for index in range(256):
        palette += list(colors.get(tactical_map_material(index), colors[2]))
    return palette


def bridge_tactical_map_line_gaps(image: Image.Image) -> Image.Image:
    """Bridge single-pixel vertical gaps in tactical-map shore/water strokes."""

    width, height = image.size
    if width < 3 or height < 3:
        return image

    colors = tactical_map_palette_colors()
    high_colors = {
        colors[9]: 1,
        colors[14]: 2,
        colors[15]: 3,
    }
    source = list(image.convert("RGB").getdata())
    output = source[:]
    for y in range(1, height - 1):
        row = y * width
        for x in range(1, width - 1):
            index = row + x
            if source[index] in high_colors:
                continue
            candidates: list[tuple[int, tuple[int, int, int]]] = []
            for upper_dx, lower_dx in ((0, 0), (-1, 1), (1, -1), (-1, 0), (0, -1), (1, 0), (0, 1)):
                upper = source[(y - 1) * width + x + upper_dx]
                lower = source[(y + 1) * width + x + lower_dx]
                if upper == lower and upper in high_colors:
                    candidates.append((high_colors[upper], upper))
            if candidates:
                output[index] = max(candidates)[1]

    cleaned = Image.new("RGB", image.size)
    cleaned.putdata(output)
    return cleaned


def tactical_map_material(value: int) -> int:
    return value & 0x07 if 0x10 <= value <= 0x1F else value


def tactical_map_palette() -> list[int]:
    colors = tactical_map_palette_colors()
    palette: list[int] = []
    for index in range(256):
        palette += list(colors.get(tactical_map_material(index), (0, 0, 0)))
    return palette


def render_tactical_map_sheet(
    strip_records: Sequence[AssetRecord],
    sector: int | None = None,
    scale: int = 3,
    orientation: str = "flip vertical",
    show_relief_hatch: bool = True,
    smooth_line_gaps: bool = False,
) -> Image.Image:
    """Render Gunboat's packed tactical sector-map sheet.

    Records 39..46 decode as `320 x 54` images, but the game does not display
    that combined buffer directly. The combined layer is a sheet of low-res
    `160 x 72` sector backgrounds: two columns by three rows. A selected sector
    is scaled up and framed by the tactical-map UI at runtime.
    """

    width = 320
    strip_height = 54
    decoded_strips: list[Image.Image] = []
    palette = tactical_map_preview_palette(show_relief_hatch)
    for rec in strip_records:
        _lzw, pixels = td3_lzw_rle_decode(asset_path(rec).read_bytes())
        image = render_indexed(pixels, width, 0, palette)
        decoded_strips.append(apply_orientation(image, orientation))

    if not decoded_strips:
        return render_notice("No tactical map strips were decoded.")

    sheet = Image.new("RGB", (width, strip_height * len(decoded_strips)), "#000000")
    y = 0
    for image in decoded_strips:
        sheet.paste(image, (0, y))
        y += image.height

    sector_width = 160
    sector_height = 72
    rows = sheet.height // sector_height
    cols = sheet.width // sector_width
    sector_count = rows * cols
    if sector is None:
        montage = Image.new("RGB", (cols * sector_width * scale, rows * sector_height * scale), "#202020")
        draw = ImageDraw.Draw(montage)
        for index in range(sector_count):
            col = index % cols
            row = index // cols
            crop = sheet.crop((col * sector_width, row * sector_height, (col + 1) * sector_width, (row + 1) * sector_height))
            if smooth_line_gaps:
                crop = bridge_tactical_map_line_gaps(crop)
            crop = crop.resize((sector_width * scale, sector_height * scale), Image.Resampling.NEAREST)
            x = col * sector_width * scale
            y = row * sector_height * scale
            montage.paste(crop, (x, y))
            draw.text((x + 4, y + 4), str(index), fill=(255, 255, 255))
        return montage

    sector = max(0, min(sector_count - 1, int(sector)))
    col = sector % cols
    row = sector // cols
    crop = sheet.crop((col * sector_width, row * sector_height, (col + 1) * sector_width, (row + 1) * sector_height))
    if smooth_line_gaps:
        crop = bridge_tactical_map_line_gaps(crop)
    map_image = crop.resize((sector_width * scale, sector_height * scale), Image.Resampling.NEAREST)

    frame_pad_x = 18
    frame_pad_y = 10
    framed = Image.new("RGB", (map_image.width + frame_pad_x * 2, map_image.height + frame_pad_y * 2), (164, 168, 164))
    framed.paste(map_image, (frame_pad_x, frame_pad_y))
    draw = ImageDraw.Draw(framed)
    for y in range(7, framed.height - 6, 23):
        draw.rectangle((4, y, 10, y + 6), fill=(0, 32, 0), outline=(0, 0, 0))
        draw.rectangle((framed.width - 11, y, framed.width - 5, y + 6), fill=(0, 32, 0), outline=(0, 0, 0))
    draw.rectangle((frame_pad_x - 1, frame_pad_y - 1, frame_pad_x + map_image.width, frame_pad_y + map_image.height), outline=(58, 66, 58))
    return framed


def palette_from_6bit_rgb(data: bytes) -> list[int]:
    palette = []
    triples = len(data) // 3
    for i in range(min(256, triples)):
        r, g, b = data[i * 3 : i * 3 + 3]
        palette += [min(255, r * 4), min(255, g * 4), min(255, b * 4)]
    while len(palette) < 768:
        index = len(palette) // 3
        palette += [index, index, index]
    return palette


def render_palette_strip(data: bytes, scale: int = 16) -> Image.Image:
    palette = palette_from_6bit_rgb(data)
    cols = 16
    rows = 16
    image = Image.new("RGB", (cols * scale, rows * scale), "black")
    pixels = image.load()
    for index in range(256):
        rgb = tuple(palette[index * 3 : index * 3 + 3])
        x0 = (index % cols) * scale
        y0 = (index // cols) * scale
        for y in range(y0, y0 + scale):
            for x in range(x0, x0 + scale):
                pixels[x, y] = rgb
    return image


def render_indexed(data: bytes, width: int, skip: int = 0, palette: str | Sequence[int] = "indexed") -> Image.Image:
    width = max(1, int(width))
    skip = min(max(0, int(skip)), len(data))
    payload = data[skip:]
    height = max(1, math.ceil(len(payload) / width))
    padded = payload + bytes(width * height - len(payload))
    image = Image.frombytes("P", (width, height), padded)
    if not isinstance(palette, str):
        image.putpalette(list(palette)[:768])
    elif palette == "gray":
        image.putpalette(grayscale_palette())
    elif palette == "map material":
        image.putpalette(map_material_palette())
    elif palette == "ega":
        image.putpalette(ega_palette())
    else:
        image.putpalette(indexed_palette())
    return image.convert("RGB")


def apply_orientation(image: Image.Image, orientation: str = "none") -> Image.Image:
    orientation = orientation.strip().lower().replace("_", " ")
    if orientation in ("flip vertical", "vertical", "vflip", "bottom to top"):
        return image.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    if orientation in ("flip horizontal", "horizontal", "hflip", "mirror"):
        return image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    if orientation in ("rotate 180", "180", "both"):
        return image.transpose(Image.Transpose.ROTATE_180)
    return image


def render_direct_blit_record(data: bytes, palette: str | Sequence[int] = "indexed") -> Image.Image:
    header = graphic_record_header(data)
    if header is None:
        return render_notice("Not enough data for DOS blit header.")
    _prefix, width, height, _secondary_width, _secondary_height, _secondary_stride = header
    if not (0 < width <= 320 and 0 < height <= 200):
        return render_notice(f"DOS blit header is not plausible: {width} x {height}.")
    payload_offset = 30
    payload = data[payload_offset : payload_offset + width * height]
    payload += bytes(max(0, width * height - len(payload)))
    image = Image.frombytes("P", (width, height), payload)
    if not isinstance(palette, str):
        image.putpalette(list(palette)[:768])
    elif palette == "gray":
        image.putpalette(grayscale_palette())
    elif palette == "map material":
        image.putpalette(map_material_palette())
    elif palette == "ega":
        image.putpalette(ega_palette())
    else:
        image.putpalette(indexed_palette())
    return image.convert("RGB")


def td3_lzw_decode(data: bytes, max_output: int = 1_000_000) -> bytes:
    """Decode the Test Drive 3 VGA-image LZW stream shape.

    TD3's image format wraps its RLE pixel data in 9..12 bit little-endian
    LZW codes with clear/end markers.  Gunboat is not proven to use this yet;
    this helper exists so probes and the GUI can test that engine-family lead.
    """

    clear_code = 0x100
    end_code = 0x101
    first_code = 0x102
    max_bits = 12

    dictionary: dict[int, bytes] = {}
    next_code = first_code
    code_bits = 9
    bit_position = 0

    def reset_dictionary() -> None:
        nonlocal dictionary, next_code, code_bits
        dictionary = {i: bytes([i]) for i in range(256)}
        next_code = first_code
        code_bits = 9

    def read_code() -> int | None:
        nonlocal bit_position
        byte_pos = bit_position // 8
        bit_offset = bit_position % 8
        if byte_pos + 2 > len(data):
            return None
        value = data[byte_pos]
        if byte_pos + 1 < len(data):
            value |= data[byte_pos + 1] << 8
        if byte_pos + 2 < len(data):
            value |= data[byte_pos + 2] << 16
        value >>= bit_offset
        value &= (1 << code_bits) - 1
        bit_position += code_bits
        return value

    def expand_width_if_needed() -> None:
        nonlocal code_bits
        if next_code >= (1 << code_bits) and code_bits < max_bits:
            code_bits += 1

    reset_dictionary()
    code = read_code()
    if code is None or code == end_code:
        return b""
    if code == clear_code:
        reset_dictionary()
        code = read_code()
        if code is None or code == end_code:
            return b""

    previous = dictionary.get(code)
    if previous is None:
        raise ValueError(f"invalid first LZW code {code}")

    output = bytearray(previous)
    while len(output) < max_output:
        code = read_code()
        if code is None or code == end_code:
            break
        if code == clear_code:
            reset_dictionary()
            code = read_code()
            if code is None or code == end_code:
                break
            previous = dictionary.get(code)
            if previous is None:
                raise ValueError(f"invalid LZW code after clear {code}")
            output.extend(previous)
            continue

        if code in dictionary:
            current = dictionary[code]
        elif code == next_code:
            current = previous + previous[:1]
        else:
            raise ValueError(f"invalid LZW code {code}, next {next_code}")

        output.extend(current)
        if next_code < 4096:
            dictionary[next_code] = previous + current[:1]
            next_code += 1
            expand_width_if_needed()
        previous = current

    return bytes(output[:max_output])


def td3_rle_unpack(data: bytes, max_output: int = 1_000_000) -> bytes:
    """Unpack TD3's post-LZW [pixel][length] RLE pairs."""

    output = bytearray()
    for pos in range(0, len(data) - 1, 2):
        pixel = data[pos]
        count = data[pos + 1]
        output.extend(bytes([pixel]) * count)
        if len(output) >= max_output:
            return bytes(output[:max_output])
    return bytes(output)


def td3_lzw_rle_decode(data: bytes, skip: int = 0, max_lzw: int = 1_000_000, max_pixels: int = 1_000_000) -> tuple[bytes, bytes]:
    payload = data[max(0, skip) :]
    lzw = td3_lzw_decode(payload, max_lzw)
    pixels = td3_rle_unpack(lzw, max_pixels)
    return lzw, pixels


def td3_dimension_candidates(pixel_count: int) -> list[tuple[int, int]]:
    widths = (320, 256, 240, 208, 192, 168, 160, 128, 112, 96, 80, 72, 64, 48, 40, 32)
    candidates: list[tuple[int, int]] = []
    for width in widths:
        if pixel_count % width == 0:
            height = pixel_count // width
            if 8 <= height <= 240:
                candidates.append((width, height))
    return candidates


def suggest_td3_width(pixel_count: int, record_index: int | None = None) -> int:
    """Pick a first-pass width for a decoded TD3-style image.

    The executable's draw tables are the real source of truth.  This heuristic
    just prevents obviously flattened previews while those tables are still
    being traced.
    """

    known_widths = {
        0: 320,
        1: 320,
        7: 320,
        8: 320,
        9: 320,
        10: 320,
        13: 160,
        14: 320,
        15: 320,
        16: 320,
        17: 320,
        24: 320,
        26: 320,
        27: 320,
        28: 320,
        30: 320,
        33: 256,
        34: 256,
        35: 256,
        36: 256,
        37: 256,
        38: 256,
    }
    if record_index in known_widths and pixel_count % known_widths[record_index] == 0:
        return known_widths[record_index]

    candidates = td3_dimension_candidates(pixel_count)
    if not candidates:
        return 320
    for width, height in candidates:
        if width == 320 and 40 <= height <= 200:
            return width
    for width, height in candidates:
        if width == 160 and 80 <= height <= 120:
            return width
    return candidates[0][0]


def render_td3_lzw_rle(
    data: bytes,
    width: int,
    skip: int = 0,
    palette: str | Sequence[int] = "indexed",
    orientation: str = "flip vertical",
) -> Image.Image:
    _lzw, pixels = td3_lzw_rle_decode(data, skip=skip)
    return apply_orientation(render_indexed(pixels, width, 0, palette), orientation)


def render_notice(lines: str | list[str], size: tuple[int, int] = (760, 420)) -> Image.Image:
    if isinstance(lines, str):
        lines = [lines]
    image = Image.new("RGB", size, "#161616")
    draw = ImageDraw.Draw(image)
    y = 22
    for line in lines:
        draw.text((24, y), line, fill="#d8d8d8")
        y += 22
    return image


def render_4bpp(data: bytes, width: int, skip: int = 0, palette: str | Sequence[int] = "ega") -> Image.Image:
    pixels = bytearray()
    for byte in data[max(0, skip) :]:
        pixels.append(byte >> 4)
        pixels.append(byte & 0x0F)
    return render_indexed(bytes(pixels), width, 0, palette)


def hex_dump(data: bytes, start: int = 0, length: int = 512) -> str:
    out = []
    end = min(len(data), start + length)
    for offset in range(start, end, 16):
        chunk = data[offset : min(end, offset + 16)]
        hex_part = " ".join(f"{value:02X}" for value in chunk)
        ascii_part = "".join(chr(value) if 32 <= value <= 126 else "." for value in chunk)
        out.append(f"{offset:06X}: {hex_part:<47} {ascii_part}")
    return "\n".join(out)


def logic_report(image_path: pathlib.Path = UNPACKED_IMAGE) -> str:
    if not image_path.exists():
        return "Unpacked image not found. Run unpack_gb_exepack.py first."
    data = image_path.read_bytes()
    sections = [
        ("In-game callouts and damage", 0xA1E0, 0xA6C0),
        ("HQ, roster, copy quiz, campaign flow", 0x22500, 0x23300),
        ("Briefings and mission assignments", 0x23600, 0x25610),
        ("Equipment and debrief systems", 0x25610, 0x26A20),
        ("Mission status panel", 0x28A00, 0x28C20),
    ]
    lines = [
        "# Gunboat Logic Extraction",
        "",
        "This report is extracted from the unpacked DOS executable strings. It is not a full decompilation yet, but it maps the visible gameplay systems and source-code hotspots to keep tracing.",
        "",
        "## Confirmed Systems",
        "",
        "- HQ/front-end flow with commander identity, personnel files, region selection, map assignment, and roster updates.",
        "- Manual quiz/copy-protection gate using ship specification questions.",
        "- Three regions: Vietnam, Colombia, and Panama Canal.",
        "- Practice modes: gunnery, grenade/weapon practice, and piloting.",
        "- PBR outfitting: engines and weapon station choices.",
        "- Crew/station model: captain, bow gunner's mate, midship engineman, rear seaman.",
        "- Damage model: hull, engines, waterjets, fuel tanks, radar/spotlights, and individual crew casualties.",
        "- Mission evaluation: complete/failure states, friendly-fire court martial, promotions, medals, and kill categories.",
        "",
        "## Important Code Hotspots",
        "",
        "- `0x15EF6`: unpacked executable entry and C runtime startup.",
        "- `0x1B30D`: DOS file open wrapper (`int 21h`, AH=3Dh).",
        "- `0x1B32C`: DOS file read wrapper (`int 21h`, AH=3Fh).",
        "- `0x1B320`: DOS file close wrapper (`int 21h`, AH=3Eh).",
        "- `0x1B430..0x1B5FC`: table-driven interpreter/update loop, likely sound or scripted device state.",
        "- `0x1BE52..0x1C397`: embedded original asset-name tables.",
        "",
    ]
    for title, start, end in sections:
        lines.append(f"## {title}")
        lines.append("")
        for offset, text in ascii_strings(data[start:end], 5):
            text = text.strip()
            if text:
                lines.append(f"- `0x{start + offset:05X}` {text}")
        lines.append("")
    return "\n".join(lines)
