# DW3 Port 2.0.0

Downloads: `DW3Port-2.0.0-Setup.exe` (Windows installer), `DW3Port-2.0.0-windows-portable.zip`, `DW3Port-2.0.0-x86_64.AppImage`, `DW3Port-2.0.0-android.apk`; each file is listed in `SHA256SUMS`. You need your own disc image (not included).

## What's new (English)

```
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
```

## Novedades (español)

```
Tus partidas y ajustes de 1.x cargan tal cual. Los mods y la carpeta data no: esta versión
reorganiza cómo se guardan los archivos del juego, así que hacen falta unos pasos.

Si actualizas desde 1.x:
  - Borra la carpeta data\ (ver "Dónde están tus archivos"). El juego la exporta de nuevo
    desde tu disco la primera vez que lo abres; puede tardar un poco esa vez.
  - Los mods hechos para 1.x ya no funcionan y sus autores deben actualizarlos al nuevo
    formato (una carpeta por recurso: stages\, cards\, sprites\, partners\, digimon\...;
    ver la guía de modding). Desactívalos en Opciones > Mods hasta que se actualicen.

Novedades:
  - Archivos legibles: todo lo que el juego exporta de tu disco es ahora una carpeta por
    recurso con archivos PNG, JSON y glTF normales, fáciles de ver y de cambiar.
  - Paquete de ejemplo "betamon-pack": un compañero nuevo (Betamon) con sus formas,
    evoluciones, técnicas, arte propio y una misión para desbloquearlo, hecha como las
    originales. Quedan otros dos ejemplos: training-field y sample-model.
  - Para quienes hacen mods: los modelos pueden tomar los movimientos y los guiones de
    otros combatientes (los guiones de enemigos se convierten al lado del compañero),
    los espacios de movimiento se revisan y se completan, se pueden agregar técnicas,
    "balance_compare" muestra tus formas de compañero junto a los ocho originales y el
    arte de un modelo se puede hacer con SpriteKiln.
  - Los compañeros ya no se limitan a ocho: se pueden agregar más compañeros y equipos
    iniciales, con su propio retrato, ícono y sprite de campo.
  - Español latinoamericano: más arreglos a textos que no cabían o se leían mal.
  - Android: se arregló el cierre en la escena de las baldosas que caen y se arregló
    el cierre anormal del juego al elegir "Quit game".
  - Si el juego se cierra por un fallo, el reporte ahora se escribe en el registro del
    juego; basta con adjuntar el registro en un issue.
```
