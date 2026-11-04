"""G-024: account setup screens (SSNAMEET) and intro text: abbreviation expansions and typos.

Widths: max(EN, ES) line width is a safe bound for these boxes (see localization/README.md).
Full descriptive sentences are carried by the overlay JSON, not quoted here.
"""
import re
RULE = 'G-024'
SETUP = {  # short titles of SSNAMEET
    'Selección de Paqu. Inicial': 'Selección de Paquete Inicial',
    'Paqu. Equilibrado': 'Paquete Equilibrado',
    'Paqu. Potente': 'Paquete Potente',
    'Paqu. Maníaco': 'Paquete Maníaco',
    'Pant. Confirmación': 'Pantalla de Confirmación',
}
P = [
    (r'Paqu\. de Inicial\.', 'Paquetes Iniciales.'),
    (r'\bcomenz\. el\b', 'comenzaremos el'),
    (r'\bKatemon\b', 'Kotemon'),          # official typo: Kotemon everywhere else
    (r'\bhabit\.\?', 'habitación?'),
    (r'\bmerzco\b', 'merezco'),            # official typo
]


def transform(t, name):
    if name == 'SSNAMEET' and t in SETUP:
        return SETUP[t]
    for a, b in P:
        t = re.sub(a, b, t)
    return t
