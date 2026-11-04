"""G-021: Gafas -> Lentes (owner decision, more neutral), item names only.

Sentence-level agreement fixes (los Lentes, admirados, ...) live in the overlay JSON,
not here, so this file quotes no official sentences.
"""
import re
RULE = 'G-021'
P = [
    (r'\bGafas de Tirador\b', 'Lentes de Tirador'),
    (r'^Gafas de tiro legendarias\.$', 'Lentes de tiro legendarios.'),
    (r'^Gafas Tirador$', 'Lentes Tirador'),
    (r'^Gafas Protec\.$', 'Lentes Protec.'),
    (r'^Gafas$', 'Lentes'),
]


def transform(t, name):
    for a, b in P:
        t = re.sub(a, b, t)
    return t
