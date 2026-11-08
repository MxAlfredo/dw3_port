#ifndef PC_CONTENT_H
#define PC_CONTENT_H

/*
 * The shared content core (ADR-009 section 3): what every content family (items, techniques, later Digimon) needs from
 * the content mods, written once.
 *
 *   - a type descriptor (ContentType): the kind name, the file and format a mod uses, the original id range, the record
 *     size, how to copy a base record and how to check and apply the type's own JSON fields;
 *   - loading mods/<mod>/content/<file>.json of every active content mod, the extended ids (deterministic: the
 *     alphabetical order of "<mod>:<key>"), the record store and the name <-> id resolution;
 *   - the per-language texts (es-419 -> spa -> eng) the text layer serves;
 *   - the byte helpers of the name chunks of dw3save-v1 (ext_state.c: "EXTI", "EXTT");
 *   - the original names: "dw3:<kind>/<id>" (ADR-009 section 2), accepted wherever a mod names an original id;
 *   - the common log lines ("<path> <kind> "<name>" rejected: ...").
 *
 * A type lives in its own file (item_ext.c, tech_ext.c) as a ContentType plus its public functions; it owns no allocator,
 * store, text or save code. Portable C: no paths except the ones modsDirOf gives (ADR-009 section 7). This header pulls
 * in no game header, so the save code can use the byte helpers without the game.
 *
 * The mods' format (items-v1, techs-v1), the ids and the saved chunks are the same as before the core existed.
 */

#include <stddef.h>
#include <stdint.h>

/* ---- a small JSON reader (the mod tables' files)
   t: 0 number, 1 string, 2 object, 3 other (a boolean: num is 1 for true, 0 for false and null), 4 list (the elements in v, n of them; keys NULL) ---- */
typedef struct ExtJson {
    int t;
    double num;
    char *str;
    int n;
    char **keys;
    struct ExtJson *v;
} ExtJson;

#define EXT_NLANG 6
extern const char *const EXT_LANGS[EXT_NLANG]; /* "eng", "fra", "ita", "ger", "spa", "es-419" */

/* 0 and an object in *out, or -1 (not JSON, or not an object; nothing to free) */
int extJsonParse(const uint8_t *data, size_t len, ExtJson *out);
void extJsonFree(ExtJson *j);
const ExtJson *extJsonGet(const ExtJson *o, const char *key); /* NULL when absent or `o` is no object */
int extWhole(double d, long lo, long hi);                      /* d is a whole number in lo..hi */
int extValidKey(const char *key);                              /* 1-48 characters of a-z, 0-9, - and _ */
/* {"eng": "...", "es-419": "..."} into text[language]; bad entries are logged ("<kind>s" channel) and skipped */
void extTakeTexts(char *text[EXT_NLANG], const ExtJson *o, const char *kind, const char *path, const char *name, const char *what);
/* The text for a language folder name, falling back es-419 -> spa -> eng; NULL when English is missing too */
const char *extPickText(char *const text[EXT_NLANG], const char *langDir);

/* ---- a type and its loaded set ---- */

typedef struct ContentEntry {
    char name[96]; /* <mod>:<key> */
    char *path;    /* the file it came from, for the log */
    int baseId;
    char *text[2][EXT_NLANG]; /* [name, description][language], NULL = not given */
    void *rec;                /* the type's record (ContentType.recSize bytes), a copy of the base with the overrides */
} ContentEntry;

struct ContentSet;

typedef struct ContentType {
    const char *kind;   /* "item", "tech": the word of the log lines and of "dw3:<kind>/<id>" */
    const char *noun;   /* "item", "technique": what a base is, in the log */
    const char *plural; /* "items", "techniques": what "new" holds, in the log */
    const char *channel;/* the log channel, and the file/format stem: content/<channel>.json, "<channel>-v1" */
    int origMin, origMax; /* the original ids a mod can name (a base, "dw3:<kind>/<id>") */
    int firstExtId;       /* the id of the first new entry */
    int maxExt;           /* the most new entries; the rest (alphabetical) are dropped */
    const char *limitNote; /* printf(declared, kept) logged when entries are dropped, or NULL: dropped silently */
    size_t recSize;

    /* Fills `rec` (zeroed) from original id `baseId`; 0: unavailable, the entry is skipped silently */
    int (*copyBase)(int baseId, void *rec);
    /* One field of the entry's JSON other than base/name/description: 1 handled (accepted or rejected with its own log
       line), 0 unknown (the core logs it) */
    int (*field)(const ContentEntry *e, const char *key, const ExtJson *v);
    void (*freeRec)(void *rec); /* optional: what the record owns */
    /* After the ids are fixed and logged, with the set filled: take the records into the game (store, redirects, the
       hooks via contentAttach). 0: failed, everything is dropped */
    int (*publish)(struct ContentSet *s);

    /* The type's public hooks, handed to the text layer and the save code by contentAttach */
    const char *(*nameOf)(int id);
    int (*idOf)(const char *name);
    int (*count)(void);
    const char *(*text)(int index, const char *langDir, int desc);
    void (*setNameHooks)(const char *(*nameOf)(int), int (*idOf)(const char *));
    void (*setTextHook)(int (*count)(void), const char *(*text)(int, const char *, int));

    /* The reference export (contentReferenceText), both optional */
    const char *refNote; /* the fields a mod can set, one sentence, for the file's notes (plain ASCII, no quotes) */
    /* The settable fields of original `id` as JSON members ("price": 10, "sellPrice": 5), no braces; 0: no such record */
    int (*describe)(int id, char *out, size_t cap);

    /* Optional: called once all the entry's fields were read, before it gets an id. 0 rejects it (the type has logged why), so a type
       whose entry needs a field (a partner's first form) can refuse one without it and the ids stay those of the entries that remain. */
    int (*accept)(const ContentEntry *e);
    /* 1: "base" may be left out (baseId is then -1 and copyBase is called with it); a "base" that is there must still be an original */
    int baseOptional;

    /* A type kept one entry per folder instead of one file (cards, A8.1): `folder` is the topic folder ("cards") of every content mod, each of its
       subfolders <key> holds `recordFile` ("card.json"), whose "format" is `recordFormat` ("cards-card-v1"), whose "name" (optional) is the folder's
       name, and whose other members are the entry's fields as in content/<channel>.json ("title" and "description" are the language texts; the
       members "$schema" and "notes" are ignored). NULL: the type reads mods/<mod>/content/<channel>.json ("new" map) as before. */
    const char *folder;
    const char *recordFile;
    const char *recordFormat;
    /* Optional, folder types: 1 when a folder key is not an entry of this type (a card's c<nnn> is an original card's art folder): skipped without a line */
    int (*reservedKey)(const char *key);
} ContentType;

typedef struct ContentSet {
    const ContentType *t;
    ContentEntry *e;
    int n;
} ContentSet;

/* Reads every active content mod's content/<channel>.json (or, for a folder type, its <folder>/<key>/<recordFile> files), assigns ids, logs them and calls publish. Returns the number
   of entries kept (the set is empty and nothing is published for 0). Call contentUnload first when reloading. */
int contentLoad(ContentSet *s);
/* Detaches the hooks and frees the set (the type puts its own redirects back first) */
void contentUnload(ContentSet *s);
/* Sets the type's name and text hooks (publish calls it when the type's store is ready) */
void contentAttach(const ContentSet *s);

int contentCount(const ContentSet *s);
int contentIdOf(const ContentSet *s, const char *fullName);   /* the id of a new entry, or -1 */
const char *contentNameOf(const ContentSet *s, int id);       /* "<mod>:<key>", or NULL when no new entry has that id */
void *contentRec(const ContentSet *s, int index);             /* the record of entry `index`, or NULL */
/* The text of entry `index` in a language folder name: desc 0 the name (falls back to the full name), 1 the description
   (may be ""). Never NULL; "" for an index out of range. */
const char *contentText(const ContentSet *s, int index, const char *langDir, int desc);

/* The original name "dw3:<kind>/<id>": the id when `name` is one of `kind` and the id is in origMin..origMax, else -1 */
int contentOriginalId(const ContentType *t, const char *name);
/* "dw3:<kind>/<id>" for any kind and any non-negative id (no range): the id, or -1 */
int contentNamedId(const char *kind, const char *name);
/* An original name, or a new entry's "<mod>:<key>": the id, or -1 */
int contentResolve(const ContentSet *s, const char *name);
/* "dw3:<kind>/<id>" into out (size cap) */
void contentOriginalName(const ContentType *t, int id, char *out, size_t cap);

/* The reference copy of the type's ORIGINAL entries, format "content-ref-v1" (written to data/content/<channel>.json by the
   host, ADR-005): for every original id its name "dw3:<kind>/<id>", its English name (engName(id), NULL: left out) and the
   fields a mod can set, as the type's describe gives them. Malloc'd text, or NULL when memory ran out. The English names
   are read from the player's disc at run time and never stored in the repository (ADR-001). */
char *contentReferenceText(const ContentType *t, const char *(*engName)(int id));

/* ---- the name chunks of dw3save-v1 (little endian) ----
   header: u32 chunk version (1), u32 entry count; a name entry is u16 length + the bytes (no NUL). */
static inline void contentPut16(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}
static inline void contentPut32(unsigned char *p, unsigned long v) {
    contentPut16(p, v);
    contentPut16(p + 2, v >> 16);
}
static inline unsigned long contentGet16(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}
static inline unsigned long contentGet32(const unsigned char *p) {
    return contentGet16(p) | (contentGet16(p + 2) << 16);
}
/* Opens a chunk: 0 and the entry count in *count, or -1 (shorter than the header, or not version 1) */
static inline int contentChunkOpen(const unsigned char *in, unsigned long len, unsigned long *count) {
    if (len < 8 || contentGet32(in) != 1) return -1;
    *count = contentGet32(in + 4);
    return 0;
}
/* Reads the name whose u16 length is at `lenPos`, into name (cap bytes): its length, or 0 when the entry is damaged (cut
   off, empty, or too long); `tail` bytes must follow the name inside `len`. */
static inline unsigned long contentReadName(const unsigned char *in, unsigned long lenPos, unsigned long len, unsigned long tail,
                                            char *name, size_t cap) {
    unsigned long n;
    size_t i;
    if (lenPos + 2 > len) return 0;
    n = contentGet16(in + lenPos);
    if (n == 0 || n >= cap || lenPos + 2 + n + tail > len) return 0;
    for (i = 0; i < n; i++) name[i] = (char)in[lenPos + 2 + i];
    name[n] = '\0';
    return n;
}
/* Writes u16 length + name at `out`: the bytes written */
static inline unsigned long contentWriteName(unsigned char *out, const char *name, unsigned long n) {
    unsigned long i;
    contentPut16(out, n);
    for (i = 0; i < n; i++) out[2 + i] = (unsigned char)name[i];
    return 2 + n;
}

#endif
