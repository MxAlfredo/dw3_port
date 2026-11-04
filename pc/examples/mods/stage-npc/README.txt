stage-npc
=========

What it changes
  Central Park (stage 330): the character at index 0 of the stage's list (it stood far to the north) is moved next to where you start, and a new character, the "greeter", stands beside it. Talk to the greeter: the first time it says a welcome line, gives you 100 BIT and sets the mod flag stage-npc:met; from then on (the flag is saved with your game) it says another line and gives nothing.

  It also adds one exit and one tiny event (stage-patch-v1 sections "slots" and "events", docs/stages.md section 10):
    "slots" "add": the key "gate" has "stage": 380 (an exit to that stage, arriving at x 1186, y 874 facing 3, where the exit of stage 395 leaves you) and an "area": a rectangle of the map in pixels (x 960 to 1008, y 990 to 1030) where it fires. A slot the
                   game already has is fired by the cells of its map; a new one has none, so it brings its own rectangle. Stand in it and press cross.
    "events" "add": the key "thanks" is a script: wait 20 frames, show the named text "stage-npc:thanks" in a message box, wait for the box, end. The greeter's welcome talk starts it with
                   ["run-event", "stage-npc:thanks"] (the same action works from any talk). A mod's event has no C code and reads its lines from the stage's own text file.

Where to see it
  Central Park, stage 330 (warp token warp:330): the two characters stand a few steps from the point where you appear; the gate is a few steps east of where you appear, and the message shows after the greeter's welcome talk.

How it is made
  stages/330.json is a stage patch (stage-patch-v1, docs/stages.md section 7):
    "patch"  changes fields of a character the stage already has, by its index in the stage's list (28 here);
    "add"    makes a new character with a key ("stage-npc:greeter"), reusing a character id and an actor image the stage already has;
    talks    are lists in the code vocabulary: the first talk whose conditions hold is the one said, the last has none.
  ["mod-flag", "stage-npc:met", 1] is a condition (the flag is set) in the first talk and an action (set it) in the last.
  ["give-money", 100] is a port action (docs/stages.md section 8): it raises the money by 100, once, because the talk that holds it is only said while the flag is not set.
  The two lines are named texts: "text": "stage-npc:welcome" and "stage-npc:again" in the talks, and their words are in text/eng/stages.json and text/es-419/stages.json
  (stage-text-v1; English is required, a language without a line shows the English one).

Install: pc/examples/install_examples.sh copies it into your mods/ folder (switched off), then
enable it in Options > Mods and restart. Remove it by deleting its folder.
Nothing here comes from the game: the lines were written for this example, there is no art, only numbers and names.

  Two more characters (S9, docs/stages.md section 11) stand beside the start: the "visitor" is character 75 of the game (it belongs to stage 380, which Central Park does not load) and the "rival" is a
  sprite this mod brings, "sprites/rival/" (a green copy of the player's sprite). Neither says "image": each gets a VRAM slot of its own. The rival's PNGs are not in the repository (no game data): the install
  script makes them from your exported data/ (pc/examples/tools/gen_art.py); without them the rival is skipped with a log line.
