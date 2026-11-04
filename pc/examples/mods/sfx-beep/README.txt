sfx-beep
========

What it changes
  Replaces one sound effect of the common bank (sample 50) with a synthetic two-tone beep.

Where to see it
  Open the field menu (Start) and move the cursor or confirm: the short blip is sample 50.
  Sample 50 is the cursor/confirm sound every menu shares (field, shops, battle, even the port's options
  menu on F1), so the beep plays in all of them: a sample mod changes the sound everywhere it is used.

How it is made
  Audio mod: audio/COMMON/vag_050.wav, a synthetic WAV generated at install time.

Install: pc/examples/install_examples.sh copies it into your mods/ folder (switched off), then
enable it in Options > Mods and restart. Remove it by deleting its folder.
Nothing here comes from the game: files that need game data are generated on your machine.
