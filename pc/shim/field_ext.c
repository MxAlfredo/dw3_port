#include "field_ext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "content.h"
#include "mods.h"
#include "port_log.h"
#include "text_data.h"

static const char *const BLOCK_NAMES[12] = {"poison", "paralysis", "confusion", "sleep", "knockOut", "drain",
                                            "block6", "steal", "lowerAttack", "lowerDefense", "lowerSpeed", "runAway"};

static FeRow sRows[FE_MAX_ROWS];
static int sCount;
static int sFirst;

static int hasKey(const char *const *list, int n, const char *k) {
    int i;

    for (i = 0; i < n; i++) {
        if (!strcmp(list[i], k)) return 1;
    }
    return 0;
}

/* a stat of an enemy: a whole number lo..hi; 0 and the reason in why otherwise */
static int takeStat(const ExtJson *v, const char *key, long lo, long hi, int *out, char *why, size_t cap) {
    if (v->t != 0 || !extWhole(v->num, lo, hi)) {
        snprintf(why, cap, "\"%s\" must be a whole number from %ld to %ld", key, lo, hi);
        return 0;
    }
    *out = (int)v->num;
    return 1;
}

static int parseEnemy(const FeHost *host, const ExtJson *o, FeEnemy *out, int slot, char *why, size_t cap) {
    static const char *const keys[] = {"enemy", "level", "hp", "mp", "strength"};
    const ExtJson *en = extJsonGet(o, "enemy");
    int i, got = 0, id = -1;

    if (o->t != 2) {
        snprintf(why, cap, "enemy %d must be an object {\"enemy\", \"level\", \"hp\", \"mp\", \"strength\"}", slot + 1);
        return 0;
    }
    for (i = 0; i < o->n; i++) {
        if (!hasKey(keys, 5, o->keys[i])) {
            snprintf(why, cap, "enemy %d: unknown field \"%s\"", slot + 1, o->keys[i]);
            return 0;
        }
    }
    if (en == NULL || extJsonGet(o, "level") == NULL || extJsonGet(o, "hp") == NULL || extJsonGet(o, "mp") == NULL || extJsonGet(o, "strength") == NULL) {
        snprintf(why, cap, "enemy %d needs \"enemy\", \"level\", \"hp\", \"mp\" and \"strength\" (a new battle has no slot to take them from)", slot + 1);
        return 0;
    }
    if (en->t == 0 && extWhole(en->num, 0, 32767)) {
        id = host->enemyIdOk && host->enemyIdOk((int)en->num) ? (int)en->num : -1;
    } else if (en->t == 1 && en->str != NULL && host->enemyByName != NULL) {
        id = host->enemyByName(en->str);
        if (id >= 0 && host->enemyIdOk != NULL && !host->enemyIdOk(id)) id = -1;
    }
    if (id < 0 || (id == 0 && slot == 0)) {
        if (id == 0) {
            snprintf(why, cap, "the first enemy cannot be empty (the battle draws its model and HUD from it)");
        } else {
            snprintf(why, cap, "enemy %d: \"enemy\" is not an enemy (an original enemy as a number or \"dw3:digimon/<id>\", or a new enemy \"<mod>:<key>\"; a partner form is no enemy)", slot + 1);
        }
        return 0;
    }
    out->fighter = id;
    if (!takeStat(extJsonGet(o, "level"), "level", 1, 99, &out->level, why, cap) ||
        !takeStat(extJsonGet(o, "hp"), "hp", 1, 32767, &out->hp, why, cap) ||
        !takeStat(extJsonGet(o, "mp"), "mp", 0, 32767, &out->mp, why, cap) ||
        !takeStat(extJsonGet(o, "strength"), "strength", 1, 32767, &out->strength, why, cap)) {
        got = 1;
    }
    return !got;
}

static int parseRewards(const ExtJson *o, FeRow *r, char *why, size_t cap) {
    static const char *const keys[] = {"digimonExp", "exp", "money"};
    int i;

    if (o->t != 2) {
        snprintf(why, cap, "\"rewards\" must be an object {\"digimonExp\", \"exp\", \"money\"}");
        return 0;
    }
    for (i = 0; i < o->n; i++) {
        const char *k = o->keys[i];

        if (!hasKey(keys, 3, k)) {
            snprintf(why, cap, "\"rewards\": unknown field \"%s\" (\"digimonExp\", \"exp\", \"money\")", k);
            return 0;
        }
        if (!strcmp(k, "digimonExp") && !takeStat(&o->v[i], k, 0, 9999999, &r->digimonExp, why, cap)) return 0;
        if (!strcmp(k, "exp") && !takeStat(&o->v[i], k, 0, 999999, &r->exp, why, cap)) return 0;
        if (!strcmp(k, "money") && !takeStat(&o->v[i], k, 0, 9999999, &r->money, why, cap)) return 0;
    }
    return 1;
}

/* a whole row; 0 and the reason in why when it is not one the game could fight */
static int parseRow(const FeHost *host, const ExtJson *o, FeRow *r, char *why, size_t cap) {
    const ExtJson *en;
    int i, k;

    if (o->t != 2) {
        snprintf(why, cap, "not an object of fields");
        return 0;
    }
    for (i = 0; i < o->n; i++) {
        const char *key = o->keys[i];

        if (strcmp(key, "enemies") && strcmp(key, "ambushChance") && strcmp(key, "kind") && strcmp(key, "rewards") && !hasKey(BLOCK_NAMES, 12, key)) {
            snprintf(why, cap, "unknown field \"%s\"", key);
            return 0;
        }
    }
    en = extJsonGet(o, "enemies");
    if (en == NULL) {
        snprintf(why, cap, "needs \"enemies\" (1 to 3 enemies)");
        return 0;
    }
    if (en->t != 4 || en->n < 1 || en->n > 3) {
        snprintf(why, cap, "\"enemies\" is a list of 1 to 3 enemies");
        return 0;
    }
    for (i = 0; i < en->n; i++) {
        if (!parseEnemy(host, &en->v[i], &r->enemy[i], i, why, cap)) return 0;
    }
    r->kind = 1;
    for (i = 0; i < o->n; i++) {
        const char *key = o->keys[i];

        if (!strcmp(key, "ambushChance") && !takeStat(&o->v[i], key, 0, 255, &r->ambushChance, why, cap)) return 0;
        if (!strcmp(key, "kind") && !takeStat(&o->v[i], key, 0, 255, &r->kind, why, cap)) return 0;
        if (!strcmp(key, "rewards") && !parseRewards(&o->v[i], r, why, cap)) return 0;
        for (k = 0; k < 12; k++) {
            int on = 0;

            if (!strcmp(key, BLOCK_NAMES[k])) {
                if (!takeStat(&o->v[i], key, 0, 1, &on, why, cap)) return 0;
                r->blocks[k] = (unsigned char)on;
            }
        }
    }
    return 1;
}

/* "<mod>:<key>" of the mod `m` itself */
static int nameOk(const char *name, int m) {
    const char *mod = modsNameOf(m);
    size_t n = strlen(mod);

    return strlen(name) < sizeof sRows[0].name && !strncmp(name, mod, n) && name[n] == ':' && extValidKey(name + n + 1);
}

static void readMod(const FeHost *host, int m) {
    char path[1200];
    uint8_t *data;
    size_t len;
    ExtJson root;
    const ExtJson *fmt, *rows;
    int i, j;

    if (!assetModFile(m, "tables", "field_encounters.json", path, sizeof path) || tdReadFile(path, &data, &len) != 0) return; /* the mod does not patch this table */
    /* a file that is not JSON, or has another format, is refused whole by the table engine, which says so */
    if (extJsonParse(data, len, &root) == 0) {
        fmt = extJsonGet(&root, "format");
        rows = extJsonGet(&root, "rows");
        if (fmt != NULL && fmt->t == 1 && !strcmp(fmt->str, "field_encounters-v1") && rows != NULL && rows->t == 2) {
            for (i = 0; i < rows->n; i++) {
                const char *key = rows->keys[i];
                char why[300];
                FeRow row;

                if (strchr(key, ':') == NULL) continue; /* a number or a "*": the rows of the table itself */
                memset(&row, 0, sizeof row);
                for (j = 0; j < sCount; j++) {
                    if (!strcmp(sRows[j].name, key)) break;
                }
                if (!nameOk(key, m)) {
                    snprintf(why, sizeof why, "the name must be the mod's own folder name (\"%s\"), a colon and a key of a-z, 0-9, - and _", modsNameOf(m));
                } else if (j < sCount) {
                    snprintf(why, sizeof why, "the name is listed twice");
                } else if (sCount >= FE_MAX_ROWS) {
                    snprintf(why, sizeof why, "the mods may add %d new encounters in all", FE_MAX_ROWS);
                } else if (parseRow(host, &rows->v[i], &row, why, sizeof why)) {
                    snprintf(row.name, sizeof row.name, "%s", key);
                    row.mod = m;
                    sRows[sCount++] = row;
                    continue;
                }
                plogLine("tables", "%s row \"%s\" rejected: %s\n", path, key, why);
            }
        }
    }
    extJsonFree(&root);
    free(data);
}

void fieldExtReset(void) {
    sCount = 0;
    sFirst = 0;
}

int fieldExtLoad(const FeHost *host) {
    int m;

    fieldExtReset();
    if (host == NULL) return 0;
    sFirst = host->firstRow;
    for (m = 0; m < modsCount(); m++) readMod(host, m);
    if (sCount > 0) {
        PLOG("tables", PLOG_MODS, "field_encounters: %d new encounter(s), numbers %d to %d\n", sCount, sFirst, sFirst + sCount - 1);
    }
    return sCount;
}

int fieldExtCount(void) {
    return sCount;
}

int fieldExtFirstRow(void) {
    return sFirst;
}

const FeRow *fieldExtRow(int i) {
    return i >= 0 && i < sCount ? &sRows[i] : NULL;
}

int fieldExtNumberOf(const char *name) {
    int i;

    for (i = 0; name != NULL && i < sCount; i++) {
        if (!strcmp(sRows[i].name, name)) return sFirst + i;
    }
    return -1;
}

const char *fieldExtNameOf(int number) {
    return number >= sFirst && number - sFirst < sCount ? sRows[number - sFirst].name : NULL;
}

const char *fieldExtBlockName(int i) {
    return i >= 0 && i < 12 ? BLOCK_NAMES[i] : NULL;
}
