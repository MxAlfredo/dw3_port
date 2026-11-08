#ifndef PC_PARTNER_TRAIN_H
#define PC_PARTNER_TRAIN_H

/*
 * Importing the gym training scene of a partner's bundle (readable-assets A4b, ADR-016 section 6): partners/<key>/training/<training>/ holds one PNG per sprite
 * and training.json ("partners-training-v1", written by the exporter of partner_export.h). The importer is that export run backwards:
 *
 *   - a frame is cut into parts by palette: the PNG of the exporter keeps each of the original's palettes at 16 indices ("paletteBase" of training.json says where
 *     they start), so each becomes the same palette row again (a palette whose entry 0 is a visible colour takes two rows); a hand-made PNG without that hint
 *     has its visible colours packed 15 to a row, in palette order (entry 0 of a row is the PS1's transparent colour). A row's pixels become one part, or
 *     several when the box would be mostly empty (the parts never overlap, so their order does not matter). A palette range that training.json lists in
 *     "blend" gives its parts the PS1 blend mode of the original (semi-transparent bit and abr);
 *   - the parts are packed (shelves, identical parts once) into at most two 256x256 4-bit pages with at most 16 palette rows each: the footprint of a disc
 *     set, so STGTRAIN_loadSet puts them where it puts the disc's (x 0x300.., CLUTs at x 0x2C0.., see docs/formats.md 4.12);
 *   - the result is a training-file image of ONE set (a table whose first word points at the set, then the header, the bank, the animations and the TIMs), the
 *     bytes the game's readSet expects and the ones the exporter's peTrainOpen reads (that is how the tests compare it with the export).
 *
 * A part's x, y are bytes in the game; a frame whose origin falls outside 0..255 is shifted and the shift is taken off its animation steps, so no limit of
 * the PS1 stays. ptBuild is pure (no globals, no disc); the registry below it reads the bundle through the asset layers of mods.h.
 */

#include <stddef.h>
#include <stdint.h>

#include "png_read.h"
#include "pc/train_set.h"

#define PT_TRAININGS 12 /* the training files (rows 1..12 of STGTRAIN_files; rows 13..24 repeat them) */
#define PT_ANIMS 6
#define PT_PAGES 2      /* own pages of a set: the effect set's two follow them in VRAM */
#define PT_PAGE_ROWS 16 /* palette rows of one page, as a disc TIM */
#define PT_PARTNERS 256 /* PC_PARTNER_LIMIT */

typedef struct PtBlend {
    int lo, hi, mode; /* indices of the frame's palette that the game draws blended, and the mode 0..3 */
} PtBlend;

typedef struct PtFrame {
    int hasImage;
    PngImage png;
    int ox, oy; /* the image's top-left relative to the sprite's origin */
    int paletteBase; /* -1: a hand-made PNG (its colours are packed 15 to a palette row); 0 or 1: the exporter's layout, 16 indices per palette from here */
    int nBlend;
    PtBlend blend[16];
} PtFrame;

typedef struct PtStep {
    int frame, time, x, y;
} PtStep;

typedef struct PtAnim {
    int n;
    PtStep *step; /* NULL with n == 0: the set has no such animation */
} PtAnim;

typedef struct PtSource {
    int nFrames; /* sprite ids 0..nFrames-1; one without an image is an empty sprite */
    PtFrame *frame;
    PtAnim anim[PT_ANIMS];
} PtSource;

/* A file of the training folder by its name in training.json ("training.json", "003.png"); 0 with bytes (malloc'ed) and their count, else -1 */
typedef int (*PtReadFn)(void *ud, const char *name, uint8_t **bytes, size_t *len);

/* Reads training.json and the PNGs it names. `decode` 0 only reads the files (a caller that wants to hash them) and fills no pixels. 0 on success, else -1
   and a reason in `why`. */
int ptParse(PtReadFn read, void *ud, int decode, PtSource *out, char *why, size_t cap);
void ptSourceFree(PtSource *s);

/* The set as a training-file image: `file` is malloc'ed; the other fields are offsets into it. */
typedef struct PtSet {
    uint8_t *file;
    size_t len;
    uint32_t bank, bankOffset, bankSize;
    uint32_t anim[PT_ANIMS];
    uint32_t image[PT_PAGES];
    int nImages;
    int sprites, parts, rows[PT_PAGES];
} PtSet;

/* 0 on success; else -1 and a reason (a frame too big for a page, more palettes or pixels than two pages hold, an animation that names a frame that is
   not there) */
int ptBuild(const PtSource *src, PtSet *out, char *why, size_t cap);
void ptSetFree(PtSet *s);
void ptView(const PtSet *s, PcTrainView *v);

/* ---- the partners' sets ---- */

/* The twelve training folder names (the slug of each training's English name, partner_export.h peTrainFolders). ptLoadPartner reads them from the disc when
   they were not given (tests give them). */
void ptSetNames(char names[PT_TRAININGS][64]);
/* A new partner's stand-in: the partner whose training set it shows when it has none of its own (pcPartnerArt) */
void ptSetBase(int (*base)(int partner));
/* 1 when `name` is the folder name of one of the twelve trainings (partners/<key>/training/<name>/; case does not matter) */
int ptTrainingNamed(const char *name);
/* The training index (0..11) of a row of STGTRAIN_files */
int ptTrainingOfRow(int row);
/* Reads partners/<key>/training/<name>/ of the twelve trainings of `partner` through the asset layers (the mods from the last to the first, then data/; a
   folder as a whole, the first layer that has one that builds). An `original` partner (0..7) only counts a folder that is a mod's or that the user changed in
   data/ (a file whose hash is not partners/manifest.json's). Returns how many trainings it built a set for. Safe to call again for the same partner. */
int ptLoadPartner(int partner, const char *key, int original);
/* The edited originals' sets (after pbStart: the folder names come from pbOriginalKey); how many partners have one. ptLoadMod is ptLoadPartner for a partner
   of a mod (partner_ext.c's trainLoad hook; `key` is the partner's folder). */
int ptStartOriginals(void);
int ptLoadMod(int partner, const char *key);
/* Forgets the sets of partners `first` and up (8: the mods' partners, 0: all) */
void ptReset(int first);

#endif
