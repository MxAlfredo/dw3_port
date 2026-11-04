"""G-020: Domador/Domadora/Domadores and their abbreviations -> Tamer/Tamers (owner decision)."""
import re
RULE='G-020'
FULL=[(r'\bDomadores\b','Tamers'),(r'\bDomadoras\b','Tamers'),(r'\bdomadores\b','tamers'),(r'\bdomadoras\b','tamers'),
      (r'\bDomadora\b','Tamer'),(r'\bDomador\b','Tamer'),(r'\bdomadora\b','tamer'),(r'\bdomadore\b','tamer'),(r'\bdomador\b','tamer')]
ABBR=re.compile(r'\b(Doamad|Domadr|Domad|Doma|Dom|domad|doma|dom)(\.?)(?=(\s|\n|\{|$|[!?¡¿,]))')
def _abbr(m):
    word='Tamer' if m.group(1)[0]=='D' else 'tamer'
    dot=m.group(2); s=m.string; i=m.end()
    if dot:
        nxt=s[i:i+2]
        drop = nxt[:1]==' ' or (nxt[:1]=='\n' and nxt[1:2].islower())
        return word + ('' if drop else '.')
    return word
def transform(t, name):
    for a,b in FULL: t=re.sub(a,b,t)
    t=ABBR.sub(_abbr,t)
    return re.sub(r'(buen\n|novio )Tamer\.!', r'\1tamer!', t)
