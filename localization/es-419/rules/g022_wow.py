"""G-022: Uau/Uauu/Guau... -> Wow (owner decision, like Tamer: closer to the English original)."""
import re
RULE = 'G-022'
# Every Uau/Guau in the game is a surprise interjection (EN: Wow, Whoa, Woah, Whoopee), never a bark.
_WOW = re.compile(r'\b(?:Guau|Uau)(u*)\b')


def transform(t, name):
    # elongated forms (Uauuu, Guauuu) keep their emphasis
    return _WOW.sub(lambda m: 'Wooow' if len(m.group(1)) >= 2 else 'Wow', t)
