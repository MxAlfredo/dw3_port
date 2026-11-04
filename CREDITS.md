# Credits

DW3 Port is a fan project. It exists thanks to the work of others. / DW3 Port es un proyecto de fans
que existe gracias al trabajo de otros.

## The game's code / El código del juego

- **[dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp)** by ReGame Labs and its contributors:
  the matching decompilation of Digimon World 3 that the whole port is built from. MIT License.
  Not copied here: `scripts/fetch_deps.sh` downloads it at the commit pinned in `deps.lock` and
  applies `patches/dw3_decomp.patch`.

## Libraries and tools / Bibliotecas y herramientas

| Project | Use | License |
|---|---|---|
| [PsyCross (Psy-X)](https://github.com/OpenDriver2/PsyCross), REDRIVER2 Project | PlayStation SDK replacement layer; fetched and patched (`patches/psycross.patch`) | MIT |
| [psyq_headers](https://github.com/Rivarux/psyq_headers) | PsyQ headers the decompiled C is compiled against; fetched by reference, never copied here. The repository states no license of its own; the headers originate from Sony's PsyQ SDK | see its repository |
| [SDL2](https://www.libsdl.org/) 2.32.10 | window, input, audio device, Android glue | zlib (text in `pc/win/licenses/SDL2-zlib.txt`) |
| [OpenAL Soft](https://openal-soft.org/) 1.24.3 | audio, used unmodified and linked dynamically | LGPL 2+ |
| [tinyfiledialogs](https://sourceforge.net/projects/tinyfiledialogs/) 3.22.1 | native file dialogs (`third_party/tinyfiledialogs/`) | zlib (`third_party/tinyfiledialogs/LICENSE.txt`) |
| [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) | game controller mappings; downloaded on demand, not included | zlib |
| Kenney "Input Prompts" (www.kenney.nl) | button prompt icons, derived (`pc/third_party/kenney-prompts/`) | CC0 1.0 |
| LLVM libc++, Android NDK r27c, Gradle, Android Gradle Plugin | Android build only | Apache 2.0 |

The full notices are in [pc/release/THIRD-PARTY.txt](pc/release/THIRD-PARTY.txt), which also ships
inside every download.

## The port

The port's own code, tools and documentation, and the es-419 translation overlay in `localization/`
(our own strings only; the official text is read from the player's disc) are MIT licensed, copyright
DW3 Port contributors ([LICENSE](LICENSE)).

## Not included / No incluido

Digimon World 3 itself. Every game asset (graphics, sound, text, video) is read at run time from the
player's own disc. Digimon is a trademark of its owners; this project is not affiliated with or
endorsed by Bandai Namco or Sony.
