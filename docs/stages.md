# Escenarios como datos (stage-v1)

Describe el modelo en memoria de un escenario del campo, su archivo
`stage-v1`, el vocabulario de códigos, qué cubre y qué no, y cómo un mod lo parcha (`stage-patch-v1`, §7) con banderas propias (§7), textos con nombre (§8) y acciones nuevas (§9). Solo hay estructura y conteos: ningún valor del juego (ADR-001). Los archivos
`data/stages/<nnn>.json` salen del disco del propio jugador y no se suben al repositorio.

## 1. Qué es un escenario

Cada `WSTAG<nnn>` (294 en el disco EU) llena `FIELDSTG_state` (`FieldState`, `include/field/field_map.h`) con su `setupStage()`. Casi todo lo que el campo
hace con un escenario es dato que ese setup entrega: dónde empieza el jugador, la música, los personajes con sus condiciones y charlas, las
salidas y los eventos. Un comportamiento que ningún dato expresa (animaciones, tareas, cinemáticas con código propio) vive en funciones de C del
escenario; el archivo las nombra en `functions` y `notes` para que se vea qué queda fuera.

El modelo, el exportador y el vocabulario no conocen ningún encabezado del juego: se leen de las tablas del juego (`pc/boot.c`), de JSON y se escriben de
vuelta, sin ventana ni disco. El proyecto online reutilizará ese mismo núcleo.

## 2. El modelo (`pc/shim/stage_data.h`)

`SdStage` es el escenario en memoria, en el orden en que se agrega contenido: primero el original, después los mods (S4).

| Campo | Qué es |
| --- | --- |
| `number`, `name`, `modes` | `WSTAG<nnn>` y los modos del juego que lo corren (`FIELDSTG_stages`, y la tabla extra del capítulo europeo) |
| `textFile` | el archivo de texto del escenario sin el idioma (archivo real = `LANGUAGE` + `textFile`); -1 sin texto |
| `mapFile`, `sheetEntry`, `imageEntry`, `imageFile` | archivos del mapa, la hoja de sprites y las imágenes (tal como los guarda el juego) |
| `soundBank`, `music` | banco de sonido y pista (`MUSIC(banco, n)` = `0x60000000 \| banco << 18 \| n`) |
| `startX`, `startY`, `startDir` | dónde empieza el jugador (píxeles x 256) y hacia dónde mira |
| `spriteColor` | color de los sprites del campo (r, g, b, cd) |
| `nobjects` | cuántos objetos de mapa (`StageTile`) tiene; el modelo no los guarda (son parte del dibujo) |
| `images` | los `ActorImage` desde el índice 2 hasta el último que usa algún personaje |
| `actors` | los personajes: `character` (`Actor.key1`), `image`, `x`, `y`, `dir`, `cond` (lista, sin fijar = siempre), `talks` |
| `talks` | las charlas de un personaje en orden: gana la primera cuyas condiciones se cumplen; la última no tiene condiciones. Cada una: `cond`, `actions`, `entry` (línea del texto del escenario) y `preview` |
| `slots` | las salidas y disparadores (`StageSlot`): `type` (`SLOT_*`), `arg`, `x`, `y`, `dir`, `hideAnim`, `place`, `placeArg` y hasta 2 condiciones; solo del puerto (S8), `area`: un rectángulo del mapa en píxeles `{x, y, w, h, facing}` donde se dispara (los del juego no lo traen: el mapa trae sus celdas) |
| `events` | los eventos (`FieldEvent`): `id`, `text` (archivo y línea), `script` (comandos), y si C corre una tarea al empezar (`start`) o una función al terminar (`end`) |
| `battles` | las batallas de área (`FieldBattles` del lugar que eligió el `setupStage`): por área del mapa (1 a 4, la cuarta es la que empiezan los eventos), `rate` (índice de `FIELDSTG_battleRates`) y 8 batallas `{encounter, stage, music}`, de las que sale una al azar. Sin la llave = el escenario no tiene (S10) |
| `functions`, `notes` | las funciones de C que define el escenario y lo que hace que el modelo no guarda |

Una lista de códigos que en el juego es un puntero `NULL` (sin condiciones, la última charla, sin acciones) es una lista **no fijada** (`SdList.set` = 0);
no es lo mismo que una lista fijada y vacía (siempre se cumple, pero no es la última). El modelo guarda las listas como palabras `u16` sin el
terminador `0xFFFF`.

### Una sola vía de carga

El original y los mods pasan por el mismo modelo: el original primero (lo que `setupStage` entregó), después los parches de los mods. Los datos
van separados del dibujo; el modelo no sabe de VRAM ni de sprites. La capa de parches (S4) se engancha tras `setupStage`: `FIELDSTG_pickStage` elige el
escenario, `FieldState.stageInit` (la `startStage` del escenario) crea su tarea y llama a `setupStage` (`src/field/field_mode/field.c:511`), así que
el gancho vive entre ese retorno y `FLAGS_00.updateModeFlags()`. Las tablas que `setupStage` entrega son memoria estática del escenario (`stageActors`,
`stageSlots`...): un parche no las modifica, arma buffers propios que viven lo que el escenario.

## 3. El archivo `stage-v1`

`<datos>/stages/<nnn>.json`, una copia de referencia por escenario (con `manifest.json`, la misma regla `.new` de las otras familias de `data/`: una copia
editada se queda y la nueva versión va al lado). El exportador corre al arrancar (ver §6) y se salta cuando la carpeta ya tiene la versión actual.

```json
{
  "format": "stage-v1",
  "about": "...texto de ayuda...",
  "notes": ["lo que el escenario hace en C y no es dato"],
  "stage": 380, "name": "WSTAG380", "modes": [551],
  "textFile": 239,
  "mapFile": 698, "sheetEntry": 45809664, "imageEntry": 0, "imageFile": 815,
  "soundBank": 9, "music": 1612972032,
  "start": {"x": 81152, "y": 212992, "dir": 0},
  "spriteColor": [84, 103, 150, 0],
  "objects": 95,
  "images": [{"x": 512, "y": 256, "imageX": 534, "imageY": 312, "u": 88, "v": 56, "clutX": 512, "clutY": 509}],
  "actors": [
    {"index": 1, "character": 75, "image": 5, "x": 530, "y": 430, "dir": 1,
     "conditions": [["flag", "1A:21", 1], ["flag", "1A:2C", 0]],
     "talks": [
       {"entry": 697, "conditions": [["flag", "1A:2C", 0]],
        "actions": [["flag", "1A:2C", 1], ["item", "dw3:item/7", 1]], "preview": "..."},
       {"entry": 0}
     ]}
  ],
  "slots": [{"type": "exit", "arg": 546, "x": 810, "y": 820, "dir": 3, "hideAnim": 0, "place": 0, "placeArg": 0}],
  "events": [{"id": 205, "text": {"file": 295, "entry": 3}, "end": true, "script": [["walk", 2, 191, 400, 3], ["end"]],
              "previews": {"3": "..."}}],
  "functions": ["endEvent205", "updateStage"]
}
```

- Unidades: `x`, `y` de personajes y salidas en píxeles; `start` en píxeles x 256 (como lo guarda el juego); `dir` 0 a 7.
- `textFile` y el `file` de un evento se cuentan sin el idioma: el archivo real es `LANGUAGE` + ese número.
- Una charla sin `conditions` es la última y siempre se cumple; `"conditions": []` se cumple siempre pero no es la última.
- `preview` (charlas) y `previews` (eventos, por línea) son el texto en el idioma con que corrió el exportador (el idioma elegido en el menú, con es-419 si es el
  caso). Son de solo lectura: nada los vuelve a leer, ni siquiera el modelo al aplicar un parche.
- El archivo es el escenario con el progreso de la historia en 0; `notes` dice qué partes del `setupStage` dependen del progreso (música, banco de sonido,
  batallas, salidas).

## 4. El vocabulario de códigos (`pc/shim/stage_codes.h`)

Un código es un arreglo JSON `[nombre, referencia, valor]` por cada par `(código, valor)` de `src/engine/game/events.c`. El mismo vocabulario sirve de condición
(una charla, un personaje, una salida) y de acción (`actions` de una charla). Una condición se cumple si la bandera vale `valor`, o si la prueba es (1) o no es (0)
verdadera; una acción pone la bandera en `valor`.

| Nombre | Referencia | Significa |
| --- | --- | --- |
| `flag` | `"GG:III"` (hex: grupo y bandera) | bandera `III` del grupo `GG` (00 `FLAGS_00`, 02..40 `GAME.flags*`) |
| `progress` | número | `GAME.progress` es (1) o no es (0) ese valor |
| `special` | `"0xNN"` o `"<mod>:<nombre>"` | fila `NN` de `SPECIAL_CONDITIONS`; si el exportador la conoce, un 4.º elemento de texto la describe (`"partner.in-party(1)"`) y se ignora al compilar. `"<mod>:<nombre>"` es una condición que un mod agregó en `tables/special_conditions.json` (S3, `docs/data-tables.md` §3.9): ids `0x100` a `0x1FF` |
| `party-stat` | número | el total de estadística de la party llega (1) o no (0) al umbral |
| `event-battle` | número | acción: empieza la batalla de evento |
| `card-battle` | `"oponente:variante"` (hex) | acción: batalla de cartas |
| `shop`, `card-shop`, `inn` | número | acción: tienda (0-29), tienda de cartas (49-67 y 70-74) o posada (el resto) |
| `lab`, `naming` | (sin referencia) `["lab", valor]` | acción: laboratorio / nombrar |
| `place`, `place-arg` | número desde 1 | `GAME.place` / `GAME.placeArg` llegados por un warp |
| `item` | `"dw3:item/<id>"`, `"<mod>:<clave>"` o número | el jugador tiene (1) o no (0) el objeto, en la bolsa o equipado; da o quita (acción). Un 4.º número (0-7) es la clase del objeto: el juego no la lee |
| `card` | `"dw3:card/<id>"` | tiene o no tiene la carta; da o quita (acción) |
| `start-event` | número | acción: empieza el evento listado |
| `training` | número | acción: entrenamiento |
| `give-money`, `take-money`, `set-progress`, `warp`, `sound` | ver §9 | acciones nuevas del puerto (S7) |
| `raw` | `"0xHHHH"` | cualquier código sin nombre; ver abajo |

Los nombres de objetos y cartas siguen el registro de contenido (ADR-009): `"dw3:item/18"` para un original, `"<mod>:<clave>"` para uno nuevo. Sin un registro, la
salida usa `dw3:item/<id>` para todo id; con uno (`StageCodeEnv.nameOf`), un id sin nombre queda como número.

**Sin pérdida.** Compilar y descompilar es una vía de ida y vuelta para cualquier lista: un código sin nombre se escribe `["raw", "0xHHHH", valor]`. En el
disco EU no aparece ninguno (todas las listas usan nombres). `PROGRESS` solo compara igualdad; los rangos de progreso son filas de `SPECIAL_CONDITIONS`
(primitiva `progress-range`).

Los eventos usan otro vocabulario, el de `FIELDSTG_runEvent` (`SCRIPT_*`): una lista de comandos que termina con `["end"]`:

| Comando | Argumentos |
| --- | --- |
| `place` | `id, x, y` |
| `pose` | `id, set, dir` |
| `command` | `id (>= 800), comando, arg` (`SCRIPT_COMMAND`: un `pose` cuyo id es un comando del script) |
| `walk` | `id, x, y, dir` |
| `talk` | `caja, línea, id, tipo` |
| `wait` | `cuadros` |
| `wait-box`, `wait-walk id`, `wait-anim id` | |
| `leave` | `modo, x, y, dir` |
| `follow` | `snap, id` |
| `look-at` | `snap, x, y` |
| `end` | termina; `["end", "0xHHHH"]` si la palabra final es de un tipo que el juego no conoce |
| `raw` | `"0xHHHH", args...`: una variante (sub-comando) que el juego corre como la 0 de su tipo |

## 5. Qué cubre y qué no

Cubierto: inicio, música y banco, color de sprites, personajes (posición, imagen, condiciones, charlas con condiciones y acciones, el texto de la charla como vista previa),
salidas (`StageSlot`), eventos con su guion completo y las líneas que muestra, y la lista de funciones de C. Todo lo que el juego lee de las tablas que `setupStage` entrega.

Fuera del modelo, y por eso en `notes`:

- Las funciones de C del escenario (`functions`): animaciones de baldosas, tareas, cinemáticas con código propio (unos 42 escenarios; Lua queda para después, decisión del dueño).
- Los eventos que empiezan una tarea de C (`start`, sin guion) o corren una función al terminar (`end`).
- Lo que `setupStage` elige según el progreso (música, banco, batallas y salidas de 42 escenarios): el archivo trae el progreso 0.
- Las batallas (`battles`): se exportan (S10) del lugar que el `setupStage` eligió con `GAME.place`; 26 escenarios eligen por lugar con una lista que no tiene el lugar 0 con el que se exporta, y llevan una nota en vez de `battles` (el parche sí ve el lugar real). Los objetos del mapa (`nobjects` solo cuenta); las imágenes de los personajes por dentro (los sprites propios de un mod, §11).
- Los personajes que el campo propio (FIELDSTG) agrega o cambia; el escenario 260 es el campo propio (modo 528, `FIELDSTG_setupField`).
- Una tabla de salidas sin fila final en la fuente (`WSTAG530`, `WSTAG615`, la primera tabla): el exportador corta en el último registro válido y lo anota.

## 6. El exportador

`pcExportStageData` (`pc/boot.c`), al arrancar y antes de la primera ventana: corre el `setupStage` de cada escenario (la tabla `SD_SETUPS` sale de los objetos de los escenarios, regla
`stage_setups.c` de `pc/Makefile`) con el progreso de 0 a 99 para ver qué cambia, lee el de progreso 0 al modelo, escribe `data/stages/<nnn>.json` y lo vuelve a leer para comprobar que es el mismo
escenario. Guarda y devuelve lo que el setup toca (`FIELDSTG_state`, `FIELDSTG_map`, `GAME`, las estáticas de los escenarios), así que con o sin la exportación el juego es idéntico
(captura de un escenario byte a byte igual). Medido en el disco EU con una carpeta de usuario nueva: 294 escenarios en unos 0.3 s; el arranque que lo salta, sin costo apreciable.
Cualquier cambio en el formato sube `SD_EXPORT_VERSION` y todos los archivos se reconstruyen (los editados se quedan y la nueva versión va como `<nnn>.json.new`).

Prueba: `make -C pc stagedatatest` (ida y vuelta de los 65 535 códigos y de todas las formas de guion; un escenario sintético a JSON y de vuelta; la regla del manifiesto).

## 7. Parches de mods: `stages/<nnn>.json` (`stage-patch-v1`) y banderas propias

Un mod cambia los personajes de un escenario con `mods/<mod>/stages/<nnn>.json` (`<nnn>` es el número del escenario, el mismo de `data/stages/<nnn>.json`). El
esquema JSON está en `docs/modding/schema/stage-patch.schema.json`; el ejemplo es `pc/examples/mods/stage-npc/`.

```json
{
  "format": "stage-patch-v1",
  "actors": {
    "patch":  [{"index": 0, "x": 850, "y": 950, "dir": 1},
               {"index": 2, "prependTalks": [{"entry": 12, "conditions": [["mod-flag", "mimod:met", 1]]}]}],
    "add":    [{"key": "greeter", "character": 43, "image": 6, "x": 950, "y": 950, "dir": 3,
                "talks": [{"entry": 636, "conditions": [["mod-flag", "mimod:met", 1]]},
                          {"entry": 631, "actions": [["mod-flag", "mimod:met", 1]]}]}],
    "remove": [5, "otromod:guardia"]
  }
}
```

- **`patch`**: cambia campos de un personaje que ya existe. Se nombra por `"index"` (su lugar, desde 0, en la lista del escenario: es el `index` del archivo de referencia) o por
  `"key"` (uno que un parche agregó, `"<mod>:<clave>"`, o solo `"<clave>"` si es de este mismo mod). Campos: `character`, `image`, `x`, `y`, `dir`, `conditions` (reemplaza las condiciones para que esté;
  `null` = siempre), `talks` (reemplaza todas sus charlas; la última sin condiciones) y `prependTalks` (charlas que van antes de las que tiene; la última original sigue cerrando la lista).
- **`add`**: personajes nuevos, después de los del escenario. Llevan un `"key"` (otros parches los nombran `"<este mod>:<clave>"`), `character`, `image`, `x`, `y` y, si quieren, `dir`, `conditions` y `talks`.
  Con `image` el personaje usa una imagen de actor que el escenario ya tiene (la lista dice cuáles; otra se omite con una línea en el log). Sin `image` el puerto le da un espacio
  de VRAM propio, y entonces `character` puede ser cualquiera del juego o un `sprite` propio del mod (§11).
- **`remove`**: un índice o una clave. Se aplica al final de cada archivo.
- **Charlas**: las listas de §4 (`conditions`, `actions`). `entry` es un número de línea del archivo de texto del propio escenario; un texto propio va por nombre, `"text"` (§8).
  La primera charla cuyas condiciones se cumplen es la que se dice; la última de una lista que el parche da no puede tener `conditions`. En vez de `entry`, una charla puede decir `"text": "<mod>:<nombre>"`: un texto propio del mod (§8).
- **Orden y conflictos**: el original primero, después cada mod en el orden de carga (el último gana). Cada archivo aplica `patch`, `add`, `remove` en ese orden. Si dos mods ponen el mismo campo
  del mismo personaje, vale el del último y el log dice `"<campo>" overrides the value <mod> set`. Las charlas de `prependTalks` del último mod quedan primero.
- **Lo que no se aplica**: un `index` que no existe al progreso actual (42 escenarios cambian su lista con la historia), un personaje ya quitado o una `image` que el escenario no tiene se omiten con
  `<mod>: stage N actor X ... skipped: <motivo>` y el resto del parche se aplica. Un archivo que no es válido se rechaza entero (`[stages] <ruta> rejected: <motivo>`; `--validate-mod` lo muestra).
- **Cuándo**: el gancho corre justo después del `setupStage` del escenario (`startField`, `src/field/field_mode/field.c`), así que el parche se aplica a lo que el escenario entregó esta vez, no a la
  copia de progreso 0 del archivo de referencia. Se leen `FieldState.actors` y sus charlas al modelo, se mezcla, y el resultado va a buffers del puerto que viven lo que el escenario
  (`pcStagePatchHook` en `pc/boot.c`; `FieldState.actors` pasa a apuntar a ellos). Sin parche para ese escenario no se cambia nada: el puntero sigue siendo el del juego y la imagen es idéntica byte a byte.
  Los escenarios con código C propio (cinemáticas) solo cambian sus arreglos; sus tareas no se tocan, y si cuentan personajes por el orden en que el campo los crea, un parche que quita o agrega
  los desplaza. El campo guarda ahora hasta 64 personajes del mapa en vez de 15 (`FIELD_NPC_MAX`).
- Salidas, eventos con guion y batallas del escenario van en §10 (S8, S10). Los personajes y sprites propios van en §11 (S9). No cubre (otras tareas): el escenario 260 (el campo propio).

### Banderas y contadores de mods (S5)

Tres códigos más para condiciones y acciones, por nombre `"<mod>:<nombre>"` (el mod, dos puntos, letras minúsculas, dígitos, `_`, `-`, `/`):

| Código | Condición | Acción |
| --- | --- | --- |
| `["mod-flag", "<mod>:<nombre>", 0\|1]` | la bandera vale 1 (o 0) | la pone en 1 (o 0) |
| `["mod-var", "<mod>:<nombre>", ">=", n]` (opcional un 5.º elemento 0\|1: el valor que se espera, 1 por omisión) | la variable cumple la comparación (`==`, `!=`, `>=`, `>`, `<=`, `<`) con `n` | nada |
| `["set-var", "<mod>:<nombre>", n]` | siempre se cumple | la variable pasa a valer `n` (0 a 65535) |

Una bandera es una variable que vale 0 o 1; todas valen 0 hasta que algo las pone. Cualquier mod puede leer las de otro (la bandera de un mod que no está instalado vale 0).

- **Intérprete.** El juego ejecuta estos códigos con `checkCondition` / `applyAction` (`src/engine/game/events.c`), que solo conocen números. El puerto reserva los grupos `0x62`-`0x68` (el tipo es
  `code >> 8 & 0xFE`; el juego no usa ninguno de ellos): el compilador de códigos hace una tabla de operaciones (hasta 2 048: 512 por grupo) y el código de cada una es `0x6200 + 512 por grupo + índice`.
  Un `#ifdef DW3_PC` en las dos funciones llama a `pcModCheck` / `pcModApply` (`pc/shim/stage_mods.c`) para esos grupos; la compilación de PS1 no cambia (`make compare`: 316 OK). Un código del grupo
  que la tabla no hizo no se cumple. En el JSON de §3 los códigos se escriben por nombre, nunca por número; sin tabla (`StageCodeEnv.modOp`) salen como `raw`.
- **Dónde viven.** En `ext_state` por nombre (`pcExtVarGet` / `pcExtVarSet`, `pc/shim/ext_state.h`), no en `GAME`: no dependen de qué mods hay ni de su orden.
- **En la partida.** Sección `EXTV` del archivo `dw3save` (la misma familia que `EXTI`, `EXTT`, `EXTD`, `EXTP`, `EXTK`): versión (1), cuántas, y por cada una el nombre (u16 + bytes) y el valor (s32, little endian).
  Solo se escriben las que no valen 0 y no hay sección si no hay ninguna. Al cargar se **conservan todos los nombres**, también los que ningún mod instalado usa: desactivar un mod y volver a
  activarlo no pierde su progreso, y lo que nadie evalúa no se toca. Una partida de antes (sin la sección) carga sin variables; una sección dañada deja todas en 0 con una línea en el log.
  Una partida nueva (`newGame`) y cargar otra las olvidan.
- Prueba de arnés: `DW3_MODVAR="<mod>:<nombre>=1,..."` pone variables cuando empieza el primer escenario parchado (después del reinicio de la partida nueva); `DW3_LOG=trace` imprime por cada personaje
  de la lista final si el campo lo crearía ahora y cuál de sus charlas diría (evaluando con el intérprete del juego, sin aplicar nada).

Prueba: `make -C pc stagemodtest` (mezcla de `patch`/`add`/`remove`/claves/`prependTalks`, el último mod gana, índice o imagen que faltan, archivos inválidos, códigos y variables, ida y vuelta de `EXTV` con nombres
desconocidos conservados, partida vieja sin sección y sección dañada) y `make -C pc memcardtest` (la sección en el archivo de la ranura y la carga real).

## 8. Textos con nombre para las charlas (S6)

Una charla de un parche dice su línea por nombre en vez de por número: `{"text": "<mod>:<nombre>", "conditions": [...], "actions": [...]}` (con `entry` o con `text`, nunca los dos). Las palabras viven en
`mods/<mod>/text/<idioma>/stages.json`, una carpeta por idioma como los demás textos (`eng`, `fra`, `ita`, `ger`, `spa`, `es-419`); el formato es `stage-text-v1` (esquema `docs/modding/schema/stage-text.schema.json`):

```json
{"format": "stage-text-v1", "texts": {"welcome": "Welcome to the park, friend!", "again": "Good to see you again."}}
```

- El nombre completo que usa el parche es `"<carpeta del mod>:<nombre>"` (nombre con a-z 0-9 `_` `-` `/`). Cualquier mod puede usar el texto de otro, como las banderas.
- **Inglés obligatorio.** Un nombre sin texto en inglés rechaza el archivo del parche que lo usa; un idioma al que le falta el nombre muestra el inglés (es-419 prueba el español primero). El validador lo dice: un nombre que
  ningún mod define o sin inglés es un error del parche (`stages/<n>.json rejected: ... the text "<mod>:<nombre>" is not defined`), y un idioma que un mod da para unos nombres y no para otros es un aviso
  (`text/<idioma>/stages.json: "<nombre>" is missing here and skipped: the English line is shown instead`). Un archivo `stages.json` con una línea vacía o que el códec de texto no puede escribir se rechaza entero.
- **Cómo llega al juego.** `smLoad` lee todos los textos de todos los mods antes de los parches. Cuando empieza un escenario con charlas con nombre (`pcStagePatchHook`), los nombres que sus parches usan reciben una entrada propia
  *después* de las entradas del archivo de texto del escenario (`tdStageTextFirst` dice cuántas tiene el disco en inglés); la charla pasa a ese número. Todos los nombres de los parches del escenario se reservan juntos, usados o no
  con el progreso de esa visita, así que las entradas no cambian de una visita a otra. La capa de texto (`pc/shim/text_data.c`, `tdSetStageTextHook`) agrega esas entradas a la copia de cada idioma del archivo (con o sin el texto de un mod),
  rellena con entradas vacías un idioma que tenga menos entradas que el inglés y deja intacto uno que tenga más (con una línea en el log), y las ajusta a la caja de charla con el mismo reacomodo que el texto de un mod
  (`localization/layout.json`, regla `TALK*`: la entrada nueva cuenta como escrita por un mod y usa el ancho de la caja de charla).
- Un escenario cuyo archivo de texto el puerto no puede extender (sin archivo de texto, o un paquete) deja la charla en la entrada 0 con una línea en el log. Dos escenarios que comparten un archivo de texto comparten sus entradas
  agregadas: el segundo en empezar puede agregar entradas nuevas (la copia servida se vuelve a armar; si el juego ya tenía el archivo en memoria, se ve en la siguiente carga).
- El texto de un evento con guion: los eventos del juego leen un bloque de líneas propio (`text` = archivo y entrada, y el guion numera líneas dentro de ese bloque); un evento que agrega un mod nombra sus textos en los `talk` de su guion (§10) y esos nombres son entradas del archivo de texto del propio escenario.

Prueba: `make -C pc stagemodtest` (nombres, idiomas, rechazos, entradas, vuelta de un escenario) y `make -C pc texttest` (`tdAppendTexts`: entradas agregadas, relleno, rechazo, reacomodo).

## 9. Acciones nuevas del puerto (S7)

Cinco acciones que PS1 no tenía, cada una una llamada a una función del motor que ya existe. Viven en los grupos reservados `0x6A` y `0x6C` de `code >> 8 & 0xFE` (después de los de las banderas: el juego no usa ninguno),
512 operaciones por grupo en una tabla que arma la capa de parches (hasta 1 024; `pcModApply` las corre con el ejecutor que instala `boot.c`). Como condición siempre se cumplen; el valor del par se ignora (compilar escribe 1).

| Acción | Qué hace | Función del motor |
| --- | --- | --- |
| `["give-money", n]` | `GAME.money` sube `n` (0 a 9 999 999), hasta `MONEY_MAX` | lo que hace `checkMoney` para las filas de dinero |
| `["take-money", n]` | `GAME.money` baja `n`, hasta 0 | ídem |
| `["set-progress", n]` | `GAME.progress` pasa a valer `n` (0 a 99) | la posición de la historia |
| `["warp", escenario, x, y, dir]` | sale del campo hacia ese escenario y aparece en `(x, y)` (píxeles 0-8191) mirando `dir` 0-7 | `FIELDSTG_leaveField(modo, -1, x << 8, y << 8, dir)`, lo que hacen las salidas; el modo es el primero de los modos del escenario (`pcStageModes`) |
| `["sound", "<nombre>"]` | suena uno de los sonidos compartidos del juego | `playSound`; nombres: `select cancel cursor menu-open menu-close menu-move menu-confirm count recovery teleport win-jingle inn-jingle` |

Corren donde corren las acciones de una charla (al terminar de hablar), como las de tienda o posada, que también salen del campo desde `applyAction`. El validador rechaza un número fuera de rango, un escenario que la compilación no tiene
y un sonido que no existe, con el motivo. `give`/`take` de un objeto de un mod ya existían: `["item", "<mod>:<nombre>", 1|0]` (registro de contenido, ADR-009).

Con las filas de `special_conditions` que un mod agrega (S3) quedan otras acciones sin código nuevo: un `["special", "<mod>:<nombre>", 1]` con la fila `{primitive: 1 (compañero), op: 3, arg: n}` desbloquea al compañero `n` y
`op: 8` cura al grupo.

**No hay (con el motivo).** *Dar un Digimon o una forma de compañero*: el motor no tiene un camino seguro y suelto (las formas se cambian por el laboratorio y las evoluciones, que leen y escriben `Partner` con su propio
estado); lo único que existe es desbloquear un compañero por la fila `partner.unlock` de arriba.

Con S8 y S10 hay dos más (§10): `["start-battle", encuentro, escenario_de_lucha, musica]` y `["run-event", "<mod>:<nombre>"]`.

**Cómo salen del campo (corrección de S7).** Una acción de charla no cambia el escenario ni empieza una batalla ella misma: `warp`, `start-battle` y `run-event` dejan una petición (`smPostRequest`, una sola, la
última gana) y la tarea del campo la toma en su siguiente cuadro de ejecución (`pcStageWarpPoll`, llamado al principio de `TASK_RUN` en `src/field/field_mode/field.c`) y pide `FIELDSTG_leaveField`,
`FIELDSTG_startBattle` o `FIELDSTG_startEvent`: lo mismo que usan las salidas y las batallas de evento del juego, pero desde el cuadro del campo y no desde dentro de la charla. La primera versión llamaba a
`FIELDSTG_leaveField` en el acto; con la prueba `DW3_STAGE_APPLY`, que aplica las acciones dentro del arranque del escenario, el campo todavía no tenía su jugador (`children->actors[0]` nulo) y
`FIELDSTG_closeField` fallaba en `field.c:283` (causa vista con gdb).

Prueba: `make -C pc stagemodtest` (compilar y descompilar cada acción, rechazos con motivo, la tabla de operaciones y su tope, el ejecutor que recibe los números). En el disco real, con `DW3_LOG=trace` y la prueba de arnés
`DW3_STAGE_APPLY="<personaje>[:<rondas>]"` (aplica las acciones de la charla que el intérprete elige para ese personaje de la lista final, `rondas` veces, y registra dinero y progreso).

## 10. Salidas, eventos con guion y batallas de área (S8, S10)

`stage-patch-v1` tiene tres secciones más, al lado de `actors`: `slots`, `events` y `battles`. Mismas reglas que los personajes: el original es lo que el `setupStage` entregó esta vez, cada mod aplica
en el orden de carga (el último gana, un conflicto va al log), cada archivo aplica `patch`, `add`, `remove` en ese orden, un índice que no existe a este progreso se omite con una línea y un archivo que no es
válido se rechaza entero. Los datos se escriben a buffers del puerto después del `setupStage`, dos juegos que alternan (`pcSlotsInstall`, `pcEventsInstall`, `pcBattlesInstall` en `pc/boot.c`; `FieldState.slots`,
`.events` y `.battles` pasan a apuntar a ellos); sin parche para esa parte no se cambia nada. Ejemplo: `pc/examples/mods/stage-npc/`.

```json
{
  "format": "stage-patch-v1",
  "slots":   {"patch":  [{"index": 0, "stage": 330, "x": 700, "y": 800, "dir": 3, "conditions": [["mod-flag", "mimod:abierto", 1]]}],
              "add":    [{"key": "portón", "stage": 380, "x": 1186, "y": 874, "dir": 3, "area": {"x": 960, "y": 990, "w": 48, "h": 40}}],
              "remove": [1]},
  "events":  {"patch": [{"id": 205, "script": [["wait", 60], ["end"]]}],
              "add":   [{"key": "gracias", "script": [["wait", 20], ["talk", 0, "mimod:gracias", 0, 4], ["wait-box"], ["end"]]}]},
  "battles": {"patch": [{"area": 1, "rate": 4}, {"area": 2, "index": 3, "encounter": 35, "stage": 1, "music": 1611137024}],
              "add":   [{"area": 5, "rate": 2, "slots": [{"encounter": 40, "stage": 1, "music": 1611137024}]}]}
}
```

### Salidas (`slots`)

- **Qué es un slot.** Un registro de `StageSlot` (`include/field/field_map.h`): `type` (`exit`, `climb-up`, `event`, `warp-1`... los de `scSlotTypeName`, o 1 a 14), `arg` (por tipo: un modo, un evento, una altura),
  `x`, `y`, `dir` (adónde sale), `hideAnim`, `place`, `placeArg` y hasta dos `conditions` (las de §4). **Dónde se dispara** lo dice el mapa: la capa de disparadores de cada mapa (`FIELD_MAP_TRIGGERS`) pone en cada
  celda el índice del slot (5 bits, 32 como mucho) y la dirección hacia la que hay que mirar. Un parche no edita mapas.
- **`patch`** (por `index`, el lugar en la tabla del escenario, o `key`): campos `type`, `arg`, `x`, `y`, `dir`, `hideAnim`, `place`, `placeArg`, `conditions` y `area`. En vez de `arg`: `"stage": N` (el modo del
  escenario N, el primero de sus modos, como `warp`: la salida va a ese escenario) o `"event": "<mod>:<nombre>"` / un número (el evento que empieza un slot `event`). Se puede dar solo uno de `arg`, `stage`, `event`.
  Más de dos condiciones se rechazan.
- **`add`** (con `key`): un slot nuevo, después de los del escenario. Ninguna celda del mapa apunta a él, así que **lleva `area`**: un rectángulo del mapa en píxeles `{x, y, w, h}` (los mismos píxeles que los personajes) y,
  si quiere, `facing` (0-7, hacia dónde hay que mirar; sin él se dispara estando dentro, mirando a donde sea). `type` es obligatorio, salvo que se dé `stage` (implica `exit`) o `event` (implica `event`).
  El campo busca antes en las áreas (la última que se agregó gana) y después en las celdas del mapa (`FIELDSTG_findTrigger`, `#ifdef DW3_PC`). Sin tope de 32 ni de celdas. Un `climb`, `drop` o `gauge` agregado necesita
  que el archivo de su animación se haya pedido: solo lo pide el campo al cargar si algún slot original lo es.
- **`remove`**: un índice o una clave. El slot se queda en su lugar con el tipo del puerto `SLOT_OFF` (15) y sin condiciones: el mapa nombra los slots por índice y quitarlo de la tabla correría a los demás. El campo lo
  salta.
- **Lo que el modelo agrega**: `SdSlot.hasArea/ax/ay/aw/ah/facing` (solo del puerto; el exportador de lo del juego no los escribe, así que la referencia no cambia).

### Eventos con guion (`events`)

- **Qué es un evento.** Un `FieldEvent`: un `id` y, o un `script` (los comandos de `FIELDSTG_runEvent`: `place`, `pose`, `walk`, `talk`, `wait`, `wait-box`, `wait-walk`, `wait-anim`, `leave`, `follow`,
  `look-at`; §4), o una tarea de C (`start`), y una función de C al terminar (`end`). Un slot `event` los empieza al pisarlo y una charla puede empezar uno con la acción `run-event`.
- **`patch`** (por `id`, uno de la tabla del escenario, o por `key`): `script` reemplaza el guion y `text` (`{"file", "entry"}`, el bloque de líneas del juego) reemplaza el texto. El evento **conserva** su `start`
  y su `end` de C (el puerto los copia del original): si un parche le pone guion a uno que empezaba una tarea de C, corre el guion (el juego prefiere el guion) y la función `end` sigue corriendo al terminar. Las vistas
  previas del evento se descartan (describían el guion que se fue).
- **`add`** (con `key` `"<mod>:<nombre>"`, y `script`): un evento nuevo, sin `start` ni `end`. El puerto le da un id desde 30 000 (`smEventId`, el mismo nombre es el mismo id toda la corrida; el juego usa menos de 10 000).
  Los ids de la tabla se ponen después de los del escenario.
- **Textos.** Los eventos del juego leen un bloque de líneas propio (su `text` apunta a una entrada de un archivo de eventos y el `talk` numera líneas dentro de ese bloque). Un evento del mod no tiene ese bloque: la
  entrada de un `talk` es una línea del archivo de texto **del propio escenario**, la misma que usan las charlas de los personajes: un número, o un nombre `"<mod>:<nombre>"` de §8 (las entradas se agregan al archivo del
  escenario, para todos los idiomas). Se marca poniéndole al evento el `text` especial `entry` 65 535 (`SM_EVENT_TEXT_STAGE`) y `FIELDSTG_createSpeech` (`speech.c`, `#ifdef DW3_PC`) lee entonces el archivo de texto del
  escenario. Un evento del juego al que un parche le pone un `talk` con nombre pasa a leer también del archivo del escenario.
- **Empezarlos.** `slots` con `"event"` (se empieza al pisar), o una charla con `["run-event", "<mod>:<nombre>"]` (o un número, un id del propio escenario): la petición la toma la tarea del campo en su siguiente
  cuadro (§9) y pide `FIELDSTG_startEvent(id)`, lo que hace `FIELDSTG_startListedEvent` para las charlas del juego.

### Batallas de área (`battles`, S10)

- **Qué hay.** `FieldState.battles` es el `FieldBattles` del lugar (`GAME.place`) que el `setupStage` eligió con `findBattles`: cuatro `BattleList` (la área 1 a 4 del mapa; la capa de áreas del mapa dice en cuál está
  el jugador, y la cuarta es la que empiezan los eventos) con una tasa (`rate`, índice de `FIELDSTG_battleRates`: las `battle_rates` de E5c) y ocho `AreaBattle {encounter, stage, music}`: el encuentro (fila de
  `FIELDSTG_encounters`: `field_encounters`), la etapa de lucha (`BATTLE_SETUP.stage`) y la música (`BATTLE_SETUP.music`, un valor de `SOUND`, número grande). Una batalla se elige con `RANDOM & 7`. No hay condiciones en
  la estructura (verificado en `field_map.h`): los 214 símbolos `stageBattles` son solo listas.
- **Exportación.** La sección `battles` del `stage-v1` ({`place`, `serial`, `areas`: [{`area`, `rate`, `slots`: 8 x {`encounter`, `stage`, `music`}}]}): 174 de los 294 escenarios, 696 áreas. `SD_EXPORT_VERSION` pasó a 2.
  26 escenarios eligen por `GAME.place` y no tienen lista para el lugar 0 con el que se exporta (`findBattles` recorre 30 registros más allá de su tabla y un vecino en ceros calza): el lector solo sigue una lista cuyo
  `serial` está entre 1 y 448 y cuyo `id` es el lugar pedido, y si no, deja una nota en vez de `battles`.
- **`patch`**: `area` (1 a 16), `index` (una de las 8) o `indices` (varias; sin ellos, las 8), y `encounter`, `stage`, `music`, `rate`. `encounter` se valida contra las filas de `field_encounters` (1 a 334; para enemigos
  de mods se cambian los enemigos de esas filas, `field_enemies`, con las tablas de E5c).
- **`add`**: `area` nueva, `rate` y `slots` (1 a 8 batallas; menos de 8 se repiten para llenar). Tiene que ser la **siguiente** a la última del escenario (4 -> 5 -> 6...); el juego indexa `battles[area]` con el valor de
  la capa de áreas del mapa, y el puerto deja la tabla más larga que sus cuatro punteros (`pcBattlesInstall`). Como los mapas no se editan, una área nueva solo se usa si el mapa de ese escenario ya tiene celdas de área
  con ese valor.
- **Acción `start-battle`** (S10, §9): `["start-battle", encuentro, escenario_de_lucha, musica]`: la misma `FIELDSTG_startBattle(AreaBattle)` de las batallas de evento del juego, desde el cuadro del campo.
  Los valores de `stage` y `music` que conviene usar son los de `battles` del propio escenario exportado.

Prueba: `make -C pc stagemodtest` (`testStageData`: parches, altas y bajas de slots con áreas, eventos con textos con nombre y conservación de `start`/`end`, batallas por índice, por lista y áreas nuevas, rechazos con
motivo; `start-battle` y `run-event` compilar/descompilar) y `make -C pc stagedatatest` (ida y vuelta).

## 11. Personajes y sprites propios (S9)

Hasta S8 un personaje agregado tenía que reusar un `character` y una `image` que el escenario ya cargaba (la VRAM de la PS1 reparte los espacios de imagen de los actores al armar el escenario). S9 quita ese límite
con un espacio de VRAM propio del puerto para cada personaje nuevo.

- **Sin `image`.** Un actor (en `add`, o en `patch` que cambia `character`/`sprite`) que no trae `image` pide un espacio propio (`SD_IMAGE_AUTO`). `smApplyStage` numera un espacio por cada `character` distinto, desde
  la imagen 20 (las del juego llegan a 19: `FieldImages.actors[20]`) y `pcImagesInstall` (`boot.c`) copia la tabla de imágenes del escenario con esas entradas al final (`FIELDSTG_state.images.actors`). Dos actores del
  mismo `character` comparten el espacio. Con `image` nada cambia respecto de §7.
- **`character` del juego.** Cualquier id que el juego tenga con archivo de sprites (`FIELDSTG_fileEntries`, `FIELDSTG_fileEntryCount`, solo `#ifdef DW3_PC`), aunque el escenario no lo cargue (p. ej. el 75, que es del
  escenario 380). El tamaño del espacio sale del mayor cuadro que usa en su archivo (`sprMaxFrame`); si no se puede medir, 64x64 y una línea de log. Un id sin sprite se rechaza con motivo al cargar el parche.
- **`sprite: "<nombre>"`.** Un personaje que el mod trae: la carpeta `mods/<mod>/sprites/<nombre>/` con un `sprite.json` (`sprite-v1`, el mismo de `images/field/sprt/<nombre>/` que escribe el exportador: cuadros,
  animaciones y conjuntos) y un PNG por cuadro (indexado de hasta 16 colores, el índice 0 transparente por `tRNS`, hasta 256x224 px). `"<mod>:<nombre>"` nombra el de otro mod. El miembro opcional `actor_width`
  (8 a 256 px) es el ancho para las colisiones del campo (por defecto, el cuadro más ancho). `character` y `sprite` juntos se rechazan; un `character` de 16384 o más también (esos son los sprites de mods).
  Los sprites se arman al cargar (`spritesLoad`) en un **banco** que es un solo archivo virtual del puerto (un espacio del caché de 64 archivos, sin importar cuántos sprites), con el mismo formato de los
  archivos de sprites del juego, así que `FIELDSTG_animateActor` los reproduce sin cambios. Cada uno recibe el id `16384 + k` (carpetas en orden de nombre por mod); `FIELDSTG_state.getFileEntry` y
  `getActorWidth` se envuelven para que esos ids apunten al banco. Se puede generar uno de partida con `pc/examples/tools/gen_art.py` (el ejemplo `stage-npc`: `rival`, una copia verde del sprite del jugador
  hecha con TUS datos exportados; el repositorio no lleva ningún PNG del juego, ADR-001).
- **Dónde viven en VRAM (la banda del puerto).** Los cuadros de un espacio los carga `FIELDSTG_animateActor` en `imageX`/`imageY`. La banda es el bloque x 0-319, y 256-495 (en halfwords): cinco páginas de textura
  de 64 halfwords en y 256 (se llenan por estantes; un espacio de cuadros de 32 px ocupa 8 halfwords) para los cuadros, en y 256-479, y las paletas (16 halfwords cada una, 20 por fila) en y 480-495. **Por qué ahí:**
  el cuadro que dibuja PsyCross vive en x 0-319, y 0-239 de la misma VRAM y la borra cada cuadro (la primera versión usaba y 16-255 y los personajes salían como ruido: se midió con el volcado de arte); el juego
  usa x 320 o más y las filas desde 496. Medido en el escenario 330; las imágenes de actores del juego están en páginas de x 320 en adelante (revisadas las de los 294 escenarios exportados).
- **Límites.** Espacios: hasta 160 de 32x48 px (o uno de 256x224), 320 paletas. Un mod trae hasta 16383 sprites; el banco tiene 65535 entradas. Lo que no cabe no rompe el escenario:
  `stage <n>: the character <id> gets no slot (<motivo>): its actors are left out` en el log (motivos: `no sprite file`, `the mods' sprite file is not there`, `the port's VRAM band has no room left`) y esos actores
  se quitan de la lista; los demás siguen. Un `sprite` sin carpeta válida se rechaza al cargar el parche con la causa.
- **Vistas previas de los eventos (arreglo de S8).** El archivo de texto de un evento es un archivo de bloques (el `text` del evento apunta a una entrada y el `talk` numera líneas dentro de ese bloque). El
  exportador leía la entrada como una línea suelta y las vistas previas salían basura (`"0"`, `"I"`). `pcStageLine` ahora lee la entrada como archivo de bloques (`textEntry`); tras reexportar con el disco EU,
  las 1693 vistas previas de los 450 eventos salen con su texto (`{hablante}Royal Guard...{fin}`; solo 2 quedan vacías).
- **API.** `SD_EXPORT_VERSION` pasó a 3 (las vistas previas de los eventos cambian de contenido: los archivos ya exportados se reescriben al reexportar). `sdActorFree` libera lo que posee un actor (la usa el anfitrión para quitar uno sin espacio).

Prueba: `make -C pc imagetest` (la banda: capacidad, estantes sin cruce, paletas, tamaños límite; el banco de sprites y sus rechazos) y `make -C pc stagemodtest` (`character` sin `image`, `sprite`, rechazos con motivo,
numeración de espacios). Ejemplo: `pc/examples/mods/stage-npc` (`visitor`, character 75, y `rival`, `sprite`).
