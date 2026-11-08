# sample-model: un enemigo y un compañero con modelo propio (mod de contenido)

Dónde verlo: batallas al azar del Parque Central (escenario 330), encuentros 34 y 35. El segundo enemigo es
"Cajabot" ("Boxbot" en inglés), nivel 3: un robot de cajas con rayas (azul, verde y rojo) y un brazo amarillo.
El mensaje de la batalla dice su nombre.

Ejemplo mínimo de un Digimon enemigo con **arte propio**: un robot de cajas hecho de una fuente neutra (glTF 2.0 + PNG + `model.json`)
que el port **hornea al cargar** al formato de modelo del juego. No trae nada del disco.

## Qué trae

- `digimon/boxbot/model/`: la fuente neutra. `robot.gltf` + `robot.bin` + `robot.png` son un robot de 4 huesos (cadera, torso, cabeza y brazo)
  con 48 triángulos, una textura de 64x64 y una animación de reposo que balancea el brazo; `model.json` (`model-src-v1`) dice la altura
  (`fitHeight`: 1800), qué animación va a qué ranura de movimiento, el luchador **donante** de los efectos de las técnicas (`dw3:digimon/383`)
  y la plantilla (`dw3:digimon/4`) y la distancia de cámara (`info.distance`). Se generó con
  `python3 -I tools/test_model_import.py --demo <carpeta>` (renombrando `robot.json` a `model.json`).
- `digimon/boxbot/digimon.json` (`digimon-digimon-v1`): el enemigo `sample-model:boxbot` ("Cajabot" en es-419). No lleva `"model"`: sin él,
  el `model/` de su propia carpeta es su modelo.
- `content/encounters.json` (`encounters-v1`): dos encuentros del Parque Central (34 y 35) donde aparece en el segundo hueco.

## El compañero `boxpal`

`digimon/boxpal/` es una **forma de compañero** (`"base": "dw3:digimon/383"`, Kotemon) con su propio modelo: un robot de cajas con las animaciones de un compañero
(reposo, reposo débil, esquivar, encogerse, golpe, caída, festejo, ráfaga y ataque). Su `model/model.json` muestra cómo se nombra cada animación y a qué ranura de movimiento va
(`"motions"`; la lista de ranuras, las 12 cámaras y los límites están en `docs/modding/README.md` §8.2c y `docs/formats.md` 4.6, punto 5b); las cámaras y los efectos de las técnicas salen de Kotemon
(`"template"` y `"effects"`). Se genera con `python3 -I tools/test_model_import.py --demo-partner <carpeta>` (renombrando `partner.json` a `model.json`) y se revisa con
`python3 -I tools/model_import.py digimon/boxpal/model/model.json --check --partner`. Para verlo en batalla sin jugar hasta obtenerlo (Kotemon nivel 3):
`DW3_SEED_PARTNER="0.0=sample-model:boxpal@1/0"` y una batalla del arnés (`pc/review.sh batalla`, `DW3_BATTLE_ENEMY=63`).

## Qué pasa al cargar

El log dice `[models] sample-model:boxbot: baked 5 bones, 48 polygons, ...` la primera vez, y `... from the cache ...` las siguientes (la copia horneada
queda en `cache/models/` de la carpeta del usuario). Si algo de la fuente falla, `[models] sample-model:boxbot rejected: <por qué>` y Cajabot usa el modelo
de su plantilla.

## Hacer el tuyo

Cambia el glTF y el PNG por los tuyos (Blender: *Archivo > Exportar > glTF 2.0*), ajusta `model.json` y deja el resto igual. La guía completa, con todas las
claves y los límites, está en `docs/modding/README.md` §8.2. Para revisar una fuente sin abrir el juego: `python3 -I tools/model_import.py digimon/boxbot/model/model.json --check`.
