# sample-tech: dos técnicas nuevas (mod de contenido)

Ejemplo mínimo de un mod `"kind": "content"` que agrega **dos técnicas nuevas** y hace que una forma de compañero
la aprenda. Usa solo texto propio; no trae nada del disco.

## Qué hace

- `sample-tech:spark-strike`: copia la técnica original 9 (un ataque físico genérico, animación `script` 5) con
  coste de 3 PM y poder 120. Se muestra con su nombre en el menú de técnicas de la batalla. No sale en la pantalla
  Techniques del menú del campo: esa solo lista técnicas de curación.
- `sample-tech:mend-spark`: copia la técnica original 184 (0xB8, una curación de campo) con coste de 2 PM. La
  pantalla Techniques del menú del campo la lista y se puede usar ahí (elegir, objetivo, curar, gasta PM), porque
  el juego decide que una técnica es "de campo" por su técnica base: las originales 0xB8-0xBC y cualquier técnica
  de mod con `"base"` en ese rango (`PC_TECH_IS_FIELD_HEAL`). Cambiar `kind` o `effect` no la saca de la lista;
  para que no sea de campo, basar la técnica en otra. Escenario de revisión: `pc/review.sh tecnicacampo`.
- Dos formas de compañero las aprenden (`learnedBy`):
  - la forma 3 (Agumon, una de las 8 formas base; la usa `pc/review.sh tecnica`): `spark-strike` en la ranura 1 y
    `mend-spark` en la 2, ambas al nivel 2.
  - la forma 374 (Kyubimon, la primera forma de combate del compañero 6; está en el grupo de `libre` y en la
    partida del dueño): `spark-strike` en la ranura 5 (nivel 6) y `mend-spark` en la 4 (nivel 8).
  El juego usa su propio código de aprendizaje; el mod solo cambia la fila de esa forma (`DigimonData.skills[n]` y
  `skillLevels[n - 1]`).
- **Una ranura `learnedBy` se aprende solo si la forma todavía no la llenó.** Después de cada batalla ganada el
  juego da UNA técnica: la primera ranura vacía de la forma cuyo nivel ya se alcanzó (`STFGTREP_addSkill`); nunca
  reemplaza una ranura llena. Con una partida avanzada, las ranuras 1 y 2 ya las llenaron las técnicas originales y
  las del mod no entrarían: por eso la forma 374 usa ranuras posteriores (la 5, vacía en el original, y la 4, donde
  el original traía la técnica 150 al nivel 55; el mod la reemplaza). El nivel del `learnedBy` debe ser igual o
  menor que el nivel de la forma en tu partida.
- Una técnica queda lista para usar en batalla en cuanto está en la ranura. La marca "conocida" (para la pantalla
  del Digilab y para heredarla) la pone el juego por el nivel `knownLevels` de la forma, que un mod no cambia: en la
  forma 374, la ranura 5 se conoce de inmediato y la 4 al nivel 99.
- La partida guarda la técnica **por nombre**.

## Archivos

- `mod.json`: `"kind": "content"` es obligatorio para declarar técnicas.
- `content/techs.json` (`techs-v1`): el mapa `"new"`. La llave (`spark-strike`) es solo minúsculas, dígitos, `-` y
  `_`; el nombre completo es `<carpeta del mod>:<llave>` (aquí `sample-tech:spark-strike`).
  - `"base"` (obligatorio): una técnica original (1..443), por su nombre `"dw3:tech/9"` o por su número, de la que se copia
    todo. Los nombres originales están en `data/content/techs.json` (lo escribe el juego desde tu disco).
  - Cambios opcionales: `mp`, `power`, `accuracy`, `elementPower`, `effectChance`, `effectPower` (números enteros),
    `hitCount` (hasta lo que hace alguna original), `script` (0..17, la animación) y, como índices de tablas del
    juego, `icon`, `kind`, `element`, `family`, `effect`, `scriptStage`, `scriptEffect`, `scriptSound`: solo se
    acepta un valor que alguna técnica original ya use.
  - `"name"` y `"description"`: por idioma (`eng`, `fra`, `ita`, `ger`, `spa`, `es-419`). `eng` en `name` es
    obligatorio; `es-419` cae primero a `spa`, y un idioma que falta usa el inglés. La descripción cabe en una línea:
    una más larga se acorta con `...` y queda una línea en el log.
  - `"learnedBy"`: `{ "<id del Digimon>": { "slot": 1..6, "level": 1..99 } }`. El id es `DigimonData.id`. La técnica
    reemplaza lo que hubiera en esa ranura. Se rechaza (con una línea en el log) si la ranura ya la reclamó otra
    técnica, si el Digimon no existe o si el archivo de su luchador no tiene la animación que la técnica necesita.

## Qué pasa al quitar el mod

Las partidas guardan el nombre, no el número. Sin el mod, la técnica se quita de su ranura al cargar con una línea
en el log (`[saves] technique "sample-tech:spark-strike" is not defined by any installed mod; removed from the
loaded save`) y la partida carga normal; la ranura queda vacía.
Si vuelves a instalar el mod, la técnica no regresa a esa partida (se quitó al cargar); el Digimon la aprende de nuevo
al subir de nivel solo si su ranura sigue vacía y alcanza el nivel.
