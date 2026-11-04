#!/usr/bin/env python3
"""Checks that the game's own code walks into the field and draws it (T19-T24).

Runs pc/build/boot with the field demo's scripted key sequence (boot's fifth
argument, see pc/shim/host_pad.c) -- the same one `make -C pc field-demo`
types:

    down,down,down,down  the country select moves its highlight to ESPANOL and
                         confirms; a non-English language makes the game
                         request MODE_OPENING_2 (0xE02), not MODE_OPENING
    start                confirm the language
    wait                 hold until the game's own mode changes (the movie)
    skipmovie            press Start until the game leaves the movie's mode
    waittitle            hold until the title menu waits for Start
    startuntil           press Start until that menu shows its two options
    waitoptions          hold until the menu navigates its options
    newgame              press Up until the highlight is on NEW GAME
    confirm              press cross until the game leaves the title's mode
    cross                advance the opening event once after starting a new game
    warp:<stage>         (debug-warp) not a key: on the first harness frame in
                         a boot mode, boot straight into the field stage whose
                         WSTAG number is <stage> (the map in
                         pc/shim/host_pad.c); an optional ,<progress> sets the
                         story progress first

and then verifies, from the harness's own output, that the game's own code is
what carried it from one mode to the next, all the way into the field, and that
the field's own code draws it to the end of the run:

  * the run boots into the country select (mode 0x1600);
  * confirming requests the title overlay's second opening movie (0xE02,
    STDWTITL, `mode >> 8` == 0xE -- the game's own low-byte dispatch,
    src/menus/title/scene.c);
  * skipping the movie makes the game's own movie task request the title
    screen (0xE00);
  * the title's own menu then requests a new game (MODE_NEW_GAME, 0x2D7),
    whose `mode >> 8` == 2 is FIELDSTG -- the field's entry point the port
    links (pc/boot.c's PC_MODE_ENTRY_POINTS);
  * the stage overlay the field's own loader asks for is the linked one
    (`overlay: the game loads stage overlay 0x1E9; this build has it linked,
    nothing is copied`, pc/shim/host_overlay.c -- nothing is copied, the stage
    is compiled into this binary);
  * the field creates its own layers (0x1000-0x1004) for the mode;
  * the field's own code draws on them and keeps drawing PAST the three fault
    frames the field has already been carried over. T19's run did not reach the
    first one: it stopped the harness on purpose at frame 463 (pcBootStopInMode),
    a graceful exit that printed its own summary and wrote its capture -- 463 is
    the STOP frame, not a crash, and it was long misread as the fault frame
    because a signal-killed process never flushes a block-buffered stdout (see
    below). The first fault T22 fixed is the field's own loader dereferencing
    FIELDSTG_state.slots, which is NULL for a stage without slots (WSTAG780 has
    none; the field's own field.c guards the same read before creating its
    triggers), and the host faults at presentFrames 468 where the PlayStation
    tolerates the read (no MMU: low RAM reads return something and the loop
    ends). T22 added the missing guard in the host's copy of that loader
    (pc/field/file_loader.c), so the field survives that point. T23 then fixed
    the layer's draw callback, which truncated its pointer (pc/shim/gfx_engine.c):
    the field used to stop drawing at frame 474. T24 fixed the field's own
    script-command path, which kept the command task's pointer in 32-bit slots
    (FIELDSTG_setEventPose's `s32 target`, EventChildren.scripts[],
    FIELDSTG_createScriptCommand's return and FIELDSTG_handleScriptCommand's
    `s32 task`): the field used to stop drawing at frame 561 and die by SIGSEGV
    at 562 in WSTAG780_handleCommand800, reached from
    FIELDSTG_handleScriptCommand through FIELDSTG_setEventPose. T24 widened those
    slots to pointers (a pointer is still 4 bytes on the PlayStation, so the
    matching build is unchanged: `make VERSION=eu compare` is 316 OK), so the
    field draws past 562; this check fails if it stops drawing at 468, 474 or 562
    again;
  * the order those callbacks ran in is the game's own (T26). The field's map
    objects and actors register on the map layer with their own y as the
    priority (map_objects.c:123, actor.c:69), and the game's own layer inserts
    them by priority, highest first, a new entry after the entries it ties with
    (src/engine/gfx/layer.c:280-320). The host shim prints one frame's
    registration order and the list order runCallbacks walks when DW3_CB_DUMP is
    set (pc/shim/gfx_engine.c), and this check runs the game's rule (simulated
    from that C) over the registrations and requires the list to match -- with
    the field's map layer among the dumped layers and its registration order
    different from the list order, so an append-only shim cannot pass. The same
    dump carries a synthetic probe of the insert function with ties and a
    registration at the sentinel's own priority (0x7FFFFFFF), cases the field's
    own frame does not reach;
  * each layer's own DRAWENV reaches its own ordering table (T27). On the
    PlayStation each layer's DRAWENV travels inside its own OT (layerDraw's
    DR_ENV, src/engine/gfx/layer.c:130-151); the host sends it through
    PsyCross's PutDrawEnv, which fills ONE global that each OT split records at
    parse time, so the env has to go in immediately before that layer's own
    table. With DW3_ENV_DUMP set the shim prints, for the armed frame, each
    layer's own env, the env its table was actually sent with, the DR_TPAGEs
    the table carries (how many carried dfe 0 before the host's pass -- the
    game's own words -- and how many carry dfe 1 after it, the bits as sent),
    and the env of the frame's clear (pc/shim/gfx_engine.c). This check
    requires: the env each layer's table was sent with IS that layer's own;
    the map layer's clip is the full screen while the area banner's is its own
    shrinking one (so the banner's clip cannot be the map's scissor); the
    clear uses the bottom layer's background env (FIELD_LAYER_BACK,
    field.c:315-316, the first layer with isbg); and the game's DR_TPAGEs
    carry dfe 0 (dfe0 == total) while the words as sent carry dfe 1
    (dfe1 == total), so the host's pass flipped every one: without it,
    measured, the map goes to PsyCross's offscreen VRAM surface and the window
    keeps only the clear;
  * the opening event moves the field actor through the game's map walk-step
    path without crashing on the host's wider pointers;
  * the final capture is a picture, not a blank: it has at least
    MIN_CAPTURE_COLORS distinct colours (measured 3213 at the captured frame;
    the uniform frame T27 started from had 1). The capture is read here (the
    PNG the harness writes is unfiltered RGB), so a frame that goes uniform
    again fails;
  * the run then reaches the harness's own frame budget (1000 frames, the demo's
    FIELD_TOTAL), not a signal: with T24's fix the field draws its whole budget,
    writes its glReadPixels capture and exits on its own (exit 0). The check
    pins the presented frame count (the budget), the last drawn frame (the
    budget's last frame) and a 320x240 PNG capture, and fails if the run ends on
    a signal instead.

What this check does NOT pin: the faulting function of a signal -- a frame
count and a signal number are what the harness's output proves. Nor the whole
picture: the capture's distinct-colour count is a coarse "the frame is not
blank" bound, not a comparison against the emulator's screen.

The run's output is taken line-buffered (`stdbuf -oL`): a process killed by a
signal never flushes a block-buffered stdout, so without it the last frames
would be lost and a measured end could not be read. That is how an old number
got in: the unguarded run's block-buffered tail ended around frame 463 -- which
also happened to be T19's deliberate stop frame -- so 463 was read as the crash.
This check reads the run under stdbuf -oL.

The negative control is the title demo's sequence (`down,down,down,down,start,
wait,start`): it never presses the title menu's new game, so the field is
never entered and the check fails on the missing mode 0x2D7. Pass it as the
optional second argument:

    .venv/bin/python pc/check_field.py <disc .bin>
    .venv/bin/python pc/check_field.py <disc .bin> down,down,down,down,start,wait,start

The run opens the PsyCross window and types keys at it, so it needs a display
(like check_font.py, check_country.py, check_input.py and check_title.py). Its
glReadPixels capture goes to a throwaway directory. Nothing of the disc is
stored (ADR-001).

    make -C pc                            # or: make -C pc field-demo
    .venv/bin/python pc/check_field.py <disc .bin>

Reports every failing case and exits non-zero if any of them failed.
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

# The same sequence the field-demo target types (pc/Makefile's FIELD_KEYS): walk
# the country select, the title overlay's movie, the title screen and its own
# menu, and confirm a new game.
KEYS = "down,down,down,down,start,wait,skipmovie,waittitle,startuntil,waitoptions,newgame,confirm,field,right,left,up,down"
FRAMES = 1000

MODE_COUNTRY_SELECT = "0x1600"
MODE_OPENING_2 = "0xE02"
MODE_TITLE = "0xE00"
MODE_NEW_GAME = "0x2D7"

# The exact-step milestone chain: one scripted
# run carries the game's own chain through both field stages (0x2D7, 0x2D8),
# the player-name screen it leaves to (0x500) and back out through the
# post-name field (0x2D9, stage 790) into the title overlay's movie family
# (0xE03). Passed as the word "milestone" in the optional keys argument; the
# exact press counts below are what the engine itself asked for (drain
# marker/page kinds and the keyboard's own states), measured on the test disc
# and pinned here so a change in the chain (or in any of those waits) shows
# as a count or mode change, not silently.
MILESTONE_KEYS = ("down,down,down,down,start,wait,skipmovie,waittitle,"
                  "startuntil,waitoptions,newgame,confirm,cross,field,"
                  "talkdrain,namedrive,field,talkdrain")
MILESTONE_FRAMES = 20000
MODE_FIELD_785 = "0x2D8"
MODE_PLAYER_NAME = "0x500"
MODE_FIELD_790 = "0x2D9"
MODE_TITLE_MOVIE = "0xE03"

# The line pc/shim/host_overlay.c prints for the field's stage overlay: the
# stage the field's own loader asked for (0x1E9, the EU new game's stage) is
# linked into this binary, so nothing is copied.
STAGE_OVERLAY = ("overlay: the game loads stage overlay 0x1E9; "
                 "this build has it linked, nothing is copied")

# The field's own layers, created for the field mode (FIELDSTG_state's layers;
# the order is the game's).
FIELD_LAYERS = ["0x1000", "0x1001", "0x1002", "0x1003", "0x1004"]

# T19's harness stopped on purpose at frame 463 (pcBootStopInMode): a graceful
# exit, not a crash. Without the guard the field's loader faults at
# presentFrames 468 (the NULL FIELDSTG_state.slots read); that is the crash T22
# fixed, and the drawing-past check below asserts against it.
T19_STOP_FRAME = 463
UNGUARDED_FAULT_FRAME = 468

# T22's measured blocker, fixed by T23: the field used to stop drawing at frame
# 473 and the next frame's layer callback (FIELDSTG_drawMapObject) died on a
# pointer truncated to 32 bits. The check asserts the field now draws PAST this
# frame, so it fails if T23's host fix (pc/shim/gfx_engine.c) regresses.
T22_FAULT_FRAME = 474

# T23's measured blocker, fixed by T24: the field used to stop drawing at frame
# 561 and the next frame died by SIGSEGV in its own script-command path
# (WSTAG780_handleCommand800, reached from FIELDSTG_handleScriptCommand through
# FIELDSTG_setEventPose) because the command task's pointer was kept in 32-bit
# slots on the game's side. T24 widened them to pointers (a pointer is still 4
# bytes on the PlayStation, so `make VERSION=eu compare` stays 316 OK); the check
# asserts the field draws PAST this frame and fails if that regresses.
T24_FAULT_FRAME = 562

# With all three fixed the field draws to the end of the run: the harness's own
# frame budget (FRAMES), no signal. The check pins the last drawn frame to the
# budget's last frame and the capture to a 320x240 PNG.
LAST_EXPECTED_FRAME = FRAMES - 1

LINE_MODE = re.compile(r"boot: mode (0x[0-9A-F]+)")
LINE_LAYER_CREATED = re.compile(r"gfx: layer id (0x[0-9A-F]+),")
LINE_LAYER = re.compile(r"frame (\d+): layer (0x[0-9A-F]+) prims=(\d+) "
                        r"\(sprite=(\d+) poly=(\d+) env=(\d+) other=(\d+)\) bytes=(\d+)")
LINE_PRESENTED = re.compile(r"presented (\d+) frames, (\d+) primitives "
                            r"drawn by the game's own mode code")
LINE_FIELD_ACTOR = re.compile(r"field (\d+): pos=(-?\d+),(-?\d+) held=([0-9A-F]{4})")

# The exact-step drains and the name drive print one summary each when the
# mode they track left the state the chain was driving (pc/shim/host_pad.c).
LINE_DRAIN_DONE = re.compile(
    r"pad: dialog drain done after (\d+) exact press\(es\) \(mode (0x[0-9A-F]+)\)")
LINE_NAME_DONE = re.compile(
    r"pad: name drive done after (\d+) exact press\(es\) \(mode (0x[0-9A-F]+)\)")

# T26: the callback-order dump. The host's layer must order its draw callbacks
# exactly as the game's own layer does (src/engine/gfx/layer.c:280-320):
# highest priority first, a new entry after the entries it ties with, and the
# sentinel head (priority 0x7FFFFFFF) taking part in the walk as a normal node.
# pc/shim/gfx_engine.c prints, when DW3_CB_DUMP is set, that frame's
# registration order (`reg`, the nodes' array) and the order runCallbacks
# walks (`list`) for every layer with a callback array, plus a synthetic probe
# of the same insert with ties and a 0x7FFFFFFF registration. The frame is one
# where the field's own map objects and actors have registered on the map
# layer (the field is entered around frame 455).
CB_DUMP_FRAME = 600
LINE_CB_HEAD = re.compile(r"cb (\d+) (\S+) head cap=(\d+) count=(\d+)")
LINE_CB_ENTRY = re.compile(r"cb (\d+) (\S+) (reg|list) (\d+) prio=(-?\d+) "
                           r"func=(\S+) param=(-?\d+)")

# The field's map layer, where its map objects (map_objects.c:123) and actors
# (actor.c:69) register their draw callbacks with their own y as the priority;
# the dump has to cover it and its registrations have to differ from the list
# order, or an append-only shim would pass.
FIELD_MAP_LAYER = "0x1002"

# The synthetic probe's tag in the dump (pc/shim/gfx_engine.c).
CB_DUMP_PROBE = "probe"

# T27: the per-layer DRAWENV dump (pc/shim/gfx_engine.c prints it when
# DW3_ENV_DUMP is set; this check sets it). Frame 610 is one where the area
# banner is already closing: its own clip is shrinking (measured: 0,8 320x224
# at 610, 0,88 320x64 at 620, 0,120 320x0 at 624) while the map layer keeps
# its full source clip but is hidden by the host draw clip. Frame 899 checks
# that the host restores the full draw clip after the banner.
ENV_DUMP_FRAME = 610
MAP_RESTORED_FRAME = 899
LINE_ENV_OWN = re.compile(r"env (\d+) (0x[0-9A-F]+) own clip=(-?\d+),(-?\d+) "
                          r"(\d+)x(\d+) ofs=(-?\d+),(-?\d+) bg=(\d+),(\d+),(\d+) "
                          r"isbg=(\d+) ot=(\d+)")
LINE_ENV_OT = re.compile(r"env (\d+) (0x[0-9A-F]+) ot clip=(-?\d+),(-?\d+) "
                         r"(\d+)x(\d+) ofs=(-?\d+),(-?\d+) bg=(\d+),(\d+),(\d+) "
                         r"isbg=(\d+)")
LINE_ENV_TPAGE = re.compile(r"env (\d+) (0x[0-9A-F]+) tpage total=(\d+) dfe0=(\d+) "
                            r"dfe1=(\d+)")
LINE_ENV_CLEAR = re.compile(r"env (\d+) (0x[0-9A-F]+) clear clip=(-?\d+),(-?\d+) "
                            r"(\d+)x(\d+) ofs=(-?\d+),(-?\d+) bg=(\d+),(\d+),(\d+) "
                            r"isbg=(\d+)")

# The field's bottom layer: the first one with a background (setBgColor(1,1,1)
# sets isbg, field.c:315-316), so the game's own order runs its clear first
# and the host applies its env for the frame's clear (hostLayerApplyClearEnv).
FIELD_BACK_LAYER = "0x1000"

# The area banner's layer: its own clip shrinks as the banner closes and is
# left at 320x0 after it ends (field.c / banner.c:204-224, the game's own
# behaviour), which must clip only the banner's own table.
FIELD_BANNER_LAYER = "0x1003"

# T27: the captured frame must be a picture, not a blank. Measured: 3213
# distinct colours at the captured frame (899) with the fix; the uniform
# `1,1,1` frame T27 started from had 1 (2 before the DR_TPAGE dfe half of the
# fix, which left the map on PsyCross's offscreen surface). The bound leaves a
# ~50x margin under the measured number.
MIN_CAPTURE_COLORS = 64

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


# T30: the multi-frame capture check (pc/shim/gfx_engine.c, pc/boot.c). One run
# of `boot` with a comma-separated list of zero-based frame indices must produce
# one PNG per requested frame, and each of those PNGs must be byte-identical to
# the capture that a separate single-shot run would write at that index. Zero-
# based: the harness's `frames` budget is the count of presented frames, so the
# last valid index is `FRAMES - 1` (the run's budget has to cover the highest
# requested index). Out-of-run, duplicate, negative and malformed entries must
# be rejected before the run starts so the harness never silently drops a
# capture. End-to-end still requires a usable disc: this check only validates
# the list locally before any boot invocation; the byte-equality pass runs the
# harness and exits with RED on failure.
PC_BOOT_MAX_CAPTURES = 8


def parse_capture_list(text):
    """A comma-separated list of zero-based frame indices, as the harness
    accepts it on its 7th positional argument. Each entry is a non-empty
    run of strict ASCII digits ('0'..'9'); entries are separated by single
    commas. Trailing, repeated or empty entries are rejected. The check
    raises ValueError with a short RED line; the caller surfaces it to the
    user."""
    if not text:
        raise ValueError("capture list is empty")
    raw = text.split(",")
    if len(raw) > PC_BOOT_MAX_CAPTURES:
        raise ValueError(
            f"capture list has {len(raw)} entries, more than the "
            f"{PC_BOOT_MAX_CAPTURES} bound (PC_BOOT_MAX_CAPTURES)")
    indices = []
    for i, item in enumerate(raw):
        if not item:
            position = "empty entry" if i == len(raw) - 1 else "repeated ','"
            raise ValueError(f"malformed capture list at {item!r} ({position})")
        # Strict ASCII digits only: str.isdigit() accepts Unicode digits and
        # superscript numerals the C harness rejects.
        for c in item:
            if c < '0' or c > '9':
                raise ValueError(f"malformed capture list at {item!r} "
                                 f"(non-ASCII digit {c!r})")
        indices.append(int(item))
    return indices


def run_boot(image_path, keys, frames, shot_path, workdir, env=None):
    """Run one boot invocation and return its CompletedProcess."""
    stdbuf = shutil.which("stdbuf")
    command = [str(TOOL), str(image_path.resolve()), str(frames),
               str(shot_path), "0", keys]
    if stdbuf is not None:
        command = [stdbuf, "-oL"] + command
    return subprocess.run(command, capture_output=True, text=True,
                          cwd=workdir, env=env)


def multi_capture_path(shot_path, index):
    """The per-index path the host writes when a multi-capture list is given:
    `<stem>_f<index>.png`, the same shape pc/shim/gfx_engine.c builds. The
    stem is `shot_path` with its last extension stripped, where the
    extension separator is the LAST '.' that follows the LAST '/' in the
    basename. Matches Python's pathlib.Path.with_suffix(""): dotfiles
    (`.hidden.png`) keep their leading dot, and directories with dots in
    their name (`dir.with.dots/shot.png`) keep the dot in the directory
    name."""
    p = Path(shot_path)
    # pathlib.Path.with_suffix("") already implements the dotfile and
    # directory-with-dots cases; it returns the path unchanged when there
    # is no suffix. We always append `_f<index>.png`, mirroring the host.
    stem = p.with_suffix("")
    return stem.parent / f"{stem.name}_f{index}.png"


def check_multi_capture(image_path, keys, indices, workdir):
    """T30 acceptance: one boot run with the multi-capture list produces every
    requested PNG and each PNG is byte-identical to the capture a separate
    single-shot run would write at that index. Raises `AssertionError` with a
    short RED/GREEN line."""
    failures = []

    # Out-of-run / duplicate / negative must be detected locally first, so a
    # broken list fails fast without invoking boot (and without depending on
    # boot's own validation order).
    seen = set()
    for idx in indices:
        if idx < 0:
            raise AssertionError(f"RED: negative index {idx} in {indices}")
        if idx >= FRAMES:
            raise AssertionError(
                f"RED: index {idx} >= frame budget {FRAMES} in {indices} "
                f"(the run never reaches it; last valid index is {FRAMES - 1})")
        if idx in seen:
            raise AssertionError(f"RED: duplicate index {idx} in {indices}")
        seen.add(idx)

    # 1. Run boot with the multi-capture list. The 7th positional argument is
    #    the comma-separated index list (pc/boot.c).
    shot = Path(workdir) / "shot.png"
    stdbuf = shutil.which("stdbuf")
    command = [str(TOOL), str(image_path.resolve()), str(FRAMES),
               str(shot), "0", keys, ",".join(str(i) for i in indices)]
    if stdbuf is not None:
        command = [stdbuf, "-oL"] + command
    proc = subprocess.run(command, capture_output=True, text=True, cwd=workdir)
    if proc.returncode != 0:
        raise AssertionError(
            f"RED: boot with the multi-capture list returned {proc.returncode}; "
            f"stderr={proc.stderr.strip()[:200]!r}")

    # 2. Every requested index produced a PNG. The harness writes one file per
    #    index under `<stem>_f<index>.png`; the stem is the user's screenshot
    #    path with its last extension removed.
    for idx in indices:
        out_path = multi_capture_path(shot, idx)
        if not out_path.is_file():
            failures.append(f"frame {idx}: harness did not write {out_path}")
            continue
        data = out_path.read_bytes()
        if not data.startswith(PNG_SIGNATURE):
            failures.append(f"frame {idx}: {out_path} is not a PNG")
            continue
        if len(data) <= 24 \
                or int.from_bytes(data[16:20], "big") != 320 \
                or int.from_bytes(data[20:24], "big") != 240 \
                or data[24] != 8 or data[25] != 2:
            failures.append(f"frame {idx}: {out_path} is not a 320x240 8-bit RGB PNG")
            continue

    # 3. Each PNG must be byte-identical to the capture a separate single-shot
    #    run would produce. The single-shot run sets `frames = idx + 1`, which
    #    is the harness's "presented frames" budget: the harness saves the
    #    frame it just drew, so `frames = N` saves displayed frame `N - 1`.
    for idx in indices:
        multi_path = multi_capture_path(shot, idx)
        if not multi_path.is_file():
            continue
        multi_bytes = multi_path.read_bytes()
        with tempfile.TemporaryDirectory() as single_workdir:
            single_shot = Path(single_workdir) / f"single_{idx}.png"
            single = run_boot(image_path, keys, idx + 1,
                              single_shot, single_workdir)
            if single.returncode != 0:
                failures.append(f"frame {idx}: single-shot run failed ({single.returncode}): "
                                f"{single.stderr.strip()[:200]!r}")
                continue
            single_bytes = single_shot.read_bytes() \
                if single_shot.is_file() else b""
            if single_bytes != multi_bytes:
                failures.append(
                    f"frame {idx}: multi-capture {multi_path} ({len(multi_bytes)} B) "
                    f"differs from single-shot {single_shot} ({len(single_bytes)} B)")
            else:
                print(f"ok   T30 multi-capture frame {idx}: "
                      f"{len(multi_bytes)} B matches single-shot run")

    if failures:
        raise AssertionError("RED: " + "; ".join(failures))
    print(f"ok   T30: {len(indices)} frame(s) captured in one run, each byte-"
          f"identical to its single-shot counterpart")


def game_callback_order(registrations):
    """The list order src/engine/gfx/layer.c:280-320 produces for a sequence
    of (priority, func, param) registrations.

    The sentinel head (priority 0x7FFFFFFF, no func) is a node of the list and
    takes part in the walk; the insert stops at the first node whose priority
    is strictly smaller than the new one, so a new entry ends up after the
    entries it ties with. (The C walks the linked list and inserts after the
    last node with priority >= the new one, which is the same place.)
    """
    order = [(0x7FFFFFFF, None, 0)]
    for entry in registrations:
        at = 0
        while at < len(order) and not (order[at][0] < entry[0]):
            at += 1
        order.insert(at, entry)
    return order


def parse_callback_dump(text):
    """The dump's per-tag heads, registrations and list entries, by index."""
    tags = {}
    for m in LINE_CB_HEAD.finditer(text):
        tags.setdefault(m.group(2), {})["head"] = (int(m.group(3)), int(m.group(4)))
    for m in LINE_CB_ENTRY.finditer(text):
        entry = tags.setdefault(m.group(2), {})
        entry.setdefault(m.group(3), {})[int(m.group(4))] = (
            int(m.group(5)),
            None if m.group(6) == "(nil)" else m.group(6),
            int(m.group(7)))
    return tags


def dumped_sequence(tag, kind):
    """One tag's `reg` or `list` entries in index order."""
    entries = tag.get(kind, {})
    return [entries[i] for i in sorted(entries)]


def parse_env_dump(text, frame=None):
    """The T27 dump's lines: own/ot/tpage entries per layer id (in the order
    the frame sent the layers) and the frame's clear entry."""
    layers = []
    seen = {}
    clear = None
    for m in LINE_ENV_OWN.finditer(text):
        if frame is not None and int(m.group(1)) != frame:
            continue
        if m.group(2) in seen:
            continue
        seen[m.group(2)] = True
        layers.append({
            "id": m.group(2),
            "own": tuple(int(m.group(i)) for i in range(3, 13)),
            "ot": None,
            "tpage": None,
        })
    by_id = {layer["id"]: layer for layer in layers}
    for m in LINE_ENV_OT.finditer(text):
        if frame is not None and int(m.group(1)) != frame:
            continue
        layer = by_id.get(m.group(2))
        if layer is not None and layer["ot"] is None:
            layer["ot"] = tuple(int(m.group(i)) for i in range(3, 13))
    for m in LINE_ENV_TPAGE.finditer(text):
        if frame is not None and int(m.group(1)) != frame:
            continue
        layer = by_id.get(m.group(2))
        if layer is not None and layer["tpage"] is None:
            layer["tpage"] = (int(m.group(3)), int(m.group(4)), int(m.group(5)))
    for m in LINE_ENV_CLEAR.finditer(text):
        if frame is None or int(m.group(1)) == frame:
            clear = {"id": m.group(2), "env": tuple(int(m.group(i)) for i in range(3, 13))}
            break
    return layers, clear


def png_colors(data):
    """The distinct colours of a 320x240 8-bit RGB PNG, as the harness writes
    it (pc/shim/host_shot.c: unfiltered scanlines in stored deflate blocks).
    Returns None when the data is not such a PNG."""
    if not data.startswith(PNG_SIGNATURE) or len(data) <= 24:
        return None
    if int.from_bytes(data[16:20], "big") != 320 \
            or int.from_bytes(data[20:24], "big") != 240 \
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
    if len(raw) != 240 * (1 + 320 * 3):
        return None
    colors = set()
    for y in range(240):
        base = y * (1 + 320 * 3)
        if raw[base] != 0:  # the harness writes filter 0
            return None
        for x in range(320):
            colors.add(raw[base + 1 + x * 3:base + 1 + x * 3 + 3])
    return len(colors)


def main():
    if len(sys.argv) not in (2, 3, 4):
        sys.exit(f"usage: {Path(sys.argv[0]).name} <disc .bin> [keys] "
                 f"[capture-list (T30, comma-separated zero-based frame indices)]")
    if not TOOL.exists():
        sys.exit(f"RED: {TOOL} does not exist yet; build it with `make -C pc`")

    image_path = Path(sys.argv[1])
    if not image_path.is_file():
        sys.exit(f"no such image: {image_path}")

    # The default is the field demo's own sequence; the optional argument is the
    # negative control (e.g. the title demo's, which never confirms a new game)
    # -- or the word "milestone", which runs the exact-step chain of
    # the field-transit scenario at its own budget and pins the exact press
    # counts the chain's drains and name drive reported.
    keys = sys.argv[2] if len(sys.argv) >= 3 else KEYS
    milestone = keys == "milestone"
    if milestone:
        keys = MILESTONE_KEYS
    frames = MILESTONE_FRAMES if milestone else FRAMES
    multi_capture_arg = sys.argv[3] if len(sys.argv) == 4 else None

    # T30: validate the multi-capture list locally first. Out-of-run,
    # duplicate and malformed entries are detected here, so a bad list fails
    # fast with RED before any boot invocation. The end-to-end check (a real
    # disc running the harness) is launched after the field acceptance
    # passes, as a different test boundary.
    if multi_capture_arg is not None:
        try:
            indices = parse_capture_list(multi_capture_arg)
        except ValueError as e:
            sys.exit(f"RED: bad multi-capture list {multi_capture_arg!r}: {e}")
        for idx in indices:
            if idx < 0 or idx >= frames:
                sys.exit(f"RED: index {idx} out of run (budget is {frames}, "
                         f"valid range is 0..{frames - 1})")
            if indices.count(idx) > 1:
                sys.exit(f"RED: duplicate index {idx} in {indices}")
        print(f"ok   T30 list validated: {len(indices)} frame(s), all in "
              f"[0, {frames - 1}]")

    # A signal-killed process never flushes a block-buffered stdout; run the
    # harness line-buffered so the frames up to the measured fault survive.
    # PsyCross writes its own <app>.log in the working directory: run in a
    # throwaway one so a check run leaves nothing behind.
    stdbuf = shutil.which("stdbuf")
    failures = 0

    def fail(what, detail):
        nonlocal failures
        print(f"FAIL {what}: {detail}")
        failures += 1

    with tempfile.TemporaryDirectory() as workdir:
        shot = Path(workdir) / "shot.png"
        command = [str(TOOL), str(image_path.resolve()), str(frames),
                   str(shot), "0", keys]
        if stdbuf is not None:
            command = [stdbuf, "-oL"] + command
        # T26: arm the shim's callback-order dump for the frame the check reads
        # (pc/shim/gfx_engine.c); without the variable the run prints nothing
        # extra.
        # The per-frame lines this check reads (`[gfx] frame N: layer`, `[field] field N: pos`,
        # `[gfx] gfx: layer id`, `[overlay] ...`) are trace lines of the port's log (feature
        # port-log T2): DW3_LOG=trace asks for them, and stderr joins stdout so the order holds.
        env = {**os.environ, "DW3_CB_DUMP": str(CB_DUMP_FRAME),
               "DW3_ENV_DUMP": str(ENV_DUMP_FRAME),
               "DW3_ENV_DUMP2": str(MAP_RESTORED_FRAME),
               "DW3_LOG": "trace"}
        proc = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True, cwd=workdir, env=env)
        shot_bytes = shot.read_bytes() if shot.is_file() else b""

    # The run ends on the measured fault (a signal), not on its own budget (exit
    # 0). Any other exit is a harness error.
    if proc.returncode == 0:
        ended_on_signal = None
    elif proc.returncode < 0:
        ended_on_signal = -proc.returncode
    else:
        sys.exit(f"boot exited {proc.returncode}: {proc.stdout[-400:].strip()}")

    stdout = proc.stdout
    modes = LINE_MODE.findall(stdout)

    # 1. The chain of modes the game's own code asked for, in order. The port's
    #    dispatcher prints each one; three of the four are the ones T18/T19
    #    added (0xE02, 0xE00 and 0x2D7), so reaching them proves the entry-point
    #    table now holds STDWTITL and FIELDSTG and that the game's own low-byte
    #    dispatch chose the movie, the title screen and the field.
    chain = [(MODE_COUNTRY_SELECT, "the country select"),
             (MODE_OPENING_2, "the title overlay's opening movie"),
             (MODE_TITLE, "the title screen"),
             (MODE_NEW_GAME, "the field")]
    previous = -1
    for mode, what in chain:
        if mode not in modes:
            fail(f"the mode for {what}",
                 f"the run never reached {what} (mode {mode}); modes seen: {modes}")
            continue
        position = modes.index(mode)
        if position <= previous:
            fail(f"the mode for {what}",
                 f"{what} ({mode}) came before the mode that leads to it")
            continue
        previous = position
        print(f"ok   the game's own code reached {what} (mode {mode})")

    # 1b. The milestone chain's deeper modes and exact press counts
    #    Only in milestone mode: the default
    #     demo never leaves the field. The drains' own summary lines say how
    #     many exact presses the engine itself asked for and at which mode
    #     their traffic ended; the mode order pins the whole run: the field's
    #     second stage (0x2D8), the player-name screen the chain leaves to
    #     (0x500), the SAME field mode again after the name is confirmed
    #     (screen.c's requestMode back into 0x2D8), the post-name stage
    #     (0x2D9) and the title overlay's movie family (0xE03).
    if milestone:
        def pos(mode):
            return modes.index(mode) if mode in modes else -1

        p_785 = [i for i, m in enumerate(modes) if m == MODE_FIELD_785]
        p_name = pos(MODE_PLAYER_NAME)
        p_790 = pos(MODE_FIELD_790)
        p_movie = pos(MODE_TITLE_MOVIE)
        if len(p_785) < 2:
            fail("the milestone's second visit to the field",
                 f"the chain must revisit mode {MODE_FIELD_785} after the "
                 f"name is confirmed; modes seen: {modes}")
        elif not p_785[0] < p_name < p_785[1]:
            fail("the milestone's name-screen slot",
                 f"mode {MODE_PLAYER_NAME} must sit between the two visits "
                 f"to {MODE_FIELD_785}; mode order: {modes}")
        elif not p_785[1] < p_790:
            fail("the milestone's post-name stage",
                 f"mode {MODE_FIELD_790} (stage 790) must come after the "
                 f"second visit to {MODE_FIELD_785}; mode order: {modes}")
        elif not p_790 < p_movie:
            fail("the milestone's movie family",
                 f"mode {MODE_TITLE_MOVIE} (the title overlay's movie family) "
                 f"must come after stage 790; mode order: {modes}")
        else:
            print(f"ok   the milestone chain's mode order holds "
                  f"({MODE_NEW_GAME} -> {MODE_FIELD_785} -> {MODE_PLAYER_NAME} -> "
                  f"{MODE_FIELD_785} again -> {MODE_FIELD_790} -> {MODE_TITLE_MOVIE})")

        drains = LINE_DRAIN_DONE.findall(stdout)
        drives = LINE_NAME_DONE.findall(stdout)
        if len(drains) != 2:
            fail("the field's exact drains",
                 f"the milestone chain runs two drains (the field pages and "
                 f"the post-name field); saw {drains}")
        else:
            first, second = drains
            if first != ("101", MODE_PLAYER_NAME):
                fail("the field pages' drain",
                     f"the first drain must report 101 exact presses ending in "
                     f"mode {MODE_PLAYER_NAME}; saw {first}")
            if second != ("128", MODE_TITLE_MOVIE):
                fail("the post-name drain",
                     f"the second drain must report 128 exact presses ending in "
                     f"mode {MODE_TITLE_MOVIE}; saw {second}")
        if drains == [("101", MODE_PLAYER_NAME), ("128", MODE_TITLE_MOVIE)]:
            print(f"ok   the drains' exact press counts hold (101 exact presses "
                  f"of marker and talk-box page pauses into {MODE_PLAYER_NAME}; "
                  f"128 after the name, out to {MODE_TITLE_MOVIE}; both grew "
                  f"from the collapsed counts 76/100 once the text typed for "
                  f"real, so every retail marker pause shows and is answered)")
        if len(drives) != 1 or drives[0] != ("17", MODE_FIELD_785):
            fail("the name drive",
                 f"the name screen must report 17 exact presses ending in mode "
                 f"{MODE_FIELD_785} (the game goes back to the field after the "
                 f"name is confirmed; the keyboard's buffer starts pre-filled "
                 f"with the game's own default name, so the drive types no "
                 f"letters); saw {drives}")
        else:
            print(f"ok   the name drive's exact press count holds (17 exact "
                  f"presses: welcome pages, Start + END on the pre-filled "
                  f"default name, the partners' set and Yes; back to "
                  f"{MODE_FIELD_785})")

    # 2. The field's stage overlay: the stage its own loader asked for is the
    #    linked one (pc/shim/host_overlay.c). This is the line that says which
    #    stage the game entered and that nothing was copied.
    if STAGE_OVERLAY not in stdout:
        fail("the field's stage overlay",
             "the run did not report the linked stage overlay the field's own "
             "loader asked for (pc/shim/host_overlay.c)")
    else:
        print("ok   the field's own loader asked for the linked stage overlay "
              "(0x1E9, nothing copied)")

    # The field's own output, from the moment the game's mode became the field:
    # its layers are created and its own code draws on them. Everything before
    # this line belongs to the country select and the title, which share the
    # layer ids (0x1000 is also the title screen's), so the split is by output
    # position and not by id.
    field_start = stdout.find(f"boot: mode {MODE_NEW_GAME}")
    field_stdout = stdout[field_start:] if field_start != -1 else ""

    # Moving the opening-event actor exercises the game's map walk-step path.
    field_actor = [
        (int(m.group(1)), int(m.group(2)), int(m.group(3)))
        for m in LINE_FIELD_ACTOR.finditer(field_stdout)
    ]
    if not any((current[1], current[2]) != (previous[1], previous[2])
               for previous, current in zip(field_actor, field_actor[1:])):
        fail("field actor walk", "the opening event did not move an actor through the map walk-step path")
    else:
        print("ok   the opening event moved an actor through the game's map walk-step path")

    # 3. The field created its own layers for the mode.
    created = LINE_LAYER_CREATED.findall(field_stdout)
    missing = [layer for layer in FIELD_LAYERS if layer not in created]
    if missing:
        fail("the field's own layers",
             f"the field mode created {created}, missing {missing}")
    else:
        print(f"ok   the field created its own layers for the mode "
              f"({', '.join(FIELD_LAYERS)})")

    # 4. The field's own code drew on them, and kept drawing PAST the frame the
    #    unguarded loader faulted on (468): the host guard in
    #    pc/field/file_loader.c lets the field survive the stage without slots,
    #    so this is the T22 evidence. (T19's own run stopped gracefully at 463,
    #    a stop that was not a crash.)
    field_frames = [{"frame": int(m.group(1)),
                     "layer": m.group(2),
                     "sprite": int(m.group(4)),
                     "poly": int(m.group(5)),
                     "env": int(m.group(6))}
                    for m in LINE_LAYER.finditer(field_stdout)]
    drawn = [f for f in field_frames if f["poly"] > 0 or f["sprite"] > 0]
    last_drawn_frame = drawn[-1]["frame"] if drawn else None
    if not drawn:
        fail("the field's own drawing",
             f"no frame after the field mode drew on the field's layers; "
             f"layers seen: {sorted({f['layer'] for f in field_frames})}")
    else:
        last = drawn[-1]
        print(f"ok   the field's own code drew on its layers "
              f"({len(drawn)} frame(s); last: layer {last['layer']} "
              f"poly={last['poly']} sprite={last['sprite']} env={last['env']})")
        if last_drawn_frame <= UNGUARDED_FAULT_FRAME:
            fail("the field surviving its stage without slots",
                 f"the field stopped drawing at frame {last_drawn_frame}; the "
                 f"unguarded loader faulted at presentFrames "
                 f"{UNGUARDED_FAULT_FRAME} (the NULL FIELDSTG_state.slots read) "
                 f"and T22's guard in pc/field/file_loader.c must let it draw "
                 f"past that (T19's own stop was at frame {T19_STOP_FRAME})")
        else:
            print(f"ok   the field kept drawing past the unguarded fault frame "
                  f"{UNGUARDED_FAULT_FRAME} (T19 stopped at {T19_STOP_FRAME}; "
                  f"last field frame {last_drawn_frame})")

        # 4b. And past T22's next measured blocker (frame 474), where the
        #     field's layer callback truncated its pointer: T23 made the host's
        #     layer carry the whole 64-bit pointer, so the field must now draw
        #     past that frame.
        if last_drawn_frame <= T22_FAULT_FRAME:
            fail("the field surviving the truncated-pointer callback",
                 f"the field stopped drawing at frame {last_drawn_frame}; T22 "
                 f"measured its layer callback faulting at frame "
                 f"{T22_FAULT_FRAME} (a pointer truncated to 32 bits) and T23's "
                 f"host fix in pc/shim/gfx_engine.c must let it draw past that")
        else:
            print(f"ok   the field kept drawing past T22's truncated-pointer "
                  f"fault frame {T22_FAULT_FRAME} (T23's host fix; last field "
                  f"frame {last_drawn_frame})")

        # 4c. And past T24's measured blocker (frame 562), where the field's own
        #     script-command path truncated the command task's pointer: T24
        #     widened those slots to pointers, so the field must now draw past
        #     that frame too.
        if last_drawn_frame <= T24_FAULT_FRAME:
            fail("the field surviving the truncated script-command pointer",
                 f"the field stopped drawing at frame {last_drawn_frame}; T23 "
                 f"measured its script-command path faulting at frame "
                 f"{T24_FAULT_FRAME} (a task pointer truncated to 32 bits in "
                 f"FIELDSTG_setEventPose's `s32 target`, EventChildren.scripts[] "
                 f"and FIELDSTG_handleScriptCommand's `s32 task`) and T24's fix "
                 f"in src/field/field_mode/ must let it draw past that")
        else:
            print(f"ok   the field kept drawing past T24's truncated-pointer "
                  f"fault frame {T24_FAULT_FRAME} (T24's fix; last field frame "
                  f"{last_drawn_frame})")

        # 4d. The order the callbacks ran in is the game's own insert by
        #     priority (T26). The shim's dump (armed above with DW3_CB_DUMP)
        #     gives the frame's registration order and the list order
        #     runCallbacks walks; the game's rule, simulated from
        #     src/engine/gfx/layer.c:280-320, has to produce exactly that list,
        #     the sentinel head included. The field's map layer must be among
        #     the dumped layers and its registrations must NOT already be in
        #     list order, or an append-only shim would pass vacuously.
        cb_lists = parse_callback_dump(field_stdout)
        if not cb_lists:
            fail("the layer callback order",
                 f"the run printed no callback-order dump for frame "
                 f"{CB_DUMP_FRAME} (pc/shim/gfx_engine.c prints it when "
                 f"DW3_CB_DUMP is set; this check sets it)")
        else:
            for tag in sorted(cb_lists):
                head = cb_lists[tag].get("head")
                reg = dumped_sequence(cb_lists[tag], "reg")
                run = dumped_sequence(cb_lists[tag], "list")
                if head is None or len(reg) != head[1] - 1 or len(run) != head[1]:
                    fail(f"the callback dump of {tag}",
                         f"frame {CB_DUMP_FRAME}'s dump of {tag} is incomplete "
                         f"(head {head}, {len(reg)} registrations, {len(run)} "
                         f"list entries)")
                    continue
                if run[0] != (0x7FFFFFFF, None, 0):
                    fail(f"the sentinel head of {tag}",
                         f"the list does not start with the game's sentinel "
                         f"(priority 0x7FFFFFFF, no func): {run[0]}")
                    continue
                expected = game_callback_order(reg)
                if run != expected:
                    fail(f"the callback order of {tag}",
                         f"the list the shim runs ({[e[0] for e in run]}) is not "
                         f"the order the game's own insert produces for the "
                         f"registrations ({[e[0] for e in reg]})")
                    continue
                if tag == FIELD_MAP_LAYER:
                    print(f"ok   the map layer's callbacks ran in the game's own "
                          f"priority order (registrations "
                          f"{[e[0] for e in reg]} run as "
                          f"{[e[0] for e in run[1:]]})")
                elif tag == CB_DUMP_PROBE:
                    ties = len(reg) - len({e[0] for e in reg})
                    print(f"ok   the insert rule's ties and sentinel priority "
                          f"(the {len(reg)}-entry probe with {ties} tied "
                          f"registrations and a 0x7FFFFFFF one runs as "
                          f"{[e[0] for e in run]})")
            map_layer = cb_lists.get(FIELD_MAP_LAYER)
            if map_layer is None or map_layer.get("head") is None:
                fail("the field's map layer in the callback dump",
                     f"frame {CB_DUMP_FRAME}'s dump has no {FIELD_MAP_LAYER} "
                     f"(layers: {sorted(cb_lists)}); the field's map objects and "
                     f"actors register on it (map_objects.c:123, actor.c:69)")
            else:
                reg = dumped_sequence(map_layer, "reg")
                run = dumped_sequence(map_layer, "list")
                if len(reg) < 2 or run[1:] == reg:
                    fail("the field's map layer callback order",
                         f"{FIELD_MAP_LAYER}'s {len(reg)} registrations "
                         f"{[e[0] for e in reg]} are already in list order, so "
                         f"the frame cannot tell the game's insert from an "
                         f"append")
                else:
                    print(f"ok   the map layer's registration order differs from "
                          f"the run order ({[e[0] for e in reg]} registered, "
                          f"{[e[0] for e in run[1:]]} run), so an append-only "
                          f"shim would fail this")
            probe = cb_lists.get(CB_DUMP_PROBE)
            if probe is None or probe.get("head") is None:
                fail("the insert probe",
                     f"frame {CB_DUMP_FRAME}'s dump has no {CB_DUMP_PROBE} "
                     f"(pc/shim/gfx_engine.c runs it on the armed frame)")
            else:
                reg = dumped_sequence(probe, "reg")
                if len(reg) - len({e[0] for e in reg}) == 0 \
                        or not any(e[0] == 0x7FFFFFFF for e in reg):
                    fail("the insert probe's cases",
                         f"the probe no longer carries a tie and a 0x7FFFFFFF "
                         f"registration: {[e[0] for e in reg]}")

    # 4e. Each layer's own DRAWENV reached its own ordering table (T27). The
    #     shim's dump (armed above with DW3_ENV_DUMP) gives, for the armed
    #     frame, each layer's own env, the env its table was actually sent
    #     with, its DR_TPAGEs and the frame's clear env. The two must match per
    #     layer; the map's clip must be the full screen while the closing
    #     banner's is its own smaller one (the banner's clip cannot be the
    #     map's scissor, the measured T27 cause); the clear must be the bottom
    #     layer's background env; and the game's DR_TPAGEs must carry dfe 0
    #     (dfe0 == total) while the words as sent must carry dfe 1
    #     (dfe1 == total), so the host's pass flipped every one (the host's
    #     one surface).
    env_layers, env_clear = parse_env_dump(field_stdout, ENV_DUMP_FRAME)
    if not env_layers:
        fail("the per-layer DRAWENV dump",
             f"the run printed no per-layer env dump for frame "
             f"{ENV_DUMP_FRAME} (pc/shim/gfx_engine.c prints it when "
             f"DW3_ENV_DUMP is set; this check sets it)")
    else:
        by_id = {layer["id"]: layer for layer in env_layers}
        mismatched = []
        missing_ot = []
        for layer in env_layers:
            if layer["ot"] is None:
                missing_ot.append(layer["id"])
            else:
                expected_ot = list(layer["own"])
                if layer["id"] == FIELD_MAP_LAYER and not milestone:
                    # The port keeps the map layer's source env full-screen,
                    # but clips its draw env while the entry banner freezes
                    # the field (pc/shim/gfx_engine.c). This frame-by-frame
                    # banner phase follows the demo's OWN key path; the
                    # milestone chain moves the same event along a different
                    # path, so there the map's clip (whatever it is) must
                    # just be the map layer's own env.
                    expected_ot[3] = 0
                if layer["ot"] != tuple(expected_ot):
                    mismatched.append(layer["id"])
        if missing_ot:
            fail("the per-layer DRAWENV dump",
                 f"frame {ENV_DUMP_FRAME}'s dump has no 'ot' env for "
                 f"{', '.join(missing_ot)}")
        elif mismatched:
            fail("each layer's own DRAWENV on its own table",
                 f"the table of {', '.join(mismatched)} was sent with an env "
                 f"that is not the layer's own: "
                 f"{[(l['id'], l['ot'], l['own']) for l in env_layers if l['id'] in mismatched]}")
        else:
            print(f"ok   every layer's table was sent with its own DRAWENV "
                  f"(frame {ENV_DUMP_FRAME}: " + ", ".join(
                      "%s clip=%d,%d %dx%d" % (l["id"], l["ot"][0], l["ot"][1],
                                               l["ot"][2], l["ot"][3])
                      for l in env_layers) + ")")

        # The map layer's own clip is the full screen, and the closing
        # banner's own clip is not: each reaches its own table, so the
        # banner's shrinking clip clips only the banner.
        map_layer = by_id.get(FIELD_MAP_LAYER)
        banner = by_id.get(FIELD_BANNER_LAYER)
        if milestone:
            # The banner's phase at a fixed frame follows the demo's own key
            # path (its event progressed differently by frame 610 there), so
            # the milestone chain does not pin the banner's mid-close state;
            # its drawenv rules are covered by the per-layer check above.
            print(f"ok   the banner phase checks belong to the demo's key path; "
                  f"the milestone chain pins the per-layer envs instead")
        elif map_layer is None or map_layer["own"][:4] != (0, 0, 320, 240):
            fail("the map layer's own clip",
                 f"frame {ENV_DUMP_FRAME}'s {FIELD_MAP_LAYER} own clip is "
                 f"{None if map_layer is None else map_layer['own'][:4]}, "
                 f"0,0 320x240 expected")
        elif map_layer["ot"] is None or map_layer["ot"][:4] != (0, 0, 320, 0):
            fail("the map stays hidden while the area banner closes",
                 f"frame {ENV_DUMP_FRAME}'s {FIELD_MAP_LAYER} draw clip is "
                 f"{None if map_layer['ot'] is None else map_layer['ot'][:4]}, "
                 "0,0 320x0 expected while the entry banner is active")
        elif banner is None or banner["own"][:4] == (0, 0, 320, 240):
            fail("the closing banner's own clip",
                 f"frame {ENV_DUMP_FRAME}'s {FIELD_BANNER_LAYER} own clip is "
                 f"{None if banner is None else banner['own'][:4]}; the closing "
                 f"banner's clip has to differ from the full screen (measured "
                 f"0,8 320x224 at 610, 0,120 320x0 at 624)")
        else:
            print(f"ok   the map layer keeps its own full-screen clip and the "
                  f"closing banner its own shrinking one (frame {ENV_DUMP_FRAME}: "
                  f"map clip={map_layer['own'][0]},{map_layer['own'][1]} "
                  f"{map_layer['own'][2]}x{map_layer['own'][3]}, banner "
                  f"clip={banner['own'][0]},{banner['own'][1]} "
                  f"{banner['own'][2]}x{banner['own'][3]})")

        # The frame's clear uses the env of the first layer with a background:
        # the field's bottom layer (FIELD_LAYER_BACK, field.c:315-316), whose
        # own env is full-screen (1,1,1).
        first_bg = next((layer for layer in env_layers if layer["own"][9] != 0), None)
        if env_clear is None:
            fail("the frame's clear env",
                 f"frame {ENV_DUMP_FRAME}'s dump has no clear line")
        elif first_bg is None or env_clear["env"] != first_bg["own"]:
            fail("the frame's clear env",
                 f"the clear used {env_clear['env']} from {env_clear['id']}, "
                 f"the first layer with a background is "
                 f"{None if first_bg is None else (first_bg['id'], first_bg['own'])}")
        elif env_clear["id"] != FIELD_BACK_LAYER:
            fail("the frame's clear env",
                 f"the clear used {env_clear['id']}'s env, not the bottom "
                 f"layer {FIELD_BACK_LAYER}'s")
        else:
            print(f"ok   the frame's clear uses the bottom layer's background env "
                  f"({env_clear['id']}, clip={env_clear['env'][0]},"
                  f"{env_clear['env'][1]} {env_clear['env'][2]}x"
                  f"{env_clear['env'][3]}, bg={env_clear['env'][6]},"
                  f"{env_clear['env'][7]},{env_clear['env'][8]})")

        # The game's own DR_TPAGEs carry dfe 0 (drawing to the display area
        # prohibited: the PS1 game draws to the back buffer and flips); the
        # host sets them to draw to the display, its one surface. The dump
        # gives both counts: dfe0 is read before the host's pass (the game's
        # own words) and dfe1 after it (the words as sent), so a missing flip
        # shows as dfe1 0 and fails. Without the flip the map's prims go to
        # PsyCross's offscreen surface and the captured frame keeps only the
        # clear (measured before the fix: 2 colours at the captured frame).
        map_tpage = map_layer["tpage"] if map_layer is not None else None
        if map_tpage is None or map_tpage[0] == 0 or map_tpage[1] != map_tpage[0]:
            fail("the game's DR_TPAGEs carry dfe 0",
                 f"frame {ENV_DUMP_FRAME}'s {FIELD_MAP_LAYER} tpage counts are "
                 f"{map_tpage} (total, dfe0, dfe1); the map's table carries "
                 f"DR_TPAGEs with dfe 0 and the host has to set every one")
        elif map_tpage[2] != map_tpage[0]:
            fail("the game's DR_TPAGEs drawn to the display",
                 f"frame {ENV_DUMP_FRAME}'s {FIELD_MAP_LAYER} table was sent "
                 f"with {map_tpage[2]} of its {map_tpage[0]} DR_TPAGE words "
                 f"carrying dfe 1 (drawing to the display); the host's pass has "
                 f"to flip every dfe-0 word ({map_tpage[1]} of them)")
        else:
            print(f"ok   the game's DR_TPAGEs carry dfe 0 and the host flipped "
                  f"every one to draw to the display (frame {ENV_DUMP_FRAME}: "
                  f"{FIELD_MAP_LAYER} total={map_tpage[0]} dfe0={map_tpage[1]} "
                  f"dfe1={map_tpage[2]})")

    # The banner's freeze is temporary: the port must restore the map layer's
    # full draw clip once the banner closes, not leave the map hidden.
    restored_layers, _ = parse_env_dump(field_stdout, MAP_RESTORED_FRAME)
    restored_map = next((layer for layer in restored_layers
                         if layer["id"] == FIELD_MAP_LAYER), None)
    if restored_map is None:
        fail("the map clip is restored after the banner",
             f"frame {MAP_RESTORED_FRAME}'s env dump has no {FIELD_MAP_LAYER}")
    elif restored_map["own"][:4] != (0, 0, 320, 240) \
            or restored_map["ot"] is None \
            or restored_map["ot"][:4] != (0, 0, 320, 240):
        fail("the map clip is restored after the banner",
             f"frame {MAP_RESTORED_FRAME}'s own/draw clips are "
             f"{restored_map['own'][:4]}/{None if restored_map['ot'] is None else restored_map['ot'][:4]}, "
             "0,0 320x240 expected")
    else:
        print(f"ok   the map layer's full draw clip is restored after the banner "
              f"(frame {MAP_RESTORED_FRAME})")

    # 5. The run now reaches the harness's own frame budget, not a signal: with
    #    T24's width fix the field's script-command path no longer truncates the
    #    command task's pointer, so the SIGSEGV T23 measured at frame 562 is gone
    #    and the run presents its whole budget and exits on its own (exit 0).
    #    The check fails if the run ends on a signal again, and pins the presented
    #    frame count and the last drawn frame.
    presented = LINE_PRESENTED.search(stdout)
    if ended_on_signal is not None:
        fail("the run's end",
             f"the run ended on signal {ended_on_signal}, not on its own frame "
             f"budget; T24's fix of the field's script-command pointer (frame "
             f"{T24_FAULT_FRAME}) must let the field draw to the budget")
    elif presented is None:
        fail("the run's end",
             "the run reached its budget but printed no 'presented' summary line")
    elif int(presented.group(1)) != frames:
        fail("the run's end",
             f"the run presented {presented.group(1)} frames, not the budget "
             f"{frames}")
    elif last_drawn_frame != frames - 1:
        fail("the run's end",
             f"the run reached its budget but the field's last drawn frame was "
             f"{last_drawn_frame}, not {frames - 1}")
    else:
        print(f"ok   the run reached its own frame budget: presented "
              f"{presented.group(1)} frames and exited cleanly, last drawn frame "
              f"{last_drawn_frame} (T24's fix; the field's script-command path "
              f"no longer truncates its task pointer)")

    # 6. The capture. The run reaches its own budget (section 5), so its frame
    #    end runs and writes the glReadPixels capture: a 320x240 PNG, and a
    #    picture (T27): its distinct-colour count must be at least
    #    MIN_CAPTURE_COLORS. Measured 3213 at the captured frame with the fix;
    #    the uniform frame T27 started from had 1. (The capture path is also
    #    exercised by check_title.py and check_input.py.)
    if ended_on_signal is None:
        if not shot_bytes.startswith(PNG_SIGNATURE):
            fail("the screenshot", "the harness wrote no PNG capture")
        elif len(shot_bytes) <= 24 or int.from_bytes(shot_bytes[16:20], "big") != 320 \
                or int.from_bytes(shot_bytes[20:24], "big") != 240:
            fail("the screenshot", "the capture is not a 320x240 PNG")
        else:
            print(f"ok   the run captured its frame to a 320x240 PNG "
                  f"({len(shot_bytes)} bytes)")
            colors = png_colors(shot_bytes)
            if colors is None:
                fail("the captured frame's content",
                     "the capture could not be read as an unfiltered 320x240 "
                     "RGB PNG (pc/shim/host_shot.c)")
            elif colors < MIN_CAPTURE_COLORS:
                fail("the captured frame's content",
                     f"the captured frame has {colors} distinct colour(s), "
                     f"fewer than {MIN_CAPTURE_COLORS}: the frame went blank "
                     f"again (T27's blank frame was uniform; the fix's frame "
                     f"measured 3213)")
            else:
                print(f"ok   the captured frame is a picture, not a blank "
                      f"({colors} distinct colours, >= {MIN_CAPTURE_COLORS}; "
                      f"T27's blank frame had 1)")
    elif shot_bytes:
        fail("the screenshot",
             "the run wrote a capture, but it ends on a signal (no budget line): "
             "the state this check guards changed")
    else:
        print("ok   the run ended on a signal before its budget, so the harness "
              "wrote no capture")

    if failures != 0:
        sys.exit(f"{failures} check(s) failed")
    if milestone:
        print("the game's own code walked its post-intro chain past the field demo: "
              "two field stages, the player-name screen (driven with 17 exact "
              "presses, keeping the game's own pre-filled default name), the "
              "post-name field stage and the "
              "title overlay's movie family, with the drains' exact press counts "
              "pinned (101 and 128, the real-typing counts) -- "
              "the field-transit scenario")
        sys.exit(0)
    print("the game's own code walked the country select, the title overlay's movie "
          "and title screen and the title's own menu into the field; the field's own "
          "code drew past the stage-without-slots point T19 stopped at, past the "
          "truncated-pointer callback T22 measured (T23's host fix) and past the "
          "truncated script-command pointer T23 measured (T24's fix), ordered its "
          "layer callbacks by priority as the game's own layer does (T26), sent each "
          "layer's own DRAWENV with its own table on one drawing surface (T27), and "
          "ran to the harness's own frame budget with a 320x240 capture that is a "
          "picture, not a blank")

    # T30: end-to-end multi-capture check on the same disc. Runs after the
    # field acceptance above; raises AssertionError on failure.
    run_optional_multi_capture(image_path, keys, multi_capture_arg)


def run_optional_multi_capture(image_path, keys, multi_capture_arg):
    """T30 entry point: when the user passes a 4th argument to the check,
    run the multi-capture acceptance (the harness's own end-to-end byte-
    equality check against single-shot runs). The end-to-end check
    deliberately runs AFTER the base field flow above succeeds: a single
    multi-capture run plus N single-shot runs is N+1 boot invocations, so
    running it without the field flow also succeeding would multiply the
    cost. The list itself was validated by main() before the field run,
    so a bad list fails fast with RED. End-to-end still requires a usable
    disc; the local list-validation passes even without one. Raises
    `AssertionError` on harness-level failures."""
    if multi_capture_arg is None:
        return
    try:
        indices = parse_capture_list(multi_capture_arg)
    except ValueError as e:
        sys.exit(f"RED: bad multi-capture list {multi_capture_arg!r}: {e}")
    with tempfile.TemporaryDirectory() as multi_workdir:
        check_multi_capture(image_path, keys, indices, multi_workdir)

if __name__ == "__main__":
    main()
