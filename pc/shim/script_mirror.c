/*
 * Battle scripts for the other side: see script_mirror.h. The command lengths and fields are read from
 * src/battle/battle_mode/battle_script.c (FIGHTSTG_updateBattleScript and the FIGHTSTG_runScript* functions).
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "script_mirror.h"

#define ARENA_MIDDLE 0x1400 /* a fighter stands this far plus its distance from the middle (Models.face, models.c) */

typedef struct Cursor {
    const unsigned char *in;
    unsigned char *out; /* NULL: only measure */
    size_t words;       /* what `in` holds */
    size_t outWords;
    size_t at;          /* the next word */
    int fromEnemy, toEnemy;
    ScriptPlace from, to;
    char *why;
    size_t whyCap;
} Cursor;

static void fail(Cursor *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void fail(Cursor *c, const char *fmt, ...) {
    va_list ap;

    if (c->why == NULL || c->whyCap == 0) return;
    va_start(ap, fmt);
    vsnprintf(c->why, c->whyCap, fmt, ap);
    va_end(ap);
}

static int wordAt(const unsigned char *b, size_t i) {
    return (short)(b[2 * i] | (b[2 * i + 1] << 8));
}

/* the word `i` of the command at `base`, or 0 past the end (the length check comes first) */
static int in(const Cursor *c, size_t base, size_t i) {
    return base + i < c->words ? wordAt(c->in, base + i) : 0;
}

static int fits(Cursor *c, size_t base, const char *what, int value) {
    if (value < -32768 || value > 32767) {
        fail(c, "%s at word %zu: a position of %d does not fit 16 bits once it is moved", what, base, value);
        return 0;
    }
    return 1;
}

static void put(Cursor *c, size_t at, int v) {
    if (c->out != NULL) {
        c->out[2 * at] = (unsigned char)(v & 0xFF);
        c->out[2 * at + 1] = (unsigned char)((v >> 8) & 0xFF);
    }
}

/* the yaw of a script rotation (the value is negated into the model's rotation, 4096 to a turn) seen in a mirror across z = 0: the
   model that looked at -z looks at +z; 2048 is a half turn and the result is kept in -2047..2048 */
static int mirrorYaw(int v) {
    int r = (-v - 2048) % 4096;

    if (r > 2048) r -= 4096;
    if (r <= -2048) r += 4096;
    return r;
}

/* a position (the game's actual coordinates: y up is negative, the caster at +z for an enemy and -z for a partner): z mirrors, and
   a place on the caster's half moves with where the caster stands, so an effect at its mouth stays at its mouth */
static int anchored(const Cursor *c, int z) {
    return c->fromEnemy ? z > 0 : z < 0;
}

static void mirrorPos(const Cursor *c, int anchor, int y, int z, int *outY, int *outZ) {
    int hs = c->fromEnemy ? 1 : -1;

    if (anchor) {
        *outY = y + c->from.height - c->to.height;
        *outZ = -z + hs * (c->from.distance - c->to.distance);
    } else {
        *outY = y;
        *outZ = -z;
    }
}

static int swapType(int t) {
    if (t >= 1 && t <= 3) return t + 3;
    if (t >= 4 && t <= 6) return t - 3;
    return t;
}

/* The commands. Each reads from c->at (the opcode's word), writes the converted words when it converts, and moves c->at past
   itself. They return the words of the command, or 0 after failing. `convert` is 0 when the sides are the same. */

static size_t needWords(Cursor *c, size_t base, size_t n, const char *what) {
    if (base + n > c->words) {
        fail(c, "%s at word %zu: the script ends inside it (it needs %zu words)", what, base, n);
        return 0;
    }
    return n;
}

static size_t modelCommand(Cursor *c, size_t base, int convert) {
    int cmd = in(c, base, 1), type = in(c, base, 2);
    size_t n;
    char what[48];

    snprintf(what, sizeof what, "model command %d", cmd);
    if (needWords(c, base, 3, what) == 0) return 0;
    switch (cmd) {
    case 1:
    case 2:
    case 9:
        n = 3;
        break;
    case 0:
    case 6:
    case 7:
    case 8:
        n = 4;
        break;
    case 3:
    case 4:
        n = 7;
        break;
    case 5:
        n = 4;
        if (in(c, base, 3) == 4 && needWords(c, base, 4, what) != 0) n = 5;
        break;
    default:
        fail(c, "model command %d at word %zu is not one the game has", cmd, base);
        return 0;
    }
    if (needWords(c, base, n, what) == 0) return 0;
    if (!convert) return n;
    put(c, base + 2, swapType(type));
    if (cmd == 3) {
        int x = in(c, base, 3), y = -in(c, base, 4), z = -in(c, base, 5), ny, nz;

        mirrorPos(c, type == 0, y, z, &ny, &nz);
        if (!fits(c, base, what, -ny) || !fits(c, base, what, -nz)) return 0;
        put(c, base + 3, x);
        put(c, base + 4, -ny);
        put(c, base + 5, -nz);
    } else if (cmd == 4) {
        put(c, base + 4, mirrorYaw(in(c, base, 4)));
        put(c, base + 5, -in(c, base, 5));
    }
    return n;
}

static size_t effectCommand(Cursor *c, size_t base, int convert) {
    int mode = in(c, base, 1);
    size_t n = mode == 1 ? 3 : 6;

    if (needWords(c, base, n, "effect command") == 0) return 0;
    if (convert && mode != 1) {
        int x = in(c, base, 3), y = in(c, base, 4), z = in(c, base, 5), ny, nz;

        if (z != 0x7FFF && z != -1) { /* a screen position stays */
            mirrorPos(c, anchored(c, z), y, z, &ny, &nz);
            if (!fits(c, base, "effect command", ny) || !fits(c, base, "effect command", nz)) return 0;
            put(c, base + 3, x);
            put(c, base + 4, ny);
            put(c, base + 5, nz);
        }
    }
    return n;
}

static size_t effectModelCommand(Cursor *c, size_t base, int convert) {
    int mode = in(c, base, 1);
    size_t n = mode == 1 ? 3 : 9;

    if (needWords(c, base, n, "effect model command") == 0) return 0;
    if (convert && mode != 1) {
        int x = in(c, base, 3), y = -in(c, base, 4), z = -in(c, base, 5), ny, nz;

        mirrorPos(c, anchored(c, z), y, z, &ny, &nz);
        if (!fits(c, base, "effect model command", -ny) || !fits(c, base, "effect model command", -nz)) return 0;
        put(c, base + 3, x);
        put(c, base + 4, -ny);
        put(c, base + 5, -nz);
        put(c, base + 7, mirrorYaw(in(c, base, 7)));
        put(c, base + 8, -in(c, base, 8));
    }
    return n;
}

static size_t cameraCommand(Cursor *c, size_t base, int convert) {
    int mode = in(c, base, 2);
    size_t n = mode == 1 ? 17 : 4;
    int id;

    if (needWords(c, base, n, "camera command") == 0) return 0;
    if (!convert) return n;
    if (mode == 1) {
        fail(c, "camera command at word %zu: a camera with a view of its own (mode 1) cannot be mirrored", base);
        return 0;
    }
    id = in(c, base, 3);
    if (id >= 1 && id <= 3) {
        id += 4;
    } else if (id >= 5 && id <= 7) {
        id -= 4;
    } else if (id == 4) {
        fail(c, "camera 4 at word %zu has no counterpart on the other side (a partner's top view)", base);
        return 0;
    }
    put(c, base + 3, id);
    return n;
}

static size_t stageCommand(Cursor *c, size_t base) {
    int mode = in(c, base, 1), id;
    size_t n = 3;

    if (needWords(c, base, 3, "stage command") == 0) return 0;
    id = in(c, base, 2);
    if (mode != 1 && id != -1) n = 5; /* the fade times: the game leaves them when the stage is none (-1) */
    return needWords(c, base, n, "stage command");
}

/* walks the script: measures it and, with c->out, writes it (the words that do not change are copied first) */
static int walk(Cursor *c, int convert) {
    size_t base = 0;
    size_t n;

    if (c->out != NULL) {
        if (c->outWords < c->words) {
            fail(c, "no room for the converted script");
            return -1;
        }
    }
    while (1) {
        int op;

        if (base >= c->words) {
            fail(c, "the script ends at word %zu without a closing command (0 or 255)", base);
            return -1;
        }
        op = wordAt(c->in, base);
        n = 0;
        switch (op) {
        case 0:
        case 255:
            n = 1;
            break;
        case 1:
            n = needWords(c, base, 2, "hits command");
            break;
        case 2:
            n = modelCommand(c, base, convert);
            break;
        case 3:
            n = effectCommand(c, base, convert);
            break;
        case 4:
            n = stageCommand(c, base);
            break;
        case 5:
            n = cameraCommand(c, base, convert);
            break;
        case 6:
            n = effectModelCommand(c, base, convert);
            break;
        case 7:
            n = needWords(c, base, 3, "fade command");
            break;
        case 8:
        case 9:
            n = 1;
            break;
        case 10:
            n = needWords(c, base, 3, "sound command");
            break;
        case 11:
            n = needWords(c, base, 2, "wait command");
            break;
        default:
            fail(c, "command %d at word %zu is not one the game has", op, base);
            return -1;
        }
        if (n == 0) return -1;
        base += n;
        if (op == 0 || op == 255) break;
    }
    return (int)(base * 2);
}

int scriptLength(const unsigned char *inBytes, size_t avail, char *why, size_t whyCap) {
    Cursor c;

    memset(&c, 0, sizeof c);
    c.in = inBytes;
    c.words = avail / 2;
    c.why = why;
    c.whyCap = whyCap;
    return walk(&c, 0);
}

int scriptConvert(const unsigned char *inBytes, size_t avail, int fromEnemy, ScriptPlace from, int toEnemy, ScriptPlace to, unsigned char *out,
                  size_t outCap, char *why, size_t whyCap) {
    Cursor c;
    int len;

    memset(&c, 0, sizeof c);
    c.in = inBytes;
    c.words = avail / 2;
    c.fromEnemy = fromEnemy != 0;
    c.toEnemy = toEnemy != 0;
    c.from = from;
    c.to = to;
    c.why = why;
    c.whyCap = whyCap;
    len = walk(&c, 0);
    if (len < 0) return -1;
    if (outCap < (size_t)len) {
        fail(&c, "no room for the converted script (%d bytes)", len);
        return -1;
    }
    memcpy(out, inBytes, (size_t)len);
    c.out = out;
    c.outWords = outCap / 2;
    c.words = (size_t)len / 2;
    return walk(&c, c.fromEnemy != c.toEnemy) < 0 ? -1 : len;
}
