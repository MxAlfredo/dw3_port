# Modding guide for the Digimon World 3 port

> Versión en español: [README.md](README.md) (same content).

This guide is the reference for what can be modded **today** and how. What is not written here is not API
(ADR-006 section 7). Mods are folders: install one by copying it, remove it by deleting it. The port never writes
inside `mods/`, and a broken mod never stops the game (it goes to the log and the next source is used).

## Start here

1. **What can I mod?** Text, images, sprites, audio, movies, every game table (prices, rewards, loot, evolutions, shops, cards...),
   new items, techniques, cards, enemies, partner forms and 3D models, and the characters and talks of a stage. The full list,
   with the file each one uses, is the table in section 1.
2. **Where do mods go?** In the `mods/` folder inside your preferences folder (`<pref>`, section 2): Linux
   `~/.local/share/dw3port/dw3/mods/`, Windows `%APPDATA%\dw3port\dw3\mods\` (portable build: `mods\` next to `DW3.exe`), Android
   `Android/data/io.github.dw3port/files/mods/` (mods are made on a PC and copied there; the game reads them only at startup, so
   restart it from Options > System > Restart game).
3. **Run the tools.** The game executable (`boot`) has command-line flags: from a source build it is `pc/build/boot`; on Windows use
   `DW3-debug.exe` (the same game with a console window); the Linux AppImage takes the same flags. `--new-mod`, `--list-mods` and
   `--validate-mod` are described in section 2c; `--export-all` writes everything the disc holds into `<pref>/data/` for you to
   look at and edit.
4. **First mod in 5 minutes, with a ready-made example.** From a checkout of this repository, start the game once with your disc
   (so `<pref>/data/` exists), then `pc/examples/install_examples.sh --only cheap-shops` and open Options > Mods, enable
   `cheap-shops`, restart, and look at the prices in item shop 0. Open its `tables/item_prices.json` and change `"*0.5"`: you have
   just edited a table. Section 2c shows how to create your own mod folder and validate it; every example (section 7) has a
   `README.txt` saying what it changes and where to see it.
5. **Tools** (section 8): `tools/file_names.py`, `tools/extract_disc.py`, `tools/image_atlas_preview.py`, `tools/image_mod_palette.py`,
   `tools/techcompat.py`, `tools/model_export.py`, `tools/model_import.py`. Run them with `python3 tools/<name>.py ...`; all read
   your own disc and store no game data.
6. **Formats and data (reference):** [`docs/formats.md`](../formats.md) (file formats), [`docs/data-tables.md`](../data-tables.md),
   [`docs/digimon-anatomy.md`](../digimon-anatomy.md), [`docs/extended-state.md`](../extended-state.md) (saves and extended
   state), [`docs/stages.md`](../stages.md) (stage data; Spanish) and the schemas in [`schema/`](schema/).
7. **Sharing your mod:** only your own work, never game data (section 9). Describe what it changes and which port version it needs
   (the `port` key of `mod.json`).

## 1. What can be modded today

| What | How | Section |
|---|---|---|
| Game text (any language) | `text/<language>/<file>.json` | 6.1 |
| Stage banner and transition timings | `config/stage_banner.json`, `config/transitions.json` | 3 |
| Battle rewards (EXP and money per encounter) | `tables/battle_rewards.json` | 6.5 |
| Enemy loot (item and chance per enemy) | `tables/battle_loot.json` | 6.5 |
| Item prices (buy and sell) | `tables/item_prices.json` | 6.5 |
| What each item shop sells | `tables/shop_stock.json` | 6.5 |
| Weapons, armour and accessories (stats) | `tables/weapon_data.json`, `armor_data.json`, `accessory_data.json` | 6.5 |
| What each usable item does | `tables/item_effects.json` | 6.5 |
| The partners' evolutions, blasts and growth (E5b) | `tables/evolutions.json`, `blast_forms.json`, `level_exp.json`, `vital_cuts.json`, `vital_random.json`, `stat_gains.json`, `resist_gains.json` | 6.5b |
| The field's encounters: enemies, ambush, battle rate (E5c) | `tables/field_enemies.json`, `field_encounters.json`, `battle_rates.json` | 6.5b |
| Cards: prices, shops, packs, effects, prizes, the default deck (E5d) and opponents (U8a) | `tables/card_prices.json`, `card_shop_stock.json`, `card_pack_slots.json`, `card_effects.json`, `prize_items.json`, `default_deck.json`, `card_opponents.json` | 6.5b |
| Gym and inns: trainings, gains, intensity costs and inn prices (E5e) | `tables/gym_trainings.json`, `gym_stat_gains.json`, `gym_stat_losses.json`, `gym_resist_gains.json`, `gym_hp_mp_gains.json`, `gym_intensity_costs.json`, `inns.json` | 6.5b |
| Existing Digimon (partners and enemies: stats, techniques, loot), new enemies and the encounters they appear in | `content/digimon.json` (content mod) | 6.9 |
| New cards (record, effect, art and name) | `content/cards.json` and `card/<key>.png` (content mod) | 6.10 |
| Screen and menu art (PNG, `atlas-v1` map) | `images/<folder>/<file>/<NN>_<MM>.png` | 6.2 |
| Field sprites (one PNG per frame, `sprite-v1` map) | `images/field/sprt/<name>/<EEE>.png` | 6.3 |
| A card picture (32x32 PNG, 256 colors; 316 cards) | `images/card/cardpak<0-4>/<NN>.png` | 6.2b |
| Map objects of a field stage (one PNG per page, `atlas-v1` map) and stage backgrounds | `images/field/stage/s<nnn>mask/<ii>.png`, `images/field/stage/s<nnn>bg<nn>/<ii>.png` | 6.2c |
| A map tile of a field stage (8-bit PNG per 128 px tile) | `z_stage/s<nnn>pack/<RR>_<CC>.png` | 6.2d |
| The files with no open format yet (collision maps, battle effects, overlays...) as a raw copy to start from | `raw/<disc path without AAA/>` | 6.2e |
| **Any disc file**, raw | `files/<disc path without AAA/>` | 6.4 |
| A game movie (MJPEG AVI with PCM audio) | `video/<NAME>.avi` | 8.1b |
| A sound effect (one sample of a bank, 16-bit WAV) | `audio/<BANK>/vag_NNN.wav` | 8.1d |
| A song (16-bit WAV in place of a sequence, with a loop) | `audio/<BANK>/seq_<package>_<record>.wav` | 8.1d |
| The characters of a field stage (change, add, remove) and their talks, with the mod's own flags; exits, scripted events and area battles | `stages/<nnn>.json` (`stage-patch-v1`) | 6.11 |

New sprites for field characters: `mods/<mod>/sprites/<name>/` (`sprite.json` + one PNG per frame), named by a stage patch with `"sprite"` (`docs/stages.md` section 11, in Spanish). Not yet: scripts (Lua, later), and loose XA audio (only inside the movies, 8.1b). See section 10.

## 2. Where things live

`<pref>` is the port's preferences folder:

| System | `<pref>` |
|---|---|
| Linux | `~/.local/share/dw3port/dw3/` |
| Windows | `%APPDATA%\dw3port\dw3\` |
| Portable mode (`portable.txt` next to the executable) | the executable's folder |
| Android | `Android/data/io.github.dw3port/files/` (visible from a PC over USB) |

```
<pref>/settings.ini          the player's; its [mods] section sets the order
<pref>/logs/                 one log per run (the last 10 are kept)
<pref>/data/                 the port's, generated from YOUR disc; safe to delete, it is rebuilt (tables in data/tables/, original content names in data/content/)
<pref>/mods/<mod>/           the community's: one folder per mod
<pref>/saves/default/        your port saves: slot0-2.dw3save (own format, ADR-008) and view/slot<N>.json (read-only view)
<pref>/import/               PS1 .mcd memory cards to import (imported once, the first time)
```

The **log** is the first place to look: `<pref>/logs/dw3-YYYYMMDD-HHMMSS.log`. Each layer prints one line per mod
(`[files] override ...`, `[images] override ...`, `[config] ... from ...`, `[mods] ... disabled: ...`) and every rejection
with its reason. To see what the game reads at that moment, set the log to `trace` (section 6.6): the **Log** row of the menu, or
`DW3_LOG=trace` (Linux `DW3_LOG=trace <executable>`, Windows cmd `set DW3_LOG=trace` and then the `.exe`);
`DW3_LOG_CHANNELS=files` limits it to the file reads.

### 2b. Getting the modding files: one button or one flag

The light exports (text, images, tables and raw files) happen by themselves on the first start. The heavy ones (videos, audio,
models and stages, about 700 MB and 2-3 minutes) sit behind **one** entry:

- **Menu:** *Options > System > Prepare modding files*. It runs the four exporters one after the other on worker threads (the game
  stays usable); the row's value shows the step (`Videos 3/14`, `Audio 12/71`, `Models 96/248`, `Stages 40/237`) and ends with `Done`
  or `Done, N failed`. It is idempotent: whatever is already up to date is skipped, so a second press finishes in seconds.
- **Command line:** `boot "<disc>.bin" --export-all` exports everything, prints progress to stdout (`export-all: Videos 3/14`), opens no
  window and exits with 0, or non-zero when anything failed (details are in the log). It works headless (`SDL_VIDEODRIVER=offscreen`,
  `ALSOFT_DRIVERS=null`).
- **Advanced:** the `[data] export_models`, `export_videos`, `export_audio` and `export_stages` keys of `settings.ini` still work
  (they export at start, also by name: `export_stages = S233, S330`); you do not need them.

### 2c. Your first mod in 5 minutes

Four command-line flags (no game window) cover create, test, adjust:

```
boot --new-mod my-mod                      # makes <pref>/mods/my-mod/ with mod.json, README.txt and empty folders
boot --list-mods                           # what is installed: version, kind, enabled or not (and why), load order
boot "<disc>.bin" --validate-mod my-mod    # runs the real parsers over that mod; one problem per line
boot "<disc>.bin" --validate-mods          # the same for every installed mod
                                           # <disc> may be the .bin or the .cue; leave it out to use the disc the game remembers
```

1. `boot --new-mod my-mod` (or `--new-mod my-mod --kind content`; `--kind` takes `assets`, `content` or folder names:
   `config content tables text images audio video files`, comma separated). It never overwrites an existing mod. A template folder with a
   tiny example per kind lives in `pc/examples/mods/_template/`.
2. A one-line patch: `mods/my-mod/tables/item_prices.json` with
   `{"format": "item_prices-v1", "rows": {"*": {"price": "*0.5"}}}` (everything at half price).
3. `boot "<disc>.bin" --validate-mod my-mod` ends with exit code 0 and `ok`, or 1 and one line per problem with the file and the key
   (`tables/battle_rewards.json row "12" field "money" rejected: ...`). Exit code 2: no such mod. `--validate-mod` also takes a folder path
   (a mod you have not installed yet).
4. Play: with `DW3_LOG=mods` the log says `tables: item_prices patched by mods/my-mod (234 rows)`.

What the validator looks at: `mod.json` and the order/dependencies; the `tables/` patches (the same per-row and per-field refusals as in
the game); `content/*.json`; `config/`; the PNG images (`images/`), the audio, the videos and the tiles (`z_stage/`); and that every file of
`files/` exists on the disc. It also warns about what the game ignores silently: an unknown `mod.json` key, a folder nobody reads (`table/`
for `tables/`), a `tables/` or `content/` file with the wrong name, `content/` in a mod whose kind is not `content`. What cannot be
checked without a running game shows as `skipped ... not checked` (today: the `text/` files, read when each one opens, and the palette of
the `z_stage/` tiles, compared with the tile while the map loads). `--list-mods` and `--new-mod` need no disc.

**JSON Schemas (completion and errors in your editor).** `docs/modding/schema/` has `mod.schema.json`, `items.schema.json`,
`techs.schema.json`, `digimon.schema.json`, `cards.schema.json` and `table-patch.schema.json`. In VS Code add, at the top of the file,
`"$schema": "<path to docs/modding/schema/items.schema.json>"` (relative to the file, or `file:///...`); the port ignores that key. The
schemas come from what the parsers read (keys, types, ranges); each table field's range is in its reference file
`<pref>/data/tables/<table>.json`.

## 3. Anatomy of a mod

```
my-mod/
  mod.json                              required
  config/stage_banner.json              only the keys you change
  config/transitions.json
  content/items.json, content/techs.json   new content (kind "content")
  tables/<table>.json                   table patches
  stages/<nnn>.json                     patch of the characters and talks of one stage (6.11)
  text/<language>/<file>.json           languages: eng fra ita ger spa es-419
  images/<folder>/<file>/<NN>_<MM>.png
  images/field/sprt/<name>/<EEE>.png
  files/<disc path without AAA/>        e.g. files/dat/z_stage/s330pack.bin
  video/<NAME>.avi                      a replaced movie (8.1b), e.g. video/MOVIEOPN.avi
  audio/<BANK>/vag_NNN.wav              a replaced sound sample (8.1d), e.g. audio/COMMON/vag_056.wav
  audio/<BANK>/seq_P_RR.wav             a song in place of a sequence (8.1d), with an optional seq_P_RR.json (loop)
```

Everything the port writes is **lower case** (`data/images/field/sprt/p001play/...`). When it reads `images/` and `files/`
(from mods and from `data/`) it ignores case, so a mod written with the disc's upper-case paths
(`files/DAT/Z_STAGE/S330PACK.BIN`) works too, on Windows, Linux and Android. New content (items, techniques) goes in
`content/<kind>.json` and table patches in `tables/<table>.json`.

- **Config** merges **per key** (a mod that only changes `hold_frames` inherits the rest). Keys and ranges: the
  reference files the port writes in `<pref>/data/config/` (a `"notes"` object explains each key).
- **Text, images and raw files** replace the **whole** resource.
- Every file carries `"format": "<name>-v<n>"`; an unknown version is rejected, never guessed.

## 4. `mod.json` (format `mod-v1`)

```json
{
  "format": "mod-v1",
  "name": "My mod",
  "version": "1.0.0",
  "author": "me",
  "description": "What it does, in one line.",
  "kind": "assets",
  "port": ">=0.4",
  "requires": ["base>=1.2", "other-mod"]
}
```

| Key | Required | Meaning |
|---|---|---|
| `format` | yes | always `"mod-v1"` |
| `name`, `author`, `description` | no | informational |
| `version` | no | digits and dots (`1.2.0`); `0` when missing. This is what `requires` compares |
| `kind` | no | `"assets"` (default: replaces resources) or `"content"` (adds or changes data; its rules arrive with phase 2, section 10) |
| `port` | no | minimum port version: `">=0.4"`. An older port disables the mod. Operators: `>=` `>` `<=` `<` `=` |
| `requires` | no | list of mods it depends on, by **folder name**: `"base"` or `"base>=1.2"` |

Keys the port does not know are ignored (the format grows compatibly). A malformed value (unknown `kind`, `requires`
that is not a list of strings, a version with letters) **rejects** the mod with
`mods: <path> rejected: mod.json: <reason>`. This port's version lives in `pc/shim/port_version.h`.

## 5. Load order and dependencies

**Mods tab (options menu, on Android too).** It lists every installed mod with its state: `On 2` (second in the load order), `Off`, `Blocked`
(a dependency or the port version stops it), `Invalid` (broken `mod.json`) or `Missing` (listed in `order` but not installed). The line below
gives the version, the kind and, for a blocked or invalid mod, the reason. *Confirm* turns a mod on or off; *Left* and *Right* move it earlier
or later in the order. Changes are written to `[mods]` in `settings.ini` (`order` only if it already existed or you moved something;
`disabled` for the ones turned off) and **apply on restart** (the row shows `Restart`). It holds up to 14 rows and the `settings.ini` lines
must fit 511 characters; with more mods, edit the file by hand. The game font has no commas, parentheses or the letter j: long reasons show
cut short (the full reason is in `--list-mods`).

1. Active mods: those in `[mods] order = a, b, c` of `settings.ini` (only those, in that order); without that line,
   every mod with a valid `mod.json`, alphabetically.
   `[mods] disabled = x, y` turns mods off even when they are in `order` (all the others stay on, including mods you install later).
   The **Mods** tab of the options menu writes both lines (see below).
2. **The last one wins.** For each resource the order is: mods (last to first) -> `data/` if you edited it -> disc.
3. Dependencies load **before** what needs them (the rest keeps the order of point 1).
4. A mod is disabled, with one log line, when:

| Log line | Cause |
|---|---|
| `mods: X disabled: needs port >=9, this is 0.5.0-beta` | its `port` is newer than the port |
| `mods: X disabled: requires base>=1.2, found 1.1` | the dependency is too old |
| `mods: X disabled: requires base, not installed or not active` | missing, or not in `order` |
| `mods: X disabled: requires base, which is disabled` | the dependency was itself disabled |
| `mods: dependency cycle, disabled: a b` | they require each other (or depend on a cycle) |

**Engine limits.** Up to 256 mods at once (`MODS_MAX`; the extra ones appear in the log as `rejected: more than 256 mods`),
64 keys per object and 64 numbers per array in `mod.json` and in the config files (`mods: ... rejected: mod.json: too many keys`
or `array too long`). Data tables have no count limit (the registry grows by itself) and a list may declare up to 65536 items;
a patch list past its field's `listMax` is rejected whole with a `tables: ... rejected: more than N items` line. A save file
holds up to 32 sections; old saves (up to 8) read as before.

## 6. Tutorials

First of all: start the game **once** with your disc. That generates `<pref>/data/` (text, images, config) from your
copy. What follows uses those folders. Run the commands from the repository folder.

### 6.1 A text mod

Texts are in `<pref>/data/text/<language>/<file>.json` (`eng fra ita ger spa es-419`):
`{"file": "...", "format": "text-v1", "strings": [{"index": 0, "text": "..."}, ...]}`.

1. Create `my-mod/mod.json` (section 4) and copy the file to `my-mod/text/<language>/<file>.json` (same name).
2. Edit only the `"text"` values. It must keep **every** entry with its `index`, and only characters the game can
   write; control codes appear as `{0202}` and are kept as they are.
3. Copy `my-mod` to `<pref>/mods/` and start the game. If something is off, the log names file, entry and position.

```
cp -r my-mod ~/.local/share/dw3port/dw3/mods/          # Linux
xcopy /E /I my-mod "%APPDATA%\dw3port\dw3\mods\my-mod"  :: Windows
```

### 6.2 An image mod (menus and screens)

1. Find the image in `<pref>/data/images/<folder>/<file>/<NN>_<MM>.png`. Next to every PNG there is a `<NN>_<MM>.json`
   (`atlas-v1`) with the pieces of the image; `python3 tools/image_atlas_preview.py <png>` draws them numbered (needs
   Pillow).
2. Copy it to `my-mod/images/<folder>/<file>/<NN>_<MM>.png` and edit it **keeping**: same width and height, an
   **indexed** PNG (with a palette, not interlaced), at most 16 colors (256 in 8-bit pages), and the palette **order**
   (the PNG's indices replace the game's). Alpha 0 = transparent.
3. A PNG that breaks the rules is rejected: `images: <path> rejected: <reason>` and the next source is used. Mods are read **once, at
   startup** (an in-memory index of which PNGs each mod has): a change shows after you **restart** the game. To see changes
   with the game open, start it with `DW3_MODS_HOTRELOAD=1` (for modders only: it re-checks the files every second and costs
   performance with many mods).

Shortcut without an editor (palette only, see section 8): example `pc/examples/mods/menu-frames-gold`.

### 6.2b An image mod: the card pictures

The card game keeps each card's picture in `CARD/CARDPAK0-4.BIN` (64 cards per file, 60 in the last: 316 in all). The
port exports them as `<pref>/data/images/card/cardpak<A>/<NN>.png`, with `A` = 0-4 and `NN` = 00-63. Game card number
`k` (from 1) is `cardpak<(k-1)/64>/<(k-1)%64>`. Each PNG is **32x32**, 8-bit, with **its own 256-color palette** (index 0
transparent): more freedom than the menus, because every card loads its own palette.

To replace one, copy it to `my-mod/images/card/cardpak<A>/<NN>.png` and edit it under the rules of 6.2 (same size,
indexed, at most 256 colors, the palette order). The replacement shows in the album, the shop, the deck editor and the
battle. The 12 bytes before each picture (color, AP, HP, kind...) are not exported: they are game data, not art.
`CARD/CARDDATA.BIN` (card battle windows and sprites) comes out as `images/card/carddata/00_00.png` and `01_00.png`, with
its `atlas-v1` map, and is replaced like any other screen. `CARDPACK.BIN` is almost a copy of the five files joined (it
differs in 6 of the 316 cards) that the game does not read by number; it is not exported.

### 6.2c An image mod: field stage art

`FIELD/STAGE/S<nnn>MASK.BIN` (it is not a depth mask) holds the art pages of stage `<nnn>`'s map objects; the port exports
them as `<pref>/data/images/field/stage/s<nnn>mask/<ii>.png` (page `ii`, 256 tall; 128 wide when 8-bit, 256 when 4-bit),
each with an `<ii>.json` (`atlas-v1`: the pieces the sheet in `S<nnn>TMPK.BIN` draws from that page). The backgrounds
`S415BG01/02` and `S815BG01/02` come out as `field/stage/s415bg01/<ii>.png` (128x256, 8-bit, no map).

Replace them under the rules of 6.2 (same size, indexed, at most 16 or 256 colors). Notes:
- A page can mix 4-bit and 8-bit pieces: the PNG is read in the mode most pieces use and the `json` marks the others
  with `read_as`.
- The palette of a 4-bit page may live in **another** page or in the CLUT of the file's 8-bit TIM: the PNG shows that
  palette, but editing its PLTE changes nothing (only the pixels do). Change the palette in the page that holds it.
- The 34 stages without a usable sheet (many of the 9xx) come out with a grey ramp, not the game's colors.

### 6.2d An image mod: the map tiles of a stage

Each field stage's map lives in `Z_STAGE/S<nnn>PACK.BIN` (237 files, 11,745 tiles, 128 px tall). The game reads them from the disc as
the player walks. They do not take the same path as the rest of the art, so they have an exporter of their own, **on demand** (about
12,000 files and 250 MB):

- **Menu:** *Options > System > Prepare modding files* (one button for videos, audio, models and stages; see 2b) exports all 237 (the row's value reads `Stages 3/237`). It runs on a
  worker thread; the game stays usable.
- **`settings.ini`:** `[data]` / `export_stages = all` or `export_stages = S233, S330` exports them at start, before the window opens.
  A stage already exported by the same exporter version is **skipped** (the second start does not read the disc again). The menu row
  rebuilds all of them.

Each stage comes out in `<pref>/data/z_stage/s<nnn>pack/`:

- `<RR>_<CC>.png`: one tile (row `RR`, column `CC`, two digits; tile `n` of the stage is `RR = n / width`, `CC = n % width`). An 8-bit
  indexed PNG (256 colors with the tile's own CLUT; color 0 is transparent), 128 tall and 128 to 256 wide. Empty tiles have no file.
- `s<nnn>pack.json` (`stage-pack-v1`): the stage's `width` and `height` in tiles, `tileBytes` and, per tile, `frame`, `row`, `col`, `png`,
  `size` (bytes on the disc), `rle` (whether it is packed), `timOffset`, the image's `width`/`height` and `depths`: three lists (one per
  depth) of the `x, y, u, v, w, h` cut-outs the game draws from that PNG. It is reference only: the port does not read it.
- `manifest.json`: what the port wrote (the usual `.new` rule: a PNG you edited stays and the new one goes beside it as
  `<RR>_<CC>.png.new`).

Replace them under the rules of 6.2 (same size, indexed PNG, at most 256 colors; a PNG with fewer colors only changes the first ones
and the rest of the palette stays as on the disc) by putting the file at `mods/<mod>/z_stage/s<nnn>pack/<RR>_<CC>.png` (the last mod
wins; any letter case); editing the PNG in `data/z_stage/` also works (once it differs from the manifest). A PNG that breaks the rules is
refused with one log line (`images: <path> rejected: <reason>`, once) and the next source is tried. The change shows when the tile is
loaded (on entering the stage or walking up to it); the files are read at startup: a change shows after a restart (or with `DW3_MODS_HOTRELOAD=1`, which looks again every second). Without a PNG the game is
identical. Only the image changes: where each cut-out goes is fixed by the disc's tile (moving cut-outs would need another format).

### 6.2e The files with no open format: raw copies

What no other exporter covers is copied byte for byte to `<pref>/data/raw/<disc path without AAA/, lower case>` (for example
`raw/dat/field/stage/s233tmpk.bin`) so you have the original to start from. It is about 15 MB (989 files on the EU disc), so it
needs no menu: it runs at start, before the window opens, and a start that finds every kind already written by the same exporter
version does not read the disc.

- **`settings.ini`:** `[data]` / `export_raw` is `all` when the line is absent, `off` to write nothing, or kinds separated by commas
  (`export_raw = stage-map, battle-effect`).
- `README.txt` (what the folder is), `raw.json` (`raw-v1`: per kind `files`, `bytes`, `know`, `usedBy`, `replace` and `works`, then
  every file with its `size` and `kind`) and `manifest.json` (the usual `.new` rule: a file you edited stays and the new one goes beside
  it as `<file>.new`).

Nothing here is read by the game from `data/`. To change a file, copy it to `mods/<mod>/files/<same path>` (section 6.4: the last
mod wins, any letter case). Whether the game reads it that way depends on the kind (`works` in `raw.json`):

| Kind | Files | Replaced from `mods/<mod>/files/`? |
|---|---|---|
| `stage-map` | `S<nnn>TMPK.BIN`, 294: collision grid, battle zones, doors (entry 0: sprite sheet of the map objects) | **Yes, tested:** with a modified `S330TMPK.BIN`, a run into stage 330 logs `read 437 DAT/FIELD/STAGE/S330TMPK.BIN ... src=raw` (without the mod: `src=disc`) |
| `stage-demo`, `battle-effect`, `battle-common`, `sprite-sheet`, `card-opponents` | `SSUBDEMO`/`SDIGDEMO`, `FIGHT/EFFECT/*` (239), `F000COM1-4` and `PGFGTC00/W00`, the `...CS.BIN` sheets (15), `CARD_NPC.BIN` | Should be: the game reads them through the file cache like `TMPK`. Not run with a replaced file |
| `training-data`, `card-pack`, `text-unused`, `filler` | `TRAINING/TRANIN*` (12), `CARDPACK.BIN`, `COUNTRY/JPN` and `USA` (100), `NONE____.BIN` | No effect: nothing in the game reads them |
| `stage-overlay`, `mode-overlay` | `PRO/*.PRO` (318): MIPS code | No effect: the port links the code from `src/`. The exception is `SMDLDATA.PRO` (the fighters file): it is read, and a copy replaces it whole (the content mods' additions to it are then not served) |

Movies and the stage tiles (`Z_STAGE`) do not use this route (they have their own folders, 8.1b and 6.2d). The edit formats of these
files do not exist yet: treat them as black boxes and replace them with another file of the same layout.

### 6.3 Recolor a field sprite

Each `FIELD/SPRT/<NAME>.BIN` is extracted to one 4-bit PNG per frame plus a `sprite.json` (`sprite-v1`: frames,
animations, sets and directions). The player in Central Park is `P002PLAY` (and `P001PLAY`). Each `P002PLAY` pose lives in its own file: `P002PLYD`
(drop, land), `P002PLYF` (gauge minigame), `P002PLYK` (search) and `P002PLYL` (stairs, climbing walls); a mod that
recolors the player must cover them all (`FIELDSTG_animateActor`, `actor_control.c`).

```
cp -r pc/examples/mods/player-recolor ~/.local/share/dw3port/dw3/mods/
python3 tools/image_mod_palette.py --sprite P001PLAY --sprite P002PLAY --sprite P002PLYD --sprite P002PLYF --sprite P002PLYK --sprite P002PLYL --to 0
```

The script changes only each frame's palette (blues to red; `--to 120` green, `--to 45` gold). On Windows it uses
`%APPDATA%\dw3port\dw3\` by itself, or pass `--data` and `--mod`. Rules: those of the menu art, with **16 colors**.
`python3 tools/image_atlas_preview.py <data>/images/field/sprt/p002play --set 4 --dir 6 --gif` animates one direction.

### 6.4 Replace a raw disc file

Works for everything without an open format yet (models, backgrounds, stages, sound). It **replaces the whole file**:
making the content valid for the game is up to you.

1. Find the file: `python3 tools/file_names.py "<disc>.bin" > names.tsv` lists number, first sector, status and path
   (`AAA/DAT/Z_STAGE/S330PACK.BIN`). The path without `AAA/` is the one for `files/`.
2. Get its original bytes: `python3 tools/extract_disc.py "<disc>.bin" out` writes every file keeping paths (under
   `out/AAA/DAT/...`). Work in a folder of your own, outside the repository.
3. Modify the file and place it at `my-mod/files/dat/z_stage/s330pack.bin`; copy the mod to `<pref>/mods/`.
4. The log says `[files] override DAT/Z_STAGE/S330PACK.BIN from mods/my-mod`. It may have a different size than the
   original. With the log at `trace` (channel `files`) each read shows as `[files] read <n> <path> off= sectors= src=raw`.

Rules for raw files:

- A file that the text or image layers already handle with an open format **keeps that behavior**: a raw replacement
  of the same file is ignored (`files: <path> from mods/<mod> ignored: ...`, once). Use `text/` or `images/`.
- The game reads the file when the scene loads it: it is read at startup: a change shows after a restart (with `DW3_MODS_HOTRELOAD=1`, at the next load, looking at most once a second).
- Movies (`STR`) and XA audio do not go through this layer: a movie is replaced with `video/<NAME>.avi` (8.1b).

### 6.5 Data tables (rewards, loot, prices and shops)

Game rules that live in tables are changed with a **patch**: `mods/<mod>/tables/<table>.json`. Four tables are open today,
`battle_rewards`, `battle_loot`, `item_prices`, `shop_stock`, `weapon_data`, `armor_data`, `accessory_data` and `item_effects`; the others (encounters, Digimon, techniques) will open with the same format
(ADR-007).

**Reference.** The port writes `<pref>/data/tables/<table>.json` with the **original values of your game**, the keys
and, under `"notes"`, the range of every field. Use it to read values and copy keys. **Editing it does not change the
game**: if you edit it the port keeps your copy and writes the new version to `<table>.json.new`. The numbers come
from your disc and never go into a mod you share.

**Patch format** (`"format": "<table>-v1"`):

```json
{
  "format": "battle_rewards-v1",
  "rows": {
    "12": { "money": 300 },
    "13": { "exp": "*2" },
    "*":  { "money": "*1.5" }
  }
}
```

- `rows` uses the **key** of each row (in `battle_rewards`, the encounter number). `"*"` applies to every row.
- A value is either a number (replaces) or an operation on the current value: `"*k"` (multiply), `"+k"`, `"-k"`
  (add or subtract). An operation is rounded and clamped to the field's range; a number outside the range is
  rejected.
- A row with its own key wins over `"*"` **on the fields it names**; its other fields follow the `"*"` rule.
- What the patch does not name stays as it was. An unknown field, a wrong type, a key that does not exist or a value
  out of range is rejected **per field or per row** (`tables: <path> row "12" field "exp" rejected: ...`) and the
  rest applies. A malformed file or one of another version is rejected whole (`tables: <path> rejected: ...`).
- **Order:** original values, then each mod in load order (section 5). Two mods that double give x4, and a later
  replacement wins over everything before it. The log says which mods touched the table:
  `tables: battle_rewards patched by mods/a, mods/b (N rows)`. The table is always rebuilt from the original values, so
  applying it again never accumulates. With no mod that has `tables/`, no table is touched.
- Saved games: changing existing rows is compatible; the memory card does not store tables. This kind of mod is
  `assets` (it adds no rows or identifiers). New rows come later, for `content` mods only.

**`battle_rewards`.** Key: the encounter number (the battle's row), from 0. Fields (range):

| Field | What it is | Range |
|---|---|---|
| `digimonExp` | EXP shared by the Digimon used in the battle | 0 to 9999999 |
| `exp` | EXP shared by the partners that fought (the report gives 100%, 60% or 33% depending on how many fought) | 0 to 999999 |
| `money` | Money the battle gives (a fifth more with the item that boosts it; the wallet tops out at 9999999) | 0 to 9999999 |

The ranges are the caps the game's report applies. The report reads the row when it opens, so a change shows in the
**next battle**, without restarting. It is applied on entering the report, after the port restores the overlay's data.
To see the row and the wallet of each report, set the log to `trace` with the `tables` channel (section 6.6).

Example: `pc/examples/mods/double-rewards` (partner EXP and money x2 in every encounter).

**`battle_loot`.** The loot of each enemy, which lives in the battle table (disc file 463, one record per enemy). Key:
the **enemy id** (`BattleTableEntry.id`, the first field of the record; not a position). Fields (range):

| Field | What it is | Range |
|---|---|---|
| `item` | The item the enemy may leave (0: none). An enemy whose original item is 0 leaves nothing: give it one here if you want it to drop. An item a mod adds is written by name (`"<mod>:<name>"`, 6.7) | 0 to 402, or the name |
| `itemChance` | Chance of leaving it, in 1024ths less one: the game drops the item when `itemChance + 1` is greater than a random number from 0 to 1023, so 1023 always drops it | 0 to 1023 |

`itemChance` also scales how easy it is to **steal** that item during the battle, so raising it helps both. Of the enemies
fighting together that carry an item, the game picks one at random and only that one rolls.

This table is not an array of the program but a disc file: the port applies the patch **to the buffer each time the game
reads the file** (when a battle starts), starting from the bytes just read, so repeating it never accumulates and the
change shows in the **next battle**. If a mod also ships the raw file (`files/DAT/BATTLE/...`, 6.4), that file replaces
the disc's first and the table patch is applied **on top** (rows are looked up by id in the file that was actually read,
not by position). With several patches the usual rules hold (mod order, `"*"`, a later replacement wins). With the mod
log (6.6) each battle says which enemies changed and whether an item dropped.

A name no installed mod defines (or a new-item number that does not exist) rejects that field with a log line and the rest of the
row applies. `"*k"`, `"+k"` and `"-k"` on `item` that give a number with no item leave it as it was. The dropped (or stolen)
item reaches the bag through the same counter as any other and its name shows in the results message.

Example: `pc/examples/mods/generous-loot` (`itemChance` x2 for every enemy); `sample-item` also ships a `battle_loot`.

**`item_prices`.** What the shops charge and what they pay. Key: the **item id** (the number the game uses, from 1).
Fields (range):

| Field | What it is | Range |
|---|---|---|
| `price` | What the shop charges for one (the buy list, the total and the money taken all read this value; 0 is free) | 0 to 65535 |
| `sellPrice` | What the shop pays when you sell one. It is a value of its own, **not** half of `price`. 0: the item cannot be sold | 0 to 65535 |

It is an array of the program that nothing resets: the port patches it **once at startup**, before the first mode, and every
shop reads it when it opens, so changing a price changes it in all of them at once (it applies from the moment the game starts;
the price is not stored in the save). A patch only changes these two fields of the item: the rest of the record (effects,
stats, type) is untouched. An item sold for 0 no longer shows in the sell list, and a buy price of 0 is treated by the shop as 1
when it works out how many you can afford.

Example: `pc/examples/mods/cheap-shops` (`price` x0.5 for every item).

**Equipment and item effects: `weapon_data`, `armor_data`, `accessory_data`, `item_effects`.** What each piece of equipment adds
and what each usable item does. Four tables of the program, all with the same key: the **item id** (or `"<mod>:<name>"` for an
item a mod adds: it has its **own copy** of the record, made from its base item's, so changing it never touches the original; it
only exists when the item is of that kind). They are patched **once at startup**, before the first mode: the status screens, the
shops and every battle read the record when they need it, so the change shows everywhere. Prices are not here but in
`item_prices`. A value outside its range is rejected (that field, with a log line; the rest of the patch applies).

`weapon_data` (123 rows, ids 92 to 214) and `armor_data` (77 rows, ids 215 to 291) share these fields:

| Field | What it is | Range |
|---|---|---|
| `charisma` | What it adds to charisma (the total of what is worn stops at 999) | -999 to 999 |
| `kind` | Where it goes: 1 right hand, 2 left hand, 3 either hand, 4 head, 5 body, 6 accessory, 7 both hands, 8 group accessory | 1 to 8 |
| `group` | Group number (only a group accessory, `kind` 8, reads it) | 0 to 255 |
| `partners` | Who can wear it: one bit per partner (`1 << number`), 255 is all | 0 to 255 |
| `stat1`, `stat2` | What each bonus raises: 0 none, 1-6 strength, defense, spirit, wisdom, speed and charisma, 7 all six, 8-14 the seven resistances | 0 to 14 |
| `amount1`, `amount2` | The bonus, signed (the stat stops at 999) | -999 to 999 |

Only weapons have: `atk` (adds to attack, 0 to 999), `accuracy` (adds to accuracy in battle, 0 to 255; an 8-bit total, keep it
low), `hitEffect` (a label of the hit effect; the game does **not** read it, the effect follows the item id), `statusChance` and
`statusPower` (chance and strength of the status, or the critical bonus; only weapons with a hit effect read them, because the
game picks those by item id) and `family` (the family its hits count as, 2 and up; under 2 it has none), all 0 to 255. Only armour
has: `def` (adds to defense, signed, -999 to 999) and `evasion` (adds to evasion in battle, 0 to 255).

`accessory_data` (68 rows, ids 292 to 359): `charisma`, `kind` (6 accessory, 8 group; 7 is rejected), `group`, `partners`, `stat`
(as above, plus 17 to 21: the battle resistance to poison, paralysis, confusion, sleep and knock-out; 0 to 21) and `amount`
(-999 to 999). Three accessories are read by their item id (triple hit, critical, counter): their `amount` is that effect's.

`item_effects` (83 rows, the usable items 43 to 394, card boosters included; one row per record, so two items that share a record
show as one row):

| Field | What it is | Range |
|---|---|---|
| `flags` | Where it can be used, a sum: 1 from the status menu, 2 in battle, 4 marks a card booster (nothing tests it) | 0 to 7 |
| `kind` | What it does: 0 nothing here (battle items act by their item id), 1 heals `amount` HP, 17 refills `amount` TP, 2 to 16 raise a stat by 1 up to `amount` points | 0 to 17 |
| `amount` | The HP healed, the TP, or the top of the random boost; battle items read it as their own number (a gauge, a boost in 128ths) | 0 to 20000 |

The 20000 cap on `amount` keeps the healed HP inside the game's 16-bit stats. A boost (`kind` 2 to 16) with `amount` 0 is rejected
**whole**: the row goes back to its original values and `tables: item_effects row 44 rejected: ...` is logged (the game rolls a
random number up to `amount`).

The id lists by item type (`WEAPON_IDS`, `ARMOR_IDS`, `ACCESSORY_IDS`, `USABLE_ITEM_IDS`, `KEY_ITEM_IDS`) and
`ITEM_TYPE_CATEGORIES` are **not tables**: they derive from each item's `kind` and `type` (they are the indexes the menus use) and
new items already join their list by themselves; editing them by hand would break the menus.

Example (weapon 92 with 500 attack and a defense bonus):

```json
{
  "format": "weapon_data-v1",
  "rows": { "92": { "atk": 500, "stat1": 2, "amount1": 12 } }
}
```

**`shop_stock`.** What each item shop sells. Key: the **shop number**, from 0 (the number the shop mode is opened with; 31
shops). A single field, which is a **list**:

| Field | What it is | Values |
|---|---|---|
| `items` | The item ids the shop sells, in the order it lists them | 1 to 64 ids, each from 1 to 402 |

A list is written in two ways, and both can be combined across mods:

```json
{
  "format": "shop_stock-v1",
  "rows": {
    "1": { "items": [5, 6, 7] },
    "2": { "items": { "remove": [5], "add": [9, 10] } },
    "*": { "items": { "add": [1] } }
  }
}
```

- **An array replaces** the whole list. A later mod that replaces wins over the earlier ones.
- **`{"remove": [...], "add": [...]}` edits** the current list: it removes first, then appends the ids that are not
  already there. So two mods that add an item to the same shop add up without overwriting each other, and a mod can add
  without copying the original list (which is data from your game). `"*"` applies to every shop.
- 0 ends the game's lists, so it is not a valid id. A list longer than 64, empty, with an id outside 1 to 402 or that is
  not an array (nor `add`/`remove`) is rejected with a log line (`tables: <path> row "3" field "items" rejected: ...`, or
  `tables: shop_stock row 3 field "items" add rejected: more than 64 items`) and that shop stays as it was. The limit of 64
  is the port's (the biggest shop in the game has 52); the game's own lists are never written, the port points the shop
  at a list of its own.

The shop reads its list when it opens, so a change shows **the next time you enter a shop**, with no restart. It is applied
on entering the shop mode, after the port restored the overlay's data. Prices are not in the shop but in `item_prices`: an
item you add costs what that table says (and changing it changes every shop). It only covers the item shops; the card shop (`card_shop_stock`) has its own table, and the inns
(`inns`) too.

Example: `pc/examples/mods/shop-extra-item` (shop 0 sells one more item).

### 6.5b The other game tables (equipment, evolutions, growth, the field and the cards)

They use the same format and rules as section 6.5: `mods/<mod>/tables/<name>.json` with `"format": "<name>-v1"` and a `"rows"` object
keyed by row; a number replaces, `"*k"` multiplies, `"+k"` and `"-k"` add, and the key `"*"` is every row. Each table has its
reference copy in `<data>/tables/<name>.json` (the original values, the valid ranges and, where it makes sense, the name of each row
in `"labels"`); it is only for reading, editing it changes nothing. A row or field out of range is rejected with a line in the log
(`DW3_LOG=mods`). The detail of each table (what the game reads, where the ranges come from) is in `docs/data-tables.md`.

| Table (`tables/<name>.json`) | Row key | What it changes | When it shows |
|---|---|---|---|
| `weapon_data` (E5a) | item id 92-214 (or `<mod>:<name>`) | the weapons' stats: charisma, attack, accuracy, two bonuses, hit effect | when equipping |
| `armor_data` (E5a) | item id 215-291 | the armour's stats | when equipping |
| `accessory_data` (E5a) | item id 292-359 | the accessories' bonus and kind | when equipping |
| `item_effects` (E5a) | usable item id (43-394) | what a usable item does: effect kind and amount | when used |
| `evolutions` (E5b) | `partner * 100 + entry` (352 rows) | the form each partner learns and its requirements | in the next battle report |
| `blast_forms` (E5b) | the Digimon id (52 forms) | the form a blast turns into, per level tier | in the next blast |
| `level_exp` (E5b) | first level of the tier (1, 5, 20, 40) | the EXP each tier of levels asks for | in the next report |
| `vital_cuts`, `vital_random` (E5b) | first level of the tier; slot 0-8 | the HP and MP a partner gains on a level up | in the next report |
| `stat_gains`, `resist_gains` (E5b) | first level of the tier (1-80); slot 0-7 | the stats' and resistances' gain per level | in the next report |
| `field_enemies` (E5c) | row number (0-522) | Digimon, level, HP, MP and strength of the enemies of the field's encounters | in the next field battle |
| `field_encounters` (E5c) | encounter index (0-334) | which enemy rows each encounter uses, the ambush, the kind and the 12 blocks | in the next field battle |
| `battle_rates` (E5c) | rate index (0-5) | how many steps each rate takes off the count to the next random battle | from the next step |
| `card_prices` (E5d) | card id (the 44 priced cards) | what the card shop charges for each | when the card shop opens |
| `card_shop_stock` (E5d) | shop number (49-67, 70-74) | the cards each shop sells (a list of ids 1-314 or `"<mod>:<name>"`) | when the buy screen opens |
| `card_pack_slots` (E5d) | `pack item id * 10 + slot` (35 packs x 6 slots) | the 16 cards each slot draws from when a pack is opened (`card1`..`card16`) | when a pack is opened |
| `card_effects` (E5d) | card id (1-60) | conditions, message, target kind and step script (`step1`..`step40`) of the first 60 cards | when the card is played in a duel |
| `prize_items` (E5d) | prize number (0-35) | the item an opponent gives when beaten | when the opponent is loaded |
| `default_deck` (E5d) | position in the deck (0-39) | the deck you play with when the chosen folder is empty (card id minus one) | when a duel picks the deck |
| `card_opponents` (U8a) | the duel mode's argument minus one (0-169) | each opponent's 40-card deck, counter cards, level and prize (`CARD_NPC.BIN`) | when the next duel with that opponent starts |
| `gym_trainings` (E5e) | `gym index * 100 + slot` (14 x 16 slots) | which trainings each gym offers: `id` (1-24), the stat it raises (`stat`) and the second one (`other`) | when the gym opens |
| `gym_stat_gains` (E5e) | column 0-11: `kind * 6 + bonus try * 3 + intensity` | how much a training raises a battle stat (`base` and `range`) | when the training result shows |
| `gym_stat_losses` (E5e) | intensity 0-2 | how much a resistance training may take off a battle stat | when the result shows |
| `gym_resist_gains` (E5e) | `table * 10 + slot` (3 tables x 9) | how much a training raises a resistance (low, middle or high table) | when the result shows |
| `gym_hp_mp_gains` (E5e) | slot 0-8: `intensity * 3 + tier` | how much a training raises the maximum HP or MP | when the result shows |
| `gym_intensity_costs` (E5e) | intensity 0-2 | the training points (TP) each intensity costs | when the intensity is chosen |
| `inns` (E5e) | game mode of the inn, in decimal (21 inns; 522 is the first) | `price` per night and per partner, and `string`, the text of the inn's name | when the inn opens |

Notes:

- The field tables are applied each time the field is entered. `field_encounters` names its enemies by row number of
  `field_enemies`: the rows are shared, so changing a row changes every encounter that uses it. Neither row 0 of `field_enemies`
  nor encounter 0 can change, and an encounter cannot be left with an empty first enemy.
- `field_enemies` takes in `fighter` an enemy id, `"dw3:digimon/<id>"` or the name of an enemy of a content mod
  (`"<mod>:<key>"`); a partner form is rejected.
- The card tables are applied each time the card shop or the duel is entered. Each reference carries the English card (or item) name in
  `"labels"`. `card_prices` only covers the 44 priced cards of the disc (the shop charges 1 for a card with no row) and its key is the card:
  a row cannot be made to price another card. The price of a mod's card is not a row: it is the `price` field of its `cards.json` (6.10).
  Wherever a card table asks for a card (`card_shop_stock.cards`, `card_pack_slots.card1`..`card16`, `default_deck.card`, `card_opponents.card1`..`card40` and `counter1`..`counter20`)
  it accepts an original id 1-314 or `"<mod>:<name>"` (a card a content mod adds, 6.10); a name no installed mod defines is rejected with a log line
  (`no installed mod defines "..."`) and the field stays as it was. The ids 315 and 316 are not cards and are rejected. `card_shop_stock.cards` is a list (replace `[1, 2]` or `{"add": [...], "remove": [...]}`) of up
  to 29 cards, six per page. Each pack slot draws from 16 cards at random: listing a card twice makes it more likely.
- `card_effects`: `step1`..`step40` are duel step numbers (1-174) and a 0 ends the script, so there must be a 0 among the 40 (otherwise the
  whole row is rejected). Changing a script is advanced: some steps wait for the target `target` gives and others skip the steps after
  them; the port only checks ranges. `default_deck.card` is the card id **minus one**, as the game stores it.
- `card_opponents` (U8a) comes from the disc file `CARD_NPC.BIN`, not from memory: the patch is applied to the bytes each time the game reads the file (when a duel starts),
  so a file a mod replaces is patched on top as well. The key is the row number (0-169): the duel reads row `argument - 1`, and the field's `CARD_BATTLE` actions set the argument, so one
  NPC can use more than one row. Each row has 40 places of the opponent's deck with `cardN` (id 1-314), `flaggedN` (0 or 1: bit 15 of the card, which the game reads when it picks
  the opponent's answer), `groupN` (1-7: from which round it may draw it; 7 is the reserve kept for last) and `kindN` (0-7: how the computer plays it, it sorts its hand by kind and
  priority), plus `counter1`..`counter20` (ids 1-314 it answers with; the first 0 ends the list and a 0 at the start makes it use its usual answers), `level` (1-99, only shown) and
  `prize` (0-35: the `prize_items` number). The whole row is rejected if the counter list has no 0 among its first 13 places or if the whole deck is in group 7 (the game would write
  outside its arrays). The reference has no `"labels"`: the file holds no opponent names. `--validate-mods` flags out-of-range fields as errors; a whole-row rejection shows as a
  log note (as in `card_effects`).
- The gym tables are applied each time the gym is entered, and `inns` once when the port starts. The references of `gym_trainings` and `inns` carry the English name of
  the training and of the inn in `"labels"`. In `gym_trainings` the key comes from the gym index (the mode's argument; the disc fills rows 0-13) and the slot (0-15); an `id` always goes
  to the same place on the menu, so two entries of one gym with ids of the same place leave only the last one. A row with an `id` and no `stat` (or `stat` 6 or 7), or with `stat` 8-14 and
  an `other` that is not 0, 1-5, 15 or 16, is rejected whole. Gains are `base` plus a random number from 0 to `range - 1` (with `range` 0, just `base`), and the row is rejected whole
  if `base + range - 1` is more than the stat can hold (31768 for stats and resistances, 22768 for maximum HP/MP, 32767 for losses). Several growth values share a table in
  `gym_resist_gains`: changing a row changes all of them. Which table each growth uses is not a table yet. In `inns` the key is the inn's mode and cannot be changed; `string` is an
  entry of the inn text file (0-21), where 1-15 and 21 are names and the rest the menu's texts.
- The opponents' decks (`CARD_NPC.BIN`) are not a table yet.
- The battles of each stage (`stageBattles`) are not a table but a section of the stage patch: `stages/<nnn>.json` with `battles` (6.11, `docs/stages.md` section 10, Spanish).
- The `"encounters"` placements of `content/digimon.json` (section 6.9) are applied after these tables.

### 6.6 Log

The port has **one log** with three levels. Each line comes out as `[channel] message` in the session log
(`<pref>/logs/dw3-YYYYMMDD-HHMMSS.log`, see section 2: Linux `~/.local/share/dw3port/dw3/logs/`, Windows
`%APPDATA%\dw3port\dw3\logs\`).

| Level | What it writes |
|---|---|
| `off` (default) | Only the startup summaries (`[mods]`, `[config]`, `[files] override`, `[images] override`, `[tables] ... patched`) and the rejections with their reason. |
| `mods` | Also one line each time a mod **changes a value that is actually used** (below). |
| `trace` | Also the technical detail of each channel: every file read, every image loaded, the row and wallet of each report, the stage timings. |

Choose it with any of these (the environment wins over the file):

- The **Log** row (Off/Mods/Trace) of the System tab in the options menu.
- `[debug]` `log = off|mods|trace` in `settings.ini`; `channels = tables, files` (optional, empty = all) lets only those
  channels through. The port writes both keys and keeps them when it saves. The old key `mod_log = on` reads as `mods`
  and is rewritten as `log = mods`.
- The environment variables `DW3_LOG=off|mods|trace` and `DW3_LOG_CHANNELS=a,b`.

Channels: `tables` (data tables), `files`, `images`, `stage` (stage timings), `config` and `mods` (summaries only). At
`trace` only, the engine's per-frame and per-event detail: `gfx` (layers, and one line per drawn layer every frame), `pad`
(the button read and the menu state, every frame), `field` (the player's position, every frame), `tasks` (actor register,
unregister and find), `overlay` (stage overlay loads), `text` (which source served each text file, reflow totals, button
icons), `memcard` (card commands) and `battle` (battle events and rolls). At any level, a normal session writes only the
startup summaries and the problems (and the mod lines at `mods`): nothing per frame. Channels filter the `mods` and `trace` lines, not the summaries or the rejections. A `settings.ini` that shows only what
your table mod changes, with the detail of the reports:

```ini
[debug]
log = trace
channels = tables
```

Shortcuts (the usual ones, still working): `DW3_TABLE_TRACE=1`, `DW3_FS_TRACE=1`, `DW3_IMG_TRACE=1`, `DW3_STAGE_TIMING=1`,
`DW3_MEMCARD_TRACE=1`, `DW3_TEXT_TRACE=1` and `DW3_BATTLE_EVENT_LOG=1` set `trace` on their channel (`tables`, `files`,
`images`, `stage`, `memcard`, `text`, `battle`) and nothing else; `DW3_MOD_LOG=1`
is the same as `log = mods`.

`mods` level lines today:

```
[tables] battle_rewards encounter 7: exp 20 (orig 10), money 100 (orig 50) by mods/double-rewards
```

One line per battle report whose encounter has changed values: only the fields the mod changed, with the original value
in brackets, and the mods that touched the table. Nothing is written when the row equals the original. For loot
(`battle_loot`), one line for each enemy of the battle whose row changed and a last one with the result:

```
[tables] battle_loot enemy 15: item 3 (orig 3), itemChance 400 (orig 200) by mods/generous-loot
[tables] battle_loot drop: item 3
```

(`drop: none` when the roll left nothing.) For prices (`item_prices`), opening a shop writes **one line per item** that the
shop sells (`price`) or that your bag could sell (`sellPrice`) and whose price changed, once per visit:

```
[tables] item_prices item 12: price 100 (orig 200) by mods/cheap-shops
```

And if a mod changed the list of the shop you open (`shop_stock`), one line with the item count:

```
[tables] shop_stock shop 0: 6 items (orig 5) by mods/shop-extra-item
```

File and image replacements already leave their own line (`[files] override ...`, `[images] override ...`) the first time
they are used; the `mods` level does not repeat them. The numbers in the log come from your disc: do not publish them
with your mod (ADR-001).

### 6.7 New content: items

A mod with `"kind": "content"` in its `mod.json` can declare **new items** in `content/items.json` (format `items-v1`). It
is the mod's own content: the original items are not touched.

```json
{
  "format": "items-v1",
  "new": {
    "test-gadget": {
      "base": "dw3:item/43",
      "price": 40,
      "sellPrice": 20,
      "effect": { "amount": 300 },
      "name": { "eng": "Test Gadget", "spa": "Artilugio de prueba", "es-419": "Cacharro de prueba" },
      "description": { "eng": "A test item. Heals a little HP.", "spa": "Objeto de prueba. Cura un poco de PS." }
    }
  }
}
```

- **Name:** `<mod folder>:<key>` (here `sample-item:test-gadget`). The key uses lowercase letters, digits, `-` and `_`.
- **`base`** (required): an original item (1..402), by its name `"dw3:item/43"` or by its number (`43`). The new item copies its
  type, its menu list and its effect or equipment stats; the fields you declare replace them.
- **Fields:** `price`, `sellPrice` (0..65535) and `effect` `{ "amount", "kind", "flags" }` (only when the base is a
  consumable; the effect is a private copy, the base does not change). An unknown or out-of-range field is rejected with a
  log line (`[items] ...`) and the rest of the item still applies; an item without `name.eng` or with a bad `base` is rejected whole.
- **Text:** `name` (`eng` required) and `description`, per language (`eng`, `fra`, `ita`, `ger`, `spa`, `es-419`). A missing
  language uses English (`es-419` falls back to `spa` first). The description box is **one line** (280 px) and cannot be split: a longer description is shortened at a word with `...` and the
  log says which (`text: ITMINF item 403 (es-419) description is wider than the box`); write it short to see it whole. This applies
  to the descriptions of the new items; the original texts are not touched.
  The port adds the strings to the item name and description files the game sees, without editing your text files.
- **Numbers:** the port hands out numbers from 403 in alphabetical order of the full name; they are not stable across
  different sets of mods, so **nothing stores them and you must not write them**: shops use the name and saves store the name.
- **Names of the original items:** every original item has the name `dw3:item/<id>` (for example `dw3:item/43`), and wherever a
  mod used to give the number of an original item it can now write that name: `base`, the `shop_stock` list, `item` of
  `battle_loot` and the key of an `item_prices` row (`"dw3:item/43": { "price": 13 }`; the number still works). A `dw3:` name outside
  1..402, or of another type (`dw3:tech/43` for an item), is rejected like any unknown name. To see which is which, the port writes
  `<pref>/data/content/items.json` (format `content-ref-v1`) with every original item: its name `dw3:item/<id>`, its English name
  (read from your disc at start-up; the file never leaves your folder) and the fields you can change (`price`, `sellPrice`,
  `effect`) with their original values. It is only a reference: editing it changes nothing in the game (if you edit it the port
  leaves it alone and writes the new version beside it, `items.json.new`).
- **Selling it:** `tables/shop_stock.json` accepts the name in the list: `{ "items": { "add": ["sample-item:test-gadget"] } }`
  (§6.5). A name or number no installed mod defines is rejected. The buy price comes from the item (`price`);
  another mod can change it with an `item_prices` row keyed by the name (`"sample-item:test-gadget": { "price": 77 }`): it goes on
  the item's record, in the usual mod order; the declaration keeps its price as the default. An unknown name rejects that row with a
  log line and the rest of the patch applies. The `"*"` row only touches the original items.
- **Removing the mod:** the save stores the item by name. Without the mod it is dropped on load with `[saves] item "..." is
  not defined by any installed mod; dropped from the loaded save` and the game carries on. Original items are untouched.
- **Loot:** `tables/battle_loot.json` accepts the name in `item` (6.5); the enemy drops it, or it is stolen, like any item.
- **Limit:** the game's item lists have room for about 404 different kinds in the bag at once.

Complete example: `pc/examples/mods/sample-item`.

#### Readable view of a save (read-only)

On every save the port writes `view/slot<N>.json` (format `dw3save-view-v1`) in the saves folder (`saves/default/`): slot, port version,
active mods, name, money, play time, place and progress, party, partners and the bag as `{id, name, count}` (mod items by
their full name). It is for looking at or comparing saves. **It is read-only:** the game never reads it, editing it
changes nothing (its `notes` field says so) and it is rewritten on the next save.

### 6.8 New content: techniques

A mod with `"kind": "content"` can also declare **new techniques** in `content/techs.json` (format `techs-v1`) and make an existing
Digimon learn them. It is the mod's own content: the original techniques are not touched.

```json
{
  "format": "techs-v1",
  "new": {
    "spark-strike": {
      "base": "dw3:tech/9",
      "mp": 3,
      "power": 120,
      "name": { "eng": "Spark Strike", "spa": "Golpe chispa", "es-419": "Golpe de chispa" },
      "description": { "eng": "A quick, crackling strike." },
      "learnedBy": { "3": { "slot": 1, "level": 2 } }
    }
  }
}
```

- **Name and number:** like items: `<mod folder>:<key>` (here `sample-tech:spark-strike`). The port hands out numbers from 444 in
  alphabetical order of the full name; they are not stable across sets of mods, so **do not write them**: saves store the name.
  Limit: a technique number is saved in 13 bits (up to 8191), about 7700 new techniques.
- **`base`** (required): an original technique (1..443), by its name `"dw3:tech/9"` or by its number (`9`). The new one copies its whole record (`TechData`: cost, power,
  element, effect, animation...); the fields you declare replace it.
- **Names of the original techniques:** `dw3:tech/<id>` (like `dw3:item/<id>` in 6.7). Today it is accepted in `base`; the keys of
  `learnedBy` are still the Digimon's number. The reference is `<pref>/data/content/techs.json` (`content-ref-v1`): every original
  technique with its name, its English name (read from your disc) and the values of the fields you can change.
- **Numeric fields:** `mp`, `power` (0..65535), `accuracy`, `elementPower`, `effectChance`, `effectPower` (0..255), `hitCount` (up to
  what some original technique does) and `script` (0..17, the animation: see below). An unknown or out-of-range field is rejected
  with a log line (`[techs] ...`) and the rest of the technique applies; a technique without `name.eng` or with a bad `base` is
  rejected whole.
- **Fields that index game tables:** `icon`, `kind`, `element`, `family`, `effect`, `scriptStage`, `scriptEffect`, `scriptSound`. Only
  a value some original technique already uses is accepted (any other value would read outside its table).
- **Text:** `name` (`eng` required) and `description`, per language, with the same fallback as items (`es-419` -> `spa` -> `eng`). The
  description box is one line (280 px): a longer one is shortened with `...` and the log says which (`text: SKLINF tech 444 (es-419)
  description is wider than the box`). The port adds the strings to `SKLNAM`/`SKLINF` in memory, without editing your text files.
- **Who learns it (`learnedBy`):** `{ "<Digimon id>": { "slot": 1..6, "level": 1..99 } }`. The id is `DigimonData.id` (the game's Digimon
  table, 52 real rows; the first 8 are the partners' base forms). The technique takes skill slot `slot` of that Digimon
  (`DigimonData.skills[slot]`, `skillLevels[slot - 1]`) and replaces whatever was there. The game's own learning code
  (`STFGTREP_addSkill`: on level-up it gives the first empty slot of the entry whose level was reached) does the rest; the mod only
  changes that row. Slot 6 is the Digimon's "signature" technique (`skills[6]`, always in the battle menu); the others are learned
  by level. A save already in progress only learns it in entries whose slot is empty. If two techniques claim the same slot of a
  Digimon, the first by name wins and the other is rejected with its line.
- **Animation compatibility:** a technique's animation is slot `script` (0..17) of the effects archive of the Digimon that casts it
  (`FighterInfo.effects`, 18 bytecode slots; an empty slot ends the script at once and an index >= 18 reads foreign memory). Not
  every technique suits every Digimon. At startup the port reads the fighters file **from your disc** and rejects the `learnedBy`
  of a Digimon that lacks the slots it needs, with a log line (`[techs] ... learnedBy 383 rejected: its fighter has no script slot 9
  ...`); the technique itself stays. On the partner side the game swaps script 5 for 6 or 8 (triple hit, action effects, weapons of
  the opponent's family) and 6 for 8, so for 5 the port also checks 6 and 8, and for 6 it checks 8.

  **How animations work** (research R1; the full anatomy of a Digimon is in [`docs/digimon-anatomy.md`](../digimon-anatomy.md), in Spanish):

  - Every fighter (partner or enemy) has an **effects archive**: a table of **18 slots** of animation code (`s16` words), offset 0
    for an empty slot. A technique's animation is `TechData.script`: the slot number in that archive, of the Digimon that casts it
    (`src/battle/normal_battle/techs.c:156`, `src/battle/battle_mode/battle_script.c:539-577`). Partners and enemies take the same path.
  - `scriptEffect`, `scriptSound` and `scriptStage` **do not belong to the Digimon**: they are indices of global tables that the code
    takes from markers inside the script (`9999` for the effect, `0x62`/`0x63` for the sound, `0x38` for the stage;
    `battle_script.c:275,458,506-512`). So a mod can change them with values some original technique already uses, whoever casts it.
  - On the partner side the game may swap script **5 for 6 or 8** and 6 for 8 (`techs.c:50-103`); the port requires those slots too.
    Script 5 is the **generic** one: every partner and almost every enemy has it.
  - An empty slot ends the script at once (inferred from the code, not seen in game) and an index >= 18 reads foreign memory
    (`getArchiveEntry` does no bounds check): the port rejects those cases before they happen.

  **Compatibility rule.** A fighter can animate a technique if it has the technique's `script` slot (and, for a partner, also 6 and 8
  when the script is 5, and 8 when it is 6). Numbers from the EU disc (computed with `tools/techcompat.py`; they are game data, the
  port reads them at startup and does not store them). There are 18 distinct slot sets among the 248 fighters:

  | Script | Techniques using it | Partners (52 real forms) | Enemies (193) |
  |---|---|---|---|
  | 5 (generic) | 246 | 52 | 189 |
  | 6 | 33 | 52 | 4 |
  | 8 | 2 | 52 | 0 |
  | 9 | 54 | 44 (missing on the 8 lowest forms) | 62 |
  | 10 | 26 | 52 | 16 |
  | 11 | 53 | 52 | 14 |
  | 12 (also loads sound bank 0x46) | 10 | 22 | 0 |
  | 13 | 8 | 44 | 1 |
  | 15 | 7 | 52 | 0 |
  | 16 | 4 | 44 | 2 |

  All 52 real partners have slots 0-8, 10, 11, 14, 15 and 17; the 8 lowest forms lack 9, 13 and 16, and 22 forms have 12. Of the
  enemies, 109 have exactly 0-5 and 189 have 5. The 3 empty rows (ids 468-470) have no archive. Pick a `script` 5 technique as `base`
  if any partner should learn it (367 of the 443 original techniques use scripts all 52 have).

  **Where to look without tools:** `<pref>/data/content/digimon.json` lists, for every fighter, `"scriptSlots": [0, 1, 2, ...]` (the
  slots it has, read from your disc at startup) and `<pref>/data/content/techs.json` the `script` of every original technique. If the
  technique's `script` (plus 6 and 8 for 5, and 8 for 6, on a partner) is in the Digimon's list, it can animate it. With a tool:
  `python3 -I tools/techcompat.py "<disc>.bin"` prints the table above and the slot sets (`--json` for another tool, `--fighters`
  to list every fighter). It reads your disc when it runs and stores no game data.

  **What is not possible yet:** a **new** animation (bytecode of its own per mod). Still missing: how many operands each script
  command takes; medium difficulty, not in the immediate plan. Today a new technique uses one of the 18 slots the Digimon already has.
- **Removing the mod:** next to the original block, the save stores the name of every new technique an entry holds (`EXTT`). Without
  the mod the slot is emptied on load with `[saves] technique "..." is not defined by any installed mod; removed from the loaded
  save` and the game carries on. If the mods change and the technique gets another number, the load renumbers it by name.
- **Readable view:** `view/slot<N>.json` lists the mod techniques the entries hold (`modTechs`: partner, entry, slot, number and name).

Complete example: `pc/examples/mods/sample-tech`.

### 6.9 Changing existing Digimon and adding new Digimon

What a Digimon is in the game (three tables, ids, model, names, caps): [`docs/digimon-anatomy.md`](../digimon-anatomy.md) (in Spanish).

A content mod can change the **Digimon that already exist** (partner forms and enemies) in `content/digimon.json` (format
`digimon-v1`), add **new enemies** and **new partner forms**, and place enemies in **encounters** (below). Enemies do not change the
save format (nothing saved holds an enemy id); new partner forms and techniques of mods are saved by name (6.8 and "New partner forms").

```json
{
  "format": "digimon-v1",
  "change": {
    "dw3:digimon/383": { "partner": { "hp": 180, "stats": { "strength": 60 },
                                      "skills": { "6": { "tech": "mymod:spark", "level": 1 } } } },
    "4":               { "enemy":   { "stats": { "defense": 200 }, "item": "dw3:item/45", "itemChance": 1023 } }
  }
}
```

- **Name:** a fighter is `dw3:digimon/<id>` (or the bare number). The id is the fighter's: the `DigimonData` id for partner forms and
  the battle table id for enemies. The two id spaces never overlap, so the id decides the facet (`partner` or `enemy`); using the
  other one is rejected with its line. The reference with all 248 fighters (55 partner forms, 3 of them marked `"placeholder": true`
  because they are empty rows no Digimon uses and cannot be changed, and 193 enemies), their English name (read from your disc,
  never stored in the repo) and the editable fields with their original values is at `<pref>/data/content/digimon.json`
  (`content-ref-v1`, same manifest and ".new" rule as `items.json` and `techs.json`).
- **`partner`** (a `DigimonData` row): `hp`, `mp`, `hpGrowth`, `mpGrowth`, `expRate`, `expLevel` (0..255), `family` (only a value some
  original form has: it indexes a game table), `stats` `{strength, defense, spirit, wisdom, speed, charisma}` (0..999, what the form
  adds to its stats), `resistances` `{"0".."6"}` (0..999, the seven elements in game order), `statusResists` `{poison, paralysis,
  confusion, sleep, knockOut}` (0..255) `statGrowth` `{strength, defense, spirit, wisdom, speed, charisma}` (0..5: the column of `STFGTREP_statGains` each stat gains from
  on a level up), `resistGrowth` `{"0".."6"}` (1..5: the row of the resistance gain tables, battle and gym; growths sharing a row share
  a table, see `docs/data-tables.md` 3.1), `knownLevels` `{"0".."4"}` (1..99: the level that marks technique `skills[i]` known; no
  table) and `skills` `{"1".."6": {tech, level}}`: the technique (number 1..443, `"dw3:tech/<id>"` or
  `"<mod>:<key>"`; 0 empties the slot) and the level (1..99) it is learned at, in `DigimonData.skills[slot]` and `skillLevels[slot -
  1]`. A value with only `tech` or only `level` leaves the other as it was.
- **`enemy`** (a battle table row): `stats` `{attack, defense, spirit, wisdom, speed}`, `resistances` `{"0".."6"}` and `statusResists`
  (0..32767), `family` (a value some enemy has), `techs` `{"1".."3"}` (technique; slots 2 and 3 accept 0 for none), `item` (id,
  `"dw3:item/<id>"` or `"<mod>:<key>"`; 0 none) and `itemChance` (0..1023, in 1024ths less one, like `battle_loot`).
- **Not editable** (they index game tables or other rows): `id`, `nameId`, `pairPartner`, `pairTech`, `blastForms`, `statGrowth`,
  `resistGrowth`, `knownLevels` and, for enemies, the AI (`actions`, `counter`). The displayed name is not editable either.
- **Validation:** a Digimon that does not exist, a facet that does not match the id, an unknown name or an out-of-range value rejects
  that field or entry with a `[digimon] ... rejected: ...` line; the invalid part never reaches the game and the rest of the entry
  applies. A technique given to a fighter must have the animation slots its effects archive offers (the same rule as `learnedBy` in
  6.8; for enemies only the technique's own `script` counts).
- **When it applies:** partner rows are patched in memory at startup; enemy rows are patched on the battle table buffer at every read
  (the change shows in the next battle). If several mods touch the same field, the one later in the mod order wins. `learnedBy` (6.8)
  also accepts `"dw3:digimon/<id>"` as a key; if `digimon.json` and `learnedBy` give the same slot, `digimon.json` wins.
- **Coexistence with `battle_loot`:** `digimon.json` content is applied first and the `battle_loot` table (6.5) goes on top, so for an
  enemy's `item` and `itemChance` `battle_loot` wins; whatever else `digimon.json` changes stays.
- **Removing the mod:** the game goes back to its values (nothing is stored in the save).

Complete example: `pc/examples/mods/sample-digimon`.

#### New enemies (`"new"`)

`digimon.json` can also declare **new enemies** with a `"new"` map, like items and techniques. The enemy is called
`"<mod folder>:<key>"` and copies the battle table row of an original enemy (`"base"`):

```json
{
  "format": "digimon-v1",
  "new": {
    "sproutmon": {
      "base": "dw3:digimon/4",
      "model": "dw3:digimon/25",
      "name": { "eng": "Sproutmon", "es-419": "Brotemon" },
      "stats": { "attack": 120, "defense": 40 },
      "item": "dw3:item/45", "itemChance": 1023
    }
  }
}
```

- **`base`** (required): an original enemy (number or `"dw3:digimon/<id>"`); an id that does not exist, or an empty row of the partner
  table, is rejected (a partner `base` is a new partner form: see "New partner forms"). Stats, techniques, loot, resistances and the AI
  come from it (the AI cannot be edited).
- **`model`** (optional): another **original enemy** whose model, motions, effects, face and cameras are reused; without it, the
  `base`'s. A partner cannot be an enemy's model (its record has another layout). It is a borrowed model: the battle model format cannot be
  edited yet.
- **`model` as an object (advanced, for someone who already has a model file in the game's format; partner forms accept it too):** the
  Digimon gets its **own** `FighterInfo` row instead of sharing another one's. All keys are optional: `"from"` (the original fighter of
  the same kind used as the template: cameras, height, distance and whatever is not replaced; the `base` by default), `"model"`,
  `"motions"` and `"effects"` (each `{"file": ..., "entry": 0..65535}` or `"dw3:digimon/<id>"` to take that reference from an original
  fighter), `"height"` and `"distance"` (16-bit integers). `"file"` is a file number (1..65535) or a name
  `"<mod folder>:<path inside the mod>"` that reads a raw model file of the mod (what `tools/model_import.py` writes) and serves it from
  memory. The face list is empty (no blinking). If something does not resolve, the Digimon keeps sharing its template's row and the log
  says why. Each table keeps room for 63 own enemy rows and 201 own partner rows. The goal is the text form below.
- **`"model": "<mod folder>:<key>"` (a model with your own art):** a neutral source (glTF + PNG + `model.json`) in `content/models/<key>/` of the same
  or another mod; the port converts it on load and the Digimon gets its own row with that model, the donor's effects, and the template, height and
  distance that the `model.json` says. Full guide: 8.2. `"dw3:model/<id>"` takes the model of an original fighter.
- **Editable fields:** the `enemy` ones above (`stats`, `resistances`, `statusResists`, `family`, `techs`, `item`, `itemChance`),
  same rules. A technique needs its animation (`script`) in the effects archive of the chosen **model**; otherwise that slot keeps the
  `base`'s technique and the log says so (`its model ... has no script slot N`).
- **`name` and `description`:** per language (`eng` required; `es-419` -> `spa` -> `eng`), as for items and techniques. The name
  shows in battle through the name file (`DIGNAM`) that the text layer extends; the description is kept, but no screen shows an
  enemy's description yet.
- **Ids:** deterministic, from **471** (above every original fighter, the highest is 470) in alphabetical order of the full name, **shared with
  new partner forms** (the new enemies and new partners of every mod form one sorted list, so they never collide); the name in
  `DIGNAM` is entry `251 + position`. **Limit:** the id is a signed 16-bit value in the battle table, the fighter list and
  the combatant (`s16`), so up to **32297** new enemies fit (ids 471..32767); the rest, alphabetically, are dropped. The id
  **depends on the installed mods**: always write it by name.
- **No size cap:** the disc's battle table and fighters files have a fixed size, but the port serves **longer** versions from memory
  (rows added; the fighter list is copied to the end and the header points at it; the original bytes are untouched) and the file
  cache allocates the size that is announced. A raw file (`files/`) for either file number wins and the new enemies are not served
  (one log line). `battle_loot` (6.5) only knows the original enemies: a new one's loot goes in its own entry.
- **Saves:** nothing saved holds an enemy id (the save keeps partners, cards, items, flags and progress; the next battle's
  encounter is not saved), so there is no name chunk and removing the mod leaves nothing dangling.
- A new Digimon cannot be changed with `"change"` (its id depends on the mods): put it in `"new"`.
- A new enemy does not appear by itself: place it in an encounter (next section).

#### New partner forms (`"new"` with a partner `base`)

When `"base"` is an **original partner form** (`"dw3:digimon/383"`; not one of the 3 empty rows) the entry is a new partner form: a
new `DigimonData` row (a copy of the `base`'s) that the game can look up by id (`GET_DIGIMON`), name and take to battle:

```json
{
  "format": "digimon-v1",
  "new": {
    "emberkote": {
      "base": "dw3:digimon/383",
      "model": "dw3:digimon/384",
      "name": { "eng": "Embermon", "es-419": "Brasamon" },
      "description": { "eng": "A partner form that burns bright." },
      "hp": 220, "stats": { "strength": 90, "speed": 60 },
      "skills": { "1": { "tech": "mymod:spark", "level": 1 } },
      "evolution": { "partner": 0, "needs": [ { "digimon": "dw3:digimon/383", "level": 1 } ], "stat": "level", "value": 3 }
    }
  }
}
```

- **Editable fields:** the `partner` ones above (`hp`, `mp`, `hpGrowth`, `mpGrowth`, `expRate`, `expLevel`, `family`, `stats`,
  `resistances`, `statusResists`, `statGrowth`, `resistGrowth`, `knownLevels`, `skills`), same rules; `blastForms` (five partner forms by name, per level tier) and `pairPartner` (a partner form by name, or 0) go by name and are
  checked (the name must be an original or a new partner form; otherwise the field is rejected and the base's stay); everything else
  (`pairTech`) is copied from the `base`; `statGrowth`, `resistGrowth` and `knownLevels` are edited as in `partner`. The `base`'s **original** row is copied, not one another mod changed with
  `"change"`.
- **`model`** (optional): another **original partner form** whose model, motions, effects, face and cameras are reused; without it,
  the `base`'s. An enemy cannot be a partner's model (its record is 0x48 bytes with one camera; a partner's is 0xC4 with twelve). The
  fighter entry only copies the model's index and kind, so **no new info record is added** and the PS1 caps (a one-byte index, 3 free
  kinds) do not apply.
- **Techniques:** every `skills.N.tech` that differs from the `base`'s is checked against the effects archive of the final **model**
  (the 6.8 rule, partner side: script 5 also needs slots 6 and 8); a slot that does not fit keeps the `base`'s technique and the log
  says `its model ... has no script slot N`. The technique reaches the battle list when the partner's entry learns it (by level,
  through `skills`/`level`) or through `learnedBy`/`digimon.json` of another form that teaches it.
- **Name:** `name` per language (`eng` required; `es-419` -> `spa` -> `eng`) in entry `251 + position` of `DIGNAM`, like a new
  enemy. `DigimonData.nameId` is **one byte** and `DIGNAM` has 251 entries, so only 5 new names would fit: the port takes them out of
  that limit. The game shows a form's name through `PC_DIGIMON_NAME(row)` (one accessor in the ~20 `src/` places that showed
  `nameId`); for a new row it reads an entry number kept beside the row, with no 8-bit cap, and the row keeps the `base`'s `nameId` as
  a fallback. `DIGINF` (the lab's description) gets the same entry with `description`.
- **Ids:** the same as the new enemies' (above): one alphabetical list of every new Digimon from 471. A new form also takes a row
  of the Digimon table (row 55 on, in id order). `pairPartner` and `blastForms` keep
  row + 1 in 16 bits (the port holds them apart from the disc's `u8`, U9), so a new form can be their value and rows go past 254 (up to
  4096; `blast_forms` and `evolutions` accept any row that exists).
- **How it is obtained (`"evolution"`):** after a battle the game checks each partner's evolution list in the report
  (`STFGTREP_learnDigimon`): the first form of the list that the partner does not have yet and whose requirements it meets is given to
  it ("Digivolution learned: ..."). The list is a fixed table of 44 entries per partner; the port **does not touch it**: it appends the
  entries that mods declare. `"evolution"` is an object (or a list of up to 4 objects, one per partner that can learn it): `"partner"`
  (required: `0..7`, or `"dw3:digimon/<id>"` of that partner's first form), `"needs"` (up to 2 requirements `{"digimon":
  "dw3:digimon/<id>" or "<mod>:<key>", "level": 1..99}`: the partner must have that form at that level; no `"level"` means 1), `"stat"`
  (`1..14` or `strength`, `defense`, `spirit`, `wisdom`, `speed`, `charisma`, `level`; 8..14 are the seven resistances in game order;
  none by default) and `"value"` (`0..32767`: the least that stat of the partner must be; 1 by default with a `"stat"`). They are the
  fields of `Evolution` (`include/battle/report.h`) with the Digimon by name. A requirement may name another new form (a chain of
  evolutions) but not an enemy; a requirement that does not resolve drops that entry with a `[digimon]` line. The entries go in the
  order of the forms' ids and of their declarations; a partner gets **one form per report** (the first that qualifies), as in the
  original game.
- **What a new form does today:** it exists as data (looked up by id), has a name (the battle's DV menu shows it, tested in game; the
  status screen and the lab read the same accessor), fights with its stats, techniques and model when it is the active form and **is
  obtained in normal play** through `"evolution"`. To test without playing that far, the test harness has
  `DW3_SEED_PARTNER="<partner>.<entry>=<mod>:<key>[@<level>][/<slot>],..."`, which puts the form in a partner's entry and picks it as
  the partner's battle form (the entry may be above 43: see the next item).
- **More than 44 forms per partner:** `PartnerStats.entries[44]` lives in the saved `GAME` block. The first 44 entries stay there, as in
  the original; the following ones live in the port's own store, per partner, up to **256 entries in all** (a safety limit, not a
  design one; the lab, the report and the game's lookup use the current capacity). The lab (entry list, form change, recipe table) and
  the battle report read the entries through the same accessors, so a form at entry 45 or later is shown and picked like any other.
- **Saves:** a new form can be in a save (a partner's entries keep its id, which depends on the mods). The save file carries an
  `EXTD` chunk with each new form's `"<mod>:<key>"` name, its id and the id of the original `base`; on load every id is reassigned
  **by name** (also when the mod order changed). If the mod is gone the rule is: the entry that held the form becomes **the original
  form it was based on** (keeping level, experience and techniques); if the partner already had that original form, the entry is
  emptied; the picked forms and the battle form that named it become the original form when the partner has it, else none. With no
  chunk (or a damaged one) the `base` is unknown: the entries are emptied. Each case leaves a `[saves]` line. `view/slotN.json` lists
  the new forms under `modForms` (those of the block, not those of the `EXTP` chunk).
  The entries **beyond the 44th** (of any form, original or new) go in a chunk of their own, `EXTP`, with the form and the mod
  techniques **by name**; it is written only when a partner has any (the file of a save with 44 forms or fewer does not change). If a
  form's mod is gone that entry is dropped (its `base` is not saved); if a technique's mod is gone only that skill slot is emptied;
  each case leaves a `[saves]` line. A picked form or the battle form that loses its mod becomes its original form only when the
  partner has it among the first 44 entries (the block's remap does not look at the `EXTP` ones), else 0.

#### Where they appear (`"encounters"`)

`"encounters"` places enemies (original or new, by name) in the game's **encounters** (the 335 rows of `FIELDSTG_encounters`;
`BATTLE_SETUP.battle` is that index, which also indexes `battle_rewards`):

```json
"encounters": {
  "34": { "2": { "enemy": "mymod:sproutmon", "level": 3, "hp": 60, "mp": 10, "strength": 16 } },
  "35": { "1": { "enemy": "dw3:digimon/4", "level": 5, "hp": 90 } }
}
```

- The key is the encounter index (1..334; 0 is the empty one) and the subkey the slot (`"1".."3"`, the three of a battle). `"enemy"`
  is `0` (leave the slot empty), an original enemy (number or `"dw3:digimon/<id>"`) or a new enemy (`"<mod>:<key>"`). `level`
  (1..99), `hp` (1..32767), `mp` (0..32767) and `strength` (1..32767, in 16ths: 16 = x1) are optional: what is missing is taken from
  the enemy that slot had; an empty slot has nothing to inherit, so all four go there.
- **Design:** an overlay table patch in the style of `shop_stock` (6.5): on entering the field the encounter's slot is pointed at a
  `BattleEnemy` row the port owns. The original table (FIELDSTG overlay data) is never written: the state scope restores it on every
  entry and the patch goes on again; **removing the mod brings the original encounters back**. Several mods on the same slot: the
  last in the order wins, whole.
- **Where each encounter happens:** each stage's `AreaBattle.encounter` (`src/field/stages/`) and the boss scripts (same indices);
  for example the Central Park (stage 330) uses encounters 34 and 35.
- **Loot:** the game draws one enemy among those with an `item` and rolls its chance. The original read the wrong slot for the draw
  (an item-bearing enemy behind a slot with no item never dropped anything); the port fixes it (fix G2), so the slot you put a new
  enemy in does not matter for its loot.
- Values the original game already checks (like a story boss's `hp` or level) are not watched: a story encounter with a different
  enemy can change the fight in ways the script does not expect.

Complete example: `pc/examples/mods/sample-enemy`.

### 6.10 New content: cards

A `"kind": "content"` mod can add **new cards** with `content/cards.json` (format `cards-v1`, the same `"new"` map as items). The card is
named `<mod folder>:<key>`; the port gives it an id from 317 up, in the alphabetical order of the full name, and that number is never saved
(a save stores the name: section `EXTK`). Complete example: `pc/examples/mods/sample-card`.

- `"base"` (required): an original card (1..314), by name (`"dw3:card/120"`) or number; its record (colour, AP, HP, kind, points, level, rank and
  combo card) and, for cards 1..60 (the option cards), its effect are copied. The original names are in `<pref>/data/content/cards.json`
  (`content-ref-v1`, written by the game from your disc).
- Fields: `color` 1..6, `ap` and `hp` 0..99, `kind` 0..16 (1..14 option, 16 Digimon), `points` 0..99, `level` 0..9, `rank` 0..999, `comboCard` 0..314. A value out of
  range is rejected with a log line and the card keeps its base's.
- `"effect"` (option cards): `{ "like": 1..60, "condition", "playCondition", "message", "target": 0..255, "script": [steps 1..174, up to 39] }`. `like` goes first and
  the other fields modify it; an invalid `script` is rejected whole. The game has 175 effect steps; new steps cannot be created.
- `"name"` (with `eng`) and `"description"` (the effect text, in `CARDST`) per language, like items.
- **Art:** `<mod>/card/<key>.png`, an indexed 32x32 PNG with up to 256 colours (the transparent index stays transparent, opaque black is written 0x8000); without a file the card
  uses its base's art.
- **Where it shows:** the album grows (ids 315 and 316 appear as empty slots between the original and the new cards), the deck editor lists them once you own them, and the
  battle draws and plays them with their record and effect. Cards in decks and in the collection are saved by name; removing the mod drops the card when the save loads (line
  `[saves] card "..." is not defined by any installed mod`) and its place in a deck goes back to the starter deck's card.
- **Price and shops (U8c):** `"price"` (0-32767, 1 when missing) is what the card shop charges for one copy. To sell it, a table of the same mod lists it by name:
  `tables/card_shop_stock.json` (`{"49": {"cards": {"add": ["my-mod:my-card"]}}}`), and to draw it from packs `tables/card_pack_slots.json` (`{"910": {"card1": "my-mod:my-card"}}`);
  `tables/card_opponents.json` and `tables/default_deck.json` put it in an opponent's deck or the default deck. The shop draws the card with its art, name and price.
  See `pc/examples/mods/sample-card`.
- **Limits:** up to 707 new cards (ids < 1024). The original ids 315 and 316 are not playable cards: no table accepts them and the album shows them as two empty slots
  (the same as any unseen card), so the album without mods looks identical and with mods the new cards start at slot 317.
- The validator (`--validate-mod`) checks `cards.json` with the same rules and `docs/modding/schema/cards.schema.json` gives the editor completion.

### 6.11 Characters and talks of a stage

`stages/<nnn>.json` (`<nnn>` is the stage number, the one of `data/stages/<nnn>.json`) changes, adds or removes the characters of a field stage and what each one says. The format is
`stage-patch-v1`; the details (fields, order, conflicts, what is skipped) are in `docs/stages.md` section 7 (Spanish) and the schema is `docs/modding/schema/stage-patch.schema.json`. Example: `pc/examples/mods/stage-npc`.

```json
{
  "format": "stage-patch-v1",
  "actors": {
    "patch": [{"index": 0, "x": 850, "y": 950}],
    "add": [{"key": "greeter", "character": 43, "image": 6, "x": 950, "y": 950, "dir": 3,
             "talks": [{"entry": 636, "conditions": [["mod-flag", "mymod:met", 1]]},
                       {"entry": 631, "actions": [["mod-flag", "mymod:met", 1]]}]}],
    "remove": [5]
  }
}
```

- A character is named by its `index` in the stage's list (see it in `data/stages/<nnn>.json`, which the game writes from your disc) or by the key of one a patch added (`"<mod>:<key>"`).
- `patch` changes fields (`character`, `image`, `x`, `y`, `dir`, `conditions`, `talks`, `prependTalks`); `add` creates new characters (reuse a `character` and an `image` the stage already has); `remove` takes them out.
- A talk is `{"entry": N, "conditions": [...], "actions": [...]}`: `entry` is a line of the stage's own text (or `"text": "<mod>:<name>"`, a text of the mod's own, see below), the first talk whose conditions hold is the one said, and the
  last one has no conditions. Conditions and actions use the vocabulary of `docs/stages.md` section 4: `["flag", "1A:1F", 1]`, `["item", "dw3:item/18", 1]`...
- **The mod's own flags:** `["mod-flag", "<mod>:<name>", 1]` (condition: it is set; action: set it), `["mod-var", "<mod>:<name>", ">=", 3]` and `["set-var", "<mod>:<name>", 3]` for counters.
  They are saved with the game by name: removing the mod and putting it back does not lose the progress.
- **Texts of your own:** a talk can say `{"text": "<mod>:<name>", ...}` instead of `entry`. The words go in `text/<language>/stages.json` (format `stage-text-v1`, one folder per language; English is required and a language that lacks the
  name shows the English one, es-419 tries Spanish first): `{"format": "stage-text-v1", "texts": {"welcome": "..."}}`. The port appends the line to the stage's text and fits it to the talk box. Details and schema: `docs/stages.md` section 8 (Spanish), `docs/modding/schema/stage-text.schema.json`.
- **Conditions of your own:** a new row in `tables/special_conditions.json` keyed `"<mod>:<name>"` (see `docs/data-tables.md` section 3.9) is used as `["special", "<mod>:<name>", 1]`; the money, progress-range and party-threshold tables are patchable too.
- **New actions:** `["give-money", 500]`, `["take-money", 500]`, `["set-progress", 12]`, `["warp", 380, 1186, 874, 3]` (stage, x, y, direction) and `["sound", "teleport"]` (`docs/stages.md` section 9). Giving or taking a mod's item was already possible: `["item", "<mod>:<name>", 1]`.
- Several mods can patch the same stage: they apply in load order and the last one wins (the log says which field was overridden). An `index` that does not exist at the current story progress is skipped with a log
  line. With no patch the stage is identical to the original. `--validate-mod` checks the file.
- **Exits, events and battles** (`docs/stages.md` section 10, Spanish): besides `actors`, the file may hold `slots`, `events` and `battles`.
  - `slots` (`patch` by `index` or `key`, `add`, `remove`): an exit has `type`, `arg`, `x`, `y`, `dir` and conditions; `"stage": 380` points the exit at that stage and `"event": "<mod>:<name>"` at an event. A new exit carries `"area": {"x", "y", "w", "h"}`, a rectangle of the map in pixels where it fires (maps are not edited).
  - `events` (`patch` by `id`, `add` with a key): `script` with the game's commands (`["wait", 20]`, `["talk", 0, "<mod>:<text>", 0, 4]`, `["walk", ...]`, `["leave", ...]`, always ending in `["end"]`). A talk or an exit starts it: `["run-event", "<mod>:<name>"]`. A new event has no C code.
  - `battles` (`patch`, `add`): per map area, `rate`, and per battle `encounter` (a row of `field_encounters`), `stage` (the fight stage) and `music`; look at `battles` in `data/stages/<nnn>.json`. The action `["start-battle", encounter, fight_stage, music]` starts one from a talk.

## 7. Examples

24 example mods in `pc/examples/mods/`, at least one per type, all switched off when installed: `pc/examples/install_examples.sh [--only tables|content|media]
[user folder]` copies them, generates what needs your `data/` (nothing from the game is in the repository) and adds them to `[mods] disabled`. Each has a
`README.txt` saying what changes and where to see it; the install guide (Windows, Android, groups) is in `pc/examples/README.md`.

| Mod | Type | What changes | Where to see it |
|---|---|---|---|
| `cheap-shops` | table | buy prices halved (`item_prices`) | item shop 0, BUY list |
| `double-rewards` | table | EXP and money x2 (`battle_rewards`) | results screen of any battle |
| `generous-loot` | table | loot chance x2 (`battle_loot`) | results of a battle (the item shows up) |
| `shop-extra-item` | table | one more item in shop 0 (`shop_stock`) | shop 0, BUY, page 2 |
| `fast-growth` | table (E5b) | double stats per level (`stat_gains`) | level up in a battle, then Status |
| `field-encounters` | tables (E5c) | the two first Central Park enemies swap places and battles come twice as often (`field_enemies`, `battle_rates`) | walk in Central Park (stage 330) |
| `gym-inn-prices` | tables (E5e) | inns at 1 BIT and half the gym intensity cost (`inns`, `gym_intensity_costs`) | talk to the innkeeper; train in the gym |
| `card-rival-deck` | table (U8a) | every rival opens with ten copies of one card (`card_opponents`) | any card duel |
| `strong-weapons` | table (E5a) | weapons with double attack (`weapon_data`) | item shop 0: select a weapon and the Details button (circle) |
| `text-upper-items` | text | item names in capitals (made from your texts) | shop lists, bag and item menu |
| `menu-frames-gold` | image (menu) | shop frames in gold (palette only; generated) | item shop |
| `player-recolor` | field sprites | the protagonist's blue clothes turn red (generated) | walk in any field stage |
| `stage-tiles` | stage tiles (E6b) | the Central Park map in purple (made from your `S330` export) | walk in Central Park |
| `stage-objects` | stage objects (E6a) | the objects of stage 233 in purple (generated) | stage 233: the big screen over the platform |
| `stage-npc` | stage characters (S4-S7) | a Central Park character moves next to the start and a new one appears that greets with a text of its own, gives 100 BIT once and changes the second time (the mod's own flag) | Central Park (stage 330), next to the start point |
| `sfx-beep` | audio: effect (E3c) | sample 50 of the COMMON bank replaced by a synthetic beep | field menu (Start): the cursor sound |
| `song-title-chime` | audio: song (E3d) | the title music replaced by a synthetic looping chime | title screen |
| `video-bars` | video (E2b) | the opening movie replaced by synthetic colour bars | when the game starts, before the title |
| `sample-item` | content | a new item, Test Gadget, sold in shop 0 and dropped by every enemy | shop 0 and after any battle |
| `sample-tech` | content | a new technique learned by base form 3 | battle Tech menu |
| `sample-card` | content | a new card with its own art: shop 49, booster 91 and a rival | card shop 49, album |
| `sample-digimon` | content | changes the starting partner and an enemy and adds a new form | stats in battle, and the lab |
| `sample-enemy` | content | a new enemy in two Central Park encounters | Central Park battles |
| `sample-model` | content | an enemy with its own 3D model (a robot of boxes) | Central Park battles |

`sample-enemy`, `sample-model` and `field-encounters` touch the same encounters: enable them one at a time. There is no raw-file example (E7, 6.4): there is no
safe, visible change that does not mean editing undocumented bytes.

## 8. Tools

| Tool | What for |
|---|---|
| `tools/file_names.py "<disc>.bin"` | file number, sector and disc path (TSV on standard output) |
| `tools/extract_disc.py "<disc>.bin" <out>` | extracts every disc file keeping paths |
| `tools/image_atlas_preview.py <png or folder>` | draws pieces (`atlas-v1`) or frames and animations (`sprite-v1`); needs Pillow |
| `tools/image_mod_palette.py` | makes an image mod changing **only the palette** (`--list`, `--to`, `--sprite`) |
| `tools/techcompat.py "<disc>.bin"` | which fighter can animate which technique (animation slots; `--json`, `--fighters`); reads your disc, stores no game data |
| `tools/model_export.py "<disc>.bin" <fighter> <out>` | exports a battle model to glTF 2.0 with skeleton and animations (`--all`, `--no-animations`, `--source`; the game does this by itself: see 8.1) |
| `tools/model_import.py <source>.json --out <mod>` | turns glTF + PNG + metadata (`model-src-v1`) into a new battle model; to check a source or bake outside the game, since the port bakes it at load (see 8.2) |

### 8.1 Exporting a battle model to glTF

**From the game (the normal way).** The port exports the original models from **your** disc to `data/models/<id>/` (in the user folder), one per fighter
(`383` is Kotemon, the same number as `"dw3:digimon/<id>"`), four files each: `model.gltf` + `model.bin` + `model.png` + `model.json`. That is exactly the
**neutral source of 8.2**: copy the folder unchanged to `mymod/content/models/<key>/` and it is a mod model (`model.json` carries the motion slots, the effects
donor and the template of that same fighter, and the height and distance of its `FighterInfo`). It is only done when you ask (all 248 are about 200 MB and
take about 8 s):

- **Menu:** *Options > System > Prepare modding files* (2b) exports all of them (no confirmation; the row's value shows `Models 12/248`). The game
  keeps running at full frame rate: it runs on a worker thread (built and written there; the game thread only hands it the disc files), a second press while it runs does nothing, and quitting in the middle stops it between two models (files are written atomically).
- **`settings.ini`:** `[data]` / `export_models = all` or `export_models = 383, 12` exports them at start, before the window opens. A model already exported
  with the same exporter version and with its four files in `data/models/<id>/` is **skipped without building it** (the
  first start takes about 8 s for all; later starts, nothing). An edited file counts as present and is not touched. To
  redo one, delete its folder; the *Prepare modding files* button does not rebuild what is up to date. A `manifest.json` from an older
  port (without the exporter version) is redone once.
- **The `.new` rule** (like the rest of `data/`): `data/models/manifest.json` remembers what the port wrote. A file you edited is **kept** and the fresh one goes
  beside it as `<file>.new`; an untouched one is replaced.
- **`data/content/digimon.json`** names each fighter's folder (`"model": "models/<id>/"`), exported yet or not.
- **Blender scale:** the glTF comes out at **1 m = 1000 game units** (Kotemon, 1886 tall, is about 1.9 m) and `model.json` says `"scale": 1000`: the importer undoes it by itself.
  No manual scaling in Blender any more (it used to be `S 0.0025`); for a new model work in metres and set `"fitHeight"` (or `"scale"`) in `model.json` for the height you want in the game.
- It is the same code as `tools/model_export.py --source` (portable C; only the menu's job thread uses SDL threads): the `.gltf`, `.bin` and `.json` are **byte for byte equal** and the PNG has the same pixels
  (checked by `pc/modeltest.c`; the PNG's deflate stream is the port's own). Limit: each motion's loop is in the animation's `extras`, not in `model.json`.
  To avoid copying the exported folder: `"from": "dw3:model/<id>"` in the mod's `model.json` (8.2).

**The Python tool** does the same from the command line (development and checks):

```
python3 -I tools/model_export.py "<disc>.bin" 383 out/ --source    # out/383/model.gltf ... model.json, at Blender scale
python3 -I tools/model_export.py "<disc>.bin" --all out/ --source  # all 248
python3 -I tools/model_export.py "<disc>.bin" 383 out/             # the earlier flat layout: fighter_383.gltf in game units
```

What the tool writes without `--source`:

- **What it writes:** per fighter `fighter_<id>.gltf` + `.bin` + `.png`: a skeleton (one node per bone, rest pose = first frame of the
  idle motion), the mesh with every vertex weighted 1.0 to its bone, normals, UVs into a PNG atlas (one 256x256 cell per texture/CLUT
  combination; colour 0 is transparent, alpha per texel) and one `motion_NN` animation per non-empty motion slot (sampled at 60 frames per second).
  Python standard library only; you do not need Blender to generate it.
- **Opening it in Blender:** *File > Import > glTF 2.0* and pick the `.gltf`; animations arrive as actions. The atlas looks best with
  *Closest* interpolation. The data comes from **your** disc and is written outside the repo: it is game material, do not publish it
  (ADR-001).
- **Limits:** parts the game hides with scale <= 48 (weapons, limbs depending on the motion) get scale 0, nothing else hides them;
  lighting is left to the viewer (the game stores no vertex colours); the game's semi-transparency (`abr`) is per texel (only texels with the STP bit blend): it
  becomes alpha 0.5 on those atlas texels and a `BLEND` material only on polygons that have them; additive mode (the common one) is
  approximated by that same alpha and looks darker than in the game; faces are single-sided like the game's (`doubleSided: false`,
  counter-clockwise order), so a viewer without back-face culling would show the interior; the animated face (blinking) and technique effects are not exported; each motion's loop and end
  are in the animation `extras`. It is read-only; the way back is 8.2. Format details and approximations: `docs/formats.md` 4.6.

### 8.1b Exporting the movies to AVI

The 14 movies of the disc (`AAA/STR/MOVIE*.STR`) come out of **your** disc as one `.avi` each in `data/video/<NAME>.avi` (in the user folder), an open format any editor or player opens.
They are only exported if you ask (the 14 take about 430 MB; with the disc it takes about 2 minutes):

- **Menu:** *Options > System > Prepare modding files* (2b) exports all 14 (no confirmation; the row's value shows `Videos 3/14`). It runs on a worker thread with its own disc handle,
  so the game keeps its frame rate and a movie that is playing is not disturbed; a second press while it runs does nothing. Closing the game halfway stops the thread and drops the
  half-written movie (the `.avi.part` file is deleted).
- **`settings.ini`:** `[data]` / `export_videos = all` or `export_videos = MOVIED01, MOVIEOPN` exports them at start, before the window opens (any case, with or without `.STR`).
  A movie already exported by the same exporter version whose `.avi` is in `data/video/` is **skipped without building it** (the second start takes half a second). An edited file counts as
  present and is not touched. To redo one, delete its `.avi`; the *Prepare modding files* button does not rebuild what is up to date.
- **`.new` rule** (like the rest of `data/`): `data/video/manifest.json` remembers what the port wrote. An `.avi` you edited is **kept** and the new one goes beside it as `<NAME>.avi.new`; an untouched one is replaced.
- **Aspect:** the stored pixels are 320x416, but the game shows them in a 640x416 display (each PS1 pixel is twice as wide as tall). Every AVI declares that with an OpenDML `vprp`
  chunk (frame aspect 20:13), so a player shows it at 640x416 with the pixels untouched. The mod replacement reader ignores that chunk: your own video can be 320x416 with or without
  `vprp` (or another size, which is scaled; see below). Movies exported by older versions are rebuilt once by *Prepare modding files*.
- **Format** (AVI 1.0 with `idx1`; the same for every file): **MJPEG** video (`MJPG`), 320x416 at 15 frames per second, one baseline JPEG per frame (sequential, 4:2:0, the standard Huffman tables of
  T.81 annex K, quality 85, JFIF, no restart markers); **16-bit PCM** audio at 37800 Hz with the channels of the disc's XA (stereo in all 14 of the EU disc). Each frame carries its audio just before it
  (2520 samples = 37800 / 15). The profile is fixed and simple on purpose: any decoder reads it, and the game itself plays it when a movie is replaced from a mod (below).
  4:2:0 because the disc's MDEC already stores the colour at half resolution (the colour loses nothing new).
- **Sync:** the first audio sample is at second 0 and frame `k` at `k / 15` s, the usual way an AVI is read. (The game's own player holds frame `N` until `N * 2520` samples have played, one frame
  later; the files do not copy that delay.) The audio and video lengths differ as much as the disc's do: in `MOVIEE02..04` the audio ends about 3 s before the picture.
- **Size:** 8 to 58 MB per movie (about 430 MB in all); AVI 1.0 goes up to 4 GB and the exporter refuses to pass 2 GB (the longest is nowhere near).
- It is portable C (`pc/shim/jpeg_write.c`, `avi_write.c`, `video_export.c`) that reuses the port's MDEC and XA decoders (`host_mdec.c`, `host_xa.c`) without touching their playback state.
  `pc/videotest.c` checks it with a synthetic disc (PSNR against PIL, RIFF and `idx1` structure with an independent Python parser).

#### Replacing a movie from a mod

`mods/<mod>/video/<NAME>.avi` (`<NAME>` = `MOVIED01` ... `MOVIEOPN`, the disc's movie; `MOVIED01.avi`, `MOVIED01.AVI` and `movied01.avi` are looked for) plays **in place of**
`AAA/STR/<NAME>.STR` and its XA audio. The last active mod that has the file wins. Without such a file the disc's movie looks and sounds exactly as before. The game is unchanged: it still believes it reads
its `.STR`; the port hands it the AVI's frames instead. The natural starting point is a file exported by 8.1b: edit it and put it in the mod.

- **What it accepts:** an AVI with **MJPEG** video and, if it has any, **16-bit PCM** audio (mono or stereo). The exporter's exact profile (baseline MJPEG 4:2:0, stereo PCM at 37800 Hz, 320x416, 15 frames/s) and
  what ffmpeg writes: baseline JPEG `yuvj420p`, `yuvj422p`, `yuvj444p` or grey, with or without Huffman tables (without them the standard annex K ones are used, as in an AVI1), with restart markers, with or without JFIF.
  The AVI is read by walking its chunks, so a missing or wrong `idx1`, `JUNK`, `rec ` lists and a file cut short (the frames before the cut are used) all work.
  It does not accept progressive or arithmetic JPEG, or 8-bit, float or compressed audio, nor a file over 2 GB; an OpenDML AVI (ffmpeg writes one past 1 GB) is read only up to its first RIFF block.
- **The ffmpeg command line** (any video to that profile):
  `ffmpeg -i in.mp4 -vf scale=320:416 -r 15 -c:v mjpeg -q:v 3 -c:a pcm_s16le -ar 37800 -ac 2 out.avi`
- **Another size:** it is **scaled** to 320x416 (bilinear, stretched: the game shows that area at 4:3, so a 4:3 source looks right and a 16:9 one looks squashed; scale it yourself with `-vf scale=320:416`). Better
  done with ffmpeg: it is sharper and you control the crop.
- **Another rate:** 1 to 60 frames per second; the AVI sets its own pace (frame `k` is shown when `(k - 1) / fps` seconds of audio have played). Other values are refused.
- **Other audio:** another sample rate is **resampled** to 37800 Hz (linear; better give it at 37800), mono is doubled to stereo, and with no audio the movie is silent (at the same pace).
- **Another length:** the movie **ends when the AVI ends**, shorter or longer than the original (the disc's movie ends at a fixed frame; with an AVI that does not count). After the last frame the game waits for the audio to
  finish (up to 0.45 s more) and goes on to whatever came after the movie. Start skips it like any other.
- **If something is wrong** the game does not stop: the reason goes to the log as `[video] <mod>: <NAME>.avi rejected: <reason>` (not an AVI, a codec that is not MJPEG, audio that is not 16-bit PCM, a rate outside 1..60,
  an unreadable first frame...) and the previous mod, and finally the disc's movie, is tried. If the **first** frame does not decode the file is refused; a damaged frame later repeats the one before (one log line).
  With the log at `mods` (`DW3_LOG=mods`) a replaced movie leaves `[video] <mod>: <NAME>.avi replaces the movie (...)`.
- Code: `pc/shim/jpeg_read.c` (baseline JPEG decoder), `avi_read.c` (AVI reader), `video_mod.c` (the replacement); `pc/videotest.c` tests them with PIL's JPEGs, hand-built ffmpeg-style AVIs, refusals and
  fuzzing (`make -C pc videotest-asan` repeats it under ASan and UBSan).

### 8.1c Exporting the audio (samples as WAV, music as MIDI)

The 71 sound banks of `AAA/DAT/SOUND/` come out of **your** disc in `data/audio/<BANK>/` (in the user folder), in open formats any audio editor or DAW opens. Each bank has its
samples (`vag_NNN.wav`), its description (`bank.json`), its sequences (`seq_<package>_<record>.mid`) and their list (`sequences.json`). On the EU disc that is 946 WAV and 1152 MIDI files in 71 folders,
about 19 MB, and the whole export takes under a second. It is only exported if you ask:

- **Menu:** *Options > System > Prepare modding files* (2b) exports all 71 banks (no confirmation; the row's value shows `Audio 3/71`). It runs on a worker thread with its own disc handle,
  so the game keeps its frame rate and the sound that is playing is not disturbed (it does not touch the loaded banks, the SPU or the sequencer); a second press while it runs does nothing.
- **`settings.ini`:** `[data]` / `export_audio = all` or `export_audio = BGM001, COMMON` (bank names, any case) exports them at start, before the window opens. A bank already exported by the
  same exporter version with its `bank.json` present is **skipped without building it**; an edited file counts as present and is not touched. To redo a bank, delete its `bank.json`; the
  *Prepare modding files* button does not rebuild what is up to date.
- **`.new` rule** (like the rest of `data/`): `data/audio/manifest.json` remembers what the port wrote, file by file. A file you edited is **kept** and the new one goes beside it as
  `<file>.new`; an untouched one is replaced.
- **The samples (`vag_NNN.wav`)**: 16-bit PCM WAV, mono, **44100 Hz**. 44100 Hz is the rate the SPU plays a sample at when its pitch value is 0x1000, which is the case when the sample's root note is played; the other
  pitches are that sample faster or slower. `NNN` is the bank's VAG number (1..). A sample of size 0 has no file. Every WAV carries a `smpl` chunk (the one samplers, DAWs and wave editors read):
  `MIDIUnityNote` and `MIDIPitchFraction` are the root note and its fine tuning and, if the sample loops, one forward loop with its first and last frame. The same data is in `bank.json`.
  - **Root note:** that of the first tone (in program order) that uses the sample: `center - shift / 128` semitones (the tone's `shift` is signed, 1/128 of a semitone a unit; the game plays note N at
    `4096 * 2^((N - center + shift / 128) / 12)` SPU pitch units). A sample no tone uses has note 60.
  - **Loop:** it comes from the flags of the ADPCM blocks: a block with the end and repeat flags = loop (`loop_end` is the last frame of that block; `loop_start` the first frame of the block with the loop-start flag, or 0 when there is none);
    end without repeat = the voice is muted there (`"end": "stop"`, no loop). On the EU disc: 394 looping samples and 552 that stop; none without an end flag.
  - **Decoding:** the same arithmetic as the port's SPU renderer (`decodeSound` of PsyX_SPUAL: a float filter), so the WAV is what the game plays. The console's integer filter can differ by a few units. An independent Python
    program decodes the ADPCM in double precision and agrees within 1 unit (133 samples of three banks of the EU disc) and exactly on the test's synthetic fixture.
- **`bank.json`** (`"format": "bank-v1"`): `bank`, `bank_id` (the bank number of the sound ids, bits 18-24), `sample_rate`, the bank's `master_volume` and `master_pan`, `samples` (per VAG: `file`, `frames`,
  `end` = `loop`/`stop`/`none`, `loop_start`, `loop_end`) and `programs`. Each program has its `volume` and `pan` and its `tones`: `vag`, `min`/`max` (the note range), `center` and `shift` (as stored), `root_note` and `root_cents`
  (the root note and how many cents above it), `volume` and `pan` (0..127, 64 is the centre), `priority`, `pitch_bend_down`/`pitch_bend_up` (semitones), `adsr1`/`adsr2` (the SPU registers, in hex) and `adsr` (the same
  decoded: mode, shift and step of the attack, decay, sustain and release, and the sustain level 0..32767). A `notes` block explains every field inside the file.
- **The sequences (`seq_P_RR.mid`)**: a Standard MIDI File, **format 1**, one per record of each SEP package of the bank (`P` = the package, in the order the game opens them; `RR` = the record inside the
  package, which is the sound id's "track"). Track 0: name, time signature, tempo (the record header's and the sequence's `FF 51` changes) and the loop markers; then one track per channel in use, with program changes,
  controllers (CC7 volume, CC10 pan, CC11 expression and any others it has), pitch bends (14-bit: the game keeps only the high byte, so the low one is 0 and the centre is 0x2000) and notes (a note end is written as `0x8n` with velocity 64).
  The resolution (ticks per quarter) is the record's (480 in all 1152 records of the EU disc). Program and note numbers are the bank's: to hear a sequence with the original instruments, load the `.mid` in a DAW or
  sampler together with the WAVs of `bank.json` (each tone says which WAV, which note range, which root note and which ADSR envelope). A SoundFont per bank is left for later.
  - **Loops:** the game marks a loop with `CC99 = 20` (opens), `CC6` (the repeat count, 127 or 0 = endless) and `CC99 = 30` (closes). In the MIDI they are the marker events `loopStart count=N` (or `count=forever`) and `loopEnd` on
  track 0, plus a `CC111` at the loop start on the first channel in use (the usual editor convention). At the end of the record the game's player jumps back to the loop start, `N` times or without end. On the EU disc 77
  of 1152 records loop.
  - **Filler records:** 1048 of the 1152 records have 10 events or fewer (most are 4 events and 2 ticks): they are empty filler tracks; they come out anyway so the numbering matches the game's (the game numbers the records by their position in the package).
- **`sequences.json`** (`"format": "sequences-v1"`): per file, `package`, `record`, `flat` (`package * 16 + record`), `id` and `id_direct` (the packed sound ids, below), `resolution`, `tempo_us`, `bpm`,
  `time_signature`, `events`, `length_ticks`, `length_seconds` (with the tempo changes), `channels` and `loop` (`start_tick`, `end_tick`, `count`, or `null`).
  - **Numbering (the game's, checked):** `playSound` opens a bank's packages in order and plays record `track` of package `SEQ` (bits 8-15 of the id; bits 0-7 the track). If the track is not a record of that package it looks it up
    flat: 16 tracks a package (`snd_sep.c`, `sndSepTrackFor`). The game's music uses `MUSIC(bank, n)` = `0x60000000 | bank << 18 | n` with `SEQ` 0, and `n` flat. `id` is that value
    (`n` = the record in package 0, `package * 16 + record` in the others; `null` if that number is answered by a record of package 0); `id_direct` is the same with `SEQ` = package and track = record, which always works. Checked
    with a known id from the disc: `W_JINGLE` = `0x6004001E` (`sound.h`) comes out as record 14 of package 1 of `COMMON` (`seq_1_14.mid`, `flat` 30).
  - **Names:** the file has no names. The debug sound test's (`SOUNDTST`, an overlay) are not part of the port; a bank's name is its disc folder's.
- **Code:** portable C (`pc/shim/audio_export.c`, `wav_write.c`, `midi_write.c`), with the port's own VAB header and SEP package readers (`snd_spu.c`, `snd_sep.c`): what is exported is what the game plays. It is checked by
  `pc/audiotest.c` with a hand-built synthetic bank (header, samples and SEP records) and `pc/test_fixtures/audio/check_audio.py`, which decodes the ADPCM on its own and reads the WAV and MIDI files with readers of its own
  (the MIDI against the events the SEP parser decoded, the JSON against the tone definitions). Replacing a sound effect or a music track from a mod: 8.1d.

### 8.1d Replacing a sound effect or a song from a mod

The files are the ones 8.1c exports, with the same names, in `mods/<mod>/audio/<BANK>/` (`<BANK>` is the bank's folder, `COMMON`, `BGM001`...). The last active mod that has the file wins.
Without those files the game's sound plays exactly as before. The game does not change. A file the port cannot use is refused with one log line, `[audio] <mod>: <file> rejected: <why>` (once per run),
and the mod before it, then the disc's sound, is tried; the game never stops for a bad file.

**An effect (`vag_NNN.wav`).** It replaces **sample** number `NNN` of the bank (the one in the exported `bank.json`/`vag_NNN.wav`), so every tone that uses it changes, whether an effect plays it
(`SsUtKeyOn`) or a music sequence does. It is a **16-bit** PCM WAV, mono or stereo (the channels are averaged), at **any rate** from 1000 to 384000 Hz (uncompressed: no 8-bit, 24-bit, float or Microsoft
ADPCM). The port resamples it to 44100 Hz, encodes it to the SPU's ADPCM (five filters and 13 scales per block, chosen by least error; the port's own decoder checked it) and decodes it back to PCM for playing
when the bank loads: the tone keeps its pitch, volume, pan and ADSR envelope (the ones in `bank.json`), because only the audio changes.

- **Rate and root note:** the exported sample is at 44100 Hz and its root note is the one in the `smpl` chunk (`center - shift / 128` semitones). If you replace the sound with another **of the original's
  pitch and length**, give it any rate: it is resampled to 44100 and sounds at its usual pitch when the game plays the root note. The root note of your `smpl` chunk is **not used**: the tone sets the pitch. To
  make it higher, raise it in your editor. A 22050 Hz file that lasts 1 s plays for 1 s, not 0.5 s.
- **Loop:** a `smpl` chunk with a loop (start and end, the end being the last frame, as in the exports) makes the sample repeat; the sound after the loop end is dropped. Without a loop the sample plays once
  and the voice is muted when it ends (the tone's ADSR envelope still rules before that). The SPU repeats **whole 28-frame blocks**: the loop start is rounded down to a multiple of 28 and the last block is
  filled by repeating the loop itself. A loop with no audible seam is one whose start and length (at 44100 Hz) are multiples of 28; the exported WAVs already are.
- **Longer than the original:** fine, with no fixed cap: only memory limits it. Replaced samples are not written into PsyCross's SPU RAM (a fixed 2 MB array with a 512K-sample decode buffer, in a library the port does not modify): they are decoded once to PCM when the bank loads and play as separate PCM voices, with the same pitch, volume, pan and ADSR envelope the SPU voice would have. What the game lays out in the SPU does not move, and without mods the sound is identical.
- **The log:** with `[debug] log = mods` (6.6) each replacement writes `[audio] <mod>: <BANK>/vag_NNN.wav replaces the sample (...)` when the bank loads; the `COMMON` bank loads at start. With `SND_DEBUG=1` the key-on of a replaced sample shows a virtual address (`0x4...`, marked `(pcm)`), not an SPU one.

**A song (`seq_<package>_<record>.wav`).** With the name of the exported `seq_<package>_<record>.mid` (rename your file to match; the record may also be written without the zero, `seq_0_2.wav`), it plays **in place of
that sequence** when the game starts it. `sequences.json` says which `MUSIC(bank, n)` each file is. On the EU disc, for example, the title screen's music is `TTLBGM/seq_0_00` (`MUSIC(0x47, 0)`); `BGM031/seq_0_02` is the language select's. A 16-bit PCM WAV, mono or stereo, at any
rate (OpenAL resamples it at the output).

- **Loop**, in this order: (1) the WAV's `smpl` loop (frames; the end is the last frame of the loop); (2) a `seq_<package>_<record>.json` beside it with `{"loopStart": frames, "loopEnd": frames}` (same units; `loopEnd` is
  the last frame; a broken JSON, or a loop outside the WAV, is refused with its reason and ignored); (3) if the original sequence **loops without end** (in `sequences.json`, `loop` with `count` `forever`), the whole file
  loops; (4) otherwise it plays once and the track ends. There is no gap between the last sample of the loop and the first: the join is exact.
- **What the game still controls:** stopping the track (`SsSepStop`), its volume (`SsSepSetVol`), the fade-out (`SsSepSetDecrescendo`) and the master volume act on the WAV (with the quadratic curve the game uses for
  voices); so do the **music** slider of the Audio menu (or the **effects** one, for a sequence that is not the game's music, like the gondola) and the player's master volume. The game never pauses sequences, so there is
  no pause to honour. A track without a loop that reaches its end stops sounding and the game sees it finished, like a sequence that ends.
- **Memory or disk:** a file of up to 8 MB of samples (about 47 s of 44100 Hz stereo) is loaded whole; a bigger one is read from the disk in chunks of 4096 frames. Both sound the same.
- **A bad file** is refused with `[audio] <mod>: <file> rejected: <why>` and the game's sequence plays (or an earlier mod's file). The same when the machine has no audio output (`DW3_SND_MUTE`).
- **The log:** with `[debug] log = mods`, `[audio] <mod>: <BANK>/seq_P_RR.wav replaces the sequence (... in memory | streamed, loops from the smpl chunk | ... the json | ... the whole file | plays once)` every time the
  game starts it; with `trace`, also when it stops or ends.
- **Code and tests:** `pc/shim/wav_read.c`, `vag_encode.c`, `audio_mod.c` (tracks, chunked OpenAL output, memory or disk reader, JSON), `snd_spu.c` (samples) and `snd_sep.c` (the track stands in for the sequence without changing
  its state); `pc/audiotest.c` tests them with a fake clock: WAV reader, ADPCM encoder (round trip), bank patch with a longer sample, and the track's state machine (start, stop, `smpl`/JSON/whole-file loop, volume, fade,
  sliders, memory against disk, bad file).

### 8.2 Making a new model (glTF to battle model)

For a Digimon with your own art: a glTF 2.0 model (Blender or any other program), its PNG texture and a metadata JSON (`model.json`). The port
**bakes it when the game loads** into the game's model format: the mod carries only that neutral source, nothing from the game. The technique effects
come from a **donor** fighter that the game resolves at run time.

**Where it goes and how it is used.** The source is a folder of the content mod (`"kind": "content"`):

```
mymod/content/models/<key>/model.json      # model-src-v1 metadata (below)
mymod/content/models/<key>/model.gltf      # or .glb; with its .bin and its PNG next to it
mymod/content/digimon.json                 # "model": "<mod folder>:<key>"
```

The key (a-z, 0-9, `-`, `_`) is named `"<mod folder>:<key>"`, and a new Digimon uses it as `"model": "<mod folder>:<key>"` (6.9): it gets its own
`FighterInfo` row with the baked model and motions, the donor's effects, and the `template` (cameras, height) and `info` (`distance`, `height`) of
`model.json`. It also works inside the `"model"` object as `{"file": "<mod>:<key>", "entry": 1}` (model; entry 0 is the motions). Original fighters are named
`"dw3:digimon/<id>"` and, for the model only, `"dw3:model/<id>"` (it shares that fighter's row). A complete example: `pc/examples/mods/sample-model/`.
If the source cannot be baked (a missing file, a broken PNG, an animation that does not exist...), the log says `[models] <mod>:<key> rejected: <why>`
and the Digimon keeps its template's model: a bad model never takes the game down.

**Cost.** Baking a full Kotemon (24 bones, 926 triangles, 32 animations) takes about 20 ms and about 20 MB while it runs (freed at the end); the sample
robot, under 1 ms. The result is kept in `cache/models/` of the user folder under a key made of the source bytes and the baker's version: the next
start with the same source reads it from there (about 5 ms, mostly reading and summing the source); changing the source or updating the port bakes
again by itself, and a damaged cache file is ignored. Deleting `cache/models/` loses nothing.

**The command-line tool** still serves to check a source without opening the game (`--check`: report and warnings) or to bake a file outside the game.
The port's bake is the same algorithm in C and gives **the same bytes** as the tool (`pc/modeltest.c` checks it):

```
python3 -I tools/model_import.py mymodel/model.json --out mymod/ [--mod <mod name>] [--check]
python3 -I tools/test_model_import.py --demo out/     # a sample robot made of boxes (robot.gltf/.bin/.png + robot.json)
```

With `--out` it writes `mymod/models/<name>.bin` and prints a `content/digimon.json` snippet using the object form of `"model"` (the raw file, which the
port reads as `"<mod>:models/<name>.bin"`); it is the transition and debugging path: with the source in `content/models/` it is not needed.

**Exporting from Blender** (*File > Export > glTF 2.0*): *glTF Separate* (`.gltf` + `.bin` + textures) or `.glb`; *+Y Up*; tick *Normals*, *UVs*,
*Materials* and *Images: PNG* (not JPEG); *Skinning* on; *Animation* on with the actions and *Always Sample*. The model faces glTF **+Z** (Blender's
default front); if yours faces the other way use `"yaw": 180`. Apply object scale and rotation (Ctrl+A) and keep a single armature; a node above the bones with a
rotation and uniform scale (Blender's "Armature") is absorbed, but a **non-uniform** scale is an error.

**`model-src-v1` metadata** (`gltf` is relative to this file):

```json
{
  "format": "model-src-v1",
  "name": "boxbot",
  "gltf": "robot.gltf",
  "fitHeight": 1800,
  "yaw": 0,
  "motions": {
    "0": "idle", "1": "idle",
    "2": { "animation": "hit", "end": "idle" },
    "3": 2,
    "4": { "animation": "walk", "loop": true, "speed": 1.0 }
  },
  "fill": "idle",
  "effects": "dw3:digimon/383",
  "template": "dw3:digimon/4",
  "info": { "distance": -1500 },
  "displayName": "Boxbot"
}
```

| Key | What for |
|---|---|
| `name` | file name (`models/<name>.bin`) and the Digimon's key in the snippet; letters, digits, `-` and `_` |
| `scale` or `fitHeight` | game units per glTF unit, or the height (game units) the rest pose must have; not both. Original fighters are 1300 to 10 000 tall; Kotemon, 1886. With neither the scale is 1 and it warns if the model is under 50 or over 20 000 |
| `yaw` | degrees of turn about the vertical axis (0 by default) |
| `motions` | slot (0 to 61) to animation: a name, an object or a number (copies that other slot). Without `motions`, animations named `motion_NN` go to slot NN (what the exporter writes, 8.1) |
| `fill` | animation (or an object as in `motions`) for **every** unassigned slot; without it an empty slot is a risk if an effect script asks for it |
| `effects` | donor: `"dw3:digimon/<id>"` whose effects file the Digimon uses (the scripts only talk to the model by motion slot number) |
| `template` | original fighter of the **same kind** (enemy or partner) that gives cameras, height and distance: put it in `"base"` and `"from"`; pick one of a similar size, cameras do not adjust themselves |
| `info` | `"distance"` and `"height"` of the own row (6.9) if the model is much bigger or smaller than the template |
| `normals` | `"file"` (default, the glTF's; smooth if missing), `"smooth"` or `"flat"` |
| `doubleSided` | `"duplicate"` adds the back faces of double-sided materials (the game draws one side; Blender marks them so by default) |
| `from` | `"dw3:model/<id>"`: starts from the original fighter (see "Starting from an original"); whatever is missing comes from it |
| `tolerance` | `{"position": units, "rotation": raw, "scale": raw}`: largest error when reducing keys (default 0.1 % of the height, 4 = 0.35 degrees, 16 = 0.4 %) |

**Starting from an original: `"from"`.** A `model.json` may say `"from": "dw3:model/<fighter id>"` (the same number as in the exported `data/models/<id>`) and ship **only what it
changes**: whatever is missing comes from the original fighter, which the port exports **in memory** at load (the same four files as `data/models/<id>/`, read from
your disc). The typical case is a retexture: the folder holds only `model.json` (`{"from": "dw3:model/383"}`) and your `model.png`. Rules:

- **Files:** a `model.gltf`, `model.bin` or `model.png` the folder lacks comes from the original; the ones it has **win**. The exported glTF names `model.png`
  and `model.bin`, so a `model.png` of your own must keep the original's **UV layout** (size and position of every island): you paint over the exported one,
  you do not move things around. If you ship your own `model.gltf`, ship its `.bin` and PNG too (or the exported names, which are filled in).
- **`model.json`:** the original's is the base and the top-level keys of yours replace it (shallow merge: `"info"`, `"template"`, `"effects"`, `"yaw"`... are
  replaced whole). Exception: `"motions"` is merged **per slot**: the slots you give win and the others stay the original's. If you bring a glTF with other
  animation names, redefine in `"motions"` every slot that used the original's names (a slot cannot be removed, only changed). `"format"`, `"name"` and `"gltf"` are inherited too.
- **Rejections** (`[models] <mod>:<key> rejected: <why>`): a `"from"` that is not `"dw3:model/<n>"`, a fighter that is not on your disc, or no disc
  available (in the tests, for instance). Here `"from"` belongs to `model.json`; do not mix it up with `digimon.json`'s own `"from"` (the template fighter).
- **Cache:** the key mixes the folder's files, the original's id and the exporter version (`MODEL_EXPORT_VERSION`), not the original's bytes: with a warm cache the original is not
  built (only the template and effects info is read from it when the `model.json` does not carry all of it). A port with another exporter bakes again.
- `tools/model_import.py` does **not** read the disc: it rejects a `model.json` with `"from"` and tells you to export the folder first (*Prepare modding files* or `export_models`) or let the game bake it.

**Motions.** Each slot is a glTF animation sampled at 60 frames per second (the game's rate) and reduced to the keys it needs. Slots **0 and 1**
(idles, required) loop; the others by default blend from the current pose for 10 frames and blend back to idle at the end. An object accepts
`animation`, `loop`, `loopStart` (clip frame the loop returns to), `end` (`"idle"` or `"hold"`, stay on the last pose), `blendIn` and `blendOut`
(blend frames; 0 in `blendOut` means `hold`) and `speed` (2 = twice as fast). Two slots with the same animation and speed share their frames. The safe minimum is
**0, 1, 2, 3, 4, 10, 11, 12, 14** (1 is the second idle, 12 the entrance, 3 the hit reaction); the donor's techniques ask for more
(`tools/techcompat.py`, 6.8); the tool warns about the missing ones.

**What it does with geometry and texture.** Rigid skin: each triangle goes to the bone that weighs most on it (it reports how many cross bones); a bone with more than 256
vertices or 256 normals is split into child bones; bones without polygons and without children that have polygons are dropped (a bone without polygons but with children keeps
an empty mesh). The texture is rebuilt into one 256x240 page of 4 bits: UV islands are repacked (and shrunk if they do not fit) and each gets a **16 colour** palette
(0 is reserved for transparency when there is cut-out; up to 64 palettes, similar ones are merged); the report gives the quantization error. Triangles only, lit by normal.

**Limits and warnings** (details in `docs/formats.md` 4.6): 256 vertices and 256 normals per bone mesh; about 40 bones is comfortable (the game has 100 bone callbacks for
every fighter on screen; error from 90); about 3000 polygons as a warning (the biggest original has 1334; the real cap is the port's primitive buffer); all motions hang off one shared
timeline of fewer than 30 000 frames and each has at most 0x640 steps. Not supported yet: vertex colours, UVs outside 0..1 (clamped), `KHR_texture_transform`, partial
transparency (alpha is all or nothing), soft skinning, blinking (the face stays empty) and more than one model file per Digimon.

**Using it.** Put the snippet in the mod's `content/digimon.json` (rename it and pick `"base"` and `"from"` of the same kind); the Digimon shows its model in battle. New enemies and
partner forms are explained in 6.9. Today the baked file has to be inside the mod; the goal (ADR-011) is that the mod ships only the glTF and the port converts it on load.

## 9. Legal rules and etiquette

- **Never redistribute game data**: no original text, images, disc files or binaries (ADR-001). A mod publishes
  **only its own changes**.
- For a change that starts from a game file (a palette, a raw file) publish the **procedure**: a script like
  `image_mod_palette.py` or a binary patch that the player applies to **their** copy. What the player generates stays
  on their machine.
- Your own new text (translations, names) can be shared.
- Two mods changing the same thing do not clash: the last in the order wins, with a log line.

## 10. Roadmap (ADR-006) and what is missing

| Phase | Content | Status |
|---|---|---|
| 1. Resources | replace text, config, images, sprites and raw files; manifest with dependencies; this guide | **done** (review pending) |
| 2. Data tables | the game's tables (Digimon, techniques, items, rewards, experience, shops, encounters) as JSON; patch by key and add rows with ids `<mod>:<name>` | **done** (`stageBattles` is part of the stage patches, S10, 6.11). Includes the card shop, gym, inns, card opponents and new cards (E5d, E5e, U8a-c), new items and techniques, changes to existing Digimon, new enemies, and new partner forms |
| 3. Scripts | an embedded language (candidate Lua 5.4, sandboxed: no files, network or system) for events, quests, characters | planned (its own ADR) |
| 4. New entities | new stage characters with talks and flags of their own already work (S4 and S5, 6.11, reusing the stage's sprites); their own sprites are S9; new Digimon already work (battle models in glTF, 8.2) | **in progress** |
| Asset formats | video (AVI), audio (WAV and MIDI export and replacement), images (PNG), battle models (glTF) and stage art (PNG) | **done** (§8) |

Reserved for phases 2 to 4: `"kind": "content"` and the `<mod>:<name>` identifiers. Saves are no longer kept on a PS1
card but in `<pref>/saves/default/slot<N>.dw3save` (ADR-008, option B: the game's original block as is, plus the
extended state by name and a CRC32; a `.mcd` card placed in `import/` is imported once and never modified). `mod.json` keys and formats are versioned; an incompatible change creates a new version and the old
one is still read for at least one more port version.
