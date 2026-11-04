"""G-018: expand abbreviations in dialogue/tutorial pages; reflow within the page without adding lines."""
import re
RULE='G-018'
FILES=re.compile(r'^SS(DMG\d+|TALK\d+)$')
P=[(r'Pant\. de Estado','Pantalla de Estado'),(r'Cartas de bat\.','Cartas de batalla'),(r'Tienda de Art\.','Tienda de Artículos'),
(r'Jue\. de Cartas','Juego de Cartas'),(r'\bMaes\. de Jue\.','Maestro de Juego'),(r'\bMae\. de Jue\.','Maestro de Juego'),(r'\bMaest\. Jue\.','Maestro de Juego'),
(r'\bMaes\. de Jueg\.','Maestro de Juego'),(r'Centro de Adm\.','Centro de Administración'),(r'Centro de Administra\.','Centro de Administración'),
(r'Internac\.,','Internacional,'),(r'Tiendas y Pos\.','Tiendas y Posadas'),(r'hay pos\. fuera','hay posadas fuera'),(r'un seg\.','un segundo'),
(r'la inform\. de','la información de'),(r'téc\. finales','técnicas finales'),(r'técnicas físi\., mági\.','técnicas físicas, mágicas'),
(r'Las Carp\. son','Las Carpetas son'),(r'Car\. de Prog\.','Cartas de Programa'),(r'de incre\.','de incrementar'),(r'de increm\.','de incrementar'),(r'el niv\. o parám\.','el nivel o parámetros'),(r'el niv\. de hab\.','el nivel de habilidad'),
(r'su fza\.','su fuerza'),(r'a máq\. y vien\.','a máquina y viento'),(r'puntos déb\.','puntos débiles'),(r'la Cám\. Maestra','la Cámara Maestra'),
(r'Bosque de las Pat\.','Bosque de las Patadas'),(r'sería dif\.','sería difícil'),(r'transf\. en','transformó en'),(r'Fase Extrac\.,','Fase de Extracción,'),
(r'ambos jugad\. pasen','ambos jugadores pasen'),(r'regl\.bás\. de crt','reglas básicas de cartas'),(r'al presid\.','al presidente'),(r'Guardro\.','Guardromon'),
(r'usan art\.','usan artículos'),(r'estos eleme\.','estos elementos'),(r'dos acc\.','dos accesorios'),(r'equip\. doble','equipar doble'),
(r'Cardmon desap\.','Cardmon desaparece'),(r'el ind\. de golpe','el indicador de golpe'),
(r'la S\. de Cámara','la Sala de Cámara'),(r'comenz\. el proceso','comenzará el proceso'),(r'Paqu\. de Inicia[l]?\.','Paquetes de Inicio'),(r'Paqu\. Inicial','Paquete Inicial'),
(r'Luch\. de nuevo','Lucha de nuevo'),(r'Pant\. Confirmación','Pantalla de Confirmación'),(r'Car\.\s?conmigo','Cartas conmigo'),(r'paquete\nequil\.:','paquete\nequilibrado:')]
TOK=re.compile(r'(\{[0-9a-z]+\})')
def wrap(words, L, maxlines):
    lines=[]; cur=''
    for w in words:
        if not cur: cur=w
        elif len(cur)+1+len(w)<=L: cur+=' '+w
        else: lines.append(cur); cur=w
    if cur: lines.append(cur)
    return lines if len(lines)<=maxlines and all(len(l)<=L for l in lines) else None
def transform(t, name):
    if not FILES.match(name): return t
    m=re.match(r'\{hablante\}[^{]*\{hablante\}',t); head=m.group(0) if m else ''; body=t[len(head):]
    parts=TOK.split(body); out=[]; changed=False
    for part in parts:
        if TOK.fullmatch(part) or not part: out.append(part); continue
        new=part
        for a,b in P: new=re.sub(a,b,new)
        if new==part: out.append(part); continue
        olines=part.split('\n'); L=max(22,max(len(l.rstrip()) for l in olines))
        if all(len(l.rstrip())<=L for l in new.split('\n')) and len(new.split('\n'))==len(olines):
            out.append(new); changed=True; continue
        nlines=new.split('\n')
        i=next((k for k,(a,b) in enumerate(zip(olines,nlines)) if a!=b),0)
        fixed=nlines[:i]
        if i==0 and re.match(r'^".*"$',nlines[0].strip()):  # keep a quoted heading line on its own
            fixed=[nlines[0]]; i=1
        w=wrap(' '.join(nlines[i:]).split(), L, len(olines)-len(fixed))
        if w is None: out.append(part); continue   # does not fit: keep original page
        out.append('\n'.join(fixed+w)); changed=True
    return head+''.join(out) if changed else t
