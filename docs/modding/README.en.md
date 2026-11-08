# Modding guide for the Digimon World 3 port

> Versión en español: [README.md](README.md) (same content).

This guide is the reference for what can be modded **today** and how. What is not written here is not API
(ADR-006 section 7). Mods are folders: install one by copying it, remove it by deleting it. The port never writes
inside `mods/`, and a broken mod never stops the game (it goes to the log and the next source is used).

It describes only the current asset format (contract `asset-v1`, ADR-016): one folder per topic, and the same shape in `data/` and in
`mods/<mod>/`. If you have a mod or a `data/` folder from an earlier version, go to [section 11](#11-migrating-from-the-previous-format).

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
   `--validate-mod` are described in sections 2c and 2d; `--export-all` writes everything the disc holds into `<pref>/data/` for you to
   look at and edit.
4. **First mod in 5 minutes.** From a checkout of this repository, start the game once with your disc (so `<pref>/data/` exists) and follow
   section 2c: a one-line patch (`tables/item_prices.json`, half prices) checked with `--validate-mod`. When you want to see a big mod,
   section 7 has two complete examples (`training-field`, new stages; `sample-model`, a Digimon with a model of its own) and a template
   folder (`_template`); each has a `README.txt` saying what it changes and where to see it.
5. **Tools** (section 8): `tools/file_names.py`, `tools/extract_disc.py`, `tools/image_atlas_preview.py`, `tools/image_mod_palette.py`,
   `tools/techcompat.py`, `tools/balance_compare.py`, `tools/model_export.py`, `tools/model_import.py`. Run them with `python3 tools/<name>.py ...`; all read
   your own disc and store no game data.
6. **Formats and data (reference):** [`docs/formats.md`](../formats.md) (file formats), [`docs/data-tables.md`](../data-tables.md),
   [`docs/digimon-anatomy.md`](../digimon-anatomy.md), [`docs/extended-state.md`](../extended-state.md) (saves and extended
   state), [`docs/stages.md`](../stages.md) (stage data; Spanish) and the schemas in [`schema/`](schema/).
7. **Sharing your mod:** only your own work, never game data (section 9). Describe what it changes and which port version it needs
   (the `port` key of `mod.json`).
8. **Validating:** `boot "<disc>.bin" --validate-mod <mod>` checks a mod without starting the game (section 2d). **Coming from another version:**
   section 11 has the table of old and new paths.

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
| The partners' evolutions, blasts and growth | `tables/evolutions.json`, `blast_forms.json`, `level_exp.json`, `vital_cuts.json`, `vital_random.json`, `stat_gains.json`, `resist_gains.json` | 6.5b |
| The field's encounters: enemies, ambush, battle rate | `tables/field_enemies.json`, `field_encounters.json`, `battle_rates.json` | 6.5b |
| Cards: prices, shops, packs, effects, prizes, the default deck and opponents | `tables/card_prices.json`, `card_shop_stock.json`, `card_pack_slots.json`, `card_effects.json`, `prize_items.json`, `default_deck.json`, `card_opponents.json` | 6.5b |
| Gym and inns: trainings, gains, intensity costs and inn prices | `tables/gym_trainings.json`, `gym_stat_gains.json`, `gym_stat_losses.json`, `gym_resist_gains.json`, `gym_hp_mp_gains.json`, `gym_intensity_costs.json`, `inns.json` | 6.5b |
| Existing Digimon (partners and enemies: stats, techniques, loot), new Digimon with their model and the encounters they appear in | `digimon/d<nnn>/digimon.json` (change), `digimon/<key>/` (new), `content/encounters.json` (content mod) | 6.9 |
| New partners (name, first form, equipment, evolution list) and new starter teams | `partners/<key>/partner.json` and `content/teams.json` (content mod) | 6.9b |
| New cards (record, effect, art and name) | `cards/<key>/card.json` and `face.png` (content mod) | 6.10 |
| Loose images that are not edited by piece yet: logos and title (`TLOGO*`, `TTCOMMPK`), ending (`END__ALL`), country window (`CNTSELWN`), card battle windows (`CARDDATA`), `FIELDCOM` and the world map (`STWMP0`, `STWMP1`) (PNG, `atlas-v1` map) | `images/<folder>/<file>/<NN>_<MM>.png` | 6.2 |
| Field sprites (one PNG per frame, `sprites-sprite-v1` map) | `sprites/<name>/<EEE>.png` | 6.3 |
| A card picture (32x32 PNG, 256 colors; 316 cards) | `cards/c<nnn>/face.png` | 6.2b |
| Stage backgrounds (4 files; one PNG per page) | `images/field/stage/s<nnn>bg<nn>/<ii>.png` | 6.2c |
| Map objects of a field stage (one PNG per piece) | `stages/s<nnn>/objects/<piece>.png` | 6.2h |
| A map tile of a field stage (8-bit PNG per 128 px tile) | `stages/s<nnn>/tiles/<RR>_<CC>.png` | 6.2d |
| The files with no open format yet (collision maps, battle effects, overlays...) as a raw copy to start from | `raw/<disc path without AAA/>` | 6.2e |
| The 8 original partners and a mod's own (portrait, icon, field sprite and the gym training scene: imported) | `partners/<name>/`, `menus/gym/effects/<training>/` | 6.2f |
| The game's screens by piece, in their real colors (exported and read back, in the original geometry) | `menus/<screen>/<piece>.png`, `menus/<screen>/menu.json` | 6.2g |
| **Any disc file**, raw | `files/<disc path without AAA/>` | 6.4 |
| A game movie (MJPEG AVI with PCM audio) | `video/<NAME>.avi` | 8.1b |
| A sound effect (one sample of a bank, 16-bit WAV) | `audio/<BANK>/vag_NNN.wav` | 8.1d |
| A song (16-bit WAV in place of a sequence, with a loop) | `audio/<BANK>/seq_<package>_<record>.wav` | 8.1d |
| The characters of a field stage (change, add, remove) and their talks, with the mod's own flags; exits, scripted events and area battles | `stages/s<nnn>/stage.json` (`stage-patch-v1`) | 6.11 |
| New field stages (a map drawn with PNGs, collision, exits, battles) | `stages/<name>/` (`stage-new-v1`) | 6.11b |

New sprites for field characters: `mods/<mod>/sprites/<name>/` with a name that is not an original's (`sprite.json` + one PNG per frame; the frames of an original go in `sprites/<original name>/<EEE>.png`, 6.3), named by a stage patch with `"sprite"` (`docs/stages.md` section 11, in Spanish). Not yet: scripts (Lua, later), and loose XA audio (only inside the movies, 8.1b). See section 10.

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
<pref>/data/                 the port's, generated from YOUR disc; safe to delete, it is rebuilt (the tree, below)
<pref>/mods/<mod>/           the community's: one folder per mod
<pref>/saves/default/        your port saves: slot0-2.dw3save (own format, ADR-008) and view/slot<N>.json (read-only view)
<pref>/import/               PS1 .mcd memory cards to import (imported once, the first time)
```

**The `data/` tree** has the same shape as a mod: `data/<topic>/…` is `mods/<mod>/<topic>/…`. An **asset** is a folder (or a single file when it is one medium: a `.avi`
movie); every topic carries its `manifest.json` (`asset-manifest-v1`, in `data/` only) and every JSON its `"format"`.

```
<pref>/data/
  stages/s<nnn>/          stage.json  tiles/  objects/                      an original stage (6.2d, 6.2h, 6.11)
  cards/c<nnn>/           face.png  card.json                               an original card, c001 to c316 (6.2b)
  partners/<name>/        partner.json  portrait.png  icon.png  field/  training/   an original partner (6.2f)
  digimon/d<nnn>/model/   model.gltf  model.bin  model.png  model.json      an original fighter (8.1)
  sprites/<name>/         sprite.json  <EEE>.png                            a field character (6.3)
  menus/<screen>/         menu.json  <piece>.png                            a screen by pieces; menus/gym/effects/ (6.2g)
  images/<folder>/<file>/<NN>_<MM>.png  (+ .json atlas-v1)                  the loose images that remain (6.2)
  audio/<BANK>/           bank.json  sequences.json  vag_NNN.wav  seq_P_RR.mid   a sound bank (8.1c)
  video/<NAME>.avi                                                          a movie (8.1b)
  text/<language>/<file>.json                                               the text (6.1)
  tables/<table>.json                                                       the tables with the original values (6.5)
  content/                items.json  techs.json  digimon.json  cards.json  names of the original content (reference only)
  config/                 stage_banner.json  transitions.json  keys and ranges (3)
  raw/<disc path, lower case, without AAA/>                                 raw copy of what has no open format (6.2e)
```

A mod uses the same names (section 3). `raw/` is only the reference copy: in a mod, raw files go in `files/` (6.4).

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
   `config content tables text images audio video files`, comma separated). It never overwrites an existing mod. The `stages/`, `cards/`, `partners/`, `digimon/`, `sprites/` and `menus/` folders are made by hand (section 3). A template folder with a
   tiny example per kind lives in `pc/examples/mods/_template/`.
2. A one-line patch: `mods/my-mod/tables/item_prices.json` with
   `{"format": "item_prices-v1", "rows": {"*": {"price": "*0.5"}}}` (everything at half price).
3. `boot "<disc>.bin" --validate-mod my-mod` ends with exit code 0 and `ok`, or 1 and one line per problem with the file and the key
   (`tables/battle_rewards.json row "12" field "money" rejected: ...`). Exit code 2: no such mod. `--validate-mod` also takes a folder path
   (a mod you have not installed yet).
4. Play: with `DW3_LOG=mods` the log says `tables: item_prices patched by mods/my-mod (234 rows)`.

What it checks in each folder, and what it does not, is in section 2d. `--list-mods` and `--new-mod` need no disc.

### 2d. Validating a mod

`boot "<disc>.bin" --validate-mod <mod>` (or `--validate-mods`, for all of them) runs the **game's own readers** over the mod without starting it: one line per problem, with the path
inside the mod. It is an `error` if the game rejects the file, a `warning` if it ignores it silently and `skipped ... not checked` if it cannot be checked. Exit code 0 (no
errors), 1 (errors) or 2 (no such mod).

| Mod folder | What it checks |
|---|---|
| `mod.json` | format, unknown keys, `version`, `port`, `requires`, order and dependencies |
| `tables/` | that the table exists (with "did you mean") and every row and field, with the same refusals as in the game |
| `content/` | `items.json`, `techs.json`, `teams.json` and `encounters.json` with their readers; warns about another file name and about a mod that is not `"kind": "content"` |
| `config/` | the keys and their ranges |
| `digimon/` | every `digimon.json` (header, `name`, fields, ranges) and `model/model.json`; warns about `d<nnn>/model/` (not read) and about a folder with neither a record nor a model |
| `partners/` | `partner.json`; `portrait.png` and `icon.png` (size and format), the `field/` frames and every `training.json`; warns about what nobody reads, about a folder that no original or mod declares and about a folder two layers bring (which one wins) |
| `cards/` | every `card.json` with the game's loader and the size of `face.png`; warns about `c<nnn>` outside `c001`-`c316`, without `face.png` or with a `card.json`, and about a folder with no `card.json` |
| `stages/` | the `stage.json` patch, new stages, tiles (`tiles/`: format and size against the original) and the pieces of `objects/` (size and name); warns about stray files and about folders no stage uses |
| `menus/` | the size of every piece; warns about a file no piece is called and about a piece two mods ship |
| `sprites/` | the `NNN.png` frames of an original and the `sprite.json` of a new sprite; warns about what is not read (a `sprite.json` in an original, a partner's follower) |
| `images/` | every PNG: size, indexed format and number of colors |
| `audio/`, `video/` | the WAV and AVI files with the game's readers |
| `files/` | that every file exists on the disc (needs the disc image) |
| `text/` | **not checked** (`skipped ... not checked`): the text is read when each file opens; the palette of the tiles is also compared while the map loads |

Any other top-level folder shows as `folder <name>/ is never read` (with a "did you mean" when it looks like a topic); that is what you will see with folders of the previous format
(section 11). A mod's `tools/` folder (your own scripts) is neither read nor reported.

### 2e. JSON Schemas (completion and errors in your editor)

`docs/modding/schema/` has one schema per format. In VS Code, add `"$schema": "<path to the schema>"` at the top of the file (relative to the file, or `file:///...`); the port
ignores that key. The schemas come from what the parsers read (keys, types, ranges); each table field's range is in its reference file
`<pref>/data/tables/<table>.json`.

| Schema | File | Read by the port |
|---|---|---|
| [`mod.schema.json`](schema/mod.schema.json) | `mod.json` | yes |
| [`table-patch.schema.json`](schema/table-patch.schema.json) | `tables/<table>.json` | yes |
| [`items.schema.json`](schema/items.schema.json), [`techs.schema.json`](schema/techs.schema.json), [`teams.schema.json`](schema/teams.schema.json), [`encounters.schema.json`](schema/encounters.schema.json) | `content/items.json`, `techs.json`, `teams.json`, `encounters.json` | yes |
| [`digimon.schema.json`](schema/digimon.schema.json) | `digimon/<key>/digimon.json` and `digimon/d<nnn>/digimon.json` | yes |
| [`partner.schema.json`](schema/partner.schema.json) | `partners/<key>/partner.json` (a new partner) | yes |
| [`card.schema.json`](schema/card.schema.json) | `cards/<key>/card.json` (a new card) | yes |
| [`stage-patch.schema.json`](schema/stage-patch.schema.json), [`stage-new.schema.json`](schema/stage-new.schema.json), [`stage-text.schema.json`](schema/stage-text.schema.json) | `stages/s<nnn>/stage.json` (patch), `stages/<key>/stage.json` (new stage), `text/<language>/stages.json` | yes |
| [`sprite.schema.json`](schema/sprite.schema.json) | `sprites/<name>/sprite.json` (a new sprite's; the exported one is informational) | only a new sprite's |
| [`manifest.schema.json`](schema/manifest.schema.json) | `data/<topic>/manifest.json` (`asset-manifest-v1`) | in `data/` only |
| [`stage.schema.json`](schema/stage.schema.json), [`stage-tiles.schema.json`](schema/stage-tiles.schema.json), [`stage-objects.schema.json`](schema/stage-objects.schema.json) | the exported `stages/s<nnn>/stage.json` (`stage-v1`), `tiles/tiles.json` and `objects/objects.json` | no (informational) |
| [`menu.schema.json`](schema/menu.schema.json) | the exported `menus/<screen>/menu.json` | no (informational) |
| [`card-export.schema.json`](schema/card-export.schema.json), [`partner-export.schema.json`](schema/partner-export.schema.json), [`partner-training.schema.json`](schema/partner-training.schema.json) | an original's `card.json` and `partner.json`, and every training's `training.json` | no (a mod partner's `training.json` is) |

## 3. Anatomy of a mod

```
my-mod/
  mod.json                              required
  config/stage_banner.json              only the keys you change; also config/transitions.json
  tables/<table>.json                   table patches (6.5)
  text/<language>/<file>.json           the text; languages: eng fra ita ger spa es-419 (6.1)
  content/items.json, techs.json, teams.json, encounters.json   new content (kind "content")
  digimon/d<nnn>/digimon.json           changes an original Digimon (6.9)
  digimon/<key>/digimon.json            a new Digimon (6.9) and, if it brings its glTF model, digimon/<key>/model/ (8.2)
  partners/<key>/partner.json           a new partner (6.9b) beside its art: portrait.png, icon.png, field/, training/ (6.2f)
  partners/<name>/portrait.png ...      an original partner's art (agumon...): only the art is replaced (6.2f)
  cards/c<nnn>/face.png                 the picture of original card <nnn> (6.2b)
  cards/<key>/card.json + face.png      a new card (6.10)
  stages/s<nnn>/stage.json              patch of the characters and talks of one original stage (6.11)
  stages/s<nnn>/tiles/<RR>_<CC>.png     a map tile (6.2d)
  stages/s<nnn>/objects/<piece>.png     a piece of the map objects (6.2h)
  stages/<key>/stage.json               a new stage (6.11b) and its maps: background.png, floor.png, triggers.png, areas.png
  sprites/<name>/<EEE>.png              a frame of an original sprite (p001play) (6.3)
  sprites/<name>/sprite.json + PNGs     a new sprite (docs/stages.md section 11)
  menus/<screen>/<piece>.png            a piece of a screen (6.2g)
  images/<folder>/<file>/<NN>_<MM>.png  one of the loose images that remain (6.2)
  files/<disc path without AAA/>        a raw disc file, e.g. files/dat/z_stage/s330pack.bin (6.4)
  video/<NAME>.avi                      a replaced movie (8.1b), e.g. video/MOVIEOPN.avi
  audio/<BANK>/vag_NNN.wav              a replaced sound sample (8.1d), e.g. audio/COMMON/vag_056.wav
  audio/<BANK>/seq_P_RR.wav             a song in place of a sequence (8.1d), with an optional seq_P_RR.json (loop)
```

Same path as in `data/` = **replacement** of that file; a folder with a new name = a **new asset** that is added. What a folder lacks uses the original's file.
The names `s<nnn>`, `c<nnn>` and `d<nnn>` (a letter and three digits) are **reserved** for the originals: a key of your own is a-z, 0-9, `-` and `_`.

Everything the port writes is **lower case** (`data/sprites/p001play/...`) and in a mod it is best to copy those names as they are: `sprites/`, `cards/` and `digimon/` expect the originals' keys
in lower case (`p001play`, `c012`, `d383`). When it reads `images/`, `files/` and the tiles it ignores case, so a mod written with the disc's upper-case paths
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
| `kind` | no | `"assets"` (default: replaces resources) or `"content"` (adds or changes data: new items, techniques, Digimon, cards and partners) |
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

### 6.2 An image mod: the loose images (`images/`)

Almost all the art is edited per asset, in its own folder: the menu screens in `menus/` (6.2g), a stage's tiles and objects in `stages/` (6.2d, 6.2h), the cards in `cards/`
(6.2b), the field sprites in `sprites/` (6.3) and the partners in `partners/` (6.2f). `images/` holds what still has no pieces: `TLOGO*`, `TTCOMMPK`, `END__ALL`, `CNTSELWN`, `CARDDATA`,
`FIELDCOM`, the stage backgrounds (6.2c) and, as an exception, the 8-bit pages `STWMP0` and `STWMP1` (their pieces have no PNG, so there is no other way to edit them).

1. Find the image in `<pref>/data/images/<folder>/<file>/<NN>_<MM>.png`. Next to most PNGs there is a `<NN>_<MM>.json` (`atlas-v1`) with the pieces of the image
   (`tools/image_atlas_preview.py` draws screens by piece, stage objects and sprites, not these images).
2. Copy it to `my-mod/images/<folder>/<file>/<NN>_<MM>.png` and edit it **keeping**: same width and height, an
   **indexed** PNG (with a palette, not interlaced), at most 16 colors (256 in 8-bit pages), and the palette **order**
   (the PNG's indices replace the game's). Alpha 0 = transparent.
3. A PNG that breaks the rules is rejected: `images: <path> rejected: <reason>` and the next source is used. Mods are read **once, at
   startup** (an in-memory index of which PNGs each mod has): a change shows after you **restart** the game. To see changes
   with the game open, start it with `DW3_MODS_HOTRELOAD=1` (for modders only: it re-checks the files every second and costs
   performance with many mods; it does not watch `menus/`).

### 6.2b An image mod: the card pictures

The card game keeps each card's picture in `CARD/CARDPAK0-4.BIN` (64 cards per file, 60 in the last: 316 in all). The
port exports them as `<pref>/data/cards/c<nnn>/face.png` (plus an informative `card.json` and `cards/manifest.json`), where
`<nnn>` is the **game's card number** in three digits (`c001` to `c316`): the same `<id>` as `dw3:card/<id>`. The game reads
it that way (`cardDrawerSetCard`: file `(k-1)/64`, image `(k-1)%64`), so card `k` is image `(k-1)%64` of
`CARDPAK<(k-1)/64>`. Each PNG is **32x32**, 8-bit, with **its own 256-color palette** (index 0 transparent): more freedom
than the menus, because every card loads its own palette. It runs at start (`settings.ini`, `[data]`, `export_cards`: `all`
if the line is missing, `off`, or numbers separated by commas: `export_cards = c012, 40`).

To replace one, copy it to `my-mod/cards/c<nnn>/face.png` (or edit the one in `data/`) under the rules of 6.2 (same size,
indexed, at most 256 colors, the palette order). The replacement shows in the album, the shop, the deck editor and the
battle. The 12 bytes before each picture (color, AP, HP, kind...) are not exported: they are game data, not art.
A `c<nnn>` folder (a `c` and three digits) is **always an original card**: a new card cannot be named that (its key does not
read `cards/c<nnn>/face.png` as its own art).
`CARD/CARDDATA.BIN` (card battle windows and sprites, not a card's art) comes out as `images/card/carddata/00_00.png`
and `01_00.png`, with its `atlas-v1` map, and is replaced like the rest of `images/` (6.2). `CARDPACK.BIN` is almost a copy of the five files joined (it
differs in 6 of the 316 cards) that the game does not read by number; it is not exported.

### 6.2c An image mod: field stage art

A stage's **map objects** (`FIELD/STAGE/S<nnn>MASK.BIN`, which is not a depth mask) are edited by piece in
`stages/s<nnn>/objects/` (6.2h).

The only stage art that is in `images/` are the **backgrounds** `S415BG01/02` and `S815BG01/02` (they are not objects and have no piece sheet): they come out as
`<pref>/data/images/field/stage/s415bg01/<ii>.png` (128x256, 8-bit, no map) and are replaced under the rules of 6.2 (same size, indexed, at most 256 colors).

### 6.2d An image mod: the map tiles of a stage

Each field stage's map lives in `Z_STAGE/S<nnn>PACK.BIN` (237 files, 11,745 tiles, 128 px tall). The game reads them from the disc as
the player walks. They do not take the same path as the rest of the art, so they have an exporter of their own, **on demand** (about
12,000 files and 250 MB):

- **Menu:** *Options > System > Prepare modding files* (one button for videos, audio, models and stages; see 2b) exports all 237 (the row's value reads `Stages 3/237`). It runs on a
  worker thread; the game stays usable.
- **`settings.ini`:** `[data]` / `export_stages = all` or `export_stages = S233, S330` exports them at start, before the window opens.
  A stage already exported by the same exporter version is **skipped** (the second start does not read the disc again). The menu row
  rebuilds all of them.

Each stage comes out in `<pref>/data/stages/s<nnn>/tiles/`:

- `<RR>_<CC>.png`: one tile (row `RR`, column `CC`, two digits; tile `n` of the stage is `RR = n / width`, `CC = n % width`). An 8-bit
  indexed PNG (256 colors with the tile's own CLUT; color 0 is transparent), 128 tall and 128 to 256 wide. Empty tiles have no file.
- `tiles.json` (`stages-tiles-v1`): the stage's `width` and `height` in tiles, `tileBytes` and, per tile, `frame`, `row`, `col`, `png`,
  `size` (bytes on the disc), `rle` (whether it is packed), `timOffset`, the image's `width`/`height` and `depths`: three lists (one per
  depth) of the `x, y, u, v, w, h` cut-outs the game draws from that PNG. It is reference only: the port does not read it.
- What the port wrote is in the one `manifest.json` of `stages/` (shared with the stage data and objects; the version is `stage-tiles/s<nnn>` in `exporter`). The usual `.new` rule: a PNG you edited stays and the new one goes beside it as
  `<RR>_<CC>.png.new`.

Replace them under the rules of 6.2 (same size, indexed PNG, at most 256 colors; a PNG with fewer colors only changes the first ones
and the rest of the palette stays as on the disc) by putting the file at `mods/<mod>/stages/s<nnn>/tiles/<RR>_<CC>.png` (the last mod
wins; any letter case); editing the PNG in `data/stages/s<nnn>/tiles/` also works (once it differs from the manifest, which is read once at start). A PNG that breaks the rules is
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

### 6.2f The original partners: one bundle per partner

Each original partner is exported **once and in its real colors** to `<pref>/data/partners/<name>/` (`<name>` is the partner's English name in lower case),
with no VRAM sheets and no per-screen palettes. It runs at startup, before the window opens (about 6000 small files), and a start that finds everything
written by the same version does not read the disc. **`portrait.png`, `icon.png` and `field/` are also read back** (see "Editing an original" below and 6.9b);
`training/` is read back too (below, "A partner's gym"); `menus/gym/effects/` is export only.

- **`settings.ini`:** `[data]` / `export_partners` is `all` when the line is absent, `off` to write nothing, or names separated by commas
  (`export_partners = agumon, veemon`).

```text
partners/<name>/
  partner.json        partners-partner-v1: name, identity (dw3:partner/<n>), first form (dw3:digimon/<n>), what each file is
  portrait.png        strip 32 pixels high and 32 x N wide (N = 3 or 4 cells), with the partner's own palette; index 0 transparent
  icon.png            44x28, the memory card icon
  field/              the field sprite (sprite-v1: <EEE>.png per frame and sprite.json)
  training/<training>/   the gym scene: <NNN>.png per sprite (real colors) and training.json
menus/gym/effects/<training>/   the effects all 8 share (same training.json; 10 of the 12 trainings have one)
```

`portrait.png`: `partner.json` gives the cells, the animation order (there and back, `0 1 2 3 2 1`), where each cell sits in its frame (`offsets`) and which
sheet frames it comes from. The game draws the same portrait on 7 screens: here it is once. `training/<training>/`: a training has the English name the gym shows
for it (`punch`, `jump-rope`, ... the *slug* of your disc's text; the disc file, `TRANIN00`..., is named only inside `training.json`); the two trainings that share one file
(the normal one and its "Super" variant) share a folder, and `partner.json` says which gym rows use it. A sprite is several pieces composed into one PNG (a 16-color palette per piece; if
the sprite mixes palettes, each takes its own 16 indices); `training.json` gives, per sprite, the PNG's `origin`, and per animation its steps (`frame`, `time`
in ticks, `x`, `y`). A partner's sprite may use pixels of the shared effect (`effectParts`). Semi-transparency (`semiTransparent`) is noted and `blend` lists the index ranges the game draws blended
(mode 0 average, 1 add, 2 subtract, 3 a quarter added); the PNG draws them opaque. `paletteBase` (0 or 1) says where its 16-index palettes start. Pieces drawn over each other follow the list order (the last on top). All of it is information: it is regenerated from the disc, and a file you edited
stays (the new one goes beside it as `<file>.new`).

**Editing an original.** Change `data/partners/<name>/portrait.png`, `icon.png` or a frame of `field/` (or put them in `mods/<mod>/partners/<name>/`: a mod beats `data/`, the last
mod beats earlier ones, **file by file**; whatever is missing stays as it was). At startup, once, the port reads your portrait and rewrites, in the 7 screens that draw it
(starter team and confirmation, naming, lab, field menu, gym, battle report and memory card), the cells and **the same palette** in every row they use: there is no
per-screen palette to keep in step. In `data/` only a file you **changed** counts (its hash is no longer the one in `partners/manifest.json`; the exporter's own file does not).
Rules for an original's `portrait.png`: 32 high and **exactly the cells it already has** (3 or 4: each replaces the frame at that place of the animation), up to 16 colours
(with more, the port merges the closest ones down to 16 and warns in the log with the path and the count; the transparent index 0 is kept); `icon.png`: 44x28, likewise. A file
that does not comply is refused with a `[partners] <path> rejected: <reason>` line and the previous one stays. The frames of `field/` follow the `sprite-v1` rules (6.3).

**A partner's gym.** The gym shows each partner's training scene, a mod's partner too (index >= 8, no PS1 limit of nine). Put a `training.json` and one `<NNN>.png` per sprite in
`mods/<mod>/partners/<key>/training/<training>/` (`<key>` is its folder; `<training>` is the English name found in `data/partners/<name>/training/`: `punch`, `defense`, `yoga`, `study`,
`jump-rope`, `heat-tolerance`, `water-tolerance`, `ice-tolerance`, `wind-tolerance`, `thunder-defense`, `missile-defense`, `haunting`): the easy way is to copy an original's folder and edit it. Rules:

- Each training stands alone: a missing one (or one that cannot be built: a `[partners] <path> rejected: <reason>` log line) shows the partner's `base` one (the disc's, or the one you edited for that original),
  and the log says so once (`partner 8 has no training defense of its own: it shows partner 3's`). A folder counts whole, from the first layer that has it (the last mod beats the earlier ones and `data/`).
- `training.json` (`partners-training-v1`): `frames` (each with `id`, `image` -- a `.png` or `null` for an empty sprite --, `origin` [x, y] of the PNG relative to the sprite's origin, and optionally `paletteBase`
  and `blend`) and `animations` (`id` 0..5, `steps` with `frame`, `time` in ticks, `x`, `y`; the last step is held). They are the set's 6 animations, the ones the game asks for by number (`docs/formats.md` 4.12);
  a missing `id` leaves that number without an animation.
- Each PNG is indexed (up to 256 colors, 256x256 at most; index 0 or a color with alpha 0 is transparent). If you made it by hand without `paletteBase`, the port deals the visible colors into palettes of 15 in index
  order; a PNG copied from the exporter keeps its palettes.
- A set fits **2 pages of 256x256 with 16 palettes each** (what a disc set fits; repeated pieces count once and, if they do not fit whole, tiles of 16 and of 8 pixels are tried). If it does not fit: `rejected` in the
  log and the partner falls back to its `base`. The shared effects (`menus/gym/effects/`) are still the game's: the effect the game draws separately does not change.
- An edited **original** works the same: change a file of `data/partners/<name>/training/<training>/` (a folder counts when some file's hash is no longer `partners/manifest.json`'s) or put it in
  `mods/<mod>/partners/<name>/training/...`. The PNG is the flat composite of the pieces: a semi-transparent piece that covered another blends only with the background.
- It is built once at startup, with no per-frame cost (about 0.1 s in all for the game's 96 folders when their hashes are checked).

### 6.2g The screens by piece

Every screen of the game is exported **by piece and in its real colors** to `<pref>/data/menus/<screen>/` (`<screen>` is the disc file without its suffix, in lower case: `stmcrd`, `stdglb`...), next to `menus/gym/effects/` and with one single
`menus/manifest.json`. It runs at startup (about 960 files, 0.7 MB; a start that finds everything written by the same version does not read the disc).

- **`settings.ini`:** `[data]` / `export_menus` is `all` if the line is missing, `off` to write nothing, or screen names separated by commas (`export_menus = stmcrd, stdglb`).
- Each `<piece>.png` is an indexed PNG of 16 colors (256 for the 8-bit ones) with the palette the game draws that piece with; `menu.json` says where it goes (page, rectangle, palette row) and which frames draw it, with their offset and blend.
  The full format is in `docs/formats.md` section 7.5b.
- **Edit a piece:** change the PNG in `data/menus/<screen>/`, or copy the screen (or just the piece) to `mods/<mod>/menus/<screen>/` and edit it there. The game puts each piece **where the disc has it**: in its rectangle of the page and its palette in its CLUT row
  (`menu.json` says so, but the port takes those positions from the disc, not from the mod's `menu.json`). So:
  - **Keep the size** of the piece; a PNG of another size is refused with a log line (`menus: <path> rejected: size WxH, the piece is wxh`) and the next layer's file, or the disc's piece, is used.
  - The PNG's indices are used as they are if it has up to 16 colors (256 for the 8-bit ones). With more colors they are reduced to 16, with a log line. Index 0 and every color the disc leaves as `0x0000` are transparent; the semi-transparency bit (STP) of each color is the disc's.
  - Pieces that draw the same rectangle with another palette row share its indices, and pieces that share a CLUT position share the palette. If two edited pieces disagree, the later layer (the last mod) wins and the log says so; inside one layer, the first one in `menu.json`.
  - Only what changes counts: a piece equal to the disc's writes nothing, so copying the whole export into a mod has no effect, and the pixels and the palette of a piece are judged apart.
  - A piece with `file: null` (those of `stwmp0` and `stwmp1`) has no PNG and stays as on the disc (`images/` edits those two screens, 6.2).
- **Layers:** the mods from the last to the first, then `data/` (section 5); a `data/` file only counts if you changed it (its hash is no longer `menus/manifest.json`'s). It is read once, when the game first builds that screen's files, and kept in memory;
  there is no hot reload. Measured on the EU disc: about 20 ms in all for the 15 screens when the hashes of `data/` have to be checked (6 ms without the packer) and about 40 ms with a full copy of the export in a mod; in the game each screen pays only for its own.
- **By piece only:** the screens that have a piece sheet are edited only here; `stwmp0` and `stwmp1` are the exception and stay in `images/` (6.2) because their pieces have no PNG.
- **Validating:** `boot <disc> --validate-mod <mod>` builds the plan of each screen: a PNG of another size is an error (`menus/<screen>/<piece>.png rejected: size WxH, the piece is wxh`), a file that no piece is called is a warning (`... ignored: no piece of <screen> is called that`) and so is a piece that two mods ship (`... ignored: the piece is also in mods/<other>, which loads later and wins`).
- **Example: frames in gold.** Look at `data/menus/<screen>/menu.json` and the PNGs to find the pieces of the frame (for instance in `stshop`, the item shop); copy those pieces to `mods/my-mod/menus/stshop/` and change **only the palette** (the blues to gold) with an editor that keeps the order and the number of colors; the pixels stay the same.
  Pieces with the same `clut` in `menu.json` share a palette: give them all the same one, or the log reports the disagreement and one wins. At startup the log says `menus: override stshop/<piece>.png (colours) from mods/my-mod`.
- **Not yet:** moving a piece elsewhere or changing its size (there is no free layout; the game asks for each texture at a fixed VRAM position). The pieces of `stwmp0`/`stwmp1` have no PNG: they are edited through `images/` (6.2).

### 6.2h A stage's objects by piece

The map objects of every stage are exported **by piece and in their real colors** to `<pref>/data/stages/s<nnn>/objects/` (one PNG per piece and an `objects.json`), with the same model as the screens of 6.2g and the `stages/manifest.json` manifest. It runs at startup, after `stages/s<nnn>/stage.json` is written (it takes from there which entry of the `TMPK` is the sheet; it is not always 0). On the EU disc: 8632 PNG and 294 `objects.json` (about 10 MB).

- **`settings.ini`:** `[data]` / `export_stage_objects` is `all` if the line is missing, `off` to write nothing, or stages separated by commas (`export_stage_objects = s202, s920`).
- A piece is a rectangle with **one** palette; its name is `f<frame>p<slot>.png`; `objects.json` says where it goes (page, rectangle, palette row) and which frames draw it. An 8-bit piece has a `clut.row` of 256 or more: its palette is a row of the CLUT of the `MASK` file's 8-bit image, not a page. Format: `docs/formats.md` section 7.5c.
- A stage without pieces (11 today, with a sheet that has no frames) has an `objects.json` with `unexported` and the reason. The palette rows an object cycles at run time are not in the disc as data: only the rows the sheet's frames use are exported.
- **They are read back.** Edit a piece (pixels and colors; same size) and put it in `mods/<mod>/stages/s<nnn>/objects/<piece>.png` (or edit the one in `data/`): when that stage loads, the game writes it back into its original place. The rectangle, mode and palette row come from the sheet on the disc, not from `objects.json` (which is informational). Only what differs from the disc is written, so copying the whole folder changes nothing; each color's STP bit and the `0x0000` (transparent) color come from the disc. A PNG of another size is refused with a log line. Two pieces that share a rectangle or palette and are edited differently: the later layer (the last mod) wins, with one log line naming the stage. A stage with no edited piece costs nothing at load. This is the **only** way to edit a stage's objects.
- **Validate:** `boot <disc> --validate-mod <mod>` builds the plan of each stage that has edited pieces: a PNG of another size is an error (`stages/s<nnn>/objects/<piece>.png rejected: size WxH, the piece is wxh`); a file that no piece is called (`... ignored: no piece of s<nnn> is called that`) and a piece two mods ship are warnings, and so is a folder of `stages/` that is not `s<nnn>`, that is a stage the disc does not have, or that holds something besides `objects/`, `tiles/` and `stage.json` (`... ignored: ...`). The tiles are checked on their own (format, size against the exported original, `images: <path> rejected: <reason>`; a file that is not `<RR>_<CC>.png` is `ignored`), and so is the stage patch (`[stages] <path> rejected: <reason>`); a file straight in `stages/` is a warning: nothing reads it.
- **No pieces, no objects.** The 11 stages without pieces (`S376`, `S875`, `S880`, `S885`, `S890`, `S895`, `S970` to `S974`) draw no map object: their `stageObjects` table holds only the terminator (`src/field/stages/`) and their sheet has no frames, so they lose no edit.

### 6.3 Recolor a field sprite

Each `FIELD/SPRT/<NAME>.BIN` (except the 8 partner followers, which live in `partners/<name>/field/`) is extracted to `data/sprites/<name>/` (the file name in lower case): one 4-bit PNG per frame (`<EEE>.png`, the frame's entry in the file) plus a `sprite.json` (`sprites-sprite-v1`: frames,
animations, sets and directions) and `sprites/manifest.json`. To replace a frame put the PNG at `mods/<mod>/sprites/<name>/<EEE>.png`; a folder named like an original only brings its frames (its `sprite.json` is not read), any other name is a new sprite of the mod (`docs/stages.md` section 11, in Spanish). `--validate-mod` reports what is not read. The player in Central Park is `P002PLAY` (and `P001PLAY`). Each `P002PLAY` pose lives in its own file: `P002PLYD`
(drop, land), `P002PLYF` (gauge minigame), `P002PLYK` (search) and `P002PLYL` (stairs, climbing walls); a mod that
recolors the player must cover them all (`FIELDSTG_animateActor`, `actor_control.c`).

```
python3 tools/image_mod_palette.py --mod ~/.local/share/dw3port/dw3/mods/my-recolor --sprite p001play --sprite p002play --sprite p002plyd --sprite p002plyf --sprite p002plyk --sprite p002plyl --to 0
```

The script changes only each frame's palette (blues to red; `--to 120` green, `--to 45` gold) and writes every PNG at the same path inside the mod (`--mod` picks the folder; by default `<pref>/mods/player-recolor`). On Windows it uses
`%APPDATA%\dw3port\dw3\` by itself, or pass `--data` and `--mod`. It works the same with one piece (`menus/stshop/f000p00.png`), a folder (`cards/c001`) or a portrait (`partners/<name>/portrait.png`); `--list` shows what is there. Rules: those of the menu art, with **16 colors**.
`python3 tools/image_atlas_preview.py <data>/sprites/p002play --set 4 --dir 6 --gif` animates one direction.

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

Game rules that live in tables are changed with a **patch**: `mods/<mod>/tables/<table>.json`. This part covers `battle_rewards`, `battle_loot`, `item_prices`, `shop_stock`, `weapon_data`, `armor_data`, `accessory_data` and `item_effects`; the others (evolutions, growth, encounters, cards, gym) are in 6.5b, all with the same format (ADR-007).

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
  `assets` (it adds no rows or identifiers). Adding rows is only possible in the tables that say so (id `<mod>:<name>`, a `content` mod).

**`battle_rewards`.** Key: the encounter number (the battle's row), from 0. Fields (range):

| Field | What it is | Range |
|---|---|---|
| `digimonExp` | EXP shared by the Digimon used in the battle | 0 to 9999999 |
| `exp` | EXP shared by the partners that fought (the report gives 100%, 60% or 33% depending on how many fought) | 0 to 999999 |
| `money` | Money the battle gives (a fifth more with the item that boosts it; the wallet tops out at 9999999) | 0 to 9999999 |

The ranges are the caps the game's report applies. The report reads the row when it opens, so a change shows in the
**next battle**, without restarting. It is applied on entering the report, after the port restores the overlay's data.
To see the row and the wallet of each report, set the log to `trace` with the `tables` channel (section 6.6).

Minimal example (partner EXP and money x2 in every encounter): `{"format": "battle_rewards-v1", "rows": {"*": {"exp": "*2", "money": "*2"}}}`.

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

Minimal example (`itemChance` x2 for every enemy): `{"format": "battle_loot-v1", "rows": {"*": {"itemChance": "*2"}}}`.

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

Minimal example (`price` x0.5 for every item): `{"format": "item_prices-v1", "rows": {"*": {"price": "*0.5"}}}`.

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

Minimal example (shop 0 sells one more item): `{"format": "shop_stock-v1", "rows": {"0": {"items": {"add": [93]}}}}`.

### 6.5b The other game tables (equipment, evolutions, growth, the field and the cards)

They use the same format and rules as section 6.5: `mods/<mod>/tables/<name>.json` with `"format": "<name>-v1"` and a `"rows"` object
keyed by row; a number replaces, `"*k"` multiplies, `"+k"` and `"-k"` add, and the key `"*"` is every row. Each table has its
reference copy in `<data>/tables/<name>.json` (the original values, the valid ranges and, where it makes sense, the name of each row
in `"labels"`); it is only for reading, editing it changes nothing. A row or field out of range is rejected with a line in the log
(`DW3_LOG=mods`). The detail of each table (what the game reads, where the ranges come from) is in `docs/data-tables.md`.

| Table (`tables/<name>.json`) | Row key | What it changes | When it shows |
|---|---|---|---|
| `weapon_data` | item id 92-214 (or `<mod>:<name>`) | the weapons' stats: charisma, attack, accuracy, two bonuses, hit effect | when equipping |
| `armor_data` | item id 215-291 | the armour's stats | when equipping |
| `accessory_data` | item id 292-359 | the accessories' bonus and kind | when equipping |
| `item_effects` | usable item id (43-394) | what a usable item does: effect kind and amount | when used |
| `evolutions` | `partner * 100 + entry` (352 rows) | the form each partner learns and its requirements | in the next battle report |
| `blast_forms` | the Digimon id (52 forms) | the form a blast turns into, per level tier | in the next blast |
| `level_exp` | first level of the tier (1, 5, 20, 40) | the EXP each tier of levels asks for | in the next report |
| `vital_cuts`, `vital_random` | first level of the tier; slot 0-8 | the HP and MP a partner gains on a level up | in the next report |
| `stat_gains`, `resist_gains` | first level of the tier (1-80); slot 0-7 | the stats' and resistances' gain per level | in the next report |
| `field_enemies` | row number (0-522) | Digimon, level, HP, MP and strength of the enemies of the field's encounters | in the next field battle |
| `field_encounters` | encounter index (0-334); `"<mod>:<name>"` adds one | which enemy rows each encounter uses, the ambush, the kind and the 12 blocks; in a new row, its enemies and rewards | in the next field battle |
| `battle_rates` | rate index (0-5) | how many steps each rate takes off the count to the next random battle | from the next step |
| `card_prices` | card id (the 44 priced cards) | what the card shop charges for each | when the card shop opens |
| `card_shop_stock` | shop number (49-67, 70-74) | the cards each shop sells (a list of ids 1-314 or `"<mod>:<name>"`) | when the buy screen opens |
| `card_pack_slots` | `pack item id * 10 + slot` (35 packs x 6 slots) | the 16 cards each slot draws from when a pack is opened (`card1`..`card16`) | when a pack is opened |
| `card_effects` | card id (1-60) | conditions, message, target kind and step script (`step1`..`step40`) of the first 60 cards | when the card is played in a duel |
| `prize_items` | prize number (0-35) | the item an opponent gives when beaten | when the opponent is loaded |
| `default_deck` | position in the deck (0-39) | the deck you play with when the chosen folder is empty (card id minus one) | when a duel picks the deck |
| `card_opponents` | the duel mode's argument minus one (0-169) | each opponent's 40-card deck, counter cards, level and prize (`CARD_NPC.BIN`) | when the next duel with that opponent starts |
| `gym_trainings` | `gym index * 100 + slot` (14 x 16 slots) | which trainings each gym offers: `id` (1-24), the stat it raises (`stat`) and the second one (`other`) | when the gym opens |
| `gym_stat_gains` | column 0-11: `kind * 6 + bonus try * 3 + intensity` | how much a training raises a battle stat (`base` and `range`) | when the training result shows |
| `gym_stat_losses` | intensity 0-2 | how much a resistance training may take off a battle stat | when the result shows |
| `gym_resist_gains` | `table * 10 + slot` (3 tables x 9) | how much a training raises a resistance (low, middle or high table) | when the result shows |
| `gym_hp_mp_gains` | slot 0-8: `intensity * 3 + tier` | how much a training raises the maximum HP or MP | when the result shows |
| `gym_intensity_costs` | intensity 0-2 | the training points (TP) each intensity costs | when the intensity is chosen |
| `inns` | game mode of the inn, in decimal (21 inns; 522 is the first) | `price` per night and per partner, and `string`, the text of the inn's name | when the inn opens |

One file per table, minimal (each is the whole file; `"*"` is every row):

```json
// tables/stat_gains.json: double the stats gained per level
{"format": "stat_gains-v1", "rows": {"*": {"gain0": "*2", "gain1": "*2", "gain2": "*2", "gain3": "*2", "gain4": "*2", "gain5": "*2", "gain6": "*2", "gain7": "*2", "gain8": "*2"}}}
// tables/field_enemies.json: rows 71 and 72 become enemy 206 and enemy 32
{"format": "field_enemies-v1", "rows": {"71": {"fighter": 206}, "72": {"fighter": 32}}}
// tables/battle_rates.json: random battles come twice as often
{"format": "battle_rates-v1", "rows": {"1": {"rate": "*2"}, "2": {"rate": "*2"}, "3": {"rate": "*2"}, "4": {"rate": "*2"}, "5": {"rate": "*2"}}}
// tables/inns.json: every inn at 1 BIT
{"format": "inns-v1", "rows": {"*": {"price": 1}}}
// tables/gym_intensity_costs.json: the three gym intensities at half cost
{"format": "gym_intensity_costs-v1", "rows": {"*": {"cost": "*0.5"}}}
// tables/weapon_data.json: every weapon with double attack
{"format": "weapon_data-v1", "rows": {"*": {"atk": "*2"}}}
// tables/card_opponents.json: every opponent opens with ten copies of card 120, level 99
{"format": "card_opponents-v1", "rows": {"*": {"card1": 120, "card2": 120, "card3": 120, "card4": 120, "card5": 120, "card6": 120, "card7": 120, "card8": 120, "card9": 120, "card10": 120, "level": 99}}}
```

Notes:

- The field tables are applied each time the field is entered. `field_encounters` names its enemies by row number of
  `field_enemies`: the rows are shared, so changing a row changes every encounter that uses it. Neither row 0 of `field_enemies`
  nor encounter 0 can change, and an encounter cannot be left with an empty first enemy.
- **New encounters.** A row of `tables/field_encounters.json` whose key is a name `"<mod>:<name>"` (the mod's own folder, a colon and a key of `a-z 0-9 - _`)
  **adds** an encounter instead of changing one; the originals are never touched and removing the mod leaves them as they were. It carries its own enemies (it does not share
  `field_enemies` rows) and its rewards:

  ```json
  {"format": "field_encounters-v1", "rows": {
    "mymod:meadow": {
      "enemies": [{"enemy": 206, "level": 10, "hp": 300, "mp": 40, "strength": 30},
                  {"enemy": "mymod:slime", "level": 9, "hp": 250, "mp": 0, "strength": 25}],
      "ambushChance": 0, "kind": 1, "runAway": 0,
      "rewards": {"money": 120, "exp": 45, "digimonExp": 30}}}}
  ```

  `enemies` is 1 to 3 enemies (the first cannot be 0); `enemy` is an id, `"dw3:digimon/<id>"` or `"<mod>:<key>"` (a new enemy; a partner form is rejected) and the four values
  are required (`level` 1-99, `hp` 1-32767, `mp` 0-32767, `strength` 1-32767). Optional: `ambushChance` 0-255 (0), `kind` 0-255 (1) and the 12 blocks above (`poison` ... `runAway`,
  0 or 1, default 0). `rewards` (`money` 0-9999999, `exp` 0-999999, `digimonExp` 0-9999999; default 0) is the row the report reads for that battle. The loot comes from the
  enemy (`battle_loot`), not from the encounter. The numbers come after the disc's 335 (335, 336...) in the order of the mods and of the file; in a stage's `battles` and in the
  `start-battle` action the encounter is written by name (`"encounter": "mymod:meadow"`) or by that number. A bad row is logged (`tables: <file> row "<name>" rejected: <why>`) and does
  not exist: a stage that names it is rejected with "there is no field encounter named". Up to 1024 new rows across all mods. The enemy condition 12 (`BATTLE_SETUP.battle == arg`)
  compares with that number, which depends on the order of the mods: do not use it with new encounters.
- `field_enemies` takes in `fighter` an enemy id, `"dw3:digimon/<id>"` or the name of an enemy of a content mod
  (`"<mod>:<key>"`); a partner form is rejected.
- The card tables are applied each time the card shop or the duel is entered. Each reference carries the English card (or item) name in
  `"labels"`. `card_prices` only covers the 44 priced cards of the disc (the shop charges 1 for a card with no row) and its key is the card:
  a row cannot be made to price another card. The price of a mod's card is not a row: it is the `price` field of its `card.json` (6.10).
  Wherever a card table asks for a card (`card_shop_stock.cards`, `card_pack_slots.card1`..`card16`, `default_deck.card`, `card_opponents.card1`..`card40` and `counter1`..`counter20`)
  it accepts an original id 1-314 or `"<mod>:<name>"` (a card a content mod adds, 6.10); a name no installed mod defines is rejected with a log line
  (`no installed mod defines "..."`) and the field stays as it was. The ids 315 and 316 are not cards and are rejected. `card_shop_stock.cards` is a list (replace `[1, 2]` or `{"add": [...], "remove": [...]}`) of up
  to 29 cards, six per page. Each pack slot draws from 16 cards at random: listing a card twice makes it more likely.
- `card_effects`: `step1`..`step40` are duel step numbers (1-174) and a 0 ends the script, so there must be a 0 among the 40 (otherwise the
  whole row is rejected). Changing a script is advanced: some steps wait for the target `target` gives and others skip the steps after
  them; the port only checks ranges. `default_deck.card` is the card id **minus one**, as the game stores it.
- `card_opponents` comes from the disc file `CARD_NPC.BIN`, not from memory: the patch is applied to the bytes each time the game reads the file (when a duel starts),
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
- The battles of each stage (`stageBattles`) are not a table but a section of the stage patch: `stages/s<nnn>/stage.json` with `battles` (6.11, `docs/stages.md` section 10, Spanish).
- The placements of `content/encounters.json` (section 6.9) are applied after these tables.

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

- **Name:** `<mod folder>:<key>` (here `mimod:test-gadget`). The key uses lowercase letters, digits, `-` and `_`.
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
- **Selling it:** `tables/shop_stock.json` accepts the name in the list: `{ "items": { "add": ["mimod:test-gadget"] } }`
  (§6.5). A name or number no installed mod defines is rejected. The buy price comes from the item (`price`);
  another mod can change it with an `item_prices` row keyed by the name (`"mimod:test-gadget": { "price": 77 }`): it goes on
  the item's record, in the usual mod order; the declaration keeps its price as the default. An unknown name rejects that row with a
  log line and the rest of the patch applies. The `"*"` row only touches the original items.
- **Removing the mod:** the save stores the item by name. Without the mod it is dropped on load with `[saves] item "..." is
  not defined by any installed mod; dropped from the loaded save` and the game carries on. Original items are untouched.
- **Loot:** `tables/battle_loot.json` accepts the name in `item` (6.5); the enemy drops it, or it is stolen, like any item.
- **Limit:** the game's item lists have room for about 404 different kinds in the bag at once.


#### Readable view of a save (read-only)

On every save the port writes `view/slot<N>.json` (format `dw3save-view-v1`) in the saves folder (`saves/default/`): slot, port version,
active mods, name, money, play time, place and progress, party, partners and the bag as `{id, name, count}` (mod items by
their full name). It is for looking at or comparing saves. **It is read-only:** the game never reads it, editing it
changes nothing (its `notes` field says so) and it is rewritten on the next save.

### 6.8 New content: techniques

A mod with `"kind": "content"` can also declare **new techniques** in `content/techs.json` (format `techs-v1`) and make an existing
Digimon learn them. It is the mod's own content: the original techniques are not touched.
A technique's **name** stays in English in every language (`"es-419"` the same as `"eng"`), like the original techniques' names
(Pepper Breath, Grand Wave); its **description** is translated.

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

- **Name and number:** like items: `<mod folder>:<key>` (here `mimod:spark-strike`). The port hands out numbers from 444 in
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


### 6.9 Changing existing Digimon and adding new Digimon

What a Digimon is in the game (three tables, ids, model, names, caps): [`docs/digimon-anatomy.md`](../digimon-anatomy.md) (in Spanish).

A Digimon lives in a folder of the mod, `digimon/<name>/` (ADR-016), with its `digimon.json` (format `digimon-digimon-v1`) and, if it has a model of its
own, its `model/` (8.2). The folder `d<nnn>` (the fighter id on three digits, the same name as the folder the port exports to `data/digimon/d<nnn>/`)
**changes a Digimon that already exists** (partner forms and enemies); any other folder name is a **new Digimon** (enemy or partner form, below). The
**encounters** where enemies appear go in `content/encounters.json`. Enemies do not change the save format (nothing saved holds an enemy id); new partner
forms and techniques of mods are saved by name (6.8 and "New partner forms").

```json
// mymod/digimon/d383/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "d383",
  "partner": { "hp": 180, "stats": { "strength": 60 },
               "skills": { "6": { "tech": "mymod:spark", "level": 1 } } }
}
// mymod/digimon/d004/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "d004",
  "enemy": { "stats": { "defense": 200 }, "item": "dw3:item/45", "itemChance": 1023 }
}
```

- **Name:** the folder `d<nnn>` is fighter `<nnn>` (`dw3:digimon/<nnn>`; ids 3 to 470, so `d003` to `d470`); `name` is optional and, when there, must be the folder (`$schema` and `notes` are ignored). A `d<nnn>` folder out of range, or with another `name`, is rejected with its line. The id is the fighter's: the `DigimonData` id for partner forms and
  the battle table id for enemies. The two id spaces never overlap, so the id decides the facet (`partner` or `enemy`); using the
  other one is rejected with its line. The reference with all 248 fighters (55 partner forms, 3 of them marked `"placeholder": true`
  because they are empty rows no Digimon uses and cannot be changed, and 193 enemies), their English name (read from your disc,
  never stored in the repo) and the editable fields with their original values is at `<pref>/data/content/digimon.json`
  (`content-ref-v1`, same manifest and ".new" rule as `items.json` and `techs.json`; it is only the list of names, the port does not read it).
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
  also accepts `"dw3:digimon/<id>"` as a key; if the `digimon.json` of `d<nnn>` and `learnedBy` give the same slot, the `digimon.json` wins.
- **Coexistence with `battle_loot`:** the content of the `d<nnn>/digimon.json` files is applied first and the `battle_loot` table (6.5) goes on top, so for an
  enemy's `item` and `itemChance` `battle_loot` wins; whatever else the `digimon.json` changes stays.
- **Only the `digimon.json` of `d<nnn>`:** its `model/` is never read, because no mod can replace an original's model (a new Digimon's, yes: 8.2; to start from an original one, `"from": "dw3:model/<id>"`). `--validate-mod` warns about a `d<nnn>/model/`.
- **Removing the mod:** the game goes back to its values (nothing is stored in the save).
- **Comparing balance:** `python3 -I tools/balance_compare.py "<disc>.bin" <mod folder> [--level N]` prints, for each partner form the mod defines (a new form, or
  the `partner` of a `d<nnn>`), HP, MP, HP and MP growth per level, the six stats and each stat's growth column, next to the first form of the eight original partners; then a
  projection to level N (30 by default: what a partner that keeps that form's growth from level 2 to N would have, with average rolls, by the rules of `STFGTREP_raiseStats`) and
  each mod form's difference in percent against the mean of the eight. The originals are read from your disc when it runs and everything goes to stdout: do not store or upload
  the output (9). The projection is for comparing, not predicting: a real game changes form (each form has its own growth) and the rolls vary. A form whose `base` is not a partner
  form (an enemy) is skipped with one line.


#### New enemies (`digimon/<key>/`)

A folder `digimon/<key>/` with a `digimon.json` declares a **new Digimon** (the key is a-z, 0-9, `-`, `_`; `d<nnn>` is reserved for the originals). The enemy is called
`"<mod folder>:<key>"` and copies the battle table row of an original enemy (`"base"`):

```json
// mymod/digimon/sproutmon/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "sproutmon",
  "base": "dw3:digimon/4",
  "model": "dw3:digimon/25",
  "title": { "eng": "Sproutmon", "es-419": "Brotemon" },
  "stats": { "attack": 120, "defense": 40 },
  "item": "dw3:item/45", "itemChance": 1023
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
- **`"model": "<mod folder>:<key>"` and the folder's `model/` (a model with your own art):** a neutral source (glTF + PNG + `model.json`) in
  `digimon/<key>/model/`; if the `digimon.json` has **no** `"model"`, the `model/` of its own folder is its model (the normal case: one folder, one Digimon,
  its model). With `"model"` the field wins: `"<mod folder>:<other key>"` uses the `model/` of another folder (of the same or another mod) and an original uses none.
  The port converts it on load and the Digimon gets its own row with that model, the donor's effects, and the template, height and
  distance that the `model.json` says. Full guide: 8.2. `"dw3:model/<id>"` takes the model of an original fighter.
- **Editable fields:** the `enemy` ones above (`stats`, `resistances`, `statusResists`, `family`, `techs`, `item`, `itemChance`),
  same rules. A technique needs its animation (`script`) in the effects archive of the chosen **model**; otherwise that slot keeps the
  `base`'s technique and the log says so (`its model ... has no script slot N`).
- **`title` and `description`:** per language (`eng` required; `es-419` -> `spa` -> `eng`), as for items and techniques; the text goes in `title` because `name` is the folder's name. The name
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
- A new Digimon cannot be changed with a `d<nnn>` folder (its id depends on the mods): its fields go in its own `digimon.json`.
- A new enemy does not appear by itself: place it in an encounter (next section).

#### New partner forms (`digimon/<key>/` with a partner `base`)

When `"base"` is an **original partner form** (`"dw3:digimon/383"`; not one of the 3 empty rows) the entry is a new partner form: a
new `DigimonData` row (a copy of the `base`'s) that the game can look up by id (`GET_DIGIMON`), name and take to battle:

```json
// mymod/digimon/emberkote/digimon.json
{
  "format": "digimon-digimon-v1",
  "name": "emberkote",
  "base": "dw3:digimon/383",
  "model": "dw3:digimon/384",
  "title": { "eng": "Embermon", "es-419": "Brasamon" },
  "description": { "eng": "A partner form that burns bright." },
  "hp": 220, "stats": { "strength": 90, "speed": 60 },
  "skills": { "1": { "tech": "mymod:spark", "level": 1 } },
  "evolution": { "partner": 0, "needs": [ { "digimon": "dw3:digimon/383", "level": 1 } ], "stat": "level", "value": 3 }
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
  row + 1 in 16 bits (the port holds them apart from the disc's `u8`), so a new form can be their value and rows go past 254 (up to
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
  the partner's battle form (the entry may be above 43: see the next item). `@<level>` is the **form's** level; the partner's own is set by
  `DW3_SEED_PARTNER_LEVEL="<partner>=<level>,..."` (2..98): it raises the stats level by level with the game's growth and leaves exactly the exp of the next level, so one won battle levels it up and teaches the form that waits
  (once per run). `pc/review.sh betamon|seadramon|betamonpesca` use it to try `betamon-pack` with the real thresholds.
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

#### Where they appear (`content/encounters.json`)

`content/encounters.json` (format `encounters-v1`) places enemies (original or new, by name) in the game's **encounters** (the 335 rows of `FIELDSTG_encounters`;
`BATTLE_SETUP.battle` is that index, which also indexes `battle_rewards`):

```json
{
  "format": "encounters-v1",
  "encounters": {
    "34": { "2": { "enemy": "mymod:sproutmon", "level": 3, "hp": 60, "mp": 10, "strength": 16 } },
    "35": { "1": { "enemy": "dw3:digimon/4", "level": 5, "hp": 90 } }
  }
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

### 6.9b New partners and starter teams

A **new** partner (a ninth, tenth... beside the game's eight) is declared by a content mod with one folder per partner, `partners/<key>/`: its `partner.json` (format
`partners-partner-v1`, schema `schema/partner.schema.json`) is the registry record and its art goes beside it (6.2f). The game walks every mod's `partners/` once at startup;
a folder with no `partner.json` is not a partner (it may be art only).
A new **starter team** stays in `content/teams.json` (`teams-v1`, `schema/teams.schema.json`). Design: `docs/decisions/ADR-015-partner-registry.md` and, for the folder,
`ADR-016-asset-layout.md` (Spanish). The partner's first form is a form of a `digimon/<key>/` folder (6.9) or an original one; its model, techniques and form name are declared there.
Example files are in the Spanish guide (6.9b); the fields are:

- **Header.** `"format": "partners-partner-v1"` (required) and `"name"` (optional; when present it is the folder's name, the key; `"$schema"` and `"notes"` are ignored). The other members
  are the partner's fields, in the same object. The folder of an **original** (`agumon`, `gabumon`...: the lower-case English name of `data/partners/<name>/`) is **not** a new partner:
  only its art is read there (6.2f) and its `partner.json`, if any, is not. The `partner.json` the game exports for an original (`<pref>/data/partners/<name>/partner.json`, the same format
  name) is descriptive and never read: it carries `disc`, `portrait`, `icon`, `field`, `training` and a plain-text `title`, which a mod's `partner.json` does not use (if you copy it to make a
  new one, change the folder's key and rewrite `title` as a per-language object). No file ever patched an original partner (`base` only gives art and equipment): a mod cannot change its record from here.
- **Names and numbers.** `"<mod folder>:<key>"` (key: a-z, 0-9, `-`, `_`, up to 48; its folder's name). Its number in the game is **8 plus its place in the alphabetical order**
  of all declared partners (a team: **3 plus** its place); the same mods always give the same numbers, which are never saved (a save keeps the name,
  ADR-014). The original eight are `dw3:partner/<0..7>`. Safety limit: 256 partners.
- **`base`** (required): an original partner (`0..7` or `"dw3:partner/<n>"`): its 2D art stands in for the new one until it has its own, and its bit of every item's
  `partners` mask is the default equipment eligibility. **`firstForm`** (required): the form it starts as (an original `"dw3:digimon/<id>"` or a form of a `digimon/<key>/` folder);
  HP, MP, stats, resistances and growth come from that row. A form that does not resolve rejects the whole partner.
- **`title`** (required, `eng`) and **`description`** (optional), per language: the partner's name in the game (battle HUD, status, lab) and the starter screen's text
  (`\n` between lines). They are extra entries of the `NAMEET` text file (31 entries on the disc).
- **`equip`**: `like` (the original partner whose mask bit it reads), `allow` / `deny` (items by number, `"dw3:item/<id>"` or `"<mod>:<key>"`; deny wins). **`equipSet`**:
  four items worn together and six bonuses (-999..999). The items' `partners` mask (0-255, §6.5b) only has a bit for each original partner.
- **Its own art is in the same folder, `mods/<mod>/partners/<key>/`**, beside `partner.json` (`<key>` is what follows the colon in `"<mod>:<key>"`; there are **no** `art`, `icon` or `sprite` fields any more: such a
  field is refused as unknown). The files are looked up through the asset layers (mods from the last to the first, then `data/`), one by one, and a missing one uses `base`'s.
  `portrait.png`: an indexed PNG **32 high and 32 x N wide** (N = 1..8 cells, up to 16 colours, index 0 transparent; with more colours they are reduced to 16 with a warning): the portrait on every
  screen that shows partners (starter team and confirmation, naming, lab, field menu, gym, battle report, memory card details); the animation walks the cells back and forth. `icon.png`: an indexed PNG of
  **exactly 44x28**, up to 16 colours, index 0 transparent: the icon of the memory card list. `field/`: a `sprite-v1` sprite (6.3, `docs/stages.md` 11; `sprite.json` + one PNG per frame): the
  character the partner **follows the player as in the field**. An invalid PNG keeps `base`'s with a `[partners]` line. `training/<training>/` is the partner's gym (6.2f, "A partner's gym"). Follower sprite template (measured in the decomp, `docs/formats.md` 4.12): **sets** `1` (stand), `5` (run) and `6` (stop, plays
  once) are the only ones a follower asks for (a missing one falls back to the first in the list); **directions** are 5 (0 down, 1 down-left, 2 left, 3 up-left, 4 up; the
  right-hand ones are mirrored by the game), and a set may name a single animation for all 5. **Frames** of any size up to 256x224 (16 colours, index 0 transparent); the port
  gives the sprite a band slot sized for its largest frame (the game's own followers use 32 px wide and at most 64 high). Each step's `x`, `y` is the frame's offset from
  the actor's feet (for example `-width/2`, `-height`). Optional `actor_width` (8..256). Minimal example: a `sprite.json` with 2 frames, 3 animations (stand, looping run,
  stop with `"loop": false`) and sets `1`, `5`, `6`.
- **`evolution`**: the forms it can learn after a battle: `digimon`, `needs` (up to 2 `{digimon, level}`), `stat`, `value`; same rules as a Digimon's `"evolution"` in its `digimon.json`.
  A new partner has no original 44-entry list, only its own.
- **Teams**: `partners` are exactly **three** different partners by name (originals allowed); `base` (`0..2`) is optional; `name`/`description` are the team's title and text.
  The selection screen shows **any number** of teams: the three originals (A, B, C) and the mods' (D, E, ..., in the alphabetical order of their full name
  `<mod>:<key>`). It shows up to six rows at a time (the three originals alone look as they always did); only with more than six teams does the column scroll with up/down and a bar in the left margin says where it is. A new
  partner's portrait is its `base`'s until it has its own art. In tests `DW3_STARTER_SET=<number>` or `DW3_STARTER_SET=<mod>:<key>` picks the team `setParty` gives where it
  used to give 0 (a `warp:` or `mode:` with no game yet). `GameState.partySet` keeps the team the game began with; only the event conditions compare it (`partner` op 0,
  against 0..2 on the disc). A save keeps a mod's team **by name** (a `TEAM` section): loaded with other mods it is still the same team; if its mod is gone it reads as "no
  team" (`-1`, which no disc condition equals) and the save still loads.
- **When.** Mods are read **once at startup** (after the `digimon/` folders and `items.json`, before any save) and served from memory. A new partner gets a new game's starting data
  (also when a loaded save did not hold it). Removing the mod: the save still loads, the partner is dropped with `[saves]` lines (ADR-014).
- **Unlocking a partner with a mission.** A partner that is not in the chosen starter team is obtained like the originals: a talk (or an event) with the "unlock" action and the "still locked" condition.
  They are two rows of `tables/special_conditions.json` whose `arg` is the partner's **name** (`"<mod>:<key>"`, or `"dw3:partner/<n>"` for an original), not its number (a new partner's number depends on which mods
  are installed and moves). The port resolves it once, when the table is read; a name no mod defines rejects the field (`row "<row>" field "arg" rejected: no installed mod defines "<name>"`, which `--validate-mod`
  shows) and the row never runs with a substitute value: an original row keeps its `arg`, and a mod's new row is not created (its name stays undefined, like an unknown condition).
  ```json
  { "format": "special_conditions-v1", "rows": {
      "mymod:bee-free": { "primitive": 1, "op": 6, "arg": "mymod:bee" },
      "mymod:give-bee": { "primitive": 1, "op": 3, "arg": "mymod:bee" } } }
  ```
  and in the stage's `stage.json`, the talk of the character who hands it over:
  `{"text": "mymod:bee-arrives", "conditions": [["special", "mymod:bee-free", 1]], "actions": [["special", "mymod:give-bee", 1]]}` followed by a talk with no conditions for when the player already has it.
  The unlocked state is saved **by name** with the rest of the partner's data (`PTNR` section, ADR-014): adding or removing mods before loading does not change who was unlocked. Op 9 ("all eight are
  unlocked") still counts only the eight originals, because it is the disc's story logic (the extra chapter); op 7 ("every unlocked partner is level 45") does look at unlocked mod partners.
- **Validation.** One `[partners]`/`[teams]` log line per rejection (`--validate-mod <folder>` shows them, from the game's own loader): a `partner.json` that is not JSON, has another `format` or a `name` that is not
  the folder's, an invalid key, a `base` that is not an original, a `title` with no `eng`, a `firstForm` that does not resolve, an unknown field, an item or form that does not exist. It also warns about a
  `partner.json` in a mod that is not `"kind": "content"` and a `partner.json` inside an original's folder (`never read`). The rest of `partners/<key>/` is checked too: the art with the game's own readers (`partners/<key>/portrait.png rejected: ...`, `icon.png`, `field rejected: frame N: ...`,
  `training/<training>/training.json rejected: ...`, one line per file, with the path inside the mod), plus warnings for a file or folder nobody reads (`partners/<key>/notes.txt is ignored (a partner's folder holds partner.json, portrait.png, icon.png, field/ and training/)`, with a "did you mean" when it looks like one of those names),
  a new partner's `field/` without `sprite.json`, a `training/<name>` that is no training or has no `training.json`, a folder that is neither an original's nor declared by a `partner.json` of an active content mod (`partners/<key> is ignored: ...`) and a shared folder:
  `partners/<key>/ is shared by mods/b, mods/a, data/ (edited): mods/b wins (...)`, one line per key (on the report of the mod that wins; with `--validate-mod` of a single mod, on the report of every mod that shares it). A bad file in the losing layer is not read, so it does not show up: the game skips it.
  Sharing is not an error (an assets mod may give art to another mod's partner), but those files shadow each other file by file.
- **Art and model:** a new partner reuses its `base`'s 2D art until it brings its own (6.2f); its own 3D model is 8.2c and its balance is compared with `tools/balance_compare.py` (6.9).

#### Making a partner pack (example: `pc/examples/mods/betamon-pack`)

A complete pack is a content mod with these pieces; `betamon-pack` has all of them and is the template to copy (change the key of each folder and the names):

1. **The forms** (`digimon/<form>/digimon.json`, one folder per form): `"base"` is an original partner form (it gives HP, stats, techniques and growth by default) and `"model"` as an object takes the model of a disc enemy **by reference**, copying
   nothing from the disc: `{"from": "dw3:digimon/148", "model": "dw3:digimon/10", "motions": "dw3:digimon/10", "effects": "dw3:digimon/374"}`. `"from"` is the partner template (12 cameras): pick one of a **similar size** to the model (a big serpent with a small
   Digimon's template sits on top of the camera; `betamon-pack` uses Renamon for Betamon and Imperialdramon for Seadramon and MegaSeadramon). The numbers come from `<pref>/data/content/digimon.json` (the list the game writes from your disc): Betamon is `dw3:digimon/4`,
   Seadramon `10` and MegaSeadramon `61`. With a borrowed model the motion slots are the enemy's, and the extra ones a partner asks for (13, 20, 32) are not in that file: an empty slot makes the game read garbage (`getArchiveEntry` does not check bounds). So
   `--validate-mod` warns about each slot the battle plays that the file leaves empty (the same `motion slot N (...) has no animation` line as a baked model), and the `"model"` object accepts `"fill": <slot 0..61>`: the port builds a copy of the
   motions file (a virtual file, the disc is untouched) where **every** empty slot plays that donor slot; `betamon-pack` uses `"fill": 14` (the generic attack). Without `"fill"` the empty slots stay and the warning stays; if the `"fill"` slot is also empty in the donor it is rejected with one line.
   If you want animations of your own for those slots, bake a model of your own (8.2c). A partner form with motions by reference always gets its own copy of the file, where the motions that the enemy leaves "hanging"
   (the last step is `0x8000`, which loops the tail for ever: the attack `14` of Betamon, Seadramon and MegaSeadramon) end: a partner's script waits for each motion to finish (`battle_script.c:168`) and hung at the first attack.
   The idle (0, 1) and the entrance and victory pose (12) keep holding.
   **The effects: the donor.** A technique's animation is the script in slot `script` of the effects archive **of the fighter that casts it**, not of the technique; the visual effects and sounds are global. An enemy's effects archives only
   have slots 0-5 and their scripts use enemy-side coordinates (that is why Seadramon's attack with its own enemy archive animated against itself, and a technique of another slot had no animation). So `betamon-pack` takes `"effects"` from a **related
   original partner form**, the donor, and gives it the techniques that donor's archive animates (the donor's own are preferred, made for that archive): Betamon (starting stage) with Renamon `373`, Seadramon with Kyubimon `374` and MegaSeadramon
   with Taomon `375` (an ice and lightning chain; the model, the motions and the water techniques stay the enemy's and the series'). The enemy's own techniques (`225`, `252`, `291`, all script 5) have no name and are not fit as they are for a partner
   (they cost 0 MP): `content/techs.json` clones them with a name, cost and power of their own (`base` is the original) and the form's script 5 (the normal attack's and these techniques') is the **enemy's own**, mirrored to the partner side with `"scripts"` (next paragraph); the starting stage's signature (`skills.6`, `spark-bubble`) is a clone of the enemy's technique 225.
   **Scripts of its own: `"scripts"`.** The `"model"` object accepts `"scripts": {"<slot 0..17>": {"from": "dw3:digimon/<id>", "slot": <0..17>}}`: that slot of the form's effects archive (the `"effects"` donor's) becomes another original fighter's script.
   The port builds a copy of the effects file (a virtual file, the disc is untouched) with those slots replaced; the rest stay the donor's. When the `"from"` fighter is of the **other side** (an enemy for a partner form, or the reverse), the script is converted to the
   form's side: the model type, the positions, the turns and the cameras by id are mirrored, and positions on the caster's half follow its place (height and distance of the form's row); the design and the commands are in `docs/formats.md` section 4.6 point 5c. The same side is copied as it is.
   `betamon-pack` takes script 5 of Betamon (`4`), Seadramon (`10`) and MegaSeadramon (`61`) this way: Seadramon's water bubble and Betamon's and MegaSeadramon's lightning leave the form toward the enemy with the game's own choreography and cameras, in the normal attack and in the cloned
   technique (`spark-bubble`, `tidal-arrow`, `thunder-justice`, all script 5). `--validate-mod` gives one error line per entry that cannot be honored, and that slot stays the donor's: a slot the row does not have (`unknown slot`), an empty slot in the source fighter
   (`has no script in slot N`) and a command that cannot be mirrored (a camera with a view of its own, or camera 4). Scripts 6 and 8, which the code uses **instead of 5** with an action effect, a weapon of the foe's family or the triple hit (`techs.c:50-103`), are not touched (the pack leaves
   them the donor's: in those rare cases the form animates like the donor); to keep the choreography there too, repeat the entry with the slots 6 and 8 (`"6": {"from": ..., "slot": 5}`), knowing that an enemy's script does not read the action's effect.
   A starting stage only uses its slot 6 (`tech_menu.c`: the form that is the partner's base row lists only `skills[6]`).
2. **The partner** (`partners/<key>/partner.json`): `"base"` (an original partner: its art and its equipment bit until you have your own), `"firstForm"` (the first form, above), `title`/`description` per language and `"evolution"`: one entry per form
   it learns, with its requirements (`needs`: the previous form at a given form level, and `stat`/`value`: for example `"level"` >= 10, the partner's level). It is learned **after a battle** in which the partner earned experience (the report panel says so),
   one form per report. The values of `betamon-pack` are of the order of the original list's (which asks for partner level 5, 15, 20 or 40 and form level 5 to 99).
3. **The art** (`partners/<key>/portrait.png`, `icon.png`, `field/`, `training/`: 6.2f and 6.9b). `betamon-pack/tools/make_art.py` generates it from SpriteKiln models (an art tool only: the port does not depend on it; see "2D art with SpriteKiln" below); whoever prefers to draw by hand overwrites the PNGs one by one without touching the JSON.
4. **The starter team** (`content/teams.json`): three partners by name; it shows up as one more row of the choice screen.
5. **How to get it without choosing that team** (`stages/s330/stage.json`, `tables/special_conditions.json`, `tables/field_encounters.json`, `content/items.json`): a **DDNA quest**, shaped like the game's own (`wstag375`/`wstag450` Veemon, `wstag405` Agumon, `wstag420` Guilmon, `wstag330` Patamon and Renamon; the P16 record of the pack has the citations). The original pattern: (a) a giver character shows up in a **story window** (a progress-range row, primitive 3) and only asks for the quest while the **sum of the party's levels** is inside a window (primitive 1, op 5: reaches `arg * 15 + 30`; the originals use 30-59, 45-74 or 60-89; outside it a "I'm busy" line plays); (b) asking sets a flag; (c) a Digimon appears while the flag is set and the DDNA is not yours, and talking to it starts an **event battle** (`kind` 2, no running away) against its most advanced stage; (d) winning gives the DDNA (a `content/items.json` item whose `base` is an original DDNA, `dw3:item/35`, with the item-received signal); (e) the giver, seeing the DDNA with the partner still locked, unlocks it (the op 3 row) **without taking the DDNA**. In the mod: the fisher is `fisher` and the Digimon `river-digimon` (its own `sprite`), the windows are three rows (`story-window`, `levels-min`, `levels-max`), the flag is `["mod-flag", "betamon-pack:quest", 1]`, the battle is `["start-battle", "betamon-pack:megaseadramon", 1, <music>]` and the result is read with `["mod-flag", "dw3:battle-won/betamon-pack/megaseadramon", 1]`. Two differences from the original: actors are evaluated when the stage is made, so the request line ends with `["warp", 330, x, y, dir]` to the same stage for the Digimon to show up (the original changes stage); and there is no scene before the battle nor an event that hands the DDNA over on return (a second talk with the Digimon gives it).
6. **Balance:** `python3 -I tools/balance_compare.py "<disc>.bin" pc/examples/mods/betamon-pack --level 30` prints each form against the eight originals; in `betamon-pack` every stat of every form is inside the range of the originals
   (adjust `hp`, `stats` and `statGrowth` in each `digimon.json` until it is).
7. **Test:** `boot "<disc>.bin" --validate-mod <mod>` (0 errors); to see the form in battle without playing up to the evolution, `DW3_STARTER_SET=<mod>:<team>`, `DW3_SEED_PARTNER="<n>.0=<mod>:<form>@20/0"` and `DW3_BATTLE_ENEMY=63` (6.9, "What a new form does today"). To play it: `pc/review.sh betamon` (first evolution), `seadramon` (second) and `betamonpesca` (the fisher), with its own copy of the pack and separate saves.

#### 2D art with SpriteKiln (optional)

[SpriteKiln](https://github.com/LucasJRamos/spritekiln) (MIT) is a local Python tool (numpy, Pillow and PyYAML; no GPU) that describes a character as a YAML model and renders it to pixel art in several directions and animations. It is a convenient way to make the 2D art of a partner (portrait, icon, field sprite, gym) or of a stage character without drawing frame by frame. **It is optional**: the port only reads PNGs and JSON; pixel art drawn by hand or with any other program works just the same, and SpriteKiln is never needed to play or to validate a mod.

Install (once, pinned to the version the example uses, in a separate environment):

```
python3 -m venv ~/.cache/spritekiln-venv
~/.cache/spritekiln-venv/bin/pip install spritekiln==0.3.0
```

Workflow, with `betamon-pack` as the example:

1. The model is a YAML file in `tools/specs/` (`betamon.yaml`: body, head, legs and crest as shapes, ramps per material, animations by channels). The other YAML files in that folder extend it and only change the camera, the size and the poses: `betamon_portrait.yaml`, `betamon_icon.yaml`, `betamon_field.yaml`, `betamon_train.yaml` and `megaseadramon_river.yaml` (the river stage sprite).
2. `~/.cache/spritekiln-venv/bin/python pc/examples/mods/betamon-pack/tools/make_art.py` renders, trims every frame, merges the colours down to 15 plus transparent and writes the indexed PNGs and the JSON (`sprite-v1`, `partners-training-v1`) at the exact sizes the game asks for. It is deterministic: two runs give the same bytes (`tools/test_make_art.py`).
3. To change the art, edit the YAML (or the camera of that view) and run it again; `--validate-mod` confirms sizes and colours.

What we learned doing it: (a) the field sprite must have a footprint like the original followers' (about 20-26 x 23-31 px): lower it with `camera.scale` in the field YAML, not in the model; (b) parts and channels with an `_L` suffix mirror to `_R` by themselves; (c) channel expressions are Python-like (`sin(p)`, `abs(...)`, comparisons such as `(f==1)`), not C-like: no `||`; (d) a portrait is cells 32 px high and the icon is 44x28: `make_art.py` fails with a message when the model does not fit, then raise `camera.scale`; (e) `spritekiln validate <yaml>` checks a model before rendering. Do not run `spritekiln update` (it changes the pinned version).

### 6.10 New content: cards

A `"kind": "content"` mod adds **new cards** with one folder per card, `cards/<key>/`: a `card.json` (format `cards-card-v1`) and, for art of its own, a `face.png`. The game
walks every mod's `cards/` once at startup; a folder with no `card.json` is not a card. The card is named `<mod folder>:<key>`; the port gives it an id from 317 up, in the alphabetical order of the full name, and that number is never saved
(a save stores the name: section `EXTK`).

```json
// mymod/cards/test-card/card.json (+ mymod/cards/test-card/face.png, optional)
{
  "format": "cards-card-v1",
  "base": "dw3:card/120",
  "ap": 45, "hp": 40, "rank": 100, "price": 120,
  "title": { "eng": "Test Card", "es-419": "Carta de ejemplo" },
  "description": { "eng": "A new card from a mod." }
}
```

- Header: `"format": "cards-card-v1"` (required) and `"name"` (optional; when present it is the folder's name, the key; `"$schema"` and `"notes"` are ignored). The other members
  are the card's fields, in the same object. A `c<nnn>` folder is **not** a new card (see Art). The `card.json` the game exports for an original card
  (`<pref>/data/cards/c<nnn>/card.json`, the same format name) is descriptive and never read: it carries `id`, `number` and `disc`, which a mod's `card.json` does not use.
  A mod cannot change an original card's record from `cards/c<nnn>/card.json` (the game has no such patch; prices, shops, packs and effects are changed with the tables, 6.5b).
- `"base"` (required): an original card (1..314), by name (`"dw3:card/120"`) or number; its record (colour, AP, HP, kind, points, level, rank and
  combo card) and, for cards 1..60 (the option cards), its effect are copied. The original names are in `<pref>/data/content/cards.json`
  (`content-ref-v1`, written by the game from your disc; it stays because the exported `card.json` carries no names or records).
- Fields: `color` 1..6, `ap` and `hp` 0..99, `kind` 0..16 (1..14 option, 16 Digimon), `points` 0..99, `level` 0..9, `rank` 0..999, `comboCard` 0..314. A value out of
  range is rejected with a log line and the card keeps its base's.
- `"effect"` (option cards): `{ "like": 1..60, "condition", "playCondition", "message", "target": 0..255, "script": [steps 1..174, up to 39] }`. `like` goes first and
  the other fields modify it; an invalid `script` is rejected whole. The game has 175 effect steps; new steps cannot be created.
- `"title"` (the card's name, with `eng`) and `"description"` (the effect text, in `CARDST`) per language, like an item's name and description. It is called `title`
  because `name` is the header (the folder).
- **Art:** `<mod>/cards/<key>/face.png` beside its `card.json`, an indexed 32x32 PNG with up to 256 colours (the transparent index stays transparent, opaque black is written 0x8000); without a file the card
  uses its base's art. A `c<nnn>` folder is an original card's (6.2b), never a new card: only its `face.png` is read there, and its `card.json`, if any, is not.
  `--validate-mod` checks every `card.json` with the game's own loader (header, `name`, required and unknown fields, valid key, ranges and the size of `face.png`: one line per case)
  and warns about a `cards/` folder with no `card.json` (that is not `c<nnn>`), a `card.json` in a mod that is not `"kind": "content"`, a `card.json` inside `c<nnn>`, a `c<nnn>` outside
  `c001`-`c316`, a `c<nnn>` with no `face.png` and a stray file in `cards/`.
- **Where it shows:** the album grows (ids 315 and 316 appear as empty slots between the original and the new cards), the deck editor lists them once you own them, and the
  battle draws and plays them with their record and effect. Cards in decks and in the collection are saved by name; removing the mod drops the card when the save loads (line
  `[saves] card "..." is not defined by any installed mod`) and its place in a deck goes back to the starter deck's card.
- **Price and shops:** `"price"` (0-32767, 1 when missing) is what the card shop charges for one copy. To sell it, a table of the same mod lists it by name:
  `tables/card_shop_stock.json` (`{"49": {"cards": {"add": ["my-mod:my-card"]}}}`), and to draw it from packs `tables/card_pack_slots.json` (`{"910": {"card1": "my-mod:my-card"}}`);
  `tables/card_opponents.json` and `tables/default_deck.json` put it in an opponent's deck or the default deck. The shop draws the card with its art, name and price.
- **Limits:** up to 707 new cards (ids < 1024). The original ids 315 and 316 are not playable cards: no table accepts them and the album shows them as two empty slots
  (the same as any unseen card), so the album without mods looks identical and with mods the new cards start at slot 317.
- The validator (`--validate-mod`) checks every `card.json` with the same rules and `docs/modding/schema/card.schema.json` gives the editor completion.

### 6.11 Characters and talks of a stage

`stages/s<nnn>/stage.json` (`s<nnn>` is the stage, the same path as the reference `stage-v1` copy in `data/stages/s<nnn>/stage.json`; the patch keeps its `stage-patch-v1` header) changes, adds or removes the characters of a field stage and what each one says. The format is
`stage-patch-v1`; the details (fields, order, conflicts, what is skipped) are in `docs/stages.md` section 7 (Spanish) and the schema is `docs/modding/schema/stage-patch.schema.json`. Example: `pc/examples/mods/training-field`.

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

- A character is named by its `index` in the stage's list (see it in `data/stages/s<nnn>/stage.json`, which the game writes from your disc) or by the key of one a patch added (`"<mod>:<key>"`).
- `patch` changes fields (`character`, `image`, `x`, `y`, `dir`, `conditions`, `talks`, `prependTalks`); `add` creates new characters (reuse a `character` and an `image` the stage already has); `remove` takes them out.
- A talk is `{"entry": N, "conditions": [...], "actions": [...]}`: `entry` is a line of the stage's own text (or `"text": "<mod>:<name>"`, a text of the mod's own, see below), the first talk whose conditions hold is the one said, and the
  last one has no conditions. Conditions and actions use the vocabulary of `docs/stages.md` section 4: `["flag", "1A:1F", 1]`, `["item", "dw3:item/18", 1]`...
- **The mod's own flags:** `["mod-flag", "<mod>:<name>", 1]` (condition: it is set; action: set it), `["mod-var", "<mod>:<name>", ">=", 3]` and `["set-var", "<mod>:<name>", 3]` for counters.
  They are saved with the game by name: removing the mod and putting it back does not lose the progress.
- **Yes/No questions:** `["ask", "<mod>:<name>"]` in a talk's `actions`: once the line has been said the field shows the game's own Yes/No panel and the variable becomes 1 (yes) or 2 (no); the character's talks are then chosen again, so the next one reads the answer with `["mod-var", ..., "==", 1]`. The question is the talk's own line. `docs/stages.md` section 17.
- **The result of a `start-battle` battle:** back on the field the port sets `dw3:battle-won/<key>` and `dw3:battle-played/<key>` (`<key>` = the encounter name with `/` instead of `:`, e.g. `dw3:battle-won/training-field/duel-trio`, or the row number in hex). A lost battle goes to the title screen, so there is nothing to read for it.
- **Texts of your own:** a talk can say `{"text": "<mod>:<name>", ...}` instead of `entry`. The words go in `text/<language>/stages.json` (format `stage-text-v1`, one folder per language; English is required and a language that lacks the
  - **The game's own speaker header:** a text can start with `{speaker-of:<FILE>:<entry>}` (FILE is one of the game's text files, e.g. `TALK04`): the port puts there the header that entry has on the player's disc, in the language being played (the game's signs say their name in `TALK04` entry 19), so the mod copies no game text. Unknown file or malformed prefix: that language's `stages.json` is rejected. `docs/stages.md` section 8.
  name shows the English one, es-419 tries Spanish first): `{"format": "stage-text-v1", "texts": {"welcome": "..."}}`. The port appends the line to the stage's text and fits it to the talk box. Details and schema: `docs/stages.md` section 8 (Spanish), `docs/modding/schema/stage-text.schema.json`.
- **Conditions of your own:** a new row in `tables/special_conditions.json` keyed `"<mod>:<name>"` (see `docs/data-tables.md` section 3.9) is used as `["special", "<mod>:<name>", 1]`; the money, progress-range and party-threshold tables are patchable too.
- **New actions:** `["give-money", 500]`, `["take-money", 500]`, `["set-progress", 12]`, `["warp", 380, 1186, 874, 3]` (stage, x, y, direction) and `["sound", "teleport"]` (`docs/stages.md` section 9). Giving or taking a mod's item was already possible: `["item", "<mod>:<name>", 1]`.
- **The item-received signal (rewards and quests).** The original plays no music and shows no extra message when it hands something over: its signal is the **icon that follows the player** (the "satellite", field task `0x16`), which
  flashes with the item-obtained sound. A talk asks for it with the action `["special", "0x13", 1]` (special condition `0x13`, which the game calls `showActorIcon`), and it goes **last** in the action list, after the item, the money and the
  flags: actions run when the text closes, so the flash comes when the reward is already given. Example (the one in `training-field`):
  `"actions": [["give-money", 300], ["item", "dw3:item/47", 1], ["set-var", "mymod:quest", 2], ["special", "0x13", 1]]`. Nothing else: do not add `["sound", ...]` or other music to signal a reward, the original does not.
  In an event script (`events`) the same flash is `["command", 813, 842, 2]` (command 842, `0x34A`, is "icon substate 3"; `["command", 813, 824, 2]` is substate 1, which also sounds, and `813, 825` is 2, which does not). There is no
  alias under another name: these are the game's own numbers. It works in a patched original stage (`stages/s330/stage.json`) and in a new one (`stages/<name>/`). The icon exists only in stages whose
  `fallback.stage` is before the new-game start (`MODE_NEW_GAME`, mode `0x2D7`: stage 330 works, stage 880 does not); without it the signal does nothing and leaves a `[field]` line in the log (it used to crash the game).
- Several mods can patch the same stage: they apply in load order and the last one wins (the log says which field was overridden). An `index` that does not exist at the current story progress is skipped with a log
  line. With no patch the stage is identical to the original. `--validate-mod` checks the file.
- **Exits, events and battles** (`docs/stages.md` section 10, Spanish): besides `actors`, the file may hold `slots`, `events` and `battles`.
  - `slots` (`patch` by `index` or `key`, `add`, `remove`): an exit has `type`, `arg`, `x`, `y`, `dir` and conditions; `"stage": 380` points the exit at that stage and `"event": "<mod>:<name>"` at an event. A new exit carries `"area": {"x", "y", "w", "h"}`, a rectangle of the map in pixels where it fires (this is the original stage's map: it is patched, not redrawn; for a map of your own use 6.11b).
  - `events` (`patch` by `id`, `add` with a key): `script` with the game's commands (`["wait", 20]`, `["talk", 0, "<mod>:<text>", 0, 4]`, `["walk", ...]`, `["leave", ...]`, always ending in `["end"]`). A talk or an exit starts it: `["run-event", "<mod>:<name>"]`. A new event has no C code.
  - `battles` (`patch`, `add`): per map area, `rate`, and per battle `encounter` (a row of `field_encounters`), `stage` (the fight stage) and `music`; look at `battles` in `data/stages/s<nnn>/stage.json`. The action `["start-battle", encounter, fight_stage, music]` starts one from a talk.

### 6.11b Your first new stage

`stages/<name>/` adds a whole field stage: a map you draw, its collision, its exits and its battles. The format is `stage-new-v1`; the detail (every field, every refusal, what was tested and what was not) is in `docs/stages.md` section 13 (Spanish) and the schema in `docs/modding/schema/stage-new.schema.json`. The stage is called `<mod>:<name>` (the folder's name) and that is how exits of other stages (`"stage": "<mod>:<name>"`) and the `warp` action name it.

```
stages/yard/
  stage.json       the header and the sections (like a stage-patch-v1, "add" only)
  background.png   the map's picture: indexed PNG (up to 256 colours), width and height multiples of 128 px, at most 255 tiles of 128 x 128
  floor.png        collision (required): the same size, one pixel per pixel of the map, the PNG's index is the value: 0 closed, 1 flat, 2-7 and 16-23 slopes
  triggers.png     (optional) exits and triggers: 0 nothing, else direction * 32 + the slot number (0 to 31) of "slots"
  areas.png        (optional) battle zones: 0 no battle, n is the n-th area of "battles"
```

```json
{
  "format": "stage-new-v1",
  "fallback": {"stage": 330, "x": 900, "y": 1000, "dir": 0},
  "start": {"x": 128, "y": 128, "dir": 0},
  "soundBank": 9, "music": {"bank": 9, "track": 0},
  "slots": {"add": [{"key": "exit", "stage": 330, "x": 900, "y": 1000, "dir": 0, "area": {"x": 100, "y": 100, "w": 56, "h": 56}}]}
}
```

- `start` is where the player appears when no exit says where; `fallback` is the game's stage a saved game that was there goes to when the mod is removed (Central Park by default).
- `banner` (optional): `{"area": "<mod>:<text>", "place": "<mod>:<text>"}`, the two lines of the area name banner the player sees when entering the stage (the big line, then the place, like "Asuka City" and "Weapons Shop"). Each is the name of a line of `text/<lang>/stages.json` (6.11 named texts), so it can be translated per language (`eng` is required, another language falls back to it). Without `banner` the stage shows none. It follows the Stage banner option (Fast or Classic) like the game's own stages. Example: `"banner": {"area": "my-mod:yard-area", "place": "my-mod:yard-place"}`.
- `dw3:card-won/<opponent>` and `dw3:card-played/<opponent>` (the opponent of a `card-battle` action in lower-case hex, `"13:0"` -> `13`) are variables the port sets the moment a card battle ends (kept in the save); read them with `mod-flag`. The game's own flags 00:10/00:11 are temporary and are cleared when the next stage starts, so a quest should read these instead (`docs/stages.md` 16).
- `actors`, `slots`, `events` and `battles` are the ones of 6.11 with `add` only (a new stage has nothing to patch). An exit can be fired by cells of `triggers.png` (the slot number is its place in `slots.add`, from 0) or by a rectangular `area`.
- A stage that is not valid is refused whole with a `[stages] <file> rejected: <reason>` line and never hangs the game; `--validate-mod` shows those lines. The maps are read once at startup.
- `battles` and the `start-battle` action accept `"encounter": "<mod>:<name>"` of a new encounter of `tables/field_encounters.json` (6.5b), with its own enemies and rewards: the stage then changes none of the original zones.
- Full example: `pc/examples/mods/training-field` (two floors, a guide in Central Park, calibrated encounters). A `tools/` folder inside a mod is for your own scripts (the example's draws its art): the game never reads it and `--validate-mod` does not warn about it.

**Step by step** (the complete working example is `pc/examples/mods/training-field`; copy its layout):

1. **Folder:** `mods/my-mod/stages/yard/` with `stage.json`, `background.png` and `floor.png` (the other two PNGs are optional). The mod needs its `mod.json` like any other (§4).
2. **Art:** an indexed `background.png` whose width and height are multiples of 128 (for example 1024 x 768 = 48 tiles; at most 255 tiles). With too many distinct blocks the stage is refused with the codec's reason.
3. **Layers** (same size as the background, one layer pixel per map pixel; the value is the **index** of the indexed PNG, not the color):
   - `floor.png`: 0 closed (cannot walk), 1 open and flat, 2-7 slopes and 16-23 mirrored slopes (`docs/formats.md` 4.3). Any other value is refused.
   - `triggers.png`: 0 nothing; otherwise `direction * 32 + slot`, with direction 1-7 and slot 0-31 (its place in `slots.add`, from 0).
   - `areas.png`: 0 no battles; `n` is the n-th entry of `battles.add` (from 1).
4. **Header:** `start` (required, inside the map and on walkable floor), `fallback`, `soundBank` and `music`, as in the example above.
5. **Encounters of your own:** in `tables/field_encounters.json` add `<mod>:<name>` rows with their enemies and rewards (6.5b) and name them in `battles.add` (`"encounter": "my-mod:meadow"`). That way no original zone changes.
6. **The entry:** a character on a game stage (`stages/s330/stage.json`, 6.11) whose talk has a `conditions` entry (for example `["item", "dw3:item/9", 1]`) and the action `["warp", "my-mod:yard", x, y, dir]`. The way back is a `slots.add` entry with `"stage": 330` and its `x`, `y`.
7. **Check:** `boot "<disc>.bin" --validate-mod my-mod` must end with `ok` and exit code 0 (2c). Maps are read once at startup: restart the game after each change.

**Common errors** (each one shows up as `[stages] <file> rejected: <reason>` and the stage is refused whole; the game never hangs):

| Symptom | Usual cause |
|---|---|
| size refused | width or height that is not a multiple of 128, or a layer whose size differs from the background |
| meaningless floor value | an index other than 0-7 and 16-23 in `floor.png` (check the PNG is indexed and that the index, not the color, is the value) |
| slot or area that does not exist | `triggers.png` names a slot `slots.add` does not have, or `areas.png` an `n` above `battles.add` |
| `there is no field encounter named` | the encounter in `battles` or `start-battle` is not in `tables/field_encounters.json` |
| `start` outside the map | a `start` on floor 0 is only warned about, not refused, but leaves the player inside a wall: put it on floor 1 |
| an exit to a refused stage | refuses the stage that holds it too: fix the destination stage first |

Open questions and caveats (not verified against the game): a `soundBank` the game lacks (nothing plays), palette bit 15 (a PS1 convention), and that mod stages do not appear on the world map.

#### Heights and ramps

The game has no height while walking: a **slope** is a `floor.png` value (2 to 7, and 16 + n mirrored) that changes the walking step so the player follows the incline (`docs/formats.md` section 4.3). The height is your drawing. Recipe (used by `training-field:lower`, a plateau with a ramp on each side, `tools/make_art.py`):

1. Pick the slope: value 3 going east steps `(3939, -819)`: 0.2079 px up per px east. A 32 px high plateau is climbed with a band 154 px long (32 / 0.2079).
2. Draw the slanted band (steps, edges) and put **exactly** that band at value 3 in `floor.png`; value 19 (16 + 3) is the same slope mirrored: walking east goes down, for the ramp on the other side.
3. The sides of the ramp and the cliff are walls (value 0): a running player does not cross them. A triangle of wall under the ramp (down to the cliff's foot) gives the wedge look.
4. Test it without a window with `fieldwalk:<x>:<y>` (a route by state) and read `floor cell` in the log (`docs/stages.md` section 15).

Not covered by mods yet: a second floor layer (`SLOT_MAP`) and climbing walls with the pad (`SLOT_CLIMB_*`, `SLOT_DROP`); do not use them untested.

#### Fishing and kick trees

Two mechanics of the game that your stage (new or patched) can fill with **your** Digimon (details and what the code says: `docs/stages.md` section 14).

**Fishing.** A `gauge` slot is a fishing spot (it needs the Fishing Pole, `dw3:item/5`: an added `gauge` asks for it by itself; `"conditions": []` removes the condition). The gauge cursor stops on cell 0 (nothing), 1 or 2. Each cell is filled with a list of results, one picked at random (optional `weight`):

```json
"slots":   {"add": [{"key": "pond", "type": "gauge", "area": {"x": 400, "y": 600, "w": 200, "h": 120}}]},
"fishing": {"slots": [{"key": "pond",
    "1": [{"encounter": "mymod:eel", "stage": 1, "music": 1611137024, "weight": 3}, {"item": "dw3:item/18"}, {"nothing": true}],
    "2": {"encounter": "mymod:bass", "stage": 1, "music": 1611137024}}]}
```

`"fishing": {"default": {"1": [...], "2": [...]}}` applies to every `gauge` slot of the stage without its own entry. A cell with no list stays as in the game. Which cell is green and which yellow is in the gauge's art (hypothesis: 2 is the long one, 1 the short one with a sound); check it in the game.

**Kick trees.** Every map has several trees and **only one** holds the Digimon; the radar tells how close it is. `hiddenSpots.add` places trees (`x`, `y` in map pixels) and `prize` changes what the prize tree hides (`common` 80%, `rare` 20%); with several entries the tree hides one of several Digimon. The player needs item 4 to kick. The game's prize is always a battle; here it may also be an `item` or `nothing`.

```json
"hiddenSpots": {"add": [{"x": 700, "y": 400}, {"x": 812, "y": 520}],
    "prize": {"common": [{"encounter": "mymod:cardmon", "stage": 1, "music": 1611137024}, {"encounter": 20, "stage": 1, "music": 1611137024, "weight": 2}]}}
```

`--validate-mod` checks the encounter and item names. Without these sections the stage plays as always.

## 7. Examples

Three complete mods and a template folder in `pc/examples/mods/`, all switched off when installed: `pc/examples/install_examples.sh [--only training-field|sample-model|betamon-pack]
[user folder]` copies them and adds them to `[mods] disabled` (nothing is generated from your disc: all the art is original). Each has a `README.txt` saying what
changes and where to see it; the install guide (Windows, Android) is in `pc/examples/README.md`. Everything else is explained with a minimal snippet in its own
section (tables 6.5 and 6.5b, items 6.7, techniques 6.8, Digimon 6.9, cards 6.10, images 6.2 and 6.3, audio 8.1d, video 8.1b).

| Mod | What it teaches | Where to see it |
|---|---|---|
| `training-field` | new stages (`stages/<name>/`: art, layers, exits, encounters, fishing, trees, a duel, a quest), a patch of a game stage (`stages/s330/stage.json`), a new sprite (`sprites/sign/`), new encounters (`tables/field_encounters.json`) and texts per language (`text/<language>/stages.json`); a `tools/` folder with the art generator | Central Park (330), beside Leomon: talk to the guide (with and without the Seiryu Badge); walk in the grass of the floors |
| `sample-model` | a new enemy Digimon with a 3D model of its own (`digimon/boxbot/`: `digimon.json` and `model/` with glTF, PNG and `model.json`) and its place in two encounters (`content/encounters.json`), and a partner form with a model of its own (`digimon/boxpal/`, 8.2c) | Central Park random battles (encounters 34 and 35) |
| `betamon-pack` | a new partner with three forms, a fourth starter team, a DDNA quest that unlocks it and art rendered from SpriteKiln models (6.9b, "Making a partner pack") | starter team screen (team D); Central Park (330), at the bottom where you arrive: the fisher (story progress 14-38 and the party's levels adding up to 45-74) and the river serpent |
| `_template` | a minimal file of every mod type (what `--new-mod` copies) | not installed |

No raw-file example (6.4): there is no safe, visible change without editing undocumented bytes.

## 8. Tools

| Tool | What for |
|---|---|
| `tools/file_names.py "<disc>.bin"` | file number, sector and disc path (TSV on standard output) |
| `tools/extract_disc.py "<disc>.bin" <out>` | extracts every disc file keeping paths |
| `tools/image_atlas_preview.py <folder>` | draws the pieces of a screen (`menu.json`) or of a stage's objects (`objects.json`) over their pages, or the frames and animations of a sprite (`sprite.json`; `--anim`, `--set`, `--dir`, `--gif`); needs Pillow |
| `tools/image_mod_palette.py` | makes an image mod changing **only the palette** of exported PNGs (`--list`, `--to`, `--sprite`; paths under `<data>` such as `menus/stshop`, `cards/c001`, `partners/<name>/portrait.png`) |
| `tools/asset_dupes.py <data>` | duplicate report for an exported folder: repeated files and pixels, sheets, text (paths and counts only, `--json`); needs Pillow |
| `tools/techcompat.py "<disc>.bin"` | which fighter can animate which technique (animation slots; `--json`, `--fighters`); reads your disc, stores no game data |
| `tools/balance_compare.py "<disc>.bin" <mod>` | HP, MP, stats and growth of the mod's partner forms next to the eight originals and a projection to level N (`--level`); reads your disc, stores no game data |
| `tools/model_export.py "<disc>.bin" <fighter> <out>` | exports a battle model to glTF 2.0 with skeleton and animations (`--all`, `--no-animations`, `--source`; the game does this by itself: see 8.1) |
| `tools/model_import.py <source>.json --out <mod>` | turns glTF + PNG + metadata (`model-src-v1`) into a new battle model; to check a source or bake outside the game, since the port bakes it at load (see 8.2) |

### 8.1 Exporting a battle model to glTF

**From the game (the normal way).** The port exports the original models from **your** disc to `data/digimon/d<nnn>/model/` (in the user folder), one per fighter
(`d383` is Kotemon: the number of `"dw3:digimon/<id>"` on three digits), four files each: `model.gltf` + `model.bin` + `model.png` + `model.json`. That is exactly the
**neutral source of 8.2**: copy the `model/` folder unchanged to `mymod/digimon/<key>/model/` and it is the model of a mod's Digimon (`model.json` carries the motion slots, the effects
donor and the template of that same fighter, and the height and distance of its `FighterInfo`). It is only done when you ask (all 248 are about 200 MB and
take about 8 s):

- **Menu:** *Options > System > Prepare modding files* (2b) exports all of them (no confirmation; the row's value shows `Models 12/248`). The game
  keeps running at full frame rate: it runs on a worker thread (built and written there; the game thread only hands it the disc files), a second press while it runs does nothing, and quitting in the middle stops it between two models (files are written atomically).
- **`settings.ini`:** `[data]` / `export_models = all` or `export_models = 383, 12` exports them at start, before the window opens. A model already exported
  with the same exporter version and with its four files in `data/digimon/d<nnn>/model/` is **skipped without building it** (the
  first start takes about 8 s for all; later starts, nothing). An edited file counts as present and is not touched. To
  redo one, delete its folder; the *Prepare modding files* button does not rebuild what is up to date.
- **The `.new` rule** (like the rest of `data/`): `data/digimon/manifest.json` remembers what the port wrote (and, in `names`, which fighter each folder is: `"d012": "dw3:digimon/12"`). A file you edited is **kept** and the fresh one goes
  beside it as `<file>.new`; an untouched one is replaced.
- **`data/content/digimon.json`** names each fighter's folder (`"model": "digimon/d<nnn>/model/"`), exported yet or not.
- **Blender scale:** the glTF comes out at **1 m = 1000 game units** (Kotemon, 1886 tall, is about 1.9 m) and `model.json` says `"scale": 1000`: the importer undoes it by itself.
  No manual scaling is needed in Blender; for a new model work in metres and set `"fitHeight"` (or `"scale"`) in `model.json` for the height you want in the game.
- It is the same code as `tools/model_export.py --source` (portable C; only the menu's job thread uses SDL threads): the `.gltf`, `.bin` and `.json` are **byte for byte equal** and the PNG has the same pixels
  (checked by `pc/modeltest.c`; the PNG's deflate stream is the port's own). Limit: each motion's loop is in the animation's `extras`, not in `model.json`.
  To avoid copying the exported folder: `"from": "dw3:model/<id>"` in the mod's `model.json` (8.2).

**The Python tool** does the same from the command line (development and checks):

```
python3 -I tools/model_export.py "<disc>.bin" 383 out/ --source    # out/383/model.gltf ... model.json, at Blender scale
python3 -I tools/model_export.py "<disc>.bin" --all out/ --source  # all 248
python3 -I tools/model_export.py "<disc>.bin" 383 out/             # the flat layout: fighter_383.gltf in game units
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
  `vprp` (or another size, which is scaled; see below).
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

```
mymod/audio/COMMON/vag_050.wav     # an effect: replaces sample 50 of the COMMON bank (16-bit WAV, mono or stereo)
mymod/audio/TTLBGM/seq_0_00.wav    # a song: the title music, with an `smpl` loop (or a seq_0_00.json beside it)
mymod/video/MOVIEOPN.avi           # a movie (8.1b): the opening one, in MJPEG
```

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
mymod/digimon/<key>/digimon.json            # the Digimon's record (6.9); without "model" it uses the model/ of its folder
mymod/digimon/<key>/model/model.json        # model-src-v1 metadata (below)
mymod/digimon/<key>/model/model.gltf        # or .glb; with its .bin and its PNG next to it
```

The key (a-z, 0-9, `-`, `_`) is named `"<mod folder>:<key>"`, and a new Digimon uses it as `"model": "<mod folder>:<key>"` (6.9): it gets its own
`FighterInfo` row with the baked model and motions, the donor's effects, and the `template` (cameras, height) and `info` (`distance`, `height`) of
`model.json`. It also works inside the `"model"` object as `{"file": "<mod>:<key>", "entry": 1}` (model; entry 0 is the motions). Original fighters are named
`"dw3:digimon/<id>"` and, for the model only, `"dw3:model/<id>"` (it shares that fighter's row). A complete example: `pc/examples/mods/sample-model/` (`digimon/boxbot/`).
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

With `--out` it writes `mymod/digimon/<name>/model.bin` (beside that Digimon's `digimon.json`; the port reads any file of the mod by its path) and prints a `digimon/<name>/digimon.json` snippet using the object form of `"model"` (the raw file, which the
port reads as `"<mod>:digimon/<name>/model.bin"`); it is the advanced and debugging path: with the source in `digimon/<key>/model/` it is not needed.

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
| `name` | folder name (`digimon/<name>/model.bin`) and the Digimon's key in the snippet; letters, digits, `-` and `_` |
| `scale` or `fitHeight` | game units per glTF unit, or the height (game units) the rest pose must have; not both. Original fighters are 1300 to 10 000 tall; Kotemon, 1886. With neither the scale is 1 and it warns if the model is under 50 or over 20 000 |
| `yaw` | degrees of turn about the vertical axis (0 by default) |
| `motions` | slot (0 to 61) to animation: a name, an object or a number (copies that other slot). Without `motions`, animations named `motion_NN` go to slot NN (what the exporter writes, 8.1) |
| `fill` | animation (or an object as in `motions`) for **every** unassigned slot; without it an empty slot is a risk if an effect script asks for it |
| `effects` | donor: `"dw3:digimon/<id>"` whose effects file the Digimon uses (the scripts only talk to the model by motion slot number) |
| `template` | original fighter of the **same kind** (enemy or partner) that gives cameras, height and distance: put it in `"base"` and `"from"`; pick one of a similar size, cameras do not adjust themselves |
| `info` | `"distance"` and `"height"` of the own row (6.9): they place the **model** at its resting place if it is much bigger or smaller than the template; they do not move the cameras (8.2c) |
| `normals` | `"file"` (default, the glTF's; smooth if missing), `"smooth"` or `"flat"` |
| `doubleSided` | `"duplicate"` adds the back faces of double-sided materials (the game draws one side; Blender marks them so by default) |
| `from` | `"dw3:model/<id>"`: starts from the original fighter (see "Starting from an original"); whatever is missing comes from it |
| `tolerance` | `{"position": units, "rotation": raw, "scale": raw}`: largest error when reducing keys (default 0.1 % of the height, 4 = 0.35 degrees, 16 = 0.4 %) |

**Starting from an original: `"from"`.** A `model.json` may say `"from": "dw3:model/<fighter id>"` (the same number as in the exported `data/digimon/d<nnn>/`) and ship **only what it
changes**: whatever is missing comes from the original fighter, which the port exports **in memory** at load (the same four files as `data/digimon/d<nnn>/model/`, read from
your disc). The typical case is a retexture: the folder holds only `model.json` (`{"from": "dw3:model/383"}`) and your `model.png`. Rules:

- **Files:** a `model.gltf`, `model.bin` or `model.png` the folder lacks comes from the original; the ones it has **win**. The exported glTF names `model.png`
  and `model.bin`, so a `model.png` of your own must keep the original's **UV layout** (size and position of every island): you paint over the exported one,
  you do not move things around. If you ship your own `model.gltf`, ship its `.bin` and PNG too (or the exported names, which are filled in).
- **`model.json`:** the original's is the base and the top-level keys of yours replace it (shallow merge: `"info"`, `"template"`, `"effects"`, `"yaw"`... are
  replaced whole). Exception: `"motions"` is merged **per slot**: the slots you give win and the others stay the original's. If you bring a glTF with other
  animation names, redefine in `"motions"` every slot that used the original's names (a slot cannot be removed, only changed). `"format"`, `"name"` and `"gltf"` are inherited too.
- **Rejections** (`[models] <mod>:<key> rejected: <why>`): a `"from"` that is not `"dw3:model/<n>"`, a fighter that is not on your disc, or no disc
  available (in the tests, for instance). Here `"from"` belongs to `model.json`; do not mix it up with the `digimon.json`'s own `"from"` (the template fighter).
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

**Using it.** Put the snippet as the mod's `digimon/<key>/digimon.json` (the folder's name is the name; pick `"base"` and `"from"` of the same kind); the Digimon shows its model in battle. New enemies and
partner forms are explained in 6.9. With the glTF source in `digimon/<key>/model/` nothing has to be baked separately: the port converts it on load (8.2, "Where it goes and how it is used").

### 8.2b A partner portrait from a model (`tools/portrait_from_model.py`)

Makes the `portrait.png` of a new partner (6.9b, `docs/formats.md` 4.12) by drawing its 3D model, so you do not have to paint the cells by hand. Python 3 standard library only.

```
python3 -I tools/portrait_from_model.py <model> <out.png> [--cells 1..8] [--icon] [--animation <name>] [--yaw 40] [--pitch 12]
                                        [--front +z|-z|+x|-x] [--margin 1..2] [--colors 3..16] [--dither] [--preview view.png]
```

`<model>` is a folder with `model.json` (or a loose `.gltf`/`.glb`, or the `model.json`), `<mod folder>:<key>` (`digimon/<key>/model/`) or
`dw3:digimon/<id>` for an original fighter **from your own disc**: `--data <the port's data folder>` (where "Export models" put `digimon/d<nnn>/model/`) or
`--disc "<disc>.bin"` (runs `tools/model_export.py --source` into a temporary folder). **Run it on your own disc; the output stays on your machine**
(do not upload or share it: it is art derived from the game, ADR-001). An already baked model file is not a valid input: the glTF source is needed.

Output: a 4-bit indexed PNG, 32 high and 32 x N wide (4 cells by default), index 0 transparent, index 1 the 1 px dark outline and up to 14 more colours
(median cut plus 4 k-means steps, no dithering unless `--dither`). The model is seen three-quarters, facing screen **left** (`--front` says which model axis is its
front: glTF uses `+z`; if it comes out facing right or away, try another), fitted and bottom-anchored with a 1 px margin (`--margin`); cell *k* is the pose at *k*/N of the
idle animation loop (motion 0 of `model.json`, or the first animation), and all cells share one scale and ground line, so the ping-pong `0 1 2 3 2 1` reads as breathing.
`--icon` gives a single 44x28 frame for the memory card icon. Design: a software rasterizer of about 400 lines (draws at 8x and reduces with a 0.5 coverage
threshold for crisp edges) on top of the glTF reader of `tools/model_import.py`; simpler and more portable than pulling triangles out of the C, and tested by
`python3 -I tools/test_portrait_from_model.py` (`make -C pc portraittest`).

### 8.2c A partner's own model

A new partner form (6.9, "New partner forms") can ship its own glTF model just like an enemy (8.2): the `digimon/<key>/model/` folder of its `digimon.json` whose `"base"` is a partner. What differs from an enemy
is what the game asks of a partner: more motion slots and 12 cameras instead of 3. A complete example, tested in battle: `pc/examples/mods/sample-model/digimon/boxpal/` (a robot made of boxes with a partner's animations;
`python3 -I tools/test_model_import.py --demo-partner <folder>` generates it again).

**Start from an original partner (on your machine).** Export one from **your** disc ("Prepare modding files" in the game, or
`python3 -I tools/model_export.py "<disc>.bin" 383 <folder> --source` for Kotemon) and copy the `d383/model/` folder (`model.gltf`, `.bin`, `.png`, `model.json`) to `digimon/<key>/model/`
(`d<nnn>/model/` is never read, 6.9). A partner's template comes out ready: `"template"` and `"effects"` of that fighter (its 12 cameras and its technique scripts), `"info"` with its distance and height, one
`motion_NN` animation for every slot that fighter has and `"motions"` joining them. **It is data derived from the game: do not upload or share it** (ADR-001); replace the mesh and the texture with your own before publishing, and publish only
your `model.json` and your animations. In Blender keep the animation names (`motion_NN`, the number is the slot) or give them your own and reassign them in `"motions"`.

**A partner's `model.json`** (the example's; the animations are named after what they do and `"motions"` gives each one's slot):

```json
{
  "format": "model-src-v1", "name": "partner", "gltf": "partner.gltf", "fitHeight": 1800,
  "effects": "dw3:digimon/383", "template": "dw3:digimon/383",
  "motions": {
    "0": "idle", "1": "idle_weak", "2": "dodge", "3": "flinch", "4": "hit", "5": 4, "6": 4,
    "10": { "animation": "ko", "end": "hold" }, "11": 4, "12": "cheer", "13": "blast", "14": "attack",
    "15": 14, "16": 14, "17": 14, "20": 14, "32": 14
  },
  "fill": "attack"
}
```

`"template"` must be an original **partner** (otherwise the Digimon keeps its `base`'s model and the log says so); without `"template"` the `base` is used. `"effects"` is the donor of the technique animations (usually the template's).
A number in `"motions"` copies that other slot; `"fill"` gives one animation to **every** unassigned slot (the donor's technique slots among them).

**Motion slots** (the 62 a fighter has; what was measured on the 248 fighters of the disc and the code that asks for them, with `path:line`, is in `docs/formats.md` 4.6, point 5b). The slot is the number in `"motions"`:

| Slot | What the game asks it for | Partner | Enemy |
|---|---|---|---|
| 0 | idle (loop) | required | required |
| 1 | weak idle: at a quarter of the HP or less | required | required |
| 2 | dodge: the target of an attack that missed | yes | yes |
| 3 | flinch: a hit that lands and is not the last of a multi-hit attack | yes | yes |
| 4, 5, 6 | taking a hit (several fighters ask for 5 and 6, in that order) | 4 yes; 5 and 6 if the donor uses them | same |
| 10 | fall: the hit that knocks out | yes | yes |
| 11 | the last of taking a hit that does not knock out (what the original shows: unknown) | yes | yes |
| 12 | entrance, and the victory pose and the entrance of a Digimon change (partner) | yes | yes |
| 13 | entrance of the temporary form (blast digivolution) | only a blast form | does not exist |
| 14 to 19 | generic attack ("Fight"): 14 and, in multi-hit attacks, the next ones | 14 yes | 14 yes |
| 20, 32 | the generic attack switches to scripts 6 (an action effect or a weapon of the enemy's family) and 8 (triple hit) | yes (20 to 25, 32 to 37) | no |
| 26 to 31, 38 to 39, 44 to 45, 50 to 55 | technique animations of the donor's scripts 7, 9, 10, 11 and 13 | the donor's that you use | same |
| 56 to 58 | the donor's script 12, which acts on another model (unknown) | only if the donor uses it | no |

Nothing asks for any other slot (7, 8, 9, 40 to 43, 46 to 49, 59 to 61). An empty slot (with no `"fill"`) makes the game read garbage if anything asks for it: **so
`--validate-mod` gives one warning for every slot the game asks of a partner (0, 1, 2, 3, 4, 10, 11, 12, 13, 14, 20 and 32) or of an enemy (0, 1, 2, 3, 4, 10, 11, 12 and 14) that the model leaves empty**, e.g.
`sample-model:boxpal: motion slot 3 (flinch) has no animation: a hit of an attack that lands and is not the last (effect script 1)`. The donor's techniques ask for more slots (14 to 55): with `"fill"`
they all exist; without it, map every one of the donor's that you use. A partner's attack that animates with script 5 can play 6 or 8 depending on the enemy (`techs.c:48-85`), hence slots 20 and 32.

**Cameras (12 on a partner, 3 on an enemy).** They are not in the model: they are part of the record the Digimon copies from its `"template"` (`docs/formats.md` 4.6, point 5b):

| Camera | What it sees |
|---|---|
| 0 to 5 | the **enemy**: the enemy's `cameraCount - 1` (1 to 6) is picked, read from the **partner's** record, so the partner decides how each enemy is framed |
| 6, 7 | the partner (7 when weak) |
| 8, 9, 10, 11 | the partner fighting (8), digivolving (9), entering and the closing shot (10) and a high view that only scripts ask for (11) |

An own model **cannot** give it new cameras: it uses the template's (or the `base`'s without `"template"`); pick one of a **similar size** to your model (`fitHeight`; Kotemon is 1886 and its camera record is made for that).
`"info": {"distance": ..., "height": ...}` does not move the cameras: it places the **model** at its resting place (`z = ∓(0x1400 + distance)`, `y = -height`; without `"info"` the template's are used). If the model is
much bigger or smaller, adjust that and check the battle with the 4 framings (battle, attack, digivolution and victory).

**Bake and check.** The port bakes it at load (8.2); without opening the game: `python3 -I tools/model_import.py digimon/<key>/model/model.json --check --partner` (`--partner` warns, one line per
slot, of the ones a partner needs and lack; without it only the short enemy warning appears). With the game: `<boot> "<disc>.bin" --validate-mod <mod>` (0 errors and 0 warnings on the example).
To see it in battle without playing up to the digivolution, the test harness seeds the form: `DW3_SEED_PARTNER="0.0=<mod>:<key>@1/0" DW3_BATTLE_ENEMY=63 <boot> "<disc>.bin" 5000 <capture.png> 0 "battle@30:cross,70,cross,..."` (6.9, "What a new form does today").

**Limits.** Those of 8.2 (bones, polygons, 16 colours per island texture, rigid skin) and also: the cameras and the technique scripts are the template's and the donor's; the face stays empty (no blinking); the name the battle
HUD shows is the partner's (`PartnerStats.name`: the name of its first form when the game starts, or the one the player gave it; the original does not change it on digivolving either, so this is on purpose), while the form's name appears in the battle report, the lab, the status screen and the digivolution lists; a new partner is obtained with `"evolution"` (6.9), not through its model.

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
| Asset layout (`asset-v1`, ADR-016) | one folder per topic and per asset, one JSON record per asset (`card.json`, `partner.json`, `digimon.json`, `stage.json`...), the same paths in `data/` and in `mods/<mod>/`, schemas in `schema/` and `--validate-mod` over everything but `text/` | **final for the next release**: the paths in this guide will not change shape before it |
| 1. Resources | replace text, config, images, sprites and raw files; manifest with dependencies; this guide | **done** (review pending) |
| 2. Data tables | the game's tables (Digimon, techniques, items, rewards, experience, shops, encounters) as JSON; patch by key and add rows with ids `<mod>:<name>` | **done** (`stageBattles` is part of the stage patches, 6.11). Includes the card shop, gym, inns, card opponents and new cards, new items and techniques, changes to existing Digimon, new enemies, and new partner forms |
| 3. Scripts | an embedded language (candidate Lua 5.4, sandboxed: no files, network or system) for events, quests, characters | planned (its own ADR) |
| 4. New entities | new field stages from a mod (ADR-012, `stage-new-v1`, 6.11b, example `training-field`); new stage characters with talks and flags of their own (6.11, with the stage's sprites or their own); new Digimon already work (battle models in glTF, 8.2) | **in progress** (new stages done; trees and fishing as data still missing) |
| Asset formats | video (AVI), audio (WAV and MIDI export and replacement), images (PNG), battle models (glTF) and stage art (PNG) | **done** (§8) |

Reserved for phases 2 to 4: `"kind": "content"` and the `<mod>:<name>` identifiers. Saves are no longer kept on a PS1
card but in `<pref>/saves/default/slot<N>.dw3save` (ADR-008, option B: the game's original block as is, plus the
extended state by name and a CRC32; a `.mcd` card placed in `import/` is imported once and never modified). `mod.json` keys and formats are versioned; an incompatible change creates a new version and the old
one is still read for at least one more port version.

## 11. Migrating from the previous format

This guide describes only the current format (`asset-v1`, ADR-016). If you have a mod made for an earlier version of the port, or a `data/` folder that version exported,
this section says where each thing lives today. Saves do not change: they store names and identities, not paths.

**Rules**

1. **There is no compatibility.** The port reads none of the paths in the table below and does not raise an error for them. `--validate-mod` warns (`folder <name>/ is never read`, `... is ignored`) about the most common
   old folders and files, but it does not check the old images of `images/` one by one. Move the files to their new path by hand (the PNGs keep their format) and rewrite the records (below).
2. **Export `data/` again.** The exporters neither migrate nor delete what was there: the old folders of `data/` stay on disk, unread (some leave one log line, e.g. `data/models/`).
   Delete them by hand, or delete the whole `data/` and start with `--export-all` (2b) so it is rebuilt. A manifest in the old format (`stamp`) counts as missing and is exported again.
3. **`name` becomes `title`.** In `card.json`, `partner.json` and `digimon.json` the per-language text (`{"eng": ..., "es-419": ...}`) now goes in `title`; `name`, when present, is the folder's name.

| Before | Now | What to do |
|---|---|---|
| `images/<folder>/<file>/<NN>_<MM>.png` + `.json` (`atlas-v1`) of the menu screens (shop, status, memory card, name entry, gym...) | `menus/<screen>/<piece>.png` + `menu.json` (6.2g) | redo the edit by piece, with the real colors. Still in `images/`: `TLOGO*`, `TTCOMMPK`, `END__ALL`, `CNTSELWN`, `CARDDATA`, `FIELDCOM`, `STWMP0` and `STWMP1` (6.2) |
| `images/field/stage/s<nnn>mask/` (a stage's objects) | `stages/s<nnn>/objects/<piece>.png` + `objects.json` (6.2h) | redo by piece. The backgrounds `s415bg01/02` and `s815bg01/02` stay in `images/field/stage/` (6.2c) |
| `z_stage/s<nnn>pack/` (tiles) | `stages/s<nnn>/tiles/` + `tiles.json` (6.2d) | move the PNGs |
| `stages/<n>.json` (a stage's data and a mod's patch) | `stages/s<nnn>/stage.json`, with `s` and three digits (6.11) | rename and move; one single `stages/manifest.json` |
| `new-stages/<name>/` | `stages/<key>/` (6.11b) | move the folder; `s<nnn>` is reserved for the originals |
| `images/field/sprt/<NAME>/` (and the copies of the followers `p003pken`...`p010pata`) | `sprites/<name>/<EEE>.png`, lower case (6.3); the 8 followers in `partners/<name>/field/` | move the frames; in an original's folder only the `NNN.png` files are read |
| `images/card/cardpak<A>/<NN>.png` and a mod's `card/<key>.png` | `cards/c<nnn>/face.png` (originals) and `cards/<key>/face.png` (new) (6.2b, 6.10) | `<nnn>` is the card's number (`c001`-`c316`), not its position inside the `cardpak` |
| `content/cards.json` (`"new"`) | `cards/<key>/card.json` (`cards-card-v1`) | each entry `"<key>": {...}` moves to its own file with `"format": "cards-card-v1"`; the `name` text becomes `title` |
| `content/partners.json` and the `art`, `icon`, `sprite` fields | `partners/<key>/partner.json` (`partners-partner-v1`) beside `portrait.png`, `icon.png`, `field/` and `training/` (6.9b, 6.2f) | the same: one file per partner, `name` becomes `title`; the art fields go away (the art is looked up in the folder) |
| portraits and icons spread over 7 screen sheets | `partners/<name>/portrait.png` and `icon.png` (6.2f) | edit the partner's PNG; the port writes it into every screen |
| `content/digimon.json` (`"change"` and `"new"`) | `digimon/d<nnn>/digimon.json` (changes an original) and `digimon/<key>/digimon.json` (new) (6.9) | one file per Digimon; `name` becomes `title`; the placements move to `content/encounters.json` |
| `content/models/<key>/` and `models/<id>/` | `digimon/<key>/model/` (new) and `digimon/d<nnn>/model/` (exported, 8.1) | move the folder; the `model/` of a mod's `d<nnn>` is not read |
| one `stamp` per exporter and a `manifest.json` per family | one `manifest.json` per topic (`asset-manifest-v1`) | nothing: it is rebuilt on export; a mod does not write it |
| `audio/`, `video/`, `text/`, `tables/`, `config/` | the same topics, with lower-case names | nothing to move in a mod |
| `files/<disc path>` in `data/` | `raw/<disc path>` in `data/` (6.2e) | in a mod, raw files stay in `files/` (6.4) |
