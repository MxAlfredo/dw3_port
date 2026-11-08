/*
 * The loader of the stages that mods add: see stage_new.h and docs/stages.md section 13.
 */
#include "stage_new.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "content.h"
#include "map_layers.h"
#include "map_pack.h"
#include "mod_stages.h"
#include "mods.h"
#include "png_read.h"
#include "port_log.h"
#include "stage_codes.h"
#include "text_data.h"

/* What a stage kept between the scan and the install: the two files the field reads, and what the checks of the sections need to know of the maps */
typedef struct Pending {
    char key[MS_KEY_MAX + 1];
    uint8_t *layers, *pack;
    size_t layersLen, packLen;
    int maxArea; /* the biggest value of the area layer: the battle area it names is value - 1 */
    int maxSlot; /* the biggest slot a nonzero cell of the trigger layer names (value & 0x1F), -1 when none */
    int tilesW, tilesH;
} Pending;

static Pending *sPend[MS_MODE_COUNT];
static int sPendCount;

#define MUSIC_VALUE(bank, track) (0x60000000 | ((bank) << 18) | (track)) /* include/engine/sound.h MUSIC(bank, n) */

/* ---- reading ---- */

static void say(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

static void say(char *err, size_t cap, const char *fmt, ...) {
    va_list ap;

    if (!err || !cap) return;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

/* a member of an object as a whole number in lo..hi; *has says whether it is there. 0 when it is there and wrong */
static int intOf(const ExtJson *o, const char *key, long lo, long hi, int *out, int *has, const char *where, char *err, size_t cap) {
    const ExtJson *j = extJsonGet(o, key);

    *has = j != NULL;
    if (!j) return 1;
    if (j->t != 0 || !extWhole(j->num, lo, hi)) {
        say(err, cap, "%s\"%s\" must be a whole number from %ld to %ld", where, key, lo, hi);
        return 0;
    }
    *out = (int)j->num;
    return 1;
}

static int onlyKeys(const ExtJson *o, const char *const *known, int n, const char *where, char *err, size_t cap) {
    int i, k;

    for (i = 0; i < o->n; i++) {
        int ok = 0;

        for (k = 0; k < n; k++) ok |= !strcmp(o->keys[i], known[k]);
        if (!ok) {
            say(err, cap, "%sunknown key \"%s\"", where, o->keys[i]);
            return 0;
        }
    }
    return 1;
}

typedef struct Header {
    int fallbackMode, fallbackX, fallbackY, fallbackDir;
    int textFile, soundBank, music;
    int startX, startY, startDir;
    int color[4];
    char bannerArea[MS_KEY_MAX + 1], bannerPlace[MS_KEY_MAX + 1]; /* the names of the two texts of the area name banner, "" for none */
} Header;

/* The members the sections do not own (those are the stage patch layer's, smLoad reads them): format, fallback, start, textFile, soundBank, music, spriteColor, banner. */
static int readHeader(const SmHost *host, const char *text, size_t len, Header *h, char *err, size_t cap) {
    ExtJson root;
    const ExtJson *j;
    int ok = 0, has, i;

    memset(h, 0, sizeof *h);
    if (extJsonParse((const uint8_t *)text, len, &root)) {
        say(err, cap, "not a JSON object");
        return 0;
    }
    j = extJsonGet(&root, "format");
    if (!j || j->t != 1 || !j->str || strcmp(j->str, SM_NEW_FORMAT)) {
        say(err, cap, "\"format\" must be \"%s\"", SM_NEW_FORMAT);
        goto done;
    }
    if ((j = extJsonGet(&root, "fallback")) != NULL) {
        static const char *const keys[] = {"stage", "x", "y", "dir"};
        int stage = 0, hs, hx, hy, hd;

        if (j->t != 2 || !onlyKeys(j, keys, 4, "\"fallback\": ", err, cap)) {
            if (j->t != 2) say(err, cap, "\"fallback\" must be an object {\"stage\", \"x\", \"y\", \"dir\"}");
            goto done;
        }
        if (!intOf(j, "stage", 1, 0xFFFF, &stage, &hs, "\"fallback\".", err, cap) || !intOf(j, "x", 0, 32767, &h->fallbackX, &hx, "\"fallback\".", err, cap) ||
            !intOf(j, "y", 0, 32767, &h->fallbackY, &hy, "\"fallback\".", err, cap) || !intOf(j, "dir", 0, 7, &h->fallbackDir, &hd, "\"fallback\".", err, cap))
            goto done;
        if (!hs || !hx || !hy) {
            say(err, cap, "\"fallback\" needs \"stage\" (a stage of the game), \"x\" and \"y\" (where the player appears there, in pixels)");
            goto done;
        }
        if (stage >= MS_NUMBER_BASE || (host && host->stageExists && !host->stageExists(stage)) || !host || !host->stageMode || host->stageMode(stage) <= 0) {
            say(err, cap, "\"fallback\".\"stage\" %d is not a stage of the game", stage);
            goto done;
        }
        h->fallbackMode = host->stageMode(stage);
    }
    if ((j = extJsonGet(&root, "start")) == NULL || j->t != 2) {
        say(err, cap, "\"start\" is required: {\"x\", \"y\", \"dir\"}, where the player appears (pixels of the map)");
        goto done;
    } else {
        static const char *const keys[] = {"x", "y", "dir"};
        int hx, hy, hd;

        if (!onlyKeys(j, keys, 3, "\"start\": ", err, cap) || !intOf(j, "x", 0, 32767, &h->startX, &hx, "\"start\".", err, cap) ||
            !intOf(j, "y", 0, 32767, &h->startY, &hy, "\"start\".", err, cap) || !intOf(j, "dir", 0, 7, &h->startDir, &hd, "\"start\".", err, cap))
            goto done;
        if (!hx || !hy) {
            say(err, cap, "\"start\" needs \"x\" and \"y\" (pixels of the map)");
            goto done;
        }
    }
    if (!intOf(&root, "textFile", 1, 0xFFFF, &h->textFile, &has, "", err, cap) || !intOf(&root, "soundBank", 0, 255, &h->soundBank, &has, "", err, cap)) goto done;
    if ((j = extJsonGet(&root, "music")) != NULL) {
        static const char *const keys[] = {"bank", "track"};
        int bank = 0, track = 0, hb;

        if (j->t != 2 || !onlyKeys(j, keys, 2, "\"music\": ", err, cap)) {
            if (j->t != 2) say(err, cap, "\"music\" must be an object {\"bank\", \"track\"}");
            goto done;
        }
        if (!intOf(j, "bank", 0, 255, &bank, &hb, "\"music\".", err, cap) || !intOf(j, "track", 0, 0xFFFF, &track, &has, "\"music\".", err, cap)) goto done;
        if (!hb) {
            say(err, cap, "\"music\" needs \"bank\" (the sound bank the game's own stages name in MUSIC(bank, track))");
            goto done;
        }
        h->music = MUSIC_VALUE(bank, track);
    }
    if ((j = extJsonGet(&root, "spriteColor")) != NULL) {
        if (j->t != 4 || j->n != 4) {
            say(err, cap, "\"spriteColor\" must be a list of four whole numbers 0-255: r, g, b and cd (0 leaves the sprites uncoloured)");
            goto done;
        }
        for (i = 0; i < 4; i++) {
            if (j->v[i].t != 0 || !extWhole(j->v[i].num, 0, 255)) {
                say(err, cap, "\"spriteColor\" must be a list of four whole numbers 0-255: r, g, b and cd (0 leaves the sprites uncoloured)");
                goto done;
            }
            h->color[i] = (int)j->v[i].num;
        }
    }
    if ((j = extJsonGet(&root, "banner")) != NULL) {
        static const char *const keys[] = {"area", "place"};
        const ExtJson *ja, *jp;

        if (j->t != 2 || !onlyKeys(j, keys, 2, "\"banner\": ", err, cap)) {
            if (j->t != 2) say(err, cap, "\"banner\" must be an object {\"area\": \"<mod>:<text>\", \"place\": \"<mod>:<text>\"}");
            goto done;
        }
        ja = extJsonGet(j, "area");
        jp = extJsonGet(j, "place");
        if (!ja || !jp || ja->t != 1 || jp->t != 1 || !ja->str || !jp->str || !scModNameValid(ja->str) || !scModNameValid(jp->str) ||
            strlen(ja->str) > MS_KEY_MAX || strlen(jp->str) > MS_KEY_MAX) {
            say(err, cap, "\"banner\" needs \"area\" and \"place\", each the name \"<mod>:<text>\" of a line of text/<lang>/stages.json (the area name's big line and the place name's)");
            goto done;
        }
        snprintf(h->bannerArea, sizeof h->bannerArea, "%s", ja->str);
        snprintf(h->bannerPlace, sizeof h->bannerPlace, "%s", jp->str);
    }
    ok = 1;
done:
    extJsonFree(&root);
    return ok;
}

/* ---- the maps ---- */

static int readPng(const char *path, int optional, PngImage *im, char *err, size_t cap) {
    uint8_t *data = NULL;
    size_t len = 0;
    char why[200];
    struct stat st;

    if (optional && stat(path, &st) != 0) return 1; /* absent: the caller's plane is all zero */
    if (tdReadFile(path, &data, &len)) {
        say(err, cap, "the file could not be read");
        return 0;
    }
    why[0] = '\0';
    if (pngReadIndexed(data, len, im, why, sizeof why)) {
        free(data);
        say(err, cap, "%s (an indexed PNG is needed: colour type 3, not interlaced)", why[0] ? why : "not a PNG the reader knows");
        return 0;
    }
    free(data);
    return 1;
}

/* the PSX 1555 colour of palette entry i: 5 bits a channel, the opaque bit set; a tRNS entry of 0 is the transparent word 0 */
static uint16_t clutOf(const PngImage *im, int i) {
    unsigned c;

    if (i >= im->nPal) return 0;
    if (i < im->nAlpha && im->alpha[i] == 0) return 0;
    c = (unsigned)(im->rgb[i * 3] >> 3) | ((unsigned)(im->rgb[i * 3 + 1] >> 3) << 5) | ((unsigned)(im->rgb[i * 3 + 2] >> 3) << 10);
    return (uint16_t)(0x8000u | c);
}

static void freeTiles(uint8_t **blocks, int n) {
    int i;

    for (i = 0; i < n; i++) free(blocks[i]);
    free(blocks);
}

/* the PACK of a background: tiles of 128 x 128 px, one sprite that shows each whole, the PNG's palette as the CLUT (map_pack.h) */
static int buildPack(const PngImage *bg, uint8_t **out, size_t *outLen, char *err, size_t cap) {
    int tw = bg->w / NS_CELL, th = bg->h / NS_CELL, n = tw * th, i, y, rc;
    uint8_t **blocks = (uint8_t **)calloc((size_t)n, sizeof *blocks);
    size_t *lens = (size_t *)calloc((size_t)n, sizeof *lens);
    uint8_t *pix = (uint8_t *)malloc((size_t)NS_CELL * NS_CELL);

    if (!blocks || !lens || !pix) {
        say(err, cap, "out of memory");
        free(blocks);
        free(lens);
        free(pix);
        return 0;
    }
    for (i = 0; i < n; i++) {
        MpTile t;
        int c;

        for (y = 0; y < NS_CELL; y++) {
            memcpy(pix + y * NS_CELL, bg->px + (size_t)((i / tw) * NS_CELL + y) * (size_t)bg->w + (size_t)((i % tw) * NS_CELL), NS_CELL);
        }
        mpTileSimple(&t, NS_CELL, NS_CELL, pix);
        t.rle = 1;
        for (c = 0; c < MP_CLUT; c++) t.clut[c] = clutOf(bg, c);
        rc = mpTileEncode(&t, &blocks[i], &lens[i]);
        if (rc != MP_OK) {
            say(err, cap, "tile %d: %s", i, mpErrorText(rc));
            freeTiles(blocks, n);
            free(lens);
            free(pix);
            return 0;
        }
    }
    rc = mpPackBuild(tw, th, 0, (const uint8_t *const *)blocks, lens, out, outLen);
    freeTiles(blocks, n);
    free(lens);
    free(pix);
    if (rc != MP_OK) {
        say(err, cap, "the tiles do not fit a PACK: %s", mpErrorText(rc));
        return 0;
    }
    return 1;
}

/* the floor values the field gives a meaning to (docs/formats.md 4.3): 0 closed, 1 flat, 2-7 slopes, 16+n the same mirrored */
static int floorOk(int v) {
    return v <= 7 || (v >= 16 && v <= 23);
}

/* Loads one stage folder. On failure returns 0 with the path of the file at fault in *at and the reason in err. */
static int loadStage(const SmHost *host, const char *dir, Header *h, Pending *pd, char *at, size_t atCap, char *err, size_t cap) {
    char path[1500];
    uint8_t *data = NULL;
    size_t len = 0;
    PngImage bg, fl, tr, ar;
    uint8_t *entry[3] = {NULL, NULL, NULL};
    size_t entryLen[3] = {0, 0, 0};
    uint8_t *zero = NULL;
    int ok = 0, i, x, y, w, hgt, rc;
    const uint8_t *layer[3];
    static const char *const names[3] = {"floor.png", "triggers.png", "areas.png"};

    memset(&bg, 0, sizeof bg);
    memset(&fl, 0, sizeof fl);
    memset(&tr, 0, sizeof tr);
    memset(&ar, 0, sizeof ar);
    pd->maxSlot = -1;

    snprintf(path, sizeof path, "%s/" NS_FILE, dir);
    snprintf(at, atCap, "%s", path);
    if (tdReadFile(path, &data, &len)) {
        say(err, cap, "the file could not be read");
        goto done;
    }
    if (!readHeader(host, (const char *)data, len, h, err, cap)) goto done;

    snprintf(path, sizeof path, "%s/background.png", dir);
    snprintf(at, atCap, "%s", path);
    if (!readPng(path, 0, &bg, err, cap)) goto done;
    w = bg.w;
    hgt = bg.h;
    if (w % NS_CELL || hgt % NS_CELL) {
        say(err, cap, "the size %d x %d is not a multiple of %d (a map is made of tiles of %d x %d px)", w, hgt, NS_CELL, NS_CELL, NS_CELL);
        goto done;
    }
    if (w / NS_CELL * (hgt / NS_CELL) > NS_MAX_TILES) {
        say(err, cap, "%d tiles of %d px (%d x %d px): the game keeps at most %d tiles of a map", w / NS_CELL * (hgt / NS_CELL), NS_CELL, w, hgt, NS_MAX_TILES);
        goto done;
    }
    if (w / NS_CELL > ML_MAX_CELLS || hgt / NS_CELL > ML_MAX_CELLS) {
        say(err, cap, "the map is too big (at most %d tiles each way)", ML_MAX_CELLS);
        goto done;
    }
    pd->tilesW = w / NS_CELL;
    pd->tilesH = hgt / NS_CELL;

    snprintf(path, sizeof path, "%s/%s", dir, names[0]);
    snprintf(at, atCap, "%s", path);
    if (!readPng(path, 0, &fl, err, cap)) goto done;
    snprintf(path, sizeof path, "%s/%s", dir, names[1]);
    snprintf(at, atCap, "%s", path);
    if (!readPng(path, 1, &tr, err, cap)) goto done;
    snprintf(path, sizeof path, "%s/%s", dir, names[2]);
    snprintf(at, atCap, "%s", path);
    if (!readPng(path, 1, &ar, err, cap)) goto done;
    {
        const PngImage *ims[3] = {&fl, &tr, &ar};

        for (i = 0; i < 3; i++) {
            if (ims[i]->px != NULL && (ims[i]->w != w || ims[i]->h != hgt)) {
                snprintf(path, sizeof path, "%s/%s", dir, names[i]);
                snprintf(at, atCap, "%s", path);
                say(err, cap, "it is %d x %d and the background is %d x %d (every layer has one pixel per pixel of the map)", ims[i]->w, ims[i]->h, w, hgt);
                goto done;
            }
        }
    }
    for (y = 0; y < hgt; y++) {
        for (x = 0; x < w; x++) {
            int v = fl.px[(size_t)y * (size_t)w + (size_t)x];

            if (!floorOk(v)) {
                snprintf(at, atCap, "%s/%s", dir, names[0]);
                say(err, cap, "the floor value %d (at %d, %d) has no meaning for the field: use 0 (closed), 1 (flat), 2-7 (slopes) or 16-23 (mirrored slopes)", v, x, y);
                goto done;
            }
            if (tr.px != NULL) {
                v = tr.px[(size_t)y * (size_t)w + (size_t)x];
                if (v != 0 && (v & 0x1F) > pd->maxSlot) pd->maxSlot = v & 0x1F;
            }
            if (ar.px != NULL) {
                v = ar.px[(size_t)y * (size_t)w + (size_t)x];
                if (v > pd->maxArea) pd->maxArea = v;
            }
        }
    }
    if (h->startX >= w || h->startY >= hgt) {
        snprintf(at, atCap, "%s/" NS_FILE, dir);
        say(err, cap, "\"start\" (%d, %d) is outside the map (%d x %d)", h->startX, h->startY, w, hgt);
        goto done;
    }
    if (fl.px[(size_t)h->startY * (size_t)w + (size_t)h->startX] == 0) {
        plogLine("stages", "%s/" NS_FILE ": the start (%d, %d) is on a closed cell of floor.png (value 0): the player would begin inside a wall\n", dir, h->startX,
                 h->startY);
    }

    zero = (uint8_t *)calloc((size_t)w * (size_t)hgt, 1);
    if (!zero) {
        say(err, cap, "out of memory");
        goto done;
    }
    layer[0] = fl.px;
    layer[1] = tr.px != NULL ? tr.px : zero;
    layer[2] = ar.px != NULL ? ar.px : zero;
    for (i = 0; i < 3; i++) {
        rc = mlEncode(layer[i], w, hgt, &entry[i], &entryLen[i]);
        if (rc != ML_OK) {
            snprintf(at, atCap, "%s/%s", dir, names[i]);
            say(err, cap, "the layer does not fit the map format: %s", mlErrorText(rc));
            goto done;
        }
    }
    snprintf(at, atCap, "%s/background.png", dir);
    if (!buildPack(&bg, &pd->pack, &pd->packLen, err, cap)) goto done;
    rc = mlArchiveBuild(NULL, 0, (const uint8_t *const *)entry, entryLen, 3, &pd->layers, &pd->layersLen);
    if (rc != ML_OK) {
        free(pd->pack);
        pd->pack = NULL;
        snprintf(at, atCap, "%s/" NS_FILE, dir);
        say(err, cap, "the layers do not fit a TMPK: %s", mlErrorText(rc));
        goto done;
    }
    ok = 1;
done:
    free(data);
    free(zero);
    for (i = 0; i < 3; i++) free(entry[i]);
    pngImageFree(&bg);
    pngImageFree(&fl);
    pngImageFree(&tr);
    pngImageFree(&ar);
    return ok;
}

/* ---- the registry side ---- */

static Pending *pendingOf(const char *key) {
    int i;

    for (i = 0; key != NULL && i < sPendCount; i++) {
        if (!strcmp(sPend[i]->key, key)) return sPend[i];
    }
    return NULL;
}

static void pendFree(Pending *p) {
    if (!p) return;
    free(p->layers);
    free(p->pack);
    free(p);
}

void nsReset(void) {
    int i;

    for (i = 0; i < sPendCount; i++) {
        pendFree(sPend[i]);
        sPend[i] = NULL;
    }
    sPendCount = 0;
}

int nsPending(void) {
    return sPendCount;
}

/* s<nnn> (any case) is the folder of an original stage: its stage.json is a stage-patch-v1, and the loaders of the original stages own it */
static int isOriginalFolder(const char *name) {
    int i;

    if (strlen(name) != 4 || tolower((unsigned char)name[0]) != 's') return 0;
    for (i = 1; i <= 3; i++) {
        if (!isdigit((unsigned char)name[i])) return 0;
    }
    return 1;
}

int nsScan(const SmHost *host) {
    int m, kept = 0;

    nsReset();
    for (m = 0; m < modsCount(); m++) {
        char root[1400];
        DIR *d;
        struct dirent *e;

        if (!assetModDir(m, NULL, NS_DIR, root, sizeof root)) continue;
        d = opendir(root);
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            char dir[1700], key[200], at[1800], err[400], why[300];
            struct stat st;
            Header h;
            Pending *pd;
            MsStage *ms;

            if (e->d_name[0] == '.') continue;
            snprintf(dir, sizeof dir, "%s/%s", root, e->d_name);
            if (stat(dir, &st) || !S_ISDIR(st.st_mode)) continue;
            if (isOriginalFolder(e->d_name)) continue;
            if (!extValidKey(e->d_name)) {
                plogLine("stages", "%s ignored: a new stage's folder is named with 1 to 48 characters of a-z, 0-9, - and _ (s<nnn> is an original stage)\n", dir);
                continue;
            }
            snprintf(key, sizeof key, "%s:%s", modsNameOf(m), e->d_name);
            pd = (Pending *)calloc(1, sizeof *pd);
            if (!pd) continue;
            snprintf(pd->key, sizeof pd->key, "%s", key);
            at[0] = err[0] = '\0';
            if (!loadStage(host, dir, &h, pd, at, sizeof at, err, sizeof err)) {
                plogLine("stages", "%s rejected: %s\n", at[0] ? at : dir, err[0] ? err : "the stage could not be built");
                pendFree(pd);
                continue;
            }
            why[0] = '\0';
            ms = msRegister(key, why, sizeof why);
            if (!ms) {
                plogLine("stages", "%s rejected: %s\n", dir, why[0] ? why : "the stage could not be registered");
                pendFree(pd);
                continue;
            }
            ms->fallbackMode = h.fallbackMode;
            ms->fallbackX = h.fallbackX;
            ms->fallbackY = h.fallbackY;
            ms->fallbackDir = h.fallbackDir;
            ms->textFile = h.textFile;
            ms->soundBank = h.soundBank;
            ms->music = h.music;
            ms->startX = h.startX;
            ms->startY = h.startY;
            ms->startDir = h.startDir;
            memcpy(ms->spriteColor, h.color, sizeof ms->spriteColor);
            memcpy(ms->bannerArea, h.bannerArea, sizeof ms->bannerArea);
            memcpy(ms->bannerPlace, h.bannerPlace, sizeof ms->bannerPlace);
            if (sPendCount < MS_MODE_COUNT) {
                sPend[sPendCount++] = pd;
                kept++;
            } else {
                pendFree(pd);
            }
        }
        closedir(d);
    }
    return kept;
}

int nsCheck(const char *key, const SmNewInfo *info, char *why, size_t whySize) {
    const Pending *pd = pendingOf(key);

    if (!pd || !info) return 0;
    if (pd->maxSlot >= info->slots) {
        say(why, whySize, "the trigger layer names the slot %d (a cell holds direction << 5 | slot) and the stage's \"slots\".\"add\" gives %d slot(s): slots count from 0, in the order they are added",
            pd->maxSlot, info->slots);
        return 1;
    }
    if (pd->maxArea > info->battleAreas) {
        say(why, whySize, "the area layer holds the area %d and the stage's \"battles\".\"add\" gives %d area(s): an area cell n is the n-th area added, from 1", pd->maxArea,
            info->battleAreas);
        return 1;
    }
    return 0;
}

void nsRefuse(const char *key) {
    msRefuse(key);
}

int nsInstall(void) {
    int i, n = 0;

    for (i = 0; i < msCount(); i++) {
        MsStage *ms = msAt(i);
        Pending *pd;

        if (!ms || ms->refused) continue;
        pd = pendingOf(ms->key);
        if (!pd) continue;
        if (msSetFiles(ms, pd->layers, pd->layersLen, pd->pack, pd->packLen) != 0) {
            plogLine("stages", "%s rejected: the port has no room for the stage's files\n", ms->key);
            msRefuse(ms->key);
            continue;
        }
        PLOG("stages", PLOG_MODS, "new stage %s: mode 0x%X, %d x %d tiles, files %d (layers) and %d (tiles)\n", ms->key, ms->mode, pd->tilesW, pd->tilesH, ms->layerFile,
             ms->packFile);
        n++;
    }
    nsReset();
    return n;
}
