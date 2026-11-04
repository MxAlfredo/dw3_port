# ADR-011 — Modelos de batalla nuevos: fuente neutra, horneado al formato del juego

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-31: "de momento sí", tras la investigación I1)

## Contexto

La investigación I1 probó que el formato de los modelos de batalla se puede escribir: Kotemon (383) re-codificado da
mallas, claves y entrada de modelo idénticas byte a byte y capturas de batalla idénticas píxel a píxel; un Kotemon
modificado (paleta cambiada, un hueso escalado) se dibuja tal cual; un archivo más grande que el del disco carga.

Límites del formato del juego (detalle en `model-import.md` y `docs/formats.md` §4.6): textura de 256x256 a 4 bpp con
paletas de 16 colores (8 bpp no cabe en la VRAM), 256 vértices por hueso, piel rígida (un hueso por vértice), 4 modelos a
la vez, 100 callbacks de huesos para todos; con el triple de polígonos de Kotemon el PC cae, por el búfer de primitivas de
la batalla que reserva el port.

Pedidos del dueño: no perder optimización; buscar la mejor manera aunque haya que romper límites de PS1; dejar la puerta
abierta a mucho modding y a un futuro servidor ("dw3 port online", memoria `online-vision`).

## Decisión

1. **Camino A ahora:** un modelo nuevo se convierte al formato del juego y el motor de batalla lo dibuja como a cualquier
   Digimon (mismo animador, misma luz, mismos efectos). Sin cambios en `src/`.
2. **Fuente de verdad neutra:** el modder entrega **glTF 2.0 + PNG + un JSON de metadatos** (nombre `dw3:model/<id>` para
   los originales, `<mod>:<llave>` para los nuevos; huesos, movimientos asignados a las ranuras que usa la batalla y el
   archivo de efectos que toma de un donante). El archivo en formato PS1 es un **derivado** que se hornea; nunca es lo que
   el modder edita ni lo que el mod tiene que traer.
3. **Tres piezas del port** (en `pc/shim/`):
   - **Números de archivo virtuales:** archivos con número >= `FILE_COUNT` servidos por la capa generada de `port_fs`, para
     modelos, movimientos, efectos y, más adelante, escenarios, sin pisar los originales.
   - **Fila nueva de `FighterInfo`** (con cara vacía) en `digimon_ext.c`, para que un Digimon nuevo tenga su propio modelo.
   - **Topes del PC levantados:** el búfer de primitivas de la batalla (y lo que mida la implementación) según el tamaño real
     de los modelos cargados.
4. **Horneado:** primero una herramienta (`tools/`) que convierte glTF al formato del juego, para validar el camino; el
   destino es que **el port hornee al cargar** (C portable, ADR-009 §7), así un mod trae solo la fuente neutra y sirve igual
   en Android y para herramientas sin pantalla.
5. **Después:** el mixto **C2** (misma malla y animación, textura RGBA de alta resolución por `DR_PSYX_TEX` de PsyCross); el
   **camino B** (malla nativa del port, piel suave, mucha geometría) solo si un contenido real lo pide.
6. **No** se rehace ahora toda la carga del juego: cargar lo original ya no cuesta, y rehacerlo obligaría a tocar `src/`.

## Consecuencias

- Un Digimon con arte propio usa el motor del juego tal cual: costo de memoria y de cuadro igual al de un original
  (medido en I1: misma RSS y mismo tiempo).
- Los límites de piel rígida y 16 colores por paleta siguen en el camino A; C2 y B son la salida cuando haga falta.
- Las batallas siguen siendo código de PS1 atado a la GTE y la GPU: un servidor que las decida necesita su propia
  simulación (fuera de este proyecto); los datos neutros sí se cargan sin pantalla.
