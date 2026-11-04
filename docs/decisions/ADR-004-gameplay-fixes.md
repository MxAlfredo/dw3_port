# ADR-004 — Arreglos de jugabilidad del port

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-29)

## Contexto

El C del juego tiene errores del original (PS1) cuya intención se ve en los propios datos o textos
del juego. El port puede corregirlos, pero el build de PS1 (matching) tiene que seguir idéntico
byte a byte.

## Decisión

Un arreglo de jugabilidad entra en el port solo si:

1. Es un error del original, y su intención está **evidenciada por los datos o textos del propio
   juego** (no por gusto ni por una hipótesis).
2. Va **siempre detrás de `DW3_PC`** (`#ifdef DW3_PC ... #else <original> #endif`), con el cambio
   mínimo en `src/`.
3. Se prueba que el build de PS1 no cambia: la salida del preprocesador de los archivos tocados,
   sin `DW3_PC`, es idéntica a la de antes del cambio.
4. Lleva una prueba de host que falla antes del arreglo y pasa después, o la razón por la que no
   se puede.

Cada arreglo lleva un ID `G<n>` y queda anotado en el comentario del código que lo contiene.

## Consecuencias

- El comportamiento del port puede diferir del disco original en estos puntos, y está documentado.
- Un merge desde `upstream/main` sigue limpio: cada cambio son pocas líneas dentro de un guard.
