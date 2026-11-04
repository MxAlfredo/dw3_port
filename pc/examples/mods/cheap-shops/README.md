# Mod de ejemplo: cheap-shops

Todos los objetos cuestan la mitad en las tiendas. Es un parche de la tabla `item_prices`
(`tables/item_prices.json`): la fila `"*"` aplica a todos los objetos y `"*0.5"` multiplica el precio
de compra (`price`); el resultado se redondea. Lo que las tiendas pagan al comprarte un objeto
(`sellPrice`) no cambia, y un objeto que originalmente no se puede vender sigue sin poder venderse.

Es un mod de tipo `assets` (el valor por defecto): solo cambia valores de filas que ya existen, así
que las partidas guardadas siguen siendo compatibles. El precio no se guarda en la partida: se lee de
la tabla cada vez que abres una tienda.

## Instalar

Copia la carpeta `cheap-shops` completa dentro de la carpeta `mods/` del port:

- Linux: `~/.local/share/dw3port/dw3/mods/` (o `mods/` junto al ejecutable si usas `portable.txt`).
- Windows: `%APPDATA%\dw3port\dw3\mods\` (o `mods\` junto a `boot.exe` en modo portátil).

Queda `mods/cheap-shops/mod.json` y `mods/cheap-shops/tables/item_prices.json`. Al arrancar el log
dice `tables: item_prices patched by mods/cheap-shops (N rows)`.

## Verlo funcionar

Abre cualquier tienda de objetos: los precios de la lista de compra son la mitad de los de siempre.
Con el log en `mods` (pestaña System, fila "Log", o `DW3_LOG=mods`), al abrir la
tienda el log dice, por cada objeto de la tienda cuyo precio cambió,
`[tables] item_prices item <n>: price <nuevo> (orig <original>) by mods/cheap-shops`.

## Probar otras cosas

Mira `data/tables/item_prices.json` (lo escribe el port con los valores originales de tu juego; la
llave es el id del objeto) y copia solo lo que quieras cambiar:

```json
{
  "format": "item_prices-v1",
  "rows": {
    "*": { "sellPrice": "*2" },
    "1":  { "price": 10 }
  }
}
```

## Quitar

Borra la carpeta `cheap-shops`. El port nunca escribe dentro de `mods/`.
