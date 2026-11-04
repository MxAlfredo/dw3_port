#ifndef PC_SPRITE_DATA_H
#define PC_SPRITE_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "image_data.h"

/*
 * Field sprites as PNG with their animations (feature field-sprites-png, T2). Evidence for the format:
 * docs/formats.md section 4.5.
 *
 * AAA/DAT/FIELD/SPRT/<NAME>.BIN (179 files) is an archive (table of offsets from the file's start, the first
 * word is the table's size). Every entry but the last is either a frame (one 4-bit TIM of its own, with one
 * 16-colour palette) or an animation (steps of four 32-bit words: time, frame entry, x, y; a 0 word ends it
 * looping, a -1 word ends it once). The LAST entry is the set table: records of six 16-bit words (the set
 * number, then the animation entry of each of the five direction slots; a set number of 0 ends it).
 * FIELDSTG_animateActor (src/field/field_mode/actor_control.c) is the reader.
 *
 * The extraction writes, for each file:
 *
 *     <data>/images/FIELD/SPRT/<NAME>/<EEE>.png    one indexed PNG per frame (EEE = the entry's index, 3 digits)
 *     <data>/images/FIELD/SPRT/<NAME>/sprite.json  the frames, animations and sets ("sprite-v1")
 *
 * and they go through the same manifest as the menu art (pristine, edited -> .new).
 */

typedef struct SprStats {
    int files;     /* sprite files extracted */
    int frames;    /* PNGs */
    int unused;    /* frames no animation uses */
    int anims;     /* animations (entries) */
    int sets;      /* set records */
    int failed;    /* images or animations that could not be converted */
} SprStats;

/* "AAA/DAT/FIELD/SPRT/P001PLAY.BIN" */
int sprIsSpriteFile(const char *discPath);

/* "FIELD/SPRT/P001PLAY/004.png" or ".../sprite.json" (what = NULL: the PNG of `entry`; else the file name).
   0 on success. */
int sprName(const char *discPath, int entry, const char *what, char *out, size_t cap);

/* The entries of the archive table (the first word is the table's size; entries run to the next offset). Returns how many
   (up to `max`), 0 when `file` is not a table. The substitution (image_data.c) uses it to find the frames. */
typedef struct SprEntry {
    size_t off, len;
} SprEntry;
int sprTable(const uint8_t *file, size_t len, SprEntry *out, int max);

/* The sprite-v1 text of one file and how many frames/animations it lists; malloc'ed, NULL on failure. */
char *sprJson(const char *discPath, const uint8_t *file, size_t len, size_t *outLen, SprStats *st);

/* Extract one disc file (frames, then sprite.json). Returns the number of PNGs; stats are added to *st. */
int sprExtractFile(ImgRun *run, const char *discPath, const uint8_t *file, size_t len, SprStats *st);

/* ---- sprites a mod brings (port-close S9, docs/stages.md section 11) ----
 *
 * mods/<mod>/sprites/<name>/sprite.json is the sprite-v1 file the exporter writes (frames, animations, sets; the ids are labels of the
 * file, not archive entries) with one PNG per frame: an indexed image of at most 16 colours (index 0 transparent through tRNS), at most
 * SPR_SLOT_MAX_W x SPR_SLOT_MAX_H pixels. A sprite is built into an entry range of a shared archive (the "bank") in the layout of the
 * game's own sprite files, so FIELDSTG_animateActor plays it unchanged; the whole bank is ONE file of the port (a virtual file), so any
 * number of sprites takes one slot of the 64 of the file cache. Optional member "actor_width" (pixels, 8-256): the width the field's
 * collision uses (default: the widest frame).
 */
#define SPR_SLOT_MAX_W 256 /* a texture page is 256 pixels wide at 4 bits: a frame never crosses it */
#define SPR_SLOT_MAX_H 224 /* the rows of the port's band */
#define SPR_ENTRIES_MAX 65535 /* an entry is the low 16 bits of the file-and-entry word */

typedef struct SprBank SprBank;
typedef struct SprInfo {
    int entry;  /* the set table's entry in the bank: what a character's file-entry word carries in its low 16 bits */
    int width;  /* the character's width for the field's collision */
    int w, h;   /* the largest frame an animation uses: the size of the VRAM slot it needs */
    int frames, anims, sets;
} SprInfo;

SprBank *sprBankNew(void);
void sprBankFree(SprBank *b);
/* The PNG named in sprite.json (a plain file name): its bytes, malloc'ed; 0 on success */
typedef int (*SprPngFn)(void *ctx, const char *name, uint8_t **data, size_t *len);
/* Builds a sprite from its sprite.json text into the bank. 0 on success (info filled); -1 with the reason in why and the bank unchanged. */
int sprBankAdd(SprBank *b, const char *json, size_t jsonLen, SprPngFn png, void *ctx, SprInfo *info, char *why, size_t whySize);
/* The same from <dir>/sprite.json and <dir>/<png> files. */
int sprBankAddDir(SprBank *b, const char *dir, SprInfo *info, char *why, size_t whySize);
int sprBankEntries(const SprBank *b);
/* The archive (table of offsets, then the entries), malloc'ed; NULL when the bank is empty or out of memory */
uint8_t *sprBankBuild(const SprBank *b, size_t *len);

/* The largest used frame of a game sprite file, from the set table at entry `setEntry`: pixels. 0 on success; -1 when it cannot be read. */
int sprMaxFrame(const uint8_t *file, size_t len, int setEntry, int *w, int *h);

/* ---- the port's VRAM band for extra actor images ----
 *
 * The game's own art lives at x 320 or more (texture pages at y 256) and PsyCross keeps the picture it draws (the frame buffer, x 0-319,
 * y 0-239) in the same VRAM: anything the port put there is overwritten every frame (seen in the S9 run: the slots came out as noise). The lower
 * left block is the one the field leaves empty: x 0-319, y 256-495 (measured on stage 330). The band is that block: the frames at y 256-479 (five texture
 * pages of 64 halfwords at y 256, filled in shelves) and the palettes at y 480-495 (20 of 16 halfwords a row; the rows from 496 hold the game's own).
 * An ActorImage is one slot of it: FIELDSTG_animateActor streams each frame of the actor into it. */
#define SPR_BAND_PAGES 5
#define SPR_BAND_PAGE_W 64
#define SPR_BAND_Y 256
#define SPR_BAND_ROWS 224
#define SPR_CLUT_Y 480
#define SPR_CLUT_ROWS 16
#define SPR_CLUTS_MAX (SPR_CLUT_ROWS * 20)
#define SPR_SHELVES_MAX 28

typedef struct SprShelf {
    int y, h, x; /* the shelf's first row and height, and where the next slot starts (halfwords) */
} SprShelf;
typedef struct SprVram {
    SprShelf shelf[SPR_BAND_PAGES][SPR_SHELVES_MAX];
    int nshelf[SPR_BAND_PAGES];
    int rows[SPR_BAND_PAGES]; /* rows the shelves of a page cover */
    int cluts;
} SprVram;
/* The values of an ActorImage (field_map.h) */
typedef struct SprSlot {
    int x, y, imageX, imageY, u, v, clutX, clutY;
} SprSlot;
void sprVramReset(SprVram *v);
/* A slot for frames up to w x h pixels (w 1-256, h 1-224) and its palette. 0 on success; -1 when it does not fit or the band is full. */
int sprVramAlloc(SprVram *v, int w, int h, SprSlot *out);
/* How many slots of w x h still fit (a copy of v is tried): the real cap for a size */
int sprVramCapacity(const SprVram *v, int w, int h);

#endif
