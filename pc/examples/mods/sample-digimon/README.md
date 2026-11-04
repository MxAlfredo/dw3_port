# sample-digimon: cambiar Digimon existentes y agregar una forma de compañero (mod de contenido)

Ejemplo mínimo de un mod `"kind": "content"` que **cambia Digimon que ya existen** y **agrega una forma de compañero nueva** con
`content/digimon.json` (`digimon-v1`). No trae nada del disco.

## Qué hace

- Compañero `dw3:digimon/383` (la forma base que usa el juego al empezar): sube `hp` a 180, `stats.strength` a 60 y
  `stats.speed` a 45, y pone en su ranura de técnica 6 la técnica propia `sample-digimon:flame-jab` (nivel 1), declarada en
  `content/techs.json` de este mismo mod (copia de la técnica original 9 con poder 140 y coste 2).
- Enemigo `dw3:digimon/4` (uno de los primeros del juego): `stats.defense` a 200, y deja `dw3:item/45` con probabilidad 1023
  (siempre).

- Forma de compañero nueva `sample-digimon:brasamon` (`"new"`): copia de Kotemon (`dw3:digimon/383`) que presta el modelo de Monmon
  (`dw3:digimon/384`), con `hp` 220, `mp` 120, `stats.strength` 90 y `stats.speed` 60, aprende `flame-jab` al nivel 1 y se llama
  "Brasamon" en es-419 ("Embermon" en inglés). Pelea con sus estadísticas, técnica y modelo cuando es la forma activa y **se consigue jugando**
  con `"evolution"`: tras una batalla, el informe se la da al compañero `0` (Kotemon) que ya tiene Kotemon (`dw3:digimon/383`) y llegó
  al nivel 3 ("Digievolución aprendida: Brasamon"); la lista de evolución original no se toca, el mod le agrega esa entrada al final.
  Para probarla sin jugar hasta ahí hay un gancho del arnés: `DW3_SEED_PARTNER="0.0=sample-digimon:brasamon@5/0"` la pone en la primera
  entrada del primer compañero.

Los nombres y los valores originales de los 248 luchadores están en `<pref>/data/content/digimon.json` (lo escribe el juego
desde tu disco al arrancar).

## Archivos

- `mod.json`: `"kind": "content"` es obligatorio.
- `content/digimon.json`: el mapa `"new"` (la forma nueva: `base`, `model`, `name`, `evolution`, campos de `partner`) y el mapa `"change"`. La llave es `dw3:digimon/<id>` (o el número); el valor trae la faceta
  `partner` o `enemy` según el id. Campos, rangos y lo que no se puede cambiar: `docs/modding/README.md` §6.9.
- `content/techs.json` (`techs-v1`): la técnica nueva que usa el compañero (§6.8). Por nombre, así que no depende de números.

## Qué pasa al quitar el mod

Una forma nueva que quedó en una partida guardada vuelve a la forma original en que se basaba (Kotemon; si el compañero ya la tenía,
la entrada se vacía) con una línea `[saves]`; si estaba más allá de la entrada 44, la entrada se descarta (otra línea `[saves]`); la partida sigue cargando. En los cambios a Digimon existentes el juego vuelve a sus
valores: las filas de compañero se parchean en memoria al arrancar y las de enemigo al leer la tabla de batalla, y nada de esto se
guarda en la partida. Un enemigo cambiado afecta la siguiente batalla; si otro mod cambia el botín
de ese enemigo con `tables/battle_loot.json`, ese gana en `item` e `itemChance`.
