#ifndef PC_IMAGE_DATA_H
#define PC_IMAGE_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "mods.h"
#include "text_data.h"

/*
 * Menu art as PNG (feature menu-art-png, T2). See docs/formats.md section 4.8 for the format evidence.
 *
 * On startup, when the disc or this converter changed (or a file is missing), the screen/menu TIMs of the
 * user's disc that menus/<screen>/ pieces do not cover (imgIsMenuScreen: those are exported by menu_export.h; A5.5) are
 * written as indexed PNGs:
 *
 *     <data>/images/<DIR>/<NAME>/<NN>[_<MM>...].png     e.g. images/SCREEN/STSHOPTM/00_00.png
 *     <data>/images/manifest.json                      the manifest: header + content hash of every generated file
 *
 * <DIR>/<NAME> is the disc path under AAA/DAT without ".BIN"; NN_MM is the chain of archive entry
 * indices (two digits each) that leads to the TIM, so the name is stable and says where the image is.
 * A file the user edited is never overwritten: the new version goes beside it as <name>.png.new.
 *
 * The first half (TIM decoding, archive walk, naming, extraction) is pure stdlib and is covered by
 * pc/imagetest.c. imgStart does the disc work.
 */

#define IMG_CODEC_VERSION 13 /* bump when the PNGs, piece maps (T5) or sprite maps (sprite_data.h) this unit writes change */
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

/* A parsed TIM (PS1 texture): the blocks stay where they are in the caller's bytes. CLUT and image sizes are in halfwords. */
typedef struct ImgTim {
    int mode;                    /* 0 4-bit, 1 8-bit, 2 16-bit */
    const uint8_t *clut;         /* CLUT pixels (cw x ch halfwords) or NULL */
    int cw, ch;
    const uint8_t *img;          /* image pixels */
    int iw, ih;                  /* in halfwords */
} ImgTim;

/* 0 when `t` is a TIM (the same validation the archive walk uses), else -1 */
int imgTimParse(const uint8_t *t, size_t len, ImgTim *o);
/* "RLEN", the unpacked size, then runs: the unpacked bytes (malloc'ed) and their count, or NULL when the packet is bad */
uint8_t *imgRlenUnpack(const uint8_t *s, size_t len, size_t *outLen);

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
/* The other way, for one palette entry: a PNG colour as a PS1 colour. Alpha 0 is 0x0000; an opaque colour that would also come out as 0x0000 is 0x8000
   (black with the STP bit, so it stays drawn); any other colour keeps the STP bit of `orig`, the disc's entry. */
uint16_t imgColor16(const uint8_t *rgb, int alpha, uint16_t orig);

/* Walk an archive (table of offsets, depth up to IMG_MAX_DEPTH) and call `cb` for each TIM, unpacking
   RLEN ones. chain[0..depth) are the entry indices. cb returns 0 to go on. Returns the TIM count. */
typedef int (*ImgVisit)(void *ctx, const int *chain, int depth, const uint8_t *tim, size_t len);
int imgWalk(const uint8_t *file, size_t len, ImgVisit cb, void *ctx);

/* A sprite sheet (SpritePart list, gfx.h): the texture mode most parts use (0 4-bit, 1 8-bit) and the
   palette position (clutX = halfword column, clutY = row inside the page) most parts of `page` use.
   0 on success. */
int imgSheetMode(const uint8_t *sheetFile, size_t len, int *mode);
int imgSheetClut(const uint8_t *sheetFile, size_t len, int page, int mode, int *clutX, int *clutY);

/* One part of a sheet (SpritePart, gfx.h): a rectangle of the texture and the CLUT position it names. */
typedef struct ImgPart {
    int u, v, w, h;
    int clutX, clutY;
    int mode;            /* 0: 4-bit texture (a 16-colour palette), 1: 8-bit (256 colours) */
} ImgPart;
int imgSheetPartCount(const uint8_t *sheetFile, size_t len);                    /* 0 when it is not a sheet */
int imgSheetPart(const uint8_t *sheetFile, size_t len, int index, ImgPart *out); /* 0 on success */

/* The pages of a screen's TM file (its 16-bit images, in file order). The game loads them side by side into VRAM, 64 halfwords per
   page, and a part names its CLUT as a position in that space: column clutX falls on page clutX / 64, halfword clutX % 64, and the
   row is clutY (plus the CLUT row a frame adds). So a palette is read where the position falls, which is not always the page of the
   pixels (stmcrdtm keeps its palettes on page 0 for pixels on page 1, sttrngtm on page 0 for pixels on pages 0 to 2). */
typedef struct ImgPages ImgPages;
ImgPages *imgPagesOpen(const uint8_t *tmFile, size_t len); /* NULL when it has no page; copies what it needs */
/* The same for a stage's MASK file (FIELD/STAGE/S<nnn>MASK.BIN, loaded by file_loader.c): every TIM's image is a page (an 8-bit TIM's too: the
   halfwords are the same) and the TIMs' CLUTs fill the alternate CLUT area, where an 8-bit part (mode 1) finds its palette. */
ImgPages *imgStagePagesOpen(const uint8_t *maskFile, size_t len);
void imgPagesFree(ImgPages *pg);

/* Where a palette was found: the page, and the page-local halfword column and row of its first colour */
typedef struct ImgPalAt {
    int page, x, y;
} ImgPalAt;

/* The palette of `part` seen from a frame that adds `row` to its CLUT row: 16 (mode 0) or 256 (mode 1) halfwords into `pal`
   (room for 256). 0 on success, -1 when the position is outside the pages (a stage's 8-bit part: not in the CLUTs the archive loads).
   `at` may be NULL; a palette that is not in a page (a stage's 8-bit one) has at->page -1. */
int imgPartPalette(const ImgPages *pg, const ImgPart *part, int row, uint16_t *pal, ImgPalAt *at);

/* How many pages the space has (the highest page index + 1; a page in between may be absent) and the size in halfwords of page k (0 on success, -1 when absent) */
int imgPagesCount(const ImgPages *pg);
int imgPageSize(const ImgPages *pg, int k, int *w, int *h);
/* A stage's 8-bit colours sit in the CLUT of the archive's TIMs: the loader sends each to the same place (the alternate CLUT area), so where two overlap the
   later TIM wins. Row r of that area is the "row" IMG_ALT_CLUT_ROW + r of a part's palette position. */
#define IMG_ALT_CLUT_ROW 256
/* Which of the TIMs with a CLUT (4 or 8 bit, counted in walk order from 0) the game keeps the colour at (row r, column c) of that area from; -1 when none
   has it. Only for pages opened with imgStagePagesOpen. */
int imgPagesAltOwner(const ImgPages *pg, int r, int c);

/* The indices of the rectangle of `part` (w x h, row by row), read from the pages as the game's VRAM holds them: texel column u of a 4-bit part is nibble
   u % 4 of halfword u / 4 of the screen's space (u / 2 and a byte for an 8-bit part), so a rectangle may cross from one page into the next. `px` has room for
   w * h. A texel outside the pages is index 0; *outside is how many are (may be NULL). 0 on success, -1 for an empty rectangle. */
int imgPartPixels(const ImgPages *pg, const ImgPart *part, uint8_t *px, int *outside);

/* The frames of a sheet (gfx.h: a frame is the part count, the CLUT row it adds and the semi-transparency rate, then a part index, x and y per part;
   the parts are drawn from the last to the first). `id` is what the game asks for (the sheet's id list), `rate` is -1 for opaque, else the PS1 blend mode
   (0 average, 1 add, 2 subtract, 3 a quarter added). */
typedef struct ImgFrame {
    int id, parts, row, rate;
} ImgFrame;
int imgSheetFrameCount(const uint8_t *sheetFile, size_t len);                    /* 0 when it is not a sheet */
int imgSheetFrame(const uint8_t *sheetFile, size_t len, int index, ImgFrame *out); /* 0 on success */
/* Part `k` of frame `index`: the index of the sheet part it draws and where (x, y relative to the sprite's origin). 0 on success, -1 when the frame or the
   slot is out of range or the part index does not name a part of the sheet. */
int imgSheetFramePart(const uint8_t *sheetFile, size_t len, int index, int k, int *part, int *x, int *y);

/* Which disc files are screen/menu art: AAA/DAT/{SCREEN,COUNTRY,NAMEENT,MCARD,DGLB,TRAINING}/ files
   named *TM.BIN, TLOGO*, TTCOMMPK, END__ALL or CNTSELWN. */
int imgIsScreenFile(const char *discPath);
/* The screens edited by pieces (menus/<screen>/, menu_pack.h) and not by images/: the *TM.BIN files of imgIsScreenFile except STWMP0TM and STWMP1TM. Their
   pieces are 8-bit raw pages with no PNG (file null in menu.json), so they stay on images/ as a documented exception. The loose pictures (TLOGO*, TTCOMMPK,
   END__ALL, CNTSELWN) have no sheet of pieces and stay on images/ too. */
int imgIsMenuScreen(const char *discPath);
/* The card pictures (content-export E4): AAA/DAT/CARD/CARDPAK0-4.BIN, 64 cards each (12-byte header, 32x32 8-bit TIM with its
   own CLUT). Their faces are not exported to images/ (A7.4): they are cards/c<nnn>/face.png (card_export.h) and a card edited there replaces the image through the
   same override table. CARD/CARDDATA.BIN (battle sprites, with piece maps) is a screen-style archive and stays on images/. */
int imgIsCardFile(const char *discPath);
/* The game's number of image `index` of a CARDPAK file (A * 64 + index + 1, as cardDrawerSetCard reads it); 0 when the path is no card file or the index is past it. */
int imgCardId(const char *discPath, int index);
/* "c012" for card 12 (-1 when the id is not 1..999 or `cap` is short), and the number of such a folder name (0 when it is not "c" and three digits). */
int imgCardFolder(int id, char *out, size_t cap);
int imgCardIdOf(const char *folder);
/* Every card face of a CARDPAK file as a PNG (the bytes the images exporter wrote before A7.4, so an edited face substitutes back the same way): cb(ud, id, png, len)
   for each image that parses, in file order; a non-zero return of cb counts as a failure. The number of faces, or -1 when the path is no card file or one failed. */
typedef int (*ImgCardFn)(void *ud, int id, const uint8_t *png, size_t len);
int imgCardFaces(const char *discPath, const uint8_t *file, size_t len, ImgCardFn cb, void *ud);
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
    char rel[128];   /* "screen/tlogousa/00_00.png"; empty for a page of a screen edited by pieces (imgIsMenuScreen) */
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
   user-edited data/images (assetDataDir) file (hash differs from the manifest). Logs `images: override <rel> from
   <source>` and `images: <path> rejected: <reason>`. Returns how many images were substituted. */
int imgOverridesBuild(ImgOverrides *o, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet,
                      size_t sheetLen);
/* Partner bundles (readable-assets A4a, partner_bundle.h): what the image layer asks of the module that owns the partners/<name>/ folders. The module sets
   them once at startup (imgSetPartnerHooks); without them nothing changes.
   - begin/patch: while the overrides of a screen file are built, every page image (the unpacked TIM, a writable copy: `tim`, `len`) is offered to patch with
     the file's sprite sheet (`sheet`, may be NULL); it rewrites the pixels and palette rows of the original partners' edited portraits and icons in place and
     returns 1 when it changed something. begin(discPath) comes first for each file and says (1) whether its pages are to be offered at all.
   - fieldFile: the follower sprite of the eight original partners lives in partners/<name>/field/, not in sprites/: for partner p and the frame or
     file name `tail` ("004.png", "sprite.json") it says whether `layer` holds one that counts (any file of a mod; in the data folder only one the user edited,
     judged against partners/manifest.json) and where. Without it those sprites are never replaced. */
typedef struct ImgPartnerHooks {
    int (*begin)(const char *discPath);
    int (*patch)(const char *discPath, const int *chain, int depth, uint8_t *tim, size_t len, const uint8_t *sheet, size_t sheetLen);
    int (*fieldFile)(int partner, const AssetLayer *layer, const char *tail, char *path, size_t cap);
} ImgPartnerHooks;
void imgSetPartnerHooks(const ImgPartnerHooks *hooks);
const ImgPartnerHooks *imgGetPartnerHooks(void); /* what is set (all NULL members when nothing is): the tests call the hooks directly */

/* The menus by piece (readable-assets A5.3, menu_pack.h): what the image layer asks of the module that owns the menus/<screen>/ folders. The module sets them
   once at startup (imgSetMenuHooks); without them nothing changes. For a screen's page file (`file`, `len`, with its sprite sheet file `sheet`, may be NULL):
   - begin(discPath, ...) comes first and says (1) whether the pages are to be offered at all (some piece differs from the disc);
   - patch is then called for every page image (the unpacked TIM, a writable copy: `tim`, `len`; `chain` as in imgWalk) and rewrites the texels and the palette
     halfwords the pieces name; it returns 1 when a byte changed. It runs after the partner hook, so the pieces go over a partner's art and over images/;
   - end(discPath) comes last, once the walk is done. */
typedef struct ImgMenuHooks {
    int (*begin)(const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet, size_t sheetLen);
    int (*patch)(const char *discPath, const int *chain, int depth, uint8_t *tim, size_t len);
    void (*end)(const char *discPath);
} ImgMenuHooks;
void imgSetMenuHooks(const ImgMenuHooks *hooks);
const ImgMenuHooks *imgGetMenuHooks(void); /* what is set (all NULL members when nothing is) */
/* The same hooks for the stage objects by piece (readable-assets A6.3, stage_obj_pack.h): asked only for a stage's MASK file (imgIsStageMask), whose `sheet` is
   the stage's TMPK file, and run after the menu hooks. begin says whether the pieces differ from the disc; a file it says no to costs nothing more. */
void imgSetStageHooks(const ImgMenuHooks *hooks);
const ImgMenuHooks *imgGetStageHooks(void);

/* Forgets the index of which PNGs each source holds (read once, on first use or by imgStart): the next question reads the folders again.
   Only the opt-in hot reload (mods.h modsHotReload) and the tests need it. */
void imgOverridesRescan(void);
/* A number that changes when any source file (mtime, size) of those images changes. Reads the files' stats: not for the frame loop */
long imgOverridesMix(const ImgOverrides *o);
const ImgOverride *imgOverrideAt(const ImgOverrides *o, size_t off);

/* Field sprites (feature field-sprites-png T3): the same rules for FIELD/SPRT/<NAME>.BIN, whose entries are frames (one
   4-bit TIM each; the image is sprites/<name>/<EEE>.png, EEE = the entry index; readable-assets A7.5). The build only looks WHICH frames have a source
   file (mods' sprites/, then an edited data/sprites/ file, judged against sprites/manifest.json); a PNG is read, checked and substituted the first time its frame is asked for
   (imgOverrideGet), then kept. Logs one line per mod and file ("images: override sprites/p001play/<EEE>.png (N) from
   mods/<name>"), rejections one by one when they are found. `file` must stay valid until imgOverridesFree. Returns the
   number of frames that have a source. */
int imgSpriteOverridesBuild(ImgOverrides *o, const char *discPath, const uint8_t *file, size_t len);
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
    int sameRun;           /* the manifest read has this run's header: same disc, same exporter version */
    TdManifest old, next;    /* the manifest read and the one being built */
    int written, kept, newFiles, failed, images, guessed;
} ImgRun;

/* Extract every image of one disc file. `sheet` (may be NULL) is the paired sprite sheet file, else
   entry 0 of the file itself is tried. Returns the number of images seen. */
int imgExtractFile(ImgRun *run, const char *discPath, const uint8_t *file, size_t len, const uint8_t *sheet,
                   size_t sheetLen);
/* Decide and write one generated file (rel = "SCREEN/STSHOPTM/00_00.png"): pristine, edited -> .new */
void imgEmit(ImgRun *run, const char *rel, const uint8_t *png, size_t len);

/* Open <dataDir>/images for a run on the disc `disc` (16 hex digits) with exporter version `version`: loads its manifest. 0 on success. */
int imgRunOpen(ImgRun *run, const char *dataDir, const char *disc, unsigned version);
/* 1 when the header matched and every file the manifest lists is still there: nothing to do */
int imgRunCurrent(const ImgRun *run);
/* Keep the entries this run did not regenerate, write the manifest, free. 0 on success. */
int imgRunClose(ImgRun *run);

/* Disc side: checks the manifest header and extracts when needed (log with timings on stderr). Returns the
   number of files written, 0 when nothing was needed, -1 when something failed or no disc/data
   folder. Only disc.readSector is used. */
int imgStart(const TdDisc *disc, const char *dataDir);

/* --validate-mod (U5): resolves the image overrides of every art file of the disc now, so each refusal of a mod's PNG is logged. Needs imgStart
   to have been given the disc. Returns how many art files were looked at, 0 when no mod has an images/ folder, -1 without a disc. */
int imgValidateMods(void);

#endif
