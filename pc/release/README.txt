DW3 Port 2.0.0
==============

Downloads, new versions and the source: https://github.com/MxAlfredo/dw3_port/releases

A PC port of Digimon World 3, built from the community decompilation of the game.
It runs on Windows, Linux and Android, and adds Latin American Spanish as an extra language.

This download does NOT include the game. You need your own disc image of
Digimon World 3 (Europe), the one with English, French, German, Spanish and
Italian (often named "Digimon World 2003 (Europe) (En,Fr,De,Es,It)"), as a
.cue + .bin pair or a single .bin.


Getting started
---------------

The downloads of version 2.0.0 are named:
  DW3Port-2.0.0-Setup.exe               Windows installer
  DW3Port-2.0.0-windows-portable.zip    Windows, portable
  DW3Port-2.0.0-x86_64.AppImage         Linux
  DW3Port-2.0.0-android.apk             Android

Windows, installer (DW3Port-...-Setup.exe)
  Run it. On the "Your game" page, choose your disc image and, if you want to
  continue your saves from an emulator, your memory card (.mcd or .mcr, for
  example from DuckStation or ePSXe). Both are optional: if you skip the disc,
  the game asks for it the first time it starts.

Windows, portable (DW3Port-...-windows-portable.zip)
  Unzip it anywhere (a USB stick works), put your disc image in the game folder
  and start DW3.exe. Settings, saves and logs stay in that same folder.

Linux (DW3Port-...-x86_64.AppImage)
  Make it executable (right click > Properties > "Allow executing", or
  chmod +x DW3Port-*.AppImage) and open it. It asks for your disc the first
  time. It runs on any x86_64 distribution from 2022 on (glibc 2.34 or newer).

Android (DW3Port-...-android.apk)
  Android 10 or newer, 64-bit (arm64) phone or tablet. Copy the .apk to the
  device and open it; Android asks you to allow installing apps from unknown
  sources (for the app you opened it with, e.g. Files). Updating from an
  earlier version of this port installs over it and keeps your saves. See
  "Android" below.


Playing
-------

Keyboard (default):  arrows = D-pad    C = Cross    V = Circle
                     Z = Triangle      X = Square   Enter = Start
                     Left Shift = L1   Right Shift = R1
Controllers work too (Xbox, PlayStation and most others).

F1 (or Select on the controller) opens the options: controls, video (with the
battle speed), display, audio and language. The language list includes
"Espanol Latam".


What's new in 2.0.0
-------------------

Your saves and settings from 1.x load as they are. Mods and the data folder do not:
this version reorganises how the game's files are stored, so a few steps are needed.

If you update from 1.x:
  - Delete the data\ folder (see "Where your files are"). The game exports it again
    from your disc the first time you start; this can take a little while once.
  - Mods made for 1.x do not work any more and their authors must update them to the
    new layout (one folder per asset: stages\, cards\, sprites\, partners\, digimon\...;
    see the modding guide). Switch them off in Options > Mods until they are updated.

New:
  - Readable files: everything the game exports from your disc is now a folder per
    asset with plain PNG, JSON and glTF files, so it is easy to look at and change.
  - Example partner pack "betamon-pack": a new partner (Betamon) with its forms,
    evolutions, techniques, its own art and a quest to unlock it, shaped like the
    originals. Two more examples remain: training-field and sample-model.
  - For modders: models can borrow the motions and scripts of other fighters
    (enemy scripts convert to the partner side), motion slots are checked and
    filled, techniques can be added, "balance_compare" prints your partner forms
    next to the eight originals, and the art of a model can be made with SpriteKiln.
  - Partners are no longer limited to eight: more partners and starter teams can be
    added, with their own portrait, icon and field sprite.
  - Latin American Spanish: more fixes to texts that did not fit or read badly.
  - Android: fixed the crash in the falling-tiles scene, and fixed the game
    closing abnormally when you choose "Quit game".
  - If the game crashes, the report is now written in the game's log file, so
    attaching the log to an issue is enough.


What's new in 1.1.0
-------------------

Saves and settings from 1.0.0 load as they are. Mods can now build whole new stages.

  - New stages from mods ("stage-new-v1"): PNG map layers and a background of
    any size, plus new field encounter rows.
  - Fishing results and kick-tree results can be set from mods.
  - A Yes/No "ask" action for talks; battle and card-game results are kept as
    variables a mod can read later.
  - An entry banner for mod stages, and the game's own speaker headers for mod
    texts ({speaker-of:FILE:N}).
  - New example mod: training-field (two floors, a guide, calibrated fights).
    There are now 25 ready-made examples.
  - Fixes found in play-tests.


What's new in 1.0.0
-------------------

The first stable version. Saves and settings from the betas load as they are.

  - Android: the game runs on phones and tablets (64-bit), with touch controls.
  - Options > System > "Import memory card": bring your saves from a PS1
    memory card or an emulator at any time.
  - Latin American Spanish as an extra language, finished and checked.
  - A complete mod platform: game tables, new items, techniques, cards,
    Digimon and enemies, images, sounds, movies, stage characters and talks,
    and new 3D models. Start with the modding guide (docs/modding/README.en.md
    in the project's repository), which has a five-minute first mod and 24
    ready-made examples.
  - Many fixes found while playing through the game on every platform.


What's new in 0.5.0-beta
------------------------

Your 0.4.0 saves and settings should load as they are. The first start after
updating re-exports data\images once (smaller, compressed PNGs), so it may take
a little longer one time.

  - Faster loading between areas.
  - New Options > Mods tab: turn mods on or off, change their order, and
    restart to apply.
  - Options > System > "Prepare modding files" replaces the four old export
    rows and exports everything in one go.
  - Everything on the disc can now be exported to data\ for modding: movies,
    music and sounds, card art, stage art, game tables and the raw files.
    Mods can replace them.
  - Mods can add new cards (they can also be sold, appear in packs and be
    used by opponents).
  - Replaced sounds no longer have a length limit.
  - Exported images are smaller.
  - For modders: --new-mod, --validate-mod and --validate-mods (with the .bin,
    the .cue or the remembered disc), --list-mods and --export-all; a mod
    template and JSON Schemas; and a modding guide in the project's
    repository.


What's new in 0.4.0-beta
------------------------

IMPORTANT, if you played 0.3.0-beta: 0.4.0 does NOT read the folder layout of
0.3.0. Uninstall 0.3.0, then delete its user folder (Windows:
%APPDATA%\dw3port\dw3, Linux: ~/.local/share/dw3port/dw3), or keep only your
saves. To bring an old PS1 memory card (.mcd) along, put it in the new
import\ folder: the game imports it the first time it starts.

  - New user folder layout: saves\, import\, mods\<mod>\content\ and a
    lower-case data\ (see "Where your files are").
  - Saves are the port's own files (no memory card needed); cards are imported
    once from import\.
  - Mods can now add or change items, techniques and Digimon: new enemies and
    where they appear (encounters), and new partner forms that you obtain by
    evolution. There are no PS1 limits on how many.
  - data\content\ lists everything the mods can refer to, with the original
    names (dw3:item/..., dw3:tech/...) and each fighter's animation slots
    (scriptSlots). The game creates it from your disc.
  - Lighter logs: set the log level to off, mods or trace (and pick channels)
    in settings.ini ([debug] log = ...) or with DW3_LOG.
  - Fixed: the loot of a battle now comes from the enemy that was actually drawn.
  - New tool for modders: tools/techcompat.py (which fighter can play which
    technique animation), in the project's repository.


Where your files are
--------------------

  Windows, installed:   %APPDATA%\dw3port\dw3\  (Start menu: "DW3 Port - Saves and logs")
  Windows, portable:    the folder of DW3.exe
  Linux:                ~/.local/share/dw3port/dw3/
  Android:              Android/data/io.github.dw3port/files/  (see "Android")

There you find:

  settings.ini     your preferences (language, video, controls, mods...)
  saves\default\   your saves (slot0.dw3save to slot2.dw3save, the port's own
                   format; view\ holds a readable copy of each, never read back)
  import\          PS1 memory cards to import (see below)
  mods\            your mods, one folder each
  data\            reference files the game generates from your disc (safe to
                   delete: it is recreated)
  logs\            the newest log files

Uninstalling never deletes them.

Android
-------

First start: the game asks for your disc with the system file picker. Choose
the .bin of your disc image (the Download folder opens by default). Choosing
the .cue is not enough: the .bin is what the game reads. It reads it in place,
without copying it, and remembers the choice; the next start goes straight in.
If you delete or move the .bin, it asks again.

Controls: a connected controller (Bluetooth or USB) works like on any other
platform. Without one, touch controls appear: a floating stick on the left (the
finger sets its center) and the buttons on the right. In Options > Display,
"Touch controls" can be Auto (hidden while a controller is connected), On or
Off, and their opacity and size can be changed. The options menu opens with
Select and also works by touch: tap a tab or a row to select it, tap a value to
change it, swipe to move the cursor, and tap outside the panel to close it. In
dialogues, tapping the picture while a page is being typed finishes the page; the
next tap turns to the following page. Questions and menus are answered with the
buttons. While Options > Controls waits for a key, a tap cancels the capture.

Screen: Options > Video > Scaling offers Fit, Integer and Aspect; on Android it
defaults to Aspect (the largest image that keeps the 4:3 proportion, with
bars). Sending the game to the background (Home, switching apps) freezes it, so
it uses no battery or CPU, and it carries on where it was when you come back.
Options > System > "Restart game" restarts the game (for example to apply mods).

Where your files are:

  Android/data/io.github.dw3port/files/

That folder holds the same files as on the other platforms (see "Where your
files are"): saves, logs, mods and settings.ini. From a PC, connect the device
over USB ("File transfer" mode) and open that path; on some devices only a
file manager that shows Android/data can see it. Saves and settings are the same
files as on the PC, so you can copy saves\default from the PC to the device.

Mods: they are made and installed on a PC (see the modding guide in the project
repository; the examples (training-field, sample-model and betamon-pack) are installed on the PC with
pc/examples/install_examples.sh from the repository). Then
copy the PC's mods\ folder (without `_template`) into the mods folder above,
turn them on in Options > Mods and restart the game. The game only reads mods at
startup.


Screen scaling (Options > Video > Scaling, "scaling" in settings.ini): Fit
stretches the picture to the window, Integer uses the biggest whole multiple of
320x240, and Aspect uses the biggest picture that keeps the 4:3 shape, with
black bars on the sides or on top. On phones and tablets Aspect is the default.

If you have a PS1 memory card (a .mcd file, for example from an emulator), put it
in the import folder (or choose it in the installer, which copies it there). The
first time the game starts its saves are imported into saves\default (once; the
card is never modified; if several cards are there, the first one, by name, that
holds a Digimon World 3 save is used). New saves are no longer written to the
card, so they do not open in an emulator. To import again, delete
import\imported.txt and the saves\default folder (copy it first if you have new
saves).

Later, from the game: Options > System > "Import memory card" (press it twice)
replaces your saves with the newest card found in the import folder or in an
emulator's own folder (DuckStation, PCSX, ePSXe, Mednafen, RetroArch). It reads
.mcr/.mcd/.mc/.srm cards (128 KB) and .gme/.vmp ones. Your previous saves are
copied first to saves\default\backup\<date-time>\.


If something goes wrong
-----------------------

Please report it on the project's GitHub page (https://github.com/MxAlfredo/dw3_port/issues)
and attach the newest file from the logs folder, saying which version you use
(2.0.0) and on which system. On Windows, DW3-debug.exe is the same game with a console
window that shows the log while you play.


Legal
-----

This is a fan project, not affiliated with or endorsed by Bandai Namco or Sony.
Digimon is a trademark of its owners. The port contains no game data: the disc
image must be your own copy. Credits and the licenses of the parts it uses are
in THIRD-PARTY.txt.
