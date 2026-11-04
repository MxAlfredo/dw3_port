# Mod de ejemplo: double-rewards

Cada batalla da el doble de EXP a los socios y el doble de dinero. Es un parche de la tabla
`battle_rewards` (`tables/battle_rewards.json`): la fila `"*"` aplica a todos los encuentros y las
operaciones `"*2"` multiplican el valor original. La EXP de los Digimon usados (`digimonExp`) no
cambia. Los valores se recortan a los topes del juego (EXP de socio 999999, dinero 9999999).

Es un mod de tipo `assets` (el valor por defecto): solo cambia valores de filas que ya existen, no
agrega filas ni identificadores nuevos, así que las partidas guardadas siguen siendo compatibles.

## Instalar

Copia la carpeta `double-rewards` completa dentro de la carpeta `mods/` del port:

- Linux: `~/.local/share/dw3port/dw3/mods/` (o `mods/` junto al ejecutable si usas `portable.txt`).
- Windows: `%APPDATA%\dw3port\dw3\mods\` (o `mods\` junto a `boot.exe` en modo portátil).

Queda `mods/double-rewards/mod.json` y `mods/double-rewards/tables/battle_rewards.json`. Al abrir el
juego, el log (`logs/`) dice `tables: battle_rewards patched by mods/double-rewards (N rows)` la
primera vez que se abre el reporte de una batalla. El cambio se nota en el siguiente reporte de
batalla.

## Probar otras cosas

Mira `data/tables/battle_rewards.json` (lo escribe el port con los valores originales de tu juego) y
copia solo las filas que quieras cambiar:

```json
{
  "format": "battle_rewards-v1",
  "rows": {
    "12": { "money": 300 },
    "*":  { "money": "*1.5" }
  }
}
```

Un número reemplaza el valor; `"*k"`, `"+k"` y `"-k"` operan sobre el valor actual. La fila con su
número gana sobre `"*"` en el campo que nombra.

## Quitar

Borra la carpeta `double-rewards`. El port nunca escribe dentro de `mods/`.
