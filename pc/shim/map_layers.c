/* Codec of the map layers of a SnnnTMPK.BIN: see map_layers.h. Portable C99, no game header. */

#include "map_layers.h"

#include <stdlib.h>
#include <string.h>

const char *mlErrorText(int code) {
    switch (code) {
    case ML_OK:
        return "ok";
    case ML_ERR_ARG:
        return "bad argument";
    case ML_ERR_SIZE:
        return "size must be 1..255 cells of 128 px each way (and within the area limit)";
    case ML_ERR_TOO_MANY:
        return "too many distinct blocks for the format's indices (256 at 128/64 px, 32767 below)";
    case ML_ERR_FORMAT:
        return "damaged or truncated data";
    case ML_ERR_MEMORY:
        return "out of memory";
    }
    return "unknown error";
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static int rdS16(const uint8_t *p) {
    return (int16_t)(uint16_t)(p[0] | (p[1] << 8));
}

void mlImageFree(MlImage *img) {
    if (img != NULL) {
        free(img->v);
        img->v = NULL;
        img->w = img->h = 0;
    }
}

/* ---- decode ---- */

int mlDecode(const uint8_t *entry, size_t len, MlImage *out) {
    uint32_t off[6];
    size_t i, nodes64, nodes32, nodes16, nodes8, blocks;
    int gw, gh, W, H, bx, by;
    const uint8_t *grid, *c64, *c32, *c16, *c8, *px;
    uint8_t *v;

    if (out == NULL) {
        return ML_ERR_ARG;
    }
    out->w = out->h = 0;
    out->v = NULL;
    if (entry == NULL) {
        return ML_ERR_ARG;
    }
    if (len < 24) {
        return ML_ERR_FORMAT;
    }
    for (i = 0; i < 6; i++) {
        off[i] = rd32(entry + i * 4);
        if (off[i] > len || off[i] < 24 || (i > 0 && off[i] < off[i - 1])) {
            return ML_ERR_FORMAT;
        }
    }
    gw = entry[off[0]];
    gh = off[0] + 2 <= len ? entry[off[0] + 1] : 0;
    if (gw == 0 || gh == 0 || (size_t)gw * (size_t)gh + 2 > off[1] - off[0]) {
        return ML_ERR_FORMAT;
    }
    W = gw * ML_CELL;
    H = gh * ML_CELL;
    if ((long)W * (long)H > ML_MAX_PIXELS) {
        return ML_ERR_SIZE;
    }
    nodes64 = (off[2] - off[1]) / 4;
    nodes32 = (off[3] - off[2]) / 8;
    nodes16 = (off[4] - off[3]) / 8;
    nodes8 = (off[5] - off[4]) / 8;
    blocks = (len - off[5]) / 64;
    grid = entry + off[0] + 2;
    c64 = entry + off[1];
    c32 = entry + off[2];
    c16 = entry + off[3];
    c8 = entry + off[4];
    px = entry + off[5];

    v = (uint8_t *)malloc((size_t)W * (size_t)H);
    if (v == NULL) {
        return ML_ERR_MEMORY;
    }
    for (by = 0; by < H; by += 8) {
        for (bx = 0; bx < W; bx += 8) {
            size_t n = grid[(by / ML_CELL) * gw + bx / ML_CELL];
            int q, r;
            if (n >= nodes64) {
                goto bad;
            }
            q = (by & 0x40 ? 2 : 0) + (bx & 0x40 ? 1 : 0);
            n = c64[n * 4 + q];
            if (n >= nodes32) {
                goto bad;
            }
            q = (by & 0x20 ? 2 : 0) + (bx & 0x20 ? 1 : 0);
            r = rdS16(c32 + (n * 4 + q) * 2);
            if (r < 0 || (size_t)r >= nodes16) {
                goto bad;
            }
            q = (by & 0x10 ? 2 : 0) + (bx & 0x10 ? 1 : 0);
            r = rdS16(c16 + ((size_t)r * 4 + q) * 2);
            if (r < 0 || (size_t)r >= nodes8) {
                goto bad;
            }
            q = (by & 8 ? 2 : 0) + (bx & 8 ? 1 : 0);
            r = rdS16(c8 + ((size_t)r * 4 + q) * 2);
            if (r < 0 || (size_t)r >= blocks) {
                goto bad;
            }
            for (q = 0; q < 8; q++) {
                memcpy(v + (size_t)(by + q) * W + bx, px + (size_t)r * 64 + q * 8, 8);
            }
        }
    }
    out->w = W;
    out->h = H;
    out->v = v;
    return ML_OK;
bad:
    free(v);
    return ML_ERR_FORMAT;
}

/* ---- encode ---- */

/* Interns keys of keyLen bytes: id = order of first appearance. */
typedef struct {
    size_t keyLen, count, cap, tabSize;
    uint8_t *keys;
    int32_t *tab; /* id + 1, 0 = empty */
} Interner;

static int internInit(Interner *t, size_t keyLen, size_t expect) {
    size_t sz = 16;

    while (sz < expect * 2) {
        sz <<= 1;
    }
    t->keyLen = keyLen;
    t->count = 0;
    t->cap = 64;
    t->tabSize = sz;
    t->keys = (uint8_t *)malloc(t->cap * keyLen);
    t->tab = (int32_t *)calloc(sz, sizeof(int32_t));
    return t->keys != NULL && t->tab != NULL;
}

static void internFree(Interner *t) {
    free(t->keys);
    free(t->tab);
    t->keys = NULL;
    t->tab = NULL;
}

/* Returns the id, or -1 when out of memory. */
static int32_t intern(Interner *t, const uint8_t *key) {
    uint32_t h = 2166136261u;
    size_t i, slot;

    for (i = 0; i < t->keyLen; i++) {
        h = (h ^ key[i]) * 16777619u;
    }
    slot = h & (t->tabSize - 1);
    while (t->tab[slot] != 0) {
        int32_t id = t->tab[slot] - 1;
        if (memcmp(t->keys + (size_t)id * t->keyLen, key, t->keyLen) == 0) {
            return id;
        }
        slot = (slot + 1) & (t->tabSize - 1);
    }
    if ((t->count + 1) * 2 > t->tabSize) { /* grow and rehash: keep the load under one half */
        size_t ns = t->tabSize * 2, k;
        int32_t *nt = (int32_t *)calloc(ns, sizeof(int32_t));
        if (nt == NULL) {
            return -1;
        }
        for (k = 0; k < t->count; k++) {
            uint32_t hh = 2166136261u;
            size_t j, s2;
            for (j = 0; j < t->keyLen; j++) {
                hh = (hh ^ t->keys[k * t->keyLen + j]) * 16777619u;
            }
            s2 = hh & (ns - 1);
            while (nt[s2] != 0) {
                s2 = (s2 + 1) & (ns - 1);
            }
            nt[s2] = (int32_t)(k + 1);
        }
        free(t->tab);
        t->tab = nt;
        t->tabSize = ns;
        slot = h & (ns - 1);
        while (t->tab[slot] != 0) {
            slot = (slot + 1) & (ns - 1);
        }
    }
    if (t->count == t->cap) {
        uint8_t *nk = (uint8_t *)realloc(t->keys, t->cap * 2 * t->keyLen);
        if (nk == NULL) {
            return -1;
        }
        t->keys = nk;
        t->cap *= 2;
    }
    memcpy(t->keys + t->count * t->keyLen, key, t->keyLen);
    t->tab[slot] = (int32_t)++t->count;
    return (int32_t)(t->count - 1);
}

/*
 * Node ids are assigned level by level, each level scanned row-major over the whole plane, a block getting the next id the first
 * time its contents appear. (The disc's own files were checked against this order: see maplayertest and docs/formats.md 4.3.)
 * Level 0 = 8x8 pixel blocks, level 1 = 16 px nodes (cells8), 2 = 32 px (cells16), 3 = 64 px (cells32), 4 = 128 px (cells64).
 */
int mlEncode(const uint8_t *v, int w, int h, uint8_t **out, size_t *len) {
    Interner tab[5];
    int32_t *ids[5] = {0, 0, 0, 0, 0};
    int nx[5], ny[5];
    int l, x, y, rc = ML_OK;
    size_t counts[5], sizes[7], off[6], total;
    uint8_t *buf = NULL;
    int gw, gh;
    static const size_t maxNodes[5] = {ML_MAX_NODES_SMALL, ML_MAX_NODES_SMALL, ML_MAX_NODES_SMALL, ML_MAX_NODES_32, ML_MAX_NODES_64};

    if (out != NULL) {
        *out = NULL;
    }
    if (len != NULL) {
        *len = 0;
    }
    if (v == NULL || out == NULL || len == NULL) {
        return ML_ERR_ARG;
    }
    if (w <= 0 || h <= 0 || w % ML_CELL != 0 || h % ML_CELL != 0 || w / ML_CELL > ML_MAX_CELLS || h / ML_CELL > ML_MAX_CELLS
        || (long)w * (long)h > ML_MAX_PIXELS) {
        return ML_ERR_SIZE;
    }
    gw = w / ML_CELL;
    gh = h / ML_CELL;
    memset(tab, 0, sizeof tab);
    for (l = 0; l < 5; l++) {
        nx[l] = w >> (3 + l);
        ny[l] = h >> (3 + l);
        ids[l] = (int32_t *)malloc((size_t)nx[l] * (size_t)ny[l] * sizeof(int32_t));
        if (ids[l] == NULL || !internInit(&tab[l], l == 0 ? 64 : 4 * sizeof(int32_t), 256)) {
            rc = ML_ERR_MEMORY;
            goto done;
        }
    }
    for (l = 0; l < 5; l++) {
        for (y = 0; y < ny[l]; y++) {
            for (x = 0; x < nx[l]; x++) {
                int32_t id;
                if (l == 0) {
                    uint8_t key[64];
                    int r;
                    for (r = 0; r < 8; r++) {
                        memcpy(key + r * 8, v + (size_t)(y * 8 + r) * w + x * 8, 8);
                    }
                    id = intern(&tab[0], key);
                } else {
                    int32_t key[4];
                    key[0] = ids[l - 1][(size_t)(2 * y) * nx[l - 1] + 2 * x];
                    key[1] = ids[l - 1][(size_t)(2 * y) * nx[l - 1] + 2 * x + 1];
                    key[2] = ids[l - 1][(size_t)(2 * y + 1) * nx[l - 1] + 2 * x];
                    key[3] = ids[l - 1][(size_t)(2 * y + 1) * nx[l - 1] + 2 * x + 1];
                    id = intern(&tab[l], (const uint8_t *)key);
                }
                if (id < 0) {
                    rc = ML_ERR_MEMORY;
                    goto done;
                }
                ids[l][(size_t)y * nx[l] + x] = id;
            }
        }
    }
    for (l = 0; l < 5; l++) {
        counts[l] = tab[l].count;
        if (counts[l] > maxNodes[l]) {
            rc = ML_ERR_TOO_MANY;
            goto done;
        }
    }
    /* layout: header, grid (4-aligned), cells64 (u8 x4), cells32/16/8 (s16 x4), pixel blocks */
    sizes[0] = 24;
    sizes[1] = (2 + (size_t)gw * gh + 3) & ~(size_t)3;
    sizes[2] = counts[4] * 4;
    sizes[3] = counts[3] * 8;
    sizes[4] = counts[2] * 8;
    sizes[5] = counts[1] * 8;
    sizes[6] = counts[0] * 64;
    total = 0;
    for (l = 0; l < 7; l++) {
        if (l >= 1) {
            off[l - 1] = total;
        }
        total += sizes[l];
    }
    /* off[0..5] = start of grid, cells64, cells32, cells16, cells8, pixels */
    buf = (uint8_t *)calloc(total, 1);
    if (buf == NULL) {
        rc = ML_ERR_MEMORY;
        goto done;
    }
    for (l = 0; l < 6; l++) {
        wr32(buf + l * 4, (uint32_t)off[l]);
    }
    buf[off[0]] = (uint8_t)gw;
    buf[off[0] + 1] = (uint8_t)gh;
    for (y = 0; y < gw * gh; y++) {
        buf[off[0] + 2 + y] = (uint8_t)ids[4][y];
    }
    {
        size_t i;
        const int32_t *k;
        for (i = 0; i < counts[4]; i++) {
            k = (const int32_t *)(tab[4].keys + i * 16);
            buf[off[1] + i * 4] = (uint8_t)k[0];
            buf[off[1] + i * 4 + 1] = (uint8_t)k[1];
            buf[off[1] + i * 4 + 2] = (uint8_t)k[2];
            buf[off[1] + i * 4 + 3] = (uint8_t)k[3];
        }
        for (l = 3; l >= 1; l--) {
            for (i = 0; i < counts[l]; i++) {
                int q;
                k = (const int32_t *)(tab[l].keys + i * 16);
                for (q = 0; q < 4; q++) {
                    uint8_t *p = buf + off[5 - l] + (i * 4 + (size_t)q) * 2;
                    p[0] = (uint8_t)(k[q] & 255);
                    p[1] = (uint8_t)((k[q] >> 8) & 255);
                }
            }
        }
        memcpy(buf + off[5], tab[0].keys, counts[0] * 64);
    }
    *out = buf;
    *len = total;
    buf = NULL;
done:
    free(buf);
    for (l = 0; l < 5; l++) {
        free(ids[l]);
        internFree(&tab[l]);
    }
    return rc;
}

/* ---- archive ---- */

int mlArchiveCount(const uint8_t *data, size_t len) {
    uint32_t first = 0, prev, o;
    size_t i, count;

    if (data == NULL || len < 4) {
        return ML_ERR_FORMAT;
    }
    /* The table ends where the first entry that is there begins; slots before it may be 0 (absent) */
    for (i = 0; i * 4 + 4 <= len && i < 0x8000; i++) {
        first = rd32(data + i * 4);
        if (first != 0) {
            break;
        }
    }
    if (first < 4 || first % 4 != 0 || first > len || i >= first / 4) {
        return ML_ERR_FORMAT;
    }
    count = first / 4;
    if (count > 0x7FFF) {
        return ML_ERR_FORMAT;
    }
    prev = first;
    for (i = 0; i < count; i++) {
        o = rd32(data + i * 4);
        if (o == 0) { /* an absent entry (the game's "no file" for that layer) */
            continue;
        }
        if (o < prev || o > len) {
            return ML_ERR_FORMAT;
        }
        prev = o;
    }
    return (int)count;
}

int mlArchiveEntry(const uint8_t *data, size_t len, int n, const uint8_t **entry, size_t *size) {
    int count = mlArchiveCount(data, len);
    size_t start, end;

    if (count < 0) {
        return count;
    }
    if (n < 0 || n >= count || entry == NULL || size == NULL) {
        return ML_ERR_ARG;
    }
    start = rd32(data + (size_t)n * 4);
    if (start == 0) { /* absent */
        *entry = NULL;
        *size = 0;
        return ML_OK;
    }
    end = len;
    for (n++; n < count; n++) { /* the next entry that is there */
        size_t o = rd32(data + (size_t)n * 4);
        if (o != 0) {
            end = o;
            break;
        }
    }
    *entry = data + start;
    *size = end - start;
    return ML_OK;
}

int mlArchiveBuild(const uint8_t *sheet, size_t sheetLen, const uint8_t *const *layers, const size_t *layerLens, int nLayers,
                   uint8_t **out, size_t *outLen) {
    size_t table, total, pos;
    uint8_t *buf;
    int i;

    if (out != NULL) {
        *out = NULL;
    }
    if (out == NULL || outLen == NULL || nLayers < 0 || (nLayers > 0 && (layers == NULL || layerLens == NULL))
        || (sheetLen > 0 && sheet == NULL) || nLayers >= 0x7FFF) {
        return ML_ERR_ARG;
    }
    table = ((size_t)nLayers + 1) * 4;
    total = table + ((sheetLen + 3) & ~(size_t)3);
    for (i = 0; i < nLayers; i++) {
        if (layers[i] != NULL) { /* a NULL layer is an absent entry: offset 0 */
            total += (layerLens[i] + 3) & ~(size_t)3;
        }
    }
    buf = (uint8_t *)calloc(total > 0 ? total : 1, 1);
    if (buf == NULL) {
        return ML_ERR_MEMORY;
    }
    pos = table;
    wr32(buf, (uint32_t)pos);
    if (sheetLen > 0) {
        memcpy(buf + pos, sheet, sheetLen);
    }
    pos += (sheetLen + 3) & ~(size_t)3;
    for (i = 0; i < nLayers; i++) {
        if (layers[i] == NULL) {
            continue;
        }
        wr32(buf + (size_t)(i + 1) * 4, (uint32_t)pos);
        if (layerLens[i] > 0) {
            memcpy(buf + pos, layers[i], layerLens[i]);
        }
        pos += (layerLens[i] + 3) & ~(size_t)3;
    }
    *out = buf;
    *outLen = total;
    return ML_OK;
}
