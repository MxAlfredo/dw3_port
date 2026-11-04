# sample-card: una carta nueva (mod de contenido)

Ejemplo mínimo de un mod `"kind": "content"` que agrega **una carta nueva** con arte propio. Usa solo texto y arte propios; no trae
nada del disco.

## Qué hace

- `sample-card:test-card`: una carta de Digimon (`"base": "dw3:card/120"`: se copia su ficha y su arte) con AP 45 y HP 40 y el arte
  de `card/test-card.png` (un disco rojo sobre azul con borde dorado, hecho para este ejemplo).
- Recibe el id 317 (o el siguiente libre; los ids dependen de los mods instalados y nunca se guardan). El álbum crece para mostrarla y el
  editor de mazos la lista cuando la tienes.

- **Se vende, sale en sobres y la juega un rival** (U8c), con tablas propias del mismo mod:
  `tables/card_shop_stock.json` la agrega a la tienda 49 (en lugar de la carta 59), `tables/card_pack_slots.json` la pone en las dos primeras
  casillas del sobre 91 (fila `910`) y `tables/card_opponents.json` la mete en las tres primeras casillas del mazo del rival de argumento 3 (fila `2`).
  Su precio es `"price": 120` de `cards.json`.

## Archivos

- `content/cards.json` (`cards-v1`): el mapa `"new"`. `"base"` (obligatorio): una carta original 1..314, por nombre (`"dw3:card/120"`)
  o por número; de ahí se copian la ficha (color, AP, HP, tipo, puntos, nivel, rango) y, en las cartas 1..60, el efecto.
  Campos: `price` 0..32767 (la tienda de cartas; 1 si falta), `color` 1..6, `ap`/`hp` 0..99, `kind` 0..16, `points` 0..99, `level` 0..9, `rank` 0..999, `comboCard` 0..314 y, para
  una carta de opción, `effect` `{ "like": 1..60, "condition", "playCondition", "message", "target", "script": [pasos 1..174] }`.
  `name` (con `eng` obligatorio) y `description` (el texto del efecto) por idioma.
- `card/<llave>.png`: el arte, PNG indexado de 32x32 con hasta 256 colores (el índice 0 con transparencia queda transparente).
  Sin archivo, la carta usa el arte de su base.

## Qué pasa al quitar el mod

Las partidas guardan el nombre, no el número (sección `EXTK`). Sin el mod, la carta se descarta al cargar con una línea en el log
(`[saves] card "sample-card:test-card" is not defined by any installed mod; dropped from the loaded save`) y, si estaba en un mazo, ese
lugar vuelve a la carta del mazo inicial; la partida carga normal.
