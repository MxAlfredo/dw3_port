training-field
==============

A complete new-stage example built only with a mod: two field floors with original art, a plateau with ramps, a lake with a pier, kick trees,
fishing, a duel with a rematch, a card Tamer with a rematch, a ranger quest, signs and an entry banner. Also a base for your own stage.

Requirements
  Port 1.1.0 or newer (mod.json: "port": ">=1.1.0"; an older port disables the mod and logs why), your own Digimon World 3 (EU) disc,
  and the Seiryu Badge (item 9) to be let in.

What it does
  A guide in Central Park (stage 330, beside Leomon's training spot) warps you to the lower floor (training-field:lower, 1024 x 768 px, grass, pond, plateau
  with a ramp on each side, tall grass, levels 8-11). Stone steps in the north-east lead to the upper floor (training-field:upper, 768 x 512 px, autumn, a training
  ring, levels 13-16). Kick trees (5 below / 4 above, one holds the prize), fishing (the pier and two shores; needs the Fishing Pole, item 5; trees need item 4),
  the ring champion (three Digimon, no running; 500 BIT once; then asks if you want a rematch), a card player (opponent 0x13, asks for a rematch after a win),
  and the ranger's quest (read the three signs, beat the card player: 300 BIT and an HP Chip). Entering a floor shows its place name.

How to play
  Install, enable in Options > Mods, restart. Central Park -> talk to the guide -> fight/fish/kick trees below -> steps up -> duel and cards -> back to the ranger.

How it is made (details and snippets in README.md, Spanish)
  mod.json                     manifest (mod-v1), with "port": ">=1.1.0"
  stages/s330/stage.json      stage-patch-v1: the guide (conditions, sound, warp action)
  stages/<floor>/             stage-new-v1: stage.json + background.png (image), floor.png (0 closed, 1 flat, 2-7 slope, 19 = 16+3 mirrored), areas.png (battle zones)
  tables/field_encounters.json new rows "training-field:..." with their own enemies and rewards (hp 24 x level + 120, EXP 5 x level, BIT 10 x level)
  text/{eng,es-419,spa}/stages.json named texts (eng required); {speaker-of:TALK04:19} takes the game's own speaker header from the player's disc
  sprites/sign/               own sign sprite (sprite-v1, 32 x 40 px), used with "sprite": "sign"
  Features: hiddenSpots (trees + prize lists with weights), fishing (gauge slots, cells 1/2 lists), the "ask" action (Yes/No -> variable 1/2) for rematches,
  port variables dw3:battle-won/<key> and dw3:card-won/<opponent> set when a battle ends, banner {area, place}, mod-flag/mod-var/set-var saved with the game.
  tools/make_art.py (Python standard library only) draws the PNGs and writes the stage.json files, encounters and texts from one layout; running it again gives
  the same files:  python3 pc/examples/mods/training-field/tools/make_art.py

Check:  boot "<disc>.bin" --validate-mod training-field   (expect 0 errors)

Use it as a base: copy the folder, rename it and the "training-field:" prefix, change the layout in make_art.py (or paint your own same-size PNG layers), move start/slots/battles,
drop the parts you do not want, write your texts, validate. See docs/modding/README.md section 6.11b and docs/stages.md sections 12-17.

Install: pc/examples/install_examples.sh --only training-field (switched off), enable in Options > Mods, restart. Remove it by deleting the folder:
a saved game inside a floor loads in Central Park. Nothing here comes from the game: all art and text are original.
