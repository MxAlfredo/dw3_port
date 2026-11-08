betamon-pack
============

What it changes
  Adds a new partner, Betamon, who digivolves to Seadramon and MegaSeadramon, and a fourth starter team on the team
  screen ("River pack": Veemon, Betamon and Kumamon). A Tamer who chose another team can get Betamon with a DDNA quest, shaped like the
  game's own (Central Park).

Where to see it
  Start a new game: the team screen shows the River pack (D) with Betamon's own portrait. Central Park (stage 330), at the
  bottom where you arrive, with the story at progress 14-38 and the party's levels adding up to 45-74: talk to the
  fisher twice (he asks for Betamon's DDNA; the park is made again and a river serpent shows up on the bank), talk to
  the serpent (event battle against MegaSeadramon), talk to it again after winning (you get "Betamon DDNA" and the
  item-received icon flashes), and show the DDNA to the fisher (he unlocks Betamon; the DDNA stays in your bag). Betamon fights with the battle model of the disc's Betamon enemy, and learns
  Seadramon (Betamon level 5, partner level 10) and MegaSeadramon (Seadramon level 20, partner level 30) after a battle.

How it is made
  Content mod (guide 6.9b, "Making a partner pack"):
    partners/betamon/partner.json     the partner: base, first form, texts (eng, es-419), evolution list
    partners/betamon/                 portrait.png, icon.png, field/ and training/ (art rendered from SpriteKiln models, see tools/)
    digimon/<form>/digimon.json       Betamon, Seadramon, MegaSeadramon: a partner template, the disc enemy's model, motions and
                                      effects by reference ("dw3:digimon/4", "10", "61"), stats and growth
    content/teams.json                the fourth starter team
    tables/special_conditions.json    the rows the quest uses: unlock, still locked, story window, level sum 45 and 75
    tables/field_encounters.json      the MegaSeadramon event battle
    content/items.json                the item Betamon DDNA
    stages/s330/stage.json            the fisher and the river serpent (sprites/river-digimon, tools/make_art.py);
                                      text/<language>/stages.json has the lines
  The balance was checked with tools/balance_compare.py (level 30): every stat of every form is inside the range of the
  eight original partners.

Art
  The art (partners/betamon/ and sprites/river-digimon/) is rendered by tools/make_art.py from the SpriteKiln models in
  tools/specs/ (Betamon and MegaSeadramon), which were written for this project. Credit: DW3 port (as in mod.json). SpriteKiln
  (MIT, https://github.com/LucasJRamos/spritekiln) is an optional art tool: the port only reads the PNG and JSON files, so
  hand-drawn art works the same (overwrite the PNGs, same names and sizes). To regenerate: install SpriteKiln 0.3.0 in a venv
  (see the header of tools/make_art.py and docs/modding/README.md, "2D art with SpriteKiln") and run
  ~/.cache/spritekiln-venv/bin/python tools/make_art.py. tools/test_make_art.py checks that two runs give the same bytes.
