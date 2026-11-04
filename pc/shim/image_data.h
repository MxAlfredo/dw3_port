#ifndef PC_IMAGE_DATA_H
#define PC_IMAGE_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "text_data.h"

/*
 * Menu art as PNG (feature menu-art-png, T2). See docs/formats.md section 4.8 for the format evidence.
 *
 * On startup, when the disc or this converter changed (or a file is missing), every screen/menu
 * TIM of the user's disc is written as an indexed PNG:
 *
 *     <data>/images/<DIR>/<NAME>/<NN>[_<MM>...].png     e.g. images/SCREEN/STSHOPTM/00_00.png
 *     <data>/images/manifest.json                      stamp + content hash of every generated file
 *
 * <DIR>/<NAME> is the disc path under AAA/DAT without ".BIN"; NN_MM is the chain of archive entry
 * indices (two digits each) that leads to the TIM, so the name is stable and says where the image is.
 * A file the user edited is never overwritten: the new version goes beside it as <name>.png.new.
 *
 * The first half (TIM decoding, archive walk, naming, extraction) is pure stdlib and is covered by
 * pc/imagetest.c. imgStart does the disc work.
 */

#define IMG_CODEC_VERSION 6 /* bump when the PNGs, piece maps (T5) or sprite maps (sprite_data.h) this unit writes change */
#define IMG_MAX_DEPTH 3     /* archive nesting that is followed */

/* A decoded image: one byte per pixel (an index), the palette as RGB and the tRNS alpha */
typedef struct ImgPic {
    int w, h;
    int depth;       /* 4 or 8: the PNG bit depth (indices 0..15 or 0..255) */
    uint8_t *px;     /* w*h indices, malloc'ed */
    int nColors;
    uint8_t rgb[256 * 3];
    uint8_t alpha[256];
    int guessed;     /* 1: the palette is a grey ramp because none could be found */
} ImgPic;

void imgPicFree(ImgPic *p);

/* What a raw 16-bit page needs to be read as indices (the TIM does not say): */
typedef struct ImgHints {
    int pixelBits;          /* 4 or 8: how the 16-bit data is read as indices */
    const uint16_t *pal;    /* the palette (RGB555 + STP), or NULL for a grey ramp */
    int nPal;
    int palAt, palX, palY;  /* palAt: a 4-bit palette sits inside the page at halfword column palX, row palY */
} ImgHints;

/* Decode one TIM (already unpacked). 4 and 8-bit TIMs use their own CLUT (row 0); a 16-bit one is a
   raw page of indices and uses `hints` (NULL: 4-bit, grey ramp). 0 on success. */
int imgDecodeTim(const uint8_t *tim, size_t len, const ImgHints *hints, ImgPic *out);
int imgPicToPng(const ImgPic *p, uint8_t **png, size_t *len);

/* RGB555 to RGB888 and the transparency rule: colour 0x0000 is transparent (the PS1 draws it as
   nothing); every other colour is opaque (its STP bit only picks semi-transparency per primitive) */
void imgColor(uint16_t c, uint8_t rgb[3], uint8_t *alpha);

/* Walk an archive (table of offsets, depth up to IMG_MAX_DEPTH) and call `cb` for each TIM, unpacking
   RLEN ones. chain[0..depth) are the entry indices. cb returns 0 to go on. Returns the TIM count. */
typedef int (*ImgVisit)(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len);
int imgWalk(const uint8_t *file, size_t len, ImgVisit cb, void *ctx);

/* A sprite sheet (SpritePart list, gfx.h): the texture mode most parts use (0 4-bit, 1 8-bit) and the
   palette position (clutX = halfword column, clutY = row inside the page) most parts of `page` use.
   0 on success. */
int imgSheetMode(const uint8_t *sheetFile, size_t len, int *mode);
int imgSheetClut(const uint8_t *sheetFile, size_t len, int page, int mode, int *clutX, int *clutY);

/* Which disc files are screen/menu art: AAA/DAT/{SCREEN,COUNTRY,NAMEENT,MCARD,DGLB,TRAINING}/ files
   named *TM.BIN, TLOGO*, TTCOMMPK, END__ALL or CNTSELWN. */
int imgIsScreenFile(const char *discPath);
/* The card pictures (content-export E4): AAA/DAT/CARD/CARDPAK0-4.BIN, 64 cards each (12-byte header, 32x32 8-bit TIM with its
   own CLUT). The image of card i is "card/cardpak0/<ii>.png". CARD/CARDDATA.BIN (battle sprites) is a screen-style archive. */
int imgIsCardFile(const char *discPath);
/* The field stage art (content-export E6a): AAA/DAT/FIELD/STAGE/S<nnn>MASK.BIN (the stage's map-object pages, 8-bit TIMs with a CLUT or
   raw 16-bit pages holding 4-bit art; image "field/stage/s<nnn>mask/<ii>.png" and a piece map from the sheet in entry 0 of S<nnn>TMPK.BIN,
   page i = entry i) and S<nnn>BG<nn>.BIN (background images, no sheet, no piece map). */
int imgIsStageMask(const char *discPath);
int imgIsStageBg(const char *discPath);
/* "SCREEN/STSHOPTM/00_00.png" from the disc path and the chain */
int imgImageName(const char *discPath, const int *chain, int depth, char *out, size_t cap);
/* The sprite sheet of a TM file: "AAA/DAT/SCREEN/STSHOPTM.BIN" -> "AAA/DAT/SCREEN/STSHOPCS.BIN"
   (0 when it is not a TM file); other files carry their sheet as entry 0. A stage MASK file's sheet is its TMPK file
   (entry 0 of it; the whole file is passed, it has the same [offset] + sheet layout as a CS file) */
int imgSheetPathOf(const char *discPath, char *out, size_t cap);

/* Piece map (menu-art-png T5, format "atlas-v1"): the JSON written next to each PNG as <NN>_<MM>.json. It lists
   the pieces (rectangles of the PNG, in its pixels) that the paired sprite sheet references. `bits` is the PNG's
   depth, `page` the last index of the image's chain (the page: 256 texels wide for 4-bit, 128 for 8-bit); `sheet`
   NULL (or not a sheet) gives an empty piece list. Pieces whose rectangle is clipped by the image or lies off it
   carry "outside": true; identical rectangles are merged with their "frames" list. Returns malloc'ed text, or NULL. */
char *imgAtlasJson(const char *pngName, int w, int h, int bits, int page, const char *sheetName, const uint8_t *sheet, size_t sheetLen,
                   size_t *outLen);

/* ---- substitution (menu-art-png T3) ----
 *
 * Rules v1 for an image that replaces an extracted one (a mod's, or a user-edited data/images PNG):
 *   - same width and height as the original; indexed PNG (colour type 3, not interlaced), bit depth 1..8;
 *   - at most as many palette entries as the original has colours (16 for 4-bit pages, 256 for 8-bit)
 *     and every pixel index below that count; the PNG's indices replace the image's indices;
 *   - its PLTE replaces the palette the extractor exported, at the same place (the CLUT row 0 of a 4/8-bit
 *     TIM; the 16 halfwords a sprite sheet names inside a raw 4-bit page); entries past the PLTE and the
 *     other palettes of the page stay as on disc. A raw 8-bit page (STWMP*) borrows the CLUT of the 8-bit
 *     TIM of its subarchive, and a grey-ramp (guessed) palette has no place: their PLTE is ignored, edit
 *     the 8-bit TIM's own PNG to recolour them;
 *   - colours: tRNS alpha 0 becomes 0x0000 (transparent); any other becomes RGB888 >> 3 per channel with the
 *     STP bit of the disc entry at that index kept, and an opaque black (which would read as 0x0000) is
 *     written 0x8000.
 * Anything else is refused with a reason and the next source is tried.
 */

/* Build the TIM that results from putting `png` in place of the image `tim` (hints as at extraction).
   0 on success: *out is a malloc'ed copy of len bytes. -1: refused, why says why. */
int imgSubstitute(const uint8_t *tim, size_t len, const ImgHints *hints, const uint8_t *png, size_t pngLen, uint8_t **out,
                  char *why, size_t whySize);

typedef struct ImgOverride {
    int chain[IMG_MAX_DEPTH];
    int depth;
    size_t off;      /* where the TIM (or its RLEN packet) starts inside the disc file */
    char rel[128];   /* "SCREEN/STSHOPTM/00_00.png" */
    uint8_t *tim;    /* the substituted TIM, or NULL: this image stays as on disc */
    size_t len;
    int resolved;    /* 0: a source file exists but has not been read yet (imgSpriteOverridesBuild decodes on first use) */
    size_t origLen;  /* the original image's size, for the lazy substitution */
} ImgOverride;

struct ImgLazy;
typedef struct ImgOverrides {
    ImgOverride *v;  /* one per image of the file */
    int n, applied;
    struct ImgLazy *lazy; /* what the lazy entries need (sprite files); owned */
    void *idx;            /* the entries sorted by offset, for imgOverrideGet; owned */
} ImgOverrides;

/* Every image of one disc file with its override from the active mods (last wins, mods.h) and then a
   user-edited <dataDir>/images file (hash differs from the manifest). Logs `images: override <rel> from
   <source>` and `images: <path> rejected: <reason>`. Returns how many images were substituted. */
int imgOverridesBuild(ImgOverrides *o, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet,
                      size_t sheetLen, const char *dataDir);
/* Forgets the index of which PNGs each source holds (read once, on first use or by imgStart): the next question reads the folders again.
   Only the opt-in hot reload (mods.h modsHotReload) and the tests need it. */
void imgOverridesRescan(void);
/* A number that changes when any source file (mtime, size) of those images changes. Reads the files' stats: not for the frame loop */
long imgOverridesMix(const ImgOverrides *o, const char *dataDir);
const ImgOverride *imgOverrideAt(const ImgOverrides *o, size_t off);

/* Field sprites (feature field-sprites-png T3): the same rules for FIELD/SPRT/<NAME>.BIN, whose entries are frames (one
   4-bit TIM each; the image is <NAME>/<EEE>.png, EEE = the entry index). The build only looks WHICH frames have a source
   file (mods, then data/images); a PNG is read, checked and substituted the first time its frame is asked for
   (imgOverrideGet), then kept. Logs one line per mod and file ("images: override FIELD/SPRT/P001PLAY/<EEE>.png (N) from
   mods/<name>"), rejections one by one when they are found. `file` must stay valid until imgOverridesFree. Returns the
   number of frames that have a source. */
int imgSpriteOverridesBuild(ImgOverrides *o, const char *discPath, const uint8_t *file, size_t len, const char *dataDir);
/* The override of the image at `off` (binary search), resolving it on first use; NULL when the file has no image there.
   The result's tim is NULL when the image stays as on disc. */
const ImgOverride *imgOverrideGet(ImgOverrides *o, size_t off);
void imgOverridesFree(ImgOverrides *o);

/* Runtime (game side): the substituted TIM of the image that starts at byte `off` of the screen file at
   disc sector `lba`, or NULL when it stays as on disc. Reads the file from the disc imgStart was given,
   once; rechecks the sources at most once a second. */
const uint8_t *imgOverrideTim(long lba, size_t off, size_t *len);
/* Does the layer substitute at least one image inside the disc file that starts at sector `lba`? The common file
   layer (port_fs.h) then leaves that file to this layer and ignores a raw override of it. */
int imgFileOverridden(long lba);

/* ---- extraction ---- */

typedef struct ImgRun {
    char dir[1100];          /* <data>/images */
    int stampMatches;
    TdManifest old, next;    /* the manifest read and the one being built */
    int written, kept, newFiles, failed, images, guessed;
} ImgRun;

/* Extract every image of one disc file. `sheet` (may be NULL) is the paired sprite sheet file, else
   entry 0 of the file itself is tried. Returns the number of images seen. */
int imgExtractFile(ImgRun *run, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet,
                   size_t sheetLen);
/* Decide and write one generated file (rel = "SCREEN/STSHOPTM/00_00.png"): pristine, edited -> .new */
void imgEmit(ImgRun *run, const char *rel, const uint8_t *png, size_t len);

/* Open <dataDir>/images with the stamp this run would write: loads its manifest. 0 on success. */
int imgRunOpen(ImgRun *run, const char *dataDir, const char *stamp);
/* 1 when the stamp matched and every file the manifest lists is still there: nothing to do */
int imgRunCurrent(const ImgRun *run);
/* Keep the entries this run did not regenerate, write the manifest, free. 0 on success. */
int imgRunClose(ImgRun *run);

/* Disc side: checks the stamp and extracts when needed (log with timings on stderr). Returns the
   number of files written, 0 when nothing was needed, -1 when something failed or no disc/data
   folder. Only disc.readSector is used. */
int imgStart(const TdDisc *disc, const char *dataDir);

/* --validate-mod (U5): resolves the image overrides of every art file of the disc now, so each refusal of a mod's PNG is logged. Needs imgStart
   to have been given the disc. Returns how many art files were looked at, 0 when no mod has an images/ folder, -1 without a disc. */
int imgValidateMods(void);

#endif
