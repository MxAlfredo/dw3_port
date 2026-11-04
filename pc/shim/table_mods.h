#ifndef PC_TABLE_MODS_H
#define PC_TABLE_MODS_H

/*
 * Data table mods (ADR-007): the game's tables are C arrays of the host binary, so a mod can change
 * their rows in memory. A table is described once (name, where its rows live, the key of each row
 * and its fields with offset, type and range); the engine does the rest:
 *
 *  - the reference export <data>/tables/<name>.json (format "<name>-v1": the original values, the
 *    keys, a "notes" object with the field ranges). It is a reference, never a source: editing it
 *    changes nothing. Same manifest and ".new" rule as the other generated files (ADR-005);
 *  - the patches <mod>/tables/<name>.json of the active mods (format "<name>-v1", a "rows" object);
 *  - the application: the table is put back to its original values and every patch is applied in mod
 *    order, so applying it again never adds up. With no patch for a table, the table is never written.
 *
 * A field can also be a list (TmField.listMax), for the tables whose rows point at an array of ids (the
 * shops' stock): `"items": [1, 2, 3]` replaces it, `"items": {"remove": [1], "add": [9]}` edits it.
 *
 * Log lines: `tables: <path> rejected: <why>` (the whole file), `tables: <path> row "12" field "exp"
 * rejected: <why>` (one field; the rest of the patch still applies) and, at the first application,
 * `tables: battle_rewards patched by mods/a, mods/b (3 rows)`.
 *
 * The host decides WHEN (ADR-007 section 6): a table of the executable once at boot, one of an overlay
 * on entering its mode, after state_scope restored the overlay's data.
 */

#include <stddef.h>

/* TmField.countOff of a list whose row holds no element count */
#define TM_NO_COUNT ((size_t)-1)

typedef enum TmType { TM_S32, TM_U32, TM_S16, TM_U16, TM_S8, TM_U8 } TmType;

typedef struct TmField {
    const char *name;
    TmType type;
    size_t off;          /* offset of the value inside a row */
    long long min, max;  /* valid range (inside the range of the type) */
    const char *note;    /* what it is; shown in the reference export */
    /* Optional, after the positional members. A LIST field (listMax > 0): the row holds a pointer to the
       elements at `off` (each of the field's type, the list is ended by a 0 element, so min must be >= 1)
       and the element count, an s32, at countOff. A patch gives the whole list (an array) or edits it
       ({"add": [...], "remove": [...]}); the engine keeps its own buffers for the patched lists and points
       the row at them, so the original arrays are never written. A list has 1 to listMax elements; one
       that would be longer, empty or hold a value outside min..max is rejected, with a log line. Only
       for tables that live in memory (tmRegister), and every original list must fit listMax. */
    int listMax;
    size_t countOff; /* TM_NO_COUNT: the row has no count (the game counts up to the 0 itself); the engine then writes none */
    /* Optional, for a LIST field whose ids can be added by mods (the shops' items). byName: a string in the list
       is a name the port resolves to an id (negative: unknown, the list is rejected). idOk: whether a number
       inside min..max may be listed (an id no installed mod defines is rejected). */
    long long (*byName)(const char *name);
    int (*idOk)(long long id);
    /* The same two hooks on a SCALAR field that holds an id (battle_loot's item): a patch may then give a name
       ("<mod>:<name>", resolved by byName; a negative or out-of-range answer rejects that field) or a number in
       min..max that idOk accepts; "*k"/"+k"/"-k" results that idOk refuses leave the field unchanged. */
    /* Optional, after the positional members. mask nonzero: the field is the bits of the unsigned value at `off` that the mask
       selects (contiguous), read and written shifted down so min..max is the value of those bits (a card id in the low 12 bits of a
       word whose top bit is a flag); the other bits of the word are never changed. Not for lists. */
    unsigned long long mask;
} TmField;

typedef struct TmTable {
    const char *name;    /* "battle_rewards": file name and format "<name>-v1" */
    void *base;          /* row 0 */
    size_t stride;       /* bytes per row */
    int rows;            /* row count */
    long keyBase;        /* the key of row i is keyBase + i */
    const char *keyNote; /* what the key is (reference export) */
    const char *whenNote;/* when a change shows (reference export) */
    const TmField *fields;
    int nfields;
    /* Optional, after the positional members. A table whose key is a field of the row (a record id,
       not a position): keyFromRow nonzero, the key is the keyType value at keyOff of each row and
       keyBase is unused; rows with key 0 end a file-backed table. */
    int keyFromRow;
    size_t keyOff;
    TmType keyType;
    /* Optional, after the positional members. Rows that live outside the table, added by content mods (the item
       records of new items): a patch row keyed by a NAME ("<mod>:<name>", a key that is not a plain number) is
       resolved with keyByName (id >= 0, or negative: no such row, the row is rejected with a log line) and applied
       to the record extRow(id) returns (same layout as a row, NULL: gone). Only for tables of memory (tmRegister).
       The engine remembers each field's value before its first change and puts it back on the next tmApply, so
       applying again never adds up; the "*" row never reaches these records. */
    long long (*keyByName)(const char *name);
    void *(*extRow)(long long id);
    /* Optional, after the positional members. keys: the key of row i (rows entries), for a table whose rows are not
       numbered keyBase + i (the item ids of the usable items). rowAt: row i lives at rowAt(i), not in one array
       (a table of separate records; needs keys, base NULL, no list fields): the engine keeps a contiguous copy,
       restores and patches that, and writes each row back through rowAt on every tmApply. rowCheck: run on every
       row after all the patches (tmApply); it returns NULL when the row is one the game handles or the reason it
       is not, and the row then keeps its original values (log line `tables: <name> row <key> rejected: <why>`). */
    const long *keys;
    void *(*rowAt)(int row);
    const char *(*rowCheck)(const void *row);
    /* Optional, after the positional members. rowLabel: a human label of row i for the reference export (the Digimon an
       enemy row names): written into buf (cap bytes), empty for none. The export lists the non-empty ones in a top-level
       "labels" object ({"<key>": "text"}); like "notes", it is only a help to read the file and no patch reads it. */
    void (*rowLabel)(int row, char *buf, size_t cap);
} TmTable;

/* The data folder of the reference exports (NULL: no export). Forgets every registered table. Call
   after modsInit and before tmRegister. */
void tmInit(const char *dataDir);

/* Writes a generated reference file <data>/<family>/<rel> (family: "content"...) with its manifest, <family>/manifest.json,
   under the same rule as the tables' reference copies: an unedited copy is replaced, an edited one stays and the new
   version goes beside it as <rel>.new. Needs tmInit with a data folder; otherwise it does nothing. */
void tmExportFile(const char *family, const char *rel, const char *text);

/* Registers a table: snapshots its original rows (the table must still hold them), writes the
   reference export, and reads the patches of the active mods. The descriptor and the rows must
   outlive the engine. Returns 0, or -1 for a bad descriptor or no memory. */
int tmRegister(const TmTable *t);

/* A table that lives in a disc file, patched in the buffer each time the file is read (ADR-007
   section 6): `orig` holds the file's disc bytes (len of them) for the reference export. Two kinds:
   keyed (keyFromRow: the rows are counted from the bytes, t->base and t->rows are ignored, the first
   row with key 0 ends it) and positional (no keyFromRow, no keys: the key of row i is keyBase + i and
   t->rows rows are taken from the start of the file, for a file whose rows have no key and whose last
   sector is padded). A rowCheck of the table runs on the patched rows (after every patch) of a file
   too, and a row it refuses is put back as the buffer held it before the patches. Returns 0, or -1 as
   tmRegister. */
int tmRegisterFile(const TmTable *t, const void *orig, size_t len);

/* Applies every patch of a file-backed table to `buf`, the bytes just read (from the disc, or a raw
   override of the same file: the patch goes on top of whatever the buffer holds, so a fresh read always
   starts from the source and applying never adds up). Rows are found in the buffer by key. Returns how
   many rows changed (0 and nothing written when no mod patches the table; -1 when the table is not a
   registered file-backed one). */
int tmPatchBuffer(const char *name, void *buf, size_t len);

/* The original bytes of the row with that key (NULL: unknown table or key). Valid until tmClear. */
const void *tmOriginalRow(const char *name, long key);

/* Writes "mods/a, mods/b" (the mods that patch the table, in order) into out; returns how many */
int tmWho(const char *name, char *out, size_t cap);

/* Puts the table back to its original rows and applies every patch in mod order. Returns how many
   rows differ from the original afterwards (0, and nothing written, when no mod patches it; -1 when
   the table is not registered). */
int tmApply(const char *name);

/* How many mods have a valid patch for the table (0 when unknown). */
int tmPatchCount(const char *name);

/* The registered tables, in registration order (--validate-mod lists the ones a mod's tables/ folder names wrongly) */
int tmTableCount(void);
const char *tmTableName(int i);

void tmClear(void);

#endif
