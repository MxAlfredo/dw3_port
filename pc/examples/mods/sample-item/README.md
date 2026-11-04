# sample-item: un objeto nuevo (mod de contenido)

Ejemplo mínimo de un mod `"kind": "content"` que agrega **un objeto nuevo** y lo vende en la primera tienda.
Usa solo texto propio; no trae nada del disco.

## Qué hace

- `sample-item:test-gadget`: un consumible que cura 300 PS (menú y batalla), con el aspecto y las reglas de un
  objeto original (`"base": "dw3:item/43"`, un consumible de curación) pero con precio 40 y venta 20.
- Aparece en la tienda 0 con su nombre; al comprarlo queda en la bolsa y se guarda con la partida.
- Todos los enemigos lo sueltan siempre (`battle_loot`, `itemChance` 1023): cambia o quita ese archivo si no lo quieres.
- La descripción en `es-419` es larga a propósito: la caja de descripción es de una línea, así que el port la acorta en
  una palabra con `...` y deja una línea en el log (`text: ITMINF item 403 (es-419) description is wider than the box`).

## Archivos

- `mod.json`: `"kind": "content"` es obligatorio para declarar objetos.
- `content/items.json` (`items-v1`): el mapa `"new"`. La llave (`test-gadget`) es solo minúsculas, dígitos, `-` y `_`;
  el nombre completo del objeto es `<carpeta del mod>:<llave>` (aquí `sample-item:test-gadget`).
  - `"base"` (obligatorio): un objeto original (1..402), por su nombre `"dw3:item/43"` o por su número, del que se copia todo
    (tipo, efecto, equipo). Los nombres originales están en `data/content/items.json` (lo escribe el juego desde tu disco).
  - `"price"`, `"sellPrice"` (0..65535), `"effect"` `{ "amount", "kind", "flags" }` (solo si la base es un consumible).
  - `"name"` y `"description"`: por idioma (`eng`, `fra`, `ita`, `ger`, `spa`, `es-419`). `eng` en `name` es
    obligatorio; un idioma que falta usa el inglés (`es-419` cae primero a `spa`).
- `tables/shop_stock.json`: el parche de tiendas de siempre; el objeto nuevo se escribe **por nombre**
  (`"sample-item:test-gadget"`), porque su número depende de los mods instalados.
- `tables/battle_loot.json`: lo mismo para el botín; el campo `item` acepta el nombre (o un número si el objeto es
  original). Otro mod puede cambiar su precio con una fila `"sample-item:test-gadget"` en `item_prices`.

## Qué pasa al quitar el mod

Las partidas guardan el nombre, no el número. Sin el mod, el objeto se descarta al cargar con una línea en el log
(`[saves] item "sample-item:test-gadget" is not defined by any installed mod; dropped from the loaded save`) y la
partida carga normal; si vuelves a instalarlo, el objeto no regresa (se descartó al cargar; vuelve a guardar para fijarlo).
