/*
 * The registry of mod stages: see mod_stages.h.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game_state.h"
#include "ext_state.h"
#include "mod_stages.h"
#include "port_fs.h"
#include "port_log.h"
#include "stage_codes.h"

/* sorted by key; the index is the mode's offset from MS_MODE_FIRST once sealed */
static MsStage *sStages[MS_MODE_COUNT];
static int sCount;
static int sSealed;

static void why(char *out, size_t cap, const char *fmt, const char *a) {
    if (out != NULL && cap > 0) {
        snprintf(out, cap, fmt, a != NULL ? a : "");
    }
}

void msReset(void) {
    int i;

    for (i = 0; i < sCount; i++) {
        free(sStages[i]);
        sStages[i] = NULL;
    }
    sCount = 0;
    sSealed = 0;
}

MsStage *msRegister(const char *key, char *whyOut, size_t whySize) {
    MsStage *s;
    int i, at;

    if (sSealed) {
        why(whyOut, whySize, "the registry is sealed: a stage (%s) cannot be added after the modes were given out", key);
        return NULL;
    }
    if (key == NULL || strlen(key) > MS_KEY_MAX || !scModNameValid(key)) {
        why(whyOut, whySize, "\"%s\" is not a stage name \"<mod>:<name>\" (at most 100 bytes)", key);
        return NULL;
    }
    at = sCount;
    for (i = 0; i < sCount; i++) {
        int c = strcmp(key, sStages[i]->key);

        if (c == 0) {
            why(whyOut, whySize, "the stage \"%s\" is already registered", key);
            return NULL;
        }
        if (c < 0 && at == sCount) {
            at = i;
        }
    }
    if (sCount >= MS_MODE_COUNT) {
        why(whyOut, whySize, "more than 256 stages (the modes 0x300-0x3FF) cannot be added: \"%s\" was refused", key);
        return NULL;
    }
    s = (MsStage *)calloc(1, sizeof *s);
    if (s == NULL) {
        why(whyOut, whySize, "out of memory registering \"%s\"", key);
        return NULL;
    }
    memcpy(s->key, key, strlen(key) + 1);
    for (i = sCount; i > at; i--) {
        sStages[i] = sStages[i - 1];
    }
    sStages[at] = s;
    sCount++;
    return s;
}

int msSeal(void) {
    int i;

    if (!sSealed) {
        sSealed = 1;
        for (i = 0; i < sCount; i++) {
            sStages[i]->mode = MS_MODE_FIRST + i;
        }
    }
    return sCount;
}

int msIsSealed(void) {
    return sSealed;
}

int msCount(void) {
    return sCount;
}

MsStage *msByKey(const char *key) {
    int i;

    if (key == NULL) {
        return NULL;
    }
    for (i = 0; i < sCount; i++) {
        if (strcmp(sStages[i]->key, key) == 0) {
            return sStages[i]->refused ? NULL : sStages[i];
        }
    }
    return NULL;
}

MsStage *msByMode(int mode) {
    if (!sSealed || mode < MS_MODE_FIRST || mode - MS_MODE_FIRST >= sCount || sStages[mode - MS_MODE_FIRST]->refused) {
        return NULL;
    }
    return sStages[mode - MS_MODE_FIRST];
}

MsStage *msAt(int i) {
    return sSealed && i >= 0 && i < sCount ? sStages[i] : NULL; /* a refused one is here with refused set: the modes are the indices */
}

int msRefuse(const char *key) {
    int i;

    for (i = 0; key != NULL && i < sCount; i++) {
        if (strcmp(sStages[i]->key, key) == 0) {
            sStages[i]->refused = 1;
            return 1;
        }
    }
    return 0;
}

int msIsModMode(int mode) {
    return mode >= MS_MODE_FIRST && mode <= MS_MODE_LAST;
}

int msModeOfKey(const char *key) {
    const MsStage *s = msByKey(key);

    return s != NULL && s->mode != 0 ? s->mode : -1;
}

int msNumberOfMode(int mode) {
    return msByMode(mode) != NULL ? MS_NUMBER_BASE + (mode - MS_MODE_FIRST) : -1;
}

int msNumberOfKey(const char *key) {
    int mode = msModeOfKey(key);

    return mode >= 0 ? msNumberOfMode(mode) : -1;
}

int msModeOfNumber(int number) {
    int mode = MS_MODE_FIRST + (number - MS_NUMBER_BASE);

    return number >= MS_NUMBER_BASE && msByMode(mode) != NULL ? mode : -1;
}

const char *msKeyOfNumber(int number) {
    const MsStage *s = msByMode(msModeOfNumber(number));

    return s != NULL ? s->key : NULL;
}

void msFallbackOf(const MsStage *s, int *mode, int *x, int *y, int *dir) {
    if (s != NULL && s->fallbackMode != 0) {
        *mode = s->fallbackMode;
        *x = s->fallbackX;
        *y = s->fallbackY;
        *dir = s->fallbackDir;
    } else {
        *mode = MS_DEFAULT_FALLBACK_MODE;
        *x = MS_DEFAULT_FALLBACK_X;
        *y = MS_DEFAULT_FALLBACK_Y;
        *dir = MS_DEFAULT_FALLBACK_DIR;
    }
}

/* ---- virtual files ---- */

int msSetFiles(MsStage *s, const uint8_t *layers, size_t layersLen, const uint8_t *pack, size_t packLen) {
    int layerFile, packFile;

    if (s == NULL || layers == NULL || layersLen == 0 || pack == NULL || packLen == 0) {
        return -1;
    }
    layerFile = pfsAddVirtualFile(layers, layersLen);
    if (layerFile < 0) {
        return -1;
    }
    packFile = pfsAddVirtualFile(pack, packLen);
    if (packFile < 0) {
        return -1;
    }
    s->layerFile = layerFile;
    s->packFile = packFile;
    return 0;
}

int msLayerRef(const MsStage *s, int entry) {
    return s != NULL && s->layerFile > 0 ? (s->layerFile << 16) | entry : 0;
}

/* ---- the visited flag ---- */

int msVisitedName(const char *key, char *out, size_t cap) {
    const char *colon;
    size_t modLen, need;

    if (key == NULL || out == NULL || !scModNameValid(key)) {
        return 0;
    }
    colon = strchr(key, ':');
    modLen = (size_t)(colon - key);
    need = strlen(key) + 8 + 1; /* "visited." */
    if (need > cap || need - 1 > PC_EXT_VAR_NAME_MAX) {
        return 0;
    }
    memcpy(out, key, modLen);
    memcpy(out + modLen, ":visited.", 9);
    strcpy(out + modLen + 9, colon + 1);
    return 1;
}

int msMarkVisited(int mode) {
    const MsStage *s = msByMode(mode);
    char name[PC_EXT_VAR_NAME_MAX + 8];

    if (s == NULL || !msVisitedName(s->key, name, sizeof name)) {
        return 0;
    }
    pcExtVarSet(name, 1);
    return 1;
}

int msVisited(const char *key) {
    char name[PC_EXT_VAR_NAME_MAX + 8];

    return msVisitedName(key, name, sizeof name) && pcExtVarGet(name) != 0;
}

/* ---- saves ---- */

#define EXTS_VERSION 1
#define EXTS_FIXED (4 + 2 + 12) /* version, name length, x y dir; the name goes between */

static void put32(unsigned char *p, unsigned long v) {
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static unsigned long get32(const unsigned char *p) {
    return (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

/* the block's own words, the way the game stores them */
static int blockGet(const unsigned char *g, size_t off) {
    int v;

    memcpy(&v, g + off, 4);
    return v;
}

static void blockPut(unsigned char *g, size_t off, int v) {
    memcpy(g + off, &v, 4);
}

static void blockSum(unsigned char *g, unsigned long len) {
    unsigned char x = 0;
    unsigned long b;

    for (b = 4; b < len; b++) {
        x ^= g[b];
    }
    g[0] = x; /* GameState.checksum: the XOR of the bytes from 4 (STGMCARD_fillSave) */
}

#define OFF_MODE offsetof(GameState, fieldMode)
#define OFF_X offsetof(GameState, fieldPos)
#define OFF_Y (offsetof(GameState, fieldPos) + 4)
#define OFF_DIR offsetof(GameState, fieldDir)

unsigned long msEncodeSaved(unsigned char *game, unsigned long gameLen, unsigned char *out, unsigned long cap) {
    const MsStage *s;
    unsigned long keyLen, need;
    int mode, x, y, dir;

    if (game == NULL || gameLen < OFF_DIR + 4) {
        return 0;
    }
    s = msByMode(blockGet(game, OFF_MODE));
    if (s == NULL) {
        return 0;
    }
    keyLen = (unsigned long)strlen(s->key);
    need = EXTS_FIXED + keyLen;
    if (out == NULL || cap < need) {
        return need;
    }
    put32(out, EXTS_VERSION);
    out[4] = (unsigned char)keyLen;
    out[5] = (unsigned char)(keyLen >> 8);
    memcpy(out + 6, s->key, keyLen);
    put32(out + 6 + keyLen, (unsigned long)(unsigned int)blockGet(game, OFF_X));
    put32(out + 10 + keyLen, (unsigned long)(unsigned int)blockGet(game, OFF_Y));
    put32(out + 14 + keyLen, (unsigned long)(unsigned int)blockGet(game, OFF_DIR));
    msFallbackOf(s, &mode, &x, &y, &dir);
    blockPut(game, OFF_MODE, mode);
    blockPut(game, OFF_X, x << 8);
    blockPut(game, OFF_Y, y << 8);
    blockPut(game, OFF_DIR, dir);
    blockSum(game, gameLen);
    return need;
}

int msRestoreSaved(unsigned char *game, unsigned long gameLen, const unsigned char *chunk, unsigned long len) {
    unsigned long keyLen;
    char key[MS_KEY_MAX + 1];
    const MsStage *s;

    if (chunk == NULL || len == 0) {
        return 0;
    }
    if (len < EXTS_FIXED || get32(chunk) != EXTS_VERSION) {
        return -1;
    }
    keyLen = (unsigned long)chunk[4] | ((unsigned long)chunk[5] << 8);
    if (keyLen == 0 || keyLen > MS_KEY_MAX || len < EXTS_FIXED + keyLen) {
        return -1;
    }
    if (game == NULL || gameLen < OFF_DIR + 4) {
        return -1;
    }
    memcpy(key, chunk + 6, keyLen);
    key[keyLen] = '\0';
    s = msByKey(key);
    if (s == NULL || s->mode == 0) {
        plogLine("saves", "the save was made in the stage \"%s\", which is not installed: the game continues at the stage it fell back to\n", key);
        return 0;
    }
    blockPut(game, OFF_MODE, s->mode);
    blockPut(game, OFF_X, (int)get32(chunk + 6 + keyLen));
    blockPut(game, OFF_Y, (int)get32(chunk + 10 + keyLen));
    blockPut(game, OFF_DIR, (int)get32(chunk + 14 + keyLen));
    blockSum(game, gameLen);
    return 1;
}
