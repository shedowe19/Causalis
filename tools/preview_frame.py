#!/usr/bin/env python3
"""Draw an engine JSON display list for layout inspection; requires Pillow.

This is a debugging output, not a Windows host screenshot. Text glyphs are
scaled to the engine's approximate advance widths; Windows uses real metrics.
"""
import argparse
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("frame", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    frame = json.loads(args.frame.read_text(encoding="utf-8"))
    width = min(4096, max(1, math.ceil(frame["width"])))
    height = min(4096, max(1, math.ceil(frame["height"])))
    image = Image.new("RGB", (width, height), "white")
    draw = ImageDraw.Draw(image)
    fonts = {}
    for command in frame["commands"]:
        x, y, w, h = [command[k] for k in ("x", "y", "width", "height")]
        if not all(math.isfinite(v) for v in (x, y, w, h)):
            raise ValueError("Non-finite engine geometry")
        color = tuple(command["color"])
        if command["kind"] == "rect":
            if w > 0 and h > 0:
                draw.rectangle((x, y, x + w, y + h), fill=color)
        elif command["kind"] == "text":
            key = (max(1, min(256, round(command["font_size"]))), command["bold"])
            if key not in fonts:
                name = "DejaVuSans-Bold.ttf" if key[1] else "DejaVuSans.ttf"
                try:
                    fonts[key] = ImageFont.truetype(name, key[0])
                except OSError:
                    fonts[key] = ImageFont.load_default(size=key[0])
            font = fonts[key]
            bounds = draw.textbbox((0, 0), command["text"], font=font, anchor="lt")
            natural_width = max(1, math.ceil(bounds[2]))
            natural_height = max(1, math.ceil(bounds[3]))
            glyphs = Image.new("RGBA", (natural_width, natural_height))
            ImageDraw.Draw(glyphs).text((0, 0), command["text"], fill=color + (255,), font=font, anchor="lt")
            target_width = max(1, min(4096, round(w)))
            if target_width != natural_width:
                glyphs = glyphs.resize((target_width, natural_height), Image.Resampling.LANCZOS)
            image.paste(glyphs, (round(x), round(y)), glyphs)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    image.save(args.output)
    print(f"Display-list preview: {width} x {height}")


if __name__ == "__main__":
    main()
