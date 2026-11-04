# ADR-003 — Convivencia del port con el árbol de upstream

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-15)

## Contexto

Hasta el hito 1 el port vivió entero en `pc/`, sin tocar una línea del árbol original. El relevamiento
de huecos encontró **10 archivos de upstream** que el port no puede
evitar tocar, porque el C del juego asume la PlayStation: sobre todo **7 casts de puntero a 32 bits**
(`(s32)`/`(u32)`) que en x86-64 truncan el puntero.

`AGENTS.md` ya fija la regla general: no tocar archivos de upstream salvo que una feature lo necesite,
y en ese caso el cambio mínimo. Lo que faltaba era decidir **de qué forma** se toca.

## Decisión

Se arregla **en su lugar**, con el cambio mínimo y con punteros, no con tipos nuevos:

- Los casts de puntero pasan a aritmética de punteros (`u8 *`, `void *`) o a campos de tipo puntero.
  **No** se introducen `uintptr_t`/`intptr_t`/`stdint.h`: el build de PS1 usa GCC 2.8.1 con
  `-nostdinc`, así que no se puede contar con esos tipos.
- Criterio de aceptación obligatorio: **`make VERSION=eu compare` sigue 316 OK**. En MIPS un puntero
  mide 32 bits, así que el código generado tiene que seguir siendo idéntico byte a byte; si algo no
  matchea, el cambio se revierte o se replantea.
- Además, el host queda más limpio: desaparecen los `-Wno-pointer-to-int-cast` de `pc/Makefile` y
  `make -C pc` sigue sin warnings con `-Wall` completo.
- Lo que **no** se puede compartir entre las dos plataformas (asm inline del scratchpad, direcciones
  fijas `0x800xxxxx` como `HEAP_END`, `OVERLAY_ADDRESS`) va con guardas explícitas de host, y se
  registra acá cuando llegue cada caso.
- Nunca se copian archivos de upstream a `pc/` para parchearlos: duplicar código envejece mal y cada
  merge desde upstream obligaría a revisar si la copia quedó vieja.

## Consecuencias

- Los cambios en `src/**`/`include/**` son pequeños y están justificados uno por uno; los merges desde
  `upstream/main` pueden dar conflicto en esas líneas exactas, que es el costo aceptado.
- Cualquier cambio en el árbol original obliga a correr el build de matching en Docker antes de
  aceptarlo: es la garantía de que el juego de PS1 no cambia.
- Un arreglo de ancho de puntero es, además, un arreglo real que upstream podría aceptar.
