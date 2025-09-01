#!/usr/bin/env python3
"""
Visualize timing of log data as a raster image where 1 ms = 1 px horizontally.

Usage:
  python log_timeline.py BusLog.log --start 1125400 --end 1125600 --out timeline.png
"""

import argparse
import os.path
import re
from collections import defaultdict
from typing import Dict, List, Tuple

from PIL import Image, ImageDraw, ImageFont

LINE_RE = re.compile(r"^([#A-Z])(\d+),(.*)$")

BASE_LABELS = {
    "A": "Ambient",
    "G": "GPS",
    "M": "Motion",
    "P": "Pressure",
}

PALETTE = [
    (31, 119, 180), (44, 160, 44), (214, 39, 40), (148, 103, 189), (140, 86, 75),
    (227, 119, 194), (127, 127, 127), (188, 189, 34), (23, 190, 207), (255, 127, 14)
]


def parse_log(filepath: str, start_ms: int, end_ms: int) -> Dict[str, List[int]]:
    rows: Dict[str, List[int]] = defaultdict(list)
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            m = LINE_RE.match(line)
            if not m:
                continue
            typ, t_ms_str, rest = m.groups()
            try:
                t = int(t_ms_str)
            except ValueError:
                continue
            if not (start_ms <= t <= end_ms):
                continue
            if typ == '#':
                parts = rest.split(',')
                subtype = parts[0].strip() if parts else ""
                label = f"# {subtype}" if subtype else "Comment"
            else:
                label = BASE_LABELS.get(typ, f"Other {typ}")
            rows[label].append(t)
    for k in rows:
        rows[k].sort()
    return rows


def measure_label_gutter(labels: List[str], font: ImageFont.ImageFont) -> int:
    # Use getbbox (available in newer Pillow) to measure text width
    max_w = 0
    for s in labels:
        bbox = font.getbbox(s)
        w = bbox[2] - bbox[0]
        max_w = max(max_w, w)
    return max_w + 16


def assign_colors(labels: List[str]) -> Dict[str, Tuple[int, int, int]]:
    return {lbl: PALETTE[i % len(PALETTE)] for i, lbl in enumerate(labels)}


def render_timeline(rows: Dict[str, List[int]], start_ms: int, end_ms: int, out_path: str,
                    row_height: int = 18, row_gap: int = 6, bg=(255, 255, 255)) -> None:
    labels = sorted(rows.keys()) or ["(no data)"]
    try:
        font = ImageFont.truetype("DejaVuSans.ttf", 13)
    except Exception:
        font = ImageFont.load_default()
    gutter = measure_label_gutter(labels, font)
    width = (end_ms - start_ms) + 1
    height = len(labels) * (row_height + row_gap) + row_gap
    img = Image.new("RGB", (gutter + width, height), bg)
    draw = ImageDraw.Draw(img)
    colors = assign_colors(labels)
    y = row_gap
    for lbl in labels:
        draw.text((8, y), lbl, fill=(0, 0, 0), font=font)
        band_top = y
        band_bottom = y + row_height - 1
        draw.rectangle([(gutter, band_top), (gutter + width - 1, band_bottom)], fill=(245, 245, 245))
        color = colors[lbl]
        for t in rows.get(lbl, []):
            x = gutter + (t - start_ms)
            if gutter <= x < gutter + width:
                draw.line([(x, band_top), (x, band_bottom)], fill=color, width=1)
        draw.line([(0, band_bottom + 1), (gutter + width - 1, band_bottom + 1)], fill=(220, 220, 220), width=1)
        y += row_height + row_gap
    img.save(out_path)


def main() -> None:
    ap = argparse.ArgumentParser(description="Visualize timing of log data (1 ms = 1 px)")
    ap.add_argument("logfile")
    ap.add_argument("--start", type=int, required=True)
    ap.add_argument("--end", type=int, required=True)
    args = ap.parse_args()
    rows = parse_log(args.logfile, args.start, args.end)
    out_path = os.path.splitext(os.path.split(args.logfile)[-1])[0] + f"_{args.start}_{args.end}.png"
    render_timeline(rows, args.start, args.end, out_path)
    print(f"Done.")


if __name__ == "__main__":
    main()
