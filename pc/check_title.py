#!/usr/bin/env python3
"""Checks that the game walks past the language select to the title screen (T18).

Runs pc/build/boot with a scripted key sequence (boot's fifth argument, see
pc/shim/host_pad.c) that walks the whole chain the game's own code takes:

    down,down,down,down,start
                    the country select moves its highlight off ENGLISH and
                    confirms; the owner reads Spanish, so the demo walks down
                    to ESPAÑOL (the same LANG_KEYS the other demos use). A
                    non-English language makes the game request
                    MODE_OPENING_2 (0xE02), not MODE_OPENING
    wait            hold until the game's own mode changes
    start           skip the movie STDWTITL_start turns 0xE02 into

and then verifies, from the harness's own output, that the game's own code is
what carried it from one mode to the next:

  * the run boots into the country select (mode 0x1600);
  * confirming requests the title overlay's second opening movie (0xE02,
    which is STDWTITL, `mode >> 8` == 0xE) -- the movie's own task, so the
    dispatcher the port links is the game's own low-byte choice
    (src/menus/title/scene.c);
  * the movie decodes: MDEC_DEBUG's per-frame lines show the port's own MDEC
    (pc/shim/host_press.c over pc/shim/host_mdec.c) delivering frames with no
    errors, and the movie's teardown line reports those frames as presented;
  * skipping the movie with Start makes the game's own movie task request the
    title screen (0xE00);
  * the title screen then draws on its own layer 0x1000, with sprites (the
    title's images come from the disc through the game's own TIM loader), and
    it is still drawing on the run's last frame.

A second run (T42) lets the movie play instead of skipping it, because the
frames this chain captures while the mode is 0xE02 hold STDWTITL's Bandai
splash -- a logo in the middle of the clear -- and not the decoded frame. In
that run the picture must land where the game put it: the movie's rect on the
320x480 display (y 36..451, i.e. rows 18..225 of the 320x240 window or 36..451
of the 640x480 one) across the full width, with the clear above and below, and
it must be a picture (many distinct colours and a mean brightness well above
black) that advances between captures. The bound is deliberately coarse: "the
frame is not blank and sits in its rect", not a comparison against an
emulator's screen.

The movie's picture indices are the frames the movie is up and drawing in the
second, non-skipping run: the country select confirms around frame 200, the
movie's mode starts at 206 and its decoded frame owns the screen from ~300 on
(measured; the pad script is frame-counted, so the window does not depend on
how fast the host renders). The title chain's own capture list is no longer
used.

The run opens the PsyCross window and types keys at it, so it needs a display
(like check_font.py, check_country.py and check_input.py). Its glReadPixels
captures go to a throwaway directory. Nothing of the disc is stored (ADR-001).

    make -C pc                            # or: make -C pc title-demo
    .venv/bin/python pc/check_title.py <disc .bin>

DW3_WINDOW=640x480 runs the same scripted flow in a bigger window (T41): the
window is what glReadPixels captures, so the capture size follows it, and a
480-line display (the movie's) then gets the 2x width the PS1's interlaced
mode means instead of being squeezed into 240 lines.

Exits non-zero on the first failing case and reports each one.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TOOL = ROOT / "pc" / "build" / "boot"

# The same sequence the title-demo target types (TITLE_KEYS in pc/Makefile):
# walk down to ESPAÑOL, wait for the game to move into the opening movie, skip
# it. The owner reads Spanish; the language only has to be non-English for the
# game to take the MODE_OPENING_2 branch.
KEYS = "down,down,down,down,start,wait,start"
FRAMES = 320

# T41: the window the port opens. boot reads DW3_WINDOW ("WxH", see pc/boot.c)
# and defaults to the game's own 320x240, so the same scripted run validates
# both sizes: the captures come from glReadPixels of the window, and the
# boot line names the size PsyX_Initialise was given.
WINDOW = os.environ.get("DW3_WINDOW", "320x240").strip().lower()
try:
    WIDTH, HEIGHT = (int(value) for value in WINDOW.split("x", 1))
except ValueError:
    sys.exit(f"DW3_WINDOW must be WIDTHxHEIGHT, not {WINDOW!r}")
if WIDTH <= 0 or HEIGHT <= 0:
    sys.exit(f"DW3_WINDOW must be positive, not {WINDOW!r}")

# T42: the frames that hold the movie's own picture. The title chain above
# skips the video around frame 230 (the movie's mode, 0xE02, is up from game
# frame 206), and what is on screen before that is STDWTITL's Bandai splash
# (a logo in the middle of the clear), so the decoded frame only exists in a
# run that lets the movie play: measured, the picture owns the movie's display
# rect from ~300 on. boot's 7th argument (T30) writes <stem>_f<index>.png.
MOVIE_KEYS = "down,down,down,down,start,wait"
MOVIE_FRAMES = 340
MOVIE_PICTURE_FRAMES = [300, 320, 335]

# T42: where the picture must land. The game sets a 320x480 display for the
# movie and pc/shim/host_press.c presents it with LoadImage(0, 36, 320, 416)
# (movie.c's rect.y and movieHeight), so in the display the picture is the
# rect x 0..319, y 36..451. The window scales that display, so the rect lands
# at rows 36 * HEIGHT/480 .. 451 * HEIGHT/480: 18..225 in the 320x240 default
# and 36..451 at 640x480, which is the 480i geometry (width doubled, height
# kept) instead of a squeezed picture.
DISPLAY_HEIGHT = 480
MOVIE_RECT_Y = 36
MOVIE_RECT_H = 416

MODE_COUNTRY_SELECT = "0x1600"
MODE_OPENING_2 = "0xE02"
MODE_TITLE = "0xE00"

TITLE_LAYER = "0x1000"

LINE_MODE = re.compile(r"boot: mode (0x[0-9A-F]+)")
LINE_WINDOW = re.compile(r"boot: the game's own main\(\), (\d+)x(\d+) window")
LINE_LAYER = re.compile(r"frame (\d+): layer (0x[0-9A-F]+) prims=(\d+) "
                        r"\(sprite=(\d+) poly=(\d+) env=(\d+) other=(\d+)\) bytes=(\d+)")

# T39: the MDEC's own lines under MDEC_DEBUG=1 (pc/shim/host_press.c). One per
# decoded frame, then the movie's totals when the game tears the callback down.
LINE_MDEC_FRAME = re.compile(r"mdec: frame (\d+) (\d+)x(\d+) macroblocks (\d+) "
                             r"blocks (\d+) errors (\d+) tokens (\d+)")
LINE_MDEC_MOVIE = re.compile(r"mdec: (\d+) frames decoded, (\d+) presented, "
                             r"(\d+) errors over the movie")

# T39: the movie picture must be a picture, not a blank. The bound is
# deliberately coarse: "the frame is not blank", not a comparison with an
# emulator's screen.
MIN_MOVIE_COLORS = 64
MIN_MOVIE_MEAN = 8.0

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def multi_capture_path(shot_path, index):
    """The per-index path the host writes for a capture list: `<stem>_f<n>.png`
    (pc/shim/gfx_engine.c, replicated in pc/check_field.py)."""
    p = Path(shot_path)
    stem = p.with_suffix("")
    return stem.parent / f"{stem.name}_f{index}.png"


def png_analysis(data, width, height):
    """Where the picture is in a width x height 8-bit RGB PNG as the harness
    writes it (pc/shim/host_shot.c: unfiltered scanlines in stored deflate
    blocks), or None when the data is not such a PNG.

    The background is the most common colour of the first scanline: every
    frame this port captures is the game's display over the renderer's clear,
    and the first scanline is always above the game's own drawing area here
    (the movie's rect starts at y 36 of the 480-line display). The most common
    colour of the whole image would be a picture colour in a bright, flat
    scene. Returns the bounding box of the pixels that differ from it
    (top/bottom/left/right), and the number of distinct colours and the mean
    brightness inside that box."""
    if not data.startswith(PNG_SIGNATURE) or len(data) <= 24:
        return None
    if int.from_bytes(data[16:20], "big") != width \
            or int.from_bytes(data[20:24], "big") != height \
            or data[24] != 8 or data[25] != 2:
        return None
    at = 8
    idat = b""
    while at < len(data):
        length = int.from_bytes(data[at:at + 4], "big")
        kind = data[at + 4:at + 8]
        if kind == b"IDAT":
            idat += data[at + 8:at + 8 + length]
        elif kind == b"IEND":
            break
        at += 12 + length
    try:
        raw = zlib.decompress(idat)
    except zlib.error:
        return None
    stride = 1 + width * 3
    if len(raw) != height * stride:
        return None
    rows = []
    for y in range(height):
        base = y * stride
        if raw[base] != 0:  # the harness writes filter 0
            return None
        rows.append(raw[base + 1:base + 1 + width * 3])

    counts = {}
    for x in range(width):
        pixel = rows[0][x * 3:x * 3 + 3]
        counts[pixel] = counts.get(pixel, 0) + 1
    background = max(counts, key=counts.get)

    top = bottom = left = right = None
    solid = set()
    total = 0
    pixels = 0
    for y, row in enumerate(rows):
        for x in range(width):
            pixel = row[x * 3:x * 3 + 3]
            if pixel == background:
                continue
            solid.add(pixel)
            total += pixel[0] + pixel[1] + pixel[2]
            pixels += 1
            if top is None:
                top = y
            bottom = y
            if left is None or x < left:
                left = x
            if right is None or x > right:
                right = x
    return {"top": top, "bottom": bottom, "left": left, "right": right,
            "colours": len(solid),
            "mean": total / (pixels * 3) if pixels else 0.0}


def run_boot(image_path, frames, keys, capture_frames=()):
    """One harness run in a throwaway directory: PsyCross writes its own
    <app>.log in the working directory, and the captures are glReadPixels of
    the window. MDEC_DEBUG=1 is on so the movie's own lines are in the output.
    Returns (stdout, final shot bytes, {index: capture bytes})."""
    stdbuf = shutil.which("stdbuf")
    with tempfile.TemporaryDirectory() as workdir:
        shot = Path(workdir) / "shot.png"
        command = [str(TOOL), str(image_path.resolve()), str(frames),
                   str(shot), "0", keys]
        if capture_frames:
            command.append(",".join(str(index) for index in capture_frames))
        if stdbuf is not None:
            command = [stdbuf, "-oL"] + command
        # `[gfx] frame N: layer` lines are trace lines of the port's log (feature port-log T2);
        # stderr joins stdout so the order holds
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, cwd=workdir,
                              env={**os.environ, "MDEC_DEBUG": "1", "DW3_LOG": "trace"})
        shot_bytes = shot.read_bytes() if shot.is_file() else b""
        captures = {index: (multi_capture_path(shot, index).read_bytes()
                            if multi_capture_path(shot, index).is_file() else b"")
                    for index in capture_frames}
    if proc.returncode != 0:
        sys.exit(f"boot exited {proc.returncode}: {proc.stdout[-400:].strip()}")
    return proc.stdout, shot_bytes, captures


def main():
    if len(sys.argv) != 2:
        sys.exit(f"usage: {Path(sys.argv[0]).name} <disc .bin>")
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image_path = Path(sys.argv[1])
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    failures = 0

    def fail(what, detail):
        nonlocal failures
        print(f"FAIL {what}: {detail}")
        failures += 1

    # The title chain: one harness run in a throwaway working directory (see
    # run_boot), line-buffered so the movie's own lines survive even a
    # signal-killed run.
    stdout, shot_bytes, _ = run_boot(image_path, FRAMES, KEYS)

    # 0. The window PsyX_Initialise was given (T41). boot reads DW3_WINDOW and
    #    defaults to the game's own 320x240; the run reports the size the
    #    renderer really has, and the captures above are glReadPixels of it.
    window = LINE_WINDOW.search(stdout)
    if window is None:
        fail("the window", "the run never printed its window size")
    elif (int(window.group(1)), int(window.group(2))) != (WIDTH, HEIGHT):
        fail("the window", f"the run opened {window.group(1)}x{window.group(2)}, "
                           f"{WIDTH}x{HEIGHT} expected")
    else:
        print(f"ok   the run opened a {WIDTH}x{HEIGHT} window")
    modes = LINE_MODE.findall(stdout)
    frames = [{"frame": int(m.group(1)),
               "layer": m.group(2),
               "prims": int(m.group(3)),
               "sprite": int(m.group(4)),
               "env": int(m.group(6)),
               "bytes": int(m.group(8))}
              for m in LINE_LAYER.finditer(stdout)]

    # 1. The chain of modes the game's own code asked for, in order. The port's
    #    dispatcher prints each one; two of the three are the ones this task
    #    added (0xE02 and 0xE00), so reaching them proves the entry-point table
    #    now holds STDWTITL and that the game's own low-byte dispatch chose the
    #    movie and then the title.
    if not modes or modes[0] != MODE_COUNTRY_SELECT:
        fail("the boot mode", f"the run started on {modes[0] if modes else 'no mode'}, "
                              f"{MODE_COUNTRY_SELECT} expected")
    else:
        print(f"ok   the run boots into the country select (mode {MODE_COUNTRY_SELECT})")
    if MODE_OPENING_2 not in modes:
        fail("the mode after confirming",
             f"the run never reached the title overlay's opening movie "
             f"({MODE_OPENING_2}); modes seen: {modes}")
    elif modes.index(MODE_OPENING_2) < modes.index(MODE_COUNTRY_SELECT):
        fail("the mode after confirming", "the movie came before the country select")
    else:
        print(f"ok   confirming the language requests the title overlay's movie "
              f"(mode {MODE_OPENING_2}, the game's own low-byte dispatch)")
    if MODE_TITLE not in modes:
        fail("the title screen mode",
             f"the run never reached the title screen ({MODE_TITLE}); modes seen: {modes}")
    elif modes.index(MODE_TITLE) < modes.index(MODE_OPENING_2):
        fail("the title screen mode", "the title screen came before the movie")
    else:
        print(f"ok   skipping the movie makes the game's own task request the "
              f"title screen (mode {MODE_TITLE})")

    # 2. The movie decodes, and the port presents it (T39). The per-frame lines
    #    are the port's own MDEC behind DecDCTin; the movie's teardown line is
    #    the same counters, and not one of the frames may fail to decode.
    decoded = [(int(m.group(1)), int(m.group(2)), int(m.group(3)),
                int(m.group(4)), int(m.group(5)), int(m.group(6)),
                int(m.group(7)))
               for m in LINE_MDEC_FRAME.finditer(stdout)]
    movie = LINE_MDEC_MOVIE.findall(stdout)
    if len(decoded) < 10:
        fail("the movie's decoding",
             f"{len(decoded)} frame(s) decoded (10+ expected); "
             f"the movie mode was not reached or nothing was decoded")
    elif any(frame[5] != 0 for frame in decoded):
        bad = [frame for frame in decoded if frame[5] != 0]
        fail("the movie's decoding",
             f"{len(bad)} frame(s) decoded with errors (first: {bad[0]})")
    elif any(frame[1:3] != (320, 416) for frame in decoded):
        fail("the movie's decoding",
             f"frames are not 320x416: {sorted({f[1:3] for f in decoded})}")
    else:
        print(f"ok   the port's MDEC decoded {len(decoded)} movie frame(s) at "
              f"320x416 with no errors")
    if not movie:
        fail("the movie's presentation",
             "the run never reported the movie's totals "
             "(MDEC_DEBUG's teardown line, pc/shim/host_press.c)")
    else:
        decoded_total, presented_total, errors_total = (int(v) for v in movie[-1])
        if decoded_total != len(decoded) or presented_total != decoded_total:
            fail("the movie's presentation",
                 f"{decoded_total} decoded, {presented_total} presented, "
                 f"{len(decoded)} per-frame lines")
        elif errors_total != 0:
            fail("the movie's presentation",
                 f"{errors_total} error(s) over the movie")
        else:
            print(f"ok   every decoded frame was presented "
                  f"({presented_total} frames, {errors_total} errors)")

    # 3. (T42) the movie's picture is checked by its own run, further down:
    #    the frames captured during this title chain hold STDWTITL's Bandai
    #    splash, not the decoded frame.

    # 4. The title screen drew on its own layer (0x1000, title.h's
    #    STDWTITL_TITLE_LAYER), with sprites -- the title's images come from the
    #    disc through the game's own TIM loader and sprite drawer.
    title_frames = [f for f in frames if f["layer"] == TITLE_LAYER]
    if not title_frames:
        fail("the title screen's drawing",
             f"no frame drew on layer {TITLE_LAYER}; layers seen: "
             f"{sorted({f['layer'] for f in frames})}")
    elif not any(f["sprite"] > 0 for f in title_frames):
        fail("the title screen's drawing",
             f"layer {TITLE_LAYER} never drew a sprite "
             f"(first frame: {title_frames[0]})")
    else:
        drawn = [f for f in title_frames if f["sprite"] > 0]
        print(f"ok   the title screen drew its own sprites on layer {TITLE_LAYER} "
              f"({len(drawn)} frame(s), first with {drawn[0]['sprite']} sprites, "
              f"last with {drawn[-1]['sprite']})")
    if title_frames and title_frames[-1]["sprite"] == 0:
        fail("the capture's frame",
             "the run's last frame drew no sprite on the title layer")
    else:
        print("ok   the run was still drawing the title screen on its last frame")

    # 5. The capture the demo takes with glReadPixels at the end (the title).
    if not shot_bytes.startswith(PNG_SIGNATURE):
        fail("the screenshot", "the harness wrote no PNG capture")
    elif len(shot_bytes) <= 24 or int.from_bytes(shot_bytes[16:20], "big") != WIDTH \
            or int.from_bytes(shot_bytes[20:24], "big") != HEIGHT:
        fail("the screenshot", f"the capture is not a {WIDTH}x{HEIGHT} PNG")
    else:
        print(f"ok   the run captured its frame to a {WIDTH}x{HEIGHT} PNG "
              f"({len(shot_bytes)} bytes)")

    # 6. The movie's own picture (T42). The title chain above skips the video
    #    around frame 230, and what the movie's mode shows before that is
    #    STDWTITL's Bandai splash: a logo in the middle of the clear. The
    #    decoded frame only exists in a run that lets the movie play, so this
    #    is a second run, and the picture it captures must be where the game
    #    put it: the movie's rect on the 320x480 display (y 36..451, full
    #    width), with the clear colour above and below. A splash-shaped
    #    picture fails both bounds, and so does a squeezed or shifted frame.
    movie_stdout, _, movie_pngs = run_boot(image_path, MOVIE_FRAMES,
                                           MOVIE_KEYS, MOVIE_PICTURE_FRAMES)
    top = MOVIE_RECT_Y * HEIGHT // DISPLAY_HEIGHT
    bottom = (MOVIE_RECT_Y + MOVIE_RECT_H) * HEIGHT // DISPLAY_HEIGHT - 1
    seen = []
    for index in MOVIE_PICTURE_FRAMES:
        data = movie_pngs.get(index, b"")
        picture = png_analysis(data, WIDTH, HEIGHT) if data else None
        if picture is None:
            fail(f"the movie's picture at frame {index}",
                 f"the harness wrote no {WIDTH}x{HEIGHT} 8-bit RGB PNG")
            continue
        seen.append((index, picture))
        if picture["top"] is None:
            fail(f"the movie's picture at frame {index}",
                 "the capture is the clear colour alone (no frame drawn)")
            continue
        if abs(picture["top"] - top) > 1 or abs(picture["bottom"] - bottom) > 1:
            fail(f"the movie's picture at frame {index}",
                 f"the picture spans rows {picture['top']}..{picture['bottom']}, "
                 f"the movie's {MOVIE_RECT_H}-line rect on the "
                 f"{DISPLAY_HEIGHT}-line display is rows {top}..{bottom}")
        elif picture["left"] > 2 or picture["right"] < WIDTH - 3:
            fail(f"the movie's picture at frame {index}",
                 f"the picture spans columns {picture['left']}..{picture['right']}, "
                 f"the movie's rect is the full {WIDTH} columns")
        elif (picture["colours"] < MIN_MOVIE_COLORS
              or picture["mean"] < MIN_MOVIE_MEAN):
            fail(f"the movie's picture at frame {index}",
                 f"the frame is blank ({picture['colours']} colours, "
                 f"mean {picture['mean']:.2f}; need {MIN_MOVIE_COLORS}+ colours "
                 f"and mean {MIN_MOVIE_MEAN}+)")
        else:
            print(f"ok   the movie's own frame is on screen at {index} "
                  f"(rows {picture['top']}..{picture['bottom']} of "
                  f"{HEIGHT}, {picture['colours']} colours, "
                  f"mean {picture['mean']:.2f})")
    if len(seen) > 1:
        blobs = {movie_pngs.get(index, b"") for index, _ in seen}
        if len(blobs) < 2:
            fail("the movie's picture",
                 "the captures are identical: the movie is not advancing")
    errors = [int(m.group(6)) for m in LINE_MDEC_FRAME.finditer(movie_stdout)]
    if not errors:
        fail("the movie's run", "the run decoded no frame (no MDEC_DEBUG lines)")
    elif any(count != 0 for count in errors):
        fail("the movie's run",
             f"{sum(1 for count in errors if count != 0)} frame(s) decoded "
             f"with errors")
    else:
        print(f"ok   the movie's run decoded {len(errors)} frame(s) with no "
              f"errors while it played")

    if failures != 0:
        sys.exit(f"{failures} check(s) failed")
    print("the game's own code walked the country select, its opening movie and "
          "the title screen, the movie decoded and showed its frames, and the "
          "title's own code drew the screen from the disc")



if __name__ == "__main__":
    main()
