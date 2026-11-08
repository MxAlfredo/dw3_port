# Escenarios como datos (stage-v1)

Describe el modelo en memoria de un escenario del campo, su archivo
`stage-v1`, el vocabulario de códigos, qué cubre y qué no, y cómo un mod lo parcha (`stage-patch-v1`, §7) con banderas propias (§7), textos con nombre (§8) y acciones nuevas (§9). Solo hay estructura y conteos: ningún valor del juego (ADR-001). Los archivos
`data/stages/s<nnn>/stage.json` salen del disco del propio jugador y no se suben al repositorio.

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

`<datos>/stages/s<nnn>/stage.json`, una copia de referencia por escenario (en el `stages/manifest.json` único, que comparten las baldosas y los objetos del escenario; la misma regla `.new` de las otras familias de `data/`: una copia
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
`stage_setups.c` de `pc/Makefile`) con el progreso de 0 a 99 para ver qué cambia, lee el de progreso 0 al modelo, escribe `data/stages/s<nnn>/stage.json` y lo vuelve a leer para comprobar que es el mismo
escenario. Guarda y devuelve lo que el setup toca (`FIELDSTG_state`, `FIELDSTG_map`, `GAME`, las estáticas de los escenarios), así que con o sin la exportación el juego es idéntico
(captura de un escenario byte a byte igual). Medido en el disco EU con una carpeta de usuario nueva: 294 escenarios en unos 0.3 s; el arranque que lo salta, sin costo apreciable.
Cualquier cambio en el formato sube `SD_EXPORT_VERSION` y todos los archivos se reconstruyen (los editados se quedan y la nueva versión va como `stage.json.new`). Desde A7.2 el archivo vive en la carpeta del escenario (antes `stages/<nnn>.json`: ya no se lee; sus entradas salen del manifiesto y los archivos viejos quedan en su sitio, con una línea en el log).

Prueba: `make -C pc stagedatatest` (ida y vuelta de los 65 535 códigos y de todas las formas de guion; un escenario sintético a JSON y de vuelta; la regla del manifiesto).

## 7. Parches de mods: `stages/s<nnn>/stage.json` (`stage-patch-v1`) y banderas propias

Un mod cambia los personajes de un escenario con `mods/<mod>/stages/s<nnn>/stage.json` (`s<nnn>` es el escenario, el mismo camino de `data/stages/s<nnn>/stage.json`; el archivo del mod lleva su encabezado `stage-patch-v1`, el de `data/` es `stage-v1`). El
esquema JSON está en `docs/modding/schema/stage-patch.schema.json`; el ejemplo es `pc/examples/mods/training-field/stages/s330/stage.json`.

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
  ningún mod define o sin inglés es un error del parche (`stages/s<nnn>/stage.json rejected: ... the text "<mod>:<nombre>" is not defined`), y un idioma que un mod da para unos nombres y no para otros es un aviso
  (`text/<idioma>/stages.json: "<nombre>" is missing here and skipped: the English line is shown instead`). Un archivo `stages.json` con una línea vacía o que el códec de texto no puede escribir se rechaza entero.
- **Cómo llega al juego.** `smLoad` lee todos los textos de todos los mods antes de los parches. Cuando empieza un escenario con charlas con nombre (`pcStagePatchHook`), los nombres que sus parches usan reciben una entrada propia
  *después* de las entradas del archivo de texto del escenario (`tdStageTextFirst` dice cuántas tiene el disco en inglés); la charla pasa a ese número. Todos los nombres de los parches del escenario se reservan juntos, usados o no
  con el progreso de esa visita, así que las entradas no cambian de una visita a otra. La capa de texto (`pc/shim/text_data.c`, `tdSetStageTextHook`) agrega esas entradas a la copia de cada idioma del archivo (con o sin el texto de un mod),
  rellena con entradas vacías un idioma que tenga menos entradas que el inglés y deja intacto uno que tenga más (con una línea en el log), y las ajusta a la caja de charla con el mismo reacomodo que el texto de un mod
  (`localization/layout.json`, regla `TALK*`: la entrada nueva cuenta como escrita por un mod y usa el ancho de la caja de charla).
- **Cabecera de hablante del juego.** La etiqueta del hablante (`{hablante}...{hablante}`) es parte de cada línea del juego, no una tabla por personaje (`createTalkBox`, `src/engine/text/talk_box.c`; los letreros dicen `SIGN`/`LETRERO`/`SEÑAL` dentro de su propia entrada de `TALK04`, entrada 19, `wstag331.c:583`). Un texto de mod puede empezar con `{speaker-of:<ARCHIVO>:<entrada>}` (`ARCHIVO` es un nombre de `tdFileBase`, p. ej. `TALK04`): el puerto pone en su lugar la etiqueta que esa entrada tiene en el disco del jugador, en el idioma que se juega (`tcSpeakerRef`/`tcSpeakerLabel`, `stageTextAt` en `text_data.c`), así que el mod no copia ningún texto del juego. Un archivo desconocido o una forma mala rechaza el `stages.json` de ese idioma; si la entrada no tiene etiqueta, la línea sale sin cabecera (aviso en stderr). Los letreros de `training-field` usan `{speaker-of:TALK04:19}`.
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
| `["sound", "<nombre>"]` | suena uno de los sonidos compartidos del juego | `playSound`; nombres: `select cancel cursor menu-open menu-close menu-move menu-confirm count recovery teleport win-jingle inn-jingle` (no sirve para señalar un premio: eso es `["special", "0x13", 1]`, abajo) |

**Señal de objeto recibido.** La acción `["special", "0x13", 1]` (no es una acción del puerto: es la condición `SPECIAL_CONDITIONS {0x13, 0x50, 0}` del juego, `showActorIcon`, `events.c`) pone el ícono del jugador (tarea `0x16`, `FIELD_TASK_ICON`)
en su subestado 3: destella y suena `SOUND_ITEM_GET` al entrar (`actor_icon.c`). Es lo único que el original hace al dar un premio (sus charlas terminan en `{FLAG, ITEM, SPECIAL(0x13), 1}`; los guiones usan `["command", 813, 842, 2]`,
`SCRIPT_COMMAND(0x32D, 0x34A, 2)`), así que **va última**, después de `item`, `give-money` y las banderas, y no se agrega música ni `sound`. El ícono solo se crea con un modo anterior a `MODE_NEW_GAME` (`0x2D7`) mirando el modo
del respaldo (`pcModeAsOriginal`); en un escenario nuevo cuyo `fallback.stage` es posterior (p. ej. 880, modo 747) no hay ícono, y `showActorIcon` y los comandos 824/825/842 no hacen nada y escriben una línea `[field]` en el
log (`pcActorIconSignal`; antes desreferenciaban nulo y cerraban el juego). Con `DW3_LOG=trace` el canal `field` imprime `actor icon: told to play`, y con `SND_DEBUG=1` el sonido sale como `sep play id=1 rec=9`.
Prueba en el disco real: `["special", "0x13", 1]` en una charla de `stages/s330/stage.json` y en `stages/lower/` de `training-field`, y `["run-event", ...]` con `["command", 813, 842, 2]`: la línea del ícono y el sonido salen al cerrarse
el texto, y la captura difiere de la misma charla sin la acción solo en el recuadro del ícono (11 x 9 px junto al jugador).

Corren donde corren las acciones de una charla (al terminar de hablar), como las de tienda o posada, que también salen del campo desde `applyAction`. El validador rechaza un número fuera de rango, un escenario que la compilación no tiene
y un sonido que no existe, con el motivo. `give`/`take` de un objeto de un mod ya existían: `["item", "<mod>:<nombre>", 1|0]` (registro de contenido, ADR-009).

Con las filas de `special_conditions` que un mod agrega (S3) quedan otras acciones sin código nuevo: un `["special", "<mod>:<nombre>", 1]` con la fila `{primitive: 1 (compañero), op: 3, arg: n}` desbloquea al compañero `n` y
`op: 8` cura al grupo. El `arg` de una fila de compañero puede ser su nombre (`"arg": "<mod>:<llave>"`), que no se mueve cuando cambian los mods; con `op: 6` ("sigue bloqueado") como condición de la charla es una misión que
entrega un compañero de mod (`docs/modding/README.md` §6.9b).

**No hay (con el motivo).** *Dar un Digimon o una forma de compañero*: el motor no tiene un camino seguro y suelto (las formas se cambian por el laboratorio y las evoluciones, que leen y escriben `Partner` con su propio
estado); lo único que existe es desbloquear un compañero por la fila `partner.unlock` de arriba.

Con S8 y S10 hay dos más (§10): `["start-battle", encuentro, escenario_de_lucha, musica]` y `["run-event", "<mod>:<nombre>"]`.

**Cómo salen del campo (corrección de S7).** Una acción de charla no cambia el escenario ni empieza una batalla ella misma: `warp`, `start-battle` y `run-event` dejan una petición (`smPostRequest`, una sola, la
última gana) y la tarea del campo la toma en su siguiente cuadro de ejecución (`pcStageWarpPoll`, llamado al principio de `TASK_RUN` en `src/field/field_mode/field.c`) y pide `FIELDSTG_leaveField`,
`FIELDSTG_startBattle` o `FIELDSTG_startEvent`: lo mismo que usan las salidas y las batallas de evento del juego, pero desde el cuadro del campo y no desde dentro de la charla. La primera versión llamaba a
`FIELDSTG_leaveField` en el acto; con la prueba `DW3_STAGE_APPLY`, que aplica las acciones dentro del arranque del escenario, el campo todavía no tenía su jugador (`children->actors[0]` nulo) y
`FIELDSTG_closeField` fallaba en `field.c:283` (causa vista con gdb).

Prueba: `make -C pc stagemodtest` (compilar y descompilar cada acción, rechazos con motivo, la tabla de operaciones y su tope, el ejecutor que recibe los números). En el disco real, con `DW3_LOG=trace` y la prueba de arnés
`DW3_STAGE_APPLY="<personaje>[:<rondas>]"` (aplica las acciones de la charla que el intérprete elige para ese personaje de la lista final, `rondas` veces, y registra dinero y progreso). `DW3_SEED_ITEMS="id=cantidad,..."` también se aplica al arranque de un escenario con parche de personajes (T6), para que una charla que pide un objeto (la Medalla de Seiryu, `9=1`) lo encuentre.

## 10. Salidas, eventos con guion y batallas de área (S8, S10)

`stage-patch-v1` tiene tres secciones más, al lado de `actors`: `slots`, `events` y `battles`. Mismas reglas que los personajes: el original es lo que el `setupStage` entregó esta vez, cada mod aplica
en el orden de carga (el último gana, un conflicto va al log), cada archivo aplica `patch`, `add`, `remove` en ese orden, un índice que no existe a este progreso se omite con una línea y un archivo que no es
válido se rechaza entero. Los datos se escriben a buffers del puerto después del `setupStage`, dos juegos que alternan (`pcSlotsInstall`, `pcEventsInstall`, `pcBattlesInstall` en `pc/boot.c`; `FieldState.slots`,
`.events` y `.battles` pasan a apuntar a ellos); sin parche para esa parte no se cambia nada. Ejemplo: `pc/examples/mods/training-field/`.

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
  El campo busca antes en las áreas (la última que se agregó gana) y después en las celdas del mapa (`FIELDSTG_findTrigger`, `#ifdef DW3_PC`). Sin tope de 32 ni de celdas. Un `climb`, `drop` o `gauge` agregado también
  pide el archivo de su animación: el campo (`FIELDSTG_requestSlotFiles`, `pc/field/file_loader.c`, la copia que compila el port) lee `FieldState.slots` después del gancho de parches, así que ve los slots agregados (probado con la pesca de §14). Un `gauge` agregado sin `conditions` lleva la Caña de pescar (`dw3:item/5`).
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
- **`sprite: "<nombre>"`.** Un personaje que el mod trae: la carpeta `mods/<mod>/sprites/<nombre>/` con un `sprite.json` (`sprites-sprite-v1`, o `sprite-v1` como en `partners/<nombre>/field/`: el mismo de `sprites/<nombre>/` que escribe el exportador: cuadros,
  animaciones y conjuntos) y un PNG por cuadro (indexado de hasta 16 colores, el índice 0 transparente por `tRNS`, hasta 256x224 px). `"<mod>:<nombre>"` nombra el de otro mod. El miembro opcional `actor_width`
  (8 a 256 px) es el ancho para las colisiones del campo y para la caja donde se le habla (por defecto, el cuadro más ancho; la caja es la mitad de ancha y un cuarto de alta, `FIELDSTG_findActorAt`; los letreros del juego son actores de 32 px, `FIELDSTG_actorWidths[63]`, y con menos de 32 solo se leen bien de los lados). `character` y `sprite` juntos se rechazan; un `character` de 16384 o más también (esos son los sprites de mods).
  Los sprites se arman al cargar (`spritesLoad`) en un **banco** que es un solo archivo virtual del puerto (un espacio del caché de 64 archivos, sin importar cuántos sprites), con el mismo formato de los
  archivos de sprites del juego, así que `FIELDSTG_animateActor` los reproduce sin cambios. Cada uno recibe el id `16384 + k` (carpetas en orden de nombre por mod); `FIELDSTG_state.getFileEntry` y
  `getActorWidth` se envuelven para que esos ids apunten al banco. Un sprite de partida: `pc/examples/mods/training-field/sprites/sign/` (un letrero dibujado a mano por `tools/make_art.py`; el repositorio no lleva ningún PNG del juego, ADR-001).
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
numeración de espacios). Ejemplo: `pc/examples/mods/training-field` (el guía usa el `character` 44 del juego y los letreros, `sprite`: `sign`).

## 12. Escenarios nuevos desde mods: el registro (T4, ADR-012)

`pc/shim/mod_stages.[ch]` guarda los escenarios que agregan los mods. Se nombran `<mod>:<nombre>` (llave), y esa llave es lo único que se guarda o se escribe.

- **Modo de sesión.** `msSeal` da modos desde `0x300` en el orden alfabético de la llave (no importa el orden de carga); hasta 256 (`0x300-0x3FF`). El juego ya trata `0x3xx` como campo: `MODE_ENTRY_POINTS[3]` es
  `FIELDSTG_start` (`overlay.c:5`) y `startField` agrupa `0x2xx`/`0x3xx` (`field.c:528`). El número de escenario de un mod para ranuras y `warp` es `0xF000 + índice`.
- **Orden de uso.** `msRegister` (todos los mods) → `msSeal` → `msSetFiles` por escenario en orden de modo → el anfitrión responde por `msByMode`. El cargador JSON (T5) llena `MsStage` (los campos de `FieldState` que fija un
  `setupStage`, más los números de archivo virtual).
- **`FIELDSTG_pickStage`.** `#ifdef DW3_PC` llama a `pcModStagePick` (`pc/boot.c`): un modo registrado usa `stageFile = 0` y el inicio genérico `pcModStageStart` (hace lo de `start_stage.inc.c` con los datos del registro);
  un modo de ese rango sin escenario corre el escenario de respaldo (Parque Central, `0x21D`) con una línea de log, en vez del `while (1)`.
- **Bandera de visita.** `pcFieldMarkVisited`: un modo de mod fija la variable `<mod>:visited.<nombre>` (EXTV); nunca un bit de `flags20`.
- **Archivos.** El TMPK (entrada 0 la hoja de objetos, 1 piso, 2 disparadores, 3 áreas) y el PACK son archivos virtuales (`pfsAddVirtualFile`). Verificado en el código: el `MapStreamer` y los streams leen con
  `CD_READER.read` y `FILE_CACHE` con el mismo lector, `cdRead` (`host_cd.c:113`), que sirve los virtuales por sectores.
- **Nombres en salidas.** `"stage": "<mod>:<nombre>"` en las ranuras y `["warp", "<mod>:<nombre>", x, y, dir]`; el registro tiene que estar sellado antes de `smLoad`.
- **Guardado (`EXTS`).** Nunca se guarda un modo de sesión. `msEncodeSaved` reemplaza `fieldMode`, `fieldPos` y `fieldDir` del bloque por el respaldo (y arregla el checksum) y escribe la llave con la posición real;
  `msRestoreSaved` lo repone si el escenario existe, y si no deja el respaldo con una línea `[saves]`. `host_memcard.c` lo cablea: `commitSlot` llama a `msEncodeSaved` antes de `writeSlotFile` (sección `EXTS`
  del archivo `.dw3save`) y la primera lectura de cada ranura (`stageDone`) llama a `msRestoreSaved`. Prueba: `make -C pc memcardtest` (guardar, recargar con el mod, recargar sin el mod).
- **Qué mira `mode >> 8 == 2`.** `PC_IS_FIELD_MODE` (`field_mode.h`) acepta `2` y `3`; `stage_timing.c` ya lo usa. `host_pad.c` no puede incluir `field_mode.h` (redefine `Encounter`, ver su espejo) y usa
  `MS_IS_FIELD_MODE` (`mod_stages.h`), igual; los 7 sitios de `host_pad.c` (`host_pad.c:381`, `1793`, `1975`, `2022`, `2111`, `2154`, `2323`) y `optLangLive` (`options_language.c:153`) significan "es un modo de campo"
  (lo dicen sus comentarios y lo que hacen: el actor del jugador, el diálogo, el cambio de idioma en vivo del campo).
- **Lo que indexa por modo.** `pcModeAsOriginal(mode)` (`pc/boot.c`, declarada en `game_state.h` tras `#ifdef DW3_PC`) da el modo del escenario de respaldo para un modo `0x3xx` (el del registro o `0x21D`) y deja igual
  los demás. La usan `getFieldZone` (`field_menu.c:124`; su único lector, `field_menu.c:159`, solo mira el signo: sin esto un `0x3xx >= 0x2D7` daba -1 y la opción 2 salía gris) y
  `FIELDSTG_createActorIcon` (`actor_icon.c:94`: el icono del jugador se crea solo antes de `MODE_NEW_GAME`). `STSTATUS_areaFuncs` (`helpers.c:410`, leído en `menu.c:109` y `map_screen.c:290-328`) se envuelve desde
  `pcInstallModStageStatus` (`pc/boot.c`): `getGameHalf`, `getArea` y `getVisitedAreas` ven el modo de respaldo durante la llamada y restauran `GAME.fieldMode` (sin editar `helpers.c`).
- **El cuelgue de `mode:3FF` (resuelto).** El port no compila `src/field/field_mode/state.c`: `pc/Makefile` lo reemplaza por `pc/field/state.c` (copia con el borrado de `FieldState` por nombre), y esa copia no tenía el
  gancho de `pcModStagePick`; un `0x3FF` caía en el `while (1) {}` de `FIELDSTG_pickStage` (sin `SIGTERM`: bucle ocupado). Ahora la copia lleva el mismo gancho.
- **Arnés.** `mode:3xx` fija `GAME.progress = 4` y siembra el grupo (como `mode:1000`), para que START abra el menú del campo. El token nuevo `reach:<hex>` termina la corrida la primera vez que el juego está en ese modo
  (y, en un campo, con el actor del jugador): `mode:3FF,reach:21D` entra por el respaldo y termina por estado; `mode:3FF,reach:1000,battle@300:start,90,cross` abre el menú del campo y la pantalla `STSTATUS` desde un modo de mod.
  Un escenario registrado de verdad se entra desde T5 (§13): `mode:300` (y siguientes, en el orden alfabético de las llaves) pone al jugador en el `start` del escenario.

Prueba: `make -C pc modstagetest memcardtest` (sin disco).

## 13. Escenarios nuevos desde mods: el cargador (T5, ADR-012)

`pc/shim/stage_new.[ch]` arma un escenario desde `mods/<mod>/stages/<nombre>/` al iniciar (los mods se leen una vez; hasta A7.3 era `new-stages/`, que ya no se lee). El escenario es `<mod>:<nombre>` (la carpeta; llave de 1 a 48 caracteres `a-z 0-9 - _`).
Regla de nombres (A7.3): dentro de `stages/` una carpeta llamada exactamente `s` + 3 dígitos (`s330`, en cualquier caja) es un escenario **original** y su `stage.json` es un `stage-patch-v1`; cualquier otra carpeta es un escenario **nuevo** (`stage-new-v1`). Una carpeta `s<nnn>` nunca es un escenario nuevo, aunque `s123` sea una llave válida: el cargador de escenarios nuevos la salta en silencio y el del parche dice, si el archivo trae otro `format`, `"format" must be "stage-patch-v1"` (un error, una línea). Un nombre que no es llave (mayúsculas, espacios, más de 48 caracteres) da el aviso `<ruta> ignored: a new stage's folder is named with ...` solo del cargador de escenarios nuevos; los lectores de originales (`smLoad`, baldosas, objetos, `stageObjPackValidate`, `stageModValidate`) saltan esas carpetas sin decir nada. Un `format` que no corresponde a la carpeta es un error de validación (`stage-patch-v1` en una carpeta nueva: `"format" must be "stage-new-v1"`).
Esquema: `docs/modding/schema/stage-new.schema.json`. Guía corta: `docs/modding/README.md` §6.11b. Prueba: `make -C pc newstagetest`.

### Archivos

| Archivo | Obligatorio | Qué es |
| --- | --- | --- |
| `stage.json` | sí | `stage-new-v1`: cabecera y secciones |
| `background.png` | sí | PNG indexado (color tipo 3, hasta 256 colores, sin entrelazado; el lector es `png_read.c`), ancho y alto múltiplos de 128, a lo más 255 mosaicos y 255 por lado |
| `floor.png` | sí | colisión; del mismo tamaño que el fondo |
| `triggers.png`, `areas.png` | no | del mismo tamaño; sin ellos, todo 0 |

**Las capas son de un píxel por píxel del mapa**, no por celda de 8 px (corrige la frase de ADR-012 §5.2): `FIELDSTG_getMapCell` baja hasta `pixels[i * 64 + (y & 7) * 8 + (x & 7)]` (`map.c:93-96`) y se consulta con `actor->tile`,
que es `pos >> 8` en píxeles (`actor.c:63-64`, `177-178`; `trigger.c:21`; `battle.c:26`, `45`). El valor de la capa es el **índice** del píxel del PNG (la paleta no cuenta). Una capa de `W x H` px lleva `W * H` bytes
que `mlEncode` deduplica (sin tope práctico para un mapa de pocos mosaicos; si el arte tiene más de 256 bloques distintos de 128 o 64 px, el escenario se rechaza con el motivo del códec).

- **Piso:** 0 cerrado, 1 plano, 2-7 pendientes, 16-23 pendientes espejadas (`formats.md` §4.3). Cualquier otro valor se rechaza (no se sabe qué hace el juego con él). Que el `start` caiga en 0 se avisa en el log (el jugador quedaría dentro de un muro), no se rechaza: *hipótesis* de que 0 bloquea al que camina, solo leída en `checkFlightProbe`.
- **Disparadores:** `dirección << 5 | ranura` (`trigger.c:22-23`); la ranura es el lugar de la salida en `slots.add`, empezando en 0 (con dirección 1 a 7: el valor 0 es "nada"); un valor con una ranura que `slots.add` no tiene se rechaza.
- **Áreas:** `n` es el `n`-ésimo `battles.add` (de 1); un `n` mayor que las áreas que el escenario define se rechaza, porque `battle.c:48` lee `battles[n - 1]->count` sin comprobar.

### Fondo (PACK)

`background.png` se corta en mosaicos de 128 x 128 (en orden de fila, `frame = columna + fila * ancho`). Cada uno es una imagen de 8 bits con un solo sprite que la muestra entera en el origen del mosaico (`mpTileSimple`, el caso más común del
disco), empaquetado con RLEN, y `mpPackBuild` arma el PACK; cada mosaico mide 17 KB sin comprimir, dentro del tope de 63 488 bytes (`map_pack.h`). La CLUT son los 256 colores de la paleta en 1555 de la PS1 (5 bits por canal) **con el bit 15
puesto** (opaco, así el negro `0,0,0` no es el transparente `0x0000`) y la entrada con `tRNS` en 0 pasa a `0x0000`. *Hipótesis:* el bit 15 es la convención de la PS1, y los mosaicos del disco no se miraron entrada por entrada; en la prueba sin ventana los
colores salen como en el PNG.

### Cabecera (`stage.json`)

`format`; `start` `{x, y, dir}` (obligatorio, píxeles dentro del mapa); `fallback` `{stage, x, y, dir}` (escenario del juego y dónde aparece el jugador si el mod falta; por defecto Parque Central, `mod_stages.h`); `textFile` (el número que un
`setupStage` suma a `LANGUAGE`, p. ej. 211 el del 330 en EU; sin él los textos con nombre no se pueden usar); `soundBank` (0-255, `SOUND.loadBank`); `music` `{bank, track}` (= `MUSIC(bank, track)`, `sound.h:70`); `spriteColor` `[r, g, b, cd]`; `banner` `{area, place}` (opcional: los nombres `<mod>:<texto>` de las dos líneas del letrero del lugar, §13b); y las
secciones `actors`, `slots`, `events`, `battles` **solo con `add`**, leídas por el mismo analizador del parche de escenario (`parsePatch` en `stage_mods.c`, con `SmFile.isNew`); `patch` o `remove` se rechazan (`unknown key`). Dos diferencias con un parche: una salida de
`slots.add` **no necesita `area`** (puede apuntarla `triggers.png`) y `battles.add` empieza en el área 1 (el escenario no trae batallas del juego). No se rechaza `bank`/`soundBank` que el juego no tenga: no hay tabla que lo diga (*sin verificar*; un banco que no existe no suena).

### Orden y rechazos

1. `nsScan` (antes de `msSeal`): lee `stage.json` (cabecera) y los PNG, arma el TMPK (`mlEncode` x 3 + `mlArchiveBuild`, entrada 0 vacía) y el PACK en memoria, y registra el escenario. Un rechazo es una línea `[stages] <archivo> rejected: <motivo>` con la ruta del archivo
   culpable (formato inválido, PNG ilegible, tamaño que no es múltiplo de 128 o que no coincide con el fondo, más de 255 mosaicos, valor de piso sin sentido, `start` fuera del mapa, `fallback` que no es un escenario del juego, `music` fuera de rango...).
2. `msSeal` da los modos (`0x300+`, alfabético).
3. `smLoad` lee las secciones y llama `nsCheck` con lo que definen (`SmNewInfo`: ranuras y áreas de batalla); un fallo se rechaza **entero** y `nsRefuse` marca el escenario (`msRefuse`): su modo queda tomado (los demás no cambian), deja de resolverse por nombre, modo o número, y
   los archivos que lo nombraban se vuelven a leer (hasta que no cambie nada), así que una salida a un escenario rechazado rechaza al que la tiene.
4. `nsInstall`: archivos virtuales (TMPK primero, luego PACK) de los escenarios que quedaron, y los datos que `pcModStageSetup` copia al `FieldState`.

`--validate-mod` corre los mismos cargadores y muestra esas líneas (`stages` está en las capas cubiertas de `pcValidateMods`; una carpeta `new-stages/` sobrante da `folder new-stages/ is never read`).

### Lo que el juego necesita de un escenario sin `setupStage` (leído)

- **Hoja de objetos (entrada 0 del TMPK):** vacía. Solo se pide si hay `imageEntry`/`imageFile` (`file_loader.c:85-96`) y solo se lee al dibujar un objeto (`map_objects.c:19-29`); el escenario no trae objetos (una tabla con el fin `y == 0`, `map_objects.c:47`,
  `commands.c:32`). Objetos del mapa de un mod: fuera de v1.
- **Imágenes:** `FieldState.images` es una unión: las dos primeras `ActorImage` son la imagen del campo (sombra, cubierta) y las cuatro siguientes las del jugador y los tres compañeros (`actor.c:140`, `field.c:410-422`, `field_map.h:456-462`).
  Los cuatro escenarios que se compararon empiezan su tabla con las mismas seis (`wstag200.c:216-221`, `wstag330.c:107-112`, `wstag345.c:254-259`, `wstag420.c:196-201`; son las seis primeras de `FIELDSTG_images`, `fieldstg.c:2118-2123`; no se revisaron los 294), y el genérico las copia (`pcModStageSetup`) en una tabla de 22 entradas que
  `pcImagesInstall` extiende con los espacios de los personajes nuevos. Sin esto el jugador existía pero no se dibujaba.
- **Posición:** el jugador entra en `FIELDSTG_state.start` si `modeArg != -1`, o en `defaultStart` (lo que dice la salida o `GAME.fieldPos`) en los demás casos (`actor.c:166-176`, `field.c:531-536`). Una corrida de arnés que entra con `mode:300` desde el selector de país
  cae en `GAME.fieldPos` (cero): el arnés (`host_pad.c`) ahora pone el `start` del escenario ahí.
- **Batallas:** `FieldState.battles` es NULL si no hay `battles.add` (`battle.c:41` no cuenta pasos); `pcStagePatchHook` pone el modelo de batallas "presente" para un escenario de mod y deja que `add` agregue las áreas 1, 2, ...
- **Ganchos:** `pcStagePatchHook` (`pc/boot.c`) reconoce el inicio genérico (`pcModStageStart`) y aplica la sección del escenario como el parche de su número `0xF000 + i` sobre un escenario vacío (`s.nimages = 0`: un `image` explícito se rechaza, los personajes toman un espacio propio).

### Prueba

`make -C pc newstagetest` (sin disco): un escenario de 2 x 2 mosaicos hecho con PNG generados en la prueba; las tres capas del TMPK leídas como archivo virtual iguales a los PNG, los cuatro mosaicos del PACK decodificados iguales al fondo y su CLUT; las secciones aplicadas a un escenario
vacío; catorce rechazos con su motivo; los rechazos de `nsCheck` (ranura y área que no existen), el modo que queda tomado, y el rechazo en cascada. Sin ventana, con el disco: `warp:330,4,fieldexit,reach:300` (de Parque Central al escenario por una salida
agregada en `stages/s330/stage.json`) y `mode:300,fieldexit,reach:21D` (de vuelta por la salida del escenario).

### T5b: encuentros propios (hecho)

El ejemplo (T6) necesita filas de encuentro **nuevas** para no cambiar las zonas originales. Dónde se indexa una fila (leído): `AreaBattle.encounter` (`field_map.h:411`, `s32`) -> `FIELDSTG_encounters[encounter]` en `FIELDSTG_startEncounter` (`field.c:1003-1010`: enemigos, bloques,
emboscada, tipo) y `BATTLE_SETUP.battle = encounter` (`field.c:1003`, `s32`); de ahí `BATTLE_RESULT.battle` (`normal_battle/menu.c:483`, `s16`) indexa `STFGTREP_rewards[...]` (dinero, EXP, EXP de Digimon: `report.c:120-122`, `328-334`, `partner.c:206`) y hay comparaciones por número
(`menu.c:15-21`: `0x143`, `0xB`, `0x1E`, `0x144`; `enemy_turn.c:397`: condición 12 contra `arg`). El botín no depende del encuentro (`battle_loot` es del enemigo: `boot.c`, `pcLootLive`).

**Cómo se resolvió.** Los dos arreglos son datos de overlay del tamaño del disco (335 filas), así que las filas nuevas viven en un almacén del puerto (`pc/boot.c`: `sExtEncounter`, `sExtEnemy`, `sExtReward`, armado una vez tras la carga de Digimon y antes de los parches de escenario) y el juego las lee por
accesores `#ifdef DW3_PC`: `FIELDSTG_ENCOUNTER(n)` (`field_mode.h`, `field.c`) y `STFGTREP_REWARD(n)` (`report.h`, `report.c`, `partner.c`); en la PS1 son los arreglos (`make compare`: 316 OK). Las filas salen de `tables/field_encounters.json`, llave `"<mod>:<nombre>"`
(`pc/shim/field_ext.[ch]`, formato en `docs/data-tables.md` §3.5 y `docs/modding/README.md` §6.5b); la tabla saltaba esas llaves con "no row with that key" y ahora las deja a ese módulo (`TmTable.ownKey`). Números: 335 en adelante, orden de mods y de archivo, hasta 1024.

**Nombres.** `battles` (`patch` y `add`) y la acción `start-battle` aceptan `"encounter": "<mod>:<nombre>"` (`SmHost.encounterByName`, `StageCodeEnv.encounterByName`; `scListToJson` la imprime por nombre) o el número; un nombre desconocido rechaza el archivo con
`there is no field encounter named "<nombre>"`. `pcEncounterOk` acepta hasta `335 + filas nuevas`. Que el juego no lea nada fuera de esos puntos: `rg "FIELDSTG_encounters|STFGTREP_rewards"` en `src/` solo da las definiciones y los accesores; `host_pad.c` (`battleenc:`) sigue usando solo la tabla del disco.

**Prueba.** `make -C pc fieldexttest` (filas, números, recompensas, 19 rechazos, orden de mods), `stagemodtest` (`testEncounterNames`: nombres en `battles` patch/add y en `start-battle`, rechazos, impresión por nombre), `tabletest` (las llaves con nombre no se registran como filas del juego).
Sin ventana: `mode:300,fieldbattles:1,battlepilot,until:1400` con un mod `t5b` cuyo escenario `yard` tiene un área con `"encounter": "t5b:meadow"` (nivel 10): `pad: fieldbattles: encounter 335 stage 1 fighters 206/0/0 levels 10/0/0 hp 60`, la batalla y el reporte leen
`[tables] report enter: encounter 335 digimonExp 55 exp 123 money 777, wallet 0` y `report exit: ... wallet 777`.

## 14. Pesca y árboles para patear (T9)

Dos mecánicas del juego que los mods pueden ahora poblar con sus propios Digimon. Sin estas secciones el juego es idéntico (`compare`: 316 OK; los ganchos son `#ifdef DW3_PC`).

### Lo que hace el juego (leído)

- **Pesca = slot `gauge` (tipo 7).** Todos los slots `gauge` del disco llevan la condición `ITEM(0, 5)` (la Caña de pescar, `dw3:item/5`; 46 archivos de escenario con `SLOT_GAUGE`, p. ej. `wstag716.c:231-233`). `FIELDSTG_setOffTrigger` (`trigger.c:203`) arranca `playGauge` con el desplazamiento `{arg, x}` del slot (dónde se dibuja el medidor junto al jugador; los valores `0xFFDC` son `u16` de un número negativo). `FIELDSTG_runGauge` (`gauge.c`): un cursor va y viene por una de las 8 filas al azar de `FIELDSTG_gaugeRows` (`fieldstg.c:2014`, 2 bits por celda; en EU, mientras dura `GAME.randomGauges` = 16 al entrar a un modo nuevo, `start.c:18`, y después la fila 8, toda en ceros). Al parar, la celda da **0 = nada**, **1 = `startEventBattle(4)`** (con un sonido propio al empezar el resultado, `gauge.c`) o **2 = `startEventBattle(7)`**: la batalla 4 u 7 de la cuarta área del lugar (`battle.c:61`: `battles[3]->battles[index]`). Las filas del disco solo traen celdas 0, 1 y 2 (ningún byte tiene el valor 3). En el 330 esas batallas son `area3Battle4 = {328, 8, ...}` y `area3Battle7 = {54, 8, ...}` (`wstag330.c:94,97`). Cuál celda es el verde y cuál el amarillo **es arte** (los sprites de `FIELD_SPRITES_FILE` entrada 1, `GAUGE_ROW_FRAMES + fila`): no se leyó del código y queda como hipótesis: la celda 2 ocupa casi toda la fila (`0xAA` = 4 celdas 2), la 1 es el tramo corto con sonido, la 0 el azul. Los archivos de la animación (`FIELD_GAUGE_FILE`) se piden si algún slot es `gauge`.
- **Árboles = puntos ocultos (`hidden_spots.c`).** Un punto oculto es un **objeto del mapa** (`StageTile`, `field_map.h:208`) con `anim == 0xFF` en la tabla `FieldState.objects` del escenario (51 archivos con objetos `0xFF`, p. ej. `wstag330.c:841-870`: `{1, 0xFF, 0x64, 4, 0x38, 0, 0, 0, 0, 0, x, y, sortY, 0}`); su sprite sale de las hojas de efectos del campo (`map_objects.c:18-24`, no de la hoja de objetos del escenario). `FIELDSTG_updateMapObjects` (`map_objects.c:47`) cuenta los objetos `0xFF` y crea `FIELDSTG_createHiddenSpots(count)`, que elige **uno** al azar con el premio (`GAME.prizeSpot`, `hidden_spots.c:67-73`; se vuelve a elegir tras cada premio; el índice se conserva dentro del mismo modo). Con el objeto 4 (`FIELD_SEARCH_ITEM = ITEM(0, 4)`, `field_mode.h:657`) y cruz frente a un punto a 10 px o menos (`FIELDSTG_findHiddenSpot`, `actor_control.c:402`), el jugador busca (`ACTOR_SEARCH`) y al terminar el efecto (`hidden_spots.c:100-117`): si el punto tiene el premio, con `RANDOM & 0x7F < 0x66` (80 %) `startEventBattle(3)` y si no (20 %) `startEventBattle(6)` (la batalla 3 u 6 de la cuarta área: en el 330 `area3Battle3 = {327,...}` = Cardmon y `area3Battle6 = {49,...}`); si no, un indicador (`FIELDSTG_showSpotHint`, `hidden_spots.c:7`) cuyo color y parpadeo dicen qué tan lejos está el premio (menos de 128 px, menos de 256, más). **El premio siempre es una batalla; el juego no da objetos aquí.** Un punto fuera del mapa (`x >= 1000`) es el de un evento (`FIELDSTG_searchEventSpot`). Un punto oculto también bloquea la celda (`map.c:144`).

### `fishing` (en `stage-patch-v1` y `stage-new-v1`)

```json
"slots":   {"add": [{"key": "laguna", "type": "gauge", "area": {"x": 400, "y": 600, "w": 200, "h": 120}}]},
"fishing": {
  "default": {"1": [{"encounter": "mimod:anguila", "stage": 1, "music": 1611137024}]},
  "slots": [{"key": "laguna",
             "1": [{"encounter": "mimod:anguila", "stage": 1, "music": 1611137024, "weight": 3}, {"item": "dw3:item/18"}, {"nothing": true}],
             "2": {"encounter": "mimod:lubina", "stage": 1, "music": 1611137024}}]
}
```

- Un slot `gauge` **agregado** sin `conditions` lleva `[["item", "dw3:item/5", 1]]` (la caña); `"conditions": []` lo deja sin condición. Los slots del juego conservan los suyos. Los del juego se parchean por `index` como cualquier slot.
- `fishing.slots[]`: `index` (el lugar del slot en la tabla del escenario) o `key` (un slot agregado), y las listas de las celdas `"1"` y `"2"` (la 0 siempre es nada). `fishing.default`: las listas de las celdas de todo slot `gauge` del escenario que no tenga entrada propia. Una celda sin lista usa la batalla 4 / 7 del juego. Ref que no existe o que no es `gauge`: se salta con una línea del log (`fishing: ...`); el último mod gana, un conflicto va al log.
- **Resultado**: un objeto o una lista de hasta 16 (uno se elige por peso, con `RANDOM`): `{"encounter": <número o "<mod>:<nombre>">, "stage", "music"}` (una batalla; `stage` y `music` son obligatorios, como en `battles`), `{"item": ...}` (un ejemplar, por la misma acción `item` de las charlas), `{"nothing": true}`; `weight` 1 a 1000 (1 por defecto). Una lista vacía es "nada".
- Ejecución: `pcFishingBegin` (`trigger.c`, al disparar el slot) fija el slot y `pcFishingCatch(celda)` (`gauge.c`) elige y ejecuta; la batalla es la misma `FIELDSTG_startBattle` de las batallas de área (el medidor termina en `TASK_DONE`; con objeto o nada, en `TASK_KILL`, como un fallo). Un objeto no muestra mensaje propio (el globo del resultado es el del juego).

### `hiddenSpots`

```json
"hiddenSpots": {
  "add":   [{"x": 700, "y": 400}, {"x": 812, "y": 520}],
  "prize": {"common": [{"encounter": "mimod:cardmon", "stage": 1, "music": 1611137024}, {"encounter": "mimod:otro", "stage": 1, "music": 1611137024, "weight": 2}],
            "rare":   {"encounter": "mimod:raro", "stage": 1, "music": 1611137024}}
}
```

- `add`: árboles nuevos (hasta 64 por parche; `x` 0 a 32767, `y` 1 a 32767, píxeles del mapa: el punto donde el jugador busca). El puerto copia la tabla de objetos del escenario (`pcSpotsInstall`, `pc/boot.c`) y agrega por cada árbol un objeto `0xFF` igual a los del 330 (`visible 1, margin 0x64, depth 4, frame 0x38`, `sortY = y`). El premio se reparte entre todos los árboles del escenario, los del juego y los agregados. No se comprueba que el punto esté dentro del mapa ni que su celda sea caminable.
- `prize.common` / `prize.rare`: qué da el árbol con el premio. `common` reemplaza la batalla 3 (80 %), `rare` la 6 (20 %); misma forma de resultado que la pesca (una batalla, un objeto o nada; con varias entradas y pesos, el árbol esconde uno de varios Digimon). Sin la llave: la batalla 3 / 6 de la cuarta área del lugar (que también se pueden cambiar con `battles`).
- Hace falta el objeto 4 (`dw3:item/4`) en la bolsa para patear, como en el juego.

### Prueba

`make -C pc stagemodtest` (`testCatches`: analizador, rechazos con motivo, caña por defecto y `conditions: []`, listas por slot y por defecto, claves, árboles, premios, peso del sorteo `sdCatchPick`). RED: la prueba contra el código anterior no compila (`SdStage` sin `nfishing`...); GREEN: `stagemodtest: ok`. Sin ventana (SDL offscreen, OpenAL null, `DW3_USER_DIR`/`DW3_SETTINGS` nuevos, mod generado `t9proof` con `new-stages/pond` y dos filas de encuentro nuevas): pesca `mode:300,fieldexit,battlepilot` con `DW3_SEED_ITEMS=5=1` y árbol `mode:300,fieldsearch,battlepilot` con `4=1` (fichas del arnés nuevas, `host_pad.c`: `fieldexit` también para el cursor del medidor y `fieldsearch` patea el árbol de enfrente).

## 15. El ejemplo completo: alturas, árboles, pesca y personajes (T10, T11)

`pc/examples/mods/training-field` usa todo lo anterior. Lo genera `tools/make_art.py` (dibujo, capas, `stage.json`, `tables/field_encounters.json` y `text/*/stages.json`: una sola fuente, salida igual byte a byte).

### Alturas y rampas (T10)

Qué hace el juego (leído, `docs/formats.md` §4.3): una pendiente es un valor de piso 2-7 (y +16 espejado) que solo cambia el paso de la caminata (`FIELDSTG_getWalkStep`, `map.c:149-160`, fila `n - 1` de `FIELDSTG_dirVectors`, `fieldstg.c:3601`);
no hay `z` al caminar, y la altura es el dibujo. `SLOT_DEPTH` solo fija la profundidad de dibujo del jugador (`trigger.c:79-82`, sin objetos de primer plano no hace falta), `SLOT_MAP` elige el piso 0 o 1 (`map.c:57-60`; un escenario nuevo
solo trae el 0, `pcModStageSetup`) y `SLOT_CLIMB_UP/DOWN`/`SLOT_DROP` son subir un muro con el pad y caer (`actor_control.c:626-680`); el ejemplo no los usa.

Diseño más simple y fiel: **una meseta con una rampa a cada lado**. Fila 2 (valor 3): el este es `(3939, -819)`, 0,2079 px hacia arriba por px hacia el este. Una meseta de 32 px (el acantilado es 32 px de dibujo) se sube con una banda de 154 px
(32 / 0,2079) y 32 px de alto cuyas celdas valen 3 (rampa oeste: caminar al este sube) o 19 = 16 + 3 (rampa este, espejada: caminar al este baja). El dibujo trae exactamente esa banda (escalones, bordes), el acantilado y el triángulo de muro bajo cada rampa (valor 0). El borde norte,
oeste y este de la cima son muros de 8 px. Sin cruces ni dos pisos de capa: el jugador sube por la rampa oeste, cruza la cima y baja por la este.

Prueba sin ventana (`mode:300` + `fieldwalk`, abajo): rampa oeste `fieldwalk 363: at (297, 264), floor cell 3` -> `fieldwalk 383: at (372, 245), floor cell 3` -> `fieldwalk 403: at (449, 229), floor cell 1` (la cima); cima y bajada por la este `fieldwalk 476: at (726, 238), floor cell 19` -> `fieldwalk 495: reached #6 (800, 252) at (800, 254), floor cell 19`;
subida por la este (`(761, 246)` -> `(684, 230)` -> `reached #7 (640, 224) at (641, 226), floor cell 1`) y bajada por la oeste hasta `fieldwalk 617: route done`. Capturas: el jugador a medio camino de la rampa y arriba (cuadros 383 y 405 de esa corrida).

### Contenido (T11)

| Qué | Dónde | Cómo |
|---|---|---|
| Árboles para patear | abajo 5 (dos en la cima), arriba 4 | `hiddenSpots.add` + `prize.common`/`rare` con listas con peso: Cardmon (filas `kind` 5 con los bloqueos de las filas del disco 327 y 328) y otros Digimon de la banda, nivel 8-11 abajo y 13-16 arriba |
| Pesca | laguna abajo: muelle (spot `fish-pier`, mira al norte) y dos orillas (este y sur) | tres slots `gauge` con `area` + `facing`; el muelle trae sus listas (Dolphmon, Octomon, Tortomon, Cardmon acuático); las orillas, las de `fishing.default` (Betamon, Gizamon, Cardmon acuático, un objeto, nada / Shellmon, Tortomon, Coelamon). La distancia del cebo al jugador va en `arg`/`x` del slot (u16 con signo, `trigger.c:203-207`, como los slots `gauge` del disco) |
| Duelo | arriba, el campeón del anillo (personaje 57) | `["start-battle", "training-field:duel-trio", ...]` con la bandera `training-field:duel-started` (una vez por partida: la segunda charla paga 500 BIT y pone `duel-paid`; la tercera solo habla). La fila `duel-trio` es de tipo 2 con `runAway` 1 (como el trío del Líder Seiryu, encuentro 3) y niveles 15, 16, 15 |
| Cartas | arriba, una Tamer (personaje 51) | `["card-battle", "13:0", 1]` (oponente 0x13 = fila 38 de `card_opponents`, nivel 15, la banda de arriba). Tras la partida, el juego deja `flag 00:11` (acabó una partida) y `flag 00:10` (ganó el jugador) (`events.c:739-748`, `battle.c:2530`): las dos primeras charlas leen esas banderas solo para comentar el resultado. El resultado que cuenta para la misión es la variable del puerto `dw3:card-won/13` (§16) |
| Minimisión | la guardabosques (personaje 69) abajo y tres letreros (el sprite propio `sign`, `sprites/sign/`, de `make_art.py`; el personaje 36 es una operadora y el 63 no tiene archivo de sprites) | `training-field:quest` (0, 1, 2) y las banderas `seen-lake`, `seen-hill`, `seen-ring` (y la del puerto `dw3:card-won/13`). Al aceptar da las botas para árboles (`dw3:item/4`) y la Caña (`dw3:item/5`) si faltan; al leer los tres letreros y vencer en cartas (`["mod-flag", "dw3:card-won/13", 1]`), paga 300 BIT y el Chip PS (`dw3:item/47`) y pone `quest` = 2 |

### Fichas de arnés nuevas (`pc/shim/host_pad.c`)

- `fieldwalk:<x>:<y>[:press]`: lleva al jugador a un punto del mapa por estado (lee la posición cada cuadro; no hay tiempos): sostiene la dirección hacia el punto y solo pone el segundo eje si el punto está a más de 2:1, así sobre una pendiente el jugador sigue la pendiente del juego. Varias fichas son una ruta; `:press` pulsa cruz al llegar (habla con lo que mira, patea el árbol, ...). Implica `fieldauto`. Imprime `fieldwalk <cuadro>: at (x, y), floor cell <valor>`, `reached #n` y `stuck` (si la distancia no baja 3 px en 150 cuadros, salta el punto).
- `walkend`: termina la corrida cuando la ruta acabó y el campo estuvo quieto 90 cuadros (el diálogo que empezó la pulsación acabó) e imprime `harness state (route done): money N, Tree Boots ..., HP Chip ...`.
- `fieldsearch` patea una sola vez por lugar (no vuelve a patear hasta que el jugador se mueve 12 px) y `flag:<hex>` puede repetirse (hasta 5); también se aplican con `mode:3xx`. El juego borra las banderas temporales (`flag 00:xx`) al empezar el escenario: sirven para que `DW3_LOG=trace` muestre qué charla elige el intérprete al crear al personaje, no para probar la pulsación (la pulsación tras una partida de cartas se prueba con `cardplay`, §16).
- Las acciones `mod-flag` y `set-var` se registran en el log (`action mod-flag <nombre> = n`), y la pesca y los árboles dicen el nombre del encuentro (`fishing cell 2: encounter 364 training-field:lake-octomon`).

## 16. Resultado de las partidas de cartas y letrero del lugar (arreglos de la prueba de juego, 2026-11-04)

### Cartas: por qué la misión no avanzaba y qué hace ahora el puerto

Lo leído: `startCardBattle` (`events.c:543`) deja el campo con `FIELDSTG_leaveField(MODE_CARD_GAME, ...)`; al terminar, `CARDGAME_updateBattle` pone `FLAGS_00.pendingFlag10` si ganó el jugador (`battle.c:2526-2532`) y
`CARDGAME_updateScene` pide `GAME.fieldMode` (`scene.c:38`). El campo vuelve a arrancar: `FIELDSTG_updateRoot` fija `GAME.clearTempFlags = 1` solo si `GAME.lastFieldMode` cambió (`start.c:14-21`; en un retorno del mismo
escenario es 0), `startField` llama a `updateModeFlags` (`field.c:522`) **después** de `stageInit` y esa función pone `00:11` y `00:10` (`events.c:739-748`). Los escenarios del juego y los de un mod siguen exactamente este camino
(`pcModStagePick` solo reemplaza el `stageInit`, y el modo de retorno `0x3xx` es el mismo `GAME.fieldMode`); lo medido sin ventana con la ficha `cardplay` (abajo) lo confirma: al volver, `flag 00:10 = 1, flag 00:11 = 1,
clearTempFlags 0` tras ganar y `00:10 = 0, 00:11 = 1` tras perder (la partida real que juega el piloto perdió), y la primera pulsación sobre la Tamer corre la charla "ganaste" (`action mod-flag ... = 1`).

Entonces no hay un defecto del retorno (no se pudo reproducir uno); lo más probable es que fallara de qué dependía la misión: `00:10`/`00:11` son banderas **temporales**. `updateModeFlags` las borra al empezar el escenario siguiente (`events.c:733-738`, `clearTempFlags`), y solo
hay una charla que las lea: si el jugador gana y se va (escaleras, otro piso) sin volver a hablarle a la Tamer, la victoria se pierde; y si le habla otra vez, la charla de "ganaste" borra las banderas y la tercera
empieza otra partida. El registro del dueño (`dw3-20261104-173204.log`) muestra justo eso: dos veces `0x700` seguidas de `0x301` y de nuevo `0x700`, sin que nada guarde el resultado.

Arreglo (puerto, detrás de `#ifdef DW3_PC`, el build de PS1 queda igual): `pcCardBattleBegin(opponent)` (en `startCardBattle`) y `pcCardBattleEnd(won)` (en `updateModeFlags`, al volver de `MODE_CARD_GAME`)
guardan el resultado **al terminar la partida** en variables con nombre, que se guardan con la partida (`EXTV`, `pc/shim/ext_state.[ch]`):

| Variable | Valor |
|---|---|
| `dw3:card-played/<oponente>` | 1 una vez que el jugador peleó con ese oponente |
| `dw3:card-won/<oponente>` | 1 una vez que le ganó (una derrota posterior no la borra) |

`<oponente>` es el de la acción `card-battle` en hex sin ceros a la izquierda (`"13:0"` -> `13`). Un mod las lee con `mod-flag` (`["mod-flag", "dw3:card-won/13", 1]`). Una partida abierta sin acción del campo
(el piloto `cardauto`) no guarda nada. El ejemplo `training-field` usa `dw3:card-won/13` en la condición de la guardabosques y deja las charlas de `00:10`/`00:11` de la Tamer solo para comentar el resultado. Prueba:
`make -C pc extstatetest` (`testCardResults`).

### Letrero del lugar en escenarios de mod (`banner`)

`FIELDSTG_updateBanner` abre dos ventanas (`FIELDSTG_showAreaName`, `banner.c:9-25`): la grande lee `FIELDSTG_areaNames[i].area` de `TEXT_AREA_NAMES` (STAREA) y la chica `.place` de `TEXT_STAGE_NAMES` (STNAME), y busca por
modo en una tabla en la que un escenario de mod no está (por eso T4 apagaba el letrero). `stage.json` ahora acepta `"banner": {"area": "<mod>:<texto>", "place": "<mod>:<texto>"}`: dos textos con nombre (§8) de
`text/<idioma>/stages.json` (eng obligatorio, otro idioma cae al inglés; es-419 prueba el español primero). Al cargar los mods, `bannerAssign` (`stage_mods.c`) agrega esas líneas al final de STAREA y de STNAME
(mismo mecanismo que las charlas: `smTextCount`/`smTextOf`, `textFirst`, `textChanged`) y deja los números de entrada en `MsStage.bannerAreaEntry/bannerPlaceEntry`; `pcFieldBannerEntries` (`pc/boot.c`) los da y
`FIELDSTG_showAreaName` (`#ifdef DW3_PC`) crea las mismas dos ventanas (mismas posiciones y retardo) con esas entradas, así que el aspecto y los tiempos son los del juego y valen las opciones Rápido/Clásico
(`stage_timing.c`, `stage_banner.c`). `pcFieldBannerShow` devuelve 1 solo si el escenario tiene los dos números; sin `banner`, o si un texto no existe (línea `[stages] stage <llave>: the banner's ... text ... no banner`),
no hay letrero. El objeto con otra clave o sin `place`/`area` rechaza el escenario. Prueba: `make -C pc newstagetest` (`testBanner`).

### Fichas de arnés

- `cardplay[:win|lose]`: una partida que empieza el campo (una acción `card-battle`) y el regreso. Sin argumento la juega el piloto de `cardauto`; con `win`/`lose` la termina a los 60 cuadros por el camino del propio juego
  (`pendingFlag10` y `requestMode(GAME.fieldMode)`), de modo que el regreso corre el cambio de modo, `startField` y `updateModeFlags` reales. Imprime `cardplay N: back in mode ...: flag 00:10 = .., flag 00:11 = .., clearTempFlags ..`.
- `walkend` imprime además `card 0x13 played N won N, quest N`.

## 17. Resultado de una batalla de evento y preguntas de sí o no (prueba de juego, 2026-11-04)

### Lo que da el juego (leído)

- **Resultado de una batalla.** El juego no guarda el resultado de las batallas de campo. `WFIGHTMN_endBattle` (`src/battle/normal_battle/menu.c:540-566`) mira `FIGHTSTG_events.funcs.result`: `BATTLE_FLED` (0) vuelve al campo con `requestMode(GAME.fieldMode)`, `BATTLE_WON` (1)
  va a `MODE_BATTLE_REPORT` (que vuelve al campo, `report.c:357`) y `BATTLE_LOST` (2) va a `MODE_TITLE`. Las batallas de evento solo ponen `FIELD_FLAG_ENCOUNTERED` al empezar (`field.c:1032`). Lo único que distingue un caso al volver es `GAME.prevMode`.
- **Sí o no en el campo.** Solo existe `FIELDSTG_runChoice` (`choice.c`): una pregunta de la historia, uno de 16 tipos fijos de `FIELDSTG_choices` (`fieldstg.c:1290`), con su texto en el archivo de la historia y dos *eventos* (`events[2]`) del `FIELDSTG_events` global que se empiezan según la respuesta; la empieza un evento
  con `start = FIELDSTG_askChoiceN`. No hay nada para charlas de personajes (las posadas y tiendas abren su propio modo). Los sí/no están en el texto del juego: `SSSTATUS` 0x32 y 0x33 (los usa el menú del campo, `item_screen.c:150`).

### `dw3:battle-won/<clave>` y `dw3:battle-played/<clave>`

`pcFieldBattleBegin(clave)` (en `pcStageWarpPoll`, cuando la acción `start-battle` empieza la batalla) y `pcFieldBattleEnd(ganó)` (`updateModeFlags`, `events.c`, `#ifdef DW3_PC`: venir de `MODE_BATTLE_REPORT` es ganar, de `MODE_BATTLE` es haber huido) guardan el resultado
en variables con nombre (se guardan con la partida, `EXTV`): `dw3:battle-played/<clave>` (peleó: ganó o huyó) y `dw3:battle-won/<clave>` (ganó; huir después no la borra). `<clave>` es el nombre del encuentro del mod con `/` en lugar de `:` (`training-field/duel-trio`) o el número en hex. Una batalla de área al azar no deja nada.
**Perder** lleva al título (`menu.c:559`): no se puede comentar. Un mod que quiere saber si ESTA pelea se ganó borra antes la variable (`["mod-flag", "dw3:battle-won/<clave>", 0]`, como hace la revancha del ejemplo). Se reinicia con una partida nueva o cargada.

### La acción `ask`

`["ask", "<mod>:<nombre>"]` en las `actions` de una charla. En `runTalk` (`actor_control.c`, `#ifdef DW3_PC`): si las acciones de la charla traen un `ask` (`pcActionsAsk`), el personaje no suelta el control al terminar la línea: corre las acciones (que dejan la pregunta), crea el panel
(`FIELDSTG_askChoicePort`, `choice.c`: la misma `FIELDSTG_runChoice` con el tipo 16, sin fila de `FIELDSTG_choices`; muestra el Sí/No del menú del campo, `SSSTATUS` 0x32/0x33, y la pregunta es la línea que se acaba de decir) y espera la respuesta como hijo de la tarea del personaje (las tareas solo corren
como hijas de otra, `registry.c:70-99`). La respuesta (1 sí, 2 no) va a la variable y las charlas del personaje se eligen otra vez (`actor->step = 0`): la siguiente lee la variable como cualquier condición. Las acciones del `ask` no se aplican dos veces. Los eventos del juego no lo usan.

Ejemplo (el campeón y la Tamer de `training-field`): primero gana, después `duel-after` (`mod-flag duel-paid`) con `["ask", "training-field:rematch"]`; `rematch == 1` -> "¡Allá vamos!" + `start-battle`, `rematch == 2` -> "Sin problema". Para el duelo la revancha pone `rematch = 3` y borra la variable de victoria, así que al volver
`rematch == 3` y `dw3:battle-won/...` dicen si ganó esa pelea.

### Fichas de arnés y pruebas

`askpick:yes|no` (responde la pregunta cuando el panel está listo; `fieldauto` aparte), `battleplay:win` (la batalla que empezó una acción termina como victoria a los 120 cuadros por el camino del juego: `BATTLE_RESULT` y `MODE_BATTLE_REPORT`; un grupo de nivel 1 no vence al duelo). Pruebas: `make -C pc stagemodtest` (`ask`: compilar, descompilar, `pcActionsAsk`, respuesta) y
`make -C pc extstatetest` (`testFieldBattleResults`). El validador (`--validate-mod`) revisa la carpeta `sprites/` (A7.5): el nombre de una original (`p001play`) son sus cuadros `NNN.png` y su `sprite.json` no se lee; cualquier otro nombre es un sprite nuevo y debe llevar `sprite.json`. Un sprite nuevo no puede llamarse como una original (el nombre sería de sus cuadros).
