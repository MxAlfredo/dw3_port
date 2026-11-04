# Mod de ejemplo: shop-extra-item

La tienda número 0 (la armería de Asuka) vende un arma más. Es un parche de la tabla `shop_stock`
(`tables/shop_stock.json`): la fila `"0"` es la tienda número 0 y `{"add": [93]}` agrega el objeto con id 93 (un
arma que ninguna tienda vende) al final de su lista. El id es solo la elección de este ejemplo (cualquier id de 1 a
402 vale); no es una recomendación de nada. El mod no copia la lista original de la tienda: `add` añade al final de
lo que ya hay, y si el objeto ya estaba no lo repite.

En una armería conviene vender equipo (armas, que van de los ids 92 a 214, armaduras y accesorios). Un objeto que no es
equipo (por ejemplo un artículo importante) se vende igual: sus detalles salen sin las estadísticas de equipo y la
compra va directo a la bolsa.

Es un mod de tipo `assets` (el valor por defecto): no crea objetos ni ids nuevos, solo decide qué objetos
existentes se venden. Las partidas guardadas siguen siendo compatibles.

## Instalar

Copia la carpeta `shop-extra-item` completa dentro de la carpeta `mods/` del port:

- Linux: `~/.local/share/dw3port/dw3/mods/` (o `mods/` junto al ejecutable si usas `portable.txt`).
- Windows: `%APPDATA%\dw3port\dw3\mods\` (o `mods\` junto a `boot.exe` en modo portátil).

Queda `mods/shop-extra-item/mod.json` y `mods/shop-extra-item/tables/shop_stock.json`. El log dice
`tables: shop_stock patched by mods/shop-extra-item (1 rows)` cuando el juego arranca.

## Verlo funcionar

Entra a la armería de Asuka (tienda 0) y abre la lista de compra (BUY): el arma agregada sale al final de la
última página. Con el log en `mods` (pestaña System, fila "Log", o `DW3_LOG=mods`), al abrir
la tienda el log dice `[tables] shop_stock shop 0: <n> items (orig <m>) by mods/shop-extra-item`.

## Probar otras cosas

Mira `data/tables/shop_stock.json` (lo escribe el port con las listas originales de tu juego; la llave es el
número de tienda) y copia solo lo que quieras cambiar. Una lista se reemplaza entera o se edita:

```json
{
  "format": "shop_stock-v1",
  "rows": {
    "1": { "items": [5, 6, 7] },
    "2": { "items": { "remove": [5], "add": [9, 10] } },
    "*": { "items": { "add": [1] } }
  }
}
```

Una lista tiene de 1 a 64 objetos; una que quedaría más larga, vacía o con un id fuera de 1 a 402 se rechaza con una
línea en el log y la tienda queda como estaba.

## Quitar

Borra la carpeta `shop-extra-item`. El port nunca escribe dentro de `mods/`.
