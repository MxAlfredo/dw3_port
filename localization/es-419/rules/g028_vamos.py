"""G-028: "Vamos allá" reads as Spain-specific; es-419 uses "Andando" (owner decision, 2026-10-15)."""
import re
RULE = 'G-028'
P = [
    (r'¡Vamos allá!', '¡Andando!'),
    (r'\bBien, vamos allá\.', 'Bien, andando.'),
]


def transform(t, name):
    for a, b in P:
        t = re.sub(a, b, t)
    return t
