"""G-030: one name for the admin building: "Centro de Administración" (owner decision, 2026-10-15).
Room names keep "Centro Admin." (16-character limit)."""
import re
RULE = 'G-030'
P = [
    (r'\bCentro(\s+)Administrativo', lambda m: 'Centro' + m.group(1) + 'de Administración'),
    (r'perd\. en Centro Adm\.', 'perdidos en el Centro.'),
    (r'Sólo se Admite\b', 'Sólo se admite'),
]


def transform(t, name):
    for a, b in P:
        t = re.sub(a, b, t)
    return t
