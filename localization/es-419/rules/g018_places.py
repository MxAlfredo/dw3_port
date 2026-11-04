"""G-018: expand abbreviated place names within each file's official max length."""
import re
RULE='G-018'
LIMIT={'SSAMTMAP':17,'SSASKMAP':17,'SSHTLNAM':19,'SSSTNAME':16,'SSSHPNAM':20}
MAP={
 'SSAMTMAP':{'Gimnasio Leg.':'Gimnasio Legend.','Cd. Xuan Wu':'Ciudad Xuan Wu','Ag. Kulon':'Agujero Kulon','Pos. Mtña.':'Posada Montaña','Pos. Bos.':'Posada Bosque','Pos. Pan.':'Posada Pantano',
   'Ruinas Ptclo.\nBosque Ptclo.':'Ruinas Protocolo\nBosque Protocolo','Cd. Quin Long':'Ciudad Qing Long','Cabo Cnex.':'Cabo Conexión','Cdd. Amaterasu':'Ciudad Amaterasu','Bque. Al.':'Bosque Alambre',
   'Etda. Bque. Alam.':'Entrada Alambre','Llanu. Vto.':'Llanura Viento','Bque. Ptdas.':'Bosque Patadas','Pya. Chas.':'Playa de Conchas','Cd. Bai Hu':'Ciudad Bai Hu','Des. Ruido':'Desierto Ruido',
   'Stuario. Jungla':'Santuario Jungla','Ptno. Bios':'Pantano Bios','Desi. Mobius':'Desierto Mobius','Cd. Zhu Que':'Ciudad Zhu Que','Ptn. Yermo\nPte. Yermo':'Pantano Yermo\nPuente Yermo','Ptno. Tqldad.':'Pantano Tranquilo'},
 'SSHTLNAM':{'Pos. del Bosq.':'Posada del Bosque','Pos. del Ptno.':'Posada del Pantano','Pos. de Suzaku':'Posada de Suzaku','Pos. del Desie.':'Posada del Desierto','Pos. Montaña':'Posada Montaña',
   'Pos. Armaterasu':'Posada Amaterasu','Pos. Qing Long':'Posada Qing Long','Qued. en la Pos.':'Quedarse en Posada','No en la Pos.':'No quedarse'},
 'SSSTNAME':{'Labor. Digimon':'Lab. Digimon','Salón de Regist.':'Sala de Registro','Esc. Front. Est.':'Recep. Estadio','Centro Admn. 1P':'Centro Admin. 1P','Centro Admn. 2P':'Centro Admin. 2P',
   'Centro Admn. B1P':'Centro Admin. S1','Cuar. Gen. A.o.A':'Cuartel A.o.A.','Alcantari. Asuka':'Cloaca Asuka','Ent.bosq. Alamb.':'Entrada Alambre','Bos. Alamb. Oes.':'Bosque Alambre O',
   'Bos. Alamb. Este':'Bosque Alambre E','Posada Bosq. PS':'Posada Bosque S1','Llanura del Vto.':'Llanura Viento','Bos. de Patadas':'Bosque Patadas','Grieta más Prof.':'Grieta Profunda',
   'Agj. Lab. Bichos':'Pozo Laberinto','Valle de las Ba.':'Valle de Balas','Sala de Cond. 01':'Sala Ductos 01','Sala de Cond. 02':'Sala Ductos 02','Sala de Cond. 03':'Sala Ductos 03',
   'Sala de Cond. 04':'Sala Ductos 04','Sala de Operac.':'Sala Operaciones','Escaleras Secre.':'Escalera Secreta','Desie. Ruido N.':'Desierto Ruido N','Puerta de Bat.':'Puerta Batalla',
   'Pos. Zhu Que':'Posada Zhu Que','Placa de Circui.':'Placa Circuitos','Alcant Amaterasu':'Cloaca Amaterasu'},
 'SSSHPNAM':{'Gua. Desierto Bai Hu':'Escondite Bai Hu','Gua. Desierto Byakko':'Escondite Byakko','Posada Pant. Asuka':'Posada Pantano Asuka','Posada Mont. Asuka':'Posada Montaña Asuka',
   'Posada Pant. AM':'Posada Pantano AM','Posada de Mont. AM':'Posada Montaña AM'},
}
MAP['SSASKMAP']=dict(MAP['SSAMTMAP']); MAP['SSASKMAP'].update({'Cd. Genbu':'Ciudad Genbu','Ruinas Ptclo.  \nBosque Ptclo.':'Ruinas Protocolo\nBosque Protocolo','Cdd. Asuka':'Ciudad Asuka','Cd. Byakko':'Ciudad Byakko','Cd. Suzaku':'Ciudad Suzaku'})
def transform(t, name):
    m=MAP.get(name)
    if not m or t not in m: return t
    new=m[t]
    assert all(len(l)<=LIMIT[name] for l in new.split('\n')), (name,new)
    return new
