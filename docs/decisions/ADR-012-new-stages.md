# ADR-012 — Escenarios de campo nuevos desde mods

- **Estado:** ACEPTADA (2026-11-04; implementada; el dueño probó el ejemplo `training-field` jugando)

## Contexto

Pedido del dueño (2026-11-04): que un mod agregue escenarios de campo nuevos (mapa, colisión, zonas de combate, salidas)
sin código C, como capacidad de la plataforma. Hoy un mod solo parchea escenarios existentes (`stage-patch-v1`,
ADR-006). Bloqueos verificados en el código:

- **Modo sin fila = cuelgue.** `FIELDSTG_pickStage` recorre `FIELDSTG_stages` (o `FIELDSTG_extraStages` en el capítulo
  extra de EU) buscando `entry->mode == mode`; si llega al final hace `while (1) {}` (`src/field/field_mode/state.c:34-47`).
- **El modo decide el overlay.** `MODE_ENTRY_POINTS[mode >> 8]` (`src/engine/system/overlay.c:33`) y
  `MODE_OVERLAY_FILES[mode >> 8]` (`:54`) son tablas de 23 entradas (`:4-19`); los índices 2 y 3 son `FIELDSTG_start`.
  Un modo con `mode >> 8 >= 23` leería fuera de la tabla.
- **Bandera de visita.** `FIELD_VISITED_FLAG(mode)` = `0x2000 + mode - 0x200` (`include/field/field_mode.h:661`), escrita
  en cada entrada al campo (`src/field/field_mode/field.c:523`, único llamador en `src/`, `include/` y `pc/`). Cae en
  `GAME.flags20[0x1E]` (240 bits, `include/engine/game_state.h:558`): para un modo >= 0x2F0 el bit sale de `flags20` y
  corrompe `flags40` (el modo 0x300 ya sería el bit 0x2100).
- **Archivos.** El mapa se lee por número de archivo y sectores: el `MapStreamer` pide la cabecera (1 sector) con
  `CD_READER.read(task->file, 0, 1, ...)` y de ella saca ancho, alto, `tileBytes` y `tileSizes[]`
  (`src/field/field_mode/map_streamer.c:306-325`).
- **`warp`** solo acepta escenarios compilados: `pcStageExists` recorre `SD_SETUPS` (`pc/boot.c:1659-1668`) y
  `stage_codes.c:523-526` rechaza el resto ("there is no such stage in this build").

Piezas del port que ya existen y se reutilizan:

- Archivos virtuales (ADR-011): `pfsAddVirtualFile` da números desde el final de la tabla del disco (`pc/shim/port_fs.h:106-119`)
  y llegan al juego "como cualquier archivo" (existe, cuenta de sectores, caché, `(file << 16) | entry`).
- Nombres `<mod>:<llave>` y asignación alfabética de números de sesión (ADR-008 §2, ADR-009 §2).
- `PcExtState` con variables por nombre `EXTV` (`pc/shim/ext_state.h:248-270`) y trozos por nombre en `dw3save-v1`.
- `SdStage` (`pc/shim/stage_data.h:114-140`) y la capa de parches (`stage-patch-v1`), que ya sabe aplicar actores,
  ranuras, eventos y batallas a un escenario.

## Decisión

### 1. Identidad

- Un escenario de mod se nombra `<mod>:<nombre>` (como ADR-009 §2). Es la única forma en que se guarda o se escribe.
- El port asigna un **modo de sesión** al cargar los mods, en orden alfabético de la llave. Rango propuesto:
  `0x300` en adelante. Razones: los modos de campo de EU son 512..750 (0x200-0x2EE) y `mode >> 8 == 3` también corre
  `FIELDSTG_start` (`overlay.c:5`), así que no hay colisión ni salida de tabla. **Hipótesis a confirmar en T4:** que
  nada del juego trate distinto a `0x3xx` de `0x2xx` (el comentario de `field.c:525-528` los agrupa como campos) y que
  ninguna fila original use `0x3xx` (la búsqueda en `fieldstg.c` no encontró ninguna). Tope: 256 escenarios nuevos
  (`0x300-0x3FF`), límite de seguridad documentado, no de diseño; pasar de ahí exigiría más índices en las tablas de
  `overlay.c` y se reabre si hace falta.
- Los números **no se guardan**. `GAME.mode` es una copia cruda dentro del bloque original (`game_state.h:560`), así que al
  guardar el códec reemplaza el modo de un escenario de mod por el **escenario de respaldo** (§1.1) y escribe el nombre
  en un trozo nuevo `EXTS` de `dw3save-v1` (u32 versión, u16 largo, nombre sin NUL, y la posición real s32 x, y, dir). Al cargar, si el nombre resuelve se
  restaura el modo de sesión; si no, queda el modo de respaldo ya guardado.

#### 1.1 Mod quitado

El escenario de respaldo se declara en el propio `stage-new-v1` (`"fallback"`: un escenario original, con posición) y por
defecto es el Parque Central (330) en su posición de inicio (**hipótesis:** el valor por defecto se valida en T4). Con
el mod ausente la partida carga allí con una línea de log; la visita y las variables del mod se conservan en `EXTV` (como
hoy: "desactivar y reactivar un mod conserva su progreso", `ext_state.h:256`).

### 2. Registro (gancho en `FIELDSTG_pickStage`)

En el build de PC, antes del bucle (`state.c:34`), un gancho acotado:

```c
#ifdef DW3_PC
    if (pcModStagePick(mode, &FIELDSTG_state)) { return; } /* fills stageFile and stageInit of a mod stage */
#endif
```

`pcModStagePick` vive en `pc/` (código nuevo del port, CLAUDE.md) y, para un modo de sesión de mod, llena `stageFile` con
el archivo virtual de TMPK/código de mapa (§4) y `stageInit` con un init genérico del port que construye el escenario
desde su `SdStage` aplicando la misma capa que `pcStagePatchHook` (`field.c:518-521`). Sin `DW3_PC` el texto es el original;
tras tocarlo se corre `make VERSION=eu compare` (316 OK). **Hipótesis a verificar en T4:** que el init genérico pueda
partir de un `FIELDSTG_state` vacío y que `smApplyStage` cubra todo lo que hoy hace un `wstagNNN.c` (hoy `SdStage` es solo
exportación y `modes[4]` supone modos originales). Además se reutiliza el acceso por fila de las tablas de campo para que el
`while (1)` final nunca sea alcanzable por un modo de mod: si el gancho no resuelve un modo >= 0x300, el port vuelve al
escenario de respaldo en vez de colgarse (guarda en el gancho, no en `src/`).

### 3. Bandera de visita

`FIELD_VISITED_FLAG(mode)` se usa una sola vez (`field.c:523`). Se cambia por un accesor `#ifdef DW3_PC`:

```c
#ifdef DW3_PC
    pcFieldMarkVisited(GAME.funcs.getMode()); /* >= 0x300: EXTV, "<mod>:visited.<name>" = 1; otherwise the original flag */
#else
    FLAGS_00.applyAction(FIELD_VISITED_FLAG(GAME.funcs.getMode()), 1);
#endif
```

Para un modo de mod no se escribe `flags20`; se fija la variable `EXTV` `<mod>:visited.<nombre>` (`pcExtVarSet`). Lectores:
hoy no hay ninguno de la bandera en `src/` (solo ese escritor); si el mapa de ciudad o los condicionales de eventos la
leen por `applyAction`/`isSet` con el número, T4 debe repasarlos (`rg 'FIELD_VISITED_FLAG\|0x2000'`) y exponer la visita de
mod como condición `mod-flag` ya existente.

### 4. Archivos virtuales

Cada escenario de mod aporta, a partir de su carpeta, tres archivos virtuales (`pfsAddVirtualFile`, orden fijo: mods por
orden de carga y luego escenarios por llave, para que el número sea determinista, `port_fs.h:112-114`):

| Archivo | Contenido | Quién lo lee |
|---|---|---|
| Capas del mapa (formato TMPK, §5.2) | piso/colisión, disparadores, áreas | `FIELDSTG_getMapCell` (`map.c:19-97`) |
| `PACK` de mosaicos de fondo | cabecera + mosaicos | `MapStreamer` por sectores |
| Hoja de objetos / imágenes | objetos del mapa (`StageTile`) | el init del escenario |

**Decisión sobre el PACK: generar un búfer en memoria con el formato original y servirlo como archivo virtual, sin tocar
el streamer.** El `MapStreamer` solo conoce un número de archivo, `CD_READER.read(file, sector, n, dst, ...)` y la cabecera
(`map_streamer.c:306-325`); un virtual pasa por `cdRead`/`pfsReadSectors`, igual que un reemplazo crudo. Un hook del
streamer duplicaría lógica de `src/` y rompería la regla de dejar PS1 intacto. Costo: hay que codificar PACK de cualquier
tamaño (T3). **Resuelto en T3** (`docs/formats.md` §4.4, `pc/shim/map_pack.[ch]`): cada mosaico es un bloque RLEN **o** el mosaico sin comprimir (el
`Decompressor` mira la primera palabra, `decompressor.c:13-25`; el disco usa los dos), con tamaño de un número entero de sectores; el PACK no
tiene otra compresión. Límites: 255 mosaicos en total (`viewTiles` es `u8`, `map_streamer.c:275,283`), 31 sectores por mosaico (`tileSizes` es `u16`),
5 sprites por lista, imagen de a lo más 256 x 128 px de 8 bits. El códec arma el PACK de cualquier tamaño (`mpPackBuild`) y lo comprueba byte a byte contra los
237 del disco.

El texto del escenario (`textFile`) reutiliza la capa de texto del port (`textFile` de `SdStage`, `text_data.h:196`); música y
banco de sonido son los existentes (`soundBank`, `music` por número del juego). No hay audio nuevo en esta versión.

### 5. Formato `stage-new-v1`

#### 5.1 Contenedor

Un `stage-new-v1` es una extensión de `SdStage` (no un tercer esquema): `mods/<mod>/new-stages/<nombre>/stage.json` (T5: la carpeta es `new-stages/`, para no mezclarla con los parches `stages/<nnn>.json`; el nombre de la carpeta es la llave, sin miembro `"key"`) con

- `"format": "stage-new-v1"`, `"key"`, `"fallback"`;
- `"text"` (archivo de texto por nombre del mod), `"music"`, `"soundBank"`, `"start"` (`x`, `y`, `dir`);
- `"actors"`, `"slots"`, `"events"`, `"battles"`: **el mismo esquema que `stage-patch-v1` agrega** (acciones, condiciones
  `mod-flag`, `warp`, `start-battle`); un escenario nuevo es un parche sobre un escenario vacío;
- `"map"`: `size` en píxeles y las capas del §5.2;
- `"tiles"`: PNG de fondo (se parte en mosaicos al construir el PACK, T3).

Se valida al cargar (ADR-009 §5): una relación que no resuelve se rechaza con una línea de log y el escenario no se
registra; un mod malo nunca cuelga el juego.

#### 5.2 Capas del mapa (TMPK)

Las capas se escriben como PNG indexado de **índice** (T5: un píxel por píxel del mapa, no por celda de 8 px; `docs/stages.md` §13). Formato de salida, tal como lo
decodifica `FIELDSTG_getMapCell` (`map.c:19-97`) y lo prototipó el escenario 345 (1280×1024; `new-stages.md` §Hallazgos):
seis `u32` de desplazamiento; luego la rejilla (`u8 w, h` y un `u8` por celda de 128 px), `cells64` (`u8`), `cells32/16/8`
(`s16`, árbol deduplicado de bloques de 128 → 64 → 32 → 16 → 8 px) y los píxeles (64 `u8` por bloque de 8×8). Cada entrada
del archivo da tres planos por celda de 8 px: **piso/colisión, disparadores y áreas**; el codificador de T2 los deduplica y
reproduce byte a byte los TMPK del disco (T2: 728 de 728 capas; el orden de identificadores es por nivel, recorrido por filas).

Significado de los valores (T2, `docs/formats.md` §4.3, leído del código): **piso** 0 = celda cerrada, 1 = abierto y plano, 2 a 7 = pendiente
(al volar, 2..6 = muro de nivel n), 16 + n = la misma pendiente espejada (al volar, 18..22 = techo de nivel 24 - v); **disparadores**
0 = nada, si no `dirección << 5 | ranura` (ranura 1..31 del `StageSlot`); **áreas** 0 = sin combate, si no el área `a - 1` de `FieldBattles`.
El disco usa solo 0-7 y 18-22 en los pisos. Lo que no está probado (el efecto del 7 en vuelo, qué dirección es cada `v >> 5`, la colisión
del suelo a pie más allá del 0) queda como hipótesis en `formats.md`; el modder usa 0 (cerrado), 1 (plano) y los disparadores y áreas.

### 6. Salidas

- Hacia un escenario de mod: `"stage": "<mod>:<nombre>"` en `slots` y en la acción `warp` (hoy un número, 1..65535,
  `stage_codes.c:521-526`); el compilador de acciones resuelve el nombre al modo de sesión. `pcStageExists` (`boot.c:1659`)
  consulta también el registro de mods; los números originales siguen valiendo.
- Hacia atrás: las ranuras del escenario nuevo apuntan a un escenario original por número (`330`) o a otro de mod.
- La entrada desde el escenario original es un parche `stage-patch-v1` (un actor/guía y una ranura con `warp`), con las
  acciones y ranuras que `stage-patch-v1` ya permite (hipótesis: se confirma en T6); la condición de la medalla es `have-item` + `mod-flag`.

### 7. Encuentros

Hoy los encuentros son 335 filas (`FIELDSTG_encounters`, llave 1..334; `docs/modding/README.md` §6.9) y `AreaBattle.encounter`
las indexa; `tables/field_encounters.json` y `"encounters"` de `digimon.json` **modifican filas existentes**, no agregan.
Para un escenario nuevo hay dos caminos:

- **v1 (decidido):** el escenario nuevo usa filas originales por número (`"encounter": 34`) y las ajusta con los parches
  existentes. Alcanza para el ejemplo (enemigos originales, niveles ajustados), con la salvedad de que el ajuste es
  global a ese encuentro.
- **v2 (hecho en T5b):** filas nuevas `<mod>:<nombre>` en `tables/field_encounters.json`, asignadas por el port sobre un almacén propio (accesores
  `#ifdef DW3_PC` `FIELDSTG_ENCOUNTER` y `STFGTREP_REWARD`; los arreglos del disco no se escriben). Se verificaron los puntos de lectura de `BATTLE_SETUP.battle`
  / `BATTLE_RESULT.battle` (`docs/stages.md` §13); el botín es del enemigo, no del encuentro. Las dos opciones conviven: el escenario puede usar filas originales
  por número o las nuevas por nombre.

### 8. Fuera de alcance y alternativas

- **Reusar un escenario existente con mapa distinto:** descartada; el PACK y el TMPK son por escenario y la colisión no se
  puede cambiar sin tocar sus archivos (reemplazo del mismo tamaño solamente).
- **Migrar de motor / reescribir el campo:** descartada; pierde la lógica original y rompe `compare`.
- **Hook del `MapStreamer`:** descartada (§4).
- **Fuera de alcance:** editor visual de mapas, cinemáticas con código propio, Lua, audio nuevo, mosaicos animados, filas
  de encuentro nuevas (v2) y más de 256 escenarios.

## Consecuencias

- Sin mods, el juego es idéntico: los dos cambios en `src/` (`pickStage`, bandera de visita) son `#ifdef DW3_PC` y el
  build de PS1 conserva su texto; `make VERSION=eu compare` obligatorio (316 OK).
- Un escenario es un nombre estable; el número de sesión nunca se guarda y quitar el mod devuelve a la partida al respaldo.
- Cuatro piezas de trabajo: herramienta TMPK (T2), PACK desde PNG (T3), registro y gancho (T4), cargador JSON (T5).
- **Preguntas abiertas:** (1) resuelta en T2: los valores de piso 4, 6, 19, 22 son pendientes (§5.2); (2) resuelta en T3: RLEN o sin comprimir, ambos válidos (§4); tope de 255 mosaicos por escenario;
  (3) **resuelta en T4, con salvedades:** `0x3xx` corre `FIELDSTG_start` (`overlay.c:5`) y `field.c:528` ya lo agrupa con `0x2xx`; difieren los lugares que indexan por modo: `actor_icon.c:97` (sin icono del jugador desde
  `0x2D7`; **cableado en T4** con `pcModeAsOriginal`), `field_menu.c:130` (`getFieldZone` da -1: la opción 2 del menú sale gris; **cableado en T4** con `pcModeAsOriginal`), `banner.c` (nombres por modo; sin fila deja `windows->area` en NULL y el banner lo desreferencia: el modo de mod crea el banner con `show = 0`),
  `STSTATUS_getArea` (`(u8)fieldMode` cae en una fila de `0x2xx`; **envuelto desde `pc/boot.c`**) y el puerto (`mode >> 8 == 2` en `stage_timing.c`, `host_pad.c`, `options_language.c`; **cambiados a `PC_IS_FIELD_MODE`/`MS_IS_FIELD_MODE`**), ver `docs/stages.md` §12; (4) **resuelta en T4:** el init genérico
  (`pcModStageStart`) parte de un `FieldState` vacío y fija lo mismo que `setupStage`; `images`, `objects`, `slots`, `events` y `actors` quedan como punteros del cargador (T5), con tablas vacías de respaldo;
  (5) **resuelta en T4:** `STSTATUS_getVisitedAreas` (`helpers.c:186`) es el único lector (recorre `0x200-0x2D6`), así que los escenarios de mod no salen en el mapa;
  (6) **resuelta en T5b:** encuentros nuevos con recompensas, por nombre o número (§7); (7) el escenario de respaldo por defecto: Parque Central, resuelta en T4/T5 (`mod_stages.h`, `"fallback"` en `stage.json`). T5 implementó el cargador (`docs/stages.md` §13); T5b hizo los encuentros propios.

## Preguntas que siguen abiertas (T7, 2026-11-04)

Las seis preguntas de arriba están resueltas. Lo que queda, dicho con honestidad:

- **Sin verificar con el juego:** que el bit 15 de la CLUT sea la convención correcta (los colores salen bien en la prueba sin ventana y en la prueba del dueño, pero no se miró entrada por entrada); qué hace un `soundBank` que el juego no tiene (no hay tabla que lo diga); que el piso 0 bloquee al jugador (solo leído en `checkFlightProbe`).
- **Fuera de alcance de la fase 1:** los escenarios de mod no salen en el mapa del mundo (pregunta 5); alturas y rampas, árboles que se patean y pesca son la fase 2 (T8 a T12), todavía sin investigar en el decomp.
- **No probado:** quitar el mod con una partida guardada dentro de un piso se probó en `memcardtest` (`fallback`), no en juego largo; varios mods con escenarios nuevos a la vez no se probaron en juego.
