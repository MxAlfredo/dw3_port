"""G-029: "Gimnasio Legend(ario)" -> "Arena Legendaria" (the full word does not fit the map and room
fields); dialogue mentions follow with feminine agreement (owner decision, 2026-10-15)."""
import re
RULE = 'G-029'
NAMES = {  # map / room name fields (<= 16 characters)
    'SSAMTMAP': 'Arena Legendaria',
    'SSASKMAP': 'Arena Legendaria',
    'SSSTNAME': 'Arena Legendaria',
}
P = [  # dialogue: article and demonstrative agree with the feminine noun
    (r'\b(el|un|Este es el) Gimnasio(\s+)Legendario', lambda m: {'el': 'la', 'un': 'una', 'Este es el': 'Esta es la'}[m.group(1)] + ' Arena' + m.group(2) + 'Legendaria'),
    (r'\bEs el más difícil\b', 'Es la más difícil'),
]


def transform(t, name):
    if t == 'Gimnasio Legend.' and name in NAMES:
        return NAMES[name]
    for a, b in P:
        t = re.sub(a, b, t)
    return t
