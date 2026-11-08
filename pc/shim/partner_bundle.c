/* Importing the partner bundle (readable-assets A4a): see partner_bundle.h. */
#include "partner_bundle.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "exp_core.h"
#include "image_data.h"
#include "mods.h"
#include "partner_export.h"
#include "port_fs.h"
#include "port_log.h"
#include "sprite_data.h"
#include "text_data.h"

#define TAG "partners"
#define PB_PARTNERS 8

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- the repack ---- */

static unsigned cellPixel(const PaArt *a, int cell, int x, int y) {
    int xx = cell * a->w + x;

    return (unsigned)(a->img[(size_t)y * (size_t)a->rowHw + (size_t)(xx / 4)] >> ((xx & 3) * 4)) & 15u;
}

/* one frame of the sheet: its 4-bit pixels (when they live in `page`) and the 16 palette halfwords (when `page` is 0). 1 pixel block, 2 palette written, 0 none */
static int writeFrame(uint8_t *img, int iw, int ih, int page, const PeFrame *f, const PaArt *a, int cell) {
    int done = 0, pg, lu, x, y, i;

    pg = f->u / 256;
    lu = f->u % 256;
    if (pg == page && f->u >= 0 && f->v >= 0 && lu + f->w <= iw * 4 && f->v + f->h <= ih) {
        for (y = 0; y < f->h; y++) {
            for (x = 0; x < f->w; x++) {
                uint8_t *hw = img + 2 * ((size_t)(f->v + y) * (size_t)iw + (size_t)((lu + x) / 4));
                unsigned v = (unsigned)hw[0] | (unsigned)hw[1] << 8, sh = (unsigned)(((lu + x) & 3) * 4);

                v = (v & ~(15u << sh)) | (cellPixel(a, cell, x, y) << sh);
                hw[0] = (uint8_t)v;
                hw[1] = (uint8_t)(v >> 8);
            }
        }
        done |= 1;
    }
    if (page == 0 && f->clutX >= 0 && f->clutX + 16 <= iw && f->clutY + f->row >= 0 && f->clutY + f->row < ih) {
        uint8_t *row = img + 2 * ((size_t)(f->clutY + f->row) * (size_t)iw + (size_t)f->clutX);

        for (i = 0; i < 16; i++) {
            row[2 * i] = (uint8_t)a->pal[i];
            row[2 * i + 1] = (uint8_t)(a->pal[i] >> 8);
        }
        done |= 2;
    }
    return done;
}

int pbRepackPage(uint8_t *tim, size_t len, int page, const uint8_t *sheet, size_t sheetLen, const PbArt art[8]) {
    ImgTim t;
    uint8_t *img;
    int p, c, wrote = 0;

    if (tim == NULL || sheet == NULL || imgTimParse(tim, len, &t) != 0 || t.mode != 2) return 0;
    img = tim + (t.img - tim);
    for (p = 0; p < PB_PARTNERS; p++) {
        const PeLayout *lay = peLayout(p);
        PeFrame f;

        if (art[p].portrait != NULL && art[p].portrait->cells == lay->portraitCells && art[p].portrait->w == PA_CELL) {
            for (c = 0; c < lay->portraitCells; c++) {
                int r;

                if (!peSheetFrame(sheet, sheetLen, lay->portraitFirst + c, &f) || f.parts != 1 || f.mode != 0 || f.w != PA_CELL || f.h != PA_CELL) continue;
                r = writeFrame(img, t.iw, t.ih, page, &f, art[p].portrait, c);
                wrote += (r & 1) + ((r >> 1) & 1);
            }
        }
        if (art[p].icon != NULL && art[p].icon->w == PA_ICON_W && peSheetFrame(sheet, sheetLen, lay->iconFrame, &f) && f.parts == 1 && f.mode == 0 &&
            f.w == PA_ICON_W && f.h == PA_ICON_H) {
            int r = writeFrame(img, t.iw, t.ih, page, &f, art[p].icon, 0);

            wrote += (r & 1) + ((r >> 1) & 1);
        }
    }
    return wrote;
}

/* ---- names ---- */

static char sKey[PB_PARTNERS][64];
static int sHaveKeys;

void pbSetKeys(char key[8][64]) {
    int p;

    sHaveKeys = 0;
    for (p = 0; key != NULL && p < PB_PARTNERS; p++) {
        snprintf(sKey[p], sizeof sKey[p], "%s", key[p]);
        if (key[p][0] != '\0') sHaveKeys = 1;
    }
    if (key == NULL) memset(sKey, 0, sizeof sKey);
}

const char *pbOriginalKey(int partner) {
    return sHaveKeys && partner >= 0 && partner < PB_PARTNERS && sKey[partner][0] != '\0' ? sKey[partner] : NULL;
}

/* the English names from the user's own disc, as the exporter reads them */
static void keysFromDisc(void) {
    const PfsFile *f = pfsDiscIsOpen() ? pfsFindPath("AAA/DAT/COUNTRY/ENG/ESDIGNAM.BIN") : NULL;
    uint8_t *bytes;
    char key[PB_PARTNERS][64];

    if (f == NULL || (bytes = pfsDiscRead(f->lba, (size_t)f->size)) == NULL) return;
    peNamesFromText(bytes, (size_t)f->size, key);
    free(bytes);
    pbSetKeys(key);
}

/* ---- which files count ---- */

static TdManifest sMan;
static int sManLoaded;

/* the data folder's own file counts only when the user changed it: its hash is not the one partners/manifest.json recorded (a file the manifest does not
   know is the user's too) */
static int dataFileEdited(const char *rel, const uint8_t *bytes, size_t len) {
    char hex[17], path[1300];

    if (!sManLoaded) {
        snprintf(path, sizeof path, "%s/partners/manifest.json", assetDataDir() != NULL ? assetDataDir() : "");
        memset(&sMan, 0, sizeof sMan);
        if (assetDataDir() != NULL) expManRead(TAG, path, &sMan);
        sManLoaded = 1;
    }
    tdHashHex(tdHash(bytes, len), hex);
    return tdFileState(1, tdManifestFind(&sMan, rel), hex) == TD_FILE_EDITED;
}

int pbDataFileEdited(const char *rel, const uint8_t *bytes, size_t len) {
    return dataFileEdited(rel, bytes, len);
}

static int fieldFileHook(int partner, const AssetLayer *layer, const char *tail, char *path, size_t cap) {
    const char *key = pbOriginalKey(partner);
    char rel[160];
    uint8_t *bytes;
    size_t len;
    int edited;

    if (key == NULL || snprintf(rel, sizeof rel, "%s/field/%s", key, tail) >= (int)sizeof rel) return 0;
    if (!assetLayerFile(layer, "partners", rel, path, cap)) return 0;
    if (layer->mod >= 0) return 1;
    if (tdReadFile(path, &bytes, &len) != 0) return 0;
    edited = dataFileEdited(rel, bytes, len);
    free(bytes);
    return edited;
}

/* any file of data/partners/<rel> below `dir` that the user changed (the exporter's partner.json of the folder is information only and never read) */
static int folderEdited(const char *dir, const char *rel, int depth) {
    DIR *d = opendir(dir);
    struct dirent *e;
    int edited = 0;

    if (d == NULL) return 0;
    while (!edited && (e = readdir(d)) != NULL) {
        char path[1500], sub[300];
        struct stat st;

        if (e->d_name[0] == '.' || snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path ||
            snprintf(sub, sizeof sub, "%s/%s", rel, e->d_name) >= (int)sizeof sub || stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            edited = depth < 6 && folderEdited(path, sub, depth + 1);
        } else if (S_ISREG(st.st_mode) && !(depth == 0 && strcasecmp(e->d_name, "partner.json") == 0)) {
            uint8_t *bytes;
            size_t len;

            if (tdReadFile(path, &bytes, &len) == 0) {
                edited = dataFileEdited(sub, bytes, len);
                free(bytes);
            }
        }
    }
    closedir(d);
    return edited;
}

int pbDataFolderEdited(const char *key) {
    char dir[1500];

    if (assetDataDir() == NULL || key == NULL || snprintf(dir, sizeof dir, "%s/partners/%s", assetDataDir(), key) >= (int)sizeof dir) return 0;
    return folderEdited(dir, key, 0);
}

/* ---- the edited originals ---- */

static PaArt sPortrait[PB_PARTNERS], sIcon[PB_PARTNERS];
static int sHasPortrait[PB_PARTNERS], sHasIcon[PB_PARTNERS], sAny;

/* partners/<key>/<file> from the first layer that has a file that counts and that converts; 1 when `out` was filled */
static int loadArt(const char *key, const char *file, int icon, int cells, PaArt *out) {
    int i;

    for (i = 0; i < assetLayerCount(); i++) {
        AssetLayer l;
        char rel[160], path[1500], why[200];
        uint8_t *bytes;
        size_t len;
        int reduced = 0, ok;

        if (!assetLayerAt(i, &l) || snprintf(rel, sizeof rel, "%s/%s", key, file) >= (int)sizeof rel) continue;
        if (!assetLayerFile(&l, "partners", rel, path, sizeof path) || tdReadFile(path, &bytes, &len) != 0) continue;
        if (l.mod < 0 && !dataFileEdited(rel, bytes, len)) {
            free(bytes);
            continue;
        }
        ok = paFromPngFit(bytes, len, icon, out, &reduced, why, sizeof why) == 0;
        free(bytes);
        if (ok && !icon && out->cells != cells) {
            snprintf(why, sizeof why, "an original partner's portrait has exactly %d cells of %dx%d, this one has %d", cells, PA_CELL, PA_CELL, out->cells);
            paFree(out);
            ok = 0;
        }
        if (!ok) {
            plogLine(TAG, "%s rejected: %s\n", path, why);
            continue;
        }
        if (reduced) plogLine(TAG, "%s: %d colours reduced to 16 (a 4-bit palette holds 16)\n", path, reduced);
        plogLine(TAG, "override %s from %s\n", rel, l.label);
        return 1;
    }
    return 0;
}

static void freeArt(void) {
    int p;

    for (p = 0; p < PB_PARTNERS; p++) {
        paFree(&sPortrait[p]);
        paFree(&sIcon[p]);
    }
    memset(sHasPortrait, 0, sizeof sHasPortrait);
    memset(sHasIcon, 0, sizeof sHasIcon);
    sAny = 0;
    if (sManLoaded) tdManifestFree(&sMan);
    sManLoaded = 0;
}

/* ---- the image layer's hooks ---- */

static int sScreenEntry; /* the sheet entry of the page file being built */

static int hookBegin(const char *discPath) {
    return sAny && peScreenOfPages(discPath, &sScreenEntry) >= 0;
}

static int hookPatch(const char *discPath, const int *chain, int depth, uint8_t *tim, size_t len, const uint8_t *sheet, size_t sheetLen) {
    PbArt art[PB_PARTNERS];
    uint32_t at;
    int p, page, n;

    if (depth != 2 || chain[0] != 0 || chain[1] < 0 || chain[1] >= 4 || sheet == NULL || sheetLen < 4 * (size_t)(sScreenEntry + 1)) return 0;
    page = chain[1];
    at = le32(sheet + 4 * (size_t)sScreenEntry);
    if (at >= sheetLen) return 0;
    for (p = 0; p < PB_PARTNERS; p++) {
        art[p].portrait = sHasPortrait[p] ? &sPortrait[p] : NULL;
        art[p].icon = sHasIcon[p] ? &sIcon[p] : NULL;
    }
    n = pbRepackPage(tim, len, page, sheet + at, sheetLen - at, art);
    if (n > 0) PLOG(TAG, PLOG_MODS, "repack %s page %d: %d cells and palettes rewritten\n", discPath, page, n);
    return n > 0;
}

void pbReset(void) {
    imgSetPartnerHooks(NULL);
    freeArt();
    pbSetKeys(NULL);
}

int pbStart(void) {
    ImgPartnerHooks hooks;
    int p, n = 0;

    freeArt();
    if (!sHaveKeys) keysFromDisc();
    memset(&hooks, 0, sizeof hooks);
    hooks.begin = hookBegin;
    hooks.patch = hookPatch;
    hooks.fieldFile = fieldFileHook;
    imgSetPartnerHooks(&hooks);
    for (p = 0; p < PB_PARTNERS; p++) {
        const char *key = pbOriginalKey(p);
        const PeLayout *lay = peLayout(p);

        if (key == NULL) continue;
        sHasPortrait[p] = loadArt(key, "portrait.png", 0, lay->portraitCells, &sPortrait[p]);
        sHasIcon[p] = loadArt(key, "icon.png", 1, 1, &sIcon[p]);
        if (sHasPortrait[p] || sHasIcon[p]) n++;
    }
    sAny = n > 0;
    return n;
}
