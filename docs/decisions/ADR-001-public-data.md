# ADR-001 — Datos públicos: solo trabajo propio

- **Estado:** ACCEPTED (decisión del dueño, 2026-10-14)

## Contexto

Una traducción es-419 es una traducción de fans del texto retail del juego. Este fork de
`dw3_decomp` es público y no puede hacerse privado. El texto, los gráficos y el audio del juego son
propiedad de Bandai Namco; la licencia del repo (MIT) no los cubre. Por eso solo publicamos trabajo
propio.

## Decisión

El repo público solo publica trabajo propio:

- Código, herramientas y documentación.
- Cadenas es-419 que difieren del texto oficial en español (nuestra autoría), indexadas por
  archivo e índice (`localization/es-419/<ARCHIVO>.json`, formato `overlay-v1`).
- Glosario y reglas de reescritura (términos cortos, sin frases completas del juego).

Todo lo demás se lee del disco del usuario en tiempo de build o ejecución: cadenas oficiales,
lang files, gráficos, audio, ejecutables. Una cadena es-419 idéntica a la oficial no se publica;
se toma tal cual del disco.

## Consecuencias

- `disks/`, `build/`, `asm/`, `assets/`, `expected/` y `build-*.log` permanecen ignorados.
- Antes de cada commit se verifica que no haya campos `en`/`es` ni texto oficial en `localization/`.
- El build aplica el overlay sobre los lang files del disco del usuario (feature futura).
- Un idioma nuevo sigue el mismo modelo: solo se publican las cadenas propias.
