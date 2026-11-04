"""G-023: interjections that read as Spain-specific or unnatural in es-419 (owner review).
The "Jo jo jo" laugh is no longer kept: it became "Ja ja ja" / "Je je je" (G-031)."""
import re
RULE = 'G-023'
P = [
    # EN "Huh?": "¿Eh?" is the neutral form, already used ~200 times by the game
    (r'¿Uh\?', '¿Eh?'),
    # Nick's nervous "Uh, uhhhh,"
    (r'\bUh, uhhhh\.', 'Eh, ehhh.'),
]


def transform(t, name):
    for a, b in P:
        t = re.sub(a, b, t)
    return t
