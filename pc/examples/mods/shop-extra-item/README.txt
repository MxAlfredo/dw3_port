shop-extra-item
===============

What it changes
  The Asuka armory sells one more weapon (item id 93) at the end of its list.

Where to see it
  the Asuka armory (shop 0): BUY, last row of the list.

How it is made
  Table patch: tables/shop_stock.json

Note
  Shop 0 is an armory. A mod that stocks a non-equipment item there (a key item, say) still shows it, just
  without equip stats, and buying it sends it to the bag without the equip prompt.

Install: pc/examples/install_examples.sh copies it into your mods/ folder (switched off), then
enable it in Options > Mods and restart. Remove it by deleting its folder.
Nothing here comes from the game: files that need game data are generated on your machine.
