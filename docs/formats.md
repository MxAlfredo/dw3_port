# Inventario de formatos del disco (EU)

Este documento solo tiene nombres de archivo, conteos y
análisis propio; no incluye bytes ni cadenas del juego (ADR-001). Los números salen de
`tools/file_names.py` contra el disco EU del dueño.

Convenciones: "número" es el índice en `FILE_SECTORS` / `FILE_SECTOR_COUNTS`
(`src/engine/file/file_table.c`, bloque `VERSION_EU`, 2382 entradas). "Hecho" está comprobado en
el código o en el disco; "hipótesis" no. Los números en hexadecimal de este documento son los de
EU.

## 1. Número a nombre

El juego direcciona los archivos por número: `FILE_TABLE.getSector(n)` da el LBA donde empieza y
`getSectorCount(n)` su largo en sectores (`file_table.c:1763-1783`). `cdRead`
(`pc/shim/host_cd.c:106`) lee por número. El nombre no existe en el juego.

`tools/file_names.py "<disco>.bin"` recorre la tabla de caminos ISO9660 (el directorio raíz no
lista `AAA/`; `text_data.c:618` hace lo mismo para el texto), lee cada directorio y empareja el
LBA de cada número con el archivo cuyo extent empieza ahí. Imprime un TSV
(`número, lba, sectores, estado, ruta, bytes`) y la cobertura. No escribe nada; la salida sale
del disco del usuario y no se sube.

Cobertura medida:

- 2382 números en la tabla; **2382 emparejan con exactamente un archivo** (0 sin archivo,
  0 ambiguos, 0 sin uso, 0 repetidos).
- El disco tiene 2385 archivos; fuera de la tabla quedan solo `SYSTEM.CNF`, `SLES_039.36` (el
  ejecutable) y `DUMMY.` (relleno al final del disco, 35 MB).
- Para los 2382, `ceil(bytes / 2048)` es igual a `FILE_SECTOR_COUNTS[n]`: los tamaños exactos
  se pueden recuperar del directorio.
- Las 350 entradas de texto son las de `COUNTRY/<idioma>`, 50 por idioma, en el orden
  `JPN, USA, ENG, FRA, ITA, GER, SPN` (número `LANGUAGE + k - 1`, `include/engine/file.h`). La
  tabla se lee con los valores originales: los huecos de es-419 (`ES419_SLOT_*`) se resuelven al
  archivo del disco, no al reemplazo.

Para el port (no se implementa aquí): el mismo emparejamiento se puede calcular al arrancar,
desde el directorio del disco, como `discSizes` hace con el texto; así `data/` y `mods/` de
ADR-005 se nombrarían con la ruta del disco (por ejemplo `FIELD/SPRT/P001PLAY.BIN`) y no con el
número, que cambia entre versiones (US usa otra tabla).

Este mapa también es el que usa el port: `pc/shim/port_fs.c` lo arma al arrancar desde el directorio ISO9660 (misma
regla por LBA) y un mod reemplaza un archivo por su ruta (`mods/<mod>/files/<ruta sin AAA/>`, en minúsculas (ADR-010; se lee sin distinguir mayúsculas), `docs/modding/`).

## 2. Cuadro por familia

| Familia | Archivos | Números | Nombre | Lector (EU) | Conocimiento |
|---|---|---|---|---|---|
| Texto | 350 (7 idiomas x 50) | 1-350 | `COUNTRY/<LANG>/<l>S<nombre>.BIN` | `TEXT_FILE()` `include/engine/file.h:184`; port: `pc/shim/text_data.c` | documentado |
| Overlays | 318 (294 `WSTAGnnn` + 24) | 353-2271 | `PRO/*.PRO` | `loadModeOverlay` `src/engine/system/overlay.c:56`, `loadSubOverlay` `:68` | parcial |
| Stage: celdas (TMPK) | 294 | 407-2379 | `FIELD/STAGE/Snnn TMPK.BIN` | `FIELDSTG_setMapFile` `src/field/field_mode/map.c:46` | parcial |
| Stage: imágenes (MASK) | 294 | 802, 2272... | `FIELD/STAGE/SnnnMASK.BIN` | `FIELDSTG_loadFieldFiles` `src/field/field_mode/file_loader.c:56` | parcial |
| Stage: fondos (BG) | 4 | 1897, 1898, 1909, 2200 | `S415BG01/02`, `S815BG01/02` | `wstag415.c:384`, `wstag815.c:51` | parcial |
| Stage: demos | 2 | 2188, 2189 | `SSUBDEMO`, `SDIGDEMO` | `FIELD_ANIM_FILE` `include/field/field_mode.h:379` | parcial |
| Z_STAGE (SnnnPACK) | 237 | 406-1926 | `Z_STAGE/SnnnPACK.BIN` | `FIELDSTG_runMapStreamer` `src/field/field_mode/map_streamer.c:293` | entendida (ida y vuelta byte a byte) |
| Sprites de campo | 179 + `FIELDCOM` | 390-2208; 352 | `FIELD/SPRT/PnnnXXXX.BIN` | `FIELDSTG_createActor` `src/field/field_mode/actor.c:136` | entendida (4.5) |
| Combate: modelos | 245 | 351-1899 | `FIGHT/MODEL/MnnnXXXX.BIN`, `MNxxxx` | `FighterInfo.model` `include/battle/battle_mode/model.h:225` | formato descifrado y probado con un prototipo (ver 4.6) |
| Combate: efectos | 239 | 1753-2210 | `FIGHT/EFFECT/MEFTnnnn`, `ENxxxx`, `ENCOMn` | `FighterInfo.effects`, `EffectModelEntry` `model.h:166` | desconocido |
| Combate: común | 6 | 1109-2019 | `FIGHT/PGFGTC00`, `PGFGTW00`, `F000COM1-4` | `src/battle/normal_battle/loader.c:23` | parcial |
| Sonido | 142 (71 MP + 71 MV) | 362-2203 | `SOUND/<banco>/MP<banco>.BIN`, `MV<banco>.BIN` | `loadSoundBankInto` `src/engine/sound/sound.c:313`; `SoundFiles` `include/engine/sound.h:83` | documentado, exportado |
| Pantallas | 23 | 708-2381 | `SCREEN/<nombre>CS.BIN`, `...TM.BIN`, `TLOGO*`, `END__ALL`, `TTCOMMPK` | `sprite.draw(getEntry(...))`, `TimLoader` | parcial (mejor entendida) |
| Otras pantallas | 3 + 4 + 2 + 2 + 2 | 646-2215 | `COUNTRY/CMFONT*`, `CNTSELWN`; `NAMEENT/*`; `MCARD/*`; `DGLB/*`; `TRAINING/STTRNG*` | `FILE_MENU_SPRITES`, `FILE_FONT` `file.h:182`; `menus/*` | parcial |
| Cartas | 8 | 605-2042 | `CARD/CARDDATA`, `CARDPACK`, `CARD_NPC`, `CARDPAK0-4` | `FILE_CARDGAME_TIMS` `include/cardgame/card_battle/screen.h`; `FILE_CARD_DATA` `card_packs.h:34` | parcial |
| Entrenamiento | 12 | 606-1087 | `TRAINING/TRANINnn.BIN` | no se encontró lector por número | desconocido |
| Películas | 14 | 2026-2380 | `STR/MOVIE*.STR` | `STDWTITL_movies` `src/menus/title/data/stdwtitl.c:22`; `FILE_TABLE.getPos` `src/menus/title/movie.c:161`; port: `pc/shim/host_st.c` | documentado |
| Otros | 1 | 0 | `DAT/NONE____.BIN` | (relleno, 1 sector) | desconocido |

Total: 2382.

## 3. Contenedores comunes

Hechos, de `src/engine/file/file_cache.c` y `src/engine/gfx/tim_loader.c`:

- **Archivo con tabla de offsets**: la primera palabra de 32 bits es el offset de la entrada 0, que
  cae justo después de la tabla (`getFileEntry((archivo << 16) | i)` devuelve
  `tabla[i] + base`, `file_cache.c:230`; `getArchiveEntry`, `:239`). La tabla tiene
  `tabla[0] / 4` palabras. En los archivos de TIM la última palabra es 0
  (`timLoaderLoadArchive`, `tim_loader.c:66`, termina cuando la entrada es el archivo mismo).
  Los archivos pueden anidarse: una entrada puede ser otro archivo con tabla.
- **RLEN**: una entrada que empieza con la palabra `RLEN` (`RLEN_MAGIC`, `file.h:103`) se
  descomprime con una codificación por repeticiones (n < 0x80 copia n bytes; n | 0x80 repite el
  byte siguiente n veces; 0 termina). Implementaciones: `tim_loader.c:86`,
  `src/engine/file/decompressor.c:15`, `pc/shim/menu_frame.c:236` (`mfRlenUnpack`) y
  `tools/es419/menu_bank.py`.
- **TIM**: formato estándar de PS1 (palabra `0x10`, banderas, bloque de CLUT y bloque de imagen);
  `timLoaderLoad` los sube a VRAM; `pc/shim/menu_frame.c:266` (`mfArchiveImage`) los lee en el
  port.
- **Hoja de sprites** (`SpritePart`, `include/engine/gfx.h:263`): tres offsets al inicio (partes,
  lista de ids de cuadro, primer cuadro), partes con `u, v, w, h, clut, modo`, cuadros con lista
  de partes. Es lo que dibuja `SPRITE_DRAWER.draw(hoja, cuadro, x, y)`.

Clasificación medida en el disco (heurística: tabla de offsets monótona que empieza en
`tabla[0]`; no verificada entrada por entrada):

| Grupo | Resultado |
|---|---|
| `SnnnMASK` (294) | los 294 son archivo cuyas entradas son todas RLEN |
| `SnnnTMPK` (294) | 276 son archivo de archivos; 18 no encajan con la heurística |
| `*CS.BIN` (hojas) | 9 en `SCREEN` + los de `NAMEENT`, `MCARD`, `DGLB`, `TRAINING`: archivo de archivos |
| `*TM.BIN` (imágenes) | idem: archivo de un archivo RLEN-TIM (la entrada 0 es otra tabla) |
| `SPRT/P*` (179) | tabla con entradas de datos y de TIM (151 con datos+TIM, 28 con además subarchivos) |
| `MODEL/M*` (245) | tabla con subarchivos y datos, sin TIM |
| `EFFECT/*` (239) | tabla con subarchivos y datos; 2 con TIM |
| `CARDPAK0-4`, `CARDPACK`, `CARD_NPC`, `TRANINnn` | no encajan con la heurística: formato propio o sin tabla |

## 4. Familias

### 4.1 Texto (350 archivos) — documentado

- Los lee el juego con `TEXT_FILE(<constante>)` (constantes `TEXT_*`, `file.h:189-215`) y los reemplaza
  el port en `cdRead` (`tdOverride`, `pc/shim/text_data.c:1209`).
- Formato: `localization/README.md` y `tools/es419/lang.py` (archivo de texto de un idioma o
  paquete con tabla de offsets); el port los pasa a JSON (ADR-005).

### 4.2 Overlays `.PRO` (318) — parcial

- 294 `WSTAGnnn.PRO` (un escenario de campo cada uno, código MIPS cargado a `SUB_OVERLAY_ADDRESS`
  por `loadSubOverlay`, `field.c:509`) y 24 de modos (`FIELDSTG`, `FIGHTSTG`, `CARDGAME`,
  `STDWTITL`, `STSTATUS`...). Son binarios de código: el contenido está decompilado en `src/`
  del proyecto `dw3_decomp`, no es un formato de datos.
- Los nombres `SMDLDATA.PRO`, `SFSTDATA.PRO`, `SDIGIEDT.PRO` no son código: `SMDLDATA` es el
  "archivo de luchadores" (`FILE_FIGHTERS`, `src/battle/battle_mode/fighter_info.c:27`,
  `FightersFile`, `model.h:217`) con tablas de `FighterInfo`.
- Un overlay ocupa `FILE_SECTOR_COUNTS` sectores y se copia entero (`overlay.c:63`).

> Copia cruda (content-export E7): lo de formato desconocido o sin exportador abierto (`TMPK`, demos, efectos de combate, `F000COM`/`PGFGT`, hojas `CS`, `TRANINnn`,
> `CARD_NPC`, `CARDPACK`, `COUNTRY/JPN|USA`, los `.PRO`) sale tal cual a `data/raw/` y se reemplaza desde `mods/<mod>/files/`; los tipos, lo que se sabe de cada uno y
> si el juego lee el reemplazo están en `data/raw/raw.json` y en `pc/README.md`.

### 4.3 Escenarios de campo: SNTMPK, SNMASK, SNBG — TMPK documentado

Hay 294 `SnnnMASK` y 294 `SnnnTMPK`, uno de cada por overlay `WSTAGnnn`. El overlay fija sus
números en `setupStage` (hecho para `wstag923.c`; el resto no se revisó uno por uno).

- **`SnnnTMPK.BIN`** (entradas por `FIELDSTG_map.setFile(índice, archivo << 16 | n)`, `map.c:46`):
  celdas de la cuadrícula de colisión, zonas de combate y puertas (`FieldMap`,
  `include/field/field_map.h:85-100`: `FIELD_MAP_FLOOR0`, `FIELD_MAP_AREAS`, `FIELD_MAP_TRIGGERS`).
  **Contenedor** (`pc/shim/map_layers.c`): tabla de `u32` con el desplazamiento de cada entrada; el primer valor distinto de 0 es
  también el tamaño de la tabla; un 0 es una entrada ausente (18 archivos, p. ej. S920, empiezan con tres ranuras en 0, y 71 ranuras
  están ausentes en total). La entrada 0 es la hoja de objetos del mapa (opaca aquí); `setFile(i, STAGE_FILE << 16 | n)` apunta a la
  entrada `n` (`wstag800.c:351-353`: piso = 1, disparadores = 2, áreas = 3). 32 entradas del disco no son capas (otra clase de dato,
  p. ej. S202 entrada 1, S415 entradas 2 y 4); el códec las rechaza como `ML_ERR_FORMAT` y el resto (728 capas) se decodifica.
  **Capa** (`map.c:19-97`, `FIELDSTG_getMapCell`): 6 `u32` (rejilla, `cells64`, `cells32`, `cells16`, `cells8`, píxeles) y las partes:
  rejilla = `u8` ancho, `u8` alto en celdas de 128 px y un `u8` por celda; `cells64` = 4 `u8` por nodo; `cells32/16/8` = 4 `s16` por
  nodo; píxeles = 64 `u8` por bloque de 8x8. Cuadrante = `(y & m ? 2 : 0) + (x & m ? 1 : 0)` con `m` = 0x40, 0x20, 0x10, 8.
  Límites que implica el lector: rejilla de 1 a 255 celdas por lado; a lo sumo 256 bloques distintos de 128 px y 256 de 64 px
  (índices `u8`); a lo sumo 32767 de 32 px, 16 px y bloques de píxeles (índices `s16`). La partes se alinean a 4 bytes (rejilla,
  con relleno de ceros) y las entradas del archivo van seguidas.
  **Escritura byte a byte** (`pc/maplayertest.c`, barrido del disco EU): reescribir una capa decodificada con `mlEncode` da los mismos
  bytes en las 728 capas, y el archivo entero reconstruido (`mlArchiveBuild`, con las entradas que no son capas copiadas tal cual)
  da los mismos bytes en 276 de 294 (los 18 restantes empiezan con ranuras ausentes que el constructor no reproduce). El orden de
  los identificadores que lo logra: cada nivel se recorre por filas sobre todo el plano (no en profundidad) y un bloque recibe el
  siguiente identificador la primera vez que aparece su contenido (nivel 0: bloques de 8x8; luego 16, 32, 64 y 128 px, cuadrantes en
  orden arriba-izquierda, arriba-derecha, abajo-izquierda, abajo-derecha).
  **Piso (`FIELD_MAP_FLOOR0/1`, dos pisos; `GAME.mapIndex` elige uno: `SLOT_MAP`, `setFirstMap`)**, por lo que lee el código:
  - `0`: celda cerrada. `FIELDSTG_checkFlightProbe` (`actor_control.c:10-85`) devuelve al actor un paso atrás cuando la celda del piso es 0
    (o no está libre, `isTileFree`), con o sin altura (`actor_control.c:81-84`). Es el valor de mayor frecuencia en el disco.
  - `1` (`FLIGHT_OPEN`, `field_mode.h:111`): abierto y plano. Para quien camina, el nibble bajo 0 y 1 dan la misma pendiente plana
    (`map.c:149-159`: `row = cell & 0xF; row -= row != 0`; fila 0 de `FIELDSTG_dirVectors`, `fieldstg.c:3601`).
  - `2` a `7` (nibble bajo): pendiente al caminar, fila `n - 1` de `FIELDSTG_dirVectors` (la tabla tiene 7 filas, 0 a 6; la 0 es plana y las
    demás tienen otro largo de paso por dirección). Al volar, `2..6` son muros de altura `n * 16` px (`FLIGHT_WALL`, `field_mode.h:105-114`;
    el actor solo pasa si su z lo supera, `actor_control.c:19-60`, `300-330`). El `7` existe en el disco (15 240 celdas) y su efecto en
    vuelo no está en estas funciones: hipótesis, no probado.
  - Bit `0x10` (`16 + n`): la misma pendiente espejada en x (`map.c:153-156`: `FIELDSTG_mirrorDirs`, signo -1). Al volar, `18..22` son
    techos a `FLIGHT_LEVEL(24 - v)` hacia arriba (`FLIGHT_CEILING(n) = 24 - n`). Valores vistos en el disco: 0-7 y 18-22 solamente.
  - Los valores 4, 6, 19 y 22 del escenario 345 son entonces: pendiente de fila 3 y 5, y pendientes espejadas de fila 2 y 5 (muro de
    nivel 4 y 6 y techos de nivel 5 y 2 en vuelo). Es la lectura del código; qué se ve en pantalla en cada celda no se probó en el juego.
  - Hipótesis sin probar: la colisión del suelo para un personaje a pie (z = 0) no sale de estas lecturas más que por el `cell == 0` de
    arriba; no se encontró otra consulta de la capa en `src/field/field_mode/` (solo `getWalkStep`, `getFlyStep` y las sondas de vuelo).
  **Pendientes caminadas (T10, sin ventana):** el valor 3 y su espejo 19 se probaron con el escenario `training-field:lower` (`pc/examples/mods/training-field`).
  La pendiente no cambia ninguna altura del personaje (no hay `z` al caminar): `FIELDSTG_getWalkStep` (`map.c:149-160`) solo cambia el paso, y como el
  mapa es una vista oblicua, el jugador sube por la pantalla. Fila 2 de `FIELDSTG_dirVectors` (valor 3), hacia el este: `(3939, -819) / 4096`, es decir
  0,2079 px hacia arriba por px hacia el este; con el espejo (valor 19) el este sale `(+3940, +818)`: baja. Medido con `fieldwalk` (cada 10 cuadros, posición y valor de la
  celda bajo el jugador, `docs/stages.md` §13): rampa oeste (valor 3) de (297, 264) a (449, 229) = 0,23 en total y 0,208 entre los dos puntos medios (372, 245) y (449, 229); rampa este (valor 19) de (726, 238) a (799, 253) bajando.
  Para que se vea una rampa hay que **dibujar** la banda que tiene el valor (el juego no dibuja nada): una meseta de 32 px se sube con una banda de 154 px
  (32 / 0,2079) y 32 px de alto; los lados de la rampa son muros (valor 0), y el pie del muro de un lado es un triángulo. El valor 0 de un muro frena al jugador que corre
  (`FIELDSTG_checkFlightProbes` en `ACTOR_RUN`, `actor_control.c:1446-1447`: una banda de rampa con muro debajo no se cruza de sur a norte; el arnés se atascó en ese muro).
  No probado: el valor 2 y 4-7 (el mismo mecanismo con otra fila), las capas `FLOOR1`/`SLOT_MAP` (un escenario nuevo solo trae el piso 0: `pcModStageSetup` fija `FIELD_MAP_FLOOR0`) y `SLOT_DEPTH`/`SLOT_CLIMB_*`/`SLOT_DROP` de un escenario de mod (hipótesis: el cargador acepta esos tipos de ranura y el campo pide su animación, `docs/stages.md` §10).
  **Disparadores (`FIELD_MAP_TRIGGERS` = índice 7)**, `FIELDSTG_findTrigger` (`trigger.c:7-60`): valor `v`; `0` = nada; si no, dirección
  = `v >> 5` (3 bits) y ranura = `v & 0x1F` (5 bits, 1 a 31 útiles) en `task->entries[ranura]` (`StageSlot`, `field_map.h:260-275`:
  `SLOT_EXIT`, `SLOT_EVENT`, `SLOT_WARP*`, ...). Los tipos `SLOT_DEPTH`, `SLOT_MAP`, `SLOT_EVENT`, `SLOT_WARP*`, `SLOT_SLIDE`,
  `SLOT_STOP_SLIDE`, `SLOT_LAUNCH*` actúan al pisar; los demás (hasta `SLOT_GAUGE`) piden mirar hacia la celda: `FIELDSTG_nearDirs[dir][dirDelActor]`
  (`trigger.c:57`). Qué dirección es cada `v >> 5` (0 a 7) sigue la tabla de 8 direcciones del juego (`fieldstg.c`, `dirVectors`): su
  mapeo exacto a norte/este... no se verificó (hipótesis).
  **Áreas (`FIELD_MAP_AREAS` = índice 4)**: valor `a`; `0` = sin combate; si no, el área es `a - 1`: `FIELDSTG_state.battles->battles[a - 1]`
  (`FieldBattles`) con `count` combates y ocho filas `AreaBattle`; cada paso resta `FIELDSTG_battleRates[count]` a `GAME.battleSteps` y,
  al llegar a 0, `startAreaBattle` elige `RANDOM.next() & 7` (`battle.c:15-58`). La capa se vacía al entrar al campo (`field.c:511`).
- **`SnnnMASK.BIN`** (`FieldState.imageFile`): archivo de TIM comprimidos con RLEN que
  `FIELDSTG_loadFieldFiles` sube a VRAM en `(FIELD_OBJECTS_X, FIELD_OBJECTS_Y)`
  (`file_loader.c:113-120`): imágenes de los objetos del escenario. Los 294 coinciden con la
  clasificación "archivo de RLEN".
- **`S415BG01/02`, `S815BG01/02`** (4): archivos de imágenes de fondo de los escenarios 415 y 815;
  `loader.loadArchive(FILE_CACHE.load(BG_FILE))` y `BG_FILE + 1` (`wstag415.c:384-387`,
  `wstag815.c:51-54`). Pesan 160-200 KB cada uno.
- **`SSUBDEMO`, `SDIGDEMO`**: animaciones de cinemática en el campo (`CutsceneAnim.kind`,
  `field_mode.h:367`); layout desconocido.

### 4.4 Z_STAGE `SnnnPACK.BIN` (237 archivos, 239 MB) — entendida, ida y vuelta byte a byte

Son los **mosaicos del mapa de fondo**: el campo los lee por sectores desde el CD mientras el
jugador se mueve. Códec portátil: `pc/shim/map_pack.[ch]`; prueba: `make -C pc mappacktest`.

- Lector: `FIELDSTG_createMapStreamer(FIELDSTG_state.mapFile)` (`src/field/field_mode/field.c:546`);
  el escenario fija `mapFile` (por ejemplo `wstag923.c`: `0x19A` = `S210PACK.BIN`). Los
  escenarios que no lo declaran explícito usan `STAGE_FILE - 1` (`wstag420.c:59`): el PACK está
  justo antes del TMPK en 228 de los 237 casos. 294 escenarios comparten 237 PACK.
- **Contenedor** (`src/field/field_mode/map_streamer.c:306-328`, `include/field/field_mode.h:462-469`):
  - Sector 0, `MapFileHeader`: `s32 tileCount` (nada lo lee; es la cuenta de `tileSizes` distintos de 0),
    `s32 width`, `s32 height` (en mosaicos de 128 px), `s32 tileBytes` (el espacio de un mosaico en el CD,
    múltiplo de 2048) y `u16 tileSizes[width * height]` (bytes de cada mosaico, 0 si no hay); el resto del
    sector es 0.
  - Sectores 1...: un mosaico cada `tileBytes / 2048` sectores, en orden de fila: `sector = frame * frameSectors + 1`
    (`FIELDSTG_seekStream`, `stream.c:17`). El archivo mide exactamente `1 + width * height * tileBytes / 2048` sectores
    (los 237). El espacio de un mosaico vacío es 0.
  - **Cada tamaño es un número entero de sectores**: el lector pide al CD `tileSizes >> 11` sectores
    (`map_streamer.c:322`, `FIELDSTG_requestTiles`, `:50-53`). En los 11 745 mosaicos, `tileSizes` = el largo del bloque
    redondeado hacia arriba al sector y el resto del último sector es 0.
- **Mosaico**: un bloque RLEN (`"RLEN"`, `u32` tamaño sin comprimir, pasadas, 0) **o el mosaico sin comprimir**:
  `decompressorSetData` (`src/engine/file/decompressor.c:13-25`) mira la primera palabra; si no es `RLEN_MAGIC`
  entrega el bloque tal cual (`:101-105`). El disco usa los dos: 7342 con RLEN y 4403 sin comprimir. Una pasada con el bit 7
  repite el byte siguiente `n & 0x7F` veces; si no, copia `n` bytes; 0 termina (`:48-69`). Cuál de los dos usa el disco no
  depende del tamaño (en 46 de 329 sin comprimir de una muestra, RLEN ahorraría un sector); el lector acepta ambos.
- **Mosaico sin comprimir** (`FIELDSTG_loadStreamSprites`, `stream.c:83-118`; `src/` y la copia `pc/field/stream.c:100-140`):
  - `u32` desplazamiento del TIM (`FILE_CACHE.getArchiveEntry(0, data)`, `file_cache.c:262`: la tabla de un archivo de una
    entrada es esa palabra).
  - Tres listas, una por profundidad: `u32` cuenta y `cuenta` registros de seis `s16`: `x, y, u, v, w, h`. Las listas terminan
    justo en el desplazamiento del TIM (11 745 de 11 745).
  - En el desplazamiento, un TIM de 8 bits con CLUT: `0x10`, bandera 9, bloque CLUT (`u32` 524, `x y w h` = `0 0 256 1`, 256 colores
    de 16 bits), bloque de imagen (`u32` 12 + 2 * `w` * `h`, `x y` = 0 0, `w` en medias palabras, `h` filas, los píxeles
    por filas con el de la izquierda en el byte bajo). Las imágenes miden hasta 128 medias palabras (256 px) por 128 filas.
- **Qué hace el juego con él**: el TIM sube a una de las 12 posiciones de VRAM (`FIELDSTG_slotImages`, `fieldstg.c:1505`: x = 640, 768,
  896, y = 0, 128, 256, 384; cada una de 128 medias palabras por 128 filas, de ahí el tope de la imagen) y la CLUT en `(0, 0xF0 + ranura)`
  con el ancho y alto de la CLUT del TIM (`stream.c:111-115`, `tim_loader.c:27-60`; el `x, y` del propio TIM se ignora). Cada
  registro es un sprite (`SPRT`) en `(x, y)` relativo al origen del mosaico, con textura desde `(u, v)` de la imagen (`u, v` en píxeles
  de 8 bits; `v` se suma al `y` de la ranura), tamaño `w x h`, paleta de la ranura (`FIELDSTG_drawStream`, `stream.c:29-71`). Los
  registros pueden salir del mosaico (1020 de 17 901 en el disco) y `u + w` cabe siempre en la imagen. Las tres listas se dibujan en las
  profundidades 7, 3 y 11 de la capa del mapa (`FIELDSTG_spriteDepths`, `fieldstg.c:1502`); la 3.ª se tiñe siempre con `spriteColor`,
  la 1.ª y la 2.ª solo si `spriteColor.cd` no es 0 y si no con el gris neutro 0x80 (`stream.c:45-52`). Hipótesis (no leída): la 1.ª lista es el fondo y las otras son capas que quedan delante o detrás de los actores
  según su orden de dibujo; el disco usa (1,0,0) en 7234 mosaicos, (1,1,0) en 2073 y (1,0,1) en 822.
- **Límites del lector** (los que respeta el códec; `MP_MAX_*` en `map_pack.h`):
  - **255 mosaicos**: `viewTiles` es `u8` con 0xFF = vacío (`map_streamer.c:275,283`), así que `frame = x + y * width` debe ser 0..254.
    El mapa mayor del disco tiene 168. (El `u8` de la cuadrícula del TMPK, 255 celdas por lado, es otro límite.)
  - `tileSizes` es `u16`: un mosaico mide a lo más 31 sectores (63 488 bytes); `tileBytes` es además el búfer de cada una de las 30 tareas
    (`:326-328`). El disco usa 14 a 22 KB.
  - 5 sprites por lista (`StreamTask.sprites[3][5]`, `field_mode.h:440`): el lector copia las que diga la cuenta sin comprobar. 8 mosaicos del disco
    declaran más; el codificador las rechaza.
  - La imagen mide a lo más 256 x 128 px y tiene ancho par.
- Verificación en el disco (los 237 y los 11 745 mosaicos, `mappacktest`): la cabecera se lee en los 237; reconstruir el contenedor
  desde los bloques en bruto da el archivo idéntico en los 237; los 11 745 se decodifican; 11 737 se vuelven a codificar y decodifican igual
  (los otros 8 tienen listas de más de 5 sprites); 11 704 dan los mismos bytes (7301 con RLEN, 4403 sin comprimir). Los 33 restantes con RLEN
  descomprimen igual: el empaquetador del disco termina con una pasada de 1 byte (`0x81 b`) donde el nuestro escribe un literal.
  Ejemplo: `S210PACK`: 34 mosaicos con datos, 7x5 mosaicos de 128 px = 896x640 px.
- Reutilización: el reemplazo de mosaicos de los mods (`stage_mod.c`, `imgSubstitute`) solo cambia el TIM del mismo tamaño y deja las listas;
  no codifica mosaicos. `stage_export.c` lee el mismo formato pero depende de SDL y del registro; `map_pack.c` es el códec portátil con
  la parte de escribir, y puede reemplazar el lector de `stage_export.c`.
- Desconocido (hipótesis): el efecto exacto de cada profundidad sobre los actores y por qué el disco deja unos mosaicos sin comprimir.

### 4.5 Sprites de campo (179 + FIELDCOM) — entendida

- `FIELD/SPRT/PnnnXXXX.BIN`: un personaje o NPC cada uno (`P001PLAY` es el protagonista: el personaje 1 de
  `FIELDSTG_fileEntries`, `src/field/field_mode/data/fieldstg.c`). `FIELDSTG_createActor` (`actor.c:136-142`) pide
  `getFileEntry(personaje)` = `archivo << 16 | entrada del conjunto de animaciones`.
- Archivo = tabla de desplazamientos (la primera palabra es su tamaño; 203 entradas en P001). Cada entrada es un cuadro,
  una animación o, la **última**, la tabla de conjuntos (los 179 archivos lo cumplen).
  - Cuadro: un TIM de 4 bits con su CLUT de 16x1 (4812 de 4812 cuadros), sin RLEN.
  - Animación: pasos de cuatro `s32` (`tiempo, entrada del cuadro, x, y`); una palabra 0 la cierra repitiendo desde el
    principio, `-1` la cierra una vez (`animDone`). `FIELDSTG_animateActor`, `actor_control.c:867`.
  - Tabla de conjuntos: registros de seis `s16` (id del conjunto y la entrada de la animación de cada una de las 5
    ranuras de dirección); id 0 la cierra. Los ids que llevan nombre son `ACTOR_ANIM_*` (`field_mode.h:80`).
- Direcciones: `actor->dir` 0 a 7 (`FIELDSTG_dirSteps`: 0 abajo, 1 abajo-izquierda, 2 izquierda, 3 arriba-izquierda,
  4 arriba, 5 a 7 arriba-derecha, derecha, abajo-derecha). 0 a 4 usan su ranura; 5 a 7 usan `8 - dir` y
  `FIELDSTG_drawActor` (`actor_control.c:759`) los dibuja en espejo (UV invertidos, `x = -(ancho + x)`).
- Imagen a VRAM: por cuadro y bajo demanda, con `TimLoader.load(getEntry(cuadro))` (`timLoaderLoad`), en la posición de
  `ActorImage` (`field_map.h:114`); el ancho del cuadro sale del TIM (`loader.w * 4`). Pasa por `timLoaderLoad`, así que
  el gancho de `shim/tim_loader_pc.c` (sustitución por PNG) aplica.
- Verificado con el disco EU: los 179 archivos se leen completos (4686 cuadros usados + 126 sin uso, 1276 animaciones,
  425 conjuntos). Extracción y formato `sprite-v1`: `pc/README.md`.
- `FIELD/FIELDCOM.BIN` (352 = `FIELD_SPRITES_FILE`): cuatro entradas; 0 y 1 son hojas de sprites (`SpritePart`: los
  objetos del mapa, el icono, los globos, el indicador), 2 y 3 son archivos con una imagen RLEN (página de 256x256 píxeles de 4 bits) cada uno que
  `file_loader.c:73-76` sube con `loadArchive` a `FIELD_SPRITES_X` y `FIELD_SPRITES2_X`. La hoja 0 describe la imagen 2 y
  la 1 la 3 (`effect.c:59-62`).

### 4.6 Combate: modelos — entendido y comprobado con un prototipo (M1); efectos y cara, parcial

Estado (M2 y M3 hechas: `tools/model_export.py` exporta cada luchador a glTF con esqueleto, malla, atlas y las animaciones de sus
ranuras de movimiento; ver `docs/modding/README.md` §8.1. Los 248 se exportan sin error. Pendiente: la revisión del dueño en Blender (M4)
y la investigación del importador).

Exportador en el juego (model-import I7): `pc/shim/model_export.c` es el mismo exportador en C portátil (`tools/model_export.py --source` es su referencia) y escribe
`data/models/<id>/model.gltf` + `.bin` + `.png` + `model.json` (`model-src-v1`) a **escala de Blender**: toda longitud (vértices, traslaciones de hueso, traslación de las
matrices inversas) se multiplica por 1/1000 (1 m = 1000 unidades del juego) y `model.json` dice `"scale": 1000`, que el importador y el horneador ya entienden (`scale`
multiplica las longitudes del glTF al convertir; la geometría vuelve igual salvo el redondeo). Las rotaciones y escalas de hueso no cambian. Los 248 salen byte a byte
iguales a los de la herramienta de Python (`.gltf`, `.bin`, `.json`; el PNG con los mismos píxeles). Las sumas de flotantes se hacen de izquierda a derecha y el C
se compila con `-ffp-contract=off` (el `sum()` de Python 3.12 o superior compensa y daría otros bits). El `.json` de glTF imprime los dobles como Python (`repr`).

Estado: el formato de la malla, las texturas, el esqueleto, las claves y los movimientos
se descifró del código y de los bytes del disco EU y se comprobó con un prototipo que lee un luchador del disco y vuelca
la malla en pose (OBJ + PNG). Resultado: **los 248 luchadores** (compañeros y enemigos) se leen sin error, y las dos
mallas probadas (Kotemon, id 383, y el enemigo id 25) se reconocen en la imagen; la de Kotemon coincide con una captura del
juego (casco azul de kendo con ojos amarillos y espada de bambú). Los números de abajo salen del código (`archivo:línea`) y
de medir el disco; nada de esto trae bytes del juego.

**Dónde está cada cosa.** `FighterInfo.model`, `.motions` y `.effects` (`model.h:225-228`) son `(archivo << 16) | entrada`.
El **archivo** (los 16 bits altos, por ejemplo uno de `FIGHT/MODEL/M*`) es una tabla de offsets (§3) con todas las piezas de
un luchador; los tres campos apuntan al mismo archivo con otra entrada (medido: los 248 luchadores traen un
archivo propio, cada uno de unas 50 entradas: 55 compañeros + 193 enemigos = 248 archivos de modelo distintos, también por contenido;
la cifra de 139 que traía `docs/digimon-anatomy.md` era errónea y ya se corrigió). Todo lo que el modelo nombra (textura, mallas, claves) es un
número de entrada **del mismo archivo**: `FIGHTSTG_createModel` le copia la mitad alta del archivo a cada índice
(`model_mesh.c:452-495`). `getFileEntry`/`getArchiveEntry` (`file_cache.c:230,239`) devuelven `tabla[i] + base`; una
entrada con offset 0 es el archivo mismo (sin uso).

#### 1. Entrada del modelo y huesos — entendido

- Entrada del modelo (palabras de 32 bits, `model_mesh.c:452-495`): `[0]` entrada de la textura (0 = sin textura), `[1]`
  número de huesos menos 1, `[2..]` una tripleta por hueso 1..N-1: `(padre, entrada de la malla, entrada de las claves)`. Hay
  N huesos con el hueso 0 como raíz sin malla ni claves (posición y rotación del control, escala 4096). Kotemon: 24 huesos;
  enemigo 25: 15; máximo medido: 45.
- `padre` es un índice de hueso; en los 248 modelos siempre es menor que el del hijo, de modo que un recorrido en orden
  de índice basta para acumular matrices.
- Cada hueso tiene una malla (`FIGHTSTG_createMesh`, `:1061`) y un archivo de claves; no hay árbol de mallas
  compartidas. Un mismo hueso puede quedar escondido por la escala (ver 4).

#### 2. Malla — entendido

Entrada de malla = archivo anidado (`createMesh`, `model_mesh.c:1061-1075`), con entradas: `0` vértices, `1` normales, `2`
comandos, `5` límites (las `3` y `4` valen 0, no se usan).

- **Vértices** (`Mesh.vertices`, `model.h:21-38`): `s16 x3` (6 bytes, sin relleno). El primer elemento trae la cuenta
  en `x`; los vértices siguen. Unidades del modelo (las mismas de las posiciones de los huesos). Máximo medido: 216 vértices por hueso.
- **Normales:** igual (`s16 x3`, cuenta en `[0].x`); unitarias en 4096 = 1.0. Las usa `FIGHTSTG_lightMesh` (`:507`) para
  colorear por luz (`gte_ncs`), una por índice de normal.
- **Límites** (`bounds`): cuenta 9 en el primer elemento, luego el centro y las 8 esquinas de la caja de los vértices.
  Solo sirven para descartar mallas fuera de pantalla; el exportador no los necesita. (`FIGHTSTG_isMeshOnScreen`, `:648`,
  los lee desde el primer elemento, o sea, incluye la cuenta como punto y deja fuera la última esquina; una rareza del
  juego sin efecto práctico.)
- **Flujo de comandos** (`FIGHTSTG_drawMesh`, `:815-897`, y `MeshDrawState`, `model.h:40-78`), bytes hasta `0xFF`:
  - nibble alto 8..14 = bandera con el valor del nibble bajo: 8 `quad` (0 triángulos, 1 cuadriláteros), 9 `textured`, 12
    `lit` (con normales), 13 `gouraud`, 14 `abr` (modo de semitransparencia + 1, 0 = opaco). Las 10 y 11 las guarda el
    código y nada las lee (el 11 vale 1 en los polígonos con textura que no tienen luz).
  - nibble alto 0: bajo 1 = **textura**, 7 bytes `01 u lo, u hi(página), v, clutX, clutY, modo`; bajo 2..5 = color plano
    (4 bytes: opcode, R, G, B) para el slot 0..3; bajo 0 = **corrida de polígonos**.
  - Corrida: cada polígono es `00` + `k` índices de vértice + (si `lit`) `k` índices de normal + (si `textured`) `k` pares
    `(u, v)` de un byte, con `k = 3` o `4` (`quad`). La corrida sigue mientras el siguiente byte sea `00`. Los cuadriláteros
    son los vértices 0,1,2,3 de un `POLY_FT4/GT4` de PS1: se parten en los triángulos (0,1,2) y (2,1,3).
  - El UV efectivo es `(u_polígono + u_base) & 0xFF`, `(v_polígono + v_base)` con `u_base = byte 1`, `v_base = byte 3`
    del comando de textura (`:695-709`, `:742-748`); el byte 2 (página) no entra en el UV sino en el tpage.
  - Medido en los 248 modelos (148 489 polígonos): 27 sin textura (todos con luz); 4 280 con textura y sin luz (estos usan
    el color plano de los comandos 2..5 como modulación); `gouraud` en 133 189; `abr` 1 en 300 y `abr` 2 en 2 166
    polígonos (semitransparencia, ver «incierto»); ningún índice de vértice ni de normal sale de rango.
- **Sentido de las caras (corregido en M2b):** el juego descarta cada polígono cuyo `NCLIP` sobre los tres primeros puntos de
  pantalla es `<= 0` (`drawMeshPolygons`, `model_mesh.c:744-757`; no hay bandera de doble cara en el formato ni en el renderer PC, que
  dibuja la lista que arma ese código), o sea que **el juego es de una sola cara** y deja los que `NCLIP > 0`. Con Y abajo y Z hacia
  adentro (mano derecha), esos son los de producto vectorial `(v1-v0)x(v2-v0)` apuntando **hacia adentro** del modelo. Medido: en 143 845
  de 144 209 polígonos con normales de los 248 modelos el producto vectorial (en reposo) apunta contra la normal de iluminación (364 al revés).
  glTF quiere caras antihorarias vistas desde fuera, así que el exportador **invierte** el orden: `(0,2,1)` y, en cuadriláteros,
  `(0,2,1)` + `(2,3,1)`; con `doubleSided: false`. La primera versión de M2 los emitía sin invertir: un visor con descarte de caras
  mostraba el interior (reporte del dueño, M4: parecía una malla traslúcida por delante y el casco vacío por detrás).

#### 3. Texturas — entendido

- La entrada `[0]` del modelo es un archivo de TIM (`timLoaderLoadArchive`, `tim_loader.c:66-110`): cada TIM puede ir
  comprimido con RLEN (§3). **En los 248 modelos hay exactamente un TIM, de modo 2 (15 bpp), 64x256 halfwords.** No es una
  imagen a color: son los **píxeles de 4 bits empaquetados** de una página de 256x256 más, en las filas altas, las
  **CLUT** (16 halfwords cada una). Los TIM de este archivo no traen CLUT propia: el cargador no la coloca (`setClutPos` no
  se llama en `FIGHTSTG_updateModel`, `:296-312`).
- `FIGHTSTG_createModel` recibe `texPos` (`FIGHTSTG_fighterTexPos`, `models.c:58-61`: (832,0), (896,0), (960,0) y (960,256)
  para las cuatro ranuras de modelo) y el cargador pone la imagen en `texPos`, 64 halfwords por TIM. Los comandos de
  textura (`setMeshTexture`, `model_mesh.c:695-709`) usan `texPos` como base:
  - CLUT en (`texPos.x + clutX`, `texPos.y + clutY`): `clutX` es múltiplo de 16 (0, 16, 32, 48) y `clutY` de 240..255; es
    decir, dentro de la propia imagen cargada, en la fila de la CLUT.
  - tpage: modo = byte 6 (siempre 0 = 4 bits en los 248 modelos), `x = texPos.x + página * 64`, `y = texPos.y`; la
    página (byte 2) siempre vale 0 en los 248 modelos (con un solo TIM no hay otra).
- Para producir un PNG: para cada par `(página, modo, clutX, clutY)` distinto de la malla se arma una página de 256x256
  leyendo el píxel `(u, v)` como el nibble `u % 4` del halfword `v, u/4` y buscando su color en la CLUT (16 halfwords en
  esa fila). Color 15 bpp BGR555: `r = c & 31`, `g = (c >> 5) & 31`, `b = c >> 10`; el valor 0 es transparente. Kotemon usa
  6 combinaciones (4 CLUT distintas), el 25 también 6. Para glTF conviene empaquetar las variantes en un atlas.
- La cara (ojos, parpadeo) se dibuja aparte (`FIGHTSTG_createFace`, `face.c`): `FaceRect` copia rectángulos de la propia
  textura. No es necesaria para la malla estática; sin explorar.

#### 4. Esqueleto y claves — entendido

- Archivo de claves (la tercera palabra de cada hueso): un archivo anidado de tres entradas **posición, rotación, escala**,
  cada una una lista de claves `s16 x, y, z, cuadro` (8 bytes; `FIGHTSTG_poseBone`, `model_mesh.c:83-135`). Los cuadros
  van en orden creciente y la última clave de cada lista es el cuadro 30000 (centinela, medido en los 248 modelos). Máximo
  medido: 192 claves por lista. Los cuadros forman **una línea de tiempo común al modelo**, no una por movimiento.
- Muestreo (`poseBone`): si hay clave exacta en el cuadro se toma; si no, interpolación **lineal** entre las dos
  vecinas, `t = ((cuadro - c0) << 12) / (c1 - c0)`, para posición, rotación y escala por igual (los ángulos se
  interpolan en bruto: sin camino corto).
- Unidades: posición en unidades del modelo (igual que los vértices); rotación `s16` con **4096 = 360 grados**
  (`RotMatrixZYX_gte`, `pc/shim/host_gte.c:97-122`: la matriz es `Rz * Ry * Rx`); escala en 4096 = 1.0
  por eje (`ScaleMatrix` escala las **columnas**, `pc/gtetest.c:59`: `M = R * diag(escala)`).
- Matriz de un hueso (`:296-408`): `local = R * diag(S)` con traslación = posición; `mundo = padre.R * local.R`, `mundo.t =
  padre.R * local.t + padre.t`, con la raíz = identidad (las escalas de un padre se heredan). Un vértice va a
  `mundo.R * v + mundo.t`. Un hueso con `(sx | sy | sz) <= 48` no se dibuja (`visible = 0`, `:380-384`): es como el juego
  esconde piezas (armas, extremidades) según el movimiento.
- Ejes del juego: PS1 con **Y hacia abajo** y el modelo mirando a **+Z**. Para glTF (Y arriba, mirando a +Z): `(x, y, z) ->
  (-x, -y, z)` (rotación propia, no cambia el sentido de las caras).
- Pose de reposo: **el cuadro 0 no es una pose de reposo limpia**: deja visibles piezas que el movimiento normal esconde
  (Kotemon aparece con tres espadas y las piernas separadas). La pose a exportar como estática es el primer cuadro del
  movimiento de reposo (cuadro 10 en 199 de los 248, 1 en 42, 20 en 6, 820 en 1).
- Tope `Model.keyframes[0x640]` (1600 posiciones de claves de un movimiento, `model.h:150`): el máximo medido en un
  movimiento es 343, o sea, sobra.

#### 5. Movimientos — entendido, falta verificar la mezcla en el juego

- `FighterInfo.motions` apunta a un archivo anidado de **62 ranuras** en los 248 luchadores (algunas vacías, offset 0);
  `Model.motion` es `ranura + 1` y `FIGHTSTG_setMotion` (`model_mesh.c:185-240`) lee `getArchiveEntry(motion - 1)`. La
  ranura 0 es el reposo principal y la 1 el segundo reposo (`idleFrames`, `:340-346`).
- Una ranura es una lista de `MotionStep` de 8 bytes `s16 index, count, frame, endFrame` (`model.h:118-124`), terminada
  en `index = 0x7FFF`:
  - `endFrame != 0` (**reproducción**): `count` cuadros consecutivos del modelo, `frame, frame+1, ...`.
  - `endFrame == 0` (**mezcla**): `count` pasos que mezclan la pose de la clave anterior (`step[-1].endFrame`, o la pose
    guardada) hacia `step[1].frame`, con peso `((i+1) << 12) / (n+1)`; si `step[1].frame == -1` se mezcla hacia el cuadro
    del reposo y el movimiento vuelve solo a él.
  - Último paso (`count == 0`): `frame = 0x8000` repite desde el cuadro de `endFrame` (se usa `endFrame` = 0: bucle
    desde el inicio del movimiento); `frame = 0xFFFF` termina el movimiento. Medido: 1876 bucles y 2310 finales en las
    ranuras.
- Tiempo: `keyframe += FIGHTSTG_battle.frames` por actualización (`:267`), y `frames` es el tiempo desde la última
  actualización medido en vsyncs (`speed.c:14`); es decir, un cuadro de clave por vsync, en el modo de velocidad normal.
  Un reposo típico dura unos 60 cuadros (Kotemon: 61, del 10 al 70).

#### 6. Escribir el formato — comprobado con una ida y vuelta

Un escritor de prototipo (fuera del repo) reconstruyó el archivo de Kotemon y el juego lo cargó como `files/dat/fight/model/m383pken.bin`: capturas de
batalla idénticas byte a byte a las del juego sin mod. Lo que hay que respetar:
- **Archivo/archivos anidados:** tabla de offsets `u32` (la primera palabra es el tamaño de la tabla); las entradas van alineadas a 8. Un offset 0 es «el
  archivo mismo» (fin de la tabla de TIM).
- **Malla** (6 palabras de cabecera: `[0]` vértices, `[1]` normales, `[2]` comandos, `[3]` y `[4]` 0, `[5]` límites): el orden en
  el disco es cabecera (24 bytes), vértices, límites, normales, comandos. Los **límites** son `9` + centro (mitad de la caja de los vértices, división entera) + 8 esquinas
  en el orden (+x,+y,+z), (-x,+y,+z), (+x,-y,+z), (-x,-y,+z) y las mismas con z mínimo. Los **comandos** se escriben con las banderas en el orden
  `13 (gouraud), 8 (quad), 9 (textura), 12 (luz), 14 (abr), 10, 11`, solo las que cambian, el comando de textura (`01` + 6 bytes) cuando cambia, y cada
  polígono como `00` + índices (+ normales) (+ UV); fin `0xFF`. Reproduce los 23 flujos de Kotemon byte a byte.
- **Claves:** cabecera de 4 palabras `[pos, rot, escala, fin]` (desfases desde el inicio del archivo de claves) y luego las tres listas de `s16 x,y,z,cuadro`; los originales
  traen 8 bytes de cola sin uso detrás de la palabra de fin.
- **Textura:** archivo de TIM `[8, 0]` + TIM de modo 2 de 64x256 halfwords (píxeles de 4 bits empaquetados + CLUT en las filas altas), comprimido con RLEN (`"RLEN"`, `u32` con el
  tamaño sin comprimir, y luego bytes: `0x80|n, v` repite `n` veces `v`; `n, n bytes` copia; `0` termina); el cargador (`tim_loader.c:66-110`) acepta el TIM sin comprimir.
- **Entrada del modelo:** `[textura, huesos - 1, (padre, malla, claves) por hueso 1..]`, con números de entrada del mismo archivo.
- **Tamaño del archivo:** el port anuncia el tamaño del reemplazo al caché de archivos; un archivo de 80 000 bytes (el original: 53 248) funciona.
- **Límite medido:** con 1692 polígonos (x3 los de Kotemon) la batalla cae (SIGSEGV por corrupción de heap, probablemente el búfer de primitivas `0x19000`); con 1128 corre.
  Los índices de vértice y de normal son de 1 byte (máx. 256 por hueso).

#### Qué falta o es incierto

- **Efectos** (`FighterInfo.effects`, `EffectModelEntry`, `model.h:166`): un archivo de bytecode de animación de técnicas
  (ver `docs/digimon-anatomy.md`); sus modelos propios (`FIGHTSTG_startEffectModel`) usan el mismo formato de modelo y malla,
  pero no se probaron.
- **Cara**: copias de rectángulos de textura; no se interpretó la tabla `FaceRect` por luchador.
- **Iluminación**: el color de cada vértice sale de las normales y de las luces de la escena (`gte_ncs`); el modelo no
  guarda colores de vértice. El color plano de los comandos 2..5 solo se usa en los polígonos sin luz. Un exportador debe
  emitir las normales y dejar la luz al visor.
- **Semitransparencia** (`abr` 1 y 2; confirmada en M2b): 2 466 polígonos en los 248 modelos, **todos con textura** (ninguno plano). El tpage lleva
  `abr - 1` (modo 0 = 0.5B+0.5F, modo 1 = B+F, `setMeshTexture`, `:695-709`) y el polígono sale con `setSemiTrans`. En la GPU de PS1 la
  mezcla es **por texel**: en un polígono semitransparente solo se mezclan los texels con el bit STP (bit 15 del color de 15 bpp) puesto;
  los de STP 0 se dibujan opacos; el color `0x0000` no se dibuja nunca; en un polígono opaco el STP no cuenta. De los 2 466, 2 445 tienen texels STP
  en su caja de UV (285 de modo 0, 2 160 de modo 1) y 21 no (quedan opacos). Kotemon (383) no usa `abr` (564 polígonos, todos opacos
  y sin STP): lo que el dueño vio ahí no era semitransparencia sino el sentido de las caras. El exportador codifica el alfa en el
  atlas por texel (color 0: alfa 0; STP en polígono semitransparente: alfa 0.5; el resto 255), con una página de atlas por (textura/CLUT, alfa);
  materiales: opaco con textura `MASK` (corte 0.5), con STP `BLEND`. Aproximación: glTF no tiene suma ni resta, el modo 1 (B+F) sale
  también como alfa 0.5 (más oscuro que el original) y el modo 3 (B+0.25F) como 0.25.
- **Pose de reposo / límites de escala:** esconder piezas con escala <= 48 vale para el reposo; falta verificar un par de
  movimientos de ataque contra capturas del juego. Frame 0 sin significado confirmado. En el glTF una pieza escondida lleva
  escala 0 (y las matrices inversas de enlace usan 1 para ella, para poder invertirse).
- **Interpolación de rotación** en bruto (sin cuaterniones): el exportador (M3) **muestrea a cuadros enteros** (60 por segundo),
  arma `Rz*Ry*Rx` con los ángulos ya interpolados como el juego, lo pasa a cuaternión y mantiene la continuidad de signo; el visor
  interpola entre cuadros vecinos. Aproximaciones: las mezclas desde «la pose guardada» (primer paso de una ranura, sin cuadro de
  origen) parten de la pose de reposo; el movimiento se emite entero (la primera muestra es el índice 0, mientras que el juego
  arranca en el 1); el bucle y el fin del movimiento (`loopStartFrame`, `ends`) solo se anotan en `extras`.
- **Formato de las entradas del archivo no nombradas** (por ejemplo, las entradas altas del archivo del modelo que no son
  del modelo, de las claves ni de la textura): sin investigar.
- Un **importador** (modelos propios) necesita además el orden de los comandos que las herramientas normales no generan:
  el escritor debe respetar `0xFF`, los 7 bytes del comando de textura, y el tope de la GTE (los vértices se proyectan
  con `gte_rtps` en 16 bits).
- `PGFGTC00`, `PGFGTW00`: menú de combate y su imagen (`FILE_BATTLE_MENU`, `FILE_BATTLE_IMAGES`,
  `file.h:228-233`). `F000COM1-4`: archivos de imágenes para VRAM (`FILE_BATTLE_IMAGES_1..4`),
  archivos de RLEN (4 de 4).

#### Piezas del port para modelos propios (model-import I3)

Tres piezas de `pc/shim` (sin tocar `src/`) que un modelo nuevo necesita; el formato del archivo no cambia.
- **Archivos virtuales** (`pfsAddVirtualFile`, `port_fs.h`): archivos que no están en el disco, con número **desde `FILE_COUNT` (2382 en EU)
  hasta 65 535** (una referencia es `(archivo << 16) | entrada`, así que el número es un `u16`: caben 63 154). Los sirve la capa generada
  desde una copia de los bytes y llegan al juego por número como cualquier archivo: `host_cd.c` envuelve `exists`, `getSector`,
  `getSectorCount` y `getPos` de `FILE_TABLE` (sus tablas solo tienen `FILE_COUNT` entradas) y el caché de archivos, `getFileEntry` y las
  referencias de `FighterInfo` los usan sin cambios. Numeración determinista: el k-ésimo archivo agregado desde la última limpieza
  (`pfsClearVirtualFiles`, al recargar mods) es `base + k`, así que el mismo orden de carga da los mismos números; el número nunca se escribe
  en un mod ni en un guardado (los mods nombran, el registro asigna). La capa generada dejó de tener tope de 8 archivos.
- **Fila propia de `FighterInfo`** (`digimon.json`, `"model"` como objeto, ver `docs/modding/README.md` §6.9): el Digimon nuevo deja de compartir
  la fila del modelo de otro. El archivo de luchadores sirve entonces una copia más larga de la tabla de compañeros (0xC4 por fila) y/o de
  enemigos (0x48 por fila): las filas usadas por alguna entrada, más las nuevas, y la cabecera apunta a la copia (los bytes originales no
  se mueven, así que las caras y los desplazamientos del archivo siguen valiendo). La fila nueva sale de la de un luchador **plantilla** del
  mismo tipo (cámaras, altura, distancia y lo que no se reemplace) con `model`, `motions` y `effects` cambiados (cualquier
  `(archivo << 16) | entrada`, también de un archivo virtual) y la **lista de caras vacía** (un `FaceRect` con `x = 0xFF`, compartido; sin
  partes la tarea de cara se mata sola, `face.c:48`, y el modelo no parpadea). Índices: `FighterEntry.index` es un `u8`, así que cada tabla
  admite **256 filas**: en el disco los compañeros usan 55 y los enemigos 193, quedan **201 filas propias de compañero y 63 de enemigo**;
  pasado el tope el Digimon vuelve a compartir la fila de su plantilla, con una línea en el log. El tipo (`kind`) es el de la plantilla (los
  compañeros `< 0x3A`, los enemigos `>= 0x3A`), no se agrega ninguno.
- **Búfer de primitivas del PC** (`gfx_engine.c`): el juego pide 0x19000 bytes (~100 KB) en batalla y el dibujante de mallas escribe sin mirar
  el final; con un Kotemon de 1692 polígonos (x3) el heap se corrompía (SIGSEGV). El port reserva **8 MiB por búfer** en todos los modos
  (las páginas que nadie escribe no se confirman: la RSS no cambió en la batalla base) y vigila el final: pasada la parte útil (8 MiB
  menos 1 MiB de holgura) el resto de las primitivas del cuadro va a un bloque de descarte, el cuadro no se dibuja y una línea del log lo
  dice (el siguiente cuadro empieza limpio). Un solo lote de más de 1 MiB (unos 16 000 polígonos de un hueso) no se puede atajar.

#### Importador `tools/model_import.py` (model-import I4)

El importador escribe el formato de §6 desde glTF (guía: `docs/modding/README.md` §8.2). Lo que se confirmó al escribirlo y probarlo en la batalla:
- **Claves:** cada lista (posición, rotación, escala) debe **empezar en el cuadro 0** y terminar en el centinela 30000: `FIGHTSTG_poseBone` (`model_mesh.c:83`) lee `key[-1]`
  si el cuadro pedido va antes de la primera clave, y `blendBone` (`:23`) toma la primera clave con cuadro >= al pedido, así que cada clip lleva claves en su primer
  y su último cuadro. Una pista constante es `[reposo@0, reposo@30000]`. Los clips se colocan en la línea de tiempo común desde el cuadro 10, con 2 cuadros de
  separación; se muestrea a 60 por segundo y se reduce con tolerancia (Douglas-Peucker sobre la interpolación lineal del juego). Los ángulos en bruto se desenrollan
  respecto a la pose de reposo (la mezcla del juego no toma el camino corto) y se descomponen como `Rz*Ry*Rx` eligiendo, de las dos soluciones, la más cercana a la anterior.
- **Pasos de movimiento** (`index` es el número de muestras acumuladas): reproducción `(i, n, f0, f0+n-1)`; entrada con mezcla `(0, B, 0, 0)` (`from` = pose actual); bucle
  `(i, 0, 0x8000, muestra a la que vuelve)`; fin que se queda `(i, 0, 0xFFFF, 0)`; fin que vuelve al reposo `(i, B, 0, 0), (i+B, 0, 0xFFFF, 0)` (mezcla hacia `step[1].frame == -1`,
  `toIdle`). La tabla es de 62 `u32` con la ranura 0 (el reposo, primera en el archivo) y la 1 obligatorias: el juego lee las dos al crear el modelo.
- **Malla:** triángulos con luz y Gouraud con textura, banderas `D1 80 91 C1 E0 A0 B0` y un comando de textura por CLUT. **Un hueso con la malla vacía** (0 vértices, 0
  normales, flujo `FF`, límites a cero) funciona en la batalla (probado 900 cuadros). Los cuadriláteros de un glTF no existen: salen 926 triángulos donde el Kotemon
  original tiene 564 polígonos (362 cuadriláteros + 202 triángulos).
- **Textura:** páginas de 256x240 texels y las CLUT en las filas 240..255 (4 por fila, `clutX` 0/16/32/48); el color 0 es transparente, un negro puro se escribe como `0x0421`.
- **Resultado medido:** Kotemon (383) exportado a glTF y vuelto a importar, usado como modelo de un enemigo nuevo, se ve igual que el original en la misma plantilla
  (capturas con a lo sumo 144 píxeles distintos de 76 800: los cuadriláteros partidos en dos triángulos). Un glTF editado y uno hecho a mano (un robot de cajas) se dibujan y animan.

#### Horneado al cargar (model-import I5)

El port convierte la fuente neutra al cargar, en C portable (`pc/shim/model_bake.c`; el mod trae `content/models/<llave>/model.json` + glTF + PNG, nombre
`<mod>:<llave>`, guía en `docs/modding/README.md` §8.2). Es el mismo algoritmo que `tools/model_import.py` y da **los mismos bytes** (`pc/modeltest.c` lo
comprueba con el robot de ejemplo y con 12 variantes que escribe `tools/test_model_import.py --parity`; un Kotemon y otros dos luchadores reexportados del disco
dieron el mismo archivo, probado a mano porque son datos del juego). Lo que hubo que fijar para que ambos coincidan:
- **Aritmética:** los dobles se suman de izquierda a derecha (el `sum()` de Python 3.12 en adelante compensa el redondeo, así que la herramienta usa su propia suma simple),
  `round()` es redondeo a par (`rint`), el `//` de flotantes es el de CPython y el C se compila con `-ffp-contract=off` (sin FMA).
- **Islas de UV:** una isla es la componente de celdas que quedan unidas **al final**; antes la raíz se leía al ir uniendo, y el resultado dependía del orden de iteración de un
  `set` de Python y partía una misma isla en varias. Corregido en la herramienta (y por tanto algo menos de islas y de paletas que en I4).
- **Sin cgltf:** el lector de glTF es propio (sobre el lector JSON del port): hace falta el árbol JSON, los accesores leídos como la herramienta (enteros normalizados divididos
  en doble) y base64; el PNG usa el `inflate` de `png_read.c`. Ninguna biblioteca más que vigilar para Windows y Android. No soporta accesores *sparse* (error claro).
- **Caché:** `cache/models/<hash>.bin` (`DW3M`, versión, largo, archivo, suma) con el hash FNV-1a de `model.json`, el glTF, los búferes, las imágenes y `MODEL_BAKE_VERSION`.
- **`"from": "dw3:model/<id>"`** (model-polish P3): el `model.json` del mod se completa con el del original exportado en memoria (`model_export.c`; claves de primer nivel del mod encima, `motions` ranura a ranura) y los `model.gltf`/`.bin`/`.png` que falten salen de la exportación. La clave de caché suma el id y `MODEL_EXPORT_VERSION` en lugar de los bytes del original, así que un acierto de caché no construye nada. El lector del disco entra por un callback (`ModelOriginals`): el horneador sigue sin leer el disco.
- **Medido** (esta máquina, -O2): Kotemon 20 ms y 24 MB de pico al hornear, 5 ms desde la caché; el robot 0.7 ms.

### 4.7 Sonido (142) — documentado y exportado (E3a/E3b)

- Por banco: `MP<banco>.BIN` (cabecera VAB y secuencias SEP, un archivo con tabla) y
  `MV<banco>.BIN` (cuerpo VAB que se manda al SPU). `SoundFiles` (`sound.h:83`) da
  `bodyFile`, `headFile`, el índice de la cabecera y las secuencias; hay 71 bancos
  (`SOUND_BANK_FILES[72]`, `sound.c:152`: el 0 es `NULL`, los ids 1..0x47) y 142 archivos en `DAT/SOUND/<banco>/`.
  Formato VAB/SEP: estándar PsyQ.
- **Comprobado con el disco EU (E3a/E3b, 2026-11-01).**
  - **Contenedor:** una tabla de desplazamientos de 32 bits little endian (sin recuento: la tabla mide lo que dice su primera entrada, y la primera entrada empieza justo después); una entrada acaba donde empieza la siguiente
    (la última, al final del archivo). El juego usa los 16 bits bajos de `vhIndex`, `seps[]` y `bodyEntry` como índice (`getArchiveEntry`, `file_cache.c:239`). El archivo `MP` tiene la cabecera VAB y los paquetes SEP (el VAB es la
    entrada 0, salvo en `TTLBGM`, donde es la 1 y el SEP es la 0); el `MV` tiene una sola entrada, el cuerpo (su tabla es `[4]`).
  - **VH:** `'pBAV'` en el 0; `u16` programas en 0x12, `u16` VAG en 0x16, volumen general en 0x18 y paneo en 0x19; `ProgAtr[128]` de 16 bytes en 0x20 (tonos, volumen, prioridad, modo, paneo); páginas de 0x200 bytes por
    programa usado desde 0x820 (16 registros de 32 bytes: prioridad, modo, volumen, paneo, nota central, ajuste fino con signo en 1/128 de semitono, nota mínima y máxima, rango de bend abajo/arriba en 0x0C/0x0D,
    `adsr1`/`adsr2` en 0x10/0x12, programa padre en 0x14, VAG en 0x16); y la tabla de tamaños de VAG (256 `u16` en unidades de 8 bytes, 1-indexada) justo después de las páginas. Lo lee `sndParseVab` (`snd_spu.c:134`).
  - **VAG:** bloques ADPCM de 16 bytes: byte 0 = desplazamiento (nibble bajo) y filtro (alto), byte 1 = marcas (1 fin, 2 repetición, 4 inicio de bucle), 14 bytes = 28 muestras (nibble bajo primero). Los 946 VAG
    no vacíos del disco acaban con marca de fin: 394 con repetición (bucle) y 552 sin ella (la voz se silencia). A 0x1000 de tono el SPU toca a 44100 Hz (`snd_spu.c`, `sndNotePrepare`).
  - **Muestras de un mod (E3c):** el port tiene 2 MB de memoria de SPU (PsyX_SPUAL), la consola 512 KB. Una muestra que un mod reemplaza se codifica a este ADPCM (`vag_encode.c`) y va en `0x80000 + 0x80000 * espacio`
    (hasta 0x7F000 bytes por espacio de banco); `SndVab.vagAddr[vag]` apunta ahí y los tonos siguen citando el número de VAG. Lo del disco no se mueve.
  - **SEP:** `'pQES'` + versión `u16`; cada registro tiene una cabecera de 13 bytes (id `u16`, resolución `u16`, tempo `u24` en µs por negra, compás `num` y `den` (potencia de 2: 2 = negras, como `FF 58`), extensión `u32`) y
    datos tipo MIDI (deltas VLQ, estado en ejecución, `FF 51` con tempo de 3 bytes **sin** byte de longitud). Los 1152 registros del disco EU tienen resolución 480 (1147 en 4/4, 4 en 3/4, 1 en 1/16); 77 hacen bucle
    (`CC99 = 20`, `CC6` = repeticiones y `CC99 = 30`); 1048 tienen 10 eventos o menos (1016: 4 eventos en 2 ticks). Los registros se numeran por su posición en el paquete tal como `sndParseSep` los decodifica (los de cero
    eventos no cuentan), 16 pistas planas por paquete (`sndSepTrackFor`, `snd_sep.c`). `W_JINGLE` (`0x6004001E`) es el registro 14 del paquete 1 de `COMMON`.
- **Perfil de exportación:** WAV PCM de 16 bits mono a 44100 Hz con trozo `smpl` (nota raíz = `centro - shift / 128`, bucle directo con el primer y el último cuadro); MIDI formato 1 (pista 0 con nombre, compás, tempo y
  marcas `loopStart count=N`/`loopEnd`; una pista por canal; `CC111` al inicio del bucle); `bank.json` (`bank-v1`) y `sequences.json` (`sequences-v1`). Detalle y reglas en `docs/modding/README.md` §8.1c. La decodificación
  ADPCM es la del renderizador del port (`decodeSound` de PsyX_SPUAL, filtro en coma flotante); un decodificador independiente en doble precisión coincide dentro de 1 unidad en 133 muestras de tres bancos. No se pudo
  comparar con el audio que el juego saca del SPU (no hay un gancho de `SND_DEBUG` que vuelque el PCM). Los nombres de la prueba de sonido (`SOUNDTST`) no se exportan: es un overlay de depuración fuera del port.

### 4.8 Pantallas y menús (TM/CS) — parcial, la familia mejor entendida

- Pares `<nombre>CS.BIN` (hoja de sprites) y `<nombre>TM.BIN` (archivo con un archivo de TIM
  RLEN); en `SCREEN/` (`STSHOP`, `STSTAT`, `STALBM`, `STCDSP`, `NAMEDT`, `STWMP0/1`, `STFRPT`,
  `CMNBGD`), `COUNTRY/` (`CMFONT`, `CNTSELWN`), `NAMEENT/` (2 pares), `MCARD/`, `DGLB/` y
  `TRAINING/`. Los lee `sprite.draw(FILE_CACHE.getEntry(<archivo> << 16), cuadro, x, y)` y
  `loader.loadArchive(getEntry(<archivo> << 16))` (por ejemplo `src/menus/gym/files.c:13`,
  `src/menus/memory_card/menu.c:172`).
- El layout está documentado en código en dos sitios: `include/engine/gfx.h:255` (`SpritePart`) y
  `tools/es419/menu_bank.py` (decodifica la hoja y el TIM de `CNTSELWN.BIN`, la edita y escribe
  PNG de vista previa). El port tiene lector en C: `pc/shim/menu_frame.c:164-320`
  (`mfSheetPart`, `mfRlenUnpack`, `mfArchiveImage`).
- Mapa de piezas (M4 T5, `atlas-v1`): `image_data.c` (`imgAtlasJson`) escribe junto a cada PNG un `.json` con las partes
  que su hoja CS referencia, en píxeles del PNG: `x = (u / 4 o 2 texeles por halfword - 64 * página) * (16 / bits del PNG)`;
  la página es el último índice de la cadena `NN_MM` y mide 64 halfwords (256 texeles de 4 bits, 128 de 8). Partes del
  otro modo se convierten (`read_as`). Formato en `pc/README.md`.
- Contenedores: un `*TM.BIN` es `[4]` -> subarchivo de TIM (tabla de
  desplazamientos desde su inicio, 0 al final); `TLOGO*`, `END__ALL`, `CNTSELWN` y `TTCOMMPK` son tablas cuyas
  entradas son hojas, subarchivos de TIM u otros datos. Casi todos los TIM son de 16 bits y 64x256 halfwords:
  páginas de VRAM crudas con índices de 4 u 8 bits y sus paletas dentro del propio bloque; solo unos pocos
  (títulos, mapa del mundo) son TIM de 8 bits con CLUT.
- `TLOGOJPN/PAL/USA`, `TTCOMMPK` (`STDWTITL_FILE_BACKGROUND`, `include/menus/title.h:39`),
  `END__ALL` (`SPLASH_FILE`, `title.h:38`): imágenes sueltas de título y pantalla fija.
- `FILE_MENU_SPRITES` (`CMFONTCS`) y `FILE_FONT` (`CMFONTTM`) son la hoja y la fuente de todos los
  menús (`file.h:182-183`).
- Sustitución al cargar (port): `timLoaderLoad`/`timLoaderLoadArchive` suben cada TIM con `LoadImage`; la copia del
  cargador en `pc/shim/tim_loader_pc.c` cambia el TIM por el que armó un PNG de mod (`image_data.c`, `imgSubstitute`): mismos
  índices y paleta que la extracción, reglas en `pc/README.md`. Observado en la tienda de objetos: `STSHOPTM/00_00` es el
  marco de sus ventanas (el fondo viene de otro archivo).

### 4.9 Cartas — parcial

- `CARDDATA.BIN` (`FILE_CARDGAME_TIMS`, entrada 3 es una hoja de sprites,
  `card_battle/sprites.c:256`); `CARD_NPC.BIN` (`FILE_CARDGAME_OPPONENTS`); `CARDPAK0-4`
  (`FILE_CARD_DATA`, las imágenes de las cartas, "la primera de cinco");
  `CARDPACK.BIN` (607, 244 sectores): no se encontró quién lo lee por número.
- Dibujos de las cartas (content-export E4): `CARDPAK0-4.BIN` (`CARD_IMAGE_FILES`, `card_drawer.c:131`; archivos 2038-2042 en
  EU) no tienen tabla: son 64 `CardImage` seguidas (`gfx.h:172`), cada una de `0x62C` bytes = 12 de cabecera (color, AP, HP,
  tipo, nivel, rango, carta de combo) + un TIM de 8 bits de 32x32 con su CLUT de 256 colores (`0x620` bytes; `cardDrawerLoadImage`
  lo sube con `timLoaderLoad`). 5 x 64 menos 4 = 316 cartas (el último archivo mide 94800 = 60 x 1580). `CARDPACK.BIN`
  (499280 = 316 x 1580) es casi la misma serie contigua (difiere en 6 cartas: 68, 199, 265, 266, 314, 315). El port las
  exporta como `images/card/cardpak<A>/<NN>.png` (`walkCards` en `image_data.c`): una por carta, sin mapa `atlas-v1`, y las
  sustituye por la posición del TIM en el archivo (`timLoaderLoad` recibe `card->tim`).
- `CARDDATA.BIN`: tabla de 4 entradas sin marca de fin; [0] y [1] son subarchivos de un TIM RLEN de 16 bits de 64x256 (páginas de
  VRAM crudas con índices de 4 bits, subidas en `0x280` y `0x340`, `card_battle/scene.c:20`), [2] y [3] las hojas de sprites que
  las describen (la hoja 2 usa la página 0 de [0], la 3 la de [1]; ambas con paletas dentro de la página). Sale como
  `card/carddata/00_00` y `01_00` con el `atlas-v1` de su hoja.

- Arte de los escenarios del campo (content-export E6a): `FIELD/STAGE/S<nnn>MASK.BIN` (294 archivos, 514 TIM) no es una
  máscara de profundidad sino un archivo de TIM RLEN con las páginas de arte de los objetos de mapa del escenario;
  `file_loader.c:113-120` lo carga con `loadArchive` en `(0x140,0x100)`, cada imagen 64 medias palabras a la derecha de la
  anterior (`gfx.h`). 243 entradas son TIM de 8 bits con CLUT y 271 páginas crudas de 16 bits con arte de 4 bits (paleta
  dentro de la página o en la CLUT) u 8 bits. La hoja es la entrada 0 de `S<nnn>TMPK.BIN` (lista de `SpritePart`, 14 bytes
  cada una): `u` en texels del área de objetos, página = `u/256` (4 bits) o `u/128` (8 bits) = entrada `i` del `MASK`; `cx`
  es columna del área (página `cx/64`) y `cy >= 256` es la fila `cy-256` de la CLUT del TIM de 8 bits (`setAltClut`). Una
  página puede mezclar piezas de los dos modos. Comprobado en el disco EU (S233: tres páginas, `u` hasta 752; S200 y S330:
  páginas mixtas) y en las capturas con un mod de prueba. 34 `TMPK` no traen hoja. `S415BG01/02` y `S815BG01/02`
  (`wstag415.c:384`, `wstag815.c:51`): una entrada 0 de 8 bits con CLUT de 6 filas y páginas crudas de 16 bits que, leídas como
  8 bits con esa CLUT, forman el fondo (nubes sobre la Tierra, roca). Salen como `field/stage/s<nnn>mask/<ii>` y
  `s<nnn>bg<nn>/<ii>`.

- Baldosas del mapa del campo (content-export E6b): `Z_STAGE/S<nnn>PACK.BIN` (237 archivos, 229 MB, 11 745 baldosas; los lee
  `FIELDSTG_createMapStreamer(FieldState.mapFile)`, `field.c:546`). Sector 0 = `MapFileHeader` (`include/field/field_mode.h:463`): `s32`
  `tileCount`, `width`, `height` (en baldosas de 128 px), `tileBytes` (el espacio de una baldosa, múltiplo de 2048) y `u16
  tileSizes[width*height]` (0 = sin baldosa; la tabla cabe en el sector: a lo sumo 1016). La baldosa `frame = fila*width + col`
  está en el sector `frame*tileBytes/2048 + 1` (`stream.c:17`) y `CD_READER.read` la deja en el montón sin pasar por `FILE_CACHE`;
  el Decompressor la desempaqueta si empieza por `RLEN` o la deja tal cual (`decompressor.c:75`). Contenido: palabra 0 = desplazamiento
  del TIM (0x1C en la mayoría; 0x28 y más cuando hay más recortes), luego tres listas (una por profundidad: `s32` cantidad y
  registros de 6 `s16`: `x, y, u, v, w, h`; el juego lee a lo sumo 5 por profundidad, `stream.c:82-114`) y en el desplazamiento un TIM
  de 8 bits con su CLUT de 256x1, 128 filas de alto y 128 a 256 px de ancho (`timLoader.load`, una de 12 posiciones de VRAM,
  CLUT en `(0, slot + 0xF0)`). Los recortes `(u, v, w, h)` salen de esa imagen y se dibujan en `(x, y)` relativo a la baldosa.
  Comprobado en el disco EU: las 11 745 baldosas se desempaquetan y decodifican, sin errores, y la composición de las tres
  profundidades de S233 (5x4 baldosas) y S330 muestra el mapa completo. Salen como `z_stage/s<nnn>pack/<FF>_<CC>.png` y
  `s<nnn>pack.json` (`stage-pack-v1`).

### 4.10 Entrenamiento, memory card, nombre, laboratorio — parcial

- `STTRNGCS/TM` (`STGTRAIN_FILE_SPRITES`/`IMAGES`, `include/menus/gym.h:20`), `STMCRDCS/TM`
  (`FILE_GMCARD_*`, `memory_card.h`), `STNMETCS/TM` y `STDGNMCS/TM` (`FILE_PLNMET_*`,
  `player_name.h`), `STDGLBCS/TM` (`FILE_LAB_SPRITES`, `digimon_lab.h`): mismo par TM/CS de 4.8.
- `TRAINING/TRANINnn.BIN` (12): no se encontró lector por número. Formato propio.

### 4.11 Películas (14) — documentado

- `.STR` de PS1 (Mode 2 Form 2, sectores de vídeo MDEC y XA). El juego las lee por LBA
  (`FILE_TABLE.getPos`, `movie.c:161`) y las decodifica con `DecDCTvlc2`; el port arma
  cuadros enteros en `pc/shim/host_st.c`. Las películas ya tienen número en la tabla (los 14
  coinciden con su directorio).
- **Comprobado con el disco EU (E2a, 2026-11-01).** El archivo de cada película es un múltiplo de 2048 bytes que
  incluye los sectores de audio. Vídeo: sectores de datos (usuario de 2048 bytes en el offset 24 del sector crudo de 2352)
  con la cabecera `StHEADER` de 32 bytes (`0x0160`, `0x8001`, sector, `nSectors` (hasta 9), `frameCount` desde 1, 320x416) y 2016
  bytes de flujo MDEC por sector; un cuadro empieza en el sector 0 y termina tras `nSectors`. 15 cuadros por segundo. Audio: sectores
  Form 2 (`submode 0x64`, audio + Form 2 + tiempo real), `file 1`, `channel 1`, `coding 0x01` = **estéreo, 37800 Hz, 4 bits**, 18
  grupos de 128 bytes (2016 cuadros L/R por sector); el audio solo existe en el `.bin` crudo (en los `.STR` extraídos solo queda la parte Form 1).
  Cuadros por película: `MOVIED01..08` 480, 375, 976, 577, 676, 240, 353, 450; `MOVIEE01` 671; `MOVIEE02..04` 1665; `MOVIEOP2` y
  `MOVIEOPN` 1792 (13 377 en total, unos 14.9 min). El audio y el vídeo no acaban a la vez (en `MOVIEE02` los últimos 442 sectores de 16650
  no llevan audio: la imagen dura 3 s más). El reproductor del juego (`host_press.c`) presenta el cuadro `N` cuando han sonado `N * 2520` muestras.
- **Exportación abierta (E2a):** `data/video/<NOMBRE>.avi`, AVI 1.0 (`RIFF`, `hdrl`, `LIST movi` con `00dc`/`01wb`, `idx1` con todas las entradas
  como cuadro clave y el desplazamiento desde el fourcc `movi`): vídeo `MJPG` 320x416 a 15/1 (JPEG base 4:2:0, calidad 85, tablas
  Huffman estándar, JFIF) y audio PCM de 16 bits a 37800 Hz (`WAVEFORMATEX` de 16 bytes, `dwScale` = alineación del bloque), el audio de cada cuadro
  (2520 muestras) justo antes de su imagen. Sin reloj: la misma entrada da los mismos bytes. Detalle y uso: `docs/modding/README.md` 8.1b.
- **Reemplazo desde un mod (E2b):** `mods/<mod>/video/<NOMBRE>.avi` se reproduce en lugar del `.STR` y su XA (`pc/shim/video_mod.c`; uso y perfil aceptado en `docs/modding/README.md` 8.1b).
  `movie.c` no cambia: `CdlSetloc` da el primer sector de la película, `host_cd.c` lo reconoce (la tabla del disco, `AAA/STR/<NOMBRE>.STR`) y apunta la fuente de sectores de la capa St y la fuente PCM del XA al
  AVI. La capa St entrega un registro de un sector por cuadro del AVI (`StHEADER` normal, 320x416, `frameCount` 0 para que un AVI más largo que el original no llegue a su cuadro final; en el
  payload, la marca `DW3V`, el número de cuadro y una bandera de fin); `DecDCTvlc2` (`host_press.c`) reconoce la marca, no decodifica y deja un resguardo; `DecDCTin` pone en la sombra la imagen del AVI
  (JPEG base a RGB, escalada a 320x416, a palabras de VRAM) en lugar de correr el MDEC. El fin lo marca un registro más con `frameCount` 0xFFFFFFFF (la condición `frameCount >= endFrame` de `movie.c:69`),
  tras el último cuadro, que además retiene al reproductor hasta que acabe el audio (a lo sumo 0,45 s). El reloj es el del XA, como siempre: el audio del AVI (16 bits, remuestreado a 37800 Hz y estéreo) entra
  por `sndXaSetPcm` (`host_xa.c`) y el cuadro `k` vence a las `(k - 1) * 37800 / fps` muestras. Decodificador: JPEG secuencial de Huffman de 8 bits (SOF0/SOF1), 1 o 3 componentes, cualquier muestreo entero
  (el croma se repite), DRI/RSTn, DQT de 8 o 16 bits, sin DHT = tablas del anexo K; lector de AVI: recorre `movi` (no usa `idx1`), códec `MJPG`/`AVI1`/`JPEG`, audio PCM de 16 bits.

## 5. Qué falta saber

- Estructura interna de los TIM de mosaicos (4.4), de los
  efectos de combate (4.6) y de `CARDPACK`/`TRANINnn` (4.9, 4.10).
- Las 18 `TMPK` y los pocos `TM` que no encajan con la heurística de 3 (se inspeccionan al
  abrir su formato).

## 6. Recomendación para M4 (primer formato abierto fuera del texto)

Orden recomendado, por valor para mods, lo entendido y el esfuerzo para ir y volver a PNG:

1. **Pantallas y menús TM/CS (4.8).** Esfuerzo bajo: el layout ya está en código
   (`gfx.h:255`, `menu_bank.py`, `menu_frame.c`), los TIM son archivos de un TIM RLEN de 8 bits
   con las paletas de 4 bits en las últimas filas de la misma imagen
   (`menu_bank.py`), y ya hay una herramienta que genera PNG de vista previa. Valor alto: es lo
   que un traductor o un diseñador de interfaz toca primero (logos, menús, el idioma es-419).
   Alcance: unos 40 archivos (18 pares CS/TM más imágenes sueltas).
2. **Sprites de campo `SPRT` (4.5).** Esfuerzo medio, ya extraídos a PNG con su mapa de animaciones
   (`sprite-v1`) y con sustitución desde mods (`mods/<mod>/images/field/sprt/<nombre>/<EEE>.png`, reglas en
   `pc/README.md`); falta la revisión del dueño. Valor alto (personajes y NPC).
3. **`SNMASK` + fondos `BG` (4.3).** Esfuerzo bajo para las imágenes (archivos de TIM RLEN, 294 +
   4) y valor medio por sí solas: sin las celdas `TMPK` y los mosaicos `PACK` no se ve el
   escenario completo.
4. **Mosaicos `SnnnPACK` (4.4).** Esfuerzo alto: 239 MB, 237 archivos con RLEN, sprites de tres
   profundidades y recomposición de mosaicos de 128 px; la cabecera ya está comprobada. Valor
   alto (todos los fondos), pero conviene después de 1 y 3.
5. **Combate (4.6), sonido (4.7), cartas (4.9).** Hay que abrir formato primero.

La recomendación para M4 es **1 (TM/CS)**, empezando por un par ya cubierto por `menu_bank.py`.
