# Inventario de las tablas de datos del juego (EU)

Solo documentación: ninguna cifra de las tablas ni cadena del
juego está copiada aquí (ADR-001); se nombran símbolos, tipos, campos y conteos de filas. "Hecho" es lo comprobado en
el C de `src/` o en el disco; "hipótesis" está marcado como tal. Los conteos de filas salen de contar las
inicializaciones de `src/` (rama `VERSION_EU`) o, para archivos del disco, de `tools/file_names.py` (tamaño exacto
en bytes dividido por el tamaño del registro).

## 1. Dónde viven las tablas y qué significa para el port

Hechos:

- El juego original guarda las tablas como datos C inicializados (`.data`) del ejecutable `SLES_039.36` o de un
  overlay `.PRO`, o las lee de un archivo del disco por número (`FILE_CACHE.load`). Los offsets de `.data` por
  módulo están en `config/eu/*.yaml`: `main.yaml:416-417` (`game/digimon`, `game/items`), `stfgtrep.yaml:60`
  (`data/stfgtrep`), `stitshop.yaml:66`, `stgtrain.yaml:68-70`, `fieldstg.yaml:97`.
- **El port enlaza el C de cada modo dentro de su binario** (`pc/README.md:1014-1028`: "los modos se enlazan, no se
  cargan"; los `.PRO` de modos no se leen). Por eso, en el port, todas las tablas "del ejecutable" y "de overlay"
  son arreglos C del host, inicializados desde `src/`, con los mismos nombres de símbolo y en memoria escribible
  (no son `const`). Los archivos del disco sí se leen del disco del usuario por `cdRead` (`pc/shim/host_cd.c`),
  donde ya se aplica la capa de reemplazo (`pfsSetLayers(tdOverride, ...)`, `host_cd.c:245`; mods por archivo,
  ADR-005 §6 y `docs/modding/`).
- Reinicio de overlays: el port restaura el `.data` de cada modo al entrar (ámbitos de `pc/shim/state_scope.h`);
  lo que vive en el ejecutable principal se conserva. El hallazgo R1 ya
  lista "cualquier parche en datos de un overlay" entre lo que hay que reaplicar al reiniciar.

Consecuencia para el punto de aplicación (resumen; detalle en §5):

| Dónde vive la tabla | Cómo se parcha |
|---|---|
| Ejecutable (`SLES`): `DIGIMON_DATA`, `TECHS`, `ITEMS` y registros | Parche en memoria una vez al arrancar; nada lo reinicia |
| Overlay (`STFGTREP`, `FIELDSTG`, `STITSHOP`, `STGTRAIN`, `WSTAG*`) | Parche en memoria con gancho `onEnter` del ámbito del modo, porque el ámbito restaura el snapshot al entrar |
| Archivo del disco (tabla de batalla, oponentes de cartas) | Reemplazo del archivo o gancho post-lectura en `cdRead` |

Hipótesis (a confirmar en T2): el repo público `dw3_port` no podrá llevar los arreglos de `src/` porque contienen datos
retail (ADR-001); tendría que leerlos del `SLES`/`.PRO` del disco del usuario con los offsets de `config/eu/*.yaml`.
El punto de aplicación (parchear el arreglo ya inicializado) es el mismo en los dos casos.

## 2. Cuadro por familia

| Familia | Símbolo / tipo | Vive en | Filas | Cambia en ejecución |
|---|---|---|---|---|
| Digimon base | `DIGIMON_DATA` / `DigimonData` | ejecutable | 55 (52 Digimon + 3 de relleno) | No |
| Técnicas | `TECHS` / `TechData` | ejecutable | 443 | No, salvo la ranura 439 (§3.2) |
| Objetos | `ITEMS` / `ItemInfo` | ejecutable | 402 | No |
| Armas / armaduras / accesorios | `WEAPON_DATA`, `ARMOR_DATA`, `ACCESSORY_DATA` | ejecutable | 123 / 77 / 68 | **Sí** (`weapon_data`, `armor_data`, `accessory_data`, E5a): referencia + parche, llave = id de objeto (92..214 / 215..291 / 292..359) |
| Efecto de objetos usables | `ITEM_EFFECT_*` / `ItemEffect` | ejecutable | 83 (un símbolo por objeto) | **Sí** (`item_effects`, E5a): una fila por registro, llave = id de objeto (43..394) |
| Listas por tipo de objeto | `KEY_ITEM_IDS`, `USABLE_ITEM_IDS`, `WEAPON_IDS`, `ARMOR_IDS`, `ACCESSORY_IDS` | ejecutable | 52 / 84 / 124 / 78 / 69 | No, a propósito: derivan de `ItemInfo.kind` y los menús las indexan; los objetos nuevos ya entran solos (`item_ext.c`) |
| Recompensas de batalla | `STFGTREP_rewards` / `BattleReward` | overlay STFGTREP | 335 | No |
| Crecimiento y nivel | `STFGTREP_levelExp`, `_vitalCuts`, `_vitalRandom`, `_statGains`, `_resistGains` | overlay STFGTREP | 4, 4, 9, 6x9, 8 | **Sí** (`level_exp`, `vital_cuts`, `vital_random`, `stat_gains`, `resist_gains`, E5b): llave = primer nivel del tramo (1, 5, 20, 40 y 60, 80 en `stat_gains`) o ranura 0..n; se aplican al abrir el reporte |
| Evoluciones | `STFGTREP_evolutionLists` / `Evolution` | overlay STFGTREP | 8 socios x 44 | **Sí** (`evolutions`, E5b): llave = `socio * 100 + entrada` (352 filas); las entradas 44+ de los `digimon/<llave>/digimon.json` no son filas |
| Formas de ráfaga | `DigimonData.blastForms[5]` | ejecutable | 52 filas x 5 | **Sí** (`blast_forms`, E5b): llave = id del Digimon; solo importan las 8 filas de socio. Valores de 16 bits (U9: `PcDigiLinks` en `ext_state`, `PC_DIGIMON_BLAST`) |
| Encuentros | `FIELDSTG_encounters` / `Encounter`; `FIELDSTG_enemies` / `BattleEnemy` | overlay FIELDSTG | 335; 523 (EU) | **Sí** (`field_encounters`, `field_enemies`, E5c; filas nuevas `"<mod>:<nombre>"` con recompensas, T5b): llave = número de fila; el encuentro nombra las filas de enemigo por número (`enemy1`..`enemy3`); se aplican al entrar al campo |
| Tasas de encuentro | `FIELDSTG_battleRates` | overlay FIELDSTG | 6 | **Sí** (`battle_rates`, E5c): llave = índice de tasa 0-5 (el `count` de la lista de batallas del escenario) |
| Lugares y áreas de batalla | `stageBattles` / `FieldBattles` -> `BattleList` -> `AreaBattle` | cada `WSTAGnnn.PRO` | 214 escenarios lo definen (conteo de `rg`) | No todavía (E5c, pendiente: ver §3.5) |
| Enemigos (stats, botín, IA) | tabla de batalla / `BattleTableEntry` | archivo del disco 463 (`FILE_BATTLE_TABLE` 0x1CF) | 194 registros de 0x46 bytes (13580 / 70, exacto) | No (copia en caché de archivos) |
| Tiendas de objetos | `STITSHOP_shops` / `ShopList`, `STITSHOP_stock0..30` | overlay STITSHOP | 31 tiendas | No |
| Tienda de cartas | `STCRDSHP_prices` / `CardPrice`; `STCRDSHP_stocks` / `CardShopStock`; `STCRDSHP_packs` / `CardPack` | overlay STCRDSHP | 44; 24; 35 sobres x 6 ranuras de 16 cartas (EU, `nm -S`) | **Sí** (`card_prices`, `card_shop_stock`, `card_pack_slots`, E5d): llaves = carta, tienda, `sobre * 10 + ranura`; se aplican al entrar a la tienda |
| Cartas: efectos | `CARDGAME_cardEffects` / `CardEffect` | overlay CARDGAME (símbolo por dirección, sin arreglo C) | los 60 primeros | **Sí** (`card_effects`, E5d): llave = id de carta 1-60; `step1`..`step40` del guion; se aplica al entrar al duelo |
| Cartas nuevas (U8b) | `CardImage` (ficha + TIM de la carta, `CARDPAK0-4`), `CardEffect` (`CARDGAME_cardEffects`) | archivo del disco / ejecutable | ids 317 a 1023 (707) | **Sí**, como contenido (`content/cards.json`, `card_ext.c`): la ficha y el efecto de una carta nueva son copias de los de su base con cambios; los conteos y `cardsSeen` de las nuevas viven en `ext_state` por nombre (sección `EXTK`). `GAME.cards`/`cardsSeen` pasan por `PC_CARD_COUNT`/`PC_CARD_SEEN` (`cardgame/card_access.h`). Las tablas de precios, tiendas, sobres y rivales siguen aceptando solo los ids originales |
| Cartas: oponentes | archivo `CARD_NPC.BIN` (0x7A4) / `CardOpponent` | archivo del disco 1956 | 170 (35360 / 208 exacto, con `CardOpponent` de 0xD0 bytes; sin cabecera: las 170 filas decodificadas del disco encajan) | **Sí** (`card_opponents`, U8a): llave = argumento del modo menos uno; parche del búfer tras cada lectura del archivo |
| Cartas: premios y mazo inicial | `CARDGAME_prizeItems`, `CARDGAME_defaultDeck` | overlay CARDGAME | 36 / 40 de 80 valores (EU, `nm -S`) | **Sí** (`prize_items`, `default_deck`, E5d): llave = número de premio / posición; se aplican al entrar al duelo |
| Gimnasio | `STGTRAIN_gymTrainings`, `_statGains`, `_statLosses`, `_resistGains{Low,Mid,High}`, `_maxHpMpGains`, `_intensityCosts` | overlay STGTRAIN | 14x16; 12; 3; 3x9; 9; 3 (EU, `nm -S`) | **Sí** (`gym_trainings`, `gym_stat_gains`, `gym_stat_losses`, `gym_resist_gains`, `gym_hp_mp_gains`, `gym_intensity_costs`, E5e): llaves = `índice del gimnasio * 100 + ranura`, columna, intensidad, `tabla * 10 + ranura`, ranura, intensidad; se aplican al entrar al gimnasio. No se expone `_resistGainTables` (punteros) |
| Posadas | `INNS` / `InnInfo` (`engine/menu/inn.c:82`) | ejecutable | 21 + cierre (0xB0 = 22 x 8, EU, `nm -S`) | **Sí** (`inns`, E5e): llave = modo de juego de la posada; se aplica una vez al arrancar |
| Condiciones de eventos | `SPECIAL_CONDITIONS`, `MONEY_REQUIRED`, `MONEY_GAINS`, `MONEY_LOSSES`, `PROGRESS_RANGES`, `PARTY_STAT_THRESHOLDS` (`engine/game/events.c`) | ejecutable | 120 + cierre (3 bytes), 10, 8, 10, 34 x 2, 15 | **Sí** (`special_conditions`, `money_required`, `money_gains`, `money_losses`, `progress_ranges`, `party_stat_thresholds`, S3): llaves = el id de la fila (u8) o el índice; se aplican una vez al arrancar. Un mod puede **agregar** filas de condición por nombre (`"<mod>:<nombre>"`, ids `0x100`-`0x1FF`, en una tabla del puerto que `checkSpecialCondition` consulta con `#ifdef DW3_PC`) |

Estado de partida (no son tablas, pero los datos de arriba terminan ahí y van a la tarjeta): `GAME`
(`include/engine/game_state.h:470`): `money`, `items[0x193]`, `equippedItems[0x193]`, `cards[0x13D]`, `partners[8]`
con `PartnerStats` y `PartnerEntry[44]` (`:299`, `:243`). `GAME_SAVE_SIZE` fija los primeros bytes que se guardan
(`:569`).

## 3. Detalle por familia

### 3.1 Digimon base, socios y evoluciones

- `DigimonData` (`include/engine/game_state.h:82-103`): `id`; `battleStats[6]`; `resistances[7]`; `skills[7]` ([1]-[6]
  se aprenden en `skillLevels`); `pairTech` y `pairPartner`; `statusResists[5]`; `skillLevels[6]`; `knownLevels[5]`;
  `expLevel` (nivel tras el cual la experiencia crece más rápido), `expRate` (experiencia por nivel, en décimos);
  `hp`, `mp`, `hpGrowth`, `mpGrowth`; `statGrowth[6]`, `resistGrowth[7]`; `blastForms[5]` (forma de la "ráfaga" por
  nivel); `nameId` (cadena del archivo de texto `0x4F`); `family`.
- Qué tabla usa cada campo de crecimiento (editables en `digimon/d<nnn>/digimon.json`, `partner`): `statGrowth[i]` (0..5) es la columna de
  `STFGTREP_statGains[tramo de nivel]` (`growth.c:56-57`, columna `statGrowth + 0..4`); `resistGrowth[i]` (1..5) elige en batalla
  `STFGTREP_resistGains[resistGrowth + 0..3]` (`growth.c:65-66`, hasta el nivel 40) y en el gimnasio
  `STGTRAIN_resistGainTables[(resistGrowth - 1) * 3 + tramo]` (`result.c:78-82`; tramo = valor <100, <300, más). Los
  arreglos que apunta (`result.c:817-821`; Low/Mid/High = `STGTRAIN_resistGainsLow/Mid/High`) por crecimiento, tramos <100/<300/más:
  1 y 2 = Low/Mid/Mid, 3 = Mid/Mid/Mid, 4 y 5 = High/High/Mid. `knownLevels[i]` no usa tabla: `STFGTREP_learnSkill` (`growth.c:266`)
  marca conocida `skills[i]` cuando el nivel de la entrada lo alcanza. Rangos del port: `resistGrowth` 1..5 (0 leería antes de la
  tabla), `statGrowth` 0..5 (el 5 del disco ya lee una posición después de la fila, hallazgo del original), `knownLevels` 1..99.
- Definición `src/engine/game/digimon.c:7` (55 filas; el comentario de `:6` dice 52 Digimon y tres de relleno; el
  buscador solo recorre 52, `digimon.c:125-131`). Los primeros 8 son los socios (`party.c:22`).
- Lectores: partida nueva copia `hp`, `mp`, `battleStats`, `resistances` de los 8 socios al `GAME`
  (`src/engine/game/party.c:22-38`); batalla (`battle/battle_mode/stats.c:67,73,138`, `battle/normal_battle/menu.c:
  41,901-903,1180`, `battle/normal_battle/setup.c:119`); informe (`battle/report/growth.c:10,79,197,242,264,306`);
  menú de campo (`menus/field_menu/digivolve_panel.c:62-729`); laboratorio (`menus/digimon_lab/*`). Acceso por id:
  `getDigimon` (`digimon.c:134`).
- Nadie escribe en la tabla (búsqueda de asignaciones sin resultados): es constante. Lo que cambia se copia a
  `GAME.partners[].info` y a `PartnerEntry`.
- Evoluciones (el socio "aprende" un Digimon al cumplir requisitos): `STFGTREP_evolutionLists[8][44]` de `Evolution`
  (`include/battle/report.h:31-38`: `digimon` (índice + 1), `needs[2]` (Digimon y nivel), `stat` (1-6 stats, 7 nivel,
  8-14 resistencias), `value`). Datos `src/battle/report/data/stfgtrep.c:684-1052`, índice por socio `:1054`. Lector
  `STFGTREP_learnDigimon` (`battle/report/growth.c:131-192`), llamado desde `battle/report/partner.c:303`.
- Otras "evoluciones" son datos de `DigimonData`: `blastForms` (`battle/normal_battle/menu.c:901-903`), `pairTech` /
  `pairPartner` (`battle/battle_mode/switch_menu.c:18-24`).

### 3.2 Técnicas

- `TechData` (`include/engine/game_state.h:106-123`): `mp` (costo), `power`, `icon`, `kind` (3 = cura), `accuracy`,
  `element`, `elementPower`, `family`, `effect` (`TECH_EFFECT_*`), `effectChance`, `effectPower`, y datos de guion
  (`scriptStage`, `scriptEffect`, `scriptSound`, `script`), `hitCount`.
- Definición `src/engine/game/items.c:906` (443 filas, desde 1: la técnica n es `TECHS[n - 1]`).
- Lectores: `battle/battle_mode/tech_effects.c:32-200`, `battle/normal_battle/techs.c:12,167`,
  `battle/battle_mode/boosts.c:80`, `battle/battle_mode/enemy_turn.c:125`, `menus/field_menu/tech_screen.c:72,110`.
  Los nombres de técnica son texto (archivo `0xA3`, `TEXT_SKILL_NAMES`).
- Constante, con una excepción: `battle/normal_battle/menu.c:1256-1260` usa `TECHS[439]` como ranura de trabajo del
  combate final (copia `dst[3]` y le pega efecto/elemento de la técnica del jugador). Un parche no debe pisar esa
  ranura ni asumir que las filas 440-443 son contenido libre (hecho: se escribe; el uso exacto es hipótesis).
- Reglas en código (no tabla): la fórmula de daño, aciertos y críticos están en `battle/battle_mode/damage.c:94,125`
  y `rolls.c:9-325`.

### 3.3 Objetos, equipamiento y efectos

- `ItemInfo` (`game_state.h:221-227`): `data` (puntero al registro), `price` (compra), `sellPrice` (0 = no vendible),
  `kind` (lista), `type` (2-14 armas, 15-20 armaduras, 21-24 accesorios, 25-28 usables, 29 clave).
  `ITEMS` en `items.c:481` (402 filas, objeto n = `ITEMS[n - 1]`, `getItem`, `items.c:15`).
- Registros: `WeaponData` (`game_state.h:140`: `charisma`, `kind`, `group`, `partners` (máscara), `amounts[2]`, `atk`,
  `stats[2]`, `accuracy`, `hitEffect` (nada lo lee; `FIGHTSTG_computeStats` prueba ids de objeto), `statusChance`,
  `statusPower`, `family`), `ArmorData` (`:158`: ..., `def`, `evasion`), `AccessoryData` (`:173`: `amount`, `stat`) y
  `ItemEffect` (`:196`: `flags`, `kind` 1 cura / 2-17 sube stat, `amount`). Definidos en `items.c:63,189,269` y
  `ITEM_EFFECT_*` en `items.c:396-478`. El campo `ItemInfo.data` apunta a esos arreglos (relocaciones: ver §5).
- Lectores: stats con equipo (`engine/game/stats.c:65-119`; `battle/battle_mode/stats.c:120-189`), uso de objeto en
  combate (`battle/battle_mode/item.c:263-699`, `item_menu.c:122`), en el menú (`menus/field_menu/item_screen.c:181`,
  `item_list.c:205,272`), tienda (§3.6).
- Los efectos de combate de algunos usables no están en `ItemEffect`, sino en código por id de objeto
  (`battle/battle_mode/item.c:121-147`, `FIGHTSTG_itemScripts`): reglas en código, ver §4.
- Constantes. Estado: conteos de bolsa en `GAME.items[0x193]` (ids hasta `0x192`) y equipo en `PartnerStats.equip[6]`.

### 3.4 Recompensas de batalla y botín

- `BattleReward` (`include/battle/report.h:24-28`): `digimonExp` (compartida por los Digimon usados), `exp`
  (compartida por socios que pelearon), `money`. `STFGTREP_rewards` en `data/stfgtrep.c:345` (rama EU), 335 filas,
  indexada por el índice de encuentro (`BATTLE_SETUP.battle = encounter`, `field.c:984`; `BATTLE_RESULT.battle =
  BATTLE_SETUP.battle`, `normal_battle/menu.c:483`). `FIELDSTG_encounters` también tiene 335 filas: una fila de
  recompensa por encuentro (hecho para el índice; que sean 1 a 1 por diseño es hipótesis).
- Dinero: `report.c:115-133` suma `money` de la fila, +1/5 con el objeto de id `0x142` en `equip[4]`/`[5]` del miembro
  `BATTLE_RESULT.member`, y topa en `MONEY_MAX` (9999999).
- EXP de socio: `report.c:325-336` reparte `exp` entre quienes pelearon (todo, una fracción para dos, un tercio para
  tres); `partner.c:492-504` suma +1/5 una vez con el objeto de id `0x141`; `growth.c:77-124` (`STFGTREP_addExp`) sube
  nivel mientras `(n^3 + 5n - 6) * expRate / 10 + STFGTREP_levelExp[tier] < exp`, con tope de nivel 99 y de exp
  999999.
- EXP de Digimon: `partner.c:203-206` -> `STFGTREP_getDigimonExp` (`growth.c:278-318`: menos al subir el nivel del
  socio, repartida por los usados, topes por `expLevel`) -> `STFGTREP_addDigimonExp` (`growth.c:196-230`).
- Crecimiento al subir: `STFGTREP_raiseStats` (`growth.c:8-73`) usa `DigimonData.hpGrowth/mpGrowth/statGrowth/
  resistGrowth` y las tablas `STFGTREP_statGains` etc. con `RANDOM.next()`.
- Objeto ganado: `normal_battle/menu.c:476-510` elige al azar un enemigo con objeto; sale si `itemChance + 1 >
  RANDOM.next() % 1024` (`BattleTableEntry.item/itemChance`, `battle.h:58-69`), o el `BATTLE_SETUP.prize` forzado
  de los luchadores con premio (`field.c:708-757` `PICK_PRIZE`, `:993-1006`, ids `FIELD_PRIZE_FIGHTERS`
  `field_mode.h:669`). Entrega: `report.c:137-151` (`GAME.items[...]++`, tope 99). Robo: `rolls.c:323-356`.
- Constantes todas; el estado resultante va a `GAME.money`, `GAME.items`, `PartnerStats.exp/stats`.

### 3.5 Encuentros y jefes

- `Encounter` (`include/field/field_mode.h:737-747`): `enemies[3]` (punteros a `BattleEnemy`), `ambushChance`, `kind`,
  `blocks[12]`. `BattleEnemy` (`game_state.h:349-355`): `fighter` (id), `level`, `hp`, `mp`, `strength` (stats y
  poder en 16avos). Datos `data/fieldstg.c:22` (`FIELDSTG_enemies`, ~523 filas) y `:948` (`FIELDSTG_encounters`, 335).
- Quién lo usa: `FIELDSTG_startEncounter` (`field/field_mode/field.c:968-1006`) copia enemigos, bloqueos y
  `ambushChance` a `BATTLE_SETUP`.
- Dónde ocurren: `FieldBattles` -> `BattleList` (`count` = índice de `FIELDSTG_battleRates`, 8 `AreaBattle`) ->
  `AreaBattle` (`encounter`, `stage`, `music`), `include/field/field_map.h:407-431`. Cada escenario define su
  `stageBattles` en su `WSTAGnnn.PRO` (por ejemplo `src/field/stages/undersea_base/wstag805.c:680-732`); 214 lo
  definen. Elección: `FIELDSTG_startAreaBattle` (`battle.c:18-29`, `RANDOM.next() & 7`); frecuencia:
  `FIELDSTG_countBattleSteps` (`battle.c:35-60`) con `FIELDSTG_battleRates` (`data/fieldstg.c:3578`) y
  `FIELDSTG_rollBattleSteps` (`battle.c:6`).
- Jefes y batallas de evento: son encuentros del mismo arreglo, lanzados por los guiones de los escenarios
  (`EVENT_BATTLE(id)`, `game_state.h:71`; `FIELDSTG_startEventBattle`, `field/field_mode/battle.c:61`).
- Los stats, técnicas, IA y botín de cada enemigo vienen de la tabla de batalla (disco 463, `BattleTableEntry`
  `battle.h:58-69`: `id`, `item`, `itemChance`, `nameId`, `techs[3]`, `stats[5]`, `resist[12]`, `family`,
  `actions[4]`, `counter`), leída por `FIGHTSTG_getBattleTableEntry` (`battle/battle_mode/battle_table.c:20`) con
  `FILE_CACHE.load`. Nombre de archivo: `PRO/SDIGIEDT.PRO` (`tools/file_names.py`; el nombre no coincide con el
  contenido, `docs/formats.md:121-123`). Puntero de caché: no se escribe.
- Los modelos y movimientos de los combatientes están en `FILE_FIGHTERS` (0x1CC, `SMDLDATA.PRO`, `FighterInfo`,
  `model.h:216-250`), sin datos de reglas.

**Tablas de `tables/` del campo (E5c).** Las tres son datos del overlay FIELDSTG, que el ámbito `fieldstg` restaura en cada
entrada al campo; el parche va después, en el `onEnter` (`gameTablesApplyField`, `pc/shim/game_tables.c`). Sin parche no se
escribe nada. Se aplican en este orden: `field_enemies`, `field_encounters`, `battle_rates`; después los emplazamientos
`content/encounters.json` (que van encima de los huecos ya parchados).

- `field_enemies` (523 filas, llave = número de fila): `fighter` (id de la tabla de batalla; también `"dw3:digimon/<id>"` o
  `"<mod>:<clave>"`; una forma de socio se rechaza: no tiene fila en la tabla de batalla), `level` 1-99, `hp` 1-32767, `mp`
  0-32767, `strength` 1-500. La fila 0 es el hueco vacío que comparten los encuentros y no cambia; una fila con Digimon no
  puede vaciarse (`fighter` 0): los encuentros que la usan pondrían un luchador 0. Rangos: `level` y `hp` salen de
  `setup.c:48` (`hp` 0 deja al enemigo derrotado) y `stats.c:12`; `strength` escala `stat * strength / 16` y se guarda en 16 bits:
  el stat más alto de los enemigos del disco es 930, que 500 deja bajo 32767. El JSON de referencia trae `labels` con el nombre
  del Digimon de cada fila (se lee del disco del usuario; nunca se sube).
- `field_encounters` (335 filas, llave = índice): `enemy1`..`enemy3` (fila de `field_enemies`, 0 = vacío), `ambushChance`
  0-255, `kind` 0-255 (el disco usa 0-5) y los 12 bloqueos (`poison`, `paralysis`, `confusion`, `sleep`, `knockOut`, `drain`,
  `block6` (nada lo lee), `steal`, `lowerAttack`, `lowerDefense`, `lowerSpeed`, `runAway`), 0 o 1 (el juego solo los
  compara con 0). Una fila del juego guarda tres punteros a `FIELDSTG_enemies`; la tabla los muestra como número de fila y el
  puerto, tras el parche, vuelve a escribirlos como punteros. Se rechaza la fila entera si el primer enemigo queda vacío (el
  modelo y la barra de la batalla salen de él) y el encuentro 0 (el "ninguno" de las listas de batalla) no cambia. `labels` trae
  los enemigos de cada encuentro.
  **Filas nuevas (T5b).** Una fila de `rows` con llave `"<mod>:<nombre>"` (la carpeta del mod propio) agrega un encuentro: `enemies` (1 a 3 objetos `enemy`, `level`, `hp`, `mp`,
  `strength`, todos obligatorios; `enemy` es un id, `"dw3:digimon/<id>"` o `"<mod>:<clave>"`), `ambushChance`, `kind` (1 por defecto), los 12 bloqueos y `rewards` (`money`, `exp`,
  `digimonExp`, la fila de `STFGTREP_rewards` de esa batalla). Lo lee `pc/shim/field_ext.c` del mismo archivo (la tabla salta esas llaves, `TmTable.ownKey`); el puerto las guarda en
  un almacén propio (`sExtEncounter`, `sExtEnemy`, `sExtReward` de `pc/boot.c`) y el juego las lee por accesores `#ifdef DW3_PC`: `FIELDSTG_ENCOUNTER(n)`
  (`field_mode.h`, usado en `FIELDSTG_startEncounter`) y `STFGTREP_REWARD(n)` (`report.h`, usado en `report.c:120-122,328-334` y `partner.c:206`); en la PS1 son
  `FIELDSTG_encounters[n]` y `STFGTREP_rewards[n]` (`make compare`: 316 OK). Los números siguen a los 335 del disco (335, 336... en el orden de los mods y del archivo; hasta 1024 en
  total). `BATTLE_SETUP.battle` es `s32` y `BATTLE_RESULT.battle` `s16`: caben. Los lectores por número (`normal_battle/menu.c:15-21`: 0x143, 0xB, 0x1E, 0x144; `enemy_turn.c:397`,
  condición 12) comparan con números del disco y nunca coinciden con una fila nueva. El botín (`battle_loot`) es del enemigo, no del encuentro. Una fila inválida se registra y no existe.
- `battle_rates` (6 filas, llave = índice 0-5): `rate` 0-2000, lo que cada paso resta a la cuenta hasta la próxima batalla. La
  cuenta se sortea entre 0 y 1279 (`FIELDSTG_rollBattleSteps`), así que 2000 (la tasa 0 del disco) es batalla en el paso
  siguiente y cualquier valor sobre 1279 hace lo mismo; 0 nunca la baja (sin batallas al azar con esa tasa).
- **Pendiente: `stageBattles`.** Cada `WSTAGnnn.PRO` define el suyo; en el binario del host son 214 símbolos
  `WSTAG<nnn>_stageBattles` (el de `wstag400` es `stageBattles0`/`1`), sin tamaño en C (`extern FieldBattles stageBattles[]`),
  y por cada área cuelgan su `BattleList` y 8 `AreaBattle` (`encounter`, `stage`, `music`) por punteros distintos en cada
  escenario. Hace falta una lista generada de las 214 (cantidad de lugares por escenario) y decidir la llave (lugar
  `GAME.place` + área + posición) y el gancho (el `onEnter` del ámbito del escenario, o el puntero `FIELDSTG_state.battles` que
  su `setup` ya fijó). Es bastante más grande que las tres tablas de arriba: no se hizo en E5c.

### 3.6 Tiendas

- Objetos: `STITSHOP_shops[31]` (`ShopList`: `count`, `items` -> `STITSHOP_stock0..30`, listas de ids de objeto
  terminadas en 0), `src/menus/item_shop/data/stitshop.c:38-225`; nombres `STITSHOP_shopNames` (`:27`). La tienda
  abierta es el argumento del modo (`ItemShop.shop`, `item_shop.h:29`); lector `menus/item_shop/equip.c:8` y la lista
  (`item_list.c`).
- Precios: no están en la tienda, son `ItemInfo.price` / `sellPrice` (`trade.c:37,155,241`; venta `trade.c:920,947`,
  `item_list.c:338`, `info.c:411`). Cambiar el precio cambia todas las tiendas a la vez.
- Posadas: `INNS[].price` (`engine/menu/inn.c:82,127-178` (campo `price`)).
- Tienda de cartas: `STCRDSHP_prices` (`card_packs/shop.c:473`, lector `:426-428`) y `STCRDSHP_packs`
  (`card_packs/pack_open.c:1041`, 6 ranuras de 16 cartas, `pack_open.c:605`, lector `:396-403`).

### 3.7 Juego de cartas (breve)

- Efectos de carta: `CARDGAME_cardEffects` (`CardEffect`, `card_battle/battle.h:53-59`: `condition`, `playCondition`,
  `message`, `target`, `script[40]`), declarada (`battle.h:301`) pero sin arreglo C: está en
  `config/eu/symbols_cardgame.txt:315` (dirección fija); lector `card_battle/effect_script.c:19-30`. Los datos de
  oponentes están en `CARD_NPC.BIN` (`CardOpponent`, `battle.h:217`, `battle.c:6-14`: mazo de 40, cartas de
  contraataque, nivel, índice de premio). Premios y mazo inicial: `CARDGAME_prizeItems` y `CARDGAME_defaultDeck`
  (`data/cardgame_3.c:134,141`). `CARDDATA.BIN` (0x25D) es hoja de sprites, no estadísticas (`docs/formats.md:246-249`).
- Sin inspección más profunda (la estadística de cada carta no se ubicó): hipótesis, a revisar en T2 si hace falta.

**Tablas de cartas (E5d, `shim/game_tables.c`).** Tamaños de `nm -S` del binario del puerto (EU): `STCRDSHP_prices` 0xB4 (44 filas + el cierre `{0, 1}`),
`STCRDSHP_stocks` 0x190 (24 + `{-1}`, el puntero de 8 bytes en el host), `STCRDSHP_packs` 0x7E0 (35 sobres + `{0}`), `CARDGAME_cardEffects` 0xA50 (60 x 44),
`CARDGAME_prizeItems` 0x48 (36 `s16`), `CARDGAME_defaultDeck` 0xA0 (80 `u16`). Los tres de la tienda son datos del overlay STCRDSHP y los tres del duelo del overlay
CARDGAME (ambos rangos dentro de las secciones de ámbito, `check_scopes`): se registran al arrancar y se parchan en el `onEnter` del ámbito `stcrdshp` / `cardgame`.
- `card_prices` (44 filas, llave = id de carta, `price` 0-32767): `STCRDSHP_getPrice` busca la primera fila con la carta (`shop.c:426-428`) y devuelve 1 si no hay;
  `price` es `s16` y se resta del dinero (`buy.c:279-329`). El `card` de la fila es la llave, no un campo. **Cartas de mods (U8c):** el precio de una carta de un mod (id >= 317) no es una
  fila (las 44 no crecen): es `CardRec.price`, el campo `price` de su `card.json` (0-32767, 1 si no lo trae, igual que una carta del disco sin fila), y `STCRDSHP_getPrice` lo pide a
  `pcCardPrice` (`#ifdef DW3_PC`, `card_access.h`). Una fila de `card_prices` con la llave de una carta de mod se rechaza como cualquier llave sin fila.
- `card_shop_stock` (24 filas, llave = el `shop` de la fila, 49-67 y 70-74): la lista `cards` (ids 1-314 del disco, o `"<mod>:<nombre>"` de una carta de un mod: `CARD_PACK_IDS` es `PC_CARD_LIST_MAX` en PC, U8c, para que `card_grid.c:20,78` las dibuje) termina en 0 y
  `getStock` cuenta hasta el 0 (`shop.c:406-419`); la fila no tiene cuenta que guardar (`TM_NO_COUNT`, extensión del motor). La compra pagina de a seis (`buy.c:35,228,445`) y
  puede leer hasta el final de la última página: el búfer del motor es de ceros y la lista llega a 29 (30 ranuras, todas en página). Un número de tienda sin fila abre la primera.
- `card_pack_slots` (210 filas = 35 sobres x 6 ranuras, llave = `item * 10 + ranura`; `card1`..`card16`, 1-314 o `"<mod>:<nombre>"`): el sobre sortea `RANDOM.next() % 16` por ranura y suma la carta a la
  colección con `addCards` (`pack_open.c:396-404`, `party.c:63`: `GAME.cards` tiene 0x13D ranuras). Las filas están repartidas en tres arreglos (`slotCards[123]`, `slotCards123`,
  `slotCards124[86]`), así que van por `rowAt` (puntero de cada ranura del sobre). El `item` del sobre no es campo.
- `card_effects` (60 filas, llave = id de carta, `condition`, `playCondition`, `message`, `target` y `step1`..`step40`): el juego lee `CARDGAME_cardEffects[id - 1]`
  (`effect_script.c:19-36`). `step` 1-174: son los casos de `CARDGAME_runEffectStep` (`effect_script.c:676`), todos presentes; un número mayor no tiene caso y el paso no
  termina nunca (bloqueo), un 0 termina el guion (`battle.c:2142`). `target` 0-9 (`CARDGAME_flagTargetSlots`, `battle.c:997`); `message` 0-166 (el mayor índice del texto del
  duelo que abre el código, `0xA6` de `CARDGAME_battleMessages`); `condition` y `playCondition` sin caso valen "nunca la juega el rival" y "sin condición" (`default` de
  `computer.c:17` y `battle_steps.c:167`). **Rechazo de fila entera** (`rowCheck`): un guion de 40 pasos sin ningún 0, que seguiría leyendo la fila siguiente (y en la 60 más allá de la
  tabla). No se comprueba la coherencia entre pasos, objetivo y saltos (`effectPos += n`): una combinación mala puede bloquear la carta.
- `prize_items` (36 filas, `item` 1-32767 con nombres de objetos de mods): `CardBattle.prize = CARDGAME_prizeItems[opponent->prize]` y se suma a la bolsa hasta 99 (`battle.c:14,2527`).
- `default_deck` (40 de 80 valores, `card` 0-313 = id de carta menos uno; un `"<mod>:<nombre>"` se resuelve a su id y el puerto resta el uno): `loadChosenDeck` lee 40 (`battle.c:810`) y la carta se carga con `setCard(valor + 1)`
  (`screen.c:648`); los otros 40 valores no los lee nadie.
- **`CARD_NPC.BIN` (E5d2, hecha en modding-ux U8a): `card_opponents`.** Archivo del disco 0x7A4 (`FILE_CARDGAME_OPPONENTS`), 170 filas de `CardOpponent` (0xD0 bytes, `battle.h:217`), sin
  llave ni cabecera: la tabla es de archivo **posicional** (`tmRegisterFile` con `keyFromRow` en 0 y `rows` fijas: el archivo llega a 18 sectores y los bytes del final son relleno). El
  juego lee una fila por duelo, `CARDGAME_loadOpponent` (`battle.c:7-67`): `FILE_CACHE.load(0x7A4)` y la fila `CardBattle.arg - 1`. Por eso el parche va **sobre el búfer, justo después
  de cada lectura del archivo** (gancho de lectura de `port_fs`, el mismo de `battle_loot`), y no en el `onEnter` del ámbito `cardgame`: no son datos de overlay (no hay nada que restaurar)
  y el duelo los pide por número de archivo. Cada fila: 40 mazos (`CardDeckCard`: `card` en los 12 bits bajos con el id 1-314, el bit 15 una marca, `group`, `kind`), 20 `counterCards`,
  `level` y `prize`. Campos de la tabla: `card1..40`, `flagged1..40` (el bit 15, con la máscara de bits que ganó `TmField`), `group1..40` (1-7), `kind1..40` (0-7), `counter1..20`
  (0-314 o `"<mod>:<nombre>"`; `card1..40` también acepta el nombre de una carta de un mod), `level` (1-99) y `prize` (0-35, índice de `CARDGAME_prizeItems`): 182 campos.
  Rangos de cómo lee el juego y de los datos del disco (las 170 filas, decodificadas aparte de la tabla): ids 1-314 (máximo en disco 314); `group` 1-7 (`CARDGAME_findOpponentDeckLimits`
  compara `round * 2 + 2 < group`, el 7 es la reserva); `kind` 0-7 (el `switch` de `battle.c:19-41` pone la prioridad de 1-7; el disco usa 0 en una carta; 1-5 en el resto); `level` en
  disco 4-92; `prize` en disco 1-35 (`CARDGAME_prizeItems[prize]` tiene 36). **Filas rechazadas enteras** (`rowCheck`, ahora también para tablas de archivo): una lista de contraataque
  sin 0 entre sus 13 primeras ranuras (el juego copia 14 respuestas fijas tras el 0 a un arreglo de 27 bytes, `battle.c:44-60`; el disco termina todas en la ranura 12 o antes) y un mazo
  entero del grupo 7 (`reserveStart` 0: `CARDGAME_returnOpponentHand` escribiría en la posición -1, `battle.c:138-148`). No hay tabla de nombres de oponentes en el juego, así que la referencia no
  trae `"labels"`; las cartas se nombran con `CARDNM`. Una fila depende del argumento del modo (`oponente * 2 + tipo + 1` del `cardauto`), así que un NPC del campo puede usar más de una.

### 3.8 Gimnasio y posadas

- `STGTRAIN_gymTrainings[14][16]` de `TrainEntry` (`id`, `stat`, `other`; `menus/gym/files.c:249`, `gym.h:326`),
  `STGTRAIN_intensityCosts` (`files.c:245`), ganancias: `STGTRAIN_statGains`, `_statLosses`, `_resistGains{Low,Mid,
  High}`, `_resistGainTables`, `_maxHpMpGains` de `TrainGain {base, range}` (`menus/gym/result.c:789-828`,
  `gym.h:282`). Lectores `result.c:22-146`, `session.c:157,233`. Todo constante. Aleatorio: `base + RANDOM.next() %
  range` si `range` no es 0.
- Las ganancias de resistencia dependen de `DigimonData.resistGrowth` (`result.c:78-82`).

**Tablas del gimnasio y las posadas (E5e, `shim/game_tables.c`).** Tamaños de `nm -S` del binario del puerto (EU): `STGTRAIN_gymTrainings` 0x700 (14 x 16 x 8),
`_statGains` 0x60 (12), `_statLosses` 0x18 (3), cada `_resistGains*` 0x48 (9), `_maxHpMpGains` 0x48 (9), `_intensityCosts` 0xC (3), `_resistGainTables` 0x78 (15 punteros de 8
bytes en el host: no se expone) e `INNS` 0xB0 (21 + el cierre `{0, 1, 1}`). Las seis del gimnasio son datos del overlay STGTRAIN (rango dentro de la sección de ámbito `stgtrain`,
`check_scopes`): se registran al arrancar y se parchan en el `onEnter` del ámbito. `INNS` es del ejecutable, que nada reinicia: se parcha una vez al registrarla.
- `gym_trainings` (224 filas, llave = `índice del gimnasio * 100 + ranura`, campos `id`, `stat`, `other`): `STGTRAIN_getGymTrainings` lee la fila del índice (el argumento del modo,
  1-14; un índice 0 o mayor que 14 lee la fila 0, y el 14 leería más allá del arreglo; el disco llena las filas 0-13 y no se verificó qué índices piden sus guiones). El menú
  recorre las 16 entradas y pone cada `id` en su sitio fijo (`menu.c:342-393`: ids 1-24); `findGymTraining` devuelve la primera con ese `id` (`files.c:230-240`). `id` 0-24 (los 25
  `TrainInfo`, `files.c:337`), `stat` 1-5 (estadística de batalla) u 8-14 (resistencia), `other` 0, 1-5 (resta a una estadística, solo con `stat` 8-14), 15 o 16 (suma al PV o PM
  máximo; `result.c:171-218`). **Rechazo de fila entera** (`rowCheck`), solo con `id` distinto de 0: `stat` 0, 6 o 7 (la pantalla de resultados lee sumas que el entrenamiento nunca fija
  si `stat` es 0: `result.c:391-399`; 6 y 7 no son estadística) y, con `stat` 8-14, un `other` que no sea 0, 1-5, 15 o 16 (el texto del juego se elige por ese número: `result.c:208,216`).
- `gym_stat_gains` (12 filas), `gym_stat_losses` (3), `gym_resist_gains` (27 = 3 tablas x 9, por `rowAt` porque en el host no son contiguas) y `gym_hp_mp_gains` (9): `base` y
  `range`. La ganancia es `base + RANDOM.next() % range` y con `range` 0 es `base` (`result.c:22-25`: el divisor nunca es 0). **Columnas leídas del código** (los comentarios del
  fuente dicen otra cosa): `statGains[tipo * 6 + bonus * 3 + intensidad]` con `tipo` 0 para los entrenamientos 1-12 y 1 para 13-24 (`result.c:19-21`); `statLosses[intensidad]`
  (`:45`); `resistGains*` y `maxHpMpGains[intensidad * 3 + nivel]` con `nivel` 0 sin intento extra, 1 con él en los 1-12, 2 con él en los 13-24 (`:86-93`, `:134-142`). Las tres
  tablas de resistencia las elige `_resistGainTables[(resistGrowth - 1) * 3 + (menos de 100, menos de 300, más)]` (`:78-82`), y varios crecimientos comparten tabla: cambiar una fila
  cambia a todos. **Rangos:** ni `base` ni `range` pueden ser negativos (una ganancia negativa bajaría la estadística de 0 sin tope: `result.c:27`), y `base + range - 1` no puede pasar lo
  que cabe en el `s16` de la estadística tras la suma (`*value += gained` va antes de acotar a 999 o 9999): 31768 = 32767 - 999 en estadísticas y resistencias, 22768 = 32767 - 9999 en
  PV/PM máximos; en las pérdidas 32767. El `rowCheck` rechaza la fila entera si `base + range - 1` pasa el tope. Las sumas de cinco intentos (`s32`) caben.
- `gym_intensity_costs` (3 filas, `cost` 0-32767): `session.c:157` compara con el TP del compañero y `:233` lo resta de un `s16`. Un costo mayor que el TP posible solo deja esa
  intensidad sin poder elegirse (se sale con triángulo).
- `inns` (21 filas, llave = el `mode` de la fila, en decimal: 522 = 0x20A; `price` 0-32767, `string` 0-21): `updateInn` busca la posada por el modo del campo (`inn.c:277-283`) y el menú
  pide `price * party` (`:127,150`, `s32`; la partida tiene 3 plazas). `string` es la entrada de `HTLNAM` (22 entradas en los cinco idiomas, comprobado en el disco): 1-15 y 21 son nombres
  de posada; 0 está vacía y 16-20 son los textos del menú ("BIT", la pregunta, las dos respuestas y el aviso de dinero). Un modo sin fila deja `inn` en el cierre (`{0, 1, 1}`: precio 1);
  el `mode` no es campo y no se pueden agregar posadas.
- **No cubierto:** `STGTRAIN_resistGainTables` (qué tabla usa cada crecimiento y nivel de resistencia), `STGTRAIN_trainings` (nombre e iconos), `STGTRAIN_waitAnims` y `STGTRAIN_files`
  (arte); la coherencia entre `stat`, `other` y el texto de un entrenamiento cambiado.

### 3.9 Condiciones de eventos (S3)

Las condiciones y acciones de las charlas (`["special", ...]`, `["party-stat", ...]`) leen seis tablas del ejecutable, todas constantes (`engine/game/events.c`). `shim/game_tables.c` las expone como tablas de mods
(`gameTablesRegisterEvents`, una vez en el arranque y antes de cargar los parches de escenarios, que usan sus nombres); ninguna se reinicia, así que se parchan una vez.
- `special_conditions` (llave = el `id` de la fila, en decimal, 0-254; el 255 cierra el arreglo y no es fila; 120 filas en EU). Una fila es `{id, check, arg}` y `checkSpecialCondition` corre la primitiva `check >> 4` con
  `op = check & 0xF` y `arg`; el parche las ve como tres campos: `primitive` (alto del byte, 0 conjunto de objetos, 1 compañero, 2 dinero, 3 rango de progreso, 4 cuenta de banderas, 5 ícono del actor), `op` (bajo del byte) y
  `arg`. Las ops de cada primitiva están en la nota del campo del archivo de referencia (`data/tables/special_conditions.json`, que además trae en `labels` lo que hace cada fila original). **Rechazo de fila entera**
  (`rowCheck`): un `primitive` fuera de 0-5, una `op` que la primitiva no tiene (conjunto 0-1, compañero 0-9, dinero 0-2, cuenta de banderas 0-1), un `arg` que indexaría fuera de un arreglo (compañero: 0-7 en las ops 1, 3, 4
  y 6; dinero: el índice de `money_required`, `money_gains` o `money_losses` según la op; rango de progreso: 0-33). La fila conserva sus valores originales. Un `arg` que el juego tolera sin leerlo (modo 3 de una cuenta de banderas) se acepta.
- **Compañeros por nombre (P11).** En las ops de compañero (1, 3, 4, 6) `arg` también puede ser el nombre del compañero, `"<mod>:<llave>"` o `"dw3:partner/<n>"`: `pcPartnerFind` (el registro, `gameTablesSetPartnerFind`)
  lo resuelve **una vez**, al leer la tabla, y el campo queda con el número de ese arranque; un nombre desconocido rechaza el campo (`row "x" field "arg" rejected: no installed mod defines "<nombre>"`): una fila original conserva su `arg` y una fila nueva de un mod no se crea (`TmTable.extRejected`: un campo rechazado de una fila fuera de la tabla). Un número sigue valiendo
  como antes (y se revisa contra los compañeros que existen). Así una misión de mod (charla con acción `["special", ..., 1]` de op 3 y condición de op 6) no depende del orden alfabético de los mods. La op 9 (los ocho
  desbloqueados) no cambia: cuenta a los ocho originales, es lógica de la historia del disco. El estado desbloqueado de un compañero de mod se guarda por nombre con su registro (ADR-014).
- **Filas que agrega un mod.** Una fila de su `tables/special_conditions.json` con una llave `"<mod>:<nombre>"` crea una condición nueva: `{"rows": {"mimod:rico": {"primitive": 2, "op": 0, "arg": 0}}}`; los parches de escenario la usan como
  `["special", "mimod:rico", 1]`. Hasta 256 (el id es de 9 bits y los del juego terminan antes de `0xFF`), en un arreglo del puerto fuera de `SPECIAL_CONDITIONS` (que nunca crece): `checkSpecialCondition` pregunta a
  `pcSpecialExtRow` por los ids desde `0x100`. Una fila nueva nace inválida (primitiva 15): tiene que dar `primitive` y `op` (y `arg` si la op lo lee) y pasar el mismo chequeo que las originales, o el nombre no queda
  definido y el parche que lo usa se rechaza; `--validate-mod` muestra `special_conditions row <mod>:<nombre> rejected: <motivo>`. Con varios mods gana el último por campo, como en cualquier tabla; los ids se asignan en el orden
  en que aparecen los nombres y no se guardan en la partida (cada arranque los arma otra vez desde los parches).
- `money_required` (10 filas, `amount` 0-9 999 999), `money_gains` (8) y `money_losses` (10): las cantidades de las filas de dinero (primitiva 2, ops 0 "tiene", 1 "da", 2 "quita"; `checkMoney` acota el total a 0 y `MONEY_MAX`).
- `progress_ranges` (34 filas, campos `min` y `max` 0-255): los rangos del progreso de la historia que prueba la primitiva 3 (`checkProgressRange`: `min <= progreso <= max`). **Rechazo de fila entera:** `min` mayor que `max` (el rango
  nunca se cumpliría; la fila 33 original es `0-0`).
- `party_stat_thresholds` (15 filas, `threshold` 0-99 999): lo que el total de la estadística de batalla 5 del grupo tiene que alcanzar para `["party-stat", n, 1]` (`checkPartyStat`).
- **Más acciones sin código:** una fila de compañero con `op` 3 desbloquea al compañero `arg` y con `op` 8 cura al grupo cuando una charla la nombra como acción (`["special", "<mod>:<nombre>", 1]`).
- Prueba: `make -C pc tabletest` (`testEventTables`: ida y vuelta sin mods, rechazos de fila y de campo, filas nuevas válidas e inválidas, dos mods, el tope de 256, y que registrar de nuevo olvida los nombres).

## 4. Reglas que son código, no datos

Se necesita un gancho (por `#ifdef DW3_PC` o envoltura del port, ADR-004) y no un parche de tabla:

| Regla | Dónde |
|---|---|
| Reparto de EXP entre socios (todo / 6/10 / un tercio) | `battle/report/report.c:325-336` |
| Bonos +1/5 de dinero (objeto `0x142`) y de EXP (objeto `0x141`) | `report.c:121-123`; `partner.c:492-504` |
| Curva de nivel de socio, topes (nivel 99, exp 999999, stats 999, HP/MP 9999, TP 99) | `growth.c:8-124` |
| EXP de Digimon (reparto, topes por `expLevel`, 10/50 por nivel) | `growth.c:196-318` |
| Tope de dinero (`MONEY_MAX`) y de objetos (99) | `report.c:130-132,144-146`; `game_state.h:461` |
| Elección del objeto ganado y probabilidad (1024avos) | `normal_battle/menu.c:476-510` |
| Objetos de premio forzados (1 en 32 / 1 en 16) | `field.c:708-757` |
| Ambush, pasos hasta la batalla | `normal_battle/setup.c:60-85`; `field/field_mode/battle.c:6-60` |
| Fórmulas de daño, aciertos, críticos, estados, robo | `battle/battle_mode/damage.c`, `rolls.c` |
| Efectos de objetos de combate por id | `battle/battle_mode/item.c:121-147` |
| Stats con equipo y bonos por id de objeto | `engine/game/stats.c:65-119`; `battle/battle_mode/stats.c:120-189` |
| Aprender Digimon: lógica de requisitos (la tabla solo da los datos) | `growth.c:131-192` |

Una opción sin gancho por regla: dejar las fórmulas intactas y mover las tablas (porcentaje en la fila de la
recompensa). Para "más EXP/dinero" alcanza con las tablas de §3.4.

## 5. Cómo se aplica un override y su riesgo

Hecho común: el build de PS1 (matching) no se toca si el parche vive solo en `pc/` y opera sobre los arreglos ya
inicializados. Todo cambio en `src/` iría detrás de `DW3_PC` (ADR-004) con prueba de que el preprocesado sin la
macro no cambia.

| Punto de aplicación | Cuándo sirve | Riesgo |
|---|---|---|
| **A. Parche en memoria del arreglo** (ejecutable): al terminar de arrancar el C, antes del primer modo | `DIGIMON_DATA`, `TECHS`, `ITEMS`, registros de equipo, `ITEM_EFFECT_*`, `INNS` | Bajo. Nada los reinicia (`overlay-state-reset.md:67-80`). Las filas nuevas piden arreglos más grandes (los de `src/` tienen tamaño fijo) o una tabla paralela con el índice como clave. `ITEMS.data` apunta a los registros: no mover los registros, parchearlos en sitio |
| **B. Parche en memoria con gancho `onEnter`** (overlay) | `STFGTREP_*`, `FIELDSTG_*`, `STITSHOP_*`, `STGTRAIN_*`, `stageBattles` | Medio. El ámbito restaura el snapshot al entrar al modo (`state_scope.h:21-28`); hay que registrar el parche con `stateScopeOnEnter` en cada modo. Para `WSTAG*` hay un ámbito por escenario. Hipótesis: el snapshot se toma de memoria tras el arranque, así que un parche hecho antes de crear el ámbito quedaría incluido; el orden hay que comprobarlo en T2 |
| **C. Reemplazo del archivo del disco** (`mods/<mod>/files/...`) | tabla de batalla (463), `CARD_NPC.BIN` | Medio. Se reemplaza el archivo entero (ADR-005 §2), así que dos mods chocan; sirve para sets completos, no para parches por clave |
| **D. Gancho post-lectura en `cdRead`** | los mismos archivos, con parche por clave sobre el buffer leído | Bajo-medio. Mantiene el archivo original y deja mezclar mods por clave; el formato de 0x46 bytes está documentado en `battle.h:11-14`. **Hecho (data-tables T6, `battle_loot`):** `pfsPostRead` (`port_fs.c`) corre tras cada lectura (disco, capa de texto o crudo), `cdRead` lo llama y `boot.c` parchea el buffer de 463 por id de enemigo con `tmPatchBuffer`, partiendo siempre de los bytes recién leídos; si un mod trae el archivo crudo, ese reemplaza al del disco y el parche de tabla va encima. **Digimon (content-techs-digimon D1):** el mismo gancho llama primero a `digimonExtPatchBattleTable` (`content/digimon.json`, faceta `enemy`: estadísticas, técnicas, `item`, `itemChance`), y después va `battle_loot`, que gana en `item` e `itemChance`; las filas de compañero (`DIGIMON_DATA`, ejecutable) se parchean en memoria al arrancar y se restauran al recargar |
| **E. Archivo generado** (`pfsSetGenerated`, `port_fs.c`) | tabla de batalla (463) y lista de luchadores (`FILE_FIGHTERS`, 460) | Medio. **Hecho (content-techs-digimon D3, enemigos nuevos):** el port construye al arrancar una versión **más larga** del archivo (filas añadidas a la tabla de batalla, lista de luchadores copiada al final con el encabezado apuntando a ella; los bytes originales intactos) y la sirve desde memoria; `getSectorCount` anuncia el tamaño nuevo, así que la caché de archivos reserva lo que hace falta y el tamaño del archivo del disco no limita nada. Va debajo de la capa de texto y del archivo crudo de un mod (si hay uno, gana y las filas nuevas no se sirven), y el gancho D parchea encima |
| **E. Gancho de lector** (envoltura) | reglas en código (§4) | Medio. Hay que mantener el comportamiento original con mods apagados |

**Hecho (data-tables T7 y T8).** Punto A: `item_prices` (`ITEMS`, llave = id, `ITEMS[id - 1]`; `price` y `sellPrice` son
u16 propios, el de venta no es la mitad del de compra) se aplica una vez en `pcRegisterTables`; `ITEMS` está en el
ejecutable y ningún ámbito lo cubre. Punto B: `shop_stock` (`STITSHOP_shops`) se aplica en el `onEnter` del ámbito
`stitshop`. Como `ShopList` es `{count, puntero a ids}` y no una fila plana, el motor ganó el **campo lista**
(`TmField.listMax` + `countOff`): el parche da el arreglo o `{"remove","add"}`, el motor escribe un buffer propio y apunta
la fila a él (las listas del juego quedan intactas), hasta 64 ids de 1 a 402 y terminador 0 (más los objetos que agregan los mods, por nombre: ver
`docs/extended-state.md` §7; `item_prices` y `battle_loot.item` también los aceptan por nombre).

**Hecho (content-export E5a).** Punto A para `weapon_data`, `armor_data`, `accessory_data` e `item_effects`
(`pc/shim/game_tables.c`, llamado desde `pcRegisterTables`). Las tres de equipo son arreglos contiguos cuyas filas siguen el id del
objeto sin huecos (`ITEMS[91 + i]` apunta a `WEAPON_DATA[i]`, y así las armaduras desde el id 215 y los accesorios desde el 292), así que
la llave es `keyBase + i`. Los `ITEM_EFFECT_*` son 83 símbolos sueltos: el motor ganó `TmTable.keys` (llave de cada fila) y `rowAt`
(dónde vive cada fila; trabaja sobre una copia contigua y escribe cada fila de vuelta) y `rowCheck` (un chequeo por fila tras los
parches: el aumento de estadística con `amount` 0 divide por cero en `STSTATUS_useItem`, y el accesorio con `kind` 7 no cabe en
las ranuras). Los rangos salen de cómo lee el juego cada campo (`stats.c`, `battle_mode/stats.c`, `item_screen.c`): bonos con signo
(`(s16)amount`), estadísticas topadas en 999, PV curados dentro de los 16 bits (`amount` hasta 20000). Un objeto de mod tiene copia propia del
registro (`ItemRec.equip` / `.effect`), así que `"<mod>:<nombre>"` se puede direccionar sin tocar al objeto base.

**Hecho (content-export E5b).** Siete tablas: `evolutions` (352 filas = 8 socios x 44 entradas, llave `socio*100 + entrada`, campos
`digimon`, `need1`/`need1Level`, `need2`/`need2Level`, `stat`, `value`), `blast_forms` (52 filas de `DIGIMON_DATA`, llave = id, `blast1`..`blast5`),
`level_exp` (4), `vital_cuts` (4), `vital_random` (9), `stat_gains` (6 tramos x 9) y `resist_gains` (8). Las seis del overlay se aplican en el
`onEnter` del ámbito `stfgtrep` (después de que `state_scope` restaura los datos); `blast_forms` es del ejecutable y se aplica una vez
al arrancar. Una forma de Digimon se da como fila + 1 o por nombre (`"dw3:digimon/<id>"`, `"<mod>:<clave>"`, resuelto por
`digimonExtRowRef`); las filas 53-55 son marcadores de posición del disco y se rechazan. Evidencia de rangos (todo de `src/battle/report/growth.c`
y `normal_battle/menu.c`): `learnDigimon` salta una entrada con `digimon` 0 (lee la fila -1, la de reserva, id 0), `stat` 1-6 batalla, 7 nivel,
8-14 resistencias; el tramo de nivel de `levelExp`/`vitalCuts` es 1/5/20/40 y el de `statGains` 1/5/20/40/60/80; `WFIGHTMN_blast` usa
`blastForms[tier] - 1` con tramos de nivel 1-3, 4-18, 19-38, 39-69, 70+, y un 0 donde el disco tiene forma lee la fila -1: `rowCheck`
rechaza la fila entera. `levelExp` se limita a +-1000000000 para que la suma quede en 32 bits. **Hallazgo del original** (no se corrige): `statGrowth` y `resistGrowth` llegan a 5
en el disco, así que `statGains[tier][5 + 4]` lee una posición después de la fila y `resistGains[5 + 3]` una después de la tabla; las notas
de la exportación lo dicen. **Sin comprobar:** que una ganancia de vitales negativa (corte alto con `vital_random` bajo) no deje el máximo en 0
o menos: no se rechaza, el motor no ve la fila de `DIGIMON_DATA` junto a las dos tablas.

Riesgos transversales:

- **Partidas guardadas** (hecho): `GAME` guarda conteos por id de objeto en arreglos de tamaño fijo (`items[0x193]`,
  `cards[0x13D]`, `game_state.h:491-493`), `PartnerEntry.skills` guarda ids de 13 bits (`SKILL_ID` 0x1FFF) y
  `PartnerStats.equip` ids de objeto de 16 bits. Cambiar valores de filas existentes es compatible con partidas ya
  guardadas; añadir ids nuevos exige la asignación guardada en `saves/<partida>.mods.json` (ADR-006 §4) y tope a
  los rangos anteriores. La tarjeta de PS1 (la que se importa desde `import/`) debe seguir válida para el original (ADR-006 §4).
- **Estado en memoria** (hecho): las tablas se copian a `GAME`/`PartnerEntry` en puntos concretos (por ejemplo, el
  nivel inicial y stats base de un socio en `party.c:22-38`, solo en partida nueva); un parche a `DIGIMON_DATA.hp`
  no afecta a una partida ya empezada.
- **Datos retail** (ADR-001): los parches de mods son JSON del modder con cifras propias; ni el exportador a
  `data/` ni la documentación pública pueden incluir los valores originales.

## 6. Recomendación ordenada

Metas del dueño: mejores ganancias de EXP, objetos y dinero; luego tiendas y encuentros.

| # | Tabla | Para qué | Esfuerzo | Riesgo | Punto de aplicación |
|---|---|---|---|---|---|
| 1 | `STFGTREP_rewards` (`BattleReward`) | EXP, EXP de Digimon y dinero por encuentro | Bajo: 335 filas x 3 campos, llave = índice de encuentro | Bajo | B (overlay STFGTREP): gancho `onEnter` del modo 0x1400, o parche antes de crearlo si el snapshot lo incluye |
| 2 | Tabla de batalla (disco 463): `item`, `itemChance` | Probabilidad y tipo de objeto soltado por enemigo | Bajo-medio: registro de 0x46 bytes por id de enemigo | Bajo | D: post-lectura en `cdRead` por id; C como respaldo |
| 3 | `ITEMS` (`price`, `sellPrice`) + `ITEM_EFFECT_*` / registros de equipo | Precios y efectos de objetos | Bajo: 402 filas, llave = id de objeto | Bajo (ids nuevos: ver partidas) | A: al arrancar |
| 4 | `STITSHOP_stock*` y `STITSHOP_shops` | Qué vende cada tienda | Bajo: listas de ids | Bajo | B: gancho `onEnter` de STITSHOP |
| 5 | `FIELDSTG_encounters`, `FIELDSTG_enemies`, `stageBattles`, `FIELDSTG_battleRates` | Encuentros, tasas y jefes | Medio: punteros entre tablas y un `stageBattles` por escenario | Medio | B: ámbito de FIELDSTG y ámbito del escenario. **Hecho en E5c:** `field_enemies`, `field_encounters`, `battle_rates` (§3.5); falta `stageBattles`. **Hecho el colocar enemigos (D4):** `"encounters"` de `content/digimon.json` apunta un hueco de `FIELDSTG_encounters[n].enemies[slot]` a una fila `BattleEnemy` del port en el `onEnter` del ámbito `fieldstg` (la tabla del overlay no se escribe; el ámbito la restaura en cada entrada); no hay tabla `encounters` de `tables/` porque necesita nombres de enemigos de mods |
| 6 | `DIGIMON_DATA`, `TECHS` | Stats, crecimiento, técnicas | Medio: muchos campos con significado parcial | Medio (ranura 439 de `TECHS`) | A |
| 7 | `STFGTREP_evolutionLists` + `blastForms` | Requisitos de evolución | Medio | Medio | B (hecho en E5b) |
| 8 | Gimnasio `STGTRAIN_*` | Ganancias de entrenamiento | Bajo | Bajo | B |
| 9 | Cartas | Mazos, premios | Medio (parte sin datos en C) | Medio | C/D y B |

Notas para T2:

- Una llave estable para cada tabla: id de objeto, id de Digimon (`DigimonData.id`), id de técnica (n), id de enemigo
  (`BattleTableEntry.id`), índice de encuentro (compartido por `FIELDSTG_encounters` y `STFGTREP_rewards`). El
  índice de encuentro no es estable entre versiones (hipótesis); conviene nombrarlo con un alias del port.
- Las reglas de §4 se parchean como constantes declaradas en `config/*.json` (ADR-005, mezcla por clave) detrás de un
  gancho `DW3_PC`, no como tabla.
