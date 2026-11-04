# Mod de ejemplo: menu-frames-gold

Cambia los marcos azules de la tienda de objetos a dorado. Solo cambia la **paleta** de una imagen
(`screen/stshoptm/00_00.png`); los píxeles quedan idénticos.

Esta carpeta **no trae imágenes del juego**: no se pueden redistribuir (ADR-001). El PNG del mod se
genera en tu equipo, a partir del que el port extrajo de tu propio disco.

## Instalar (Linux)

1. Abre el juego una vez con tu disco, para que el port extraiga las imágenes a
   `~/.local/share/dw3port/dw3/data/images/`.
2. Copia el mod y genera su imagen:

```
cp -r pc/examples/mods/menu-frames-gold ~/.local/share/dw3port/dw3/mods/
python3 tools/image_mod_palette.py
```

El segundo comando lee `data/images/screen/stshoptm/00_00.png`, cambia en su paleta los azules a
dorado y escribe `mods/menu-frames-gold/images/screen/stshoptm/00_00.png`. En Windows el mismo script
usa `%APPDATA%\dw3port\dw3\` (o `--data` y `--mod` para otras rutas, o `DW3_DATA_DIR` y `DW3_MODS_DIR`).

3. Abre el juego y entra a una tienda de objetos (por ejemplo la Armería de Asuka): el marco es dorado.
   El log (`logs/`) dice `images: override screen/stshoptm/00_00.png from mods/menu-frames-gold`.

Otro color: `python3 tools/image_mod_palette.py --to 120` (verde; el valor es el matiz en grados).
Otra imagen: `python3 tools/image_mod_palette.py --list` muestra las que hay, y se pasan por nombre.

## Cómo hacer un mod de imágenes

1. Busca la imagen en `data/images/<CARPETA>/<ARCHIVO>/<NN>_<MM>.png` (el nombre dice de qué archivo del
   disco sale; `NN_MM` es la ruta de entradas del archivo). Con el log en `trace` (canal `images`, o `DW3_IMG_TRACE=1`) el log lista las que el
   juego carga (`[images] load <nombre> (disc|override)`).
2. Cópiala a `mods/<tu-mod>/images/<CARPETA>/<ARCHIVO>/<NN>_<MM>.png` (misma ruta relativa) y edítala.
3. Junto a ella pon `mods/<tu-mod>/mod.json` (como el de esta carpeta).

Reglas (v1). Si algo no se cumple, el log dice `images: <ruta> rejected: <motivo>` y el port usa la
siguiente fuente; el juego nunca se detiene.

- Mismo ancho y alto que el original.
- PNG **indexado** (con paleta), no entrelazado, de 4 u 8 bits. Truecolor (RGB/RGBA) se rechaza: guárdalo
  como indexado.
- A lo sumo 16 colores (páginas de 4 bits) o 256 (de 8 bits), y los índices de los píxeles dentro de
  ese número. **Los índices del PNG reemplazan los del juego**, así que no reordenes ni optimices la
  paleta: el editor debe conservar el orden.
- La paleta del PNG reemplaza la que se exportó, en el mismo lugar. Las demás paletas de la página
  siguen como en el disco (una página tiene varias; el PNG solo muestra una).
- Transparencia: una entrada con alfa 0 (`tRNS`) es transparente; las demás son opacas.
- Las páginas de 8 bits (`STWMP*`) toman su paleta de la imagen de 8 bits con CLUT del mismo archivo
  (por ejemplo `01_00`): para recolorearlas edita esa. Las imágenes marcadas con paleta gris (sin
  paleta conocida) cambian píxeles, no colores.

Orden de fuentes: los mods (el último de `order` o, sin `order`, el último alfabético gana), luego una
imagen de `data/images/` que tú editaste (si su contenido cambió respecto al que el port extrajo), y al
final el disco. Los cambios se ven mientras el juego corre: el port revisa los archivos una vez por
segundo y recarga la imagen la próxima vez que el juego la sube a pantalla.

Para ver qué es cada región de una imagen (qué rectángulo es una esquina, una bandera...), junto a cada
PNG hay un `<NN>_<MM>.json` (`atlas-v1`) y `python3 tools/image_atlas_preview.py <png>` dibuja sus piezas
numeradas; ver `pc/README.md`.

## Quitar

Borra la carpeta `mods/menu-frames-gold`. El port nunca escribe dentro de `mods/`.
