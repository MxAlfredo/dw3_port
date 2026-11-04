#ifndef PC_OPTIONS_MODS_H
#define PC_OPTIONS_MODS_H

/*
 * The options menu's Mods tab (feature modding-ux, U6): every installed mod as one row, so a player with no file
 * editor (Android) can see and manage them.
 *
 * Each row shows the mod's name and its state: "On 2" (second in the load order), "Off", "Blocked" (a dependency
 * or the port version stops it), "Invalid" (a bad mod.json) or "Missing" (listed in [mods] order but not
 * installed). The line under the list says the version, the kind and, for a blocked or rejected mod, the reason.
 * Confirm turns a mod on or off, Left and Right move it earlier or later in the load order (the last one wins).
 * Nothing applies live: the choice is written to the [mods] lines of settings.ini (`order`, only once the player
 * reordered or the file already had one, and `disabled`) and "Restart to apply" shows until the game restarts.
 * Without mods the tab has one line saying where they go.
 *
 * The rows are built from the report of the last modsInit (shim/mods.h), so call this after it. The game's font has
 * no commas or parentheses: the texts it shows are mapped to spaces and dashes.
 */

/* Adds the rows to the Mods tab (called by optionsSystemInit, which the video options already call) */
void optionsModsInit(void);
/* 1 when what the tab holds differs from what this run started with (a restart is needed) */
int optionsModsPending(void);
/* How many mods the rows list (0 for the empty-state line) */
int optionsModsListed(void);

#endif
