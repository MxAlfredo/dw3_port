# ADR-006 — Plataforma de mods: alcance, reglas comunes y fases

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-30)

## Contexto

ADR-005 fijó dónde viven los datos y los mods y cómo se reemplazan archivos. El dueño quiere que la
comunidad pueda ir más allá de reemplazar: agregar eventos, personajes, Digimon y misiones secundarias,
y cambiar reglas del juego (experiencia, objetos, recompensas). Algunas decisiones de hoy, si se toman
mal, impedirían eso después: los identificadores, las partidas guardadas y las dependencias entre mods.

## Decisión

### 1. Alcance: el port es la plataforma, no el autor de mods

- El proyecto construye las **capacidades** (cargar, reemplazar, agregar, programar) y su
  **documentación** en `docs/modding/`. Los mods los hace la comunidad.
- El repo solo trae **ejemplos mínimos** que prueban cada capacidad (como `pc/examples/mods/`), sin datos
  del juego (ADR-001).
- Sin mods, el juego es el original con las mejoras del port; ningún mod viene activado.

### 2. Dos clases de mods

- **Recursos** (ADR-005): reemplazan archivos, texto, imágenes y valores. Son seguros: quitarlos deja
  el juego como estaba.
- **Contenido**: agregan o cambian datos y comportamiento (tablas, eventos, entidades nuevas). Tienen
  reglas extra: identificadores, partidas y dependencias.

### 3. Identificadores

- El contenido original conserva sus números del juego.
- Lo agregado por un mod se nombra `<mod>:<nombre>` (por ejemplo `mi-mod:espada-fuego`). El port le asigna
  un número libre de un rango reservado al cargar, y guarda la asignación con la partida (punto 4), para
  que el mismo objeto conserve su número entre sesiones aunque cambie el orden de los mods.
- Dos mods nunca chocan por número: chocan solo si cambian lo mismo, y entonces gana el orden de carga
  (ADR-005) con un aviso en el log.

### 4. Partidas guardadas

- La tarjeta de memoria (`memcard0.mcd`) sigue siendo una tarjeta de PS1 válida y compatible con el juego
  original; el port no guarda ahí nada que el original no entienda.
- Lo que un mod de contenido necesita guardar va en un archivo aparte, junto a la tarjeta
  (`saves/<partida>.mods.json`, formato versionado), con la asignación de identificadores.
- Si al cargar una partida falta un mod que agregaba contenido, el port avisa y reemplaza ese contenido por
  algo neutro del juego (por ejemplo, quita el objeto); nunca deja la partida rota ni la tarjeta corrupta.

### 5. Manifiesto de mod

`mod.json` (`mod-v1` hoy) crece de forma compatible con:

- `"requires": ["otro-mod>=1.2"]`: el port ordena para que las dependencias carguen antes y desactiva,
  con aviso, un mod cuyas dependencias faltan.
- `"port": ">=0.4"`: la versión mínima del port.
- `"kind": "assets" | "content"`: los de contenido entran en las reglas 3 y 4.

### 6. Fases de la plataforma

1. **Recursos** (en curso): ADR-005, la capa general de archivos (M3) y la guía `docs/modding/`.
2. **Tablas de datos**: las tablas del juego (Digimon, técnicas, objetos, recompensas, experiencia,
   tiendas, encuentros) se exportan a JSON en `data/` y los mods las parchean por clave o agregan filas con
   identificadores del punto 3. Es lo que permite "mejorar la obtención de experiencia y objetos".
3. **Scripts**: un lenguaje embebido para eventos, misiones y personajes (candidato: Lua 5.4, licencia MIT,
   en una caja sin acceso a archivos, red ni sistema). Ganchos del port: entrar a un escenario, hablar con
   un personaje, banderas del juego, recompensas, batallas. La decisión del lenguaje se toma al empezar la
   fase, con su propio ADR.
4. **Entidades nuevas**: personajes y Digimon nuevos, que combinan las fases 2 y 3 con sus sprites (ya
   abiertos) y sus modelos de batalla (formato aún desconocido; requiere investigarlo primero).

Cada fase termina con: capacidad probada por un ejemplo mínimo, tests de host y su sección en
`docs/modding/`. Una fase no empieza sin que la anterior esté documentada.

### 7. Estabilidad para la comunidad

- Cada formato y cada API llevan versión (`"format": "<nombre>-vN"`). Un cambio incompatible crea una
  versión nueva; la anterior se sigue leyendo al menos una versión del port, con aviso.
- La documentación de `docs/modding/` es la referencia: lo que no está documentado no es API.

## Consecuencias

- M3 tiene que dejar listo el manifiesto con `requires`, `port` y `kind` (aunque las fases 2 a 4 aún no
  existan) y reservar el lugar de `saves/` en la estructura de ADR-005.
- Las fases 2 a 4 son trabajo grande; se planifican una por una, cada una con su documento de tareas.
- El port sin mods no cambia: todo esto está apagado si no hay mods instalados.
