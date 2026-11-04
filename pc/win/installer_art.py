#!/usr/bin/env python3
"""Installer art from one square picture (pc/win/installer.png).

    installer_art.py <source.png> <out-dir>
    installer_art.py --icon <source.png> <out.ico>   (the executable's icon, pc/Makefile)

Writes what NSIS's Modern UI takes:
  icon.ico     the installer's and uninstaller's icon (16 to 256 px)
  welcome.bmp  the welcome and finish pages' side panel, 164x314: the picture on
               its corners' blue, its edges faded into it so no square shows
  header.bmp   the other pages' header, 150x57: the picture at the right on the
               header's white
"""
import sys

from PIL import Image, ImageDraw, ImageFilter

ICON_SIZES = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
WELCOME = (164, 314)
HEADER = (150, 57)


def corner_color(img):
    n = img.width - 3
    corners = [img.getpixel(p) for p in ((2, 2), (n, 2), (2, n), (n, n))]
    return tuple(round(sum(c[i] for c in corners) / 4) for i in range(3))


def feathered(art, edge):
    """A mask that fades the picture's outer EDGE pixels to nothing"""
    mask = Image.new("L", art.size, 0)
    ImageDraw.Draw(mask).rectangle((edge, edge, art.width - 1 - edge, art.height - 1 - edge), fill=255)
    return mask.filter(ImageFilter.GaussianBlur(edge / 2))


def main():
    if sys.argv[1] == "--icon":
        Image.open(sys.argv[2]).convert("RGB").save(sys.argv[3], sizes=ICON_SIZES)
        return
    src = Image.open(sys.argv[1]).convert("RGB")
    out = sys.argv[2]

    src.save(f"{out}/icon.ico", sizes=ICON_SIZES)

    panel = Image.new("RGB", WELCOME, corner_color(src))
    side = WELCOME[0]
    art = src.resize((side, side), Image.LANCZOS)
    panel.paste(art, (0, (WELCOME[1] - side) // 3), feathered(art, 10))
    panel.save(f"{out}/welcome.bmp")

    header = Image.new("RGB", HEADER, (255, 255, 255))
    art = src.resize((HEADER[1], HEADER[1]), Image.LANCZOS)
    header.paste(art, (HEADER[0] - HEADER[1], 0))
    header.save(f"{out}/header.bmp")


if __name__ == "__main__":
    main()
