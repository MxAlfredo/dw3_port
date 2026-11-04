"""G-027: owner-approved global terms (session 02): Emblema, Escudo Pequeño, LETRERO,
la Líder Suzaku (she is female in the English text), Tarjeta Azul (item name)."""
import re
RULE = 'G-027'
ITEMS = {  # SSITMNAM, kept <= 15 characters (English max)
    'Hebilla': 'Escudo Pequeño',        # Buckler
    'Multivale': 'Multiemblema',        # Multi Crest
    'Buscar Vale': 'Emblema Buscar',    # Search Crest
    'Mostr. Vales': 'Emblema Contra',   # Counter Crest
    'Vale Unión': 'Emblema Unión',      # Binder Crest
    'Recup. Vale': 'Emblema Curar',     # Recover Crest
}
P = [
    (r'\buna Hebilla\b', 'un Escudo Pequeño'),
    (r'\bel Vale\b', 'el Emblema'),
    (r'\{hablante\}SEÑAL\{hablante\}', '{hablante}LETRERO{hablante}'),
    (r'\b([Ee])l Líder( de)? Suzaku', lambda m: ('L' if m.group(1) == 'E' else 'l') + 'a Líder' + (m.group(2) or '') + ' Suzaku'),
    (r'\bCarta Azul\b', 'Tarjeta Azul'),
]


def transform(t, name):
    if name == 'SSITMNAM' and t in ITEMS:
        return ITEMS[t]
    for a, b in P:
        t = re.sub(a, b, t)
    return t
