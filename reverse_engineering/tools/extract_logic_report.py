#!/usr/bin/env python3
"""Write a text report of currently visible Gunboat gameplay logic."""

from __future__ import annotations

import pathlib

from gunboat_formats import OUT_DIR, logic_report


def main() -> None:
    out = OUT_DIR / "gunboat_logic_report.md"
    out.write_text(logic_report(), encoding="utf-8")
    print(f"wrote: {out}")


if __name__ == "__main__":
    main()
