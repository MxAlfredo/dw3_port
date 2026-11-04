# Mod de ejemplo: player-recolor

Pinta de rojo lo azul del protagonista en el campo (en la prueba, la gorra y la ropa). Solo cambia la **paleta** de cada
cuadro de sus sprites (`field/sprt/p001play`, `field/sprt/p002play` y los archivos de poses `p002plyd` caída, `p002plyf` minijuego, `p002plyk` búsqueda y `p002plyl` trepar/escaleras); los píxeles quedan idénticos.
Cada pose del jugador vive en su propio archivo del disco (el juego elige el archivo según la animación, `FIELDSTG_animateActor`), y cada cuadro lleva su paleta: un archivo que no se recolorea se ve con los colores originales.

Esta carpeta **no trae imágenes del juego**: no se pueden redistribuir (ADR-001). Los PNG del mod se generan en tu
equipo, a partir de los que el port extrajo de tu propio disco.

## Instalar (Linux)

1. Abre el juego una vez con tu disco, para que el port extraiga las imágenes a
   `~/.local/share/dw3port/dw3/data/images/` (los sprites quedan en `field/sprt/<nombre>/<EEE>.png`).
2. Copia el mod y genera sus imágenes (desde la carpeta del repositorio):

```
cp -r pc/examples/mods/player-recolor ~/.local/share/dw3port/dw3/mods/
python3 tools/image_mod_palette.py --sprite p001play --sprite p002play --sprite p002plyd --sprite p002plyf --sprite p002plyk --sprite p002plyl --to 0
```

El segundo comando copia cada cuadro de esos seis archivos a `mods/player-recolor/images/field/sprt/<nombre>/` cambiando
solo su paleta: los matices entre 190 y 270 grados (los azules) pasan al matiz 0 (rojo), con la misma saturación y brillo.
Otro color: `--to 120` (verde), `--to 45` (dorado). Otros rangos: `--from-hue LO HI`. Otro personaje: `--sprite p006agum`
(el nombre del archivo del disco; `--list` muestra todo lo extraído, o pasa una carpeta, por ejemplo `field/sprt/p006agum`).
En Windows el mismo script usa `%APPDATA%\dw3port\dw3\` (o `--data` y `--mod`, o `DW3_DATA_DIR` y `DW3_MODS_DIR`).

3. Abre el juego y camina por el Parque Central (partida nueva): el jugador va de rojo. El log (`logs/`) dice
   `images: override field/sprt/p002play/<EEE>.png (87) from mods/player-recolor`.
   Con el log en `trace` (canal `images`, o `DW3_IMG_TRACE=1`) también dice cada cuadro que sube (`[images] load field/sprt/p002play/004.png (override)`).

## Reglas de un mod de sprites

Las mismas que las del arte de menús (ver `menu-frames-gold`): mismo ancho y alto que el cuadro original, PNG indexado
de 4 u 8 bits con **a lo sumo 16 colores**, y los índices de los píxeles dentro de 16. Los índices del PNG reemplazan
los del juego (no reordenes la paleta); su paleta reemplaza la del cuadro. Alfa 0 = transparente. Lo que no cumpla se
registra (`images: <ruta> rejected: <motivo>`) y el port usa la siguiente fuente. El `sprite.json` junto a los PNG dice
qué cuadro usa cada animación y dirección; las orientaciones de la derecha son las de la izquierda en espejo.

Los cambios se ven mientras el juego corre (se revisan los archivos una vez por segundo).

## Quitar

Borra la carpeta `mods/player-recolor`. El port nunca escribe dentro de `mods/`.
