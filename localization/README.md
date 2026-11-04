# Localización es-419 (español de Latinoamérica)

Este directorio contiene nuestro trabajo de traducción, no el texto del juego (ver
[ADR-001](../docs/decisions/ADR-001-public-data.md)). El texto oficial se lee del disco del usuario.

## Qué es el overlay

`localization/es-419/<ARCHIVO>.json` (50 archivos, uno por lang file del idioma base `SPN`, el español
oficial del disco europeo) lista solo las cadenas cuyo texto es-419 **difiere** del español oficial.
Las cadenas que no aparecen se toman sin cambios del disco del usuario. Hoy son 4,835 cadenas; 2 archivos
quedaron con lista vacía (el español oficial ya servía) y se conservan para mantener la cobertura.

## Formato `overlay-v1`

```json
{"file": "SSXXXXXX.BIN", "base": "SPN", "format": "overlay-v1", "description": "...",
 "strings": [{"index": 6, "note": "G-024", "text": "..."}]}
```

- Clave de una cadena: `index`; en los packs `SSDMG*` la clave es `(part, index)` y `part` aparece.
- `note` (opcional): IDs de reglas del glosario aplicadas (`G-018`, ...). `text`: el texto es-419.
- `{xxxx}`: códigos de control crudos del juego, que se conservan. Alias legibles: `{hablante}Nombre{hablante}`
  envuelve la etiqueta del hablante, `{pagina}` es salto de página, `{fin}` cierra la cadena.
  El resto queda en hexadecimal (`{0102}`, `{0208}`...). `\n` es salto de línea.
- Nunca se guardan los campos `en` ni `es` (texto oficial). Verificar con una búsqueda de esas claves en `localization/`.

## Glosario y reglas

- `es-419/GLOSSARY.md`: decisiones del dueño (registro neutro de doblaje latino, "Tamer", "Lentes", "Wow"),
  tabla de reglas G-001..G-036 y límites de ancho por tipo de texto.
- `es-419/rules/*.py`: reglas de reescritura es-ES a es-419 (módulos con `RULE` y `transform(texto, archivo)`).
  Se auditaron para no citar frases completas del juego: solo términos y frases cortas. Las frases completas
  reescritas están en el overlay. La herramienta que las aplique (leer disco, aplicar reglas en orden, escribir solo lo que
  difiere) es trabajo de una tarea futura.

## Formato de los lang files

- **Lang file:** `u32` cantidad `N`, luego `N` `u32` de offsets (desde el inicio del archivo, little endian)
  y después las cadenas. Cada cadena termina en `0x00` y empieza alineada a 4 bytes (relleno con ceros).
  El primer offset es >= `4 + 4N`; puede haber un hueco tras la cabecera y offsets compartidos entre índices.
- **Lang pack** (archivos `SSDMG*`): tabla de `u32` cuyo primer valor es el tamaño de la tabla; cada
  entrada apunta a un lang file embebido (de ahí `part`). Algunas partes pueden ser datos crudos.
- **Round trip:** para reconstruir bytes idénticos hay que conservar hueco tras la cabecera, orden, offsets
  compartidos y cualquier cola distinta de "terminador + ceros hasta alinear".

## Codificación y charmap

- Un byte por carácter común: `0x04`-`0x0D` dígitos `0`-`9`, `0x0E`-`0x27` mayúsculas A-Z, `0x28`-`0x41`
  minúsculas a-z, y unos pocos signos sueltos (`?`, `!`, `~`).
- Secuencias `0x01 xx`: espacio, puntuación (`, . : ; / ( ) + - = < > % &`, comillas, elipsis), `¡`, `¿` y
  los acentos y diéresis latinos (`á é í ó ú ñ ü` y sus mayúsculas, más ç, ß, ø, etc.).
- Secuencias `0x02 xx`: códigos de control; `0x02 0x01` es salto de línea. Los demás (`{0202}`, `{0123}`...)
  se tratan como tokens opacos: decodificar nunca debe perder bytes.
- Un carácter sin codificación es un error al construir, no se omite en silencio. Las kana quedan como tokens.
- Algunas glifos cambian según la fuente: `0x01 0x2B` es `&` en campo/menú/cartas pero `d` en la fuente de
  mensajes de combate; decodificar según el contexto.
- Fuente de la tabla: referencia pública `markisha64/ddw3` (codepoint); a confirmar en ejecución.
  Los caracteres solo "de referencia" deben confirmarse antes de usarlos.

## Cómo carga el texto este código

Verificado en el código fuente; lo marcado "a confirmar en ejecución" no se ha comprobado corriendo el juego.

- **Direccionamiento.** Los archivos de texto se piden por índice de la tabla de archivos:
  `TEXT_FILE(file) = LANGUAGE + file - 1` (`include/engine/file.h`, solo `VERSION_EU`). Cada archivo de
  texto existe una vez por idioma en bloques de 7 consecutivos, así que los `TEXT_*` base (0x02, 0x09, ...)
  avanzan de 7 en 7. Los textos de escenario usan macros `LANGUAGE + <base>` (p. ej. `STGTRAIN_TEXT`,
  `STAGE_TEXT` en `src/field/stages/`).
- **Ranuras.** El disco EU tiene 7 ranuras de idioma (0-6). La tabla de opciones del menú
  `CNTY_SEL_languages[] = {2, 3, 5, 4, 6, 1, 0}` (`src/menus/country_select/country_select.c`) expone 5.
  Los nombres de las opciones son sprites prerenderizados (datos del disco), no cadenas.
- **`LANGUAGE`.** Es un `s32` simple (`src/engine/data/eu_settings.c`, valor inicial 2). Solo lo asigna el
  menú de idiomas (y el selector de escenas de depuración); no se encuentra en el código de guardado, así
  que no se guarda en la Tarjeta de Memoria (a confirmar en ejecución).
- **Tamaño.** Cada archivo se reserva en el heap con `FILE_SECTOR_COUNTS` (`src/engine/file/file_table.c`,
  `file_cache.c`), no en un búfer fijo. Un archivo más largo necesita un conteo de sectores mayor en la
  tabla reconstruida (a confirmar en ejecución).
- **Disco.** `AAA/DAT/COUNTRY/{ENG,FRA,GER,ITA,JPN,SPN,USA}`, 50 archivos cada uno; el nombre es la letra del
  idioma más los mismos 6 caracteres (p. ej. `SSAMTMAP.BIN` en `SPN`, `USAMTMAP.BIN` en `USA`).

## Generar el texto es-419

Lee los lang files `SPN` del disco extraído, aplica el overlay y escribe los archivos es-419 (mismo nombre)
más `manifest.json` (tamaño y sectores por archivo, y cuáles crecen respecto al original). Falla si una
clave `index`/`part` del overlay no existe. La salida va a `build/` y nunca se versiona.

```sh
.venv/bin/python tools/es419/build_text.py --disc disks/eu --overlay localization/es-419 --out build/eu/es419/text
.venv/bin/python -m unittest discover -s tools/es419 -v   # round trip, overlay y códec (se saltan sin disks/eu)
```

## Compilar con es-419

`make VERSION=eu ES419=1` compila el código con la sexta opción del menú (`#if ES419`; la compilación por
defecto no cambia y sigue dando 316 OK). Va a `build/eu/es419/` y las comprobaciones de coincidencia
(`compare`, ...) la rechazan, como con `TOOLCHAIN=gcc`. El Makefile genera antes, desde `disks/eu`, el texto
(`build_text.py`) y `build/eu/es419/es419_table.h` + `es419_layout.json` (`tools/es419/gen_table.py`): el
primero coloca sectores y conteos de los 50 archivos en los huecos USA de la tabla de archivos (`2 + 7k`), el
segundo asigna su LBA dentro de `DUMMY.` (desde 276901) para el parcheador de disco.

```sh
make VERSION=eu ES419=1 -j$(nproc)   # dentro del contenedor Docker del upstream (su tools/docker.sh)
```

Cómo funciona: el exe no cambia de tamaño (923648 B, `main_VRAM_END` = `OVERLAY_VRAM`). Los huecos USA de la
tabla guardan los archivos es-419 y, al pulsar Start en CNTY_SEL (opción 5, `LANGUAGE = 6`), el overlay
intercambia en RAM las entradas españolas (`7 + 7k`) con las USA (`2 + 7k`) de `FILE_SECTORS` y
`FILE_SECTOR_COUNTS`; cualquier otra opción las deja como el original. El estado se lee de la propia tabla
(`FILE_SECTORS[7]` frente al LBA original), así que un reset suave al menú sigue correcto. Con ES419 se omite
el paso 1 de `moveSelection` (la segunda columna, que nada activa) para que CNTY_SEL.PRO quepa (5751 B de
6144; con la fila nueva, 5763 B).

## Arte del menú (sexta fila)

`tools/es419/menu_bank.py` lee `AAA/DAT/COUNTRY/CNTSELWN.BIN` del disco del usuario (hoja de sprites + TIM
RLEN; el decodificar y recodificar sin cambios da bytes idénticos), aplica los cambios es-419 y escribe
`build/eu/es419/CNTSELWN.BIN` (`make ES419=1` lo regenera) más vistas previas PNG en
`build/eu/es419/preview/` (`before_menu.png`, `after_row0..5.png`, `after_row5_zoom.png`). No se versiona
ningún píxel del juego:

- Seis filas caben en 240 líneas subiendo la barra de título 12 px (y=8..36) y repartiendo las filas cada 32 px
  desde y=40 (antes cada 34 desde y=52): el último resaltado acaba en y=231 (margen de 8-9 px).
- "ESPAÑOL LATINO" va en dos líneas (una línea de ~118 px no cabe en los ~92 px del panel): la palabra
  ESPAÑOL existente y LATINO compuesto con recortes de las letras de ITALIANO (partes nuevas que apuntan a
  texels existentes, sin píxeles nuevos).
- Bandera: la parte de la bandera italiana (verde, blanco, rojo) más un emblema propio de 11x12 píxeles dibujado
  por código en texels libres del TIM (fila 224) y una paleta libre (x=16, fila 250). El emblema es arte
  provisional: esquema de ave sobre nopal, sujeto a revisión del dueño.
- El resaltado de la fila 6 es un cuadro nuevo (12, `SPRITE_ES419`); `country_select.c` lo dibuja para la
  opción 5 bajo `#if ES419`.
- El banco reconstruido (16680 B) cabe en sus 9 sectores: `make_disc.py` lo escribe en sitio sobre los del
  original (la longitud del registro ISO queda en 16416, el juego lee por la tabla de sectores).

## Probar en un emulador

Con la compilación `ES419=1` hecha, `tools/es419/make_disc.py` copia la imagen original (solo lectura) a
`build/eu/es419/disc/DW2003_ES419.bin` + `.cue`: escribe en su sitio el exe y los `.PRO` (como
`patch_disc.py --fit`) y los 50 archivos de texto en sus LBA de `es419_layout.json`, dentro de `DUMMY.`
(sectores a cero que el juego no lee), con cabecera, subcabecera, relleno, EDC y ECC. Rechaza cualquier
archivo que salga de `DUMMY.` o caiga en un sector que no sea Form 1 con ceros. `DISC` es la imagen original,
la pasas tú (solo para tu máquina; la imagen resultante no se publica):

```sh
make VERSION=eu ES419=1 -j$(nproc)   # dentro del contenedor Docker del upstream (su tools/docker.sh)
make ES419=1 DISC="<imagen original>.bin" es419-disc   # o: .venv/bin/python tools/es419/make_disc.py --image <...>.bin
```

Pruebas: `.venv/bin/python -m unittest discover -s tools/es419`.

## Reglas de trabajo

1. **Anchos.** Cada caja tiene su límite (diálogo ~22 caracteres, etiqueta de hablante 18, mapa 17, salas 16,
   posadas 19, tiendas 20; ver glosario). Desde la ronda 2 (G-042) el texto nunca se abrevia: el
   port ajusta el texto completo a la caja (reflujo en varias líneas, auto-escala en una línea); medir en pantalla.
2. **Evidencia.** Marcar como verificado solo lo visto en ejecución; la traducción se revisa por sentido
   (calcos del inglés, subjuntivo en -ase/-iese, interjecciones de España) además de por regla.
3. **Control de calidad.** Las reglas deben ser idempotentes y no alterar los tokens `{...}`; un chequeo
   "0 cambios" tras aplicarlas detecta regresiones. Nombres propios con errata oficial se corrigen en reglas.

## Pendiente

- Opción es-419 en el menú de idiomas: lector/aplicador de lang files y conteo de sectores en el build.
- Verificar en pantalla los nombres de lugar expandidos y los nombres de items de cartas/laboratorio.

## Reacomodo automático del texto (`layout.json`, L4a)

El motor no parte líneas: una línea termina en `02 01` y una página en `02 03`. El es-419 es más largo que el
español oficial y el port no acorta el texto para que quepa (no lo limita la PS1), así que el port agrega los
saltos que hacen falta. Lo hace `pc/shim/text_reflow.c` sobre los bytes de cada cadena, midiendo con las mismas
tablas de glifos que usa `measureText`.

- **Qué se reacomoda.** Solo las cadenas que difieren del disco (el texto oficial nunca se toca) y solo las
  páginas con alguna línea que no cabe: el resto conserva sus saltos originales. Una página que no cabe se une
  en un párrafo y se corta con avance voraz en los espacios; si pasa de las líneas de la caja, se inserta el
  salto de página `{pagina}`. Esperas, códigos de color y demás códigos quedan pegados a su palabra; la etiqueta
  de hablante no se mide. Los códigos `02 05` (nombre del jugador, números) reservan un margen fijo: 8 letras
  del glifo más ancho para el nombre y 6 dígitos para un número.
- **Dónde se aplica.** Al armar el es-419 (el JSON generado en `<datos>/text/es-419` ya trae el texto
  reacomodado), y al servir archivos de datos editados o de `mods/`. Los idiomas del disco, sin editar, siguen
  siendo los bytes del disco. Cambiar `layout.json` o el códec regenera las carpetas (los archivos editados por
  el usuario no se pisan). `DW3_TEXT_REFLOW=0` lo apaga.
- **`localization/layout.json`.** Presupuestos por archivo (nombre sin letras de idioma; `TALK*` es prefijo):
  `maxWidthPx`, `maxLines` (1 = no se puede partir, solo se informa), `tightWidthPx` (si el texto original de la
  cadena cabía en ese ancho, usa ese: es una caja de charla de 95 a 139 px; si no, el ancho de la caja de
  mensaje), `noreflow`, y `entries` (`"índice"` o `"parte:índice"`) para excepciones. Quedan sin presupuesto los
  campos de una línea (nombres, celdas, UI de cartas): son de L4c.
- **Registro.** Al armar, por archivo: cuántas cadenas se reescribieron, saltos de página agregados y cuántas
  siguen sin caber (una palabra más ancha que la caja, o una caja de una línea).
- **Auditoría local** (con el disco del usuario, no se versiona nada):
  `pc/build/text_measure --audit <datos> localization/layout.json [carpeta]` cuenta las cadenas de
  `<datos>/text/<carpeta>` con una línea fuera de su caja (`AUDIT_LIST=1` las lista).
