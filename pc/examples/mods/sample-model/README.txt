sample-model
============

What it changes
  Adds an enemy with its own 3D model (a robot made of boxes) in the same two Central Park encounters, and a partner
  form ("Boxpal", "Cajamigo" in es-419) with its own 3D model, which Kotemon gets at level 3.

Where to see it
  Random battles in Central Park (stage 330), encounters 34 and 35: the second enemy is "Boxbot" ("Cajabot" in es-419),
  level 3, a robot of four striped boxes (blue, green, red) with a yellow arm. The battle message names it.

How it is made
  Content mod with a glTF model baked at load:
    digimon/boxbot/digimon.json   the Digimon (digimon-digimon-v1): name, base enemy, texts, stats; no "model", so its own model/ is used
    digimon/boxbot/model/         the neutral source: model.json (model-src-v1), robot.gltf, robot.bin, robot.png
    content/encounters.json       puts it in the second slot of Central Park's encounters 34 and 35
    digimon/boxpal/               the partner form (base dw3:digimon/383): digimon.json and model/ whose model.json names an animation for
                                  each motion slot a partner plays (idle, weak idle, dodge, flinch, hit, knock out, cheer, blast, attack);
                                  generated with `python3 -I tools/test_model_import.py --demo-partner <folder>` (guide 8.2c); its glTF carries
                                  the buffer inside (no .bin file)

Install: pc/examples/install_examples.sh copies it into your mods/ folder (switched off), then
enable it in Options > Mods and restart. Remove it by deleting its folder.
Nothing here comes from the game: files that need game data are generated on your machine.
