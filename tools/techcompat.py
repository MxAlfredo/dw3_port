#!/usr/bin/env python3
"""Technique animation compatibility of the EU disc: which fighter can play which technique script.

Usage: python3 -I tools/techcompat.py "<disc>.bin" [repo-root] [--json] [--fighters]

A technique's animation is its TechData.script: a slot (0..17) of the effects archive of the fighter
that casts it (FighterInfo.effects in FILE_FIGHTERS, (file << 16) | entry; the entry is a table of 18
script offsets, 0 for an empty slot). A fighter that lacks the slot cannot play the technique. A partner
also needs slots 6 and 8 for script 5 and slot 8 for script 6 (the battle code can switch to them).
See docs/modding/README.md (techniques) and docs/digimon-anatomy.md.

Everything is read from your disc when the tool runs and printed to stdout; nothing is stored or embedded
(ADR-001), so do not commit the output. repo-root (default: the repository this file lives in) only
provides src/engine/file/file_table.c, the file table, through tools/file_names.py. The executable's
addresses of TECHS are those of the EU build (VERSION_EU).
"""
import argparse
import importlib.util
import json
import struct
import sys
from collections import Counter
from pathlib import Path

SLOTS = 18  # scripts in an effects archive entry
FILE_FIGHTERS = 0x1CC
EXE_LBA, EXE_SIZE = 24, 923648  # the main executable (SLUS-style PS-X EXE) on the EU disc
TECHS_ADDR, TECH_COUNT, TECH_SIZE = 0x80042BDC, 443, 18  # TECHS[443] in the EU executable
TECH_SCRIPT_AT = 16  # TechData.script offset (mp u16, power u16, then 14 bytes: icon .. hitCount; script is the 13th)
PARTNER_INFO, ENEMY_INFO = 0xC4, 0x48  # sizeof FighterInfo / FighterInfoEnemy
ENEMY_KIND = 0x3A  # FighterEntry.kind >= 0x3A is an enemy


def u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


def s16(b, o):
    return struct.unpack_from("<h", b, o)[0]


def parse_fighters(fighters, get_file):
    """{fighter id: {"enemy": bool, "kind": int, "file": int, "slots": [bool] * 18}} from the bytes of FILE_FIGHTERS.
    get_file(n) returns the bytes of file n. A fighter without an effects archive (placeholders) has no slots."""
    entries_at, partners_at, enemies_at = u32(fighters, 4), u32(fighters, 8), u32(fighters, 12)
    out = {}
    o = entries_at
    while o + 4 <= len(fighters) and s16(fighters, o) != 0:
        fid, index, kind = s16(fighters, o), fighters[o + 2], fighters[o + 3]
        o += 4
        enemy = kind >= ENEMY_KIND
        info = enemies_at + index * ENEMY_INFO if enemy else partners_at + index * PARTNER_INFO
        effects = u32(fighters, info + 8) if info + 12 <= len(fighters) else 0
        fnum, entry = effects >> 16, effects & 0xFFFF
        slots = [False] * SLOTS
        if fnum:
            data = get_file(fnum)
            if entry * 4 + 4 <= len(data):
                at = u32(data, entry * 4)
                if at + 4 <= len(data):
                    count = min(u32(data, at) // 4, SLOTS)  # the first offset is where the table ends
                    for i in range(count):
                        if at + 4 * i + 4 <= len(data):
                            slots[i] = u32(data, at + 4 * i) != 0
        out[fid] = {"enemy": enemy, "kind": kind, "file": fnum, "slots": slots}
    return out


def needed_slots(script, partner):
    """The slots a fighter needs to play a technique of this script (partner: the ones its battle code can switch to)."""
    need = [script]
    if partner and script == 5:
        need += [6, 8]
    elif partner and script == 6:
        need += [8]
    return need


def analyze(fighters, scripts):
    """Summary of the compatibility. scripts: TechData.script of every technique (technique n is scripts[n - 1])."""
    parts = {f: v for f, v in fighters.items() if not v["enemy"]}
    enems = {f: v for f, v in fighters.items() if v["enemy"]}
    real = {f: v for f, v in fighters.items() if any(v["slots"])}

    def playable(v, script):
        return all(v["slots"][s] for s in needed_slots(script, not v["enemy"]) if s < SLOTS)

    per_script = []
    for sc in sorted(set(scripts)):
        per_script.append({
            "script": sc,
            "techs": sum(1 for s in scripts if s == sc),
            "partners_ok": sum(playable(v, sc) for v in parts.values() if f_real(v)),
            "partners": sum(1 for v in parts.values() if f_real(v)),
            "enemies_ok": sum(playable(v, sc) for v in enems.values() if f_real(v)),
            "enemies": sum(1 for v in enems.values() if f_real(v)),
        })
    sigs = Counter(tuple(i for i, on in enumerate(v["slots"]) if on) for v in real.values())
    by_side = {}
    for name, group in (("partners", parts), ("enemies", enems)):
        c = Counter(tuple(i for i, on in enumerate(v["slots"]) if on) for v in group.values() if f_real(v))
        by_side[name] = [{"slots": list(k), "fighters": n} for k, n in sorted(c.items(), key=lambda kv: -kv[1])]
    return {
        "fighters": len(fighters),
        "partners": len(parts),
        "enemies": len(enems),
        "without_archive": sorted(f for f, v in fighters.items() if not f_real(v)),
        "distinct_slot_sets": len(sigs),
        "slot_sets": by_side,
        "scripts": per_script,
        "techs_by_fighter_ok": dict(sorted(Counter(
            sum(playable(v, s) for s in scripts) for v in real.values()).items())),
    }


def f_real(v):
    return any(v["slots"])


def fighter_rows(fighters, scripts):
    rows = {}
    for fid, v in sorted(fighters.items()):
        ok = [t + 1 for t, s in enumerate(scripts)
              if all(v["slots"][x] for x in needed_slots(s, not v["enemy"]) if x < SLOTS)]
        rows[fid] = {"kind": "enemy" if v["enemy"] else "partner", "slots": [i for i, on in enumerate(v["slots"]) if on],
                     "techs_ok": len(ok) if f_real(v) else 0}
    return rows


def load_disc(disc, repo):
    spec = importlib.util.spec_from_file_location("file_names", str(repo / "tools" / "file_names.py"))
    fn = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fn)
    sectors, counts = fn.parse_table(str(repo / "src" / "engine" / "file" / "file_table.c"))
    f = open(disc, "rb")

    def get_file(n):
        return fn.read_sector(f, sectors[n], counts[n])

    exe = fn.read_sector(f, EXE_LBA, (EXE_SIZE + 2047) // 2048)
    if exe[:8] != b"PS-X EXE":
        sys.exit("techcompat: the executable was not found where the EU disc has it; is this the European disc?")
    base = u32(exe, 0x18)  # load address of the text segment; the file's data starts after the 0x800 header
    at = TECHS_ADDR - base + 0x800
    techs = exe[at:at + TECH_COUNT * TECH_SIZE]
    scripts = [techs[i * TECH_SIZE + TECH_SCRIPT_AT] for i in range(TECH_COUNT)]
    return get_file(FILE_FIGHTERS), get_file, scripts


def fmt_slots(slots):
    return ",".join(str(s) for s in slots)


def print_report(summary, rows, show_fighters):
    print(f"fighters {summary['fighters']}: {summary['partners']} partner forms, {summary['enemies']} enemies "
          f"({len(summary['without_archive'])} without an effects archive: {fmt_slots(summary['without_archive'])})")
    print(f"distinct slot sets: {summary['distinct_slot_sets']}")
    for side in ("partners", "enemies"):
        print(f"\nslot sets of the {side} (slots present in the effects archive):")
        for s in summary["slot_sets"][side]:
            print(f"  {s['fighters']:4d} fighters  slots {fmt_slots(s['slots'])}")
    print("\nscript  techs  partners ok  enemies ok   (a partner also needs 6 and 8 for script 5, 8 for script 6)")
    for s in summary["scripts"]:
        print(f"{s['script']:6d} {s['techs']:6d} {s['partners_ok']:6d}/{s['partners']:<5d} {s['enemies_ok']:6d}/{s['enemies']:<5d}")
    print("\nfighters by number of techniques they can animate:",
          ", ".join(f"{k}: {v}" for k, v in summary["techs_by_fighter_ok"].items()))
    if show_fighters:
        print("\nid     kind     techs  slots")
        for fid, r in rows.items():
            print(f"{fid:<6d} {r['kind']:<8s} {r['techs_ok']:5d}  {fmt_slots(r['slots'])}")


def main(argv=None):
    ap = argparse.ArgumentParser(description="Which fighter can play which technique animation (reads your disc; stores nothing).")
    ap.add_argument("disc", help="path to the EU disc image (.bin, raw 2352-byte sectors)")
    ap.add_argument("repo", nargs="?", default=str(Path(__file__).resolve().parents[1]),
                    help="repository root, for src/engine/file/file_table.c (default: this repository)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of a table")
    ap.add_argument("--fighters", action="store_true", help="also list every fighter (table) or include them (JSON)")
    args = ap.parse_args(argv)
    fighters_file, get_file, scripts = load_disc(args.disc, Path(args.repo))
    fighters = parse_fighters(fighters_file, get_file)
    summary = analyze(fighters, scripts)
    rows = fighter_rows(fighters, scripts)
    if args.json:
        if args.fighters:
            summary["fighter_list"] = {str(k): v for k, v in rows.items()}
        json.dump(summary, sys.stdout, indent=1)
        print()
    else:
        print_report(summary, rows, args.fighters)


if __name__ == "__main__":
    main()
