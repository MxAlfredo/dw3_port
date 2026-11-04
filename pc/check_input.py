#!/usr/bin/env python3
"""Checks the game's own input path (T17).

Runs pc/build/boot with a scripted key sequence (boot's fifth argument, see
pc/shim/host_pad.c): the harness writes the scripted pad state in process
(T21: no XTest, no keystrokes anywhere near the owner's session), which is the
its libpad, and the game's own src/engine/pad/{pad,port}.c -- and prints one
line a frame with what the game's own pad code saw and the option the country
select's own menu task highlights:

    pad 040: raw=FFBF held=0040 pressed=0040 menu=1/7 sel=1

`raw` is the pad word in the buffer PadInitDirect handed to libpad (what the
game's own readPad reads); `held`/`pressed` are the logical buttons its own
readPadButtons built from it; `menu` is the state/substate of the country
select's controller task (MENU_SELECT is 7 in menus/country_select.h) and `sel`
its highlighted option.

Two things are verified from that output, both about the game's own code:

  * every scripted press reached the game: a frame whose logical pressed word
    carries that button's bit (PAD_DOWN is 6 and PAD_UP is 4 in
    include/engine/pad.h) and whose raw word has the bit clear (a PS1 pad sends
    a clear bit while a button is down), counted once per press;
  * the menu responded with the option sequence those presses ask for:
    'down,down,up' moves the highlighted option 0 -> 1 -> 2 -> 1.

The run opens the PsyCross window (it does not take the focus and never touches
the keyboard), so it needs a display (like check_font.py and check_country.py;
the other five checks are headless).
Its glReadPixels capture goes to the throwaway directory. Nothing of the disc
is stored (ADR-001).

    make -C pc
    .venv/bin/python pc/check_input.py <disc .bin>

Exits non-zero on the first failing case and reports each one.
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

TOOL = ROOT / "pc" / "build" / "boot"

# A sequence whose effect the game's own menu makes visible: four steps down the
# column, which is also the ESPAÑOL option (T20), so the screen the demo leaves
# behind is readable in Spanish (country_select.c's moveSelection).
KEYS = "down,down,down,down"
KEYS_EXPECTED = [1, 2, 3, 4]
FRAMES = 200

# include/engine/pad.h: the logical buttons, and MENU_SELECT of
# menus/country_select.h.
PAD_UP = 1 << 4
PAD_DOWN = 1 << 6
MENU_SELECT = 7

LINE_PAD = re.compile(r"pad (\d+): raw=([0-9A-F]{4}) held=([0-9A-F]{4}) "
                      r"pressed=([0-9A-F]{4}) menu=(-?\d+)/(-?\d+) sel=(-?\d+)")

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def parse(stdout):
    """The harness's per-frame pad lines, in frame order."""
    frames = []
    for line in stdout.splitlines():
        m = LINE_PAD.search(line)
        if m:
            frames.append({
                "frame": int(m.group(1)),
                "raw": int(m.group(2), 16),
                "held": int(m.group(3), 16),
                "pressed": int(m.group(4), 16),
                "state": int(m.group(5)),
                "substate": int(m.group(6)),
                "sel": int(m.group(7)),
            })
    return frames


def collapsed(values):
    """The values with consecutive duplicates dropped."""
    out = []
    for value in values:
        if not out or out[-1] != value:
            out.append(value)
    return out


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

    # PsyCross writes its own <app>.log in the working directory: run in a
    # throwaway one so a check run leaves nothing behind.
    shot = None
    with tempfile.TemporaryDirectory() as workdir:
        shot = Path(workdir) / "shot.png"
        proc = subprocess.run([str(TOOL), str(image_path.resolve()), str(FRAMES),
                               str(shot), "0", KEYS],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                              cwd=workdir,
                              # the `[pad] pad N: raw=` lines are trace lines of the port's log (port-log T2)
                              env={**os.environ, "DW3_LOG": "trace", "DW3_LOG_CHANNELS": "pad"})
        shot_bytes = shot.read_bytes() if shot.is_file() else b""
    if proc.returncode != 0:
        sys.exit(f"boot exited {proc.returncode}: {proc.stdout[-400:].strip()}")

    frames = parse(proc.stdout)

    # 1. The game's own pad code ran, frame after frame, and reached the menu
    #    substate that reads it.
    if len(frames) < FRAMES - 4:
        fail("the game's pad code",
             f"{len(frames)} frames of pad output, {FRAMES} asked for "
             f"(the harness prints one a frame)")
    selecting = [f for f in frames if f["state"] == 1 and f["substate"] == MENU_SELECT]
    if not selecting:
        fail("the country select's menu",
             "never reached MENU_SELECT in the pad output")

    # 2. Each scripted press reached the game's own pad code: the logical
    #    pressed word carries the button's bit, the raw word has it clear, and
    #    it happened while the menu was reading the pad.
    for name, bit, count in (("down", PAD_DOWN, 4),):
        hits = [f for f in frames if (f["pressed"] & bit) and not (f["raw"] & bit)]
        if len(hits) != count:
            fail(f"the scripted {name} press(es)",
                 f"{len(hits)} frame(s) with pressed bit {bit:#x} and the raw bit "
                 f"clear, {count} expected")
        elif any(f["state"] != 1 or f["substate"] != MENU_SELECT for f in hits):
            fail(f"the scripted {name} press(es)",
                 "the game's pad code had them outside the menu's read substate")
        else:
            print(f"ok   the scripted {name} press(es) reached the game's own pad "
                  f"code (pressed bit {bit:#x} on frame(s) "
                  f"{', '.join(str(f['frame']) for f in hits)})")

    # 3. The menu's own highlight followed those presses. The menu task starts
    #    on option 0 (country_select.c's own TASK_INIT); frames before it
    #    exists (sel < 0) are not part of the walk.
    sequence = collapsed([f["sel"] for f in frames if f["sel"] >= 0])
    expected = [0] + KEYS_EXPECTED
    if sequence != expected:
        fail("the highlighted option",
             f"the menu walked {sequence}, {expected} expected")
    else:
        print(f"ok   the game's own menu moved its highlight "
              f"{' -> '.join(str(v) for v in sequence)} for '{KEYS}'")

    # 4. The capture the demo takes with glReadPixels.
    if not shot_bytes.startswith(PNG_SIGNATURE):
        fail("the screenshot", "the harness wrote no PNG capture")
    elif len(shot_bytes) <= 24 or int.from_bytes(shot_bytes[16:20], "big") != 320 \
            or int.from_bytes(shot_bytes[20:24], "big") != 240:
        fail("the screenshot", "the capture is not a 320x240 PNG")
    else:
        print(f"ok   the run captured its frame to a 320x240 PNG ({len(shot_bytes)} bytes)")

    if failures != 0:
        sys.exit(f"{failures} check(s) failed")
    print(f"the game's own pad code saw the scripted '{KEYS}' presses and its own "
          f"country-select menu moved the highlight to match")


if __name__ == "__main__":
    main()
