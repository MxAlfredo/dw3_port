# ADR-008 — Estado de partida extendido (sin límites de PS1)

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-30: opción B y los valores que se indican abajo)

## Contexto

Principio del dueño (2026-10-30): el port reacomoda lo necesario para que los topes de la consola no limiten
contenido nuevo ni optimización; sin mods el juego es idéntico al original. Precisión del mismo día: que la
tarjeta siga válida para el juego original es una **preferencia, no un requisito**; si complica el diseño, el
port puede tener su propio formato de partida, aunque una tarjeta de PS1 no pueda llevar el contenido nuevo (importar
tarjetas viejas es deseable, no obligatorio).

`docs/extended-state.md` inventaría los topes. Lo que decide este ADR:

- El guardado es una **copia cruda de 0x26C4 bytes** de `GAME` con XOR de bytes (`saves.c:405,486-494`,
  `memcard.c:444`), en una de 3 secciones de 0x2700 de un archivo de tarjeta de tamaño fijo (`memcard.c:14-15`).
- Los arreglos por id están **pegados y se indexan directo** (`GAME.items[id]`: 42 sitios variables en 20 archivos;
  `equippedItems` y `cards`: 24 más). Un id fuera de rango corrompe el vecino sin avisar.
- Las tablas (`ITEMS` 402, `TECHS` 443, `DIGIMON_DATA` 55 con `findDigimon` a 52) también son de tamaño fijo, pero
  sus accesos pasan casi todos por funciones o punteros que el port ya parchea (ADR-007).

## Decisión

### 1. Estado extendido del port, junto a `GAME`

El port tiene un `PcExtState` propio (código en `pc/shim/`, no en `src/`) con lo que el original no puede guardar.
`GAME` conserva su disposición exacta (es lo que el C del juego conoce). El estado extendido se organiza por
**tres mecanismos**, uno por clase de límite:

- **M1, campos que guardan un id** (equipo `s16`, cartas de mazo `s16`, `PartnerEntry.id` `s16`, técnicas de 13 bits
  hasta 8191, lista de tienda, premio de batalla): el vivo en `GAME` **puede** llevar un id extendido, porque el ancho
  alcanza. Lo que hace falta es que la tabla a la que apunta tenga la fila (ADR-007 §4).
- **M2, arreglos indexados por id** (`items`, `equippedItems`, `cards`, `cardsSeen`): las cuentas de ids >= al
  tope original viven en arreglos de `PcExtState`, y el acceso pasa por un accesor (§3).
- **M3, estructuras de cuenta fija** (8 socios, 44 entradas por socio, 6 técnicas por entrada, 3 mazos, bits de
  banderas): se amplían caso por caso cuando un contenido concreto lo pida, no por adelantado. Cada ampliación entra
  con su documentación y su prueba.

Los topes de las tablas del código (`ITEMS`, `TECHS`, listas por tipo, `findDigimon` a 52, bucles a 315 cartas) se
levantan al mismo tiempo que cada familia: con una tabla propia del port detrás de `ITEM_FUNCS.get/list/isKind`
(`items.c:890`) y con guardas `DW3_PC` mínimas en los bucles con tope literal (ADR-004).

### 2. Ids

- El contenido original conserva sus números.
- Lo agregado por un mod se nombra `<mod>:<nombre>` (ADR-006 §3). El port asigna números **por familia** en el rango que
  empieza justo después del original (objetos desde 403, cartas desde 316, técnicas desde 444, Digimon desde la
  fila 56) al cargar los mods, en orden **alfabético de la llave**, de modo que el número no dependa del orden de
  carga de los mods.
- **Refinamiento de ADR-006 §3 (a aprobar):** los números **no se persisten**. Todo lo que se guarda va **por nombre**
  (`<mod>:<nombre>`), así que cambiar los mods instalados o su orden nunca reasigna objetos de una partida. La
  asignación del puerto es solo de la sesión.
- Topes iniciales del port, configurables en `config/limits.json` (valores propuestos, no medidos): +1024 objetos,
  +512 cartas, técnicas hasta 8191 (el ancho de 13 bits de `SKILL_ID`), +64 Digimon. Cada tope se registra en el log
  al arrancar.

### 3. Cómo llega el código del juego a las entradas extendidas

1. **Por punteros ya existentes, sin tocar `src/`:** `ITEM_FUNCS.get/list/isKind/category` y `GAME.funcs.*` de
   socios y mazos se redirigen en el arranque a funciones del port que conocen la tabla extendida.
2. **Accesor para las cuentas (M2).** Hay ~66 sitios variables que escriben `GAME.items[x]`, `GAME.equippedItems[x]` o
   `GAME.cards[x]` directo. Propuesta: una macro en un encabezado del port incluido desde `game_state.h` (3 líneas):
   `PC_ITEM_COUNT(x)` expande, **sin `DW3_PC`, al texto idéntico `GAME.items[x]`**, y con `DW3_PC` a una referencia a la
   casilla de `GAME` si `x` es original o a la de `PcExtState` si no. Los sitios se cambian a mano en un commit propio
   por familia (ADR-004: guarda, mínimo, y la salida del preprocesador sin `DW3_PC` es idéntica; `make compare` sigue en
   316 OK).
3. **Cerco del compilador:** en el build de PC el miembro `items` se renombra (`itemsCard`), así que un sitio olvidado
   **no compila** en vez de corromper en silencio.
4. **Búferes de lista:** los 13 `s16 [0x194]` (`extended-state.md` §3) pasan a una macro de tamaño (`PC_ITEM_LIST_MAX`), que
   en PS1 vale 0x194.

Alternativa descartada por ahora: agrandar los arreglos dentro de `GameState` para que `GAME.items[x]` funcione sin
cambiar sitios. Desplaza todo lo que sigue y rompe lo que copia `GAME` por tamaño (`newGame`
`state.c:6`, `GAME_SAVE`, `MemCardSaves.pad[]` `memory_card.h:158`, `SaveData`), de modo que el guardado exigiría de todos
modos un códec; se reabre si los 66 sitios resultan una carga de merge mayor que la esperada con `upstream`.

### 4. Guardado: dos opciones

Ambas comparten §1-3 (el estado vivo extendido es igual). Lo que cambia es **dónde y cómo se persiste**.

#### Opción A — tarjeta válida + archivo auxiliar (lo que dice ADR-006 §4)

- La tarjeta guarda solo la parte original, **sin ningún id que el original no conozca**. Al guardar, un códec copia
  `GAME`, **reemplaza por un valor neutro** todo id extendido que haya en campos M1 (equipo, técnicas de entradas,
  mazos, entradas de Digimon, premio) y vuelca M2/M3 al auxiliar `saves/<tarjeta>.slot<N>.mods.json`, por nombre.
- **Emparejar casilla y auxiliar:** el auxiliar lleva una marca de 16 bytes que el códec también escribe en los bytes sin
  uso de la tarjeta (`pad10`, `game_state.h:478`; H: nada los lee). Si la tarjeta la cambió otro programa o el original
  (la marca falta o no coincide), el auxiliar se ignora con una línea de log y se guarda `.bak`.
- **Falta el auxiliar o un mod:** el contenido extendido se reemplaza por algo neutro (se quita el objeto, la carta o la
  técnica); nunca una tarjeta rota.
- Ventajas: la tarjeta sigue abriendo en emulador y en el original; ADR-006 §4 ya lo dice; no se toca la UI de
  tarjetas ni `host_memcard.c`.
- Costos: hay que **neutralizar cada campo de id** del §2 del inventario (al menos 9 campos M1 distintos) y mantener esa
  lista al día; cada campo nuevo es un riesgo de "id desconocido en la tarjeta"; la marca depende de bytes sin uso aún
  sin verificar; dos archivos que pueden desincronizarse; un guardado no es atómico entre tarjeta y auxiliar (el
  corte entre ambas escrituras da un par mal emparejado).

#### Opción B — formato propio del port (con importación de tarjetas de PS1, de una vía)

- Cada casilla es un archivo `saves/<partida>/slot<N>.dw3save` (formato versionado `dw3save-v1`): cabecera con magia y
  versión del port, lista de mods usados (nombre y versión), el bloque de la parte original **tal cual** (los 0x26C4 bytes
  de `GAME`, ya con ids extendidos donde el ancho los admite), trozos de extensión (M2, M3) por nombre, y CRC32 de todo.
  Se escribe a un temporal y se renombra (atómico).
- El menú de partidas del juego (`STGMCARD_*`) no cambia: el port sirve una **tarjeta virtual** detrás de las mismas
  llamadas de `host_memcard.c`; la sección de datos del juego es el bloque original y el resto lo toma la capa del port
  en el mismo guardado (H: el cierre de la escritura de la sección de datos, `saves.c:530`, es el punto de enganche).
- **Importación:** si no hay partidas nativas, el port ofrece leer la `memcard0.mcd` existente (formato estándar,
  `host_memcard.c:1-30`) y crea las casillas nativas con el bloque original y sin extensión. Es de una vía: el port no
  escribe tarjetas de PS1. Una tarjeta vieja nunca se modifica.
- **Falta un mod:** igual que A, el contenido por nombre sin mod se reemplaza por algo neutro con una línea de log; la
  casilla no se rompe y el mod que falta se recuerda en la cabecera para mostrarlo en el menú de mods.
- Ventajas: **un solo códec y un solo archivo**; ningún campo hay que neutralizar (los ids extendidos viajan tal cual
  y el CRC32 detecta corrupción mucho mejor que el XOR); guardado atómico; no depende de bytes sin uso; el diseño no
  limita lo que se agregue después (más socios, más técnicas, más mazos, almacén de banderas de scripts) porque el
  formato es nuestro.
- Costos: una tarjeta del port ya no abre en emulador ni en el original (ni cualquier otro programa de tarjetas); hay que
  escribir y probar el formato, el lector, la importación y un menú que muestre las casillas nativas con los mismos
  datos de resumen (`MemCardFile`, `memory_card.h:84-90`); relaja ADR-006 §4 ("el port no guarda ahí nada que el
  original no entienda").

#### Recomendación: B

Evidencia del inventario:

1. **La serialización original es trivial de envolver:** es una copia de struct (`saves.c:405,486`), así que el bloque
   original entra en el archivo nuevo sin transformarlo. La dificultad de B es el contenedor y la UI, no el estado.
2. **A crece con cada campo.** Hay al menos nueve campos que guardan un id (`extended-state.md` §2: equipo, técnicas de
   entradas, entradas de socio, mazos, premios, listas de tienda...). Cada uno necesita regla de neutralización en A y
   cada campo que se agregue en el futuro puede dejar un id desconocido en la tarjeta, que es justo el fallo que A
   promete evitar. En B no hay esa lista.
3. **A no puede ampliar las estructuras fijas (M3)** (8 socios, 44 entradas, 6 técnicas, bits de banderas): su tarjeta
   no tiene sitio para ellas (sobran solo 0x3C bytes por casilla, `memcard.c:14-15`, y el tamaño de sección es el del
   original). El auxiliar sí podría llevarlas, pero entonces la tarjeta ya no representa la partida y se pierde la
   ventaja que justificaba A.
4. El dueño dejó la compatibilidad de tarjeta como preferencia; B la sacrifica solo hacia afuera (no escribir en PS1),
   y la importación conserva las partidas viejas.

Riesgo de B para vigilar: el punto de enganche en la sección de datos (H). Si no sirve, B se implementa a nivel de
sustituir `readSave/writeSave` (`memcard.c:177-300`) en el port.

### 5. Interfaz

- Las listas de objetos tienen búferes de 404 entradas (`s16 [0x194]`, 13 sitios) y líneas por página fijas (7 de
  objetos, 6 de técnicas, `menu.h:65,81`); H: la paginación es por cursor y alcanza para listas largas, a confirmar con
  una corrida headless del inventario con cientos de tipos.
- Los nombres y descripciones se indexan por id en los archivos de texto del juego (`report.c:139-140`); un id nuevo
  trae su cadena por la capa de texto del port (por nombre, no por índice del disco), con la regla de neutralidad de
  localización.
- Los scripts del juego solo nombran ids hasta 511 (`events.c:496-550`): el contenido nuevo se otorga por tiendas,
  botines y por la fase 3 (scripts), no por eventos originales.

### 6. Orden de construcción

1. **Fase 0, base sin comportamiento nuevo:** `PcExtState`, el accesor `PC_ITEM_COUNT` aplicado a **todos** los sitios de
   objetos con el cerco `itemsCard`, y prueba de que el build de PS1 no cambia (preprocesador sin `DW3_PC` idéntico,
   `make compare` 316 OK). Sin mods, partida idéntica.
2. **Fase 1, primer paso concreto, objetos de punta a punta:** un objeto nuevo `<mod>:<nombre>` con (a) fila en una
   tabla `ITEMS` del port tras `ITEM_FUNCS.get` y entrada en su lista por tipo (ADR-007 §4), (b) nombre por la capa de
   texto, (c) comprable por `shop_stock`, (d) cuenta en `PcExtState`, (e) guardar y cargar con el formato elegido en
   §4, (f) quitar el mod y recargar: el objeto desaparece con una línea de log y la partida carga. Pruebas de host:
   ida y vuelta del códec, sin mods el archivo equivale al original, mod faltante, archivo corrupto. Mod de ejemplo
   `pc/examples/mods/`.
3. **Fase 2:** cartas (`cards`, `cardsSeen`, bucles a 315) y técnicas (`TECHS` y ranuras, ids hasta 8191).
4. **Fase 3:** Digimon (`DIGIMON_DATA`, `findDigimon` a 52, entradas por socio) y las estructuras M3 que algún contenido
   necesite.
5. Banderas y progreso de scripts: con la fase 3 de ADR-006 (almacén de claves propio del port).

## Decisión del dueño (2026-10-30)

- **Guardado: opción B**, formato propio del port (`dw3save-v1`). Relaja ADR-006 §4: la tarjeta de PS1 deja de ser el
  formato de las partidas del port; ADR-006 no se edita y esta nota manda.
- **Topes: sin tope fijo.** Las tablas y conteos del estado extendido son del port y crecen según lo que agreguen los
  mods instalados; solo hay un límite de seguridad alto por tabla (documentado, para que un mod con errores no agote
  la memoria). Los números de §-topes de este documento quedan como ejemplos, no como límites.
- **Importador de tarjetas viejas (`.mcd`)** incluido desde la primera versión de B, para no perder partidas.
- **Pantalla de partidas propia del port** en lugar de las pantallas de tarjeta, manteniendo el estilo del juego.
- **Acceso por macro** a los ~66 sitios de `src/` (idéntico sin `DW3_PC`, ADR-004), aceptado.
- El proyecto no hace mods de contenido: construye y documenta la capacidad (ADR-006 §1).

## Consecuencias

- Sin mods, ningún arreglo extendido se usa y el juego es idéntico (los cambios en `src/` son macros que expanden al texto
  original sin `DW3_PC`).
- Si se elige la opción B, este ADR **relaja ADR-006 §4** (la tarjeta de memoria deja de ser el único soporte de las
  partidas del port); ADR-006 no se edita aquí y se actualiza con una nota al aceptar B.
- El refinamiento de ADR-006 §3 (números no persistidos, todo por nombre) vale en las dos opciones.
- Cada tope levantado queda registrado en `docs/extended-state.md` con su sitio y su prueba.
- Preguntas abiertas para el dueño: A o B (la recomendación es B); topes iniciales (§2); si la importación de tarjetas
  viejas entra en la primera versión de B o después; si el menú de partidas nativo se acepta como pantalla del port
  (con los mismos datos de resumen) en lugar de las de tarjeta.
