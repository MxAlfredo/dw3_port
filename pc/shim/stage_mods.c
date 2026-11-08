/*
 * The stage patch layer and the mod flag codes: see stage_mods.h and docs/stages.md section 7.
 */
#include "stage_mods.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ext_state.h"
#include "mod_stages.h"
#include "mods.h"
#include "port_log.h"
#include "stage_new.h"
#include "text_codec.h"
#include "text_data.h"

/* ================= the table of mod ops (the codes the interpreter reads) ================= */

typedef struct ModOp {
    int kind; /* SC_MOD_* */
    char *name;
    int cmp, n;
} ModOp;

static ModOp sOps[SC_MOD_OPS_MAX];
static int sOpCount;

static unsigned opCodeOf(int idx) {
    return (unsigned)(((SC_MOD_GROUP_FIRST + 2 * (idx >> 9)) << 8) | (idx & 0x1FF));
}

/* the table index of a code, or -1 when it is not one of the reserved groups or the table never made it */
static int opIndexOf(unsigned code) {
    int group = (int)((code >> 8) & 0xFE), idx;

    if (group < SC_MOD_GROUP_FIRST || group > SC_MOD_GROUP_LAST) return -1;
    idx = ((group - SC_MOD_GROUP_FIRST) >> 1) * 512 + (int)(code & 0x1FF);
    return idx < sOpCount ? idx : -1;
}

int smOpCode(int kind, const char *name, int cmp, int n) {
    int i;
    char *copy;

    if (kind < SC_MOD_FLAG || kind > SC_MOD_ASK || !scModNameValid(name)) return -1;
    if (kind != SC_MOD_VAR) cmp = n = 0; /* the other kinds have neither: the same op is the same code */
    for (i = 0; i < sOpCount; i++) {
        if (sOps[i].kind == kind && sOps[i].cmp == cmp && sOps[i].n == n && !strcmp(sOps[i].name, name)) return (int)opCodeOf(i);
    }
    if (sOpCount >= SC_MOD_OPS_MAX) return -1;
    copy = (char *)malloc(strlen(name) + 1);
    if (!copy) return -1;
    strcpy(copy, name);
    sOps[sOpCount].kind = kind;
    sOps[sOpCount].name = copy;
    sOps[sOpCount].cmp = cmp;
    sOps[sOpCount].n = n;
    return (int)opCodeOf(sOpCount++);
}

int smOpInfo(unsigned code, int *kind, char *name, size_t cap, int *cmp, int *n) {
    int i = opIndexOf(code);

    if (i < 0) return 0;
    if (kind) *kind = sOps[i].kind;
    if (name && cap) snprintf(name, cap, "%s", sOps[i].name);
    if (cmp) *cmp = sOps[i].cmp;
    if (n) *n = sOps[i].n;
    return 1;
}

/* ---- the table of port actions ---- */

typedef struct ActOp {
    int kind; /* SC_ACT_* */
    int a[SC_ACT_ARGS];
} ActOp;

static ActOp sActs[SC_ACT_OPS_MAX];
static int sActCount;
static void (*sActRunner)(int kind, const int *args);

void smSetActRunner(void (*run)(int kind, const int *args)) {
    sActRunner = run;
}

static SmRequest sLeave;
static int sLeavePending;

void smPostRequest(const SmRequest *l) {
    sLeave = *l;
    sLeavePending = 1;
}

int smTakeRequest(SmRequest *out) {
    if (!sLeavePending) return 0;
    *out = sLeave;
    sLeavePending = 0;
    return 1;
}

/* ---- the ids of the events a stage patch adds (S8) ----
   A name "<mod>:<name>" gets an id on first use (patches and the run-event action may name an event before the file that defines it is read), up
   from SM_EVENT_BASE: far above the game's own (under 10 000) and under 0x10000, what a slot's arg and the script's words hold. */
static char *sEventNames[SM_EVENTS_MAX];
static int sEventCount;

int smEventId(const char *name) {
    int i;
    char *copy;

    if (!name || !scModNameValid(name)) return -1;
    for (i = 0; i < sEventCount; i++) {
        if (!strcmp(sEventNames[i], name)) return SM_EVENT_BASE + i;
    }
    if (sEventCount >= SM_EVENTS_MAX) return -1;
    copy = (char *)malloc(strlen(name) + 1);
    if (!copy) return -1;
    strcpy(copy, name);
    sEventNames[sEventCount] = copy;
    return SM_EVENT_BASE + sEventCount++;
}

const char *smEventName(int id) {
    return (id >= SM_EVENT_BASE && id - SM_EVENT_BASE < sEventCount) ? sEventNames[id - SM_EVENT_BASE] : NULL;
}

static unsigned actCodeOf(int idx) {
    return (unsigned)(((SC_ACT_GROUP_FIRST + 2 * (idx >> 9)) << 8) | (idx & 0x1FF));
}

static int actIndexOf(unsigned code) {
    int group = (int)((code >> 8) & 0xFE), idx;

    if (group < SC_ACT_GROUP_FIRST || group > SC_ACT_GROUP_LAST) return -1;
    idx = ((group - SC_ACT_GROUP_FIRST) >> 1) * 512 + (int)(code & 0x1FF);
    return idx < sActCount ? idx : -1;
}

int smActCode(int kind, const int *args) {
    int i;

    if (kind < 1 || kind >= SC_ACT_KINDS) return -1;
    for (i = 0; i < sActCount; i++) {
        if (sActs[i].kind == kind && !memcmp(sActs[i].a, args, sizeof sActs[i].a)) return (int)actCodeOf(i);
    }
    if (sActCount >= SC_ACT_OPS_MAX) return -1;
    sActs[sActCount].kind = kind;
    memcpy(sActs[sActCount].a, args, sizeof sActs[sActCount].a);
    return (int)actCodeOf(sActCount++);
}

int smActInfo(unsigned code, int *kind, int *args) {
    int i = actIndexOf(code);

    if (i < 0) return 0;
    if (kind) *kind = sActs[i].kind;
    if (args) memcpy(args, sActs[i].a, sizeof sActs[i].a);
    return 1;
}

static int envModOp(void *ud, int kind, const char *name, int cmp, int n) {
    (void)ud;
    return smOpCode(kind, name, cmp, n);
}

static int envModOpInfo(void *ud, unsigned code, int *kind, char *name, size_t cap, int *cmp, int *n) {
    (void)ud;
    return smOpInfo(code, kind, name, cap, cmp, n);
}

static int envActOp(void *ud, int kind, const int *args) {
    (void)ud;
    return smActCode(kind, args);
}

static int envActOpInfo(void *ud, unsigned code, int *kind, int *args) {
    (void)ud;
    return smActInfo(code, kind, args);
}

static int envEventId(void *ud, const char *name) {
    (void)ud;
    return smEventId(name);
}

static const char *envEventName(void *ud, int id) {
    (void)ud;
    return smEventName(id);
}

static SmHost sHost;

static int envStageExists(void *ud, int stage) {
    (void)ud;
    return sHost.stageExists ? sHost.stageExists(stage) : 1;
}

static int envStageByName(void *ud, const char *name) {
    (void)ud;
    return sHost.stageByName ? sHost.stageByName(name) : -1;
}

static const char *envStageName(void *ud, int number) {
    (void)ud;
    return sHost.stageName ? sHost.stageName(number) : NULL;
}

static int envEncounterByName(void *ud, const char *name) {
    (void)ud;
    return sHost.encounterByName ? sHost.encounterByName(name) : -1;
}

static const char *envEncounterName(void *ud, int number) {
    (void)ud;
    return sHost.encounterName ? sHost.encounterName(number) : NULL;
}

void smCodeEnv(StageCodeEnv *out, const StageCodeEnv *base) {
    if (base) *out = *base;
    else memset(out, 0, sizeof *out);
    out->modOp = envModOp;
    out->modOpInfo = envModOpInfo;
    out->actOp = envActOp;
    out->actOpInfo = envActOpInfo;
    out->eventId = envEventId;
    out->eventName = envEventName;
    if (!out->stageExists) out->stageExists = envStageExists;
    out->stageByName = envStageByName;
    out->stageName = envStageName;
    out->encounterByName = envEncounterByName;
    out->encounterName = envEncounterName;
}

static int compare(int cmp, int a, int b) {
    switch (cmp) {
    case SC_CMP_EQ: return a == b;
    case SC_CMP_NE: return a != b;
    case SC_CMP_GE: return a >= b;
    case SC_CMP_GT: return a > b;
    case SC_CMP_LE: return a <= b;
    case SC_CMP_LT: return a < b;
    }
    return 0;
}

/* The `ask` action: the talk that holds it posts the question when the line has been said (pcModApply), the field's talk code takes it (pcAskTake), shows the
   game's Yes/No panel and gives the answer back (pcAskAnswered), and the talk is chosen again. */
static char sAskName[160];
static int sAskPending, sAskDone;

int pcAskTake(void) {
    int had = sAskPending;

    sAskPending = 0;
    sAskDone = 0;
    return had;
}

void pcAskAnswered(int answer) {
    if (sAskName[0]) {
        pcExtVarSet(sAskName, answer);
        PLOG("stages", PLOG_TRACE, "ask %s answered %s\n", sAskName, answer == 1 ? "yes" : "no");
    }
    sAskDone = 1;
}

int pcAskDone(void) {
    return sAskDone;
}

/* 1 when the action list (pairs up to 0xFFFF) holds an `ask` */
int pcActionsAsk(const unsigned short *list) {
    int i;

    for (; list && *list != 0xFFFF; list += 2) {
        if ((((unsigned)*list >> 8) & 0xFE) > SC_MOD_GROUP_LAST) continue;
        i = opIndexOf(*list);
        if (i >= 0 && sOps[i].kind == SC_MOD_ASK) return 1;
    }
    return 0;
}

int pcModCheck(unsigned short code, unsigned short value) {
    int i, have;

    if (((code >> 8) & 0xFE) >= SC_ACT_GROUP_FIRST) return 1; /* a port action holds as a condition, whatever the table made */
    i = opIndexOf(code);
    if (i < 0) return 0; /* a code the table never made (none exists in the game's own data): it does not hold */
    have = pcExtVarGet(sOps[i].name);
    switch (sOps[i].kind) {
    case SC_MOD_FLAG: return (have != 0) == (value != 0);
    case SC_MOD_VAR: return compare(sOps[i].cmp, have, sOps[i].n) == (value != 0);
    }
    return 1; /* set-var is an action; as a condition it always holds */
}

void pcModApply(int code, int value) {
    int i;

    if ((((unsigned)code >> 8) & 0xFE) >= SC_ACT_GROUP_FIRST) {
        i = actIndexOf((unsigned)code & 0xFFFF);
        if (i >= 0 && sActRunner) sActRunner(sActs[i].kind, sActs[i].a);
        return;
    }
    i = opIndexOf((unsigned)code & 0xFFFF);
    if (i < 0) return;
    if (sOps[i].kind == SC_MOD_FLAG) {
        pcExtVarSet(sOps[i].name, value != 0 ? 1 : 0);
        PLOG("stages", PLOG_TRACE, "action mod-flag %s = %d\n", sOps[i].name, value != 0 ? 1 : 0);
    } else if (sOps[i].kind == SC_MOD_SET) {
        pcExtVarSet(sOps[i].name, value);
        PLOG("stages", PLOG_TRACE, "action set-var %s = %d\n", sOps[i].name, value);
    } else if (sOps[i].kind == SC_MOD_ASK) {
        PLOG("stages", PLOG_TRACE, "action ask %s\n", sOps[i].name);
        snprintf(sAskName, sizeof sAskName, "%s", sOps[i].name);
        sAskPending = 1;
    }
}

/* ================= named texts (S6) ================= */

static char *smDup(const char *s);
static int hasUnknownKey(const ExtJson *o, const char *const *known, int nknown, char *err, size_t cap, const char *where);

typedef struct SmText {
    char *name;               /* "<mod>:<name>" */
    char *text[EXT_NLANG];    /* per language folder (EXT_LANGS), NULL when that language has none */
} SmText;

typedef struct SmFileTexts {
    int *ids; /* registry ids in the order their entries were appended to the file */
    int n;
} SmFileTexts;

static SmText *sTexts;
static int sTextCount;
static SmFileTexts sFileTexts[TD_FILES];

static void textsFree(void) {
    int i, k, lang;

    for (i = 0; i < sTextCount; i++) {
        free(sTexts[i].name);
        for (lang = 0; lang < EXT_NLANG; lang++) free(sTexts[i].text[lang]);
    }
    free(sTexts);
    sTexts = NULL;
    sTextCount = 0;
    for (k = 0; k < TD_FILES; k++) {
        free(sFileTexts[k].ids);
        sFileTexts[k].ids = NULL;
        sFileTexts[k].n = 0;
    }
}

static int textFind(const char *name) {
    int i;

    for (i = 0; i < sTextCount; i++) {
        if (!strcmp(sTexts[i].name, name)) return i;
    }
    return -1;
}

/* the text codec accepts it (the readable syntax of the text files) */
static int textUsable(const char *text, char *why, size_t cap) {
    char base[16];
    const char *rest = text;
    int entry, malformed;
    char *raw;
    uint8_t *enc = NULL;
    size_t el = 0;
    int bad;

    /* "{speaker-of:<FILE>:<entry>}" in front: the game's speaker label (tcSpeakerRef), made at run time; the rest is the text */
    if (tcSpeakerRef(text, base, sizeof base, &entry, &rest, &malformed)) {
        if (tdFileFromBase(base) < 0) {
            snprintf(why, cap, "{speaker-of:%s:%d}: %s is not one of the game's text files (TALK04, ...)", base, entry, base);
            return 0;
        }
        if (!rest[0]) {
            snprintf(why, cap, "{speaker-of:...} needs the line after it");
            return 0;
        }
    } else if (malformed) {
        snprintf(why, cap, "{speaker-of:<FILE>:<entry>} is not well formed (for example {speaker-of:TALK04:19})");
        return 0;
    }
    raw = tcUglify(rest);
    bad = !raw || tcEncode(raw, &enc, &el, why, cap) != 0;

    free(raw);
    free(enc);
    return !bad;
}

/* The registry entry of `name`, made when new; -1 without memory */
static int textAdd(const char *name) {
    SmText *grown;
    int id = textFind(name);

    if (id >= 0) return id;
    grown = (SmText *)realloc(sTexts, (size_t)(sTextCount + 1) * sizeof *sTexts);
    if (!grown) return -1;
    sTexts = grown;
    memset(&sTexts[sTextCount], 0, sizeof sTexts[sTextCount]);
    sTexts[sTextCount].name = smDup(name);
    if (!sTexts[sTextCount].name) return -1;
    return sTextCount++;
}

/* mods/<mod>/text/<lang>/stages.json, one language of one mod; a file with a bad line is refused whole */
static void textsLoadFile(int m, int lang) {
    char path[1500], rel[48], full[200], why[200] = "";
    uint8_t *data = NULL;
    size_t len = 0;
    ExtJson root;
    const ExtJson *j, *t;
    static const char *const top[] = {"$schema", "format", "about", "texts"};
    int i;

    snprintf(rel, sizeof rel, "%s/stages.json", EXT_LANGS[lang]);
    if (!assetModFile(m, "text", rel, path, sizeof path) || tdReadFile(path, &data, &len)) return; /* the mod has no text in this language */
    if (extJsonParse(data, len, &root)) {
        plogLine("stages", "%s rejected: not a JSON object\n", path);
        free(data);
        return;
    }
    free(data);
    j = extJsonGet(&root, "format");
    if (!j || j->t != 1 || !j->str || strcmp(j->str, "stage-text-v1")) {
        plogLine("stages", "%s rejected: \"format\" must be \"stage-text-v1\"\n", path);
        goto done;
    }
    if (hasUnknownKey(&root, top, 4, why, sizeof why, "the file")) {
        plogLine("stages", "%s rejected: %s\n", path, why);
        goto done;
    }
    t = extJsonGet(&root, "texts");
    if (!t || t->t != 2) {
        plogLine("stages", "%s rejected: \"texts\" must be an object of \"<name>\": \"the line\"\n", path);
        goto done;
    }
    for (i = 0; i < t->n; i++) {
        snprintf(full, sizeof full, "%s:%s", modsNameOf(m), t->keys[i]);
        if (!scModNameValid(full)) {
            plogLine("stages", "%s rejected: \"%s\" is not a valid name (a-z, 0-9, _ - /)\n", path, t->keys[i]);
            goto done;
        }
        if (t->v[i].t != 1 || !t->v[i].str || !t->v[i].str[0]) {
            plogLine("stages", "%s rejected: \"%s\" must be a line of text that is not empty\n", path, t->keys[i]);
            goto done;
        }
        if (!textUsable(t->v[i].str, why, sizeof why)) {
            plogLine("stages", "%s rejected: \"%s\": %s\n", path, t->keys[i], why[0] ? why : "the text codec cannot encode it");
            goto done;
        }
    }
    for (i = 0; i < t->n; i++) {
        int id;

        snprintf(full, sizeof full, "%s:%s", modsNameOf(m), t->keys[i]);
        id = textAdd(full);
        if (id < 0) goto done;
        free(sTexts[id].text[lang]);
        sTexts[id].text[lang] = smDup(t->v[i].str);
    }
done:
    extJsonFree(&root);
}

/* every mod's texts, then one line for each language a mod gives for some names and not for others */
static void textsLoad(void) {
    int m, lang, i;

    for (m = 0; m < modsCount(); m++) {
        char prefix[200];
        int provided[EXT_NLANG] = {0};
        size_t pl;

        for (lang = 0; lang < EXT_NLANG; lang++) {
            int before = 0, after = 0;

            for (i = 0; i < sTextCount; i++) before += sTexts[i].text[lang] != NULL;
            textsLoadFile(m, lang);
            for (i = 0; i < sTextCount; i++) after += sTexts[i].text[lang] != NULL;
            provided[lang] = after != before;
        }
        snprintf(prefix, sizeof prefix, "%s:", modsNameOf(m));
        pl = strlen(prefix);
        for (i = 0; i < sTextCount; i++) {
            if (strncmp(sTexts[i].name, prefix, pl)) continue;
            if (!sTexts[i].text[0]) {
                plogLine("stages", "%s/text: \"%s\" has no English text (text/eng/stages.json): a patch that names it is rejected\n", modsDirOf(m), sTexts[i].name);
            }
            for (lang = 1; lang < EXT_NLANG; lang++) {
                if (provided[lang] && !sTexts[i].text[lang]) {
                    plogLine("stages", "%s/text/%s/stages.json: \"%s\" is missing here and skipped: the %s line is shown instead\n", modsDirOf(m), EXT_LANGS[lang], sTexts[i].name,
                             lang == 5 && sTexts[i].text[4] ? "Spanish" : "English");
                }
            }
        }
    }
}

int smTextNames(void) {
    return sTextCount;
}

int smTextCount(int k) {
    return k >= 0 && k < TD_FILES ? sFileTexts[k].n : 0;
}

const char *smTextOf(int k, int i, const char *langDir) {
    const char *t;

    if (k < 0 || k >= TD_FILES || i < 0 || i >= sFileTexts[k].n) return "";
    t = extPickText(sTexts[sFileTexts[k].ids[i]].text, langDir);
    return t ? t : "";
}

/* the slot (appended entry) of registry text `id` in file k; *added is set when it is new; -1 without memory */
static int fileSlotOf(int k, int id, int *added) {
    SmFileTexts *f = &sFileTexts[k];
    int i, *grown;

    for (i = 0; i < f->n; i++) {
        if (f->ids[i] == id) return i;
    }
    grown = (int *)realloc(f->ids, (size_t)(f->n + 1) * sizeof *grown);
    if (!grown) return -1;
    f->ids = grown;
    f->ids[f->n] = id;
    *added = 1;
    return f->n++;
}

/* ================= the sprites a mod brings (S9) ================= */

typedef struct SprReg {
    char *name; /* "<mod>:<name>" */
    SmSprite info;
} SprReg;

static SprReg *sSprites;
static int sSpriteCount;

static void spritesFree(void);
static char *smDup(const char *s);

static int cmpName(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The follower sprites of the partners the mods bring (readable-assets A4a): every partners/<key>/field/ of the active mods is a sprite named
   ASSET_PARTNER_FIELD_PREFIX + <key>, built whole from the folder the asset layers resolve for sprite.json (the last layer that has one wins). */
static void partnerFieldSprites(void) {
    int m;

    for (m = 0; m < modsCount(); m++) {
        char dirPath[1400];
        DIR *d;
        struct dirent *e;

        if (!assetModDir(m, NULL, "partners", dirPath, sizeof dirPath)) continue;
        d = opendir(dirPath);
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            char rel[200], json[1500], label[400], why[240], *slash;
            SmSprite info;
            SprReg *grown;
            int k;

            if (e->d_name[0] == '.' || !extValidKey(e->d_name)) continue;
            snprintf(label, sizeof label, "%s%s", ASSET_PARTNER_FIELD_PREFIX, e->d_name);
            for (k = 0; k < sSpriteCount && strcmp(sSprites[k].name, label) != 0; k++) {
            }
            if (k < sSpriteCount) continue; /* an earlier mod already brought this key */
            snprintf(rel, sizeof rel, "%s/field/sprite.json", e->d_name);
            if (!assetResolve("partners", rel, json, sizeof json, NULL)) continue;
            slash = strrchr(json, '/');
            if (slash == NULL) continue;
            *slash = '\0'; /* the folder that holds sprite.json */
            if (sSpriteCount >= SM_SPRITES_MAX) {
                plogLine("stages", "%s: partner sprite \"%s\" skipped: the port holds %d mod sprites\n", modsNameOf(m), label, SM_SPRITES_MAX);
                continue;
            }
            why[0] = '\0';
            if (sHost.spriteAdd(json, label, &info, why, sizeof why)) {
                plogLine("stages", "%s rejected: %s\n", json, why[0] ? why : "the sprite could not be built");
                continue;
            }
            grown = (SprReg *)realloc(sSprites, (size_t)(sSpriteCount + 1) * sizeof *sSprites);
            if (!grown) continue;
            sSprites = grown;
            sSprites[sSpriteCount].name = smDup(label);
            sSprites[sSpriteCount].info = info;
            if (!sSprites[sSpriteCount].name) continue;
            plogLine("stages", "sprite %s is character %d (frames up to %dx%d)\n", label, SM_SPRITE_CHAR_BASE + sSpriteCount, info.w, info.h);
            sSpriteCount++;
        }
        closedir(d);
    }
}

/* the folders of mods/<mod>/sprites, each built by the host and given the next character id (a mod's folders in name order, so the ids do not depend on
   the file system) */
static void spritesLoad(void) {
    int m;

    spritesFree();
    if (!sHost.spriteAdd) return;
    for (m = 0; m < modsCount(); m++) {
        char dirPath[1400];
        DIR *d;
        struct dirent *e;
        char **names = NULL;
        int nn = 0, i;

        if (!assetModDir(m, NULL, "sprites", dirPath, sizeof dirPath)) continue;
        d = opendir(dirPath);
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            char sub[1700];
            char **grown;
            struct stat st;

            if (e->d_name[0] == '.') continue;
            snprintf(sub, sizeof sub, "%s/%s", dirPath, e->d_name);
            if (stat(sub, &st) || !S_ISDIR(st.st_mode)) continue;
            grown = (char **)realloc(names, (size_t)(nn + 1) * sizeof *names);
            if (!grown) break;
            names = grown;
            names[nn] = smDup(e->d_name);
            if (names[nn]) nn++;
        }
        closedir(d);
        qsort(names, (size_t)nn, sizeof *names, cmpName);
        for (i = 0; i < nn; i++) {
            char sub[1700], label[400], why[240];
            SmSprite info;
            SprReg *grown;
            int rc;

            if (!extValidKey(names[i])) {
                plogLine("stages", "%s: the sprite folder \"%s\" is skipped: its name must be 1 to 48 characters of a-z, 0-9, - and _\n", modsNameOf(m), names[i]);
                continue;
            }
            snprintf(sub, sizeof sub, "%s/%s", dirPath, names[i]);
            snprintf(label, sizeof label, "%s:%s", modsNameOf(m), names[i]);
            if (sSpriteCount >= SM_SPRITES_MAX) {
                plogLine("stages", "%s: sprite \"%s\" skipped: the port holds %d mod sprites\n", modsNameOf(m), label, SM_SPRITES_MAX);
                continue;
            }
            why[0] = '\0';
            rc = sHost.spriteAdd(sub, label, &info, why, sizeof why);
            if (rc > 0) continue; /* not a new sprite: the folder is an original's (its frames are read by the images loader) */
            if (rc) {
                plogLine("stages", "%s rejected: %s\n", sub, why[0] ? why : "the sprite could not be built");
                continue;
            }
            grown = (SprReg *)realloc(sSprites, (size_t)(sSpriteCount + 1) * sizeof *sSprites);
            if (!grown) continue;
            sSprites = grown;
            sSprites[sSpriteCount].name = smDup(label);
            sSprites[sSpriteCount].info = info;
            if (!sSprites[sSpriteCount].name) continue;
            plogLine("stages", "sprite %s is character %d (frames up to %dx%d)\n", label, SM_SPRITE_CHAR_BASE + sSpriteCount, info.w, info.h);
            sSpriteCount++;
        }
        for (i = 0; i < nn; i++) free(names[i]);
        free(names);
    }
    partnerFieldSprites();
    if (sHost.spritesDone) sHost.spritesDone();
}

static void spritesFree(void) {
    int i;

    for (i = 0; i < sSpriteCount; i++) free(sSprites[i].name);
    free(sSprites);
    sSprites = NULL;
    sSpriteCount = 0;
}

int smSpriteCount(void) { return sSpriteCount; }

const char *smSpriteName(int character) {
    int k = character - SM_SPRITE_CHAR_BASE;

    return k >= 0 && k < sSpriteCount ? sSprites[k].name : NULL;
}

int smSpriteFind(const char *label) {
    int k;

    for (k = 0; label != NULL && k < sSpriteCount; k++) {
        if (strcmp(sSprites[k].name, label) == 0) return SM_SPRITE_CHAR_BASE + k;
    }
    return -1;
}

int smSpriteInfo(int character, SmSprite *out) {
    int k = character - SM_SPRITE_CHAR_BASE;

    if (k < 0 || k >= sSpriteCount) return 0;
    if (out) *out = sSprites[k].info;
    return 1;
}

/* ================= the patches ================= */

enum { F_CHARACTER, F_IMAGE, F_X, F_Y, F_DIR, F_COND, F_TALKS, F_COUNT };
static const char *const FIELD_NAMES[F_COUNT] = {"character", "image", "x", "y", "dir", "conditions", "talks"};

typedef struct SmEdit {
    int index;      /* the actor's place in the setup's list, or -1 when `key` names it */
    char *key;      /* "<mod>:<key>" */
    unsigned mask;  /* which F_* fields are set */
    int v[5];       /* character, image, x, y, dir */
    SdList cond;    /* F_COND: set 0 clears the conditions */
    SdTalk *talks;  /* F_TALKS: replaces the actor's talks */
    int ntalks;
    SdTalk *prepend; /* goes before the actor's talks */
    int nprepend;
} SmEdit;

typedef struct SmAdd {
    char *key; /* "<mod>:<key>" */
    SdActor actor;
} SmAdd;

typedef struct SmRef {
    int index;
    char *key;
} SmRef;

/* ---- slots, events and battles (S8, S10) ---- */

enum { SL_TYPE, SL_ARG, SL_X, SL_Y, SL_DIR, SL_HIDE, SL_PLACE, SL_PLACEARG, SL_COND, SL_AREA, SL_COUNT };
static const char *const SLOT_FIELDS[SL_COUNT] = {"type", "arg", "x", "y", "dir", "hideAnim", "place", "placeArg", "conditions", "area"};

/* A slot patch (index or key) or an added slot (key) */
typedef struct SmSlotEdit {
    int index; /* the slot's place in the setup's table, -1 when `key` names it */
    char *key;
    int isAdd;
    unsigned mask;  /* which SL_* are set */
    int v[8];       /* type, arg, x, y, dir, hideAnim, place, placeArg */
    int stage;      /* "stage": the exit's destination, the arg becomes its mode (0 none) */
    SdList cond;
    int ax, ay, aw, ah, facing;
} SmSlotEdit;

/* An event patch (by id or key) or an added event (key) */
typedef struct SmEventEdit {
    int id; /* the stage's own event, -1 when `key` names it */
    char *key;
    int isAdd;
    int evId; /* an added event's id (smEventId of its key) */
    int hasScript;
    int16_t *script;
    int nscript;
    int *nameOff; /* the named texts of the script's talks: the word offset of the talk and the registry id of the name */
    int *nameId;
    int nnames;
    int hasText, textFile, textEntry; /* an explicit {"file", "entry"} (textFile -1: the stage's own file) */
} SmEventEdit;

enum { BA_ENCOUNTER, BA_STAGE, BA_MUSIC, BA_RATE, BA_COUNT };
static const char *const BATTLE_FIELDS[BA_COUNT] = {"encounter", "stage", "music", "rate"};

/* A battle patch (area; a slot, some or all) or an added area (area, its eight battles) */
typedef struct SmBattleEdit {
    int isAdd;
    int area; /* 1-based, as the map's area layer */
    unsigned mask;
    int v[4];       /* encounter, stage, music, rate */
    unsigned slots; /* which of the eight battles a patch touches */
    SdAreaBattle b[8]; /* an added area's battles (the given ones repeat to fill eight) */
} SmBattleEdit;

/* The results of a gauge slot's cells 1 and 2 (T9); a cell not given has set 0 */
typedef struct SmFishEdit {
    SmRef ref;
    SdCatchList cell[2];
} SmFishEdit;

typedef struct SmFile {
    char *mod;  /* the folder name */
    char *path; /* for the log */
    int stage;
    int isNew; /* a stage-new-v1 file (new-stages T5): the stage's own sections, only "add" */
    SmEdit *edits;
    int nedits;
    SmAdd *adds;
    int nadds;
    SmRef *rems;
    int nrems;
    SmSlotEdit *sedits;
    int nsedits;
    SmRef *srems;
    int nsrems;
    SmEventEdit *eedits;
    int needits;
    SmBattleEdit *bedits;
    int nbedits;
    /* T9: fishing results per gauge slot (a reference like the slots') and the file's default, hidden spots to add, the prize lists */
    SmFishEdit *fishEdits;
    int nfishEdits;
    SdCatchList fishDef[2];
    SdSpot *spots;
    int nspots;
    SdCatchList prize[2];
} SmFile;

static SmFile *sFiles;
static int sFileCount;

static void listFree(SdList *l) {
    free(l->w);
    memset(l, 0, sizeof *l);
}

static void talksFree(SdTalk *t, int n) {
    int i;

    for (i = 0; i < n; i++) {
        listFree(&t[i].cond);
        listFree(&t[i].actions);
        free(t[i].preview);
    }
    free(t);
}

static void actorFree(SdActor *a) {
    listFree(&a->cond);
    talksFree(a->talks, a->ntalks);
    memset(a, 0, sizeof *a);
}

static int listClone(SdList *dst, const SdList *src) {
    memset(dst, 0, sizeof *dst);
    dst->set = src->set;
    dst->n = src->n;
    if (src->n > 0) {
        dst->w = (uint16_t *)malloc((size_t)src->n * sizeof *dst->w);
        if (!dst->w) return -1;
        memcpy(dst->w, src->w, (size_t)src->n * sizeof *dst->w);
    }
    return 0;
}

/* a deep copy of n talks; NULL for none (or no memory: *bad is set) */
static SdTalk *talksClone(const SdTalk *src, int n, int *bad) {
    SdTalk *out;
    int i;

    if (n <= 0) return NULL;
    out = (SdTalk *)calloc((size_t)n, sizeof *out);
    if (!out) {
        *bad = 1;
        return NULL;
    }
    for (i = 0; i < n; i++) {
        out[i].entry = src[i].entry;
        if (listClone(&out[i].cond, &src[i].cond) || listClone(&out[i].actions, &src[i].actions)) *bad = 1;
        /* a preview is read only and nothing reads it back: a copy does not carry it */
    }
    return out;
}

static int actorClone(SdActor *dst, const SdActor *src) {
    int bad = 0;

    *dst = *src;
    dst->talks = NULL;
    dst->ntalks = 0;
    if (listClone(&dst->cond, &src->cond)) bad = 1;
    dst->talks = talksClone(src->talks, src->ntalks, &bad);
    dst->ntalks = dst->talks ? src->ntalks : 0;
    return bad ? -1 : 0;
}

static void fileFree(SmFile *f) {
    int i;

    for (i = 0; i < f->nedits; i++) {
        free(f->edits[i].key);
        listFree(&f->edits[i].cond);
        talksFree(f->edits[i].talks, f->edits[i].ntalks);
        talksFree(f->edits[i].prepend, f->edits[i].nprepend);
    }
    free(f->edits);
    for (i = 0; i < f->nadds; i++) {
        free(f->adds[i].key);
        actorFree(&f->adds[i].actor);
    }
    free(f->adds);
    for (i = 0; i < f->nrems; i++) free(f->rems[i].key);
    free(f->rems);
    for (i = 0; i < f->nsedits; i++) {
        free(f->sedits[i].key);
        listFree(&f->sedits[i].cond);
    }
    free(f->sedits);
    for (i = 0; i < f->nsrems; i++) free(f->srems[i].key);
    free(f->srems);
    for (i = 0; i < f->needits; i++) {
        free(f->eedits[i].key);
        free(f->eedits[i].script);
        free(f->eedits[i].nameOff);
        free(f->eedits[i].nameId);
    }
    free(f->eedits);
    free(f->bedits);
    for (i = 0; i < f->nfishEdits; i++) {
        free(f->fishEdits[i].ref.key);
        sdCatchListFree(&f->fishEdits[i].cell[0]);
        sdCatchListFree(&f->fishEdits[i].cell[1]);
    }
    free(f->fishEdits);
    sdCatchListFree(&f->fishDef[0]);
    sdCatchListFree(&f->fishDef[1]);
    free(f->spots);
    sdCatchListFree(&f->prize[0]);
    sdCatchListFree(&f->prize[1]);
    free(f->mod);
    free(f->path);
    memset(f, 0, sizeof *f);
}

void smUnload(void) {
    int i;

    for (i = 0; i < sFileCount; i++) fileFree(&sFiles[i]);
    free(sFiles);
    sFiles = NULL;
    sFileCount = 0;
    for (i = 0; i < sOpCount; i++) free(sOps[i].name);
    memset(sOps, 0, sizeof sOps);
    sOpCount = 0;
    memset(sActs, 0, sizeof sActs);
    sActCount = 0;
    for (i = 0; i < sEventCount; i++) free(sEventNames[i]);
    memset(sEventNames, 0, sizeof sEventNames);
    sEventCount = 0;
    textsFree();
    spritesFree();
}

int smPatchCount(void) {
    return sFileCount;
}

int smHasPatches(int stage) {
    int i;

    for (i = 0; i < sFileCount; i++) {
        if (sFiles[i].stage == stage) return 1;
    }
    return 0;
}

/* ---- parsing ---- */

static void fail(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void fail(char *err, size_t cap, const char *fmt, ...) {
    va_list ap;

    if (!err || !cap) return;
    va_start(ap, fmt);
    vsnprintf(err, cap, fmt, ap);
    va_end(ap);
}

static char *smDup(const char *s) {
    char *c = (char *)malloc(strlen(s) + 1);

    if (c) strcpy(c, s);
    return c;
}

/* an integer member in lo..hi; *has says whether it is there. 0 when it is there and wrong. */
static int getInt(const ExtJson *o, const char *key, long lo, long hi, int *out, int *has, char *err, size_t cap, const char *where) {
    const ExtJson *j = extJsonGet(o, key);

    *has = j != NULL;
    if (!j) return 1;
    if (j->t != 0 || !extWhole(j->num, lo, hi)) {
        fail(err, cap, "%s: \"%s\" must be a whole number from %ld to %ld", where, key, lo, hi);
        return 0;
    }
    *out = (int)j->num;
    return 1;
}

/* a "<mod>:<key>" (or the bare key of the file's own mod) into *out; 0 with the reason in err */
static int readKey(const char *text, const char *mod, char **out, char *err, size_t cap, const char *where) {
    char full[160];
    const char *colon;

    if (!strchr(text, ':')) {
        if (!extValidKey(text)) {
            fail(err, cap, "%s: the key \"%s\" must be 1 to 48 characters of a-z, 0-9, - and _", where, text);
            return 0;
        }
        snprintf(full, sizeof full, "%s:%s", mod, text);
    } else {
        snprintf(full, sizeof full, "%s", text);
    }
    colon = strchr(full, ':');
    if (strlen(text) >= sizeof full - 1 || colon == full || strchr(colon + 1, ':') || !extValidKey(colon + 1)) {
        fail(err, cap, "%s: \"%s\" is not a key (\"<mod>:<key>\", the key 1 to 48 characters of a-z, 0-9, - and _)", where, text);
        return 0;
    }
    *out = smDup(full);
    if (!*out) {
        fail(err, cap, "out of memory");
        return 0;
    }
    return 1;
}

static int readCodes(const StageCodeEnv *env, const ExtJson *o, const char *key, SdList *l, int *has, char *err, size_t cap, const char *where) {
    const ExtJson *j = extJsonGet(o, key);
    char why[200];

    *has = j != NULL;
    if (!j) return 1;
    if (j->t == 3 && !strcmp(key, "conditions")) { /* null: no conditions at all */
        memset(l, 0, sizeof *l);
        return 1;
    }
    if (scListFromJson(env, j, &l->w, &l->n, why, sizeof why)) {
        fail(err, cap, "%s \"%s\": %s", where, key, why);
        return 0;
    }
    l->set = 1;
    return 1;
}

/* a "<mod>:<name>" of the registry of named texts: the entry SM_TEXT_BASE + its id */
static int namedText(const ExtJson *tx, const char *w, int *entry, char *err, size_t cap) {
    int id;

    if (tx->t != 1 || !tx->str || !scModNameValid(tx->str)) {
        fail(err, cap, "%s: \"text\" must be a name \"<mod>:<name>\" (the mod's folder, a colon, then a-z 0-9 _ - /)", w);
        return 0;
    }
    id = textFind(tx->str);
    if (id < 0) {
        fail(err, cap, "%s: the text \"%s\" is not defined (a line \"%s\" in mods/<mod>/text/eng/stages.json)", w, tx->str, strchr(tx->str, ':') + 1);
        return 0;
    }
    if (!sTexts[id].text[0]) {
        fail(err, cap, "%s: the text \"%s\" has no English text (text/eng/stages.json): the other languages fall back to it", w, tx->str);
        return 0;
    }
    *entry = SM_TEXT_BASE + id;
    return 1;
}

/* a list of talks; with `last`, the final one must have no conditions (the talk that always holds) */
static int readTalks(const StageCodeEnv *env, const ExtJson *arr, int last, SdTalk **out, int *n, char *err, size_t cap, const char *where) {
    SdTalk *t;
    int i;

    *out = NULL;
    *n = 0;
    if (arr->t != 4) {
        fail(err, cap, "%s must be a list of talks", where);
        return 0;
    }
    if (arr->n == 0) return 1;
    t = (SdTalk *)calloc((size_t)arr->n, sizeof *t);
    if (!t) {
        fail(err, cap, "out of memory");
        return 0;
    }
    for (i = 0; i < arr->n; i++) {
        const ExtJson *o = &arr->v[i];
        char w[120];
        int has, entry = 0, hasEntry;

        snprintf(w, sizeof w, "%s %d", where, i);
        if (o->t != 2) {
            fail(err, cap, "%s must be an object", w);
            goto bad;
        }
        if (extJsonGet(o, "text")) {
            if (extJsonGet(o, "entry")) {
                fail(err, cap, "%s: give an \"entry\" or a \"text\", not both", w);
                goto bad;
            }
            if (!namedText(extJsonGet(o, "text"), w, &entry, err, cap)) goto bad;
            hasEntry = 1;
        } else {
            if (!getInt(o, "entry", 0, 0xFFFF, &entry, &hasEntry, err, cap, w)) goto bad;
            if (!hasEntry) {
                fail(err, cap, "%s needs an \"entry\" (a line of the stage's text file) or a \"text\" (a name of mods/<mod>/text/<lang>/stages.json)", w);
                goto bad;
            }
        }
        t[i].entry = entry;
        if (!readCodes(env, o, "conditions", &t[i].cond, &has, err, cap, w)) goto bad;
        if (!readCodes(env, o, "actions", &t[i].actions, &has, err, cap, w)) goto bad;
    }
    if (last && t[arr->n - 1].cond.set) {
        fail(err, cap, "%s: the last talk must have no \"conditions\" (it is the one that always holds)", where);
        goto bad;
    }
    *out = t;
    *n = arr->n;
    return 1;
bad:
    talksFree(t, arr->n);
    return 0;
}

#define GROW(arr, n) \
    do { \
        void *g_ = realloc((arr), (size_t)((n) + 1) * sizeof *(arr)); \
        if (!g_) { \
            fail(err, cap, "out of memory"); \
            return 0; \
        } \
        (arr) = g_; \
        memset(&(arr)[(n)], 0, sizeof *(arr)); \
    } while (0)

static int hasUnknownKey(const ExtJson *o, const char *const *known, int nknown, char *err, size_t cap, const char *where) {
    int i, k;

    for (i = 0; i < o->n; i++) {
        int ok = 0;

        for (k = 0; k < nknown; k++) ok |= !strcmp(o->keys[i], known[k]);
        if (!ok) {
            fail(err, cap, "%s: unknown key \"%s\"", where, o->keys[i]);
            return 1;
        }
    }
    return 0;
}

/* "character" (a number of the game's) or "sprite" (a mod's: "<mod>:<name>", the bare name is this file's mod's) as one character id; *has says whether
   either is there. The number is checked against the host's table of characters. */
static int readWho(const ExtJson *o, const char *mod, int *character, int *has, char *err, size_t cap, const char *where) {
    const ExtJson *c = extJsonGet(o, "character"), *sp = extJsonGet(o, "sprite");
    char *full = NULL;
    int k;

    *has = c != NULL || sp != NULL;
    if (c && sp) {
        fail(err, cap, "%s: give \"character\" (one of the game's) or \"sprite\" (one a mod brings), not both", where);
        return 0;
    }
    if (c) {
        if (c->t != 0 || !extWhole(c->num, 0, 0xFFFF)) {
            fail(err, cap, "%s: \"character\" must be a whole number from 0 to 65535", where);
            return 0;
        }
        *character = (int)c->num;
        if (*character >= SM_SPRITE_CHAR_BASE) {
            fail(err, cap, "%s: the characters from %d up are the sprites mods bring: name one with \"sprite\"", where, SM_SPRITE_CHAR_BASE);
            return 0;
        }
        if (sHost.characterOk && !sHost.characterOk(*character)) {
            fail(err, cap, "%s: the game has no character %d with a sprite", where, *character);
            return 0;
        }
        return 1;
    }
    if (!sp) return 1;
    if (sp->t != 1 || !sp->str) {
        fail(err, cap, "%s: \"sprite\" must be a string (\"<mod>:<name>\")", where);
        return 0;
    }
    if (!readKey(sp->str, mod, &full, err, cap, where)) return 0;
    for (k = 0; k < sSpriteCount; k++) {
        if (!strcmp(sSprites[k].name, full)) {
            *character = SM_SPRITE_CHAR_BASE + k;
            free(full);
            return 1;
        }
    }
    fail(err, cap, "%s: there is no sprite \"%s\" (a folder mods/<mod>/sprites/<name> with a valid sprite.json)", where, full);
    free(full);
    return 0;
}

static int parseEdit(const StageCodeEnv *env, const ExtJson *o, SmFile *f, char *err, size_t cap, const char *where) {
    static const char *const known[] = {"index", "key", "character", "sprite", "image", "x", "y", "dir", "conditions", "talks", "prependTalks"};
    SmEdit *e;
    const ExtJson *k, *tl;
    int has, hasIndex, i;
    int *dst[5];

    if (o->t != 2) {
        fail(err, cap, "%s must be an object", where);
        return 0;
    }
    if (hasUnknownKey(o, known, (int)(sizeof known / sizeof known[0]), err, cap, where)) return 0;
    GROW(f->edits, f->nedits);
    e = &f->edits[f->nedits++];
    e->index = -1;
    if (!getInt(o, "index", 0, 1023, &e->index, &hasIndex, err, cap, where)) return 0;
    k = extJsonGet(o, "key");
    if (hasIndex == (k != NULL)) {
        fail(err, cap, "%s needs exactly one of \"index\" (its place in the stage's list) and \"key\" (an actor a patch added)", where);
        return 0;
    }
    if (k) {
        if (k->t != 1 || !k->str) {
            fail(err, cap, "%s: \"key\" must be a string", where);
            return 0;
        }
        if (!readKey(k->str, f->mod, &e->key, err, cap, where)) return 0;
    }
    dst[0] = &e->v[0];
    dst[1] = &e->v[1];
    dst[2] = &e->v[2];
    dst[3] = &e->v[3];
    dst[4] = &e->v[4];
    {
        static const long lo[5] = {0, 0, -32768, -32768, 0}, hi[5] = {0xFFFF, 32767, 32767, 32767, 7};

        for (i = 1; i < 5; i++) { /* the character (0) is readWho's */
            if (!getInt(o, FIELD_NAMES[i], lo[i], hi[i], dst[i], &has, err, cap, where)) return 0;
            if (has) e->mask |= 1u << i;
        }
    }
    if (!readWho(o, f->mod, &e->v[0], &has, err, cap, where)) return 0;
    if (has) e->mask |= 1u << F_CHARACTER;
    if ((e->mask & (1u << F_CHARACTER)) && !(e->mask & (1u << F_IMAGE))) { /* a new character needs a slot that suits it (S9) */
        e->mask |= 1u << F_IMAGE;
        e->v[1] = SD_IMAGE_AUTO;
    }
    if (!readCodes(env, o, "conditions", &e->cond, &has, err, cap, where)) return 0;
    if (has) e->mask |= 1u << F_COND;
    if ((tl = extJsonGet(o, "talks")) != NULL) {
        if (!readTalks(env, tl, 1, &e->talks, &e->ntalks, err, cap, "\"talks\" talk")) {
            return 0;
        }
        e->mask |= 1u << F_TALKS;
    }
    if ((tl = extJsonGet(o, "prependTalks")) != NULL) {
        if (!readTalks(env, tl, 0, &e->prepend, &e->nprepend, err, cap, "\"prependTalks\" talk")) return 0;
    }
    if (!e->mask && !e->nprepend) {
        fail(err, cap, "%s changes nothing (name a field, \"talks\" or \"prependTalks\")", where);
        return 0;
    }
    return 1;
}

static int parseAdd(const StageCodeEnv *env, const ExtJson *o, SmFile *f, char *err, size_t cap, const char *where) {
    static const char *const known[] = {"key", "character", "sprite", "image", "x", "y", "dir", "conditions", "talks"};
    SmAdd *a;
    const ExtJson *k, *tl;
    int has, i, hasCharacter, hasImage, hasX, hasY;
    SdActor *act;

    if (o->t != 2) {
        fail(err, cap, "%s must be an object", where);
        return 0;
    }
    if (hasUnknownKey(o, known, (int)(sizeof known / sizeof known[0]), err, cap, where)) return 0;
    k = extJsonGet(o, "key");
    if (!k || k->t != 1 || !k->str) {
        fail(err, cap, "%s needs a \"key\" (a name other patches can use to refer to this character)", where);
        return 0;
    }
    GROW(f->adds, f->nadds);
    a = &f->adds[f->nadds++];
    if (!readKey(k->str, f->mod, &a->key, err, cap, where)) return 0;
    for (i = 0; i < f->nadds - 1; i++) {
        if (!strcmp(f->adds[i].key, a->key)) {
            fail(err, cap, "%s: the key \"%s\" is added twice", where, k->str);
            return 0;
        }
    }
    act = &a->actor;
    if (!readWho(o, f->mod, &act->character, &hasCharacter, err, cap, where) ||
        !getInt(o, "image", 0, 32767, &act->image, &hasImage, err, cap, where) ||
        !getInt(o, "x", -32768, 32767, &act->x, &hasX, err, cap, where) || !getInt(o, "y", -32768, 32767, &act->y, &hasY, err, cap, where) ||
        !getInt(o, "dir", 0, 7, &act->dir, &has, err, cap, where))
        return 0;
    if (!hasCharacter || !hasX || !hasY) {
        fail(err, cap, "%s needs \"character\" (or \"sprite\"), \"x\" and \"y\"", where);
        return 0;
    }
    if (!hasImage) act->image = SD_IMAGE_AUTO; /* its own VRAM slot (S9) */
    if (!readCodes(env, o, "conditions", &act->cond, &has, err, cap, where)) return 0;
    if ((tl = extJsonGet(o, "talks")) != NULL) {
        if (!readTalks(env, tl, 1, &act->talks, &act->ntalks, err, cap, "\"talks\" talk")) return 0;
    }
    return 1;
}

static int parseRemove(const ExtJson *r, SmFile *f, char *err, size_t cap, const char *where) {
    SmRef *x;

    GROW(f->rems, f->nrems);
    x = &f->rems[f->nrems++];
    x->index = -1;
    if (r->t == 0 && extWhole(r->num, 0, 1023)) {
        x->index = (int)r->num;
        return 1;
    }
    if (r->t == 1 && r->str) return readKey(r->str, f->mod, &x->key, err, cap, where);
    fail(err, cap, "%s must be an actor's index or key", where);
    return 0;
}

enum { SLOT_EXIT_TYPE = 1, SLOT_EVENT_TYPE = 8 }; /* SLOT_EXIT and SLOT_EVENT of include/field/field_map.h */

/* ---- slots (S8) ---- */

static int parseSlot(const SmHost *host, const StageCodeEnv *env, const ExtJson *o, SmFile *f, int isAdd, char *err, size_t cap, const char *where) {
    static const char *const known[] = {"index", "key", "type", "arg", "stage", "event", "x", "y", "dir", "hideAnim", "place", "placeArg", "conditions", "area"};
    SmSlotEdit *e;
    const ExtJson *k, *ty, *ar;
    int has, hasIndex = 0, i;
    static const long lo[8] = {1, 0, 0, 0, 0, 0, 0, 0}, hi[8] = {14, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

    if (o->t != 2) {
        fail(err, cap, "%s must be an object", where);
        return 0;
    }
    if (hasUnknownKey(o, known, (int)(sizeof known / sizeof known[0]), err, cap, where)) return 0;
    GROW(f->sedits, f->nsedits);
    e = &f->sedits[f->nsedits++];
    e->index = -1;
    e->isAdd = isAdd;
    e->facing = -1;
    k = extJsonGet(o, "key");
    if (isAdd) {
        if (extJsonGet(o, "index") || !k) {
            fail(err, cap, "%s needs a \"key\" (a name other patches can use) and no \"index\"", where);
            return 0;
        }
    } else {
        if (!getInt(o, "index", 0, 1023, &e->index, &hasIndex, err, cap, where)) return 0;
        if (hasIndex == (k != NULL)) {
            fail(err, cap, "%s needs exactly one of \"index\" (the slot's place in the stage's table) and \"key\" (a slot a patch added)", where);
            return 0;
        }
    }
    if (k) {
        if (k->t != 1 || !k->str) {
            fail(err, cap, "%s: \"key\" must be a string", where);
            return 0;
        }
        if (!readKey(k->str, f->mod, &e->key, err, cap, where)) return 0;
        if (isAdd) {
            for (i = 0; i < f->nsedits - 1; i++) {
                if (f->sedits[i].isAdd && !strcmp(f->sedits[i].key, e->key)) {
                    fail(err, cap, "%s: the key \"%s\" is added twice", where, k->str);
                    return 0;
                }
            }
        }
    }
    if ((ty = extJsonGet(o, "type")) != NULL) {
        if (ty->t == 1 && ty->str) {
            e->v[0] = scSlotTypeByName(ty->str);
            if (e->v[0] <= 0) {
                fail(err, cap, "%s: unknown slot type \"%s\"", where, ty->str);
                return 0;
            }
        } else if (ty->t == 0 && extWhole(ty->num, 1, 14)) {
            e->v[0] = (int)ty->num;
        } else {
            fail(err, cap, "%s: \"type\" is a slot type name (exit, event, warp-1...) or a number from 1 to 14", where);
            return 0;
        }
        e->mask |= 1u << SL_TYPE;
    }
    for (i = 1; i < 8; i++) {
        static const int field[8] = {0, SL_ARG, SL_X, SL_Y, SL_DIR, SL_HIDE, SL_PLACE, SL_PLACEARG};

        if (!getInt(o, SLOT_FIELDS[field[i]], lo[i], hi[i], &e->v[i], &has, err, cap, where)) return 0;
        if (has) e->mask |= 1u << field[i];
    }
    if ((ar = extJsonGet(o, "stage")) != NULL) {
        if (e->mask & (1u << SL_ARG)) {
            fail(err, cap, "%s: give \"arg\" or \"stage\", not both", where);
            return 0;
        }
        if (ar->t == 1 && ar->str) { /* a stage a mod adds, by name (mod_stages.h): the number the registry gave it */
            e->stage = host && host->stageByName ? host->stageByName(ar->str) : -1;
            if (e->stage < 0) {
                fail(err, cap, "%s: there is no stage \"%s\"", where, ar->str);
                return 0;
            }
        } else {
            if (ar->t != 0 || !extWhole(ar->num, 1, 0xFFFF)) {
                fail(err, cap, "%s: \"stage\" must be a stage number or a name \"<mod>:<name>\"", where);
                return 0;
            }
            e->stage = (int)ar->num;
        }
        if (host && host->stageExists && !host->stageExists(e->stage)) {
            fail(err, cap, "%s: there is no stage %d in this build", where, e->stage);
            return 0;
        }
        if (!(e->mask & (1u << SL_TYPE)) && isAdd) {
            e->v[0] = SLOT_EXIT_TYPE;
            e->mask |= 1u << SL_TYPE;
        }
        e->mask |= 1u << SL_ARG;
    }
    if ((ar = extJsonGet(o, "event")) != NULL) {
        if (e->mask & (1u << SL_ARG)) {
            fail(err, cap, "%s: give only one of \"arg\", \"stage\" and \"event\"", where);
            return 0;
        }
        if (ar->t == 1 && ar->str) {
            e->v[1] = smEventId(ar->str);
            if (e->v[1] < 0) {
                fail(err, cap, "%s: \"event\" \"%s\" is not a name \"<mod>:<name>\"", where, ar->str);
                return 0;
            }
        } else if (ar->t == 0 && extWhole(ar->num, 0, 0xFFFF)) {
            e->v[1] = (int)ar->num;
        } else {
            fail(err, cap, "%s: \"event\" is a name \"<mod>:<name>\" or an event id", where);
            return 0;
        }
        if (!(e->mask & (1u << SL_TYPE)) && isAdd) {
            e->v[0] = SLOT_EVENT_TYPE;
            e->mask |= 1u << SL_TYPE;
        }
        e->mask |= 1u << SL_ARG;
    }
    if (!readCodes(env, o, "conditions", &e->cond, &has, err, cap, where)) return 0;
    if (has) {
        if (e->cond.n > 4) {
            fail(err, cap, "%s: a slot holds at most two conditions", where);
            return 0;
        }
        e->mask |= 1u << SL_COND;
    }
    if ((ar = extJsonGet(o, "area")) != NULL) {
        static const char *const ak[] = {"x", "y", "w", "h", "facing"};
        int hx, hy, hw, hh;

        if (ar->t != 2 || hasUnknownKey(ar, ak, 5, err, cap, "\"area\"")) {
            if (ar->t != 2) fail(err, cap, "%s: \"area\" must be an object {\"x\", \"y\", \"w\", \"h\", \"facing\"}", where);
            return 0;
        }
        if (!getInt(ar, "x", 0, 32767, &e->ax, &hx, err, cap, where) || !getInt(ar, "y", 0, 32767, &e->ay, &hy, err, cap, where) ||
            !getInt(ar, "w", 1, 32767, &e->aw, &hw, err, cap, where) || !getInt(ar, "h", 1, 32767, &e->ah, &hh, err, cap, where) ||
            !getInt(ar, "facing", 0, 7, &e->facing, &has, err, cap, where))
            return 0;
        if (!hx || !hy || !hw || !hh) {
            fail(err, cap, "%s: \"area\" needs \"x\", \"y\", \"w\" and \"h\" (pixels of the map)", where);
            return 0;
        }
        if (!has) e->facing = -1;
        e->mask |= 1u << SL_AREA;
    }
    if (isAdd) {
        if (!(e->mask & (1u << SL_TYPE))) {
            fail(err, cap, "%s needs a \"type\" (or a \"stage\" or an \"event\", which imply exit and event)", where);
            return 0;
        }
        if (!(e->mask & (1u << SL_AREA)) && !f->isNew) { /* a stage a mod adds has the cells of its own trigger layer to point at the slot */
            fail(err, cap, "%s needs an \"area\": the map has no cell that points at a new slot, so it is set off inside this rectangle", where);
            return 0;
        }
    } else if (!e->mask) {
        fail(err, cap, "%s changes nothing", where);
        return 0;
    }
    return 1;
}

static int parseRef(const ExtJson *r, SmFile *f, SmRef **arr, int *n, char *err, size_t cap, const char *where) {
    SmRef *x;
    void *g = realloc(*arr, (size_t)(*n + 1) * sizeof **arr);

    if (!g) {
        fail(err, cap, "out of memory");
        return 0;
    }
    *arr = (SmRef *)g;
    x = &(*arr)[(*n)++];
    memset(x, 0, sizeof *x);
    x->index = -1;
    if (r->t == 0 && extWhole(r->num, 0, 1023)) {
        x->index = (int)r->num;
        return 1;
    }
    if (r->t == 1 && r->str) return readKey(r->str, f->mod, &x->key, err, cap, where);
    fail(err, cap, "%s must be an index or a key", where);
    return 0;
}

/* ---- events (S8) ---- */

/* The script's names: a talk's entry may be a string "<mod>:<name>"; the string is replaced by 0 here and its place is kept to be filled when the stage starts */
static int parseScript(const ExtJson *list, SmEventEdit *e, char *err, size_t cap, const char *where) {
    int i, ntalk = 0, offs[256], found, k;
    char why[200];

    if (!list || list->t != 4) {
        fail(err, cap, "%s: \"script\" must be a list of commands ending with [\"end\"]", where);
        return 0;
    }
    /* names, in the order of the talk commands that carry them */
    {
        int named = 0;
        ExtJson *w = (ExtJson *)list; /* the tree is ours: a name's slot becomes 0 */

        e->nameId = (int *)calloc((size_t)list->n + 1, sizeof *e->nameId);
        e->nameOff = (int *)calloc((size_t)list->n + 1, sizeof *e->nameOff);
        if (!e->nameId || !e->nameOff) {
            fail(err, cap, "out of memory");
            return 0;
        }
        for (i = 0; i < w->n; i++) {
            ExtJson *c = &w->v[i];

            if (c->t == 4 && c->n >= 1 && c->v[0].t == 1 && c->v[0].str && !strcmp(c->v[0].str, "talk")) {
                int entry;

                if (c->n >= 3 && c->v[2].t == 1) {
                    char w2[100];

                    snprintf(w2, sizeof w2, "%s command %d", where, i);
                    if (!namedText(&c->v[2], w2, &entry, err, cap)) return 0;
                    e->nameId[named] = entry - SM_TEXT_BASE;
                    e->nameOff[named] = ntalk; /* the talk's number for now, its word offset below */
                    named++;
                    c->v[2].t = 0;
                    c->v[2].num = 0;
                }
                ntalk++;
            }
        }
        e->nnames = named;
    }
    if (scScriptFromJson(list, &e->script, &e->nscript, why, sizeof why)) {
        fail(err, cap, "%s: \"script\": %s", where, why);
        return 0;
    }
    e->hasScript = 1;
    found = scScriptTalks(e->script, e->nscript, offs, 256);
    if (found != ntalk) {
        fail(err, cap, "%s: a script with named texts cannot hold raw talk words", where);
        return 0;
    }
    for (k = 0; k < e->nnames; k++) e->nameOff[k] = offs[e->nameOff[k]];
    return 1;
}

static int parseEvent(const StageCodeEnv *env, const ExtJson *o, SmFile *f, int isAdd, char *err, size_t cap, const char *where) {
    static const char *const known[] = {"id", "key", "script", "text"};
    SmEventEdit *e;
    const ExtJson *k, *tx;
    int hasId = 0;

    (void)env;
    if (o->t != 2) {
        fail(err, cap, "%s must be an object", where);
        return 0;
    }
    if (hasUnknownKey(o, known, (int)(sizeof known / sizeof known[0]), err, cap, where)) return 0;
    GROW(f->eedits, f->needits);
    e = &f->eedits[f->needits++];
    e->id = -1;
    e->isAdd = isAdd;
    e->textFile = -1;
    k = extJsonGet(o, "key");
    if (isAdd) {
        if (extJsonGet(o, "id") || !k) {
            fail(err, cap, "%s needs a \"key\" (the event's name, \"<mod>:<name>\") and no \"id\": the port gives it an id", where);
            return 0;
        }
    } else {
        if (!getInt(o, "id", 0, 0xFFFE, &e->id, &hasId, err, cap, where)) return 0;
        if (hasId == (k != NULL)) {
            fail(err, cap, "%s needs exactly one of \"id\" (an event id of the stage's table) and \"key\" (an event a patch added)", where);
            return 0;
        }
    }
    if (k) {
        if (k->t != 1 || !k->str) {
            fail(err, cap, "%s: \"key\" must be a string", where);
            return 0;
        }
        if (!readKey(k->str, f->mod, &e->key, err, cap, where)) return 0;
        e->evId = smEventId(e->key);
        if (e->evId < 0) {
            fail(err, cap, "%s: the key \"%s\" cannot be an event name (the table of events is full)", where, e->key);
            return 0;
        }
        if (isAdd) {
            int i;

            for (i = 0; i < f->needits - 1; i++) {
                if (f->eedits[i].isAdd && f->eedits[i].evId == e->evId) {
                    fail(err, cap, "%s: the key \"%s\" is added twice", where, k->str);
                    return 0;
                }
            }
        }
    }
    if ((tx = extJsonGet(o, "script")) != NULL && !parseScript(tx, e, err, cap, where)) return 0;
    if ((tx = extJsonGet(o, "text")) != NULL) {
        static const char *const tk[] = {"file", "entry"};
        int hasFile, hasEntry;

        if (tx->t == 1) {
            fail(err, cap, "%s: \"text\" is the game's own block of lines {\"file\", \"entry\"}; name a mod's texts in the script's talks: [\"talk\", 0, \"<mod>:<name>\", id, kind]", where);
            return 0;
        }
        if (tx->t != 2 || hasUnknownKey(tx, tk, 2, err, cap, "\"text\"")) {
            if (tx->t != 2) fail(err, cap, "%s: \"text\" is {\"file\", \"entry\"}", where);
            return 0;
        }
        if (!getInt(tx, "file", 0, 0xFFFF, &e->textFile, &hasFile, err, cap, where) || !getInt(tx, "entry", 0, 0xFFFE, &e->textEntry, &hasEntry, err, cap, where))
            return 0;
        if (!hasEntry) {
            fail(err, cap, "%s: \"text\" needs an \"entry\" (and may give a \"file\", the stage's own by default)", where);
            return 0;
        }
        if (!hasFile) e->textFile = -1;
        e->hasText = 1;
    }
    if (isAdd && !e->hasScript) {
        fail(err, cap, "%s needs a \"script\" (a mod's event has no C start function)", where);
        return 0;
    }
    if (!isAdd && !e->hasScript && !e->hasText) {
        fail(err, cap, "%s changes nothing (give a \"script\" or a \"text\")", where);
        return 0;
    }
    return 1;
}

/* ---- battles (S10) ---- */

static int parseBattleSlot(const SmHost *host, const ExtJson *o, int v[3], unsigned *mask, char *err, size_t cap, const char *where) {
    int i, has;
    static const long lo[3] = {1, 0, 0}, hi[3] = {4095, 0x7FFFFFFF, 0x7FFFFFFF};

    for (i = 0; i < 3; i++) {
        const ExtJson *j = extJsonGet(o, BATTLE_FIELDS[i]);

        if (i == 0 && j != NULL && j->t == 1) { /* a new encounter a mod added, by name (new-stages T5b) */
            int number = host && host->encounterByName && j->str ? host->encounterByName(j->str) : -1;

            if (number < 0) {
                fail(err, cap, "%s: there is no field encounter named \"%s\" (a name \"<mod>:<name>\" of a row a mod adds in tables/field_encounters.json)", where, j->str ? j->str : "");
                return 0;
            }
            v[0] = number;
            *mask |= 1u;
            continue;
        }
        if (!getInt(o, BATTLE_FIELDS[i], lo[i], hi[i], &v[i], &has, err, cap, where)) return 0;
        if (has) *mask |= 1u << i;
    }
    if ((*mask & 1u) && host && host->encounterOk && !host->encounterOk(v[0])) {
        fail(err, cap, "%s: there is no field encounter %d (a row of field_encounters)", where, v[0]);
        return 0;
    }
    return 1;
}

static int parseBattle(const SmHost *host, const ExtJson *o, SmFile *f, int isAdd, char *err, size_t cap, const char *where) {
    static const char *const kp[] = {"area", "index", "indices", "rate", "encounter", "stage", "music"};
    static const char *const ka[] = {"area", "rate", "slots"};
    SmBattleEdit *e;
    int has, i, hasArea;
    const ExtJson *ix;
    unsigned dummy;

    if (o->t != 2) {
        fail(err, cap, "%s must be an object", where);
        return 0;
    }
    if (hasUnknownKey(o, isAdd ? ka : kp, isAdd ? 3 : 7, err, cap, where)) return 0;
    GROW(f->bedits, f->nbedits);
    e = &f->bedits[f->nbedits++];
    e->isAdd = isAdd;
    if (!getInt(o, "area", 1, SD_AREAS_MAX, &e->area, &hasArea, err, cap, where)) return 0;
    if (!hasArea) {
        fail(err, cap, "%s needs an \"area\" (1 to %d: the value of the map's area layer)", where, SD_AREAS_MAX);
        return 0;
    }
    if (!getInt(o, "rate", 0, 0xFFFF, &e->v[3], &has, err, cap, where)) return 0;
    if (has) e->mask |= 1u << BA_RATE;
    if (isAdd) {
        const ExtJson *sl = extJsonGet(o, "slots");

        if (!has || !sl || sl->t != 4 || sl->n < 1 || sl->n > 8) {
            fail(err, cap, "%s needs a \"rate\" and \"slots\": 1 to 8 battles {\"encounter\", \"stage\", \"music\"} (fewer than 8 repeat to fill them)", where);
            return 0;
        }
        for (i = 0; i < 8; i++) {
            int v[3] = {0, 0, 0};
            unsigned m = 0;
            char w[100];

            if (i < sl->n) {
                snprintf(w, sizeof w, "%s slot %d", where, i);
                if (!parseBattleSlot(host, &sl->v[i], v, &m, err, cap, w)) return 0;
                if (m != 7u) {
                    fail(err, cap, "%s: a battle needs \"encounter\", \"stage\" and \"music\"", w);
                    return 0;
                }
                e->b[i].encounter = v[0];
                e->b[i].stage = v[1];
                e->b[i].music = v[2];
            } else {
                e->b[i] = e->b[i % sl->n];
            }
        }
        return 1;
    }
    e->slots = 0xFF;
    if ((ix = extJsonGet(o, "index")) != NULL) {
        if (ix->t != 0 || !extWhole(ix->num, 0, 7) || extJsonGet(o, "indices")) {
            fail(err, cap, "%s: \"index\" is one battle, 0 to 7 (or \"indices\": a list of them, not both)", where);
            return 0;
        }
        e->slots = 1u << (int)ix->num;
    }
    if ((ix = extJsonGet(o, "indices")) != NULL) {
        if (ix->t != 4 || ix->n < 1) {
            fail(err, cap, "%s: \"indices\" is a list of battles, 0 to 7", where);
            return 0;
        }
        e->slots = 0;
        for (i = 0; i < ix->n; i++) {
            if (ix->v[i].t != 0 || !extWhole(ix->v[i].num, 0, 7)) {
                fail(err, cap, "%s: \"indices\" is a list of battles, 0 to 7", where);
                return 0;
            }
            e->slots |= 1u << (int)ix->v[i].num;
        }
    }
    dummy = 0;
    if (!parseBattleSlot(host, o, e->v, &dummy, err, cap, where)) return 0;
    e->mask |= dummy;
    if (!e->mask) {
        fail(err, cap, "%s changes nothing (give \"rate\", \"encounter\", \"stage\" or \"music\")", where);
        return 0;
    }
    return 1;
}


/* ---- fishing and hidden spots (T9) ---- */

/* an item: a number (1 to 511), "dw3:item/<id>" or a name the host resolves ("<mod>:<key>"); -1 when it is none */
static int catchItemRef(const StageCodeEnv *env, const ExtJson *j) {
    if (j->t == 0) return extWhole(j->num, 1, 0x1FF) ? (int)j->num : -1;
    if (j->t != 1 || !j->str) return -1;
    if (!strncmp(j->str, "dw3:item/", 9)) {
        char *end;
        long v = strtol(j->str + 9, &end, 10);

        return (j->str[9] && !*end && v >= 1 && v <= 0x1FF) ? (int)v : -1;
    }
    if (env && env->resolve) {
        int id = env->resolve(env->ud, "item", j->str);

        return (id >= 1 && id <= 0x1FF) ? id : -1;
    }
    return -1;
}

/* One result: {"encounter", "stage", "music"} (a battle), {"item"} or {"nothing": true}, each with an optional "weight" */
static int parseCatchOne(const SmHost *host, const StageCodeEnv *env, const ExtJson *o, SdCatch *c, char *err, size_t cap, const char *where) {
    static const char *const known[] = {"encounter", "stage", "music", "item", "nothing", "weight"};
    const ExtJson *j;
    int has, kinds = 0;

    if (o->t != 2) {
        fail(err, cap, "%s must be an object ({\"encounter\", \"stage\", \"music\"}, {\"item\"} or {\"nothing\": true})", where);
        return 0;
    }
    if (hasUnknownKey(o, known, (int)(sizeof known / sizeof known[0]), err, cap, where)) return 0;
    c->weight = 1;
    if (!getInt(o, "weight", 1, SD_CATCH_WEIGHT_MAX, &c->weight, &has, err, cap, where)) return 0;
    kinds = (extJsonGet(o, "encounter") != NULL) + (extJsonGet(o, "item") != NULL) + (extJsonGet(o, "nothing") != NULL);
    if (kinds != 1) {
        fail(err, cap, "%s needs exactly one of \"encounter\", \"item\" and \"nothing\"", where);
        return 0;
    }
    if ((j = extJsonGet(o, "encounter")) != NULL) {
        int v[3] = {0, 0, 0}, hs, hm;

        c->kind = SD_CATCH_BATTLE;
        if (!getInt(o, "stage", 0, 0x7FFFFFFF, &v[1], &hs, err, cap, where) || !getInt(o, "music", 0, 0x7FFFFFFF, &v[2], &hm, err, cap, where)) return 0;
        if (!hs || !hm) {
            fail(err, cap, "%s: a battle result needs \"stage\" (the fight stage) and \"music\" too", where);
            return 0;
        }
        if (j->t == 1) {
            int number = host && host->encounterByName && j->str ? host->encounterByName(j->str) : -1;

            if (number < 0) {
                fail(err, cap, "%s: there is no field encounter named \"%s\" (a name \"<mod>:<name>\" of a row a mod adds in tables/field_encounters.json)", where, j->str ? j->str : "");
                return 0;
            }
            v[0] = number;
        } else if (j->t != 0 || !extWhole(j->num, 1, 4095)) {
            fail(err, cap, "%s: \"encounter\" must be a field encounter number or a name \"<mod>:<name>\"", where);
            return 0;
        } else {
            v[0] = (int)j->num;
        }
        if (host && host->encounterOk && !host->encounterOk(v[0])) {
            fail(err, cap, "%s: there is no field encounter %d (a row of field_encounters)", where, v[0]);
            return 0;
        }
        c->encounter = v[0];
        c->stage = v[1];
        c->music = v[2];
    } else if ((j = extJsonGet(o, "item")) != NULL) {
        c->kind = SD_CATCH_ITEM;
        c->item = catchItemRef(env, j);
        if (c->item < 0) {
            fail(err, cap, "%s: \"item\" must be an item (a number from 1 to 511, \"dw3:item/<id>\" or a mod's \"<mod>:<key>\")", where);
            return 0;
        }
    } else {
        j = extJsonGet(o, "nothing");
        if (j->t != 3 || !j->num) {
            fail(err, cap, "%s: \"nothing\" must be true", where);
            return 0;
        }
        c->kind = SD_CATCH_NOTHING;
    }
    return 1;
}

/* A cell or a prize: one result object or a list of up to SD_CATCH_MAX (an empty list gives nothing). Sets the list. */
static int parseCatchList(const SmHost *host, const StageCodeEnv *env, const ExtJson *j, SdCatchList *l, char *err, size_t cap, const char *where) {
    int i;

    if (j->t != 4 && j->t != 2) {
        fail(err, cap, "%s must be a result or a list of results", where);
        return 0;
    }
    l->set = 1;
    if (j->t == 2) {
        l->v = (SdCatch *)calloc(1, sizeof *l->v);
        if (!l->v) {
            fail(err, cap, "out of memory");
            return 0;
        }
        l->n = 1;
        return parseCatchOne(host, env, j, &l->v[0], err, cap, where);
    }
    if (j->n > SD_CATCH_MAX) {
        fail(err, cap, "%s holds at most %d results", where, SD_CATCH_MAX);
        return 0;
    }
    if (j->n > 0) {
        l->v = (SdCatch *)calloc((size_t)j->n, sizeof *l->v);
        if (!l->v) {
            fail(err, cap, "out of memory");
            return 0;
        }
    }
    for (i = 0; i < j->n; i++) {
        char w[100];

        l->n = i + 1;
        snprintf(w, sizeof w, "%s result %d", where, i);
        if (!parseCatchOne(host, env, &j->v[i], &l->v[i], err, cap, w)) return 0;
    }
    return 1;
}

static int parseCells(const SmHost *host, const StageCodeEnv *env, const ExtJson *o, SdCatchList cell[2], char *err, size_t cap, const char *where) {
    static const char *const names[2] = {"1", "2"};
    int c;

    for (c = 0; c < 2; c++) {
        const ExtJson *j = extJsonGet(o, names[c]);
        char w[100];

        if (!j) continue;
        snprintf(w, sizeof w, "%s cell %s", where, names[c]);
        if (!parseCatchList(host, env, j, &cell[c], err, cap, w)) return 0;
    }
    return 1;
}

static int parseFishing(const SmHost *host, const StageCodeEnv *env, const ExtJson *j, SmFile *f, char *err, size_t cap) {
    static const char *const top[] = {"default", "slots"};
    static const char *const cellKeys[] = {"1", "2"};
    static const char *const entryKeys[] = {"index", "key", "1", "2"};
    const ExtJson *d, *list;
    int i;

    if (j->t != 2 || hasUnknownKey(j, top, 2, err, cap, "\"fishing\"")) {
        if (j->t != 2) fail(err, cap, "\"fishing\" must be an object with \"default\" and \"slots\"");
        return 0;
    }
    if ((d = extJsonGet(j, "default")) != NULL) {
        if (d->t != 2 || hasUnknownKey(d, cellKeys, 2, err, cap, "fishing default")) {
            if (d->t != 2) fail(err, cap, "fishing default must be an object with the cells \"1\" and \"2\"");
            return 0;
        }
        if (!parseCells(host, env, d, f->fishDef, err, cap, "fishing default")) return 0;
    }
    if ((list = extJsonGet(j, "slots")) != NULL) {
        if (list->t != 4) {
            fail(err, cap, "\"fishing\".\"slots\" must be a list");
            return 0;
        }
        for (i = 0; i < list->n; i++) {
            const ExtJson *o = &list->v[i], *k, *ix;
            SmFishEdit *e;
            char w[60];

            snprintf(w, sizeof w, "fishing slot %d", i);
            if (o->t != 2 || hasUnknownKey(o, entryKeys, 4, err, cap, w)) {
                if (o->t != 2) fail(err, cap, "%s must be an object", w);
                return 0;
            }
            k = extJsonGet(o, "key");
            ix = extJsonGet(o, "index");
            if ((k != NULL) == (ix != NULL)) {
                fail(err, cap, "%s needs exactly one of \"index\" (the gauge slot's place in the stage's table) and \"key\" (a slot a patch added)", w);
                return 0;
            }
            GROW(f->fishEdits, f->nfishEdits);
            e = &f->fishEdits[f->nfishEdits++];
            e->ref.index = -1;
            if (ix) {
                if (ix->t != 0 || !extWhole(ix->num, 0, 1023)) {
                    fail(err, cap, "%s: \"index\" must be a whole number from 0 to 1023", w);
                    return 0;
                }
                e->ref.index = (int)ix->num;
            } else {
                if (k->t != 1 || !k->str || !readKey(k->str, f->mod, &e->ref.key, err, cap, w)) {
                    if (k->t != 1) fail(err, cap, "%s: \"key\" must be a string", w);
                    return 0;
                }
            }
            if (!parseCells(host, env, o, e->cell, err, cap, w)) return 0;
            if (!e->cell[0].set && !e->cell[1].set) {
                fail(err, cap, "%s gives no result (cells \"1\" and \"2\")", w);
                return 0;
            }
        }
    }
    return 1;
}

#define SM_SPOTS_MAX 64
static int parseHiddenSpots(const SmHost *host, const StageCodeEnv *env, const ExtJson *j, SmFile *f, char *err, size_t cap) {
    static const char *const top[] = {"add", "prize"};
    static const char *const pk[] = {"common", "rare"};
    static const char *const sk[] = {"x", "y"};
    const ExtJson *list, *p;
    int i;

    if (j->t != 2 || hasUnknownKey(j, top, 2, err, cap, "\"hiddenSpots\"")) {
        if (j->t != 2) fail(err, cap, "\"hiddenSpots\" must be an object with \"add\" and \"prize\"");
        return 0;
    }
    if ((list = extJsonGet(j, "add")) != NULL) {
        if (list->t != 4 || list->n > SM_SPOTS_MAX) {
            fail(err, cap, "\"hiddenSpots\".\"add\" must be a list of at most %d spots {\"x\", \"y\"}", SM_SPOTS_MAX);
            return 0;
        }
        for (i = 0; i < list->n; i++) {
            SdSpot sp;
            int hx, hy;
            char w[60];

            snprintf(w, sizeof w, "hidden spot add %d", i);
            if (list->v[i].t != 2 || hasUnknownKey(&list->v[i], sk, 2, err, cap, w)) {
                if (list->v[i].t != 2) fail(err, cap, "%s must be an object {\"x\", \"y\"}", w);
                return 0;
            }
            if (!getInt(&list->v[i], "x", 0, 32767, &sp.x, &hx, err, cap, w) || !getInt(&list->v[i], "y", 1, 32767, &sp.y, &hy, err, cap, w)) return 0;
            if (!hx || !hy) {
                fail(err, cap, "%s needs \"x\" and \"y\" (map pixels; y 1 or more: the object table ends at y 0)", w);
                return 0;
            }
            GROW(f->spots, f->nspots);
            f->spots[f->nspots++] = sp;
        }
    }
    if ((p = extJsonGet(j, "prize")) != NULL) {
        int c;

        if (p->t != 2 || hasUnknownKey(p, pk, 2, err, cap, "hidden spots prize")) {
            if (p->t != 2) fail(err, cap, "\"prize\" must be an object with \"common\" and \"rare\"");
            return 0;
        }
        for (c = 0; c < 2; c++) {
            const ExtJson *r = extJsonGet(p, pk[c]);
            char w[60];

            if (!r) continue;
            snprintf(w, sizeof w, "hidden spots prize %s", pk[c]);
            if (!parseCatchList(host, env, r, &f->prize[c], err, cap, w)) return 0;
        }
    }
    return 1;
}

static int parsePatch(const SmHost *host, const StageCodeEnv *env, const char *text, size_t len, int stage, SmFile *f, char *err, size_t cap) {
    const int isNew = f->isNew;
    ExtJson root;
    const ExtJson *j, *list;
    int i, pass, ok = 0;
    static const char *const top[] = {"$schema", "format", "about", "notes", "actors", "slots", "events", "battles", "fishing", "hiddenSpots"};
    static const char *const kinds[] = {"patch", "add", "remove"};
    /* a stage a mod adds (new-stages T5) has the header members of its own and nothing to patch or remove: only "add" */
    static const char *const topNew[] = {"$schema", "format", "about", "notes", "fallback", "start", "textFile", "soundBank", "music", "spriteColor", "banner", "actors", "slots", "events", "battles", "fishing", "hiddenSpots"};
    static const char *const addOnly[] = {"add"};
    const char *const *kindsOf = isNew ? addOnly : kinds;
    const int nKinds = isNew ? 1 : 3;

    if (extJsonParse((const uint8_t *)text, len, &root)) {
        fail(err, cap, "not a JSON object");
        return 0;
    }
    j = extJsonGet(&root, "format");
    if (!j || j->t != 1 || !j->str || strcmp(j->str, isNew ? SM_NEW_FORMAT : SM_FORMAT)) {
        fail(err, cap, "\"format\" must be \"%s\"", isNew ? SM_NEW_FORMAT : SM_FORMAT);
        goto done;
    }
    if (isNew ? hasUnknownKey(&root, topNew, (int)(sizeof topNew / sizeof topNew[0]), err, cap, "the file")
              : hasUnknownKey(&root, top, (int)(sizeof top / sizeof top[0]), err, cap, "the file")) goto done;
    if (!isNew && host && host->stageExists && !host->stageExists(stage)) {
        fail(err, cap, "there is no stage %d in this build", stage);
        goto done;
    }
    j = extJsonGet(&root, "actors");
    if (j) {
        if (j->t != 2) {
            fail(err, cap, "\"actors\" must be an object with \"patch\", \"add\" and \"remove\"");
            goto done;
        }
        if (hasUnknownKey(j, kindsOf, nKinds, err, cap, "\"actors\"")) goto done;
        if ((list = extJsonGet(j, "patch")) != NULL) {
            if (list->t != 4) {
                fail(err, cap, "\"actors\".\"patch\" must be a list");
                goto done;
            }
            for (i = 0; i < list->n; i++) {
                char w[60];

                snprintf(w, sizeof w, "patch %d", i);
                if (!parseEdit(env, &list->v[i], f, err, cap, w)) goto done;
            }
        }
        if ((list = extJsonGet(j, "add")) != NULL) {
            if (list->t != 4) {
                fail(err, cap, "\"actors\".\"add\" must be a list");
                goto done;
            }
            for (i = 0; i < list->n; i++) {
                char w[60];

                snprintf(w, sizeof w, "add %d", i);
                if (!parseAdd(env, &list->v[i], f, err, cap, w)) goto done;
            }
        }
        if ((list = extJsonGet(j, "remove")) != NULL) {
            if (list->t != 4) {
                fail(err, cap, "\"actors\".\"remove\" must be a list");
                goto done;
            }
            for (i = 0; i < list->n; i++) {
                char w[60];

                snprintf(w, sizeof w, "remove %d", i);
                if (!parseRemove(&list->v[i], f, err, cap, w)) goto done;
            }
        }
    }
    if ((j = extJsonGet(&root, "slots")) != NULL) {
        static const char *const sk[] = {"patch", "add", "remove"};

        if (j->t != 2 || (isNew ? hasUnknownKey(j, addOnly, 1, err, cap, "\"slots\"") : hasUnknownKey(j, sk, 3, err, cap, "\"slots\""))) {
            if (j->t != 2) fail(err, cap, "\"slots\" must be an object with \"patch\", \"add\" and \"remove\"");
            goto done;
        }
        for (pass = 0; pass < 3; pass++) {
            list = extJsonGet(j, sk[pass]);
            if (!list) continue;
            if (list->t != 4) {
                fail(err, cap, "\"slots\".\"%s\" must be a list", sk[pass]);
                goto done;
            }
            for (i = 0; i < list->n; i++) {
                char w[60];

                snprintf(w, sizeof w, "slot %s %d", sk[pass], i);
                if (pass == 2 ? !parseRef(&list->v[i], f, &f->srems, &f->nsrems, err, cap, w) : !parseSlot(host, env, &list->v[i], f, pass == 1, err, cap, w)) goto done;
            }
        }
    }
    if ((j = extJsonGet(&root, "events")) != NULL) {
        static const char *const ek[] = {"patch", "add"};

        if (j->t != 2 || (isNew ? hasUnknownKey(j, addOnly, 1, err, cap, "\"events\"") : hasUnknownKey(j, ek, 2, err, cap, "\"events\""))) {
            if (j->t != 2) fail(err, cap, "\"events\" must be an object with \"patch\" and \"add\"");
            goto done;
        }
        for (pass = 0; pass < 2; pass++) {
            list = extJsonGet(j, ek[pass]);
            if (!list) continue;
            if (list->t != 4) {
                fail(err, cap, "\"events\".\"%s\" must be a list", ek[pass]);
                goto done;
            }
            for (i = 0; i < list->n; i++) {
                char w[60];

                snprintf(w, sizeof w, "event %s %d", ek[pass], i);
                if (!parseEvent(env, &list->v[i], f, pass == 1, err, cap, w)) goto done;
            }
        }
    }
    if ((j = extJsonGet(&root, "battles")) != NULL) {
        static const char *const bk[] = {"patch", "add"};

        if (j->t != 2 || (isNew ? hasUnknownKey(j, addOnly, 1, err, cap, "\"battles\"") : hasUnknownKey(j, bk, 2, err, cap, "\"battles\""))) {
            if (j->t != 2) fail(err, cap, "\"battles\" must be an object with \"patch\" and \"add\"");
            goto done;
        }
        for (pass = 0; pass < 2; pass++) {
            list = extJsonGet(j, bk[pass]);
            if (!list) continue;
            if (list->t != 4) {
                fail(err, cap, "\"battles\".\"%s\" must be a list", bk[pass]);
                goto done;
            }
            for (i = 0; i < list->n; i++) {
                char w[60];

                snprintf(w, sizeof w, "battles %s %d", bk[pass], i);
                if (!parseBattle(host, &list->v[i], f, pass == 1, err, cap, w)) goto done;
            }
        }
    }
    if ((j = extJsonGet(&root, "fishing")) != NULL && !parseFishing(host, env, j, f, err, cap)) goto done;
    if ((j = extJsonGet(&root, "hiddenSpots")) != NULL && !parseHiddenSpots(host, env, j, f, err, cap)) goto done;
    ok = 1;
done:
    extJsonFree(&root);
    return ok;
}

/* "s330" (any case) -> the stage number 330, else -1 */
static int stageOfFolder(const char *name) {
    int i, v = 0;

    if (strlen(name) != 4 || tolower((unsigned char)name[0]) != 's') return -1;
    for (i = 1; i < 4; i++) {
        if (!isdigit((unsigned char)name[i])) return -1;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

/* ---- stages a mod adds (new-stages T5) ---- */

static void dropNewFiles(void) {
    int i, k = 0;

    for (i = 0; i < sFileCount; i++) {
        if (sFiles[i].isNew) {
            fileFree(&sFiles[i]);
        } else {
            sFiles[k++] = sFiles[i];
        }
    }
    sFileCount = k;
}

/* One pass over every mods/<mod>/stages/<name>/stage.json whose stage the host knows. Returns how many stages it refused (the host was told: the names
   they had stop resolving, so another pass reads the files that named them again). */
static int newStagesPass(const SmHost *host, const StageCodeEnv *env) {
    int m, refused = 0;

    for (m = 0; m < modsCount(); m++) {
        char dirPath[1400];
        DIR *d;
        struct dirent *e;

        if (!assetModDir(m, NULL, NS_DIR, dirPath, sizeof dirPath)) continue;
        d = opendir(dirPath);
        if (!d) continue;
        while ((e = readdir(d)) != NULL) {
            char path[1700], key[160], err[300];
            uint8_t *data = NULL;
            size_t len = 0;
            int number;
            SmFile *grown, file;
            SmNewInfo info;
            int i;

            if (e->d_name[0] == '.' || stageOfFolder(e->d_name) >= 0 || !extValidKey(e->d_name)) continue; /* s<nnn> is an original stage */
            snprintf(key, sizeof key, "%s:%s", modsNameOf(m), e->d_name);
            number = host && host->stageByName ? host->stageByName(key) : -1;
            if (number < 0) continue; /* not registered: the scan said why, or it is not a stage folder */
            snprintf(path, sizeof path, "%s/%s/stage.json", dirPath, e->d_name);
            memset(&file, 0, sizeof file);
            err[0] = '\0';
            if (tdReadFile(path, &data, &len)) {
                plogLine("stages", "%s rejected: the file could not be read\n", path);
                if (host->stageRefused) host->stageRefused(key);
                refused++;
                continue;
            }
            file.mod = smDup(modsNameOf(m));
            file.path = smDup(path);
            file.stage = number;
            file.isNew = 1;
            if (!file.mod || !file.path || !parsePatch(host, env, (const char *)data, len, number, &file, err, sizeof err)) {
                plogLine("stages", "%s rejected: %s\n", path, err[0] ? err : "out of memory");
                fileFree(&file);
                free(data);
                if (host->stageRefused) host->stageRefused(key);
                refused++;
                continue;
            }
            free(data);
            memset(&info, 0, sizeof info);
            for (i = 0; i < file.nsedits; i++) info.slots += file.sedits[i].isAdd;
            for (i = 0; i < file.nbedits; i++) info.battleAreas += file.bedits[i].isAdd;
            if (host->newCheck && host->newCheck(key, &info, err, sizeof err)) {
                plogLine("stages", "%s rejected: %s\n", path, err[0] ? err : "the stage's map and its sections do not agree");
                fileFree(&file);
                if (host->stageRefused) host->stageRefused(key);
                refused++;
                continue;
            }
            grown = (SmFile *)realloc(sFiles, (size_t)(sFileCount + 1) * sizeof *sFiles);
            if (!grown) {
                fileFree(&file);
                continue;
            }
            sFiles = grown;
            sFiles[sFileCount++] = file;
        }
        closedir(d);
    }
    return refused;
}

/* ---- the banner texts of the new stages ---- */

/* The text files the banner reads, counted without the language like a stage's textFile (file = LANGUAGE + n, include/engine/file.h: TEXT_FILE(file) is
   LANGUAGE + file - 1): STAREA 0xAA, the big line, and STNAME 0xB8, the place. */
#define SM_BANNER_AREA_FILE (0xAA - 1)
#define SM_BANNER_PLACE_FILE (0xB8 - 1)

/* the entry of text `name` appended to file k: first + slot, 0 when the name is not a text with an English line or the host cannot extend the file */
static int bannerEntry(int k, const char *name, const char *stage, const char *what) {
    int id = textFind(name), first, slot, added = 0;

    if (id < 0 || !sTexts[id].text[0]) {
        plogLine("stages", "stage %s: the banner's %s text \"%s\" is not a line of any mod's text/eng/stages.json: no banner\n", stage, what, name);
        return 0;
    }
    first = k >= 0 && sHost.textFirst ? sHost.textFirst(k) : -1;
    slot = first >= 0 ? fileSlotOf(k, id, &added) : -1;
    if (slot < 0) {
        plogLine("stages", "stage %s: the banner's %s text cannot be appended (the host has no text file the port can extend): no banner\n", stage, what);
        return 0;
    }
    if (added && sHost.textChanged) sHost.textChanged(k);
    return first + slot;
}

/* Gives each new stage with a "banner" the entries of its two lines in the area-name and place-name files, once, when the stages are loaded (before the
   game reads those files, so no cached copy goes stale); the numbers do not change afterwards. A stage whose texts fail simply shows no banner. */
static void bannerAssign(void) {
    int i, ka = tdStageTextK(SM_BANNER_AREA_FILE), kp = tdStageTextK(SM_BANNER_PLACE_FILE);

    for (i = 0; i < msCount(); i++) {
        MsStage *ms = msAt(i);
        int a, p;

        if (!ms || ms->refused || !ms->bannerArea[0]) continue;
        ms->bannerAreaEntry = ms->bannerPlaceEntry = 0;
        a = bannerEntry(ka, ms->bannerArea, ms->key, "area");
        p = a ? bannerEntry(kp, ms->bannerPlace, ms->key, "place") : 0;
        if (a && p) {
            ms->bannerAreaEntry = a;
            ms->bannerPlaceEntry = p;
            PLOG("stages", PLOG_TRACE, "stage %s: banner area \"%s\" is entry %d of file %d, place \"%s\" entry %d of file %d\n", ms->key, ms->bannerArea, a, ka, ms->bannerPlace, p, kp);
        }
    }
}

static void loadNewStages(const SmHost *host, const StageCodeEnv *env) {
    int round;

    if (!host || !host->stageByName) return;
    /* A refused stage takes its name away, so a stage whose exit named it is refused too in the next pass; each pass refuses at least one or ends it. */
    for (round = 0; round < 260; round++) {
        dropNewFiles();
        if (newStagesPass(host, env) == 0) break;
    }
}

/* "s330/stage.json": the stage patch inside the mod's stages/ folder */
static const char *patchRel(const char *folder, char *out, size_t cap) {
    snprintf(out, cap, "%s/stage.json", folder);
    return out;
}

int smLoad(const SmHost *host) {
    StageCodeEnv env;
    int m;

    smUnload();
    memset(&sHost, 0, sizeof sHost);
    if (host) sHost = *host;
    smCodeEnv(&env, host ? host->env : NULL);
    textsLoad(); /* the names a patch's talks use, from every mod, before any patch is read */
    spritesLoad(); /* the characters a patch's "sprite" names (S9) */
    for (m = 0; m < modsCount(); m++) {
        char dirPath[1400];
        DIR *d;
        struct dirent *e;

        if (!assetModDir(m, NULL, "stages", dirPath, sizeof dirPath)) continue;
        d = opendir(dirPath);
        if (!d) continue;
        for (;;) {
            char path[1700], err[300], folderRel[40];
            uint8_t *data = NULL;
            size_t len = 0;
            int stage;
            SmFile *grown, file;

            e = readdir(d);
            if (!e) break;
            stage = stageOfFolder(e->d_name);
            if (stage < 0 || !assetModFile(m, "stages", patchRel(e->d_name, folderRel, sizeof folderRel), path, sizeof path)) continue; /* mods/<mod>/stages/s<nnn>/stage.json */
            if (tdReadFile(path, &data, &len)) {
                plogLine("stages", "%s rejected: the file could not be read\n", path);
                continue;
            }
            memset(&file, 0, sizeof file);
            file.mod = smDup(modsNameOf(m));
            file.path = smDup(path);
            file.stage = stage;
            err[0] = '\0';
            if (!file.mod || !file.path || !parsePatch(host, &env, (const char *)data, len, stage, &file, err, sizeof err)) {
                plogLine("stages", "%s rejected: %s\n", path, err[0] ? err : "out of memory");
                fileFree(&file);
                free(data);
                continue;
            }
            free(data);
            grown = (SmFile *)realloc(sFiles, (size_t)(sFileCount + 1) * sizeof *sFiles);
            if (!grown) {
                fileFree(&file);
                continue;
            }
            sFiles = grown;
            sFiles[sFileCount++] = file;
        }
        closedir(d);
    }
    loadNewStages(host, &env);
    bannerAssign();
    if (sFileCount > 0) PLOG("stages", PLOG_MODS, "%d stage patch file(s) kept\n", sFileCount);
    return sFileCount;
}

/* ---- applying ---- */

typedef struct Work {
    SdActor a;
    int orig;     /* its place in the setup's list, -1 for an added one */
    char *key;    /* an added one's "<mod>:<key>" */
    int removed;
    int touched;
    const char *setBy[F_COUNT]; /* the mod whose patch last set the field */
} Work;

static int findWork(const Work *w, int n, int index, const char *key) {
    int i;

    for (i = 0; i < n; i++) {
        if (key ? (w[i].key && !strcmp(w[i].key, key)) : w[i].orig == index) return i;
    }
    return -1;
}

static void describe(char *out, size_t cap, int index, const char *key) {
    if (key) snprintf(out, cap, "actor \"%s\"", key);
    else snprintf(out, cap, "actor %d", index);
}

static void skipLog(SmReport *rep, const SmFile *f, const char *what, const char *why) {
    rep->skipped++;
    plogLine("stages", "%s: stage %d %s skipped: %s\n", f->mod, f->stage, what, why);
}

static int imageOk(const SdStage *s, int image) {
    return image == SD_IMAGE_AUTO || (image >= 2 && image < s->nimages + 2);
}

static void applyEdit(const SdStage *s, const SmFile *f, const SmEdit *e, Work *w, SmReport *rep) {
    char who[100], why[160];
    int i, changed = 0, bad = 0;

    describe(who, sizeof who, e->index, e->key);
    for (i = 0; i < F_COUNT; i++) {
        if (!(e->mask & (1u << i))) continue;
        if (i == F_IMAGE && !imageOk(s, e->v[1])) {
            snprintf(why, sizeof why, "image %d: this stage has the actor images 2 to %d", e->v[1], s->nimages + 1);
            skipLog(rep, f, who, why);
            continue;
        }
        if (w->setBy[i] && strcmp(w->setBy[i], f->mod)) {
            rep->conflicts++;
            plogLine("stages", "%s: stage %d %s: \"%s\" overrides the value %s set\n", f->mod, f->stage, who, FIELD_NAMES[i], w->setBy[i]);
        }
        w->setBy[i] = f->mod;
        changed = 1;
        switch (i) {
        case F_CHARACTER: w->a.character = e->v[0]; break;
        case F_IMAGE: w->a.image = e->v[1]; break;
        case F_X: w->a.x = e->v[2]; break;
        case F_Y: w->a.y = e->v[3]; break;
        case F_DIR: w->a.dir = e->v[4]; break;
        case F_COND: {
            SdList fresh;

            if (listClone(&fresh, &e->cond)) {
                listFree(&fresh);
                bad = 1;
                break;
            }
            listFree(&w->a.cond);
            w->a.cond = fresh;
            break;
        }
        case F_TALKS: {
            int b = 0;
            SdTalk *fresh = talksClone(e->talks, e->ntalks, &b);

            if (b) {
                talksFree(fresh, e->ntalks);
                bad = 1;
                break;
            }
            talksFree(w->a.talks, w->a.ntalks);
            w->a.talks = fresh;
            w->a.ntalks = fresh ? e->ntalks : 0;
            break;
        }
        }
    }
    if (e->nprepend > 0) {
        int b = 0;
        SdTalk *front = talksClone(e->prepend, e->nprepend, &b);
        SdTalk *rest = talksClone(w->a.talks, w->a.ntalks, &b);
        SdTalk *all = NULL;

        if (!b) all = (SdTalk *)malloc((size_t)(e->nprepend + w->a.ntalks) * sizeof *all);
        if (b || !all) {
            talksFree(front, e->nprepend);
            talksFree(rest, w->a.ntalks);
            bad = 1;
        } else {
            memcpy(all, front, (size_t)e->nprepend * sizeof *all);
            if (w->a.ntalks > 0) memcpy(all + e->nprepend, rest, (size_t)w->a.ntalks * sizeof *all);
            free(front);
            free(rest);
            talksFree(w->a.talks, w->a.ntalks);
            w->a.talks = all;
            w->a.ntalks += e->nprepend;
            changed = 1;
        }
    }
    if (bad) skipLog(rep, f, who, "out of memory");
    if (changed && !w->touched) {
        w->touched = 1;
        rep->patched++;
    }
}

/* The named texts the events of this visit use: filled in by resolveTexts once the stage knows the entries */
typedef struct Pend {
    int event;  /* index in s->events */
    int kind;   /* 0: a script word, 1: the event's text entry */
    int off;
    int name;   /* registry id of the text */
} Pend;

static Pend *sPend;
static int sPendN;

/* ---- named texts of a stage ---- */

static void useTalks(int k, const SdTalk *t, int n, int *added) {
    int i;

    for (i = 0; i < n; i++) {
        if (t[i].entry >= SM_TEXT_BASE && t[i].entry - SM_TEXT_BASE < sTextCount) fileSlotOf(k, t[i].entry - SM_TEXT_BASE, added);
    }
}

/* Gives every named text of the stage's patches an entry appended to the stage's text file (all of them, used or not at this progress, so the
   appended entries never change from one visit to the next) and turns the named talks of the final list into those entry numbers. */
static void resolveTexts(int stage, SdStage *s, SmReport *rep) {
    int k = tdStageTextK(s->textFile), first = -1, fi, i, j, added = 0;

    if (k >= 0 && sHost.textFirst) first = sHost.textFirst(k);
    if (first >= 0) {
        for (fi = 0; fi < sFileCount; fi++) {
            const SmFile *f = &sFiles[fi];

            if (f->stage != stage) continue;
            for (i = 0; i < f->nedits; i++) {
                useTalks(k, f->edits[i].talks, f->edits[i].ntalks, &added);
                useTalks(k, f->edits[i].prepend, f->edits[i].nprepend, &added);
            }
            for (i = 0; i < f->nadds; i++) useTalks(k, f->adds[i].actor.talks, f->adds[i].actor.ntalks, &added);
            for (i = 0; i < f->needits; i++) {
                const SmEventEdit *e = &f->eedits[i];
                int q;

                for (q = 0; q < e->nnames; q++) fileSlotOf(k, e->nameId[q], &added);
            }
        }
        if (added && sHost.textChanged) sHost.textChanged(k);
    }
    for (i = 0; i < sPendN; i++) { /* the events' named texts */
        const Pend *pd = &sPend[i];
        SdEvent *ev = &s->events[pd->event];
        int added2 = 0, slot = first >= 0 ? fileSlotOf(k, pd->name, &added2) : -1, entry = first + slot;

        if (slot < 0 || entry > 32767 || entry < 0) {
            plogLine("stages", "stage %d: the named text \"%s\" has no entry for an event (the stage has no text file the port can extend): it says entry 0\n", stage,
                     sTexts[pd->name].name);
            entry = 0;
        } else {
            PLOG("stages", PLOG_TRACE, "stage %d: event text \"%s\" is entry %d of text file %d\n", stage, sTexts[pd->name].name, entry, k);
            rep->texts++;
        }
        ev->script[pd->off + 2] = (int16_t)entry;
    }
    for (i = 0; i < s->nactors; i++) {
        for (j = 0; j < s->actors[i].ntalks; j++) {
            SdTalk *t = &s->actors[i].talks[j];
            int added2 = 0, slot;

            if (t->entry < SM_TEXT_BASE) continue;
            slot = first >= 0 ? fileSlotOf(k, t->entry - SM_TEXT_BASE, &added2) : -1;
            if (slot < 0) {
                plogLine("stages", "stage %d: the named text \"%s\" has no entry (the stage has no text file the port can extend): its talk says entry 0\n", stage,
                         sTexts[t->entry - SM_TEXT_BASE].name);
                t->entry = 0;
            } else {
                PLOG("stages", PLOG_TRACE, "stage %d: text \"%s\" is entry %d of text file %d\n", stage, sTexts[t->entry - SM_TEXT_BASE].name, first + slot, k);
                t->entry = first + slot;
                rep->texts++;
            }
        }
    }
}

/* S9: each actor with image SD_IMAGE_AUTO gets the extra slot of its character: the characters in order of first use, numbered from SD_IMAGE_EXTRA_FIRST.
   0 on a failure to allocate (the list is then left without extras: such actors keep AUTO and the host drops them). */
static int numberExtras(SdStage *s) {
    int i, k;

    free(s->extra);
    s->extra = NULL;
    s->nextra = 0;
    for (i = 0; i < s->nactors; i++) {
        if (s->actors[i].image != SD_IMAGE_AUTO) continue;
        for (k = 0; k < s->nextra; k++) {
            if (s->extra[k] == s->actors[i].character) break;
        }
        if (k == s->nextra) {
            int *grown = (int *)realloc(s->extra, (size_t)(s->nextra + 1) * sizeof *grown);

            if (!grown) return 0;
            s->extra = grown;
            s->extra[s->nextra++] = s->actors[i].character;
        }
        s->actors[i].image = SD_IMAGE_EXTRA_FIRST + k;
    }
    return 1;
}

/* the characters of a stage: the patches merged into s->actors (replaced); 1 when something changed. The named texts are the caller's. */
static int applyActors(int stage, SdStage *s, SmReport *rep) {
    Work *w;
    int n, cap, i, fi, any = 0, changed;
    SdActor *out;

    n = s->nactors;
    cap = n;
    for (fi = 0; fi < sFileCount; fi++) {
        if (sFiles[fi].stage == stage) cap += sFiles[fi].nadds;
    }
    w = (Work *)calloc((size_t)(cap > 0 ? cap : 1), sizeof *w);
    out = (SdActor *)calloc((size_t)(cap > 0 ? cap : 1), sizeof *out);
    if (!w || !out) { /* nothing was touched yet: the stage keeps its own actors */
        free(w);
        free(out);
        return 0;
    }
    for (i = 0; i < n; i++) {
        w[i].a = s->actors[i];
        w[i].orig = i;
    }
    for (fi = 0; fi < sFileCount; fi++) {
        const SmFile *f = &sFiles[fi];

        if (f->stage != stage) continue;
        any = 1;
        for (i = 0; i < f->nedits; i++) {
            const SmEdit *e = &f->edits[i];
            int at = findWork(w, n, e->index, e->key);
            char who[100];

            describe(who, sizeof who, e->index, e->key);
            if (at < 0) {
                skipLog(rep, f, who, e->key ? "no actor has this key" : "the stage's list has no actor here at this story progress");
            } else if (w[at].removed) {
                skipLog(rep, f, who, "an earlier patch removed it");
            } else {
                applyEdit(s, f, e, &w[at], rep);
            }
        }
        for (i = 0; i < f->nadds; i++) {
            const SmAdd *a = &f->adds[i];
            char who[100];

            describe(who, sizeof who, -1, a->key);
            if (findWork(w, n, -1, a->key) >= 0) {
                skipLog(rep, f, who, "an actor with this key is already there");
            } else if (!imageOk(s, a->actor.image)) {
                char why[120];

                snprintf(why, sizeof why, "image %d: this stage has the actor images 2 to %d", a->actor.image, s->nimages + 1);
                skipLog(rep, f, who, why);
            } else if (actorClone(&w[n].a, &a->actor) != 0) {
                actorFree(&w[n].a);
                skipLog(rep, f, who, "out of memory");
            } else {
                w[n].orig = -1;
                w[n].key = a->key;
                n++;
                rep->added++;
            }
        }
        for (i = 0; i < f->nrems; i++) {
            const SmRef *r = &f->rems[i];
            int at = findWork(w, n, r->index, r->key);
            char who[100];

            describe(who, sizeof who, r->index, r->key);
            if (at < 0) {
                skipLog(rep, f, who, r->key ? "no actor has this key" : "the stage's list has no actor here at this story progress");
            } else if (!w[at].removed) {
                w[at].removed = 1;
                rep->removed++;
            }
        }
    }
    changed = rep->patched + rep->added + rep->removed > 0;
    /* the result: the survivors in order, the added ones last */
    {
        int k = 0;

        for (i = 0; i < n; i++) {
            if (w[i].removed) actorFree(&w[i].a);
            else out[k++] = w[i].a;
        }
        free(s->actors);
        s->actors = out;
        s->nactors = k;
    }
    free(w);
    if (any && changed && !numberExtras(s)) {
        plogLine("stages", "stage %d: out of memory numbering the extra actor images\n", stage);
    }
    return any && changed;
}

int smApplyActors(int stage, SdStage *s, SmReport *rep) {
    SmReport local;
    int changed;

    if (!rep) rep = &local;
    memset(rep, 0, sizeof *rep);
    if (!smHasPatches(stage)) return 0;
    sPendN = 0;
    changed = applyActors(stage, s, rep);
    if (changed && sTextCount > 0) resolveTexts(stage, s, rep);
    return changed;
}
/* ================= slots, events and battles of a stage (S8, S10) ================= */

typedef struct SlotWork {
    SdSlot a;
    int orig; /* its place in the setup's table, -1 for an added one */
    char *key;
    int removed;
    int touched;
    const char *setBy[SL_COUNT];
} SlotWork;

static int findSlot(const SlotWork *w, int n, int index, const char *key) {
    int i;

    for (i = 0; i < n; i++) {
        if (key ? (w[i].key && !strcmp(w[i].key, key)) : w[i].orig == index) return i;
    }
    return -1;
}

static void describeSlot(char *out, size_t cap, int index, const char *key) {
    if (key) snprintf(out, cap, "slot \"%s\"", key);
    else snprintf(out, cap, "slot %d", index);
}

/* one edit's fields into a work slot; the exit's "stage" becomes the stage's game mode here. 0 when it could not apply (logged) */
static int slotSet(const SmFile *f, const SmSlotEdit *e, SlotWork *w, const char *who, SmReport *rep) {
    int i, changed = 0, bad = 0;

    for (i = 0; i < SL_COUNT; i++) {
        if (!(e->mask & (1u << i))) continue;
        if (i == SL_ARG && e->stage) {
            int mode = sHost.stageMode ? sHost.stageMode(e->stage) : 0;

            if (mode <= 0) {
                char why[100];

                snprintf(why, sizeof why, "stage %d has no game mode the port can leave for", e->stage);
                skipLog(rep, f, who, why);
                continue;
            }
            w->a.arg = mode;
        }
        if (!e->isAdd && w->setBy[i] && strcmp(w->setBy[i], f->mod)) {
            rep->conflicts++;
            plogLine("stages", "%s: stage %d %s: \"%s\" overrides the value %s set\n", f->mod, f->stage, who, SLOT_FIELDS[i], w->setBy[i]);
        }
        w->setBy[i] = f->mod;
        changed = 1;
        switch (i) {
        case SL_TYPE: w->a.type = e->v[0]; break;
        case SL_ARG:
            if (!e->stage) w->a.arg = e->v[1];
            break;
        case SL_X: w->a.x = e->v[2]; break;
        case SL_Y: w->a.y = e->v[3]; break;
        case SL_DIR: w->a.dir = e->v[4]; break;
        case SL_HIDE: w->a.hideAnim = e->v[5]; break;
        case SL_PLACE: w->a.place = e->v[6]; break;
        case SL_PLACEARG: w->a.placeArg = e->v[7]; break;
        case SL_COND: {
            SdList fresh;

            if (listClone(&fresh, &e->cond)) {
                listFree(&fresh);
                bad = 1;
                break;
            }
            listFree(&w->a.cond);
            w->a.cond = fresh;
            break;
        }
        case SL_AREA:
            w->a.hasArea = 1;
            w->a.ax = e->ax;
            w->a.ay = e->ay;
            w->a.aw = e->aw;
            w->a.ah = e->ah;
            w->a.facing = e->facing;
            break;
        }
    }
    if (bad) skipLog(rep, f, who, "out of memory");
    if (changed && !w->touched) w->touched = 1;
    return changed;
}

/* the keys of the slots applySlots left, by final index (the pointers belong to the files' edits); read by applyCatches right after */
typedef struct SlotKey {
    const char *key;
    int index;
} SlotKey;
static SlotKey *sSlotKeys;
static int sSlotKeyCount;

static int applySlots(int stage, SdStage *s, SmReport *rep) {
    SlotWork *w;
    SdSlot *out;
    int n = s->nslots, cap = s->nslots, i, fi, k;

    for (fi = 0; fi < sFileCount; fi++) {
        if (sFiles[fi].stage == stage) cap += sFiles[fi].nsedits;
    }
    if (cap == n) {
        int any = 0;

        for (fi = 0; fi < sFileCount; fi++) any |= sFiles[fi].stage == stage && (sFiles[fi].nsedits || sFiles[fi].nsrems);
        if (!any) return 0;
    }
    w = (SlotWork *)calloc((size_t)(cap > 0 ? cap : 1), sizeof *w);
    out = (SdSlot *)calloc((size_t)(cap > 0 ? cap : 1), sizeof *out);
    if (!w || !out) {
        free(w);
        free(out);
        return 0;
    }
    for (i = 0; i < n; i++) {
        w[i].a = s->slots[i];
        w[i].a.facing = -1;
        w[i].orig = i;
    }
    for (fi = 0; fi < sFileCount; fi++) {
        const SmFile *f = &sFiles[fi];

        if (f->stage != stage) continue;
        for (i = 0; i < f->nsedits; i++) {
            const SmSlotEdit *e = &f->sedits[i];
            char who[100];

            describeSlot(who, sizeof who, e->index, e->key);
            if (!e->isAdd) {
                int at = findSlot(w, n, e->index, e->key);

                if (at < 0) skipLog(rep, f, who, e->key ? "no slot has this key" : "the stage's table has no slot here at this story progress");
                else if (w[at].removed) skipLog(rep, f, who, "an earlier patch removed it");
                else if (slotSet(f, e, &w[at], who, rep) && !(w[at].touched & 2)) {
                    w[at].touched |= 2;
                    rep->slotsPatched++;
                }
            } else if (findSlot(w, n, -1, e->key) >= 0) {
                skipLog(rep, f, who, "a slot with this key is already there");
            } else {
                SlotWork *nw = &w[n];

                nw->a.facing = -1;
                nw->orig = -1;
                nw->key = e->key;
                if (slotSet(f, e, nw, who, rep) && nw->a.type > 0) {
                    n++;
                    rep->slotsAdded++;
                } else {
                    listFree(&nw->a.cond);
                    memset(nw, 0, sizeof *nw);
                }
            }
        }
        for (i = 0; i < f->nsrems; i++) {
            const SmRef *r = &f->srems[i];
            int at = findSlot(w, n, r->index, r->key);
            char who[100];

            describeSlot(who, sizeof who, r->index, r->key);
            if (at < 0) {
                skipLog(rep, f, who, r->key ? "no slot has this key" : "the stage's table has no slot here at this story progress");
            } else if (!w[at].removed) {
                w[at].removed = 1;
                w[at].a.type = SM_SLOT_OFF; /* the map's cells name slots by index: it stays, doing nothing */
                w[at].a.hasArea = 0;
                listFree(&w[at].a.cond);
                rep->slotsRemoved++;
            }
        }
    }
    free(sSlotKeys);
    sSlotKeys = (SlotKey *)calloc((size_t)(n > 0 ? n : 1), sizeof *sSlotKeys);
    sSlotKeyCount = 0;
    for (k = 0, i = 0; i < n; i++) {
        /* a gauge slot a patch adds is the fishing spot of the Fishing Pole (dw3:item/5) unless it gives its own conditions: every gauge slot of the game
           is conditioned ITEM(0, 5) */
        if (w[i].orig < 0 && w[i].a.type == 7 && !w[i].a.cond.set && !w[i].removed) {
            w[i].a.cond.w = (uint16_t *)malloc(2 * sizeof *w[i].a.cond.w);
            if (w[i].a.cond.w) {
                w[i].a.cond.w[0] = 0x8005; /* ITEM(0, 5) */
                w[i].a.cond.w[1] = 1;
                w[i].a.cond.n = 2;
                w[i].a.cond.set = 1;
            }
        }
        if (w[i].key && sSlotKeys) {
            sSlotKeys[sSlotKeyCount].key = w[i].key;
            sSlotKeys[sSlotKeyCount++].index = k;
        }
        out[k++] = w[i].a;
    }
    free(s->slots);
    s->slots = out;
    s->nslots = k;
    free(w);
    return rep->slotsPatched + rep->slotsAdded + rep->slotsRemoved > 0;
}

static int pendAdd(int event, int kind, int off, int name) {
    void *g = realloc(sPend, (size_t)(sPendN + 1) * sizeof *sPend);

    if (!g) return -1;
    sPend = (Pend *)g;
    sPend[sPendN].event = event;
    sPend[sPendN].kind = kind;
    sPend[sPendN].off = off;
    sPend[sPendN].name = name;
    sPendN++;
    return 0;
}

static void pendDrop(int event, int kind) {
    int i, k = 0;

    for (i = 0; i < sPendN; i++) {
        if (sPend[i].event == event && sPend[i].kind == kind) continue;
        sPend[k++] = sPend[i];
    }
    sPendN = k;
}

static int findEvent(const SdEvent *ev, int n, int id) {
    int i;

    for (i = 0; i < n; i++) {
        if (ev[i].id == id) return i;
    }
    return -1;
}

static void eventPrevFree(SdEvent *e) {
    int i;

    for (i = 0; i < e->npreviews; i++) free(e->previews[i].text);
    free(e->previews);
    e->previews = NULL;
    e->npreviews = 0;
}

static int applyEvents(int stage, SdStage *s, SmReport *rep) {
    SdEvent *out;
    int n = s->nevents, cap = s->nevents, i, fi, any = 0;

    for (fi = 0; fi < sFileCount; fi++) {
        if (sFiles[fi].stage == stage) {
            cap += sFiles[fi].needits;
            any |= sFiles[fi].needits > 0;
        }
    }
    if (!any) return 0;
    out = (SdEvent *)calloc((size_t)(cap + 1), sizeof *out);
    if (!out) return 0;
    if (n > 0) memcpy(out, s->events, (size_t)n * sizeof *out);
    for (fi = 0; fi < sFileCount; fi++) {
        const SmFile *f = &sFiles[fi];

        if (f->stage != stage) continue;
        for (i = 0; i < f->needits; i++) {
            const SmEventEdit *e = &f->eedits[i];
            char who[100];
            int at, id = e->isAdd ? e->evId : (e->key ? e->evId : e->id);
            SdEvent *x;

            if (e->key) snprintf(who, sizeof who, "event \"%s\"", e->key);
            else snprintf(who, sizeof who, "event %d", e->id);
            at = findEvent(out, n, id);
            if (e->isAdd) {
                if (at >= 0) {
                    skipLog(rep, f, who, "an event with this id is already there");
                    continue;
                }
                x = &out[n];
                memset(x, 0, sizeof *x);
                x->id = id;
                x->isNew = 1;
                at = n++;
                rep->eventsAdded++;
            } else {
                if (at < 0) {
                    skipLog(rep, f, who, e->key ? "no event has this key" : "the stage's table has no event with this id at this story progress");
                    continue;
                }
                x = &out[at];
                rep->eventsPatched++;
            }
            if (e->hasScript) {
                int16_t *copy = (int16_t *)malloc((size_t)e->nscript * sizeof *copy);
                int k;

                if (!copy) {
                    skipLog(rep, f, who, "out of memory");
                    continue;
                }
                memcpy(copy, e->script, (size_t)e->nscript * sizeof *copy);
                free(x->script);
                x->script = copy;
                x->nscript = e->nscript;
                x->hasScript = 1;
                eventPrevFree(x); /* the previews described the script that is gone */
                pendDrop(at, 0);
                for (k = 0; k < e->nnames; k++) pendAdd(at, 0, e->nameOff[k], e->nameId[k]);
            }
            if (e->hasText) {
                x->hasText = 1;
                x->textFile = e->textFile >= 0 ? e->textFile : (s->textFile >= 0 ? s->textFile : 0);
                x->textEntry = e->textEntry;
            } else if (e->isAdd || e->nnames > 0) {
                /* the port's own text source: the script's talks name entries of the stage's text file, as the characters' talks do (the game's events
                   read a block of their own: src/field/field_mode/speech.c) */
                x->hasText = 1;
                x->textFile = s->textFile >= 0 ? s->textFile : 0;
                x->textEntry = SM_EVENT_TEXT_STAGE;
            }
        }
    }
    free(s->events);
    s->events = out;
    s->nevents = n;
    return rep->eventsPatched + rep->eventsAdded > 0;
}

static int applyBattles(int stage, SdStage *s, SmReport *rep) {
    int fi, i, k, any = 0;

    for (fi = 0; fi < sFileCount; fi++) {
        const SmFile *f = &sFiles[fi];

        if (f->stage != stage) continue;
        for (i = 0; i < f->nbedits; i++) {
            const SmBattleEdit *e = &f->bedits[i];
            char who[60];

            snprintf(who, sizeof who, "battles area %d", e->area);
            if (!s->battles.present) {
                skipLog(rep, f, who, "the stage has no battles at this story progress");
            } else if (e->isAdd) {
                if (e->area != s->battles.nareas + 1) {
                    char why[120];

                    snprintf(why, sizeof why, "an added area is the next one after the stage's last (%d), here area %d", s->battles.nareas, s->battles.nareas + 1);
                    skipLog(rep, f, who, why);
                } else {
                    SdArea *a = &s->battles.areas[s->battles.nareas++];

                    a->rate = e->v[3];
                    memcpy(a->b, e->b, sizeof a->b);
                    rep->areasAdded++;
                    any = 1;
                }
            } else if (e->area > s->battles.nareas) {
                char why[100];

                snprintf(why, sizeof why, "the stage has %d areas", s->battles.nareas);
                skipLog(rep, f, who, why);
            } else {
                SdArea *a = &s->battles.areas[e->area - 1];

                for (k = 0; k < BA_COUNT; k++) {
                    if (!(e->mask & (1u << k)) || k != BA_RATE) continue;
                    a->rate = e->v[3];
                }
                for (k = 0; k < 8; k++) {
                    if (!(e->slots & (1u << k))) continue;
                    if (e->mask & (1u << BA_ENCOUNTER)) a->b[k].encounter = e->v[0];
                    if (e->mask & (1u << BA_STAGE)) a->b[k].stage = e->v[1];
                    if (e->mask & (1u << BA_MUSIC)) a->b[k].music = e->v[2];
                }
                rep->battlesPatched++;
                any = 1;
            }
        }
    }
    return any;
}


/* ---- fishing and hidden spots: apply (T9) ---- */

static int catchClone(SdCatchList *dst, const SdCatchList *src) {
    SdCatchList fresh;

    memset(&fresh, 0, sizeof fresh);
    fresh.set = 1;
    fresh.n = src->n;
    if (src->n > 0) {
        fresh.v = (SdCatch *)malloc((size_t)src->n * sizeof *fresh.v);
        if (!fresh.v) return -1;
        memcpy(fresh.v, src->v, (size_t)src->n * sizeof *fresh.v);
    }
    sdCatchListFree(dst);
    *dst = fresh;
    return 0;
}

static int applyCatches(int stage, SdStage *s, SmReport *rep) {
    int fi, i, c, any = 0;
    const char *defBy[2] = {NULL, NULL}, *prizeBy[2] = {NULL, NULL};

    for (fi = 0; fi < sFileCount; fi++) {
        const SmFile *f = &sFiles[fi];

        if (f->stage != stage) continue;
        for (c = 0; c < 2; c++) {
            if (f->fishDef[c].set) {
                if (defBy[c] && strcmp(defBy[c], f->mod)) {
                    rep->conflicts++;
                    plogLine("stages", "%s: stage %d fishing default cell %d: overrides the list %s set\n", f->mod, stage, c + 1, defBy[c]);
                }
                if (catchClone(&s->fishDefault[c], &f->fishDef[c])) return any;
                defBy[c] = f->mod;
                rep->fishingSet++;
                any = 1;
            }
            if (f->prize[c].set) {
                if (prizeBy[c] && strcmp(prizeBy[c], f->mod)) {
                    rep->conflicts++;
                    plogLine("stages", "%s: stage %d hidden spots prize %s: overrides the list %s set\n", f->mod, stage, c ? "rare" : "common", prizeBy[c]);
                }
                if (catchClone(&s->prize[c], &f->prize[c])) return any;
                prizeBy[c] = f->mod;
                rep->prizesSet++;
                any = 1;
            }
        }
        for (i = 0; i < f->nfishEdits; i++) {
            const SmFishEdit *e = &f->fishEdits[i];
            char who[100];
            int slot = -1, k;
            SdFishSlot *fs = NULL;

            describeSlot(who, sizeof who, e->ref.index, e->ref.key);
            if (e->ref.key) {
                for (k = 0; k < sSlotKeyCount; k++) {
                    if (!strcmp(sSlotKeys[k].key, e->ref.key)) slot = sSlotKeys[k].index;
                }
            } else {
                slot = e->ref.index;
            }
            if (slot < 0 || slot >= s->nslots) {
                skipLog(rep, f, who, e->ref.key ? "fishing: no slot has this key" : "fishing: the stage's table has no slot here at this story progress");
                continue;
            }
            if (s->slots[slot].type != 7) { /* SLOT_GAUGE */
                skipLog(rep, f, who, "fishing: it is not a gauge slot (type \"gauge\")");
                continue;
            }
            for (k = 0; k < s->nfishing; k++) {
                if (s->fishing[k].slot == slot) fs = &s->fishing[k];
            }
            if (!fs) {
                void *g = realloc(s->fishing, (size_t)(s->nfishing + 1) * sizeof *s->fishing);

                if (!g) return any;
                s->fishing = (SdFishSlot *)g;
                fs = &s->fishing[s->nfishing++];
                memset(fs, 0, sizeof *fs);
                fs->slot = slot;
            }
            for (c = 0; c < 2; c++) {
                if (e->cell[c].set && catchClone(&fs->cell[c], &e->cell[c])) return any;
            }
            rep->fishingSet++;
            any = 1;
        }
        if (f->nspots > 0) {
            for (i = 0; i < f->nspots; i++) {
                void *g;

                if (s->nspots >= SM_SPOTS_MAX) {
                    skipLog(rep, f, "hidden spot add", "the stage holds at most 64 spots added by patches");
                    break;
                }
                g = realloc(s->spots, (size_t)(s->nspots + 1) * sizeof *s->spots);
                if (!g) return any;
                s->spots = (SdSpot *)g;
                s->spots[s->nspots++] = f->spots[i];
                rep->spotsAdded++;
                any = 1;
            }
        }
    }
    return any;
}

int smApplyStage(int stage, SdStage *s, SmReport *rep) {
    SmReport local;
    int changed;

    if (!rep) rep = &local;
    memset(rep, 0, sizeof *rep);
    if (!smHasPatches(stage)) return 0;
    sPendN = 0;
    sSlotKeyCount = 0;
    changed = applyActors(stage, s, rep);
    changed |= applySlots(stage, s, rep);
    changed |= applyEvents(stage, s, rep);
    changed |= applyBattles(stage, s, rep);
    changed |= applyCatches(stage, s, rep);
    if (changed && sTextCount > 0) resolveTexts(stage, s, rep);
    return changed;
}

