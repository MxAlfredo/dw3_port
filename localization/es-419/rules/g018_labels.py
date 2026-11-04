"""G-018: normalize speaker labels (expand, unify variants, drop decorative dot); max 18 chars."""
import re
RULE='G-018'
LIMIT=18
D={'Guard. DO':'Guardia DO','Guard. DO.':'Guardia DO','Cazafant. Puck':'Cazafantasmas Puck','Mr. Alexei':'Sr. Alexei','Mr. Smith':'Sr. Smith','Mr.Smith':'Sr. Smith',
'Mr. Wedge':'Sr. Wedge','Mr. Suzuki':'Sr. Suzuki','Ms. Olivia.':'Srta. Olivia','Ms. Bridget':'Srta. Bridget','Srt. Misha':'Srta. Misha','Srta. Asamo':'Srta. Asami',
'Digimon Nav.':'Digimon Navi','Nav. Digimon':'Digimon Navi','Digimon Navi.':'Digimon Navi','Tina de la Resis.':'Tina Resistencia','Maes. Car. Ken':'Maestro Cartas Ken',
'Maes. Cart. Ken':'Maestro Cartas Ken','Cardster Ken.':'Maestro Cartas Ken','Elle Encantad.':'Elle Encantadora','Bruja Hie.':'Bruja de Hielo','Caball. Fuego':'Caballero Fuego',
'Caball. Negro':'Caballero Negro','Mayumi Perd.':'Mayumi Perdida','Profe. Sullivan':'Profesor Sullivan','Profes. Ayumi':'Profesor Ayumi','Supervi. Yumiko':'Supervisora Yumiko',
'Def. animales Jen':'Jen Animalista','Defen. animal Jen':'Jen Animalista','Df. animales Jen':'Jen Animalista','Erick C.basura':'Erick Chatarra',
'Digiev. Navi.':'Navi Digievolución','Digievolve Nav.':'Navi Digievolución','Digievolución Nav.':'Navi Digievolución','Digivolve Nav.':'Navi Digievolución',
'Nav. Digievoluci.':'Navi Digievolución','Digievol. Nav.':'Navi Digievolución','Ratón Sue.':'Ratón Suerte','Freak Ken.':'Coleccionista Ken','Eddy el Program.':'Eddy Programador',
'Max el Guard.':'Guardia Max','Guard. Max':'Guardia Max','Guard. Curt':'Guardia Curt','Guard. Russell':'Guardia Russell','Bbdor. Bacchus':'Bebedor Bacchus',
'Luke el coleccio.':'Luke Coleccionista','Luke colección.':'Luke Coleccionista','Luke Coleccionista.':'Luke Coleccionista',
'Maestro de jue.':'Maestro de Juego','Maestro jue.':'Maestro de Juego','Maest. Jue.':'Maestro de Juego','Maestro Jue.':'Maestro de Juego','Maest. de Juego':'Maestro de Juego','Maes. Jue.':'Maestro de Juego',
'Maes. de hielo':'Maestro de Hielo','M. de Hielo':'Maestro de Hielo','BK MegaGargo.':'BK MegaGargomon','BK Imperialdra.':'BK Imperialdramon','Jefe Qing Lo. ':'Jefe Qing Long',
'Ciudad. Byakko':'Ciudadano Byakko','Ciudano. Byakko':'Ciudadano Byakko','¿Ciudada. Byakko?':'¿Ciudadano Byakko?','¿Ciuda.Byak?':'¿Ciudadano Byakko?','Ciudada. Byakko':'Ciudadano Byakko',
'¿Ciu. Bya.?':'¿Ciudadano Byakko?','Ciudad. Byak.':'Ciudadano Byakko','Ciud. Byakko':'Ciudadano Byakko','¿Ciuda. Byakko?':'¿Ciudadano Byakko?','Ciudadano Byakko.':'Ciudadano Byakko',
'Saki amante de corr.':'Saki Fan Correo','Teruko el Compr.':'Teruko Compradora','Teruko el compr.':'Teruko Compradora','Técn. Savy Seiya':'Técnico Seiya',
'Carta Manía. Chako':'Chako Fan Cartas','Cta. Man. Chako':'Chako Fan Cartas','Coleccion. Leia':'Coleccionista Leia','Colección. Leia':'Coleccionista Leia',
'Jugueto. Toy Ami':'Ami Fan Juguetes','Abu. Butter':'Abuelo Butter','Recepción Estad.':'Recepción Estadio','Rec. Est.':'Recepción Estadio','Rec. Estadio':'Recepción Estadio',
'Recepción Estd. ':'Recepción Estadio','Gabum.':'Gabumon','Escri. Rikako':'Escritora Rikako','D. Kadomatsu':'Dr. Kadomatsu','Tamer. Bob':'Tamer Bob',
'Fan de Marat. Naoko':'Naoko Maratonista','Fan de Mart. Naoko':'Naoko Maratonista','Fan de Past. Bianca':'Bianca Golosa','Fan Past. Bianca':'Bianca Golosa',
'Tamer Hakura.':'Tamer Haruka','tamer Hakura.':'Tamer Haruka','Tamer Poermy.':'Tamer Poemy','Líder Sukazu.':'Líder Suzaku','¿Oficial Guard.?':'¿Oficial de Datos?',
'Ofici. Digimon':'Oficial Digimon','Jugador Takashi .':'Jugador Takashi','Civil Byakko 08.':'Civil Byakko 08','Srta. Ripley ':'Srta. Ripley','Junior. ':'Junior','Keith. ':'Keith',
'Mike el rapero.':'Mike el Rapero','Phillip el maníaco.':'Phillip el Maníaco','Phillip Maníaco.':'Phillip el Maníaco','Yuki el energético.':'Yuki Energético',
'Yuki el Energético.':'Yuki Energético','Shiro el novato.':'Shiro Novato','Jack Maníaco.':'Jack el Maníaco'}
HONOR=('Sr.','Sra.','Srta.','Dr.')
def fix(lab):
    new=D.get(lab)
    if new is None:
        new=lab.rstrip()
        if new.endswith('.') and not new.endswith(HONOR) and not re.search(r'\b\w{1,5}\.$',new[:-1]+'.') is None:
            new=new[:-1]
        elif new.endswith('.') and len(re.findall(r'\.',new))==1 and not new.endswith(HONOR):
            new=new[:-1]
    return new
def transform(t, name):
    m=re.match(r'\{hablante\}([^{]*?)\{hablante\}',t)
    if not m: return t
    lab=m.group(1); new=fix(lab)
    if new==lab or len(new)>max(LIMIT,len(lab.rstrip())): return t
    return '{hablante}'+new+'{hablante}'+t[m.end():]
