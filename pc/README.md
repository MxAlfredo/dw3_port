# Port a PC — el C del juego en el host

Este directorio compila y ejecuta **C propio del juego** (`src/**`, sin modificar) para
x86-64 Linux con el `gcc` del host, y mide cuánto *shim* hace falta para que ese C corra
fuera de PsyQ.

Son diez hitos del port:

- **Hito 0 (T3):** el descompresor **RLEN** del juego (`rlen_tool`).
- **Hito 1 (T5):** la **capa de archivos** del juego lee el disco del usuario
  (`file_dump`): tabla de archivos + caché + un reemplazo propio de `libcd`.
- **Hito 2 (T8):** el **motor de texto** del juego mide en el host
  (`text_measure`): carga un lang file del disco, saca una cadena y la mide
  con `FONT.decode` + `measureText`, estilo 1 (el del banner de área).
- **Hito 3 (T12):** el **dibujo** del juego corre en el host
  (`draw_order`): la ventana de texto del juego (`text_window.c`, sin
  modificar) se compila contra las cabeceras de **PsyCross**, arma una orden de
  dibujo con sus primitivos (`SPRT`, `DR_TPAGE`) sobre una *ordering table* y
  el check la mide (cantidad y tamaño de primitivos = el *layout* de PsyCross,
  no el retail 4.7). Sin ventana y sin renderer.
- **Hito 4 (T13):** la **primera imagen en pantalla** (`first_frame`): el
  **banner de área** del juego (`banner.c`, sin modificar) dibuja sus
  cuadriláteros planos sobre una ventana de PsyCross y un bucle de
  `PsyX_BeginScene`/`PsyX_EndScene` los presenta. Sin disco, sin VRAM y sin
  fuentes: el banner no usa texturas.
- **Hito 5 (T14):** la **fuente en VRAM y el texto del juego** (`font_frame`): el cargador
  de TIM del juego (`tim_loader.c`, sin modificar) sube los TIMs de `FILE_FONT` (0x287)
  del disco del dueño a la VRAM por `LoadImage`/`DrawSync`, y la ventana de texto del
  juego dibuja una cadena de un lang file del disco en el estilo del banner (estilo 1),
  con los UV y el CLUT reales de la tabla de glifos. **Acá aparecen letras del juego.**
- **Hito 6 (T15):** **el arranque del juego** (`boot`): corre el `main()` del propio
  juego (`src/engine/system/main.c`, sin modificar), con su bucle de cuadros y de
  modos, sobre los shims de `libgs` y `libsnd` y un motor de dibujo propio
  (`shim/gfx_engine.c`); el primer modo que el juego alcanza es la **selección de
  país/idioma**, que se dibuja con su propio código (`country_select.c`) y su arte
  del disco (file 0x8A3). **Acá se ve la pantalla del juego.**
- **Hito 7 (T17):** el **input del juego** (`input-demo`): el pad del propio juego
  (`src/engine/pad/`) lee un pad de verdad cada cuadro por la `libpad` de PsyCross, y el
  teclado del host llega hasta ahí por la cadena normal de PsyCross (`PsyX_UpdateInput`).
  La selección de idioma **responde**: el resaltado se mueve con arriba/abajo y Start
  confirma, con el código del juego. **Acá el juego se maneja.**
- **Hito 8 (T18):** **la pantalla de título** (`title-demo`): al confirmar el idioma, el juego
  pide su segundo video de apertura (`MODE_OPENING_2`, 0xE02), que es el overlay **STDWTITL**
  (índice 14) en su rama de video. El port lo enlaza y, desde T37-T39, **decodifica el video de
  verdad** (MDEC + streaming del `.STR`, ver "La pantalla de título y el video"): la demo saltea
  el video con Start —lo que hace un jugador— y el **código del propio juego** pide entonces la
  pantalla de título (`MODE_TITLE`, 0xE00) y la dibuja con su arte del disco. **Acá el juego
  encadena modos: del menú de idiomas al título.** (El cuadro decodificado llega a la VRAM, pero
  PsyCross no muestra VRAM en la ventana: la imagen todavía no se ve, ver esa misma sección.)
- **Hito 9 (T19/T22/T23/T24):** **el campo** (`field-demo`): el menú del título muestra sus dos
  opciones, el guion navega hasta **NEW GAME** y confirma, y el **código del propio juego** pide
  entonces `MODE_NEW_GAME` (0x2D7), cuyo índice 2 es **FIELDSTG**, el modo del campo
  (`src/field/`), que el port **enlaza** junto con sus stages (`pc/shim/host_overlay.c`,
  `WSTAG###_*`). El campo crea sus capas y dibuja con su propio código. Su cargador leía un
  `FIELDSTG_state.slots` en `NULL` --un stage sin slots, que la PS1 tolera y el host no--; T22 le
  puso el guard que le faltaba (`pc/field/file_loader.c`) y el campo sobrevive ese punto. T23
  hizo que la capa del host cargue el puntero entero que el juego registra en su callback de
  dibujo (el campo de 32 bits del header lo truncaba). T24 ensanchó los `s32` que el propio campo
  usaba para el puntero de la tarea de un comando de guion (en `src/**`, con el matching de PS1
  revalidado en 316 OK), y el campo ya no muere: corre sus 900 cuadros de presupuesto y dibuja el
  mapa (una captura temprana muestra el rótulo del área, dibujado por el motor de texto del juego;
  no se cita acá). T26 hizo que la capa del host ordene sus callbacks de dibujo por prioridad,
  como la del propio juego (`src/engine/gfx/layer.c`). T27 hizo que el host mande el `DRAWENV` de
  **cada capa con su propia tabla** (en la PS1 viaja adentro de la OT, `layerDraw`'s `DR_ENV`) y
  que los `DR_TPAGE` del propio juego —que traen `dfe 0`, dibujar al área de display prohibido,
  porque en la PS1 el juego dibuja al buffer trasero y voltea— se dibujen en la **única
  superficie** del port (la ventana): el cuadro del campo ya no queda en blanco.
  **Acá el juego entra al campo, dibuja su mapa y corre hasta el final del presupuesto.**

T11 suma la **capa PsyCross** (`build/psycross/libpsycross.a`): la biblioteca que reemplaza a
Psy-Q, compilada desde el submódulo sin tocarlo. T13 la ejecuta por primera vez: ventana,
renderer y presentación.

En los hitos 0-3 no hay GPU, sonido ni entrada; la prueba es que el C del juego corre en el host y
produce bytes idénticos a los de una lectura independiente en Python. El hito 4 sí abre una
ventana y presenta. El hito 5 carga la fuente del disco y dibuja texto real. El hito 6 es el
primero que corre el **juego entero desde su `main()`**: no hay arnés por debajo, la pantalla la
arma el código del juego sobre el estado gráfico del shim. El hito 7 es el primero en que el
**juego se maneja**: el pad del propio juego lee un pad de verdad y la pantalla de idiomas
responde a las teclas. El hito 8 es el primero en que el **juego encadena modos por sí mismo**:
del menú de idiomas al video de apertura y de ahí a la pantalla de título, cada paso pedido por
su propio código. El hito 9 es el primero en que el juego **entra al campo**: la cadena llega
hasta `MODE_NEW_GAME` y el código del campo dibuja sus propias capas; desde T22 el campo además
sobrevive el punto donde su cargador leía un puntero que la PS1 toleraba y el host no, desde T23
dibuja sus objetos del mapa (el callback de la capa recibe el puntero entero), desde T24 corre su
presupuesto entero dibujando el mapa (el camino de comandos de guion ya no trunca el puntero de su
tarea), desde T26 la capa del host ordena sus callbacks de dibujo por prioridad como la del
propio juego y desde T27 el cuadro se dibuja con el `DRAWENV` de cada capa en su propia tabla y
sobre una sola superficie, así que ya no queda en blanco cuando el banner de área cierra.

## Comandos

```sh
# Compila (no usa Docker, no toca el build de PS1):
make -C pc

# Hito 0: genera vectores RLEN con el empaquetador del repositorio, corre el programa
# del host sobre cada uno y compara la salida byte a byte:
.venv/bin/python pc/check_rlen.py
# (o: make -C pc check)

# Hito 1: resuelve índices de archivo por el camino del propio juego y los compara con
# una lectura independiente del mismo disco. La imagen la aporta el dueño; no se copia
# nada de ella (ADR-001):
.venv/bin/python pc/check_file.py "<imagen>.bin"
# (o: make -C pc check-file DISC="<imagen>.bin")

# Hito 2: mide cadenas de un lang file del disco con el motor de texto del juego y las
# compara con una medición independiente en Python:
.venv/bin/python pc/check_text.py "<imagen>.bin"
# (o: make -C pc check-text DISC="<imagen>.bin")

# Hito 3: arma una orden de dibujo con la ventana de texto del juego (sus primitivos
# contra PsyCross) y verifica cantidad, tipo y tamaño de los primitivos:
.venv/bin/python pc/check_draw.py "<imagen>.bin"
# (o: make -C pc check-draw DISC="<imagen>.bin")

# Hito 4: abre la ventana de PsyCross y presenta el banner de área del juego dibujado
# por su propio código (sin disco, sin VRAM). Se cierra solo; FRAMES cambia el largo:
make -C pc first-frame
# (o: pc/build/first_frame [cuadros] [screenshot])

# Hito 5: carga la fuente del disco del dueño (por el cargador de TIM del juego) en la
# VRAM y dibuja una cadena de un lang file del disco con la ventana de texto del juego,
# en el estilo del banner. Abre la ventana, presenta FRAMES cuadros y se cierra solo;
# SHOT es la captura PNG (la escribe el propio arnés con glReadPixels):
make -C pc font-frame DISC="<imagen>.bin"
# (o: pc/build/font_frame "<imagen>.bin" [archivo] [cadena] [cuadros] [captura.png])

# Hito 5, la comprobación: los bytes de los TIMs de la fuente están en la VRAM y los
# primitivos de los glifos apuntan a los UV/CLUT de la tabla de glifos:
.venv/bin/python pc/check_font.py "<imagen>.bin"
# (o: make -C pc check-font DISC="<imagen>.bin")

# Hito 6 (T16), la comprobación: el arte del banco de la selección de país (su imagen de
# 8 bits y su CLUT de 256 entradas) está en la VRAM byte a byte como en el disco y el sprite
# del panel izquierdo apunta a su parte, su página y su CLUT:
.venv/bin/python pc/check_country.py "<imagen>.bin"
# (o: make -C pc check-country DISC="<imagen>.bin>")

# Hito 6: corre el main() del JUEGO y su bucle de modos en el host. Sin teclas guionadas, el juego se
# queda en su primer modo (la selección de país/idioma) y la pantalla se dibuja con su propio
# código y su arte del disco. Abre la ventana, presenta FRAMES cuadros, guarda la captura con
# glReadPixels y se cierra solo. El 4º argumento arma el diagnóstico del arte (T16): en ese
# cuadro lee la VRAM de vuelta y la imprime con los sprites del cuadro; 0 lo deja apagado.
# El 6º argumento (T30) es una lista de índices de cuadros separados por comas, basada en
# cero (0..FRAMES-1; con FRAMES=900 los válidos son 0..899): por cada uno el arnés escribe
# un PNG bajo `<captura>_f<índice>.png` desde una sola corrida, además de la captura única
# al final:
make -C pc boot DISC="<imagen>.bin"
# (o: pc/build/boot "<imagen>.bin" [cuadros] [captura.png] [cuadro-del-arte] [teclas]
#                   [lista-de-cuadros])

# Hito 7: el mismo main() del juego, pero el arnés escribe en proceso una secuencia del pad
# (eventos de tecla reales en la ventana de PsyCross, así que el run pasa por todo el camino
# que pasa una tecla del jugador: SDL -> la libpad de PsyCross -> el pad del propio juego ->
# el menú de la selección de país). Cada cuadro imprime lo que vio el código del juego y qué
# opción resalta el menú, y el run termina solo con la captura en INPUT_SHOT.
# PAD_KEYS es la secuencia separada por comas ('down,down,up' por defecto):
make -C pc input-demo DISC="<imagen>.bin"
# (o: pc/build/boot "<imagen>.bin" [cuadros] [captura.png] 0 "<up,down,...>")

# Hito 7, la comprobación: corre esa misma demo guionada y verifica, a partir de su propia
# salida, que cada pulsación llegó al pad del juego y que el resaltado del menú se movió
# abajo, abajo y de vuelta arriba (pc/check_input.py):
.venv/bin/python pc/check_input.py "<imagen>.bin"
# (o: make -C pc check-input DISC="<imagen>.bin")

# Hito 8: el mismo main() del juego, pero la secuencia guionada camina más allá de la selección
# de idioma. 'down' saca el resaltado de la primera opción, el primer 'start' confirma (cualquier
# otra opción de la lista hace que el juego pida MODE_OPENING_2, 0xE02 = el overlay STDWTITL),
# 'wait' espera a que el modo del juego cambie —el video de apertura— y el segundo 'start'
# saltea ese video, que el port ya decodifica y dibuja (T39; ver "La pantalla de título y el
# video"). El propio código del
# juego pide entonces la pantalla de título (0xE00) y la dibuja con su arte del disco. El run
# presenta TITLE_FRAMES cuadros, captura con glReadPixels y se cierra solo:
make -C pc title-demo DISC="<imagen>.bin"
# (o: pc/build/boot "<imagen>.bin" [cuadros] [captura.png] 0 "down,start,wait,start")

# Hito 8, la comprobación: corre esa demo y verifica, de su propia salida, que el código del
# juego pasó por los modos 0x1600 -> 0xE02 -> 0xE00, que el MDEC del port decodificó y presentó
# los cuadros del video sin errores, que la captura de la corrida es una imagen (no un blanco) y
# que la pantalla de título dibujó sus sprites en su propia capa (pc/check_title.py):
.venv/bin/python pc/check_title.py "<imagen>.bin"
# (o: make -C pc check-title DISC="<imagen>.bin")

# T39, la demo del video: entra al video de apertura (0xE02) y se queda ahí, sin saltearlo, con
# MDEC_DEBUG=1 (una línea por cuadro decodificado y el total al cerrar el video). La lista de
# captura (7º argumento) escribe <stem>_f<cuadro>.png por cada índice, así el dueño mira varios
# cuadros sin repetir la corrida. El cuadro se dibuja en la ventana desde T39 y se ritma contra
# el audio desde T40; DW3_WINDOW=640x480 (T41) lo muestra con el ancho doble del modo 480i.
make -C pc movie-demo DISC="<imagen>.bin"

# Hito 9 (T19/T22/T23/T24/T26/T27): el mismo main() del juego, con el guion que además navega el menú del título
# hasta NEW GAME y confirma. El código del propio juego pide entonces MODE_NEW_GAME (0x2D7) =
# FIELDSTG (índice 2), el modo del campo, que el port enlaza con sus stages. El campo crea sus
# capas y dibuja. T19 se detenía a los pocos cuadros porque su cargador leía un
# FIELDSTG_state.slots en NULL (un stage sin slots) que la PS1 tolera y el host no; T22 le puso
# el guard que le faltaba (pc/field/file_loader.c) y T23 hizo que la capa del host le pase al
# callback de dibujo el puntero entero que el juego registra (el campo de 32 bits del header lo
# truncaba). T24 ensanchó los s32 con que el propio campo transportaba el puntero de la tarea de
# un comando de guion (en src/**, con make VERSION=eu compare revalidado en 316 OK), T26 hizo
# que la capa del host ordene sus callbacks por prioridad como la del juego (layer.c) y T27 que
# cada capa reciba su propio DRAWENV en su propia tabla, sobre una sola superficie (el cuadro ya
# no queda en blanco al cerrar el banner de área). El run ya
# no tiene parada artificial y ya no muere: corre sus 900 cuadros de presupuesto, dibuja el mapa
# y deja la captura:
make -C pc field-demo DISC="<imagen>.bin"
# (o: pc/build/boot "<imagen>.bin" [cuadros] [captura.png] 0 "<teclas>")

# Hito 9, la comprobación: corre esa demo y verifica, de su propia salida, que el código del
# juego pasó por 0x1600 -> 0xE02 -> 0xE00 -> 0x2D7, que pidió el stage enlazado (0x1E9), que
# creó sus capas 0x1000-0x1004, que dibujó en ellas y siguió dibujando más allá del cuadro 468
# (donde moría el cargador sin el guard; el 463 es la parada a propósito de T19), del 474 (donde
# moría el callback por el puntero truncado, que T23 arregló) y del 562 (el camino de comandos de
# guion, que T24 arregló), que los callbacks de la capa del mapa corrieron en el orden por
# prioridad del propio juego (T26, con el volcado que arma DW3_CB_DUMP, ver abajo), que cada capa
# mandó su propio DRAWENV con su propia tabla y que el cuadro capturado es una imagen, no un
# blanco (T27, con el volcado que arma DW3_ENV_DUMP, ver abajo), y que el run
# alcanza su presupuesto (900 cuadros) y deja una captura
# de 320x240. Un segundo argumento
# (por ejemplo el guion del título, sin newgame) es el control negativo: la comprobación falla
# porque el campo nunca se entra (pc/check_field.py). Un 3er argumento (T30) corre la
# aceptación de captura múltiple: una corrida del arnés con la lista, una corrida única
# por índice y verifica que cada PNG de la corrida múltiple sea byte a byte igual al de
# la corrida única:
.venv/bin/python pc/check_field.py "<imagen>.bin"
.venv/bin/python pc/check_field.py "<imagen>.bin" "<teclas>" "<lista-de-cuadros>"
# (o: make -C pc check-field DISC="<imagen>.bin")

# Limpia:
make -C pc clean
```

Los binarios quedan en `pc/build/` (bajo `build/`, ignorado por git). Uso manual:

```sh
pc/build/rlen_tool <archivo.rlen> <salida.bin>
pc/build/file_dump <disco.bin> <índice de archivo> <salida.bin>
pc/build/text_measure <disco.bin> <índice de archivo> <índice de cadena>...
pc/build/draw_order <disco.bin> <índice de archivo> <índice de cadena>
pc/build/first_frame [cuadros] [screenshot]   # el 2º argumento guarda first_frame.ppm
pc/build/font_frame <disco.bin> [archivo] [cadena] [cuadros] [captura.png]
pc/build/boot <disco.bin> [cuadros] [captura.png] [cuadro-del-arte] [teclas] [lista-de-cuadros]   # el main() del juego; sin 2º argumento, 300 cuadros
```

`[lista-de-cuadros]` (T30): una lista opcional separada por comas con índices de cuadros
basados en cero. El presupuesto del arnés es la cantidad de cuadros *presentados*: con
`cuadros = 900` los índices válidos van del 0 al 899 (899 es el 900° cuadro mostrado).
Cuando se da, el arnés escribe un PNG por índice bajo `<captura>_f<índice>.png` desde una
sola corrida, además de la captura única al final del presupuesto de cuadros que ya hacía.
Lista vacía, coma final o repetida, entrada no ASCII / no dígito, entrada que desborda
`INT_MAX`, índice negativo, fuera del rango, duplicado, o más de `PC_BOOT_MAX_CAPTURES`
(8) entradas — todo se rechaza al inicio con `exit 2`, antes de `pcCdReaderInit`, para
que el arnés nunca pierda una captura en silencio. Aceptación: `pc/check_field.py
<disco.bin> <teclas> <lista-de-cuadros>` valida la lista localmente, corre una vez la
captura múltiple, una captura única por índice (`frames = idx + 1` cada una), y verifica
que cada PNG de la corrida múltiple sea **byte a byte** igual a su contraparte de
corrida única. La parte de byte-equality sigue necesitando un disco utilizable; las
validaciones de la lista se ejecutan sin disco y fallan en RED antes de cualquier
invocación de `boot`.

## Qué consume (C propio, sin copiar)

El build del host **compila estos archivos del upstream tal cual** (sin editarlos):

- `src/engine/file/decompressor.c` — el RLEN: `decompressorSetData` reconoce el magic
  `RLEN`, `decompressorRun`/`decompressorStep` descomprimen. La lógica no se reimplementa
  ni se copia: `pc/rlen_tool.c` solo la invoca.
- `src/engine/task/task.c` — `createTask`, el constructor que la tarea del descompresor usa.
- `src/engine/file/file_table.c` — `FILE_TABLE` (existe/sectores/conteo/posición) y las
  tablas `FILE_SECTORS`/`FILE_SECTOR_COUNTS` del disco EU.
- `src/engine/file/file_cache.c` — `FILE_CACHE`: la caché de hasta 64 archivos, su
  petición, su carga en el heap y su desalojo por uso.
- `src/engine/text/text_tools.c` — `getString` (una tabla de cadenas es un conteo y
  offsets), `measureText` (el ancho en píxeles de la línea más ancha, con los avances de
  los glifos) y las tablas de la fuente; incluidas las europeas (`#elif VERSION_EU`): los
  230 glifos y 114 íconos que ya vienen en el C.
- `src/engine/text/font.c` — `FONT`, los `FONT_STYLES` (0 sin usar; 1, 2 y 3 con líneas de
  14, 11 y 9 píxeles) y `decodeChar` (el próximo carácter como `tipo << 8 | valor`).
- `src/field/field_mode/banner.c` (hito 4) — el banner de área:
  `FIELDSTG_updateBanner` (su máquina de estados, `TASK_INIT`/`TASK_RUN`),
  `FIELDSTG_stretchBannerBox` (estira cada caja hacia su rango) y
  `FIELDSTG_drawBannerBox` (un `POLY_F4` plano por caja). Es la primera unidad del
  juego que **dibuja y se ve**.
- `src/field/field_mode/data/fieldstg.c` (hito 4) — **solo** por sus datos
  `FIELDSTG_bannerBoxes` (las diez cajas: posición, tamaño, color y rango de
  estirado) y `FIELDSTG_areaNames`. El resto de FIELDSTG.PRO nombra 488 símbolos
  que este arnés no tiene, así que la unidad se compila con
  `-ffunction-sections -fdata-sections` y el enlace del hito 4 usa
  `--gc-sections`: el linker descarta todo lo que nadie referencia en lugar de
  compilar el overlay entero.
- `src/field/field_mode/*.c` y `src/field/stages/**/*.c` (hito 9, T19) — el **modo del campo**
  y sus stages. Son unidades de dibujo (cabeceras de PsyCross, C11, `FIELD_CFLAGS` en
  `pc/Makefile`): el campo arma `POLY_F4`/`POLY_FT4`/`SPRT` y sus `Layer` con
  `libgpu`/`libgte`, y **no nombra ningún `Gs*`/`Ss*`/`gte_*`**, por eso se enlaza sin
  `libgs`. Cada global de un stage se renombra `WSTAG###_*` al compilarlo, como lo hace el
  linker de la PS1, para que dos stages no colisionen en un solo binario. **Con una excepción
  (T22)**: `field_mode/file_loader.c` **no** se compila tal cual; el build compila la copia del
  host `pc/field/file_loader.c`, idéntica salvo un guard documentado (ver "El campo
  (T19/T22)"). `pc/Makefile` la saca del glob de las unidades del campo y pone ese objeto en su
  lugar.
- `src/engine/data/eu_settings.c` (hito 4) — el `LANGUAGE` que el `TEXT_FILE()`
  del banner lee; es dato del juego, no shim.
- `src/engine/gfx/tim_loader.c` (hito 5) — el **cargador de TIM** del juego:
  `initTimLoader`, `timLoaderLoad` (sube el CLUT y la imagen de un TIM a la VRAM con
  `LoadImage`/`DrawSync` de `libgpu`) y `timLoaderLoadArchive` (recorre un archivo de
  TIMs, los descomprime si son RLEN y los coloca 0x40 medias palabras a la derecha).
  Es la unidad que en el hito 2/3 estaba **tapada por un stub**
  (`pc/shim/host_tim.c`): ahora `loadFont` corre de verdad. Compila con las cabeceras de
  PsyCross y con `pc/ps1_types.h` delante (ver "`u_long`", más abajo).

El **hito 6** agrega las unidades del camino de arranque del juego, todas sin modificar:

- `src/engine/system/main.c` — el `main()` del juego: modos de video, reset del gráfico,
  imagen de arranque, heap, sonido, pad, memcard, estado del juego, fuente, y después el
  bucle de un cuadro por iteración (tarea de modo, `drawFrame`, pad, RNG, caché y sonido).
  Se compila con `-Dmain=pcGameMain`: el punto de entrada del programa lo pone
  `pc/boot.c`, y el `main` del juego es el que corre (con la ranura de sus hijos medida
  con `sizeof(Task *)`, no con los 4 bytes del `overlay.c` de la PS1).
- `src/menus/country_select/country_select.c` — la pantalla de **selección de país**:
  su tarea raíz, el menú, los tres paneles que abren y cierran con un *tween*, la
  opción resaltada que parpadea y el fondo que se desplaza.
- `src/engine/gfx/sprite_drawer.c` — el **dibujante de sprites** con el que esa pantalla
  dibuja (sprites sueltos y, cuando hay escala, cuadriláteros texturados con el GTE).
- `src/engine/game/*.c`, `src/engine/data/get_digimon.c` y `src/engine/data/eu_settings.c`
  — el estado del juego (`GAME.funcs`), la party, los items, las cartas, los Digimon y el
  tiempo de juego, que `GAME.funcs.newGame()` arma al arrancar. Sólo `events.c` queda
  afuera: nombra símbolos de los overlays de batalla/campo que este binario no tiene.
- `src/engine/pad/{pad,port,demo}.c` — el pad, con la `libpad` de PsyCross (hito 6). En el
  hito 7 pasa a leer de verdad: `PsyX_UpdateInput` (llamado por el shim una vez por cuadro)
  llena el buffer que `PadInitDirect` le dio a `libpad`, y `updatePad`/`readPad`/
  `readPadButtons` lo convierten en los botones lógicos que leen los menús.
- `src/engine/sound/sound.c` — el sonido del juego, con `libsnd` como stub (ver abajo).
- `src/engine/memcard/memcard.c` — la memory card, con la `libmcrd` de PsyCross.
- `src/engine/system/random.c` — los números al azar.

`pc/rlen_tool.c` (nuestro) es el arnés del hito 0: lee el archivo empacado, crea la tarea
del descompresor (`createDecompressor`), llama a su método `run` y escribe los bytes
descomprimidos.

`pc/file_dump.c` (nuestro) es el arnés del hito 1: resuelve un índice por el camino del
juego (`FILE_TABLE` + `FILE_CACHE.load`, que a su vez llama a `CD_READER.read`) y escribe
los sectores que quedaron en el heap.

`pc/text_measure.c` (nuestro) es el arnés del hito 2: pide el lang file por índice de la
tabla de archivos, saca la cadena con `getString` y la mide con `measureText` en estilo 1
(el del banner de área: `createTextWindow(..., 1, 0x28, 0x44)` en
`src/field/field_mode/banner.c`), que a su vez usa `FONT.decode`. Imprime el ancho y los
bytes de la cadena (sus códigos de fuente).

`pc/first_frame.c` (nuestro) es el arnés del hito 4: inicializa PsyCross, arma la tarea del
banner con el `createTask` del juego y llama a su propio `FIELDSTG_updateBanner` una vez por
cuadro, con las ventanas que el banner espera (`createTextWindow`, estilo 1). El detalle está
en "El banner en pantalla (T13)".

`pc/font_frame.c` (nuestro) es el arnés del hito 5: inicializa PsyCross, pide la fuente con
`FONT.load` (el `loadFont` del juego) por el cargador de TIM real, lee la región de VRAM con
`GR_ReadVRAM`, crea una ventana de texto con `createTextWindow` del juego, le pasa una cadena
del lang file con `textWindowSetString` y la dibuja con `textWindowDraw`; camina la *ordering
table* imprimiendo cada `SPRT` con su UV, su tamaño, su CLUT y la página de textura del
`DR_TPAGE` que lo gobierna, y guarda la captura con `glReadPixels`. El detalle está en "La
fuente en VRAM y el texto del juego (T14)".

## `text_shot`: capturas de cadenas concretas

`pc/text_shot.c` dibuja cadenas del juego con su propia fuente y su propio motor de texto y guarda
un PNG por página, para ver cómo queda en pantalla el texto es-419 con códigos poco comunes sin
jugar hasta ese punto de la historia. No es una `TalkBox` completa (esa es un árbol de tareas con
*zoom boxes* y marco): dibuja las dos ventanas de texto que `createTalkBox` arma (nombre del
hablante en estilo 2, cuerpo en estilo 1 de 3 líneas, `PALETTE_DARK_BLUE`) sobre fondo claro, sin
el panel. Una cadena sin hablante (menús, resultados de entrenamiento) sale como ventana de texto
simple. Los saltos de página dan `_p1`, `_p2`... El jugador se llama "Junior" y los códigos de
número (`02 05`) muestran 7.

```
make -C pc build/text_shot
SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1/0x1 \
  pc/build/text_shot "<disco>.bin" es419 build/l6/shots SSTALK00:0:15 SSDMG300:14:2
# o: make -C pc text-shot DISC="<disco>.bin" LANG_SHOT=eng SHOTS="SSTRAINI:0:101"
```

`<archivo>:<parte>:<índice>`: `parte` solo existe en los `SSDMG*` (0 en el resto). `es419` sirve la
cadena como el puerto (archivo `S`, overlay `localization/es-419` por `shim/text_data.c`; se
ejecuta desde la raíz del repo o con `DW3_L10N_DIR`); `eng` lee el archivo `E` del disco. Salida
en `build/` (ignorado por git; nunca se versionan).

### Pestaña Mods (modding-ux U6)

Una pestaña nueva, `CS_TAB_MODS` (la última, así que `options:6` sigue siendo System y el arnés abre la nueva con `options:7`, `options:7.<fila>`):
se eligió pestaña y no subpantalla porque el menú ya sabe mostrar pestañas (la tira es un carrusel) y una subpantalla habría pedido dibujo nuevo en
`controls_draw.c`. Cada mod es una fila de acción (`CsWidget` gana `id`, `applyId`, `textId`, `stepId` para que las filas compartan funciones; Izquierda y
Derecha de una acción llaman `stepId`). `shim/options_mods.c` arma la lista con `modsReportAt` (los activos por posición, luego los apagados, luego los
bloqueados, inválidos y ausentes) y la llama `optionsSystemInit`. Escribe con `inputCfgSetMods` (`[mods] order` y la clave nueva `disabled`; `inputCfgReadModsOrder`
las junta en el texto de `modsInit` como entradas `-nombre`, así `boot.c` no cambió). `inputtest` (`testOptionsMods`) y `configtest` cubren lista, apagar, mover,
escritura y lectura del archivo y que un mod apagado no se carga. Las demás pestañas tienen tope de 14 filas (lo que cabe en el panel).

**Desplazamiento (U6b).** La pestaña Mods guarda hasta 64 mods (`CS_MODS_ROWS`) y se desplaza: muestra una ventana de 14 filas (`csTabWindowTop`) que sigue al cursor, con un triángulo arriba o abajo cuando hay más filas en ese lado; mover un mod más allá del borde de la ventana la arrastra. Un nombre de más de 16 caracteres se corta en la fila y la línea de ayuda lo muestra completo. Las otras pestañas se dibujan igual que antes (captura de System byte a byte igual a la de HEAD).

### Preparar los archivos de modding (modding-ux U4)

Lo pesado (videos, audio, modelos, escenarios) sale con **una** entrada: menú System > *Prepare modding files* o, sin ventana,
`pc/build/boot "<disco>.bin" --export-all` (imprime `export-all: Videos 3/14`... en la salida estándar y devuelve 0, o 1 si algo falló). Los
cuatro exportadores corren uno tras otro (videos, audio, modelos, escenarios), cada uno en su hilo, y saltan lo que ya está al día
(cada tipo de la tabla de exportadores, `shim/exp_types.h`, arranca con `"all"` y salta lo hecho; el menú viejo reconstruía todo). La lógica está en `shim/options_system.c` (`optionsSystemPrepStart`,
`optionsSystemFrame`, `optionsSystemExportAllRun`) y la bandera se saca de `argv` con `inputCfgTakeFlag` (`shim/input_cfg.c`); `inputtest`
cubre los dos. Las claves `[data] export_*` de `settings.ini` siguen igual (avanzadas). Guía para modders: `docs/modding/README.md` §2b.
Medido en el disco EU con una carpeta de usuario nueva: `--export-all` 145 s, 14 videos, 71 bancos de audio, 248 modelos y 11 745 baldosas, código 0;
la segunda corrida 1.5 s.

### Herramientas de mod (modding-ux U5)

Cuatro banderas que corren sin abrir el juego (`shim/mod_tools.c`; `main` de `boot.c` las saca de `argv` con `modToolsTakeArgs`):

- `boot --list-mods`: imprime lo que dejó el último `modsInit` (`modsReportAt`: versión, tipo, activo/deshabilitado/rechazado con el motivo, posición). No pide disco.
- `boot --new-mod <nombre> [--kind <tipo>]`: crea `<pref>/mods/<nombre>/` con `mod.json` y `README.txt` (el texto vive en `mod_tools.c`; `configtest` lo compara con `examples/mods/_template/`) y carpetas vacías. Nunca pisa un mod ni un nombre que difiere solo en mayúsculas.
- `boot [<disco>.bin|.cue] --validate-mod <nombre|ruta>` y `--validate-mods` (sin disco: el que recuerda `settings.ini`, `dpToolDisc`): el arranque normal hasta antes de la ventana (mods, config, contenido, texto), con el stderr capturado (`modToolsCaptureBegin`); después `pcValidateRun` (en `boot.c`) corre `pcRegisterStateScopes` (registra y aplica todas las tablas con los datos del ejecutable, como al jugar) y las capas de imágenes (`imgValidateMods`), audio (`audioModValidate`), video (`videoModValidate`) y baldosas (`stageModValidate`) con **los mismos cargadores** que el juego. `modToolsValidate` convierte las líneas de rechazo del registro en problemas por mod (`error`, `warning`, `skipped`), agrega lo que ningún cargador dice (claves y carpetas ignoradas, tablas de nombre equivocado, `files/` que no están en el disco) y devuelve 0, 1 (errores) o 2 (mod inexistente).
- Sin las banderas el arranque no cambia (`inputtest` y el resto de las pruebas siguen igual). Esquemas JSON: `docs/modding/schema/`. Guía: `docs/modding/README.md` §2c.

`make -C pc configtest` cubre las banderas, `--new-mod`, el listado y el validador sobre mods sintéticos (buen mod, `mod.json` roto, fila de tabla mala, imagen de más tamaño, nombre repetido) con una tabla de prueba registrada en el motor real de tablas.

## Alcance del shim (lo medido)

Compilar el C propio contra las **cabeceras reales de PsyQ 4.7**
(`external/psyq_headers/psyq_lib47/include`) **no requirió ningún stub de cabecera**: las
cabeceras del juego importan bien en el host. Lo único que falta es enlazar (y, para el
hito 1, reemplazar `libcd`).

Símbolos que el C propio deja indefinidos y quién los satisface:

| Símbolo | Quién lo satisface |
| --- | --- |
| `createTask` | el propio `src/engine/task/task.c` (no es shim) |
| `HEAP` | `pc/shim/heap.c` |
| `TASK_REGISTRY` | `pc/shim/task_registry.c` |
| `CD_READER` | `pc/shim/host_cd.c` (reemplazo de `libcd`; el del juego es `cd_reader.c`) |
| `GFX` | `pc/shim/host_gfx.c` (el reloj `GFX.funcs.getTime`); el hito 3 suma los métodos de dibujo en `host_draw.c` |
| `CdIntToPos` | `pc/shim/host_cd.c` (utilidad de `libcd` que usa `getFilePos`) |
| `initTimLoader` | `pc/shim/host_tim.c` (stub, para los arneses sin GPU) o el propio `src/engine/gfx/tim_loader.c` (hito 5, para `font_frame`) |
| `PAD`, `SOUND`, `GAME` | `pc/shim/host_draw.c` (el camino de dibujo los nombra; en cero, nunca se leen) |
| `FIELDSTG_pickStage`, `FIELDSTG_getFileEntry`, `FIELDSTG_getActorWidth`, `FIELDSTG_findBattles` | `pc/first_frame.c` (hito 4): las cuatro funciones que la tabla `FIELDSTG_state` nombra; el banner solo escribe `FIELDSTG_state.frozen`, así que no se llaman |
| `LANGUAGE` | el propio `src/engine/data/eu_settings.c` (no es shim) |
| `GsInitGraph`, `GsInit3D`, `GsGetTimInfo` | `pc/shim/host_gs.c` (hito 6): **libgs**, que PsyCross no trae (ADR-002) |
| `Ss*` (24 funciones) | `pc/shim/host_snd.c` (hito 6): **libsnd**, que PsyCross no trae (ADR-002); es un stub documentado, sin audio |
| `DecDCT*`, `St*` (13 funciones), `StCdIntrFlag` | `pc/shim/host_press.c` (hito 8, T18; real desde T39) y `pc/shim/host_st.c` (T38): **libpress** y la mitad de *streaming* de `libcd`, que PsyCross tampoco trae (ADR-002). El decodificador es `pc/shim/host_mdec.c` (T37): los cuadros se decodifican, se presentan en la VRAM y el presentador (`pc/shim/host_video.c`, T39) los dibuja en la ventana (ver "La pantalla de título y el video") |
| `CdInit`, `CdSetDebug`, `CdControl`, `CdControlB`, `CdRead2` | `pc/shim/host_cd.c` (hitos 6 y 8): las órdenes de la lectora que `main` manda al arrancar y la lectura en *streaming* que arranca el reproductor de video |
| `GFX` + las capas de dibujo | `pc/shim/host_gfx.c` (hitos 0-5, el reloj), `pc/shim/host_draw.c` (hitos 3-5, para los arneses sin GPU) o `pc/shim/gfx_engine.c` (hito 6, el motor del juego) |
| `HEAP.init`, `HEAP.freeByTag`, `HEAP.lock` | `pc/shim/heap.c` (hito 6): el bucle de `main` libera por tag cada vez que muere un modo |
| `PadChkVsync` | `pc/shim/host_pad.c` (hito 7): la `libpad` de PsyCross contesta 0 y el `updatePad` del juego se corta ahí; el enlace de `boot` la envuelve (`-Wl,--wrap=PadChkVsync`), sin tocar el submódulo |

- `pc/shim/heap.c` define la tabla `HEAP` con `alloc`, `allocZeroed`, `free`, `zero`,
  `fill`, y las tres que el arranque del juego necesita: `init` (no hay lista de bloques
  que armar en el host), `freeByTag` (el bucle de `main` la llama con `MEM_MODE` cada vez
  que muere la tarea del modo) y `lock` (el cargador de sonido la llama). El allocador es
  el del host (`malloc`/`free`) con un encabezado de 16 bytes por bloque que guarda el
  tag, que es lo que hace posible `freeByTag`. El heap del juego
  (`src/engine/system/heap.c`) es una lista de bloques entre `HEAP_START` y `HEAP_END`
  (`0x801FF000`), direcciones fijas de PS1: por eso no se compila tal cual.
  Las **asignaciones puestas a cero piden el doble de bytes**, y no es un redondeo: el
  `createTask` del juego (`src/engine/task/task.c`) cuenta los hijos de una tarea como
  `childrenSize / 4`, que es correcto cuando una ranura de hijo mide 4 bytes (el puntero
  de la PS1). En el host mide 8, así que el juego recorre el doble de ranuras de las que
  entran en el arreglo que pidió: el heap le da esa memoria (en cero) para que su propia
  aritmética quede dentro del bloque. Es la misma clase de arreglo del lado del host que
  `ps1_types.h`, y está medido en "El arranque del juego (T15)".
- `pc/shim/task_registry.c` define `TASK_REGISTRY` y sus ocho métodos, con la misma
  comportamiento que documentan `engine/task.h` y `task.c`: una tarea corre su `update`
  una vez por cuadro, `TASK_KILL` la destruye después de su último update, una tarea
  pausada se saltea y los hijos de una viva corren justo después. `registry.c` no se
  puede compilar en el host (cambia la pila al *scratchpad* con ensamblador MIPS), pero
  lo que no es portable es la **pila**, no el comportamiento: el hito 6 lo necesita de
  verdad, porque el bucle de `main` corre el árbol de tareas entero del modo por acá.
- `pc/shim/host_cd.c` define `CD_READER`, la tabla `CdReader` del juego, con dos métodos
  de host: `read` (síncrono) e `isBusy` (siempre 0). El `cd_reader.c` del juego maneja la
  lectora de PS1 con `CdControlF`/`CdReadyCallback` y callbacks de interrupción, que en el
  host no existen; acá los sectores se leen directo de la imagen y la lectura termina antes
  de volver. También aporta `CdIntToPos` (LBA → `CdlLOC`: los 150 sectores de *lead-in* y
  *BCD*), que `file_table.c` usa en `getFilePos`.
- `pc/shim/host_gfx.c` define un `GFX` en cero salvo `GFX.funcs.getTime`, el reloj 8.8 que
  la caché estampa en sus ranuras. El `display.c` del juego maneja la GPU por `libgpu`/`Gs`
  e interrupciones de vsync; en el arnés no hay bucle de frames, así que el reloj se queda
  en 0 (suficiente para ordenar las ranuras).
- `pc/shim/host_tim.c` aporta `initTimLoader` como stub **para los arneses sin GPU**: el
  hito 2 solo usa `FONT` y `decodeChar` de `font.c`, y enlazar el cargador real arrastraría
  la GPU. El stub solo limpia el objeto, así que un `loadFont` en esos binarios explota en un
  método `NULL` en vez de fallar en silencio. El hito 5 compila el cargador **real**
  (`src/engine/gfx/tim_loader.c`) y no enlaza el stub: `font_frame` sube la fuente a la VRAM
  de verdad.
- `src/engine/task/registry.c` **no** se puede compilar en el host: sus updates cambian el
  stack al scratchpad del PS1 con ensamblador MIPS en línea (`SetSpadStack`, `0x1F8003FC`).
- `src/engine/file/cd_reader.c` tampoco: es la lectora de `libcd` (ver arriba).
- `src/engine/gfx/tim_loader.c` tampoco: sus métodos llaman a la GPU (`LoadImage`,
  `DrawSync`).

### El lector de CD del host

Una imagen cruda `.bin` son *frames* de 2352 bytes: 12 de sincronía, 4 de cabecera, 8 de
subcabecera XA, los 2048 de datos (lo que lee el juego, Mode 2 Form 1) y el EDC/ECC. El
lector del host (`pc/shim/host_cd.c`) toma la imagen como **ruta en tiempo de ejecución**
y lee cada sector pidiendo la posición `sector * 2352 + 24`; el mismo *framing* que ya usan
`tools/patch_disc.py` y `tools/extract_disc.py`.

Flags de compilación del host (`pc/Makefile`):

- `-DSKIP_ASM=1` — interruptor que ya trae el upstream (lo usan m2c y el permuter): sin él,
  `include/include_asm.h` emite en **cada** unidad el `__asm__(".include
  \"include/labels.inc\"")` para el ensamblador MIPS, que en el host no existe.
- `-DVERSION_EU=1 -DES419=0` y `-DASM_DIR` — lo que `include/version.h` e `include_asm.h`
  esperan.
- `-idirafter external/psyq_headers/...` — las cabeceras de PsyQ van **después** de las del
  sistema, para que el port gane con `<stdlib.h>`/`<stdio.h>` del host y PsyQ solo resuelva
  lo suyo (`<libgte.h>`, `<libgpu.h>`, `<libcd.h>`).
- Dialecto del build de PS1 que sigue teniendo sentido en el host: `-std=gnu89`,
  `-fno-builtin`, `-fwrapv`, `-fsigned-char`, `-fno-common`, etc.
- `GAME_CFLAGS` es igual a `HOST_CFLAGS` (`-Wall` y nada más). Hasta T6 las unidades del
  juego necesitaban `-Wno-pointer-to-int-cast -Wno-int-to-pointer-cast`, porque
  `file_cache.c` casteaba punteros a `u32`/`s32`. Esos casts ya no están (ver más abajo) y
  el build del host queda sin un solo warning con `-Wall` (salvo `-Wno-pointer-sign` en las
  dos unidades del motor de texto, ver abajo).
- `TEXT_CFLAGS` (= `GAME_CFLAGS`) agrega `-Wno-pointer-sign` **solo** para las dos unidades
  del motor de texto: `text_tools.c` le pasa un `u8 *` al `int strlen(const char *)` que
  declara `game.h`, y GCC 12 lo avisa. Se apaga por unidad para que el resto del juego
  conserve el `-Wall` completo; no se toca `src/`.

## La comprobación, primero el test

Los tres chequeos se escribieron **antes** que su binario, y sin él **fallan** (RED):

```
$ .venv/bin/python pc/check_rlen.py
RED: .../pc/build/rlen_tool does not exist yet; build it with `make -C pc`
$ echo $?
1
```

Los vectores del hito 0 los **genera el propio check** con `pack_rlen` de
`tools/es419/menu_bank.py` (el mismo empaquetador que arma el menú es-419): nada empaquetado
queda en el repo (ADR-001). Cubren vacío, literales, *runs* de 127/128/300, bytes `0x00`
(que también es el terminador) dentro de literales, datos al azar y al azar con runs.

```
$ make -C pc
$ .venv/bin/python pc/check_rlen.py
ok   empty: 0 bytes unpacked
...
ok   random_with_runs: 65688 bytes unpacked
all vectors unpacked byte for byte
```

El hito 1 compara el volcado del juego con una lectura independiente del mismo índice:

```
$ .venv/bin/python pc/check_file.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/file_dump does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_file.py <imagen>.bin
ok   index 7: AAA/DAT/COUNTRY/SPN/SSAMTMAP.BIN 1 sectors at 3895, 1007 bytes
...
all files read byte for byte through the game's own file layer
```

`pc/check_file.py` recorre **100 índices** de la tabla de archivos: los 50 archivos de texto
en sus dos copias que el repo ya tiene nombradas (`tools/es419/gen_table.py`: la copia
española en `7 + 7k` y la copia USA en `2 + 7k`), que cubren desde 1 sector hasta 24. Para
cada uno le pide al disco su LBA y su tamaño con el recorrido ISO 9660 que ya tiene el
repo (`tools/patch_disc.py`) y arma el mismo *framing* (2352 bytes por *frame*, 2048 de
datos al offset 24). **Nada del disco queda en el repo**: la imagen se pasa por ruta y solo
se lee.

El hito 2 mide cadenas de un lang file del disco con el motor de texto del juego y las
compara con una medición independiente:

```
$ .venv/bin/python pc/check_text.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/text_measure does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_text.py <imagen>.bin
ok   SSSTNAME.BIN index 71: 92 px, 18 bytes: 'Desierto Norte O'
...
all strings measured the same through the game's own text engine
```

`pc/check_text.py` recorre **23 cadenas** de tres lang files del disco —el banner de área
(`SSSTNAME`, índice 189) y la etiqueta del mapa (`SSAMTMAP`, 7; `SSASKMAP`, 14)—. Los
índices 30, 31, 71, 72 y 84 (SSSTNAME) y 27 y 29 (los mapas) son los que la regla G-041
reescribió; alrededor van cadenas cortas y largas (el 0 es la cadena vacía, el 14 de los
mapas tiene salto de línea). Para cada índice el check hace dos cosas, y las dos tienen que
dar:

- lee la cadena del mismo disco por su cuenta (misma tabla de offsets) y compara **los
  bytes** con los que imprimió el binario; y
- la mide por su cuenta: saca las tablas europeas de glifos e íconos del propio
  `src/engine/text/text_tools.c` (los bloques `#elif VERSION_EU`) y decodifica la cadena con
  el charmap que ya usan las herramientas del overlay (`tools/es419/lang.py`: `SINGLE`,
  `EXT`, `CONTROL`, con los mismos tokens `{xx}`), y compara **el ancho en píxeles**.

La medición en píxeles de G-041 queda así validada por partida doble con el código del
propio juego: la cadena oficial más larga de esos índices (`'Desierto Norte O'`, 92 px)
coincide con el dato que el glosario ya tenía medido en pantalla, y las cadenas del disco
que el binario mide son las que el Python mide, byte a byte. **Nada del disco queda en el
repo**: la imagen se pasa por ruta; lo único que se imprime son anchos y bytes de paso, que
no se guardan.

### Modo es-419 (T9)

`pc/check_text.py` también puede medir las cadenas contra el disco EU **parchado por
`tools/es419/make_disc.py`** (el overlay es-419 ocupa sectores libres de `DUMMY.`). Se activa
con `--es419-layout <ruta>` y lee el layout que `tools/es419/gen_table.py` escribe bajo
`build/eu/es419/es419_layout.json`. Para cada fila, lee los sectores directamente desde la
imagen y compara bytes y pixeles con el mismo `text_measure` del host. La medición usa la
fila `index` (la ranura USA que `file_table.c` redirige con `ES419=1`); `spanish_index` se
conserva como dato de referencia y no sirve para medir el overlay.

**Artefactos locales necesarios** (se guardan bajo `build/`, ignorado por Git):

- una imagen de disco EU ya parchada: `tools/es419/make_disc.py` la produce a partir de la
  imagen retail del dueño y del layout. No se incluye en el repo (ADR-001).
- un `text_measure` del host compilado con `ES419=1` y con su tabla es-419 generada
  (`build/eu/es419/es419_table.h`), para que la `FILE_CACHE` resuelva los índices del
  layout a los sectores del overlay. Sin esto el host mide la copia USA (inglés) aunque la
  imagen esté parchada.
- el `es419_layout.json` escrito por `gen_table.py --layout`, con `file`, `index`
  (`usa_slot(k)`), `spanish_index`, `lba` y `sectors` por fila.

Para generar y ejecutar la comprobación completa en una máquina con el toolchain:

```sh
DISC="/ruta/a/tu/disco-eu.bin"
python3 tools/extract_disc.py "$DISC" build/t9-eu
make ES419=1 ES419_DISC=build/t9-eu DISC="$DISC" es419-disc
make -C pc clean
make -C pc ES419=1 build/text_measure
python3 pc/check_text.py build/eu/es419/disc/DW2003_ES419.bin \
  --es419-layout build/eu/es419/es419_layout.json
```

Si se pasa `--es419-layout` y el archivo no existe o es inválido, el comando falla con un
mensaje de error. El modo EU sigue siendo el predeterminado cuando no se pasa la opción.
La comprobación es-419 se verificó contra una copia local con los textos en `DUMMY.`; los
artefactos y la imagen quedan en `build/` y no se incluyen en el repo.

La lógica que **sí** valida esta tarea vive en `pc/test_check_text.py`: parseo del layout,
validación de filas (campos requeridos, `sectors > 0`, archivos del set es-419, `spanish_index`
y `usa_slot` que coincidan con `gen_table`), lectura LBA-sector desde cualquier objeto *image*,
asociación `case -> fila del layout` e inclusión del directorio de la tabla al compilar con
`ES419=1`. El comando canónico es `.venv/bin/python -m unittest pc.test_check_text`.

## PsyCross en el host (T11)

PsyCross (`external/psycross`, submódulo, MIT — Copyright REDRIVER2 Project) es la capa que
reemplaza a las bibliotecas de Psy-Q (ADR-002). Su propio `CMakeLists.txt` **no configura** en
Linux, y ninguno de los dos motivos es una dependencia que falte:

1. llama `find_package(OPENAL)`, y CMake distingue mayúsculas: el sistema trae
   `OpenAL`/`FindOpenAL.cmake`, así que la forma que funciona es `find_package(OpenAL)`;
2. usa `file(GLOB_RECURSE ... "*.c")`, que en un sistema de archivos sensible a mayúsculas
   **no** ve los once archivos `.C` con las implementaciones de Psy-Q (`LIBGPU.C`, `LIBCD.C`,
   `LIBSPU.C`, `LIBGTE.C`, `LIBPAD.C`, `LIBETC.C`, `LIBAPI.C`, `LIBMCRD.C`, `LIBSN.C`,
   `ABS.C`, `INLINE_C.C`). En Windows el glob los tomaba; en Linux la biblioteca saldría **sin**
   sus implementaciones.

No se parchea el submódulo (ADR-003): el build es nuestro, `pc/psycross/CMakeLists.txt`, lista
las 21 fuentes a mano y compila el submódulo **sin tocarlo**. El resultado es
`build/psycross/libpsycross.a` (508 KB, los 21 objetos).

```sh
make -C pc psycross          # o: cmake -S pc/psycross -B build/psycross && cmake --build build/psycross
make -C pc check-packets     # o: build/psycross/check_packets
```

### Idioma por archivo: qué se compila como C y qué como C++

La extensión `.C` (mayúscula), por convención y para el driver de gcc, es **C++**; el build
original de PsyCross marcaba `LANGUAGE C` a todo lo que su glob tomaba. Se sigue esa intención
—C, salvo donde el frente de C del host rechaza el archivo— y se decide por grupo:

| Grupo | Idioma | Por qué |
| --- | --- | --- |
| `*.c` (`glad.c`, `timer.c`) | C11 | son C. |
| `*.cpp` (8 unidades) | C++17 | su propio CMake las compila como C++. |
| `.C` menos los tres de abajo | C11 | `LANGUAGE C` del build original; además `-x c`, porque el driver de gcc elegiría C++ por la extensión. |
| `LIBGPU.C`, `LIBGTE.C` | C++17 | llegan a `psx/libgpu.h` antes de `<assert.h>`, así que el `static_assert` de alcance de archivo es un identificador desconocido para C; en C++ es palabra clave. |
| `LIBAPI.C` | C++17 | C rechaza sus definiciones de `LoadTest`/`Load`: la cabecera las declara con un `struct EXEC *` cuyo tag queda en el alcance del prototipo, y C++ lo declara en el alcance del archivo. |

Dos interruptores de compilador, ninguno un parche —van en nuestro `CMakeLists.txt`, no en las
fuentes—:

- `-Wno-narrowing` para el inicializador `{ -1, 0 }` de `PsyX_GPU.cpp`: con el `P_TAG` de
  `uintptr_t` es una conversión que C++11 declara *ill-formed* y gcc rechaza, y que MSVC/clang
  solo avisan.
- `-idirafter` para `external/psycross/include/psx`: PsyCross trae su propio `psx/strings.h`,
  que con un `-I` normal tapa el `<strings.h>` de glibc y rompe su propio `PsyX_main.cpp`
  (`strcasecmp` no declarado). Con `-idirafter` el directorio queda **después** de las
  cabeceras del sistema y conviven.

### Interfaz para el consumidor

La biblioteca expone como públicos los dos directorios de cabeceras que un consumidor
necesita: `external/psycross/include` (para `<PsyX/...>`) y `external/psycross/include/psx`
(para `<libgpu.h>`, `<libgte.h>`, ...). El segundo llega por `-idirafter`, así que un
consumidor también puede escribir `<strings.h>` y recibir el del sistema.

### La comprobación: `pc/psycross/check_packets.c`

Es un consumidor C11 de las cabeceras. Afirma en tiempo de compilación, con `static_assert`,
que los tamaños de paquete son los de PsyCross en el host y no los de Psy-Q 4.7 (que daría
`P_TAG` 8 y `POLY_F3` 24):

```
$ build/psycross/check_packets
sizeof(P_TAG)=16 sizeof(OT_TAG)=12 sizeof(POLY_F3)=28 P_LEN=3
ok: PsyCross host packet layout
```

Además llama a `fst_abs`, que vive en `src/psx/ABS.C` (compilada como C), así que el enlace
contra la biblioteca estática se ejerce de verdad; y un enlace con `--whole-archive`, que
arrastra los 21 objetos, cierra sin símbolos indefinidos en este host.

## El dibujo del juego en el host (T12)

El hito 3 saca a la luz la primera unidad del juego que **dibuja**: la ventana de texto
(`src/engine/text/text_window.c`, la del camino de los menús), sin modificar, compilada
para x86-64 **contra las cabeceras de PsyCross**. Es la prueba de que el código de GPU del
juego entra en el port: sus glifos salen como paquetes `SPRT`/`POLY_FT4` y se enlazan a una
*ordering table* con el `addPrim` de PsyCross.

### Qué hizo falta para compilarla

- **Dialecto: `gnu11`** (no el `gnu89` del resto de las unidades del juego). PsyCross
  declara `static_assert` a nivel de archivo en `libgpu.h`, y esa macro existe recién desde
  C11. El build de PS1 y las otras unidades del host siguen en `gnu89`; `text_window.o` es
  la única en C11.
- **`<assert.h>` antes de `libgpu.h`**: la versión fijada del submódulo todavía **no** trae
  el `#include <assert.h>` que el PR de PsyCross agrega para C, así que el arnés lo fuerza
  con `-include pc/psx_compat.h` (antes de la primera línea de `text_window.c`, que es la
  que incluye `game.h`).
- **`pc/psx_compat.h`** (nuestro): el puente entre los nombres del juego y los de PsyCross.
  Son tres diferencias medidas, ninguna en el código del juego ni en el submódulo:
  `ONE` (`libgte.h` de PsyQ lo define como 4096; el de PsyCross no), `RECT` (PsyCross lo
  llama `RECT16`, mismos cuatro `short`) y `RotMatrixYXZ_gte` (PsyQ declara las dos formas;
  PsyCross solo la sin `_gte`, que es la misma rotación por los registros del GTE).
- **`include/psx` con `-idirafter`**: la misma trampa de T11 (el `psx/strings.h` de PsyQ
  taparía el de glibc).

### El arnés (`pc/draw_order.c`) y el layout medido

`draw_order <disco.bin> <índice de archivo> <índice de cadena>` carga el lang file por la
capa de archivos del juego (hito 1), crea una ventana de texto con su propio
`createTextWindow`, le pasa la cadena por su `textWindowSetString` (que es el camino de los
menús: `sjis = 0`) y la dibuja con `textWindowDraw`. La ventana escribe `SPRT` por glifo y
`DR_TPAGE` por glifo, más un `DR_TPAGE` cuando cambia la *texture page* del glifo y uno al
final; todo va a la *ordering table* y al *packet buffer* del shim (`pc/shim/host_draw.c`),
que devuelve bytes y las entradas de la OT.

**Nada de esto abre ventana ni inicializa el renderer**: no se llama a `PsyX_Initialise`, ni a
SDL, ni a X11. La librería dinámica queda enlazada (los objetos de `libpsycross.a` que el
`SetDrawTPage` y el GTE arrastran), pero el arnés no ejecuta ninguno; corre con `DISPLAY`
sin definir y con drivers de SDL falsos.

### La comprobación (`pc/check_draw.py`)

Recorre las mismas cadenas que el motor de texto y para cada una verifica el resultado
**estructuralmente**, sin renderer:

- los tamaños de paquete son los de PsyCross en el host y **no** los del retail 4.7: el
  arnés imprime `sizeof(P_TAG)`, `sizeof(SPRT)`, `sizeof(POLY_FT4)`, `sizeof(DR_TPAGE)` y
  `P_LEN`, y el check los compara con `16 / 28 / 48 / 16 / 3` (el layout retail daría
  `8 / 20 / … / 1`);
- el tamaño de cada primitivo es `(largo + P_LEN) * 4`, su `(code, largo)` es uno de los que
  este camino escribe (`0x64` SPRT, `0x2C` POLY_FT4, `0xE1` DR_TPAGE), y la suma coincide con
  los bytes usados del *packet buffer* (la cadena es contigua);
- la **cantidad y el tipo** son los que el propio `textWindowDraw` produce para esa cadena,
  recalculados en Python desde los bytes de la cadena y la tabla de glifos europeos sacada
  de `src/engine/text/text_tools.c`: un `SPRT` por carácter dibujado y un `DR_TPAGE` por
  carácter más los cambios de *texture page* y el final.

Ejemplo de la corrida (`SSSTNAME` índice 189, cadena 71, `'Desierto Norte O'`):

```sh
$ .venv/bin/python pc/check_draw.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/draw_order does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_draw.py <imagen>.bin
ok   SSSTNAME.BIN index 71: 38 prims (16 spr, 0 ft4, 22 tpage), 800 bytes
...
all draw orders built with the game's own drawing code and PsyCross's host layout
```

La cadena `71` arma **38 primitivos**: 16 `SPRT` (los 16 caracteres dibujados de la cadena)
y 22 `DR_TPAGE` (16 + los 4 cambios de página entre glifos + 1 inicial contra la página 0
+ 1 final), 800 bytes en total, con la cadena de la OT contigua. La medición coincide con
la del modelo independiente en las 23 cadenas.

## El banner en pantalla (T13)

El hito 4 es la **primera imagen**: `pc/build/first_frame` abre una ventana de PsyCross
(`PsyX_Initialise`, 320x240), y en cada cuadro llama al **banner de área del juego** y
presenta con `PsyX_BeginScene` / `DrawOTag` / `PsyX_EndScene`. Lo que se ve es el banner tal
como el juego lo arma: el mismo que aparece al entrar a una zona en la PlayStation.

```sh
$ make -C pc first-frame
...
frame 000: state=1 substate=0 bytes=0 quads:
frame 001: state=1 substate=0 bytes=64 quads: 3E3EC8(312,40 8x3) 00E4FF(312,41 8x1)
...
frame 069: state=1 substate=4 bytes=320 quads: 000080(0,66 320x31) 000080(125,16 116x31) ...
presented 300 frames, 2779 quads drawn by the game's own banner code
last frame: state=1 substate=4
```

Los cuadros son **para mirar**: por cada uno se imprime el estado de la máquina del banner
(`state`/`substate`) y **cada cuadrilátero** con su color y su rectángulo. Así se ve la
secuencia real: la caja de arriba y la de abajo entran desde el borde (cuadros 1-67), después
aparecen las demás y el cuadro 68 llega al estado quieto con las **diez** cajas (320 bytes).
Los colores salen de `FIELDSTG_bannerBoxes` (cian `0xFFE400`, azul `0xC83E3E`, azul oscuro
`0x800000`, leídos tal como la PS1 los tiene: el juego escribe el color con
`*(s32 *)&poly->r0 = color` sobre little-endian).

### Cómo lo arma (y qué NO dibuja)

- El arnés crea la tarea con el `createTask` del propio juego y llama a su
  `FIELDSTG_updateBanner` una vez por cuadro: la copia de `FIELDSTG_bannerBoxes`, el estirado
  (`FIELDSTG_stretchBannerBox`) y el dibujo (`FIELDSTG_drawBannerBox`) son **código del
  juego, sin modificar**. El shim (`pc/shim/host_draw.c`) solo aporta la *ordering table*, el
  búfer de paquetes y `GFX.funcs`, y PsyCross solo presenta.
- El banner real abre además **dos ventanas de texto** (el nombre del área y el del lugar) en
  `FIELDSTG_showAreaName`; esas necesitan el disco y una fuente en VRAM, que es el paso
  siguiente y no entra acá. Para que la máquina de estados del banner corra igual, el arnés
  hace que `GAME.funcs.getMode` no reporte ninguna zona: `FIELDSTG_showAreaName` no encuentra
  área, no crea ninguna ventana ni toca el disco, y las dos `TextWindow` que el arnés le pasa
  al banner (creadas con el `createTextWindow` del juego) quedan válidas y sin terminar, así
  que el banner recorre toda su animación y se queda quieto en pantalla.
- **Sin disco y sin VRAM**: el banner son diez `POLY_F4`, cuadriláteros planos de color, sin
  textura. Por eso es el primer objetivo correcto.
- Con `pc/build/first_frame <cuadros> screenshot` el arnés guarda además `first_frame.ppm`
  (un `glReadPixels` del cuadro presentado) en el directorio actual. Es la forma de sacar una
  captura sin depender de herramientas de X: acá `xwd`/`x11grab` devuelven la ventana en
  negro porque el búfer frontal es de *direct rendering*.
- PsyCross escribe siempre su propio `<nombre de la app>.log` en el directorio actual
  (`dw3_pc_spike.log`): `pc/.gitignore` lo ignora, igual que el `.ppm` del punto anterior.

### `VERTTYPE`: lo que costó que se viera (hallazgo)

La primera corrida abría la ventana y no dibujaba nada, y el error no daba ningún aviso: la
geometría llegaba degenerada. La causa es de tipos, no de la máquina:

- PsyCross guarda las coordenadas de los primitivos en `VERTTYPE`. Con **PGXP** encendido, en
  C++ `VERTTYPE` es un **float de 16 bits** (`half`, `pgxp_defs.h`), y su propio comentario lo
  dice: "use `_HF` to convert to half whenever you in C mode". Un juego en C++ (el objetivo
  original de PsyCross, REDRIVER2) escribe `half` y todo cierra.
- Este port compila el **C del juego tal cual**, y ese C escribe la coordenada entera de la
  PS1 (`poly->x0 = pos.vx`, un `s16`) directamente en el primitivo. Con PGXP encendido el
  parser lee esos bits como `half` → todas las coordenadas quedan ≈ 0 → triángulos de área
  cero → nada en pantalla (y ningún error de OpenGL).
- Arreglo: la librería se construye con **`USE_PGXP=0`** (lo fija nuestro
  `pc/psycross/CMakeLists.txt`, y `pc/Makefile` usa el mismo define para que las dos partes
  coincidan). Ahí `VERTTYPE` es `short` en C y en C++ (`pgxp_defs.h`'s `#else`), y el parser
  lee exactamente lo que el juego escribe. **No cambia el *layout* de los paquetes**:
  `P_TAG` 16, `OT_TAG` 12, `P_LEN` 3 (el `check-packets` sigue verde) y las coordenadas
  siguen midiendo 2 bytes. Lo que se pierde es la mejora de precisión PGXP, que necesita que
  el lado del juego escriba `_HF()`; si algún día se quiere, es un cambio del código de
  dibujo del juego (edición en `src/`), no de la librería.
- Consecuencia práctica: cambiar `GAME_DEFS` en `pc/Makefile` exige `make -C pc clean` (make
  no recompila por un flag distinto).

## La fuente en VRAM y el texto del juego (T14)

El hito 5 es el paso donde aparecen **letras del juego**. Tres cosas, todas con el C propio:

1. **La fuente, del disco a la VRAM, por el cargador del juego.** `FONT.load` es el
   `loadFont` de `src/engine/text/font.c`: arma un `TimLoader`, le pide a la caché el
   archivo `FILE_FONT` (0x287, que en el disco EU es `AAA/DAT/COUNTRY/CMFONTTM.BIN`) y
   llama a `loadArchive`. `timLoaderLoadArchive` recorre el archivo (una tabla de offsets
   con dos TIMs), descomprime los RLEN y `timLoaderLoad` sube cada imagen con
   `LoadImage`/`DrawSync` de `libgpu` (PsyCross). `loadFont` pide la imagen en
   `(0x140, 0)` = (320, 0) y el loader mueve cada TIM siguiente `0x40` medias palabras a la
   derecha: los dos TIMs quedan en **(320, 0) y (384, 0)**, de `0x40` medias palabras de
   ancho por `0x100` filas (32768 bytes cada uno; 65536 en total). Eso es exactamente la
   región que `getTPage(0, 1, texX + (page << 6), texY)` del `textWindowDraw` direcciona:
   la página 0 es el primer TIM y la 1 el segundo.
2. **El texto, de un lang file del disco.** El arnés crea una ventana con el
   `createTextWindow` del juego en **estilo 1** (el del banner de área, líneas de 14 px) en
   `(0x28, 0x44)` —la posición del nombre del lugar en `FIELDSTG_showAreaName`—, le pasa la
   cadena con `textWindowSetString` (que usa el `getString` del motor de texto) y la dibuja
   con `textWindowDraw`. La cadena es la 71 del `SSSTNAME` español del disco.
3. **La captura.** `glReadPixels` del cuadro presentado, escrita como PNG por el propio
   arnés (`pc/font_frame.c`, sin bibliotecas: el *stream* zlib usa bloques deflate
   *stored*). Otra vez, `xwd`/`x11grab` dan la ventana en negro por el *direct rendering*.

Lo que se ve:

```sh
$ make -C pc font-frame DISC="<imagen>.bin"
...
vram 320,0 64x256 (32768 bytes) sum=5bba8b36
vram 384,0 64x256 (32768 bytes) sum=60248926
vram region 320,0 128x256 (65536 bytes) sum=38532c55
 glyph 0 u=32 v=30 w=8 h=12 clut=14871 tpage=38 at 126,68
...
frame 299: 16 glyphs, 800 bytes
presented 300 frames, 4800 glyph sprites drawn by the game's own text window
screenshot: /…/build/t14_font_frame.png
```

Una ventana negra de 320x240 con **`Desierto Norte O`** en blanco, dibujada en la fuente del
propio juego (8x12 por glifo, y 4x12 los angostos). Es la cadena 71 del `SSSTNAME` español,
la misma que el hito 2 mide en 92 px.

### La comprobación (`pc/check_font.py`)

Todo lo que el arnés imprime se compara contra una lectura independiente del mismo disco y
del mismo C:

- **La VRAM:** el check resuelve `AAA/DAT/COUNTRY/CMFONTTM.BIN` en la imagen, lee la tabla
  del archivo, descomprime los dos RLEN (el mismo algoritmo: `n < 0x80` copia `n` bytes,
  `n | 0x80` repite el siguiente `n` veces, un 0 termina) y parsea los bloques de cada TIM.
  Después compara la **FNV-1a de cada bloque de imagen** con la que el arnés leyó de la VRAM
  con `GR_ReadVRAM`, en la dirección y el tamaño que `loadFont`/`timLoaderLoadArchive`
  eligen. También comprueba que el archivo que el juego cargó como `FILE_FONT` es el mismo
  (los primeros 32 bytes y la cantidad de sectores).
- **Los primitivos:** el arnés imprime cada `SPRT` con su UV, su tamaño, su CLUT y la página
  de textura del `DR_TPAGE` que lo gobierna (los `DR_TPAGE` se agregan *antes* que su sprite
  en la cadena que `DrawOTag` recorre, así que el que vale es el último visto). El check
  recalcula esos seis números desde los bytes de la cadena y la tabla de glifos europea
  sacada de `text_tools.c`: `u`/`v`/`w`/`h` de la tabla, `getClut(0x140 + clutX, 0 + clutY)`
  y `getTPage(0, 1, 0x140 + (page << 6), 0)`. Los 16 glifos de la cadena coinciden.

```sh
$ .venv/bin/python pc/check_font.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/font_frame does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_font.py <imagen>.bin
ok   the game loads AAA/DAT/COUNTRY/CMFONTTM.BIN as file 647, 22 sectors, byte for byte
ok   VRAM 320,0 64x256 (32768 bytes) is TIM 0 byte for byte
ok   VRAM 384,0 64x256 (32768 bytes) is TIM 1 byte for byte
ok   VRAM 320,0 128x256 (65536 bytes) is the font's two TIMs
ok   16 glyph sprites per frame reference the glyph table (first: u=32 v=30 w=8 h=12 clut=14871 tpage=38)
the font's TIMs are in VRAM with the disc's own bytes and the game's own text window draws the glyph table's UV and CLUT
```

### `u_long`: lo que costó que la fuente llegara a la VRAM (hallazgo)

La primera corrida **no puso nada en la VRAM**: las dos regiones volvían con 32768 bytes en
cero cada una, y ningún error. La causa es de ancho de tipo, y es el hueco que T4 había
anotado como el número uno:

- En la PS1 `u_long` mide 32 bits; en el host mide 64, porque tanto `<sys/types.h>` como el
  `psx/types.h` de PsyCross lo escriben `unsigned long`. El `TimCursor` de
  `include/engine/gfx.h` camina las palabras de un TIM con un `u_long *`, así que en el host
  el cursor avanza **8 bytes** en lugar de 4: lee la *flag* del TIM en la mitad equivocada,
  calcula un tamaño de imagen absurdo y `LoadImage` no copia nada (acá, además, sin
  romperse: de ahí que el síntoma fuera "0 bytes" y no un *crash*).
- Arreglo: `pc/ps1_types.h`, que se fuerza (`-include`) **antes** de `pc/psx_compat.h` solo
  para `tim_loader.o`; hace que `u_long` signifique ahí lo que significa en la PS1. No se
  toca `src/**` (ADR-003) ni el submódulo: es el host el que se acomoda, como el resto de
  `pc/`. Las otras unidades de dibujo no lo necesitan y quedan como estaban.
- Segunda consecuencia: el `LoadImage` de PsyCross declara `u_long *`, así que con el cambio
  la llamada del juego pasa `u_int *`. No rompe el enlace porque `libgpu.h` envuelve sus
  declaraciones en `extern "C"` (`LoadImage` queda sin *mangle*); solo pide compilar con las
  dos cabeceras en el orden correcto.

### Por qué el TIM dice "16 bits" y la fuente es de 4

Los dos TIMs de `CMFONTTM.BIN` declaran `mode = 2` (16 bits) y un bloque de imagen de
`0x40 x 0x100` medias palabras. No es un error del archivo: es un truco del dato. El juego
nunca usa el `mode` del TIM (su loader lo lee solo para el bloque de CLUT, que estos TIMs no
tienen); lo que sí usa son `w` y `h` para decirle a `LoadImage` cuántas **medias palabras** por
fila copiar. Con `w = 0x40`, cada TIM llena exactamente una página de textura de 4 bits
(256 texeles de ancho, que es lo que `getTPage(0, ...)` pide) y `u` puede ir de 0 a 248 como
en la tabla de glifos. La paleta no viene en un bloque de CLUT: está **dentro de la misma
imagen**, en la zona que `getClut(0x140 + 48, 232)` direcciona (`clutX = 48`, `clutY = 232`
en toda la tabla europea), así que subir la imagen es subir la paleta.

## El arranque del juego y la selección de país (T15)

El hito 6 es el primero que corre **el juego**, no una unidad suelta: `make -C pc boot
DISC="<imagen>.bin"` corre el `main()` del juego (`src/engine/system/main.c`, sin
modificar) en el host. `main` setea el modo de video, resetea el gráfico, sube la imagen de
arranque, arranca heap, sonido, pad, memcard, el estado del juego y la fuente, y entra en su
bucle de un cuadro por iteración: tarea del modo, `drawFrame`, pad, azar, caché de archivos
y sonido. En la versión europea el primer modo del juego es la **selección de país/idioma**,
que se queda ahí dibujándose con su propio código mientras no se toque el pad. (Desde el hito 7
el pad se lee de verdad y esa pantalla responde: ver "El input del juego (T17)"; esta corrida es
la de T15, sin teclas.)

**Qué se ve** (la captura es de la propia corrida, `glReadPixels` antes del
`PsyX_EndScene`):

- el fondo del menú, una grilla azul oscura que **se desplaza en diagonal**;
- el rótulo superior que titula la pantalla (un cartel grande arriba);
- los tres paneles que **se abren solos** (los primeros ~31 cuadros, con un *tween* de
  escala: son 77 `poly`, o sea el camino escalado del dibujante con el GTE, y quedan
  abiertos a partir del cuadro 32, 142 `sprite` por cuadro, sólo sprites);
- las cinco opciones de idioma con su bandera y su nombre — la primera fila de la lista
  resaltada y parpadeando (ocho filas de CLUT, cuatro cuadros cada una), seguidas de las
  otras cuatro opciones que el menú expone en ese orden;
- el panel de la izquierda, con su propio CLUT (la paleta alternativa que el dibujante usa
  sólo para ese panel: `setAltClut`).

```sh
$ make -C pc boot DISC="<imagen>.bin"
boot: the game's own main(), 320x240 window, 300 frames
boot: capture goes to /…/build/t15_country_select.png
boot: mode 0x1600 (country select)
gfx: two packet buffers of 40960 bytes for the mode
gfx: layer id 0x100, 320x240 at 0,0, OT 8 entries
frame 000: layer 0x100 prims=0 (sprite=0 poly=0 env=0 other=0) bytes=0
frame 001: layer 0x100 prims=141 (sprite=63 poly=77 env=1 other=0) bytes=5476
…
frame 032: layer 0x100 prims=148 (sprite=142 poly=0 env=6 other=0) bytes=4072
…
presented 300 frames, 44085 primitives drawn by the game's own mode code
screenshot: /…/build/t15_country_select.png
```

Los primitivos se cuentan por su **tipo** (`code & 0xF0`, como los lee el parser de
PsyCross: los bits bajos de un primitivo —semitransparencia, textura *raw*— no cambian lo
que es): `sprite` son los `SPRT`/`TILE`, `poly` los planos (`POLY_F4`, `POLY_FT4`), `env`
los `DR_ENV`/`DR_TPAGE`, y `bytes` es lo que el búfer de paquetes usó. Cuadra con el
*layout* medido: 142 × 28 + 6 × 16 = 4072.

La corrida dura unos 6,5 s (300 cuadros a la cadencia del vsync de PsyCross) y **se cierra
sola**: el `for (;;)` de `main` no vuelve nunca, así que el fin de cuadro del host cuenta los
cuadros presentados y, al llegar al pedido, guarda la captura y sale.

### El arte viene del disco, y de dónde sale cada cosa

La pantalla pide su arte con `FILE_CACHE.getEntry(CNTY_SEL_SPRITES)` y
`(CNTY_SEL_IMAGES)`: son las dos entradas (el *sprite bank* y el archivo de TIMs que va a la
VRAM) del **archivo 0x8A3** del disco, que es `AAA/DAT/COUNTRY/CNTSELWN.BIN` (9 sectores;
leído por la capa del hito 1 son **16.416 bytes idénticos al archivo del disco**, comparados
byte a byte). Los TIMs van a la VRAM desde (640, 0) con los CLUTs desde (0, 496) por el
cargador de TIM del juego (el del hito 5), y los sprites salen de ahí.

Ojo con un nombre: **`FILE_MENU_SPRITES` (0x286) no participa de esta pantalla**. Lo único
que lo nombra es `FONT.load` (que lo *encola*, no lo lee ni lo sube). Esta pantalla usa su
propio archivo (0x8A3). Y **no usa el motor de texto**: sus rótulos (el titular de la
pantalla y la etiqueta de cada idioma) son parte del arte del *sprite bank*, no glifos de
`FONT`. El motor de texto del juego se ve en el hito 5.

### Cómo despacha el port a la selección de país

En la PS1 cada modo es un **overlay**: MIPS que el ejecutable copia a una dirección fija
(`OVERLAY_ADDRESS`) y al que salta. Eso no se puede ejecutar en el host, así que el port
compila el C de cada modo **dentro del binario** y despacha por su propia tabla de puntos de
entrada:

- `main` llama a `createModeTask()`; la del juego es `system/overlay.c`, que copia
  `CNTY_SEL.PRO` a la memoria fija y busca el punto de entrada en `MODE_ENTRY_POINTS`. Ese
  archivo **no es parte del camino del host**: `pc/boot.c` define su propio `createModeTask`
  y su propia tabla, indexada igual (`GAME.funcs.getMode() >> 8`), con **sólo** la selección
  de país adentro (`MODE_COUNTRY_SELECT` = 0x1600, índice 0x16 = 22, el mismo lugar donde la
  tiene el juego). Cualquier otro modo dice que este binario no lo tiene y se detiene, en vez
  de saltar a ningún lado. Los `.PRO` no se leen nunca.
- `OVERLAY_VRAM` y `STAGE_VRAM` (los dos símbolos del mapa de memoria que `main.c` convierte
  en `OVERLAY_ADDRESS`/`SUB_OVERLAY_ADDRESS`) son buffers del host, del tamaño de cualquier
  overlay; no se copia nada ahí. Es la consecuencia de diseño de la tarea: los modos se
  enlazan, no se cargan.

### Los shims que hicieron falta

- **libgs** (`pc/shim/host_gs.c`): `GsInitGraph`, `GsInit3D` y `GsGetTimInfo`.
  `main` usa la tercera para la **imagen de arranque**: en la PS1, el ejecutable está seguido
  de un TIM de 320x480 que se sube con `LoadImage` antes de todo lo demás. En el host esa
  zona no existe (es un buffer en cero, no un TIM), así que el shim la reporta vacía: el
  `LoadImage` del propio juego borra ese bloque de VRAM en vez de subir un logo. Nada se
  inventa.
- **libsnd** (`pc/shim/host_snd.c`): **un stub documentado, sin audio**. Las 24 funciones
  `Ss*` que el sonido del juego llama no hacen nada, y las que devuelven algo lo dicen
  honestamente (un *key-on* devuelve `-1`, "no hay voz"; un banco devuelve un id válido y su
  transferencia "termina" al instante). Lo que **sí corre** es `sound.c` del juego: el
  cargador de bancos lee los VAB del disco del dueño con la caché de archivos (su máquina de
  estados, los cuatro estados, las ranuras, el `isLoading` que la pantalla espera) y el
  `playSound` de la música. O sea: el juego *cree* que hay sonido; no hay.
- **El motor gráfico y las capas** (`pc/shim/gfx_engine.c`): el `GFX` y los `Layer`. Son el
  reemplazo de `display.c` y `layer.c`, que sí compilan para el host pero **no se pueden
  usar tal cual**:
  - `layer.c` arma cada *ordering table* como `otLen * sizeof(u_long)` y la recorre como
    `u_long *`. En la PS1 las dos cosas miden 4 bytes; en el host la entrada de la OT es el
    `OT_TAG` de PsyCross (12 bytes: dirección de 8 más el largo) y `sizeof(u_long)` es 8. La
    tabla quedaría corta y se leería con el paso equivocado. `gfx_engine.c` la arma con
    `OT_TAG` y la indexa como `OT_TAG` (es el **tercer** caso del mismo tenedor `u_long` que
    T14 encontró en el cargador de TIMs, un nivel más arriba).
  - `display.c` termina su `drawFrame` esperando la **interrupción de vsync** que da vuelta
    el buffer (`while (FLIP_PENDING) {}`); en PsyCross el que presenta es `PsyX_EndScene`
    (T10: "nadie más lo llama") y su cuadro es un solo buffer de ventana, no las dos zonas de
    VRAM de la PS1 (la de `y` y la de `y+256`). Así que el host presenta una vez por cuadro
    del bucle de `main`, como los hitos 4 y 5.
  - lo que **es** del juego acá es el dibujo: los sprites los arma `sprite_drawer.c`, los
    TIMs los sube `tim_loader.c`, las llamadas son `addPrim`/`DrawOTag` sobre la OT de
    PsyCross y el árbol de tareas es el de `task.c`.
- **El registro de tareas** (`pc/shim/task_registry.c`): el bucle de `main` corre todo el
  árbol del modo por acá, así que dejó de ser una tabla vacía y pasa a tener los ocho
  métodos, con el comportamiento que documentan `engine/task.h` y `task.c`. `registry.c` no
  se puede compilar en el host (su update cambia la pila al *scratchpad* con asm de MIPS),
  pero lo que no es portable es la pila, no la semántica.
- **`HEAP.lock` / `HEAP.init` / `HEAP.freeByTag`**: los tres que el arranque llama y los hitos
  anteriores no (ver "Alcance del shim").
- **La captura** (`pc/shim/host_shot.c`) es ahora compartida con el hito 5: el mismo PNG de
  `glReadPixels` que ya usaba `font_frame`.

Dos mundos de cabeceras conviven en este binario: las unidades de motor (archivos, tareas,
estado, pad, sonido, memcard, azar, y también `main.c`) siguen con las cabeceras **retail de
Psy-Q 4.7** y los flags que verificaron los hitos anteriores; las de dibujo (la pantalla, el
dibujante de sprites, el cargador de TIM, el motor gráfico y el arnés) van con las de
**PsyCross**, como los hitos 3 a 5. Eso es posible porque las estructuras que se pasan entre
los dos lados miden lo mismo con las dos: medido en este host, `GfxState` 608 bytes (con
`funcs` en 0x1E0), `GameState` 10176 (el modo en 0x26C4), `PadState` 1136, `SoundState` 17104,
`FileCache` 1640, `Task` 128 y `Heap` 104, **idénticas** en ambos juegos de cabeceras. Lo que
sí difiere (`DRAWENV` 160 contra 101, `Layer` 608 contra 488, `P_TAG` 8 contra 16) sólo lo
usan las unidades compiladas con PsyCross.

### Dos trampas más de la PS1 al host (medidas)

1. **La ranura de hijo de una tarea.** `createTask` (`src/engine/task/task.c`) calcula la
   cantidad de hijos de una tarea como `childrenSize / 4`, que es correcto cuando una ranura
   mide 4 bytes (el puntero de la PS1). En el host mide 8, así que el juego cuenta el **doble**
   de ranuras de las que entran en el arreglo: el bucle de tareas leía (y escribía) una
   ranura más allá del bloque, con basura adentro. `task.c` es del juego (`src/`) y no se
   toca, así que el arreglo es del lado del host: el heap le da a cada asignación *puesta a
   cero* el **doble** de bytes, en cero, que es exactamente lo que la aritmética del juego
   direcciona; las ranuras de más se leen como los NULL que el bucle saltea. El costo es un
   factor 2 en esas asignaciones (tareas, sus arreglos de hijos, algún buffer), no en el
   resto. (El `overlay.c` del juego, además, pide la ranura del modo con un `4` a mano: en el
   `createModeTask` del port esa ranura mide `sizeof(Task *)`.)
2. **El `DR_ENV` de la capa dentro de la OT.** El `layerDraw` del juego pone el ambiente de
   dibujo de la capa como un primitivo `DR_ENV` a la cabeza de la *ordering table*. Con los
   punteros extendidos de PsyCross, el primer `DR_AREA` de ese `DR_ENV` cae sobre el campo
   `len` del tag, así que el `setlen(p, 5)` le escribe un 5 en `clip.x` y la capa dibujaría
   recortada 5 píxeles. En el host el ambiente se aplica con `PutDrawEnv` (el mismo `DRAWENV`
   del juego, los mismos campos) antes del `BeginScene` del cuadro, y la OT queda con lo que
   el dibujante del juego escribió. De paso: PsyCross **no** mira el `isbg` de un `DR_ENV`
   en la OT, así que el fondo gris oscuro de la capa se limpia por el camino de
   `PutDrawEnv`, que sí lo mira.

### Comprobación y límites

La evidencia de la corrida es la medición por cuadro (los primitivos de la OT del cuadro por
tipo y los bytes usados del búfer de paquetes) y la captura. **No hay un `pc/check_*.py`
nuevo para el hito 6**: lo que hay que comprobar es que la pantalla aparece, y eso el dueño
lo mira; lo que el port puede comprobar sin ojos (que los TIMs del arte lleguen a la VRAM,
que los primitivos apunten a la tabla de glifos, etc.) ya lo comprueban los checks de los
hitos 1 a 5 con el mismo disco y el mismo código. Queda como trabajo pendiente para el hito
siguiente si el dueño lo quiere.

Lo que el hito 6 **no** probaba: el pad (en T15 no había entrada: `PadChkVsync` de PsyCross
devuelve 0, así que el juego nunca leía un botón y la pantalla se quedaba en la opción 0
parpadeando — **resuelto en T17**), el sonido, los otros 22 modos, ni el camino es-419 del menú
(`ES419=0` en este build: la sexta opción del overlay es-419 no está en el arte que este binario
usa).

## El arte de la selección de país (T16)

El panel izquierdo de la selección de idioma —la imagen de 8 bits del banco `CNTSELWN.BIN`
(file 0x8A3), la única parte del arte que es un TIM de 8 bits con su propia CLUT de 256
entradas— sale en pantalla como **ruido**. Las banderas y los rótulos, que son partes de 4
bits del mismo banco, salen bien.

Se verificó **antes** de tocar nada, como con la fuente (`check_font.py`):

```sh
$ make -C pc
$ .venv/bin/python pc/check_country.py <imagen>.bin
ok   VRAM 640,0 64x256 (32768 bytes) is the disc's image byte for byte
ok   VRAM 0,496 256x1 (512 bytes) is the disc's CLUT byte for byte
ok   the left panel's sprite u=0 v=0 w=120 h=58 clut=31744 at 21,162 is the bank's 8-bit part, under texture page 138
```

`pc/check_country.py` parsea el banco por su cuenta (las dos entradas, el RLEN y los bloques
del TIM) y compara contra lo que el cargador del propio juego dejó en la VRAM leído con
`GR_ReadVRAM`; además recalcula, desde la tabla de partes del banco, el `SPRT` del panel
izquierdo y el `DR_TPAGE` que lo gobierna. El arnés imprime esas medidas con el cuarto
argumento de `boot` (`pcBootArtInit`, `shim/gfx_engine.c`), en el cuadro 40.

Lo que la comprobación dice, entonces:

- la **imagen de 8 bits** (0x280,0) y su **CLUT de 256 entradas** (0, 0x1F0) están en la VRAM
  **byte a byte** como en el disco: el camino de subida (el cargador de TIM del juego sobre
  `LoadImage`) y `u_long` no tienen nada que ver;
- el `SPRT` del panel tiene exactamente el UV, el tamaño y el `clut = getClut(0, 496)` que
  calcula `sprite_drawer.c` desde la tabla de partes, y el `DR_TPAGE` que lo precede en la
  cadena vale `0x8A` = `getTPage(1, 0, 0x280, 0)`: las coordenadas del código del juego son
  correctas.

Con la capa anfitriona descartada, la causa está más adentro: en cómo PsyCross **muestrea** la
textura de 8 bits y su CLUT. Medido con escrituras controladas en la VRAM (reemplazar la paleta
por la identidad y la imagen por un valor constante por fila): el color que sale es `r = 4v`
para el byte `v`, o sea el índice de CLUT a la **mitad** de lo que dice su propio shader;
preescalar la CLUT en la VRAM (`CLUT'[x] = CLUT[2x]`) **no** devuelve el arte. Eso no se
arregla desde `pc/`: la capa anfitriona hace exactamente lo que pide el código del juego, y
ADR-003 prohíbe parchear el submódulo `external/psycross`. Queda **escalado al dueño** (cambiar
de versión de PsyCross, parchearla —lo que cambiaría su licencia— o mandar el arreglo aguas
arriba); no se inventó un parche local.

La captura de esa corrida está en `build/t16_country_select.png` (se ve el panel izquierdo como
ruido, el resto de la pantalla correcto).

## El input del juego (T17)

El hito 7 es el primero en que **el juego se maneja**. El pad ya era del juego
(`src/engine/pad/{pad,port,demo}.c`): `main()` lo abre (`PAD.init` → `PadInitDirect` y
`PadStartCom`) y lo lee una vez por cuadro (`PAD.update`). Lo que faltaba en el host eran las
dos cosas que PsyCross no puede dar por sí sola:

1. **`PadChkVsync`.** El `updatePad` del propio juego gatea toda su lectura en esa llamada
   (`ret = PadChkVsync(); if (ret != 1) return;`) y la `libpad` de PsyCross contesta un 0 plano
   (`LIBPAD.C`, `PSYX_UNIMPLEMENTED`): por eso el juego nunca leía un botón y la pantalla de
   idiomas no se movía. En la PS1 la llamada reporta que la lectura del pad de ese vsync
   terminó; acá el pad se lee una vez por cuadro del bucle de `main`, así que contesta 1 una vez
   por llamada. **No se parchea PsyCross** (ADR-003): el enlace de `boot` envuelve el símbolo
   (`-Wl,--wrap=PadChkVsync`, `pc/Makefile`) y lo ata a `__wrap_PadChkVsync`
   (`pc/shim/host_pad.c`). Es una decisión de enlace del port; el submódulo y `src/**` quedan
   intactos.
2. **`PsyX_UpdateInput`.** PsyCross llena el buffer del pad que su `libpad` repartió
   (`PadInitDirect` lo guarda en `PsyX_Pad_InitPad`) dentro de
   `PsyX_UpdateInput → PsyX_Pad_InternalPadUpdates`, y solo lo llama por su cuenta bajo
   `_DEBUG`. El port lo llama una vez por cuadro desde el gancho de fin de cuadro
   (`shim/gfx_engine.c`), justo antes de que el `PAD.update()` del juego lea esos buffers.

Con esas dos cosas la cadena es del juego, de punta a punta: el teclado es el de SDL, leído por
el mapeo propio de PsyCross, PsyCross escribe la palabra cruda del pad en `PAD.buf[0].pad`, y
`readPad`/`readPadButtons` la vuelven held/pressed/repeated (los botones lógicos que
`getPadPressed` y compañía dan a los menús).

### Cómo se ve

`pc/shim/host_pad.c` imprime una línea por cuadro con lo que el código del juego vio y qué
opción resalta su menú:

```sh
$ make -C pc input-demo DISC="<imagen>.bin"
pad: scripted keys armed: down,down,up
boot: the game's own main(), 320x240 window, 160 frames
boot: capture goes to /…/build/t17_input_demo.png
boot: mode 0x1600 (country select)
pad 038: raw=FFFF held=0000 pressed=0000 menu=1/7 sel=0
pad: press 1 of 3
pad 039: raw=FFBF held=0040 pressed=0040 menu=1/7 sel=1
…
pad: press 2 of 3
pad 061: raw=FFBF held=0040 pressed=0040 menu=1/7 sel=2
…
pad: press 3 of 3
pad 083: raw=FFEF held=0010 pressed=0010 menu=1/7 sel=1
…
presented 160 frames, 23365 primitives drawn by the game's own mode code
screenshot: /…/build/t17_input_demo.png
```

`raw` es la palabra del buffer que `PadInitDirect` le dio a `libpad` (lo que lee el `readPad`
del juego; el bit se apaga mientras el botón está apretado, `raw = ~botones`); `held`/`pressed`
son los botones lógicos que armó el propio `readPadButtons`; `menu=1/7` es el
`state/substate` de la tarea controladora de la pantalla (`MENU_SELECT` es el 7 en
`menus/country_select.h`); `sel` es la opción resaltada. La captura
(`build/t17_input_demo.png`) muestra la **quinta opción** de la lista resaltada después de los
cuatro `abajo` que guiona la demo (`check_input.py` espera la caminata `0 -> 1 -> 2 -> 3 -> 4`).

### La demo guionada y la comprobación

`boot` acepta un quinto argumento: una lista de teclas separada por comas. El arnés la tipea con
la escritura directa del estado del pad (T21) — no eventos de teclado, y nada que salga del
juego: el run pasa por todo el camino que pasa una tecla del jugador (SDL → la `libpad` de
PsyCross → el pad del juego → el menú). La secuencia espera a que el menú llegue al substate que
lee el pad (`MENU_SELECT`) antes de empezar, así que no depende de adivinar en qué cuadro se
abren los paneles.

`pc/check_input.py` corre con `down,down,up` y verifica, a partir de la salida del propio arnés:

- que cada pulsación llegó al pad del juego (un cuadro cuyo `pressed` lógico tiene el bit del
  botón y cuyo `raw` lo tiene en claro, uno por pulsación), y que pasó mientras el menú estaba en
  `MENU_SELECT`;
- que el resaltado del menú hizo la secuencia que piden esas pulsaciones: `0 → 1 → 2 → 1`.

```sh
$ .venv/bin/python pc/check_input.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/boot does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_input.py <imagen>.bin
ok   the scripted down press(es) reached the game's own pad code (pressed bit 0x40 on frame(s) 39, 61)
ok   the scripted up press(es) reached the game's own pad code (pressed bit 0x10 on frame(s) 83)
ok   the game's own menu moved its highlight 0 -> 1 -> 2 -> 1 for 'down,down,up'
ok   the run captured its frame to a 320x240 PNG (230723 bytes)
the game's own pad code saw the scripted 'down,down,up' presses and its own country-select menu moved the highlight to match
```

### El teclado, interactivo

`make -C pc boot DISC="…"` (sin quinto argumento) corre exactamente igual, pero las teclas las
aprieta una persona: la cadena es la misma y las líneas `pad …` muestran lo que el código del
juego ve. `pcBootPadInit` levanta la ventana de PsyCross y le pone el foco con la API de SDL
(`SDL_RaiseWindow`/`SDL_SetWindowInputFocus`), no con `XSetInputFocus` sobre una ventana buscada
por nombre: bajo XWayland esa es otra ventana del árbol y moverle el foco ahí es justo lo que
hace que SDL **pierda** el foco. Gracias a eso las teclas llegan sin tener que hacer clic antes.

Mapa de teclas (el propio de PsyCross, `PsyX_main.cpp`): **flechas** = cruceta, **Enter** =
Start, **Espacio** = Select, **X** = cuadrado, **V** = círculo, **Z** = triángulo, **C** = cruz,
Shift/Ctrl/`[`/`]` = L1/L2/L3/R1/R2/R3. Con las flechas el resaltado se mueve y con Enter la
pantalla confirma.

### Qué hace el juego al confirmar

Con `down,start` (o apretando Start en la opción resaltada) el propio código del juego
(`CNTY_SEL_tickMenu`) hace lo que hace en la PS1: guarda el idioma (`LANGUAGE =
CNTY_SEL_languages[selección]`), flash del cursor, cierra los paneles, funde a negro y pide el
modo siguiente en su substate `MENU_EXIT`: `MODE_OPENING_2` (**0xE02**) cuando la opción elegida
no es la primera de la lista, `MODE_OPENING` (0xE01) cuando lo es. O sea que la cadena del input
llega hasta el cambio de modo, y el modo siguiente es el **overlay de título, STDWTITL**:
el hito 8 lo enlaza.
Los modos que este binario todavía no tiene siguen el mismo freno que documenta T15 (el host los
nombra y se detiene); alcanzarlos pide el input que lleve hasta ellos.

## La pantalla de título y el video (T18, T37-T39)

El hito 8 contesta la pregunta que dejó abierta T17: al confirmar el idioma, el juego pide
`MODE_OPENING_2` (0xE02). Ese modo es el **overlay STDWTITL** (`FILE_STDWTITL`, índice `mode >> 8`
= 0xE = **14**), el mismo que en la PS1 es un `.PRO` y que acá se **enlaza**, no se carga.

### Qué pide 0xE02 (y por qué no es directamente el título)

`STDWTITL_start` (`scene.c`) mira el **byte bajo** del modo y elige entre la pantalla de título
(0), un video (1-12 en la UE) y la pantalla fija del *splash* (13). 0xE02 tiene byte bajo **2**,
así que no entra a la pantalla de título: entra al **segundo video de apertura**
(`STDWTITL_startMovieTask(1)`), que en `data/stdwtitl.c` de la UE es el archivo **0x7F4** con
`nextMode` = **`MODE_TITLE` (0xE00)**. El título está **después del video**; el video es el que
hay que pasar primero. (Con la primera opción de la lista el juego pide 0xE01 = video 0, 0x94C,
que también termina en `MODE_TITLE`: por los dos caminos se llega igual, por otro video.)

### El video: decodificador real, y una ventana que todavía no lo muestra

`src/menus/title/` no usa ni `libgs` ni `libsnd`; lo que usa es **`libpress` (MDEC) y el
*streaming* de `libcd` (`St*`)**, que PsyCross **no implementa** (trae las cabeceras
`libpress.h`/`libcd.h`, no los cuerpos). El port los aporta completo desde T37-T39:

- **El decodificador** es `pc/shim/host_mdec.c` (T37, módulo propio, sin PsyQ): tabla Huffman de
  MPEG-1, des-cuantización, IDCT y YUV→RGB, todo de estándares públicos o por fórmula.
- **El *streaming*** es `pc/shim/host_st.c` (T38): `StGetNext` entrega **fotogramas completos**
  ensamblados de los sectores del `.STR` (payloads concatenados, cabecera del primer sector
  aparte), con anillo y contrapresión.
- **La fachada de `libpress`** es `pc/shim/host_press.c` (T39): `DecDCTvlcBuild`/`DecDCTvlc2`
  arman el flujo run-level con el decodificador (el buffer de tabla del juego queda opaco, no se
  escribe); `DecDCTin(buf, 3)` decodifica **el cuadro entero una vez** a un *shadow* de 15 bits;
  `DecDCTout(buf, size)` copia la franja pedida al buffer del juego y dispara el callback, que
  encadena la franja siguiente (20 por cuadro de 320x416), y **en el retorno más externo**
  presenta el cuadro con `DrawSync(0)` + `LoadImage(0, 36, 320, 416)`.
- **Por qué 15 bits y no 24**: PsyCross ignora `DISPENV.isrgb24` (la única mención en
  `external/psycross/src/psx/LIBGPU.C` sólo lo pone en 0), así que el camino de display de 24
  bits del juego no puede mostrar nada acá. `setDisplayMode(320, 480, 1, 1)` llega a
  `hostSetDisplayMode` (`pc/shim/gfx_engine.c`), que deja un área de display de 320x480 a 15 bits
  en la VRAM (0,0): el cuadro tiene que estar en la VRAM a 15 bits, aunque la PS1 pida 24.
- **Por qué el orden importa**: las `LoadImage` por franja del juego escriben su reconstrucción
  de 24 bits en la VRAM (20 tiras de 24 halfwords en `x = slice.x`, `y = 36`, con `+480` en los
  cuadros cuyo `rectid` es 1). El display tiene 320 halfwords de ancho, así que esas escrituras
  caen fuera de las columnas visibles o dentro como basura. La `LoadImage` del port se hace en el
  retorno **más externo** de `DecDCTout`, o sea después de la última franja del juego: la última
  escritura sobre el área visible es el cuadro de 15 bits. Hacerlo antes deja una tira de 24 bits
  encima y el cuadro sale en bandas.
- **Cómo llega a la ventana (T39)**: PsyCross **no copia la VRAM a la ventana** (su
  `GR_StoreFrameBuffer` guarda el back buffer *en* la VRAM, no al revés), así que el cuadro se
  dibuja como primitivas: `pc/shim/host_video.c` lo pinta como **10 quads texturados**
  (`POLY_FT4`, 5 columnas de 64 px por 2 filas cortadas en el límite de página de la textura de
  15 bits) y `gfx_engine.c` los manda en la OT del cuadro. `DrawPrim` de PsyCross no vuelca sus
  *splits*, que sólo cierra `DrawOTag`, y el `DRAWENV` de pantalla completa necesita `dfe = 1`
  (PsyCross lo lee como "dibuja a la ventana"). Antes del presentador, esas capturas salían
  planas (1 color, media 31.00: el gris del clear); con él, el cuadro 214 da un error absoluto
  medio de 7.07 por canal contra el PNG del decodificador offline.
- **Ritmo (T40)**: el cuadro N vence cuando el reloj del XA lleva `N * 2520` muestras
  (`sndXaPosition`, el *playhead* de las muestras reproducidas). Medido en juego: 15,00 fps
  (antes ~49), 0 esperas cortadas, 0 *underruns*.

### La ventana (T41)

El display de los videos es de **480 líneas** (`setDisplayMode(320, 480, ...)`) y la ventana del
port es de 320x240, así que ese display se veía **a media altura**: el cuadro de 320x416 salía
como 320x208 con 16 filas negras arriba y abajo. PsyCross escala lo que el juego dibuja a la
ventana (su proyección ortográfica sale del `DISPENV` activo), así que la ventana se puede abrir
más grande sin tocar el juego. `boot` lee **`DW3_WINDOW="<ancho>x<alto>"`** y sigue usando el
320x240 del juego si no está:

```sh
DW3_WINDOW=640x480 pc/build/boot "<imagen>.bin" 700 build/video.png 0 "down,down,down,down,start,wait"
```

Los AVI exportados (`pc/shim/video_export.c`, `avi_write.c`) guardan 320x416 y declaran con un chunk OpenDML `vprp` la proporción de cuadro 20:13
(`dwFrameAspectRatio = 20<<16 | 13`, un campo progresivo, versión del exportador 2): un reproductor los muestra a 640x416, la misma geometría. `avi_read.c` recorre
los chunks y no necesita ese `vprp`.

Con 640x480 el cuadro del video ocupa las 416 filas de su rectángulo en el display
(`y = 36..451`, ancho completo 640), o sea la misma geometría que la PS1 en su modo entrelazado:
se duplica el ancho, no se comprime el alto. Medido sobre cuatro capturas de la apertura
(`build/t41d/shot_f300..f480.png`, git-ignored): contenido en las filas 36..451 y columnas
0..639 en las cuatro, con md5 distinto; a 320x240 las mismas capturas dan filas 18..225 (208).
El *pacing* no cambia con la ventana grande: `pacing: 20 frames 1..20 in 1329 ms = 14.30 fps,
waited 1047 ms (max 71), capped 0, stalled 0`.

El *default* sigue siendo 320x240 a propósito: las capturas de `check-field`, `check-title` y
compañía se comparan contra números medidos en ese tamaño. `pc/check_title.py` respeta la misma
variable (`DW3_WINDOW=640x480 .venv/bin/python pc/check_title.py <disco>`) y entonces exige la
ventana, las capturas y el PNG final de 640x480.

La ventana también es **redimensionable** y `RAlt+Enter` la pone en pantalla completa; las dos
cosas ya venían de PsyCross y estiran la imagen sin respetar la relación de aspecto, mientras que
`DW3_WINDOW` da el tamaño exacto que corresponde al modo de 480 líneas.

**Hallazgo (T41) y su arreglo (T42)**: las capturas "del video" que tomaba `check_title.py` en
los cuadros 210/215/220 eran el *splash* de Bandai que dibuja STDWTITL antes de que el cuadro
del MDEC se vea (el logo ocupa 160x150 en el centro de un fondo gris del clear; con 640x480 se
ve idéntico, sólo que al doble), así que la aserción "the movie shows a picture" medía que hay
una imagen en pantalla en el modo del video, no el cuadro decodificado. Desde T42 la corrida del
título no captura el video y una **segunda corrida** deja la película correr y captura el cuadro
real (desde el ~300), afirmando su geometría: el rectángulo del video en el display de 480
líneas (filas 18..225 de la ventana de 320x240, 36..451 de la de 640x480), ancho completo, clear
arriba y abajo, y que las capturas avancen.

**Hueco abierto (T43)**: el **fondo de la pantalla de título** salía en bandas de colores planos
donde debería haber una imagen. Medido por el dueño mirando la ventana: las bandas no eran las
mismas en la ventana de 320x240 que en la de 640x480. **Causa medida el 2026-10-22** (medida, T43): no era nada de las tablas de la pantalla
del título, sino el **presentador de las películas** (`pc/shim/host_video.c`) dibujando sus quads
para siempre: al matarse el reproductor (`STDWTITL_tickMoviePlayer`, `TASK_KILL`) nadie vuelve a
llamar `DecDCTin`/`DecDCTReset`, así que el `hostVideoDisable()` de `pressInvalidate` no corre
nunca más y los quads del último cuadro de la película quedan dibujándose **sobre** el título en
cada frame, muestreando la VRAM que el título re-subió (arte de 8 bits y CLUTs leído como 15 bits:
las bandas, que además cambian con la escala de la ventana). El arreglo (commiteado el
2026-10-22 con aval del dueño): `hostSetDisplayArea` ahora desarma el presentador — es el único punto
host-visible del fin de la película, y el `setDisplayArea` de `TASK_KILL` vuelve a poner el display
del juego una vez que la película ya no corre; una película siguiente re-arma el presentador por
su cuenta (`pressPresentFrame`). Medido con el arreglo: el título dibuja su fondo **coincidiendo
con la reconstrucción página+CLUT de la VRAM píxel a píxel** en los cinco bloques de 64 columnas
(0 píxeles fuera de tolerancia 24; el baseline en bandas: 84-89%), y `check_title` sigue verde
(13 `ok`) con las ventanas de 320x240 y 640x480, con la película viéndose dentro del video.

Como el reproductor **no termina solo** dentro del presupuesto de cuadros de las demos (el video
de apertura dura muchos cientos de cuadros), la forma de pasar el video sigue siendo la que el
juego le da a un jugador: **Start** (`movie.c`: `PAD.getPressed(0) & (1 << PAD_START)`). La demo
lo aprieta.

### Cómo llega al título

Con el video salteado, el propio código del juego sigue: `STDWTITL_tickMovie` (`TASK_RUN`) ve que
su reproductor hijo ya no está, hace `FONT.load()` y `GAME.funcs.requestMode(MODE_TITLE, 0)`. El
bucle de `main` mata la tarea del modo viejo, libera su memoria (`HEAP.freeByTag(MEM_MODE)`),
`commitMode` y crea el modo **0xE00**, que `STDWTITL_start` manda por el byte bajo **0** a la
**pantalla de título**: el *loader* del título, que espera su banco de sonido, carga sus TIMs
(`STDWTITL_loadTitleImages`: el archivo de imágenes y el banco de sprites del idioma, y los
cuatro TIMs de fondo del archivo 0x887) por el **cargador de TIM del juego** desde el disco, y
después el fondo animado, las dos partes del título, el brillo, el logo y el menú (el
aviso de pulsar el botón de inicio que esa pantalla muestra), todo con el código del juego.

Lo que se ve (la captura la toma el propio arnés con `glReadPixels`): el **logo del juego**, el
aviso de pulsar START (el texto del propio juego, en el idioma configurado para la demo: la
pantalla de título tiene sus sprites por idioma) y el texto de copyright. Nada de
eso lo dibuja el port: es `STDWTITL` + `sprite_drawer.c` sobre la OT de PsyCross, con el arte
del disco.

```sh
$ make -C pc title-demo DISC="<imagen>.bin"
pad: scripted keys armed: down,down,down,down,start,wait,start
boot: the game's own main(), 320x240 window, 320 frames
boot: mode 0x1600 (country select)
gfx: layer id 0x100, 320x240 at 0,0, OT 8 entries
…
pad: press 2 of 3
boot: mode 0xE02 (STDWTITL (title screen / opening movies))
gfx: two packet buffers of 10240 bytes for the mode
st: arm lba=258813
st: frame 1 320x416 9 sectors count 3136
pad: the game left mode 0x1600; the script goes on
pad: press 3 of 3
boot: mode 0xE00 (STDWTITL (title screen / opening movies))
gfx: two packet buffers of 81920 bytes for the mode
gfx: layer id 0x1000, 320x240 at 0,0, OT 4 entries
frame 231: layer 0x1000 prims=0 (sprite=0 poly=0 env=0 other=0) bytes=0
…
presented 320 frames, …
screenshot: /…/build/t18_title_screen.png
```

(Con `MDEC_DEBUG=1` salen además las líneas del decodificador: una por cuadro —
`mdec: frame 20 320x416 macroblocks 520 blocks 3120 errors 0 tokens 13732`— y el total al
cerrar el video: `mdec: 20 frames decoded, 20 presented, 0 errors over the movie`.)

Los tres modos los pide el juego: 0x1600 al arrancar, 0xE02 al confirmar el idioma y 0xE00
cuando el video se saltea. La capa `0x1000` es `STDWTITL_TITLE_LAYER` (la del título); los
cuadros con `sprite=` son los del código del juego dibujando su arte del disco, y `poly=0`
porque esta pantalla es sólo sprites. Los `frame` del video (entre 0xE02 y 0xE00) **no dibujan
primitivas**: el juego sólo sube el cuadro a la VRAM con `LoadImage`, y PsyCross no la muestra
(ver arriba), así que la ventana queda en el gris del clear. Eso es lo que falta, no lo que el
decodificador hace.

### El token `wait` de la demo

El guion de teclas de T17 (una lista separada por comas, `pc/shim/host_pad.c`) suma un token
`wait`: una **barrera** que espera a que el modo del juego ya no sea el que vio al llegar a ese
paso. Es lo que hace que la demo no dependa de adivinar en qué cuadro el juego cambió de modo
(igual que los demás pasos esperan a que el menú de idiomas llegue a `MENU_SELECT`). `down,down,
down,down,start,wait,start` es entonces: bajar hasta la quinta opción de la lista, confirmar,
esperar el cambio de modo del propio juego, saltear el video.

### La comprobación (`pc/check_title.py`)

Corre la misma demo guionada y verifica, **desde la salida del propio arnés**:

- que el run arranca en la selección de país (`0x1600`);
- que al confirmar el juego pide **0xE02** (el video del overlay de título) y que después pide
  **0xE00** (la pantalla de título), en ese orden — o sea que el despacho del port enlaza
  `STDWTITL` y que la elección por byte bajo es la del juego;
- que el **MDEC del port decodificó** los cuadros del video (con `MDEC_DEBUG=1`, una línea por
  cuadro) a 320x416 y sin errores, y que **todos se presentaron** (la línea de cierre del video);
- que el cuadro **capturado durante el video** (lista de captura de T30, cuadros 210/215/220) es
  una imagen: colores distintos y brillo medio (hoy **falla**: PsyCross no muestra la VRAM, ver
  arriba; el fallo es la evidencia de lo que falta, no un umbral flojo);
- que la pantalla de título dibujó **sus sprites en su propia capa** (`0x1000`) y que seguía
  dibujándolos en el último cuadro del run;
- que la captura final es un PNG de 320x240.

```sh
$ .venv/bin/python pc/check_title.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/boot does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_title.py <imagen>.bin
ok   the run boots into the country select (mode 0x1600)
ok   confirming the language requests the title overlay's movie (mode 0xE02, the game's own low-byte dispatch)
ok   skipping the movie makes the game's own task request the title screen (mode 0xE00)
ok   the port's MDEC decoded 20 movie frame(s) at 320x416 with no errors
ok   every decoded frame was presented (20 frames, 0 errors)
FAIL the movie's picture: the captured movie frame is blank (frame 210: 1 colours, mean 31.00, …; need 64+ colours and mean 8.0+)
ok   the title screen drew its own sprites on layer 0x1000 (83 frame(s), first with 33 sprites, last with 63)
ok   the run was still drawing the title screen on its last frame
ok   the run captured its frame to a 320x240 PNG (230723 bytes)
1 check(s) failed
```

Cuando el port pinte el cuadro de VRAM en la ventana, el `FAIL` tiene que pasar a `ok` sin tocar
los umbrales: la captura es el mismo PNG que escribe el arnés, y el número medido ahí es lo que
dice si el video se ve.

## El campo (T19/T22/T23/T24)

El hito 9 contesta la pregunta que dejó abierta T18: qué hay al confirmar *new game* en el menú
del título. La pantalla de título muestra sus dos opciones y su menú lee el pad (`STDWTITL`), el
guion de la demo navega hasta **NEW GAME** y confirma, y el **código del propio juego** pide
entonces `MODE_NEW_GAME` (0x2D7), cuyo `mode >> 8` es **2**: el índice donde el juego tiene
**`FIELDSTG`**, el modo del campo (`src/field/`), el mismo lugar donde la PS1 pone su
`FIELDSTG.PRO`.

### Cómo el port enlaza el campo y sus stages (en vez de los overlays MIPS)

Igual que los modos de T15 y T18: en la PS1 el campo y cada escenario son **overlays** —MIPS que
el ejecutable copia a una dirección fija (`OVERLAY_ADDRESS`/`SUB_OVERLAY_ADDRESS`) y a los que
salta—. Eso no se ejecuta en el host, así que:

- El modo se **enlaza**: el `PC_MODE_ENTRY_POINTS` de `pc/boot.c` pone `pcStartField`
  (`FIELDSTG_start`) en el índice 2, y el despacho del port (`GAME.funcs.getMode() >> 8`) lo
  llama. Los `.PRO` no se leen nunca.
- Los **stages** también se enlazan. `FIELD_STAGE_TUS` (`pc/Makefile`) compila cada
  `src/field/stages/**/*.c` a `build/stages/` y **renombra cada global con el nombre de su
  stage** (`WSTAG###_*`), que es lo que el linker de la PS1 hace con sus `<stage>_syms.ld`: sin
  el prefijo, dos stages que nombran sus funciones igual colisionarían en un solo binario.
- Lo que queda para el C del juego es su propio **cargador**: el `startField` de `field.c` lee
  `OVERLAY_LOADER.loadSubOverlay(FIELDSTG_state.stageFile)` para cargar el stage del modo que
  entra. Ese `OVERLAY_LOADER` es del host (`pc/shim/host_overlay.c`): no copia nada —el stage ya
  está enlazado— y dice en una línea qué stage pidió el juego. El `overlay.c` del juego no es
  parte del camino del host (su `MODE_ENTRY_POINTS` nombra los 22 overlays, que arrastrarían
  todos los modos que este binario no tiene).
- **Una unidad del campo se compila desde una copia del host (T22)**:
  `field_mode/file_loader.c` se reemplaza por `pc/field/file_loader.c`, idéntica salvo un guard
  (ver "La causa medida" abajo); `pc/Makefile` la saca del glob de las unidades del campo y
  compila la copia en su lugar.

Un dato del relevamiento que este hito confirma: `src/field/` (379 archivos C, 136.530 líneas,
el grueso del juego) nombra **cero** `Gs*`, **cero** `Ss*` y **cero** `gte_*`; se dibuja con
`libgpu`/`libgte` (que PsyCross **sí** provee) y con código propio. Por eso el campo se puede
enlazar sin escribir `libgs`.

### Qué prueba la corrida

```sh
$ make -C pc field-demo DISC="<imagen>.bin"
pad: scripted pad armed (in process, no XTest): down,down,down,down,start,wait,skipmovie,...
boot: the game's own main(), 320x240 window, 900 frames
boot: mode 0x1600 (country select)
...
boot: mode 0xE02 (STDWTITL (title screen / opening movies))
...
boot: mode 0xE00 (STDWTITL (title screen / opening movies))
...
boot: mode 0x2D7 (FIELDSTG (the field))
gfx: two packet buffers of 25600 bytes for the mode
gfx: layer id 0x1000, 320x240 at 0,0, OT 2 entries
gfx: layer id 0x1001, 320x240 at 0,0, OT 2 entries
gfx: layer id 0x1002, 320x240 at 0,0, OT 16 entries
gfx: layer id 0x1004, 320x240 at 0,0, OT 2 entries
gfx: layer id 0x1003, 320x240 at 0,0, OT 2 entries
overlay: the game loads stage overlay 0x1E9; this build has it linked, nothing is copied
frame 457: layer 0x1002 prims=0 (sprite=0 poly=0 env=0 other=0) bytes=0
frame 458: layer 0x1002 prims=40 (sprite=0 poly=40 env=0 other=0) bytes=2016
frame 458: layer 0x1003 prims=4 (sprite=0 poly=2 env=2 other=0) bytes=2016
...
frame 899: layer 0x1002 prims=114 (sprite=25 poly=44 env=45 other=0) bytes=3724
frame 899: layer 0x1004 prims=8 (sprite=0 poly=0 env=0 other=8) bytes=3724
pad 899: raw=BFFF held=2000 pressed=0000 menu=-1/-1 sel=-1 title=-1/-1 sel=-1 choice=-1
screenshot: /…/build/t19_field.png
presented 900 frames, 107469 primitives drawn by the game's own mode code
```

Los cuatro modos los pide el juego: `0x1600` al arrancar, `0xE02` al confirmar el idioma,
`0xE00` cuando el video se saltea y `0x2D7` al confirmar **NEW GAME** en el menú del título. La
cadena llega hasta el campo por el mismo camino que un jugador: el menú del título es del juego
y el pad es del juego (T17).

Las cinco capas del campo (`0x1000`-`0x1004`, creadas para el modo: OT de 2, 2, 16, 2 y 2
entradas) son del código del campo; el título también usa `0x1000`, así que lo que las separa es
el momento: se crean después de la línea `boot: mode 0x2D7`. El campo **dibuja con su propio
código**: el mapa en `0x1002` (de 40 `poly` en el primer cuadro dibujado, el 458, a 114 prims
—44 `poly` + 25 `sprite` + 45 `env`— una vez cargado, con los `sprite` que T23 sumó: los objetos
del mapa que el propio campo registra como callback de dibujo de la capa) y la animación del
banner de área en `0x1003`, que cicla sus cuadros (4 → 10 → … → 84 prims, sostenido, y después
74 → 0) y termina cuando el rótulo de la zona se va (0 prims en el cuadro 625). El run **ya no
tiene parada artificial** (T22 quitó
`pcBootStopInMode` de `pc/boot.c`) y, con el puntero del comando de guion entero (T24), **ya no
muere**: corre sus 900 cuadros de presupuesto y sale solo. `pcBootStopInMode` (el mecanismo de
parada de T19, `pc/shim/gfx_engine.c` y `host.h`) queda **sin usar**: ningún demo lo arma ya, así
que es código muerto del shim que una limpieza posterior puede sacar (no se tocó acá: la superficie
de edición de T24 no incluye el shim). Al alcanzar el presupuesto el arnés **sí deja captura**
(320x240): la de una corrida corta a 600 cuadros muestra el mapa de la ciudad con su rótulo de
área y el de la esquina, dibujados por el motor de texto del juego (no se citan acá); desde T27
también la del cuadro final es el mapa (antes salía uniforme `1,1,1`; ver "El entorno de dibujo
por capa (T27)" abajo).

### La causa medida: el stage sin slots y el guard del cargador (T22)

T19 había anotado mal la causa: decía que el campo "no llega a montar su stage" y que era un
**problema de orden** (`FIELDSTG_setupField` corriendo desde las story events, después del
cargador). **Eso es falso**, y T22 lo corrigió midiendo el código:

- El setup del stage del campo **sí corre**, y antes de lo que T19 creía: `startField` llama a
  `FIELDSTG_state.stageInit(task)` (`src/field/field_mode/field.c:512`) **antes** de crear el
  file loader (`field.c:607`). Para el modo `0x2D7` el stage es `WSTAG780`
  (`{727, 489, WSTAG780_startStage}`, `src/field/field_mode/data/fieldstg.c:3475`), y su
  `setupStage` (`src/field/stages/kusanagi_city/wstag780.c:645`) simplemente **nunca asigna**
  `FIELDSTG_state.slots`: ese stage **no tiene slots**.
- `FIELDSTG_setupField` —el que asigna `FIELDSTG_state.slots = FIELDSTG_slots`,
  `src/field/field_mode/field_stage.c:57`— es el `stageInit` del **modo 528**, no el de una
  partida nueva: no participa de este camino.
- `FIELDSTG_pickStage` deja el estado en cero (`HEAP.zero(&FIELDSTG_state, 100)`,
  `src/field/field_mode/state.c:33`), así que `slots == NULL` es el estado **normal** de un stage
  sin slots, y el propio campo lo sabe: `field.c:621` guarda
  `if (FIELDSTG_state.slots != NULL)` antes de crear sus triggers. El cargador **no** guarda:
  `FIELDSTG_requestSlotFiles` (`src/field/field_mode/file_loader.c:25`) lee
  `FIELDSTG_state.slots` y lo desreferencia en el bucle (`:30`, `entry->type`; `StageSlot.type`
  está en el offset `0x08`, así que la lectura es en la dirección 8).
- En la PS1 esa lectura cae en RAM baja mapeada (no hay MMU) y el bucle termina; en x86-64 la
  página 0 no está mapeada y es **SIGSEGV**. Medido con gdb antes del guard: la desreferencia es
  en **`presentFrames=468`** con `rax=0`, dentro de `FIELDSTG_requestSlotFiles`. **No es el cuadro
  463**: el 463 es la parada a propósito de T19 (`pcBootStopInMode`, una salida limpia que imprime
  su resumen y deja captura), y se lo confundió con el choque porque un proceso muerto por señal
  no vacía un stdout con buffer de bloque: una tubería o gdb muestran la cola unos cuadros antes.
  Por eso el arnés y `pc/check_field.py` corren bajo `stdbuf -oL`. **El valor exacto que la PS1
  lee en la dirección 8 no está medido**: es una suposición que el bucle termine ahí, y se dice
  como tal.
- `vm.mmap_min_addr` es 65536 en esta máquina, así que mapear una página cero en la dirección 0
  como usuario normal **no** es una opción; no se fue por ahí.

**El guard (solo del host).** `pc/field/file_loader.c` es la copia del host de esa unidad, con
una sola diferencia: en `FIELDSTG_requestSlotFiles` agrega

```c
if (FIELDSTG_state.slots == NULL) {
    return;
}
```

que es exactamente lo que hace el propio campo (`field.c:621`): un stage sin slots no pide
archivos de slots. El resto de la unidad es **idéntico** al upstream (un `diff` da solo el
comentario de cabecera y el guard). No se toca `src/**` (ADR-003): la tolerancia es del host, no
un arreglo del juego. `pc/Makefile` saca `field_mode/file_loader.c` del glob de las unidades del
campo y compila esta copia en su lugar.

### El callback de la capa y el puntero entero (T23)

Con el guard de T22 el campo **sobrevive** el punto de T19 y sigue dibujando hasta el cuadro
**473**. En el cuadro **474** moría por **SIGSEGV** dentro de **`FIELDSTG_drawMapObject`**,
alcanzado desde `hostLayerRunCallbacks` (el callback ordenado que el propio campo registra en su
capa, `src/field/field_mode/map_objects.c:123`), en la instrucción `mov 0x88(%rax),%rcx` con un
`rax` **truncado** (muestras medidas: `0x5724a420`, `0x5724c950`, `0x5725d9b0`, `0x57260630`).

**Causa medida**: el callback pasaba un puntero por un campo de **32 bits**. `DrawCallback.arg`
es `s32` (`include/engine/gfx.h:120`) y la firma del callback es
`void (*)(s32 arg, void *layer, s32 param)`; en la PS1 el puntero entra en 32 bits, pero en el
host se truncaba. Los 32 bits bajos del puntero de la tarea `MapObjects` quedaban en el campo (en
la corrida con `0x5724a420`, el puntero del host era `0x5555_5724_a420`), y el callback lo
desreferenciaba como si fuera de 64 → SIGSEGV. Es la **misma clase** que los casts de puntero de
T6/T7 y que la ranura de hijo de T15 (un puntero que no cabe en un campo de 4 bytes).

**El arreglo (solo del host, T23).** `pc/shim/gfx_engine.c` es el reemplazo del `layer.c` del
juego, así que la capa es del host: el host define su **propio nodo** de callback
(`HostDrawCallback`) con `arg` como `void *` (el campo del header queda `s32` y no se toca), lo
aloja en `layer->callbacks` (que solo lee el host: grep `DrawCallback`/`callbacks` da
`src/engine/gfx/layer.c` --la unidad reemplazada-- e `include/engine/gfx.h`; todo otro llamador
llega por la tabla `addSortedCallback`/`addCallback`/`allocCallbacks`) y llama al callback con
la forma compatible `((void (*)(void *, void *, s32))cb->func)(cb->arg, layer, cb->param)`,
dejando `param` en `s32`. El argumento del host pasa a `void *`, así que el registro llega
entero. No se toca `src/**` ni el header ni el build de PS1 (el `addSortedCallback` del juego
sigue siendo el sin prototipo, que es lo que deja pasar los 64 bits). Evidencia: el campo deja
de morir en el 474, dibuja sus objetos del mapa y sigue hasta el 561 (ver abajo).

### El puntero del comando de guion (T24)

Con el callback entero el campo dibujaba hasta el cuadro **561** y en el **562** moría por
**SIGSEGV** en **`WSTAG780_handleCommand800`** (el comando de guion del stage `WSTAG780`,
`src/field/stages/kusanagi_city/wstag780.c:448`), alcanzado desde `FIELDSTG_handleScriptCommand`
→ `FIELDSTG_setEventPose` → `FIELDSTG_runEvent` (`src/field/field_mode/script.c:93`,
`event.c:28/55`), con `rax` = los 32 bits bajos del puntero del host (muestras medidas:
`0x572572f0`, `0x57254420`, `0x57264260`; el valor exacto depende del layout del heap).

**Causa medida**: la **misma clase** que T23, pero el campo de 32 bits estaba en el **código del
juego**. `FIELDSTG_setEventPose` guardaba el puntero de la tarea del comando en un `s32 target`
(`event.c:33`) con un cast `(s32)TASK_REGISTRY.funcs.find(...)` (`event.c:42`) y en
`EventChildren.scripts[10]` (`s32`, `include/field/field_mode.h:294`), y
`FIELDSTG_handleScriptCommand(s32 task, ...)` (`script.c:93`, `field_mode.h:967`) lo recibía en 32
bits y lo pasaba al `handle` del comando (`WSTAG780_handleCommand800(Task *task, s32 id)`), que lo
desreferenciaba como puntero de 64. La PS1 tolera el truncado porque ahí el puntero mide 32 bits;
el host no.

**El arreglo (T24, en su lugar).** Es la primera vez desde T6/T7 que el port toca `src/**`/
`include/**` (ADR-003): los `s32` que transportaban el puntero pasan a **tipo puntero** —
`void *target` en `event.c`, `void *scripts[10]` en `EventChildren` (`field_mode.h`), el retorno de
`FIELDSTG_createScriptCommand` y el parámetro de `FIELDSTG_handleScriptCommand` — y se cae el cast
`(s32)` del `find`. En la PS1 un puntero mide 4 bytes, así que el layout de la estructura y el
código generado **no cambian**: `make VERSION=eu compare` sigue en **316 OK, 0 FAILED**. El diff
del árbol de upstream es de **3 archivos, 14 líneas** (`git diff --stat -- src include`:
`include/field/field_mode.h`, `src/field/field_mode/event.c`, `src/field/field_mode/script.c`).

Dos detalles del cambio, medidos:

- `FIELDSTG_createScriptCommand` llama al `create` de la tabla, cuyo campo está declarado
  `s32 (*create)(s32)` (`ScriptCommand`, `field_mode.h:302`) pero **todas** las funciones `create`
  de los stages están definidas devolviendo un puntero (`void *`, `Task *`, `Lift *`,
  `StageEffect *`; el header `include/field/stages.h` las declara `s32`, una declaración vieja y
  equivocada). El arreglo las llama a través del tipo de retorno puntero
  (`((void *(*)(s32))cmd->create)(id)`) sin tocar la tabla ni el header de los stages (fuera de la
  superficie de edición de T24): así el host lee el puntero entero de **cualquier** comando, no
  solo del 800.
- `EventChildren` solo tiene punteros, así que su tamaño en el host es exactamente el doble del de
  la PS1 (`0x70` contra `0x38`); `createTask` ya pide el doble de bytes para las reservas en cero
  (`pc/shim/heap.c`, la concesión de T15), así que el bloque de hijos alcanza justo.

### El orden de los callbacks de la capa (T26)

Un verificador independiente de T23 encontró que la capa del host **guardaba la prioridad pero no
ordenaba**: `hostLayerAddSortedCallback` (`pc/shim/gfx_engine.c`) agregaba al final en orden de
registro. La regla del juego, leída de su código (`src/engine/gfx/layer.c:280-320`), no es un
ordenamiento de la lista: la lista es el arreglo de nodos enlazado por `next`, el nodo 0 es una
**centinela** (`priority = 0x7FFFFFFF`, `layer.c:264`) que **participa del recorrido como un nodo
más**, y el recorrido corta en el primer nodo con prioridad **estrictamente menor** que la nueva y
inserta delante — **mayor prioridad primero**, y una entrada nueva queda **después de las que
empatan** (los empates dibujan en orden de registro). El host ahora es un port literal de ese bucle
(mismos `cur`/`prev`/`e`, mismas condiciones, misma centinela) y `hostLayerAddCallback` es el
append literal del juego (`layer.c:323-338`: prioridad 0 y `cb[-1].next = cb`, que es la cola de la
lista solo mientras la capa se registró en orden de append; una capa que mezcle las dos llamadas
queda como el propio juego la deja).

**La medición.** El shim trae un diagnóstico de un cuadro, apagado por defecto: con
`DW3_CB_DUMP=<cuadro>` en el entorno imprime, para ese cuadro y para cada capa con arreglo de
callbacks, el orden de registro (el arreglo, índice 1 = el primer registro) y el orden que
`runCallbacks` recorre, más una **sonda sintética** de la misma función de inserción (7 entradas:
tres `9` empatadas, una `0x7FFFFFFF`, una corrida ascendente). Sin la variable la corrida no
imprime nada extra. En el cuadro 600 la capa del mapa (`0x1002`) registra **3** callbacks —los
`tile.y` de dos actores (`actor.c:69`) y el `sortY` de un map object (`map_objects.c:123`):
**152, 176, 231**— y la lista que `runCallbacks` recorre queda **231, 176, 152**; la regla del
juego simulada en Python desde ese C da exactamente esa lista, centinela incluida.
`pc/check_field.py` corre esa comparación (arma `DW3_CB_DUMP=600`), exige que la capa del mapa
esté entre las volcadas y que su orden de registro **difiera** del orden de la lista (sin eso, un
shim que solo agregue al final pasaría en vano), y compara también la sonda, que es lo que mide
los empates y la centinela (la secuencia del campo no tiene prioridades repetidas).

**Sin diferencia visible, medido.** Las capturas de 600 y 900 cuadros son **idénticas byte a byte**
antes y después del arreglo (mismos md5) y los contadores de primitivos por cuadro no cambian: los
callbacks agregan sus sprites a la entry de la OT que fija el `depth` de cada objeto
(`FIELDSTG_drawActor` y `spriteDrawerSetLayer`), así que el orden de llamada solo decide entre
primitivos que caen en la **misma** entry de la OT. Y **sí comparten entry** en los cuadros
medidos: medido con una sonda de la OT en el cuadro 600, la entry 4 gana 8 primitivos y la 5 gana
4 después de `runCallbacks` (los dos actores y el objeto del mapa caen en la 4, las dos sombras en
la 5), o sea que la cadena de dentro de esas entries **sí** se invirtió y la captura salió igual:
el reordenamiento fue inerte a la vista en esos cuadros. Lo que **no** se midió es el efecto visual
del orden dentro de una entry (el volcado es de los callbacks, no de la OT); lo medido es que el
orden de la lista es el del juego y que la captura no cambió. El cuadro en blanco de T27 (desde el
624) tampoco cambió con T26: la captura de 900 seguía uniforme `1,1,1`; lo arregló T27, abajo.

### El entorno de dibujo por capa (T27)

**La causa, medida.** En la PS1 el `DRAWENV` de cada capa **viaja adentro de su propia ordering
table**: `layerDraw` copia `layer->env`, le pone el offset de la capa, lo escribe con `SetDrawEnv`
como un `DR_ENV` y lo cuelga de la cabeza de la OT de esa capa
(`src/engine/gfx/layer.c:130-151`), y `display.c:58-81` manda cada tabla por su cuenta. El host
aplicaba **un solo `DRAWENV` por cuadro**: `hostDrawFrame` corría `hostLayerApplyEnv` para todas
las capas **antes** de `PsyX_BeginScene` y `hostLayerDraw` solo hacía `DrawOTag`. El `PutDrawEnv`
de PsyCross copia el env a **un único global** (`activeDrawEnv`,
`external/psycross/src/psx/LIBGPU.C:311-315`) y cada *split* de la OT lo graba al parsear, así que
la última capa en aplicarse —el banner de área, `FIELD_LAYER_BANNER` `0x1003`, creado último en
`field.c:321`— le dejaba su clip a **todas**. El clip del banner **se encoge** al cerrarse
(`banner.c:212-218`: `y += 8`, `h -= 16` por cuadro hasta `h == 0`, y su `TASK_KILL` no lo
restaura: es el comportamiento de la PS1) y con el clip del banner como viewport/clip de todas las
capas, el cuadro se apagaba con él y quedaba en blanco en el 624.

El volcado lo muestra crudo (`DW3_ENV_DUMP=<cuadro>`, apagado por defecto; imprime el `DRAWENV`
propio de cada capa con su offset y su largo de OT, el env con el que **de verdad** se mandó su
tabla, sus `DR_TPAGE` —cuántos traían `dfe 0` antes del pase del host y cuántos llevan `dfe 1`
después, o sea lo que se mandó— y el env del clear del cuadro). **Antes**, cuadro 624 (recortado:
se omiten las líneas `frame …`, una por capa; están todas las líneas `env` del cuadro armado; el
volcado de entonces no traía todavía la línea `tpage` —entró con la mitad del `dfe`— y esta salida
se reprodujo con el código de antes del arreglo, el env de todas las capas aplicado antes de
`PsyX_BeginScene`):

```
env 624 0x1003 clear clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1000 own clip=0,0 320x240 ofs=0,0 bg=1,1,1 isbg=1 ot=2
env 624 0x1000 ot clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1001 own clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0 ot=2
env 624 0x1001 ot clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1002 own clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0 ot=16
env 624 0x1002 ot clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1004 own clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0 ot=2
env 624 0x1004 ot clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1003 own clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1 ot=2
env 624 0x1003 ot clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
```

(el `ot` de **todas** las capas es el env del banner, `0,120 320x0`; la propia del mapa, `0,0
320x240`, no llega a ninguna tabla).

**El arreglo, en dos mitades medidas.**

1. **Cada capa manda su propio env con su propia tabla**: `hostDrawFrame` aplica el env de la
   capa (`hostLayerApplyEnv`) inmediatamente antes de su `DrawOTag`, como el `DR_ENV` de la PS1
   viaja en su propia OT; y el clear del cuadro (`GR_Clear` de `PsyX_BeginScene`) usa el env de
   la **primera capa con fondo** en el orden del juego —la de abajo, `FIELD_LAYER_BACK` `0x1000`
   (`field.c:315-316`, `setBgColor(1,1,1)`), que es el primer clear que la PS1 correría; el
   banner de arriba tiene el mismo color y su clear es un no-op sobre el de abajo
   (`hostLayerApplyClearEnv`).
2. **Una sola superficie**: los `DR_TPAGE` del propio juego traen **`dfe 0`**
   (`SetDrawTPage(p, 0, 1, tpage)`, `sprite_drawer.c:146` y compañía): `dfe` es el bit 10 de la
   palabra, *drawing to display area*, y en `0` **prohíbe** dibujar al área de display. El juego
   lo pone así porque en la PS1 dibuja al **buffer trasero** (las dos áreas de `display.c:188-191`,
   a `y` y `y+256`; el buffer lo eligen los offsets del `DRAWENV`/`DISPENV` y el volteo, no este
   bit) y después voltea. PsyCross manda un split con `dfe 0` a su **superficie offscreen** (un
   back buffer de VRAM) y solo uno con `dfe 1` a la ventana; como el port dibuja **un solo buffer**
   (la ventana, ver el comentario del archivo), lo que quedaba en offscreen no se presentaba:
   medido, el mapa entero (todos sus primitivos siguen a un `DR_TPAGE`) iba ahí, la ventana
   conservaba el clear y los pocos primitivos sin `DR_TPAGE` delante, y la captura leía la
   superficie que el **último** split del cuadro hubiera dejado ligada. El host ahora pone el bit
   `dfe` de **cada palabra `DR_TPAGE`** de la tabla que está por mandar (`hostOtDrawToDisplay`),
   así todos los splits dibujan en la ventana: una superficie, que es lo que significa el buffer
   único del port. Son las palabras del propio juego en el buffer de primitivos del host, rehechas
   cada cuadro; nada más cambia en ellas. El recorrido que las voltea cubre la tabla entera: su
   cota es la longitud de la propia tabla más el buffer de primitivos del cuadro
   (`layer->otLen + GFX.primBufSize`; todo nodo de la cadena es una entry de la OT o un primitivo
   del buffer, y un primitivo ocupa al menos un byte), así que una tabla de 4096 entries
   (`otShift 0xC`) con su cola de prims también se voltea completa; una cota fija podía dejar esa
   cola en la superficie offscreen. Ningún modo medido tiene `DR_TPAGE`s en una tabla así: la cota
   es la de la tabla, no un número ajustado.

**La medición, después.** El mismo volcado, cuadro 624 (recortado: se omiten las líneas
`frame …`; están todas las líneas `env` del cuadro armado):

```
env 624 0x1000 clear clip=0,0 320x240 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1000 own clip=0,0 320x240 ofs=0,0 bg=1,1,1 isbg=1 ot=2
env 624 0x1000 ot clip=0,0 320x240 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1000 tpage total=0 dfe0=0 dfe1=0
env 624 0x1001 own clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0 ot=2
env 624 0x1001 ot clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0
env 624 0x1001 tpage total=0 dfe0=0 dfe1=0
env 624 0x1002 own clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0 ot=16
env 624 0x1002 ot clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0
env 624 0x1002 tpage total=45 dfe0=45 dfe1=45
env 624 0x1004 own clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0 ot=2
env 624 0x1004 ot clip=0,0 320x240 ofs=0,0 bg=0,0,0 isbg=0
env 624 0x1004 tpage total=0 dfe0=0 dfe1=0
env 624 0x1003 own clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1 ot=2
env 624 0x1003 ot clip=0,120 320x0 ofs=0,0 bg=1,1,1 isbg=1
env 624 0x1003 tpage total=43 dfe0=43 dfe1=43
```

(cada `ot` es el `own` de su capa; el clip del banner, `0,120 320x0`, queda solo en su tabla; el
clear usa el env de la capa de abajo, pantalla completa; y las palabras `DR_TPAGE` de las tablas
—45 en el mapa, 43 en el banner— traen `dfe 0` (`dfe0 = total`) y se mandan con `dfe 1`
(`dfe1 = total`), o sea que el host las puso todas a dibujar a la ventana). En el 610 el banner
todavía encogido a medias (`own clip=0,8 320x224`) y el mapa a pantalla completa: cada uno con lo
suyo.

Las capturas (`pc/build/boot <disco> <cuadro+1> <png> 0 "<guion>"`; la captura del target `N`
guarda el cuadro `N−1`), medidas por colores distintos del PNG:

| cuadro | antes de T27 | después |
|--------|--------------|---------|
| 610 | 2602 (el mapa) | 2610 (el mapa) |
| 620 | 1617 (perdiéndose) | 3021 (el mapa) |
| 624 | **1** (`1,1,1`) | **3241** (el mapa) |
| 630 | **1** | **3209** |
| 900 | **1** | **3237** |

Antes de la segunda mitad (solo el env por capa) la captura del 610 salía gris (`0.5,0.5,0.5`, el
color con el que PsyCross limpia su superficie offscreen) y la del 900 mostraba el clear y las
cuatro líneas de la capa de texto: el cuadro seguía repartido entre dos superficies. Las dos
mitades juntas lo dejan entero en la ventana.

**Lo que no cambió, medido**: las capturas del menú de idiomas (cuadro 40) y del título (cuadro
319) son **idénticas byte a byte** antes y después (mismos md5): esos modos ya dibujaban en la
superficie que la captura leía. `pc/check_country.py` no movió ninguna expectativa (su `tpage` se
calcula sin el bit `dfe`). Los otros ocho checks siguen verdes.

### La comprobación (`pc/check_field.py`)

Corre la misma demo guionada y verifica, **desde la salida del propio arnés**:

- que el run pasó por los modos `0x1600 → 0xE02 → 0xE00 → 0x2D7`, en ese orden;
- que el cargador del campo pidió el stage enlazado (`overlay: … stage overlay 0x1E9 … nothing is
  copied`);
- que, una vez creado el actor del jugador y liberado el control al cerrar el banner, el guion
  envía direcciones por el pad del juego y la posición del jugador cambia. Esto prueba movimiento
  básico en el host; todavía no prueba colisiones contra obstáculos;
- que el modo del campo creó sus cinco capas (`0x1000`-`0x1004`);
- que el código del campo dibujó en ellas y **siguió dibujando más allá del cuadro 468** (donde
  moría el cargador sin el guard; el 463 es la parada a propósito de T19), **más allá del 474** (el
  callback con el puntero truncado que T23 arregló) y **más allá del 562** (el puntero del comando
  de guion que T24 arregló);
- que los callbacks de la capa del mapa corrieron en el **orden por prioridad del propio juego**
  (T26): el check arma el volcado del shim (`DW3_CB_DUMP=600`), simula la inserción de
  `layer.c:280-320` sobre el orden de registro y exige la misma lista (centinela incluida), que la
  capa del mapa esté entre las volcadas, que su orden de registro **difiera** del de la lista
  (para que un shim que solo agregue al final no pase) y que la sonda de empates/centinela
  coincida también;
- que **cada capa mandó su propio `DRAWENV` con su propia tabla** (T27): el check arma el volcado
  del shim (`DW3_ENV_DUMP=610`) y exige que el env con el que se mandó la tabla de cada capa sea
  el de esa capa (el mismo volcado lo imprime como `own` y `ot`), que el clip propio de la capa
  del mapa sea la pantalla completa mientras el del banner que se cierra es el suyo (más chico:
  el clip del banner no puede ser el del mapa), que el clear del cuadro use el env de la capa de
  abajo (`0x1000`, la primera con fondo) y que los `DR_TPAGE` del propio juego traigan `dfe 0`
  (`dfe0 = total`, 45 de 45 en el cuadro armado) **y que lo mandado los traiga dibujando a la
  ventana**: `dfe1 = total` (45 de 45), el bit releído después del pase del host, así que sin el
  pase la comprobación falla por ese lado (probado: quitándolo, `dfe1=0`);
- que el **cuadro capturado sea una imagen, no un blanco** (T27): el check lee el PNG que deja el
  arnés (RGB sin filtro) y cuenta sus colores distintos, con un mínimo de `MIN_CAPTURE_COLORS`
  (64; medido **3213** en el cuadro capturado con el arreglo, **1** en el cuadro uniforme del que
  partió T27);
- que el run **alcanza el presupuesto del arnés** (900 cuadros, `presented 900 frames`) y **no**
  una señal, con el último cuadro dibujado en el del presupuesto (899): la evidencia de que T24
  arregló el camino de comandos;
- que, al alcanzar el presupuesto, el arnés **deja la captura** (un PNG de 320x240).

El check fija el **último cuadro dibujado** y el **PNG**, no la función que fallaba: el nombre
`WSTAG780_handleCommand800` es la causa medida con gdb, no algo que la salida pruebe. Tampoco
compara el cuadro contra la pantalla de un emulador: lo que mide del contenido es que la captura
tenga más de `MIN_CAPTURE_COLORS` colores distintos (una cota gruesa de "no está en blanco"), no
que cada píxel sea el correcto.

El check corre el arnés con `stdbuf -oL`: un proceso muerto por señal no vacía un stdout con
buffer de bloque, así que sin eso se perderían los últimos cuadros. Así entró un número viejo: la
cola con buffer de bloque del run sin guard terminaba cerca del cuadro 463 -- que además es la
parada a propósito de T19 -- y se leyó como el choque.

```sh
$ .venv/bin/python pc/check_field.py <imagen>.bin     # sin el binario: RED
RED: .../pc/build/boot does not exist yet; build it with `make -C pc`

$ make -C pc
$ .venv/bin/python pc/check_field.py <imagen>.bin
ok   the game's own code reached the country select (mode 0x1600)
ok   the game's own code reached the title overlay's opening movie (mode 0xE02)
ok   the game's own code reached the title screen (mode 0xE00)
ok   the game's own code reached the field (mode 0x2D7)
ok   the field's own loader asked for the linked stage overlay (0x1E9, nothing copied)
ok   the field created its own layers for the mode (0x1000, 0x1001, 0x1002, 0x1003, 0x1004)
ok   the field's own code drew on its layers (609 frame(s); last: layer 0x1002 poly=44 sprite=25 env=45)
ok   the field kept drawing past the unguarded fault frame 468 (T19 stopped at 463; last field frame 899)
ok   the field kept drawing past T22's truncated-pointer fault frame 474 (T23's host fix; last field frame 899)
ok   the field kept drawing past T24's truncated-pointer fault frame 562 (T24's fix; last field frame 899)
ok   the map layer's callbacks ran in the game's own priority order (registrations [152, 176, 231] run as [231, 176, 152])
ok   the insert rule's ties and sentinel priority (the 7-entry probe with 2 tied registrations and a 0x7FFFFFFF one runs as [2147483647, 2147483647, 9, 9, 9, 7, 3, 1])
ok   the map layer's registration order differs from the run order ([152, 176, 231] registered, [231, 176, 152] run), so an append-only shim would fail this
ok   every layer's table was sent with its own DRAWENV (frame 610: 0x1000 clip=0,0 320x240, 0x1001 clip=0,0 320x240, 0x1002 clip=0,0 320x240, 0x1004 clip=0,0 320x240, 0x1003 clip=0,8 320x224)
ok   the map layer keeps its own full-screen clip and the closing banner its own shrinking one (frame 610: map clip=0,0 320x240, banner clip=0,8 320x224)
ok   the frame's clear uses the bottom layer's background env (0x1000, clip=0,0 320x240, bg=1,1,1)
ok   the game's DR_TPAGEs carry dfe 0 and the host flipped every one to draw to the display (frame 610: 0x1002 total=45 dfe0=45 dfe1=45)
ok   the run reached its own frame budget: presented 900 frames and exited cleanly, last drawn frame 899 (T24's fix; the field's script-command path no longer truncates its task pointer)
ok   the run captured its frame to a 320x240 PNG (230723 bytes)
ok   the captured frame is a picture, not a blank (3213 distinct colours, >= 64; T27's blank frame had 1)
the game's own code walked the country select, the title overlay's movie and title screen and the title's own menu into the field; the field's own code drew past the stage-without-slots point T19 stopped at, past the truncated-pointer callback T22 measured (T23's host fix) and past the truncated script-command pointer T23 measured (T24's fix), ordered its layer callbacks by priority as the game's own layer does (T26), sent each layer's own DRAWENV with its own table on one drawing surface (T27), and ran to the harness's own frame budget with a 320x240 capture that is a picture, not a blank
```

**El control negativo** (el guion del título, sin `newgame`) es lo que muestra que la
comprobación falla por el motivo correcto: el campo nunca se entra y el check lo dice con los
modos que vio (y con las comprobaciones de T27 que piden las capas del campo).

```sh
$ .venv/bin/python pc/check_field.py <imagen>.bin down,down,down,down,start,wait,start
ok   the game's own code reached the country select (mode 0x1600)
ok   the game's own code reached the title overlay's opening movie (mode 0xE02)
ok   the game's own code reached the title screen (mode 0xE00)
FAIL the mode for the field: the run never reached the field (mode 0x2D7); modes seen: ['0x1600', '0xE02', '0xE00']
FAIL the field's stage overlay: the run did not report the linked stage overlay the field's own loader asked for (pc/shim/host_overlay.c)
FAIL the field's own layers: the field mode created [], missing ['0x1000', '0x1001', '0x1002', '0x1003', '0x1004']
FAIL the field's own drawing: no frame after the field mode drew on the field's layers; layers seen: []
FAIL the per-layer DRAWENV dump: the run printed no per-layer env dump for frame 610 (pc/shim/gfx_engine.c prints it when DW3_ENV_DUMP is set; this check sets it)
FAIL the run's end: the run reached its budget but the field's last drawn frame was None, not 899
ok   the run captured its frame to a 320x240 PNG (230723 bytes)
ok   the captured frame is a picture, not a blank (1183 distinct colours, >= 64; T27's blank frame had 1)
6 check(s) failed
```

`check_field.py` escribe su captura en un directorio temporal, así que una corrida del check no deja
nada atrás (la captura del demo, `build/t19_field.png`, es del target `field-demo`).

## La cadena post-intro y el arnés de pasos exactos (el tránsito de escenas)

Aquí queda el contrato del
arnés. La regla del dueño: **cada pulsación la pide el propio motor** — nada
de cadencia ni "press until" a ciegas. Los tokens nuevos del guion
(5º argumento de `pc/build/boot`, ver `pc/shim/host_pad.c`):

- `field` — barrera: espera a que el juego esté en un modo de campo con el
  actor del jugador. **Composición:** una barrera `field` solo va *después* de
  que el modo ya abandonó la página 2 (un drain menciona el modo de salida al
  terminar porque la cadena salió del campo); un solo `talkdrain` abarca
  tantos campos como la cadena visite seguidos.
- `talkdrain` — el drenaje exacto de diálogos del campo. Hay dos clases de
  espera y cada una pide su botón:
  1. *pausa de marcador* — una ventana de texto en `state 2, substate 1`
     responde al botón `TEXT_WAIT_BUTTONS[step]` del propio marcador
     (`text_window.c:814,887`);
  2. *pausa de página* — la caja de habla a nivel box avanza con cualquier X
     (`updateTalkBox` case 1, `talk_box.c:299-303`): llamada `showPage` por el
     flanco de X, sin flecha.
  Termina cuando el modo deja la página 2 (la cadena salió a otro modo).
- `namedrive` — la pantalla de nombre (`STPLNMET`, `0x500`), mapeada de
  `src/menus/player_name/`:
  - la bienvenida muestra su página con cualquier X mientras su texto no
    termine (`welcome.c` case 2 — `showPage` incondicional, como la caja);
  - el teclado (`substate 2` y `active`): el buffer ya viene **pre-llenado**
    con el nombre por defecto del juego (la pantalla lo recibe: `GAME.name`,
    `src/menus/player_name/name_entry.c:49`; el `set_name.inc.c` compartido
    lo convierte y rellena), así que el arnés no teclea letras: **START**
    aparcón al cursor en `NAME_KEY_END` (fila 6 col 13,
    `include/shared/name_entry.h:24`), luego X para aceptar (nombre no
    vacío → `substate 100`). Mantener el nombre sugerido es exactamente el
    camino del jugador que lo acepta; una revisión anterior tecleaba dos
    letras y dejaba un prefijo espurio "AA" en el nombre de cada diálogo
    posterior;
  - la página de partners confirma su set con una X (`choice.c:101-113`,
    cross → `substate 100`; triangle volvería a 200);
  - el confirm tiene el cursor en Yes por defecto: una X y el progreso corre
    solo; al terminar, `screen.c:165` pide `0x2D8` — **el juego vuelve al
    campo**.
  - `namepick:<n>` (antes o después de `namedrive`): en la página de equipos
    pulsa abajo/arriba hasta que el cursor (`PartnerChoice.choice`, leído del
    estado de la página) esté en el equipo `n`, y recién entonces confirma
    con X; `namestop` termina la corrida en esa página (captura) en vez de
    confirmar. `mode:500,0,namedrive,...` también arranca el guion: la
    pantalla de nombre sola, sin el título (el guion normal esperaba el menú
    de país).
  Los tasks de STPLNMET no tienen id de registro: el arnés camina el árbol
  (`mode` → `scene` → `screen` → los 4 miembros) comparando punteros de
  `update`.
- `gymdrive[:<k>]` — el gimnasio (`STGTRAIN`, `0xA00`), mapeado de `src/menus/gym/` (A4b): `mode:A00,0,gymdrive:<k>` abre el gimnasio y recorre un
  entrenamiento hasta que existe la tarea del resultado (`win->result`: de ahí sale el actor con el conjunto de sprites que se prueba). Cada
  pulsación responde a un estado que la propia tarea tiene: el partner (subestado 5 de la pantalla, X), el entrenamiento (subestado 10 del menú:
  `k` veces derecha y X; el `k`-ésimo de la fila de arriba, `k` = 0 el primero), la intensidad (subestado 6 de la sesión, X) y el Yes (subestado
  0x12, X). El partner es el primero del equipo (`DW3_STARTER_SET` lo cambia). Sin tiempos: el arnés busca el árbol `mode` → `scene` → `screen`
  por punteros de `update` como `namedrive`. Con `DW3_OT_DUMP=<cuadro>` y `DW3_VRAM_DUMP=<ruta>` (cuadro 1000) la corrida es determinista: dos
  corridas del mismo binario dan la misma captura y el mismo volcado de VRAM.
- **Fin de la corrida por estado** — ninguna corrida del arnés ni escenario de
  `pc/review.sh` termina por agotar el presupuesto de cuadros. `until:<hex>`
  termina la corrida en el primer cuadro después de que el juego estuvo en ese
  modo y lo dejó (`XX00` compara por byte alto, otro valor exacto; log `end:
  left mode 0x.. for 0x.. -> end of run`). `mode:<hex>` lo implica, y las
  fichas de batalla (`battle`, `battle:`, `battle@`, `battleauto`) implican
  `until:600`; `battlepilot` y `fieldauto` no implican nada (corren en un
  campo real). `until:` explícito gana, y `stay` apaga todo fin implícito
  (juego libre: `pc/review.sh libre`). `movieskip:<n>` salta una película con
  Start (`src/menus/title/movie.c:173`) tras `n` cuadros suyos (contador de
  cuadros entregados de la capa St; 15 cuadros/s).
- **Crash forzado** (`--crash-test[=segv|abort|fpe|bus|ill]`, R1) — una opción de la línea de comandos (no una ficha de `<keys>`): provoca el fallo
  elegido (`segv` por omisión: una escritura real a la dirección 0) una vez abierto el log, para probar el reporte de crash (sección "Reportes de
  crash"). Se saca de `argv` antes de leer el resto: `boot --crash-test` solo es un arranque de jugador (log `dw3-*.log`), `boot <disco> --crash-test`
  una corrida del arnés (stderr; en Android `logs/harness.log`). En Android van como extra del intent: `--esa args --crash-test` o
  `--es argv 'disco.bin|--crash-test=abort'`. Un valor desconocido sale con código 2.
- **Tabla de warp generada** (`warp:<escenario>[,<progreso>]`) — `pc/shim/warp_stages.inc` la
  genera `pc/gen_warp_stages.py` (`.venv/bin/python -I pc/gen_warp_stages.py`, determinista) a
  partir de `FIELDSTG_stages`/`FIELDSTG_extraStages` (`src/field/field_mode/data/fieldstg.c`) y del
  `setupStage` de cada `wstagNNN.c`: una fila `{escenario, modo, inicio, dirección, progreso}` por
  escenario. En EU, `FIELDSTG_pickStage` (`src/field/field_mode/state.c`) busca el modo primero en
  `FIELDSTG_extraStages` solo si `GAME.progress == FIELD_PROGRESS_EXTRA` (0x2D) y si no, en
  `FIELDSTG_stages`; por eso la columna `progreso` es -1 (la tabla normal lo tiene) o 0x2D (solo lo
  tiene la tabla extra; el warp lo pone si la ficha no trae su propio progreso). Los escenarios sin
  inicio literal o sin fila en ninguna tabla quedan en un comentario del `.inc` (hoy: 260, sin fila
  en las tablas EU).
- **Barrido de escenarios** — `pc/stage_sweep.sh <dir> [disco] [cuadros]` corre
  `stay,warp:<N>` para cada escenario de la tabla y deja `sweep.tsv` (escenario, exit, último modo,
  si el warp disparó, línea de caída, segundos, CPU %, RSS máx. en KB), con logs y capturas en
  `<dir>`. `JOBS` fija corridas en paralelo (1 por defecto: el CPU % solo es comparable con
  pocas).
- **`fieldbattles:<n>`** — desde un campo real, arranca n batallas al azar del área
  (`FIELDSTG_startAreaBattle`), cada una con el campo quieto (240 cuadros) y de vuelta de la anterior;
  se combina con `battlepilot`. Regresión del cuelgue de la 2a batalla:
  `stay,warp:330,optlang:4,fieldbattles:2,battlepilot` (2 cuadros `0x1400`, exit 0 al llegar al presupuesto).

- **Battle speed** (opción Display, clave `battle_speed`: Normal / x1.5 / x2; por defecto Normal, que no cambia nada) —
  `hostDrawFrame` (`pc/shim/gfx_engine.c`, `battleSpeedGate`) escala solo el paso de `GFX.frameTimeCounter` (8.8 fijo:
  x1.5 alterna 1 y 2 cuadros), que es el reloj de las animaciones de batalla. `GFX.time` (parpadeos de la UI), el contador de
  cuadros y el tiempo de juego no se escalan. Compuerta: modo batalla (0x600), tarea de menú `BATTLE_TASK_MENU` en estado 1,
  tarea `BATTLE_TASK_PLAYER_TURN` ausente o en `substate 0` (el jugador no elige comando) y `FIGHTSTG_battle.speed.mode == 0`
  (no se duplica el giro lento de cámara de la intro). Con el menú de comandos abierto va a velocidad normal. La música
  y el XA van por el reloj de PsyCross y no se aceleran; los efectos de sonido duran lo mismo, así que en x2 pueden pisarse.
  La lógica pura está en `pc/shim/battle_speed.c` (prueba en `inputtest`). `DW3_BATTLE_SPEED_LOG=1` imprime a stderr cada
  cambio de compuerta (`battle-speed: gate N frame F`).

- **Map loading** (pestaña Enhancements, clave `map_loading` en `[enhancements]`: Fast / Classic; por defecto Fast) — el
  campo carga el mapa en bloques de 128 px y descomprime uno por cuadro, así que la rejilla azul se ve ~0,6 s al entrar a
  un escenario y al desplazarse. **Fast**: tras cada cuadro, `mapLoadingFrame` (`pc/shim/stage_timing.c`) descomprime y sube
  en ese mismo cuadro los bloques de la vista ya leídos (tope `map_tiles_per_frame`, 12); los 12 espacios de VRAM y el
  dibujado siguen siendo los del juego. **Classic**: el original.
- **Stage banner** (opción Display, clave `stage_banner`: Fast / Classic; por defecto Fast) — qué hace el port con el
  banner del nombre del lugar al cambiar de escenario. **Classic** es el original: el escenario queda oculto (el port
  recorta la capa del mapa en `hostLayerEnvForFrame`, `gfx_engine.c`) y el jugador congelado hasta que termina el banner
  (2.6 a 3.4 s). **Fast**: `stageBannerFrame` (`pc/shim/stage_timing.c`, una vez por cuadro desde `hostDrawFrame`) espera a
  que el campo termine de cargar (tarea del campo en `state 0 substate 4`), pasa el banner a `TASK_DONE` (con lo que
  `showField` deja correr el campo), muestra todas las cajas, acelera el tipeo y descongela al jugador cuando el campo
  corre (`playable` en el cuadro ~26 en vez del ~175); el nombre se queda encima del escenario visible y se va solo. Solo
  escribe `frozen` mientras maneja el banner que armó, y lo vuelve a poner en 1 si el campo empieza a cerrarse (el
  `TASK_KILL` del banner lo pondría en 0). Los tiempos del modo rápido están en `pc/shim/stage_banner.h`
  (`STAGE_BANNER_FAST_*`). Los bloques azules con rejilla que se ven al principio son la cobertura del propio juego para
  los mosaicos del mapa aún sin descomprimir (`FIELDSTG_drawCoverBlock`, `map_streamer.c`): aparecen sobre el escenario hasta
  que cada mosaico carga (~1 s). Con `DW3_STAGE_TIMING=1` el trace añade `fast banner: stage shown / player released`.

- **Pestaña System** (menú F1, después de Enhancements): "Restart game" y "Quit game". Cada una pide confirmación: el
  primer Confirm cambia el valor de la fila a "Press again to confirm" y solo un segundo Confirm en la misma fila (dentro
  de `OPT_SYS_CONFIRM_MS`, 4 s) actúa; mover el cursor, cambiar de pestaña o cerrar el menú la cancela (`csNavEpoch`,
  `controls_screen.c`; el modelo puro está en `pc/shim/options_system.c` y lo prueba `inputtest`). Quit empuja un
  `SDL_QUIT` (el mismo camino que cerrar la ventana: `PsyX_Exit`, `exit(0)`, los `atexit`) tras guardar los ajustes
  pendientes. Restart hace lo mismo y un último `atexit` (registrado antes de PsyCross en `boot.c`, así corre cuando el
  audio y la ventana ya se cerraron) vuelve a lanzar el programa con la misma línea de comandos: en Linux `execv` del
  ejecutable (`/proc/self/exe`, o `$APPIMAGE` si existe, para pasar por el AppImage); en Windows `CreateProcessW` sin
  heredar handles y con los argumentos entrecomillados, más `--wait-pid <pid del viejo>`: el proceso nuevo espera (hasta
  30 s) a que el viejo termine, en lo primero de `main` y antes del log, SDL u OpenAL (`optionsSystemTakeParent`), y deja
  la línea `system: restarted, the old process (pid N) was gone after N ms` en su propio log. (Antes `_wspawnv` heredaba el
  disco y el log, y el nuevo arrancaba mientras el viejo seguía cerrando; además el `stdout` del viejo, con búfer
  completo en Windows, se volcaba al final y parecía la sesión del nuevo en el mismo log.) Prueba: `make -C pc
  check-restart` (nativo) y `make -C pc TARGET=windows check-restart` (Wine: el nuevo arranca después del `old-end`). La tarjeta de memoria se escribe y cierra en cada operación, no hay
  nada que vaciar.

- **Pestaña Enhancements** (menú F1, después de Audio): agrupa las opciones que cambian el ritmo del juego. Velocidad
  de batalla y banner se mudaron aquí desde Display y siguen en `[display]` de `settings.ini` (`CsWidget.section`,
  `controls_screen.c`), así que los archivos viejos valen. Tres opciones nuevas en `[enhancements]`, todas Fast (por
  defecto) / Classic (el original, sin tocar). Los tiempos del modo rápido están juntos en `pc/shim/fast_transitions.h`
  (`FAST_*`) y la lógica de juego es el sondeo `fastTransitionsFrame` (`stage_timing.c`, desde `hostDrawFrame`), que solo
  ajusta el estado de las tareas del juego, sin tocar `src/` ni `include/`:
  - `door_transition`: el cierre del campo al salir por una puerta (`FIELDSTG_closeField`) corre 3 veces más rápido
    (mismas formas del recorte y del fundido): 56 cuadros pasan a 23. En Fast el puerto además no dibuja la cubierta
    de cuadrícula azul que el juego funde sobre el escenario que se deja (`FIELD_LAYER_COVER`, `stageExitCoverHidden`
    en `gfx_engine.c`): queda la vista que se encoge sobre negro. En Classic se dibuja como la del original: una
    cuadrícula tenue que se funde poco a poco alrededor de la vista que se encoge.
  - `battle_transition`: la espiral de losetas del campo a la batalla mueve 3 losetas por cuadro (antes 1) y la espera final
    es de 4 cuadros (antes 16): 61 cuadros pasan a 22.
  - `battle_results`: las animaciones de apertura y cierre del reporte de victoria (paneles y fundido final) duran un
    tercio; los mensajes y las esperas de botón no se tocan ni las recompensas: antes del primer mensaje 39 pasan a 21,
    después del último 37 a 17 (reporte de 125 a 84 cuadros).
  Los 31 cuadros del inicio de batalla al primer menú no se acortaron: son carga y turnos.
  `DW3_STAGE_TIMING=1` ahora también traza la tarea del menú de batalla (`battle menu`, con `step`).

- **Compañero en una forma y nivel** — `partner:<miembro>,<forma>[,<nivel>[,<hp%>]]` (miembro 0-2, forma
  `PartnerEntry.id` >= 3, nivel 1 por defecto; `hp%` 1-100 opcional deja sus PV en ese porcentaje del máximo, para
  que curar con una técnica de campo muestre efecto) pone esa forma en el primer espacio de batalla del miembro,
  con las funciones del propio juego (`addPartnerEntry`, `STFGTREP_addDigimonExp` hasta el nivel,
  `STFGTREP_addSkill`/`STFGTREP_learnSkill`, `setPartnerSlots`), así que aprende las técnicas que le tocan,
  también las de mods. Se aplica al disparar `mode:` o una batalla (`battle`, `battleauto`); solo no hace
  nada. Imprime `pad: partner seeded: ...`.

- **Ficha de encuentro y barrido de batallas** — `battleenc:<n>[:<escenario>[:<música>]]` abre
  la batalla real con `FIELDSTG_encounters[n]` del propio juego (arreglos enlazados, no una tabla
  generada): llena `BATTLE_SETUP` como `FIELDSTG_startEncounter` (enemigos, emboscada, tipo,
  bloqueos; sin el premio) y como `FIELDSTG_startBattle` (escenario de pelea, por defecto 10, y
  banco de música, por defecto 2), con el equipo inicial de `padBattleFire`. Un índice fuera de la
  tabla no hace nada (se valida que los punteros caigan en `FIELDSTG_enemies`). Se combina con
  `battleauto`. `pc/battle_sweep.sh <dir> [disco] [cuadros]` la corre para cada encuentro (la
  cantidad sale del tamaño del símbolo; `IDS="n n:s:m"` elige) y deja `sweep.tsv` (id, exit, línea
  de fin, resultado win/loss/fled, caída, segundos, CPU %, RSS). Una pelea dura hasta ~2 min
  (el juego va a 50 cuadros/s), así que el barrido completo con `JOBS=2` tarda ~2 h.
- **Piloto de cartas** (`cardauto[:<arg>]`, M6b) — abre `MODE_CARD_GAME` 0x700 con
  el arg del oponente (`oponente * 2 + tipo + 1`; por defecto 3 = oponente 1 de
  Asuka City, `src/field/stages/asuka_city/wstag270.c`) e implica `until:700`. Como `battleauto`, lee el estado
  del propio juego (la tarea `CARDGAME_updateBattle`: `run`, `phase`,
  `phaseStep`, `record.turnState`, `effectStep.id/state`) y aprieta la tecla que
  el juego espera; la lista de estados está en el comentario de `padCardDecide`
  (`pc/shim/host_pad.c`). Log: una línea `card N:` por cada cambio de estado y
  `auto N:` por tecla. El mazo es el inicial; ante un estado sin regla, el piloto
  espera sin tecla (el log muestra el estado donde quedó).
- **Teclas del arnés** — `up down left right start select cross circle triangle
  square` y, desde M6a, `l1 r1 l2 r2` (el álbum de cartas pasa de página con
  L1/R1; los nombres de `battle@` aceptan dígitos tras la primera letra).
- **Texto veloz** — host-side, sin tocar código del upstream: cada flanco de X
  con una ventana escribiendo (`state 1`) baja el ritmo al piso del propio
  motor: una letra por cuadro, a mitad de página y **sin reiniciar el texto
  ya visible** (`win->typeDelay = 1`; el caso "all at once" del setter,
  `text_window.c:393-402`, completa la página pero silencia el sonido por
  letra de `updateTextWindow` state 1, `:794-796`). El motor sigue tecleando
  y tocando el sonido por letra con el `typeSound` de cada ventana — los
  datos del juego deciden cuál — igual que el retail, solo a velocidad
  máxima; ventanas con `typeSound` 0 quedan mudas como su propio dato.
  Vale en corridas guiadas y en tu juego interactivo; las ventanas pausadas
  no se tocan (su liberación ya es una pulsación pedida por el motor, y el
  resto de su página sigue tecleando a su cadencia de datos, con sonido).

La cadena del hito, con los conteos medidos (test disc del dueño):

```sh
pc/build/boot "<imagen>.bin" 20000 captura.png 0 \
  "down,down,down,down,start,wait,skipmovie,waittitle,startuntil,waitoptions,newgame,confirm,cross,field,talkdrain,namedrive,field,talkdrain"
# log: dialog drain done after 101 exact press(es) (mode 0x500)  <- 780+785
#      name drive done after 17 exact press(es) (mode 0x2D8)    <- el nombre
#      boot: mode 0x2D9 (FIELDSTG (the field))                  <- WSTAG790
#      dialog drain done after 128 exact press(es) (mode 0xE03) <- familia STDWTITL
#
# (Los conteos con tecleo real: la revisión anterior tecleaba "all at once" y
# colapsaba pausas del propio marcador — con 1 letra/cuadro TODAS las pausas
# de marcador y de página aparecen y el arnés las responde, como el retail.)
```

La comprobación del hito (presupuesto 20000, pin de esos tres conteos y del
orden de modos `0x2D7 → 0x2D8 → 0x500 → 0x2D8 → 0x2D9 → 0xE03`):

```sh
.venv/bin/python pc/check_field.py "<imagen>.bin" milestone
```

Diagnósticos del arnés (todos en `pc/shim/host_pad.c`): el `wait` del drain
imprime el estado interno de cada ventana cuando no encuentra pausas
(`padPrintWindowStates`), y una muerte silenciosa (cerrar la ventana del
juego = `PsyX_Exit` en `PsyX_main.cpp:700-711`) ahora queda anotada en la
salida por el `atexit` del shim (`pc/boot.c`, `pcBootExitNote`).

## Huecos honestos (lo que esto NO prueba)

- El volcado es **el archivo tal como el juego lo tiene en el heap**: `sectorCount * 2048`
  bytes, con el relleno del último sector. El juego no conoce el largo exacto en bytes
  (la tabla solo guarda sectores), así que el largo real del archivo no se prueba acá.
- **Tamaño de puntero**: la PS1 tiene punteros de 32 bits; en x86-64 son de 64. Las
  estructuras del juego están documentadas con offsets fijos (`/* 0x24 */ ...`); en el host
  esos offsets **no** se cumplen. Los siete casts de puntero que truncaban en el host ya se
  arreglaron en su lugar (T6, `docs/decisions/ADR-003-upstream-coexistence.md`), con un
  puente por `size_t` porque la aritmética de punteros hacía que GCC 2.8.1 emitiera los
  operandos al revés y el objeto dejaba de matchear. Verificado: `make VERSION=eu compare`
  sigue en 316 OK y los objetos afectados son idénticos byte a byte. Queda **un aviso**, en
  `src/battle/battle_mode/cursor.c:13` (`(MenuCursor *)arg`, donde `menu.h` declara `arg`
  como `s32`): esa unidad ni siquiera entra en el build de `pc/`.
- **Direcciones fijas**: `HEAP_END` (`0x801FF000`), `OVERLAY_ADDRESS` y `SUB_OVERLAY_ADDRESS`
  siguen siendo direcciones de la PS1; en el host valen por los shims, no por el mapa real.
- **`getFilePos`/`CdIntToPos`**: se aporta `CdIntToPos`, pero el lector del host no usa
  `loc` (lee por LBA directo). Que compile no prueba la conversión MSF/BCD.
- **`GFX` es un stub**: solo `getTime` (constante 0). Nada de dibujo, y el orden de desalojo
  de la caché por tiempo no se ejercita con el reloj quieto.
- **`loadFont` sí entra desde el hito 5** (`font_frame`): `initTimLoader` es el del juego y la
  carga de la fuente en VRAM se prueba con los bytes del disco. Los arneses sin GPU (hitos
  2/3) siguen con el stub de `pc/shim/host_tim.c` y no cargan fuentes. `loadFont` además
  pide `FILE_MENU_SPRITES` con `FILE_CACHE.request` (la *encola*, no la lee ni la sube): eso
  queda sin ejercitar, y no hace falta para el texto porque el CLUT de la fuente viene dentro
  de su propia imagen.
- **Solo el estilo 1**: el hito mide con `FONT_STYLES[1]` (el del banner de área, líneas de
  14 px). Los estilos 2 y 3 (11 y 9 px) se compilan (están en `font.c`/`text_tools.c`) pero
  no se ejercitan, ni sus tablas `FONT_GLYPHS_2/3`.
- **Solo modo 0 de `decodeChar`**: las cadenas de los lang files no son Shift-JIS
  (`text->sjis == 0`); la rama Shift-JIS (los mapas `sjisMap`/`iconMap`) no se prueba.
- **Solo los códigos de control que aparecen**: el salto de línea (`0x02 01`) sí; el código 5
  (insertar sub-texto, donde `measureText` recursa en los buffers de trabajo) y los demás no
  aparecen en estas cadenas. El arnés arma los 6 `TextBuffer` que la recursión espera, pero
  no hay cadena que la dispare.
- **Solo la copia española** (SPN) de 3 de los 50 lang files. Los otros idiomas y archivos:
  sin verificar.
- **Ancho fijo**: se pasa `spacing` 0 (proporcional, el del banner). El modo de ancho fijo
  (`spacingX != 0`) no se prueba.
- **T14 no ejercita el bloque de CLUT de un TIM**: los dos TIMs de la fuente no lo traen
  (`hasClut` en 0); la rama de `timLoaderLoad` que sube un CLUT con `setClutPos` se compila
  pero no corre. La paleta de esta fuente vive dentro de la imagen.
- **T14 dibuja una sola cadena y con los valores por defecto** (el `SSSTNAME` español,
  cadena 71, estilo 1): los otros estilos, los otros idiomas y las cadenas con códigos de
  control (salto de página, inserción, nombre del jugador) no se prueban. `check_font.py`
  necesita `DISPLAY`: abre la ventana de PsyCross (los otros cuatro checks son *headless*).
- **`pc/ps1_types.h` solo arregla `u_long` en `tim_loader.o`**: las demás unidades del host
  siguen viendo 64 bits. Es a propósito (no cambiar lo ya verificado), pero significa que
  cualquier otra unidad que camine datos con `u_long *` va a necesitar el mismo tratamiento.
- **El PNG lo escribe el arnés** (`pc/font_frame.c`) con bloques deflate *stored*: el archivo
  es más grande que uno comprimido y no se probó en otros visores más allá del de este host.
- **La captura sin X**: el hito 5 guarda con `glReadPixels` (el PNG); con `xwd`/`x11grab` la
  ventana sale negra por el *direct rendering*, como en T13. Los colores del texto no se
  compararon contra un emulador, solo contra la tabla de glifos y los bytes del disco.
- **`long`**: en MIPS o32 mide 32 bits, en x86-64 Linux mide 64 (p. ej. los `open/read/
  write/close` que declara `include/engine/game.h`).
- **Endianness**: PS1 y host son *little-endian*, por eso el magic `RLEN` (`0x4E454C52`) y
  la tabla de sectores coinciden. En un host *big-endian* no.
- **Nada de `libgs`/`libsnd`/`libpress`/GTE en los hitos 0-2**: ese camino no llama a `Gs*`,
  `Ss*`, `DecDCT*` ni `gte_*`. Queda el grueso del trabajo (T1 midió 155 llamadas). El hito 3
  sí usa el GTE (las rotaciones del texto escalado), pero solo del que PsyCross cubre. El
  hito 6 empieza a cubrir `libgs` (tres llamadas) y `libsnd` (un stub); el hito 8 suma
  `libpress`/MDEC y el *streaming* `St*` **como stub** (no decodifican nada: ver T18), y el XA
  sigue sin cubrirse.
- **El hito 6 tiene un check del arte desde T16** (`pc/check_country.py`): los bytes del
  banco de la selección de país en la VRAM y el sprite del panel izquierdo contra el disco.
  Que el resto de la pantalla se vea bien se sigue mirando con los ojos (la captura), y el
  panel izquierdo **no** está arreglado: ver "El arte de la selección de país (T16)".
- **El hito 6 no ejerce el sonido**: no hay audio (`libsnd` es un stub). La **entrada sí** desde
  el hito 7: el pad del juego lee un pad de verdad y la pantalla de idiomas responde (ver "El
  input del juego (T17)"). Con Start **sí** se ejecuta el camino de `requestMode`/cierre de modo
  del bucle de `main`: pide `MODE_OPENING_2`, que desde el hito 8 **sí** está enlazado.
- **El hito 6 corre un solo modo a la vez**: `MODE_ENTRY_POINTS` tiene 23 puntos de entrada y en
  el binario sólo están el de país, desde el hito 8 el de título (`0xE`) y desde el hito 9 el del
  campo (índice 2, `FIELDSTG`); pedir otro lo dice y se detiene.
- **El hito 7 sólo ejercita el menú de la selección de país para mover el resaltado**: el input
  llega hasta el `requestMode` del confirmar, y desde el hito 8 la demo del título usa el mismo
  camino para saltear el video; los demás modos no se ejercitan por acá (el campo, enlazado desde
  el hito 9, lo navega su propia demo).
- **El hito 8 ya muestra el video, y ritmado**: `DecDCT*` y `St*` son reales desde T37-T40 (ver
  "La pantalla de título y el video"): el `.STR` se ensambla, el MDEC decodifica cada cuadro a
  15 bits, lo presenta en la VRAM y el port lo dibuja en la ventana como quads texturados
  (PsyCross no copia la VRAM a la ventana, sólo dibuja primitivas). Desde T40 el cuadro N se
  entrega cuando el reloj del XA lleva `N * 2520` muestras (las películas son de 15 fps), así
  que el video sigue al audio; medido: 15,00 fps, 0 *underruns*. La ventana abre 320x240, de modo
  que el display de 320x480 se veía a media altura (imagen de 320x208 con 16 filas negras arriba
  y abajo); desde T41 `DW3_WINDOW=640x480` la abre a 640x480 y el cuadro ocupa sus 416 filas y
  640 columnas, la geometría del modo entrelazado de la PS1. El juicio final de sincronismo es
  del dueño, con una corrida larga.
- **La pantalla de título del hito 8 se alcanza salteando el video**: no se probó el camino en
  que el video termina solo (ahora puede: hay stream, pero el de apertura dura cientos de
  cuadros, más que el presupuesto de las demos), ni el *splash* (`STDWTITL_SPLASH_MODE`,
  13), ni el menú de opciones del título (el aviso de pulsar el botón de inicio se ve; el
  hito 8 no lo navega —el hito 9 sí confirma la opción de empezar partida nueva—, y la
  opción de continuar sigue sin enlazar: `MODE_CONTINUE`, 0xC00, cargar partida de la
  memory card). La música del título (`STDWTITL_TITLE_MUSIC`) es silencio (`libsnd` es un
  stub), aunque el **cargador de bancos** del juego sí corre: el *loader* del título espera a que
  el banco 0x47 se cargue leyendo sus VAB del disco.
- **El hito 8 prueba un solo idioma**: la demo selecciona el quinto recurso lingüístico
  (`LANGUAGE` 6), así que carga el
  arte de título de `STDWTITL_titleImages[6]` (archivo 0x94D, el mismo que comparten DE/IT/ES en
  la tabla EU) y sus sprites de menú en español. Las demás opciones de la lista, incluida la
  primera entrada (que llega al título por `MODE_OPENING`, 0xE01, el video 0), quedan sin
  verificar.
- **El hito 9 dibuja el mapa y corre su presupuesto**: el modo
  del campo arranca, crea sus capas y dibuja; su cargador leía un `FIELDSTG_state.slots` en NULL
  (un stage sin slots, `WSTAG780`) que la PS1 tolera y el host no, y T22 le puso el guard del host
  (`pc/field/file_loader.c`) que el propio campo ya tenía (`field.c:621`). T23 hizo que la capa del
  host pase al callback de dibujo el puntero entero que el juego registra (`DrawCallback.arg` es
  `s32`; ver "El callback de la capa y el puntero entero"), así que el campo dibuja sus objetos del
  mapa. T24 ensanchó los `s32` con que el propio campo transportaba el puntero de la tarea de un
  comando de guion (en `src/**`, con el matching revalidado), así que el run ya no muere y corre sus
  900 cuadros. T27 hizo que cada capa mande su propio `DRAWENV` con su propia tabla y que los
  `DR_TPAGE` del juego (`dfe 0`: dibujar al área de display, prohibido) se dibujen en la única
  superficie del port, así que el cuadro ya
  no se pierde con el clip del banner de área: medido, una captura a 600 cuadros muestra el mapa
  (con su rótulo de área y el de la esquina, texto del juego que no se cita) y las del 624, 630 y
  900 también (2.600-3.200 colores distintos; antes el 624 en adelante salía uniforme `1,1,1`
  aunque la capa del mapa reportara sus 114 primitivos).
- **La transición del banner al mapa en el host**: `FIELDSTG_createBanner` congela el campo y
  `showField` espera tanto al streamer en `TASK_RUN` como al banner en `TASK_DONE`. Luego el banner
  conserva la transición 60 cuadros y cierra su clip; las solicitudes de tiles y las cargas de TIM
  siguen ejecutándose en paralelo. Eso da tiempo al CD, pero no garantiza en el código que toda la
  VRAM termine de cargarse. Para que la lectura local del port no revele el mapa antes que la
  transición, `gfx_engine.c` recorta solo el `DRAWENV` de la capa del mapa mientras
  `FIELDSTG_state.frozen` está activo; no detiene el streamer ni cambia el C decompilado. El check
  mide el clip efectivo en el cuadro 610 y su restauración al cuadro 899. La
  captura PS1 disponible muestra negro detrás del banner en un instante; la temporización exacta
  aún requiere una secuencia limpia de varios cuadros.
- **El hito 9 cubre el arranque y un paso del evento inicial**: `src/field/` tiene 379 archivos C;
  se ejercita el modo, sus cinco capas, su primer dibujo y el camino de movimiento del actor que
  usa el mapa. Esto no prueba todavía el control direccional del jugador ni las colisiones; NPC,
  scripts, batallas, menús y otras escenas del campo siguen sin verificar.
- **El hito 9 sólo prueba un stage**: la demo confirma *new game*, cuyo stage en la UE es 0x1E9
  (`WSTAG780`); los otros ~294 stages se compilan y enlazan (`WSTAG###_*`) pero no se ejercitan.
- **El hito 9 prueba un solo idioma** (la quinta opción de la lista, `LANG_KEYS` por defecto):
  el arte de título que se navega es el español; los otros idiomas quedan como en T18.
- **El campo deja captura y el cuadro final es el mapa (desde T27)**: el PNG (320x240) lo escribe el
  fin de cuadro del arnés, que corre al alcanzar el presupuesto; medido, la corrida a 600 cuadros
  muestra el mapa con los rótulos de área y la del cuadro final (999) también (3.213 colores
  distintos; antes de T27 salía uniforme `1,1,1`). No
  se comparó ningún color contra un emulador.
- **T27 no cambió las capturas de los otros modos, medido**: las del menú de idiomas (cuadro 40) y
  del título (cuadro 319) son **idénticas byte a byte** antes y después del arreglo (mismos md5) y
  los ocho checks restantes siguen verdes. Lo que **no** se probó: que la ventana (lo que ve el
  dueño) muestre el mismo cuadro que la captura en cada modo; la captura lee el framebuffer que
  PsyCross tenga ligado al final del cuadro y desde T27 todos los splits dibujan en la ventana,
  pero no se comparó píxel a píxel contra una lectura de la ventana.
- **T27 pinta el bit `dfe` de los `DR_TPAGE` del juego**: el port tiene un solo buffer y manda
  todos los splits a la ventana; la decisión de presentación (un buffer, la ventana) es la del
  comentario de `pc/shim/gfx_engine.c`, no una medida contra el emulador. Sin verificar: el modo
  que de verdad muestrea la pantalla como textura, la transición de batalla del campo
  (`FIELDSTG_playBattleTransition`, `src/field/field_mode/field.c:118,159,182`), que copia la
  pantalla a VRAM con `MoveImage` y la muestrea como *tpage* de 15 bits: el port la alimenta por
  su camino de frame-store (`GR_StoreFrameBuffer` → `GR_ReadFramebufferDataToVRAM`, marcado por
  `GR_CopyVRAM(NULL, …)`), no por el camino `dfe 0`, así que el pase de T27 no la toca; ninguna
  demo llega a la transición, así que no se midió. Los modos que corren las demos suben TIMs con
  `LoadImage` (copia de software de PsyCross) y no llegan ahí.
- **T26 no cambió ningún píxel medido**: con el orden por prioridad, las capturas de 600 y 900
  cuadros son **idénticas byte a byte** a las de antes del arreglo (mismos md5) y los contadores
  de primitivos por cuadro no cambian. El orden de llamada sólo decide entre primitivos que caen
  en la **misma** entry de la OT (cada callback elige la suya por el `depth` de su objeto), y en
  los cuadros medidos no se midió ninguno; el orden de dentro de una entry de la OT no se midió
  (el volcado del shim es de los callbacks, no de la OT). Sin verificar: que el orden cambie la
  pantalla en algún cuadro del demo.
- **`pc/check_title.py` necesita `DISPLAY`**: abre la ventana de PsyCross (los *headless* son
  `check_rlen`, `check_file`, `check_text` y `check_draw`; `check_font`, `check_country`,
  `check_input`, `check_title` y `check_field` piden display).
- **El hito 7 no prueba un gamepad**: PsyCross trae el camino de SDL GameController
  (`PsyX_Pad_UpdateGameControllerInput`) y el juego lo leería igual por `libpad`, pero en esta
  máquina no se conectó ninguno: **sin verificar**.
- **El hito 7 solo prueba lo que el guion dice**: `pc/check_input.py` verifica `down,down,up`
  (y el arranque escribe el mapa de teclas de PsyCross, del que no se probaron L1/R1/… ni el
  auto-repeat: las pulsaciones guionadas duran 4 cuadros y el `repeatRate` del juego es 18, así
  que la rama `repeated` de `readPadButtons` se compila pero no se ejercita).
- **El hito 7 no ejercita la vibración** (`updateVibration` y `PadSetAct` corren en cada lectura
  porque `PadInfoMode` contesta 7, el modo analógico de PsyCross, pero no hay `setVibration` que
  los dispare).
- **El hito 7 no prueba la cruceta analógica**: `PsyX_Pad_UpdateKeyboardInput` no toca los
  ejes, así que `slot->analog` queda en 127 (neutro) y la rama del stick no mueve la selección.
  Sería otro camino a probar con un gamepad.
- **El `--wrap` del hito 7 es de `ld`**: la cadena se probó en Linux x86-64 con gcc/ld y el
  teclado por X11 (XWayland incluido); otros enlazadores o plataformas: sin verificar.
- **El bucle de cuadros de `main` lo cierra el port**: `main` es `for (;;)` y no vuelve, así
  que la salida (y la captura) la dispara el fin de cuadro del shim, no el juego. Es la única
  forma de que una demo termine sola sin tocar `src/`.
- **PsyCross en el host**: T11 probó que compila y enlaza (y el *layout* de paquetes, 16 / 12 /
  28); **T13 ya abre ventana, inicializa el renderer y presenta** (`PsyX_Initialise`,
  `PsyX_BeginScene`, `DrawOTag`, `PsyX_EndScene`, `PsyX_WaitForTimestep`). Lo que sigue sin
  ejercitarse en runtime: sonido (`libspu`), pad, memcard y CD de PsyCross (el lector de CD es
  el nuestro, `pc/shim/host_cd.c`).
- **T12 dibuja, pero no muestra**: se mide la *ordering table*, no la pantalla. El arnés no llama
  a `DrawOTag`/`PutDrawEnv` ni sube nada a VRAM; que los `SPRT` tengan la posición, el UV y el
  CLUT correctos contra los píxeles de la fuente no se prueba. Las *texture pages* (`getTPage`) y
  los CLUTs (`getClut`) se calculan, pero en T12 la fuente nunca se cargó (hito 2: `loadFont`
  queda fuera; el hito 5 es el que la sube y el que sí comprueba que los primitivos apunten a
  la tabla).
  (T13 sí llama a `DrawOTag` y se ve, pero con `POLY_F4` planos, sin texturas.)
- **T13 no dibuja las ventanas de texto del banner**: `FIELDSTG_showAreaName` no encuentra zona
  (ver arriba), así que el nombre del área y el del lugar no aparecen: eso pide el disco y una
  fuente en VRAM, que `first_frame` no carga (el hito 5 sí, pero con una ventana propia, fuera
  del banner). Tampoco llega a `TASK_DONE` (el cierre del clip
  de la capa), porque eso espera a que las ventanas terminen de escribir; el arnés le da dos
  `TextWindow` válidas que nunca terminan, así que el banner se queda en el último substate.
  Los métodos `setClipPos`/`setClipSize` de la capa son *no-ops* del shim: no hay *clip* de GPU.
- **T13 se enlaza con `--gc-sections`**: es lo que permite usar `FIELDSTG.PRO`'s datos
  (`data/fieldstg.c`) sin compilar el resto del overlay. Los cuatro símbolos `FIELDSTG_*` que
  quedan colgando de `FIELDSTG_state` son stubs que **abortan** si alguien los llama (nadie lo
  hace en este camino).
- **T13 apaga PGXP** (`USE_PGXP=0`, ver el hallazgo arriba): no se prueba la mejora de precisión,
  solo el render fiel a las coordenadas enteras de la PS1.
- **Los colores del banner no se compararon contra el emulador**: se ven los bytes que el propio
  `FIELDSTG_bannerBoxes` tiene, leídos como la PS1 los lee (little-endian). La captura sin X es
  con `glReadPixels` (`first_frame.ppm`); con `xwd`/`x11grab` la ventana sale negra por el
  *direct rendering*.
- **T12 es solo el camino de la ventana de texto** (los menús). El *sprite drawer*, el de cartas,
  los marcos de `TalkBox`/`MessageBox` y todo lo que pasa por `libgs` no entran: `Gs*` sigue sin
  existir en PsyCross. El GTE que se ejercita es solo el que el texto usa si se escala.
- **T12 no ejercita la rama rotada/escalada**: las cadenas medidas no se escalan, así que los
  `POLY_FT4` (`0x2C`) salen en 0 y `RotMatrixYXZ_gte`/`ScaleMatrix`/`ApplyMatrixSV` se compilan
  pero no se llaman. Por lo mismo, la equivalencia `RotMatrixYXZ_gte` → `RotMatrixYXZ` que hace
  `pc/psx_compat.h` no se verifica numéricamente.
- **T12 enlaza SDL2/OpenAL/X11 sin usarlos**: el `SetDrawTPage` y el GTE de `libpsycross.a`
  arrastran `PsyX_GPU`/`PsyX_pad`/`PsyX_main`, y con ellos sus dependencias. El arnés no las
  ejecuta (corre con `DISPLAY` sin definir y drivers de SDL falsos, y su único inicializador
  estático es el del GTE), pero el binario queda ligado a ellas.
- **La cantidad de `DR_TPAGE` que predice el check** sale de emular en Python la lógica de
  *texture page* de `textWindowDraw` con la columna `page` de la tabla de glifos; el check falla
  si no coincide, así que es una comprobación, no una suposición, pero solo cubre las cadenas
  elegidas (sin códigos de control salvo el salto de línea).
- **`-x c`, `-idirafter` y `-Wno-narrowing` son específicos de gcc/clang**: son el precio de
  compilar las fuentes de PsyCross sin tocarlas con este compilador. Otro compilador puede
  necesitar otros, o ninguno.
- **PsyCross no cubre `libgs`, `libsnd` ni MDEC/XA**: `libgs.h` y `libsnd.h` no existen en el
  submódulo (T10), así que siguen siendo nuestros.
- Solo se corrió en **Linux x86-64 con gcc/clang** (este host) y con la imagen EU del dueño.
  Otras plataformas y otras imágenes: sin verificar.

## Carpeta de datos de texto y capa de reemplazo (es419-live-option, L2b)

Los textos del juego se extraen del disco del usuario a una carpeta de datos y se pueden reemplazar sin
tocar el disco. Nada de esto se versiona (ADR-001).

- **Mapa de índices.** El disco EU trae cada uno de los 50 archivos de texto una vez por idioma, en filas
  de 7 ranuras de la tabla de archivos: `índice = ranura + 7k` con ranura 1 JPN, 2 USA (sin uso en EU),
  3 ENG, 4 FRA, 5 ITA, 6 GER, 7 SPN (es `TEXT_FILE() = LANGUAGE + (2 + 7k) - 1`, con `LANGUAGE` 2 a 6). El
  orden de `k` es el de los sectores en el disco (los DMG al final), no el alfabético. Verificado con el
  disco del dueño: los 350 archivos de `AAA/DAT/COUNTRY/<LANG>` caen en su `ranura + 7k` según el LBA de la
  tabla (`FILE_SECTORS`). JPN y USA no se usan: el juego EU nunca los pide. Código: `pc/shim/text_data.c`.
- **Carpeta.** `<datos>` es `DW3_DATA_DIR`, o `<carpeta del usuario>/data` (ADR-010):
  `<datos>/text/manifest.json` (cabecera `asset-manifest-v1` y hash por archivo) y `<datos>/text/<idioma>/<archivo>.json` con
  idiomas `eng fra ita ger spa es-419` (es-419 = español + `localization/es-419`). Los reemplazos van en
  `mods/<mod>/text/<idioma>/<archivo>.json`. `<archivo>` es el nombre del disco sin `.BIN` **en minúsculas**
  (`esamtmap`, `ssamtmap`...; el es-419 usa los nombres del español); al leer un mod el nombre se compara sin
  distinguir mayúsculas.
- **Overlay.** `DW3_L10N_DIR`, o `localization/es-419` relativo al directorio de trabajo, o relativo al
  binario. Cómo se empaqueta queda para la tarea de builds finales; sin overlay el es-419 no se ofrece y el
  juego corre desde el disco.
- **Extracción** al arrancar si falta `<datos>/text/` o la cabecera del manifiesto cambia (`disc`, `inputs.overlay`,
  `inputs.disc-text` y `exporter.text`, la versión del códec): 300 archivos en 0,13 s; con la cabecera vigente, 2 ms. Un archivo cuyo
  hash difiere del manifiesto es del usuario y no se pisa: la versión nueva va al lado como `.json.new`.
- **Orden al servir** (`host_cd.c`, `cdRead` + envoltorio de `FILE_TABLE.getSectorCount`): `mods/`, luego el
  archivo de `data/` solo si fue editado, luego (solo con la variante es419 y para los archivos del español)
  la mezcla disco + overlay armada en memoria, y si nada aplica, el disco: los idiomas del disco sin
  ediciones salen byte por byte del disco. Un JSON del usuario debe traer exactamente las entradas del
  disco y caracteres codificables; si no, se registra archivo/entrada/posición y se sirve el siguiente.
  Los bytes armados se guardan por índice y se rearman si cambia la fecha del archivo (revisión como mucho
  una vez por segundo, solo al pedir el tamaño).
- **Variante** del español: `tdSetVariant(TD_VARIANT_SPA | TD_VARIANT_ES419)`; provisoriamente
  `DW3_TEXT_VARIANT=es419` al arrancar (sin fila en F1 todavía: eso es L2c). `DW3_TEXT_TRACE=1` registra
  qué sirvió cada lectura de texto (disco o reemplazo) con su hash.
- Pruebas: `make -C pc texttest` (mapa, manifiesto, decisiones, validación) y las corridas del feature doc.

## Mods y config (ADR-005, stage-banner T2)

> Guía para quien hace mods: [`docs/modding/README.md`](../docs/modding/README.md) (English: [`README.en.md`](../docs/modding/README.en.md)).

Estructura (`<pref>` = la carpeta del usuario, ADR-010: la del ejecutable con `portable.txt`, o
`SDL_GetPrefPath("dw3port","dw3")`, en Linux `~/.local/share/dw3port/dw3/`; el único módulo que la conoce es
`pc/shim/port_paths.c`, y `DW3_USER_DIR` la mueve entera, útil para las pruebas):

```
<pref>/settings.ini            del jugador; su sección [mods] fija el orden
<pref>/saves/<perfil>/         slot<N>.dw3save, card.dw3card y view/slot<N>.json (vista de solo lectura)
<pref>/import/                 tarjetas .mcd de PS1 por importar (y imported.txt)
<pref>/data/config/            del port, se rehace: <nombre>.json (copia de referencia) y manifest.json
<pref>/mods/<mod>/mod.json     de la comunidad, el port nunca escribe aquí
<pref>/mods/<mod>/content/<tipo>.json    contenido nuevo (items.json, techs.json, teams.json, encounters.json)
<pref>/mods/<mod>/digimon/<llave>/       un Digimon: digimon.json y model/ (d<nnn>: cambia a un original)
<pref>/mods/<mod>/tables/<tabla>.json    parches de tablas
<pref>/mods/<mod>/config/<nombre>.json   solo las claves que el mod cambia
```

- **Mods activos y orden.** Cada carpeta de `mods/` con un `mod.json` válido (`"format": "mod-v1"`) es un mod. Sin
  configuración, todos están activos en orden alfabético y el último gana. Con `order = a, b` en la sección `[mods]` de
  `settings.ini`, solo esos quedan activos, en ese orden (el último gana). Un nombre que no existe o un mod inválido se
  registra y se salta. Las líneas `order` y `disabled` no tienen largo máximo (hasta 256 mods y más): se leen del
  archivo con memoria dinámica (`inputCfgReadModsOrderAlloc`, el límite de 512 bytes de `boot.c` desapareció) y, si el
  archivo pasa de 4 MB, o no hay memoria, el log lo dice y no se lee nada en silencio.
- **Muestras de sonido reemplazadas (U7).** Un mod que reemplaza `vag_NNN.wav` no escribe en la memoria del SPU de
  PsyCross (matriz fija de 2 MB y búfer de decodificación de 512K muestras, constantes de una biblioteca que no se
  modifica): `shim/audio_mod.c` decodifica el ADPCM una vez a PCM al cargar el banco y `shim/snd_spu.c` toca esa
  muestra como voz PCM aparte (OpenAL, mismas cuentas de tono, volumen y balance que PsyX_SPUAL), con la envolvente ADSR del
  tono. No hay tope de duración, solo la memoria; el `SpuWrite exceeded SPU_REALMEMSIZE` de antes ya no aparece. Con
  `SND_DEBUG=1` el key-on muestra una dirección virtual `0x4...` marcada `(pcm)`. `make -C pc audiotest` lo prueba con 60 s
  en los tres espacios de banco a la vez.
- **Configs.** Hoy hay dos: `stage_banner` (`type_delay_frames`, `hold_frames`, `raise_px`, `hidden_boxes`) y
  `transitions` (`door_factor`, `battle_tiles_per_frame`, `battle_wait_frames`, `results_factor`, `map_tiles_per_frame`). Cada una lleva
  `"format": "<nombre>-v1"`. Los valores por defecto y sus rangos están en `shim/port_config.c` (una tabla por config) y
  se escriben en `data/config/<nombre>.json` con un objeto `"notes"` que explica cada clave.
- **Mezcla por clave.** Por defecto del código, luego `data/config/<nombre>.json` solo si el usuario lo editó, luego cada
  mod en orden. Un mod que cambia solo `hold_frames` hereda el resto. Se carga una vez al arrancar; el log dice
  `config: stage_banner from default+data+mods/<mod>`.
- **Copia de referencia.** El port la escribe si falta y la refresca mientras siga idéntica a su hash en
  `data/config/manifest.json` (por eso un valor por defecto nuevo del port nunca queda frenado por una copia vieja). Si la
  editaste, nunca la toca; para recibir una fresca, bórrala.
- **Errores.** Archivo ilegible, JSON mal formado, versión de formato desconocida o sin `"format"`: `config: <ruta>
  rejected: <motivo>` y se usa la fuente siguiente. Una clave de tipo equivocado, no entera o fuera de rango:
  `config: <ruta> key "<clave>" rejected: <motivo>`, esa clave sigue con el valor anterior y las demás se aplican. Un
  `mod.json` malo: `mods: <ruta> rejected: <motivo>`. El juego nunca se cae por un mod.
- **Texto.** Los reemplazos de texto también se leen de `<pref>/mods/<mod>/text/<idioma>/<ARCHIVO>.json`, en el orden
  de los mods (el último gana), antes que el reemplazo viejo `<datos>/mods/text/` y que lo editado en `data/text/`. La
  ruta vieja sigue valiendo durante la beta con un aviso en el log (`text: ... deprecated`) y se retira en la versión
  siguiente. El texto conserva su rechazo: un archivo mal formado se registra y se usa la fuente siguiente (el mod
  anterior, luego la ruta vieja, luego `data/`).
- **Pruebas.** `make -C pc configtest` (carpeta temporal en `$TMPDIR`). Para ensayar sin tocar tu carpeta real:
  `DW3_DATA_DIR`, `DW3_MODS_DIR` (carpeta de mods, sin barra final) y `DW3_SETTINGS` apuntan a una carpeta de prueba.
- **Manifiesto (port-filesystem T2, ADR-006 §5).** `mod.json` admite además `"version"` (dígitos y puntos, `1.2.0`),
  `"kind"` (`assets`, por defecto, o `content`), `"port"` (versión mínima del port: `">=0.4"`; el port es
  `shim/port_version.h`) y `"requires"` (`["base>=1.2", "otro"]`, nombres de carpeta). Las claves desconocidas se
  ignoran. Las dependencias cargan antes que quien las pide (orden estable sobre el de `settings.ini` o el alfabético).
  Se desactiva con aviso (`mods: <mod> disabled: <motivo>`) un mod cuyo `port` es más nuevo que este, cuya dependencia
  falta, está desactivada o es más vieja, o que está en un ciclo (`mods: dependency cycle, disabled: a b`). Un valor mal
  formado rechaza el mod (`mods: <ruta> rejected: mod.json: ...`). `<pref>/saves/` queda reservada para ADR-006 §4
  (`saves/<partida>.mods.json`); hoy no se lee ni se escribe.
- **Archivos crudos (port-filesystem T1).** `mods/<mod>/files/<ruta del disco sin AAA/>` reemplaza ese archivo entero
  (por ejemplo `files/dat/z_stage/s330pack.bin`; se acepta cualquier combinación de mayúsculas); `shim/port_fs.c` recorre una sola vez el directorio ISO9660 del disco (`pfsDiscOpen`) y lo comparte: une número de archivo y ruta, y da a `text_data.c` y `image_data.c` los tamaños, el listado por prefijo y la identidad del disco (sello).
  El último mod gana y gana sobre el disco. Un archivo que el texto o las imágenes ya manejan con formato abierto
  conserva ese comportamiento y el crudo se ignora (una línea en el log: `files: ... ignored: ...`). Se mira de nuevo
  como mucho una vez por segundo (mtime y tamaño; un tamaño distinto llega por el contador de sectores). El log dice
  `files: override <ruta> from mods/<mod>` una vez; `DW3_FS_TRACE=1` traza cada lectura
  (`files: read <n> <ruta> off= sectors= src=disc|text|raw`). Películas y XA no pasan por ahí. `make -C pc fstest`.
- **Topes del motor (modding-ux U1).** Sin topes de PS1 para el contenido: el registro de tablas (`table_mods.c`) crece solo (antes
  `TM_MAX_TABLES` 32 y, al pasarse, `registerEntry` devolvía -1 sin avisar); los buffers de listas salen del heap según el `listMax`
  del campo (tope de cordura `TM_LIST_CAP` 65536; un descriptor que lo pasa, o cualquier registro fallido, deja `tables: table
  "<n>" not registered: <motivo>`); `MODS_MAX` 256, `PJ_MAX_KEYS` 64, `PJ_MAX_ARR` 64 (`shim/mods.h`; la lista `order` ya no se
  corta a 600 caracteres); `SAVEFILE_MAX_SECTIONS` 32 (el formato guarda el conteo, no cambia; `saveFileAdd` ya avisa en `[saves]`
  cuando no cabe). Pruebas: `tabletest` (`testNoCaps`), `configtest` (`testLimits`), `savetest` (sección de la tabla). **No se
  tocó** `FILE_CACHE` (64 lugares): `slots[FILE_CACHE_SLOTS]` está dentro de `FileCache` (`include/engine/file.h:87`) y lo indexan
  `tim_loader_pc.c`, `controls_draw.c` y `host_pad.c`; agrandarlo pide cambiar `include/` (o una copia de la cabecera para todo
  el port), y `requestFile` (`file_cache.c:99`) sigue sin comprobar NULL.
- **Tablas de datos (ADR-007, data-tables T3).** `shim/table_mods.c` es el motor de parches: una tabla se describe
  con un `TmTable` (nombre, filas, llave, campos con desplazamiento, tipo y rango) y el motor hace el resto:
  la exportación de referencia `data/tables/<tabla>.json` (valores originales; con manifiesto y regla `.new`; editarla
  no cambia el juego), la lectura de `mods/<mod>/tables/<tabla>.json` y la aplicación en orden de mods. Siempre
  restaura la copia original de la tabla antes de aplicar, así que re-aplicar no acumula; sin parches no escribe nada.
  La primera tabla, `battle_rewards` (`STFGTREP_rewards`, 335 filas), se registra en `boot.c` (`pcRegisterTables`) y se
  aplica en el `onEnter` del ámbito `stfgtrep`, que corre después de que `state_scope` restaura el overlay.
  `DW3_TABLE_TRACE=1` imprime la fila y la cartera al entrar y salir del reporte. `make -C pc tabletest` (tabla
  sintética; carpeta temporal en `$TMPDIR`). Guía: `docs/modding/README.md` §6.5 (con el ejemplo mínimo).
  **Evoluciones y crecimiento (content-export E5b, `shim/game_tables.c`).** `evolutions` (352 filas: `socio*100 + entrada`),
  `blast_forms` (52 filas, llave = id del Digimon), `level_exp`, `vital_cuts`, `vital_random`, `stat_gains` y `resist_gains`. Las seis
  del overlay se registran con `gameTablesRegisterReport` y se aplican con `gameTablesApplyReport` en el `onEnter` de `stfgtrep`
  (`pcApplyReportTables`, `boot.c`); `blast_forms` se aplica una vez en `gameTablesRegister`. Con parche de `evolutions`, las primeras 44
  entradas de la lista propia del puerto (`pcEvolutionList`, la de los mods) reciben los originales parchados; las entradas 44+ son de
  los `digimon/<llave>/digimon.json`. Las formas se dan por fila + 1 o por nombre; las filas 53-55 y un 0 en una fila de `blast_forms` que tiene formas se rechazan.
  Con `DW3_LOG=mods` sale una línea por fila cambiada (`evolutions row 0: value 1 (orig 5) by mods/m`). `make -C pc tabletest`.
  **Referencias a Digimon de 16 bits (U9).** `DigimonData.pairPartner` y `blastForms` son `u8` en el disco (fila + 1, hasta 254); el port los
  lleva también en `PcDigiLinks` (`shim/ext_state.[ch]`: `u16` por fila, original y de extensión, con la marca `set`) y el juego los lee con
  `PC_DIGIMON_BLAST(d, tier)` / `PC_DIGIMON_PAIR(d)` (`engine/digimon_access.h`; en PS1 se expanden a `d->blastForms[tier]` / `d->pairPartner`).
  Una fila sin `set` lee su `u8`, así que sin mods se lee lo mismo que el disco. `blast_forms` es ahora una tabla de filas propias (`rowAt`, campos
  `TM_U16` 0..4096, validados contra las filas que existen), y el `digimon.json` de un Digimon acepta `blastForms` (5 nombres) y `pairPartner` (un nombre o 0) en
  formas de socio nuevas. `findPairTech` compara la pareja con `PC_DIGIMON_REF` (fila + 1 en filas nuevas, `nameId` en las originales). Las
  listas de evolución ya eran de 32/16 bits (`Evolution.digimon` s32, `needs[].digimon` s16). Formato de partida sin cambios (ids de 16 bits).
  Pruebas: `digimontest` (`testWideRows`: 300 formas, filas 55-354) y `tabletest` (`blast_forms` con 360 filas).
  **Campo (content-export E5c, `shim/game_tables.c`).** `field_enemies` (523 filas de `FIELDSTG_enemies`), `field_encounters` (335) y
  `battle_rates` (6). Son datos del overlay FIELDSTG: se registran con `gameTablesRegisterField` y se aplican con
  `gameTablesApplyField` al comienzo del `onEnter` de `fieldstg` (`pcApplyEncounterPlacements`, `boot.c`; los emplazamientos de
  `content/encounters.json` van después). Un `Encounter` del juego guarda tres punteros: la tabla trabaja con una copia plana (`enemy1`..`enemy3` =
  fila de `field_enemies`) y escribe los punteros de vuelta tras el parche (`boot.c` comprueba con `_Static_assert` que `GameFieldEncounter`
  tiene el layout de `Encounter`). El JSON de referencia trae `labels` con el nombre del Digimon (`TmTable.rowLabel`, `gameTablesSetFighterNames`).
  Prueba de estado: `stay,warp:330,optlang:4,fieldbattles:2,battlepilot` con un mod que parcha el encuentro 35 imprime los luchadores
  del encuentro (`pad: fieldbattles: encounter 35 stage 1 fighters 77/8/0`, sin mod `206/0/0`). Pendiente: `stageBattles` de cada WSTAG.
  **Cartas (content-export E5d, `shim/game_tables.c`).** Seis tablas: de la tienda de cartas (overlay STCRDSHP) `card_prices` (44), `card_shop_stock` (24, lista de
  cartas) y `card_pack_slots` (210 = 35 sobres x 6 ranuras, por `rowAt`), y del duelo (overlay CARDGAME) `card_effects` (60 x 44 campos, `rowCheck` rechaza un guion sin 0),
  `prize_items` (36) y `default_deck` (40). Se registran al arrancar (`gameTablesRegisterCardShop` / `gameTablesRegisterCardGame`, con los nombres de `CARDNM` e `ITMNAM`
  para `labels`) y se parchan en el `onEnter` de los ámbitos `stcrdshp` y `cardgame` (`pcApplyCardShopTables`, `pcApplyCardGameTables`). El motor ganó
  `TM_NO_COUNT` (una lista sin cuenta en la fila: `STCRDSHP_stocks` no la guarda) y reserva sus listas con ceros (`calloc`). `boot.c` comprueba con `_Static_assert` los espejos
  `GameCardPrice`/`GameCardStock`/`GameCardPack`/`GameCardEffect`. Prueba de estado: `mode:1300,49,stay` y `cardauto,stay` con un mod de prueba y `DW3_LOG=mods`
  (`card_prices card 10: price 7 (orig 2200)`, `card_effects card 1: condition 128 (orig 145)`...).
  **`card_opponents` (modding-ux U8a, E5d2).** `CARD_NPC.BIN` (disco 0x7A4, 170 x 0xD0 bytes, sin llave) como tabla de archivo **posicional**: `tmRegisterFile` admite ahora una tabla sin
  `keyFromRow` con `rows` fijas (el archivo mide 18 sectores y el final es relleno) y `tmPatchBuffer` corre el `rowCheck` de la tabla sobre las filas parchadas (una fila rechazada vuelve a como
  estaba el búfer). `TmField.mask` (bits de un valor sin signo, p. ej. el id de carta en los 12 bits bajos y la marca en el bit 15) permite separar `cardN` de `flaggedN`: 182 campos por fila
  (`game_tables.c`, armados al registrar). Se parcha en el gancho de lectura de `port_fs` (`pcPatchBattleTable` ahora atiende los dos archivos) y no en el `onEnter` de `cardgame`: el duelo
  lee el archivo por número (`FILE_CACHE.load(0x7A4)`) y no hay datos de overlay que restaurar. Prueba: `make -C pc tabletest` (`testCardOpponents`).
  **Gimnasio y posadas (content-export E5e, `shim/game_tables.c`).** Siete tablas: del gimnasio (overlay STGTRAIN) `gym_trainings` (224 = 14 x 16, llave `índice * 100 + ranura`),
  `gym_stat_gains` (12), `gym_stat_losses` (3), `gym_resist_gains` (27 = tres arreglos, por `rowAt`), `gym_hp_mp_gains` (9) y `gym_intensity_costs` (3); y `inns` (21, llave = el
  modo de la posada; `INNS` es del ejecutable y se parcha una vez en `gameTablesRegisterInns`). Las del gimnasio se registran con `gameTablesRegisterGym` y se parchan en el `onEnter` del
  ámbito `stgtrain` (`pcApplyGymTables`, `boot.c`); `boot.c` comprueba con `_Static_assert` los espejos `GameTrainEntry`/`GameTrainGain`/`GameTrainInfo` (`InnInfo` no está en ninguna
  cabecera: los 22 x 8 bytes salen de `nm -S`). Nombres para `labels`: `TRAINI` (entrenamientos) y `HTLNAM` (posadas). Rechazos de fila entera con `rowCheck`: una ganancia cuyo
  `base + range - 1` no cabe en el `s16` de la estadística, y un entrenamiento sin `stat` válido. Prueba de estado: `mode:A00,1,stay` con un mod de prueba y `DW3_LOG=mods`
  (`gym_stat_gains column 0: base 9 (orig 1), range 0 (orig 2)`, `inns mode 522: price 3 (orig 8)`...). Ningún modo del arnés recorre un entrenamiento ni una noche de posada sin teclas.
- **Log único (port-log T1; antes "Registro de mods", data-tables T5).** `shim/port_log.c/.h`: `PLOG(canal, nivel, ...)`
  (nivel `PLOG_MODS` o `PLOG_TRACE`; una línea apagada no evalúa sus argumentos), `plogLine`/`plogRaw` (siempre, para los
  resúmenes y rechazos del arranque) y `plogOn` (`pfsTraceEnabled`, `stageTimingOn`, `pcTraceReward` lo usan). Formato
  `[canal] mensaje` a `stderr` (el log de la sesión). Nivel y canales: `[debug] log = off|mods|trace` y `channels = a, b`
  en `settings.ini` (`input_cfg.c`; `mod_log = on|off` se lee como alias, `on` = `mods`, y se reescribe como `log`), fila "Log"
  (Off/Mods/Trace) de la pestaña System (`options_system.c`). Entorno (gana al archivo): `DW3_LOG=off|mods|trace`,
  `DW3_LOG_CHANNELS=a,b`, `DW3_MOD_LOG=1` (= `mods`) y los atajos viejos, que ponen `trace` solo en su canal:
  `DW3_TABLE_TRACE` (`tables`), `DW3_FS_TRACE` (`files`), `DW3_IMG_TRACE` (`images`), `DW3_STAGE_TIMING>=1` (`stage`; 2 y 3
  siguen terminando la corrida / forzando la salida). T2: los trazos por cuadro y por evento (antes `printf`/`fprintf` sin condición) van a `trace` en los canales
  `gfx` (`frame N: layer`, `gfx: layer id`, paquetes), `pad` (`pad N: raw=`), `field` (`field N: pos=`), `tasks`
  (`register/unregister/actor find`), `overlay` y `text`; `DW3_TEXT_TRACE`, `DW3_MEMCARD_TRACE` y `DW3_BATTLE_EVENT_LOG` son
  atajos de `text`, `memcard` y `battle`. Los `check_field/title/input.py` piden `DW3_LOG=trace` y juntan `stderr` con `stdout`.
  Siguen como variables sueltas (inertes por defecto): `SND_DEBUG`, `MDEC_DEBUG`, `PACING_DEBUG`, `ST_DEBUG`, `DW3_*_DUMP`. `boot.c` emite `[tables] battle_rewards encounter N: exp X (orig Y), money ... by mods/a` al abrirse el reporte, solo
  si la fila difiere del original (el motor da el original con `tmOriginalRow` y los mods con `tmWho`). Los resúmenes de
  mods, config, archivos, imágenes y tablas salen siempre (`[mods]`, `[config]`, `[files] override`...). `make -C pc extstatetest` (estado extendido de objetos, `shim/ext_state.c`), `make -C pc logtest`
  (niveles, canales, entorno, atajos) y `inputtest` (clave, alias, fila). Guía: `docs/modding/README.md` §6.6.
- **Botín (data-tables T6).** `battle_loot` es la primera tabla respaldada por un archivo del disco (463,
  `FILE_BATTLE_TABLE`, `BattleTableEntry` de 0x46 bytes): `tmRegisterFile` toma los bytes originales del disco
  (`pcCdReadDiscFile`, sin capas) para la referencia `data/tables/battle_loot.json` (llave: id del enemigo, campos
  `item` 0-402 e `itemChance` 0-1023, en 1024avos menos uno), y el parche se aplica al buffer en cada lectura del archivo
  con el gancho post-lectura `pfsSetPostRead`/`pfsPostRead` (`port_fs.c`, llamado desde `cdRead`). Parte siempre de los
  bytes recién leídos (disco o archivo crudo de `files/`), así que repetir no acumula; con un crudo del mismo archivo,
  el crudo va primero y el parche encima, buscando las filas por id. Con el registro de mods encendido,
  `[tables] battle_loot enemy N: item A (orig B), itemChance C (orig D) by mods/x` y `[tables] battle_loot drop: item N|none`
  al abrirse el reporte; `DW3_TABLE_TRACE=1` suma `report enter/exit: dropped item N, in the bag M`. Guía §6.5 (ejemplo mínimo).
- **Precios de objetos (data-tables T7).** `item_prices` parchea `ITEMS` (`price` y `sellPrice`, ambos u16, llave = id
  del objeto, `ITEMS[id - 1]`), un arreglo del ejecutable que ningún ámbito reinicia (`state_scope` solo cubre datos
  de overlays), así que `pcRegisterTables` lo registra y aplica **una vez** al arrancar, antes del primer modo. La
  aplicación parte siempre del snapshot original (idempotente) y no toca `data`, `kind` ni `type`. El precio de venta
  es un campo propio, no una fracción del de compra (`trade.c:241,947`). Con el registro de mods encendido, al entrar
  a la tienda (hook `onEnter` del ámbito `stitshop`, `pcModLogShop`) sale una línea por objeto de la tienda o de la
  bolsa con precio cambiado: `[tables] item_prices item N: price X (orig Y) by mods/x`. Guía §6.5 (ejemplo mínimo).
- **Tiendas (data-tables T8).** `shop_stock` parchea `STITSHOP_shops[31]` (`ShopList {count, items*}`, lista de ids
  terminada en 0). Es el primer campo **lista** del motor (`TmField.listMax`/`countOff`): un parche da el arreglo
  entero o `{"remove","add"}`, el motor arma el resultado en un buffer propio (por fila y campo, hasta 64 ids, id 1..402,
  terminador 0) y apunta la fila a él con su `count`; los arreglos `STITSHOP_stock0..30` no se escriben nunca, así que
  reaplicar parte siempre del snapshot. Una lista vacía, más larga que el tope o con un id fuera de rango se rechaza con
  una línea. Punto B: el hook `onEnter` del ámbito `stitshop` aplica `tmApply("shop_stock")` justo después de que
  `state_scope` restaura el overlay (la lista de la tienda se lee en `STITSHOP_getShopItems`, `equip.c:8`; la página
  se calcula con `count`, `item_list.c:139-147`, sin tope fijo). Registro: `[tables] shop_stock shop N: C items (orig O)
  by mods/x` al abrir. Guía §6.5 (ejemplo mínimo).
- **Ejemplo.** Un mod con `config/stage_banner.json` que contenga `{"format": "stage_banner-v1", "hold_frames": 120}` deja
  el nombre del lugar 2 s en pantalla (hereda las demás claves). Ya no hay un mod de ejemplo instalable de este tipo: chocaba
  con la opción Display > Stage banner = Fast del port.

### Mods de ejemplo por tipo (U10)

- `pc/examples/mods/` trae dos ejemplos completos (A9; los demás se borraron y lo que enseñaban está en la guía de mods): `training-field` (dos escenarios nuevos
  con arte propio, un guía, un parche del Parque Central, un sprite, tablas y textos) y `sample-model` (un Digimon nuevo con modelo glTF propio), más la carpeta modelo
  `_template`. `pc/examples/install_examples.sh [--only a,b] [carpeta de usuario]` los instala **apagados** (`[mods] disabled`, con `settings.ini.bak`, sin tocar
  `saves/`); no generan nada desde el disco: todo el arte es original (`training-field/tools/make_art.py`; el robot sale de `tools/test_model_import.py --demo`). Guía: `pc/examples/README.md`.

## Arte de pantallas y menús en PNG (menu-art-png, T2 y T3)

- Al arrancar, si el disco cambió o falta un archivo, el port escribe en `<data>/images/` un PNG indexado por cada
  imagen TIM de pantallas y menús (`SCREEN`, `COUNTRY`, `NAMEENT`, `MCARD`, `DGLB`, `TRAINING`): 54 en el disco EU,
  en unos 20 ms, y una segunda ejecución solo compara la huella (`images: ... is current`). Nada de esto se publica
  (ADR-001): sale del disco del usuario.
- Nombre: `images/<DIR>/<NOMBRE>/<NN>_<MM>.png` (cadena de índices de entrada del archivo). El diseño, la regla de
  paletas y los límites están en `docs/formats.md` §4.8.
- `images/manifest.json` guarda el hash de cada PNG. Un PNG que editaste no se pisa: la versión nueva va a `.png.new`.
- Código: `pc/shim/image_data.c` (extracción), `pc/shim/png_write.c` (escritor PNG sin dependencias, deflate sin
  comprimir). Prueba: `make -C pc imagetest` (TIM sintéticos, carpeta temporal en `$TMPDIR`).

### Mods de imágenes (T3)

- **Dónde se sustituye.** `pc/shim/tim_loader_pc.c` es la copia del cargador de TIM del juego
  (`src/engine/gfx/tim_loader.c`, que no se toca) con dos ganchos: antes de subir una imagen a VRAM pregunta a
  `imgOverrideTim` (`image_data.c`) por la imagen que empieza en ese desplazamiento del archivo en caché. El número de archivo
  da el sector (`FILE_TABLE.getSector`), el sector da el archivo del disco, y el desplazamiento da la cadena `NN_MM`.
  Mantén la copia al día con la original (la diferencia son solo los bloques `PORT`).
- **Fuentes, en orden.** `mods/<mod>/images/<DIR>/<NOMBRE>/<NN>_<MM>.png` (el último mod activo gana; ver "Mods y config"),
  luego un `data/images/...png` que editaste (su hash difiere del manifiesto) y, si nada aplica, los bytes del disco.
- **Reglas v1.** Mismo ancho y alto; PNG indexado no entrelazado de 1 a 8 bits (4/8 es lo normal), con zlib real (el
  lector trae su propio inflate: `shim/png_read.c`); a lo sumo 16 colores (páginas de 4 bits) o 256 (8 bits) y píxeles
  con índice menor a ese número. Los índices del PNG reemplazan los del juego; su PLTE reemplaza la paleta que se
  exportó, en el mismo lugar (fila 0 del CLUT de un TIM de 4/8 bits; las 16 entradas que la hoja nombra dentro de una página
  cruda de 4 bits). Las otras paletas de la página quedan como en el disco. Páginas de 8 bits (`STWMP*`) y paletas grises
  (sin paleta conocida): solo cambian los índices; para recolorear las primeras se edita el PNG del TIM de 8 bits de su
  subarchivo. Alfa 0 (`tRNS`) = transparente (0x0000); lo demás es opaco, con el bit STP de la entrada del disco, y un negro
  opaco se escribe 0x8000. Lo que no cumpla: `images: <ruta> rejected: <motivo>` y se prueba la siguiente fuente.
- **Los mods se leen una vez.** Al arrancar (`imgStart`) se recorre la carpeta `images/` de cada mod y `data/images` UNA vez y
  se guarda un índice ordenado en memoria (nombre en minúsculas -> escritura real): saber si una fuente tiene una imagen es una
  búsqueda en la tabla, sin `stat` ni listar carpetas mientras se juega. Antes (hasta 0.6.0-beta) el puerto volvía a mirar
  cada fuente de cada imagen una vez por segundo: con 23 mods son unos 3500 `stat` por archivo de sprites y unos 35 000 por
  segundo caminando, lo que en Windows (NTFS + antivirus) bajaba el campo a 10-21 FPS. Medido en Linux (1500 cuadros en el
  campo con los 23 mods del dueño, `strace -c -f`): 5 832 089 llamadas de archivo (3 376 632 `newfstatat`, 1 600 177
  `getdents64`) antes, 71 938 después (las mismas del arranque; 300 cuadros dan igual: 0 por cuadro). **Hot reload:** solo si
  el modder arranca con `DW3_MODS_HOTRELOAD=1` (`modsHotReload`, `mods.h`) vuelve la revisión por segundo (imágenes, mosaicos
  de escenario, `files/`, texto) y se reindexan las carpetas; sin eso, un cambio en un mod se ve al reiniciar.
- **Registro.** `images: override <ruta> from mods/<mod>` (o `from data (edited)`) al construir; con `DW3_IMG_TRACE=1`,
  `images: load <ruta> (override|disc)` por cada imagen de pantalla que el juego sube.
- **Ejemplo.** Recolorear un marco de menú: `docs/modding/README.md` §6.2g (piezas de `menus/`).
- Código: `shim/png_read.c` (lector e inflate), `shim/image_data.c` (`imgSubstitute`, `imgOverridesBuild`,
  `imgOverrideTim`). Prueba: `make -C pc imagetest` (también lee PNG con deflate dinámico hechos con zlib).

### Mapa de piezas `atlas-v1` (T5)

- Junto a cada `data/images/<DIR>/<NOMBRE>/<NN>_<MM>.png` el port escribe `<NN>_<MM>.json` con las piezas de esa imagen
  que su hoja CS referencia (54 mapas, ~1100 piezas en el disco EU). Se generan del disco como los PNG: viven solo en
  `data/`, van en el mismo `manifest.json` (pristino/editado/`.json.new`) y no se suben al repo. Se regeneran al cambiar la
  versión del conversor (`IMG_CODEC_VERSION`, en la huella).
- Campos: `image`, `width`, `height`, `mode` (`4bit`/`8bit`, el del PNG), `sheet` (el archivo CS, o el propio archivo
  `(entry 0)`; `null` si no hay hoja), `pieces` y `notes`. Cada pieza: `id` (`f<cuadro>p<parte>`), `frame`/`part` (primer
  cuadro que la usa y parte de la hoja), `x`,`y`,`w`,`h` en **píxeles del PNG** (índices, no colores), `palette`
  (`"columna,fila"` de su paleta dentro de la página), `draw_at` (`[dx, dy]` donde la dibuja ese cuadro) y `frames` (todos
  los cuadros que la usan). Los rectángulos idénticos de varias partes/cuadros se funden en una pieza.
- `"outside": true`: la pieza se recorta contra la imagen o cae fuera de ella (se lista recortada, o sin recortar si nada
  queda dentro; nunca se descarta en silencio). `"read_as": "4bit"|"8bit"`: el juego lee esa pieza en el otro modo de
  textura (p. ej. los marcos de `CNTSELWN`, imagen de 8 bits con partes de 4); su rectángulo se convierte a píxeles del PNG
  y se redondea hacia afuera. Una imagen sin hoja válida lleva `"pieces": []` y la razón en `notes`.
- Reglas de edición (también en `notes`): conserva ancho y alto del PNG y no pases de 16 (4 bits) o 256 (8 bits) colores;
  el JSON es solo informativo (no se lee al sustituir) y se regenera.
- Vista previa: `python3 tools/image_atlas_preview.py <data>/images/screen/stshoptm/00_00.png [--out vista.png] [--scale 2]`
  (necesita Pillow). Dibuja los rectángulos numerados (amarillo: normal, rojo: `outside`, cian: `read_as`) y escribe la leyenda
  en la salida estándar; sin `--out` el PNG va a la carpeta temporal del sistema, nunca a `data/`.
- Código: `imgAtlasJson` en `shim/image_data.c`. Prueba: `make -C pc imagetest` (hoja sintética con cuadros: 4 y 8 bits,
  fusión de rectángulos, recorte `outside`, `read_as`, sin hoja, y el manifiesto).

### Sprites de campo y su mapa `sprites-sprite-v1` (field-sprites-png, T2; `sprites/` desde A7.5)

- Los 171 `FIELD/SPRT/<NOMBRE>.BIN` (179 menos los 8 seguidores de compañeros, que van en `partners/<nombre>/field/`) los escribe el
  exportador propio `export_sprites` (`shim/sprite_export.c`, al arrancar; por omisión `all`; `SPRITE_EXPORT_VERSION` 1) a
  `data/sprites/<nombre>/<EEE>.png`, con `<nombre>` el nombre del archivo del disco en minúsculas (`p001play`; el juego
  direcciona estos archivos por nombre, así que es la identidad estable y no hay número que usar): un PNG indexado de 4 bits
  por cuadro (`EEE` = índice de la entrada en el archivo, tres dígitos), con la paleta de 16 colores del propio cuadro
  (índice 0 transparente, igual que el arte de menús). Más un `sprite.json` por archivo (formato `sprites-sprite-v1`, abajo) y un
  `sprites/manifest.json` (`asset-manifest-v1`; `names` lleva `p001play` → `dw3:sprite/p001play`). Antes de A7.5 salían de
  `images/field/sprt/<nombre>/` (mismos bytes; el JSON solo cambia el nombre del formato); esa ruta ya no se exporta ni se lee.
  `FIELD/FIELDCOM.BIN` va por la vía de los menús: `field/fieldcom/02_00.png` y `03_00.png` con su mapa `atlas-v1`
  (cada imagen con la hoja de su entrada: la 0 para la 2 y la 1 para la 3). En el disco EU: 4812 PNG de cuadros (126 sin
  uso), 1276 animaciones, 425 conjuntos, 0 fallos, ~0.13 s; 23 MB en total con el arte de menús.
- Misma huella, manifiesto y reglas que el arte de menús (`IMG_CODEC_VERSION` 3 regenera las carpetas): pristino se
  reescribe, editado se conserva y lo nuevo va a `<nombre>.new`.
- Arte de los escenarios del campo (content-export E6a): los fondos `S415BG01/02` y `S815BG01/02` (22 imágenes) salen como `field/stage/s<nnn>bg<nn>/<ii>.png` (sin mapa; mismas reglas
  de huella, manifiesto, mods y `.new`). Las páginas de los `FIELD/STAGE/S<nnn>MASK.BIN` (294 archivos) **ya no** se exportan ni se leen como `field/stage/s<nnn>mask/<ii>.png`
  (A6.4, `IMG_CODEC_VERSION` 11): sus objetos se editan solo por pieza, abajo. La entrada por página de cada `MASK` queda en `imgOverridesBuild` solo como portadora de lo que escribe el
  gancho de los escenarios (`imgIsStageMask`, como las pantallas de A5.5). Prueba: `imagetest` (`testStageMask`: un PNG de `images/` para un `MASK` no se lee y uno de un `BG` sí;
  `testStagePartPalette`: `imgStagePagesOpen` e `imgPartPalette`).
- Objetos de escenario por pieza (readable-assets A6.2, `shim/stage_obj_export.c` sobre `menu_export.c`): `data/stages/s<nnn>/objects/<pieza>.png` y `objects.json` (`stages-objects-v1`), una fila
  más de `kExporters` (`export_stage_objects`, al arrancar, por omisión `all`). La hoja es la entrada del `TMPK` que fija el `setupStage` (`sheetEntry`, que el exportador lee de `data/stages/s<nnn>/stage.json`),
  no la 0: 266 escenarios la 0, 10 la 1, 4 la 3 y 14 la 4. Disco EU: 294 `objects.json`, 8632 PNG, 10 456 158 bytes, 11 escenarios con `unexported` (hoja sin cuadros). Formato en `docs/formats.md` §7.5c;
  prueba: `stageobjexptest` (hoja en la entrada 1, piezas de 4 y 8 bits, fila de cuadro +1, repintado cuadro por cuadro). `--validate-mod` cubre `stages/` (A6.4: `stageObjPackValidate`, `stageobjpacktest`).
- Cartas originales por carpeta (readable-assets A7.4, `shim/card_export.c`): `data/cards/c<nnn>/face.png` y `card.json` (`cards-card-v1`), con `cards/manifest.json` (`names`: `c012` a `dw3:card/12`), una fila
  más de `kExporters` (`export_cards`, al arrancar, por omisión `all`). `<nnn>` es el número de la carta del juego (`cardDrawerSetCard`: archivo `(id-1)>>6`, imagen `(id-1)&63`; `imgCardId`); el PNG es
  el que el exportador de imágenes escribía en `images/card/cardpak<A>/<NN>.png` (mismos bytes), que ya no se exporta (`IMG_CODEC_VERSION` 12) ni se lee. Un `cards/c<nnn>/face.png` de un mod o editado en
  `data/` entra por la misma tabla de sustituciones (`imgOverridesBuild` lee el tema `cards` para los `CARDPAK`; el manifiesto de comparación es `cards/manifest.json`). El arte de una carta nueva
  de un mod pasó de `card/<llave>.png` a `cards/<llave>/face.png` (`card_ext.c`); una llave `c<nnn>` es de una carta original y no lee arte (`cardExtKeyIsOriginal`). `CARDDATA` (hojas de sprites del
  combate de cartas) sigue en `images/card/carddata/`. Pruebas: `cardexptest` (disco sintético: números, manifiesto, `.new`, sin reescribir), `imagetest` (`testCardPak`), `extstatetest` (arte de cartas nuevas).
  `--validate-mod`: `cards/` conocida, `card/` "is never read", y avisos por carpeta sin `face.png`, `c<nnn>` fuera de `c001`-`c316` y llave sin carta en `content/cards.json`.
- Baldosas del mapa del campo (content-export E6b, `shim/stage_export.c`, `shim/stage_mod.c`, `field/stream.c`): `Z_STAGE/S<nnn>PACK.BIN`
  (237 archivos, 11 745 baldosas) salen como `data/stages/s<nnn>/tiles/<FF>_<CC>.png` (8 bits, CLUT propia) y `tiles.json`
  (`stages-tiles-v1`), con su versión (unidad `stage-tiles/s<nnn>` del `exporter`) en el `stages/manifest.json` único que comparten el dato y los objetos del escenario (A7.2; antes `data/z_stage/s<nnn>pack/` con un manifiesto por escenario: ya no se lee, el exportador no lo borra y deja una línea de log; el manifiesto se lee una vez, al primer uso tras `stageModSetup`/`stageModRecheck`). Bajo demanda como los
  videos: menú System > *Prepare modding files* (un botón para videos, audio, modelos y escenarios) y `[data] export_stages = all | S233, S330` en
  `settings.ini` (avanzado), hilo aparte, se salta por versión del exportador (`STAGE_EXPORT_VERSION` + `STAGE_FILE_REV`), regla `.new`. Medido en el disco EU: 11 745 baldosas, 11 982 archivos (11 745 PNG + 237 JSON),
  0 fallos, 2.4 s y 256 MB la primera vez con PNG sin comprimir (ahora con deflate real, U3: 154 MB, ~10 s); 0.6 s el arranque que lo salta. Reemplazo: el juego
  copia `stream.c` en el host (`pc/field/stream.c`, igual que `file_loader.c`; `src/` intacto) y `FIELDSTG_loadStreamSprites` pregunta a
  `stageModTim(sector del PACK, baldosa, TIM)` antes de cargar el TIM; `mods/<mod>/stages/s<nnn>/tiles/<FF>_<CC>.png` y el PNG editado de
  `data/stages/s<nnn>/tiles/` por las reglas de las demás imágenes (`imgSubstitute`), con el resultado guardado por baldosa y revisado una vez por
  segundo; sin archivo devuelve NULL y todo queda igual (captura idéntica al binario anterior). Prueba: `make -C pc stagetest`.
- Escenarios como datos (port-close S1/S2, `shim/stage_codes.c`, `shim/stage_data.c`, `docs/stages.md`): `data/stages/s<nnn>/stage.json` (`stage-v1`) por cada `WSTAG<nnn>`
  (294 con el campo propio, 260): inicio, música, personajes con sus condiciones y charlas (con el texto como vista previa), salidas, eventos con su guion y las funciones
  de C que quedan fuera del dato. Al arrancar corre cada `setupStage` (tabla `SD_SETUPS` generada por la regla `stage_setups.c` de `pc/Makefile` desde los objetos de los
  escenarios) con el progreso 0 a 99, lee el de 0 a un `SdStage` (modelo portátil, sin encabezados del juego) y escribe el archivo; guarda y devuelve el estado que el setup toca,
  así que el juego queda idéntico. Misma regla de manifiesto y `.new` que las demás familias; se salta si la carpeta tiene `SD_EXPORT_VERSION` y todos los archivos. Los códigos
  `(código, valor)` y los comandos de guion se escriben simbólicos (`["flag", "1A:1F", 1]`, `["walk", 2, 191, 400, 3]`) y vuelven sin pérdida (lo que no tiene nombre es `raw`).
  Medido en el disco EU con una carpeta de usuario nueva: 294 escenarios, 3 710 personajes, 5 811 charlas, 8 499 listas de códigos (15 512 códigos), 450 eventos (365 guiones,
  51 529 palabras), 1 737 salidas, 0 diferencias al releer, 0 códigos `raw`, ~0.3 s; salida idéntica en dos corridas. Prueba: `make -C pc stagedatatest`.
- Parches de escenario y banderas de mods (port-close S4/S5, `shim/stage_mods.c`, `docs/stages.md` §7, `docs/modding/README.md` §6.11): `mods/<mod>/stages/s<nnn>/stage.json` (`stage-patch-v1`, el mismo camino que el dato de `data/`) cambia, agrega o quita
  los personajes de un escenario y sus charlas. Un solo camino de carga: tras el `setupStage` (gancho `pcStagePatchHook` en `startField`, `field.c`) se lee `FieldState.actors` al modelo
  (lo que el escenario entregó ESTA vez, con el progreso real), se mezcla el original con los parches en orden de carga (el último gana, conflictos al log) y el resultado va a buffers del puerto
  (dos juegos que alternan) que `FieldState.actors` pasa a apuntar; sin parche para el escenario el puntero no se toca (captura idéntica byte a byte con y sin mods en 380 y 330). Un `index` que no existe al progreso
  actual o una `image` que el escenario no tiene se omiten con una línea de log. El campo guarda 64 personajes del mapa en vez de 15 (`FIELD_NPC_MAX`, solo en PC). Banderas propias:
  `["mod-flag", "<mod>:<nombre>", 0|1]`, `["mod-var", ..., ">=", n]` y `["set-var", ..., n]` usan los grupos `0x62`-`0x68` de `checkCondition`/`applyAction` (sin uso en el juego; `#ifdef DW3_PC` llama a
  `pcModCheck`/`pcModApply`), el valor vive en `ext_state` por nombre y se guarda en la sección `EXTV` de la partida (todos los nombres se conservan al cargar, también los de un mod que no está). El validador
  (`--validate-mod`) rechaza un archivo inválido con su motivo y avisa de nombres que no son `stages/<número>.json`. Arnés: `DW3_MODVAR="<mod>:<nombre>=1"` y `DW3_LOG=trace` (qué charla diría cada personaje).
  Pruebas: `make -C pc stagemodtest` (mezcla, códigos, variables y `EXTV`) y `memcardtest` (la sección en el archivo de la ranura y la carga real). Ejemplo: `pc/examples/mods/training-field`.
- Condiciones de eventos, textos con nombre y acciones nuevas (port-close S3, S6, S7; `docs/stages.md` §8-§9, `docs/data-tables.md` §3.9, `docs/modding/README.md` §6.11):
  - **S3** (`shim/game_tables.c`, `gameTablesRegisterEvents`, una vez en el arranque y antes de cargar los parches de escenarios): `special_conditions` (`primitive`/`op`/`arg` como campos de dos nibbles del byte `check`),
    `money_required`, `money_gains`, `money_losses`, `progress_ranges` y `party_stat_thresholds` son tablas de mods con referencia en `data/tables/` y rechazo de fila entera. Un mod agrega condiciones con una fila
    `"<mod>:<nombre>"` (ids `0x100`-`0x1FF`, hasta 256, arreglo del puerto; `checkSpecialCondition` en `events.c` pregunta a `pcSpecialExtRow` bajo `#ifdef DW3_PC`) y las nombra desde un parche con
    `["special", "<mod>:<nombre>", 1]`. `newGame` llama ahora a `pcExtVarsReset()` por sí mismo (ya no depende de `pcPartnerEntriesReset`).
  - **S6** (`shim/stage_mods.c`, `shim/text_data.c`): `{"text": "<mod>:<nombre>"}` en una charla; palabras en `mods/<mod>/text/<idioma>/stages.json` (`stage-text-v1`, inglés obligatorio, es-419 cae a español y luego a inglés).
    Los nombres de un escenario reciben entradas después de las del disco (`tdStageTextFirst`), la capa de texto las agrega a cada idioma (`tdSetStageTextHook`, `tdAppendTexts`) y las reacomoda a la caja de charla (`TALK*` de `layout.json`).
  - **S7** (`shim/stage_mods.c`, ejecutor en `boot.c`): `give-money`, `take-money`, `set-progress`, `warp` y `sound` en los grupos `0x6A`-`0x6C`. Sin acción para dar un Digimon ni empezar una batalla (motivos en `docs/stages.md` §9).
  - **S7 corregido y S8/S10** (`docs/stages.md` §9-§10): `warp`, `start-battle` y `run-event` no actúan dentro de la charla: dejan una petición (`smPostRequest`) que la tarea del campo toma en su siguiente cuadro de
    ejecución (`pcStageWarpPoll`, `field.c`) y pide `FIELDSTG_leaveField` / `FIELDSTG_startBattle` / `FIELDSTG_startEvent`. La falla de `warp` con la prueba `DW3_STAGE_APPLY` era que la acción se aplicaba dentro del
    arranque del escenario, sin jugador todavía (`children->actors[0]` nulo en `FIELDSTG_closeField`). `stage-patch-v1` suma `slots` (parche, alta con `area` propia del puerto, baja que deja el slot como `SLOT_OFF`;
    `FIELDSTG_findTrigger` pregunta a `pcSlotAreaAt` antes de las celdas del mapa), `events` (parche y alta; ids desde 30 000, `smEventId`; textos del `talk` = entradas del archivo de texto del escenario,
    `FIELDSTG_createSpeech` bajo `#ifdef DW3_PC`) y `battles` (`patch` por área y batalla, `add` de un área; `pcBattlesInstall`). La exportación `stage-v1` trae `battles` (`SD_EXPORT_VERSION` 2: 294 escenarios, 0
    diferencias en la ida y vuelta, 174 con batallas). Arnés: token `fieldexit` (como `fieldauto`, y además pulsa cruz mientras el jugador está dentro del `area` de un slot de un parche), con `DW3_LOG=trace` la
    línea `the player is at (x, y), inside the area of slot N`. Pruebas: `make -C pc stagemodtest` (`testStageData`), `stagedatatest`.
  - Arnés: `DW3_STAGE_APPLY="<personaje>[:<rondas>]"` aplica las acciones de la charla que elige el intérprete para ese personaje de la lista final y registra dinero y progreso (con `DW3_LOG=trace`, la traza de cada
    personaje imprime también el texto que diría, resuelto por la capa de texto). Pruebas: `make -C pc tabletest`, `stagemodtest`, `texttest`.
- Copias crudas de lo que ningún exportador cubre (content-export E7, `shim/raw_export.c`, prueba `make -C pc rawtest`): 989 archivos y 15 438 836 bytes en
  el disco EU, en 12 tipos (`stage-map` `TMPK` 294 / 5 101 880, `stage-demo` 2 / 58 328, `battle-effect` 239 / 3 998 932, `battle-common` 6 / 116 886,
  `training-data` 12 / 2 211 892, `sprite-sheet` los `...CS.BIN` 15 / 64 064, `card-opponents` 1 / 35 360, `card-pack` 1 / 499 280, `text-unused` `COUNTRY/JPN` y
  `USA` 100 / 1 038 172, `stage-overlay` `WSTAG*.PRO` 294 / 1 253 385, `mode-overlay` 24 / 972 219, `filler` 1 / 1; el tipo `other` recoge lo que no nombre
  ninguna regla y en el disco EU queda vacío). Qué queda cubierto sale de los mismos predicados que usa el exportador de imágenes (`imgIs*`, `sprIsSpriteFile`) y
  de las carpetas de texto (5 idiomas), audio, video, `Z_STAGE` y `FIGHT/MODEL`: un exportador nuevo saca sus archivos de aquí cambiando `rawKindOf`. Salida:
  `data/raw/<ruta del disco sin AAA/ en minúsculas>`, `README.txt` (inglés, generado), `raw.json` (`raw-v1`: notas por tipo y lista de archivos con tamaño y tipo)
  y `manifest.json` (hash por archivo, versión del exportador por tipo, en `exporter` de la cabecera). `manifest.json` es el `asset-manifest-v1` de `tdManifest`, por eso las
  notas van en `raw.json` y no dentro de él. **Al arrancar, no bajo demanda:** son 15 MB (frente a los 256 MB de `Z_STAGE`), 1.5 s en total la primera
  vez y 1.1 s el arranque que lo salta; sin hilo ni menú, antes de abrir la ventana, como las imágenes. `[data] export_raw` = `all` (por defecto si falta la línea) |
  `off` | tipos separados por comas. Regla `.new` y salto por versión del exportador (`RAW_EXPORT_VERSION`; un tipo de la versión actual con todos sus archivos presentes no
  lee el disco). Reemplazo (verificado leyendo el código y una corrida): la capa de archivos de `port_fs` (`mods/<mod>/files/<ruta del disco>`) sirve todo lo que el juego
  lee por `FILE_CACHE`/`cdRead`; lo que lo salta es `CD_READER.read` directo (las baldosas de `Z_STAGE`, que tienen su gancho) y las películas/XA. `TMPK`: corrida por
  estado `stay,warp:330` con un `S330TMPK.BIN` modificado en un mod: `read 437 DAT/FIELD/STAGE/S330TMPK.BIN ... src=raw` (sin el mod `src=disc`). Efectos, demos,
  `F000COM`/`PGFGT`, hojas `CS` y `CARD_NPC` los lee el juego por la caché, así que deberían funcionar, pero no se corrió ninguno con un archivo reemplazado. `TRANIN`,
  `CARDPACK`, `COUNTRY/JPN|USA`, `NONE____` y los `.PRO` de código no los lee nadie (los `.PRO` están enlazados: `loadSubOverlay` no copia nada); `SMDLDATA.PRO` sí se lee.
- `sprite.json`: `format` (`"sprites-sprite-v1"`; los `partners/<nombre>/field/` escriben `"sprite-v1"`, los mismos campos, y el lector acepta los dos), `source` (la ruta del disco), `set_table` (entrada con los conjuntos; -1 si no
  hay), `frames` (`id`, `image`, `w`, `h` en píxeles del PNG; `"used": false` si ninguna animación lo usa),
  `animations` (`id` = entrada, `loop`, `ticks` = suma de tiempos, `steps`: `frame`, `time`, `x`, `y`; `x`,`y` colocan
  la esquina superior izquierda respecto a la posición del actor, en píxeles), `sets` (`id`, `name` si el decompilado
  lo nombra con `ACTOR_ANIM_*`, y `animations`: la animación de cada una de las 5 direcciones o "ranuras") y `directions`
  (las 8 orientaciones: ranura que usa, `flip`, y `step` = hacia dónde da el paso). Las orientaciones 5 a 7 reusan las
  ranuras 3 a 1 dibujadas en espejo (el `x` pasa a ser `-(w + x)`).
- Paletas: cada cuadro trae **una** paleta de 16 colores (4812 de 4812); no hay variantes de color dentro de un archivo.
  Las variantes (traje, estado) son archivos distintos (`P002PLAY`, `P002PLYD`, ...). Un mod que cambie colores edita la
  paleta de cada PNG.
- Reglas de edición (también en `notes`): conserva ancho y alto de cada PNG y no pases de 16 colores; el JSON es solo
  informativo y se regenera. La unidad de `time` es el tic del motor (cada actualización resta `getFrameTime()`).
- Vista previa (Pillow): `python3 tools/image_atlas_preview.py <data>/sprites/p001play` dibuja todos los cuadros
  numerados por id (en rojo los sin uso); `--anim <id>` o `--set <conjunto> --dir <0-7>` dibujan esa animación como tira de
  pasos sobre la posición del actor (la cruz); `--gif` escribe un GIF (`--tick-ms`, por defecto 1000/60: supuesto sin
  verificar). Sin `--out` va a la carpeta temporal.
- **Mods de sprites (T3, A7.5).** Mismas fuentes, orden y reglas que el arte de menús ("Mods de imágenes"), con la ruta
  `mods/<mod>/sprites/<nombre>/<EEE>.png` (y un `data/sprites/...` que editaste, juzgado contra `sprites/manifest.json`).
  Una carpeta de mod con el nombre de una original (`p001play`) son los cuadros de esa original y su `sprite.json` no se lee; cualquier otro nombre es un
  sprite nuevo del mod (`sprite.json` + un PNG por cuadro, `docs/stages.md` §11), como `s<nnn>` y `c<nnn>` en escenarios y cartas. `<EEE>` es la entrada del cuadro
  (la que dice `sprite.json`). Cada cuadro es de 4 bits: mismo ancho y alto, a lo sumo 16 colores, y el PLTE del PNG
  reemplaza la paleta propia del cuadro (cada cuadro lleva la suya, no hay otra que tocar); alfa 0 = transparente. Las
  imágenes de `field/fieldcom/02_00.png` y `03_00.png` siguen las reglas de las páginas crudas del arte de menús (la paleta
  es la que nombra la hoja de su entrada).
  Cómo se resuelve (`imgSpriteOverridesBuild`, `imgOverrideGet` en `shim/image_data.c`): al primer cuadro que el juego
  sube de un archivo solo se mira **qué cuadros tienen un PNG** (una llamada `stat` por cuadro y fuente); el PNG se lee,
  valida y sustituye la primera vez que ese cuadro se pide y se guarda. La búsqueda es por desplazamiento (búsqueda
  binaria, el último archivo usado se recuerda); las fuentes salen del índice en memoria (arriba) y no se revisan mientras se juega
  (salvo `DW3_MODS_HOTRELOAD=1`: como mucho una vez por segundo **en total**, del archivo que se está usando). Registro: una línea por archivo y mod,
  `images: override sprites/p002play/<EEE>.png (87) from mods/<mod>` (cuántos cuadros ofrece ese mod); un PNG rechazado
  dice `images: <ruta> rejected: <motivo>` al pedirse su cuadro. Con `DW3_IMG_TRACE=1`, `images: load <ruta> (override|disc)`
  por cada subida.
- **Qué archivo es el protagonista.** Depende de la escena: en el Parque Central de la partida nueva (`warp:330`) el jugador
  es `P002PLAY` (cargó el cuadro 004, visto en el log); el decompilado da `P001PLAY` al personaje 1 (no verificado en otra
  escena). Para un mod del jugador se recolorean los dos. Con `DW3_IMG_TRACE=1` el log dice qué archivos y cuadros se cargan.
- **Ejemplo.** Azules a rojo en `P001PLAY`, `P002PLAY` y las poses `P002PLYD/F/K/L` (guía §6.3); los PNG los
  genera `tools/image_mod_palette.py --sprite NOMBRE` desde tu propia copia.
- Código: `shim/sprite_data.c` (`sprExtractFile`, `sprJson`, `sprTable`) y el reparto de hojas de `FIELDCOM` en `shim/image_data.c`.
  Prueba: `make -C pc imagetest` (archivo sintético con cuadros, animaciones y conjuntos; sin conjuntos; animación sin
  marca de fin; archivo que no lo es; `FIELDCOM` con dos hojas).

## Reportes de crash (R1)

Una señal fatal (SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT) escribe su reporte en el log del propio juego (`logs/dw3-*.log`; en Android también
`harness.log` y logcat, etiqueta `dw3`) y muere con la misma señal. Está en `shim/fault_report.c`: seguro dentro de una señal (el descriptor del
log se abrió antes; solo `write()` de bytes ya formateados, sin stdio, malloc ni locks; la tabla de módulos se toma al arrancar). Un reporte:

```
boot: fatal signal 11 (SIGSEGV) code 1 fault addr 0x0000000000000000 tid 671487 thread "boot"
boot: pc 0000652b05206718 exe+0x405718 sp 00007ffe128620d0
  #00 pc 0000652b05206deb exe+0x405deb
  #02 pc 0000652b05206718 exe+0x405718
boot: module exe build-id 2f7d87007e7cdf21bd9ba5e6b7167456b2db783e
boot: end of report
```

Cada marco es `módulo+desplazamiento`; el build id de cada módulo que aparece sale una vez al final (en Android, `libmain.so` y `libopenal.so`).
Para nombrar la línea hace falta el binario **sin recortar** con ese mismo build id:

- Escritorio: `addr2line -f -e pc/build/boot 0x<desplazamiento>` (el binario del `make` lleva `-g`).
- Android: cada `pc/android/build.sh apk|release` guarda `libmain.so` y `libopenal.so` sin recortar en `pc/build-android-symbols/debug/` o
  `.../<versión>/`, con `BUILD-ID.txt` (los ids del APK y de los guardados coinciden o el script falla). Un comando:
  `llvm-addr2line -f -e pc/build-android-symbols/<debug|versión>/libmain.so 0x<desplazamiento>` (el `llvm-addr2line` está en el NDK, o dentro
  de la imagen con `docker run --rm -v $PWD:/dw3 -w /dw3 dw3port-android sh -c '$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-addr2line ...'`).
  Si el build id del reporte no es el del `BUILD-ID.txt`, el binario no es el de esa instalación. Los marcos 1 en adelante son direcciones de
  retorno: para la línea de la llamada, restar 1 al desplazamiento.

Límites conocidos: la pila alterna es solo del hilo que instala el reporte (el del juego); una librería cargada después de arrancar no está en la
tabla (sale la dirección sin módulo). Prueba: `make -C pc faulttest` (formateador, argumento y un hijo que se cae por cada señal y deja el reporte en un log).

## Compilación cruzada a Windows (windows-build, W1)

`make -C pc TARGET=windows` produce `pc/build-win/boot.exe` (MinGW-w64 x86_64, hilos posix) sin tocar el árbol de Linux
(`pc/build`). Un solo comando; `pc/win/fetch_deps.sh` (lo invoca la regla `psycross`) baja SDL2 2.32.10 (paquete
`-mingw`) y OpenAL Soft 1.24.3 a `~/.cache/dw3-win` (`DW3_WIN_CACHE`), con versiones fijas y sha256 (anotado en la
primera descarga); nada de eso se versiona. PsyCross se compila con `pc/win/mingw-w64.cmake` en `build/psycross-win`.
Junto al `.exe` van `SDL2.dll` y `OpenAL32.dll` (el `soft_oal.dll` de OpenAL Soft renombrado); libgcc, libstdc++ y
winpthreads van enlazadas estáticas. Subsistema GUI (`-mwindows`, ver W2 abajo); `main()` sigue siendo `main` (`SDL_MAIN_HANDLED`,
sin `SDL2main`).

**Captura de ámbitos de estado, portable (decisión).** Se evaluaron tres caminos: (a) tabla generada desde los objetos
con `nm`: los `static` no se pueden nombrar desde otro objeto, así que no los cubre; (b) enlace en dos pasadas con rangos
absolutos: frágil (el diseño de la imagen cambia entre pasadas, y cada ámbito sería una lista de trozos, no el rango
contiguo que `state_scope.c` reinicia hoy); (c) **los mismos enunciados de enlazador** (`ovl_sections.ld` y el gemelo
generado de los 294 escenarios son la única fuente de verdad), traducidos por plataforma. En Linux no cambia nada
(`INSERT` ELF, una sección por ámbito, mismas 312 reglas). En PE `check_scopes.py pe` deriva un script con **una sola
sección `.ovldat`** que contiene los 312 pares `__ovl_<n>_data_start/_end` (el cargador de Windows limita las secciones a 96, dato
no probado aquí; ld exige además `BLOCK(__section_alignment__)` en el encabezado, o la imagen sale con una sección sin alinear
y Wine la rechaza como "Bad EXE format"), con los patrones ampliados a `.data$*`/`.bss$*` de COFF. Una línea sin
objeto (`*(.bss.ovl_cardgame)`) conserva su lista: ensancharla se tragaba todo el `.data`. `check_scopes.py check`
detecta PE por la firma `MZ`, usa el `nm`/`objdump` con prefijo (`CROSS_PREFIX`) y falla igual si un símbolo escribible
(estático incluido) de un objeto del ámbito queda fuera de su rango. Resultado: 312 ámbitos OK en ambos destinos.
Los objetos de los escenarios se renombran con `objcopy` por prefijo; los nombres que empiezan por punto (los `.refptr.*`
COMDAT de MinGW) no se renombran, o el enlazador pierde el COMDAT.

**Tipos (LLP64).** `win/bsd_types.h` (forzado con `-include` en el juego y en PsyCross) define `u_char/u_short/u_int` y
`u_long` de **64 bits** (`unsigned long long`): en Linux `u_long` es de 64 bits y el puerto depende de que quepa un
puntero (las tablas de ordenación, `shim/gfx_engine.c`); el `unsigned long` de MinGW es de 32 y truncaba esos punteros
(en Wine corría porque, por lo que se ve, su heap queda bajo 4 GB: hipótesis; un Windows real puede darlas más arriba). `ps1_types.h` sigue dando el `u_long` de 32 bits donde
hace falta, en ambos destinos. `MemCardSync` escribía 64 bits por el puntero que le pasa el juego (un `long`): ahora
escribe un `long`. `host_memcard.c` sirve las mismas llamadas `MemCard*` (las pantallas STGMCARD no cambian) sobre archivos propios (desde extended-state T2, ADR-008 opción B: la tarjeta ya no es el almacén), `saves/default/slot<N>.dw3save`
(formato `dw3save-v1`, `shim/save_file.c`: secciones con etiqueta, CRC32, escritura atómica con `.bak`) y `card.dw3card`
(existencia, nombre, cabecera e info del archivo de partida); la vista legible de cada casilla va en
`saves/default/view/slot<N>.json`. La carpeta es `$DW3_SAVES_DIR` o `saves/default` de la carpeta del usuario (ADR-010).
Las tarjetas `.mcd` de `import/` (todas, por nombre sin distinguir mayúsculas, hasta que una dé partidas; `DW3_MEMCARD`
nombra una sola, para los arneses) se importan una sola vez (solo si la carpeta de partidas no tiene `card.dw3card` ni
casillas y `import/` no tiene `imported.txt`) y no se modifican; para reimportar, borrar `imported.txt` y la carpeta de
partidas. No se lee ninguna ruta vieja (`memcard0.mcd` suelto, `dw3-port`). `make -C pc savetest memcardtest`. Para pruebas con
el juego real: `DW3_SEED_ITEMS="id=cantidad,..."` (da objetos justo antes de abrir la pantalla de tarjeta) y
`DW3_ITEM_DUMP=1` (imprime los conteos no nulos al terminar, línea `[state] items:`). `boot.c`: sin `execinfo.h` ni `SIGBUS` en Windows (el manejador solo nombra la señal).
Otros: `-D__USE_MINGW_ANSI_STDIO=1` (`%zu`), `-Wno-format` (el verificador de gcc usa el arquetipo de msvcrt),
`SDL_setenv` en vez de `setenv` en `snd_spu.c`, `uintptr_t` en dos impresiones de depuración de `gfx_engine.c`,
`(unsigned long)` en `rd32` de `menu_frame.c`. Pendiente conocido: `fseek` de `host_cd.c` usa `long` (imágenes hasta 2 GB).

**Arranque para jugadores (windows-build, W2).** Sin argumentos (doble clic) el programa es el del jugador: pide la
imagen del disco propio (`.bin`, `.cue` o `.iso`; un `.cue` se resuelve a su `.bin`) con el diálogo nativo
(`third_party/tinyfiledialogs`, zlib: `GetOpenFileName` en Windows; `zenity`/`kdialog` y afines en Linux, y si no hay
ninguno un mensaje que dice cómo iniciar con la ruta), lo valida y recuerda la ruta en `settings.ini` (`[disc] path`,
clave oculta). Validación (`shim/disc_picker.c`): ISO 9660 en tramas crudas de 2352 bytes, `SYSTEM.CNF` con
`BOOT = cdrom:\SLES_039.36;1` (el disco europeo). Si falla, un cuadro de mensaje (español para locales `es*`, inglés
si no) dice por qué (no abre / no es imagen de PlayStation / formato `.iso` de 2048 / otro disco, con el id detectado) y
ofrece elegir otro o salir; la ruta recordada que ya no sirve pasa por el mismo camino. Todo el texto de salida va a
`<pref>/logs/dw3-AAAAMMDD-HHMMSS.log` (se conservan los 10 últimos), en ambos destinos. La línea de comandos de
desarrollo (`boot <disco> ...`, `play.sh`, `review.sh`) no cambia: conserva stdout/stderr y no toca el diálogo.
Arrastrar y soltar (`SDL_DROPFILE`) no se hizo: el cuadro de mensaje es modal y no entrega eventos; queda el diálogo.
`DW3_PICK_DISC=<ruta>` sustituye al diálogo (pruebas): la ruta se valida igual y un fallo sale en el registro, sin
ventanas. **Modo portátil:** un archivo `portable.txt` junto al ejecutable hace que `settings.ini`, `data/`, `logs/` y la
tarjeta de memoria vivan en la carpeta del ejecutable en vez de `SDL_GetPrefPath`. **Disco portátil (W2b):** en modo portátil, si la imagen elegida está dentro de la carpeta del ejecutable, `[disc] path` se guarda relativo a ella (con `/`) y un valor relativo se resuelve contra esa carpeta, así que una memoria USB con otra letra de unidad sigue encontrando el juego; las rutas absolutas siguen valiendo. Sin ruta válida recordada, antes del diálogo se busca en la carpeta del ejecutable y sus subcarpetas directas (primero `.cue`, luego `.bin` y `.iso`) la primera imagen europea válida, que se usa y recuerda sin preguntar. **Subsistema:** `boot.exe` se enlaza
`-mwindows` (sin consola); `make -C pc TARGET=windows CONSOLE=1` produce además `build-win/boot-console.exe`
(`-mconsole`) para desarrollo. Pendiente conocido: `fopen` en Windows usa la página de códigos ANSI, así que una ruta del
disco con caracteres fuera de ella puede no abrir.

**Prueba en Wine, sin ventana ni sonido.** `wine64` (prefijo propio: `WINEPREFIX=~/.cache/dw3-win/prefix WINEARCH=win64`),
sin `DISPLAY`/`WAYLAND_DISPLAY` (Wine usa su controlador nulo y OpenGL por EGL sin pantalla), `SDL_VIDEODRIVER=windows`
(el `offscreen` de SDL en Windows no tiene GL y el `dummy` tampoco), `SDL_AUDIODRIVER=dummy`, `ALSOFT_DRIVERS=null`,
`DW3_SETTINGS=Z:/ruta/nueva.ini`:

```
wine64 pc/build-win/boot.exe "<disco>.bin" 300 Z:/tmp/shot.png
DW3_BATTLE_EVENT_LOG=1 wine64 pc/build-win/boot.exe "<disco>.bin" 3000 Z:/tmp/shot.png 0 \
  "stay,warp:330,optlang:4,fieldbattles:3,battlepilot"
```

La corrida encadenada da en Wine la misma traza que en Linux: el primer `POP` de las tres batallas trae
`q=[3@615]`, `q=[2@615]`, `q=[3@545]` (una sola entrada).

## Archivos

- `pc/Makefile` — el build del host (los siete binarios y el build propio de PsyCross).
- `pc/rlen_tool.c` — arnés del hito 0: invoca el descompresor del juego.
- `pc/file_dump.c` — arnés del hito 1: resuelve un índice por el camino del juego y lo vuelca.
- `pc/text_measure.c` — arnés del hito 2: mide cadenas con el motor de texto del juego.
- `pc/draw_order.c` — arnés del hito 3: arma una orden de dibujo con la ventana de texto del juego.
- `pc/first_frame.c` — arnés del hito 4: abre la ventana y presenta el banner del juego.
- `pc/font_frame.c` — arnés del hito 5: sube la fuente del disco a la VRAM con el cargador de TIM
  del juego y dibuja texto con la ventana de texto del juego (captura PNG propia).
- `pc/boot.c` — hito 6: el punto de entrada del programa, su tabla de puntos de entrada de
  modos (el despacho propio; desde el hito 8 con el de país y el de título y desde el hito 9 con
  el del campo, índice 2 = `FIELDSTG`) y la llamada al
  `main()` del juego; los dos buffers del mapa de memoria (`OVERLAY_VRAM`, `STAGE_VRAM`).
- `pc/shim/host_gs.c` — **libgs**: `GsInitGraph`, `GsInit3D`, `GsGetTimInfo` (la imagen de
  arranque, que en el host no existe).
- `pc/shim/host_snd.c` — **libsnd**: stub documentado, sin audio.
- `pc/shim/host_press.c` — hito 8: **libpress** (`DecDCT*`) y, hasta T38, la mitad de
  *streaming* de `libcd`. Desde T39 es la fachada real del decodificador: `DecDCTvlc2` arma el
  run-level con `host_mdec.c`, `DecDCTin` decodifica el cuadro a 15 bits y `DecDCTout` lo
  presenta en la VRAM cuando vuelve la cadena de franjas (ver "La pantalla de título y el
  video"). El *streaming* vive en `pc/shim/host_st.c` (T38).
- `pc/shim/host_mdec.c`/`.h` — T37: el decodificador MDEC (VLC, des-cuantización, IDCT,
  YUV→RGB24/BGR555), sin PsyQ ni PsyCross; lo usan `pc/mdectest.c`, `pc/sttest.c` y el binario
  `boot` por `host_press.c`.
- `pc/shim/host_overlay.c` — hito 9: el reemplazo del cargador de overlays del juego
  (`OVERLAY_LOADER`). No copia nada (los modos y los stages están enlazados) y la línea del stage
  dice cuál pidió el código del juego.
- `pc/shim/gfx_engine.c` — hito 6: el estado `GFX` y las capas de dibujo (el reemplazo de
  `display.c` y `layer.c`), y el fin de cuadro que captura y sale. Desde T16 también lee de
  vuelta las regiones de VRAM que el arnés le arma (`pcBootArtInit`) y las imprime con los
  sprites y los `DR_TPAGE` del cuadro: es de donde `pc/check_country.py` saca sus medidas. Desde
  T26, con `DW3_CB_DUMP=<cuadro>` en el entorno (apagado por defecto: sin la variable la corrida
  no imprime nada extra), imprime el orden de registro y el orden de la lista de los callbacks de
  cada capa en ese cuadro, más una sonda de la inserción con empates y centinela: es de donde
  `pc/check_field.py` saca su medida del orden por prioridad. Desde T27 manda el `DRAWENV` de
  cada capa justo antes de su propia tabla (y el clear del cuadro con el env de la primera capa
  con fondo), pone el bit `dfe` de cada `DR_TPAGE` de la tabla (la única superficie del port es la
  ventana; el recorrido cubre la tabla entera: su cota es `layer->otLen + GFX.primBufSize`) y,
  con `DW3_ENV_DUMP=<cuadro>` y, opcionalmente, `DW3_ENV_DUMP2=<cuadro>` (ambos apagados por
  defecto), imprime por capa su `DRAWENV`
  propio con su offset y su largo de OT, el env con el que se mandó su tabla, sus `DR_TPAGE`
  (cuántos traían `dfe 0` antes del pase y cuántos llevan `dfe 1` después) y el
  env del clear: es de donde `pc/check_field.py` saca su medida por capa. Desde T43, con
  `DW3_OT_DUMP=<cuadro>` (apagado por defecto), imprime cada primitiva de cada tabla en el orden
  en que `DrawOTag` la recorre, con la página de textura vigente en ese punto, y con
  `DW3_VRAM_DUMP=<archivo>` guarda la VRAM de ese mismo cuadro con `GR_SaveVRAM` de PsyCross
  (un TGA de 16 bits de 1024x512, eje Y invertido, aunque el nombre diga otra cosa). Con
  `DW3_SKIP_OT_DRAW` (apagado por defecto), ninguna capa envía su tabla: el cuadro queda
  reducido a su clear más lo que dibujen el resto de caminos del shim (por ejemplo el
  presentador de las películas); es el instrumento con el que se midió que las bandas del
  fondo del título no venían de los primitivos de las tablas (T43).
- `pc/shim/host_shot.c` — la captura PNG compartida (`pcSaveShot`): `glReadPixels` de la
  ventana, PNG de bloques *stored*, sin bibliotecas.
- `pc/shim/host_pad.c` — hito 7: el lado host del input del juego. Aporta `__wrap_PadChkVsync`
  (el `PadChkVsync` que la `libpad` de PsyCross contesta 0), llama a `PsyX_UpdateInput` una vez
  por cuadro, aplica la secuencia guionada de la demo escribiendo el pad (con el token `wait` de T18, una
  barrera que espera el cambio de modo) e imprime lo que el código del juego vio y la opción que
  resalta su menú.
- `pc/shim/heap.c` — `HEAP` sobre `malloc`/`free`, con el tag de cada bloque (`freeByTag`) y
  las asignaciones puestas a cero con el doble de bytes (ver el hito 6).
- `pc/shim/task_registry.c` — `TASK_REGISTRY`: el registro de tareas y sus ocho métodos (hito 6).
- `pc/shim/host_cd.c` — `CD_READER` (reemplazo de `libcd`) que lee el `.bin` del host, más las
  órdenes de la lectora que el arranque del juego manda (`CdInit`, `CdControl`, `CdRead2`, ...).
- `pc/shim/host_gfx.c` — `GFX` con el reloj que la caché de archivos usa.
- `pc/shim/host_draw.c` — `GFX.funcs.getLayer/getPrim/setPrim/getFrameTime`, la `Layer` del
  dibujo (con su *ordering table* y su búfer de paquetes) y `PAD`/`SOUND`/`GAME` (hitos 3 y 4).
- `pc/shim/host_tim.c` — `initTimLoader` como stub, para los arneses que no enlazan la GPU
  (hitos 2 y 3); el hito 5 compila el cargador real.
- `pc/shim/host.h` — las llamadas de arranque de los shims (`pcCdReaderInit`, `pcGfxInit`,
  `pcDrawInit`, `pcDrawBeginFrame`, `pcSaveShot`, `pcBootPresentInit`, ...).
- `pc/psx_compat.h` — el puente de nombres (`ONE`, `RECT`, `RotMatrixYXZ_gte`, `<assert.h>`)
  para las unidades que dibujan contra PsyCross (hitos 3, 4 y 5).
- `pc/ps1_types.h` — `u_long` de 32 bits, para `tim_loader.o` (hito 5) y `movie.o` (hito 8; ver
  el hallazgo).
- `pc/psx_press.h` — el puente de las tres funciones de libpress que PsyCross's `libpress.h` no
  declara (`DecDCTvlc2`, `DecDCTvlcBuild`, `DecDCToutCallback`), solo para `movie.o` (hito 8).
- `pc/check_rlen.py` — test del hito 0 (escrito primero).
- `pc/check_file.py` — test del hito 1 (escrito primero).
- `pc/check_text.py` — test del hito 2 (escrito primero).
- `pc/check_draw.py` — test del hito 3 (escrito primero).
- `pc/check_font.py` — test del hito 5 (escrito primero): los bytes de la fuente en VRAM y los
  UV/CLUT de los primitivos contra el disco y la tabla de glifos.
- `pc/check_country.py` — test del hito 6 (T16): el arte del banco de la selección de país en
  la VRAM (imagen de 8 bits y CLUT de 256) y el sprite del panel izquierdo con su página y su
  CLUT, contra el disco y la tabla de partes del banco.
- `pc/check_input.py` — test del hito 7 (T17): corre la demo guionada (`down,down,up`) y
  verifica, de su propia salida, que cada pulsación llegó al pad del juego y que su menú movió
  el resaltado `0 → 1 → 2 → 1`.
- `pc/check_title.py` — test del hito 8 (T18, T39, T42): corre la demo del título
  (`down,down,down,down,start,wait,start`) y verifica, de su propia salida, que el juego pasó por
  los modos `0x1600 → 0xE02 → 0xE00`, que el MDEC del port decodificó y presentó los cuadros del
  video, que la pantalla de título dibujó sus sprites en su propia capa y —en una segunda corrida
  que deja la película correr— que el cuadro del video está en pantalla y en su rectángulo (T42;
  la corrida del título capturaba el splash de Bandai, no el cuadro).
- `pc/check_field.py` — test del hito 9 (T19/T22/T23/T24): corre la demo del campo (el guion que navega el
  menú del título hasta NEW GAME, confirma y avanza una vez el evento inicial) y verifica, de su propia salida, que el juego pasó
  por `0x1600 → 0xE02 → 0xE00 → 0x2D7`, que pidió el stage enlazado (0x1E9), que creó y dibujó
  sus capas `0x1000`-`0x1004`, que siguió dibujando más allá de los cuadros 468 (el cargador sin
  guard), 474 (el callback con el puntero truncado, que T23 arregló) y 562 (el camino de comandos
  de guion, que T24 arregló), que el actor se mueve por el camino de consulta del mapa, y que el run alcanza su presupuesto (1000 cuadros) y deja una captura
  de 320x240. Un segundo argumento es el control negativo (el guion del título: el campo nunca se
  entra).
- `pc/shim/text_codec.{c,h}` — códec de textos del juego (feature `es419-live-option`, L2a): lee y arma
  archivos de idioma y paquetes `SSDMG*` conservando lo necesario para reconstruir byte a byte (hueco tras la
  cabecera, offsets compartidos, colas), charmap y alias legibles de `tools/es419/lang.py` (que sigue siendo el
  oráculo), JSON con el esquema del overlay y la mezcla base + overlay. C puro, sin dependencias. Aún no está
  conectado al juego.
- `pc/texttest.c` — prueba del códec (`make -C pc texttest && pc/../build/texttest`): casos sintéticos y,
  con `texttest <disc-dir> <overlay-dir> <work-dir>`, la paridad local contra `lang.py`/`build_text.py` sobre
  el disco del dueño (solo local: escribe texto decodificado, que no se versiona).
- `pc/psycross/CMakeLists.txt` — build propio de PsyCross (submódulo intacto, T11; T13 apaga
  PGXP acá).
- `pc/psycross/check_packets.c` — consumidor C11 que fija el layout de paquetes (T11).
- `pc/README.md` — este documento.

### Registro de turnos de batalla (`battle_trace`)

`pc/shim/battle_trace.{c,h}`: con `DW3_BATTLE_EVENT_LOG=1` se escribe en stderr una línea `battle-trace:` por
cada evento que sale de la cola de batalla (turno del jugador, del enemigo y otros), con los PV, las banderas de
estado y la velocidad de cada lado y la cola completa, y una línea por cada tirada de parálisis ("turn LOST" si se
pierde el turno). Solo registra: no cambia el juego. `pc/review.sh inicio` lo activa siempre. Las demás variables
(`DW3_BATTLE_SCRIPT`, `DW3_BATTLE_ENEMY`, `DW3_BATTLE_PARALYZE`...) arman escenarios de prueba y están descritas en
`battle_trace.h`. Comprobado: con `DW3_BATTLE_PARALYZE=200` en `battleauto` se ven turnos perdidos seguidos.

### Ámbitos de estado (`state_scope`, overlay-state-reset R2a/R2b)

En la PlayStation cada modo es un overlay que se copia del disco al entrar (`src/engine/system/overlay.c`), así que
sus variables vuelven a su valor inicial; el port enlaza todo en un binario y nada las reiniciaba (la cola de turnos
`FIGHTSTG_events` arrastraba turnos de la batalla anterior). `pc/shim/state_scope.{c,h}` es un registro dinámico de
ámbitos: rangos de bytes escribibles, un padre opcional y ganchos `onEnter`/`onExit` que los shims y los mods pueden
registrar. Entrar a un ámbito corre los `onExit` de los activos (hijos primero), restaura el ámbito y sus hijos a la
instantánea (tomada de memoria al registrarlo, con el runtime de C ya inicializado) y corre los `onEnter` en orden de
registro. La PlayStation solo dice CUÁNDO: un ámbito de modo se reinicia únicamente si `mode >> 8` cambia respecto de la
entrada anterior (campo -> campo no), y entrar a un modo olvida el sub-overlay; el de un sub-overlay, si cambia su
archivo (`stateScopeModeEntered` en `pcUpdateModeTask` de `pc/boot.c`, `stateScopeSubLoaded` en `loadSubOverlay` de
`pc/shim/host_overlay.c`).

Captura sin tocar `src/` (R2b: todos los overlays): `pc/ovl_sections.ld` (fragmento `INSERT`, se pasa con `-T` al
enlazar `build/boot`) reúne las secciones escribibles de cada grupo de objetos en una sección `ovl_<nombre>_data`, con
`__ovl_<nombre>_data_start/_end`. Ámbitos de modo (id = `mode >> 8`): `fightstg` (6), `stfgtrep` (0x14), `fieldstg`
(2, `build/field/*.o` con las copias `pc/field/*.c`), `ststatus` (0x10), `stgmcard` (0xC), `stgdglab` (0xD),
`stdgname` (0xB), `stgtrain` (0xA), `stitshop` (0xF), `stcrdshp` (0x13), `stcrddek` (4), `stcrdabm` (0x12), `cardgame`
(7, con `CARDGAME_markedCounts` del host, puesta en la sección `.bss.ovl_cardgame`), `stdwtitl` (0xE; objetos planos de
`build/` por nombre), `stplnmet` (5; `plnmet_*.o`) y `cntysel` (0x16). Sub-overlays hijos de `fightstg`: `wfightmn` y
`wfightts` (el banco de pruebas de batalla). Escenarios del campo: el Makefile genera `build/ovl_stages.ld` y
`build/stage_scopes.c` (`check_scopes.py gen`) con un rango `wstagNNN` por escenario (294, unos 1,4 MB en total); en
`pcRegisterStateScopes` cada rango se liga como hijo de `fieldstg` a todos los ids de archivo de las tablas
`FIELDSTG_extraStages`/`FIELDSTG_stages` cuyo `init` es su `WSTAGnnn_startStage` (el capítulo extra reutiliza código
con otros archivos: cada id de archivo reinicia al cambiar, como cada .PRO en la PlayStation). Cada escenario reinicia
solo su rango. Se conservan los casos sin reinicio del original: campo -> campo (mismo id), mismo escenario vuelto a
entrar, y título/películas dentro de 0xE. `pc/field/state.c` (borrado de `FieldState` por nombre) sigue igual.
`pc/check_scopes.py check` corre al terminar el enlace: lee ambos `.ld`, busca los objetos que cada sentencia reclama y
falla si un objeto lo reclaman dos, si una sección escribible no la mueve la sentencia, si un símbolo escribible queda
fuera de su rango o si aparece `.data.rel.ro`; imprime símbolos y bytes por ámbito. La tabla fija `PC_MODE_ENTRY_POINTS`
sigue diciendo qué arrancar y las tablas del registro dicen qué reiniciar.
`battle_trace` reinstala sus envolturas de `pop` y `testParalysis` en un `onEnter` de `fightstg`. Prueba de la lógica:
`testStateScopes` en `inputtest`. Tres batallas en una corrida (`stay,warp:330,optlang:4,fieldbattles:3,battlepilot` con
`DW3_BATTLE_EVENT_LOG=1`): la primera línea `POP` de cada una trae solo los turnos de apertura.

### Traza de tiempos del cambio de escenario (M0)

`DW3_STAGE_TIMING=1` imprime a stderr una línea por evento, `[stage] t=<ms> f=<cuadro> <evento>`
(`t` monotónico desde el primer evento, `f` el contador de cuadros de 60 Hz): cada lectura de disco
(`read file=<índice> off sectors bytes src=disc|override ms=<tiempo leyendo>`, en `shim/host_cd.c`),
el cambio de modo, los estados de la tarea del campo y del banner del nombre del área, `frozen=` del
jugador y los cuadros lentos (más de 25 ms). Con `DW3_STAGE_TIMING=2` la corrida termina sola cuando el
banner ya no está y el jugador se descongela (`playable`). Apagado por defecto, sin costo. El sondeo por
cuadro está en `shim/stage_timing.c`, llamado desde `hostDrawFrame`; no toca archivos del juego. Ejemplo:
`DW3_STAGE_TIMING=2 SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null pc/build/boot <disco>.bin 9999999 x.png 0 warp:330`.


Eventos que añadió M0b, con cualquier nivel: `first frame, N ms after the process started` (un constructor en
`shim/host_cd.c` toma el reloj antes de `main`), `playable` en cada campo (el jugador libre, en cada modo del campo y no
solo en el primero) y, en la batalla (`0x600`) y su reporte (`0x1400`), una línea `<tarea> n=<cuántas> state= substate=`
cuando cambian las tareas de cámara, turno del jugador, menú de comandos, menú de técnicas, caja de mensaje, reporte y
compañero del reporte (se buscan en el heap del modo por su función `update`, como el piloto de batalla). Con
`DW3_STAGE_TIMING=3` la corrida sale sola por la primera ficha de salida del escenario (`SLOT_EXIT`), 30 cuadros después de
`playable`, con las mismas llamadas que la cruz sobre una puerta (`trigger.c:149-151`), y termina en el `playable` del
escenario siguiente: mide el cierre y la entrada sin teclas. Ejemplos: `DW3_STAGE_TIMING=3 ... warp:330` y
`DW3_STAGE_TIMING=1 ... stay,warp:330,fieldbattles:1,battlepilot,until:1400`.

### Caché de archivos por lote (U2 de `modding-ux`)

`updateFileCache` (`src/engine/file/file_cache.c`) avanza un archivo por llamada (en cola, leyendo, cargado, en llamadas
distintas) y el bucle principal la llama una vez por cuadro, aunque el lector del disco del port (`shim/host_cd.c`) completa
cada lectura antes de volver. Con 7 archivos en cola el escenario esperaba 10 cuadros solo por eso. `shim/file_cache_pc.c`
reemplaza el puntero `FILE_CACHE.update` (lo instala `pcCdReaderOpen`) por una envoltura que vuelve a llamar a la función del
juego mientras haya cola (tope `FILE_CACHE_SLOTS + 2` llamadas): la lógica, el orden de lectura y los sellos `lastUsed` son los
del juego; cada archivo pedido durante un cuadro queda cargado al final de ese cuadro. Las llamadas directas
(`waitForFile`, `loadFile`) no cambian. Esa envoltura no toca `src/` ni `include/`; el chequeo de caché llena (abajo, U8c) sí.

- `DW3_FILE_CACHE_STEP=1` conserva el ritmo original (un archivo por llamada), para comparar.
- Traza (canal `stage`: `DW3_STAGE_TIMING=1` o log `trace`; apagada no cuesta nada más que una prueba por llamada):
  `file cache: N of 64 slots in use (new peak)` cada vez que sube el máximo de lugares usados, `file cache: peak N of 64`
  al salir, y `file cache: queue of K files took M updates` (cuadros que el juego esperó) en el ritmo original.
- Prueba: `make -C pc filecachetest` (el `file_cache.c` del juego sobre una tabla de archivos y un lector falsos, sin
  disco: tras una llamada todo lo pedido queda cargado, en el orden de la cola, también con los 64 lugares llenos).
  Con `DW3_FILE_CACHE_STEP=1` falla a propósito (ritmo original).
- Lectura del disco: se midió y no se cambió (`fseek`+`fread` por sector suma 1-12 ms por cambio de escenario con la imagen
  en la caché del sistema y 16-33 ms con la caché fría, de 0.5-1.8 s; una lectura de 146 sectores tarda 0.36 ms). 
- **Caché llena (U8c).** `requestFile` desreferenciaba `findFreeFileSlot()` sin comprobar NULL: con las 64 ranuras ocupadas, un archivo más era un fallo de
  segmento. Bajo `#ifdef DW3_PC` (`file_cache.c`, el PS1 queda igual) ahora expulsa el archivo cargado usado hace más tiempo (la misma regla de `evictOldestFile`) y, si no
  hay ninguno cargado, descarta la petición (quienes sondean, `isFileLoading`/`waitForFile`, vuelven a pedir). En ambos casos avisa por `pcFileCacheFullHook`, que
  `pcFileCacheInstall` apunta a una línea siempre visible del canal `stage` (`file cache full (64 slots): file N evicted ...` / `... none loaded to evict ...`). Se mantienen 64
  ranuras (pico medido 36 de 64 en U2). `filecachetest` cubre las dos ramas y `waitForFile` sobre la caché llena.

### Cartas de mods en las tablas y en la tienda (U8c de `modding-ux`)

- `card_shop_stock`, `card_pack_slots`, `default_deck` y `card_opponents` (`card1..40`, `counter1..20`) aceptan una carta como id original 1-314 o `"<mod>:<nombre>"`: los campos usan
  el mismo gancho `TmField.byName`/`idOk` de los objetos (`cardExtByName`/`cardExtIdOk`, `card_ext.c`) con tope `PC_CARD_LIST_MAX - 1`; 315 y 316 se rechazan. `default_deck` guarda
  el id menos uno y los contraataques terminan en 0, por eso tienen sus envoltorios (`cardNameMinusOne`, `cardIdMinusOneOk`, `cardIdOrZeroOk`). Un nombre desconocido: `no installed mod
  defines "..."`.
- Precio: `CardRec.price` (campo `price` de `card.json`, 0-32767, 1 por defecto) en vez de una fila por carta en `card_prices` (44 filas fijas del disco); `STCRDSHP_getPrice`
  pregunta a `pcCardPrice` bajo `DW3_PC` para ids >= 317.
- La tienda dibujaba solo ids < 315: `CARD_PACK_IDS` era 315 (`card_grid.c:20,78` cortaba el dibujo). En PC es `PC_CARD_LIST_MAX`.
- Álbum: los ids 315-316 siguen como dos huecos vacíos (indistinguibles de cualquier carta sin ver; quitarlos exigía renumerar `grid->first`, el cursor y las páginas en todo
  `album.c`). Sin mods el álbum es idéntico.
