# ADR-015 — Registro de compañeros y equipos iniciales desde mods (`partners.json`, `teams.json`)

- **Estado:** ACCEPTED (dirección del dueño, 2026-11-05: sin compatibilidad hacia atrás obligatoria; diseño correcto y sin topes de PS1;
  los mods se leen una vez al arrancar y se sirven desde memoria).
- Continúa ADR-013 (arreglo de compañeros) y ADR-014 (por nombre, `PTNR`). Tarea P4 del paquete betamon-pack.

## Decisión

> Actualización A8.2 (2026-11-07, ADR-016): el registro de compañeros ya no es `content/partners.json` sino `partners/<llave>/partner.json` (`partners-partner-v1`) de cada mod de contenido, y `name` pasó a `title`; los equipos siguen en `content/teams.json`. El resto de este ADR (nombres, números, validación) sigue igual.

1. **Dos tipos del núcleo de contenido** (`pc/shim/partner_ext.c`, `content.c`): `content/partners.json` (`partners-v1`) y
   `content/teams.json` (`teams-v1`). Mismas reglas que los demás tipos: nombre `<mod>:<llave>`, validación con una línea de log por
   rechazo (`[partners]` / `[teams]`), números deterministas por orden alfabético del nombre completo (compañeros desde el índice 8,
   equipos desde el 3), tope solo de seguridad (`PC_PARTNER_LIMIT` 256). Esquemas: `docs/modding/schema/partners.schema.json`,
   `teams.schema.json`. Se añadieron dos ganchos opcionales al núcleo: `ContentType.accept` (rechazar una entrada ya leída sin dejar un
   hueco en los números: un compañero sin `firstForm`) y `baseOptional` (un equipo no necesita `base`).
2. **Se registra al cargar los mods, antes de leer cualquier partida.** `pcInstallPartners` (`pc/boot.c`) corre justo después de
   `digimonExtLoad` (las formas existen) y de `itemExtLoad` (los objetos existen): `partnerExtLoad` registra con `pcPartnerRegister`
   (ADR-014), así que `PTNR` se decodifica siempre contra un registro completo. Los mods no se vuelven a leer.
3. **El compañero** (`partners-v1`): `base` (compañero original cuyo arte sustituye y cuyo bit de la máscara de equipo se lee por omisión),
   `firstForm` (forma de compañero original o de `digimon.json`: la fila `PC_PARTNER_ROW`), `name`/`description` por idioma, `equip`
   (`like`, `allow`, `deny`), `equipSet` (4 objetos y 6 bonos) y `evolution` (lista de formas que aprende, las entradas de `Evolution`
   con los Digimon por nombre, que `pcEvolutionExtend` agrega a la lista del compañero: el mismo mecanismo que el `evolution` de
   `digimon.json`, pero declarado por el compañero).
4. **Textos por la capa de texto.** `choice.c` lee el nombre de un compañero en `DIGNAM[p+1]` y su descripción en `NAMEET[p+13]` (`TEXT_ONLINE`):
   para `p >= 8` eso cae en otros textos (`DIGNAM` 9.. son otros Digimon; `NAMEET` 21.. son los textos de la pantalla). Los textos nuevos son
   entradas añadidas al final de `NAMEET`, que en el disco tiene **31 entradas** en todos los idiomas (el encabezado de
   `disks/eu/AAA/DAT/COUNTRY/ENG/ESNAMEET.BIN` empieza con `0x1F`): `tdSetPartnerTextHook` (`text_data.c`, una clase más de `ExtText`) añade, para
   cada compañero nuevo `k`, las entradas `31 + 2k` (nombre) y `32 + 2k` (descripción) y, después de todos los compañeros, las de cada
   equipo nuevo `j` (`31 + 2P + 2j`, título y descripción). Los accesores `PC_STARTER_NAME_FILE/ENTRY`, `PC_STARTER_TEXT_ENTRY`,
   `PC_STARTER_PACK_NAME_ENTRY/TEXT_ENTRY` (`include/engine/partner_access.h`) dan archivo y entrada; en PS1 expanden al texto original
   (`p + 1`, `p + 13`, `choice + 10`, `choice + 26`). El nombre con que un compañero nuevo empieza la partida (`Partner.info.name`, lo que
   muestra el HUD de batalla, `hud.c:73`) sale del mismo texto.
5. **Equipos iniciales: una tabla.** `STARTER_PARTIES` (`party.c`) y `STPLNMET_partnerChoices` (`stplnmet.c`) eran dos copias; con `DW3_PC` ambas
   se leen por `pcStarterParty(set)` (`PC_STARTER_SET_PARTNER`, `PC_STARTER_CHOICE`), una tabla que empieza de `STARTER_PARTIES` y a la que los
   mods añaden (`PC_STARTER_PARTY_COUNT`). `setParty` acepta cualquier compañero registrado y pone `-1` en una ranura inválida. La pantalla con
   más de tres opciones es P5; el arnés elige equipo con `DW3_STARTER_SET=<n>` (`host_pad.c`, donde antes `setParty(0)`).
   `GameState.partySet` guarda el **número** del equipo (solo lo compara `events.c:93`): un número de mod depende de los mods instalados.
6. **Pantalla de equipo con cualquier cantidad (P5).** La columna izquierda del original es un solo sprite de tres cajas
   (`choice.c`, frame 7 de la hoja del menú: sus tres primeras partes son la misma caja de 28x20, 0x18 aparte desde y 0x2C; etiquetas en `0x34 + 0x18 i`, cursor en `0x30 + 0x18 i`).
   El puerto muestra hasta 6 filas (`PC_STARTER_ROWS_MAX`; las de abajo repiten esa parte con la misma textura y paleta; con 3 equipos nada cambia) y con más de seis se desplaza (`PartnerChoice.top`,
   `pcStarterPageMove`, una función pura probada en `extstatetest`) y una barra en el margen x 6..10 (vacío: las cajas empiezan en x 0x10) marca las filas. `choice` sigue siendo el
   número del equipo, por lo que `screen.c:74`/`confirm.c` y `setParty` no cambian. Las etiquetas de los equipos nuevos (D, E, ..., AA) son entradas de `NAMEET` tras los
   textos de los equipos (`PC_STARTER_LABEL_ENTRY`). Contenido del equipo (título `choice + 10`, texto `choice + 26` en el original): `PC_STARTER_PACK_*_ENTRY`.
7. **`partySet` por nombre.** Cada equipo tiene un nombre (`dw3:team/<i>`, `<mod>:<llave>`; `pcStarterPartyName/Find`). Único lector: `checkPartner` op 0 (`events.c:93`), contra los
   `arg` 0, 1 y 2 de las filas `special_conditions` del disco (`events.c:803`, EU; `:778`, US). La partida guarda un equipo de mod en la sección `TEAM` (u32 versión 1 + nombre); un equipo
   original no escribe sección (los archivos de siempre quedan idénticos). Al cargar: nombre -> número de hoy; sin el mod (o sección dañada) `-1`, que ningún `arg` iguala, o sea
   "sin equipo" como un equipo de mod para el disco. Sin sección el bloque se deja como está.

## Cierre de lo pendiente de P3 (ADR-014 §8)

- **Ficha inicial** (`party.c`): `initPartnerData(i)` es el cuerpo del bucle de `initNewGameData` (nombre, nivel 1, HP/PT, estadísticas y resistencias
  de la forma inicial), usado por `initNewGameData` y por `pcInitMissingPartners`, que corre tras cargar una partida (`saves.c:407`) y la da a
  los compañeros nuevos que la partida no traía (nivel 0 tras la importación). Los 8 originales siempre vienen del bloque.
- **Máscara de equipo `u8`** (`equip.c:25`, `helpers.c:88`): `PC_CAN_EQUIP(item, mask, partner)`. Para `p < 8` el bit de siempre; para un compañero
  nuevo, `deny` > `allow` > el bit del compañero original de `equip.like` (`pcPartnerCanEquip`, `ext_state.c`). La máscara del registro del objeto
  no se ensancha (su layout es el del disco): la elegibilidad de los nuevos vive en el registro del compañero.
- **`EQUIP_SETS`** (`stats.c:138-143`): `PC_HAS_EQUIP_SET`, `PC_EQUIP_SET_ITEM`, `PC_EQUIP_SET_BONUS`; los originales siguen en las tablas del juego.
- **Reserva del laboratorio** (`digimon_lab.h:89`, `party_screen.c:402`): con `DW3_PC`, `LabPartyScreen.partners` es un puntero a un búfer del port
  (`pcLabPartnerBuffer`, `PC_PARTNER_COUNT + 1` entradas, la última para la elección vacía) y el bucle recorre `PC_PARTNER_COUNT`.
  `pickPartner` recorre `count` sin tope, así que no hay otra cifra de 8.
- **Operación de evento** (`game_tables.c`, `specialProblem`): el `arg` de las operaciones de compañero se compara con el número real de compañeros
  (`gameTablesSetPartnerCount`, un gancho porque `tabletest` no enlaza `ext_state.c`) en lugar de `PARTNER_COUNT`. `events.c` sigue indexando
  `PC_PARTNER(arg)` sin comprobar: las filas se validan al cargar. La operación 9 de `checkPartner` ("los ocho desbloqueados") se queda en los ocho originales
  (`events.c:140-146`).
- **Nombre del HUD** (`hud.c:73`): lee `Partner.info.name`; lo da la ficha inicial.
- **`view/slotN.json`** (`save_view.c`): lista los compañeros hasta `PC_PARTNER_COUNT` (los >= 8 con `"id"`, el nombre estable) y sus formas/técnicas de mod.

## Consecuencias

- Sin mods todo queda idéntico (la tabla de equipos y los accesores devuelven lo mismo); PS1: los macros expanden al texto original.
- Quitar el mod: ADR-014 (PTNR descarta al compañero con línea `[saves]`); `partySet` pasa a `-1` (§7).
- Pendiente (P6-P10): el arte 2D propio (hoy `PC_PARTNER_ART`), el modelo de compañero, un camino de juego
  para desbloquear un compañero nuevo (las filas `special_conditions` lo nombran por número; un nombre estable sería mejor), `DW3_SEED_PARTNER` por nombre.
- Pruebas: `pc/partnerexttest.c` (registro, validación, números, equipo, evolución, textos, equipos), `pc/extstatetest.c` (`testPartnerTables`),
  `pc/tabletest.c` (conteo de compañeros), `pc/savetest.c` (vista de la partida).
