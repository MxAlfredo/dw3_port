# ADR-002 — Runtime del port a PC: PsyCross (MIT) más shims propios

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-15)

## Contexto

El port a PC necesita reemplazar PsyQ 4.7: las bibliotecas que el juego enlaza son objetos
desensamblados de Sony, no C nuestro, y no existen fuera de PS1. El C del juego llama sobre todo a
`libgs` (21 llamadas), `libsnd` (31), `libcd` (25), `libpad` (42), `libpress`/MDEC (8) y `libgte` (28).

Las alternativas investigadas:

- **PsyCross** (MIT): reimplementa `libgte`, `libgpu`, `libcd`, `libspu`, `libpad`, `libetc`,
  `libapi` y `libmcrd` sobre SDL2, OpenAL y OpenGL. **No cubre `libgs`, `libsnd` ni MDEC/XA/CD-audio**,
  y su `libcd` es parcial.
- **PsyZ**: mucho más capaz (SDL3, 32 y 64 bits, Linux/macOS/Windows/WASM/PSP, mayor fidelidad a
  PSY-Q), y es lo que usa el port a PC de sotn-decomp. Pero su licencia mezcla MPL-2.0 con directorios
  de cabeceras marcados **"Unlicensed"**.
- **Shim propio sin framework**: licencia limpia y control total, pero es el camino más largo.

## Decisión

El port se construye sobre **PsyCross (MIT)** y escribimos nosotros los shims que faltan:
`libgs`, `libsnd` y `libpress` (MDEC/XA).

- Solo se aceptan dependencias con licencia permisiva y redistribuible, con su archivo de licencia.
  **No** se adopta PsyZ mientras sus cabeceras estén marcadas "Unlicensed".
- De otros ports (sotn-decomp, AGPL; REDRIVER2; Tenchu) se copia **el patrón**, nunca el código: la
  estructura es carpeta de overlay + shim de plataforma + stubs + `main` propio.
- Toda dependencia nueva se anota acá antes de entrar.

## Consecuencias

- La fidelidad de `libgs` (escena 3D), `libsnd` (audio) y `libpress` (video) es responsabilidad
  nuestra, y es el grueso del trabajo pendiente.
- Hay que sacar las direcciones fijas `0x800xxxxx`, las suposiciones de puntero de 32 bits y el
  scratchpad: eso es trabajo de port en sí, independiente de la biblioteca elegida.
- `docs/decisions/` es el registro de licencias del proyecto: antes de sumar una dependencia, se
  verifica y se documenta acá.
