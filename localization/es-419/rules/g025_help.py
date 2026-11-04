"""G-025: Online Help Desk menu and pages (SSDMG200 part 1): menu labels and abbreviations.

Menu window: ~22 characters. Help pages: 3 lines, ~45 characters.
Page breaks ({pagina}) stay. Rewritten full sentences are carried by the overlay JSON.
"""
import re
RULE = 'G-025'
MENU = {
    'Digimon Onl.': 'Digimon Online',
    'Guía Ciu.': 'Guía de Ciudad',
    'Guía Campo.': 'Guía de Campo',
    'Cons. Bat.': 'Consejos de Batalla',
    'Bases Entr.': 'Entrenamiento',
    'Pant. Estado': 'Pantalla de Estado',
}
P = [
    (r'servidores:Amaterasu', 'servidores: Amaterasu'),
    (r'Los Digimon, ganan EXP', 'Los Digimon ganan EXP'),
    (r'\bopon\.,', 'oponentes,'),
    (r'\bPant\. de Estado\b', 'Pantalla de Estado'),
    (r'\bTiendas y Pos\.', 'Tiendas y Posadas'),
    (r'\bGuardro\.', 'Guardromon'),
]


def transform(t, name):
    if name != 'SSDMG200':
        return t
    if t in MENU:
        return MENU[t]
    for a, b in P:
        t = re.sub(a, b, t)
    return t
