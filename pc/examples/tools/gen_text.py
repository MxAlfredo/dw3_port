#!/usr/bin/env python3
"""Generate the text example (text-upper-items) from YOUR exported data.

Reads <user dir>/data/text/<language>/*itmnam.json (the item names, as the port exported them from
your disc) and writes copies into mods/text-upper-items/text/<language>/ with every ASCII letter
outside {control codes} in capitals. Accented letters and codes stay as they are. No game text is
stored in the repository (ADR-001).

    gen_text.py <user dir>
"""
import json
import os
import re
import sys

SUFFIX = "itmnam.json"  # the item names: ssitmnam (spa, es-419), esitmnam (eng)... one per language
CODE = re.compile(r"(\{[^}]*\})")


def upper(text):
    return "".join(p if p.startswith("{") else re.sub(r"[a-z]+", lambda m: m.group(0).upper(), p)
                   for p in CODE.split(text))


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        return 1
    user = sys.argv[1]
    root = os.path.join(user, "data", "text")
    out = os.path.join(user, "mods", "text-upper-items", "text")
    made = 0
    if not os.path.isdir(root):
        print(f"gen_text: text-upper-items skipped: {root} not found (start the game once with your disc)")
        return 3
    for lang in sorted(os.listdir(root)):
        folder = os.path.join(root, lang)
        if not os.path.isdir(folder):
            continue
        hits = [n for n in sorted(os.listdir(folder)) if n.lower().endswith(SUFFIX)]
        for name in hits:
            with open(os.path.join(folder, name), encoding="utf-8") as f:
                doc = json.load(f)
            for s in doc.get("strings", []):
                s["text"] = upper(s["text"])
            dst = os.path.join(out, lang, name)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "w", encoding="utf-8") as f:
                json.dump(doc, f, ensure_ascii=False, indent=1)
        made += bool(hits)
    if not made:
        print("gen_text: text-upper-items skipped: no *itmnam.json under data/text")
        return 3
    print(f"gen_text: text-upper-items: {made} language(s) -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
