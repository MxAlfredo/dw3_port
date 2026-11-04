# Mod de ejemplo: generous-loot

Los enemigos sueltan su objeto el doble de veces. Es un parche de la tabla `battle_loot`
(`tables/battle_loot.json`): la fila `"*"` aplica a todos los enemigos y `"*2"` multiplica la
probabilidad original (`itemChance`, en milésimas de 1024). El resultado se recorta al tope del campo
(1023, que siempre suelta). El objeto que suelta cada enemigo (`item`) no cambia, y un enemigo que
originalmente no suelta nada sigue sin soltar.

`itemChance` también escala la facilidad de robar ese objeto durante la batalla, así que este mod
facilita ambas cosas.

Es un mod de tipo `assets` (el valor por defecto): solo cambia valores de filas que ya existen, así
que las partidas guardadas siguen siendo compatibles.

## Instalar

Copia la carpeta `generous-loot` completa dentro de la carpeta `mods/` del port:

- Linux: `~/.local/share/dw3port/dw3/mods/` (o `mods/` junto al ejecutable si usas `portable.txt`).
- Windows: `%APPDATA%\dw3port\dw3\mods\` (o `mods\` junto a `boot.exe` en modo portátil).

Queda `mods/generous-loot/mod.json` y `mods/generous-loot/tables/battle_loot.json`. El log dice
`tables: battle_loot patched by mods/generous-loot (N rows)` cuando el juego lee la tabla de batalla
(al empezar una batalla). El cambio se nota en la siguiente batalla.

## Verlo funcionar

Pon el log en `mods` (pestaña System, fila "Log", o `DW3_LOG=mods`): al terminar una
batalla el log dice, por cada enemigo cuya fila cambió, `[tables] battle_loot enemy <id>: item ...
itemChance ... (orig ...)` y después si hubo objeto (`[tables] battle_loot drop: item <n>` o `none`).

## Probar otras cosas

Mira `data/tables/battle_loot.json` (lo escribe el port con los valores originales de tu juego; la
llave es el id del enemigo) y copia solo lo que quieras cambiar:

```json
{
  "format": "battle_loot-v1",
  "rows": {
    "*": { "itemChance": "+100" }
  }
}
```

## Quitar

Borra la carpeta `generous-loot`. El port nunca escribe dentro de `mods/`.
