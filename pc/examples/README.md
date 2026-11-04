# Mods de ejemplo

24 mods pequeños, al menos uno por tipo de mod, para activarlos de a uno (pestaña *Mods* del menú de opciones) y ver cada
cambio en tu propia partida. No traen datos del juego (ADR-001): lo que necesita el disco (imágenes, textos, escenarios) se
genera en tu equipo a partir de tu `data/` exportado, y el audio y el video son sintéticos.

## Instalar

```
pc/examples/install_examples.sh [--only tablas|contenido|media|nombre,nombre] [CARPETA_DE_USUARIO]
```

- La carpeta de usuario es `$DW3_USER_DIR` o `~/.local/share/dw3port/dw3` (la que tiene `data/`, `mods/`, `saves/` y `settings.ini`).
- Copia los ejemplos a `mods/`, corre los generadores (`pc/examples/tools/`, necesitan `python3`; el video necesita Pillow) y agrega
  cada ejemplo instalado a `[mods] disabled` de `settings.ini`: **nada cambia hasta que actives uno**. Antes guarda
  `settings.ini.bak`. Nunca toca `saves/`.
- Los grupos (`--only`): `tables` (10), `content` (6) y `media` (9). La pestaña *Mods* se desplaza por todos los
  mods instalados. Con `--list` solo imprime los nombres.
- Un ejemplo cuyos datos aún no existen se salta con un aviso: `stage-tiles` necesita el escenario exportado (`[data]`
  `export_stages = S330`, o *Options > System > Prepare modding files*), los de imagen y texto necesitan haber abierto el juego
  una vez con tu disco. Vuelve a correr el instalador después.
- Windows (`%APPDATA%\dw3port\dw3`) y Android: el script es `sh`; en Windows corre con Git Bash o copia las carpetas de
  `pc/examples/mods/` a `mods\`, corre a mano `python3 pc/examples/tools/gen_*.py <carpeta de usuario>` y agrega los nombres a
  `[mods] disabled`. En Android copia las carpetas desde la PC a la carpeta de datos de la app (los generadores se corren en la PC
  sobre una copia de `data/`).

## Probarlos

Cada carpeta tiene un `README.txt` (en inglés) con qué cambia y dónde verlo. `boot "<disco>.bin" --validate-mod <nombre>` los
revisa. Para quitarlos, borra la carpeta o su nombre de `[mods] disabled`.

| Mod | Tipo | Qué cambia | Dónde verlo |
|---|---|---|---|
| `cheap-shops` | tabla | precios de compra a la mitad (`item_prices`) | tienda de objetos 0, lista COMPRAR |
| `double-rewards` | tabla | EXP y dinero x2 (`battle_rewards`) | pantalla de resultados de cualquier batalla |
| `generous-loot` | tabla | botín al doble de probabilidad (`battle_loot`) | resultados de una batalla (aparece el objeto) |
| `shop-extra-item` | tabla | un objeto más en la tienda 0 (`shop_stock`) | tienda 0, COMPRAR, página 2 |
| `fast-growth` | tabla (E5b) | el doble de estadísticas por nivel (`stat_gains`) | subir de nivel en una batalla y abrir Status |
| `field-encounters` | tablas (E5c) | los dos primeros enemigos del Parque Central se intercambian y las batallas vienen el doble de seguido (`field_enemies`, `battle_rates`) | caminar por el Parque Central (escenario 330) |
| `gym-inn-prices` | tablas (E5e) | posadas a 1 BIT y intensidades del gimnasio a la mitad (`inns`, `gym_intensity_costs`) | hablar con el posadero; entrenar en el gimnasio |
| `card-rival-deck` | tabla (U8a) | todos los rivales abren con diez copias de la misma carta (`card_opponents`) | cualquier duelo de cartas |
| `strong-weapons` | tabla (E5a) | armas con el doble de ataque (`weapon_data`) | tienda de objetos 0: seleccionar un arma y botón Detalles (círculo) |
| `text-upper-items` | texto | nombres de objetos en mayúsculas (se genera de tus textos) | listas de las tiendas, bolsa y menú de objetos |
| `menu-frames-gold` | imagen (menú) | marcos de la tienda en dorado (solo paleta; se genera) | tienda de objetos |
| `player-recolor` | sprites del campo | la ropa azul del protagonista pasa a rojo (se genera) | caminar por cualquier escenario |
| `stage-tiles` | baldosas de escenario (E6b) | el mapa del Parque Central en violeta (se genera de tu export `S330`) | caminar por el Parque Central |
| `stage-objects` | objetos de escenario (E6a) | los objetos del escenario 233 en violeta (se genera) | escenario 233: la pantalla grande sobre la plataforma |
| `stage-npc` | escenario: personajes, salida y evento (S4-S8) | un personaje se mueve junto al inicio y otro nuevo saluda con texto propio (en inglés y es-419), da 100 BIT una vez, recuerda la visita con una bandera del mod y empieza un evento con un mensaje; además una salida nueva (un rectángulo al este del inicio) lleva al escenario 380 | escenario 330 (`warp:330`): los dos personajes cerca de donde apareces; hablar con el nuevo; caminar al este hasta el portón y pulsar cruz |
| `sfx-beep` | audio: efecto (E3c) | la muestra 50 del banco COMMON se reemplaza por un pitido sintético | menú del campo (Start): el sonido del cursor |
| `song-title-chime` | audio: canción (E3d) | la música del título se reemplaza por un carillón sintético en bucle | pantalla de título |
| `video-bars` | video (E2b) | la película de apertura se reemplaza por barras de colores sintéticas | al iniciar el juego, antes del título |
| `sample-item` | contenido | un objeto nuevo, Test Gadget, vendido en la tienda 0 y soltado por todos | tienda 0 y después de cualquier batalla |
| `sample-tech` | contenido | una técnica nueva que aprende la forma base 3 | menú Tech de la batalla |
| `sample-card` | contenido | una carta nueva con arte propio: tienda 49, sobre 91 y un rival | tienda de cartas 49, álbum |
| `sample-digimon` | contenido | cambia al compañero inicial y un enemigo y agrega una forma nueva | estadísticas en batalla y laboratorio |
| `sample-enemy` | contenido | un enemigo nuevo en dos encuentros del Parque Central | batallas del Parque Central |
| `sample-model` | contenido | un enemigo con modelo 3D propio (robot de cajas) | batallas del Parque Central |

Notas: `sample-enemy`, `sample-model` y `field-encounters` tocan los mismos encuentros del Parque Central: actívalos de a uno.

## Sin ejemplo: archivos crudos (E7)

No hay ejemplo de reemplazo de un archivo crudo (`files/`): los archivos sin formato abierto son cajas negras y no hay un cambio
seguro y visible que se pueda describir sin copiar bytes del disco. El único probado (`S330TMPK.BIN` con la rejilla de colisión)
necesita editar bytes sin documentación. La ruta está explicada en `docs/modding/README.md` §6.4.
