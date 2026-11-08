/*
 * The read-only readable view of a save: see save_view.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/game_state.h"
#include "ext_state.h"
#include "port_log.h"
#include "port_version.h"
#include "save_view.h"
#include "text_codec.h"

static SaveViewCtx sCtx;
static int sHave;

void saveViewSetContext(const SaveViewCtx *ctx) {
    sHave = ctx != NULL;
    if (ctx != NULL) sCtx = *ctx;
}

typedef struct Buf {
    char *d;
    size_t n, cap;
    int bad;
} Buf;

static void put(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#include <stdarg.h>

static void put(Buf *b, const char *fmt, ...) {
    va_list ap;
    int n;
    char tmp[512];

    va_start(ap, fmt);
    n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n < 0 || n >= (int)sizeof tmp || b->bad) {
        b->bad = 1;
        return;
    }
    if (b->n + (size_t)n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 4096;
        char *g;
        while (cap < b->n + (size_t)n + 1) cap *= 2;
        g = realloc(b->d, cap);
        if (g == NULL) {
            b->bad = 1;
            return;
        }
        b->d = g;
        b->cap = cap;
    }
    memcpy(b->d + b->n, tmp, (size_t)n + 1);
    b->n += (size_t)n;
}

/* a JSON string of UTF-8 text */
static void str(Buf *b, const char *s) {
    put(b, "\"");
    for (; s != NULL && *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') put(b, "\\%c", c);
        else if (c == '\n') put(b, "\\n");
        else if (c < 0x20) put(b, "\\u%04x", c);
        else put(b, "%c", c);
    }
    put(b, "\"");
}

/* the game's text bytes (up to the NUL) as readable text */
static void gameText(Buf *b, const char *bytes, size_t cap) {
    uint8_t raw[0x40];
    size_t n = 0;
    char *t;

    while (n < cap && n < sizeof raw - 1 && bytes[n] != 0) {
        raw[n] = (uint8_t)bytes[n];
        n++;
    }
    t = tcDecode(raw, n);
    str(b, t != NULL ? t : "");
    free(t);
}

static void item(Buf *b, int id, int count, int equipped, int *first) {
    const char *name = (sHave && sCtx.itemName != NULL) ? sCtx.itemName(id) : NULL;

    put(b, "%s    { \"id\": %d", *first ? "\n" : ",\n", id);
    *first = 0;
    if (name != NULL && name[0] != '\0') {
        put(b, ", \"name\": ");
        str(b, name);
    }
    put(b, ", \"count\": %d", count);
    if (equipped != 0) put(b, ", \"equipped\": %d", equipped);
    put(b, " }");
}

char *saveViewBuild(const unsigned char *game, unsigned long gameLen, int slot, unsigned sequence) {
    static GameState g;
    Buf b;
    int i, first;

    memset(&g, 0, sizeof g);
    memcpy(&g, game, gameLen < GAME_SAVE_SIZE ? gameLen : GAME_SAVE_SIZE);
    memset(&b, 0, sizeof b);
    put(&b, "{\n  \"format\": \"dw3save-view-v1\",\n");
    put(&b, "  \"notes\": \"READ-ONLY view of slot%d.dw3save, rewritten on every save. The game never reads this file: "
            "editing it changes nothing. Item names come from the game's own text and from mods (\\\"<mod>:<name>\\\").\",\n", slot);
    put(&b, "  \"slot\": %d,\n  \"sequence\": %u,\n  \"portVersion\": \"%s\",\n  \"mods\": [", slot, sequence, PORT_VERSION);
    for (i = 0; sHave && i < sCtx.modCount; i++) {
        put(&b, "%s{ \"name\": ", i ? ", " : "");
        str(&b, sCtx.modName != NULL ? sCtx.modName(i) : "");
        put(&b, ", \"version\": ");
        str(&b, sCtx.modVersion != NULL ? sCtx.modVersion(i) : "");
        put(&b, " }");
    }
    put(&b, "],\n  \"player\": ");
    gameText(&b, g.name, sizeof g.name);
    put(&b, ",\n  \"money\": %d,\n", (int)g.money);
    put(&b, "  \"playTime\": { \"hours\": %d, \"minutes\": %d, \"seconds\": %d },\n", (int)g.playTime.hours,
        (int)g.playTime.minutes, (int)g.playTime.seconds);
    put(&b, "  \"progress\": %d,\n  \"place\": %d,\n  \"placeArg\": %d,\n", (int)g.progress, (int)g.place, (int)g.placeArg);
    put(&b, "  \"party\": [%d, %d, %d],\n  \"partners\": [", (int)g.party[0], (int)g.party[1], (int)g.party[2]);
    first = 1;
    for (i = 0; i < PC_PARTNER_COUNT; i++) {
        const Partner *p = &PC_PARTNER_SAVED(g, i); /* the block holds the original eight; the partners a mod adds live in the port's array */
        if (p->unlocked == 0) continue;
        put(&b, "%s\n    { \"index\": %d, ", first ? "" : ",", i);
        if (i >= PARTNER_COUNT) { /* an index above the eight is only this run's registration order: the stable name goes beside it */
            put(&b, "\"id\": ");
            str(&b, pcPartnerName(i) != NULL ? pcPartnerName(i) : "");
            put(&b, ", ");
        }
        put(&b, "\"name\": ");
        first = 0;
        gameText(&b, p->info.name, sizeof p->info.name);
        put(&b, ", \"level\": %d, \"hp\": %d, \"maxHp\": %d }", (int)p->info.stats[STAT_LEVEL], (int)p->info.stats[STAT_HP],
            (int)p->info.stats[STAT_MAX_HP]);
    }
    put(&b, first ? "],\n" : "\n  ],\n");
    put(&b, "  \"modTechs\": [");
    first = 1;
    for (i = 0; i < PC_PARTNER_COUNT; i++) {
        int e, k;
        for (e = 0; e < PARTNER_ENTRY_COUNT; e++) {
            for (k = 0; k < 6; k++) {
                int id = (int)((unsigned)PC_PARTNER_SAVED(g, i).info.entries[e].skills[k] & SKILL_ID);
                const char *name;
                if (id < PC_TECH_FIRST_EXT_ID) continue;
                name = (sHave && sCtx.techName != NULL) ? sCtx.techName(id) : NULL;
                put(&b, "%s\n    { \"partner\": %d, \"entry\": %d, \"slot\": %d, \"id\": %d", first ? "" : ",", i, e, k, id);
                first = 0;
                if (name != NULL && name[0] != '\0') {
                    put(&b, ", \"name\": ");
                    str(&b, name);
                }
                put(&b, " }");
            }
        }
    }
    put(&b, first ? "],\n" : "\n  ],\n");
    put(&b, "  \"modForms\": [");
    first = 1;
    for (i = 0; i < PC_PARTNER_COUNT; i++) {
        int e;
        for (e = 0; e < PARTNER_ENTRY_COUNT; e++) {
            int id = (int)PC_PARTNER_SAVED(g, i).info.entries[e].id;
            const char *name;
            if (id < PC_DIGIMON_FIRST_NEW_ID) continue;
            name = (sHave && sCtx.formName != NULL) ? sCtx.formName(id) : NULL;
            put(&b, "%s\n    { \"partner\": %d, \"entry\": %d, \"id\": %d", first ? "" : ",", i, e, id);
            first = 0;
            if (name != NULL && name[0] != '\0') {
                put(&b, ", \"name\": ");
                str(&b, name);
            }
            put(&b, " }");
        }
    }
    put(&b, first ? "],\n" : "\n  ],\n");
    put(&b, "  \"inventory\": [");
    first = 1;
    for (i = 0; i < PC_ITEM_ORIG_COUNT; i++) {
        if (g.itemsCard[i] != 0 || g.equippedItemsCard[i] != 0) item(&b, i, g.itemsCard[i], g.equippedItemsCard[i], &first);
    }
    for (i = PC_ITEM_ORIG_COUNT; i < pcExtItemCapacity(); i++) {
        int c = *pcItemCountRef(i), e = *pcItemEquippedRef(i);
        if (c != 0 || e != 0) item(&b, i, c, e, &first);
    }
    put(&b, first ? "]\n}\n" : "\n  ]\n}\n");
    if (b.bad) {
        free(b.d);
        return NULL;
    }
    return b.d;
}

int saveViewWriteFile(const char *path, const char *text) {
    char tmp[1300];
    FILE *f;
    size_t n = strlen(text);

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (f == NULL) return 0;
    if (fwrite(text, 1, n, f) != n || fclose(f) != 0) {
        remove(tmp);
        return 0;
    }
#ifdef _WIN32
    remove(path);
#endif
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}
