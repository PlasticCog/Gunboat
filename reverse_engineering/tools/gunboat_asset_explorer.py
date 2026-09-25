#!/usr/bin/env python3
"""Tkinter GUI for exploring Gunboat's DOS asset banks and logic clues."""

from __future__ import annotations

import pathlib
import re
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from PIL import Image, ImageTk

from gunboat_formats import (
    ASSET_DIR,
    OUT_DIR,
    AssetRecord,
    adjacent_equal_ratio,
    ascii_strings,
    asset_path,
    embedded_asset_names,
    ensure_assets,
    entropy,
    graphic_record_header,
    hex_dump,
    logic_report,
    looks_like_compressed_graphics,
    looks_like_palette_record,
    map_material_palette,
    palette_from_6bit_rgb,
    read_datac,
    render_4bpp,
    render_direct_blit_record,
    render_indexed,
    render_notice,
    render_palette_strip,
    render_tactical_map_sheet,
    render_td3_lzw_rle,
    suggest_td3_width,
    td3_dimension_candidates,
    td3_lzw_rle_decode,
)


class GunboatAssetExplorer(tk.Tk):
    def __init__(self) -> None:
        super().__init__()
        self.title("Gunboat DOS Asset Explorer")
        self.geometry("1280x820")
        self.minsize(980, 640)
        self.records = read_datac()
        ensure_assets()
        self.palette_records = [rec for rec in self.records if looks_like_palette_record(rec, asset_path(rec).read_bytes())]
        self.record_by_iid: dict[str, AssetRecord] = {}
        self.current_record: AssetRecord | None = None
        self.preview_image: Image.Image | None = None
        self.preview_photo: ImageTk.PhotoImage | None = None
        self.preview_zoom = 1.0
        self.preview_offset = [10.0, 10.0]
        self.preview_drag_start: tuple[int, int] | None = None
        self.map_photo: ImageTk.PhotoImage | None = None
        self._build_ui()
        self._populate_assets()
        self._populate_embedded_names()
        self._populate_logic()
        if self.records:
            first = str(self.records[0].index)
            self.asset_tree.selection_set(first)
            self.asset_tree.focus(first)
            self._on_asset_selected()

    def _build_ui(self) -> None:
        self.columnconfigure(0, weight=1)
        self.rowconfigure(0, weight=1)
        self.tabs = ttk.Notebook(self)
        self.tabs.grid(row=0, column=0, sticky="nsew")
        self._build_assets_tab()
        self._build_map_tab()
        self._build_logic_tab()

    def _build_assets_tab(self) -> None:
        frame = ttk.Frame(self.tabs, padding=8)
        self.tabs.add(frame, text="Assets")
        frame.columnconfigure(0, weight=0)
        frame.columnconfigure(1, weight=1)
        frame.rowconfigure(0, weight=1)

        left = ttk.Frame(frame)
        left.grid(row=0, column=0, sticky="nsew", padx=(0, 8))
        left.rowconfigure(0, weight=1)

        cols = ("name", "source", "offset", "length", "meta")
        self.asset_tree = ttk.Treeview(left, columns=cols, show="headings", height=28)
        self.asset_tree.heading("name", text="Candidate")
        self.asset_tree.heading("source", text="Bank")
        self.asset_tree.heading("offset", text="Offset")
        self.asset_tree.heading("length", text="Length")
        self.asset_tree.heading("meta", text="Meta")
        self.asset_tree.column("name", width=155)
        self.asset_tree.column("source", width=74, anchor="center")
        self.asset_tree.column("offset", width=86, anchor="e")
        self.asset_tree.column("length", width=74, anchor="e")
        self.asset_tree.column("meta", width=108, anchor="center")
        self.asset_tree.grid(row=0, column=0, sticky="nsew")
        self.asset_tree.bind("<<TreeviewSelect>>", lambda _event: self._on_asset_selected())
        scrollbar = ttk.Scrollbar(left, orient="vertical", command=self.asset_tree.yview)
        self.asset_tree.configure(yscrollcommand=scrollbar.set)
        scrollbar.grid(row=0, column=1, sticky="ns")

        right = ttk.Frame(frame)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(2, weight=1)

        controls = ttk.Frame(right)
        controls.grid(row=0, column=0, sticky="ew")
        for i in range(15):
            controls.columnconfigure(i, weight=0)
        controls.columnconfigure(14, weight=1)

        ttk.Label(controls, text="Preview").grid(row=0, column=0, padx=(0, 4))
        self.preview_mode = tk.StringVar(value="Auto")
        modes = [
            "Auto",
            "Palette",
            "Tactical map sector",
            "Tactical map sheet",
            "TD3 LZW+RLE",
            "DOS direct blit",
            "Indexed 8bpp",
            "Packed 4bpp",
            "Compressed notice",
            "Hex only",
        ]
        ttk.Combobox(controls, textvariable=self.preview_mode, values=modes, width=18, state="readonly").grid(row=0, column=1)
        ttk.Label(controls, text="Width").grid(row=0, column=2, padx=(12, 4))
        self.preview_width = tk.IntVar(value=96)
        ttk.Spinbox(controls, from_=8, to=640, increment=8, textvariable=self.preview_width, width=7, command=self._render_preview).grid(row=0, column=3)
        ttk.Button(controls, text="Suggest", command=self._suggest_preview_width).grid(row=0, column=4, padx=(6, 0))
        ttk.Label(controls, text="Skip").grid(row=0, column=5, padx=(12, 4))
        self.preview_skip = tk.IntVar(value=0)
        ttk.Spinbox(controls, from_=0, to=4096, increment=1, textvariable=self.preview_skip, width=7, command=self._render_preview).grid(row=0, column=6)
        ttk.Label(controls, text="Palette").grid(row=0, column=7, padx=(12, 4))
        self.palette_mode = tk.StringVar(value="auto palette")
        self.palette_combo = ttk.Combobox(
            controls,
            textvariable=self.palette_mode,
            values=self._palette_choices(),
            width=18,
            state="readonly",
        )
        self.palette_combo.grid(row=0, column=8)
        ttk.Label(controls, text="Orient").grid(row=0, column=9, padx=(12, 4))
        self.orientation_mode = tk.StringVar(value="flip vertical")
        ttk.Combobox(
            controls,
            textvariable=self.orientation_mode,
            values=["none", "flip vertical", "flip horizontal", "rotate 180"],
            width=13,
            state="readonly",
        ).grid(row=0, column=10)
        ttk.Button(controls, text="Render", command=self._render_preview).grid(row=0, column=11, padx=(12, 0))
        ttk.Button(controls, text="Export PNG", command=self._export_preview).grid(row=0, column=12, padx=(6, 0))
        self.combine_strips = tk.BooleanVar(value=True)
        ttk.Checkbutton(controls, text="Combine strips", variable=self.combine_strips, command=self._render_preview).grid(
            row=0,
            column=13,
            padx=(12, 0),
        )
        self.strip_order_mode = tk.StringVar(value="record order")
        ttk.Combobox(
            controls,
            textvariable=self.strip_order_mode,
            values=["record order", "A to Z", "Z to A"],
            width=11,
            state="readonly",
        ).grid(row=0, column=14, padx=(6, 0))
        ttk.Label(controls, text="Sector").grid(row=1, column=0, pady=(6, 0), padx=(0, 4), sticky="w")
        self.tactical_sector = tk.IntVar(value=0)
        ttk.Spinbox(
            controls,
            from_=0,
            to=5,
            increment=1,
            textvariable=self.tactical_sector,
            width=5,
            command=self._render_preview,
        ).grid(row=1, column=1, pady=(6, 0), sticky="w")
        self.tactical_relief_hatch = tk.BooleanVar(value=False)
        ttk.Checkbutton(
            controls,
            text="Relief hatch",
            variable=self.tactical_relief_hatch,
            command=self._render_preview,
        ).grid(row=1, column=2, columnspan=3, pady=(6, 0), padx=(12, 0), sticky="w")
        self.tactical_smooth_lines = tk.BooleanVar(value=True)
        ttk.Checkbutton(
            controls,
            text="Smooth lines",
            variable=self.tactical_smooth_lines,
            command=self._render_preview,
        ).grid(row=1, column=5, columnspan=3, pady=(6, 0), padx=(12, 0), sticky="w")

        self.preview_mode.trace_add("write", lambda *_args: self._render_preview())
        self.palette_mode.trace_add("write", lambda *_args: self._render_preview())
        self.orientation_mode.trace_add("write", lambda *_args: self._render_preview())
        self.strip_order_mode.trace_add("write", lambda *_args: self._render_preview())
        self.tactical_sector.trace_add("write", lambda *_args: self._render_preview())
        self.tactical_relief_hatch.trace_add("write", lambda *_args: self._render_preview())
        self.tactical_smooth_lines.trace_add("write", lambda *_args: self._render_preview())

        self.asset_info = tk.StringVar(value="")
        ttk.Label(right, textvariable=self.asset_info).grid(row=1, column=0, sticky="ew", pady=(8, 6))

        body = ttk.PanedWindow(right, orient=tk.VERTICAL)
        body.grid(row=2, column=0, sticky="nsew")

        preview_holder = ttk.Frame(body)
        preview_holder.columnconfigure(0, weight=1)
        preview_holder.rowconfigure(0, weight=1)
        self.preview_canvas = tk.Canvas(preview_holder, background="#161616", highlightthickness=0)
        self.preview_canvas.grid(row=0, column=0, sticky="nsew")
        self.preview_canvas.bind("<MouseWheel>", self._on_preview_mousewheel)
        self.preview_canvas.bind("<Button-4>", self._on_preview_mousewheel)
        self.preview_canvas.bind("<Button-5>", self._on_preview_mousewheel)
        self.preview_canvas.bind("<ButtonPress-1>", self._on_preview_drag_start)
        self.preview_canvas.bind("<B1-Motion>", self._on_preview_drag)
        self.preview_canvas.bind("<Double-Button-1>", self._reset_preview_view)
        self.preview_canvas.bind("<Configure>", lambda _event: self._draw_preview_on_canvas())
        body.add(preview_holder, weight=4)

        text_holder = ttk.Frame(body)
        text_holder.columnconfigure(0, weight=1)
        text_holder.rowconfigure(0, weight=1)
        self.detail_text = tk.Text(text_holder, wrap="none", height=12, font=("Consolas", 10))
        self.detail_text.grid(row=0, column=0, sticky="nsew")
        yscroll = ttk.Scrollbar(text_holder, orient="vertical", command=self.detail_text.yview)
        xscroll = ttk.Scrollbar(text_holder, orient="horizontal", command=self.detail_text.xview)
        self.detail_text.configure(yscrollcommand=yscroll.set, xscrollcommand=xscroll.set)
        yscroll.grid(row=0, column=1, sticky="ns")
        xscroll.grid(row=1, column=0, sticky="ew")
        body.add(text_holder, weight=2)

    def _build_map_tab(self) -> None:
        frame = ttk.Frame(self.tabs, padding=8)
        self.tabs.add(frame, text="Map Viewer")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(1, weight=1)

        controls = ttk.Frame(frame)
        controls.grid(row=0, column=0, sticky="ew")
        ttk.Label(controls, text="Candidate chunk").grid(row=0, column=0, padx=(0, 4))
        self.map_choice = tk.StringVar()
        choices = [self._record_label(rec) for rec in self.records]
        self.map_combo = ttk.Combobox(controls, textvariable=self.map_choice, values=choices, width=42, state="readonly")
        self.map_combo.grid(row=0, column=1)
        default = next((self._record_label(rec) for rec in self.records if rec.index == 83), choices[-1])
        self.map_choice.set(default)
        ttk.Label(controls, text="Width").grid(row=0, column=2, padx=(12, 4))
        self.map_width = tk.IntVar(value=96)
        ttk.Spinbox(controls, from_=8, to=640, increment=8, textvariable=self.map_width, width=7, command=self._render_map).grid(row=0, column=3)
        ttk.Label(controls, text="Skip").grid(row=0, column=4, padx=(12, 4))
        self.map_skip = tk.IntVar(value=5)
        ttk.Spinbox(controls, from_=0, to=4096, increment=1, textvariable=self.map_skip, width=7, command=self._render_map).grid(row=0, column=5)
        ttk.Label(controls, text="Zoom").grid(row=0, column=6, padx=(12, 4))
        self.map_zoom = tk.IntVar(value=4)
        ttk.Spinbox(controls, from_=1, to=12, increment=1, textvariable=self.map_zoom, width=5, command=self._render_map).grid(row=0, column=7)
        ttk.Button(controls, text="Render", command=self._render_map).grid(row=0, column=8, padx=(12, 0))
        ttk.Button(controls, text="Export PNG", command=self._export_map).grid(row=0, column=9, padx=(6, 0))

        self.map_status = tk.StringVar(value="Candidate map interpretation. Width/skip are adjustable because MAP.LZ compression is not fully decoded yet.")
        ttk.Label(frame, textvariable=self.map_status).grid(row=2, column=0, sticky="ew", pady=(6, 0))
        self.map_canvas = tk.Canvas(frame, background="#101010", highlightthickness=0)
        self.map_canvas.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        self.map_combo.bind("<<ComboboxSelected>>", lambda _event: self._render_map())

    def _build_logic_tab(self) -> None:
        frame = ttk.Frame(self.tabs, padding=8)
        self.tabs.add(frame, text="Logic")
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(1, weight=1)

        controls = ttk.Frame(frame)
        controls.grid(row=0, column=0, sticky="ew")
        ttk.Button(controls, text="Refresh Report", command=self._populate_logic).grid(row=0, column=0)
        ttk.Button(controls, text="Save Report", command=self._save_logic_report).grid(row=0, column=1, padx=(6, 0))
        ttk.Button(controls, text="Show Embedded Names", command=self._show_embedded_names).grid(row=0, column=2, padx=(6, 0))

        self.logic_text = tk.Text(frame, wrap="word", font=("Consolas", 10))
        self.logic_text.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
        yscroll = ttk.Scrollbar(frame, orient="vertical", command=self.logic_text.yview)
        self.logic_text.configure(yscrollcommand=yscroll.set)
        yscroll.grid(row=1, column=1, sticky="ns", pady=(8, 0))
        self.embedded_names: list[tuple[int, str]] = []

    def _populate_assets(self) -> None:
        for rec in self.records:
            iid = str(rec.index)
            self.record_by_iid[iid] = rec
            self.asset_tree.insert(
                "",
                "end",
                iid=iid,
                values=(
                    f"{rec.index:02d} {rec.candidate_name or rec.extracted_name}",
                    rec.source_file,
                    f"0x{rec.offset:06X}",
                    str(rec.length),
                    f"{rec.meta0:04X}/{rec.meta1:04X}",
                ),
            )

    def _palette_choices(self) -> list[str]:
        choices = ["auto palette", "map material", "indexed", "ega", "gray"]
        choices.extend(f"rec {rec.index:02d} {rec.candidate_name or rec.extracted_name}" for rec in self.palette_records)
        return choices

    def _strip_name_parts(self, rec: AssetRecord) -> tuple[str, str] | None:
        match = re.match(r"^(.+\d)([A-Z])\.(?:LZ|DAT)$", rec.candidate_name or "")
        if match is None:
            return None
        return match.group(1), match.group(2)

    def _td3_shape_for_record(self, rec: AssetRecord) -> tuple[int, int, int] | None:
        data = asset_path(rec).read_bytes()
        if not looks_like_compressed_graphics(data):
            return None
        try:
            _lzw, pixels = td3_lzw_rle_decode(data, max_lzw=1_000_000, max_pixels=1_000_000)
        except Exception:
            return None
        width = suggest_td3_width(len(pixels), rec.index)
        if width <= 0 or len(pixels) % width:
            return None
        return width, len(pixels) // width, len(pixels)

    def _same_shape_strip_records(self, rec: AssetRecord) -> list[AssetRecord]:
        selected_shape = self._td3_shape_for_record(rec)
        if selected_shape is None:
            return [rec]
        records_by_index = {item.index: item for item in self.records}

        run = [rec]
        cursor = rec.index - 1
        while cursor in records_by_index:
            item = records_by_index[cursor]
            if item.file_key != rec.file_key or self._td3_shape_for_record(item) != selected_shape:
                break
            run.append(item)
            cursor -= 1

        cursor = rec.index + 1
        while cursor in records_by_index:
            item = records_by_index[cursor]
            if item.file_key != rec.file_key or self._td3_shape_for_record(item) != selected_shape:
                break
            run.append(item)
            cursor += 1

        run = sorted(run, key=lambda item: item.index)
        if len(run) < 2:
            return [rec]

        _width, height, _pixels = selected_shape
        group_size = max(2, min(len(run), 240 // max(1, height)))
        selected_position = next((i for i, item in enumerate(run) if item.index == rec.index), 0)
        group_start = (selected_position // group_size) * group_size
        group = run[group_start : group_start + group_size]
        return group if len(group) > 1 else [rec]

    def _looks_like_tactical_map_strip(self, rec: AssetRecord) -> bool:
        shape = self._td3_shape_for_record(rec)
        return rec.file_key == "b" and shape is not None and shape[0] == 320 and shape[1] == 54 and 39 <= rec.index <= 46

    def _combined_strip_records(self, rec: AssetRecord) -> list[AssetRecord]:
        parts = self._strip_name_parts(rec)
        if parts is None:
            return self._same_shape_strip_records(rec)
        base, _letter = parts
        records_by_index = {item.index: item for item in self.records}

        group: list[AssetRecord] = [rec]
        cursor = rec.index - 1
        while cursor in records_by_index:
            item = records_by_index[cursor]
            item_parts = self._strip_name_parts(item)
            if item_parts is None or item_parts[0] != base or item.file_key != rec.file_key:
                break
            if not looks_like_compressed_graphics(asset_path(item).read_bytes()):
                break
            group.append(item)
            cursor -= 1

        cursor = rec.index + 1
        while cursor in records_by_index:
            item = records_by_index[cursor]
            item_parts = self._strip_name_parts(item)
            if item_parts is None or item_parts[0] != base or item.file_key != rec.file_key:
                break
            if not looks_like_compressed_graphics(asset_path(item).read_bytes()):
                break
            group.append(item)
            cursor += 1

        if len(group) < 2:
            return [rec]
        named_group = sorted(group, key=lambda item: item.index)
        same_shape_group = self._same_shape_strip_records(rec)
        if len(named_group) < 3 and len(same_shape_group) > len(named_group):
            return same_shape_group
        return named_group

    def _ordered_strip_records(self, records: list[AssetRecord]) -> list[AssetRecord]:
        if len(records) < 2:
            return records
        mode = self.strip_order_mode.get()
        if mode == "A to Z":
            return sorted(records, key=lambda item: self._strip_name_parts(item)[1] if self._strip_name_parts(item) else "")
        if mode == "Z to A":
            return sorted(
                records,
                key=lambda item: self._strip_name_parts(item)[1] if self._strip_name_parts(item) else "",
                reverse=True,
            )
        return sorted(records, key=lambda item: item.index)

    def _populate_embedded_names(self) -> None:
        self.embedded_names = embedded_asset_names()

    def _populate_logic(self) -> None:
        text = logic_report()
        self.logic_text.delete("1.0", "end")
        self.logic_text.insert("1.0", text)

    def _record_label(self, rec: AssetRecord) -> str:
        return f"{rec.index:02d} {rec.candidate_name or rec.extracted_name} ({rec.length} bytes)"

    def _selected_record(self) -> AssetRecord | None:
        selection = self.asset_tree.selection()
        if not selection:
            return None
        return self.record_by_iid.get(selection[0])

    def _record_from_label(self, label: str) -> AssetRecord | None:
        try:
            index = int(label.split()[0])
        except (ValueError, IndexError):
            return None
        return next((rec for rec in self.records if rec.index == index), None)

    def _palette_for_current(self, rec: AssetRecord) -> str | list[int]:
        choice = self.palette_mode.get()
        if choice in ("indexed", "ega", "gray", "map material"):
            return choice
        if choice.startswith("rec "):
            try:
                index = int(choice.split()[1])
            except (IndexError, ValueError):
                return "indexed"
            pal_rec = next((item for item in self.records if item.index == index), None)
            if pal_rec is not None:
                return palette_from_6bit_rgb(asset_path(pal_rec).read_bytes())
            return "indexed"
        if choice == "auto palette":
            if self._looks_like_tactical_map_strip(rec):
                return map_material_palette()
            bank_palettes = [item for item in self.palette_records if item.file_key == rec.file_key and item.index < rec.index]
            if bank_palettes:
                pal_rec = min(bank_palettes, key=lambda item: (rec.index - item.index, abs(rec.offset - item.offset)))
                return palette_from_6bit_rgb(asset_path(pal_rec).read_bytes())
        return "indexed"

    def _decoded_td3_info(self, rec: AssetRecord, data: bytes) -> tuple[int, int, str] | None:
        if not looks_like_compressed_graphics(data):
            return None
        try:
            lzw, pixels = td3_lzw_rle_decode(data, max_lzw=1_000_000, max_pixels=1_000_000)
        except Exception:
            return None
        candidates = td3_dimension_candidates(len(pixels))
        candidate_text = ", ".join(f"{width}x{height}" for width, height in candidates[:8]) or "none"
        return len(lzw), len(pixels), candidate_text

    def _suggest_preview_width(self) -> None:
        rec = self.current_record
        if rec is None:
            return
        data = asset_path(rec).read_bytes()
        if not looks_like_compressed_graphics(data):
            return
        try:
            _lzw, pixels = td3_lzw_rle_decode(data, max_lzw=1_000_000, max_pixels=1_000_000)
        except Exception:
            return
        self.preview_width.set(suggest_td3_width(len(pixels), rec.index))
        self._render_preview()

    def _preview_base_scale(self) -> float:
        if self.preview_image is None:
            return 1.0
        canvas_w = max(240, self.preview_canvas.winfo_width() or 900)
        canvas_h = max(180, self.preview_canvas.winfo_height() or 500)
        fit_w = max(1, canvas_w - 24) / max(1, self.preview_image.width)
        fit_h = max(1, canvas_h - 42) / max(1, self.preview_image.height)
        return min(1.0, fit_w, fit_h)

    def _preview_display_scale(self) -> float:
        return max(0.05, self._preview_base_scale() * self.preview_zoom)

    def _clamp_preview_offset(self) -> None:
        if self.preview_image is None:
            return
        canvas_w = max(240, self.preview_canvas.winfo_width() or 900)
        canvas_h = max(180, self.preview_canvas.winfo_height() or 500)
        scale = self._preview_display_scale()
        display_w = max(1, int(round(self.preview_image.width * scale)))
        display_h = max(1, int(round(self.preview_image.height * scale)))
        min_x = min(10, canvas_w - display_w - 10)
        min_y = min(10, canvas_h - display_h - 32)
        self.preview_offset[0] = min(10, max(min_x, self.preview_offset[0]))
        self.preview_offset[1] = min(10, max(min_y, self.preview_offset[1]))

    def _reset_preview_view(self, _event: tk.Event | None = None) -> str:
        self.preview_zoom = 1.0
        self.preview_offset = [10.0, 10.0]
        self._draw_preview_on_canvas()
        return "break"

    def _on_preview_drag_start(self, event: tk.Event) -> None:
        self.preview_drag_start = (event.x, event.y)

    def _on_preview_drag(self, event: tk.Event) -> None:
        if self.preview_drag_start is None:
            return
        last_x, last_y = self.preview_drag_start
        self.preview_offset[0] += event.x - last_x
        self.preview_offset[1] += event.y - last_y
        self.preview_drag_start = (event.x, event.y)
        self._clamp_preview_offset()
        self._draw_preview_on_canvas()

    def _on_preview_mousewheel(self, event: tk.Event) -> str:
        if self.preview_image is None:
            return "break"
        old_scale = self._preview_display_scale()
        image_x = (event.x - self.preview_offset[0]) / old_scale
        image_y = (event.y - self.preview_offset[1]) / old_scale

        if getattr(event, "num", None) == 4:
            wheel_steps = 1.0
        elif getattr(event, "num", None) == 5:
            wheel_steps = -1.0
        else:
            wheel_steps = (getattr(event, "delta", 0) or 0) / 120
        factor = 1.15 ** wheel_steps
        self.preview_zoom = min(32.0, max(0.25, self.preview_zoom * factor))

        new_scale = self._preview_display_scale()
        self.preview_offset[0] = event.x - image_x * new_scale
        self.preview_offset[1] = event.y - image_y * new_scale
        self._clamp_preview_offset()
        self._draw_preview_on_canvas()
        return "break"

    def _render_tactical_map_preview(self, rec: AssetRecord, sector: int | None) -> Image.Image:
        if not self._looks_like_tactical_map_strip(rec):
            return render_notice(
                [
                    "This preview is for tactical-map sector sheets.",
                    "Known candidate records are 39..46.",
                ]
            )
        group = sorted(self._combined_strip_records(rec), key=lambda item: item.index)
        if len(group) < 4:
            return render_notice(
                [
                    "Tactical map sheet needs a four-record strip group.",
                    "Try selecting records 39..42 or 43..46.",
                    "Detected: " + ", ".join(f"{item.index:02d}" for item in group),
                ]
            )
        return render_tactical_map_sheet(
            group[:4],
            sector=sector,
            scale=2,
            orientation=self.orientation_mode.get(),
            show_relief_hatch=self.tactical_relief_hatch.get(),
            smooth_line_gaps=self.tactical_smooth_lines.get(),
        )

    def _render_combined_td3_strips(self, rec: AssetRecord, data: bytes) -> Image.Image | None:
        if not self.combine_strips.get():
            return None
        group = self._combined_strip_records(rec)
        if len(group) < 2:
            return None
        group = self._ordered_strip_records(group)

        palette = self._palette_for_current(rec)
        orientation = self.orientation_mode.get()
        width = self._combined_strip_width(group)
        images: list[Image.Image] = []
        for item in group:
            item_data = asset_path(item).read_bytes()
            try:
                images.append(render_td3_lzw_rle(item_data, width, self.preview_skip.get(), palette, orientation))
            except Exception as exc:
                return render_notice(
                    [
                        "Combined strip render failed.",
                        f"Record {item.index:02d} {item.candidate_name}",
                        str(exc),
                    ]
                )
        combined = Image.new("RGB", (width, sum(image.height for image in images)), "#000000")
        y = 0
        for image in images:
            combined.paste(image, (0, y))
            y += image.height
        return combined

    def _combined_strip_width(self, group: list[AssetRecord]) -> int:
        common_widths: set[int] | None = None
        fallback_widths: list[int] = []
        for item in group:
            try:
                _lzw, pixels = td3_lzw_rle_decode(asset_path(item).read_bytes(), max_lzw=1_000_000, max_pixels=1_000_000)
            except Exception:
                continue
            candidates = {width for width, _height in td3_dimension_candidates(len(pixels))}
            if candidates:
                common_widths = candidates if common_widths is None else common_widths & candidates
            fallback_widths.append(suggest_td3_width(len(pixels), item.index))

        if common_widths:
            bounded = [width for width in common_widths if width <= 320]
            if bounded:
                return max(bounded)
            return max(common_widths)
        if fallback_widths:
            return max(fallback_widths)
        return max(1, self.preview_width.get())

    def _on_asset_selected(self) -> None:
        rec = self._selected_record()
        if rec is None:
            return
        self.current_record = rec
        self.preview_zoom = 1.0
        self.preview_offset = [10.0, 10.0]
        data = asset_path(rec).read_bytes()
        strings = ascii_strings(data)
        ent = entropy(data)
        adjacent = adjacent_equal_ratio(data)
        self.asset_info.set(
            f"{rec.extracted_name} | {rec.source_file} 0x{rec.offset:06X}-0x{rec.end:06X} | "
            f"meta {rec.meta0:04X}/{rec.meta1:04X} | entropy {ent:.3f} | adjacent {adjacent:.2%} | strings {len(strings)}"
        )
        header = graphic_record_header(data)
        header_line = "DOS blit header: unavailable"
        if header is not None:
            prefix, width, height, swidth, sheight, stride = header
            header_line = (
                f"DOS blit header if expanded: prefix {prefix:02X}, primary {width}x{height}, "
                f"secondary {swidth}x{sheight}, stride/count {stride}"
            )
        td3_info = self._decoded_td3_info(rec, data)
        if td3_info is None:
            td3_line = "TD3 LZW+RLE trial: not run for this record"
            dim_line = "TD3 dimensions: unavailable"
            strip_line = "Strip group: unavailable"
        else:
            lzw_len, pixel_count, candidate_text = td3_info
            suggested = suggest_td3_width(pixel_count, rec.index)
            td3_line = f"TD3 LZW+RLE trial: {lzw_len} post-LZW bytes, {pixel_count} pixels"
            dim_line = f"TD3 dimensions: {candidate_text}; suggested width {suggested}"
            strip_group = self._combined_strip_records(rec)
            if len(strip_group) > 1:
                ordered_group = self._ordered_strip_records(strip_group)
                strip_line = f"Strip group ({self.strip_order_mode.get()}): " + ", ".join(
                    f"{item.index:02d} {item.candidate_name}" for item in ordered_group
                )
            else:
                strip_line = "Strip group: none detected"
        details = [
            "Candidate name mapping is provisional until the loader table is fully traced.",
            "High-entropy .LZ records use a Test Drive 3-style LZW wrapper followed by RLE pixels.",
            "Palette selection is still heuristic; try nearby rec XX palette entries when colors look wrong.",
            header_line,
            td3_line,
            dim_line,
            strip_line,
            "",
            "Hex:",
            hex_dump(data, 0, min(1024, len(data))),
            "",
            "ASCII strings:",
        ]
        for offset, text in strings[:80]:
            details.append(f"0x{offset:04X}: {text}")
        self.detail_text.delete("1.0", "end")
        self.detail_text.insert("1.0", "\n".join(details))
        self._auto_preview_defaults(rec, data)
        self._render_preview()

    def _auto_preview_defaults(self, rec: AssetRecord, data: bytes) -> None:
        if looks_like_palette_record(rec, data):
            self.preview_mode.set("Palette")
            self.palette_mode.set("auto palette")
        elif rec.index == 83:
            self.preview_mode.set("Indexed 8bpp")
            self.preview_width.set(96)
            self.preview_skip.set(5)
        elif self._looks_like_tactical_map_strip(rec):
            self.preview_mode.set("Tactical map sector")
            self.tactical_sector.set(0)
            self.preview_width.set(320)
            self.preview_skip.set(0)
            self.palette_mode.set("map material")
            self.orientation_mode.set("flip vertical")
        elif looks_like_compressed_graphics(data):
            self.preview_mode.set("TD3 LZW+RLE")
            td3_info = self._decoded_td3_info(rec, data)
            if td3_info is not None:
                self.preview_width.set(suggest_td3_width(td3_info[1], rec.index))
            else:
                self.preview_width.set(320)
            self.preview_skip.set(0)
            self.palette_mode.set("auto palette")
            self.orientation_mode.set("flip vertical")
        elif rec.length > 1000:
            self.preview_mode.set("Indexed 8bpp")
            self.preview_width.set(96 if rec.length < 10000 else 160)
            self.preview_skip.set(0)
        else:
            self.preview_mode.set("Hex only")

    def _render_preview(self) -> None:
        rec = self.current_record
        if rec is None:
            return
        data = asset_path(rec).read_bytes()
        mode = self.preview_mode.get()
        if mode == "Auto":
            if looks_like_palette_record(rec, data):
                mode = "Palette"
            elif self._looks_like_tactical_map_strip(rec):
                mode = "Tactical map sector"
            elif looks_like_compressed_graphics(data):
                mode = "TD3 LZW+RLE"
            elif rec.length > 1000:
                mode = "Indexed 8bpp"
            else:
                mode = "Hex only"
        try:
            if mode == "Palette":
                image = render_palette_strip(data)
            elif mode == "Tactical map sector":
                image = self._render_tactical_map_preview(rec, self.tactical_sector.get())
            elif mode == "Tactical map sheet":
                image = self._render_tactical_map_preview(rec, None)
            elif mode == "DOS direct blit":
                image = render_direct_blit_record(data, self._palette_for_current(rec))
            elif mode == "TD3 LZW+RLE":
                if not looks_like_compressed_graphics(data):
                    image = render_notice(
                        [
                            "This record does not look like a TD3-compressed image.",
                            f"Length: {rec.length} bytes",
                            f"Entropy: {entropy(data):.3f}",
                            f"Adjacent repeated bytes: {adjacent_equal_ratio(data):.2%}",
                            "Use Palette, Indexed 8bpp, Packed 4bpp, or Hex for this chunk.",
                        ]
                    )
                else:
                    try:
                        image = self._render_combined_td3_strips(rec, data)
                        if image is None:
                            image = render_td3_lzw_rle(
                                data,
                                self.preview_width.get(),
                                self.preview_skip.get(),
                                self._palette_for_current(rec),
                                self.orientation_mode.get(),
                            )
                    except Exception as exc:
                        image = render_notice(
                            [
                                "TD3 LZW+RLE decode failed for this record.",
                                str(exc),
                                "",
                                "This usually means the chunk is palette/control data",
                                "or uses a different record format.",
                            ]
                        )
            elif mode == "Compressed notice":
                image = render_notice(
                    [
                        "This chunk is compressed or encoded.",
                        "Raw Indexed/Packed previews show the compressed byte stream, not the image.",
                        f"Entropy: {entropy(data):.3f}",
                        f"Adjacent repeated bytes: {adjacent_equal_ratio(data):.2%}",
                        "Current best match: Test Drive 3-style LZW followed by RLE pixels.",
                    ]
                )
            elif mode == "Packed 4bpp":
                image = render_4bpp(data, self.preview_width.get(), self.preview_skip.get(), self._palette_for_current(rec))
            elif mode in ("Indexed 8bpp", "Auto"):
                image = render_indexed(data, self.preview_width.get(), self.preview_skip.get(), self._palette_for_current(rec))
            else:
                image = Image.new("RGB", (640, 360), "#161616")
        except Exception as exc:
            image = render_notice(["Preview failed.", str(exc)])
        self.preview_image = image
        self._draw_preview_on_canvas()

    def _render_map(self) -> None:
        rec = self._record_from_label(self.map_choice.get())
        if rec is None:
            return
        data = asset_path(rec).read_bytes()
        try:
            image = render_indexed(data, self.map_width.get(), self.map_skip.get(), "indexed")
            zoom = max(1, self.map_zoom.get())
            image = image.resize((image.width * zoom, image.height * zoom), Image.Resampling.NEAREST)
        except Exception as exc:
            messagebox.showerror("Map render failed", str(exc))
            return
        self.map_image = image
        self._draw_image_on_canvas(self.map_canvas, image, "map_photo")
        self.map_status.set(
            f"Chunk {rec.index:02d}, skip {self.map_skip.get()}, width {self.map_width.get()}, "
            f"rendered {image.width}x{image.height}. This is a candidate tile/raw view."
        )

    def _draw_image_on_canvas(self, canvas: tk.Canvas, image: Image.Image, attr: str) -> None:
        canvas.delete("all")
        max_w = max(240, canvas.winfo_width() or 900)
        max_h = max(180, canvas.winfo_height() or 500)
        display = image.copy()
        display.thumbnail((max(1, max_w - 20), max(1, max_h - 20)), Image.Resampling.NEAREST)
        photo = ImageTk.PhotoImage(display)
        setattr(self, attr, photo)
        canvas.create_image(10, 10, image=photo, anchor="nw")
        canvas.create_text(10, max_h - 18, text=f"{image.width} x {image.height}", fill="#d8d8d8", anchor="sw")

    def _draw_preview_on_canvas(self) -> None:
        canvas = self.preview_canvas
        canvas.delete("all")
        if self.preview_image is None:
            return
        max_w = max(240, canvas.winfo_width() or 900)
        max_h = max(180, canvas.winfo_height() or 500)
        self._clamp_preview_offset()
        scale = self._preview_display_scale()
        display_w = max(1, int(round(self.preview_image.width * scale)))
        display_h = max(1, int(round(self.preview_image.height * scale)))
        display = self.preview_image.resize((display_w, display_h), Image.Resampling.NEAREST)
        self.preview_photo = ImageTk.PhotoImage(display)
        canvas.create_image(
            int(round(self.preview_offset[0])),
            int(round(self.preview_offset[1])),
            image=self.preview_photo,
            anchor="nw",
        )
        canvas.create_text(
            10,
            max_h - 18,
            text=f"{self.preview_image.width} x {self.preview_image.height} | zoom {self.preview_zoom * 100:.0f}%",
            fill="#d8d8d8",
            anchor="sw",
        )

    def _export_preview(self) -> None:
        if self.preview_image is None:
            return
        default = OUT_DIR / "asset_preview.png"
        path = filedialog.asksaveasfilename(
            title="Export preview",
            initialfile=default.name,
            defaultextension=".png",
            filetypes=[("PNG", "*.png")],
        )
        if path:
            self.preview_image.save(path)

    def _export_map(self) -> None:
        image = getattr(self, "map_image", None)
        if image is None:
            self._render_map()
            image = getattr(self, "map_image", None)
        if image is None:
            return
        path = filedialog.asksaveasfilename(
            title="Export map preview",
            initialfile="map_candidate.png",
            defaultextension=".png",
            filetypes=[("PNG", "*.png")],
        )
        if path:
            image.save(path)

    def _save_logic_report(self) -> None:
        path = filedialog.asksaveasfilename(
            title="Save logic report",
            initialdir=str(OUT_DIR),
            initialfile="gunboat_logic_report.md",
            defaultextension=".md",
            filetypes=[("Markdown", "*.md"), ("Text", "*.txt")],
        )
        if path:
            pathlib.Path(path).write_text(self.logic_text.get("1.0", "end"), encoding="utf-8")

    def _show_embedded_names(self) -> None:
        top = tk.Toplevel(self)
        top.title("Embedded Asset Names")
        top.geometry("520x620")
        text = tk.Text(top, wrap="none", font=("Consolas", 10))
        text.pack(fill="both", expand=True)
        lines = [f"0x{offset:05X}: {name}" for offset, name in self.embedded_names]
        text.insert("1.0", "\n".join(lines))


def main() -> None:
    app = GunboatAssetExplorer()
    app.after(250, app._render_map)
    app.mainloop()


if __name__ == "__main__":
    main()
