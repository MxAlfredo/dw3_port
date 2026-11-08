#ifndef PC_PARTNER_EXPORT_H
#define PC_PARTNER_EXPORT_H

/*
 * One readable bundle per original partner (readable-assets A3, ADR-016 section 6): everything 2D the game draws of the eight partners, each asset once,
 * with its real colours, so a modder needs no PS1 knowledge:
 *
 *   <data>/partners/<name>/partner.json     "partners-partner-v1": name, disc identity, first form, what the other files are
 *   <data>/partners/<name>/portrait.png     indexed strip, 32 pixels high and 32 x N wide (N = 3 or 4 cells), the partner's own palette
 *   <data>/partners/<name>/icon.png         44x28 memory card icon
 *   <data>/partners/<name>/field/           the follower sprite, "sprite-v1" (the same files as images/field/sprt/<file>/)
 *   <data>/partners/<name>/training/<t>/    the gym training scene: one PNG per sprite, training.json (animations)
 *   <data>/partners/manifest.json
 *   <data>/menus/gym/effects/<t>/           the effect set the twelve trainings share between the partners (not a partner's)
 *   <data>/menus/manifest.json              shared with the menu exporter (menu_export.h), which writes <data>/menus/<screen>/ beside gym/
 *
 * <name> is the slug of the partner's English name (DIGNAM[p + 1]), <t> the slug of the training's English name (ADR-016 section 2; the disc file is only in training.json). The first
 * form's model lives once under digimon/<form>/ (partner.json names it); the importer (partner_bundle.h, A4a) reads this layout; the follower
 * sprites are no longer written under images/field/sprt/.
 *
 * The first half (sheet frames, portrait cells, strips, training sets and their composites) is pure stdlib and is covered by pc/partnerexptest.c;
 * the second reads the disc through the same file list and sector reader as raw_export.h and writes through exp_core.h.
 * Bump PARTNER_EXPORT_VERSION when the bytes or the layout change.
 */

#include <stddef.h>
#include <stdint.h>

#include "image_data.h"
#include "sprite_data.h"

#define PARTNER_EXPORT_VERSION 2
#define PE_PARTNERS 8
#define PE_SETS 9       /* image sets of a training file: 0..7 the partners, 8 the shared effect */
#define PE_TRAININGS 12 /* training files */
#define PE_CELLS_MAX 4

/* ---- portrait sheets ---- */

/* The first part of a frame of a sprite sheet (docs/formats.md 4.12; gfx.h SpritePart): u/v/w/h in texels of the screen's pages (page = u / 256),
   the palette at halfword column clutX, row clutY + row of page 0. `parts` is how many parts the frame has. */
typedef struct PeFrame {
    int id, parts;
    int u, v, w, h;
    int clutX, clutY, row, mode;
    int x, y; /* where the first part sits in the frame */
} PeFrame;

/* Frame `id` of the sheet that starts at sheet[0] (three offsets: parts, frame ids, first frame; then the other frames). 1 when it exists. */
int peSheetFrame(const uint8_t *sheet, size_t len, int id, PeFrame *out);

/* The 4-bit pixels of a frame cut out of the raw 16-bit pages of a screen (pages[k] = image k of its TM file, NULL when absent) and its 16-colour
   palette, read from page 0 at the frame's CLUT position (the drawer's CLUT base is page 0's; the palette is not in the page of the pixels, 4.12).
   `px` holds w*h indices. 1 on success. */
int peArtCell(const ImgTim *const *pages, int nPages, const PeFrame *f, uint8_t *px, uint16_t pal[16]);

/* n cells of cw x ch indices side by side as one indexed image (cw*n wide, ch high) with the 16 halfwords of `pal` as real colours.
   ImgPic.px is malloc'ed. 0 on success. */
int peStrip(const uint8_t *const *cells, int n, int cw, int ch, const uint16_t pal[16], ImgPic *out);

/* ---- gym training sets ---- */

/* One image set of a TRAINING/TRANIN<nn>.BIN file (src/menus/gym/files.c STGTRAIN_readSet): a sprite bank, 6 animations and 2 images (4-bit TIMs
   with their own CLUT, RLEN packed). */
typedef struct PeTrainSet {
    int present, nImages, sprites;
    uint8_t *buf[4];
    ImgTim tim[4];
    uint32_t *spr;     /* offset of each sprite's part count */
    uint32_t anim[6];  /* offset of each animation, 0 = none */
} PeTrainSet;

typedef struct PeTrain {
    const uint8_t *file; /* the caller's bytes: they must outlive the PeTrain */
    size_t len;
    PeTrainSet set[PE_SETS];
} PeTrain;

/* Reads the set table of a training file and unpacks every image. Returns how many sets are present (0: not a training file). */
int peTrainOpen(PeTrain *t, const uint8_t *file, size_t len);
void peTrainClose(PeTrain *t);

/* One sprite of a set drawn into one indexed image. The parts of sets 0..7 may take their pixels from the effect set's pages (columns 2 and 3 of
   the sheet; see peTrainFrame in the .c). pic.px is NULL for a sprite with no visible part. */
typedef struct PeTrainFrame {
    ImgPic pic;
    int ox, oy;      /* the image's top-left relative to the sprite's origin */
    int parts;       /* parts of the sprite */
    int shared;      /* of them, drawn from the effect set's pages */
    int semi;        /* a part is semi-transparent */
    int missing;     /* parts that could not be drawn (no such page, palette or pixels) */
    int nSlots;      /* palettes of the image (pic.px != NULL): palette k holds the indices base + 16 k .. base + 16 k + 15 */
    int base;        /* 1 when the first palette's entry 0 is visible, so every palette sits one index up (index 0 is the transparent background) */
    int slotBlend[16]; /* palette k: 0 opaque, else 1 + the PS1 blend mode of its parts (0 average, 1 add, 2 subtract, 3 a quarter added) */
} PeTrainFrame;
/* 1 when the sprite exists (pic may be empty), 0 when the index is out of the bank */
int peTrainFrame(const PeTrain *t, int set, int sprite, PeTrainFrame *out);
void peTrainFrameFree(PeTrainFrame *f);

typedef struct PeTrainStats {
    int frames, empty, failed, missing;
} PeTrainStats;

/* Emits <NNN>.png for every sprite of `set` and training.json (format `format`, name `name`, partner identity `discId` or NULL, the disc file) through
   emit (names relative to the set's folder). 0 when the set was written, -1 when it is absent or a write failed. */
int peTrainEmit(const PeTrain *t, int set, const char *format, const char *name, const char *discId, const char *discFile, SprEmitFn emit, void *ctx,
                PeTrainStats *st);

/* ---- names and tables ---- */

/* Which of the seven screens that draw the portraits has the page file `discPath` ("AAA/DAT/SCREEN/NAMEDTTM.BIN"): 0..6, -1 for any other file. *entry is the
   entry of the screen's sprite sheet file (its CS file) that holds the portraits' sheet. */
int peScreenOfPages(const char *discPath, int *entry);

/* "Agumon" -> "agumon": lower case, every run of other characters one '-', none at the ends, at most 48 characters (ADR-016 section 2). 0 when nothing is left. */
int peSlug(const char *text, char *out, size_t cap);

typedef struct PeLayout {
    int portraitFirst, portraitCells; /* the partner's frames in the sheets: first id and how many (consecutive) */
    int iconFrame;                    /* frame of the memory card sheet */
    const char *fieldFile;            /* "P006AGUM": FIELD/SPRT/<fieldFile>.BIN */
} PeLayout;
const PeLayout *peLayout(int partner);
/* the entry of the gym's text file (ESTRAINI) that names training file i, -1 past the last (see the table in the .c) */
int peTrainNameEntry(int i);
/* The slug of entry entry[i] of a text file for each of n, "" when unreadable; the eight partner folders (entry p + 1 of ESDIGNAM) and the twelve training folders
   (slug of the training's English name, "training-<row>" when unreadable, "-2".. for a repeat). The bytes are the file's, as read from the disc. */
void peSlugsFromText(const uint8_t *bytes, size_t len, const int *entry, int n, char (*out)[64]);
void peNamesFromText(const uint8_t *bytes, size_t len, char key[PE_PARTNERS][64]);
void peTrainFolders(const uint8_t *bytes, size_t len, char names[PE_TRAININGS][64]);
/* the training files in row order (rows 1..12 of STGTRAIN_files; rows 13..24 repeat them): "TRANIN00" ... */
const char *peTrainFile(int i);

/* ---- the disc side ---- */

typedef struct PartnerExportIo {
    /* the i-th file of the disc ("AAA/DAT/..." as on the disc, first sector, exact size); 1 when there is one, 0 past the last */
    int (*list)(void *ud, int i, char *path, size_t cap, long *lba, long *size);
    /* one raw 2352-byte frame of the disc image; 1 when read */
    int (*read)(void *ud, long lba, unsigned char *frame);
    void *ud;
} PartnerExportIo;

typedef struct PartnerExportStats {
    int partners;   /* partners built */
    int skipped;    /* 1 when everything was up to date and the disc was not read */
    int written, kept, newFiles, failed;
    long bytes;
    int mismatches; /* portrait cells or palettes of a screen that differ from the first screen's */
} PartnerExportStats;

void partnerExportSetup(const char *dataDir, const PartnerExportIo *io);
/* "all", or partner names (kotemon, agumon...) separated by commas or spaces. Synchronous. The number of partners built, 0 when up to date, or -1
   when there is nothing to do (no data folder, no reader, nothing valid, the disc is not the EU one). */
int partnerExportRun(const char *spec);
void partnerExportTotals(PartnerExportStats *out);

#endif
