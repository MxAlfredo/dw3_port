# Inventario del estado de partida y sus topes (EU)

Base de `docs/decisions/ADR-008-extended-game-state.md`. Responde:
qué arreglos de tamaño fijo tiene el estado persistente, cómo se guarda en la tarjeta, quién los indexa y qué
se rompe si un mod agrega ids más allá. Solo hay conteos y estructura; ningún valor del juego (ADR-001).
Las líneas `ruta:línea` son del árbol al 2026-10-30. Lo marcado **(H)** es hipótesis, no verificada.

## 1. Cómo se guarda hoy

- `GAME` (`GameState`, `include/engine/game_state.h:470`) tiene primero la parte guardable, de 0x26C4 bytes en
  EU (`GAME_SAVE_SIZE`, `game_state.h:567-570`; `newGame` pone a cero justo esa parte, `src/engine/game/state.c:6`),
  y después el modo actual y la tabla de métodos (`mode` en 0x26C4, `game_state.h:513-540`).
- **Copia de struct completa, sin formato propio.** Guardar es `dataBuf->save = GAME_SAVE`
  (`src/menus/memory_card/saves.c:486-494`); cargar es `GAME_SAVE = dataBuf->save`
  (`saves.c:405`). `GameSave` son `GAME_SAVE_SIZE/4` palabras (`include/menus/memory_card.h:113-120`); `SaveData`
  superpone `GameSave`, `GameState` y bytes (`memory_card.h:124-128`).
- **Suma de control:** XOR de bytes (`computeChecksum`, `src/engine/memcard/memcard.c:444-454`) desde el byte 4
  hasta el final (`SAVE_CHECKED`, `memory_card.h:102`), guardada en el byte 0 (`GameState.checksum`,
  `game_state.h:471`). La carga rechaza (error 8) si `computeChecksum(..) & ~checksum` no es 0 (`saves.c:397`):
  solo detecta bits que faltan, es débil. La versión (`MEMCARD_SAVE_VERSION` = 4 en EU, `memory_card.h:98-103`) solo
  se exige al cargar desde el título (`saves.c:401`).
- **Disposición en la tarjeta:** archivo con cabecera, íconos, sección de info (`infoSize` 0x100) y **tres**
  secciones de datos de `dataSize` 0x2700 cada una (`memcard.c:14-15`, desplazamientos en `memcard.c:198-214` y
  `279-295`). Una sección de datos mide 0x2700 y el guardado usa 0x26C4: **sobran 0x3C bytes** por casilla, pero los
  tamaños son fijos del archivo que crea el juego original (H: cambiarlos rompería la tarjeta original).
- **Info de casilla** (`MemCardFile`, `memory_card.h:84-90`): nombre, área, lugar, dinero, tiempo, 3 socios y
  niveles; la rellena `STGMCARD_fillSave` (`saves.c:495-502`).
- El port respalda la tarjeta con una imagen cruda de 128 KB (16 bloques de 8 KB) en
  `pc/shim/host_memcard.c:1-30`; es formato estándar `.mcd`.
- **Bytes sobrantes aprovechables (H):** `pad1`, `pad3`, `pad5[7]`, `pad10[0x18]` y `unusedC` están comentados como
  "nada los lee ni escribe" (`game_state.h:472-478`). Se copian enteros al cargar y al guardar, y están dentro de la
  suma de control. Servirían para una marca de emparejamiento (ADR-008 §4, opción A), pero hay que verificar con una corrida
  que ningún código los toque.

## 2. Arreglos y topes del estado guardado

"Usado" es lo que el contenido original ocupa (de las tablas de código). Offsets de `game_state.h:491-497`.

| Campo | Capacidad | Usado | Ancho de id | Quién indexa (muestra) | Qué se rompe con un id nuevo |
|---|---|---|---|---|---|
| `items[0x193]` (s8, cuentas, tope 99), 0x7C | 403 casillas (0..402) | ids 1..402 (`ITEMS[402]`, `items.c:481`); casilla 0 sin uso | 9 bits en eventos (`code & 0x1FF`, `events.c:496-550`); s16 en listas | 42 sitios con índice variable en 20 archivos: `events.c:340-461`, `report.c:144`, `action_events.c:11`, `helpers.c:105-159`, `item_screen.c:116-239`, `pack_open.c:406,541`, `battle/item.c:347,847` | id >= 403 escribe en `equippedItems` (los arreglos son contiguos): corrupción silenciosa |
| `equippedItems[0x193]` (s8), 0x20F | 403 | ídem | ídem | 29 usos en 7 archivos (`helpers.c:119-158`, `events.c:461-480`, `items.c:54`) | ídem; pisa `cards` |
| `cards[0x13D]` (s8, tope 9 `CARD_COPIES_MAX`), 0x3A2 | 317 | 315 (`CARD_COUNT`, `include/cardgame/album.h:15`) | 9 bits en eventos (`events.c:551`) | `addCards` (`party.c:63`), `changeCard` (`events.c:484`), `checkCard` (`events.c:351`), editor de mazos `editor.c:214,223` (bucle a 315) | id >= 317 pisa `cardsSeen`; los bucles a 315 ignoran las nuevas |
| `cardsSeen[0x149]` (u8), 0x4DF | 329 | 315 | = `cards` | `album.c:86,145,161,324,486`, `party.c:64`, `pack_open.c:111` | las cartas nuevas no salen en el álbum |
| `decks[3]` (nombre 0x16 + `s16 cards[40]`), 0x628 | 3 mazos de 40 | 3 x 40 | s16 | `GAME.decks` 26 usos en 9 archivos; `DECK_SIZE`/`DECK_COUNT` (`game_state.h:257-258`) | el ancho s16 alcanza; los topes 3 y 40 son de estructura |
| `partners[8]` (0x3DC bytes c/u), 0x75C | `PARTNER_COUNT` 8 (`game_state.h:457`) | 8 | `unlocked` = id + 3 | 351 usos de `partners[`/`entries[` | socios nuevos: ver Digimon |
| `PartnerEntry entries[44]` por socio | `PARTNER_ENTRY_COUNT` 44 (`game_state.h:236`) | hasta 44 por socio (evoluciones 8 x 44, `data-tables.md` §2) | `id` s16 desde `FIRST_ENTRY_ID` 3 | `findPartnerEntry`, `listPartnerEntries` (`partner.c`), `digivolve_panel.c`, lista del laboratorio `owned[44]` (`digimon_lab.h:136`) | un Digimon 45 no cabe; la lista del laboratorio tiene 44 |
| `PartnerEntry.skills[6]` (s16: id en 13 bits `SKILL_ID` 0x1FFF + banderas 0x2000/0x4000/0x8000, `game_state.h:250-254`) | 6 por entrada; ids 1..8191 | 443 (`TECHS[443]`, `items.c:906`) | 13 bits | `growth.c:269`, `tech_menu.c:20,101,203`, `partner_info.c:102,133,208`; `TECHS[id-1]` en `techs.c:12,167`, `tech_effects.c:32-104`, `menu.c:1255` | un id > 443 indexa `TECHS` fuera de rango (el ancho de 13 bits sí alcanza hasta 8191); más de 6 por entrada no cabe |
| `PartnerStats.equip[6]` (s16) | 6 | 6 | s16 | `unequipItem` (`events.c:435`), `stats.c:83` | alcanza hasta 32767; `ITEM_FUNCS.get(id)` indexa `ITEMS[id-1]` (`items.c:16`) |
| `party[3]` (s32), 0x70 | `PARTY_SIZE` 3 | 3 | índice de socio | `getPartyMember`, `setParty` | estructura de 3 |
| `progress` (s32), 0x263C | 32 bits | hasta 0x2B visto (`game_state.h:446`) | s32 | 291 usos en 117 archivos | sin límite práctico |
| `flags02..flags40` (bits, 0x2644..) | 144, 16, 8, 8, 32, 64, 96, 32... bits por grupo (`game_state.h:513-531`) | los que usan los scripts | 9 bits por bandera (`code & 0x1FF`) | `checkCondition`/`applyAction` (`events.c:536-630`), 33 usos de `GAME.flags` | cada grupo tiene tamaño exacto en bytes y cabe en 512 bits como máximo: banderas nuevas necesitan almacén aparte |
| `money` (s32), `name[0x18]`, `playTime` | tope `MONEY_MAX` | — | — | — | — |

Notas:

- **Los 5 arreglos por id (items, equippedItems, cards, cardsSeen y las listas por tipo) están pegados**: no hay
  holgura, así que ampliar un arreglo "en su lugar" desplazaría todo lo que sigue y cambiaría el formato de la tarjeta.
- Los índices son **directos** (`GAME.items[id]`), no pasan por una función; por eso un id fuera de rango no falla:
  corrompe el vecino. Las funciones por puntero (`ITEM_FUNCS.get/list/isKind`, `GAME.funcs.*` de socios) sí se
  pueden redirigir desde el port sin editar `src/` (`items.c:890`, `state.c:64-71`).
- Ancho del contador de objetos: s8 con tope 99 aplicado en código (`events.c:460`, `report.c:144-146`,
  `action_events.c:11-13`); no es un tope de tabla.

## 3. Topes en el código y en las tablas (no son estado guardado)

| Tope | Valor | Dónde | Efecto en contenido nuevo |
|---|---|---|---|
| `ITEMS` | 402 filas (id n = `ITEMS[n-1]`) | `items.c:481`, `getItem` `items.c:16` | un id nuevo lee fuera de la tabla |
| Listas por tipo de objeto | 52 + 84 + 124 + 78 + 69 ids con terminador (llaves, usables, armas, armaduras, accesorios) | `items.c:341-383,903` | sin entrada en su lista, el objeto no aparece en ningún menú (`listItems`, `items.c:40-58`, índice < 5) |
| `ITEM_TYPE_CATEGORIES` | 32 tipos | `items.c:898` | tipos nuevos no caben |
| `WEAPON_DATA`/`ARMOR_DATA`/`ACCESSORY_DATA` | 123 / 77 / 68 | `items.c` | el registro de equipo de un objeto nuevo necesita fila propia |
| Búferes de lista de objetos | `s16 [0x194]` (404) en 13 declaraciones | `battle_mode/menu.h:88`, `card_packs.h:91,128`, `item_shop.h:123-124`, `field_menu.h:81,211-212,246,441,443` | con más de 404 tipos distintos en la bolsa el búfer se desborda |
| `TECHS` | 443 filas (`TECHS[id-1]`) | `items.c:906`; la ranura 439 se escribe en ejecución (`data-tables.md` §3.2) | un id nuevo lee fuera de la tabla |
| `DIGIMON_DATA` | 55 filas (52 Digimon + 3 de relleno) | `digimon.c:7` | filas nuevas no existen |
| `findDigimon` | busca solo en las **primeras 52** | `digimon.c:125` | un Digimon con fila 53 o más no se encuentra, aunque la tabla crezca |
| Bucle por Digimon | 55 | `pc/shim/battle_trace.c:141` (herramienta del port) | solo depuración |
| Álbum y editor de cartas | bucles a 315 | `editor.c:214,223`; `album.c:145,161,486` (`CARD_COUNT`) | las cartas nuevas no se listan |
| Tiendas | 31 (`STITSHOP_shops`) y tope del port de 64 ids por tienda | `pc/boot.c:563-585` (`PC_ITEM_ROWS` 402) | el rango válido del motor de tablas es 1..402 (`boot.c:461,585`) |
| Botín / enemigos | `item` s16 en el registro de 0x46 bytes: ids 1..402 y los de objetos de mods (T6) | `pc/boot.c` | resuelto: acepta el nombre |
| Páginas de listas | 7 líneas de objetos, 6 de técnicas, 8 paquetes por página | `battle_mode/menu.h:65,81`; `pack_open.c:535` | **(H)** la paginación es por cursor, no por cantidad fija; confirmar con una corrida |
| Textos | nombres y descripciones por índice de id en los archivos de texto | `report.c:139-140` (`TEXT_ITEM_NAMES`, índice = id) | un id nuevo no tiene cadena; el port tiene que dársela (H: capa de texto) |
| Eventos | `code & 0x1FF` = ids de objeto y de bandera de 0 a 511 | `events.c:496-550` | los scripts del juego no pueden nombrar ids >= 512 (los mods usan nombres, fase 3 de ADR-006) |

## 4. Resumen de lo que importa

1. **Lo que el juego original lee de la tarjeta no puede traer nada que no entienda.** Un id de objeto, carta o
   técnica fuera de rango en la tarjeta haría que el juego original (y el port sin el mod) corrompa memoria.
2. Hay **tres clases de límite**: (a) arreglos indexados por id y pegados (objetos, equipados, cartas, vistas);
   (b) campos que guardan un id y ya tienen ancho de sobra (`s16` de equipo, mazos y entradas; 13 bits de técnica);
   (c) estructuras de cuenta fija (8 socios, 44 entradas, 6 técnicas, 3 mazos, 3 de grupo, bits de banderas).
3. El código indexa **directo** (`GAME.items[id]`, `TECHS[id-1]`, `ITEMS[id-1]`), con puntos por puntero solo para
   los objetos (`ITEM_FUNCS`) y los socios (`GAME.funcs`).

## 5. Estado de la fase 0 (objetos)

Hecho: los 90 sitios variables de `items` y `equippedItems` (85 en `src/`,
5 en `pc/`) pasan por `PC_ITEM_COUNT(id)` y `PC_ITEM_EQUIPPED(id)` (`include/engine/item_access.h`, incluido al final
de `game_state.h`). Sin `DW3_PC` expanden al texto `GAME.items[id]` / `GAME.equippedItems[id]` (el preprocesador
da la misma salida y `make compare` sigue en 316 OK). Con `DW3_PC` los miembros se llaman `itemsCard` y
`equippedItemsCard` (cerco: un acceso directo no compila) y las macros llaman a `pcItemCountRef` /
`pcItemEquippedRef` (`pc/shim/ext_state.c`): ids 0..402 al arreglo de `GAME`, 403..32767 a un almacén que crece
(tope de seguridad `PC_EXT_ITEM_LIMIT`), cualquier otro a una casilla desechable que lee 0, con una línea en el canal
`[state]` (una vez). `cards` y `cardsSeen` siguen directos (fase 2). El guardado copia `GAME` como bloque, sin
cambios; la extensión se guarda en T2.

## 6. Estado de la fase 1 (guardado `dw3save-v1`, T2)

Hecho: `host_memcard.c` sigue sirviendo las llamadas `MemCard*`
(las pantallas STGMCARD no cambian) pero detrás no hay imagen de tarjeta.

- **Qué ve el juego:** una tarjeta en el puerto 0 con un solo archivo de 4 bloques, `BESLES-03936DMW3-EUR` (el nombre
  cambia con el idioma, `memcard.c:setSaveFileName`; cualquier nombre con `DMW3` es ese archivo), con la disposición que el
  juego escribe: sector de cabecera + icono(s) (el número va en el byte 2 de la cabecera), sección de info de `0x100`
  bytes (`MemCardFile`, 3 resúmenes) y 3 secciones de datos de `0x2700` (`memcard.c:initMemCard`, `readSave`/`writeSave`).
- **Cómo se asignan las casillas:** una por sección de datos. La casilla `N` es `saves/<carpeta>/slot<N>.dw3save`.
  El juego escribe primero la info y después los datos de la casilla (`saves.c:507-530`): la info se retiene y el archivo
  de la casilla se escribe, atómico, cuando llega el último sector de datos (`0x26C4` bytes de `GAME`), con la copia
  de la info, el número de secuencia y la extensión en ese momento. La info que lee el juego es la copia de la casilla
  guardada más recientemente (cada escritura de info trae los tres resúmenes, así la última es la verdad y el port no
  necesita conocer su disposición interna); sin casillas, la que el juego escribió al crear el archivo
  (`card.dw3card`, que además guarda cabecera, iconos y nombre y es lo que significa "el archivo existe").
- **Formato `dw3save-v1`:** cabecera de 24 bytes (magia `DW3SAVE\0`, versión de formato 1, versión del port, número de
  secciones, tamaño total), secciones `etiqueta[4] + u32 tamaño + datos` y un CRC32 (IEEE) final de todo lo anterior. Una
  versión de formato más nueva se rechaza. Secciones de casilla: `META` (casilla, secuencia, tamaño), `GAME` (los
  `0x26C4` bytes originales sin tocar), `INFO` (`0x100`), `EXTI` (la extensión por nombre) y `MODS` (reservada, hoy sin
  mods). `EXTI`: versión 1, cuenta, y por entrada nombre (`<mod>:<nombre>`), cuenta y equipado; un id ≥ 403 sin nombre no
  se guarda (línea en `[saves]`); al cargar, un nombre que ningún mod define se descarta con una línea (T3 conecta los
  nombres con `pcExtStateSetNameHooks`).
- **Escritura y daños:** `<ruta>.tmp` + fsync + el archivo anterior bueno pasa a `.bak` + renombrar. Al leer, un archivo
  dañado (cortado, CRC) cae a su `.bak` con una línea en `[saves]`; sin copia buena la casilla se lee vacía. Un guardado
  interrumpido deja el archivo anterior.
- **Carpeta:** `$DW3_SAVES_DIR`; si no, `saves/default` de la carpeta del usuario (ADR-010). La vista legible de cada casilla va en `saves/default/view/slot<N>.json`.
- **Importación (una vía):** sin `card.dw3card` ni casillas en la carpeta de partidas, sin `imported.txt` en `import/` y con
  tarjetas `.mcd` en `import/` (todas, por nombre sin distinguir mayúsculas, hasta que una dé partidas; `DW3_MEMCARD` nombra
  una sola, para los arneses, y su `imported.txt` queda junto a ella), se importa el archivo DW3
  (preferencia EUR, USA, otros; los demás se anotan) a casillas nativas con el bloque original y sin extensión; una
  casilla vacía se omite y una cuya suma de control del juego falla se omite con una línea. La tarjeta no se modifica
  (prueba: `cmp`/md5 antes y después). Para reimportar: borrar `imported.txt` y la carpeta de partidas. No se lee ninguna
  ruta ni formato anterior a ADR-010.
- **Límite de este diseño:** la tarjeta ya no se puede abrir en emulador tras guardar con el port; el juego original solo ve
  lo que se importa.

## 7. Estado de la fase 1 (objeto nuevo, T3, y vista legible, T3b)

Nota: lo que describen §7 y §8 sobre leer `content/<tipo>.json`, ordenar los
nombres y dar ids, el almacén, los textos por idioma y los ayudantes de bytes de los trozos `EXTI`/`EXTT` vive ahora en
`pc/shim/content.c/.h` (`ContentType`, `ContentSet`, `contentLoad`, `contentIdOf`, `contentText`, `contentReferenceText`, `contentOriginalId`,
`contentReadName`/`contentWriteName`). `item_ext.c` y `tech_ext.c` son definiciones de tipo (sus campos, su registro, `ITEM_FUNCS`,
`learnedBy` y la comprobación de animación); los formatos, los ids, los trozos guardados y las líneas de log no cambiaron. Los nombres
`dw3:item/<id>` y `dw3:tech/<id>` los resuelve el núcleo (`contentOriginalId`); `data/content/items.json` y `techs.json` (`content-ref-v1`)
los escribe `pc/boot.c` con `tmExportFile` (mismo manifest y regla ".new" que `data/tables/`). Los textos del código de §7 y §8 siguen
valiendo con ese reparto. El código de los trozos (`pcExtStateEncode`/`Decode`/`EncodeTechs`/`RemapTechs`, en `ext_state.c`) conserva su
distribución de bytes y usa los ayudantes del núcleo.

Hecho: `pc/shim/item_ext.c` lee `mods/<mod>/content/items.json` (`items-v1`) de los mods
`"kind": "content"`, ordena los nombres completos `<mod>:<llave>` y da ids desde 403 (`PC_ITEM_ORIG_COUNT`) en ese orden;
nunca se guardan (el trozo `EXTI` guarda el nombre). Con al menos un objeto, redirige `ITEM_FUNCS.get/getCategory/isKind/list`
(el juego no llama `getItem` ni indexa `ITEMS` fuera de `items.c`): `get` devuelve una copia propia del `ItemInfo` de la base con
los campos cambiados (y un `ItemEffect` propio), `list` agrega los objetos nuevos de la bolsa a las listas por tipo. Sin objetos,
`ITEM_FUNCS` queda intacto. Nombres y descripciones: la capa de texto (`tdSetItemTextHook`) agrega entradas 403.. a `ITMNAM` e
`ITMINF` de cada idioma servido (relleno vacío bajo 403), con la caída es-419, spa, eng. `shop_stock` acepta el nombre del
objeto en la lista (`TmField.byName`/`idOk`); `item_prices` acepta filas con llave `"<mod>:<nombre>"` y `battle_loot.item` acepta el nombre (T6, T7: `TmField.byName`/`idOk` en
campos escalares y `TmTable.keyByName`/`extRow`: el motor guarda el valor de cada campo del registro del objeto antes del primer
cambio y lo repone en cada `tmApply`; la fila `"*"` no llega a los objetos nuevos; el precio de la declaración queda como valor por
defecto). Descripciones (T8): la caja es de una línea de 280 px (`layout.json`, `ITMINF`); `tdFitLine` (`text_data.c`) acorta con `...` en
una palabra, con las métricas del reflujo, las descripciones de objetos nuevos que no caben, y el log dice cuál. El botín soltado o
robado llega por `PC_ITEM_COUNT` (`report.c`, `action_events.c`) sin cambios en `src/`; el nombre sale del gancho de texto. Al cargar una partida cuyo nombre ya no existe, el objeto se descarta con la línea `[saves]`.

T3b: `pc/shim/save_view.c` escribe `slot<N>.json` (`dw3save-view-v1`, solo lectura) en cada guardado, tras el `.dw3save`, con
escritura atómica; un fallo solo registra una línea `[saves]`.

## 8. Estado de las técnicas nuevas

Hecho: `pc/shim/tech_ext.c` lee `mods/<mod>/content/techs.json` (`techs-v1`) de los mods `"kind": "content"`, ordena los nombres
completos y da ids desde 444 (`PC_TECH_FIRST_EXT_ID`). El juego las alcanza por `PC_TECH(índice)` (T1): `pcTechRef` (`ext_state.c`)
devuelve la tabla original para 0..442 y, para 443 en adelante, el almacén que el registro le entrega (`pcExtStateSetTechStore`);
fuera de ambos, una entrada desechable. Sin mods el almacén está vacío y nada cambia (sin cambios en `src/` ni `include/`).

- **Dónde viven los ids de técnica en el estado guardado:** solo en `PartnerEntry.skills[6]` (13 bits de id + banderas `0x2000` conocida,
  `0x4000` marcada, `0x8000` última) de las 44 entradas de cada uno de los 8 compañeros, dentro del bloque `GAME`. Evidencia: una
  búsqueda de `skills`/`SKILL_ID` en `src/` y `GameState`/`PartnerStats`: el resto de las estructuras que los nombran (`DigimonData`,
  `BattleStats`, el menú de batalla) no se guardan.
- **Guardado por nombre (sección `EXTT`):** el bloque `GAME` conserva los números tal cual (el original no sabe de nombres); la casilla
  agrega `EXTT` (versión 1, cuenta y, por cada id nuevo distinto del bloque, `u16` id guardado, `u16` largo y el nombre). Sin técnicas
  nuevas no hay sección. Al leer una casilla del disco, la primera lectura de su bloque reescribe cada id nuevo al número que el nombre
  tiene ahora (banderas intactas) y recalcula el byte de suma de control (`GameState.checksum`, XOR desde el byte 4); un nombre que ningún
  mod define, un id sin entrada en el trozo o un trozo dañado vacían la ranura (`skills[k] = 0`) con una línea `[saves]`: nunca queda un id
  colgando. Código: `pcExtStateEncodeTechs`/`pcExtStateRemapTechs` (`ext_state.c`), `host_memcard.c` (sección, reescritura en la lectura).
- **Textos:** la capa de texto generaliza el gancho de objetos: `tdSetTechTextHook` agrega entradas 444.. a `SKLNAM`/`SKLINF`
  (`SKLINF` trae 220 entradas en el disco EU: el hueco se rellena con cadenas vacías) y acorta las descripciones largas a una línea.
- **Aprender (T3):** `learnedBy` parchea `DigimonData.skills[slot]` y `skillLevels[slot - 1]` de la fila del Digimon (al recargar o quitar el
  mod se restauran). Antes se comprueba, con el archivo de luchadores del disco, que el luchador tenga la ranura de script de la técnica
  (y la 6 y la 8 si es 5, la 8 si es 6): `pcFighterSlotProbe` (`boot.c`) y `techExtSetFighterProbe`.

## 9. Digimon existentes

`pc/shim/digimon_ext.c/.h` lee `mods/<mod>/content/digimon.json` (`digimon-v1`, objeto `"change"`), que cambia luchadores existentes
por `dw3:digimon/<id>` con las facetas `partner` (fila de `DIGIMON_DATA`) y `enemy` (fila de `FILE_BATTLE_TABLE`, que se lee del disco
para validar los ids). No crea ids, así que el guardado no cambia. Las filas de compañero se parchean en memoria con una máscara de
bytes y `digimonExtReset` las restaura (antes de `techExtReset`, porque `learnedBy` guarda sus propios valores previos); las de enemigo
se parchean en el búfer de lectura de la tabla de batalla por el mismo gancho post-lectura de `battle_loot` (`pcPatchBattleTable`):
primero el contenido y después la tabla, que gana en `item` e `itemChance`. `data/content/digimon.json` (`content-ref-v1`) lista los 248
luchadores con su nombre en inglés (DIGNAM, por `nameId`) y los campos editables con su valor original. Guía: `docs/modding/README.md` §6.9.

## 10. Enemigos nuevos y encuentros

`digimon.json` suma `"new"` (enemigos, núcleo de contenido: ids desde 471 en orden alfabético, nombre en `DIGNAM` con la entrada
`251 + posición`, hasta 32297 por el `s16` de los ids) y `"encounters"` (colocar enemigos en `FIELDSTG_encounters`, `onEnter` del
ámbito `fieldstg`). **Guardado: sin
impacto, por evidencia.** `GameState` (los `0x26BC` bytes que se guardan, `include/engine/game_state.h`) contiene el dinero, el grupo,
objetos, cartas (`cards`, `cardsSeen`: ids de carta), mazos, los ocho `Partner` (ids de formas de compañero y de técnicas), el
progreso y las banderas; no hay campo con un id de enemigo. El encuentro de la batalla siguiente (`BATTLE_SETUP`) y el resultado
(`BATTLE_RESULT`) viven fuera de ese bloque y no se guardan. Por eso el tipo `digimon` no tiene trozo de nombres (`EXTI`/`EXTT`):
sus ganchos de nombre del núcleo son vacíos a propósito, y quitar un mod con enemigos nuevos no deja nada colgado. Si algún día un
dato guardado llevara el id de un enemigo (por ejemplo un bestiario), ese dato debe guardarse por nombre como los objetos.

## 11. Acceso a `DIGIMON_DATA` y filas nuevas

Igual que `TECHS` (§8), la tabla de formas de compañero se alcanza con un accesor: `PC_DIGIMON(fila)` es un lvalue por **fila**
(base 0; la fila no es el id de Digimon: el id es `PC_DIGIMON(fila).id` y `findDigimon` lo convierte en fila) y `PC_DIGIMON_BASE` es el
puntero a la primera fila, para los bucles sobre las filas originales (`include/engine/digimon_access.h`). En PS1 los macros se expanden
al mismo texto (`DIGIMON_DATA[fila]`, `DIGIMON_DATA`), así que el preprocesado y el build que coincide no cambian. En el port la tabla
se llama `DIGIMON_DATA_CARD` (cerco: un `DIGIMON_DATA` directo no compila) y el accesor llama a `pcDigimonRef` (`pc/shim/ext_state.c`):
filas 0..54 a la tabla original, filas desde 55 a un almacén que crece (`pcDigimonExtendRows(n)` agrega `n` filas en cero y devuelve la
primera; el límite de seguridad es `PC_DIGIMON_ROW_LIMIT`, 4096), y cualquier otra fila una fila desechable en cero con una línea
`[state]` una sola vez. Los punteros a filas del almacén dejan de valer tras `pcDigimonExtendRows`: se amplía al cargar, antes de que el
juego tome alguno. `findDigimon` busca igual que antes las 52 formas reales y, solo en el port, después las filas agregadas (las 3 vacías
originales siguen sin buscarse: mismos resultados para los ids originales).

Espacios de fila que no cambian en P1: `pairPartner`, `blastForms` y `Evolution.digimon` guardan fila + 1 en un `u8`, por lo que llegan
a la fila 254 como máximo (255 es el tope de esos campos); P3 decide cómo levantarlo.

## 12. Formas de compañero nuevas: filas, nombres y guardado

`digimon.json` `"new"` con un compañero de `"base"` agrega una **forma de compañero**: una fila de `DigimonData` (copia de la fila original
del `base` con los campos de §9), una entrada en el archivo de luchadores (`{id, índice y tipo del modelo}`, sin ficha nueva) y un nombre.

- **Ids:** una sola lista alfabética de todos los Digimon nuevos (enemigos y compañeros, de todos los mods), `id = 471 + posición`; así no
  hay choques y el id sigue siendo determinista para un mismo conjunto de mods (cambia si se instala o quita otro mod con Digimon nuevos,
  por eso lo guardado lleva el nombre). La fila es `55 + posición entre las formas de compañero nuevas` (`pcDigimonExtendRows` al cargar).
  Un enemigo nuevo solo pone su fila en la tabla de batalla; una forma, solo en `DIGIMON_DATA`.
- **Nombres:** `DigimonData.nameId` es `u8` y `DIGNAM` trae 251 entradas, así que 5 nombres nuevos agotaban el campo. Se levantó con un
  accesor, no con un parche de texto: `PC_DIGIMON_NAME(d)` (`include/engine/digimon_access.h`; en PS1 se expande al mismo `d->nameId`)
  reemplaza a `d->nameId` en los 20 sitios de `src/` (12 archivos) y el de `pc/battle/menu.c` que dan a una pantalla el nombre de una forma (menú de DV y
  mensajes de la batalla, informe, panel y estado del menú de campo, y las pantallas del laboratorio); en el port llama a
  `pcDigimonNameId` (`ext_state.c`), que para una fila del almacén devuelve el número de entrada guardado junto a la fila
  (`pcDigimonSetNameId`) y para cualquier otra, `d->nameId`. Quedan sin convertir a propósito: `enemy->nameId`/`entry->nameId` de la
  tabla de batalla (ya `s16`, enemigos), `party.c` (las 8 formas base), `switch_in_menu.c:159` (compara `pairPartner` con el `nameId` de la
  forma: el campo de otro espacio, §11) y el arnés (`battle_trace.c`). La fila nueva conserva el `nameId` del `base` (respaldo para un
  lector que no use el accesor). La entrada de `DIGNAM` (y la de `DIGINF`, la descripción del laboratorio) es `251 + posición` también para
  los enemigos, por el mismo gancho de texto.
- **Modelo:** la entrada de luchador copia el índice y el tipo de la forma de `"model"` (o del `base`); no hay `FighterInfo` nueva, así que no
  se agota el `u8` del índice ni el tipo (`fighter_info.c`: solo `kind >= 0x3A` es enemigo; no se agrega ningún tipo). Excepción (model-import
  I3): si `"model"` es un objeto, el Digimon tiene su **propia fila** de `FighterInfo` y la entrada lleva `{id, índice nuevo, tipo de la
  plantilla}`; el índice es el primero libre de su tabla (compañeros desde 55, enemigos desde 193, tope `u8` de 256 filas por tabla). No se
  guarda: el guardado sigue usando solo el nombre de la forma (`EXTD`).
- **Guardado:** dentro del bloque `GAME`, un id de Digimon vive en `PartnerEntry.id` (44 por compañero), `PartnerStats.slots[4]` (formas
  elegidas) y `Partner.battleDigivolve` (la forma con que empieza la batalla). Evidencia: `include/engine/game_state.h`, y que
  `GameState` solo trae esos campos de id de forma. La casilla agrega el trozo `EXTD` (versión 1; por cada id nuevo distinto, `u16` id
  guardado, `u16` id original del `base`, `u16` largo y nombre). Al leer una casilla del disco, junto al remapeo de técnicas
  (`pcExtStateRemapDigimon`, `host_memcard.c`), cada id se reasigna por nombre (suma de control recalculada). **Regla de descarte** (el mod
  ya no está, no hay trozo o está dañado): la entrada pasa a ser la forma original del `base` (con su nivel, experiencia y técnicas) si el
  compañero no la tiene ya; si la tiene, la entrada se vacía; las formas elegidas y la de batalla que la nombraban pasan a la original si
  el compañero la tiene, si no a 0; sin trozo, sin `base` conocido: se vacían. Es la opción más segura porque nunca deja un id sin
  `DigimonData` (que `GET_DIGIMON` devolvería como NULL) y la partida sigue cargando. `view/slotN.json` lista `modForms`.
- **No cambia en P2:** `pcExtStateReset` (que corre en cada carga de partida) ya no reinicia las filas de Digimon: pertenecen a los mods
  del arranque (`digimonExtReset`). Los campos de fila + 1 en `u8` siguen como en §11 (P3).

## 13. Más de 44 formas por compañero y evolución de las formas nuevas

Dos topes de la lista de formas se levantaron con el patrón de M3 (ADR-008: un tope de tamaño fijo se levanta caso por caso, con accesor
y guardado):

- **`PartnerStats.entries[44]` (las formas que un compañero ha aprendido).** Vive dentro de `GAME` (8 compañeros x 44 x 20 bytes,
  `include/engine/game_state.h`), así que las primeras 44 entradas **siguen ahí, byte a byte** como en el original (un archivo de partida
  de 44 formas o menos no cambia). Las siguientes viven en el almacén de `pc/shim/ext_state.c` (`pcPartnerEntryRef`, `pcPartnerEntryLimit`,
  `pcPartnerEntryGrow`): un arreglo por compañero que crece de a una entrada, hasta `PC_PARTNER_ENTRY_MAX` = 256 entradas en total (tope
  de seguridad: los búferes de las pantallas son estáticos). Acceso: `PC_PARTNER_ENTRY(partner, i)` (lvalue) y
  `PC_PARTNER_ENTRY_LIMIT(partner)` (`include/engine/partner_access.h`, incluido al final de `game_state.h`); en PS1 se expanden al mismo
  texto (`GAME.partners[p].info.entries[i]`, `PARTNER_ENTRY_COUNT`), así que la salida de `cpp -P` de los 627 `.c` no cambia. Todos los sitios
  del juego que recorren o indexan las entradas pasan por `src/engine/game/partner.c` (`findPartnerEntry`, `listPartnerEntries`,
  `addPartnerEntry`, `getPartnerEntry`, `setPartnerEntry`; el juego solo llama a través de `GAME.funcs`): ahí se cambiaron los recorridos y los accesos por índice. `addPartnerEntry` pide una entrada más al almacén cuando las 44 y las guardadas están llenas.
- **Búferes de `listPartnerEntries`.** El laboratorio y el informe copian los ids de las entradas en búferes de tamaño fijo (`LabSlotScreen.entries[45]`,
  `LabEntryPanel.entries[47]`, `LabRecipeScreen.owned[44]`, `LabEntryList.ids[50]`, la variable local de `STGDGLAB_updateEntryList`,
  `ReportPartner.entries[44]`): en el port miden `PC_ENTRY_LIST(n)` = `n` + 212, y `listPartnerEntries` rellena de ceros hasta 256 (en PS1, 44
  como antes; no se lee más allá). Los bucles de desplazamiento del laboratorio ya trabajaban con la cuenta (`entryCount`), no con 44.
- **Guardado: sección `EXTP`.** El almacén no está en el bloque `GAME`, así que va en su propia sección de `dw3save-v1`: versión (1), cuenta y, por entrada
  guardada (las vacías no se escriben): compañero, banderas (bit k: la técnica k es de un mod; bit 6: la forma es nueva), nivel, experiencia, id
  de forma (0 si es nueva), 6 técnicas y, a continuación, los **nombres** de la forma y de las técnicas de mod (los mismos ganchos de
  `EXTD`/`EXTT`). No lleva ids de la corrida que guardó. Al leer, se reasignan por nombre; una forma cuyo mod ya no está se descarta con
  una línea `[saves]` (esta sección no guarda el `base`, a diferencia de `EXTD`), y una técnica de un mod ausente deja su ranura vacía. Sin entradas guardadas no hay sección, y una partida que se carga
  sin sección las deja sin almacén (`pcExtStateDecodeEntries`, junto a `pcExtStateDecode`); una partida nueva también (`newGame`, bajo
  `DW3_PC`). `view/slotN.json` no las lista.
  Limitación conocida: el remapeo de `EXTD` del bloque decide si el compañero "ya tiene" la forma original de un `base` mirando solo las 44
  entradas del bloque; una forma elegida o de batalla que pierde su mod y cuyo `base` está en una entrada tras la 44 pasa a 0.
- **Campos de fila + 1 en `u8`.** `DigimonData.pairPartner` y `blastForms` (`u8`, tope fila 254) **no se levantaron**: la evolución de las formas
  nuevas no los necesita (una forma nueva no es el compañero de pareja ni la forma de una explosión de nadie; `switch_in_menu.c:159`
  compara `pairPartner` con el `nameId` de la forma y, como la fila nueva conserva el `nameId` del `base`, una forma nueva hace pareja
  como su `base`: no se tocó). `Evolution.digimon` es `s32` y `Evolution.needs[].digimon` es `s16` (fila + 1), así que **no hay tope de 254** en la lista de evolución: nunca fueron `u8`.

**Evolución.** `STFGTREP_learnDigimon` (`src/battle/report/growth.c`) revisa al final de cada batalla la lista del compañero
(`STFGTREP_evolutions[partner]`, de 44 entradas `Evolution`) y le da la primera forma que no tiene y cuyos requisitos cumple (formas a un
nivel y un dato del compañero). Dos líneas cambiadas: la lista que lee (`PC_EVOLUTION_LIST(partner, original)`) y su largo
(`PC_EVOLUTION_COUNT(partner)`, `include/battle/report.h`; PS1: el mismo texto). En el port, `pcEvolutionList` devuelve una copia de la lista original
con las entradas de los mods al final (`pcEvolutionExtend`, `ext_state.c`; la lista original no se escribe) y `digimon_ext.c` las publica
al cargar los mods (`digimon.json` `"evolution"` de una forma nueva; formato en `docs/modding/README.md` §6.9; se reinician con `digimonExtReset`).
`PcEvolution` (`ext_state.h`) tiene el diseño de `Evolution` (una unidad del shim no puede incluir `battle/report.h`); `boot.c` comprueba que coinciden.

