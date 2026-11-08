# Mods de ejemplo

Tres mods completos en el formato `asset-v1` y una carpeta modelo, para activarlos y ver cada cambio en tu propia partida. No traen datos del
juego (ADR-001): el arte y los textos son originales y se escribieron aquí (`training-field/tools/make_art.py` dibuja los dos pisos; el robot de
`sample-model` sale de `tools/test_model_import.py --demo`). Lo que enseñaban los ejemplos de tablas, objetos, técnicas, cartas, audio, video y
recolores está en la guía de mods (`docs/modding/README.md` §6 y §8), con un fragmento mínimo de cada uno.

## Instalar

```
pc/examples/install_examples.sh [--only training-field|sample-model|betamon-pack] [CARPETA_DE_USUARIO]
```

- La carpeta de usuario es `$DW3_USER_DIR` o `~/.local/share/dw3port/dw3` (la que tiene `data/`, `mods/`, `saves/` y `settings.ini`).
- Copia los ejemplos a `mods/` y agrega cada uno a `[mods] disabled` de `settings.ini`: **nada cambia hasta que actives uno**
  (pestaña *Mods* del menú de opciones, y reinicia). Antes guarda `settings.ini.bak`. Nunca toca `saves/`. Con `--list` solo imprime los nombres.
- No necesita `python3` ni tu disco para instalarse.
- Windows (`%APPDATA%\dw3port\dw3`) y Android: el script es `sh`; en Windows corre con Git Bash o copia las carpetas de `pc/examples/mods/` a `mods\` y
  agrega los nombres a `[mods] disabled`. En Android copia las carpetas desde la PC a la carpeta de datos de la app.

## Probarlos

Cada carpeta tiene un `README.txt` (en inglés) con qué cambia y dónde verlo. `boot "<disco>.bin" --validate-mod <nombre>` los revisa (0 errores, 0 avisos).
Para quitarlos, borra la carpeta o su nombre de `[mods] disabled`.

| Mod | Qué enseña | Dónde verlo |
|---|---|---|
| `training-field` | escenarios nuevos (`stages/<nombre>/`: arte, capas, salidas, encuentros, pesca, árboles, duelo y misión), un parche de un escenario del juego (`stages/s330/stage.json`, `stage-patch-v1`), un sprite nuevo (`sprites/sign/`), `tables/field_encounters.json` con filas nuevas y textos por idioma (`text/<idioma>/stages.json`) | Parque Central (330), junto a Leomon: hablar con el guía (con y sin la Medalla de Seiryu); `mode:300` y `mode:301` entran directo a los pisos |
| `sample-model` | un Digimon enemigo nuevo con modelo 3D propio (`digimon/boxbot/`: `digimon.json` y `model/` con glTF, PNG y `model.json`) y su lugar en dos encuentros (`content/encounters.json`), y una forma de compañero con modelo propio (`digimon/boxpal/`, con las ranuras de movimiento de un compañero; guía §8.2c) | batallas del Parque Central (encuentros 34 y 35); `DW3_BATTLE_ENEMY=251` con el atajo `batalla` |
| `betamon-pack` | un compañero nuevo con tres formas (`partners/betamon/partner.json`, `digimon/<forma>/digimon.json` con el modelo del enemigo del disco por referencia), un cuarto equipo inicial (`content/teams.json`), una charla que lo desbloquea (`stages/s330/stage.json` y `tables/special_conditions.json`) y arte de relleno generado; guía §6.9b «Hacer un paquete de compañero» | pantalla de equipo inicial (equipo D); Parque Central (330), abajo donde llegas: el pescador |
| `_template` | un archivo mínimo de cada tipo de mod; es lo que `boot --new-mod` copia (`configtest` compara su `README.txt` con el texto de ayuda) | no se instala |

`sample-model`, `training-field` y `betamon-pack` no tocan los mismos datos (`training-field` y `betamon-pack` parchan el escenario 330 con personajes distintos): se pueden activar juntos.

## Sin ejemplo: archivos crudos

No hay ejemplo de reemplazo de un archivo crudo (`files/`): los archivos sin formato abierto son cajas negras y no hay un cambio
seguro y visible que se pueda describir sin copiar bytes del disco. La ruta está explicada en `docs/modding/README.md` §6.4.
