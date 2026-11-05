/* The Z_STAGE SnnnPACK.BIN codec (new-stages T3); see map_pack.h. Portable C99. */
#include "map_pack.h"

#include <stdlib.h>
#include <string.h>

#define RLEN_MAGIC 0x4E454C52u /* "RLEN", RLEN_MAGIC of include/engine/file.h:103 */

const char *mpErrorText(int code) {
    switch (code) {
    case MP_OK: return "ok";
    case MP_ERR_ARG: return "bad argument";
    case MP_ERR_SIZE: return "size is zero, not a whole number of sectors, or does not fit";
    case MP_ERR_LIMIT: return "past a limit of the map reader";
    case MP_ERR_FORMAT: return "damaged or unsupported pack or tile";
    case MP_ERR_MEMORY: return "out of memory";
    default: return "unknown error";
    }
}

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int rd16s(const uint8_t *p) { return (int16_t)(uint16_t)(p[0] | (p[1] << 8)); }
static unsigned rd16(const uint8_t *p) { return (unsigned)(p[0] | (p[1] << 8)); }
static void wr32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static void wr16(uint8_t *p, unsigned v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* ---------------------------------------------------------------- container */

int mpPackParse(const uint8_t *data, size_t len, MpPack *out) {
    int n, i;

    if (data == NULL || out == NULL) {
        return MP_ERR_ARG;
    }
    memset(out, 0, sizeof *out);
    if (len < MP_SECTOR) {
        return MP_ERR_FORMAT;
    }
    out->tileCount = (int)rd32(data);
    out->width = (int)rd32(data + 4);
    out->height = (int)rd32(data + 8);
    out->tileBytes = (int)rd32(data + 12);
    if (out->width <= 0 || out->height <= 0 || out->width > MP_MAX_HEADER_TILES || out->height > MP_MAX_HEADER_TILES ||
        out->width * out->height > MP_MAX_HEADER_TILES) {
        return MP_ERR_FORMAT;
    }
    if (out->tileBytes <= 0 || out->tileBytes % MP_SECTOR != 0 || out->tileBytes > 0x100000) {
        return MP_ERR_FORMAT;
    }
    n = out->width * out->height;
    for (i = 0; i < n; i++) {
        unsigned s = rd16(data + 16 + 2 * i);
        if (s % MP_SECTOR != 0 || s > (unsigned)out->tileBytes) {
            return MP_ERR_FORMAT; /* the reader reads size >> 11 sectors into a tileBytes buffer */
        }
        out->sizes[i] = (uint16_t)s;
        if (s != 0 && (size_t)MP_SECTOR + (size_t)i * (size_t)out->tileBytes + s > len) {
            return MP_ERR_FORMAT; /* the tile is cut by the end of the file */
        }
    }
    return MP_OK;
}

int mpPackTile(const uint8_t *data, size_t len, const MpPack *p, int frame, const uint8_t **block, size_t *blockLen) {
    (void)len;
    if (data == NULL || p == NULL || block == NULL || blockLen == NULL || frame < 0 || frame >= p->width * p->height) {
        return MP_ERR_ARG;
    }
    if (p->sizes[frame] == 0) {
        *block = NULL;
        *blockLen = 0;
        return MP_OK;
    }
    *block = data + MP_SECTOR + (size_t)frame * (size_t)p->tileBytes;
    *blockLen = p->sizes[frame];
    return MP_OK;
}

int mpPackBuild(int width, int height, int tileBytes, const uint8_t *const *blocks, const size_t *blockLens, uint8_t **out, size_t *outLen) {
    int n, i, count = 0;
    size_t biggest = MP_SECTOR, total;
    uint8_t *d;

    if (out != NULL) {
        *out = NULL;
    }
    if (blocks == NULL || blockLens == NULL || out == NULL || outLen == NULL) {
        return MP_ERR_ARG;
    }
    if (width <= 0 || height <= 0 || width > MP_MAX_TILES || height > MP_MAX_TILES) {
        return MP_ERR_SIZE;
    }
    if (width * height > MP_MAX_TILES) {
        return MP_ERR_LIMIT;
    }
    n = width * height;
    for (i = 0; i < n; i++) {
        size_t l = blocks[i] != NULL ? blockLens[i] : 0;
        if (l == 0) {
            continue;
        }
        if (l % MP_SECTOR != 0) {
            return MP_ERR_SIZE;
        }
        if (l > MP_MAX_TILE_BYTES) {
            return MP_ERR_LIMIT;
        }
        if (l > biggest) {
            biggest = l;
        }
        count++;
    }
    if (tileBytes == 0) {
        tileBytes = (int)biggest;
    }
    if (tileBytes <= 0 || tileBytes % MP_SECTOR != 0 || (size_t)tileBytes < biggest) {
        return MP_ERR_SIZE;
    }
    if (tileBytes > MP_MAX_TILE_BYTES) {
        return MP_ERR_LIMIT;
    }
    total = (size_t)MP_SECTOR + (size_t)n * (size_t)tileBytes;
    d = (uint8_t *)calloc(total, 1);
    if (d == NULL) {
        return MP_ERR_MEMORY;
    }
    wr32(d, (uint32_t)count);
    wr32(d + 4, (uint32_t)width);
    wr32(d + 8, (uint32_t)height);
    wr32(d + 12, (uint32_t)tileBytes);
    for (i = 0; i < n; i++) {
        size_t l = blocks[i] != NULL ? blockLens[i] : 0;
        wr16(d + 16 + 2 * i, (unsigned)l);
        if (l != 0) {
            memcpy(d + MP_SECTOR + (size_t)i * (size_t)tileBytes, blocks[i], l);
        }
    }
    *out = d;
    *outLen = total;
    return MP_OK;
}

/* ---------------------------------------------------------------- RLEN */

/* The Decompressor's loop (src/engine/file/decompressor.c:48-69) with bounds: a count byte with bit 7 repeats the next byte count & 0x7F
   times, otherwise it copies count bytes; a 0 ends. */
int mpRlenUnpack(const uint8_t *block, size_t len, uint8_t **out, size_t *outLen) {
    size_t want, i = 8, o = 0;
    uint8_t *d;

    if (out != NULL) {
        *out = NULL;
    }
    if (block == NULL || out == NULL || outLen == NULL) {
        return MP_ERR_ARG;
    }
    if (len < 9 || rd32(block) != RLEN_MAGIC) {
        return MP_ERR_FORMAT;
    }
    want = rd32(block + 4); /* decompressorSetData: task->size = data[1], the buffer it unpacks into (decompressor.c:17) */
    if (want == 0 || want > MP_MAX_UNPACKED) {
        return MP_ERR_FORMAT;
    }
    d = (uint8_t *)malloc(want);
    if (d == NULL) {
        return MP_ERR_MEMORY;
    }
    while (i < len && block[i] != 0) {
        size_t n = block[i] & 0x7Fu;
        if (block[i] & 0x80u) {
            if (i + 1 >= len || o + n > want) {
                free(d);
                return MP_ERR_FORMAT;
            }
            memset(d + o, block[i + 1], n);
            i += 2;
        } else {
            if (i + 1 + n > len || o + n > want) {
                free(d);
                return MP_ERR_FORMAT;
            }
            memcpy(d + o, block + i + 1, n);
            i += 1 + n;
        }
        o += n;
    }
    if (i >= len || o != want) { /* no end marker, or the size word lies */
        free(d);
        return MP_ERR_FORMAT;
    }
    *out = d;
    *outLen = o;
    return MP_OK;
}

int mpRlenPack(const uint8_t *src, size_t len, uint8_t **out, size_t *outLen) {
    size_t cap, i = 0, o = 8, litStart = 0, litLen = 0;
    uint8_t *d;

    if (out != NULL) {
        *out = NULL;
    }
    if (src == NULL || out == NULL || outLen == NULL) {
        return MP_ERR_ARG;
    }
    if (len == 0 || len > MP_MAX_UNPACKED) {
        return MP_ERR_SIZE;
    }
    cap = 8 + len + len / 127 + 16;
    d = (uint8_t *)malloc(cap);
    if (d == NULL) {
        return MP_ERR_MEMORY;
    }
    wr32(d, RLEN_MAGIC);
    wr32(d + 4, (uint32_t)len);
    while (i < len) {
        size_t j = i;
        while (j < len && src[j] == src[i] && j - i < 127) {
            j++;
        }
        if (j - i >= 2) { /* a run: flush the literal before it */
            if (litLen != 0) {
                d[o++] = (uint8_t)litLen;
                memcpy(d + o, src + litStart, litLen);
                o += litLen;
                litLen = 0;
            }
            d[o++] = (uint8_t)(0x80u | (unsigned)(j - i));
            d[o++] = src[i];
            i = j;
        } else {
            if (litLen == 0) {
                litStart = i;
            }
            litLen++;
            i++;
            if (litLen == 127) {
                d[o++] = (uint8_t)litLen;
                memcpy(d + o, src + litStart, litLen);
                o += litLen;
                litLen = 0;
            }
        }
    }
    if (litLen != 0) {
        d[o++] = (uint8_t)litLen;
        memcpy(d + o, src + litStart, litLen);
        o += litLen;
    }
    d[o++] = 0;
    *out = d;
    *outLen = o;
    return MP_OK;
}

/* ---------------------------------------------------------------- tiles */

void mpTileSimple(MpTile *t, int width, int height, uint8_t *pix) {
    memset(t, 0, sizeof *t);
    t->w = width;
    t->h = height;
    t->pix = pix;
    t->count[0] = 1;
    t->sp[0][0].w = width;
    t->sp[0][0].h = height;
}

void mpTileFree(MpTile *t) {
    if (t != NULL) {
        free(t->pix);
        t->pix = NULL;
    }
}

/* the unpacked tile: offset word, three lists, the TIM at the offset (stream.c:83-107 reads them, tim_loader.c:27-60 the TIM) */
int mpTileDecode(const uint8_t *block, size_t len, MpTile *t) {
    uint8_t *own = NULL;
    const uint8_t *d;
    size_t dl, off, p, q;
    int i, j, rle = 0, iw, ih;
    unsigned flag;

    if (t != NULL) {
        memset(t, 0, sizeof *t);
    }
    if (block == NULL || t == NULL) {
        return MP_ERR_ARG;
    }
    if (len >= 4 && rd32(block) == RLEN_MAGIC) {
        int r = mpRlenUnpack(block, len, &own, &dl);
        if (r != MP_OK) {
            return r;
        }
        d = own;
        rle = 1;
    } else {
        d = block; /* plain: the Decompressor hands the block itself over (decompressor.c:101-105) */
        dl = len;
    }
    t->rle = rle;
    if (dl < 20) {
        goto bad;
    }
    off = rd32(d);
    if (off < 16 || off > dl) {
        goto bad;
    }
    p = 4;
    for (i = 0; i < MP_LISTS; i++) {
        unsigned n;
        if (p + 4 > off) {
            goto bad;
        }
        n = rd32(d + p);
        p += 4;
        if (n > MP_MAX_LIST_READ || p + 12 * (size_t)n > off) {
            goto bad;
        }
        t->count[i] = (int)n;
        for (j = 0; j < (int)n; j++, p += 12) {
            t->sp[i][j].x = rd16s(d + p);
            t->sp[i][j].y = rd16s(d + p + 2);
            t->sp[i][j].u = rd16s(d + p + 4);
            t->sp[i][j].v = rd16s(d + p + 6);
            t->sp[i][j].w = rd16s(d + p + 8);
            t->sp[i][j].h = rd16s(d + p + 10);
        }
    }
    if (p != off) { /* the lists end where the TIM starts (the disc: 11745 of 11745) */
        goto bad;
    }
    /* TIM: id 0x10, flag 9 (8 bit with a CLUT), the CLUT block 256 x 1, the image block */
    if (off + 8 > dl || rd32(d + off) != 0x10) {
        goto bad;
    }
    flag = rd32(d + off + 4);
    if (flag != 9) {
        goto bad;
    }
    q = off + 8;
    if (q + 12 + 2 * MP_CLUT > dl || rd32(d + q) != 12 + 2 * MP_CLUT || rd16(d + q + 8) != MP_CLUT || rd16(d + q + 10) != 1) {
        goto bad;
    }
    t->clutX = (int)rd16(d + q + 4);
    t->clutY = (int)rd16(d + q + 6);
    for (i = 0; i < MP_CLUT; i++) {
        t->clut[i] = (uint16_t)rd16(d + q + 12 + 2 * i);
    }
    q += 12 + 2 * MP_CLUT;
    if (q + 12 > dl) {
        goto bad;
    }
    iw = (int)rd16(d + q + 8);
    ih = (int)rd16(d + q + 10);
    if (iw == 0 || ih == 0 || iw > MP_MAX_IMAGE_W / 2 || ih > MP_MAX_IMAGE_H || rd32(d + q) != 12u + 2u * (unsigned)iw * (unsigned)ih ||
        q + 12 + 2 * (size_t)iw * (size_t)ih > dl) {
        goto bad;
    }
    t->imgX = (int)rd16(d + q + 4);
    t->imgY = (int)rd16(d + q + 6);
    t->w = 2 * iw;
    t->h = ih;
    t->pix = (uint8_t *)malloc((size_t)t->w * (size_t)t->h);
    if (t->pix == NULL) {
        free(own);
        return MP_ERR_MEMORY;
    }
    memcpy(t->pix, d + q + 12, (size_t)t->w * (size_t)t->h); /* two pixels to a halfword, the left one in the low byte */
    free(own);
    return MP_OK;
bad:
    free(own);
    memset(t, 0, sizeof *t);
    return MP_ERR_FORMAT;
}

int mpTileEncode(const MpTile *t, uint8_t **block, size_t *len) {
    size_t lists = 0, off, timLen, dl, padded, i;
    int k, j;
    uint8_t *d, *p, *b;

    if (block != NULL) {
        *block = NULL;
    }
    if (t == NULL || block == NULL || len == NULL || t->pix == NULL) {
        return MP_ERR_ARG;
    }
    if (t->w <= 0 || t->h <= 0 || (t->w & 1) != 0 || t->w > MP_MAX_IMAGE_W || t->h > MP_MAX_IMAGE_H) {
        return MP_ERR_SIZE; /* two pixels to a halfword; the VRAM slot is 128 halfwords x 128 rows */
    }
    for (k = 0; k < MP_LISTS; k++) {
        if (t->count[k] < 0 || t->count[k] > MP_MAX_SPRITES) {
            return MP_ERR_LIMIT; /* StreamTask.sprites[3][5] */
        }
        for (j = 0; j < t->count[k]; j++) {
            const MpSprite *s = &t->sp[k][j];
            if (s->x < -32768 || s->x > 32767 || s->y < -32768 || s->y > 32767 || s->u < 0 || s->v < 0 || s->w <= 0 || s->h <= 0 ||
                s->u + s->w > t->w || s->v + s->h > t->h) {
                return MP_ERR_SIZE; /* the sprite reads outside the image (SPRT u, v are bytes of the image: stream.c:76-77) */
            }
        }
        lists += 4 + 12 * (size_t)t->count[k];
    }
    off = 4 + lists;
    timLen = 8 + (12 + 2 * MP_CLUT) + 12 + (size_t)t->w * (size_t)t->h;
    dl = off + timLen;
    d = (uint8_t *)calloc(dl, 1);
    if (d == NULL) {
        return MP_ERR_MEMORY;
    }
    wr32(d, (uint32_t)off);
    p = d + 4;
    for (k = 0; k < MP_LISTS; k++) {
        wr32(p, (uint32_t)t->count[k]);
        p += 4;
        for (j = 0; j < t->count[k]; j++, p += 12) {
            wr16(p, (unsigned)t->sp[k][j].x & 0xFFFFu);
            wr16(p + 2, (unsigned)t->sp[k][j].y & 0xFFFFu);
            wr16(p + 4, (unsigned)t->sp[k][j].u & 0xFFFFu);
            wr16(p + 6, (unsigned)t->sp[k][j].v & 0xFFFFu);
            wr16(p + 8, (unsigned)t->sp[k][j].w & 0xFFFFu);
            wr16(p + 10, (unsigned)t->sp[k][j].h & 0xFFFFu);
        }
    }
    wr32(p, 0x10);
    wr32(p + 4, 9);
    p += 8;
    wr32(p, 12 + 2 * MP_CLUT);
    wr16(p + 4, (unsigned)t->clutX);
    wr16(p + 6, (unsigned)t->clutY);
    wr16(p + 8, MP_CLUT);
    wr16(p + 10, 1);
    for (i = 0; i < MP_CLUT; i++) {
        wr16(p + 12 + 2 * i, t->clut[i]);
    }
    p += 12 + 2 * MP_CLUT;
    wr32(p, 12 + (uint32_t)t->w * (uint32_t)t->h);
    wr16(p + 4, (unsigned)t->imgX);
    wr16(p + 6, (unsigned)t->imgY);
    wr16(p + 8, (unsigned)t->w / 2);
    wr16(p + 10, (unsigned)t->h);
    memcpy(p + 12, t->pix, (size_t)t->w * (size_t)t->h);

    if (t->rle) {
        uint8_t *packed = NULL;
        size_t pl = 0;
        int r = mpRlenPack(d, dl, &packed, &pl);
        free(d);
        if (r != MP_OK) {
            return r;
        }
        d = packed;
        dl = pl;
    }
    padded = (dl + MP_SECTOR - 1) / MP_SECTOR * MP_SECTOR;
    if (padded > MP_MAX_TILE_BYTES) {
        free(d);
        return MP_ERR_LIMIT; /* tileSizes is a u16 */
    }
    b = (uint8_t *)calloc(padded, 1);
    if (b == NULL) {
        free(d);
        return MP_ERR_MEMORY;
    }
    memcpy(b, d, dl);
    free(d);
    *block = b;
    *len = padded;
    return MP_OK;
}
