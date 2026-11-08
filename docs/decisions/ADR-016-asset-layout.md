# ADR-016 — Contrato `asset-v1`: una estructura legible para exportar, importar y moddear assets

- **Estado:** ACCEPTED como contrato de diseño (dirección del dueño, 2026-11-05: assets limpios, normalizados, unificados y sin duplicados;
  colores reales; un asset por archivo o carpeta, agrupado por lo que representa; los mods reflejan la estructura exportada; agregar nunca reemplaza un
  original; sin conocimiento de PS1 para el modder; sin compatibilidad con formatos de mod anteriores). Tarea A2 de la reestructuración de assets.
  Solo diseño: no cambia código. La referencia de formatos está en `docs/formats.md` §7.
- Continúa ADR-005 (datos y mods), ADR-009 (nombres `dw3:<tipo>/<id>`), ADR-010 (carpeta del usuario) y ADR-013..015 (compañeros). Donde chocan con
  este documento, gana este (ver «Migración»).
- Principio legal: igual que ADR-001, el repo no trae datos del juego; todo lo descrito se genera en la máquina del usuario desde su disco.

## Contexto (evidencia, 2026-11-05)

- Siete exportadores escriben cada uno su árbol, su sello y su manifiesto: `images/<DIR>/<NAME>/NN_MM.png` + `atlas-v1` (`image_data.c:527`),
  `z_stage/<llave>/` (`stage_export.c:308`), `stages/<n>.json`, `models/<id>/` (`model_export.c:1903`), `audio/<BANCO>/`, `video/*.avi`, `text/<idioma>/`,
  `tables/`, `raw/` y `config/`. Cada uno con su sello: `stage-v1` (`stage_export.c:323`), `stages-v1` (`stage_data.c:726`), `models-v1`
  (`model_export.c:1914`), `audio-v1` (`audio_export.c:701`), `video-v1` (`video_export.c:384`), `tables-v1` (`table_mods.c:1112`), `raw-v1`
  (`raw_export.c:20`), `content-v1`, y el de imágenes, `disc=<hash>;images=<n>` (`image_data.c:1685`).
- Los nombres llevan ids o nombres de archivo del disco: `models/<id de luchador>/` (el `model.json` dice `"name": "fighter_<id>"`), `audio/BGM001`,
  `images/dglb/stdglbtm`, `z_stage/s200pack`. Un modder no sabe qué Digimon es `models/468/`.
- Un Digimon es una sola entidad con tres facetas (ADR-009 §4); los 248 modelos exportados son exactamente los 248 ids de `content/digimon.json`
  (55 de `kind: partner` y 193 de `kind: enemy`, un solo espacio de ids). Hay nombres en inglés repetidos entre filas (la forma de compañero y la enemiga de la misma especie son
  ids distintos; también hay enemigos repetidos).
- Los 8 compañeros originales se nombran por texto (`DIGNAM[p+1]`, ADR-015 §4); sus formas son filas de `DIGIMON_DATA` (la forma inicial es `PC_PARTNER_ROW(p)`,
  `partner_access.h:113`).
- El arte de un compañero está repartido en siete hojas de VRAM de 256x256 (`docs/formats.md` 4.12); P7 (`77f8788`) lo importa desde `partners.json`
  (`art`, `icon`, `sprite`: `partner_ext.c:53-55`, `:397-405`).
- Hoy un PNG de menú comparte paleta con su página entera; el color que ve el modder son índices (`image_data.c:573`).
- El orden "mods, del último al primero, y luego `data/`" está repetido en unos doce archivos (A1b); no hay un único resolvedor.

## Decisión

### 1. Layout: `data/<tema>/…` = `mods/<mod>/<tema>/…`

Un árbol, dos raíces con la **misma forma** (la carpeta del usuario sigue como en ADR-010). Un **asset** es una carpeta (o un archivo suelto si es un solo
medio: un `.avi`). Los temas:

| Tema | Un asset es | Contenido legible |
|---|---|---|
| `partners/` | un compañero (los 8 originales y los de mods) | `partner.json`, `portrait.png`, `icon.png`, `field/` o una referencia a `sprites/` |
| `digimon/` | una fila de Digimon (forma de compañero o enemigo; no hay carpeta aparte de `enemies/`) | `digimon.json`, `model/` (glTF) |
| `sprites/` | un personaje del campo | `sprite.json` + un PNG por cuadro |
| `stages/` | un escenario | `stage.json`, `tiles/`, `objects/`, mapas (fondo, suelo, disparadores, áreas) |
| `menus/` | una pantalla | `menu.json` + una pieza PNG por elemento |
| `cards/` | una carta del juego de cartas | `card.json`, `face.png` |
| `audio/` | un banco de sonido | `bank.json`, `vag_NNN.wav`, `seq_P_RR.mid/.wav` |
| `video/` | una película | `<nombre>.avi` |
| `text/<idioma>/` | un archivo de texto | `<archivo>.json` |
| `tables/` | una tabla de datos | `<tabla>.json` (parche por filas en mods) |
| `raw/` | un archivo del disco sin formato abierto (caja negra) | el archivo tal cual, en minúsculas y sin `AAA/` |
| `content/` | un tipo de registro sin medios | `items.json`, `techs.json`, `teams.json` |
| `config/` | un archivo de configuración | como hoy |

**Por qué `digimon/` y no `enemies/`:** el modelo, los efectos y las estadísticas de un Digimon son una sola fila con un solo espacio de ids (ADR-009 §4);
un modelo de enemigo y uno de compañero tienen la misma forma. Separarlos duplicaría el tema y obligaría a elegir carpeta para una forma que se usa de
las dos maneras. La faceta (`partner`/`enemy`) va dentro de `digimon.json`. **Por qué `partners/` aparte:** un compañero no es una forma: reúne
arte 2D por pantalla, ícono, personaje del campo, equipo y evolución; sus formas son Digimon.

**Regla de tema:** lo que **posee medios** (imágenes, modelos, audio) es una carpeta de asset; los registros puros (objetos, técnicas, equipos iniciales)
quedan en `content/<tipo>.json` porque son relaciones por nombre entre muchos registros pequeños y una carpeta por objeto no aporta nada legible. Los
registros de compañeros, Digimon y cartas pasan a vivir dentro de la carpeta de su asset (junto a sus medios); desaparecen `content/partners.json`,
`content/digimon.json` y `content/cards.json` como archivos únicos.

### 2. Nombres: estables, legibles, sin ids de disco en las rutas

**Originales.** La carpeta es un *slug* del nombre inglés del disco (compañero: `DIGNAM[p+1]`; Digimon: `DIGNAM`; carta: `ESCARDNM`):

1. minúsculas; todo lo que no sea `a-z 0-9` pasa a `-` (una racha, una sola vez); sin `-` al principio ni al final; hasta 48 caracteres (la misma llave
   válida que `extValidKey` y el README de mods §6.9b);
2. si varias filas dan el mismo slug, el orden es: forma de compañero antes que enemigo, y luego id de disco ascendente; la primera conserva el slug y las
   siguientes añaden `-2`, `-3`... Es determinista para el mismo disco EU. Nunca se usa un id como único distintivo.
3. lo que **no tiene nombre legible** usa el nombre de archivo del disco en minúsculas, que es un nombre y no un número: bancos de audio (`bgm001`),
   pantallas (`stdglb`: el archivo `*CS`/`*TM` sin sufijo), personajes de campo (`p001play`), escenarios (`s330`, igual que `S330PACK` y que `stages/330.json` de
   hoy), películas (`movieopn`). Las cartas originales (A7.4, decisión del dueño 2026-11-07) son `c<nnn>`, el número de la carta del juego (`dw3:card/<nnn>`,
   que `cardDrawerSetCard` calcula como archivo `(nnn-1)>>6`, imagen `(nnn-1)&63`): `ESCARDNM` no se usa porque el arte se identifica por ese número, y una llave
   de carta nueva con esa forma está reservada. Los Digimon originales (A8.3, decisión del dueño 2026-11-07) son `d<nnn>`, el id del luchador con tres dígitos (`dw3:digimon/<nnn>`; ids 3 a 470, el mismo id de `modelExportFolder` y de `FighterInfo`), igual que `s<nnn>` y `c<nnn>`: la carpeta de un Digimon nuevo con esa forma está reservada, y el slug de `DIGNAM` no se usa porque hay nombres repetidos entre filas. Los alias legibles de estos (qué es `p001play`) llegan cuando se prueben (preguntas abiertas), como un campo `title` en el JSON, sin
   renombrar la carpeta.

**Mods.** La carpeta es la `<llave>` (a-z 0-9 `-` `_`, hasta 48), como los escenarios nuevos (`stages/<nombre>`, desde A7.3). Su identidad es `<mod>:<llave>`.

**Identidad.** Los nombres de ADR-009 no cambian (`dw3:digimon/<id>`, `dw3:partner/<i>`, `dw3:card/<id>`, `<mod>:<llave>`): son lo que guardan las
partidas y las relaciones. El **id del disco vive solo en el JSON** del asset (`"disc": {"id": "dw3:digimon/3", ...}`) y en el `names` del manifiesto del tema
(carpeta -> identidad). Cómo se refiere un mod a un original: por la identidad de siempre, o por el alias legible `dw3:<tipo>/<carpeta>` (por ejemplo
`dw3:digimon/agumon`) que el cargador resuelve con `names` y convierte a la identidad canónica; un alias nunca se escribe en una partida.

### 3. Semántica de mods

- **Misma ruta relativa = reemplazo** de ese archivo del asset original; **una carpeta con nombre nuevo = un asset nuevo**, que se agrega y no reemplaza.
  El validador (`--validate-mod`) avisa si el nombre de una carpeta nueva coincide con un original (sería un reemplazo, no un asset nuevo).
- **Un archivo que falta cae al asset base**: un mod puede traer solo `partners/<original>/portrait.png`. Un asset nuevo que omite un archivo usa lo que
  su JSON declare como base (por ejemplo el `base` del compañero, ADR-015), no un original al azar.
- **Resolución (la de B4):** un solo `assetResolve(tema, ruta)` que, **al arrancar**, recorre los mods activos del último al primero y luego `data/`, y
  guarda el resultado en memoria. Los mods se leen una vez; ningún costo en el bucle del juego.
- La mezcla de un JSON de mod con el original (parche por filas de una tabla, parche de escenario) la define el formato (`tables-table-v1`,
  `stages-stage-v1`), no el layout: para el layout un archivo reemplaza a otro.
- **Agregar nunca reemplaza**: los ids de un asset nuevo salen de su nombre `<mod>:<llave>` (ADR-009 §2); dos mods con la misma llave no chocan porque el
  prefijo los separa.

### 4. Cabecera JSON, manifiesto por tema y hash

**Cabecera.** Todo JSON de asset empieza por:

```json
{ "format": "<tema>-<tipo>-v<N>", "name": "agumon", "disc": { "id": "dw3:digimon/3", "files": ["..."] } }
```

`format` (obligatorio, el único campo de versión; una versión desconocida se rechaza, no se adivina), `name` (la carpeta), `disc` (solo en exportados:
`id` y los archivos de origen para procedencia, nunca bytes ni textos del juego). Sustituye a `stamp`/`schema`/`format` como tres nombres para una cosa.
Formatos: `partners-partner-v1`, `digimon-digimon-v1`, `digimon-model-v1`, `sprites-sprite-v1`, `stages-stage-v1`, `menus-menu-v1`,
`cards-card-v1`, `audio-bank-v1`, `text-file-v1`, `tables-table-v1`, `content-items-v1`...

**Manifiesto.** Un `data/<tema>/manifest.json` por tema (`asset-manifest-v1`): `topic`, `exporter` (**objeto** unidad -> entero que sube cuando cambian los bytes de
salida, el papel de `IMG_CODEC_VERSION`, `image_data.h:26`: la unidad es el exportador, o cada cosa que exporta cuando se versiona por pieza, como un banco de audio, un
luchador o un tipo de `raw/`; un tema compartido por varios exportadores, `stages/` y `menus/`, lleva una entrada por exportador, así que un solo entero no alcanza),
`disc` (el hash del descriptor de volumen y la tabla de rutas, `pfsDiscHash(1)`, igual en todos los manifiestos), `names` (carpeta -> identidad; `{}` mientras el
exportador no tenga mapa) y `files` (ruta relativa -> hash); además `inputs` (opcional: otros hashes de los que derivan los archivos; solo `text/` lo usa, con `overlay` y
`disc-text`). Las versiones ya no viven dentro de `files`. Reemplaza a los sellos por exportador. Un manifiesto que no sea exactamente este formato (uno viejo con `stamp`, uno
dañado, un campo desconocido) cuenta como ausente (sin compatibilidad): el exportador exporta de nuevo y un archivo editado se conserva con su `.new`; uno de otro disco
conserva sus archivos pero pierde las versiones. Regla vigente que se conserva (de `placeFile`,
`stage_export.c:381-419`): si el archivo en disco coincide con el hash del manifiesto se reemplaza; si fue editado se deja y lo nuevo se escribe al lado como
`<archivo>.new`.

**Hash.** FNV-1a de 64 bits sobre los bytes exactos del archivo (`tdHash`, `text_data.c:91-102`), 16 dígitos hexadecimales en minúscula. Sirve para detectar
edición y para el informe de duplicados de B5: se agrupan archivos con el mismo hash **de medios** (PNG, glTF `.bin`, WAV, MID), nunca JSON (llevan
`name`). Para imágenes, B5 puede agregar un hash de píxeles y paleta decodificados, que no depende de la compresión del PNG. No es un hash criptográfico
ni protege contra un mod hostil: un duplicado se **confirma comparando bytes** antes de servirlo una sola vez, y el informe solo propone (no borra).

### 5. Regla de imagen

- Toda imagen exportada es un **PNG independiente, indexado, con su propia paleta de colores reales**: 8 bits por canal (cada componente de 5 bits `v`
  se expande a `(v<<3)|(v>>2)`, `imgColor`, `image_data.c:38-45`), como mucho **16 colores** para una pieza de 4 bits y **256** para una de 8 bits, y el
  **índice 0 transparente** (`tRNS`). Un PNG de un asset no comparte paleta con otro.
- El port **empaqueta en VRAM al cargar** (páginas y bandas; A4 para compañeros y A5 para menús): el modder nunca ve páginas, cuadrículas de 256x256, filas de
  paletas ni `atlas-v1`. El formato de página desaparece de lo exportado.
- Límites que **se quedan y por qué**: (a) 16 colores por pieza de 4 bits y 256 por pieza de 8 bits, porque la paleta de hardware (CLUT) que el GPU de PS1
  usa tiene ese tamaño por modo; (b) transparencia binaria (0x0000 transparente, resto opaco; un negro opaco necesita el bit
  STP, **no probado**, ver preguntas abiertas); (c) color de 5 bits por canal al volver al juego; (d) el tamaño de una ranura de la banda de VRAM del
  puerto (`SPR_BAND_*`, `sprite_data.h:100-103`: 5 páginas de 64 halfwords, 224 filas) acota el arte que un asset puede traer a la vez. El port avisa en el
  log, con la ruta y el tamaño, y rechaza el archivo; no recorta en silencio. No se baja de 16 colores sin que el modder lo decida.

### 6. Paquete de compañero (lo implementan A3 y A4)

```text
partners/<nombre>/
  partner.json        partners-partner-v1: base, firstForm, name/description por idioma, equip, equipSet, evolution (los campos de ADR-015 §3)
  portrait.png        tira de 32 de alto y 32 x N de ancho (N = 1..8 celdas), indexado, <= 16 colores, índice 0 transparente
  icon.png            44x28, indexado, <= 16 colores, índice 0 transparente (ícono de la memory card)
  field/              un personaje de campo sprite-v1, con los conjuntos 1, 5 y 6 y las direcciones 0..4 (docs/formats.md 4.12)
  training/<entrenamiento>/   la escena de entrenamiento del gimnasio de este compañero: <NNN>.png por sprite + training.json (A3)
  (sin model/)        la forma inicial es un Digimon: digimon/<forma>/model/
menus/gym/effects/<entrenamiento>/   los efectos que comparten los 8 (no son de un compañero; A3 los escribe aquí hasta que A5 traiga menus/)
```

- **Qué reemplaza a `art`/`icon`/`sprite` de `partners.json` (`77f8788`):** la **presencia de los archivos**. `portrait.png` en la carpeta es el `art`; `icon.png`
  es el `icon`; `field/` es el `sprite` propio (o `"field": "<referencia>"` en `partner.json` para apuntar a un asset de `sprites/` y no copiarlo, de modo
  que los 8 originales no dupliquen un personaje). Los tres campos se quitaron sin capa de compatibilidad (A4a, 2026-11-06). Sin archivo, cae al de `base`, como P7.
- El registro (`partner.json`) sustituye a `content/partners.json`: un compañero es una carpeta que se copia y renombra. `content/teams.json` no cambia
  (es un registro puro). **Hecho en A8.2 (2026-11-07):** el cargador recorre `partners/` de cada mod de contenido (`readFolders`, como las cartas de A8.1); un solo nombre de formato, `partners-partner-v1`, sirve al `partner.json` descriptivo del exportador (nunca se lee) y al registro de un mod (cabecera `format`/`name`; `title` por idioma porque `name` es la cabecera); la carpeta de un original (`pbOriginalKey`) solo reemplaza arte y su `partner.json` no se lee; `content/partners.json` ya no se lee; no existe `data/content/partners.json` de referencia.
- Reglas ya probadas por P7 que se conservan: la animación recorre las celdas de ida y vuelta; el sprite de seguidor pide solo los conjuntos 1, 5 y 6
  y las direcciones 0..4 con las derechas espejadas (`docs/formats.md` 4.12).
- **Desviación del encargo, a confirmar:** la especificación pedía `model/` dentro del compañero. Se evita porque la forma inicial ya es un asset de
  `digimon/` (con su modelo) y copiarla sería un duplicado; `partner.json` la nombra en `firstForm`. Un mod que quiera todo en una carpeta no puede (pregunta abierta 1).

**Lo que exporta A3 (2026-11-06, `pc/shim/partner_export.c`, `docs/formats.md` 4.12 y 7.6).**

- **Inventario del arte 2D por compañero** (decomp + disco EU): el retrato (29 cuadros de 32x32 en 7 pantallas, idénticos), el ícono de la memory card (cuadros 40..47 de 44x28), el sprite
  del seguidor (`FIELD/SPRT/P003..P010`) y la **escena de entrenamiento del gimnasio** (12 archivos `TRAINING/TRANIN*`, 9 conjuntos cada uno: 8 compañeros y 1 efecto compartido). El HUD de combate,
  la evolución, el mapa, las tiendas y el juego de cartas no tienen arte por compañero (búsqueda de accesos; conclusión por ausencia).
- **Retrato:** una sola tira por compañero con su paleta real, leída de una hoja (las 7 dan los mismos índices y la misma paleta; la paleta vive en la página 0 de la imagen de la pantalla, no
  en la página de los píxeles). Las "3 paletas distintas" de B5 son el exportador de páginas, que elige una sola paleta por página (la más usada) y la aplica a todo el PNG: cada hoja trae las 8 paletas, iguales entre
  hojas. No hay que elegir "qué paleta": no hay variantes por pantalla. El exportador verifica la igualdad en cada corrida.
- **Entrenamiento por compañero:** `training/<entrenamiento>/` con un PNG por sprite (las piezas del sprite compuestas; una paleta de 16 colores por pieza, o varias en 8 bits si el sprite las mezcla) y
  `training.json` (animaciones, origen del PNG). El nombre `<entrenamiento>` fue el del archivo del disco en minúsculas (`tranin00` ... `tranin16`) en A3 y es desde A4a el slug del nombre inglés del entrenamiento (ver «Nombres de los entrenamientos (A4a)»); el gimnasio
  tiene 24 entrenamientos que comparten estos 12 archivos de dos en dos: `partner.json` lista las filas). Cada carpeta es autocontenida: lo que un sprite toma del efecto compartido viene ya dibujado.
- **Efectos compartidos:** `menus/gym/effects/<entrenamiento>/` (un asset de la pantalla del gimnasio, no de un compañero; un conjunto de efecto existe en 10 de los 12 archivos). Es la única parte de A3 fuera
  de `partners/`; usa su propio `menus/manifest.json` y A5 lo absorbe.
- **Legibilidad sobre ahorro de bytes:** un sprite de campo ya existe en `images/field/sprt/<archivo>/` y también sale en `partners/<nombre>/field/` (paquete autocontenido, sin alias); A4 quitó el de los 8 compañeros y A7.5 el resto (`images/field/sprt/` ya no existe: `sprites/<nombre>/`).
- **Importación (A4a, 2026-11-06, `pc/shim/partner_bundle.c`).** Un compañero de mod (`<mod>:<llave>` de `partners.json`) lee `partners/<llave>/portrait.png`, `icon.png` y `field/` por las capas de B4
  (`assetResolve`, archivo por archivo; el último mod gana; un archivo que falta cae al del `base`). El sprite del campo es el sprite que el registro de sprites llama `partner:<llave>`. Un original editado
  (un archivo de `data/partners/<nombre>/` cuyo hash difiere del manifiesto, o un archivo de un mod) se re-empaqueta **una vez al arrancar** en lo que leen las 7 pantallas y la memory card: las celdas en la página
  de cada pantalla y la **misma paleta** en todas las filas de paleta que nombran sus cuadros; el retrato de un original conserva el número de celdas del original (3 o 4; otro número se rechaza con una línea de log);
  más de 16 colores se reducen a 16 uniendo los colores más cercanos (ponderados por píxeles; el índice 0 transparente se conserva) y se avisa en el log con la ruta y la cuenta. El sprite del campo de los 8
  sale de `partners/<nombre>/field/` y ya no de `images/field/sprt/p003pken..p010pata` (el exportador dejó de escribirlos). Sin mods, nada cambia (los archivos exportados que no se tocaron no cuentan).
- **Importación del entrenamiento (A4b, 2026-11-06, `pc/shim/partner_train.c`).** El gimnasio guarda ahora un conjunto por compañero sin el límite de nueve de la PS1 (`PC_TRAIN_SLOTS`: ranuras 0..7 los originales,
  8 el efecto compartido, `p + 1` el compañero de mod `p`; el build PS1 sigue idéntico). Un compañero de mod lee `partners/<llave>/training/<entrenamiento>/` (`training.json` + un PNG por sprite) y un original
  editado la de `data/` o la de un mod, por las capas de assets, **la carpeta entera de la primera capa que se arma**, y se arma **una vez al arrancar** en el formato de conjunto del disco (la importación es el
  exportador de A3 al revés: piezas por paleta, empacadas en 2 páginas de 256x256 con las filas de paleta propias de cada página; ver `docs/formats.md` 4.12). Se sube exactamente donde `STGTRAIN_loadSet` sube un conjunto del disco,
  así que no hay colisión con la VRAM del juego por construcción (medido: lo único que cambia respecto al conjunto del disco es x 704..735 y 0..15 y x 768..895 y 0..255, más los buffers de pantalla). Sin
  carpeta, el compañero de mod muestra el conjunto de su `base` (editado o del disco) y lo dice una vez en el log. Para que la importación sea posible el exportador anota por fotograma `paletteBase` (dónde empiezan
  las paletas del PNG) y `blend` (qué paletas dibuja el juego mezcladas): `PARTNER_EXPORT_VERSION` 2. Límite honrado: una carpeta que no cabe en 2 páginas se rechaza con una línea de log (no se recorta en silencio).
- **Nombres de los entrenamientos (A4a).** `training/<entrenamiento>/` y `menus/gym/effects/<entrenamiento>/` ya no llevan el nombre del archivo del disco (`tranin00`, contra §2): la carpeta es el slug del
  nombre inglés que el gimnasio muestra para ese entrenamiento (`STGTRAIN_trainings[i + 1].name` en `ESTRAINI`; `requestFile(training)` y el menú indexan las dos tablas con el mismo número), y el archivo del disco
  queda solo en `training.json`. Los entrenamientos de las filas 13..24 (los "Super") usan el mismo archivo que los de las filas 1..12: una carpeta por archivo, `partner.json` lista las dos filas.
- **Nombres:** la carpeta es el slug del texto inglés `DIGNAM[p + 1]` del disco del usuario; `partner.json` guarda la identidad (`dw3:partner/<p>`, `firstForm` = `dw3:digimon/<p>`, fila `PC_PARTNER_ROW(p)`, que
  para los 8 originales es la identidad). Si el texto no se pudiera leer, la carpeta sería `partner-<p>`.

### 6b. Menús por pieza (A5.2 exporta, A5.3 importa)

`menus/<pantalla>/<pieza>.png` + `menu.json` (`menus-menu-v1`) y **un** `menus/manifest.json` (el de la carpeta `gym/effects/` de A3 vive en él: un tema, un manifiesto). Elegido en A5.2:

- **Pantalla**: el archivo del disco sin sufijo en minúsculas (`stmcrd`, `stdglb`, `cmfont`; §2.3 punto 3; no hay nombre inglés probado). Sigue la geometría original (decisión del dueño, 2026-11-07): una pieza se reemplaza por otra del mismo tamaño,
  en su rectángulo y su fila de paleta del disco. El layout libre es A5.4.
- **Pieza**: un rectángulo de una parte de la hoja con **una** paleta (la misma parte con otra fila de CLUT es otra pieza; la misma parte con la misma paleta, una). Nombre `f<cuadro>p<ranura>.png`, el primer cuadro y ranura que la usan; sin ids del disco en rutas.
- **Paleta real**: la posición de CLUT de la parte más la fila del cuadro, leída donde cae en el espacio de páginas de la pantalla (A5.1), no en la página de los píxeles. PNG de 16 colores (256 en 8 bits) en el orden de índices del disco, para que A5.3 pueda escribirlo de vuelta.
- **Nada se pierde en silencio**: una pieza que no se puede cortar sigue en `menu.json` con `file: null` y el motivo. Las únicas hoy son las 56 de `stwmp0`/`stwmp1` (paleta en la CLUT de una TIM de 8 bits relativa a la CLUT alterna).

Importación (A5.3, `pc/shim/menu_pack.c`): el empaquetador es un gancho de la capa de imágenes (`ImgMenuHooks`, hermano del de compañeros) que reescribe cada página en memoria al armar el archivo de la pantalla, y no toca `src/` ni `include/`.

- **Geometría original, sin posiciones nuevas.** Cada pieza vuelve a su rectángulo y su paleta a su posición de CLUT, tomadas de la hoja del disco y no del `menu.json` del mod (que es informativo): un `menu.json` viejo o editado no puede mover una pieza. Mover o redimensionar es A5.4.
- **Qué cuenta:** por pieza, la primera capa con un archivo válido (B4: mods del último al primero, luego `data/` solo si su hash difiere del manifiesto). Tamaño distinto: se rechaza con una línea de log y se prueba la capa siguiente. Y solo lo que difiere del disco se escribe, píxeles y paleta por separado,
  así una copia de la exportación es un no-op y piezas hermanas sin editar no pelean con la editada.
- **Colores:** el índice 0 y el `0x0000` del disco son transparentes; cada halfword de paleta conserva el STP del disco (negro opaco = `0x8000`). El STP píxel por píxel sigue fuera de alcance (prueba de hardware).
- **Conflictos:** rectángulo o posición de CLUT compartidos con datos distintos: gana la capa más tardía, con una línea de log.
- **Una sola lectura:** al armar por primera vez los archivos de la pantalla (como `images/`), guardada en memoria; sin recarga en caliente.

Quitado en A5.5 (sin compatibilidad): la parte de pantallas de `images/`. Las páginas `*TM` de las pantallas con piezas (`imgIsMenuScreen`: los `*TM` de `SCREEN`, `COUNTRY`, `NAMEENT`, `MCARD`, `DGLB` y `TRAINING` salvo `STWMP0TM` y `STWMP1TM`) ya no se exportan a `images/`
(`IMG_CODEC_VERSION` 9) ni leen un PNG de `images/` de un mod o de `data/`; el cuadro por página de la capa de imágenes se queda solo como portador del TIM que parchean los ganchos. Evidencia de la excepción: `stwmp0`/`stwmp1` tienen sus 56 piezas con `file: null` y 0 de
163 840 halfwords de página cubiertos por una pieza, así que sin `images/` no tendrían ninguna vía de edición; siguen en `images/` hasta que su paleta (CLUT de una TIM de 8 bits, `setAltClut`) se pruebe en el código de quien las dibuja; su paleta no se inventa. `TLOGO*`, `TTCOMMPK`, `END__ALL`,
`CNTSELWN`, `CARDDATA`, `FIELDCOM`, las cartas, los sprites y las máscaras de escenario siguen en `images/` (sus propias fases). `--validate-mod` arma el plan de cada pantalla y avisa de piezas rechazadas (ruta, tamaño recibido y tamaño de la pieza), de archivos que ninguna pieza lleva
por nombre y de una pieza que traen dos mods. La recarga en caliente opcional (`DW3_MODS_HOTRELOAD`) de `images/` no mira `menus/`, y ya no hay `images/` de pantallas que recargar.

### 6c. Objetos de escenario por pieza (A6.2 exporta, A6.3 importa)

`stages/s<nnn>/objects/<pieza>.png` + `objects.json` (`stages-objects-v1`) con el mismo exportador de piezas, modelo y nombres que los menús (§6b); A6 solo crea `objects/` (`stage.json` y `tiles/` los trae A7.2) y la misma forma vale en `mods/<mod>/stages/s<nnn>/objects/`. Elegido en A6.2:

- **La hoja es una entrada del `TMPK` que fija el escenario**, no la 0: el exportador lee `sheetEntry` (`archivo << 16 | entrada`, `FieldState`, lo fija el `setupStage` de cada escenario) de `data/stages/s<nnn>/stage.json`, donde el puerto ya lo guarda. Medido: entrada 0 en 266 de 294 escenarios, 1 en 10, 3 en 4 (`S920` a `S923`) y 4 en 14 (`S961` a `S974`); en los 294 el archivo es el `S<nnn>TMPK.BIN` del mismo escenario. Las páginas son el `S<nnn>MASK.BIN` (`imgStagePagesOpen`).
- **Dedup solo dentro de un escenario** (una PNG por rectángulo y fila de paleta): dos escenarios con el mismo arte tienen un archivo cada uno (regla del dueño, B5 solo informa).
- **Paleta real por pieza**: 4 bits en las páginas (posición de CLUT de la parte + fila del cuadro), 8 bits en la CLUT de la TIM del `MASK` (`clut.row >= 256`, fila `row - 256`; A6.3 la escribe de vuelta en la CLUT de esa TIM, no en una página).
- **Nada se pierde en silencio**: un escenario que no se puede cortar escribe igual `objects.json` con `unexported` y el motivo; hoy son 11 (hoja de cabecera de dos palabras, sin cuadros). Las filas de CLUT que un objeto cicla en ejecución (`StageTile.cycle == 2`) viven en los datos C del overlay del escenario, no en el disco: solo salen las filas que usan los cuadros de la hoja.
- **Importación (A6.3)**: el empaquetador de menús (`menu_pack.c`) se generaliza a cualquier par (páginas, hoja) con `PackSource`; los escenarios son el segundo usuario (`stage_obj_pack.c`, gancho propio `imgSetStageHooks`). Geometría original: la hoja es la entrada que nombra `sheetEntry` de `data/stages/s<nnn>/stage.json`, nunca el `objects.json` del mod. La paleta de 8 bits va a la CLUT de la TIM que el juego conserva en cada color (la última cuya CLUT lo cubre), con el STP del disco. Costo: un índice al arrancar de los escenarios con piezas editadas; los demás no pagan nada al cargar (medido). Para los de `data/`, una firma de tamaño y hora (`stages/objects.stamp`, solo cuando la carpeta se encontró limpia) evita calcular 8632 hashes en cada arranque.
- **Sin compatibilidad (A6.4, hecho)**: `images/field/stage/s<nnn>mask` ya no se exporta ni se lee (`IMG_CODEC_VERSION` 11; la entrada por página de cada `MASK` queda solo como portadora de lo que escribe el gancho, como en las pantallas de A5.5). Los fondos `s<nnn>bg<nn>` (`S415BG01/02`, `S815BG01/02`: no son objetos y no tienen hoja) son la excepción y siguen en `images/`. Los 11 escenarios sin piezas no pierden ninguna edición: su tabla `stageObjects` solo trae el terminador y su hoja no tiene cuadros, así que no dibujan ningún objeto de mapa. `--validate-mod` cubre `stages/`: el plan de los escenarios con piezas editadas (tamaño, archivo sin pieza, pieza de dos mods) y `stageObjPackValidate` (carpeta que no es `s<nnn>`, escenario que el disco no tiene, algo distinto de `objects/`).
- **Costo del manifiesto (A6.4)**: `tdManifestParse` era cuadrático (`tdManifestSet` buscaba en toda la lista: 108 ms para los 9222 archivos de `stages/manifest.json`, en cada arranque). Ahora el manifiesto se indexa por ruta con una tabla hash (mismo orden y mismo formato de archivo): de 108 ms a 1 ms. La firma de `stages/objects.stamp` se conserva: con ella el índice de arranque cuesta 7 ms; sin ella, 35 ms (leer y calcular el hash de los 8632 PNG).
- **Escenarios originales en una carpeta (A7.2, hecho)**: `z_stage/s<nnn>pack/` pasa a `stages/s<nnn>/tiles/` (`tiles.json`, formato `stages-tiles-v1`, mismos campos que el `stage-pack-v1` de antes), `stages/<nnn>.json` a `stages/s<nnn>/stage.json` (`stage-v1`) y el parche de un mod al mismo camino bajo `mods/<mod>/stages/s<nnn>/stage.json` (conserva su encabezado `stage-patch-v1`; el `<n>.json` viejo no se lee). Un solo `stages/manifest.json`: la versión de las baldosas es la unidad `stage-tiles/s<nnn>` del `exporter` (una por escenario, como los bancos de audio, para reintentar solo el que falló) y desaparecen los 237 manifiestos por escenario. La comprobación de "esta baldosa de `data/` está editada" lee ese manifiesto una vez (al primer uso tras `stageModSetup`/`stageModRecheck`), no por escenario. Sin compatibilidad: las carpetas viejas no se leen y el exportador **no las borra** (un archivo de `z_stage/` puede ser una edición del usuario; es lo mismo que hicieron A5.5 y A6.4 con lo que dejaron de exportar): deja una línea de log y las entradas `<nnn>.json` del manifiesto se quitan (`tdManifestDrop`). `SD_EXPORT_VERSION` 4 y `STAGE_EXPORT_VERSION` 2 hacen que un `data/` viejo se reexporte entero.

### 7. Migración (sin compatibilidad; los ejemplos se rehacen en A9)

| Hoy | Pasa a | Se quita en |
|---|---|---|
| `images/<DIR>/<NAME>/NN_MM.png` + `.json` `atlas-v1`; `mods/*/images/...` de menús | `menus/<pantalla>/<pieza>.png` + `menu.json` | A5 |
| retratos e íconos en 7 hojas; `art`/`icon`/`sprite` de `partners.json`; `content/partners.json` | `partners/<nombre>/` (desde A8.2 el registro de un compañero nuevo es su `partner.json`, `partners-partner-v1`; `content/partners.json` ya no se lee, `--validate-mod` lo avisa) | A3 (exporta), A4a (importa; quitados `art`/`icon`/`sprite`) y A8.2 (`content/partners.json`, hecho) |
| `images/field/sprt/<nombre>/NNN.png` + `sprite-v1`; `mods/*/sprites/` | `sprites/<nombre>/` (`sprites-sprite-v1`) | A7.5 (hecho: `sprites/<nombre>/` con el nombre del archivo del disco en minúsculas, identidad `dw3:sprite/<nombre>`; una carpeta de mod con el nombre de una original son sus cuadros `NNN.png` y cualquier otro nombre es un sprite nuevo) |
| `images/field/stage/s<nnn>mask` (objetos; quitado en A6.4), `z_stage/<llave>/`, `stages/<n>.json`, `new-stages/<nombre>/` | `stages/s<nnn>/` y `stages/<llave>/` (`stage.json`, `tiles/`, `objects/`) | A6 (objetos, hecho), A7.2 (baldosas, `stage.json` y parche de mod, hecho) y A7.3 (`new-stages/`, hecho: una carpeta `s<nnn>` es un original, cualquier otra es un escenario nuevo) |
| `images/card/cardpak<A>/<NN>.png`, `mods/*/card/<llave>.png`; `content/cards.json` | `cards/c<nnn>/face.png` (originales, A7.4), `cards/<llave>/face.png` (nuevas, A7.4) y, desde A8.1, `cards/<llave>/card.json` (`cards-card-v1`, el registro de una carta nueva: los campos de la entrada vieja con `title` en lugar de `name`, que queda para la cabecera); `content/cards.json` ya no se lee (`--validate-mod` lo avisa). El `card.json` exportado de una original usa el mismo formato, es descriptivo y no se lee: una carpeta `c<nnn>` de un mod solo reemplaza el arte (el juego no tiene un parche de la ficha de una original; sus datos se cambian con `tables/`) | A7 (arte) y A8.1 (registro) |
| `models/<id>/` (`model-src-v1`), `content/models/<llave>/`, `content/digimon.json` | `digimon/d<nnn>/model/` (originales, con `names` en `digimon/manifest.json`), `digimon/<llave>/digimon.json` (`digimon-digimon-v1`, el registro de un Digimon nuevo, con `title` para el texto) y `digimon/<llave>/model/`; `digimon/d<nnn>/digimon.json` cambia a un original (`partner`/`enemy`, lo que hacía `"change"`; su `model/` no se lee: no hay forma de reemplazar el modelo de un original); los emplazamientos pasan a `content/encounters.json` (`encounters-v1`). `models/` de datos de antes: el exportador deja una línea en el log y no la toca | A8.3 (hecho) |
| `audio/<BANCO>/`, `video/<NOMBRE>.avi`, `text/<idioma>/`, `tables/`, `config/` | mismos temas, nombres en minúsculas, cabecera y manifiesto nuevos | B1 |
| `files/<ruta del disco>` | `raw/<ruta del disco>` | B1 |
| un sello por exportador y un `manifest.json` por familia | un `manifest.json` por tema (`asset-manifest-v1`) | B1 |

Todo lo que cambia de forma obliga a rehacer los mods de ejemplo, no a migrarlos (A9). Las partidas no cambian: guardan identidades, no rutas.

## Por qué

- Un modder abre `partners/<nombre>/` y ve un retrato, un ícono y un personaje, no siete hojas; copiar y renombrar la carpeta crea algo nuevo.
- Dos raíces con la misma forma hacen que «exportar y editar» y «mod» sean el mismo trabajo, y un solo resolvedor evita doce copias del orden de carga.
- Mantener las identidades de ADR-009 evita tocar partidas y relaciones; el alias legible solo agrega una forma de escribir.
- Un manifiesto por tema y una cabecera uniforme sustituyen los sellos dispares y dan a B5 un lugar donde medir duplicados sin adivinar.

## Consecuencias

- **A3/A4/A5/A6/A7** implementan cada tema; B1 y B4 son prerrequisitos (escritura y resolvedor únicos). Hasta que cada tema se migre, convive el formato viejo
  del exportador que aún no se tocó; al migrarlo se borra su formato anterior, sin capa de compatibilidad.
- `content/` queda con registros sin medios; los registros con medios cuelgan de su asset. Eso cambia cómo los lee el núcleo (`content.c`): ahora reciben
  una **fuente por carpeta** además de la de archivo único (a diseñar en A3/A4, ver pregunta 3).
- Aparecen reglas de validación nuevas: nombre de carpeta nuevo igual a un original, archivos de un asset que no declaran su base, PNG fuera de límites.
- La guía de mods, el esquema (`docs/modding/schema/`) y `--validate-mod` (`mod_tools.c:517`, lista de carpetas conocidas) se reescriben en A8.
- Los 8 originales y los demás assets se nombran con el inglés del disco EU del usuario; otra versión de disco puede producir otros nombres. El nombre es
  solo el camino; la identidad (id) y el hash del manifiesto son lo estable.

## Preguntas abiertas (no decididas por falta de prueba en el código)

1. **`model/` en el compañero.** Se desvió de la especificación por duplicación (§6). Hay que decidir con el dueño si hace falta un atajo `partners/<llave>/forms/<forma>/`
   para un mod que quiera un compañero y su forma inicial en una sola carpeta.
2. **Personaje de campo de cada compañero original.** *Resuelta por A3:* `pcPartnerFieldCharacter` (`ext_state.c:934`) da `primer personaje + p` y las entradas `FIELDSTG_fileEntries` son
   `P003PKEN` ... `P010PATA` (archivos 392..399); `partner.json` guarda el nombre del archivo del disco en `field.character`. Cuál NPC usa cada seguidor del juego completo no se recorrió.
3. **Costo de arranque con un archivo por asset.** Leer 248 + 316 + 8 JSON pequeños (en vez de tres archivos) se hace una vez, pero no está medido; `names` en el
   manifiesto evita abrirlos para resolver un alias. A3 debe medirlo y, si pesa, el manifiesto puede llevar también los registros.
4. **Gemelos partner/enemy.** Si los modelos de dos filas de la misma especie son idénticos (B5 lo dirá con el hash), la fila gemela referencia al otro asset
   (`"model": "<otra carpeta>"`) y no copia; hoy no hay prueba de que lo sean.
5. **Nombre legible de escenarios.** `stage.json` puede llevar `title` del banner (`FIELDSTG_areaNames`, `src/field/field_mode/banner.c:20-31`), pero no todo escenario tiene
   banner ni el lugar es único; la carpeta se queda en `s<nnn>`.
6. **Audio:** se desconoce qué es cada banco (`bgm001`); y **agregar** un sonido o una canción nuevos a un banco sin reemplazar uno no tiene gancho en el juego
   (hoy el mod solo reemplaza).
7. **Transparencia y negro.** El PNG usa `tRNS` en el índice 0; `imgColor` trata 0x0000 como transparente y todo lo demás opaco. Un negro opaco necesita el bit
   STP al reimportar, y la cuantización a 5 bits cambia el color: A4/A5 deben probar el ida y vuelta (PNG -> VRAM -> PNG) antes de fijar la regla.
8. **Alias `dw3:<tipo>/<carpeta>`.** Es una regla nueva del resolvedor de nombres (B4) que no existe hoy; si añade ambigüedad con una llave de mod, se deja solo la identidad numérica.
9. **Texto:** los archivos de texto inglés llevan el prefijo `es` en su nombre (`esdignam.json`); no está probado si todos los idiomas lo comparten, así que se conserva la
   raíz tal cual hasta que A8 lo revise.
10. **Nombre del compañero.** A3 usa `DIGNAM[p+1]` para la carpeta (`choice.c` lo lee así para el nombre inicial) y `PC_PARTNER_ROW(p)` = `p` como forma inicial (`partner_access.h`, identidad 0..7); que ese texto
    sea también el nombre de la fila `p` de `DIGIMON_DATA` no se probó con el texto de `digimon.json`: A8.3 exporta `digimon/d<nnn>/model/` por id (sin `digimon.json` descriptivo ni nombre por fila) y la pregunta sigue abierta para quien exporte los registros de los Digimon.
