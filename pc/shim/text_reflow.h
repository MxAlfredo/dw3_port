#ifndef PC_TEXT_REFLOW_H
#define PC_TEXT_REFLOW_H

#include <stddef.h>
#include <stdint.h>

#include "text_codec.h"

/*
 * Automatic reflow of text that does not fit its box (feature es419-live-option, L4a).
 *
 * The engine never wraps: a line ends at an explicit `02 01` and a page at `02 03` (the strings
 * write the pair as `{pagina}`, wait plus page break). A translation that is longer than the
 * original would run out of the box, and the port does not shorten text to fit (the PS1 limits
 * do not apply to it). So the port adds the line and page breaks the longer text needs.
 *
 * trReflow works on the raw bytes of one string (not on the token text: the control codes have
 * arguments that look like characters in it). A page whose lines all fit its box is kept
 * byte for byte, original breaks included. A page with a line that does not fit is joined
 * into one paragraph and wrapped greedily at the spaces; when it needs more lines than the box
 * has, a page break is inserted. Waits and every other control code stay where they were, glued
 * to the word they follow. The speaker label (`02 07 ... 02 07`) is not measured. Inserts
 * (`02 05 n`, the player name and numbers) cannot be measured here: a fixed allowance is reserved.
 *
 * The width of every glyph comes from a callback, so the unit is pure (stdlib only). The port's
 * callback (trEngineMetrics) reads the game's own glyph tables, the ones measureText uses.
 */

/* kind 0: a glyph (value = the byte, 4 and up); 1: an icon (value = n, `01 n`); 2: a missing
   character. Returns the advance in pixels, as measureText adds it. */
typedef int (*TrWidthFn)(void *ctx, int kind, int value);

typedef struct TrOptions {
    TrWidthFn width;       /* NULL: no metrics, nothing is reflowed */
    void *ctx;
    uint8_t space[2];      /* the bytes of a space (`01 01`) */
    int nameAllowance;     /* px reserved for an insert that shows the player name */
    int numberAllowance;   /* px reserved for any other insert (a number) */
} TrOptions;

typedef struct TrBudget {
    int maxWidth;          /* px of the widest line */
    int maxLines;          /* lines in a page; 1 means the text cannot be wrapped */
} TrBudget;

typedef struct TrStats {
    int changed;           /* strings rewritten */
    int pagesAdded;        /* page breaks inserted */
    int unfit;             /* strings that still have a line wider than the budget */
    int checked;           /* strings looked at */
} TrStats;

/* Fills the allowances from the metrics: 8 name characters of the widest letter, 6 digits */
void trOptionsInit(TrOptions *o, TrWidthFn width, void *ctx);

/* The game's glyph tables as metrics (style 1, the one the dialogue boxes use). Returns 0 and
   leaves `o` untouched when the tables are not linked (the pure unit tests). */
int trEngineMetrics(TrOptions *o);

/* Width of the widest line of a string (speaker label excluded, inserts at their allowance) */
int trMaxLine(const TrOptions *o, const uint8_t *s, size_t len);

/* Returns 0 when the string is kept as it is (*out untouched), 1 when it was rewritten (*out is
   malloc'ed), -1 when it could not be parsed (an unknown control code; it is kept as it is).
   `unfit` (may be NULL) is set to 1 when a line is still wider than the budget in the result. */
int trReflow(const TrOptions *o, const TrBudget *b, const uint8_t *s, size_t len, uint8_t **out,
             size_t *outLen, int *pagesAdded, int *unfit);

/* ---- budgets (localization/layout.json) ---- */

typedef struct TrEntryRule {
    int part;              /* -1: plain file */
    int index;
    int noreflow;
    int maxWidth;          /* 0: the file's */
    int maxLines;
} TrEntryRule;

typedef struct TrFileRule {
    char pattern[40];      /* a file base name ("TALK00"), or a prefix ending in `*` ("TALK*") */
    int noreflow;
    int maxWidth;          /* px of the box */
    int maxLines;          /* default 3 */
    int tightWidth;        /* 0: off; else entries whose original text fits it get this width */
    int tightBudget;       /* 0: tightWidth; else the width such entries get (L4b: the wider talk box); the class stays tightWidth */
    int entryCount;
    TrEntryRule *entries;
} TrFileRule;

typedef struct TrLayout {
    int count;
    TrFileRule *files;
} TrLayout;

int trLayoutParse(const char *json, size_t len, TrLayout *out, char *err, size_t errSize);
void trLayoutFree(TrLayout *l);
/* The rule of a file base name ("TALK03"): exact name first, then the longest prefix; NULL if none */
const TrFileRule *trLayoutFind(const TrLayout *l, const char *base);

/* ---- a whole document ---- */

/* Reflows the entries of `doc` that differ from `base` (the same file as the disc has it: the
   untouched retail text is never rewritten). The budget of an entry is the rule's width, or its
   tight width when the original text fits it (a talk box); entry rules override. `name` is for
   the log line on stderr. Returns 0 (stats filled) or -1. */
int trReflowDoc(TcDoc *doc, const TcDoc *base, const TrOptions *o, const TrFileRule *rule,
                const char *name, TrStats *stats, char *err, size_t errSize);

#endif
