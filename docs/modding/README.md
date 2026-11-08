# Guía de mods del port de Digimon World 3

> English version: [README.en.md](README.en.md) (mismo contenido).

Esta guía es la referencia de lo que se puede modificar **hoy** y de cómo hacerlo. Lo que no está aquí no es API
(ADR-006 §7). Los mods son carpetas: se instalan copiándolas y se quitan borrándolas; el port nunca escribe dentro de
`mods/` y un mod roto no detiene el juego (queda en el log y se usa lo siguiente).

Describe solo el formato actual de los assets (contrato `asset-v1`, ADR-016): una carpeta por tema y la misma forma en `data/` y en
`mods/<mod>/`. Si tienes un mod o una carpeta `data/` de una versión anterior, ve a la [sección 11](#11-migración-desde-el-formato-anterior).

## Empieza aquí

1. **¿Qué puedo modificar?** Textos, imágenes, sprites, audio, películas, todas las tablas del juego (precios, recompensas, botín,
   evoluciones, tiendas, cartas...), objetos, técnicas, cartas, enemigos, formas de compañero y modelos 3D nuevos, y los personajes
   y charlas de un escenario. La lista completa, con el archivo de cada cosa, es la tabla de la sección 1.
2. **¿Dónde van los mods?** En la carpeta `mods/` de tu carpeta de preferencias (`<pref>`, sección 2): Linux
   `~/.local/share/dw3port/dw3/mods/`, Windows `%APPDATA%\dw3port\dw3\mods\` (versión portátil: `mods\` junto a `DW3.exe`), Android
   `Android/data/io.github.dw3port/files/mods/` (los mods se hacen en una PC y se copian allí; el juego los lee solo al arrancar,
   así que reinícialo con Options > System > Restart game).
3. **Correr las herramientas.** El ejecutable del juego (`boot`) tiene opciones de línea de comandos: en una compilación desde el
   código es `pc/build/boot`; en Windows usa `DW3-debug.exe` (el mismo juego con una ventana de consola); el AppImage de Linux acepta
   las mismas opciones. `--new-mod`, `--list-mods` y `--validate-mod` están en la sección 2c; `--export-all` escribe en
   `<pref>/data/` todo lo que tiene el disco para que lo veas y lo edites.
4. **Primer mod en 5 minutos.** Desde una copia de este repositorio, abre el juego una vez con tu disco (para que exista `<pref>/data/`) y sigue la
   sección 2c: un parche de una línea (`tables/item_prices.json`, precios a la mitad) que se valida con `--validate-mod`. Cuando quieras ver un mod
   grande, la sección 7 tiene dos ejemplos completos (`training-field`, escenarios nuevos; `sample-model`, un Digimon con modelo propio) y una
   carpeta modelo (`_template`); cada uno trae un `README.txt` que dice qué cambia y dónde verlo.
5. **Herramientas** (sección 8): `tools/file_names.py`, `tools/extract_disc.py`, `tools/image_atlas_preview.py`,
   `tools/image_mod_palette.py`, `tools/techcompat.py`, `tools/balance_compare.py`, `tools/model_export.py`, `tools/model_import.py`. Se corren con
   `python3 tools/<nombre>.py ...`; todas leen tu propio disco y no guardan datos del juego.
6. **Formatos y datos (referencia):** [`docs/formats.md`](../formats.md) (formatos de archivo), [`docs/data-tables.md`](../data-tables.md),
   [`docs/digimon-anatomy.md`](../digimon-anatomy.md), [`docs/extended-state.md`](../extended-state.md) (guardados y estado
   extendido), [`docs/stages.md`](../stages.md) (datos de escenario) y los esquemas en [`schema/`](schema/).
7. **Compartir tu mod:** solo trabajo propio, nunca datos del juego (sección 9). Cuenta qué cambia y qué versión del port necesita
   (la clave `port` de `mod.json`).
8. **Validar:** `boot "<disco>.bin" --validate-mod <mod>` revisa un mod sin abrir el juego (sección 2d). **Venías de otra versión:** la
   sección 11 trae la tabla de rutas viejas y nuevas.

## 1. Qué se puede modificar hoy

| Qué | Cómo | Sección |
|---|---|---|
| Texto del juego (cualquier idioma) | `text/<idioma>/<archivo>.json` | §6.1 |
| Tiempos del banner de escenario y de las transiciones | `config/stage_banner.json`, `config/transitions.json` | §3 |
| Recompensas de batalla (EXP y dinero por encuentro) | `tables/battle_rewards.json` | §6.5 |
| Botín de los enemigos (objeto y probabilidad por enemigo) | `tables/battle_loot.json` | §6.5 |
| Precios de los objetos (compra y venta) | `tables/item_prices.json` | §6.5 |
| Qué vende cada tienda de objetos | `tables/shop_stock.json` | §6.5 |
| Armas, armaduras y accesorios (estadísticas) | `tables/weapon_data.json`, `armor_data.json`, `accessory_data.json` | §6.5 |
| Qué hace cada objeto usable | `tables/item_effects.json` | §6.5 |
| Evoluciones, ráfagas y crecimiento de los compañeros | `tables/evolutions.json`, `blast_forms.json`, `level_exp.json`, `vital_cuts.json`, `vital_random.json`, `stat_gains.json`, `resist_gains.json` | §6.5b |
| Encuentros del campo: enemigos, emboscada, tasa de batalla | `tables/field_enemies.json`, `field_encounters.json`, `battle_rates.json` | §6.5b |
| Cartas: precios, tiendas, sobres, efectos, premios, mazo por defecto y rivales | `tables/card_prices.json`, `card_shop_stock.json`, `card_pack_slots.json`, `card_effects.json`, `prize_items.json`, `default_deck.json`, `card_opponents.json` | §6.5b |
| Gimnasio y posadas: entrenamientos, ganancias, costo de intensidad y precio de la posada | `tables/gym_trainings.json`, `gym_stat_gains.json`, `gym_stat_losses.json`, `gym_resist_gains.json`, `gym_hp_mp_gains.json`, `gym_intensity_costs.json`, `inns.json` | §6.5b |
| Digimon existentes (compañeros y enemigos: estadísticas, técnicas, botín), Digimon nuevos con su modelo y en qué encuentros aparecen | `digimon/d<nnn>/digimon.json` (cambio), `digimon/<llave>/` (nuevo), `content/encounters.json` (mod de contenido) | §6.9 |
| Compañeros nuevos (nombre, forma inicial, equipo, lista de evolución) y equipos iniciales nuevos | `partners/<llave>/partner.json` y `content/teams.json` (mod de contenido) | §6.9b |
| Cartas nuevas (ficha, efecto, arte y nombre) | `cards/<llave>/card.json` y `face.png` (mod de contenido) | §6.10 |
| Personajes de un escenario del campo (cambiar, agregar, quitar) y sus charlas, con banderas propias del mod; salidas, eventos con guion y batallas de área | `stages/s<nnn>/stage.json` (`stage-patch-v1`) | §6.11 |
| Escenarios de campo nuevos (mapa dibujado con PNG, colisión, salidas, batallas) | `stages/<nombre>/` (`stage-new-v1`) | §6.11b |
| Imágenes sueltas que aún no se editan por pieza: logos y título (`TLOGO*`, `TTCOMMPK`), final (`END__ALL`), ventana de país (`CNTSELWN`), ventanas del combate de cartas (`CARDDATA`), `FIELDCOM` y el mapa del mundo (`STWMP0`, `STWMP1`) (PNG, mapa `atlas-v1`) | `images/<carpeta>/<archivo>/<NN>_<MM>.png` | §6.2 |
| Sprites del campo (PNG por cuadro, mapa `sprites-sprite-v1`) | `sprites/<nombre>/<EEE>.png` | §6.3 |
| Dibujo de una carta (PNG de 32x32 y 256 colores; 316 cartas) | `cards/c<nnn>/face.png` | §6.2b |
| Fondos de escenario (4 archivos; PNG por página) | `images/field/stage/s<nnn>bg<nn>/<ii>.png` | §6.2c |
| Objetos de mapa de un escenario del campo (PNG por pieza) | `stages/s<nnn>/objects/<pieza>.png` | §6.2h |
| Un mosaico del mapa de un escenario del campo (PNG de 8 bits por baldosa de 128 px) | `stages/s<nnn>/tiles/<FF>_<CC>.png` | §6.2d |
| Los archivos sin formato abierto todavía (mapas de colisión, efectos de batalla, overlays...) como copia cruda de partida | `raw/<ruta del disco sin AAA/>` | §6.2e |
| Los 8 compañeros originales y los de un mod (retrato, ícono, sprite del campo y escena de entrenamiento del gimnasio: se importan) | `partners/<nombre>/`, `menus/gym/effects/<entrenamiento>/` | §6.2f |
| Las pantallas por pieza, con sus colores reales (se exportan y se leen de vuelta, en la geometría original) | `menus/<pantalla>/<pieza>.png`, `menus/<pantalla>/menu.json` | §6.2g |
| **Cualquier archivo del disco**, crudo | `files/<ruta del disco sin AAA/>` | §6.4 |
| Una película del juego (AVI MJPEG con audio PCM) | `video/<NOMBRE>.avi` | §8.1b |
| Un efecto de sonido (una muestra de un banco, WAV de 16 bits) | `audio/<BANCO>/vag_NNN.wav` | §8.1d |
| Una canción (WAV de 16 bits en lugar de una secuencia, con bucle) | `audio/<BANCO>/seq_<paquete>_<registro>.wav` | §8.1d |

Sprites nuevos para personajes del campo: `mods/<mod>/sprites/<nombre>/` con un nombre que no sea el de una original (`sprite.json` + un PNG por cuadro; los cuadros de una original van en `sprites/<nombre-original>/<EEE>.png`, §6.3), que un parche de escenario nombra con `"sprite"` (`docs/stages.md` §11). Todavía no: scripts (Lua, más adelante) y audio XA suelto (solo dentro de las películas, 8.1b). Ver §10.

## 2. Dónde está cada cosa

`<pref>` es la carpeta de preferencias del port:

| Sistema | `<pref>` |
|---|---|
| Linux | `~/.local/share/dw3port/dw3/` |
| Windows | `%APPDATA%\dw3port\dw3\` |
| Modo portátil (`portable.txt` junto al ejecutable) | la carpeta del ejecutable |
| Android | `Android/data/io.github.dw3port/files/` (visible desde una PC por USB) |

```
<pref>/settings.ini          del jugador; su sección [mods] fija el orden
<pref>/logs/                 un log por ejecución (se guardan los 10 últimos)
<pref>/data/                 del port, generado desde TU disco; se puede borrar y se rehace (el árbol, abajo)
<pref>/mods/<mod>/           de la comunidad: una carpeta por mod
<pref>/saves/default/        tus partidas del port: slot0-2.dw3save (formato propio, ADR-008) y view/slot<N>.json (vista de solo lectura)
<pref>/import/               tarjetas .mcd de PS1 por importar (la primera vez se importan una sola vez)
```

**El árbol de `data/`** tiene la misma forma que un mod: `data/<tema>/…` es `mods/<mod>/<tema>/…`. Un **asset** es una carpeta (o un archivo suelto si es un
solo medio: una película `.avi`); cada tema lleva su `manifest.json` (`asset-manifest-v1`, solo en `data/`) y cada JSON su `"format"`.

```
<pref>/data/
  stages/s<nnn>/          stage.json  tiles/  objects/                      un escenario original (§6.2d, §6.2h, §6.11)
  cards/c<nnn>/           face.png  card.json                               una carta original, c001 a c316 (§6.2b)
  partners/<nombre>/      partner.json  portrait.png  icon.png  field/  training/   un compañero original (§6.2f)
  digimon/d<nnn>/model/   model.gltf  model.bin  model.png  model.json      un luchador original (§8.1)
  sprites/<nombre>/       sprite.json  <EEE>.png                            un personaje del campo (§6.3)
  menus/<pantalla>/       menu.json  <pieza>.png                            una pantalla por piezas; menus/gym/effects/ (§6.2g)
  images/<carpeta>/<archivo>/<NN>_<MM>.png  (+ .json atlas-v1)              las imágenes sueltas que quedan (§6.2)
  audio/<BANCO>/          bank.json  sequences.json  vag_NNN.wav  seq_P_RR.mid   un banco de sonido (§8.1c)
  video/<NOMBRE>.avi                                                        una película (§8.1b)
  text/<idioma>/<archivo>.json                                              el texto (§6.1)
  tables/<tabla>.json                                                       las tablas con los valores originales (§6.5)
  content/                items.json  techs.json  digimon.json  cards.json  nombres del contenido original (solo referencia)
  config/                 stage_banner.json  transitions.json  claves y rangos (§3)
  raw/<ruta del disco, en minúsculas, sin AAA/>                             copia cruda de lo que no tiene formato abierto (§6.2e)
```

Un mod usa los mismos nombres (sección 3). `raw/` es solo la copia de referencia: en un mod, los archivos crudos van en `files/` (§6.4).

El **log** es el primer lugar donde mirar: `<pref>/logs/dw3-AAAAMMDD-HHMMSS.log`. Cada capa deja una línea por mod
(`[files] override ...`, `[images] override ...`, `[config] ... from ...`, `[mods] ... disabled: ...`) y cada rechazo con su
motivo. Para ver qué lee el juego en ese momento, pon el log en `trace` (sección 6.6): fila **Log** del menú, o
`DW3_LOG=trace` (Linux `DW3_LOG=trace <ejecutable>`, Windows cmd `set DW3_LOG=trace` y luego el `.exe`); `DW3_LOG_CHANNELS=files`
lo limita a las lecturas de archivos.

### 2b. Conseguir los archivos de modding: un botón o una bandera

Lo liviano (texto, imágenes, tablas y archivos crudos) se exporta solo en el primer arranque. Lo pesado (videos, audio, modelos y
escenarios, unos 700 MB y 2-3 minutos) va detrás de **una sola** entrada:

- **Menú:** *Opciones > System > Prepare modding files*. Corre los cuatro exportadores uno tras otro en hilos de trabajo (el juego
  sigue usable); el valor de la fila muestra el paso (`Videos 3/14`, `Audio 12/71`, `Models 96/248`, `Stages 40/237`) y al final
  `Done` o `Done, N failed`. Es idempotente: lo que ya está al día se salta, así que una segunda pulsación termina en segundos.
- **Línea de comandos:** `boot "<disco>.bin" --export-all` exporta todo, imprime el avance en la salida estándar (`export-all: Videos 3/14`),
  no abre ventana y termina con código 0, o distinto de 0 si algo falló (los detalles quedan en el registro). Sirve sin pantalla ni
  sonido (`SDL_VIDEODRIVER=offscreen`, `ALSOFT_DRIVERS=null`).
- **Avanzado:** las claves `[data] export_models`, `export_videos`, `export_audio` y `export_stages` de `settings.ini` siguen
  funcionando (exportan al arrancar, también por nombre: `export_stages = S233, S330`); no hace falta tocarlas.

### 2c. Tu primer mod en 5 minutos

Cuatro banderas de la línea de comandos (sin abrir el juego) hacen el ciclo crear, probar, ajustar:

```
boot --new-mod mi-mod                      # crea <pref>/mods/mi-mod/ con mod.json, README.txt y carpetas vacías
boot --list-mods                           # qué mods hay: versión, tipo, activo o no (y por qué) y orden de carga
boot "<disco>.bin" --validate-mod mi-mod   # corre los lectores reales sobre ese mod; un problema por línea
boot "<disco>.bin" --validate-mods         # lo mismo con todos los mods instalados
                                          # <disco> puede ser el .bin o el .cue; sin él usa el disco que el juego recuerda
```

1. `boot --new-mod mi-mod` (o `--new-mod mi-mod --kind content`; `--kind` acepta `assets`, `content` o nombres de carpeta:
   `config content tables text images audio video files`, separados por comas). Nunca pisa un mod que ya existe. Las carpetas de `stages/`, `cards/`, `partners/`, `digimon/`, `sprites/` y `menus/` se crean a mano (sección 3). Hay una carpeta modelo
   con un ejemplo mínimo por tipo en `pc/examples/mods/_template/`.
2. Un parche de una línea: `mods/mi-mod/tables/item_prices.json` con
   `{"format": "item_prices-v1", "rows": {"*": {"price": "*0.5"}}}` (todo a mitad de precio).
3. `boot "<disco>.bin" --validate-mod mi-mod` termina con código 0 y `ok`, o con 1 y una línea por problema con el archivo y la clave
   (`tables/battle_rewards.json row "12" field "money" rejected: ...`). Código 2: no existe ese mod. `--validate-mod` acepta también la
   ruta de una carpeta (un mod que aún no instalaste).
4. Juega: con `DW3_LOG=mods` el registro dice `tables: item_prices patched by mods/mi-mod (234 rows)`.

Qué revisa en cada carpeta, y qué no, está en la sección 2d. Sin disco (`--list-mods`, `--new-mod`) no hace falta pasar la imagen.

### 2d. Validar un mod

`boot "<disco>.bin" --validate-mod <mod>` (o `--validate-mods`, con todos) corre sobre el mod los **mismos lectores que el juego**, sin abrirlo: una línea por problema, con la ruta
dentro del mod. Es `error` si el juego rechaza el archivo, `warning` si lo ignora sin decir nada y `skipped ... not checked` si no se puede comprobar. Código de salida 0 (sin
errores), 1 (con errores) o 2 (no existe ese mod).

| Carpeta del mod | Qué revisa |
|---|---|
| `mod.json` | formato, claves desconocidas, `version`, `port`, `requires`, orden y dependencias |
| `tables/` | que la tabla exista (con «did you mean») y cada fila y campo, con los mismos rechazos que al jugar |
| `content/` | `items.json`, `techs.json`, `teams.json` y `encounters.json` con sus lectores; avisa de otro nombre de archivo y de un mod que no es `"kind": "content"` |
| `config/` | las claves y sus rangos |
| `digimon/` | cada `digimon.json` (cabecera, `name`, campos, rangos) y `model/model.json`; avisa de `d<nnn>/model/` (no se lee) y de una carpeta sin registro ni modelo |
| `partners/` | `partner.json`; `portrait.png` e `icon.png` (tamaño y formato), los cuadros de `field/` y cada `training.json`; avisa de lo que nadie lee, de una carpeta que ninguna original ni mod declara y de una carpeta que traen dos capas (cuál gana) |
| `cards/` | cada `card.json` con el lector del juego y el tamaño de `face.png`; avisa de `c<nnn>` fuera de `c001`-`c316`, sin `face.png` o con `card.json`, y de una carpeta sin `card.json` |
| `stages/` | el parche `stage.json`, los escenarios nuevos, las baldosas (`tiles/`: formato y tamaño frente al original) y las piezas de `objects/` (tamaño y nombre); avisa de archivos sueltos y de carpetas que ningún escenario usa |
| `menus/` | el tamaño de cada pieza; avisa de un archivo que ninguna pieza lleva por nombre y de una pieza que traen dos mods |
| `sprites/` | los cuadros `NNN.png` de un original y el `sprite.json` de un sprite nuevo; avisa de lo que no se lee (un `sprite.json` en un original, el seguidor de un compañero) |
| `images/` | cada PNG: tamaño, formato indexado y número de colores |
| `audio/`, `video/` | los WAV y AVI con los lectores del juego |
| `files/` | que cada archivo exista en el disco (pide la imagen del disco) |
| `text/` | **no se revisa** (`skipped ... not checked`): el texto se lee al abrir cada archivo; la paleta de las baldosas también se compara al cargar el mapa |

Cualquier otra carpeta de primer nivel sale como `folder <nombre>/ is never read` (con «did you mean» si se parece a un tema); es lo que verás con carpetas del formato
anterior (sección 11). La carpeta `tools/` de un mod (tus propios scripts) no se lee ni se avisa.

### 2e. Esquemas JSON (autocompletado y errores en el editor)

`docs/modding/schema/` trae un esquema por formato. En VS Code, agrega al inicio del archivo `"$schema": "<ruta al esquema>"` (relativa al archivo o `file:///...`); el puerto
ignora esa clave. Los esquemas salen de lo que leen los lectores (claves, tipos, rangos); el rango de cada campo de una tabla está en su archivo de referencia
`<pref>/data/tables/<tabla>.json`.

| Esquema | Archivo | Lo lee el puerto |
|---|---|---|
| [`mod.schema.json`](schema/mod.schema.json) | `mod.json` | sí |
| [`table-patch.schema.json`](schema/table-patch.schema.json) | `tables/<tabla>.json` | sí |
| [`items.schema.json`](schema/items.schema.json), [`techs.schema.json`](schema/techs.schema.json), [`teams.schema.json`](schema/teams.schema.json), [`encounters.schema.json`](schema/encounters.schema.json) | `content/items.json`, `techs.json`, `teams.json`, `encounters.json` | sí |
| [`digimon.schema.json`](schema/digimon.schema.json) | `digimon/<llave>/digimon.json` y `digimon/d<nnn>/digimon.json` | sí |
| [`partner.schema.json`](schema/partner.schema.json) | `partners/<llave>/partner.json` (un compañero nuevo) | sí |
| [`card.schema.json`](schema/card.schema.json) | `cards/<llave>/card.json` (una carta nueva) | sí |
| [`stage-patch.schema.json`](schema/stage-patch.schema.json), [`stage-new.schema.json`](schema/stage-new.schema.json), [`stage-text.schema.json`](schema/stage-text.schema.json) | `stages/s<nnn>/stage.json` (parche), `stages/<llave>/stage.json` (escenario nuevo), `text/<idioma>/stages.json` | sí |
| [`sprite.schema.json`](schema/sprite.schema.json) | `sprites/<nombre>/sprite.json` (el de un sprite nuevo; el exportado es informativo) | solo el de un sprite nuevo |
| [`manifest.schema.json`](schema/manifest.schema.json) | `data/<tema>/manifest.json` (`asset-manifest-v1`) | solo en `data/` |
| [`stage.schema.json`](schema/stage.schema.json), [`stage-tiles.schema.json`](schema/stage-tiles.schema.json), [`stage-objects.schema.json`](schema/stage-objects.schema.json) | `stages/s<nnn>/stage.json` (`stage-v1`), `tiles/tiles.json`, `objects/objects.json` exportados | no (informativo) |
| [`menu.schema.json`](schema/menu.schema.json) | `menus/<pantalla>/menu.json` exportado | no (informativo) |
| [`card-export.schema.json`](schema/card-export.schema.json), [`partner-export.schema.json`](schema/partner-export.schema.json), [`partner-training.schema.json`](schema/partner-training.schema.json) | el `card.json` y el `partner.json` de un original, y el `training.json` de cada entrenamiento | no (el `training.json` de un compañero de mod sí) |


## 3. Estructura de un mod

```
mi-mod/
  mod.json                              obligatorio
  config/stage_banner.json              solo las claves que cambias; también config/transitions.json
  tables/<tabla>.json                   parches de tablas (§6.5)
  text/<idioma>/<archivo>.json          el texto; idiomas: eng fra ita ger spa es-419 (§6.1)
  content/items.json, techs.json, teams.json, encounters.json   contenido nuevo (kind "content")
  digimon/d<nnn>/digimon.json           cambia un Digimon original (§6.9)
  digimon/<llave>/digimon.json          un Digimon nuevo (§6.9) y, si trae su modelo glTF, digimon/<llave>/model/ (§8.2)
  partners/<llave>/partner.json         un compañero nuevo (§6.9b) junto a su arte: portrait.png, icon.png, field/, training/ (§6.2f)
  partners/<nombre>/portrait.png ...    el arte de un compañero original (agumon...): solo se reemplaza el arte (§6.2f)
  cards/c<nnn>/face.png                 el dibujo de la carta original <nnn> (§6.2b)
  cards/<llave>/card.json + face.png    una carta nueva (§6.10)
  stages/s<nnn>/stage.json              parche de los personajes y charlas de un escenario original (§6.11)
  stages/s<nnn>/tiles/<FF>_<CC>.png     una baldosa del mapa (§6.2d)
  stages/s<nnn>/objects/<pieza>.png     una pieza de los objetos del mapa (§6.2h)
  stages/<llave>/stage.json             un escenario nuevo (§6.11b) y sus mapas: background.png, floor.png, triggers.png, areas.png
  sprites/<nombre>/<EEE>.png            un cuadro de un sprite original (p001play) (§6.3)
  sprites/<nombre>/sprite.json + PNG    un sprite nuevo (docs/stages.md §11)
  menus/<pantalla>/<pieza>.png          una pieza de una pantalla (§6.2g)
  images/<carpeta>/<archivo>/<NN>_<MM>.png   una de las imágenes sueltas que quedan (§6.2)
  files/<ruta del disco sin AAA/>       un archivo del disco, crudo; p. ej. files/dat/z_stage/s330pack.bin (§6.4)
  video/<NOMBRE>.avi                    una película reemplazada (§8.1b), p. ej. video/MOVIEOPN.avi
  audio/<BANCO>/vag_NNN.wav             una muestra de sonido reemplazada (§8.1d), p. ej. audio/COMMON/vag_056.wav
  audio/<BANCO>/seq_P_RR.wav            una canción en lugar de una secuencia (§8.1d), con seq_P_RR.json opcional (bucle)
```

Misma ruta que en `data/` = **reemplazo** de ese archivo; una carpeta con un nombre nuevo = un **asset nuevo** que se agrega. Lo que falta en una carpeta usa el archivo del original.
Los nombres `s<nnn>`, `c<nnn>` y `d<nnn>` (una letra y tres cifras) están **reservados** para los originales: una llave propia es a-z, 0-9, `-` y `_`.

Todo lo que el port escribe va **en minúsculas** (`data/sprites/p001play/...`) y en un mod conviene copiar esos nombres tal cual: `sprites/`, `cards/` y `digimon/` esperan las llaves de las originales en minúsculas
(`p001play`, `c012`, `d383`). Al leer `images/`, `files/` y las baldosas el port no distingue mayúsculas, así que un mod escrito con las rutas del disco en mayúsculas
(`files/DAT/Z_STAGE/S330PACK.BIN`) también funciona, en Windows, Linux y Android. El contenido nuevo (objetos, técnicas) va en
`content/<tipo>.json` y los parches de tablas en `tables/<tabla>.json`.

- **Config:** se mezcla **por clave** (un mod que solo cambia `hold_frames` hereda el resto). Claves y rangos: los
  archivos de referencia que el port escribe en `<pref>/data/config/` (con un objeto `"notes"` que explica cada clave).
- **Texto, imágenes y archivos crudos:** reemplazan el recurso **entero**.
- Cada archivo lleva `"format": "<nombre>-v<n>"`; una versión desconocida se rechaza, no se adivina.

## 4. `mod.json` (formato `mod-v1`)

```json
{
  "format": "mod-v1",
  "name": "Mi mod",
  "version": "1.0.0",
  "author": "yo",
  "description": "Qué hace, en una línea.",
  "kind": "assets",
  "port": ">=0.4",
  "requires": ["base>=1.2", "otro-mod"]
}
```

| Clave | Obligatoria | Significado |
|---|---|---|
| `format` | sí | siempre `"mod-v1"` |
| `name`, `author`, `description` | no | informativas |
| `version` | no | dígitos y puntos (`1.2.0`); sin ella vale `0`. Es lo que comparan los `requires` |
| `kind` | no | `"assets"` (por defecto: reemplaza recursos) o `"content"` (agrega o cambia datos: objetos, técnicas, Digimon, cartas y compañeros nuevos) |
| `port` | no | versión mínima del port: `">=0.4"`. Un port más viejo desactiva el mod. Operadores: `>=` `>` `<=` `<` `=` |
| `requires` | no | lista de mods de los que depende, por **nombre de carpeta**: `"base"` o `"base>=1.2"` |

Las claves que el port no conoce se ignoran (el formato crece de forma compatible). Un valor mal escrito (`kind`
desconocido, `requires` que no es una lista de textos, versión con letras) **rechaza** el mod con
`mods: <ruta> rejected: mod.json: <motivo>`. La versión de este port está en `pc/shim/port_version.h`.

## 5. Orden de carga y dependencias

**Pestaña Mods (menú de opciones, también en Android).** Lista cada mod instalado con su estado: `On 2` (segundo en el orden de carga), `Off`,
`Blocked` (una dependencia o la versión del port lo frena), `Invalid` (`mod.json` roto) o `Missing` (está en `order` pero no instalado). La línea
de abajo da la versión, el tipo y, si está bloqueado o inválido, el motivo. *Confirmar* enciende o apaga el mod; *Izquierda* y *Derecha* lo
mueven antes o después en el orden. Los cambios se escriben en `[mods]` de `settings.ini` (`order` solo si ya existía o si moviste algo;
`disabled` para los apagados) y **se aplican al reiniciar** (la fila muestra `Restart`). Cabe una lista de hasta 14 filas y las líneas de
`settings.ini` deben caber en 511 caracteres; con más mods, edita el archivo a mano. La fuente del juego no tiene comas, paréntesis ni la
letra j: los motivos largos se muestran recortados (el motivo completo está en `--list-mods`).

1. Mods activos: los de `[mods] order = a, b, c` en `settings.ini` (solo esos, en ese orden); sin esa línea, todos los
   que tengan un `mod.json` válido, en orden alfabético.
   `[mods] disabled = x, y` apaga mods aunque estén en `order` (y los demás siguen encendidos, también los que instales después).
   La pestaña **Mods** del menú de opciones escribe ambas líneas (ver abajo).
2. **El último gana.** Para cada recurso el orden es: mods (del último al primero) → `data/` si lo editaste → disco.
   Un solo lugar del port decide ese orden (`assetResolve` y las capas de `pc/shim/mods.h`); en `audio`, `video`, `files` y `text` solo cuentan
   los mods (lo de `data/` es la exportación de los originales, no un reemplazo).
3. Las dependencias cargan **antes** que quien las pide (el resto conserva el orden del punto 1).
4. Se desactiva el mod, con una línea en el log, si:

| Línea del log | Causa |
|---|---|
| `mods: X disabled: needs port >=9, this is 0.5.0-beta` | su `port` es más nuevo que el port |
| `mods: X disabled: requires base>=1.2, found 1.1` | la dependencia es más vieja |
| `mods: X disabled: requires base, not installed or not active` | falta, o no está en `order` |
| `mods: X disabled: requires base, which is disabled` | la dependencia quedó desactivada |
| `mods: dependency cycle, disabled: a b` | se piden entre sí (o dependen de un ciclo) |

**Límites del motor.** Hasta 256 mods a la vez (`MODS_MAX`; los que sobran salen en el log como `rejected: more than 256 mods`),
64 claves por objeto y 64 números por arreglo en `mod.json` y en los archivos de configuración (`mods: ... rejected: mod.json:
too many keys` o `array too long`). Las tablas de datos no tienen tope de cantidad (el registro crece solo) y una lista puede
declarar hasta 65536 elementos; una lista de un parche que pasa del `listMax` de su campo se rechaza entera con una línea
`tables: ... rejected: more than N items`. Un archivo de partida guarda hasta 32 secciones; las partidas viejas (hasta 8) se leen
igual.

## 6. Tutoriales

Antes de todo: abre el juego **una vez** con tu disco. Eso genera `<pref>/data/` (texto, imágenes, config) a partir de
tu copia. Lo que sigue usa esas carpetas. Los comandos se ejecutan en la carpeta del repositorio.

### 6.1 Un mod de texto

Los textos están en `<pref>/data/text/<idioma>/<archivo>.json` (`eng fra ita ger spa es-419`):
`{"file": "...", "format": "text-v1", "strings": [{"index": 0, "text": "..."}, ...]}`.

1. Crea `mi-mod/mod.json` (§4) y copia el archivo a `mi-mod/text/<idioma>/<archivo>.json` (mismo nombre).
2. Edita solo los `"text"`. Debe traer **todas** las entradas con sus `index`, y solo caracteres que el juego sabe
   escribir; los códigos de control van como `{0202}` y se conservan tal cual.
3. Copia `mi-mod` a `<pref>/mods/` y abre el juego. Si algo no cuadra, el log dice archivo, entrada y posición.

```
cp -r mi-mod ~/.local/share/dw3port/dw3/mods/          # Linux
xcopy /E /I mi-mod "%APPDATA%\dw3port\dw3\mods\mi-mod"  :: Windows
```

### 6.2 Un mod de imagen: las imágenes sueltas (`images/`)

Casi todo el arte se edita por asset, en su carpeta: las pantallas de menú en `menus/` (§6.2g), las baldosas y los objetos de un escenario en `stages/` (§6.2d, §6.2h), las cartas en `cards/`
(§6.2b), los sprites del campo en `sprites/` (§6.3) y los compañeros en `partners/` (§6.2f). `images/` guarda lo que todavía no tiene piezas: `TLOGO*`, `TTCOMMPK`, `END__ALL`, `CNTSELWN`, `CARDDATA`,
`FIELDCOM`, los fondos de escenario (§6.2c) y, como excepción, las páginas de 8 bits `STWMP0` y `STWMP1` (sus piezas no tienen PNG, así que no hay otra vía para editarlas).

1. Busca la imagen en `<pref>/data/images/<carpeta>/<archivo>/<NN>_<MM>.png`. Junto a la mayoría de los PNG hay un `<NN>_<MM>.json` (`atlas-v1`) con las piezas de la imagen
   (`tools/image_atlas_preview.py` dibuja pantallas por pieza, objetos de escenario y sprites, no estas imágenes).
2. Cópiala a `mi-mod/images/<carpeta>/<archivo>/<NN>_<MM>.png` y edítala **conservando**: mismo ancho y alto, PNG
   **indexado** (con paleta, no entrelazado), a lo sumo 16 colores (256 en páginas de 8 bits), y el **orden** de la
   paleta (los índices del PNG reemplazan los del juego). Alfa 0 = transparente.
3. Un PNG que no cumpla se rechaza: `images: <ruta> rejected: <motivo>` y se usa el siguiente. Los mods se leen **una sola
   vez, al arrancar** (un índice en memoria de qué PNG tiene cada mod): un cambio se ve al **reiniciar** el juego. Para ver los
   cambios con el juego abierto, arranca con `DW3_MODS_HOTRELOAD=1` (solo para modders: revisa los archivos cada segundo y
   cuesta rendimiento con muchos mods; no mira `menus/`).

### 6.2b Un mod de imagen: los dibujos de las cartas

El juego de cartas guarda el dibujo de cada carta en `CARD/CARDPAK0-4.BIN` (64 cartas por archivo, 60 en el último: 316).
El port los exporta como `<pref>/data/cards/c<nnn>/face.png` (más un `card.json` informativo y `cards/manifest.json`), donde
`<nnn>` es el **número de la carta del juego** con tres cifras (`c001` a `c316`): el mismo `<id>` de `dw3:card/<id>`. El juego
lo lee así (`cardDrawerSetCard`: archivo `(k-1)/64`, imagen `(k-1)%64`), así que la carta `k` es la imagen `(k-1)%64` de
`CARDPAK<(k-1)/64>`. Cada PNG es de **32x32**, 8 bits, con **su propia paleta de 256 colores** (índice 0 transparente): más
libertad que en los menús, porque cada carta carga su paleta. Corre al arrancar (`settings.ini`, `[data]`, `export_cards`:
`all` si la línea no está, `off`, o números separados por comas: `export_cards = c012, 40`).

Para reemplazar uno, cópialo a `mi-mod/cards/c<nnn>/face.png` (o edita el de `data/`) con las reglas de §6.2 (mismo tamaño,
indexado, a lo sumo 256 colores, el orden de la paleta). El reemplazo se ve en el álbum, la tienda, el editor de mazos y
el combate. Los 12 bytes que preceden a cada dibujo (color, AP, HP, tipo...) no se exportan: son datos de juego, no arte.
Una carpeta `c<nnn>` (una `c` y tres cifras) **siempre es una carta original**: una carta nueva no puede llamarse así (su
llave no lee `cards/c<nnn>/face.png` como arte propio).
`CARD/CARDDATA.BIN` (ventanas y sprites del combate de cartas, no el arte de una carta) sale como `images/card/carddata/00_00.png` y `01_00.png`, con su mapa `atlas-v1`, y se reemplaza como el resto de `images/` (§6.2). `CARDPACK.BIN` es casi una copia de las cinco hojas
juntas (difiere en 6 de las 316 cartas) que el juego no lee por su número; no se exporta.

### 6.2c Un mod de imagen: el arte de los escenarios del campo

Los **objetos de mapa** de un escenario (`FIELD/STAGE/S<nnn>MASK.BIN`, que no es una máscara de profundidad) se editan por pieza en `stages/s<nnn>/objects/` (§6.2h).

Lo único de los escenarios que está en `images/` son los **fondos** `S415BG01/02` y `S815BG01/02` (no son objetos y no tienen hoja de piezas): salen como
`<pref>/data/images/field/stage/s415bg01/<ii>.png` (128x256, 8 bits, sin mapa) y se reemplazan con las reglas de §6.2 (mismo tamaño, indexado, a lo sumo 256 colores).

### 6.2d Un mod de imagen: los mosaicos del mapa de un escenario

El mapa de cada escenario del campo está en `Z_STAGE/S<nnn>PACK.BIN` (237 archivos, 11 745 baldosas de 128 px de alto). El juego las
va leyendo del disco mientras el jugador camina. Como no pasan por el mismo camino que el resto del arte, tienen su propio
exportador, **bajo demanda** (son unos 12 000 archivos y 250 MB):

- **Menú:** *Opciones > System > Prepare modding files* (un solo botón para videos, audio, modelos y escenarios; ver §2b) exporta los 237
  (el valor de la fila muestra `Stages 3/237`). Corre en un hilo aparte; el juego sigue usable.
- **`settings.ini`:** `[data]` / `export_stages = all` o `export_stages = S233, S330` los exporta al arrancar, antes de abrir la
  ventana. Un escenario ya exportado con la misma versión del exportador se **salta** (el segundo arranque no relee el disco). El menú
  sí los rehace todos.

Cada escenario sale en `<pref>/data/stages/s<nnn>/tiles/`:

- `<FF>_<CC>.png`: una baldosa (fila `FF`, columna `CC`, dos dígitos; la baldosa `n` del escenario es `FF = n / ancho`, `CC = n % ancho`).
  Es un PNG indexado de 8 bits (256 colores con la CLUT propia de la baldosa; el color 0 es transparente), de 128 de alto y 128 a 256
  de ancho. Las baldosas vacías no tienen archivo.
- `tiles.json` (`stages-tiles-v1`): `width` y `height` del escenario en baldosas, `tileBytes` y, por baldosa, `frame`, `row`, `col`,
  `png`, `size` (bytes en el disco), `rle` (si está comprimida), `timOffset`, `width`/`height` de la imagen y `depths`: tres listas
  (una por profundidad) de los recortes `x, y, u, v, w, h` que el juego dibuja desde ese PNG. Es referencia: el port no lo lee.
- Lo que escribió el port va en el `manifest.json` único de `stages/` (junto con los datos y los objetos de escenario; versión `stage-tiles/s<nnn>` en `exporter`). Regla `.new` de siempre: un PNG que editaste se conserva y el nuevo queda al lado como
  `<FF>_<CC>.png.new`.

Se reemplazan con las reglas de §6.2 (mismo tamaño, PNG indexado, a lo sumo 256 colores; un PNG con menos colores solo cambia los
primeros y el resto de la paleta queda como en el disco) poniendo el archivo en `mods/<mod>/stages/s<nnn>/tiles/<FF>_<CC>.png`
(el último mod gana; mayúsculas o minúsculas); también vale editar el PNG de `data/stages/s<nnn>/tiles/` (si cambió respecto al manifiesto, que se lee una vez al arrancar). Un PNG
que no cumple se rechaza con una línea en el log (`images: <ruta> rejected: <motivo>`, una vez) y se prueba la fuente siguiente.
El cambio se ve al cargar la baldosa (al entrar al escenario o al caminar hasta ella); los archivos se leen al arrancar: un cambio se ve al reiniciar (o con `DW3_MODS_HOTRELOAD=1`, que vuelve a mirarlos cada segundo).
Sin PNG el juego es idéntico. Solo se cambia la imagen: dónde va cada recorte lo fija la baldosa del disco (para mover recortes haría
falta otro formato).

### 6.2e Los archivos sin formato abierto: copias crudas

Lo que ningún otro exportador cubre se copia byte a byte a `<pref>/data/raw/<ruta del disco sin AAA/, en minúsculas>` (por ejemplo
`raw/dat/field/stage/s233tmpk.bin`) para tener el original como punto de partida. Son unos 15 MB (989 archivos en el disco EU), así que no
lleva menú: corre al arrancar, antes de abrir la ventana, y un arranque que encuentra cada tipo ya escrito por la misma versión del
exportador no lee el disco.

- **`settings.ini`:** `[data]` / `export_raw` vale `all` si la línea no está, `off` para no escribir nada, o tipos separados por comas
  (`export_raw = stage-map, battle-effect`).
- `README.txt` (qué es la carpeta, en inglés), `raw.json` (`raw-v1`: por tipo `files`, `bytes`, `know`, `usedBy`, `replace` y `works`, y luego
  cada archivo con su `size` y `kind`) y `manifest.json` (la regla `.new` de siempre: un archivo que editaste se conserva y el nuevo queda al
  lado como `<archivo>.new`).

El juego no lee nada de esto desde `data/`. Para cambiar un archivo, cópialo a `mods/<mod>/files/<la misma ruta>` (§6.4: gana el último mod,
mayúsculas o minúsculas). Que el juego lo lea así depende del tipo (`works` en `raw.json`):

| Tipo | Archivos | ¿Se reemplaza desde `mods/<mod>/files/`? |
|---|---|---|
| `stage-map` | `S<nnn>TMPK.BIN`, 294: celdas de colisión, zonas de combate, puertas (entrada 0: hoja de los objetos del mapa) | **Sí, probado:** con un `S330TMPK.BIN` modificado, una corrida que entra al escenario 330 registra `read 437 DAT/FIELD/STAGE/S330TMPK.BIN ... src=raw` (sin el mod: `src=disc`) |
| `stage-demo`, `battle-effect`, `battle-common`, `sprite-sheet`, `card-opponents` | `SSUBDEMO`/`SDIGDEMO`, `FIGHT/EFFECT/*` (239), `F000COM1-4` y `PGFGTC00/W00`, las hojas `...CS.BIN` (15), `CARD_NPC.BIN` | Debería: el juego los lee por la caché de archivos igual que `TMPK`. No se corrió con un archivo reemplazado |
| `training-data`, `card-pack`, `text-unused`, `filler` | `TRAINING/TRANIN*` (12), `CARDPACK.BIN`, `COUNTRY/JPN` y `USA` (100), `NONE____.BIN` | Sin efecto: nada del juego los lee |
| `stage-overlay`, `mode-overlay` | `PRO/*.PRO` (318): código MIPS | Sin efecto: el port enlaza el código desde `src/`. La excepción es `SMDLDATA.PRO` (el archivo de luchadores): sí se lee, y una copia lo reemplaza entero (las adiciones de los mods de contenido a él dejan de servirse) |

Las películas y las baldosas del escenario (`Z_STAGE`) no usan esta vía (tienen sus carpetas, §8.1b y §6.2d). Aún no hay formatos de edición
para estos archivos: trátalos como cajas negras y reemplázalos por otro archivo con el mismo diseño.

### 6.2f Los compañeros originales: un paquete por compañero

Cada compañero original sale **una sola vez y con sus colores reales** en `<pref>/data/partners/<nombre>/` (`<nombre>` es el nombre inglés del compañero en
minúsculas), sin hojas de VRAM ni paletas por pantalla. Corre al arrancar, antes de abrir la ventana (unos 6000 archivos pequeños), y un arranque que
encuentra todo escrito por la misma versión no lee el disco. **`portrait.png`, `icon.png` y `field/` también se leen de vuelta** (ver abajo, «Editar un original» y §6.9b);
`training/` también se lee de vuelta (abajo, «El gimnasio de un compañero»); `menus/gym/effects/` solo se exporta.

- **`settings.ini`:** `[data]` / `export_partners` vale `all` si la línea no está, `off` para no escribir nada, o nombres separados por comas
  (`export_partners = agumon, veemon`).

```text
partners/<nombre>/
  partner.json        partners-partner-v1: nombre, identidad (dw3:partner/<n>), forma inicial (dw3:digimon/<n>), qué es cada archivo
  portrait.png        tira de 32 de alto y 32 x N de ancho (N = 3 o 4 celdas), con la paleta propia del compañero; índice 0 transparente
  icon.png            44x28, el ícono de la tarjeta de memoria
  field/              el sprite del campo (sprite-v1: <EEE>.png por cuadro y sprite.json)
  training/<entrenamiento>/   la escena del gimnasio: <NNN>.png por sprite (colores reales) y training.json
menus/gym/effects/<entrenamiento>/   los efectos que los 8 comparten (training.json igual; 10 de los 12 entrenamientos tienen)
```

`portrait.png`: `partner.json` dice las celdas, el orden de la animación (ida y vuelta, `0 1 2 3 2 1`), dónde cae cada celda dentro de su cuadro
(`offsets`) y de qué cuadros de las hojas salen. El juego dibuja el mismo retrato en 7 pantallas: aquí está una vez. `training/<entrenamiento>/`: un
entrenamiento tiene el nombre inglés que el gimnasio le muestra (`punch`, `jump-rope`, ... el *slug* del texto de tu disco; el archivo del disco, `TRANIN00`..., solo se nombra dentro de `training.json`);
los dos entrenamientos que comparten un archivo (el normal y su variante "Super") comparten carpeta, y `partner.json` dice qué filas del gimnasio la usan. Un sprite es varias piezas
compuestas en un PNG (una paleta de 16 colores por pieza; si el sprite mezcla paletas, cada una ocupa sus 16 índices); `training.json` trae, por sprite,
el `origin` del PNG, y por animación sus pasos (`frame`, `time` en ticks, `x`, `y`). Un sprite de un compañero puede usar píxeles del efecto compartido
(`effectParts`). La semitransparencia (`semiTransparent`) se anota y `blend` lista los rangos de índices que el juego dibuja mezclados (modo 0 promedio, 1 suma, 2 resta, 3 un cuarto sumado); el PNG los dibuja opacos. `paletteBase` (0 o 1) dice dónde empiezan sus paletas de 16 índices. Las piezas que se dibujan una sobre otra van en el orden
de la lista (las últimas encima). Todo es información: se regenera del disco, y un archivo que editaste se conserva (el nuevo queda como `<archivo>.new`).

**Editar un original.** Cambia `data/partners/<nombre>/portrait.png`, `icon.png` o un cuadro de `field/` (o ponlos en `mods/<mod>/partners/<nombre>/`: un mod gana a `data/`, el último
mod gana a los anteriores, **archivo por archivo**; lo que falte queda como estaba). Al arrancar, una sola vez, el puerto lee tu retrato y reescribe en las 7 pantallas que lo
dibujan (equipo inicial y confirmación, nombre, laboratorio, menú de campo, gimnasio, informe de combate y memory card) las celdas y **la misma paleta** en cada fila que usan:
no hay una paleta por pantalla que mantener. En `data/` solo cuenta un archivo que **cambiaste** (su hash ya no es el de `partners/manifest.json`; el archivo que dejó el exportador no).
Reglas del `portrait.png` de un original: 32 de alto y **exactamente las celdas que ya tiene** (3 o 4: cada una reemplaza el cuadro de esa posición de la animación), hasta 16 colores
(con más, el puerto junta los más parecidos a 16 y avisa en el log con la ruta y la cuenta; el índice 0 transparente se conserva); `icon.png`: 44x28, igual. Un archivo que no cumple
se rechaza con una línea `[partners] <ruta> rejected: <motivo>` y queda el anterior. Los cuadros de `field/` siguen las reglas de `sprite-v1` (§6.3).

**El gimnasio de un compañero.** El gimnasio muestra la escena de entrenamiento de cada compañero, también la de uno de un mod (índice >= 8, sin el límite de nueve de la PS1). Pon en
`mods/<mod>/partners/<llave>/training/<entrenamiento>/` (`<llave>` la de su carpeta; `<entrenamiento>` el nombre inglés que sale en `data/partners/<nombre>/training/`: `punch`, `defense`, `yoga`, `study`,
`jump-rope`, `heat-tolerance`, `water-tolerance`, `ice-tolerance`, `wind-tolerance`, `thunder-defense`, `missile-defense`, `haunting`) un `training.json` y un `<NNN>.png` por sprite: lo más cómodo es copiar la carpeta
de un original y editarla. Reglas:

- Cada entrenamiento va por separado: el que falta (o no se puede armar: línea `[partners] <ruta> rejected: <motivo>`) muestra el del `base` del compañero (el del disco, o el que hayas editado de ese original), y
  el log lo dice una vez (`partner 8 has no training defense of its own: it shows partner 3's`). Una carpeta cuenta entera, de la primera capa que la tiene (el último mod gana a los anteriores y a `data/`).
- `training.json` (`partners-training-v1`): `frames` (cada uno con `id`, `image` —un `.png` o `null` para un sprite vacío—, `origin` [x, y] del PNG respecto al origen del sprite, y opcionalmente `paletteBase` y
  `blend`) y `animations` (`id` 0..5, `steps` con `frame`, `time` en ticks, `x`, `y`; el último paso se mantiene). Son las 6 animaciones del conjunto, las que el juego pide por número (`docs/formats.md` 4.12); un `id` ausente deja ese número sin animación.
- Cada PNG es indexado (hasta 256 colores, 256x256 como mucho; el índice 0 o un color con alfa 0 es transparente). Si lo hiciste a mano sin `paletteBase`, el puerto reparte los colores visibles en paletas de 15 por
  orden de índice; un PNG copiado del exportador conserva sus paletas.
- Un conjunto cabe en **2 páginas de 256x256 con 16 paletas cada una** (lo que cabe un conjunto del disco; las piezas repetidas cuentan una vez y, si no caben enteras, se prueban teselas de 16 y de 8 píxeles).
  Si no cabe: `rejected` en el log y el compañero cae al `base`. Los efectos compartidos (`menus/gym/effects/`) siguen siendo los del juego: el efecto que dibuja el juego aparte no cambia.
- Un **original** editado funciona igual: cambia un archivo de `data/partners/<nombre>/training/<entrenamiento>/` (cuenta una carpeta con algún archivo cuyo hash ya no es el de `partners/manifest.json`)
  o ponla en `mods/<mod>/partners/<nombre>/training/...`. El PNG es la composición plana de las piezas: una pieza semitransparente que cubría a otra se mezcla solo con el fondo.
- Se arma una sola vez al arrancar, sin costo por cuadro (unos 0,1 s en total para las 96 carpetas del juego si hay que comprobar sus hashes).

### 6.2g Las pantallas por pieza

Cada pantalla del juego sale **por piezas y con sus colores reales** en `<pref>/data/menus/<pantalla>/` (`<pantalla>` es el archivo del disco sin sufijo, en minúsculas: `stmcrd`, `stdglb`...), junto a `menus/gym/effects/` y con un solo `menus/manifest.json`.
Corre al arrancar (unos 960 archivos, 0,7 MB; un arranque que lo encuentra todo escrito por la misma versión no lee el disco).

- **`settings.ini`:** `[data]` / `export_menus` vale `all` si la línea no está, `off` para no escribir nada, o nombres de pantalla separados por comas (`export_menus = stmcrd, stdglb`).
- Cada `<pieza>.png` es un PNG indexado de 16 colores (256 en las de 8 bits) con la paleta que el juego usa para esa pieza; `menu.json` dice dónde va (página, rectángulo, fila de paleta) y qué cuadros la dibujan, con su desplazamiento y su mezcla.
  El formato completo está en `docs/formats.md` §7.5b.
- **Editar una pieza:** cambia el PNG en `data/menus/<pantalla>/` o copia la pantalla (o solo la pieza) a `mods/<mod>/menus/<pantalla>/` y edítala ahí. El juego pone cada pieza **donde el disco la tiene**: en su rectángulo de la página y su paleta en su fila de CLUT
  (`menu.json` lo dice, pero el port toma esas posiciones del disco, no del `menu.json` del mod). Por eso:
  - **Conserva el tamaño** de la pieza; un PNG de otro tamaño se rechaza con una línea de log (`menus: <ruta> rejected: size WxH, the piece is wxh`) y vale el de la capa siguiente o el del disco.
  - Usa los índices del PNG tal cual si tiene hasta 16 colores (256 en las de 8 bits). Con más colores se reducen a 16 con una línea de log. El índice 0 y todo color que el disco deje en `0x0000` es transparente; el bit de semitransparencia (STP) de cada color es el del disco.
  - Las piezas que dibujan el mismo rectángulo con otra fila de paleta comparten los índices, y las que comparten posición de CLUT comparten la paleta. Si dos piezas editadas no coinciden, gana la capa más tardía (el último mod) y el log lo dice; dentro de una capa, la primera de `menu.json`.
  - Solo cuenta lo que cambia: una pieza igual a la del disco no escribe nada, así que copiar toda la exportación a un mod no tiene efecto, y los píxeles y la paleta de una pieza se juzgan por separado.
  - Una pieza con `file: null` (las de `stwmp0` y `stwmp1`) no tiene PNG y queda como en el disco (`images/` edita esas dos pantallas, §6.2).
- **Capas:** los mods del último al primero y luego `data/` (§5); un archivo de `data/` solo cuenta si lo cambiaste (su hash ya no es el de `menus/manifest.json`). Se lee una sola vez, cuando el juego arma por primera vez los archivos de esa pantalla, y se guarda en memoria;
  no hay recarga en caliente. Medido en el disco EU: unos 20 ms en total para las 15 pantallas si hay que comprobar los hashes de `data/` (6 ms sin el empaquetador) y unos 40 ms con una copia completa de la exportación en un mod; en el juego cada pantalla paga solo la suya.
- **Solo por pieza:** las pantallas con hoja de piezas se editan únicamente aquí; `stwmp0` y `stwmp1` son la excepción y siguen en `images/` (§6.2) porque sus piezas no tienen PNG.
- **Validar:** `boot <disco> --validate-mod <mod>` arma el plan de cada pantalla: un PNG de otro tamaño sale como error (`menus/<pantalla>/<pieza>.png rejected: size WxH, the piece is wxh`), un archivo que ninguna pieza lleva como nombre sale como aviso (`... ignored: no piece of <pantalla> is called that`) y una pieza que traen dos mods también (`... ignored: the piece is also in mods/<otro>, which loads later and wins`).
- **Ejemplo: marcos en dorado.** Mira `data/menus/<pantalla>/menu.json` y los PNG para ubicar las piezas del marco (por ejemplo en `stshop`, la tienda de objetos); copia esas piezas a `mods/mi-mod/menus/stshop/` y cambia **solo la paleta** (los azules a dorado) con un editor que conserve el orden y la cantidad de colores; los píxeles quedan iguales.
  Las piezas con el mismo `clut` en `menu.json` comparten paleta: dales la misma paleta a todas, o el log avisa del desacuerdo y gana una. Al arrancar, el log dice `menus: override stshop/<pieza>.png (colours) from mods/mi-mod`.
- **Todavía no:** mover una pieza a otro lugar o cambiar su tamaño (no hay layout libre; el juego pide cada textura en una posición fija de VRAM). Las piezas de `stwmp0`/`stwmp1` no tienen PNG: se editan por `images/` (§6.2).

### 6.2h Los objetos de un escenario por pieza

Los objetos de mapa de cada escenario salen **por piezas y con sus colores reales** en `<pref>/data/stages/s<nnn>/objects/` (un PNG por pieza y un `objects.json`), con el mismo modelo que las pantallas de §6.2g y el manifiesto `stages/manifest.json`. Corre al arrancar, después de escribir `stages/s<nnn>/stage.json` (de ahí toma qué entrada del `TMPK` es la hoja; no siempre es la 0). En el disco EU: 8632 PNG y 294 `objects.json` (unos 10 MB).

- **`settings.ini`:** `[data]` / `export_stage_objects` vale `all` si la línea no está, `off` para no escribir nada, o escenarios separados por comas (`export_stage_objects = s202, s920`).
- Una pieza es un rectángulo con **una** paleta; el nombre es `f<cuadro>p<ranura>.png`; `objects.json` dice dónde va (página, rectángulo, fila de paleta) y qué cuadros la dibujan. Una pieza de 8 bits tiene `clut.row` de 256 en adelante: su paleta es una fila de la CLUT de la imagen de 8 bits del `MASK`, no una página. Formato: `docs/formats.md` §7.5c.
- Un escenario sin piezas (hoy 11, de hoja sin cuadros) trae `objects.json` con `unexported` y el motivo. Las filas de paleta que un objeto cicla en ejecución no están en el disco como dato: solo salen las que usan los cuadros de la hoja.
- **Se leen de vuelta.** Edita una pieza (píxeles y colores; mismo tamaño) y colócala en `mods/<mod>/stages/s<nnn>/objects/<pieza>.png` (o edita la de `data/`): al cargar ese escenario el juego la escribe en su lugar original. El rectángulo, el modo y la fila de paleta salen de la hoja del disco, no del `objects.json` (que es informativo). Solo se escribe lo que difiere del disco, así que copiar toda la carpeta no cambia nada; el bit STP de cada color y el color `0x0000` (transparente) los pone el disco. Un PNG de otro tamaño se rechaza con un aviso en el log. Dos piezas que comparten rectángulo o paleta y se editan distinto: gana la capa más tardía (el último mod), con una línea de log que nombra el escenario. Un escenario sin piezas editadas no cuesta nada al cargar. Esta es la **única** vía para los objetos de un escenario.
- **Validar:** `boot <disco> --validate-mod <mod>` arma el plan de cada escenario con piezas editadas: un PNG de otro tamaño sale como error (`stages/s<nnn>/objects/<pieza>.png rejected: size WxH, the piece is wxh`), un archivo que ninguna pieza lleva como nombre (`... ignored: no piece of s<nnn> is called that`) y una pieza que traen dos mods salen como aviso, y también una carpeta de `stages/` que no es `s<nnn>`, de un escenario que el disco no tiene, o con algo distinto de `objects/`, `tiles/` y `stage.json` (`... ignored: ...`). Las baldosas se validan aparte (formato, tamaño frente al original exportado, `images: <ruta> rejected: <motivo>`; un archivo que no es `<FF>_<CC>.png` sale como `ignored`) y el parche de escenario también (`[stages] <ruta> rejected: <motivo>`); un archivo suelto en `stages/` sale como aviso: nadie lo lee.
- **Sin piezas, sin objetos.** Los 11 escenarios sin piezas (`S376`, `S875`, `S880`, `S885`, `S890`, `S895`, `S970` a `S974`) no dibujan ningún objeto de mapa: su tabla `stageObjects` solo trae el terminador (`src/field/stages/`) y su hoja no tiene cuadros, así que no pierden ninguna edición.

### 6.3 Recolorear un sprite del campo

Cada archivo `FIELD/SPRT/<NOMBRE>.BIN` (menos los 8 seguidores de compañeros, que van en `partners/<nombre>/field/`) se extrae a `data/sprites/<nombre>/` (el nombre del archivo en minúsculas): un PNG de 4 bits por cuadro (`<EEE>.png`, la entrada del cuadro en el archivo) con su `sprite.json` (`sprites-sprite-v1`:
cuadros, animaciones, conjuntos y direcciones) y `sprites/manifest.json`. Para reemplazar un cuadro pon el PNG en `mods/<mod>/sprites/<nombre>/<EEE>.png`; una carpeta con el nombre de una original solo trae sus cuadros (su `sprite.json` no se lee), un nombre distinto es un sprite nuevo del mod (`docs/stages.md` §11). `--validate-mod` avisa de lo que no se lee. El jugador en el Parque Central es `P002PLAY` (y `P001PLAY`). Cada pose de `P002PLAY` vive en su propio archivo:
`P002PLYD` (caer, aterrizar), `P002PLYF` (minijuego de la barra), `P002PLYK` (buscar) y `P002PLYL` (escaleras, subir y
bajar muros); un mod que recolorea al jugador tiene que cubrirlos todos (`FIELDSTG_animateActor`, `actor_control.c`).

```
python3 tools/image_mod_palette.py --mod ~/.local/share/dw3port/dw3/mods/mi-recolor --sprite p001play --sprite p002play --sprite p002plyd --sprite p002plyf --sprite p002plyk --sprite p002plyl --to 0
```

El script cambia solo la paleta de cada cuadro (azules a rojo; `--to 120` verde, `--to 45` dorado) y escribe cada PNG en la misma ruta dentro del mod (`--mod` elige la carpeta; por omisión `<pref>/mods/player-recolor`). En Windows usa
`%APPDATA%\dw3port\dw3\` solo, o `--data` y `--mod`. Sirve igual con una pieza (`menus/stshop/f000p00.png`), una carpeta (`cards/c001`) o un retrato (`partners/<nombre>/portrait.png`); `--list` muestra qué hay. Reglas: las del arte de menús, con **16 colores**.
`python3 tools/image_atlas_preview.py <data>/sprites/p002play --set 4 --dir 6 --gif` anima una dirección.

### 6.4 Reemplazar un archivo crudo del disco

Sirve para todo lo que aún no tiene formato abierto (modelos, fondos, escenarios, sonido). **Reemplaza el archivo
entero**: tú te encargas de que el contenido sea válido para el juego.

1. Encuentra el archivo: `python3 tools/file_names.py "<disco>.bin" > nombres.tsv` lista número, primer sector, estado y
   ruta (`AAA/DAT/Z_STAGE/S330PACK.BIN`). La ruta sin `AAA/` es la de `files/`.
2. Obtén sus bytes originales: `python3 tools/extract_disc.py "<disco>.bin" salida` los escribe todos conservando rutas
   (en `salida/AAA/DAT/...`). Trabaja en una carpeta tuya, fuera del repositorio.
3. Modifica el archivo y colócalo en `mi-mod/files/dat/z_stage/s330pack.bin`; copia el mod a `<pref>/mods/`.
4. El log dice `[files] override DAT/Z_STAGE/S330PACK.BIN from mods/mi-mod`. Puede tener otro tamaño que el original.
   Con el log en `trace` (canal `files`) cada lectura aparece como `[files] read <n> <ruta> off= sectors= src=raw`.

Reglas del archivo crudo:

- Un archivo que el texto o las imágenes ya manejan con formato abierto **conserva ese comportamiento**: un reemplazo
  crudo del mismo archivo se ignora (`files: <ruta> from mods/<mod> ignored: ...`, una vez). Usa `text/` o `images/`.
- El juego lee el archivo cuando la escena lo carga: se lee al arrancar: un cambio se nota al reiniciar (con `DW3_MODS_HOTRELOAD=1`, en la próxima carga, mirando como mucho una vez por segundo).
- Las películas (`STR`) y el audio XA no pasan por esta capa: se reemplazan con `video/<NOMBRE>.avi` (§8.1b).

### 6.5 Tablas de datos (recompensas, botín, precios y tiendas)

Las reglas del juego que viven en tablas se cambian con un **parche**: `mods/<mod>/tables/<tabla>.json`. Este apartado trata `battle_rewards`, `battle_loot`, `item_prices`, `shop_stock`, `weapon_data`, `armor_data`, `accessory_data` e `item_effects`; las demás (evoluciones, crecimiento, encuentros, cartas, gimnasio) están en §6.5b, todas con el mismo formato (ADR-007).

**Referencia.** El port escribe `<pref>/data/tables/<tabla>.json` con los valores **originales de tu juego**, las
llaves y, en `"notes"`, el rango de cada campo. Sirve para leer y copiar llaves. **Editarlo no cambia el juego**:
si lo editas, el port lo respeta y deja la versión nueva en `<tabla>.json.new`. Los números salen de tu disco y nunca
van a un mod que compartas.

**Formato del parche** (`"format": "<tabla>-v1"`):

```json
{
  "format": "battle_rewards-v1",
  "rows": {
    "12": { "money": 300 },
    "13": { "exp": "*2" },
    "*":  { "money": "*1.5" }
  }
}
```

- `rows` usa la **llave** de cada fila (en `battle_rewards`, el número de encuentro). `"*"` aplica a todas las filas.
- Un valor puede ser un número (reemplaza) o una operación sobre el valor actual: `"*k"` (multiplica), `"+k"`,
  `"-k"` (suma o resta). Una operación se redondea y se recorta al rango del campo; un número fuera de rango se
  rechaza.
- La fila con su llave gana sobre `"*"` **en los campos que nombra**; los demás campos siguen la regla `"*"`.
- Lo que el parche no nombra queda como estaba. Un campo desconocido, un tipo equivocado, una llave que no existe o
  un valor fuera de rango se rechaza **por campo o por fila** (`tables: <ruta> row "12" field "exp" rejected: ...`) y
  el resto se aplica. Un archivo mal formado o de otra versión se rechaza entero (`tables: <ruta> rejected: ...`).
- **Orden:** valores originales, luego cada mod en el orden de carga (§5). Dos mods que multiplican por 2 dan x4, y un
  reemplazo posterior gana sobre todo lo anterior. El log dice qué mods tocaron la tabla:
  `tables: battle_rewards patched by mods/a, mods/b (N rows)`. Siempre se parte de los valores originales, así que
  repetir la aplicación nunca acumula. Sin mods con `tables/`, ninguna tabla se toca.
- Partidas guardadas: cambiar filas existentes es compatible; la tarjeta no guarda las tablas. Un mod de este tipo
  es de tipo `assets` (no agrega filas ni identificadores). Agregar filas solo es posible en las tablas que lo dicen (id `<mod>:<nombre>`, mod de tipo `content`).

**`battle_rewards`.** Llave: el número de encuentro (la fila de la batalla), desde 0. Campos (rango):

| Campo | Qué es | Rango |
|---|---|---|
| `digimonExp` | EXP repartida entre los Digimon usados en la batalla | 0 a 9999999 |
| `exp` | EXP repartida entre los socios que pelearon (el reporte da 100 %, 60 % o 33 % según cuántos peleen) | 0 a 999999 |
| `money` | Dinero de la batalla (un quinto más con el objeto que lo aumenta; la cartera llega a 9999999) | 0 a 9999999 |

Los rangos son los topes que aplica el reporte del juego. El reporte lee la fila al abrirse, así que el cambio se nota
en la **siguiente batalla**, sin reiniciar. Se aplica al entrar al reporte, después de que el port restaura los datos
del overlay. Para ver la fila y la cartera de cada reporte, pon el log en `trace` con el canal `tables` (sección 6.6).

Ejemplo mínimo (EXP de socios y dinero x2 en todos los encuentros): `{"format": "battle_rewards-v1", "rows": {"*": {"exp": "*2", "money": "*2"}}}`.

**`battle_loot`.** El botín de cada enemigo, que vive en la tabla de batalla (archivo 463 del disco, un registro por
enemigo). Llave: el **id del enemigo** (`BattleTableEntry.id`, el primer campo del registro; no es una posición). Campos
(rango):

| Campo | Qué es | Rango |
|---|---|---|
| `item` | Objeto que el enemigo puede dejar (0: ninguno). Un enemigo cuyo objeto original es 0 no suelta nada: dale uno aquí si quieres que suelte. Un objeto que agrega un mod se escribe por nombre (`"<mod>:<nombre>"`, §6.7) | 0 a 402, o el nombre |
| `itemChance` | Probabilidad de dejarlo, en 1024avos menos uno: el juego suelta el objeto si `itemChance + 1` es mayor que un número al azar de 0 a 1023, así que 1023 siempre lo suelta | 0 a 1023 |

`itemChance` también escala qué tan fácil es **robar** ese objeto durante la batalla, así que subirlo ayuda a las dos
cosas. De los enemigos que pelean juntos con objeto, el juego elige uno al azar y solo ese tira el dado.

Esta tabla no es un arreglo del programa, sino un archivo del disco: el port aplica el parche **al buffer cada vez
que el juego lee el archivo** (al empezar una batalla), partiendo de los bytes que acaban de leerse, así que repetirlo
nunca acumula y el cambio se nota en la **siguiente batalla**. Si un mod trae además el archivo crudo
(`files/DAT/BATTLE/...`, §6.4), ese archivo reemplaza al del disco primero y el parche de tabla se aplica **encima**
(las filas se buscan por id en el archivo que de verdad se leyó, no por posición). Cuando hay varios parches, rigen
las reglas de siempre (orden de mods, `"*"`, el reemplazo posterior gana). Con el registro de mods (§6.6) cada
batalla dice qué enemigos cambiaron y si hubo objeto.

Un nombre que ningún mod instalado define (o un número de objeto nuevo que no existe) rechaza ese campo con una línea en el log y
el resto de la fila se aplica. `"*k"`, `"+k"` y `"-k"` sobre `item` que den un número sin objeto lo dejan como estaba. El objeto
soltado (o robado) llega a la bolsa por el mismo contador que los demás y su nombre sale en el mensaje de resultados.

Ejemplo mínimo (`itemChance` x2 en todos los enemigos): `{"format": "battle_loot-v1", "rows": {"*": {"itemChance": "*2"}}}`.

**`item_prices`.** Lo que cuestan y lo que pagan las tiendas. Llave: el **id del objeto** (el número que usa el juego, desde
1). Campos (rango):

| Campo | Qué es | Rango |
|---|---|---|
| `price` | Lo que la tienda cobra por uno (la lista de compra, el total y el dinero que se descuenta leen este valor; 0 es gratis) | 0 a 65535 |
| `sellPrice` | Lo que la tienda paga al comprarte uno. Es un valor propio, **no** la mitad de `price`. 0: el objeto no se puede vender | 0 a 65535 |

Es un arreglo del programa que nada reinicia: el port lo parchea **una vez al arrancar**, antes del primer modo, y todas las
tiendas lo leen al abrirse, así que cambiar un precio lo cambia en todas a la vez (el cambio aplica desde que arranca el
juego; el precio no se guarda en la partida). Un parche solo cambia estos dos campos del objeto: el resto del registro
(efectos, estadísticas, tipo) no se toca. Un objeto vendido a 0 deja de aparecer en la lista de venta, y un precio de compra
de 0 se trata en la tienda como 1 al calcular cuántos puedes comprar con tu dinero.

Ejemplo mínimo (`price` x0.5 en todos los objetos): `{"format": "item_prices-v1", "rows": {"*": {"price": "*0.5"}}}`.

**Equipo y efectos de objetos: `weapon_data`, `armor_data`, `accessory_data`, `item_effects`.** Lo que cada pieza de equipo suma
y lo que hace cada objeto usable. Son cuatro tablas del programa, todas con la misma llave: el **id del objeto** (o
`"<mod>:<nombre>"` para un objeto que agrega un mod: tiene su **propia copia** del registro, hecha a partir del de su objeto
base, así que cambiarla no toca al original; solo existe si el objeto es de ese tipo). Se parchean **una vez al arrancar**,
antes del primer modo: las pantallas de estado, las tiendas y cada batalla leen el registro cuando lo necesitan, así que el
cambio se ve en todas. Los precios no están aquí sino en `item_prices`. Un valor fuera del rango se rechaza (el campo, con una
línea en el registro; el resto del parche aplica).

`weapon_data` (123 filas, ids 92 a 214) y `armor_data` (77 filas, ids 215 a 291) comparten estos campos:

| Campo | Qué es | Rango |
|---|---|---|
| `charisma` | Lo que suma al carisma (la suma de lo que se lleva se detiene en 999) | -999 a 999 |
| `kind` | Dónde va: 1 mano derecha, 2 izquierda, 3 cualquiera, 4 cabeza, 5 cuerpo, 6 accesorio, 7 ambas manos, 8 accesorio de grupo | 1 a 8 |
| `group` | Número de grupo (solo lo lee el accesorio de grupo, `kind` 8) | 0 a 255 |
| `partners` | Quién puede llevarlo: un bit por compañero (`1 << número`), 255 es todos. Solo tiene bit para los 8 originales; los compañeros nuevos usan `equip` de su `partner.json` (§6.9b) | 0 a 255 |
| `stat1`, `stat2` | Qué sube cada bono: 0 ninguno, 1-6 fuerza, defensa, espíritu, sabiduría, velocidad y carisma, 7 los seis, 8-14 las siete resistencias | 0 a 14 |
| `amount1`, `amount2` | El bono, con signo (el valor se detiene en 999) | -999 a 999 |

Solo las armas tienen: `atk` (suma al ataque, 0 a 999), `accuracy` (suma a la puntería en batalla, 0 a 255; es un total de 8 bits,
mejor bajo), `hitEffect` (etiqueta del efecto del golpe; el juego **no** la lee, el efecto depende del id del objeto),
`statusChance` y `statusPower` (probabilidad y fuerza del estado, o el bono de crítico; solo las armas con efecto de golpe los
leen, porque el juego las elige por id), `family` (familia de sus golpes, 2 en adelante; por debajo no tiene), todos 0 a 255.
Solo las armaduras tienen: `def` (suma a la defensa, con signo, -999 a 999) y `evasion` (suma a la evasión en batalla, 0 a 255).

`accessory_data` (68 filas, ids 292 a 359): `charisma`, `kind` (6 accesorio, 8 de grupo; el 7 se rechaza), `group`, `partners`,
`stat` (como arriba, y además 17 a 21: resistencia en batalla a veneno, parálisis, confusión, sueño y KO; 0 a 21) y `amount`
(-999 a 999). Tres accesorios los lee el juego por su id (golpe triple, crítico, contraataque): su `amount` es el de ese efecto.

`item_effects` (83 filas, los objetos usables del 43 al 394, cartas de refuerzo incluidas; una fila por registro, así que dos
objetos que compartan registro salen como una sola fila):

| Campo | Qué es | Rango |
|---|---|---|
| `flags` | Dónde se puede usar, una suma: 1 desde el menú de estado, 2 en batalla, 4 marca un sobre de cartas (nada lo mira) | 0 a 7 |
| `kind` | Qué hace: 0 nada aquí (los objetos de batalla actúan por su id), 1 cura `amount` PV, 17 recarga `amount` PT, 2 a 16 suben una estadística en 1 hasta `amount` puntos | 0 a 17 |
| `amount` | Los PV curados, los PT, o el tope del aumento al azar; los objetos de batalla lo leen como su propio número (un medidor, un aumento en 128avos) | 0 a 20000 |

El tope de 20000 en `amount` es para que los PV curados quepan en las estadísticas de 16 bits del juego. Un aumento (`kind` 2 a
16) con `amount` 0 se rechaza **entero**: la fila vuelve a sus valores originales y sale `tables: item_effects row 44 rejected:
...` (el juego saca un número al azar hasta `amount`).

Las listas de ids por tipo de objeto (`WEAPON_IDS`, `ARMOR_IDS`, `ACCESSORY_IDS`, `USABLE_ITEM_IDS`, `KEY_ITEM_IDS`) y
`ITEM_TYPE_CATEGORIES` **no son tablas**: derivan del `kind` y el `type` de cada objeto (son los índices que usan los menús) y
los objetos nuevos ya se agregan solos a su lista; editarlas a mano rompería los menús.

Ejemplo (arma 92 con 500 de ataque y un bono de defensa):

```json
{
  "format": "weapon_data-v1",
  "rows": { "92": { "atk": 500, "stat1": 2, "amount1": 12 } }
}
```

**`shop_stock`.** Qué vende cada tienda de objetos. Llave: el **número de tienda**, desde 0 (el número con que se abre la
tienda; 31 tiendas). Un solo campo, que es una **lista**:

| Campo | Qué es | Valores |
|---|---|---|
| `items` | Los ids de objeto que la tienda vende, en el orden en que los lista | de 1 a 64 ids, cada uno de 1 a 402 |

Una lista se escribe de dos formas, y las dos se pueden combinar entre mods:

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

- **Un arreglo reemplaza** la lista entera. Un mod posterior que reemplaza gana sobre los anteriores.
- **`{"remove": [...], "add": [...]}` edita** la lista actual: primero quita y luego agrega al final los ids que no
  estén ya. Así dos mods que agregan un objeto a la misma tienda se suman sin pisarse, y un mod puede agregar sin
  copiar la lista original (que son datos de tu juego). `"*"` aplica a todas las tiendas.
- El 0 termina las listas del juego, así que no es un id válido. Una lista más larga que 64, vacía, con un id fuera de
  1 a 402 o que no es un arreglo (ni `add`/`remove`) se rechaza con una línea en el log
  (`tables: <ruta> row "3" field "items" rejected: ...`, o `tables: shop_stock row 3 field "items" add rejected: more
  than 64 items`) y esa tienda queda como estaba. El tope de 64 es del port (la tienda más grande del juego tiene 52); las
  listas del juego no se escriben nunca, el port apunta la tienda a su propia lista.

La tienda lee su lista al abrirse, así que el cambio se nota **la siguiente vez que entras a una tienda**, sin reiniciar. Se
aplica al entrar al modo de tienda, después de que el port restaura los datos del overlay. Los precios no están en la
tienda sino en `item_prices`: un objeto que agregas cuesta lo que dice esa tabla (y cambiarlo cambia todas las tiendas).
Solo cubre las tiendas de objetos; la tienda de cartas (`card_shop_stock`) y las posadas (`inns`) tienen tablas propias.

Ejemplo mínimo (la tienda 0 vende un objeto más): `{"format": "shop_stock-v1", "rows": {"0": {"items": {"add": [93]}}}}`.

### 6.5b Las demás tablas del juego (equipo, evoluciones, crecimiento, campo y cartas)

Siguen el mismo formato y las mismas reglas que §6.5: `mods/<mod>/tables/<nombre>.json` con `"format": "<nombre>-v1"` y un
objeto `"rows"` por llave; un número reemplaza, `"*k"` multiplica, `"+k"` y `"-k"` suman, y la llave `"*"` es toda fila. Cada tabla
tiene su copia de referencia en `<datos>/tables/<nombre>.json` (valores originales, rangos válidos y, donde corresponde, el nombre
de cada fila en `"labels"`); es solo para leer, editarla no cambia nada. Una fila o un campo fuera de rango se rechaza con una línea en
el log (`DW3_LOG=mods`). El detalle de cada tabla (qué lee el juego, de dónde salen los rangos) está en `docs/data-tables.md`.

| Tabla (`tables/<nombre>.json`) | Llave de la fila | Qué cambia | Cuándo se nota |
|---|---|---|---|
| `weapon_data` | id de objeto 92-214 (o `<mod>:<nombre>`) | estadísticas de las armas: carisma, ataque, precisión, dos bonos, efecto de golpe | al equipar |
| `armor_data` | id de objeto 215-291 | estadísticas de las armaduras | al equipar |
| `accessory_data` | id de objeto 292-359 | bono y tipo de los accesorios | al equipar |
| `item_effects` | id de objeto usable (43-394) | qué hace un objeto usable: tipo de efecto y cantidad | al usarlo |
| `evolutions` | `compañero * 100 + entrada` (352 filas) | forma que aprende cada compañero y sus requisitos | en el siguiente reporte de batalla |
| `blast_forms` | id del Digimon (52 formas) | en qué forma se convierte una ráfaga, por tramo de nivel | en la siguiente ráfaga |
| `level_exp` | primer nivel del tramo (1, 5, 20, 40) | EXP que pide cada tramo de niveles | en el siguiente reporte |
| `vital_cuts`, `vital_random` | primer nivel del tramo; ranura 0-8 | PV y PM que gana un compañero al subir de nivel | en el siguiente reporte |
| `stat_gains`, `resist_gains` | primer nivel del tramo (1-80); ranura 0-7 | ganancia de estadísticas y resistencias por nivel | en el siguiente reporte |
| `field_enemies` | número de fila (0-522) | Digimon, nivel, PV, PM y fuerza de los enemigos de los encuentros del campo | en la siguiente batalla del campo |
| `field_encounters` | índice del encuentro (0-334); `"<mod>:<nombre>"` agrega uno | qué filas de enemigo usa cada encuentro, la emboscada, el tipo y los 12 bloqueos; en una fila nueva, sus enemigos y recompensas | en la siguiente batalla del campo |
| `battle_rates` | índice de tasa (0-5) | cuántos pasos resta cada tasa a la cuenta hasta la próxima batalla al azar | desde el siguiente paso |
| `card_prices` | id de carta (44 cartas con precio) | lo que cobra la tienda de cartas por cada una | al abrir la tienda de cartas |
| `card_shop_stock` | número de tienda (49-67, 70-74) | las cartas que vende cada tienda (lista de ids 1-314 o `"<mod>:<nombre>"`) | al abrir la compra |
| `card_pack_slots` | `id del sobre * 10 + ranura` (35 sobres x 6 ranuras) | las 16 cartas de las que sale cada ranura al abrir un sobre (`card1`..`card16`) | al abrir un sobre |
| `card_effects` | id de carta (1-60) | condiciones, mensaje, tipo de objetivo y guion de pasos (`step1`..`step40`) de las primeras 60 cartas | al jugar la carta en un duelo |
| `prize_items` | número de premio (0-35) | el objeto que da un rival al perder | al cargar el rival |
| `default_deck` | posición en el mazo (0-39) | el mazo con el que juegas si la carpeta elegida está vacía (id de carta menos uno) | al elegir mazo en un duelo |
| `card_opponents` | argumento del modo del duelo menos uno (0-169) | el mazo de 40 cartas, las cartas de contraataque, el nivel y el premio de cada rival (`CARD_NPC.BIN`) | al empezar el siguiente duelo con ese rival |
| `gym_trainings` | `índice del gimnasio * 100 + ranura` (14 x 16 ranuras) | qué entrenamientos ofrece cada gimnasio: `id` (1-24), la estadística que sube (`stat`) y la segunda (`other`) | al abrir el gimnasio |
| `gym_stat_gains` | columna 0-11: `tipo * 6 + intento extra * 3 + intensidad` | cuánto sube un entrenamiento una estadística de batalla (`base` y `range`) | al ver el resultado del entrenamiento |
| `gym_stat_losses` | intensidad 0-2 | cuánto puede restar un entrenamiento de resistencia a una estadística de batalla | al ver el resultado |
| `gym_resist_gains` | `tabla * 10 + ranura` (3 tablas x 9) | cuánto sube un entrenamiento una resistencia (tabla baja, media o alta) | al ver el resultado |
| `gym_hp_mp_gains` | ranura 0-8: `intensidad * 3 + nivel` | cuánto sube un entrenamiento el PV o PM máximo | al ver el resultado |
| `gym_intensity_costs` | intensidad 0-2 | los puntos de entrenamiento (TP) que cuesta cada intensidad | al elegir la intensidad |
| `inns` | modo de juego de la posada, en decimal (21 posadas; 522 es la primera) | `price` por noche y por compañero, y `string`, el texto del nombre de la posada | al abrir la posada |

Un archivo por tabla, mínimo (cada uno es el archivo entero; `"*"` es toda fila):

```json
// tables/stat_gains.json: el doble de estadísticas por nivel
{"format": "stat_gains-v1", "rows": {"*": {"gain0": "*2", "gain1": "*2", "gain2": "*2", "gain3": "*2", "gain4": "*2", "gain5": "*2", "gain6": "*2", "gain7": "*2", "gain8": "*2"}}}
// tables/field_enemies.json: las filas 71 y 72 pasan a ser el enemigo 206 y el 32
{"format": "field_enemies-v1", "rows": {"71": {"fighter": 206}, "72": {"fighter": 32}}}
// tables/battle_rates.json: las batallas al azar vienen el doble de seguido
{"format": "battle_rates-v1", "rows": {"1": {"rate": "*2"}, "2": {"rate": "*2"}, "3": {"rate": "*2"}, "4": {"rate": "*2"}, "5": {"rate": "*2"}}}
// tables/inns.json: toda posada a 1 BIT
{"format": "inns-v1", "rows": {"*": {"price": 1}}}
// tables/gym_intensity_costs.json: las tres intensidades del gimnasio a la mitad
{"format": "gym_intensity_costs-v1", "rows": {"*": {"cost": "*0.5"}}}
// tables/weapon_data.json: todas las armas con el doble de ataque
{"format": "weapon_data-v1", "rows": {"*": {"atk": "*2"}}}
// tables/card_opponents.json: todo rival abre con diez copias de la carta 120, nivel 99
{"format": "card_opponents-v1", "rows": {"*": {"card1": 120, "card2": 120, "card3": 120, "card4": 120, "card5": 120, "card6": 120, "card7": 120, "card8": 120, "card9": 120, "card10": 120, "level": 99}}}
```

Notas:

- Las tablas del campo se aplican cada vez que se entra al campo. `field_encounters` nombra a los enemigos por número de fila de
  `field_enemies`: las filas se comparten, así que cambiar una fila cambia todos los encuentros que la usan. Ni la fila 0 de
  `field_enemies` ni el encuentro 0 pueden cambiar, y un encuentro no puede quedar con el primer enemigo vacío.
- **Encuentros nuevos.** Una fila de `tables/field_encounters.json` cuya llave es un nombre `"<mod>:<nombre>"` (la carpeta del mod propio, dos puntos y
  una llave de `a-z 0-9 - _`) **agrega** un encuentro en vez de cambiar uno; los originales nunca se tocan y quitar el mod los deja como estaban. Lleva sus propios
  enemigos (no comparte filas de `field_enemies`) y sus recompensas:

  ```json
  {"format": "field_encounters-v1", "rows": {
    "mimod:prado": {
      "enemies": [{"enemy": 206, "level": 10, "hp": 300, "mp": 40, "strength": 30},
                  {"enemy": "mimod:limo", "level": 9, "hp": 250, "mp": 0, "strength": 25}],
      "ambushChance": 0, "kind": 1, "runAway": 0,
      "rewards": {"money": 120, "exp": 45, "digimonExp": 30}}}}
  ```

  `enemies` son de 1 a 3 enemigos (el primero no puede ser 0); `enemy` es un id, `"dw3:digimon/<id>"` o `"<mod>:<clave>"` (un enemigo nuevo; una forma de socio se rechaza) y
  los cuatro valores son obligatorios (`level` 1-99, `hp` 1-32767, `mp` 0-32767, `strength` 1-32767). Opcionales: `ambushChance` 0-255 (0), `kind` 0-255 (1) y los 12 bloqueos
  de arriba (`poison` ... `runAway`, 0 o 1, por defecto 0). `rewards` (`money` 0-9999999, `exp` 0-999999, `digimonExp` 0-9999999; por defecto 0) es la fila que el reporte lee para
  esa batalla. El botín sale del enemigo (`battle_loot`), no del encuentro. Los números se dan después de los 335 del disco (335, 336...) en el orden de los mods y del archivo; en
  `battles` de un escenario y en la acción `start-battle` el encuentro se escribe por nombre (`"encounter": "mimod:prado"`) o por ese número. Una fila inválida se registra
  (`tables: <archivo> row "<nombre>" rejected: <motivo>`) y no existe: el escenario que la nombra se rechaza con "there is no field encounter named". Hasta 1024 filas nuevas entre todos los mods.
  La condición 12 del enemigo (`BATTLE_SETUP.battle == arg`) compara con ese número, que depende del orden de los mods: no la uses con encuentros nuevos.
- `field_enemies` acepta en `fighter` el id de un enemigo, `"dw3:digimon/<id>"` o el nombre de un enemigo de un mod de contenido
  (`"<mod>:<clave>"`); una forma de compañero se rechaza.
- Las tablas de cartas se aplican cada vez que se entra a la tienda de cartas o al duelo. Cada referencia trae en `"labels"` el nombre inglés
  de la carta (o del objeto). `card_prices` solo cubre las 44 cartas con precio del disco (la tienda cobra 1 por una carta sin fila) y su llave es la
  carta: no se puede cambiar a qué carta pertenece una fila. El precio de una carta de un mod no es una fila: es el campo `price` de su `card.json` (§6.10).
  Donde una tabla de cartas pide una carta (`card_shop_stock.cards`, `card_pack_slots.card1`..`card16`, `default_deck.card`, `card_opponents.card1`..`card40` y `counter1`..`counter20`)
  acepta un id original 1-314 o `"<mod>:<nombre>"` (una carta que agrega un mod de contenido, §6.10); un nombre que ningún mod instalado define se rechaza con una línea de log
  (`no installed mod defines "..."`) y el campo queda como estaba. Los ids 315 y 316 no son cartas y se rechazan. `card_shop_stock.cards` es una lista (reemplazo `[1, 2]` o `{"add": [...], "remove": [...]}`)
  de hasta 29 cartas, a seis por página. Cada ranura de un sobre sale de 16 cartas al azar: repetir una carta la hace más probable.
- `card_effects`: `step1`..`step40` son números de paso del duelo (1-174) y un 0 termina el guion, así que debe haber un 0 entre los 40 (si no, la
  fila entera se rechaza). Cambiar un guion es avanzado: algunos pasos esperan el objetivo que da `target` y otros saltan pasos siguientes; el
  puerto solo comprueba rangos. `default_deck.card` es el id de carta **menos uno**, como lo guarda el juego.
- `card_opponents` viene del archivo `CARD_NPC.BIN` del disco, no de la memoria: el parche se aplica a los bytes cada vez que el juego lee el archivo (al empezar un
  duelo), así que un archivo reemplazado por un mod también se parcha encima. La llave es el número de fila (0-169): el duelo lee la fila `argumento - 1`, y las acciones del campo
  (`CARD_BATTLE`) fijan el argumento, así que un mismo NPC puede usar más de una fila. Cada fila tiene 40 lugares del mazo del rival, con `cardN` (id 1-314), `flaggedN` (0 o 1: el bit 15 de la
  carta, que el juego lee al elegir la respuesta del rival), `groupN` (1-7: desde qué ronda puede robarla, el 7 es la reserva que se guarda para el final) y `kindN` (0-7: cómo la juega la
  computadora, ordena su mano por tipo y prioridad), más `counter1`..`counter20` (ids 1-314 con los que responde; el primer 0 termina la lista y con un 0 al principio usa
  sus respuestas de siempre), `level` (1-99, solo se muestra) y `prize` (0-35: el número de `prize_items`). Se rechaza la fila entera si la lista de contraataque no tiene un 0 entre
  las 13 primeras ranuras o si todo el mazo es del grupo 7 (el juego escribiría fuera de sus arreglos). La referencia no trae `"labels"`: el archivo no tiene nombres de rivales. Un
  `--validate-mods` marca como error los campos fuera de rango; el rechazo de una fila entera sale como nota del registro (como en `card_effects`).
- Las tablas del gimnasio se aplican cada vez que se entra al gimnasio, y `inns` una sola vez al arrancar el puerto. Las referencias de `gym_trainings` e `inns` traen el nombre
  inglés del entrenamiento y de la posada en `"labels"`. En `gym_trainings` la llave sale del índice del gimnasio (el argumento del modo; el disco llena las filas 0-13) y la ranura (0-15); un
  `id` va siempre al mismo sitio del menú, así que dos entradas del mismo gimnasio con ids del mismo sitio dejan solo la última. Una fila con `id` y sin `stat` (o con `stat` 6 o 7),
  o con `stat` 8-14 y un `other` que no sea 0, 1-5, 15 o 16, se rechaza entera. Las ganancias son `base` más un número al azar de 0 a `range - 1` (con `range` 0, solo `base`) y la
  fila se rechaza entera si `base + range - 1` pasa lo que cabe en la estadística (31768 en estadísticas y resistencias, 22768 en PV/PM máximos, 32767 en pérdidas). En
  `gym_resist_gains` hay varias combinaciones de crecimiento que comparten una tabla: cambiar una fila cambia a todas. Qué tabla usa cada crecimiento no es una tabla todavía.
  En `inns`, la llave es el modo de la posada y no se puede cambiar; `string` es una entrada del archivo de textos de posada (0-21), donde 1-15 y 21 son nombres y el resto los
  textos del menú.
- Las batallas de cada escenario (`stageBattles`) no son una tabla sino una sección del parche de escenario: `stages/s<nnn>/stage.json` con `battles` (§6.11, `docs/stages.md` §10).
- Los emplazamientos de `content/encounters.json` (§6.9) se aplican después de estas tablas.

### 6.6 Log

El port tiene **un solo log** con tres niveles. Cada línea sale como `[canal] mensaje` en el log de la sesión
(`<pref>/logs/dw3-AAAAMMDD-HHMMSS.log`, ver sección 2: Linux `~/.local/share/dw3port/dw3/logs/`, Windows
`%APPDATA%\dw3port\dw3\logs\`).

| Nivel | Qué escribe |
|---|---|
| `off` (por defecto) | Solo los resúmenes del arranque (`[mods]`, `[config]`, `[files] override`, `[images] override`, `[tables] ... patched`) y los rechazos con su motivo. |
| `mods` | Además, una línea cada vez que un mod **cambia un valor que de verdad se usa** (abajo). |
| `trace` | Además, el detalle técnico de cada canal: cada lectura de archivo, cada imagen cargada, la fila y la cartera de cada reporte, los tiempos del escenario. |

Se elige con uno de estos (el entorno gana al archivo):

- La fila **Log** (Off/Mods/Trace) de la pestaña System del menú de opciones.
- `[debug]` `log = off|mods|trace` en `settings.ini`; `channels = tables, files` (opcional, vacío = todos) deja pasar solo esos
  canales. El port escribe las dos claves y las conserva al guardar. La clave vieja `mod_log = on` se lee como `mods` y se
  reescribe como `log = mods`.
- Las variables de entorno `DW3_LOG=off|mods|trace` y `DW3_LOG_CHANNELS=a,b`.

Canales: `tables` (tablas de datos), `files` (archivos), `images` (imágenes), `stage` (tiempos de escenario), `config` y `mods`
(solo resúmenes). Solo en `trace`, el detalle por cuadro y por evento del motor: `gfx` (capas y una línea por capa dibujada en
cada cuadro), `pad` (el botón leído y el estado del menú, cada cuadro), `field` (la posición del jugador, cada cuadro), `tasks`
(altas, bajas y búsquedas de actores), `overlay` (carga de overlays de escenario), `text` (qué fuente sirvió cada archivo de texto,
totales del reflujo, íconos de botones), `memcard` (comandos de la tarjeta) y `battle` (eventos y tiradas de la batalla). A
cualquier nivel, y sin tocar nada, una sesión normal solo escribe los resúmenes del arranque y los problemas (en `mods`, también
las líneas de los mods): nada por cuadro. Los canales filtran las líneas de `mods` y `trace`, no los resúmenes ni los rechazos. Ejemplo de
`settings.ini` para ver solo lo que tu mod de tablas cambia, con el detalle de los reportes:

```ini
[debug]
log = trace
channels = tables
```

Atajos (los de siempre, siguen funcionando): `DW3_TABLE_TRACE=1`, `DW3_FS_TRACE=1`, `DW3_IMG_TRACE=1`, `DW3_STAGE_TIMING=1`,
`DW3_MEMCARD_TRACE=1`, `DW3_TEXT_TRACE=1` y `DW3_BATTLE_EVENT_LOG=1` ponen `trace` en su canal (`tables`, `files`, `images`,
`stage`, `memcard`, `text`, `battle`) y nada más; `DW3_MOD_LOG=1` equivale a `log = mods`.

Líneas de nivel `mods` hoy:

```
[tables] battle_rewards encounter 7: exp 20 (orig 10), money 100 (orig 50) by mods/double-rewards
```

Una línea por reporte de batalla cuyo encuentro tiene valores cambiados: solo los campos que el mod cambió, con el
valor original entre paréntesis, y los mods que tocaron la tabla. Si la fila queda igual al original no sale nada. Para
el botín (`battle_loot`), una línea por cada enemigo de la batalla cuya fila cambió y una última con el resultado:

```
[tables] battle_loot enemy 15: item 3 (orig 3), itemChance 400 (orig 200) by mods/generous-loot
[tables] battle_loot drop: item 3
```

(`drop: none` si el dado no soltó nada.) Para los precios (`item_prices`), al abrir una tienda sale **una línea por
objeto** que la tienda vende (`price`) o que tu bolsa podría vender (`sellPrice`) y cuyo precio cambió, una vez por visita:

```
[tables] item_prices item 12: price 100 (orig 200) by mods/cheap-shops
```

Y si un mod cambió la lista de la tienda que abres (`shop_stock`), una línea con la cantidad de objetos:

```
[tables] shop_stock shop 0: 6 items (orig 5) by mods/shop-extra-item
```

Para el equipo y los efectos (`weapon_data`, `armor_data`, `accessory_data`, `item_effects`), al arrancar sale una línea por fila que
un mod cambió, con los valores que quedaron en la memoria del juego y el original de cada campo:

```
[tables] weapon_data item 92: stat1 3 (orig 0), amount1 -5 (orig 0), atk 500 (orig 14) by mods/eq
```

Las sustituciones de archivos y de imágenes ya dejan su propia línea (`[files] override ...`, `[images] override ...`) la
primera vez que se usan; el nivel `mods` no las repite. Los valores del log salen de tu disco: no los publiques con tu mod
(ADR-001).

### 6.7 Contenido nuevo: objetos

Un mod con `"kind": "content"` en su `mod.json` puede declarar **objetos nuevos** en `content/items.json` (formato
`items-v1`). Es contenido propio del mod: no toca los objetos originales.

```json
{
  "format": "items-v1",
  "new": {
    "test-gadget": {
      "base": "dw3:item/43",
      "price": 40,
      "sellPrice": 20,
      "effect": { "amount": 300 },
      "name": { "eng": "Test Gadget", "spa": "Artilugio de prueba", "es-419": "Cacharro de prueba" },
      "description": { "eng": "A test item. Heals a little HP.", "spa": "Objeto de prueba. Cura un poco de PS." }
    }
  }
}
```

- **Nombre:** `<carpeta del mod>:<llave>` (aquí `mimod:test-gadget`). La llave usa minúsculas, dígitos, `-` y `_`.
- **`base`** (obligatorio): un objeto original (1..402), por su nombre `"dw3:item/43"` o por su número (`43`). El objeto nuevo
  copia su tipo, su lista de menú y su efecto o estadísticas de equipo; los campos que declares lo reemplazan.
- **Campos:** `price`, `sellPrice` (0..65535) y `effect` `{ "amount", "kind", "flags" }` (solo si la base es un
  consumible; el efecto es una copia propia, la base no cambia). Un campo desconocido o fuera de rango se rechaza con una
  línea en el log (`[items] ...`) y el resto del objeto se aplica; un objeto sin `name.eng` o con `base` inválida se rechaza entero.
- **Textos:** `name` (obligatorio en `eng`) y `description`, por idioma (`eng`, `fra`, `ita`, `ger`, `spa`, `es-419`). Falta un
  idioma: se usa el inglés (`es-419` cae primero a `spa`). La caja de la descripción es de **una línea** (280 px) y no se puede
  partir: una descripción más larga se acorta en una palabra con `...` y el log dice cuál (`text: ITMINF item 403 (es-419)
  description is wider than the box`); escríbela corta para que se vea completa. Esto vale para las descripciones de los objetos
  nuevos; los textos originales no se tocan.
  El port agrega las cadenas a los archivos de nombres y descripciones de objetos que ve el juego, sin editar tus archivos de texto.
- **Números:** el port da números desde el 403 en orden alfabético del nombre completo; no son estables entre conjuntos de
  mods distintos, por eso **nada los guarda ni debes escribirlos**: las tiendas usan el nombre y las partidas guardan el nombre.
- **Nombres de los objetos originales:** cada objeto original tiene el nombre `dw3:item/<id>` (por ejemplo `dw3:item/43`), y
  donde un mod daba el número de un objeto original ahora puede escribir ese nombre: `base`, la lista de `shop_stock`, `item` de
  `battle_loot` y la llave de una fila de `item_prices` (`"dw3:item/43": { "price": 13 }`; el número sigue valiendo). Un nombre
  `dw3:` fuera de 1..402, o de otro tipo (`dw3:tech/43` en un objeto), se rechaza como cualquier nombre desconocido. Para ver
  cuál es cuál, el port escribe `<pref>/data/content/items.json` (formato `content-ref-v1`) con cada objeto original: su nombre
  `dw3:item/<id>`, su nombre en inglés (leído de tu disco al arrancar; el archivo no sale de tu carpeta) y los campos que puedes
  cambiar (`price`, `sellPrice`, `effect`) con sus valores originales. Es solo una referencia: editarlo no cambia el juego
  (si lo editas, el port lo deja como está y escribe la versión nueva al lado, `items.json.new`).
- **Venderlo:** `tables/shop_stock.json` acepta el nombre en la lista: `{ "items": { "add": ["mimod:test-gadget"] } }`
  (§6.5). Un nombre o un número que ningún mod instalado define se rechaza. El precio de compra sale del objeto
  (`price`); otro mod puede cambiarlo con una fila de `item_prices` cuya llave es el nombre (`"mimod:test-gadget": { "price": 77 }`):
  se aplica al registro del objeto, en el orden de mods de siempre; la declaración conserva su precio como valor por defecto. Un nombre
  que no existe rechaza esa fila con una línea en el log y el resto del parche se aplica. La fila `"*"` solo toca los objetos originales.
- **Quitar el mod:** la partida guarda el objeto por nombre. Sin el mod, al cargar se descarta con `[saves] item "..." is
  not defined by any installed mod; dropped from the loaded save` y el juego sigue normal. Los objetos originales no se tocan.
- **Botín:** `tables/battle_loot.json` acepta el nombre en `item` (§6.5); el enemigo lo suelta o se lo roban como a cualquier objeto.
- **Límite:** las listas de objetos del juego tienen espacio para unos 404 tipos distintos en la bolsa a la vez.


#### Vista legible de una partida (solo lectura)

En cada guardado el port escribe `view/slot<N>.json` (formato `dw3save-view-v1`) en la carpeta de partidas (`saves/default/`): casilla, versión del
port, mods activos, nombre, dinero, tiempo, lugar y progreso, grupo, compañeros y la bolsa como `{id, name, count}` (los
objetos de mods por su nombre completo). Sirve para mirar o comparar partidas. **Es solo lectura:** el juego nunca lo lee,
editarlo no cambia nada (lo dice su campo `notes`) y se reescribe en el siguiente guardado.

### 6.8 Contenido nuevo: técnicas

Un mod `"kind": "content"` también puede declarar **técnicas nuevas** en `content/techs.json` (formato `techs-v1`) y hacer que
un Digimon existente las aprenda. Es contenido propio del mod: no toca las técnicas originales.
El **nombre** de una técnica va en inglés en todos los idiomas (`"es-419"` igual que `"eng"`), como los de las técnicas
originales (Pepper Breath, Grand Wave); la **descripción** sí se traduce.

```json
{
  "format": "techs-v1",
  "new": {
    "spark-strike": {
      "base": "dw3:tech/9",
      "mp": 3,
      "power": 120,
      "name": { "eng": "Spark Strike", "spa": "Golpe chispa", "es-419": "Golpe de chispa" },
      "description": { "eng": "A quick, crackling strike." },
      "learnedBy": { "3": { "slot": 1, "level": 2 } }
    }
  }
}
```

- **Nombre y número:** igual que los objetos: `<carpeta del mod>:<llave>` (aquí `mimod:spark-strike`). El port da números
  desde el 444 en orden alfabético del nombre completo; no son estables entre conjuntos de mods, por eso **no los escribas**: las
  partidas guardan el nombre. Tope: el número de una técnica se guarda en 13 bits (hasta 8191), unas 7700 técnicas nuevas.
- **`base`** (obligatorio): una técnica original (1..443), por su nombre `"dw3:tech/9"` o por su número (`9`). La nueva copia todo su registro (`TechData`: coste,
  poder, elemento, efecto, animación...); lo que declares lo reemplaza.
- **Nombres de las técnicas originales:** `dw3:tech/<id>` (como `dw3:item/<id>` en §6.7). Hoy se acepta en `base`; las llaves de
  `learnedBy` siguen siendo el número del Digimon. La referencia está en `<pref>/data/content/techs.json` (`content-ref-v1`): cada
  técnica original con su nombre, su nombre en inglés (leído de tu disco) y los valores de los campos que puedes cambiar.
- **Campos numéricos:** `mp`, `power` (0..65535), `accuracy`, `elementPower`, `effectChance`, `effectPower` (0..255), `hitCount`
  (hasta lo que hace alguna técnica original) y `script` (0..17, la animación: ver abajo). Un campo desconocido o fuera de rango se
  rechaza con una línea en el log (`[techs] ...`) y el resto de la técnica se aplica; una técnica sin `name.eng` o con `base`
  inválida se rechaza entera.
- **Campos que son índices de tablas del juego:** `icon`, `kind`, `element`, `family`, `effect`, `scriptStage`, `scriptEffect`,
  `scriptSound`. Solo se acepta un valor que alguna técnica original ya use (otro valor leería fuera de la tabla correspondiente).
- **Textos:** `name` (obligatorio en `eng`) y `description`, por idioma, con la misma caída que los objetos (`es-419` -> `spa` ->
  `eng`). La caja de la descripción es de una línea (280 px): una más larga se acorta con `...` y el log dice cuál
  (`text: SKLINF tech 444 (es-419) description is wider than the box`). El port agrega las cadenas a `SKLNAM`/`SKLINF` en
  memoria, sin editar tus archivos de texto.
- **Quién la aprende (`learnedBy`):** `{ "<id del Digimon>": { "slot": 1..6, "level": 1..99 } }`. El id es `DigimonData.id` (el
  de la tabla de Digimon del juego, 52 filas reales; las 8 primeras son las formas base de los compañeros). La técnica ocupa la
  ranura `slot` de ese Digimon (`DigimonData.skills[slot]`, `skillLevels[slot - 1]`) y reemplaza lo que hubiera ahí. El juego usa su
  propio código de aprendizaje (`STFGTREP_addSkill`: al subir de nivel da la primera ranura vacía de la entrada cuyo nivel se alcanzó),
  el mod solo cambia esa fila. La ranura 6 es la técnica "firma" del Digimon (`skills[6]`, la que siempre aparece en el menú de batalla);
  las demás se aprenden por nivel. Una partida ya empezada solo la aprende en las entradas cuya ranura está vacía. Si dos técnicas
  reclaman la misma ranura de un Digimon, gana la primera por nombre y la otra se rechaza con su línea.
- **Compatibilidad de animación:** la animación de una técnica es la ranura `script` (0..17) del archivo de efectos del Digimon que
  la lanza (`FighterInfo.effects`, 18 ranuras de código; una ranura vacía termina el script al instante y un índice >= 18 lee memoria
  ajena). No toda técnica sirve a todo Digimon. Al arrancar, el port lee el archivo de luchadores **de tu disco** y rechaza el
  `learnedBy` de un Digimon que no tenga las ranuras necesarias, con una línea en el log (`[techs] ... learnedBy 383 rejected: its
  fighter has no script slot 9 ...`); la técnica sigue existiendo. Del lado del compañero el juego cambia el script 5 por el 6 o el 8
  (triple golpe, efectos de acción, armas de la familia del rival) y el 6 por el 8, así que para el 5 se comprueban también el 6 y el 8,
  y para el 6 el 8.

  **Cómo funcionan las animaciones** (investigación R1; la anatomía completa de un Digimon está en [`docs/digimon-anatomy.md`](../digimon-anatomy.md)):

  - Cada luchador (compañero o enemigo) tiene un **archivo de efectos**: una tabla de **18 ranuras** de código de animación (palabras
    `s16`), con desfase 0 para una ranura vacía. La animación de una técnica es `TechData.script`: el número de ranura de ese
    archivo, del Digimon que la lanza (`src/battle/normal_battle/techs.c:156`, `src/battle/battle_mode/battle_script.c:539-577`).
    El camino es el mismo para compañeros y enemigos.
  - `scriptEffect`, `scriptSound` y `scriptStage` **no son del Digimon**: son índices de tablas globales que el código toma por
    marcadores dentro del script (`9999` para el efecto, `0x62`/`0x63` para el sonido, `0x38` para la escena;
    `battle_script.c:275,458,506-512`). Por eso un mod puede cambiarlos con los valores que alguna técnica original ya usa, sin que
    dependan de quién la lance.
  - Del lado del compañero el juego puede cambiar el script **5 por el 6 o el 8** y el 6 por el 8 (`techs.c:50-103`); el port exige
    también esas ranuras. El script 5 es el **genérico**: lo tienen todos los compañeros y casi todos los enemigos.
  - Una ranura vacía termina el script al instante (inferido del código, no visto en juego) y un índice >= 18 lee memoria ajena
    (`getArchiveEntry` no comprueba límites): el port rechaza esos casos antes de que ocurran.

  **Regla de compatibilidad.** Un luchador puede animar una técnica si tiene la ranura `script` de ella (y, si es compañero y el
  script es 5, también la 6 y la 8; si es 6, también la 8). Números del disco EU (calculados con `tools/techcompat.py`; son datos del
  juego, el port los lee al arrancar y no los guarda). Hay 18 combinaciones distintas de ranuras entre los 248 luchadores:

  | Script | Técnicas que lo usan | Compañeros (52 formas reales) | Enemigos (193) |
  |---|---|---|---|
  | 5 (genérico) | 246 | 52 | 189 |
  | 6 | 33 | 52 | 4 |
  | 8 | 2 | 52 | 0 |
  | 9 | 54 | 44 (faltan en las 8 formas más bajas) | 62 |
  | 10 | 26 | 52 | 16 |
  | 11 | 53 | 52 | 14 |
  | 12 (también carga el banco de sonido 0x46) | 10 | 22 | 0 |
  | 13 | 8 | 44 | 1 |
  | 15 | 7 | 52 | 0 |
  | 16 | 4 | 44 | 2 |

  Los 52 compañeros reales tienen las ranuras 0-8, 10, 11, 14, 15 y 17; a las 8 formas más bajas les faltan 9, 13 y 16, y 22 formas
  tienen la 12. De los enemigos, 109 tienen justo 0-5 y 189 tienen la 5. Las 3 filas vacías (ids 468-470) no tienen archivo. Elige
  como `base` una técnica de `script` 5 si quieres que la aprenda cualquier compañero (367 de las 443 técnicas originales usan scripts
  que los 52 tienen).

  **Dónde mirar la compatibilidad sin herramientas:** `<pref>/data/content/digimon.json` trae para cada luchador `"scriptSlots": [0,
  1, 2, ...]` (las ranuras que tiene, leídas de tu disco al arrancar) y `<pref>/data/content/techs.json` el `script` de cada técnica
  original. Si el `script` de la técnica (más 6 y 8 para el 5, y 8 para el 6, en un compañero) está en la lista del Digimon, lo anima.
  Con herramienta: `python3 -I tools/techcompat.py "<disco>.bin"` imprime la tabla de arriba y las combinaciones de ranuras
  (`--json` para otra herramienta, `--fighters` para listar cada luchador). Lee tu disco al correr y no guarda nada del juego.

  **Qué todavía no se puede:** una animación **nueva** (bytecode propio por mod). Falta descifrar cuántos operandos lleva cada
  comando del script; es dificultad media y no está en el plan inmediato. Hoy una técnica nueva usa una de las 18 ranuras que el
  Digimon ya tiene.
- **Quitar el mod:** la partida guarda, junto al bloque original, el nombre de cada técnica nueva que guarda una entrada (`EXTT`). Sin el
  mod, al cargar la ranura queda vacía con `[saves] technique "..." is not defined by any installed mod; removed from the loaded
  save` y el juego sigue normal. Si cambian los mods y la técnica cambia de número, la carga la renumera por su nombre.
- **Vista legible:** `view/slot<N>.json` lista las técnicas de mods que tienen las entradas (`modTechs`: compañero, entrada, ranura, número y nombre).


### 6.9 Cambiar Digimon existentes y agregar Digimon nuevos

Qué es un Digimon en el juego (tres tablas, ids, modelo, nombres, topes): [`docs/digimon-anatomy.md`](../digimon-anatomy.md).

Un Digimon vive en una carpeta del mod, `digimon/<nombre>/` (ADR-016), con su `digimon.json` (formato `digimon-digimon-v1`) y, si tiene modelo propio,
su `model/` (§8.2). La carpeta `d<nnn>` (el id del luchador con tres dígitos, el mismo nombre que la carpeta que exporta el puerto en
`data/digimon/d<nnn>/`) **cambia a un Digimon que ya existe** (formas de compañero y enemigos); cualquier otro nombre de carpeta es un
**Digimon nuevo** (enemigo o forma de compañero, más abajo). Los **encuentros** donde aparecen los enemigos van en `content/encounters.json`. Los
enemigos no cambian el formato de la partida (ningún dato guardado lleva el id de un enemigo); las formas de compañero nuevas y las técnicas de mods
se guardan por nombre (§6.8 y "Formas de compañero nuevas").

```json
// mimod/digimon/d383/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "d383",
  "partner": { "hp": 180, "stats": { "strength": 60 },
               "skills": { "6": { "tech": "mimod:chispa", "level": 1 } } }
}
// mimod/digimon/d004/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "d004",
  "enemy": { "stats": { "defense": 200 }, "item": "dw3:item/45", "itemChance": 1023 }
}
```

- **Nombre:** la carpeta `d<nnn>` es el luchador `<nnn>` (`dw3:digimon/<nnn>`; ids 3 a 470, así que de `d003` a `d470`); `name` es opcional y, si está, debe ser la carpeta (`$schema` y `notes` se ignoran). Una carpeta `d<nnn>` fuera de rango, o con otro `name`, se rechaza con su línea. El id es el del luchador: el de `DigimonData` para las
  formas de compañero y el de la tabla de batalla para los enemigos. Los dos espacios de ids no se cruzan, así que el id decide la
  faceta (`partner` o `enemy`); usar la otra se rechaza con su línea. La referencia con los 248 luchadores (55 formas de compañero,
  3 de ellas marcadas `"placeholder": true` porque son filas vacías que ningún Digimon usa y no se pueden cambiar, y 193 enemigos),
  su nombre en inglés (leído de tu disco, nunca guardado en el repo) y los campos editables con su valor original, está en
  `<pref>/data/content/digimon.json` (`content-ref-v1`, mismo manifest y regla ".new" que `items.json` y `techs.json`; es solo la lista de nombres, el puerto no la lee).
- **`partner`** (una fila de `DigimonData`): `hp`, `mp`, `hpGrowth`, `mpGrowth`, `expRate`, `expLevel` (0..255), `family` (solo un
  valor que ya tenga alguna forma original: indexa una tabla del juego), `stats` `{strength, defense, spirit, wisdom, speed,
  charisma}` (0..999, lo que la forma suma a sus estadísticas), `resistances` `{"0".."6"}` (0..999, los siete elementos en el orden
  del juego), `statusResists` `{poison, paralysis, confusion, sleep, knockOut}` (0..255) `statGrowth` `{strength, defense, spirit, wisdom, speed, charisma}` (0..5: la columna de `STFGTREP_statGains` de la que
  gana cada estadística al subir de nivel), `resistGrowth` `{"0".."6"}` (1..5: la fila de las tablas de ganancia de resistencia, en
  batalla y en el gimnasio; varios crecimientos comparten arreglo de ganancias, ver `docs/data-tables.md` 3.1), `knownLevels`
  `{"0".."4"}` (1..99: el nivel que marca como conocida la técnica `skills[i]`; sin tabla) y `skills` `{"1".."6": {tech, level}}`: la
  técnica (número 1..443, `"dw3:tech/<id>"` o `"<mod>:<llave>"`; 0 vacía la ranura) y el nivel (1..99) al que se aprende, en
  `DigimonData.skills[slot]` y `skillLevels[slot - 1]`. Un valor sin `tech` o sin `level` deja el otro como estaba.
- **`enemy`** (una fila de la tabla de batalla): `stats` `{attack, defense, spirit, wisdom, speed}`, `resistances` `{"0".."6"}` y
  `statusResists` (0..32767), `family` (un valor que ya tenga algún enemigo), `techs` `{"1".."3"}` (técnica; las ranuras 2 y 3
  aceptan 0 para ninguna), `item` (id, `"dw3:item/<id>"` o `"<mod>:<llave>"`; 0 ninguno) e `itemChance` (0..1023, en 1024avos
  menos uno, igual que `battle_loot`).
- **No se puede cambiar** (indexan tablas del juego u otras filas): `id`, `nameId`, `pairPartner`, `pairTech`, `blastForms`,
  `statGrowth`, `resistGrowth`, `knownLevels` y, en enemigos, la IA (`actions`, `counter`). El nombre mostrado tampoco.
- **Validación:** un Digimon que no existe, una faceta que no corresponde al id, un nombre desconocido o un valor fuera de rango
  rechazan ese campo o esa entrada con una línea `[digimon] ... rejected: ...`; lo inválido nunca llega al juego y el resto de la
  entrada se aplica. Una técnica que se da a un luchador debe tener las ranuras de animación que su archivo de efectos ofrece
  (la misma regla de §6.8 que `learnedBy`; para enemigos solo cuenta el `script` propio de la técnica).
- **Cuándo se aplica:** las filas de compañero se parchean en memoria al arrancar; las de enemigo, sobre el búfer de la tabla de
  batalla en cada lectura (el cambio se ve en la batalla siguiente). Si varios mods tocan el mismo campo, gana el que va después en
  el orden de mods. `learnedBy` (§6.8) también acepta `"dw3:digimon/<id>"` como llave; si el `digimon.json` de `d<nnn>` y `learnedBy` dan la
  misma ranura, gana el `digimon.json`.
- **Convivencia con `battle_loot`:** el contenido de los `d<nnn>/digimon.json` se aplica primero y la tabla `battle_loot` (§6.5) va encima, así
  que para `item` e `itemChance` de un enemigo gana `battle_loot`; el resto de lo que cambie el `digimon.json` sigue.
- **Solo el `digimon.json` de `d<nnn>`:** su `model/` nunca se lee, porque ningún mod puede reemplazar el modelo de un original (el modelo de un Digimon nuevo, sí: §8.2; para partir de uno original, `"from": "dw3:model/<id>"`). `--validate-mod` avisa de un `d<nnn>/model/`.
- **Quitar el mod:** el juego vuelve a sus valores (nada se guarda en la partida).
- **Comparar el balance:** `python3 -I tools/balance_compare.py "<disco>.bin" <carpeta del mod> [--level N]` imprime, para cada forma de compañero que define el mod
  (una forma nueva, o el `partner` de un `d<nnn>`), PV, PT, crecimiento de PV y PT por nivel, las seis estadísticas y la columna de crecimiento de cada una, junto a la
  primera forma de los ocho compañeros originales; después una proyección a nivel N (30 por omisión: lo que tendría un compañero que conserve el crecimiento de esa forma
  del nivel 2 al N con tiradas promedio, con las reglas de `STFGTREP_raiseStats`) y la diferencia en porcentaje de cada forma del mod contra el promedio de los ocho. Los
  originales se leen de tu disco al correr y todo sale por la salida estándar: no guardes ni subas esa salida (§9). La proyección sirve para comparar, no para predecir:
  una partida real cambia de forma (cada forma tiene su crecimiento) y las tiradas varían. Una forma cuya `base` no es una forma de compañero (un enemigo) se omite con una línea.


#### Enemigos nuevos (`digimon/<llave>/`)

Una carpeta `digimon/<llave>/` con un `digimon.json` declara un **Digimon nuevo** (la llave es a-z, 0-9, `-`, `_`; `d<nnn>` está reservada para los originales). El enemigo
se llama `"<carpeta del mod>:<llave>"` y copia la fila de la tabla de batalla de un enemigo original (`"base"`):

```json
// mimod/digimon/brotemon/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "brotemon",
  "base": "dw3:digimon/4",
  "model": "dw3:digimon/25",
  "title": { "eng": "Sproutmon", "es-419": "Brotemon" },
  "stats": { "attack": 120, "defense": 40 },
  "item": "dw3:item/45", "itemChance": 1023
}
```

- **`base`** (obligatorio): un enemigo original (número o `"dw3:digimon/<id>"`); un id que no existe, o una fila vacía de la tabla de
  compañeros, se rechaza (con un compañero de base, ver "Formas de compañero nuevas"). De ahí salen estadísticas, técnicas, botín,
  resistencias y la IA (la IA no se edita).
- **`model`** (opcional): otro **enemigo original** cuyo modelo, movimientos, efectos, cara y cámaras se reutilizan; sin él, el del
  `base`. Un compañero no sirve de modelo de un enemigo (su ficha tiene otro formato). Es un modelo prestado: el formato de los
  modelos de batalla todavía no se puede editar.
- **`model` como objeto (avanzado, para quien ya tiene un archivo de modelo en el formato del juego; sirve igual en las formas de
  compañero):** el Digimon recibe una fila de `FighterInfo` **propia** en vez de compartir la de otro. Claves, todas opcionales:
  `"from"` (el luchador original del mismo tipo que sirve de plantilla: cámaras, altura, distancia y lo que no se reemplace; por defecto
  el `base`), `"model"`, `"motions"` y `"effects"` (cada uno `{"file": ..., "entry": 0..65535}` o `"dw3:digimon/<id>"` para tomar esa
  referencia de un luchador original), `"height"` y `"distance"` (enteros de 16 bits). `"file"` es un número de archivo (1..65535) o un
  nombre `"<carpeta del mod>:<ruta dentro del mod>"` que lee un archivo de modelo crudo del mod (el que escribe `tools/model_import.py`)
  y lo sirve desde memoria. La cara queda vacía (sin parpadeo). Si algo no se resuelve, el Digimon sigue compartiendo la fila de su
  plantilla y el log dice por qué. Queda un tope de 63 filas propias de enemigo y 201 de compañero por tabla. El destino es la forma de
  texto de abajo.
- **`"model": "<carpeta del mod>:<llave>"` y el `model/` de la carpeta (modelo con arte propio):** una fuente neutra (glTF + PNG + `model.json`) en
  `digimon/<llave>/model/`; si el `digimon.json` **no** trae `"model"`, el `model/` de su propia carpeta es su modelo (lo normal: una carpeta, un Digimon,
  su modelo). Con `"model"` gana el campo: `"<carpeta del mod>:<otra llave>"` usa el `model/` de otra carpeta (del mismo mod u otro) y un original no usa ninguno.
  El port la convierte al cargar y el Digimon recibe una fila propia con ese modelo, los efectos del donante y la plantilla, la altura y la
  distancia que dice el `model.json`. Guía completa: §8.2. `"dw3:model/<id>"` toma el modelo de un luchador original.
- **Campos editables:** los de `enemy` de arriba (`stats`, `resistances`, `statusResists`, `family`, `techs`, `item`, `itemChance`),
  con las mismas reglas. Una técnica debe tener su animación (`script`) en el archivo de efectos del **modelo** elegido; si no, esa
  ranura conserva la técnica del `base` y queda una línea en el log (`its model ... has no script slot N`).
- **`title` y `description`:** por idioma (`eng` obligatorio; `es-419` -> `spa` -> `eng`), como en objetos y técnicas; el texto va en `title` porque `name` es el nombre de la carpeta. El nombre sale
  en la batalla por el archivo de nombres (`DIGNAM`) ampliado por la capa de texto; la descripción se guarda, pero ninguna pantalla
  muestra la de un enemigo todavía.
- **Ids:** deterministas, desde **471** (por encima de todos los luchadores originales, el mayor es 470) en el orden alfabético del
  nombre completo, **compartido con las formas de compañero nuevas** (enemigos y compañeros nuevos de todos los mods forman una sola
  lista ordenada, así que nunca chocan); el nombre en el archivo `DIGNAM` es la entrada `251 + posición`. **Límite:** el id es de 16 bits con signo en la
  tabla de batalla, la lista de luchadores y el combatiente (`s16`), así que caben hasta **32297** enemigos nuevos (ids 471..32767); el
  resto, por orden alfabético, se descarta. El id **depende de los mods instalados**: escríbelo siempre por nombre.
- **Sin tope de tamaño:** el archivo de la tabla de batalla y el de luchadores del disco tienen tamaño fijo, pero el port sirve
  desde memoria versiones **más largas** (filas añadidas; la lista de luchadores se copia al final y el encabezado apunta a ella; los
  bytes originales no se tocan) y el caché de archivos reserva el tamaño que se anuncia. Un archivo crudo (`files/`) de esos dos números
  gana y los enemigos nuevos no se sirven (una línea en el log). `battle_loot` (§6.5) solo conoce los enemigos originales: el botín de
  uno nuevo va en su propia entrada.
- **Guardado:** ningún dato guardado lleva el id de un enemigo (la partida guarda compañeros, cartas, objetos, banderas y progreso;
  el encuentro de la batalla siguiente no se guarda), así que no hay trozo de nombres y quitar el mod no deja nada colgado.
- **No se puede** cambiar un Digimon nuevo con una carpeta `d<nnn>` (su id depende de los mods): sus campos van en su propio `digimon.json`.
- Un enemigo nuevo no aparece solo: hay que colocarlo en un encuentro (siguiente apartado).

#### Formas de compañero nuevas (`digimon/<llave>/` con un compañero de `base`)

Si `"base"` es una **forma de compañero original** (`"dw3:digimon/383"`; no una de las 3 filas vacías), la entrada es una forma de
compañero nueva: una fila nueva de `DigimonData` (copia de la del `base`) que el juego puede buscar por id (`GET_DIGIMON`), nombrar
y llevar a batalla:

```json
// mimod/digimon/brasamon/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "brasamon",
  "base": "dw3:digimon/383",
  "model": "dw3:digimon/384",
  "title": { "eng": "Embermon", "es-419": "Brasamon" },
  "description": { "eng": "A partner form that burns bright." },
  "hp": 220, "stats": { "strength": 90, "speed": 60 },
  "skills": { "1": { "tech": "mimod:chispa", "level": 1 } },
  "evolution": { "partner": 0, "needs": [ { "digimon": "dw3:digimon/383", "level": 1 } ], "stat": "level", "value": 3 }
}
```

- **Campos editables:** los de `partner` de arriba (`hp`, `mp`, `hpGrowth`, `mpGrowth`, `expRate`, `expLevel`, `family`, `stats`,
  `resistances`, `statusResists`, `statGrowth`, `resistGrowth`, `knownLevels`, `skills`), con las mismas reglas; `blastForms` (cinco formas de socio por nombre, por tramo de nivel) y `pairPartner` (una forma de socio por nombre, o 0)
  van por nombre y se validan (el nombre debe ser una forma de socio original o nueva; si no, se rechaza el campo y quedan los del `base`); lo demás
  (`pairTech`) se copia del `base`; `statGrowth`, `resistGrowth` y `knownLevels` se editan igual que en `partner`. Se copia la fila **original** del `base`, no la que otro mod haya
  cambiado con `"change"`.
- **`model`** (opcional): otra **forma de compañero original** cuyo modelo, movimientos, efectos, cara y cámaras se reutilizan; sin
  él, el del `base`. Un enemigo no sirve de modelo de un compañero (su ficha mide 0x48 bytes y tiene una cámara; la de un compañero
  0xC4 y doce). Como la entrada de luchador solo copia el índice y el tipo del modelo, **no se agrega ninguna ficha nueva** y los
  topes de PS1 (un índice de un byte, 3 tipos libres) no aplican.
- **Técnicas:** cada `skills.N.tech` distinto del `base` se comprueba contra el archivo de efectos del **modelo** final (la regla de
  §6.8, para un compañero: el script 5 exige además las ranuras 6 y 8); la ranura que no cabe conserva la técnica del `base` y
  queda una línea `its model ... has no script slot N`. La técnica llega a la lista de batalla cuando la entrada del compañero la
  aprende (por nivel, con `skills`/`level`) o por `learnedBy`/`digimon.json` de otra forma que la enseñe.
- **Nombre:** `title` por idioma (`eng` obligatorio; `es-419` -> `spa` -> `eng`) en la entrada `251 + posición` de `DIGNAM`, igual que
  un enemigo nuevo. `DigimonData.nameId` es de **un byte** y `DIGNAM` trae 251 entradas, así que solo cabrían 5 nombres nuevos:
  el port los saca de ahí. El juego muestra el nombre de una forma por `PC_DIGIMON_NAME(fila)` (un solo accesor en los ~20 sitios
  de `src/` que mostraban `nameId`), y para una fila nueva el accesor lee un número de entrada propio, sin tope de 8 bits; la fila
  conserva el `nameId` del `base` como respaldo. `DIGINF` (la descripción del laboratorio) recibe la misma entrada con `description`.
- **Ids:** los mismos que los de los enemigos nuevos (ver arriba): una sola lista alfabética de todos los Digimon nuevos, desde 471.
  Una forma nueva ocupa también una fila de la tabla de Digimon (la fila 55 en adelante, en el orden de los ids). `pairPartner` y `blastForms`
  guardan fila + 1 en 16 bits (el port los lleva aparte del `u8` del disco), así que una forma nueva puede ser su valor y las filas pasan de 254
  (hasta 4096; `blast_forms` y `evolutions` aceptan cualquier fila que exista).
- **Cómo se consigue (`"evolution"`):** el juego revisa la lista de evolución de cada compañero al terminar la batalla, en el informe
  (`STFGTREP_learnDigimon`): la primera forma de la lista que el compañero aún no tiene y cuyos requisitos cumple se le da
  ("Digievolución aprendida: ..."). La lista es una tabla fija de 44 entradas por compañero; el port **no la toca**: le agrega al final las
  entradas que declaran los mods. `"evolution"` es un objeto (o una lista de objetos, hasta 4, uno por compañero que pueda aprenderla):
  `"partner"` (obligatorio: `0..7` o `"dw3:digimon/<id>"` de la primera forma de ese compañero), `"needs"` (hasta 2 requisitos
  `{"digimon": "dw3:digimon/<id>" o "<mod>:<llave>", "level": 1..99}`: el compañero debe tener esa forma a ese nivel; sin `"level"`,
  1), `"stat"` (`1..14` o `strength`, `defense`, `spirit`, `wisdom`, `speed`, `charisma`, `level`; 8..14 son las siete resistencias en
  el orden del juego; sin él, ninguno) y `"value"` (`0..32767`: lo mínimo que debe valer ese dato del compañero; con `"stat"`, por
  defecto 1). Son los campos de `Evolution` (`include/battle/report.h`) con los Digimon por nombre. Un requisito puede nombrar otra
  forma nueva (la que aprende, una cadena de evoluciones) pero no un enemigo; el requisito que no se resuelve descarta esa entrada
  con una línea `[digimon]`. El orden de las entradas es el de los ids de las formas y el de sus declaraciones; el compañero recibe
  **una forma por informe** (la primera que cumpla), como en el juego original.
- **Qué hace una forma nueva hoy:** existe como dato (se busca por id), tiene nombre (el menú de DV de la batalla lo muestra, probado en el
  juego; el estado y el laboratorio leen el mismo accesor), pelea con sus estadísticas, técnicas y modelo cuando es la forma activa y
  **se consigue jugando normalmente** con `"evolution"`. Para probar sin jugar hasta ahí, el arnés de pruebas tiene
  `DW3_SEED_PARTNER="<compañero>.<entrada>=<mod>:<llave>[@<nivel>][/<hueco>],..."`, que pone la forma en una entrada del compañero y
  la elige como su forma de batalla (la entrada puede ser mayor que 43: ver el siguiente apartado). `@<nivel>` es el nivel de la **forma**; el del compañero lo pone
  `DW3_SEED_PARTNER_LEVEL="<compañero>=<nivel>,..."` (2..98): sube sus estadísticas nivel a nivel con el crecimiento del juego y le deja exactamente la EXP del nivel siguiente, así que una batalla ganada lo sube y le enseña la forma que espera
  (una vez por corrida). `pc/review.sh betamon|seadramon|betamonpesca` lo usan para probar `betamon-pack` con los umbrales reales.
- **Más de 44 formas por compañero:** `PartnerStats.entries[44]` vive en el bloque guardado `GAME`. Las primeras 44 entradas siguen
  ahí, igual que en el original; las siguientes viven en un almacén propio del port, por compañero, hasta **256 entradas en total**
  (tope de seguridad, no de diseño; el laboratorio, el informe y la búsqueda del juego usan la capacidad actual). El laboratorio
  (lista de entradas, cambio de forma, tabla de recetas) y el informe de batalla leen las entradas por los mismos accesores, así que
  una forma de la entrada 45 en adelante se ve y se elige como cualquier otra.
- **Guardado:** una forma nueva puede estar en la partida (las entradas del compañero guardan su id, que depende de los mods). El
  archivo de la partida lleva un trozo `EXTD` con el nombre `"<mod>:<llave>"` de cada forma nueva, su id y el id original de su
  `base`; al cargar, cada id se reasigna **por nombre** (también si cambió el orden de los mods). Si el mod ya no está, la regla
  es: la entrada que tenía esa forma pasa a ser **la forma original en que se basaba** (conserva nivel, experiencia y técnicas); si
  el compañero ya tenía esa forma original, la entrada se vacía; las formas elegidas y la forma de batalla que la nombraban pasan a
  la original si el compañero la tiene, o a ninguna. Sin trozo (o dañado) no se conoce el `base`: las entradas se vacían. Cada
  caso deja una línea `[saves]`. `view/slotN.json` lista las formas nuevas en `modForms` (las del bloque; no las del trozo `EXTP`).
  Las entradas **más allá de la 44** (de cualquier forma, original o nueva) van en su propio trozo, `EXTP`, con la forma y las
  técnicas de mod **por nombre**; solo se escribe si el compañero tiene alguna (el archivo de una partida de 44 formas o menos no
  cambia). Si falta el mod de una forma, esa entrada se descarta (no se guarda su `base`); si falta el mod de una técnica, solo esa
  ranura queda vacía; cada caso deja una línea `[saves]`. Una forma elegida o la de batalla que pierde su mod pasa a su forma
  original solo si el compañero la tiene entre las primeras 44 entradas (el remapeo del bloque no mira las del trozo `EXTP`), si no a 0.
- **Quitar el mod** sin tocar la partida: lo anterior; la partida sigue cargando.

#### Dónde aparecen (`content/encounters.json`)

`content/encounters.json` (formato `encounters-v1`) coloca enemigos (originales o nuevos, por nombre) en los **encuentros** del juego (las 335 filas de
`FIELDSTG_encounters`; `BATTLE_SETUP.battle` es ese índice, que también indexa `battle_rewards`):

```json
{
  "format": "encounters-v1",
  "encounters": {
    "34": { "2": { "enemy": "mimod:brotemon", "level": 3, "hp": 60, "mp": 10, "strength": 16 } },
    "35": { "1": { "enemy": "dw3:digimon/4", "level": 5, "hp": 90 } }
  }
}
```

- La llave es el índice del encuentro (1..334; el 0 es el vacío) y la subllave el hueco (`"1".."3"`, los tres de una batalla).
  `"enemy"` es `0` (deja el hueco vacío), un enemigo original (número o `"dw3:digimon/<id>"`) o un enemigo nuevo
  (`"<mod>:<llave>"`). `level` (1..99), `hp` (1..32767), `mp` (0..32767) y `strength` (1..32767, en 16avos: 16 = x1) son opcionales:
  lo que falta se toma del enemigo que ese hueco tenía; un hueco vacío no tiene qué heredar, así que ahí van los cuatro.
- **Diseño:** es un parche de tabla de overlay en el estilo de `shop_stock` (§6.5): al entrar al campo, el hueco del encuentro se
  apunta a una fila `BattleEnemy` que el port posee. La tabla original (datos del overlay FIELDSTG) nunca se escribe: el ámbito
  de estado la restaura en cada entrada y el parche se vuelve a poner; **quitar el mod devuelve los encuentros originales**.
  Varios mods sobre el mismo hueco: gana el último del orden, entero.
- **Dónde ocurre cada encuentro:** `AreaBattle.encounter` de cada escenario (`src/field/stages/`) y los guiones de jefes (los mismos
  índices); por ejemplo, el Parque Central (escenario 330) usa los encuentros 34 y 35.
- **Botín:** el juego sortea un enemigo entre los que tienen `item` y tira su probabilidad. El original leía mal el hueco sorteado
  (un enemigo con botín detrás de un hueco sin objeto nunca soltaba nada); el port lo corrige (arreglo G2), así que el hueco donde
  pongas un enemigo nuevo no importa para su botín.
- Valores ya validados por el juego original (como `hp` o el nivel de un jefe de historia) no se vigilan: un encuentro de historia
  con un enemigo distinto puede cambiar el combate de forma que el guion no espera.

### 6.9b Compañeros nuevos y equipos iniciales

Un compañero **nuevo** (un noveno, décimo... además de los ocho del juego) lo declara un mod de contenido con una carpeta por compañero, `partners/<llave>/`: su
`partner.json` (formato `partners-partner-v1`, esquema `schema/partner.schema.json`) es el registro y junto a él va el arte (§6.2f). El juego recorre `partners/` de cada mod
una vez al arrancar; una carpeta sin `partner.json` no es un compañero (puede ser solo arte). Un **equipo inicial** nuevo sigue en `content/teams.json` (`teams-v1`, `schema/teams.schema.json`). Decisión de diseño:
`docs/decisions/ADR-015-partner-registry.md` y, para la carpeta, `ADR-016-asset-layout.md`. La forma inicial del compañero es una forma
de una carpeta `digimon/<llave>/` (6.9) o una original; su modelo, técnicas y nombre de forma se declaran allí.

```json
{
  "format": "partners-partner-v1",
  "name": "betamon",
  "base": "dw3:partner/0",
  "firstForm": "mimod:betamon",
  "title": { "eng": "Betamon", "es-419": "Betamon" },
  "description": { "eng": "A river digimon.\nStrong in the water.", "es-419": "Un digimon de río.\nFuerte en el agua." },
  "equip": { "like": "dw3:partner/0", "allow": ["dw3:item/120"], "deny": ["mimod:espada-rota"] },
  "equipSet": { "items": ["dw3:item/243", "dw3:item/269", "dw3:item/98", "dw3:item/284"], "bonuses": [4, 4, 4, 4, 4, 20] },
  "evolution": [ { "digimon": "mimod:seadramon", "needs": [ { "digimon": "mimod:betamon", "level": 20 } ], "stat": "level", "value": 30 } ]
}
```

(`mods/mimod/partners/betamon/partner.json`, con `portrait.png`, `icon.png` y lo demás a su lado.)

```json
{
  "format": "teams-v1",
  "new": {
    "rio": { "partners": ["mimod:betamon", "dw3:partner/1", "dw3:partner/2"],
             "name": { "eng": "River pack", "es-419": "Paquete Río" }, "description": { "eng": "Betamon with two friends." } }
  }
}
```

- **Cabecera.** `"format": "partners-partner-v1"` (obligatorio) y `"name"` (opcional; si está, es el nombre de la carpeta, la llave; `"$schema"` y `"notes"` se ignoran). El resto de
  los miembros son los campos del compañero, en el mismo objeto. La carpeta de una **original** (`agumon`, `gabumon`...: el nombre inglés en minúsculas de `data/partners/<nombre>/`) **no**
  es un compañero nuevo: ahí solo se lee el arte (§6.2f) y su `partner.json`, si lo hay, no se lee. El `partner.json` que el juego exporta de un original (`<pref>/data/partners/<nombre>/partner.json`,
  mismo nombre de formato) es descriptivo y nunca se lee: lleva `disc`, `portrait`, `icon`, `field`, `training` y un `title` de texto, que un `partner.json` de mod no usa (si lo copias para hacer uno nuevo,
  cambia la llave de la carpeta y reescribe `title` como objeto por idioma). Ningún archivo parchó nunca a un compañero original (`base` solo da arte y equipo): un mod no puede cambiar su ficha desde aquí.
- **Nombres y números.** El compañero se llama `"<carpeta del mod>:<llave>"` (llave: a-z, 0-9, `-`, `_`, hasta 48; la de su carpeta). Su número en el juego es **8 más
  su lugar en el orden alfabético** de los nombres completos de todos los compañeros declarados (y el de un equipo, **3 más** el suyo): los mismos mods dan
  siempre los mismos números, que no se guardan (la partida guarda el nombre, ADR-014). Los ocho originales se nombran `dw3:partner/<0..7>`. Tope de
  seguridad: 256 compañeros en total.
- **`base`** (obligatorio): un compañero original (`0..7` o `"dw3:partner/<n>"`). Su arte 2D (retratos, animaciones de menú) se usa en lugar del del nuevo
  mientras no tenga propio, y su bit de la máscara `partners` de los objetos es la elegibilidad de equipo por omisión.
- **`firstForm`** (obligatorio): la forma con la que el compañero empieza: una forma de compañero original (`"dw3:digimon/<id>"`) o una nueva de
  `digimon/<llave>/digimon.json` (`"<mod>:<llave>"`, con `"base"` de compañero; una forma puede usar `"model"` como objeto con el modelo de un enemigo). De esa fila salen
  PV, PT, estadísticas, resistencias y crecimiento; el compañero no tiene entrada de esa forma hasta el primer informe de batalla, igual que los originales.
  Una forma que no resuelve rechaza **al compañero entero** (con su línea `[partners]`).
- **`title`** (obligatorio, `eng`) y **`description`** (opcional): por idioma (`es-419` -> `spa` -> `eng`). El nombre es el que la partida le da al compañero
  (HUD de batalla, estado, laboratorio) y el de la pantalla de equipo inicial; la descripción, el texto de esa pantalla (las líneas con `\n`). Los textos son
  entradas añadidas al archivo `NAMEET` (31 entradas en el disco), servidas por la capa de texto en todos los idiomas.
- **`equip`** — `like`: el compañero original cuyo bit de la máscara lee (por omisión `base`); `allow`: objetos que puede llevar diga lo que diga la máscara;
  `deny`: objetos que no (gana a `allow` y a la máscara). Objetos por número, `"dw3:item/<id>"` o `"<mod>:<llave>"`. **`equipSet`**: cuatro objetos que
  puestos juntos suman seis bonos (-999..999) a las seis estadísticas de batalla, como las `EQUIP_SETS` originales; sin él, el compañero no tiene conjunto.
  La máscara `partners` de los objetos (`weapon_data`/`armor_data`/`accessory_data`, §6.5b, 0 a 255) solo tiene un bit para cada uno de los ocho originales:
  para un compañero nuevo se usa esto.
- **El arte propio está en la misma carpeta, `mods/<mod>/partners/<llave>/`**, junto al `partner.json` (`<llave>` es lo que va después de los dos puntos en `"<mod>:<llave>"`; no hay campos `art`, `icon` ni `sprite`: un
  campo así se rechaza como desconocido). Los archivos se buscan por las capas de assets (los mods del último al primero, luego `data/`), uno por uno, y el que falte usa el de `base`.
  `portrait.png`: un PNG indexado de **32 de alto y 32 x N de ancho** (N = 1..8 celdas, hasta 16 colores, índice 0 transparente; con más colores se reducen a 16 con un aviso); el retrato de todas
  las pantallas con compañeros (equipo inicial y confirmación, nombre, laboratorio, menú de campo, gimnasio, informe de combate, ficha de la memory card); la animación recorre las
  celdas de ida y vuelta. `icon.png`: un PNG indexado de **exactamente 44x28**, hasta 16 colores, índice 0 transparente: el ícono de la lista de la memory card.
  `field/`: un sprite `sprite-v1` (§6.3, `docs/stages.md` 11; `sprite.json` + un PNG por cuadro): el
  personaje con el que el compañero **sigue al jugador en el campo**. Un PNG inválido deja el de `base` con una línea `[partners]`. `training/<entrenamiento>/` es el gimnasio del compañero (§6.2f, «El gimnasio de un compañero»).
  Plantilla del sprite de seguidor (medida en el decomp, `docs/formats.md` 4.12): los **conjuntos** `1` (parado), `5` (correr) y `6` (parar, se reproduce una vez) son los
  únicos que un seguidor pide (los que falten caen al primero de la lista); las **direcciones** son 5 (0 abajo, 1 abajo-izquierda, 2 izquierda, 3 arriba-izquierda,
  4 arriba; las derechas se espejan solas), y un conjunto puede nombrar una sola animación para las 5. **Cuadros** de cualquier tamaño hasta 256x224 (16 colores, índice 0
  transparente); el puerto le da una ranura del tamaño de su cuadro mayor (los seguidores del juego usan 32 px de ancho y hasta 64 de alto). La posición `x`, `y` de cada paso
  es el desplazamiento del cuadro respecto de los pies del actor (por ejemplo `-ancho/2`, `-alto`). `actor_width` opcional (8..256). Ejemplo mínimo: un `sprite.json` con 2
  cuadros, 3 animaciones (parado, correr en bucle, parar con `"loop": false`) y los conjuntos `1`, `5`, `6`.
- **`evolution`**: la lista de formas que puede aprender tras una batalla (§6.9, "Cómo se consigue"): `digimon` (la forma que aprende, obligatorio), `needs`
  (hasta 2 `{digimon, level}`), `stat` (`1..14` o un nombre) y `value`. Mismas reglas que el `"evolution"` del `digimon.json` de un Digimon, pero declarado por el
  compañero (que no existe en los ocho originales); una forma o un requisito que no resuelve descarta esa entrada con su línea. Un compañero nuevo no tiene la
  lista original de 44 entradas: solo las suyas.
- **Equipos** (`teams.json`): `partners` son **tres** compañeros distintos por nombre (originales o de un `partner.json` de mod; un nombre que ningún mod define, repetido o
  una cantidad distinta rechaza el equipo entero); `base` (`0..2` o `"dw3:team/<n>"`) es opcional y da los miembros por omisión de un equipo original.
  `name`/`description` son el título y el texto del equipo en la pantalla de elección. Esa pantalla muestra **cualquier cantidad** de equipos: los tres originales (A, B, C)
  y los de los mods (D, E, ..., en el orden alfabético de su nombre completo `<mod>:<llave>`). Muestra hasta seis filas a la vez (con los tres originales, las tres de siempre); solo con más de seis equipos la columna se
  desplaza con arriba/abajo y una barra en el margen izquierdo dice dónde está. El retrato de un compañero nuevo es el de su `base` hasta que tenga arte propio.
  Para pruebas, `DW3_STARTER_SET=<número>` o `DW3_STARTER_SET=<mod>:<llave>` elige el equipo que `setParty` da donde antes daba el 0 (un `warp:` o `mode:` sin partida).
  `GameState.partySet` guarda el equipo con el que empezó la partida; solo lo comparan las condiciones de evento (`partner` op 0, contra 0..2 en el disco). La partida
  guarda un equipo de mod **por nombre** (sección `TEAM`): al cargar con otros mods, sigue siendo el mismo equipo; si su mod ya no está, queda "sin equipo" (`-1`, que
  ninguna condición del disco iguala) y la partida carga igual.
- **Cuándo se aplica.** Los mods se leen **una vez al arrancar** (después de las carpetas `digimon/` y `items.json`, antes de leer cualquier partida) y se sirven desde
  memoria. Un compañero nuevo recibe los datos iniciales de una partida nueva (nombre, nivel 1, PV, PT, estadísticas y resistencias de su forma inicial) y
  también al **cargar una partida que no lo traía** (un mod instalado después). Las filas `special_conditions` (§6.5b) con una operación de compañero aceptan
  el número de cualquier compañero que exista o, mejor, su **nombre** (siguiente punto).
- **Desbloquear un compañero con una misión.** Un compañero que no está en el equipo inicial elegido se consigue como los originales: una charla (o un evento) con la acción "desbloquear" y la condición "sigue
  bloqueado". Son dos filas de `tables/special_conditions.json` cuyo `arg` es el **nombre** del compañero (`"<mod>:<llave>"`, o `"dw3:partner/<n>"` para uno original), no su número (el número de un compañero
  nuevo depende de qué mods hay y se mueve). El puerto lo resuelve una sola vez, al leer la tabla; un nombre que ningún mod define rechaza el campo (`row "<fila>" field "arg" rejected: no installed mod defines "<nombre>"`,
  que `--validate-mod` muestra) y la fila no corre con un valor sustituto: una fila original conserva su `arg`, y una fila nueva de un mod no se crea (su nombre queda sin definir, como una condición desconocida).
  ```json
  { "format": "special_conditions-v1", "rows": {
      "mimod:abeja-libre": { "primitive": 1, "op": 6, "arg": "mimod:abeja" },
      "mimod:dar-abeja":   { "primitive": 1, "op": 3, "arg": "mimod:abeja" } } }
  ```
  y en el `stage.json` del escenario, la charla del personaje que la entrega:
  `{"text": "mimod:abeja-llega", "conditions": [["special", "mimod:abeja-libre", 1]], "actions": [["special", "mimod:dar-abeja", 1]]}` seguida de una charla sin condiciones para cuando ya la tiene.
  El estado desbloqueado se guarda **por nombre** con el resto de los datos del compañero (sección `PTNR`, ADR-014): quitar o agregar mods antes de cargar no cambia a quién se desbloqueó. La op 9 ("los ocho
  desbloqueados") sigue contando solo a los ocho originales, porque es lógica de la historia del disco (el capítulo extra); la op 7 ("todo compañero desbloqueado es nivel 45") sí mira a los de mods que se hayan desbloqueado.
- **Quitar el mod.** La partida sigue cargando: el compañero y sus formas se descartan con líneas `[saves]` (ADR-014), el equipo se compacta, el ícono de la
  tarjeta usa el arte de reemplazo. Volver a instalar el mod no recupera lo descartado.
- **Validación.** Una línea `[partners]`/`[teams]` por rechazo (`--validate-mod <carpeta>` las muestra, con el cargador del juego): un `partner.json` que no es JSON, con otro `format` o con un `name` que no es el de la carpeta,
  una llave inválida, un `base` que no es original, un `title` sin `eng`, una `firstForm` que no resuelve, un campo desconocido, un objeto o una forma que no existe. Además avisa de un `partner.json` en un mod que no es
  `"kind": "content"` y de un `partner.json` dentro de la carpeta de una original (`never read`). Lo inválido nunca llega al juego.
  El resto de `partners/<llave>/` también se revisa: el arte con los lectores del juego (`partners/<llave>/portrait.png rejected: ...`, `icon.png`, `field rejected: frame N: ...`, `training/<entrenamiento>/training.json rejected: ...`, una línea por archivo, con la ruta dentro del mod),
  y avisa de un archivo o carpeta que nadie lee (`partners/<llave>/notas.txt is ignored (a partner's folder holds partner.json, portrait.png, icon.png, field/ and training/)`, con «did you mean» si se parece a uno de esos nombres), de un `field/` de compañero nuevo sin `sprite.json`,
  de un `training/<nombre>` que no es de ningún entrenamiento o sin `training.json`, de una carpeta que ni es de una original ni la declara un `partner.json` de un mod de contenido activo (`partners/<llave> is ignored: ...`) y de una carpeta compartida:
  `partners/<llave>/ is shared by mods/b, mods/a, data/ (edited): mods/b wins (...)`, una sola línea por llave (en el informe del mod que gana; con `--validate-mod` de un solo mod, en el informe de cada mod que la comparte). Un archivo malo de la capa que pierde no se lee y por eso no sale: el juego lo salta.
  Compartir no es un error (un mod de recursos puede dar arte al compañero de otro mod), pero esos archivos se pisan archivo por archivo.
- **Arte y modelo:** un compañero nuevo reutiliza el arte 2D de su `base` mientras no traiga uno propio (§6.2f); su modelo 3D propio es §8.2c y su balance se compara con `tools/balance_compare.py` (§6.9).

#### Hacer un paquete de compañero (ejemplo: `pc/examples/mods/betamon-pack`)

Un paquete completo es un mod de contenido con estas piezas; `betamon-pack` las tiene todas y es la plantilla para copiar (cambia la llave de cada carpeta y los nombres):

1. **Las formas** (`digimon/<forma>/digimon.json`, una carpeta por forma): `"base"` es una forma de compañero original (da PV, estadísticas, técnicas y crecimiento por omisión) y `"model"` como objeto
   toma el modelo de un enemigo del disco **por referencia**, sin copiar nada del disco: `{"from": "dw3:digimon/148", "model": "dw3:digimon/10", "motions": "dw3:digimon/10", "effects": "dw3:digimon/374"}`.
   `"from"` es la plantilla de compañero (12 cámaras): elige una de **tamaño parecido** al modelo (una serpiente grande con la plantilla de un Digimon chico queda pegada a la cámara; `betamon-pack` usa Renamon para Betamon e Imperialdramon para
   Seadramon y MegaSeadramon). Los números salen de `<pref>/data/content/digimon.json` (la lista que escribe el juego desde tu disco): Betamon es `dw3:digimon/4`, Seadramon `10` y MegaSeadramon `61`. Con un modelo prestado,
   las ranuras de movimiento son las del enemigo, y las que el compañero pide de más (13, 20, 32) no existen en ese archivo: una ranura vacía hace que el juego lea basura (`getArchiveEntry` no revisa límites). Por eso
   `--validate-mod` avisa cada ranura que la batalla pide y el archivo deja vacía (la misma línea `motion slot N (...) has no animation` que un modelo horneado), y el objeto `"model"` acepta `"fill": <ranura 0..61>`: el port arma una copia del archivo
   de movimientos (un archivo virtual, sin tocar el disco) donde **toda** ranura vacía toca esa ranura del donante; `betamon-pack` usa `"fill": 14` (el ataque genérico). Sin `"fill"` las ranuras vacías quedan y el aviso se queda; si la ranura de `"fill"` también está vacía en el donante se rechaza con una línea.
   Si quieres animaciones propias para esas ranuras, hornea un modelo propio (§8.2c). Una forma de compañero con movimientos por referencia recibe siempre su propia copia del archivo, donde los movimientos que el enemigo deja «colgados»
   (el último paso es `0x8000`, que repite la cola para siempre: el ataque `14` de Betamon, Seadramon y MegaSeadramon) terminan: un guion de compañero espera a que cada movimiento acabe (`battle_script.c:168`) y se colgaba en el primer ataque.
   El reposo (0, 1) y la pose de entrada y victoria (12) siguen sosteniéndose.
   **Los efectos: el donante.** La animación de una técnica es el guion de la ranura `script` del archivo de efectos **del que la lanza**, no de la técnica; los efectos visuales y los sonidos sí son globales. Los archivos de efectos de un enemigo
   traen solo las ranuras 0-5 y sus guiones usan coordenadas del lado del enemigo (por eso el ataque de Seadramon con los efectos de su propio archivo se animaba contra sí mismo y una técnica de otra ranura no se animaba). Por eso `betamon-pack` toma `"effects"`
   de una **forma original de compañero afín**, el donante, y le da las técnicas que el archivo de ese donante anima (se prefieren las propias del donante, hechas para ese archivo): Betamon (etapa inicial) con Renamon `373`, Seadramon con Kyubimon `374`
   y MegaSeadramon con Taomon `375` (cadena de hielo y rayo; el modelo, los movimientos y las técnicas de agua siguen siendo del enemigo y de la serie). Las técnicas propias del enemigo (`225`, `252`, `291`, todas guion 5) no tienen nombre ni son aptas
   tal cual para un compañero (cuestan 0 PM): `content/techs.json` las clona con nombre, costo y poder propios (`base` es la original) y el guion 5 de la forma (el del ataque normal y el de esas técnicas) es el **propio del enemigo**, espejado al lado del compañero con `"scripts"` (siguiente párrafo); la firma de la etapa inicial (`skills.6`, `spark-bubble`) es un clon de la técnica 225 del enemigo.
   **Guiones propios: `"scripts"`.** El objeto `"model"` acepta `"scripts": {"<ranura 0..17>": {"from": "dw3:digimon/<id>", "slot": <0..17>}}`: esa ranura del archivo de efectos de la forma (el donante de `"effects"`) pasa a ser el guion de otro luchador original.
   El port arma una copia del archivo de efectos (un archivo virtual, el disco no se toca) con esas ranuras cambiadas; las demás siguen siendo del donante. Si el luchador de `"from"` es del **otro lado** (un enemigo para una forma de compañero, o al revés), el guion se convierte
   al lado de la forma: el tipo de modelo, las posiciones, los giros y las cámaras por id se espejan, y las posiciones de la mitad del lanzador siguen su lugar (altura y distancia de la fila de la forma); el diseño y los comandos están en `docs/formats.md` §4.6 punto 5c. Del mismo lado se copia tal cual.
   `betamon-pack` toma así el guion 5 de Betamon (`4`), Seadramon (`10`) y MegaSeadramon (`61`): la burbuja de agua de Seadramon y los rayos de Betamon y MegaSeadramon salen de la forma hacia el enemigo con la coreografía y las cámaras del juego, tanto en el ataque normal como en la técnica
   clonada (`spark-bubble`, `tidal-arrow`, `thunder-justice`, todas de guion 5). `--validate-mod` da una línea de error por entrada que no se puede cumplir, y esa ranura sigue siendo del donante: una ranura que la fila no tiene (`unknown slot`), una ranura vacía en el luchador de origen
   (`has no script in slot N`) y un comando que no se puede espejar (una cámara con vista propia, o la cámara 4). Los guiones 6 y 8, que el código usa **en vez del 5** con un efecto de acción, un arma de la familia del rival o el golpe triple (`techs.c:50-103`), no se tocan (el paquete los deja del donante: en
   esos casos raros la forma anima como el donante); si quieres que mantengan la coreografía, repite la entrada con las ranuras 6 y 8 (`"6": {"from": ..., "slot": 5}`), sabiendo que el guion de enemigo no lee el efecto de la acción.
   Una etapa inicial solo usa su ranura 6 (`tech_menu.c`: la forma que es la fila base del compañero lista solo `skills[6]`).
2. **El compañero** (`partners/<llave>/partner.json`): `"base"` (un compañero original: su arte y su bit de equipo mientras no tengas propios), `"firstForm"` (la primera forma, la de arriba), `title`/`description` por idioma y
   `"evolution"`: una entrada por forma que aprende, con sus requisitos (`needs`: la forma anterior a cierto nivel de forma, y `stat`/`value`: por ejemplo `"level"` >= 10, el nivel del compañero). Se aprende **tras una batalla** donde el compañero ganó
   experiencia (el panel del informe lo dice), una forma por informe. Los valores de `betamon-pack` son del orden de los de la lista original (que pide nivel de compañero 5, 15, 20 o 40 y nivel de forma 5 a 99).
3. **El arte** (`partners/<llave>/portrait.png`, `icon.png`, `field/`, `training/`: §6.2f y §6.9b). `betamon-pack/tools/make_art.py` lo genera desde modelos de SpriteKiln (solo herramienta de arte: el port no depende de ella; ver «Arte 2D con SpriteKiln» abajo); quien prefiera dibujar a mano pisa los PNG uno por uno sin tocar el JSON.
4. **El equipo inicial** (`content/teams.json`): tres compañeros por nombre; aparece como una fila más de la pantalla de elección.
5. **Cómo se consigue sin elegir ese equipo** (`stages/s330/stage.json`, `tables/special_conditions.json`, `tables/field_encounters.json`, `content/items.json`): una **misión de DDNA**, con la forma de las del juego (`wstag375`/`wstag450` Veemon, `wstag405` Agumon, `wstag420` Guilmon, `wstag330` Patamon y Renamon; el registro de la tarea P16 del paquete tiene las citas). El patrón original: (a) un personaje que entrega aparece en una **ventana de historia** (una fila de rango de progreso, primitiva 3) y solo pide la misión mientras la **suma de los niveles del equipo** está en una ventana (primitiva 1, op 5: llega a `arg * 15 + 30`; los originales usan 30-59, 45-74 o 60-89; fuera de la ventana dice una línea de «estoy ocupado»); (b) al pedirla pone una bandera; (c) un Digimon aparece mientras la bandera está y el DDNA no, y al hablarle empieza una **batalla de evento** (`kind` 2, sin huida) contra su etapa más avanzada; (d) al ganar recibes el DDNA (un objeto `content/items.json` cuya `base` es un DDNA original, `dw3:item/35`, con la señal de objeto recibido); (e) el que entrega, al verte con el DDNA y con el compañero aún bloqueado, lo desbloquea (la fila de la op 3) **sin quitarte el DDNA**. En el mod: el pescador es `fisher` y el Digimon `river-digimon` (`sprite` propio), las ventanas son tres filas (`story-window`, `levels-min`, `levels-max`), la bandera es `["mod-flag", "betamon-pack:quest", 1]`, la batalla es `["start-battle", "betamon-pack:megaseadramon", 1, <música>]` y el resultado se lee con `["mod-flag", "dw3:battle-won/betamon-pack/megaseadramon", 1]`. Dos diferencias con el original: los actores se evalúan cuando se arma el escenario, así que la línea del pedido termina con `["warp", 330, x, y, dir]` al mismo escenario para que el Digimon aparezca (el original cambia de escenario); y no hay escena antes de la batalla ni evento que entregue el DDNA al volver (lo da una segunda charla con el Digimon).
6. **Balance:** `python3 -I tools/balance_compare.py "<disco>.bin" pc/examples/mods/betamon-pack --level 30` imprime cada forma contra los ocho originales; en `betamon-pack` cada estadística de cada forma queda dentro del rango de los originales
   (ajusta `hp`, `stats` y `statGrowth` de cada `digimon.json` hasta lograrlo).
7. **Probar:** `boot "<disco>.bin" --validate-mod <mod>` (0 errores); para ver la forma en batalla sin jugar hasta evolucionar, `DW3_STARTER_SET=<mod>:<equipo>`, `DW3_SEED_PARTNER="<n>.0=<mod>:<forma>@20/0"` y `DW3_BATTLE_ENEMY=63` (§6.9, «Qué hace una forma nueva hoy»). Para jugarlo: `pc/review.sh betamon` (primera evolución), `seadramon` (segunda) y `betamonpesca` (el pescador), con copia propia del paquete y partidas aparte.

#### Arte 2D con SpriteKiln (opcional)

[SpriteKiln](https://github.com/LucasJRamos/spritekiln) (MIT) es una herramienta local de Python (numpy, Pillow y PyYAML; sin GPU) que dibuja un personaje como un modelo en YAML y lo renderiza a pixel art en varias direcciones y animaciones. Es una forma cómoda de hacer el arte 2D de un compañero (retrato, ícono, sprite del campo, gimnasio) o de un personaje de escenario sin dibujar cuadro por cuadro. **Es opcional**: el port solo lee los PNG y los JSON; el pixel art hecho a mano o con cualquier otro programa funciona igual, y SpriteKiln nunca hace falta para jugar ni para validar un mod.

Instalación (una vez, fijada a la versión que usa el ejemplo, en un entorno aparte):

```
python3 -m venv ~/.cache/spritekiln-venv
~/.cache/spritekiln-venv/bin/pip install spritekiln==0.3.0
```

Flujo, con `betamon-pack` de ejemplo:

1. El modelo es un YAML en `tools/specs/` (`betamon.yaml`: cuerpo, cabeza, patas y cresta como formas, paletas por material, animaciones por canales). Los demás YAML de esa carpeta lo extienden y solo cambian la cámara, el tamaño y las poses: `betamon_portrait.yaml`, `betamon_icon.yaml`, `betamon_field.yaml`, `betamon_train.yaml` y `megaseadramon_river.yaml` (el sprite de escenario del río).
2. `~/.cache/spritekiln-venv/bin/python pc/examples/mods/betamon-pack/tools/make_art.py` renderiza, recorta cada cuadro, junta los colores hasta 15 más el transparente y escribe los PNG indexados y los JSON (`sprite-v1`, `partners-training-v1`) con las medidas exactas que pide el juego. Es determinista: dos corridas dan los mismos bytes (`tools/test_make_art.py`).
3. Para cambiar el arte se edita el YAML (o la cámara de la vista) y se corre otra vez; `--validate-mod` confirma tamaños y colores.

Lo aprendido haciéndolo: (a) el sprite del campo debe tener una huella parecida a la de los seguidores originales (unos 20-26 x 23-31 px): se baja con `camera.scale` en el YAML del campo, no con el modelo; (b) las piezas y canales con sufijo `_L` se espejan solas a `_R`; (c) las expresiones de los canales son de tipo Python (`sin(p)`, `abs(...)`, comparaciones como `(f==1)`), no de C: sin `||`; (d) un retrato son celdas de 32 px de alto y el ícono mide 44x28: `make_art.py` falla con un mensaje si el modelo no entra, y entonces se sube `camera.scale`; (e) `spritekiln validate <yaml>` revisa un modelo antes de renderizar. No ejecutes `spritekiln update` (cambia la versión fijada).

### 6.10 Contenido nuevo: cartas

Un mod `"kind": "content"` agrega **cartas nuevas** con una carpeta por carta, `cards/<llave>/`: un `card.json` (formato `cards-card-v1`) y, si quieres arte propio,
un `face.png`. El juego recorre `cards/` de cada mod una vez al arrancar; una carpeta sin `card.json` no es una carta. La carta se llama `<carpeta del mod>:<llave>`; el port le da un id a partir de 317, en el orden alfabético del nombre completo, y ese número
no se guarda nunca (la partida guarda el nombre: sección `EXTK`).

```json
// mimod/cards/carta-prueba/card.json (+ mimod/cards/carta-prueba/face.png, opcional)
{
  "format": "cards-card-v1",
  "base": "dw3:card/120",
  "ap": 45, "hp": 40, "rank": 100, "price": 120,
  "title": { "eng": "Test Card", "es-419": "Carta de ejemplo" },
  "description": { "eng": "A new card from a mod." }
}
```

- Cabecera: `"format": "cards-card-v1"` (obligatorio) y `"name"` (opcional; si está, es el nombre de la carpeta, la llave; `"$schema"` y `"notes"` se ignoran).
  El resto de los miembros son los campos de la carta, en el mismo objeto. La carpeta `c<nnn>` **no** es una carta nueva (ver Arte). El `card.json` que el juego exporta de una carta
  original (`<pref>/data/cards/c<nnn>/card.json`, mismo nombre de formato) es descriptivo y nunca se lee: lleva `id`, `number` y `disc`, que un `card.json` de mod no usa.
  Un mod no puede cambiar la ficha de una carta original desde `cards/c<nnn>/card.json` (el juego no tiene ese parche; sus precios, tiendas, sobres y efectos se cambian con las tablas, §6.5b).
- `"base"` (obligatorio): una carta original (1..314) por nombre (`"dw3:card/120"`) o por número; de ella se copian la ficha (color, AP, HP, tipo,
  puntos, nivel, rango y carta de combo) y, en las cartas 1..60 (las de opción), su efecto. Los nombres originales: `<pref>/data/content/cards.json`
  (`content-ref-v1`, lo escribe el juego desde tu disco; sigue existiendo porque el `card.json` exportado no trae nombres ni fichas).
- Campos: `color` 1..6, `ap` y `hp` 0..99, `kind` 0..16 (1..14 opción, 16 Digimon), `points` 0..99, `level` 0..9, `rank` 0..999, `comboCard` 0..314.
  Un valor fuera de rango se rechaza con una línea de log y la carta conserva el de su base.
- `"effect"` (cartas de opción): `{ "like": 1..60, "condition", "playCondition", "message", "target": 0..255, "script": [pasos 1..174, hasta 39] }`.
  `like` va primero y los demás campos lo modifican; un `script` inválido se rechaza entero. Hay 175 pasos de efecto en el juego; no se pueden crear pasos nuevos.
- `"title"` (el nombre de la carta, con `eng`) y `"description"` (el texto del efecto, en `CARDST`) por idioma, como el nombre y la descripción de los objetos. Se llama `title`
  porque `name` es la cabecera (la carpeta).
- **Arte:** `<mod>/cards/<llave>/face.png` junto a su `card.json`, PNG indexado de 32x32 con hasta 256 colores (índice con transparencia = transparente, negro opaco se escribe 0x8000);
  sin archivo la carta usa el arte de su base. Una carpeta `c<nnn>` es la de una carta original (§6.2b), nunca una carta nueva: ahí solo se lee `face.png` y su `card.json`, si lo hay, no se lee.
  `--validate-mod` revisa cada `card.json` con el cargador del juego (cabecera, `name`, campos obligatorios y desconocidos, llave válida, rangos y el tamaño de `face.png`: una línea por caso)
  y avisa de una carpeta de `cards/` sin `card.json` (y que no sea `c<nnn>`), de un `card.json` en un mod que no es `"kind": "content"`, de un `card.json` dentro de `c<nnn>`, de un `c<nnn>` fuera de
  `c001`-`c316`, de un `c<nnn>` sin `face.png` y de un archivo suelto en `cards/`.
- **Dónde se ve:** el álbum crece (los ids 315 y 316 salen como huecos vacíos entre las originales y las nuevas), el editor de mazos las lista cuando las tienes,
  y el combate las dibuja y juega con su ficha y su efecto. `<carta>` en mazos y colección se guarda por nombre; al quitar el mod la carta se descarta al cargar
  (línea `[saves] card "..." is not defined by any installed mod`) y su lugar en un mazo vuelve a la carta del mazo inicial.
- **Precio y tiendas:** `"price"` (0-32767, 1 si falta) es lo que cobra la tienda de cartas por una copia. Para venderla, una tabla del mismo mod la lista por nombre:
  `tables/card_shop_stock.json` (`{"49": {"cards": {"add": ["mi-mod:mi-carta"]}}}`), y para que salga de los sobres `tables/card_pack_slots.json` (`{"910": {"card1": "mi-mod:mi-carta"}}`);
  `tables/card_opponents.json` y `tables/default_deck.json` la ponen en el mazo de un rival o en el mazo por defecto. La tienda dibuja la carta con su arte, su nombre y su precio.
- **Límites:** hasta 707 cartas nuevas (ids < 1024). Los ids 315 y 316 originales no son cartas de juego: ninguna tabla los acepta y en el álbum salen como dos huecos vacíos
  (iguales a cualquier carta sin ver), así que el álbum sin mods se ve idéntico y con mods las cartas nuevas empiezan en la ranura 317.
- El validador (`--validate-mod`) revisa cada `card.json` con las mismas reglas y `docs/modding/schema/card.schema.json` da el autocompletado.

### 6.11 Personajes y charlas de un escenario

`stages/s<nnn>/stage.json` (`s<nnn>` es el escenario, el mismo archivo de `data/stages/s<nnn>/stage.json`, que es el `stage-v1` de referencia; el parche conserva su encabezado `stage-patch-v1`) cambia, agrega o quita los personajes de un escenario del campo y las charlas de cada uno. Es el formato
`stage-patch-v1`; el detalle (campos, orden, conflictos, lo que se omite) está en `docs/stages.md` §7 y el esquema en `docs/modding/schema/stage-patch.schema.json`. Ejemplo: `pc/examples/mods/training-field`.

```json
{
  "format": "stage-patch-v1",
  "actors": {
    "patch": [{"index": 0, "x": 850, "y": 950}],
    "add": [{"key": "greeter", "character": 43, "image": 6, "x": 950, "y": 950, "dir": 3,
             "talks": [{"entry": 636, "conditions": [["mod-flag", "mimod:met", 1]]},
                       {"entry": 631, "actions": [["mod-flag", "mimod:met", 1]]}]}],
    "remove": [5]
  }
}
```

- Un personaje se nombra por su `index` en la lista del escenario (míralo en `data/stages/s<nnn>/stage.json`, que el juego escribe desde tu disco) o por la llave del que un parche agregó (`"<mod>:<llave>"`).
- `patch` cambia campos (`character`, `image`, `x`, `y`, `dir`, `conditions`, `talks`, `prependTalks`); `add` crea personajes nuevos (reusa un `character` y una `image` que el escenario ya tiene); `remove` los quita.
- Una charla es `{"entry": N, "conditions": [...], "actions": [...]}`: `entry` es una línea del texto del propio escenario (o `"text": "<mod>:<nombre>"`, un texto propio del mod, ver abajo), se dice la primera cuyas condiciones se cumplen y la
  última no lleva condiciones. Las condiciones y acciones usan el vocabulario de `docs/stages.md` §4: `["flag", "1A:1F", 1]`, `["item", "dw3:item/18", 1]`...
- **Banderas propias:** `["mod-flag", "<mod>:<nombre>", 1]` (condición: está puesta; acción: ponerla), `["mod-var", "<mod>:<nombre>", ">=", 3]` y `["set-var", "<mod>:<nombre>", 3]` para contadores.
  Se guardan en la partida por nombre: quitar el mod y volver a ponerlo no pierde el progreso.
- **Preguntas de sí o no:** `["ask", "<mod>:<nombre>"]` en las `actions` de una charla: cuando se dijo la línea, el campo muestra el panel Sí/No del juego y la variable vale 1 (sí) o 2 (no); después las charlas del personaje se eligen otra vez, así que la siguiente lee la respuesta con `["mod-var", ..., "==", 1]`. La pregunta es la línea de la charla. `docs/stages.md` §17.
- **Resultado de una batalla de `start-battle`:** al volver al campo el puerto deja `dw3:battle-won/<clave>` y `dw3:battle-played/<clave>` (`<clave>` = el nombre del encuentro con `/` en vez de `:`, p. ej. `dw3:battle-won/training-field/duel-trio`, o el número en hex). Perder lleva a la pantalla de título: no queda nada que leer.
- **Textos propios:** una charla puede decir `{"text": "<mod>:<nombre>", ...}` en vez de `entry`. Las palabras van en `text/<idioma>/stages.json` (formato `stage-text-v1`, una carpeta por idioma; el inglés es obligatorio y un idioma que
  - **La cabecera de hablante del juego:** un texto puede empezar con `{speaker-of:<ARCHIVO>:<entrada>}` (ARCHIVO es un archivo de texto del juego, p. ej. `TALK04`): el puerto pone ahí la cabecera que esa entrada tiene en el disco del jugador, en el idioma que se juega (los letreros del juego dicen su nombre en `TALK04` entrada 19), así que el mod no copia texto del juego. Archivo desconocido o prefijo mal formado: se rechaza el `stages.json` de ese idioma. `docs/stages.md` sección 8.
  no tiene el nombre muestra el inglés, es-419 prueba antes el español): `{"format": "stage-text-v1", "texts": {"bienvenida": "..."}}`. El puerto agrega la línea al texto del escenario y la ajusta a la caja de charla. Detalle y esquema: `docs/stages.md` §8, `docs/modding/schema/stage-text.schema.json`.
- **Condiciones propias:** una fila nueva en `tables/special_conditions.json` con la llave `"<mod>:<nombre>"` (ver `docs/data-tables.md` §3.9) se usa como `["special", "<mod>:<nombre>", 1]`; también están las tablas de dinero, rangos de progreso y umbrales del grupo.
- **Acciones nuevas:** `["give-money", 500]`, `["take-money", 500]`, `["set-progress", 12]`, `["warp", 380, 1186, 874, 3]` (escenario, x, y, dirección) y `["sound", "teleport"]` (`docs/stages.md` §9). Dar o quitar un objeto de un mod ya se podía: `["item", "<mod>:<nombre>", 1]`.
- **Señal de objeto recibido (premios y misiones).** El original no usa música ni un mensaje extra al entregar algo: su señal es el **ícono que sigue al jugador** (el "satélite", tarea `0x16` del campo), que destella
  con el sonido de objeto obtenido. Una charla lo pide con la acción `["special", "0x13", 1]` (la condición especial `0x13`, que el juego llama `showActorIcon`), y **va al final** de la lista de acciones, después del objeto, el dinero y las
  banderas: las acciones corren al cerrarse el texto, y así el destello sale cuando el premio ya está dado. Ejemplo (el de `training-field`):
  `"actions": [["give-money", 300], ["item", "dw3:item/47", 1], ["set-var", "mimod:mision", 2], ["special", "0x13", 1]]`. Nada más: no pongas `["sound", ...]` ni otra música para señalar un premio, porque el original no lo hace.
  En un guion de evento (`events`) el mismo destello es `["command", 813, 842, 2]` (el comando 842, `0x34A`, es "subestado 3 del ícono"; `["command", 813, 824, 2]` es el subestado 1, que también suena, y `813, 825` el 2, que no). No hay
  alias con otro nombre: son los números del juego. Funciona en un escenario original parchado (`stages/s330/stage.json`) y en uno nuevo (`stages/<nombre>/`). El ícono solo existe en los escenarios cuyo
  `fallback.stage` es de antes del arranque de partida nueva (`MODE_NEW_GAME`, modo `0x2D7`: el escenario 330 sirve, el 880 no); si no existe, la señal no hace nada y deja una línea `[field]` en el log (antes cerraba el juego).
- Varios mods pueden parchar el mismo escenario: se aplican en el orden de carga y el último gana (el log dice qué campo se pisó). Un `index` que no existe al progreso actual de la historia se omite con una línea
  de log. Sin parche, el escenario es idéntico al original. `--validate-mod` revisa el archivo.
- **Personajes y sprites propios** (`docs/stages.md` §11): un personaje nuevo sin `image` recibe un espacio de VRAM propio, y puede ser cualquier `character` del juego o un `"sprite": "<nombre>"` de `mods/<mod>/sprites/<nombre>/`; lo que no cabe se quita con una línea de log.
- **Salidas, eventos y batallas** (`docs/stages.md` §10): además de `actors`, el archivo puede traer `slots`, `events` y `battles`.
  - `slots` (`patch` por `index` o `key`, `add`, `remove`): una salida lleva `type`, `arg`, `x`, `y`, `dir` y condiciones; `"stage": 380` apunta la salida a ese escenario y `"event": "<mod>:<nombre>"` a un evento. Una salida nueva lleva `"area": {"x", "y", "w", "h"}`, un rectángulo del mapa en píxeles donde se dispara (este mapa es el del escenario original: se parcha, no se redibuja; para un mapa propio usa §6.11b).
  - `events` (`patch` por `id`, `add` con llave): `script` con los comandos del juego (`["wait", 20]`, `["talk", 0, "<mod>:<texto>", 0, 4]`, `["walk", ...]`, `["leave", ...]`, siempre terminando en `["end"]`). Una charla o una salida lo empiezan: `["run-event", "<mod>:<nombre>"]`. Un evento nuevo no tiene código de C.
  - `battles` (`patch`, `add`): por área del mapa, `rate`, y por batalla `encounter` (una fila de `field_encounters`), `stage` (escenario de lucha) y `music`; mira `battles` en `data/stages/s<nnn>/stage.json`. La acción `["start-battle", encuentro, escenario_de_lucha, musica]` empieza una desde una charla.

### 6.11b Tu primer escenario nuevo

`stages/<nombre>/` agrega un escenario de campo entero: un mapa que dibujas tú, su colisión, sus salidas y sus batallas. Es el formato `stage-new-v1`; el detalle (cada campo, cada rechazo, lo que se probó y lo que no) está en `docs/stages.md` §13 y el esquema en `docs/modding/schema/stage-new.schema.json`. El escenario se llama `<mod>:<nombre>` (el nombre de la carpeta) y así lo nombran las salidas de otros escenarios (`"stage": "<mod>:<nombre>"`) y la acción `warp`.

```
stages/patio/
  stage.json       la cabecera y las secciones (como un stage-patch-v1, solo "add")
  background.png   el dibujo del mapa: PNG indexado (hasta 256 colores), ancho y alto múltiplos de 128 px, a lo más 255 mosaicos de 128 x 128
  floor.png        colisión (obligatorio): del mismo tamaño, un píxel por píxel del mapa, el índice del PNG es el valor: 0 cerrado, 1 plano, 2-7 y 16-23 pendientes
  triggers.png     (opcional) salidas y disparadores: 0 nada, si no dirección * 32 + número de ranura (0 a 31) de "slots"
  areas.png        (opcional) zonas de combate: 0 sin combate, n es la n-ésima área de "battles"
```

```json
{
  "format": "stage-new-v1",
  "fallback": {"stage": 330, "x": 900, "y": 1000, "dir": 0},
  "start": {"x": 128, "y": 128, "dir": 0},
  "soundBank": 9, "music": {"bank": 9, "track": 0},
  "slots": {"add": [{"key": "salida", "stage": 330, "x": 900, "y": 1000, "dir": 0, "area": {"x": 100, "y": 100, "w": 56, "h": 56}}]}
}
```

- `start` es donde aparece el jugador sin una salida que lo diga; `fallback` es el escenario del juego adonde va una partida guardada ahí si quitas el mod (por defecto, el Parque Central).
- `banner` (opcional): `{"area": "<mod>:<texto>", "place": "<mod>:<texto>"}`, las dos líneas del letrero con el nombre del lugar que ve el jugador al entrar al escenario (la grande y luego el sitio, como "Asuka City" y "Weapons Shop"). Cada una es el nombre de una línea de `text/<idioma>/stages.json` (textos con nombre de §6.11), así que se traduce por idioma (`eng` es obligatorio, otro idioma cae a él). Sin `banner` el escenario no muestra letrero. Sigue la opción de banner (Rápido o Clásico) igual que los escenarios del juego. Ejemplo: `"banner": {"area": "mi-mod:patio-area", "place": "mi-mod:patio-lugar"}`.
- `dw3:card-won/<oponente>` y `dw3:card-played/<oponente>` (el oponente de una acción `card-battle` en hex en minúsculas, `"13:0"` -> `13`) son variables que el puerto fija al terminar una partida de cartas (se guardan con la partida); se leen con `mod-flag`. Las banderas del propio juego 00:10/00:11 son temporales y se borran al empezar el escenario siguiente, así que una misión debe leer estas (`docs/stages.md` §16).
- Las secciones `actors`, `slots`, `events` y `battles` son las de §6.11 pero solo con `add` (un escenario nuevo no tiene nada que parchar). Una salida puede dispararse por celdas de `triggers.png` (el número de ranura es su lugar en `slots.add`, desde 0) o por un `area` rectangular.
- Un escenario que no es válido se rechaza entero con una línea `[stages] <archivo> rejected: <motivo>` y nunca cuelga el juego; `--validate-mod` muestra esas líneas. Los mapas se leen una vez al inicio.
- `battles` y la acción `start-battle` aceptan `"encounter": "<mod>:<nombre>"` de un encuentro nuevo de `tables/field_encounters.json` (§6.5b), con sus propios enemigos y recompensas: así el escenario no cambia las zonas originales.
- Ejemplo completo: `pc/examples/mods/training-field` (dos pisos, guía en el Parque Central, encuentros calibrados). Una carpeta `tools/` dentro del mod es para tus propios scripts (el ejemplo trae el que dibuja su arte): el juego no la lee y `--validate-mod` no la avisa.

**Receta paso a paso** (el ejemplo completo y funcional es `pc/examples/mods/training-field`; copia su estructura):

1. **Carpeta:** `mods/mi-mod/stages/patio/` con `stage.json`, `background.png` y `floor.png` (los otros dos PNG son opcionales). El mod necesita su `mod.json` como cualquier otro (§4).
2. **Dibujo:** `background.png` indexado, de `ancho x alto` múltiplos de 128 (por ejemplo 1024 x 768 = 48 mosaicos; máximo 255 mosaicos). Con muchos bloques distintos el escenario se rechaza con el motivo del códec.
3. **Capas** (mismo tamaño que el fondo, un píxel de la capa por píxel del mapa; el valor es el **índice** del PNG indexado, no el color):
   - `floor.png`: 0 cerrado (no se camina), 1 abierto y plano, 2-7 pendientes y 16-23 pendientes espejadas (`docs/formats.md` §4.3). Otro valor se rechaza.
   - `triggers.png`: 0 nada; si no, `dirección * 32 + ranura`, con dirección 1-7 y ranura 0-31 (su lugar en `slots.add`, desde 0).
   - `areas.png`: 0 sin combate; `n` es la `n`-ésima entrada de `battles.add` (desde 1).
4. **Cabecera:** `start` (obligatorio, dentro del mapa y sobre piso caminable), `fallback`, `soundBank` y `music`, como en el ejemplo de arriba.
5. **Encuentros propios:** en `tables/field_encounters.json` agrega filas `<mod>:<nombre>` con sus enemigos y recompensas (§6.5b) y nómbralas en `battles.add` (`"encounter": "mi-mod:prado"`). Así no cambias ninguna zona original.
6. **La entrada:** un personaje en un escenario del juego (`stages/s330/stage.json`, §6.11) cuya charla tiene una `conditions` (por ejemplo `["item", "dw3:item/9", 1]`) y la acción `["warp", "mi-mod:patio", x, y, dir]`. La salida de vuelta es una ranura de `slots.add` con `"stage": 330` y su `x`, `y`.
7. **Comprobar:** `boot "<disco>.bin" --validate-mod mi-mod` debe terminar con `ok` y código 0 (§2c). Los mapas se leen una vez al arrancar: reinicia el juego tras cada cambio.

**Errores comunes** (cada uno sale como `[stages] <archivo> rejected: <motivo>` y el escenario se rechaza entero; nunca cuelga el juego):

| Síntoma | Causa habitual |
|---|---|
| tamaño rechazado | ancho o alto que no es múltiplo de 128, o una capa de otro tamaño que el fondo |
| valor de piso sin sentido | un índice distinto de 0-7 y 16-23 en `floor.png` (revisa que el PNG sea indexado y que el índice, no el color, sea el valor) |
| ranura o área que no existe | `triggers.png` nombra una ranura que `slots.add` no tiene, o `areas.png` un `n` mayor que `battles.add` |
| `there is no field encounter named` | el encuentro de `battles` o de `start-battle` no está en `tables/field_encounters.json` |
| `start` fuera del mapa | el aviso de `start` sobre piso 0 no rechaza, pero deja al jugador dentro de un muro: ponlo en piso 1 |
| una salida a un escenario rechazado | rechaza también al escenario que la tiene: arregla primero el escenario de destino |

Preguntas abiertas y salvedades (sin verificar con el juego): `soundBank` inexistente (no suena nada), el bit 15 de la paleta (convención de PS1), y que los escenarios de mod no salen en el mapa del mundo.

#### Alturas y rampas

El juego no tiene altura al caminar: una **pendiente** es un valor de `floor.png` (2 a 7, y 16 + n espejado) que cambia el paso de la caminata para que el jugador siga la inclinación (`docs/formats.md` §4.3). La altura la pone tu dibujo. Receta (la usa `training-field:lower`, una meseta con una rampa a cada lado, `tools/make_art.py`):

1. Elige la pendiente: el valor 3 hacia el este avanza `(3939, -819)`: 0,2079 px hacia arriba por px hacia el este. Una meseta de 32 px de alto se sube con una banda de 154 px de largo (32 / 0,2079).
2. Dibuja la banda inclinada (escalones, bordes) y pon **exactamente** esa banda con el valor 3 en `floor.png`; el valor 19 (16 + 3) es la misma pendiente espejada: caminar al este baja, para la rampa del otro lado.
3. Los lados de la rampa y el acantilado son muros (valor 0): el jugador que corre no los cruza. Abajo de la rampa, un triángulo de muro (hasta el pie del acantilado) da el aspecto de cuña.
4. Pruébalo sin ventana con `fieldwalk:<x>:<y>` (ruta por estado) y mira `floor cell` en el log (`docs/stages.md` §15).

No cubierto por mods todavía: un segundo piso de capa (`SLOT_MAP`) y subir muros con el pad (`SLOT_CLIMB_*`, `SLOT_DROP`); no los uses sin probarlos.

#### Pesca y árboles para patear

Dos mecánicas del juego que tu escenario (nuevo o parchado) puede llenar con **tus** Digimon (detalle y lo que dice el código: `docs/stages.md` §14).

**Pesca.** Un slot `gauge` es un punto de pesca (necesita la Caña de pescar, `dw3:item/5`: un `gauge` agregado la pide solo; `"conditions": []` quita la condición). El cursor del medidor se detiene en la celda 0 (nada), 1 o 2. Cada celda se llena con una lista de resultados, uno se elige al azar (con `weight` opcional):

```json
"slots":   {"add": [{"key": "laguna", "type": "gauge", "area": {"x": 400, "y": 600, "w": 200, "h": 120}}]},
"fishing": {"slots": [{"key": "laguna",
    "1": [{"encounter": "mimod:anguila", "stage": 1, "music": 1611137024, "weight": 3}, {"item": "dw3:item/18"}, {"nothing": true}],
    "2": {"encounter": "mimod:lubina", "stage": 1, "music": 1611137024}}]}
```

`"fishing": {"default": {"1": [...], "2": [...]}}` vale para todo slot `gauge` del escenario sin entrada propia. Una celda sin lista queda como en el juego. Cuál celda es verde y cuál amarillo lo dice el dibujo del medidor (hipótesis: la 2 es la larga y la 1 la corta con sonido); pruébalo en el juego.

**Árboles para patear.** Cada mapa tiene varios árboles y **uno solo** tiene al Digimon; el indicador dice qué tan cerca está. `hiddenSpots.add` pone árboles (`x`, `y` en píxeles del mapa) y `prize` cambia qué esconde el árbol del premio (`common` 80 %, `rare` 20 %); con varias entradas, el árbol esconde uno de varios Digimon. El jugador necesita el objeto 4 para patear. El premio del juego siempre es una batalla; aquí también puede ser un `item` o `nothing`.

```json
"hiddenSpots": {"add": [{"x": 700, "y": 400}, {"x": 812, "y": 520}],
    "prize": {"common": [{"encounter": "mimod:cardmon", "stage": 1, "music": 1611137024}, {"encounter": 20, "stage": 1, "music": 1611137024, "weight": 2}]}}
```

`--validate-mod` comprueba los nombres de encuentro y de objeto. Sin estas secciones, el escenario se comporta como siempre.

## 7. Ejemplos

Tres mods completos y una carpeta modelo en `pc/examples/mods/`, todos apagados al instalarlos: `pc/examples/install_examples.sh [--only training-field|sample-model|betamon-pack]
[carpeta de usuario]` los copia y los agrega a `[mods] disabled` (no genera nada desde tu disco: todo el arte es original). Cada uno tiene un `README.txt` con qué
cambia y dónde verlo; la guía de instalación (Windows, Android) está en `pc/examples/README.md`. Lo demás se explica con un fragmento mínimo en su sección
(tablas §6.5 y §6.5b, objetos §6.7, técnicas §6.8, Digimon §6.9, cartas §6.10, imágenes §6.2 y §6.3, audio §8.1d, video §8.1b).

| Mod | Qué enseña | Dónde verlo |
|---|---|---|
| `training-field` | escenarios nuevos (`stages/<nombre>/`: arte, capas, salidas, encuentros, pesca, árboles, duelo, misión), un parche de un escenario del juego (`stages/s330/stage.json`), un sprite nuevo (`sprites/sign/`), encuentros nuevos (`tables/field_encounters.json`) y textos por idioma (`text/<idioma>/stages.json`); una carpeta `tools/` con el generador del arte | Parque Central (330), junto a Leomon: hablar con el guía (con y sin la Medalla de Seiryu); caminar en la hierba de los pisos |
| `sample-model` | un Digimon enemigo nuevo con modelo 3D propio (`digimon/boxbot/`: `digimon.json` y `model/` con glTF, PNG y `model.json`) y su lugar en dos encuentros (`content/encounters.json`), y una forma de compañero con modelo propio (`digimon/boxpal/`, §8.2c) | batallas del Parque Central (encuentros 34 y 35) |
| `betamon-pack` | un compañero nuevo con tres formas, un cuarto equipo inicial, una misión de DDNA que lo desbloquea y arte generado con modelos de SpriteKiln (§6.9b, «Hacer un paquete de compañero») | pantalla de equipo inicial (equipo D); Parque Central (330), abajo donde llegas: el pescador (con la historia en progreso 14-38 y los niveles del equipo sumando 45-74) y la serpiente del río |
| `_template` | un archivo mínimo de cada tipo de mod (lo que copia `--new-mod`) | no se instala |

Sin ejemplo de archivo crudo (§6.4): no hay un cambio seguro y visible sin editar bytes sin documentación.

## 8. Herramientas

| Herramienta | Para qué |
|---|---|
| `tools/file_names.py "<disco>.bin"` | número de archivo, sector y ruta del disco (TSV a la salida estándar) |
| `tools/extract_disc.py "<disco>.bin" <salida>` | extrae todos los archivos del disco conservando rutas |
| `tools/image_atlas_preview.py <carpeta>` | dibuja las piezas de una pantalla (`menu.json`) o de los objetos de un escenario (`objects.json`) sobre sus páginas, o los cuadros y animaciones de un sprite (`sprite.json`; `--anim`, `--set`, `--dir`, `--gif`); necesita Pillow |
| `tools/image_mod_palette.py` | genera un mod de imágenes cambiando **solo la paleta** de PNG exportados (`--list`, `--to`, `--sprite`; rutas bajo `<data>` como `menus/stshop`, `cards/c001`, `partners/<nombre>/portrait.png`) |
| `tools/asset_dupes.py <data>` | informe de duplicados de una carpeta exportada: archivos y píxeles repetidos, hojas, texto (solo rutas y cuentas, `--json`); necesita Pillow |
| `tools/techcompat.py "<disco>.bin"` | qué luchador puede animar qué técnica (ranuras de animación; `--json`, `--fighters`); lee tu disco, no guarda datos |
| `tools/balance_compare.py "<disco>.bin" <mod>` | PV, PT, estadísticas y crecimiento de las formas de compañero del mod junto a los ocho originales y una proyección a nivel N (`--level`); lee tu disco, no guarda datos |
| `tools/model_export.py "<disco>.bin" <luchador> <salida>` | exporta un modelo de combate a glTF 2.0 con esqueleto y animaciones (`--all`, `--no-animations`, `--source`; el juego ya lo hace por sí mismo: ver §8.1) |
| `tools/model_import.py <fuente>.json --out <mod>` | convierte glTF + PNG + metadatos (`model-src-v1`) en un modelo de combate nuevo; para revisar una fuente o hornear fuera del juego, porque el port la hornea al cargar (ver §8.2) |

### 8.1 Exportar un modelo de combate a glTF

**Desde el juego (el camino normal).** El port exporta los modelos originales de **tu** disco a `data/digimon/d<nnn>/model/` (en la carpeta del usuario), uno por luchador
(`d383` es Kotemon: el número de `"dw3:digimon/<id>"` con tres dígitos), con cuatro archivos: `model.gltf` + `model.bin` + `model.png` + `model.json`. Es justo la
**fuente neutra de §8.2**: copia la carpeta `model/` tal cual a `mimod/digimon/<llave>/model/` y ya es el modelo de un Digimon de un mod (el `model.json` trae los movimientos
por ranura, el donante de efectos y la plantilla de ese mismo luchador, y la altura y la distancia de su `FighterInfo`). Solo se exporta si lo pides
(los 248 ocupan unos 200 MB y tardan unos 8 s):

- **Menú:** *Opciones > System > Prepare modding files* (§2b) exporta todos (sin pedir confirmación; el valor de la fila muestra `Models 12/248`).
  Corre en un hilo de trabajo (construye y escribe allí; el hilo del juego solo le pasa los archivos del disco), así que el juego mantiene sus cuadros por segundo; una segunda pulsación mientras corre no hace nada. Al cerrar el juego a mitad, el hilo se detiene entre dos modelos (los archivos se escriben de forma atómica).
- **`settings.ini`:** `[data]` / `export_models = all` o `export_models = 383, 12` los exporta al arrancar, antes de abrir la ventana. Un modelo ya exportado con la
  misma versión del exportador y con sus cuatro archivos en `data/digimon/d<nnn>/model/` se **salta sin construirlo** (la primera vez
  tarda unos 8 s para todos; los arranques siguientes, nada). Un archivo editado cuenta como presente y no se toca. Para
  rehacer uno, borra su carpeta; el botón *Prepare modding files* **no** reconstruye lo que ya está al día (borra la carpeta para rehacerlo).
- **Regla `.new`** (como el resto de `data/`): `data/digimon/manifest.json` recuerda lo que escribió el port (y en `names` qué luchador es cada carpeta: `"d012": "dw3:digimon/12"`). Un archivo que editaste se **conserva** y el nuevo queda
  al lado como `<archivo>.new`; uno sin tocar se reemplaza.
- **`data/content/digimon.json`** nombra la carpeta de cada luchador (`"model": "digimon/d<nnn>/model/"`), exista ya o no.
- **Escala de Blender:** el glTF sale a **1 m = 1000 unidades del juego** (Kotemon, de 1886, mide unos 1.9 m) y el `model.json` dice `"scale": 1000`: el
  importador lo deshace solo. No hace falta escalar a mano en Blender; para un modelo nuevo, trabaja en metros y pon en `model.json`
  `"fitHeight"` (o `"scale"`) para la altura que quieras en el juego.
- Es el mismo código que `tools/model_export.py --source` (C portátil; solo el hilo del menú usa los hilos de SDL): los `.gltf`, `.bin` y `.json` salen **byte a byte iguales** y el PNG con los mismos
  píxeles (lo comprueba `pc/modeltest.c`; el flujo deflate del PNG es el del port). Límite: el bucle de cada movimiento va en `extras` de la animación, no en `model.json`.
  Para no copiar la carpeta exportada: `"from": "dw3:model/<id>"` en el `model.json` del mod (§8.2).

**La herramienta de Python** hace lo mismo desde la línea de comandos (desarrollo y comprobaciones):

```
python3 -I tools/model_export.py "<disco>.bin" 383 salida/ --source   # salida/383/model.gltf ... model.json, a escala de Blender
python3 -I tools/model_export.py "<disco>.bin" --all salida/ --source # los 248
python3 -I tools/model_export.py "<disco>.bin" 383 salida/            # el formato plano: fighter_383.gltf en unidades del juego
```

Lo que escribe la herramienta sin `--source`:

- **Qué escribe:** por luchador `fighter_<id>.gltf` + `.bin` + `.png`: un esqueleto (un nodo por hueso, pose de reposo = primer
  cuadro del movimiento de reposo), la malla con cada vértice pesado 1.0 a su hueso, normales, UV en un atlas PNG (una celda de 256x256
  por combinación textura/CLUT; el color 0 es transparente, con alfa por texel) y una animación `motion_NN` por cada ranura de movimiento no vacía
  (muestreada a 60 cuadros por segundo). Solo usa la biblioteca estándar de Python; no necesita Blender para generarlo.
- **Abrirlo en Blender:** *Archivo > Importar > glTF 2.0* y elige el `.gltf`; las animaciones salen como acciones. El atlas se ve
  mejor con interpolación *Closest*. Los datos salen de **tu** disco y se escriben fuera del repo: es material del juego, no lo publiques
  (ADR-001).
- **Límites:** las piezas que el juego esconde con escala <= 48 (armas, extremidades según el movimiento) llevan escala 0, no se
  ocultan por otro medio; la iluminación la pone el visor (el juego no guarda colores de vértice); la semitransparencia del juego
  (`abr`) es por texel (solo los texels con bit STP se mezclan): sale como alfa 0.5 en esos texels del atlas y material `BLEND` solo
  en los polígonos que los tienen; el modo de suma (la mayoría) se aproxima con ese mismo alfa y se ve más oscuro que en el juego;
  las caras son de una sola cara como en el juego (`doubleSided: false`, orden antihorario), así que un visor sin descarte de caras
  mostraría el interior; la cara animada (parpadeo) no se exporta; los efectos de
  técnica tampoco; el bucle y el fin de cada movimiento van en `extras` de la animación. Es solo de lectura; el camino inverso es §8.2.
  Detalle del formato y de las aproximaciones: `docs/formats.md` 4.6.

### 8.1b Exportar las películas a AVI

Las 14 películas del disco (`AAA/STR/MOVIE*.STR`) salen de **tu** disco como un `.avi` cada una en `data/video/<NOMBRE>.avi` (en la carpeta del usuario), un formato abierto que
abre cualquier editor o reproductor. Solo se exportan si lo pides (los 14 ocupan unos 430 MB; con el disco tardan unos 2 minutos):

- **Menú:** *Opciones > System > Prepare modding files* (§2b) exporta las 14 (sin pedir confirmación; el valor de la fila muestra `Videos 3/14`). Corre en un hilo de
  trabajo con su propio manejador del disco, así que el juego mantiene sus cuadros por segundo y una película en marcha no se altera; una segunda pulsación mientras corre no hace nada. Al cerrar el
  juego a mitad, el hilo se detiene y la película a medias se descarta (el archivo `.avi.part` se borra).
- **`settings.ini`:** `[data]` / `export_videos = all` o `export_videos = MOVIED01, MOVIEOPN` las exporta al arrancar, antes de abrir la ventana (mayúsculas o minúsculas, con o sin `.STR`).
  Una película ya exportada con la misma versión del exportador y con su `.avi` en `data/video/` se **salta sin construirla** (el segundo arranque tarda medio segundo). Un archivo editado cuenta como
  presente y no se toca. Para rehacer una, borra su `.avi`; el botón *Prepare modding files* no reconstruye lo que ya está al día.
- **Regla `.new`** (como el resto de `data/`): `data/video/manifest.json` recuerda lo que escribió el port. Un `.avi` que editaste se **conserva** y el nuevo queda al lado como `<NOMBRE>.avi.new`; uno sin tocar se reemplaza.
- **Proporción:** los píxeles guardados son 320x416, pero el juego los muestra en un display de 640x416 (cada píxel de la PS1 es el doble de ancho que de alto). Cada AVI lo declara con un
  chunk OpenDML `vprp` (proporción de cuadro 20:13), así que un reproductor lo muestra a 640x416 sin tocar los píxeles. El lector de reemplazos de mods ignora ese chunk: un video
  propio puede ser de 320x416 con o sin `vprp` (o de otro tamaño, que se escala; ver más abajo).
- **Formato** (AVI 1.0, `idx1`; el mismo para todas): vídeo **MJPEG** (`MJPG`) de 320x416 a 15 cuadros por segundo, un JPEG base por cuadro (secuencial, 4:2:0, tablas Huffman estándar del anexo K de
  T.81, calidad 85, JFIF, sin marcadores de reinicio); audio **PCM de 16 bits** a 37800 Hz con los canales del XA del disco (estéreo en las 14 del disco EU). Cada cuadro lleva su audio justo antes
  (2520 muestras = 37800 / 15). El perfil es fijo y simple a propósito: lo lee cualquier decodificador y el propio juego lo reproduce cuando una película se reemplaza desde un mod (más abajo).
  4:2:0 porque el MDEC del disco ya guarda el color a media resolución (el color no pierde nada nuevo).
- **Sincronía:** la primera muestra de audio está en el segundo 0 y el cuadro `k` en `k / 15` s, la lectura normal de un AVI. (El reproductor del juego retiene el cuadro `N` hasta que han sonado `N * 2520`
  muestras, un cuadro más tarde; los archivos no copian ese retraso.) La duración del audio y la del vídeo difieren lo que difiere el disco: en `MOVIEE02..04` el audio acaba unos 3 s antes que la imagen.
- **Tamaño:** de 8 a 58 MB por película (unos 430 MB en total); AVI 1.0 llega a 4 GB y el exportador se niega a pasar de 2 GB (la más larga está lejos).
- Es código C portátil (`pc/shim/jpeg_write.c`, `avi_write.c`, `video_export.c`) que reutiliza el decodificador MDEC y el de XA del port (`host_mdec.c`, `host_xa.c`) sin tocar su estado de reproducción.
  Lo comprueba `pc/videotest.c` con un disco sintético (PSNR contra PIL, estructura RIFF e `idx1` con un analizador independiente en Python).

#### Reemplazar una película desde un mod

`mods/<mod>/video/<NOMBRE>.avi` (con `<NOMBRE>` = `MOVIED01` ... `MOVIEOPN`, la película del disco; se buscan `MOVIED01.avi`, `MOVIED01.AVI` y `movied01.avi`) se reproduce **en lugar** de
`AAA/STR/<NOMBRE>.STR` y de su audio XA. Gana el último mod activo que tenga el archivo. Sin ese archivo la película del disco suena y se ve exactamente igual que antes. El juego no cambia:
sigue creyendo que lee su `.STR`; el port le entrega los cuadros del AVI en su lugar. El punto de partida natural es un archivo exportado por §8.1b: edítalo y ponlo en el mod.

- **Qué acepta:** un AVI con vídeo **MJPEG** y, si lleva, audio **PCM de 16 bits** (mono o estéreo). El perfil exacto del exportador (MJPEG base 4:2:0, PCM estéreo a 37800 Hz, 320x416, 15 cuadros/s) y lo que
  produce ffmpeg: JPEG base `yuvj420p`, `yuvj422p`, `yuvj444p` o gris, con o sin tablas Huffman (sin ellas valen las estándar del anexo K, como en un AVI1), con marcadores de reinicio, con o sin JFIF.
  El AVI se lee recorriendo sus bloques, así que sirve con `idx1` ausente o torcido, con `JUNK`, con listas `rec ` y con un archivo cortado (valen los cuadros anteriores al corte).
  No acepta JPEG progresivo ni aritmético, ni audio de 8 bits, flotante o comprimido, ni un archivo de más de 2 GB; un AVI OpenDML (ffmpeg lo escribe pasado 1 GB) se lee solo hasta su primer bloque RIFF.
- **La línea de ffmpeg** (cualquier vídeo a ese perfil):
  `ffmpeg -i entrada.mp4 -vf scale=320:416 -r 15 -c:v mjpeg -q:v 3 -c:a pcm_s16le -ar 37800 -ac 2 salida.avi`
- **Otro tamaño:** se **escala** a 320x416 (bilineal, estirando: el juego muestra esa zona a 4:3, así que un 4:3 se ve bien y un 16:9 se ve achatado; escálalo tú con `-vf scale=320:416`). Mejor
  hacerlo con ffmpeg: es más nítido y controlas el recorte.
- **Otra velocidad:** de 1 a 60 cuadros por segundo; el AVI marca su ritmo (el cuadro `k` se muestra al sonar `(k - 1) / fps` s de audio). Otros valores se rechazan.
- **Otro audio:** otra frecuencia se **remuestrea** a 37800 Hz (lineal; mejor entrégalo ya a 37800), mono se duplica a estéreo, y sin audio la película es muda (con el mismo ritmo).
- **Otra duración:** la película **acaba cuando acaba el AVI**, la más corta o la más larga que la original (la del disco acaba en un cuadro fijo; con un AVI no cuenta). Tras el último cuadro el juego espera a que
  acabe el audio (hasta 0,45 s más) y sigue a lo que viniera después de la película. Start la salta como a cualquiera.
- **Si algo falla** el juego no se detiene: el motivo queda en el log como `[video] <mod>: <NOMBRE>.avi rejected: <motivo>` (no es AVI, códec que no es MJPEG, audio que no es PCM de 16 bits, ritmo fuera de 1..60,
  primer cuadro ilegible...) y se prueba el mod anterior y, al final, la película del disco. Si el **primer** cuadro no se decodifica se rechaza; un cuadro dañado más adelante repite el anterior (una línea en el log).
  Con el log en `mods` (`DW3_LOG=mods`) una película reemplazada deja `[video] <mod>: <NOMBRE>.avi replaces the movie (...)`.
- Código: `pc/shim/jpeg_read.c` (decodificador JPEG base), `avi_read.c` (lector de AVI), `video_mod.c` (la sustitución); `pc/videotest.c` los prueba con JPEG de PIL, AVI armados a mano al estilo ffmpeg, rechazos y
  fuzz (`make -C pc videotest-asan` lo repite con ASan y UBSan).

### 8.1c Exportar el audio (muestras en WAV, música en MIDI)

Los 71 bancos de sonido de `AAA/DAT/SOUND/` salen de **tu** disco en `data/audio/<BANCO>/` (en la carpeta del usuario), en formatos abiertos que abre cualquier editor de audio o DAW. Cada banco
trae sus muestras (`vag_NNN.wav`), su descripción (`bank.json`), sus secuencias (`seq_<paquete>_<registro>.mid`) y su lista (`sequences.json`). En el disco EU son 946 WAV y 1152 MIDI en 71 carpetas,
unos 19 MB, y el export entero tarda menos de un segundo. Solo se exporta si lo pides:

- **Menú:** *Opciones > System > Prepare modding files* (§2b) exporta los 71 bancos (sin confirmación; el valor de la fila muestra `Audio 3/71`). Corre en un hilo de trabajo con su propio
  manejador del disco, así que el juego mantiene sus cuadros por segundo y el sonido que esté sonando no se altera (no toca los bancos cargados, el SPU ni el secuenciador); una segunda pulsación mientras corre no hace nada.
- **`settings.ini`:** `[data]` / `export_audio = all` o `export_audio = BGM001, COMMON` (nombres de banco, mayúsculas o minúsculas) los exporta al arrancar, antes de abrir la ventana. Un banco ya
  exportado con la misma versión del exportador y con su `bank.json` presente se **salta sin construirlo**; un archivo editado cuenta como presente y no se toca. Para rehacer un banco, borra su
  `bank.json`; el botón *Prepare modding files* no reconstruye lo que ya está al día.
- **Regla `.new`** (como el resto de `data/`): `data/audio/manifest.json` recuerda lo que escribió el port, archivo por archivo. Un archivo que editaste se **conserva** y el nuevo queda al lado como
  `<archivo>.new`; uno sin tocar se reemplaza.
- **Las muestras (`vag_NNN.wav`)**: WAV PCM de 16 bits, mono, **44100 Hz**. 44100 Hz es la frecuencia a la que el SPU toca una muestra cuando su valor de tono es 0x1000, que es el caso al tocar la nota raíz
  de la muestra; los demás tonos son esa muestra más rápida o más lenta. `NNN` es el número de VAG del banco (1..). Una muestra de tamaño 0 no tiene archivo. Cada WAV lleva un trozo `smpl` (el que leen los
  samplers, los DAW y los editores de onda): `MIDIUnityNote` y `MIDIPitchFraction` son la nota raíz y su afinación fina, y, si la muestra hace bucle, un bucle directo con su primer y su último cuadro. Los
  mismos datos están en `bank.json`.
  - **Nota raíz:** la del primer tono (por orden de programa) que usa la muestra: `centro - desplazamiento / 128` semitonos (el `shift` del tono es con signo, 1/128 de semitono por unidad; el juego toca la nota N
    a `4096 * 2^((N - centro + shift / 128) / 12)` unidades de tono del SPU). Una muestra que ningún tono usa tiene la nota 60.
  - **Bucle:** sale de las marcas de los bloques ADPCM: bloque con la marca de fin y de repetición = bucle (`loop_end` es el último cuadro de ese bloque; `loop_start` el primer cuadro del bloque con la marca de
    inicio de bucle, o 0 si no hay); fin sin repetición = la voz se silencia ahí (`"end": "stop"`, sin bucle). En el disco EU: 394 muestras con bucle y 552 que paran; ninguna sin marca de fin.
  - **Decodificación:** el mismo cálculo que el renderizador de SPU del port (`decodeSound` de PsyX_SPUAL: filtro en coma flotante), de modo que el WAV es lo que el juego toca. El filtro entero de la consola
    puede diferir unas pocas unidades. Un programa independiente en Python decodifica el ADPCM en doble precisión y coincide dentro de 1 unidad (133 muestras de tres bancos del disco EU) y exacto en el fixture sintético de la prueba.
- **`bank.json`** (`"format": "bank-v1"`): `bank`, `bank_id` (el número de banco de los ids de sonido, bits 18-24), `sample_rate`, `master_volume` y `master_pan` del banco, `samples` (por VAG: `file`, `frames`,
  `end` = `loop`/`stop`/`none`, `loop_start`, `loop_end`) y `programs`. Cada programa trae su `volume` y `pan` y sus `tones`: `vag`, `min`/`max` (el rango de notas), `center` y `shift` (tal cual), `root_note` y `root_cents`
  (la nota raíz y cuántos centésimos de semitono por encima), `volume` y `pan` (0..127, 64 es el centro), `priority`, `pitch_bend_down`/`pitch_bend_up` (semitonos), `adsr1`/`adsr2` (los registros del SPU, en hex) y `adsr` (lo
  mismo decodificado: modo, desplazamiento y paso de ataque, decaimiento, sostenido y liberación, y el nivel de sostenido 0..32767). Un bloque `notes` explica cada campo dentro del propio archivo.
- **Las secuencias (`seq_P_RR.mid`)**: un Standard MIDI File **formato 1**, uno por registro de cada paquete SEP del banco (`P` = el paquete, en el orden en que el juego los abre; `RR` = el registro dentro del
  paquete, que es la «pista» del id de sonido). Pista 0: nombre, compás, tempo (el de la cabecera del registro y los cambios `FF 51` de la secuencia) y las marcas de bucle; después una pista por canal en uso, con cambios de programa,
  controladores (CC7 volumen, CC10 paneo, CC11 expresión y los demás que traiga), curvas de tono (14 bits: el juego solo guarda el byte alto, así que el bajo es 0 y el centro es 0x2000), notas (el fin de nota se escribe `0x8n` con velocidad 64).
  La resolución (ticks por negra) es la del registro (480 en los 1152 registros del disco EU). Los números de programa y de nota son los del banco: para oír la secuencia con los instrumentos originales, carga el `.mid` en un DAW o
  sampler junto con los WAV de `bank.json` (cada tono dice qué WAV, qué rango de notas, qué nota raíz y qué sobre ADSR). Un SoundFont por banco queda para más adelante.
  - **Bucles:** el juego marca el bucle con `CC99 = 20` (abre), `CC6` (el número de repeticiones, 127 o 0 = sin fin) y `CC99 = 30` (cierra). En el MIDI son los eventos de marca `loopStart count=N` (o `count=forever`) y `loopEnd` en la
  pista 0, más un `CC111` al inicio del bucle en el primer canal en uso (la convención habitual de los editores). Al llegar al final del registro el reproductor del juego vuelve al inicio del bucle, `N` veces o sin fin. En el disco EU 77
  de 1152 registros hacen bucle.
  - **Registros de relleno:** 1048 de los 1152 registros tienen 10 eventos o menos (casi todos 4 eventos y 2 ticks): son pistas vacías de relleno; salen igual para que la numeración coincida con la del juego (el juego numera los registros por su posición en el paquete).
- **`sequences.json`** (`"format": "sequences-v1"`): por archivo, `package`, `record`, `flat` (`paquete * 16 + registro`), `id` e `id_direct` (los ids de sonido empaquetados, abajo), `resolution`, `tempo_us`, `bpm`,
  `time_signature`, `events`, `length_ticks`, `length_seconds` (con los cambios de tempo), `channels` y `loop` (`start_tick`, `end_tick`, `count`, o `null`).
  - **Numeración (la del juego, comprobada):** `playSound` abre los paquetes de un banco en orden y toca el registro `pista` del paquete `SEQ` (bits 8-15 del id; bits 0-7 la pista). Si la pista no es un registro de ese paquete, busca en
    plano: 16 pistas por paquete (`snd_sep.c`, `sndSepTrackFor`). La música del juego usa `MUSIC(banco, n)` = `0x60000000 | banco << 18 | n` con `SEQ` 0, y `n` plano. `id` es ese valor
    (`n` = el registro en el paquete 0, `paquete * 16 + registro` en los demás; `null` si ese número lo contesta un registro del paquete 0); `id_direct` es el mismo con `SEQ` = paquete y pista = registro, que siempre sirve. Comprobado
    con un id conocido del disco: `W_JINGLE` = `0x6004001E` (`sound.h`) sale como el registro 14 del paquete 1 de `COMMON` (`seq_1_14.mid`, `flat` 30).
  - **Nombres:** el archivo no trae nombres. Los de la prueba de sonido de depuración (`SOUNDTST`, un overlay) no forman parte del port; el nombre del banco es el de su carpeta del disco.
- **Código:** C portátil (`pc/shim/audio_export.c`, `wav_write.c`, `midi_write.c`), con los lectores de cabecera VAB y de paquetes SEP del propio port (`snd_spu.c`, `snd_sep.c`): lo exportado es lo que el juego toca. Lo comprueba
  `pc/audiotest.c` con un banco sintético hecho a mano (cabecera, muestras y registros SEP) y `pc/test_fixtures/audio/check_audio.py`, que decodifica el ADPCM por su cuenta y lee los WAV y MIDI con lectores propios
  (el MIDI contra los eventos que decodificó el analizador SEP, los JSON contra las definiciones de los tonos). Reemplazar un efecto o una pista de música desde un mod: §8.1d.

### 8.1d Reemplazar un efecto o una canción desde un mod

```
mimod/audio/COMMON/vag_050.wav     # un efecto: reemplaza la muestra 50 del banco COMMON (WAV de 16 bits, mono o estéreo)
mimod/audio/TTLBGM/seq_0_00.wav    # una canción: la música del título, con bucle `smpl` (o un seq_0_00.json al lado)
mimod/video/MOVIEOPN.avi           # una película (§8.1b): la de apertura, en MJPEG
```

Los archivos son los que exporta §8.1c, con los mismos nombres, en `mods/<mod>/audio/<BANCO>/` (`<BANCO>` es la carpeta del banco, `COMMON`, `BGM001`...). Gana el último mod activo que tenga el
archivo. Sin esos archivos el sonido del juego suena exactamente igual que antes. El juego no cambia. Un archivo que el port no puede usar se rechaza con una línea en el log,
`[audio] <mod>: <archivo> rejected: <motivo>` (una vez por corrida), y se prueba el mod anterior y luego el sonido del disco; el juego nunca se detiene por un archivo malo.

**Un efecto (`vag_NNN.wav`).** Reemplaza la **muestra** número `NNN` del banco (la de `bank.json`/`vag_NNN.wav` exportado), así que cambian todos los tonos que la usan, ya sea que los toque un efecto
(`SsUtKeyOn`) o una secuencia de música. Es un WAV PCM de **16 bits**, mono o estéreo (los canales se promedian), a **cualquier frecuencia** de 1000 a 384000 Hz (sin comprimir: ni 8 bits, ni 24, ni flotante,
ni ADPCM de Microsoft). El port lo pasa a 44100 Hz, lo codifica al ADPCM del SPU (cinco filtros y 13 escalas por bloque, elegidos por menor error; el mismo decodificador del port lo comprobó) y lo vuelve a decodificar a PCM para tocarlo
al cargar el banco: el tono conserva su altura, volumen, paneo y envolvente ADSR (los de `bank.json`), porque solo cambia el audio.

- **Frecuencia y nota raíz:** la muestra exportada está a 44100 Hz y su nota raíz es la del `smpl` (`centro - desplazamiento / 128` semitonos). Si reemplazas el sonido por otro **de la misma altura y duración
  que el original**, ponle cualquier frecuencia: se remuestrea a 44100 y suena a la altura de siempre cuando el juego toca la nota raíz. La nota raíz del `smpl` **no se usa**: la altura la fija el tono. Si
  quieres que suene más agudo, súbelo tú en el editor. Si tu archivo es de 22050 Hz y dura 1 s, suena 1 s, no 0,5 s.
- **Bucle:** un trozo `smpl` con un bucle (inicio y fin, el fin es el último cuadro, como en lo exportado) hace que la muestra se repita; el sonido después del fin del bucle se descarta. Sin bucle la muestra
  suena una vez y la voz se silencia al acabar (el sobre ADSR del tono sigue mandando antes). El SPU repite **bloques enteros de 28 cuadros**: el inicio del bucle se redondea hacia abajo a un múltiplo de 28 y el
  último bloque se completa repitiendo el propio bucle. Un bucle sin corte audible es uno cuyo inicio y largo (a 44100 Hz) son múltiplos de 28; los WAV exportados ya lo cumplen.
- **Más largo que el original:** vale, y no hay tope fijo: solo la memoria. Las muestras reemplazadas no se escriben en la memoria del SPU de PsyCross (una matriz fija de 2 MB con un búfer de decodificación de 512K muestras, de una biblioteca que el port no modifica): se decodifican una vez a PCM al cargar el banco y suenan como voces PCM aparte, con el mismo tono, volumen, balance y sobre ADSR que tendría la voz del SPU. Lo que el juego acomoda en el SPU no se mueve y, sin mods, el sonido es idéntico.
- **El log:** con `[debug] log = mods` (§6.6) cada reemplazo escribe `[audio] <mod>: <BANCO>/vag_NNN.wav replaces the sample (...)` al cargar el banco; el banco `COMMON` se carga al arrancar. Con `SND_DEBUG=1`, el key-on de una muestra reemplazada muestra una dirección virtual (`0x4...`, marcada `(pcm)`), no una del SPU.

**Una canción (`seq_<paquete>_<registro>.wav`).** Con el nombre del `seq_<paquete>_<registro>.mid` exportado (renombra tu archivo para que coincida; el registro también puede ir sin el cero, `seq_0_2.wav`), suena **en lugar de esa
secuencia** cuando el juego la inicia. `sequences.json` dice a qué `MUSIC(banco, n)` corresponde cada archivo. En el disco EU, por ejemplo, la música de la pantalla de título es `TTLBGM/seq_0_00` (`MUSIC(0x47, 0)`); `BGM031/seq_0_02` es la de la selección de idioma. WAV PCM de 16 bits, mono o estéreo,
a cualquier frecuencia (OpenAL la remuestrea en la salida).

- **Bucle**, por orden: (1) el bucle `smpl` del WAV (cuadros; el fin es el último cuadro del bucle); (2) un archivo `seq_<paquete>_<registro>.json` al lado con `{"loopStart": cuadros, "loopEnd": cuadros}` (mismas unidades;
  `loopEnd` es el último cuadro; un JSON roto o con un bucle fuera del WAV se rechaza con su motivo y se ignora); (3) si la secuencia original **repite sin fin** (en `sequences.json`, `loop` con `count` `forever`), el
  archivo entero hace bucle; (4) si no, suena una vez y la pista termina. Entre la última muestra del bucle y la primera no hay hueco: el empalme es exacto.
- **Lo que el juego sigue controlando:** detener la pista (`SsSepStop`), su volumen (`SsSepSetVol`), el desvanecimiento (`SsSepSetDecrescendo`) y el volumen general actúan sobre el WAV (con la curva cuadrática que el
  juego usa para las voces); el control de **música** del menú Audio (o el de **efectos**, para una secuencia que no es la música del juego, como la góndola) y el volumen general del jugador también. El juego no pausa
  secuencias, así que no hay pausa que honrar. Una pista sin bucle que llega al final deja de sonar y el juego la ve terminada, como a una secuencia que acaba.
- **Memoria o disco:** un archivo de hasta 8 MB de muestras (unos 47 s de estéreo a 44100 Hz) se carga entero; uno mayor se lee del disco por trozos de 4096 cuadros. Las dos formas suenan igual.
- **Un archivo malo** se rechaza con `[audio] <mod>: <archivo> rejected: <motivo>` y suena la secuencia del juego (o el archivo de un mod anterior). Igual si el equipo no tiene salida de audio (`DW3_SND_MUTE`).
- **El log:** con `[debug] log = mods`, `[audio] <mod>: <BANCO>/seq_P_RR.wav replaces the sequence (... in memory | streamed, loops from the smpl chunk | ... the json | ... the whole file | plays once)` cada vez que el juego
  la inicia; con `trace`, también cuando se detiene o termina.
- **Código y pruebas:** `pc/shim/wav_read.c`, `vag_encode.c`, `audio_mod.c` (pistas, salida OpenAL en trozos, lector en memoria o por disco, JSON), `snd_spu.c` (muestras) y `snd_sep.c` (la pista sustituye a la secuencia sin cambiar su
  estado); `pc/audiotest.c` las prueba con un reloj falso: lector WAV, codificador ADPCM (ida y vuelta), parche de banco con una muestra más larga, y la máquina de estados de la pista (inicio, parada, bucle `smpl`/JSON/entero, volumen,
  desvanecimiento, deslizadores, memoria contra disco, archivo malo).

### 8.2 Hacer un modelo nuevo (glTF a modelo de combate)

Para un Digimon con arte propio: un modelo en glTF 2.0 (Blender, u otro programa), su textura PNG y un JSON de metadatos (`model.json`). El
**port lo hornea al cargar** al formato de modelo del juego: el mod trae solo esa fuente neutra, sin ningún archivo del juego. Los efectos de las
técnicas los toma de un luchador **donante** que el juego resuelve al correr.

**Dónde va y cómo se usa.** La fuente es una carpeta del mod de contenido (`"kind": "content"`):

```
mimod/digimon/<llave>/digimon.json              # el registro del Digimon (§6.9); sin "model" usa el model/ de su carpeta
mimod/digimon/<llave>/model/model.json          # metadatos model-src-v1 (abajo)
mimod/digimon/<llave>/model/modelo.gltf         # o .glb; con su .bin y su PNG al lado
```

La llave (a-z, 0-9, `-`, `_`) la nombra `"<carpeta del mod>:<llave>"`, y un Digimon nuevo la usa sin escribir nada (su carpeta es `digimon/<llave>/`) o como `"model": "<carpeta del mod>:<llave>"` (§6.9):
recibe una fila de `FighterInfo` propia con el modelo y los movimientos horneados, los efectos del donante, y la `template` (cámaras, altura) y el
`info` (`distance`, `height`) de `model.json`. También sirve en el objeto de `"model"` como `{"file": "<mod>:<llave>", "entry": 1}` (modelo; el 0 son los
movimientos). Los luchadores originales se nombran `"dw3:digimon/<id>"` y, solo para el modelo, `"dw3:model/<id>"` (comparte la fila de ese luchador). Ejemplo completo:
`pc/examples/mods/sample-model/` (`digimon/boxbot/`). Si la fuente no se puede hornear (un archivo que falta, un PNG roto, una animación que no existe...), el log dice
`[models] <mod>:<llave> rejected: <por qué>` y el Digimon se queda con el modelo de su plantilla: un modelo malo nunca tumba el juego.

**Costo.** Hornear un Kotemon completo (24 huesos, 926 triángulos, 32 animaciones) tarda unos 20 ms y ocupa unos 20 MB mientras se hace (se liberan al
terminar); el robot de ejemplo, menos de 1 ms. El resultado se guarda en `cache/models/` de la carpeta del usuario con una clave que mezcla los
bytes de la fuente y la versión del horneador: al volver a arrancar con la misma fuente se lee de ahí (unos 5 ms, casi todo leer y sumar la fuente);
cambiar la fuente o actualizar el port rehornea solo, y un archivo de caché dañado se ignora. Borrar `cache/models/` no pierde nada.

**La herramienta de línea de comandos** sigue sirviendo para revisar una fuente sin abrir el juego (`--check`: informe y avisos) o para hornear un archivo
fuera del juego. El horneado del port es el mismo algoritmo en C y da **los mismos bytes** que la herramienta (lo comprueba `pc/modeltest.c`):

```
python3 -I tools/model_import.py mimodelo/model.json --out mimod/ [--mod <nombre del mod>] [--check]
python3 -I tools/test_model_import.py --demo salida/     # un robot de cajas de ejemplo (robot.gltf/.bin/.png + robot.json)
```

Con `--out` escribe `mimod/digimon/<name>/model.bin` (junto al `digimon.json` de ese Digimon; el port lee cualquier archivo del mod por su ruta) y muestra un `digimon/<name>/digimon.json` con la forma de objeto de `"model"` (el archivo crudo, que el port
lee con `"<mod>:digimon/<name>/model.bin"`); es el camino avanzado y de depuración: con la fuente en `digimon/<llave>/model/` no hace falta.

**Exportar desde Blender** (*Archivo > Exportar > glTF 2.0*): formato *glTF separado* (`.gltf` + `.bin` + texturas) o `.glb`; *+Y arriba*; marcar
*Normales*, *Coordenadas UV*, *Materiales* e *Imágenes: PNG* (no JPEG); *Skinning* activado; *Animación* activada con las acciones y
*muestreo siempre* (Always Sample). El modelo mira hacia **+Z** de glTF (el frente por omisión de Blender); si el tuyo mira al otro lado, usa `"yaw": 180`.
Aplica la escala y la rotación de los objetos (Ctrl+A) y deja una sola armadura; un nodo por encima de los huesos con rotación y escala uniforme
(el «Armature» de Blender) se absorbe solo, pero una escala **no uniforme** es un error.

**Metadatos `model-src-v1`** (la ruta de `gltf` es relativa a este archivo):

```json
{
  "format": "model-src-v1",
  "name": "boxbot",
  "gltf": "robot.gltf",
  "fitHeight": 1800,
  "yaw": 0,
  "motions": {
    "0": "idle", "1": "idle",
    "2": { "animation": "hit", "end": "idle" },
    "3": 2,
    "4": { "animation": "walk", "loop": true, "speed": 1.0 }
  },
  "fill": "idle",
  "effects": "dw3:digimon/383",
  "template": "dw3:digimon/4",
  "info": { "distance": -1500 },
  "displayName": "Boxbot"
}
```

| Clave | Para qué |
|---|---|
| `name` | nombre de la carpeta (`digimon/<name>/model.bin`) y llave del Digimon en el fragmento; letras, dígitos, `-` y `_` |
| `scale` o `fitHeight` | unidades del juego por unidad de glTF, o la altura (en unidades del juego) que debe tener la pose de reposo; no los dos. Los luchadores originales miden 1300 a 10 000; Kotemon, 1886. Sin ninguno la escala es 1 y avisa si el modelo mide menos de 50 o más de 20 000 |
| `yaw` | grados de giro alrededor del eje vertical (0 por omisión) |
| `motions` | ranura (0 a 61) a animación: un nombre, un objeto o un número (copia esa otra ranura). Sin `motions`, las animaciones llamadas `motion_NN` van a la ranura NN (así sale del exportador, §8.1) |
| `fill` | animación (o un objeto como en `motions`) para **todas** las ranuras sin asignar; sin él, una ranura vacía es un riesgo si un script de efectos la pide |
| `effects` | donante: `"dw3:digimon/<id>"` cuyo archivo de efectos usa el Digimon (los scripts hablan del modelo solo por número de ranura) |
| `template` | luchador original del **mismo tipo** (enemigo o compañero) que da cámaras, altura y distancia: ponlo en `"base"` y `"from"`; conviene uno de tamaño parecido, las cámaras no se ajustan solas |
| `info` | `"distance"` y `"height"` de la fila propia (§6.9): colocan el **modelo** en su lugar de reposo si es mucho más grande o chico que la plantilla; no mueven las cámaras (§8.2c) |
| `normals` | `"file"` (por omisión, las del glTF; si faltan, suaves), `"smooth"` o `"flat"` |
| `doubleSided` | `"duplicate"` agrega las caras de atrás de los materiales de doble cara (el juego dibuja una sola cara; Blender los marca así por omisión) |
| `from` | `"dw3:model/<id>"`: parte del luchador original (ver «Empezar de un original»); lo que falta sale de él |
| `tolerance` | `{"position": u, "rotation": raw, "scale": raw}`: error máximo al reducir las claves (por omisión 0.1 % de la altura, 4 = 0.35 grados, 16 = 0.4 %) |

**Empezar de un original: `"from"`.** Un `model.json` puede decir `"from": "dw3:model/<id del luchador>"` (el mismo número que en
*Prepare modding files* o `export_models`) y traer **solo lo que cambia**: lo que falta lo pone el luchador original, que el port exporta **en memoria** al cargar (los mismos
cuatro archivos que `data/digimon/d<nnn>/model/`, leídos de tu disco). Caso típico, un retexturizado: la carpeta lleva solo `model.json` (`{"from": "dw3:model/383"}`) y
tu `model.png`. Reglas:

- **Archivos:** `model.gltf`, `model.bin` y `model.png` que la carpeta no tenga salen del original; los que sí tiene **mandan**. El glTF exportado nombra
  `model.png` y `model.bin`, así que un `model.png` propio debe tener la **misma disposición de UV** (tamaño y posición de cada isla) que el original: se pinta
  encima del exportado, no se cambia de sitio. Si traes tu propio `model.gltf`, trae también su `.bin` y su PNG (o los nombres exportados, que se completan).
- **`model.json`:** el del original es la base y las claves de primer nivel del tuyo la reemplazan (fusión superficial: `"info"`, `"template"`, `"effects"`,
  `"yaw"`... se sustituyen enteras). Excepción: `"motions"` se fusiona **por ranura**: las ranuras que pones mandan y las demás siguen siendo las del original.
  Si traes un glTF propio con otros nombres de animación, redefine en `"motions"` todas las ranuras que usaban nombres del original (una ranura no se puede quitar,
  solo cambiar). `"format"`, `"name"` y `"gltf"` también se heredan.
- **Rechazos** (`[models] <mod>:<llave> rejected: <por qué>`): `"from"` que no es `"dw3:model/<n>"`, un luchador que no existe en tu disco, o sin disco
  disponible (por ejemplo en las pruebas). `"from"` aquí es del `model.json`; no lo confundas con el `"from"` del `digimon.json` (el luchador plantilla).
- **Caché:** la clave mezcla los archivos de la carpeta, el id del original y la versión del exportador (`MODEL_EXPORT_VERSION`), no los bytes del original: con la caché
  caliente no se construye el original (solo se lee la información de plantilla y efectos si el `model.json` no la trae entera). Cambiar de port con otro exportador rehornea.
- `tools/model_import.py` **no** lee el disco: rechaza un `model.json` con `"from"` y dice que exportes la carpeta antes (*Prepare modding files* o `export_models`) o que dejes que el juego lo hornee.

**Movimientos.** Cada ranura es una animación de glTF muestreada a 60 cuadros por segundo (la velocidad del juego) y reducida a las claves necesarias.
Las ranuras **0 y 1** (reposos, obligatorias) son bucles; el resto, por omisión, se mezclan desde la pose actual durante 10 cuadros y al terminar se
mezclan de vuelta al reposo. Cada objeto acepta `animation`, `loop`, `loopStart` (cuadro del clip al que vuelve el bucle), `end` (`"idle"` o `"hold"`, quedarse en
la última pose), `blendIn` y `blendOut` (cuadros de mezcla; 0 en `blendOut` equivale a `hold`) y `speed` (2 = el doble de rápido). Dos ranuras con la misma animación y velocidad
comparten los cuadros. La lista mínima segura es **0, 1, 2, 3, 4, 10, 11, 12, 14** (el 1 es el segundo reposo, el 12 la entrada, el 3 la reacción al golpe); las
técnicas del donante piden más (`tools/techcompat.py`, §6.8); la herramienta avisa de las que falten.

**Qué hace con la geometría y la textura.** Piel rígida: cada triángulo va al hueso que más pesa sobre él (avisa de cuántos cruzan huesos); un hueso con más de 256
vértices o 256 normales se parte en huesos hijo; los huesos sin polígonos y sin hijos con polígonos se descartan (un hueso sin polígonos pero con hijos queda con una
malla vacía). La textura se rehace en una página de 256x240 a 4 bits: las islas de UV se reempaquetan (y se reducen si no caben) y cada isla recibe una paleta de **16 colores**
(la 0 reservada a lo transparente si hay recorte; hasta 64 paletas, las parecidas se unen); el informe da el error de cuantización. Solo triángulos, con luz por normal.

**Límites y avisos** (detalle en `docs/formats.md` 4.6): 256 vértices y 256 normales por malla de hueso; unos 40 huesos como máximo cómodos (el juego tiene 100 llamadas
de hueso para todos los luchadores en pantalla; error a partir de 90); unos 3000 polígonos como aviso (el mayor original tiene 1334; el tope real es el búfer
de primitivas del port); todos los movimientos cuelgan de una línea de tiempo común de menos de 30 000 cuadros y cada uno tiene como máximo 0x640 pasos. No se soporta aún:
colores por vértice, UV fuera de 0..1 (se recortan), `KHR_texture_transform`, semitransparencia parcial (el alfa es todo o nada), piel suave, el parpadeo (la cara queda vacía)
ni más de un archivo de modelo por Digimon.

**Usarlo.** Pon el fragmento como `digimon/<llave>/digimon.json` del mod (el nombre es la carpeta; elige `"base"` y `"from"` del mismo tipo); el Digimon sale con su modelo en la batalla.
Los enemigos nuevos y las formas de compañero se explican en §6.9. Con la fuente glTF en `digimon/<llave>/model/` no hace falta hornear nada aparte: el port la convierte al cargar (§8.2, «Dónde va y cómo se usa»).

### 8.2b Un retrato de compañero a partir de un modelo (`tools/portrait_from_model.py`)

Genera el `portrait.png` de un compañero nuevo (§6.9b, `docs/formats.md` 4.12) dibujando su modelo 3D: no hace falta pintar los cuadros a mano. Solo biblioteca estándar de Python 3.

```
python3 -I tools/portrait_from_model.py <modelo> <salida.png> [--cells 1..8] [--icon] [--animation <nombre>] [--yaw 40] [--pitch 12]
                                        [--front +z|-z|+x|-x] [--margin 1..2] [--colors 3..16] [--dither] [--preview vista.png]
```

`<modelo>` es una carpeta con `model.json` (o un `.gltf`/`.glb` suelto, o el `model.json`), `<carpeta del mod>:<llave>` (`digimon/<llave>/model/`) o
`dw3:digimon/<id>` para un luchador original **de tu propio disco**: `--data <carpeta de datos del puerto>` (donde "Exportar modelos" dejó `digimon/d<nnn>/model/`) o
`--disc "<disco>.bin"` (llama a `tools/model_export.py --source` en una carpeta temporal). **Córrelo con tu propio disco; el resultado se queda en tu máquina**
(no lo subas ni lo compartas: es arte derivado del juego, ADR-001). Un archivo de modelo ya horneado no sirve de entrada: hace falta la fuente glTF.

Salida: PNG indexado de 4 bits, 32 de alto y 32 x N de ancho (4 celdas por defecto), índice 0 transparente, índice 1 el contorno oscuro de 1 px y hasta 14 colores
más (cuantización por corte de mediana más 4 pasos de k-medias, sin tramado salvo `--dither`). El modelo se ve de tres cuartos mirando a la **izquierda** de la pantalla
(`--front` dice qué eje del modelo es su frente: glTF usa `+z`; si sale mirando a la derecha o de espaldas, prueba otro), ajustado y apoyado abajo con 1 px de margen
(`--margin`); la celda *k* es la pose a *k*/N del bucle de la animación de reposo (la del movimiento 0 de `model.json`, o la primera), y todas comparten escala y línea
de suelo, así que el ida y vuelta `0 1 2 3 2 1` se ve como respiración. `--icon` da un solo cuadro de 44x28 para el ícono de la memory card.
Diseño: un rasterizador por software de unas 400 líneas (dibuja a 8x y reduce con umbral de cobertura 0.5 para bordes nítidos) sobre el lector de glTF de `tools/model_import.py`;
más simple y portable que sacar los triángulos del C, y se prueba con `python3 -I tools/test_portrait_from_model.py` (`make -C pc portraittest`).

### 8.2c Modelo propio de compañero

Una forma de compañero nueva (§6.9, «Formas de compañero nuevas») puede traer su propio modelo glTF igual que un enemigo (§8.2): la carpeta `digimon/<llave>/model/` de su
`digimon.json` con `"base"` de un compañero. Lo que cambia respecto a un enemigo es qué pide el juego de un compañero: más ranuras de movimiento y 12 cámaras en vez de 3. Ejemplo
completo y probado en batalla: `pc/examples/mods/sample-model/digimon/boxpal/` (un robot de cajas con las animaciones de un compañero; `python3 -I tools/test_model_import.py --demo-partner <carpeta>` lo vuelve a generar).

**Empezar de un compañero original (en tu máquina).** Exporta uno de **tu** disco (*Prepare modding files* del juego, o
`python3 -I tools/model_export.py "<disco>.bin" 383 <carpeta> --source` para Kotemon) y copia la carpeta `d383/model/` (`model.gltf`, `.bin`, `.png`, `model.json`) a `digimon/<llave>/model/`
(la `d<nnn>/model/` nunca se lee, §6.9). Sale ya hecha la plantilla de un compañero: `"template"` y `"effects"` del propio luchador (sus 12 cámaras y los scripts de sus técnicas), `"info"` con su distancia y altura, una
animación `motion_NN` por cada ranura que ese luchador trae y `"motions"` que las une. **Es dato derivado del juego: no lo subas ni lo compartas** (ADR-001); reemplaza la malla y la textura por las tuyas antes de publicar, y publica solo
tu `model.json` y tus animaciones. En Blender conserva los nombres de las animaciones (`motion_NN`, el número es la ranura) o ponles los tuyos y reasígnalos en `"motions"`.

**Un `model.json` de compañero** (el del ejemplo; las animaciones llevan el nombre de lo que hacen y `"motions"` dice la ranura de cada una):

```json
{
  "format": "model-src-v1", "name": "partner", "gltf": "partner.gltf", "fitHeight": 1800,
  "effects": "dw3:digimon/383", "template": "dw3:digimon/383",
  "motions": {
    "0": "idle", "1": "idle_weak", "2": "dodge", "3": "flinch", "4": "hit", "5": 4, "6": 4,
    "10": { "animation": "ko", "end": "hold" }, "11": 4, "12": "cheer", "13": "blast", "14": "attack",
    "15": 14, "16": 14, "17": 14, "20": 14, "32": 14
  },
  "fill": "attack"
}
```

`"template"` debe ser un **compañero** original (si no, el Digimon queda con el modelo de su `base` y el log lo dice); sin `"template"` se usa el `base`. `"effects"` es el donante de las animaciones de las técnicas (el de la
plantilla casi siempre). Un número en `"motions"` copia esa otra ranura; `"fill"` da una animación a **todas** las ranuras sin asignar (las de técnica del donante entre ellas).

**Ranuras de movimiento** (las 62 que tiene un luchador; lo medido en los 248 luchadores del disco y el código que las pide, con `ruta:línea`, está en `docs/formats.md` 4.6, punto 5b). La ranura es el número de `"motions"`:

| Ranura | Para qué la pide el juego | Compañero | Enemigo |
|---|---|---|---|
| 0 | reposo (bucle) | obligatoria | obligatoria |
| 1 | reposo débil: con un cuarto de PS o menos | obligatoria | obligatoria |
| 2 | esquivar: el objetivo de un ataque que falló | sí | sí |
| 3 | encogerse: un golpe que acierta y no es el último de un ataque de varios | sí | sí |
| 4, 5, 6 | recibir un golpe (la 5 y la 6 las piden en ese orden varios luchadores) | 4 sí; 5 y 6 si el donante las usa | igual |
| 10 | caer: el golpe que derrota | sí | sí |
| 11 | la última de recibir un golpe que no derrota (qué muestra el original: desconocido) | sí | sí |
| 12 | entrada, y la pose de victoria y la entrada de un cambio de Digimon (compañero) | sí | sí |
| 13 | entrada de la forma temporal (DV de ráfaga) | solo si es una forma de ráfaga | no existe |
| 14 a 19 | ataque genérico («Lucha»): la 14 y, en ataques de varios golpes, las siguientes | 14 sí | 14 sí |
| 20, 32 | el ataque genérico cambia a los scripts 6 (efecto de acción o arma de la familia del enemigo) y 8 (golpe triple) | sí (20 a 25, 32 a 37) | no |
| 26 a 31, 38 a 39, 44 a 45, 50 a 55 | animaciones de técnicas de los scripts 7, 9, 10, 11 y 13 del donante | las del donante que use | igual |
| 56 a 58 | script 12 del donante, que actúa sobre otro modelo (desconocido) | solo si el donante lo usa | no |

Todas las demás (7, 8, 9, 40 a 43, 46 a 49, 59 a 61) no las pide nada. Una ranura vacía (sin `"fill"`) hace que el juego lea basura si alguien la pide: **por eso
`--validate-mod` da una advertencia por cada ranura que el juego pide a un compañero (0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 20 y 32) o a un enemigo (0, 1, 2, 3, 4, 10, 11, 12 y 14) y que el modelo deja vacía**, p. ej.
`sample-model:boxpal: motion slot 3 (flinch) has no animation: a hit of an attack that lands and is not the last (effect script 1)`. Las técnicas del donante piden más ranuras (14 a 55): con `"fill"`
todas existen; sin él mapea todas las del donante que uses. Dos técnicas de un compañero que se animan con el script 5 pueden jugar el 6 u 8 según el enemigo (`techs.c:48-85`), por eso sus ranuras 20 y 32.

**Cámaras (12 en un compañero, 3 en un enemigo).** No están en el modelo: son parte de la ficha que el Digimon copia de su `"template"` (`docs/formats.md` 4.6, punto 5b):

| Cámara | Qué ve |
|---|---|
| 0 a 5 | el **enemigo**: se elige la `cameraCount - 1` del enemigo (1 a 6) leída de la ficha del **compañero**, así que el compañero decide el encuadre de cada enemigo |
| 6, 7 | el compañero (7 si está débil) |
| 8, 9, 10, 11 | el compañero en pelea (8), en la digievolución (9), en su entrada y el cierre (10) y una vista alta que solo piden los scripts (11) |

Un modelo propio **no puede** darle cámaras nuevas: usa las de la plantilla (o del `base` sin `"template"`); escoge una de **tamaño parecido** a tu modelo (`fitHeight`; Kotemon mide 1886 y su ficha de cámaras está hecha para eso).
`"info": {"distance": ..., "height": ...}` no mueve las cámaras: coloca el **modelo** en su lugar de reposo (`z = ∓(0x1400 + distance)`, `y = -height`; sin `"info"` salen los de la plantilla). Si el modelo es
mucho más grande o pequeño, ajusta eso y comprueba la batalla con los 4 encuadres (batalla, ataque, digievolución y victoria).

**Hornear y revisar.** El port lo hornea al cargar (§8.2); sin abrir el juego: `python3 -I tools/model_import.py digimon/<llave>/model/model.json --check --partner` (`--partner` avisa, una línea por
ranura, de las que un compañero necesita y faltan; sin él solo sale el aviso corto de los enemigos). Con el juego: `<boot> "<disco>.bin" --validate-mod <mod>` (0 errores y 0 advertencias en el ejemplo).
Para verlo en batalla sin jugar hasta la digievolución, el arnés de pruebas siembra la forma: `DW3_SEED_PARTNER="0.0=<mod>:<llave>@1/0" DW3_BATTLE_ENEMY=63 <boot> "<disco>.bin" 5000 <captura.png> 0 "battle@30:cross,70,cross,..."` (§6.9, «Qué hace una forma nueva hoy»).

**Límites.** Los de §8.2 (huesos, polígonos, textura de 16 colores por isla, piel rígida) y además: las cámaras y los scripts de técnicas son los de la plantilla y el donante; la cara queda vacía (sin parpadeo); el nombre que muestra
el HUD de la batalla muestra el nombre del compañero (`PartnerStats.name`: el de su primera forma al empezar la partida, o el que le puso el jugador; el original tampoco lo cambia al digievolucionar, y esto es así a propósito), mientras que el nombre de la forma sale en el informe de batalla, el laboratorio, el estado y las listas de digievolución; un compañero nuevo se consigue con `"evolution"` (§6.9), no por el modelo.

## 9. Reglas legales y de convivencia

- **Nunca redistribuyas datos del juego**: ni textos originales, ni imágenes, ni archivos del disco, ni binarios
  (ADR-001). Un mod publica **solo sus propios cambios**.
- Para un cambio que parte de un archivo del juego (una paleta, un archivo crudo) publica el **procedimiento**: un script
  como `image_mod_palette.py` o un parche binario que el jugador aplica a **su** copia. Lo que el jugador genera queda en
  su equipo.
- Texto nuevo propio (traducciones, nombres) sí se puede compartir.
- Dos mods que cambian lo mismo no chocan: gana el último del orden, con su línea en el log.

## 10. Hoja de ruta (ADR-006) y qué falta

| Fase | Contenido | Estado |
|---|---|---|
| Layout de los assets (`asset-v1`, ADR-016) | una carpeta por tema y por asset, un registro JSON por asset (`card.json`, `partner.json`, `digimon.json`, `stage.json`...), las mismas rutas en `data/` y en `mods/<mod>/`, esquemas en `schema/` y `--validate-mod` sobre todo menos `text/` | **final para la próxima versión**: las rutas de esta guía no cambian de forma antes del release |
| 1. Recursos | reemplazar texto, config, imágenes, sprites y archivos crudos; manifiesto con dependencias; esta guía | **hecha** (revisión pendiente) |
| 2. Tablas de datos | tablas del juego (Digimon, técnicas, objetos, recompensas, experiencia, tiendas, encuentros) en JSON; parches por clave y filas nuevas con id `<mod>:<nombre>` | **hecha** (`stageBattles` va en los parches de escenario, §6.11). Incluye tienda de cartas, gimnasio, posadas, rivales de cartas y cartas nuevas, objetos y técnicas nuevos, cambios a Digimon existentes, enemigos nuevos y formas de compañero nuevas |
| 3. Scripts | lenguaje embebido (candidato Lua 5.4, en una caja sin acceso a archivos, red ni sistema) para eventos, misiones, personajes | planeada (su propio ADR) |
| 4. Entidades nuevas | escenarios de campo nuevos desde un mod (ADR-012, `stage-new-v1`, §6.11b, ejemplo `training-field`); personajes nuevos de escenario con charlas y banderas propias (§6.11, con sprites del escenario o propios); los Digimon nuevos ya funcionan (modelos de batalla en glTF, 8.2) | **en curso** (escenarios nuevos hechos; faltan árboles y pesca como datos) |
| Formatos de recursos | video (AVI), audio (exportar y reemplazar WAV/MIDI), imágenes (PNG), modelos de batalla (glTF) y arte de escenario (PNG) | **hecha** (§8) |

Reservado para las fases 2 a 4: `"kind": "content"` y los identificadores `<mod>:<nombre>`. Las partidas ya no se guardan
en una tarjeta de PS1 sino en `<pref>/saves/default/slot<N>.dw3save` (ADR-008, opción B: el bloque original del juego tal
cual, más el estado extendido por nombre y un CRC32; una tarjeta `.mcd` puesta en `import/` se importa una vez y no se
modifica). Las claves de `mod.json` y los formatos llevan versión; un cambio incompatible crea una nueva y la
anterior se lee al menos una versión del port más.

## 11. Migración desde el formato anterior

Esta guía describe solo el formato actual (`asset-v1`, ADR-016). Si tienes un mod hecho para una versión anterior del port, o una carpeta `data/` que esa versión exportó,
esta sección dice dónde está hoy cada cosa. Las partidas no cambian: guardan nombres e identidades, no rutas.

**Reglas**

1. **No hay compatibilidad.** El port no lee ninguna ruta de la tabla de abajo, ni da error por ella. `--validate-mod` avisa (`folder <nombre>/ is never read`, `... is ignored`) de las carpetas y
   los archivos viejos más comunes, pero no revisa una por una las imágenes viejas de `images/`. Mueve los archivos a su ruta nueva a mano (los PNG no cambian de formato) y reescribe los registros (abajo).
2. **Vuelve a exportar `data/`.** Los exportadores no migran ni borran lo anterior: las carpetas viejas de `data/` quedan en el disco sin leerse (algunas dejan una línea en el log, p. ej. `data/models/`).
   Bórralas a mano, o borra `data/` entera y arranca con `--export-all` (§2b) para que se rehaga. Un manifiesto con el formato viejo (`stamp`) cuenta como ausente y se exporta de nuevo.
3. **`name` pasa a `title`.** En `card.json`, `partner.json` y `digimon.json` el texto por idioma (`{"eng": ..., "es-419": ...}`) va ahora en `title`; `name`, si está, es el nombre de la carpeta.

| Antes | Ahora | Qué hacer |
|---|---|---|
| `images/<carpeta>/<archivo>/<NN>_<MM>.png` + `.json` (`atlas-v1`) de las pantallas de menú (tienda, estado, memory card, nombre, gimnasio...) | `menus/<pantalla>/<pieza>.png` + `menu.json` (§6.2g) | rehacer la edición por pieza, con los colores reales. Siguen en `images/`: `TLOGO*`, `TTCOMMPK`, `END__ALL`, `CNTSELWN`, `CARDDATA`, `FIELDCOM`, `STWMP0` y `STWMP1` (§6.2) |
| `images/field/stage/s<nnn>mask/` (objetos de un escenario) | `stages/s<nnn>/objects/<pieza>.png` + `objects.json` (§6.2h) | rehacer por pieza. Los fondos `s415bg01/02` y `s815bg01/02` siguen en `images/field/stage/` (§6.2c) |
| `z_stage/s<nnn>pack/` (baldosas) | `stages/s<nnn>/tiles/` + `tiles.json` (§6.2d) | mover los PNG |
| `stages/<n>.json` (datos de un escenario y parche de un mod) | `stages/s<nnn>/stage.json`, con `s` y tres cifras (§6.11) | renombrar y mover; un solo `stages/manifest.json` |
| `new-stages/<nombre>/` | `stages/<llave>/` (§6.11b) | mover la carpeta; `s<nnn>` está reservado para los originales |
| `images/field/sprt/<NOMBRE>/` (y las copias de los seguidores `p003pken`...`p010pata`) | `sprites/<nombre>/<EEE>.png`, en minúsculas (§6.3); los 8 seguidores, en `partners/<nombre>/field/` | mover los cuadros; en la carpeta de una original solo se leen los `NNN.png` |
| `images/card/cardpak<A>/<NN>.png` y `card/<llave>.png` de un mod | `cards/c<nnn>/face.png` (originales) y `cards/<llave>/face.png` (nuevas) (§6.2b, §6.10) | `<nnn>` es el número de la carta (`c001`-`c316`), no la posición dentro del `cardpak` |
| `content/cards.json` (`"new"`) | `cards/<llave>/card.json` (`cards-card-v1`) | cada entrada `"<llave>": {...}` pasa a su archivo con `"format": "cards-card-v1"`; el texto `name` pasa a `title` |
| `content/partners.json` y los campos `art`, `icon`, `sprite` | `partners/<llave>/partner.json` (`partners-partner-v1`) junto a `portrait.png`, `icon.png`, `field/` y `training/` (§6.9b, §6.2f) | igual: un archivo por compañero, `name` pasa a `title`; los campos de arte se quitan (el arte se busca en la carpeta) |
| retratos e íconos repartidos en 7 hojas de pantalla | `partners/<nombre>/portrait.png` e `icon.png` (§6.2f) | editar el PNG del compañero; el port lo escribe en todas las pantallas |
| `content/digimon.json` (`"change"` y `"new"`) | `digimon/d<nnn>/digimon.json` (cambia un original) y `digimon/<llave>/digimon.json` (nuevo) (§6.9) | un archivo por Digimon; `name` pasa a `title`; los emplazamientos pasan a `content/encounters.json` |
| `content/models/<llave>/` y `models/<id>/` | `digimon/<llave>/model/` (nuevo) y `digimon/d<nnn>/model/` (exportado, §8.1) | mover la carpeta; el `model/` de una `d<nnn>` de un mod no se lee |
| un `stamp` por exportador y un `manifest.json` por familia | un `manifest.json` por tema (`asset-manifest-v1`) | nada: se rehace al exportar; el mod no lo escribe |
| `audio/`, `video/`, `text/`, `tables/`, `config/` | los mismos temas, con nombres en minúsculas | nada que mover en un mod |
| `files/<ruta del disco>` en `data/` | `raw/<ruta del disco>` en `data/` (§6.2e) | en un mod, los crudos siguen en `files/` (§6.4) |
