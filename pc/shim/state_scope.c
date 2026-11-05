/*
 * State scopes: see state_scope.h. Plain C, no game or SDL dependency, so the registry logic runs
 * in inputtest on byte arrays. Everything is dynamic (growing arrays); nothing has a fixed size.
 */
#include "state_scope.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned char *begin;
    size_t size;
    unsigned char *snapshot;
} ScopeRange;

typedef struct {
    unsigned char *begin;
    size_t size;
    int fromId;
} ScopeCarry;

typedef struct {
    StateScopeHook fn;
    void *user;
} ScopeHook;

struct StateScope {
    const char *name;
    StateScope *parent;
    int active;
    ScopeRange *ranges;
    size_t rangeCount;
    ScopeCarry *carries;
    size_t carryCount;
    ScopeHook *enter;
    size_t enterCount;
    ScopeHook *exit;
    size_t exitCount;
};

typedef struct {
    int key;
    StateScope *scope;
} Binding;

static StateScope **scopes;
static size_t scopeCount;
static Binding *modeTable, *subTable;
static size_t modeCount, subCount;
/* The overlay loader's memory (OVERLAY_LOADER.mode / .subOverlay): -1 = none */
static int lastMode = -1, lastSub = -1;

/* Grows a malloc'd array by one element; returns the new slot or NULL */
static void *growOne(void **array, size_t *count, size_t elem) {
    void *bigger = realloc(*array, (*count + 1) * elem);

    if (bigger == NULL) {
        return NULL;
    }
    *array = bigger;
    return (char *)bigger + (*count)++ * elem;
}

StateScope *stateScopeCreate(const char *name, StateScope *parent) {
    StateScope *scope = calloc(1, sizeof(*scope));
    StateScope **slot;

    if (scope == NULL) {
        return NULL;
    }
    slot = growOne((void **)&scopes, &scopeCount, sizeof(*scopes));
    if (slot == NULL) {
        free(scope);
        return NULL;
    }
    scope->name = name;
    scope->parent = parent;
    *slot = scope;
    return scope;
}

StateScope *stateScopeFind(const char *name) {
    size_t i;

    for (i = 0; i < scopeCount; i++) {
        if (strcmp(scopes[i]->name, name) == 0) {
            return scopes[i];
        }
    }
    return NULL;
}

const char *stateScopeName(const StateScope *scope) {
    return scope->name;
}

int stateScopeIsActive(const StateScope *scope) {
    return scope->active;
}

int stateScopeAddRange(StateScope *scope, void *begin, void *end) {
    ScopeRange *range;
    size_t size;

    if (scope == NULL || begin == NULL || (unsigned char *)end < (unsigned char *)begin) {
        return -1;
    }
    size = (size_t)((unsigned char *)end - (unsigned char *)begin);
    range = growOne((void **)&scope->ranges, &scope->rangeCount, sizeof(*range));
    if (range == NULL) {
        return -1;
    }
    range->begin = begin;
    range->size = size;
    range->snapshot = malloc(size != 0 ? size : 1);
    if (range->snapshot == NULL) {
        scope->rangeCount--;
        return -1;
    }
    memcpy(range->snapshot, begin, size);
    return 0;
}

static int addHook(ScopeHook **list, size_t *count, StateScopeHook hook, void *user) {
    ScopeHook *slot = growOne((void **)list, count, sizeof(**list));

    if (slot == NULL) {
        return -1;
    }
    slot->fn = hook;
    slot->user = user;
    return 0;
}

int stateScopeOnEnter(StateScope *scope, StateScopeHook hook, void *user) {
    return addHook(&scope->enter, &scope->enterCount, hook, user);
}

int stateScopeOnExit(StateScope *scope, StateScopeHook hook, void *user) {
    return addHook(&scope->exit, &scope->exitCount, hook, user);
}

static int isDescendant(const StateScope *scope, const StateScope *ancestor) {
    for (scope = scope->parent; scope != NULL; scope = scope->parent) {
        if (scope == ancestor) {
            return 1;
        }
    }
    return 0;
}

static void runHooks(StateScope *scope, ScopeHook *list, size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        list[i].fn(scope, list[i].user);
    }
}

/* onExit hooks of every active descendant (deepest first, creation order among equals), then of
   the scope itself; all become inactive. */
static void leaveTree(StateScope *scope) {
    size_t i;
    int depth, maxDepth = 0;

    for (i = 0; i < scopeCount; i++) {
        StateScope *s = scopes[i];
        int d = 0;

        for (; s->parent != NULL; s = s->parent) {
            d++;
        }
        if (d > maxDepth) {
            maxDepth = d;
        }
    }
    for (depth = maxDepth; depth >= 0; depth--) {
        for (i = 0; i < scopeCount; i++) {
            StateScope *s = scopes[i];
            int d = 0;
            StateScope *p;

            for (p = s; p->parent != NULL; p = p->parent) {
                d++;
            }
            if (d == depth && s->active && (s == scope || isDescendant(s, scope))) {
                s->active = 0;
                runHooks(s, s->exit, s->exitCount);
            }
        }
    }
}

static void restore(StateScope *scope) {
    size_t i;

    for (i = 0; i < scope->rangeCount; i++) {
        memcpy(scope->ranges[i].begin, scope->ranges[i].snapshot, scope->ranges[i].size);
    }
}

int stateScopeCarry(StateScope *scope, void *begin, size_t size, int fromModeId) {
    ScopeCarry *carry;

    if (scope == NULL || begin == NULL || scope->carryCount >= 8) {
        return -1;
    }
    carry = growOne((void **)&scope->carries, &scope->carryCount, sizeof(*carry));
    if (carry == NULL) {
        return -1;
    }
    carry->begin = begin;
    carry->size = size;
    carry->fromId = fromModeId;
    return 0;
}

void stateScopeLeave(StateScope *scope) {
    leaveTree(scope);
}

void stateScopeEnter(StateScope *scope) {
    size_t i;

    leaveTree(scope);
    restore(scope);
    for (i = 0; i < scopeCount; i++) {
        if (isDescendant(scopes[i], scope)) {
            restore(scopes[i]);
        }
    }
    scope->active = 1;
    runHooks(scope, scope->enter, scope->enterCount);
}

/* stateScopeEnter, keeping the carried ranges of the scope that come from the mode id `fromId` as they are in memory */
static void enterFrom(StateScope *scope, int fromId) {
    unsigned char *kept[8];
    size_t i, n = 0;

    for (i = 0; i < scope->carryCount && i < 8; i++) {
        kept[i] = NULL;
        if (scope->carries[i].fromId == fromId && scope->carries[i].size != 0) {
            kept[i] = malloc(scope->carries[i].size);
            if (kept[i] != NULL) {
                memcpy(kept[i], scope->carries[i].begin, scope->carries[i].size);
                n++;
            }
        }
    }
    if (n == 0) {
        stateScopeEnter(scope);
        return;
    }
    stateScopeEnter(scope);
    for (i = 0; i < scope->carryCount && i < 8; i++) {
        if (kept[i] != NULL) {
            memcpy(scope->carries[i].begin, kept[i], scope->carries[i].size);
            free(kept[i]);
        }
    }
}

static int bind(Binding **table, size_t *count, int key, StateScope *scope) {
    Binding *slot;
    size_t i;

    for (i = 0; i < *count; i++) {
        if ((*table)[i].key == key) {
            (*table)[i].scope = scope;
            return 0;
        }
    }
    slot = growOne((void **)table, count, sizeof(**table));
    if (slot == NULL) {
        return -1;
    }
    slot->key = key;
    slot->scope = scope;
    return 0;
}

static StateScope *lookup(const Binding *table, size_t count, int key) {
    size_t i;

    for (i = 0; i < count; i++) {
        if (table[i].key == key) {
            return table[i].scope;
        }
    }
    return NULL;
}

int stateScopeBindMode(int modeId, StateScope *scope) {
    return bind(&modeTable, &modeCount, modeId, scope);
}

int stateScopeBindSub(int file, StateScope *scope) {
    return bind(&subTable, &subCount, file, scope);
}

/* overlay.c:53-65: the mode overlay is (re)loaded only when mode >> 8 changes, and that forgets
   the sub-overlay. Leaving a mode scope leaves its sub scopes with it. */
void stateScopeModeEntered(int mode) {
    int id = (mode >> 8) & 0xFF;
    StateScope *old, *next;

    if (id == lastMode) {
        return;
    }
    old = lookup(modeTable, modeCount, lastMode);
    next = lookup(modeTable, modeCount, id);
    if (old != NULL) {
        stateScopeLeave(old);
    }
    if (next != NULL) {
        enterFrom(next, lastMode);
    }
    lastMode = id;
    lastSub = -1;
}

/* overlay.c:68-77: the sub-overlay is (re)loaded only when its file changes */
void stateScopeSubLoaded(int file) {
    StateScope *old, *next;

    if (file == lastSub) {
        return;
    }
    old = lookup(subTable, subCount, lastSub);
    next = lookup(subTable, subCount, file);
    if (old != NULL) {
        stateScopeLeave(old);
    }
    lastSub = file;
    if (next != NULL) {
        stateScopeEnter(next);
    }
}

void stateScopeClear(void) {
    size_t i, j;

    for (i = 0; i < scopeCount; i++) {
        for (j = 0; j < scopes[i]->rangeCount; j++) {
            free(scopes[i]->ranges[j].snapshot);
        }
        free(scopes[i]->ranges);
        free(scopes[i]->carries);
        free(scopes[i]->enter);
        free(scopes[i]->exit);
        free(scopes[i]);
    }
    free(scopes);
    free(modeTable);
    free(subTable);
    scopes = NULL;
    modeTable = subTable = NULL;
    scopeCount = modeCount = subCount = 0;
    lastMode = lastSub = -1;
}
