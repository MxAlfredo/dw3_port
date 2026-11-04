# ADR-007 — Mods de tablas de datos

- **Estado:** ACCEPTED

## Contexto

El inventario (`docs/data-tables.md`) mostró que las reglas del juego que importan a la comunidad (EXP y dinero por
batalla, botines, precios, tiendas, encuentros, Digimon, técnicas) viven en tablas. En el port esas tablas son
arreglos C del propio programa, así que se pueden parchear en memoria sin tocar `src/`. Hay dos momentos de
aplicación: las del ejecutable se parchean una vez al arrancar; las de los overlays se reinician al entrar a su modo
(`state_scope`) y hay que parchearlas al entrar. La tabla de batalla (enemigos y botín) es el archivo 463 del disco.
Algunas reglas son código, no datos (reparto de EXP, bonos, topes, fórmulas).

## Decisión

### 1. Exportar para ver, parchear para cambiar

- El port escribe cada tabla abierta en `data/tables/<tabla>.json` (formato `<tabla>-v1`), generado en la máquina del
  usuario desde su juego, con el manifiesto y la regla de `.new` de ADR-005. Es **referencia**: sirve para leer los
  valores y copiar claves; editarlo no cambia el juego (así un default nuevo del port nunca queda bloqueado por una
  copia vieja).
- Los cambios van en **parches**: `mods/<mod>/tables/<tabla>.json`. Un parche solo trae lo que cambia.

### 2. Forma de un parche

```json
{
  "format": "battle_rewards-v1",
  "rows": {
    "12":  { "money": 300 },
    "13":  { "exp": "*2" },
    "*":   { "money": "*1.5" }
  }
}
```

- `rows` usa la **llave estable** de cada tabla (id de objeto, id de Digimon, id de técnica, id de enemigo, número
  de encuentro). La llave de cada tabla se documenta en su sección de `docs/modding/`.
- Un valor puede ser un número (reemplaza) o una operación simple sobre el valor actual: `"*k"` (multiplica),
  `"+k"` / `"-k"` (suma o resta). Se redondea y se recorta al rango del campo.
- `"*"` aplica a todas las filas; una fila con su llave gana sobre `"*"` dentro del mismo parche.
- Los campos que el parche no nombra quedan como estaban. Un campo desconocido, un tipo equivocado o un valor fuera de
  rango se rechaza **por campo**, con una línea en el log, y el resto del parche se aplica.

### 3. Orden

Original → cada mod en el orden de carga de ADR-005/ADR-006 (las dependencias antes). Las operaciones se aplican en
ese orden, así que dos mods que multiplican el dinero por 2 dan ×4, y un reemplazo posterior gana sobre todo lo
anterior. El log dice qué mods tocaron cada tabla.

### 4. Filas nuevas

- Solo en tablas marcadas como ampliables en su documentación, y solo para mods `"kind": "content"` (ADR-006).
- Una fila nueva usa una llave `<mod>:<nombre>` y el port le asigna un número libre del rango reservado de esa tabla,
  guardado con la partida en `saves/<partida>.mods.json` (ADR-006 §3-4). Los arreglos de las partidas tienen tamaño
  fijo (`items[0x193]`...), así que cada tabla documenta cuántas filas nuevas admite.
- Esta parte se construye después de los parches de filas existentes; el formato ya la contempla.

### 5. Reglas que son código

Las reglas del juego escritas en C (reparto de EXP entre socios, bonos de accesorios, topes de dinero y objetos,
curva de niveles) se abren, cuando se abran, como valores de `config/<nombre>.json` (ADR-005, mezcla por clave) leídos
por un gancho del port, con el comportamiento original como valor por defecto. No son tablas.

### 6. Aplicación técnica

- Tablas del ejecutable: se parchean una vez tras el arranque, antes del primer modo.
- Tablas de overlay: se parchean al entrar a su modo, después de que `state_scope` restaure su estado original.
- Tabla de batalla (archivo 463): se parchea el buffer al leerlo, en la capa de archivos (M3), por id de enemigo.
- Sin mods de tablas, ninguna tabla se toca y el juego es idéntico.

### 7. Partidas

Cambiar valores de filas existentes es compatible con partidas guardadas: la tarjeta no guarda las tablas. Hay que
documentar los casos donde un valor se copia a la partida en un momento concreto (por ejemplo, los stats iniciales de
un socio solo se leen al empezar partida nueva): esos parches solo se notan en una partida nueva.

### 8. Orden de construcción

1. El motor de parches (formato, operaciones, orden, errores, log) y la primera tabla: **recompensas de batalla**
   (EXP, EXP de Digimon y dinero por encuentro), con un mod de ejemplo.
2. Botines (tabla de batalla), precios de objetos y tiendas.
3. Encuentros, Digimon, técnicas, evoluciones y gimnasio.
4. Filas nuevas (§4) y las reglas de código (§5).

## Ampliación (2026-10-30, aprobada por el dueño): campos lista

Una fila puede tener un campo lista (arreglo de ids con puntero y contador, terminado en 0), como el surtido de una
tienda. El parche lo escribe como arreglo (reemplaza la lista) o como `{"remove": [..], "add": [..]}` (la edita; dos
mods que agregan a la misma lista se suman). El port arma el resultado en un buffer propio y apunta la fila a él, sin
escribir los arreglos del juego. Una lista vacía, más larga que el tope documentado de la tabla o con un id fuera de
rango se rechaza con una línea en el log y la fila queda igual.

## Consecuencias

- Los mods de "más EXP" o "más dinero" son un archivo de pocas líneas, sin herramientas.
- Cada tabla nueva que se abra necesita: llave, campos con rango, momento de aplicación, exportación, sección en la
  guía y tests.
- Los números del juego aparecen solo en `data/` del usuario, nunca en el repo ni en la guía (ADR-001).
