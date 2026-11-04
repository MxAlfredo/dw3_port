# ADR-010 — Carpeta del usuario: una sola estructura para todo lo que el port lee y escribe

- **Estado:** ACCEPTED (pedido del dueño, 2026-10-31: "la mejor estructura posible para saves, assets, configs, etc.;
  si ves algo mejorable, adelante con cambiarlo"). Reemplaza el árbol de ADR-005 §1 y la ubicación de partidas de
  ADR-008; el resto de esos ADR sigue vigente.

## Contexto

Inventario del 2026-10-31:

- **No hay una capa de rutas.** La carpeta base sale de `inputCfgPrefDir` (`pc/shim/input_cfg.c:560`), pero `data/` y
  `mods/` se calculan en el módulo de controles, las partidas y la tarjeta en `host_memcard.c`, los logs en
  `disc_picker.c`, y cada módulo de datos guarda su propia copia de la ruta de `data/`. ADR-009 §7 pide "rutas solo por
  la capa del port", y esa capa no existe.
- **Restos y duplicados:** la ruta vieja `~/.local/share/dw3-port/` (escrita a mano en `host_memcard.c:236-241`, fuera
  de SDL), `controls.ini`, el alias `DW3_CONTROLS`, `data/mods/text/` (ADR-005 decía retirarlo), `memcard0.mcd` suelto en
  la raíz aunque ya solo sirve para importar.
- **Mayúsculas:** `data/images/FIELD/...`, `mods/<mod>/images/...` y `mods/<mod>/files/DAT/...` copian los nombres del
  disco en mayúsculas; en Android (y en Linux) el sistema distingue mayúsculas y un mod escrito en Windows se rompe.
- **Contenido y parches mezclados:** `mods/<mod>/tables/` guarda tanto parches de tablas (`shop_stock.json`) como
  registros de contenido nuevo (`items.json`); nada los distingue salvo el contenido. ADR-009 necesita un lugar propio.
- **ADR-005 se contradice** (un `manifest.json` único en §1, uno por familia en §5; el código hace uno por familia).

## Decisión

### 1. El árbol

```text
<carpeta del usuario>/               Linux ~/.local/share/dw3port/dw3/, Windows %APPDATA%\dw3port\dw3\,
│                                    Android la carpeta de la app (SDL); modo portátil: junto al ejecutable
├── settings.ini                     preferencias del jugador (idioma, video, controles, mejoras, [mods], [disc], [debug])
├── saves/<perfil>/                  partidas del port (ADR-008): slot<N>.dw3save (+ .bak), card.dw3card
│   └── view/slot<N>.json            vista legible de solo lectura (antes junto a la partida)
├── import/                          tarjetas viejas (.mcd) para importar; imported.txt registra lo ya importado
├── mods/<mod>/                      un mod por carpeta (ADR-005), ver §2
├── data/                            referencia generada desde el disco (ADR-005): se puede borrar y se regenera
│   ├── text/<idioma>/  images/<ruta del disco en minúsculas>/  tables/  config/  content/
│   └── (un manifest.json por familia, como hoy)
├── logs/                            dw3-AAAAMMDD-HHMMSS.log (los últimos N)
├── screenshots/                     capturas del jugador (cuando exista la función)
└── cache/                           lo que el port pueda regenerar para cargar más rápido (opcional, borrable)
```

- **Perfil de partidas:** `saves/default/` sigue siendo el único por ahora; el nombre queda como concepto para varios
  perfiles más adelante.
- **`import/`:** al migrar, `memcard0.mcd` se mueve aquí; en Android es la forma natural de traer una tarjeta.
- **`data/content/`:** la referencia del contenido original para ADR-009 (`items.json`, `techs.json`... con los nombres
  `dw3:<tipo>/<id>`), para que el modder vea qué existe y cómo se llama.

### 2. Un mod

```text
mods/<mod>/
├── mod.json             mod-v1: nombre, versión, kind (assets | content), requires, port
├── content/<tipo>.json  registros nuevos o cambios de contenido (ADR-009): items.json, techs.json, digimon.json...
├── tables/<tabla>.json  parches de tablas del juego (ADR-007): shop_stock, battle_loot, item_prices...
├── text/<idioma>/       textos
├── images/<ruta>        imágenes (PNG + JSON), ruta del disco en minúsculas
├── config/<nombre>.json ajustes del port
└── files/<ruta>         reemplazo crudo de un archivo del disco, ruta en minúsculas
```

`items.json` y `techs.json` pasan de `tables/` a `content/`. No hay compatibilidad que cuidar: los objetos de mods
(`items-v1`) y las técnicas todavía no salieron en ninguna versión publicada.

### 3. Nombres de archivo en minúsculas

Todo lo que el port escribe va en minúsculas. Al leer `images/` y `files/` de un mod o de `data/`, el port compara sin
distinguir mayúsculas (normaliza la ruta), así que un mod escrito con mayúsculas en Windows también funciona en Linux y
Android.

### 4. Una sola capa de rutas

Un módulo nuevo (`pc/shim/port_paths.c`) es el único que conoce la carpeta del usuario y sus subcarpetas: resuelve la
base (SDL, modo portátil, más adelante Android), las variables de entorno (`DW3_USER_DIR` mueve todo el árbol, y cada
parte tiene la suya: `DW3_SETTINGS`, `DW3_SAVES_DIR`, `DW3_IMPORT_DIR`, `DW3_MODS_DIR`, `DW3_DATA_DIR`, `DW3_LOGS_DIR`;
además `DW3_MEMCARD`, una sola tarjeta para las pruebas, y `DW3_L10N_DIR`) y crea carpetas. Implementado en
`pc/shim/port_paths.c` (núcleo sin SDL) y `pc/shim/port_paths_sdl.c` (el gancho de plataforma, donde va la rama de
Android). Los demás módulos le piden rutas y no
guardan copias propias ni usan `HOME`, `XDG_*` o separadores a mano.

### 5. Sin migración: estructura limpia

Decisión del dueño (2026-10-31): no hay compatibilidad con versiones anteriores, porque nadie más tiene el port. El
código no carga ninguna ruta ni formato viejo: se quitan la ruta `~/.local/share/dw3-port/`, `controls.ini` y el alias
`DW3_CONTROLS`, `data/mods/text/`, el `<DW3_MEMCARD>.saves` de respaldo y `memcard0.mcd` en la raíz (las tarjetas a
importar van en `import/`). Las instalaciones existentes (la del dueño y la de su amigo) se desinstalan y se instalan de
nuevo; lo único que se conserva es la partida del dueño (`saves/default/slot0.dw3save`, que ya está en su lugar final),
con respaldo en `~/DW3-respaldo-partidas/`.

### 6. Formatos

- `settings.ini` se queda en INI: es lo único que el jugador edita a mano y ya tiene comentarios y valores válidos
  escritos dentro.
- Todo lo demás que lee o escribe el port es JSON con versión en el nombre del formato (`items-v1`, `techs-v1`,
  `mod-v1`...), o binario propio con versión (`dw3save-v1`).

## Consecuencias

- Un solo lugar donde cambiar rutas (y el único que necesita una rama para Android).
- Los documentos que nombran rutas se actualizan juntos: `pc/release/README.txt`, `LEEME.txt`,
  `docs/modding/README(.en).md`, `docs/formats.md`, `docs/data-tables.md`, `pc/README.md`, `pc/review.sh`, el
  instalador de Windows.
- ADR-005 queda corregido: un manifest por familia (lo que hace el código).
- El build de PS1 no se toca (todo esto vive en `pc/`).
