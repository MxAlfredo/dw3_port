# Anatomía de un Digimon en DW3 (para modders y desarrolladores)

Qué es "un Digimon" en este juego y dónde vive cada pieza. Es el resultado de las investigaciones R2 (recursos por Digimon) y R1
(animaciones) y de la implementación D1-D4 y P1-P3. Las rutas `archivo:línea` son del árbol
actual; si el código se mueve, buscar el símbolo. Nada de lo que se describe aquí trae datos del juego: los números salen del código
y de la estructura, y las tablas concretas se leen del disco del usuario (ADR-001). Para crear contenido, la guía de mods es
[`modding/README.md`](modding/README.md) (§6.8 técnicas, §6.9 Digimon).

## 1. Tres tablas, un solo espacio de ids

Un Digimon no es una fila de una tabla: son hasta tres registros, unidos solo por un **id** (`s16`) que no se repite entre ellos
(ids 3..470 en el disco EU; los ids de compañero y de enemigo son disjuntos).

| Tabla | Qué guarda | Filas EU | Dónde |
|---|---|---|---|
| `DIGIMON_DATA` | formas de **compañero**: estadísticas base, resistencias, técnicas por nivel, crecimiento, nombre, pareja y destello | 55 (52 reales y 3 vacías, ids 468-470) | ejecutable: `src/engine/game/digimon.c:10`, tipo `DigimonData` `include/engine/game_state.h:82` |
| tabla de batalla | **enemigos**: estadísticas, técnicas, botín, IA | 193 (filas de 70 bytes) | archivo `FILE_BATTLE_TABLE` (0x1CF en EU), tipo `BattleTableEntry` `include/battle/battle_mode/battle.h:58` |
| archivo de luchadores | el cuerpo de **todos**: modelo, movimientos, efectos, cara, cámaras | 248 entradas | archivo `FILE_FIGHTERS` (0x1CC en EU), `FighterEntry`/`FighterInfo` `include/battle/battle_mode/model.h:210-254` |

- Un enemigo **no** es una fila de `DIGIMON_DATA` ni al revés. Quien lanza una batalla trae el id; el motor busca el cuerpo en el
  archivo de luchadores (`FIGHTSTG_fighterCache`, `src/battle/battle_mode/fighter_info.c:28-45`) y las estadísticas en la tabla que
  toque.
- `findDigimon` busca el id de un compañero en `DIGIMON_DATA` (`src/engine/game/digimon.c:125`); el tope de 52 del juego original
  se levantó en el port (ver §7).
- Las 3 filas vacías (468-470) existen en `DIGIMON_DATA` pero no tienen archivo de efectos: el port las rechaza como objetivo de
  cambios.

## 2. El cuerpo: modelo, movimientos y efectos

`FighterInfo` (`model.h:225`) guarda tres referencias de archivo del disco, cada una `(archivo << 16) | entrada`:

- `model`: el modelo 3D (huesos, mallas, texturas). Estructura visible en el código pero formato en investigación: `docs/formats.md` §4.6.
- `motions`: el archivo de movimientos (animaciones por huesos, claves por cuadro).
- `effects`: el archivo de **animaciones de técnicas**: una tabla de 18 ranuras de bytecode (guía §6.8). Un luchador tiene las
  ranuras que tiene; `TechData.script` elige cuál se reproduce.

Hay **248 archivos de modelo para los 248 luchadores** (55 compañeros + 193 enemigos; medido con el disco, y también 248 por contenido
distinto): cada luchador trae su archivo, no se comparten modelos a nivel de archivo (una cifra anterior de 139 era errónea). Lo
que sí se comparte es la ficha: un Digimon nuevo copia `{id, índice, tipo}` de otro (el port lo aprovecha, ver §6). Los compañeros y los enemigos usan estructuras distintas: `FighterInfo` mide 0xC4 bytes y
trae 12 cámaras; `FighterInfoEnemy` mide 0x48 y trae 3 más su cuenta (`model.h:239-254`). `FighterEntry.kind >= 0x3A` marca enemigo
(`fighter_info.c:37`); los tipos de compañero libres son pocos, pero el port no los gasta porque comparte la ficha del modelo. Un Digimon
nuevo con **modelo propio** (`"model"` como objeto) sí recibe una fila de `FighterInfo` propia: copia la de un luchador plantilla, cambia las
tres referencias, deja la cara vacía y usa un índice libre de su tabla (el `u8` admite 256 filas por tabla; quedan 201 de compañero y 63 de
enemigo sobre el disco EU). Detalle en `docs/formats.md` §4.6, «Piezas del port para modelos propios».

## 3. Cara y cámaras

- `face` es un desfase dentro del mismo archivo de luchadores a un arreglo de `FaceRect` (`model.h:229`, `FaceRect` en `model.h:298`;
  `FIGHTSTG_fighterCache.getFace`, `model.h:273`, termina en una `x` de 0xFF): rectángulos de la cara del luchador (su uso exacto en pantalla no
  está verificado aquí).
- `camPos`/`camRef`/`camProj` son las cámaras que lo miran (12 en un compañero, 3 en un enemigo), con `distance` y `height`.
  Se heredan con la ficha del modelo.

## 4. Retratos y menús

Los retratos de menú y los sprites de menú **van por compañero (8 compañeros)**, no por forma: una forma nueva de un compañero usa los
del compañero (hallazgo de R2). El port no los cambia; si algún día se necesitan retratos por forma habrá que investigar su archivo.
(Esta parte no está verificada con una ruta de código en este documento: es la nota de R2.)

## 5. Nombres

- `DigimonData.nameId` (`game_state.h:101`) es un `u8` que indexa `DIGNAM` (`TEXT_DIGIMON_NAMES`, `include/engine/file.h:200`;
  `src/engine/game/party.c:24`); `BattleTableEntry.nameId` es `s16` (`battle.h:62`). `DIGNAM` trae 251 entradas en el disco EU (los seis idiomas).
- Los Digimon nuevos del port toman las entradas **251 + posición** de `DIGNAM` y `DIGINF` (la lista alfabética de todos los Digimon
  nuevos, ver §6). Como `nameId` de un compañero es `u8`, la fila nueva conserva el de su `base` como respaldo y el nombre real sale por
  `PC_DIGIMON_NAME(d)` (`include/engine/digimon_access.h:38`, `pcDigimonNameId` en `pc/shim/ext_state.c`), que reemplaza a
  `d->nameId` en los sitios que muestran un nombre (PS1 se expande al mismo texto).
- La referencia `data/content/digimon.json` trae el nombre en inglés de cada luchador (leído del disco al arrancar).

## 6. Cómo el port agrega Digimon (ids y filas)

- **Ids:** los Digimon nuevos (enemigos y formas de compañero, de todos los mods) forman una lista alfabética por nombre completo
  `<mod>:<llave>`; el id es `471 + posición` (`PC_DIGIMON_FIRST_NEW_ID`, `pc/shim/ext_state.h:69`). No chocan por construcción y solo
  cambian si cambia el conjunto de mods; el guardado usa el nombre.
- **Enemigo nuevo:** copia la fila de 70 bytes de un enemigo original y reutiliza su ficha de luchador (o la de otro enemigo, `model`).
  La tabla de batalla y el archivo de luchadores se sirven ampliados desde memoria (`pc/shim/digimon_ext.c`, capa "generada" de
  `port_fs`); el disco no se toca. Con `"model"` como objeto, el enemigo (o la forma de compañero) tiene su propia fila de `FighterInfo`.
- **Forma de compañero nueva:** una fila nueva de `DIGIMON_DATA` (almacén `pcDigimonExtendRows`, filas desde la 55) y una entrada de
  luchador que comparte el modelo de otra forma de compañero. Las filas del juego original nunca se escriben.
- **Cómo se consigue:** la lista de evolución de cada compañero (`STFGTREP_evolutionLists[8][44]`,
  `src/battle/report/data/stfgtrep.c:684`; tipo `Evolution` `include/battle/report.h:31`) se recorre al final de la batalla y da la
  primera forma que el compañero no tiene y cuyos requisitos cumple (`STFGTREP_learnDigimon`, `src/battle/report/growth.c:131`). El
  port no escribe la lista original: devuelve una copia con las formas nuevas al final (`PC_EVOLUTION_LIST`).
- **Cuántas formas guarda un compañero:** `PartnerEntry` (`game_state.h:243`) son 44 en `GAME` (`PARTNER_ENTRY_COUNT`,
  `game_state.h:236`); el port añade un almacén por compañero hasta 256 (`PC_PARTNER_ENTRY_MAX`,
  `include/engine/partner_access.h:21`) y guarda lo que pasa de 44 en la sección `EXTP` del archivo de partida, por nombre.

## 7. Topes: qué se levantó y cuáles quedan

Levantados en el port (el build de PS1 sigue igual y compara 316 OK):

| Tope original | Estado |
|---|---|
| `findDigimon` solo miraba 52 filas | busca también las filas agregadas (`digimon.c:134-141`) |
| `DIGIMON_DATA[55]` fijo | almacén de filas desde la 55 |
| 44 formas por compañero (`PartnerEntry`, `owned[44]` del laboratorio, 44 de la lista de evolución) | hasta 256 entradas por compañero; las listas de evolución crecen sin tope de 254 (`Evolution.digimon` es `s32`) |
| `nameId` de un byte | `PC_DIGIMON_NAME`, entradas de `DIGNAM`/`DIGINF` desde la 251 |
| tamaño de la tabla de batalla y del archivo de luchadores | se sirven más largos desde memoria |
| índice de ficha `u8` de `FighterEntry` | no se gasta: una forma nueva comparte la ficha del modelo |

Siguen (o son límites de seguridad propios):

| Tope | Valor | Por qué |
|---|---|---|
| Id de Digimon | `s16`: hasta 32767; ids nuevos 471..32767 (32297) | `BattleTableEntry.id`, `FighterEntry.id` y `BattleFighter.id` son `s16` |
| Campos "fila + 1" en `u8` | tope fila 254 en `pairPartner`, `blastForms` | indexan `DIGIMON_DATA` en un byte; una forma nueva no es valor de ninguno (hace pareja como su `base`), y `Evolution.digimon` no los usa |
| Entradas por compañero | 256 (`PC_PARTNER_ENTRY_MAX`) | límite de los búferes del port |
| Filas de `DIGIMON_DATA` | 4096 (`PC_DIGIMON_ROW_LIMIT`, `ext_state.h:45`) | seguro del almacén |
| `FighterEntry.kind >= 0x3A` = enemigo | pocos tipos de compañero libres | la ficha se comparte, así que no aplica a los nuevos |
| Formato del modelo | sin investigar a fondo | no se pueden traer modelos propios (ver `docs/formats.md`) |

## 8. Animaciones de técnicas

Se explica en la guía de mods (§6.8). Resumen: la animación es `TechData.script` = una de las 18 ranuras del archivo de efectos del
luchador; `data/content/digimon.json` lista las ranuras de cada luchador (`scriptSlots`) y `tools/techcompat.py` calcula la tabla
de compatibilidad desde el disco.

## 9. Lo que falta saber

- El formato de los modelos de batalla (bytes de la malla y de las claves): `docs/formats.md`.
- La semántica completa del bytecode de animación (operandos por comando), necesaria para animaciones nuevas.
- Los archivos de retratos y menús por compañero, si se quisieran por forma.
