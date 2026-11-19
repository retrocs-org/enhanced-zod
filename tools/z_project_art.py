"""Deterministic project branding; contains no original game raster data."""
from PIL import Image, ImageDraw


def brand_icon(size):
    image = Image.new('RGBA', (128, 128), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle((3, 3, 124, 124), radius=23, fill=(25, 29, 34, 255), outline=(131, 141, 153, 255), width=3)
    draw.rounded_rectangle((12, 12, 115, 115), radius=16, fill=(102, 22, 16, 255))
    draw.polygon([(27, 27), (102, 27), (102, 44), (51, 87), (102, 87), (102, 104),
                  (25, 104), (25, 85), (76, 44), (27, 44)], fill=(255, 156, 36, 255))
    for x in (15, 112):
        for y in (15, 112):
            draw.ellipse((x-3, y-3, x+3, y+3), fill=(169, 177, 188, 255))
    return image.resize(size, Image.Resampling.LANCZOS)


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    brand_icon((512, 512)).save(args.output)
