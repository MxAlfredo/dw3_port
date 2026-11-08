# training-field: un escenario nuevo completo, hecho solo con un mod

Dos pisos de campo nuevos con arte original: una meseta con rampas, una laguna con muelle, árboles para patear, pesca, un duelo con
revancha, una Tamer de cartas con revancha, una misión y letreros. Sirve para jugarlo y, sobre todo, como **base para tu propio escenario**.
No trae nada del disco: el arte y los textos son originales y el arte sale de `tools/make_art.py` (solo biblioteca estándar de Python).

## Qué es

- **Piso de abajo** (`training-field:lower`, 1024 x 768 px): césped, laguna con muelle, una meseta con una rampa a cada lado, rocas, vallas y dos parches de pasto alto
  (combates al azar niveles 8 a 11). Una alfombra al sur vuelve al Parque Central; unas escaleras al noreste suben.
- **Piso de arriba** (`training-field:upper`, 768 x 512 px, paleta de otoño): un anillo de entrenamiento, estanque, rocas y pasto alto (niveles 13 a 16). Unas escaleras bajan.
- **Árboles para patear** (5 abajo, 4 arriba): uno solo guarda el premio (un Cardmon o un Digimon de la banda; muy rara vez uno más fuerte).
- **Pesca**: el muelle y dos orillas de la laguna, con Digimon acuáticos, un objeto y "nada".
- **Duelo** (arriba): el campeón del anillo, tres Digimon, sin huida. Paga 500 BIT la primera vez y después ofrece **revancha** (Sí/No).
- **Cartas** (arriba): una Tamer (oponente 0x13). Comenta el resultado y ofrece **revancha** si ganaste.
- **Misión** (la guardabosques de abajo): lee los tres letreros y vence a la Tamer; paga 300 BIT y un Chip PS.
- **Letreros**: tres, con la cabecera del propio juego. **Letrero de entrada** con el nombre del lugar ("Campo de Entrenamiento" / "Planta Baja").

## Requisitos

- **Port 1.1.0 o más nuevo.** El `mod.json` lo declara con `"port": ">=1.1.0"`: un port más viejo desactiva el mod y lo dice en el log
  (`mods: training-field disabled: needs port >=1.1.0, this is 1.0.0`).
- Tu propio disco de Digimon World 3 (EU), como para jugar al port.
- Para entrar: la **Medalla de Seiryu** (vence al Líder Seiryu en la historia). Sin ella el guía te pide volver más tarde.

## Instalar

```
pc/examples/install_examples.sh --only training-field
```

Copia el mod a tu carpeta `mods/` (queda apagado). Actívalo en **Opciones > Mods** y reinicia el port. A mano: copia la carpeta `training-field`
(sin `tools/` si no quieres regenerar el arte) a `~/.local/share/dw3port/dw3/mods/` (Linux) o `%APPDATA%\dw3port\dw3\mods\` (Windows).
Para quitarlo, borra la carpeta: una partida guardada dentro de un piso carga en el Parque Central, a los pies del guía (`fallback`).

## Cómo se juega

1. Ve al **Parque Central**, junto al lugar de entrenamiento de Leomon. Habla con el **guía** (sin la Medalla pide volver después; con ella suena el teletransporte y apareces abajo).
2. Abajo: pelea en el césped y el pasto alto, pesca (necesitas la Caña, objeto 5) y patea árboles (necesitas las botas, objeto 4). Habla con la **guardabosques**: si te faltan, te da ambas y empieza la misión.
3. Lee los letreros del lago, de la colina y del anillo (hablándoles de frente).
4. Sube por las escaleras del noreste. Arriba: acepta el **duelo** con el campeón y vence a la **Tamer de cartas**.
5. Vuelve con la guardabosques: paga 300 BIT y un Chip PS. Campeón y Tamer te preguntan si quieres la revancha cuando quieras repetir.

## Cómo está hecho

```
training-field/
  mod.json                      manifiesto (mod-v1)
  stages/s330/stage.json        parche: el guía en el Parque Central (stage-patch-v1)
  stages/lower, upper/          un escenario nuevo cada uno (stage-new-v1)
      stage.json  background.png  floor.png  areas.png
  tables/field_encounters.json  encuentros nuevos
  text/{eng,es-419,spa}/stages.json   textos con nombre
  sprites/sign/                 sprite propio del letrero (sprite-v1)
  tools/make_art.py             genera todo lo anterior (no hace falta para jugar)
```

### `mod.json`

```json
{ "format": "mod-v1", "name": "Training Field", "version": "1.0.0", "port": ">=1.1.0", ... }
```

`port` es la versión mínima del port (`docs/modding/README.md` §4). `requires` (otros mods) no hace falta aquí.

### Un escenario nuevo: `stage.json` y las capas PNG

`stages/<nombre>/` se registra como `"<mod>:<nombre>"` (aquí `training-field:lower`). Las capas tienen el mismo tamaño y **un valor por píxel del mapa**:

| Archivo | Qué guarda |
|---|---|
| `background.png` | la imagen (PNG indexado, ancho y alto múltiplos de 128) |
| `floor.png` | colisión: `0` cerrado, `1` plano, `2` a `7` pendiente (`19` = `16 + 3`: la misma pendiente espejada) |
| `areas.png` | zonas de combate: `0` sin combates, `n` = la n-ésima entrada de `battles` |

```json
{ "format": "stage-new-v1",
  "fallback": { "stage": 330, "x": 952, "y": 412, "dir": 0 },
  "start": { "x": 512, "y": 656, "dir": 4 },
  "textFile": 211, "soundBank": 9, "music": { "bank": 9, "track": 0 } }
```

`fallback` es adonde carga una partida si quitas el mod. **Rampas**: la meseta de abajo mide 32 px de alto; la rampa oeste tiene valor `3` (caminar al este sube) y la este `19`
(caminar al este baja). El dibujo trae exactamente la banda con ese valor, así que la pendiente que ves es la que caminas (`docs/stages.md` §15).

### Salidas y conexión con el Parque Central

Las salidas son `slots` con un rectángulo (`area`): se disparan estando dentro y pulsando cruz.

```json
{ "key": "park", "stage": 330, "x": 952, "y": 412, "dir": 0, "area": { "x": 464, "y": 720, "w": 96, "h": 32 } }
```

El guía es un personaje agregado a un escenario del juego (`stages/s330/stage.json`, `stage-patch-v1`) con una charla condicionada y la acción `warp`:

```json
{ "text": "training-field:offer", "conditions": [["item", "dw3:item/9", 1]],
  "actions": [["sound", "teleport"], ["warp", "training-field:lower", 512, 656, 4]] }
```

### Encuentros nuevos

`tables/field_encounters.json` agrega filas `"<mod>:<nombre>"` con sus enemigos y recompensas; no cambian ninguna zona original. Se usan en `battles`:

```json
{ "area": 1, "rate": 3, "slots": [ { "encounter": "training-field:lower-coelamon", "stage": 1, "music": 1611137024 }, ... ] }
```

Los PS siguen `24 x nivel + 120` y las recompensas EXP `5 x nivel`, BIT `10 x nivel`, EXP de Digimon = nivel, como las filas al azar del disco
(la tabla completa está en el repo, `docs/stages.md` §14).

### Árboles para patear (`hiddenSpots`)

```json
"hiddenSpots": { "add": [ { "x": 520, "y": 150 }, ... ],
                 "prize": { "common": [ { "encounter": "training-field:tree-cardmon", "stage": 1, "music": 1611137024, "weight": 3 } ], "rare": [ ... ] } }
```

Cada punto es un árbol; uno solo tiene el premio. Las listas llevan `weight` (peso del sorteo).

### Pesca (`fishing`)

Un lugar de pesca es un slot `gauge` con `area` y `facing`; las listas están por celda (`"1"`, `"2"`) y cada entrada es un combate, un objeto o nada:

```json
"fishing": { "default": { "1": [ { "encounter": "training-field:lake-betamon", "weight": 3, ... }, { "item": "dw3:item/43" }, { "nothing": true } ] },
             "slots": [ { "key": "fish-pier", "1": [ ... ], "2": [ ... ] } ] }
```

`default` sirve a todo slot `gauge` sin entrada propia; el muelle (`fish-pier`) trae las suyas.

### Preguntas y revancha (`ask`)

La acción `["ask", "<mod>:<nombre>"]` muestra Sí/No al final de la línea y guarda `1` (sí) o `2` (no) en la variable; la siguiente charla del personaje la lee:

```json
{ "text": "training-field:duel-after", "conditions": [["mod-flag", "training-field:duel-paid", 1]], "actions": [["ask", "training-field:rematch"]] },
{ "text": "training-field:duel-rematch-go", "conditions": [["mod-var", "training-field:rematch", "==", 1]],
  "actions": [["set-var", "training-field:rematch", 3], ["start-battle", "training-field:duel-trio", 1, 1611137024]] }
```

### Resultados de batalla: variables del puerto

El puerto deja el resultado cuando acaba la batalla; no hace falta volver a hablar para que cuente:

- `dw3:battle-played/<clave>` y `dw3:battle-won/<clave>`: batallas de `start-battle` (aquí `training-field/duel-trio`).
- `dw3:card-played/<oponente>` y `dw3:card-won/<oponente>`: partidas de cartas (aquí `dw3:card-won/13`, que usa la misión).

```json
{ "text": "training-field:ranger-complete",
  "conditions": [["mod-flag", "dw3:card-won/13", 1], ["mod-flag", "training-field:seen-lake", 1]],
  "actions": [["give-money", 300], ["set-var", "training-field:quest", 2]] }
```

Las banderas y variables propias (`mod-flag`, `mod-var`, `set-var`) se guardan con la partida.

### Letrero de entrada (`banner`)

```json
"banner": { "area": "training-field:banner-area", "place": "training-field:banner-lower" }
```

Dos textos con nombre que se muestran al entrar (nombre del lugar y de la planta). Están en `text/<idioma>/stages.json` (`eng` es obligatorio; `es-419` y `spa` son opcionales).

### Cabecera de hablante (`speaker-of`) y sprites propios

Un texto puede empezar con `{speaker-of:TALK04:19}`: el puerto pone la cabecera que esa entrada tiene en el disco del jugador, en el idioma que se juega.
Así el letrero se presenta como los del juego sin copiar su texto.

```json
"navi-lake": "{speaker-of:TALK04:19}This is Lake Meadow. You can fish from the pier and from the shores. Bring a Fishing Pole!"
```

Los letreros usan un sprite propio, `sprites/sign/` (`sprite.json` + PNG de 32 x 40 px), que el actor nombra con `"sprite": "sign"`. `actor_width` de 32 le da una caja de 16 px de alto
por 32 de ancho, la de los letreros del juego, para poder leerlos de frente.

## Regenerar el arte

```
python3 pc/examples/mods/training-field/tools/make_art.py
```

Escribe las capas PNG, los `stage.json`, `tables/field_encounters.json`, `text/*/stages.json` y el sprite (misma salida byte a byte cada vez). Comprueba que el inicio, las salidas, las zonas de
combate, la cima de la meseta, los puntos de pesca y los personajes se alcanzan caminando. Para cambiar el mapa, edita la disposición en el script y vuelve a correrlo.

## Comprobar el mod

```
boot "<disco>.bin" --validate-mod training-field
```

Corre los lectores reales sobre el mod y dice un problema por línea; el resultado esperado es 0 errores y 0 avisos.

## Úsalo como base para tu escenario

1. Copia la carpeta con otro nombre y cambia `name` en `mod.json` (y cambia `training-field:` por tu prefijo en los textos, los encuentros y los `stage.json`).
2. Cambia el dibujo en `tools/make_art.py` (o pinta tus propios `background.png`, `floor.png` y `areas.png` del mismo tamaño) y vuelve a generar.
3. Reubica `start`, las salidas (`slots`), los combates (`battles`) y, si quieres, quita árboles, pesca, duelo o cartas: cada parte es independiente.
4. Cambia el punto de entrada en `stages/s330/stage.json` o apunta otro escenario del juego con un nuevo parche.
5. Escribe tus textos en `text/<idioma>/stages.json` y valida con `--validate-mod`.

Detalles de cada formato: `docs/modding/README.md` (§6.11b, la guía "Tu primer escenario nuevo") y `docs/stages.md` (§12 a 17).

## Notas

La bandera de visita (`training-field:visited.*`) se queda en la partida si quitas el mod. Los encuentros originales no cambian.
