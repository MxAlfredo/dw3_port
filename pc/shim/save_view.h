#ifndef PC_SAVE_VIEW_H
#define PC_SAVE_VIEW_H

/*
 * The read-only readable view of a save (extended-state T3b): on every save, next to slotN.dw3save the port writes
 * slotN.json, format "dw3save-view-v1". It is a convenience for people who want to look at a save (or diff two); the
 * game never reads it, editing it changes nothing, and a failure to write it is only logged. Its "notes" field says so.
 *
 * Content: slot, sequence, port version, the active mods, the player's name, money, play time, place and progress,
 * the party, the unlocked partners (name, level, HP) and the inventory as {id, name, count[, equipped]}: original
 * items by their number, extended ones ("<mod>:<name>") by the id they have now plus their full name; and "modTechs",
 * the techniques of mods the partners' entries hold ({partner, entry, slot, id, name}; none without mods), and "modForms",
 * the new partner forms they hold ({partner, entry, id, name}).
 *
 * The names come from callbacks the host sets (the text layer and the mod registry): without them the view has ids.
 */

typedef struct SaveViewCtx {
    int modCount;
    const char *(*modName)(int i);
    const char *(*modVersion)(int i);
    /* the name of an item id (original: the item name file; extended: "<mod>:<name>"), or NULL */
    const char *(*itemName)(int id);
    /* the name of a technique id added by a mod ("<mod>:<name>"), or NULL */
    const char *(*techName)(int id);
    /* the name of a new partner form id added by a mod ("<mod>:<key>"), or NULL */
    const char *(*formName)(int id);
} SaveViewCtx;

void saveViewSetContext(const SaveViewCtx *ctx); /* NULL: no names, no mods */

/* The view of a slot's original save block (`gameLen` bytes of GAME, at least the saved part) and of the extended
   items as they are now; malloc'ed JSON, or NULL */
char *saveViewBuild(const unsigned char *game, unsigned long gameLen, int slot, unsigned sequence);

/* Writes `text` to `path` atomically (temporary file, then rename). 1 on success */
int saveViewWriteFile(const char *path, const char *text);

#endif
