"""G-031: the "Jo jo jo" laugh -> "Ja ja ja" (hearty) or "Je je je" (sly, mocking, nervous),
chosen per scene by the owner's delegate (2026-10-15). Same length as the original.
The default here is "ja"; speakers listed in SLY laugh "je". Per-scene exceptions live in the overlay."""
import re
RULE = 'G-031'
SLY = ('KingEtemon', 'MetalEtemon', 'Haruka')
RUN = re.compile(r'\b[Jj]o(?:,? ?[Jj]o\b)+')  # two or more "jo" in a row (a lone "Jo," is a complaint)


def transform(t, name):
    m = re.match(r'\{hablante\}(.*?)\{hablante\}', t)
    vowel = 'e' if m and m.group(1) in SLY else 'a'
    return RUN.sub(lambda run: re.sub(r'\b([Jj])o\b', lambda x: x.group(1) + vowel, run.group(0)), t)
