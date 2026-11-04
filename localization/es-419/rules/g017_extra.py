"""G-017: miscellaneous Spain-specific words and official typos (word level only)."""
import re
RULE='G-017'
P=[(r'patada en el culo','patada en el trasero'),(r'\bguays\b','geniales'),
   (r'\btú Digimon\b','tu Digimon'),(r'\bArt\.(\s)Épicos','Artículos\\1Épicos')]
def transform(t,name):
    for a,b in P: t=re.sub(a,b,t)
    return t
