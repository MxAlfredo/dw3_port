# DW3 Port

A PC and Android port of **Digimon World 3** (Europe), built from the community
[decompilation](https://github.com/ReGame-Labs/dw3_decomp) of the game. It adds Latin American
Spanish (es-419) as an extra language and a mod platform (data tables, new content, images, audio,
stages, models). *[Leer en español](LEEME.md)*

> **The game is not included.** This repository holds no game data: no text, graphics, audio,
> executables or disc files. You need your own disc image of Digimon World 3 (Europe), the
> En/Fr/De/Es/It one, as `.cue` + `.bin` or a single `.bin`; the port reads everything from it on
> your machine. It is a fan project, not affiliated with or endorsed by Bandai Namco or Sony.

**Players:** [download version 1.0.0](https://github.com/MxAlfredo/dw3_port/releases/latest), install it, and point it at your own disc.
**Modders:** start with the [modding guide](docs/modding/README.en.md): a first mod in 5 minutes, where mods go on
each system (including Android), the tools and the file formats. Bugs and ideas: [issues](https://github.com/MxAlfredo/dw3_port/issues).

## Download

Get version **1.0.0** from **[Releases](https://github.com/MxAlfredo/dw3_port/releases/latest)** (each file is listed in
`SHA256SUMS`; older versions are on the same page):

| System | File |
|---|---|
| Windows (recommended) | `DW3Port-1.0.0-Setup.exe`: installer; it asks for your disc and, optionally, an emulator memory card |
| Windows, portable | `DW3Port-1.0.0-windows-portable.zip`: unzip anywhere, put your disc in `game/`, run `DW3.exe` |
| Linux (x86_64, glibc 2.34+, i.e. 2022 or newer) | `DW3Port-1.0.0-x86_64.AppImage`: make it executable and open it |
| Android 10+ (arm64) | `DW3Port-1.0.0-android.apk`: copy it to the device, open it and allow installing from this source; the app asks for your disc through the system file picker |

Each download has a `README.txt` / `LEEME.txt` with the controls, where your saves are and what to
do if something goes wrong.

## Quick start

1. Download the file for your system.
2. Install or unzip it, then start it.
3. When asked, pick your disc image (`.cue` or `.bin`). Optionally import a memory card from an
   emulator (`.mcd` / `.mcr`) to continue your saves.
4. Language: the game language menu lists Latin American Spanish next to the original languages.

## Status

Version 1.0.0 is the first stable release. Bugs are welcome as issues: see [CONTRIBUTING.md](CONTRIBUTING.md) for what to include.

## Modding

Everything a modder needs is in this repository:

- [`docs/modding/README.en.md`](docs/modding/README.en.md) (Spanish: [`README.md`](docs/modding/README.md)): the guide: mod folders,
  data tables, new Digimon, items, cards, images, audio, video, stages, models and how to load them.
- [`pc/examples/`](pc/examples/README.md): ready-to-copy example mods.
- [`docs/`](docs/): file formats (`formats.md`), data tables, extended game state, stage data and the design
  decisions (`docs/decisions/`).
- [`tools/`](tools/): `model_export.py` / `model_import.py` (glTF models), `techcompat.py`,
  `file_names.py`, `image_atlas_preview.py`, `image_mod_palette.py`, with their tests. After
  `scripts/fetch_deps.sh`, `tools/extract_disc.py` is also there. All of them read your own disc.

Mods are read once at startup; the guide explains where the game looks for them.

## Building from source

You build the game's code from this repository plus its public dependencies, pinned in
`deps.lock`: the decompilation ([ReGame-Labs/dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp)),
[OpenDriver2/PsyCross](https://github.com/OpenDriver2/PsyCross) and the PsyQ headers. `patches/`
holds our changes to the first two. The build produces a game that reads your disc at run time;
it does not need the disc to compile.

### Linux

Needs `git`, `gcc`, `make`, `cmake`, `python3` and the SDL2 and OpenAL development packages
(Debian/Ubuntu: `sudo apt install git build-essential cmake python3 libsdl2-dev libopenal-dev`).

```sh
scripts/fetch_deps.sh      # clone the dependencies at their pinned commits and apply patches/
make -C pc -j$(nproc)      # builds pc/build/boot
pc/build/boot              # asks for your disc
```

### Windows (cross-compiled from Linux with MinGW-w64)

Also needs `mingw-w64` and `curl`; the SDL2 and OpenAL Windows development packages are
downloaded, pinned and checksum-verified, into `~/.cache/dw3-win`.

```sh
scripts/fetch_deps.sh
pc/win/fetch_deps.sh
make -C pc TARGET=windows -j$(nproc)    # builds pc/build-win/boot.exe (make_release renames it DW3.exe)
```

### Android (Docker)

Needs Docker. The image (JDK, Android SDK, NDK) and every download are pinned and checksum-verified.

```sh
scripts/fetch_deps.sh
pc/android/build.sh apk                 # debug APK: pc/android/app/build/outputs/apk/debug/app-debug.apk
```

`pc/android/build.sh release <version>` makes the signed release APK; see "Maintainers" below.

### Tests

Host tests with no disc build and run with, for example, `make -C pc tabletest`, `make -C pc modeltest`
or `make -C pc stagemodtest`. The Python tool tests run with `python3 -I tools/test_model_import.py`
(also `test_model_export.py`, `test_techcompat.py`). Checks that read a disc take `DISC=<your .bin>`.

## Maintainers: how releases are made

Releases are built on a Linux machine with MinGW-w64, NSIS (`makensis`), Docker and the tools
above, from a clean checkout after `scripts/fetch_deps.sh`:

```sh
pc/make_release.sh <version>      # e.g. 1.0.0
```

It writes `pc/build-release/<version>/`: the Windows installer and portable zip, the Linux AppImage
(built in an old-glibc container), the signed Android APK, `SHA256SUMS`, and the player guides
`README.txt` / `LEEME.txt`. It refuses to finish if any download carries a disc image, memory card or
settings file. The Android signing key is **never** in the repository: it lives in `DW3_KEY_DIR`
(default `~/.config/dw3port`: `android-release.keystore` plus its password file, created on first
use). Back it up: an APK signed with another key cannot update an installed one. Upload the files
and `SHA256SUMS` to a new GitHub release.

## Credits and license

The port's own code is MIT licensed ([LICENSE](LICENSE)). It builds on the work of others: see
[CREDITS.md](CREDITS.md) and [pc/release/THIRD-PARTY.txt](pc/release/THIRD-PARTY.txt). The license
covers our code, not the text, graphics or audio of the game. Contributions: see
[CONTRIBUTING.md](CONTRIBUTING.md).
