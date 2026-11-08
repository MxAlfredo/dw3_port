# Contributing / Colaborar

Thanks for helping, every report and idea makes the port better. / Gracias por ayudar: cada reporte
e idea mejora el port.

## How this repository works / Cómo funciona este repositorio

This repository is **generated** from the maintainer's private working repository. Issues and pull
requests are welcome here. When a pull request is accepted, the maintainer applies it in the working
repository and it shows up in the next sync of this one; you are credited as the author in the
commit message. / Este repositorio se **genera** desde el repositorio de trabajo privado del
mantenedor. Los issues y *pull requests* son bienvenidos. Cuando se acepta un PR, el mantenedor lo
aplica en el repositorio de trabajo y aparece en la siguiente sincronización de este; tu autoría
queda en el mensaje del commit.

## Reporting a bug / Cómo reportar un error

Open an [issue](https://github.com/MxAlfredo/dw3_port/issues) with:

1. **The version and system**: the name of the file you downloaded (for example
   `DW3Port-2.0.0-x86_64.AppImage`) and your Windows, Linux or Android version.
2. **What happened** and what you expected, and the steps to repeat it (where in the game, what you
   pressed).
3. **The newest log file** (the `.log` with the latest date in the `logs` folder). Open the game once
   more if needed: each run writes its own log.

| System | Where the `logs` folder is |
|---|---|
| Windows, installed | `%APPDATA%\dw3port\dw3\logs\` (Start menu: "DW3 Port - Saves and logs") |
| Windows, portable | `logs\` next to `DW3.exe` |
| Linux | `~/.local/share/dw3port/dw3/logs/` |
| Android | `Android/data/io.github.dw3port/files/logs/` (from a PC over USB, in file-transfer mode) |

On Windows, `DW3-debug.exe` is the same game with a console window that shows the log while you play.
If you use mods, say which ones, and try without them first.

Abre un [issue](https://github.com/MxAlfredo/dw3_port/issues) con: la versión y el sistema (el
nombre del archivo que bajaste y tu versión de Windows, Linux o Android), qué pasó y qué esperabas,
los pasos para repetirlo y el archivo `.log` más reciente de la carpeta `logs` (las rutas están en la
tabla de arriba). Si usas mods, di cuáles y prueba antes sin ellos.

**Never attach** your disc image, a memory card, or anything extracted from the game. / **Nunca
adjuntes** la imagen de tu disco, una tarjeta de memoria ni nada extraído del juego.

## Sharing mods / Compartir mods

Mods are folders (see [`docs/modding/README.en.md`](docs/modding/README.en.md)); you can share one
anywhere: a release of your own repository, a forum, a Discord. Please:

- Include **only your own work**: your JSON, your own new images, sounds and models. No original
  text (English or official Spanish), no graphics, audio, video, disc files or binaries that come
  from the game, and no images ripped from it. Tables and patches that describe *changes* are fine;
  a mod that needs the game's data should make it from the player's own disc (the example mods in
  [`pc/examples/`](pc/examples/README.md) show how).
- Say what the mod changes, which port version it needs (the `port` key of `mod.json`) and which
  other mods it conflicts with. Run `--validate-mod` before you publish it.
- Do not bundle fan content you do not have the right to share.

Los mods son carpetas (ver [`docs/modding/README.md`](docs/modding/README.md)) y puedes compartirlos
donde quieras. Incluye **solo trabajo propio**: nada de texto original (inglés o español oficial),
gráficos, audio, video, archivos del disco ni binarios del juego, ni imágenes extraídas de él. Cuenta
qué cambia, qué versión del port necesita y con qué mods choca, y corre `--validate-mod` antes de
publicarlo.

## Code and docs / Código y documentación

- Identifiers, comments and commit messages in English, [Conventional Commits](https://www.conventionalcommits.org)
  (`feat:`, `fix:`, `docs:`, `test:`), no AI attribution lines. Docs are in English and Spanish where
  the existing files already are.
- Changes to the decompiled game code or PsyCross go to `patches/`, which is computed against the
  pinned upstream commits in `deps.lock`. Changes that belong to the decompilation itself (matching,
  naming) are better sent to [ReGame-Labs/dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp).
- Translations: see `localization/README.md` and the glossary in `localization/es-419/GLOSSARY.md`.
  Only strings of your own that differ from the official text are accepted.
- Run the tests that apply (`make -C pc <name>test`, `python3 -I tools/test_*.py`) before opening a PR.

## Legal note / Nota legal

This is a fan project, not affiliated with or endorsed by Bandai Namco or Sony. Digimon and Digimon
World 3 belong to their owners. The port's code is MIT licensed; the license does **not** cover the
game's text, graphics, audio or any other game data, which this repository never contains and which
you must never submit. By contributing you agree that your contribution is under the same MIT license.
/ Es un proyecto de fans, sin relación con Bandai Namco ni Sony. Digimon y Digimon World 3 son de sus
dueños. El código del port es MIT; la licencia **no** cubre el texto, gráficos, audio ni otros datos
del juego, que este repositorio nunca contiene y que nunca debes enviar. Al contribuir aceptas que tu
aporte quede bajo la misma licencia MIT.
