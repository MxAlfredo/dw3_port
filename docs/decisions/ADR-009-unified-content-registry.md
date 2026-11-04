# ADR-009 — Registro de contenido unificado

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-31: "el que más recomiendes para una buena experiencia de modding y
  que el port siga tan estable como hoy")

## Contexto

Pedido del dueño (2026-10-31): quitar los topes de PS1 de todas las tablas y, donde se pueda, reemplazar la duplicación
por relaciones sobre ids o referencias comunes, para objetos, técnicas, Digimon y lo que siga.

Lo que muestran las investigaciones R1 y R2:

- **Un Digimon vive en tres tablas** con un solo espacio de ids, sin cruces: `DIGIMON_DATA` (55 formas de compañero,
  ejecutable), la tabla de batalla (193 enemigos, archivo 0x1CF) y la de luchadores (248 entradas con modelo, efectos,
  cara y cámaras, archivo 0x1CC). Además hay dos formas de nombrar una forma de compañero: su id y "fila + 1"
  (`pairPartner`, `blastForms`, `Evolution.digimon`).
- **Cada familia de contenido repite el mismo mecanismo.** Objetos (ADR-008, `pc/shim/item_ext.c`) tiene su asignador de
  ids, su almacén extendido, su gancho de texto y su trozo del guardado; técnicas (T2) repite lo mismo; Digimon sería
  una tercera copia.
- **Las relaciones son números sueltos.** El botín apunta a un id de objeto, una forma de compañero a seis ids de
  técnica, una técnica a una ranura de animación del luchador; nada se valida y un número fuera de rango corrompe o
  cuelga (R1: `getArchiveEntry` sin límites).

## Decisión

### 1. Una capa del port como fuente de verdad, sin reescribir el juego

El código C del juego (`src/`) **no cambia sus estructuras ni sus tablas**. Sigue leyendo por los accesores que ya
existen o se agregan (`PC_ITEM_*`, `PC_TECH`, `ITEM_FUNCS`, `GET_DIGIMON`, los de Digimon que vengan), y esos accesores
pasan a ser vistas del registro del port.

Por qué no reescribir las tablas de `src/`: rompería la prueba del build de PS1 (`make VERSION=eu compare`, 316 OK), los
merges desde upstream (ADR-003) y la garantía de que sin mods el juego es idéntico. Con accesores se obtiene lo mismo
(sin topes, con relaciones) tocando solo los puntos de acceso, como ya se hizo con objetos (90 sitios) y técnicas (84).

### 2. Nombre estable para todo el contenido

- Original: `dw3:<tipo>/<id>` (por ejemplo `dw3:item/43`, `dw3:tech/12`, `dw3:digimon/3`). Nuevo: `<mod>:<llave>`.
- Los mods, las relaciones y las partidas guardan **nombres**; el número interno lo asigna el port al arrancar y puede
  cambiar entre instalaciones sin romper nada (como ya hacen los objetos de ADR-008).
- Los mods siguen pudiendo escribir números para el contenido original, por compatibilidad con lo ya documentado.

### 3. Un núcleo genérico, un tipo por familia

Un solo núcleo en `pc/shim/` con lo que hoy se repite:

- registro de un tipo: nombre, rango original, tamaño del registro, cómo copiar una base y validar los campos;
- asignación determinista de ids extendidos (orden alfabético del nombre completo);
- almacén de registros extendidos y resolución nombre ↔ id;
- textos (nombre, descripción por idioma, con caída de idioma y ajuste al cuadro);
- trozo de nombres en `dw3save-v1` por tipo, con reasignación al cargar y descarte seguro de lo que ya no existe;
- vista legible (`slotN.json`) y líneas de log comunes.

Objetos y técnicas pasan a ser los dos primeros tipos del núcleo, sin cambio de comportamiento ni de formato para el
modder (sus pruebas actuales siguen pasando). Digimon es el tercero.

### 4. Digimon como una sola entidad con facetas

Para el modder un Digimon es un solo registro con facetas opcionales:

- `partner`: la forma de compañero (lo de `DIGIMON_DATA`);
- `enemy`: su versión enemiga (lo de la tabla de batalla);
- `fighter`: modelo, movimientos, efectos, cara y cámaras (lo de la tabla de luchadores).

El port reparte cada faceta en la tabla que el juego lee. Las dos formas de nombrar una forma de compañero (id y
"fila + 1") quedan escondidas detrás del accesor.

### 5. Relaciones por nombre y validadas al cargar

Botín, técnicas que aprende un Digimon, evoluciones, recetas, encuentros, modelo de un luchador: cada relación guarda
un nombre y se valida al cargar los mods. Una relación que no resuelve, o que es incompatible (por ejemplo una técnica
cuya ranura de animación no existe en el luchador, R1), se rechaza con una línea de log y **nunca llega al juego**.

### 6. El disco no cambia

Los archivos originales se leen tal cual; la capa los traduce. Nada del disco entra al repositorio (ADR-001).

### 7. Pensado también para Android

El build de Android ya está planeado (NDK con la plantilla de SDL y controles táctiles, `controls-config.md` C7). Para que
el registro sirva igual ahí (pedido del dueño, 2026-10-31):

- **Todo en C portable y dentro del juego.** Cargar mods, validar relaciones y comprobar compatibilidades (como la de
  animaciones de R1) lo hace el port al arrancar, leyendo el disco. Nada en tiempo de ejecución depende de Python ni de
  herramientas externas; las herramientas de `tools/` son para quien desarrolla o crea mods, y su lógica, si el juego la
  necesita, vive en C.
- **Rutas solo por la capa de archivos del port** (`port_fs`, ADR-005): ninguna ruta absoluta ni función propia de un
  sistema; en Android la carpeta de datos la da SDL, y los mods se instalan dentro de ella.
- **Formatos de texto simples y estables** (JSON con versión en el nombre del formato, como `items-v1`), iguales en las
  tres plataformas; nombres de archivo en minúsculas (el sistema de Android distingue mayúsculas).
- **Memoria acotada y carga perezosa**: el registro reserva según lo que hay, sin tablas fijas grandes, y lee solo lo que
  usa; nada de cargar todo el disco por adelantado.
- **Errores que no cierran el juego**: un mod malo se rechaza con su línea de log y el juego sigue (en Android no hay
  consola, así que el log y un aviso dentro del juego son la única pista).

## Consecuencias

- Una sola forma de agregar o modificar contenido para el modder, y un solo lugar donde arreglar errores en el port.
- Refactor mediano antes de Digimon: extraer el núcleo de objetos y técnicas sin cambiar su comportamiento (las pruebas de
  host y una corrida real lo prueban).
- Los topes de estructuras fijas (44 formas por compañero, 8 compañeros, 6 técnicas por entrada) siguen el criterio M3
  de ADR-008: se amplían cuando un contenido concreto lo pide, con su prueba.
- La prueba de PS1 (316 OK) sigue siendo obligatoria cada vez que se toca `src/`.

## Orden

1. Terminar técnicas (T2-T3 de `content-techs-digimon.md`) con el diseño actual: dan el segundo caso real.
2. Extraer el núcleo con objetos y técnicas como primeros tipos (sin cambio de comportamiento).
3. Digimon sobre el núcleo, en el orden de R2: modificar existentes, nuevos sobre un modelo existente (enemigos
   primero), formato de modelos, compañeros nuevos.
