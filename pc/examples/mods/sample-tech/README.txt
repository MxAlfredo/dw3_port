sample-tech
===========

What it changes
  Adds two new techniques: Spark Strike (an attack) and Mend Spark (a healing technique based on an original field healing one). Form 3 (Agumon, used by pc/review.sh tecnica) learns them in skill slots 1 and 2 at level 2; form 374 (Kyubimon, the first battle form of partner 6, present in the libre party and in an advanced save) learns Spark Strike in slot 5 (level 6) and Mend Spark in slot 4 (level 8).

  A learnedBy slot is learned only if the form has not filled that slot yet: after each won battle the game gives ONE technique, the first empty slot whose level the form has reached, and never replaces a filled slot. An advanced save has already filled the early slots with the original techniques, so use later slots (here slot 5, empty in the original, and slot 4, which replaces original technique 150, level 55) and a level at or below the form's level. A technique in a slot is usable in battle at once; the "known" mark (Digilab, passing it on) follows the form's own knownLevels (slot 5: immediately; slot 4: level 99).

Where to see it
  Battle > Tech menu of a partner of form 3 (pc/review.sh tecnica) or form 374 after winning a battle or two. The field Techniques screen lists healing techniques only: Mend Spark shows there and heals from the menu (pc/review.sh tecnicacampo); the attack does not.

How it is made
  Content mod: content/techs.json

Install: pc/examples/install_examples.sh copies it into your mods/ folder (switched off), then
enable it in Options > Mods and restart. Remove it by deleting its folder.
Nothing here comes from the game: files that need game data are generated on your machine.
