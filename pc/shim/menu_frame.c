/* The options menu's window art, pure side (feature controls-config, O5b); see menu_frame.h. */
#include "menu_frame.h"

#include <string.h>

int mfOpenLevel(int frame) {
    int level;

    if (frame <= 0) {
        return 0;
    }
    if (frame >= MF_OPEN_FRAMES) {
        return MF_ONE;
    }
    level = frame * (MF_ONE / MF_OPEN_FRAMES);
    return level > MF_ONE ? MF_ONE : level;
}

int mfCursorFrame(int tick) {
    static const int swing[4] = {1, 2, 3, 2};
    int p;

    if (tick < 0) {
        return 0;
    }
    p = tick % (32 + 6 * 4);
    return p < 32 ? 0 : swing[(p - 32) / 6];
}

int mfArrowOffset(int tick) {
    return mfCursorFrame(tick);
}

int mfTabLayout(const int *textW, int n, int avail, int pad, int gap, int *x, int *w) {
    int i, sum = 0, total, left;

    if (n <= 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        sum += textW[i];
    }
    total = sum + 2 * pad * n + gap * (n - 1);
    while (total > avail && gap > 0) {
        gap--;
        total = sum + 2 * pad * n + gap * (n - 1);
    }
    while (total > avail && pad > 0) {
        pad--;
        total = sum + 2 * pad * n + gap * (n - 1);
    }
    left = total < avail ? (avail - total) / 2 : 0;
    for (i = 0; i < n; i++) {
        w[i] = textW[i] + 2 * pad;
        x[i] = left;
        left += w[i] + gap;
    }
    return total;
}

int mfTabWindow(const int *textW, int n, int current, int avail, int pad, int gap, int arrowW, int *first) {
    int i, total = 0, lo, hi, room, grew;

    for (i = 0; i < n; i++) {
        total += textW[i] + 2 * pad + (i > 0 ? gap : 0);
    }
    if (total <= avail) {
        *first = 0;
        return n;
    }
    room = avail - 2 * arrowW;
    lo = hi = current;
    total = textW[current] + 2 * pad;
    do {
        grew = 0;
        if (hi + 1 < n && total + gap + textW[hi + 1] + 2 * pad <= room) {
            hi++;
            total += gap + textW[hi] + 2 * pad;
            grew = 1;
        }
        if (lo > 0 && total + gap + textW[lo - 1] + 2 * pad <= room) {
            lo--;
            total += gap + textW[lo] + 2 * pad;
            grew = 1;
        }
    } while (grew);
    *first = lo;
    return hi - lo + 1;
}

static void slice(MfSlice *s, int dx, int dy, int dw, int dh, int su, int sv, int sw, int sh) {
    s->dx = dx;
    s->dy = dy;
    s->dw = dw;
    s->dh = dh;
    s->su = su;
    s->sv = sv;
    s->sw = sw;
    s->sh = sh;
}

/* Both margins of one axis for a destination of `len`: kept as they are when they fit, else in proportion */
static void margins(int len, int a, int b, int *outA, int *outB) {
    if (a + b > len && a + b > 0) {
        *outA = len * a / (a + b);
        *outB = len - *outA;
    } else {
        *outA = a;
        *outB = b;
    }
}

int mfSlice9(int w, int h, int sw, int sh, int ml, int mr, int mt, int mb, MfSlice out[9]) {
    int dl, dr, dt, db, n = 0, i, j;
    int sx[4], sy[4], dxs[4], dys[4];

    if (w <= 0 || h <= 0 || sw <= ml + mr || sh <= mt + mb || ml < 0 || mr < 0 || mt < 0 || mb < 0) {
        return 0;
    }
    margins(w, ml, mr, &dl, &dr);
    margins(h, mt, mb, &dt, &db);
    sx[0] = 0;
    sx[1] = ml;
    sx[2] = sw - mr;
    sx[3] = sw;
    sy[0] = 0;
    sy[1] = mt;
    sy[2] = sh - mb;
    sy[3] = sh;
    dxs[0] = 0;
    dxs[1] = dl;
    dxs[2] = w - dr;
    dxs[3] = w;
    dys[0] = 0;
    dys[1] = dt;
    dys[2] = h - db;
    dys[3] = h;
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 3; i++) {
            if (dxs[i + 1] - dxs[i] <= 0 || dys[j + 1] - dys[j] <= 0 || sx[i + 1] - sx[i] <= 0 || sy[j + 1] - sy[j] <= 0) {
                continue;
            }
            slice(&out[n++], dxs[i], dys[j], dxs[i + 1] - dxs[i], dys[j + 1] - dys[j], sx[i], sy[j], sx[i + 1] - sx[i],
                  sy[j + 1] - sy[j]);
        }
    }
    return n;
}

static long rd32(const unsigned char *p) {
    /* unsigned: `long` is 32 bits on Windows and a set top byte would overflow it */
    return (long)((unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) |
                  ((unsigned long)p[3] << 24));
}

static int rd16(const unsigned char *p) {
    return (short)(p[0] | (p[1] << 8));
}

static int inside(long size, long off, long len) {
    return off >= 0 && len >= 0 && off <= size && len <= size - off;
}

int mfSheetPart(const unsigned char *file, long size, int frame, int which, MfPart *out) {
    long base, partsOff, idsOff, frameOff;
    int count, i, n, k, best = -1, bestArea = -1, pick;
    const unsigned char *sheet, *f, *part;

    if (!inside(size, 0, 4)) {
        return 0;
    }
    base = rd32(file);
    if (!inside(size, base, 12)) {
        return 0;
    }
    sheet = file + base;
    partsOff = rd32(sheet);
    idsOff = rd32(sheet + 4);
    if (partsOff < 12 || !inside(size, base + idsOff, 0)) {
        return 0;
    }
    count = (int)((partsOff - 8) / 4); /* one frame offset per id, between the header and the parts */
    for (i = 0; i < count; i++) {
        if (!inside(size, base + idsOff + i, 1) || file[base + idsOff + i] == 0xFF) {
            return 0; /* the id list ended before the frame */
        }
        if (file[base + idsOff + i] == frame) {
            break;
        }
    }
    if (i == count) {
        return 0;
    }
    frameOff = rd32(sheet + 8 + 4 * i);
    if (!inside(size, base + frameOff, 6)) {
        return 0;
    }
    f = sheet + frameOff;
    n = rd16(f);
    if (n <= 0 || !inside(size, base + frameOff + 6, 6L * n)) {
        return 0;
    }
    for (k = 0; k < n; k++) {
        long po;
        int w, h;

        pick = rd16(f + 6 + 6 * k);
        po = partsOff + 14L * pick;
        if (pick < 0 || !inside(size, base + po, 14)) {
            return 0;
        }
        part = sheet + po;
        w = rd16(part + 4);
        h = rd16(part + 6);
        if (which < 0 ? (w * h > bestArea) : (k == which)) {
            bestArea = w * h;
            best = pick;
            if (which >= 0) {
                break;
            }
        }
    }
    if (best < 0) {
        return 0;
    }
    part = sheet + partsOff + 14L * best;
    out->u = rd16(part);
    out->v = rd16(part + 2);
    out->w = rd16(part + 4);
    out->h = rd16(part + 6);
    out->clutX = rd16(part + 8);
    out->clutY = rd16(part + 10);
    return out->w > 0 && out->h > 0;
}

long mfRlenUnpack(const unsigned char *src, long srcSize, unsigned char *dst, long cap) {
    long i = 8, o = 0; /* "RLEN" and the unpacked size come first */
    int c, n, k;

    if (srcSize < 9) {
        return -1;
    }
    while (i < srcSize && (c = src[i]) != 0) {
        if (c & 0x80) {
            n = c & 0x7F;
            if (i + 1 >= srcSize || o + n > cap) {
                return -1;
            }
            for (k = 0; k < n; k++) {
                dst[o++] = src[i + 1];
            }
            i += 2;
        } else {
            n = c;
            if (i + 1 + n > srcSize || o + n > cap) {
                return -1;
            }
            memcpy(dst + o, src + i + 1, (size_t)n);
            o += n;
            i += 1 + n;
        }
    }
    return i < srcSize ? o : -1; /* the terminating 0 must be there */
}

int mfArchiveImage(const unsigned char *file, long size, int index, unsigned char *scratch, long cap, MfImage *out) {
    long base, entry, at, len;
    const unsigned char *tim;
    long timSize;
    long flag, block;
    int w, h;

    if (index < 0 || !inside(size, 0, 4)) {
        return 0;
    }
    base = rd32(file);
    if (!inside(size, base + 4L * index, 4)) {
        return 0;
    }
    entry = rd32(file + base + 4L * index);
    if (entry == 0 || !inside(size, base + entry, 8)) {
        return 0;
    }
    at = base + entry;
    len = size - at;
    tim = file + at;
    timSize = len;
    if (rd32(tim) == 0x4E454C52) { /* "RLEN" */
        timSize = mfRlenUnpack(tim, len, scratch, cap);
        if (timSize < 0) {
            return 0;
        }
        tim = scratch;
    }
    if (timSize < 20 || rd32(tim) != 0x10) {
        return 0;
    }
    flag = rd32(tim + 4);
    block = 8;
    if ((flag & 7) != 2) { /* a 16-bit image: the font pages carry their palettes as pixels */
        return 0;
    }
    if (flag & 8) {
        block += rd32(tim + block);
    }
    if (block < 0 || block + 12 > timSize) {
        return 0;
    }
    w = rd16(tim + block + 8);
    h = rd16(tim + block + 10);
    if (w <= 0 || h <= 0 || block + 12 + 2L * w * h > timSize) {
        return 0;
    }
    out->w = w;
    out->h = h;
    out->px = tim + block + 12;
    return 1;
}

int mfImageCopy(const MfImage *im, int x, int y, int w, int h, unsigned short *dst, int stride) {
    int r, c;

    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > im->w || y + h > im->h || stride < w) {
        return 0;
    }
    for (r = 0; r < h; r++) {
        const unsigned char *row = im->px + 2L * ((long)(y + r) * im->w + x);

        for (c = 0; c < w; c++) {
            dst[r * stride + c] = (unsigned short)(row[2 * c] | (row[2 * c + 1] << 8));
        }
    }
    return 1;
}

int mfLeaseSync(unsigned short *backup, const unsigned short *ours, const unsigned short *cur, int n) {
    int i, changed = 0;

    for (i = 0; i < n; i++) {
        if (cur[i] != ours[i] && cur[i] != backup[i]) {
            backup[i] = cur[i];
            changed++;
        }
    }
    return changed;
}
