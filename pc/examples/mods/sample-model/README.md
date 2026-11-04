# sample-model: un enemigo con modelo propio (mod de contenido)

Dónde verlo: batallas al azar del Parque Central (escenario 330), encuentros 34 y 35. El segundo enemigo es
"Cajabot" ("Boxbot" en inglés), nivel 3: un robot de cajas con rayas (azul, verde y rojo) y un brazo amarillo.
El mensaje de la batalla dice su nombre. No lo prendas junto con `sample-enemy` (usan el mismo lugar).

Ejemplo mínimo de un Digimon enemigo con **arte propio**: un robot de cajas hecho de una fuente neutra (glTF 2.0 + PNG + `model.json`)
que el port **hornea al cargar** al formato de modelo del juego. No trae nada del disco.

## Qué trae

- `content/models/robot/`: la fuente neutra. `robot.gltf` + `robot.bin` + `robot.png` son un robot de 4 huesos (cadera, torso, cabeza y brazo)
  con 48 triángulos, una textura de 64x64 y una animación de reposo que balancea el brazo; `model.json` (`model-src-v1`) dice la altura
  (`fitHeight`: 1800), qué animación va a qué ranura de movimiento, el luchador **donante** de los efectos de las técnicas (`dw3:digimon/383`)
  y la plantilla (`dw3:digimon/4`) y la distancia de cámara (`info.distance`). Se generó con
  `python3 -I tools/test_model_import.py --demo <carpeta>` (renombrando `robot.json` a `model.json`).
- `content/digimon.json`: el enemigo `sample-model:boxbot` ("Cajabot" en es-419) con `"model": "sample-model:robot"`, y dos encuentros del Parque
  Central (34 y 35) donde aparece en el segundo hueco, igual que `sample-enemy`.

## Qué pasa al cargar

El log dice `[models] sample-model:robot: baked 5 bones, 48 polygons, ...` la primera vez, y `... from the cache ...` las siguientes (la copia horneada
queda en `cache/models/` de la carpeta del usuario). Si algo de la fuente falla, `[models] sample-model:robot rejected: <por qué>` y Cajabot usa el modelo
de su plantilla.

## Hacer el tuyo

Cambia el glTF y el PNG por los tuyos (Blender: *Archivo > Exportar > glTF 2.0*), ajusta `model.json` y deja el resto igual. La guía completa, con todas las
claves y los límites, está en `docs/modding/README.md` §8.2. Para revisar una fuente sin abrir el juego: `python3 -I tools/model_import.py content/models/robot/model.json --check`.
