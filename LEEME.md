# DW3 Port

Un port para PC y Android de **Digimon World 3** (Europa), hecho a partir de la
[decompilación](https://github.com/ReGame-Labs/dw3_decomp) del juego que hizo la comunidad. Agrega
el español latinoamericano (es-419) como idioma extra y una plataforma de mods (tablas de datos,
contenido nuevo, imágenes, audio, escenarios, modelos). *[Read in English](README.md)*

> **El juego no está incluido.** Este repositorio no tiene datos del juego: ni texto, ni gráficos,
> ni audio, ni ejecutables, ni archivos del disco. Necesitas tu propia imagen del disco de Digimon
> World 3 (Europa), la de En/Fr/De/Es/It, como `.cue` + `.bin` o como un solo `.bin`; el port lo lee
> todo de ella en tu equipo. Es un proyecto de fans, sin relación con Bandai Namco ni Sony ni
> respaldo de ellas.

**Jugadores:** [baja la versión 2.0.0](https://github.com/MxAlfredo/dw3_port/releases/latest), instálala y apúntala a tu propio disco.
**Modders:** empieza por la [guía de mods](docs/modding/README.md): un primer mod en 5 minutos, dónde van los mods en
cada sistema (también Android), las herramientas y los formatos de archivo. Errores e ideas: [issues](https://github.com/MxAlfredo/dw3_port/issues).

## Descarga

Baja la versión **2.0.0** desde **[Releases](https://github.com/MxAlfredo/dw3_port/releases/latest)** (cada archivo está en
`SHA256SUMS`; las versiones anteriores están en la misma página):

| Sistema | Archivo |
|---|---|
| Windows (recomendado) | `DW3Port-2.0.0-Setup.exe`: instalador; pide tu disco y, si quieres, una tarjeta de memoria de un emulador |
| Windows, portátil | `DW3Port-2.0.0-windows-portable.zip`: descomprímelo donde quieras, pon tu disco en `game/` y abre `DW3.exe` |
| Linux (x86_64, glibc 2.34+, de 2022 en adelante) | `DW3Port-2.0.0-x86_64.AppImage`: dale permiso de ejecución y ábrelo |
| Android 10+ (arm64) | `DW3Port-2.0.0-android.apk`: cópialo al equipo, ábrelo y permite instalar desde ese origen; la app pide tu disco con el selector de archivos del sistema |

Cada descarga trae un `LEEME.txt` / `README.txt` con los controles, dónde quedan tus partidas y qué
hacer si algo falla.

## Inicio rápido

1. Baja el archivo de tu sistema.
2. Instálalo o descomprímelo y ábrelo.
3. Cuando lo pida, elige la imagen de tu disco (`.cue` o `.bin`). Si quieres seguir tus partidas,
   importa también una tarjeta de memoria de un emulador (`.mcd` / `.mcr`).
4. Idioma: el menú de idiomas del juego muestra el español latinoamericano junto a los originales.

## Estado

La versión 2.0.0 reestructura los archivos del juego (carpetas legibles por recurso) y agrega un paquete de compañero de ejemplo, más herramientas de modding y arreglos de Android. Los mods y la carpeta `data` de 1.x deben actualizarse o volver a exportarse (ver las notas de la versión). Los errores son bienvenidos como *issues*: en [CONTRIBUTING.md](CONTRIBUTING.md) está qué incluir.

## Mods

Todo lo que necesita quien hace mods está en este repositorio:

- [`docs/modding/README.md`](docs/modding/README.md) (inglés: [`README.en.md`](docs/modding/README.en.md)): la guía: carpetas de mods,
  tablas de datos, Digimon nuevos, objetos, cartas, imágenes, audio, video, escenarios, modelos y cómo se cargan. Los assets viven en una
  carpeta por tema (`stages/`, `cards/`, `partners/`, `digimon/`, `sprites/`, `menus/`...), igual en `data/` y en un mod; un mod hecho
  para una versión anterior se mueve con la [tabla de migración](docs/modding/README.md#11-migración-desde-el-formato-anterior) de la guía.
- [`pc/examples/`](pc/examples/README.md): tres mods de ejemplo completos (escenarios nuevos, un Digimon con modelo propio, un paquete de compañero) y una carpeta modelo.
- [`docs/`](docs/): formatos de archivo (`formats.md`), tablas de datos, estado extendido, datos de escenarios y las
  decisiones de diseño (`docs/decisions/`).
- [`tools/`](tools/): `model_export.py` / `model_import.py` (modelos glTF), `techcompat.py`,
  `file_names.py`, `image_atlas_preview.py`, `image_mod_palette.py`, con sus pruebas. Después de
  `scripts/fetch_deps.sh` también está `tools/extract_disc.py`. Todas leen tu propio disco.

Los mods se leen una sola vez al arrancar; la guía explica dónde los busca el juego.

## Compilar desde el código

El código del juego se compila con este repositorio más sus dependencias públicas, fijadas en
`deps.lock`: la decompilación ([ReGame-Labs/dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp)),
[OpenDriver2/PsyCross](https://github.com/OpenDriver2/PsyCross) y los headers de PsyQ. `patches/`
guarda nuestros cambios sobre las dos primeras. El resultado lee tu disco al ejecutarse; no lo
necesita para compilar.

### Linux

Necesita `git`, `gcc`, `make`, `cmake`, `python3` y los paquetes de desarrollo de SDL2 y OpenAL
(Debian/Ubuntu: `sudo apt install git build-essential cmake python3 libsdl2-dev libopenal-dev`).

```sh
scripts/fetch_deps.sh      # clona las dependencias en sus commits fijados y aplica patches/
make -C pc -j$(nproc)      # compila pc/build/boot
pc/build/boot              # pide tu disco
```

### Windows (cruzado desde Linux con MinGW-w64)

Además `mingw-w64` y `curl`; los paquetes de desarrollo de SDL2 y OpenAL para Windows se bajan
fijados y con checksum a `~/.cache/dw3-win`.

```sh
scripts/fetch_deps.sh
pc/win/fetch_deps.sh
make -C pc TARGET=windows -j$(nproc)    # compila pc/build-win/boot.exe (make_release lo renombra DW3.exe)
```

### Android (Docker)

Necesita Docker. La imagen (JDK, SDK de Android, NDK) y cada descarga están fijadas y con checksum.

```sh
scripts/fetch_deps.sh
pc/android/build.sh apk                 # APK de depuración: pc/android/app/build/outputs/apk/debug/app-debug.apk
```

`pc/android/build.sh release <versión>` hace el APK de lanzamiento firmado; ver "Mantenedores".

### Pruebas

Las pruebas del host que no usan disco se compilan y corren con, por ejemplo, `make -C pc tabletest`,
`make -C pc modeltest` o `make -C pc stagemodtest`. Las de las herramientas de Python:
`python3 -I tools/test_model_import.py` (también `test_model_export.py`, `test_techcompat.py`). Los
chequeos que leen un disco reciben `DISC=<tu .bin>`.

## Mantenedores: cómo se hacen las versiones

Las versiones se arman en una máquina Linux con MinGW-w64, NSIS (`makensis`), Docker y lo anterior,
desde un checkout limpio después de `scripts/fetch_deps.sh`:

```sh
pc/make_release.sh <versión>      # por ejemplo 2.0.0
```

Escribe `pc/build-release/<versión>/`: el instalador y el zip portátil de Windows, el AppImage de Linux
(compilado en un contenedor con glibc vieja), el APK firmado de Android, `SHA256SUMS` y las guías
`README.txt` / `LEEME.txt`. Falla si alguna descarga trae una imagen de disco, una tarjeta de memoria
o un archivo de configuración. La llave de firma de Android **nunca** está en el repositorio: vive en
`DW3_KEY_DIR` (por defecto `~/.config/dw3port`: `android-release.keystore` y su archivo de clave, que
se crean la primera vez). Respáldala: un APK firmado con otra llave no puede actualizar uno ya
instalado. Sube los archivos y `SHA256SUMS` a un release nuevo de GitHub.

## Créditos y licencia

El código propio del port es MIT ([LICENSE](LICENSE)). Se apoya en el trabajo de otros: ver
[CREDITS.md](CREDITS.md) y [pc/release/THIRD-PARTY.txt](pc/release/THIRD-PARTY.txt). La licencia
cubre nuestro código, no el texto, los gráficos ni el audio del juego. Para colaborar:
[CONTRIBUTING.md](CONTRIBUTING.md).
