# ADR-005 — Estructura de la carpeta de datos y de los mods

- **Estado:** ACCEPTED. El árbol de §1 está
  reemplazado por el de ADR-010 (`import/`, `saves/`, `content/`, nombres en minúsculas); un `manifest.json` por familia (§5).

## Contexto

El port lee el juego del disco del usuario (ADR-001) y ya tiene una carpeta de datos para el texto:
`<pref>/data/text/<idioma>/*.json`, generada desde el disco con un manifiesto de hashes, y
`<pref>/data/mods/text/<idioma>/*.json` para reemplazos (`pc/shim/text_data.c`; esa ruta se retiró, ADR-010). `<pref>` es la
carpeta del ejecutable en modo portátil (`portable.txt`) o `SDL_GetPrefPath("dw3port","dw3")`.

Ahora vienen más cosas modificables: los tiempos del banner y de las transiciones (hoy constantes en
`pc/shim/stage_banner.h` y `fast_transitions.h`), y más adelante sprites, fondos y audio. Hace falta
una sola estructura antes de escribir código, para no romper los mods que la comunidad haga.

Hechos que condicionan la decisión:

- Todas las lecturas de datos del juego pasan por `cdRead` (`pc/shim/host_cd.c:104`), por número
  de archivo, y el texto ya se reemplaza ahí (`tdOverride`). Las películas y el audio XA no pasan por
  ahí (`CdRead2` con LBA).
- Los archivos del disco tienen número, no nombre: solo los 350 de texto tienen nombre en el port.
- El texto ya tiene reglas probadas: los archivos generados que el usuario editó nunca se pisan
  (`.new` al lado), un archivo mal formado se registra y se usa la fuente siguiente, el juego nunca
  se cae.
- M0 midió que el disco no es el cuello de botella: la capa de datos no necesita caché para rendir.

## Decisión

### 1. Tres carpetas con dueños distintos

```
<pref>/
  settings.ini, logs/, saves/, import/  del jugador (ver ADR-010)
  data/                                 del port: generada desde el disco; se puede borrar y se rehace
    text/manifest.json                  un manifest.json por familia (text/, images/, tables/, config/):
                                        hash de cada archivo generado y la huella del disco
    text/<idioma>/<archivo>.json        (ya existe)
    config/<nombre>.json                valores ajustables del port, copia de referencia
    ... (sprites/, stages/, audio/ cuando cada formato exista: M1/M4)
  mods/                                 de la comunidad: el port nunca escribe aquí
    <mod>/
      mod.json                          nombre, versión, autor, descripción, formato "mod-v1"
      text/<idioma>/<archivo>.json      mismas rutas que data/
      config/<nombre>.json              solo las claves que cambia
```

- `mods/` sale de `data/` y queda al lado. `data/` es regenerable; los mods no se pueden perder por
  borrar o rehacer `data/`. La ruta vieja `data/mods/text/` ya no se lee (ADR-010 §5).
- Cada mod es una carpeta con su `mod.json`: se instala copiando una carpeta y se quita borrándola.

### 2. Orden de fuentes

Para cada recurso: **mods** (en orden) → **data/** editado por el usuario → **valor del port**
(el disco, o el valor por defecto en el código para `config/`).

- Mods activos y su orden: la sección `[mods]` de `settings.ini` (`order = a, b, c`; el último gana).
  Sin sección, todas las carpetas con `mod.json` válido, en orden alfabético. Un menú para esto llega
  después; el archivo es la fuente de verdad.
- `config/*.json` se mezcla **por clave**: un mod que solo cambia `hold_frames` del banner hereda el
  resto. Los demás recursos (texto, imágenes) se reemplazan **por archivo completo**.

### 3. Formatos

- Datos estructurados: JSON con un campo `"format": "<nombre>-v<n>"` (como `overlay-v1` y
  `layout-v1`). Una versión que el port no conoce se rechaza con aviso, no se adivina.
- Imágenes: PNG. Audio: WAV u OGG. Cada uno con su ADR o tarea cuando llegue (M4).
- Unidades explícitas en el nombre de la clave cuando no son obvias (`hold_frames`, `raise_px`).

### 4. Errores

Un archivo de mod o de `data/` que falta, no se puede leer, tiene un formato desconocido o valores
fuera de rango se registra en el log (`<recurso>: <ruta> rejected: <motivo>`) y se usa la fuente
siguiente. Nunca se cae el juego por un mod. Los rangos válidos de cada clave viven junto a su valor
por defecto.

### 5. Generación desde el disco

- `data/` se genera en la máquina del usuario desde su disco (ADR-001); nada de eso se publica.
- Un `manifest.json` por familia guarda el hash de cada archivo generado y una huella (disco, versión
  del convertidor). Se regenera solo lo que cambió; un archivo editado por el usuario nunca se pisa
  (la versión nueva va a `<archivo>.new`), igual que hoy el texto.
- `config/*.json` se escribe como referencia con los valores por defecto y comentarios en una clave
  `"notes"`, para que un modder vea qué puede cambiar. El port los lee, pero el valor de verdad es el
  del código si el archivo falta.

### 6. Dónde se conecta

Una capa única de archivos del port (M3) en `cdRead` y `FILE_TABLE.getSectorCount`, que generaliza
lo que hoy hace `tdOverride` para el texto. Los recursos que no son archivos del disco (`config/`) se
leen al arrancar. Películas y XA quedan fuera hasta que haga falta.

## Consecuencias

- Mover `mods/` cambia una ruta ya documentada en `pc/README.md`; hay compatibilidad durante la beta.
- Un recurso nuevo necesita un nombre estable: los archivos del disco que no son texto necesitan un
  mapa número → nombre (M1) antes de poder reemplazarse.
- El primer uso es `config/stage_banner.json` y `config/transitions.json` (T2 de `stage-banner.md`),
  que no dependen de M1 ni de M3: sirven para probar el orden de mods, la mezcla por clave y los
  errores con algo chico.
- Queda abierto, sin bloquear: un menú de mods en F1, empaquetar mods en zip, y `saves/` legibles
  (ver `assets-modding.md`).

## Referencias

Comparado con otros proyectos (2026-10-30): Ship of Harkinian guarda los mods en una carpeta `mods/`
propia, un archivo por mod, activos por defecto y con orden de carga en un menú (prioridad arriba);
Dolphin carga paquetes de recursos desde su propia carpeta con una lista de prioridad donde el de
arriba gana. Este ADR sigue el mismo modelo; el empaquetado en un solo archivo queda para después.

- https://code.moparisthebest.com/mirror/Shipwright/raw/tag/8.0.1/README.md
- https://code.moparisthebest.com/mirror/Shipwright/src/commit/dc5e9686c0a5c1b4f1cdf285ad8bf581be6233e1
- https://git.devminer.xyz/archive/dolphin/src/branch/master/docs/ResourcePacks.md
