/* The one table of exporter types (B3): see exp_types.h. */
#include "exp_types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input_cfg.h"

static const ExpType *sTypes;
static int sCount;

void expTypesSet(const ExpType *types, int count) {
    sTypes = count > 0 ? types : NULL;
    sCount = sTypes != NULL ? count : 0;
}

int expTypeCount(void) { return sCount; }

const ExpType *expTypeAt(int i) { return i >= 0 && i < sCount ? &sTypes[i] : NULL; }

int expTypeIsJob(const ExpType *t) { return t != NULL && t->start != NULL; }

int expTypesJobCount(void) {
    int i, n = 0;

    for (i = 0; i < sCount; i++) {
        n += expTypeIsJob(&sTypes[i]);
    }
    return n;
}

static void shutdownAll(void) {
    int i;

    for (i = 0; i < sCount; i++) {
        if (sTypes[i].shutdown != NULL) {
            sTypes[i].shutdown(); /* stops and joins a worker between two items; the files are written atomically */
        }
    }
}

void expTypesSetup(const char *dataDir) {
    static int armed;
    int i;

    for (i = 0; i < sCount; i++) {
        const ExpType *t = &sTypes[i];

        inputCfgRegisterExport(t->key, t->note);
        if (t->setup != NULL) {
            t->setup(dataDir);
        }
    }
    if (!armed) {
        armed = 1;
        atexit(shutdownAll);
    }
}

void expTypesRunSettings(const char *dataDir) {
    int i;

    if (dataDir == NULL) {
        return;
    }
    for (i = 0; i < sCount; i++) {
        const ExpType *t = &sTypes[i];
        char spec[256];

        if (!inputCfgReadExport(t->key, spec, (int)sizeof(spec))) {
            if (t->dflt == NULL) {
                continue;
            }
            snprintf(spec, sizeof(spec), "%s", t->dflt);
        }
        if (strcmp(spec, "off") == 0) {
            continue;
        }
        t->run(spec);
        if (t->after != NULL) {
            t->after();
        }
    }
}
