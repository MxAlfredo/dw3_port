# ADR-013 — Compañeros como arreglo propio del port

- **Estado:** ACCEPTED (dirección del dueño, 2026-11-05: quitar por completo los topes de PS1 para compañeros; el guardado
  `dw3save-v1` es la fuente de verdad y el diseño del bloque `GAME` original no condiciona nada tras la importación).

## Contexto

`GameState.partners[8]` vive dentro del bloque guardado de tamaño fijo (`include/engine/game_state.h`, 0x75C, 8 x 0x3DC). Para
paquetes de compañeros nuevos (el paquete de ejemplo betamon-pack) hacen falta más de 8, sin cambiar los bytes de las partidas sin
mods. Los demás topes (objetos ADR-008, cartas ADR-009) se levantaron con un campo renombrado y un accesor; este sigue ese patrón.

## Decisión

- **Un arreglo contiguo y creciente de `Partner`, propiedad del port** (`pcPartners`, `pcPartnerCount`, en `pc/shim/ext_state.c`),
  con los 8 originales primero. Acceso: `PC_PARTNER(p)` (lvalue, indexación simple: sin llamada ni comprobación, costo cero) y
  `PC_PARTNER_COUNT` (8 sin mods) en `include/engine/partner_access.h`. `pcPartnersGrow(n)` lo amplía (los nuevos en cero,
  límite de seguridad `PC_PARTNER_LIMIT` = 256, no un tope de diseño); los punteros a un compañero no valen tras ampliar, se
  amplía al cargar. Empieza como un arreglo estático de 8: nada necesita llamada de inicio.
- **Cerco:** con `DW3_PC` el campo se llama `partnersCard` (como `itemsCard`/`cardsCard`), así que un `GAME.partners[...]` directo no
  compila. En PS1 los macros expanden al mismo texto (`GAME.partners[p]`, `PARTNER_COUNT`) y el build que coincide no cambia.
- **`GAME.partnersCard[8]` queda solo como espejo de importación/serialización de los 8 originales.** `pcPartnersImport` (espejo ->
  arreglo; limpia y quita los compañeros >= 8) corre tras cargar una casilla (`saves.c`, `GAME_SAVE = ...`) y en `newGame`;
  `pcPartnersExport` (arreglo -> espejo) corre antes de copiar la parte guardable de `GAME` al búfer de la tarjeta
  (`STGMCARD_fillSave`). Macros `PC_PARTNERS_IMPORT/EXPORT` (vacíos en PS1). Resultado: carga -> guardado deja el bloque
  idéntico byte a byte (prueba en `pc/extstatetest.c`).
- Los compañeros >= 8 no se crean en esta tarea (P3/P4); el arreglo y el accesor ya los admiten. Las tablas fijas por compañero
  (animaciones `[8]`, `STFGTREP_evolutionLists[8]`, `STARTER_PARTIES`, máscaras `u8` de equipo, `LabPartyScreen.partners[8]`) no se
  tocan y no se indexan por encima de 8 mientras no haya compañeros nuevos (P4/P6).
- Código que lee el bloque guardado en bytes (los códecs de `ext_state.c`, `save_view.c`, la ficha de la tarjeta) sigue con los 8
  del bloque: `PC_PARTNER_BLOCK(game, i)` y `PARTNER_COUNT`.

## Evidencia: no hay acceso por desplazamiento a `GAME.partners`

Búsqueda de todo uso de `GAME` como bytes o por dirección en `src/`, `include/` y `pc/` (`&GAME`, `GameState *`, `offsetof(GameState`,
`GAME_SAVE`, `memcpy`/`memset` de `GAME`): 
- `include/menus/memory_card.h:120` (`GAME_SAVE`) y `src/menus/memory_card/saves.c:405,492`: copia de la parte guardable, tratada arriba con import/export.
- `src/engine/game/state.c:10` (`HEAP.zero(&GAME, GAME_SAVE_SIZE)`): puesto a cero del espejo, seguido de import.
- `src/engine/game/stats.c:75` y `src/menus/item_shop/info.c:20` (`GameState *save = &GAME; save->partners[...]`): el mismo objeto `GAME`,
  no una copia; en el port pasan por `PC_PARTNER_VIA`.
- `pc/boot.c` (instantánea `savedGame` de `memcpy(&savedGame, &GAME, ...)` alrededor de la pasada de etapas): se agregó una
  copia del arreglo de compañeros.
- `pc/shim/ext_state.c` (`SK_BASE`, `P_BASE`, `DIG_END`), `mod_stages.c` y `memcardtest.c`: desplazamientos sobre un bloque ya
  guardado (no sobre `GAME`), y por eso sobre el espejo; usan `offsetof(GameState, partnersCard)`.
- Los scripts de evento (`src/engine/game/events.c`) indexan `GAME.partners[arg]` por C, no por desplazamiento (líneas 98-159, 485, 522).
No se encontró ningún acceso por desplazamiento o dirección que no pase por el espejo, así que no hizo falta el accesor con rama
(`p < 8 ? &GAME.partners[p] : ext`).

## Consecuencias

- Sin mods las partidas son idénticas byte a byte; `make VERSION=eu compare` sigue en 316 OK.
- P2 (`PC_PARTNER_ROW`), P3 (almacén y chunk para compañeros >= 8) y P4 (registro `partners-v1`) se apoyan en esto.
- Un acceso con índice fuera de `0..PC_PARTNER_COUNT-1` es indefinido, igual que antes (no hay comprobación: es el costo cero pedido).

## P2: el compañero no es la fila de su primera forma

El original usa el índice de compañero (0..7) como fila de `DIGIMON_DATA` de su primera forma (`game_state.h:81`: "52 de ellas; las 8
primeras son los compañeros"). Con compañeros nuevos eso deja de valer, así que `PC_PARTNER_ROW(p)` es la fila de la primera forma del
compañero `p` (identidad para 0..7; en el port, un arreglo `pcPartnerRows` junto a `pcPartners`: indexación simple, y -1 hasta que un mod
fija la de un compañero nuevo con `pcPartnerSetRow`). En PS1 el macro es `(p)` y el texto compilado no cambia.

Cada sitio se verificó leyendo su código (el índice viene de `GAME.funcs.getPartyMember`, `task->partner` o un bucle sobre compañeros,
y esos mismos valores entran a `getPartnerStats`/`computeStats`, que indexan compañeros):
- Convertidos (índice de compañero -> fila): `party.c:22-23` (bucle sobre los compañeros, nombre y stats iniciales); `growth.c:8-10` y
  `:77-79` (`getPartnerStats(partner)` y el crecimiento de esa fila); `setup.c:31-35` (`partner = getPartyMember(i)`) y `:115-119`;
  `partner_info.c:161,164,179` (`partner = task->partner`, `computeStats(partner)`); `normal_battle/menu.c:38-46` (`getPartyMember(0)`),
  `:887-901` (`getPartyMember(active)`), `:1177-1180`; `pc/battle/menu.c` (copias de esos cuatro); `stats.c:66-67`;
  `switch_in_menu.c:133-137`; `digivolve_menu.c:75-76`; `tech_menu.c:269-274`; `gym/result.c:74-75` (`getPartnerStats(result->partner)`);
  `digivolve_panel.c:58/73, 121/175, 260/272, 326/337, 729` (`id = getPartyMember(panel->member)`).
- Convertidos como bucle: `rolls.c:398-404` ("nunca cuando es uno de los 8 primeros Digimon") y `enemy_turn.c:358-365` (condición 8:
  el luchador del jugador es una de las primeras formas): recorren los compañeros y comparan con `PC_DIGIMON(PC_PARTNER_ROW(i)).id`
  (en PS1 el texto original, bajo `#ifdef DW3_PC`).
- NO convertidos porque ya son una fila (no un compañero): `switch_menu.c:24` (`next - 1`, fila de `pairPartner`), `normal_battle/menu.c:903`
  (fila de la forma de un blast), `growth.c:143,149` (`Evolution.digimon - 1`), `digimon.c` (`findDigimon`) y las facetas de
  `digimon_ext.c:948-973` (filas originales). `party_screen.c:402` recorre 8 compañeros hacia el búfer fijo `LabPartyScreen.partners[8]` (P6).

## P3

Identidad por nombre, guardado por nombre y almacenes por compañero sin tope: ADR-014. `pcPartnersImport` ya no devuelve la cuenta a 8 (`pcPartnersUnregister` sí).
