# ADR-014 — Compañeros por nombre: registro, sección `PTNR` y guardado `dw3save` formato 2

- **Estado:** ACCEPTED (dirección del dueño, 2026-11-05: sin compatibilidad hacia atrás obligatoria; corrección y diseño sin topes).
- Continúa ADR-013 (arreglo de compañeros). Tarea P3 del paquete betamon-pack.

## Qué campos guardan un índice de compañero (evidencia)

| Campo | Dónde | Qué guarda | Ancho |
|---|---|---|---|
| `GameState.party[3]` | `include/engine/game_state.h:496`; escrito en `src/engine/game/party.c:15-17,56`, `src/menus/digimon_lab/party_screen.c:339`, `src/menus/field_menu/sort_screen.c:376-377`; leído en `party.c:46` (`getPartyMember`), `stats.c:9` | índice de compañero (-1: sin miembro) | `s32`: alcanza |
| `Partner.unlocked` | `game_state.h:343`; `party.c:57`, `events.c:111` | el índice propio + 3 (0: bloqueado); `getPartyPartner` lo devuelve restando 3 (`stats.c:9`) | `s32`: alcanza. **Autorreferente**: debe reescribirse al cambiar el índice |
| `MemCardSave.partners[3]` (info de casilla, íconos de la pantalla de guardado) | `include/menus/memory_card.h:77`; lo llena `src/menus/memory_card/saves.c:505-506` (`unlocked` del miembro); lo lee `menu.c:178`, `info.c:82,175,180,188` | índice + 3 | `s32`: alcanza |

No guardan índice de compañero (probado): `GameState.partySet` (`game_state.h:525`, `party.c:59`, `events.c:93`) es el número del equipo inicial (`STARTER_PARTIES`), no un compañero;
`Partner.battleDigivolve` (`game_state.h:344`) y `PartnerStats.slots` son ids de Digimon/entradas (ya por nombre en EXTD); `BattleResult.partners` (`game_state.h:~398`) no se guarda; `LabPartyScreen.partners[8]` (`include/menus/digimon_lab.h:89`) es de sesión; las banderas de eventos son bits sueltos
(`events.c:536-630`); los argumentos `arg` de `checkPartner` (`events.c:90-160`) vienen de datos de escenario (P4). Máscara de equipo `WeaponData.partners` (`game_state.h:144`, `u8`, `1 << partner`): leída como
`(mask >> partner) & 1` (`helpers.c:88`, `equip.c:25`): para un compañero >= 8 da 0 (no hay lectura fuera de rango); P4 la ensancha.
Almacenes del port por compañero: `sXEntries`, `sXCount`, `sEvo`, `sEvoExtra` (ahora dinámicos, ver abajo) y el chunk `EXTP` (que llevaba `u8` de índice).

## Decisión

1. **Identidad.** Cada compañero tiene un nombre estable: `dw3:partner/<i>` (los 8 originales) o `<mod>:<llave>`. `pcPartnerRegister(nombre, filaPrimeraForma)` (`include/engine/partner_access.h`, `pc/shim/ext_state.c`) devuelve el índice
   (idempotente por nombre); `pcPartnerFind`, `pcPartnerName`, `pcPartnerSetIconBase`, `pcPartnersUnregister`. El índice es solo el orden de registro de la corrida. El registro persiste a través de cargas
   (`pcPartnersImport` ya no reduce la cuenta a 8: limpia los datos de los >= 8 y los deja registrados).
2. **Almacenes por compañero sin tope fijo.** `sXEntries/sXCount/sEvo/sEvoExtra` crecen con `pcPartnersGrow` (`storesEnsure`). Un compañero nuevo no tiene lista de evolución original: cuenta 0 hasta que su mod declare entradas
   (`pcEvolutionList` devuelve una lista vacía, nunca la de otro).
3. **Guardado: `dw3save` formato 2.** Si el port tiene más de 8 compañeros, la casilla lleva la sección **`PTNR`** (versión 1) y el archivo se escribe con versión de formato 2: una versión anterior lo rechaza ("newer format") en vez
   de leer un `party` con índices >= 8 como si fueran de 8. Sin compañeros nuevos no hay `PTNR` y el archivo es formato 1 y byte a byte lo de antes (la `EXTP` v1 sigue escribiéndose entonces).
   `PTNR`: cuenta, los chunks de nombres `EXTD`/`EXTT` incrustados (formas y técnicas nuevas dentro de los registros), el equipo (3 nombres) y, por compañero: nombre, el registro `Partner` completo (disposición del bloque `GAME`) y sus entradas
   guardadas más allá de la 44 (registros `EXTP`). Los originales se escriben desde el espejo del bloque; el resto, desde el arreglo del port.
   Se lee también el formato 1 (best-effort: no hay `PTNR`, los 8 vienen del bloque, `EXTP` v1 por índice de los 8). El bloque `GAME` sigue escribiendo los 8 originales: es el espejo de importación `.mcd` y la copia que lee el juego.
4. **Carga.** El host decodifica `PTNR` al leer el último sector de la casilla (`pcExtStateDecodePartners`): nombre -> índice actual (un nombre que ningún mod define se descarta con línea `[saves]`), registros a un área de preparación,
   formas/técnicas remapeadas por nombre, `unlocked` reescrito como índice + 3. `pcPartnersImport` (tras `GAME_SAVE = ...`, `saves.c:405`) copia el espejo y luego superpone lo preparado; `newGame` descarta lo preparado (`pcPartnersStageClear`).
   El orden "crecer primero" lo da el registro: los compañeros están registrados al arrancar, antes de cualquier carga, así que ningún puntero se invalida durante la carga.
5. **Equipo (`party`).** `pcExtStateRemapParty` reescribe `GAME.party` del bloque leído por nombre (`PTNR` guarda los 3 nombres) y lo deja válido: cada ranura es un compañero que existe o -1, miembros compactados al frente y, si no queda ninguno, el primer
   original desbloqueado del bloque. Prueba de que es válido para el código que lo lee: `party.c:15-17` inicia el equipo con `-1` en las ranuras 2 y 3, `lab.c:41-60` compacta miembros hacia delante y trata `< 0` como vacío,
   `getPartyMember` (`party.c:46`) devuelve -1 y los llamadores comprueban `>= 0` (`events.c:131-135`, `host_pad.c:1117`); el original nunca deja la ranura 0 vacía y por eso se rellena. Un bloque sin `PTNR` no se toca.
6. **Pantalla de guardado.** `MemCardSave.partners` se escribe con el compañero real (`PC_PARTNER_SAVED`: el bloque para <8, el arreglo del port para >=8, vacío para -1: antes se indexaba `partnersCard[member]` y un miembro >= 8 leía fuera del bloque).
   Sección **`ICON`**: los íconos de compañeros >= 8 de las tres casillas por nombre (y el original de reemplazo); la lectura reescribe el valor y recalcula el checksum de la info (`MemCardFile.checksum`, `saves.c:~498`). Un compañero cuyo mod se fue muestra el arte de su reemplazo.
7. **Arte por compañero hasta P6/P7.** `PC_PARTNER_ART(p)` (`pcPartnerArt`): `p` para los 8; para uno nuevo, `iconBase` (fila de su primera forma si es 0..7, si no 0; `pcPartnerSetIconBase`). Envuelve cada lectura de tablas `[8]`: `STGMCARD_partnerIcons`/`partnerAnims`
   (`menu.c:178`, `info.c:180,188`), `STSTATUS_data.partnerAnims` (`status_screen.c`, `card_screen.c`, `demo_screen.c`, `tech_screen.c`, `sort_screen.c`, `item_screen.c`), `STGTRAIN_waitAnims` (`gym/screen.c`), `STGDGLAB_data.anim` (`digimon_lab/menu.c`, `party_screen.c`),
   `STDGNAME_menuAnims` (`partner_name/menu.c`), `STFGTREP_animations` (`report/partner.c`). `EQUIP_SETS`/`EQUIP_SET_BONUSES` (`stats.c:137`): sin conjunto para un compañero nuevo. `STFGTREP_evolutions[partner]` (`growth.c:141`): `PC_EVOLUTION_LIST` ya no evalúa la tabla `[8]` para >= 8.
   PS1: los macros expanden a `(p)`, `compare` 316 OK.
8. **Inalcanzable hasta P4 (cerrado en ADR-015):** `STARTER_PARTIES` / `STPLNMET_partnerChoices` (`choice.c`, `confirm.c`: solo originales), el arreglo `LabPartyScreen.partners[8]` y el bucle de 8 de `party_screen.c:402` (un compañero >= 8 desbloqueado no aparece en la reserva del laboratorio),
   la máscara `u8` de equipo, el texto/nombre del HUD y la ficha inicial de un compañero ausente del guardado (queda a cero; P4 debe inicializarla como `initNewGameData`, `party.c:22-37`).

## Consecuencias

- Quitar el mod deja la partida sana: el compañero y sus entradas se descartan con línea `[saves]`, el equipo se compacta, el ícono usa el reemplazo; volver a instalar el mod no recupera los datos descartados (se pierden al guardar).
- Las partidas de 1.1.0 (formato 1) se leen igual; las nuevas con compañeros extra no las abre 1.1.0 (formato 2).
- Pruebas: `pc/extstatetest.c` (`testPartnerNames`), `pc/memcardtest.c` (sección 12), `pc/savetest.c` (formato 2).
