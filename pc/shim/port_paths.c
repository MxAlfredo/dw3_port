/*
 * The port's path layer: see port_paths.h. Pure standard C; the platform's own folders come through the weak
 * portPathsPlatform hook (shim/port_paths_sdl.c in the game, a stub in the host tests).
 */
#include "port_paths.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define PP_MKDIR(p) _mkdir(p)
#else
#include <sys/types.h>
#ifdef __ANDROID__
#define PP_MKDIR(p) mkdir((p), 0777) /* with the start's umask 000: rwxrwxrwx (a PC's adb push needs it; see android_perms.h) */
#else
#define PP_MKDIR(p) mkdir((p), 0755)
#endif
#endif

static const struct {
    const char *env;
    const char *leaf;
} ITEMS[PP_DIR_COUNT] = {
    {"DW3_SETTINGS", "settings.ini"},
    {"DW3_SAVES_DIR", "saves/" PP_DEFAULT_PROFILE},
    {"DW3_IMPORT_DIR", "import"},
    {"DW3_MODS_DIR", "mods"},
    {"DW3_DATA_DIR", "data"},
    {"DW3_LOGS_DIR", "logs"},
    {"DW3_SCREENSHOTS_DIR", "screenshots"},
    {"DW3_CACHE_DIR", "cache"},
};

__attribute__((weak)) int portPathsPlatform(char *exeDir, int exeCap, char *pref, int prefCap) {
    if (exeCap > 0) {
        exeDir[0] = '\0';
    }
    if (prefCap > 0) {
        pref[0] = '\0';
    }
    return 0;
}

static int isSep(char c) {
    return c == '/' || c == '\\';
}

/* ---- file helpers ---- */

int portJoin(char *out, size_t cap, const char *dir, const char *leaf) {
    size_t n = strlen(dir);

    if (n == 0) {
        return snprintf(out, cap, "%s", leaf) < (int)cap;
    }
    return snprintf(out, cap, "%s%s%s", dir, isSep(dir[n - 1]) ? "" : "/", leaf) < (int)cap;
}

int portIsDir(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int portIsFile(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int portMkdirs(const char *dir) {
    char tmp[PP_CAP + 8];
    char *p;

    if (dir == NULL || dir[0] == '\0' || strlen(dir) >= PP_CAP) {
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (p = tmp + 1; *p != '\0'; p++) {
        if (isSep(*p)) {
            char sep = *p;

            *p = '\0';
            PP_MKDIR(tmp); /* an existing folder, or a drive like "C:", fails harmlessly */
            *p = sep;
        }
    }
    PP_MKDIR(tmp);
    return portIsDir(tmp);
}

int portMkdirsFor(const char *file) {
    char tmp[PP_CAP + 8];
    char *p, *last = NULL;

    if (file == NULL || strlen(file) >= PP_CAP) {
        return 0;
    }
    snprintf(tmp, sizeof(tmp), "%s", file);
    for (p = tmp; *p != '\0'; p++) {
        if (isSep(*p)) {
            last = p;
        }
    }
    if (last == NULL || last == tmp) {
        return 1;
    }
    *last = '\0';
    return portMkdirs(tmp);
}

static int sameNoCase(const char *a, const char *b) {
    while (*a != '\0' && *b != '\0') {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == *b;
}

int portPathsResolveCI(const char *root, const char *rel, char *out, size_t cap) {
    char cur[PP_CAP];
    struct stat st;
    const char *p;

    if (root == NULL || rel == NULL || !portJoin(cur, sizeof(cur), root, rel)) {
        return 0;
    }
    if (stat(cur, &st) == 0) {
        return snprintf(out, cap, "%s", cur) < (int)cap;
    }
    snprintf(cur, sizeof(cur), "%s", root);
    for (p = rel; *p != '\0';) {
        char comp[256], next[PP_CAP];
        size_t n = 0;
        DIR *d;
        struct dirent *e;
        int found = 0;

        while (isSep(*p)) {
            p++;
        }
        while (*p != '\0' && !isSep(*p) && n < sizeof(comp) - 1) {
            comp[n++] = *p++;
        }
        comp[n] = '\0';
        if (n == 0) {
            break;
        }
        if (portJoin(next, sizeof(next), cur, comp) && stat(next, &st) == 0) {
            snprintf(cur, sizeof(cur), "%s", next);
            continue;
        }
        d = opendir(cur[0] != '\0' ? cur : ".");
        if (d == NULL) {
            return 0;
        }
        while ((e = readdir(d)) != NULL) {
            if (sameNoCase(e->d_name, comp)) {
                found = portJoin(next, sizeof(next), cur, e->d_name);
                break;
            }
        }
        closedir(d);
        if (!found) {
            return 0;
        }
        snprintf(cur, sizeof(cur), "%s", next);
    }
    return stat(cur, &st) == 0 && snprintf(out, cap, "%s", cur) < (int)cap;
}

/* ---- the base ---- */

static char gBase[PP_CAP];
static char gExe[PP_CAP];
static int gDone;
static int gPortable;

int portPathsChoosePrefDir(const char *exeDir, int portable, const char *pref, char *out, int cap) {
    const char *pick = (portable && exeDir != NULL && exeDir[0] != '\0') ? exeDir : pref;

    if (pick == NULL || pick[0] == '\0') {
        return 0;
    }
    return snprintf(out, (size_t)cap, "%s", pick) < cap;
}

int portPathsAndroidBase(const char *external, const char *internal, char *out, int cap) {
    const char *pick = (external != NULL && external[0] != '\0') ? external : internal;
    size_t n;

    if (pick == NULL || pick[0] == '\0') {
        return 0;
    }
    n = strlen(pick);
    return snprintf(out, (size_t)cap, "%s%s", pick, isSep(pick[n - 1]) ? "" : "/") < cap;
}

static void baseInit(void) {
    char pref[PP_CAP];
    const char *user;
    int havePref;

    if (gDone) {
        return;
    }
    gDone = 1;
    gBase[0] = gExe[0] = '\0';
    gPortable = 0;
    pref[0] = '\0';
    user = getenv("DW3_USER_DIR");
    /* with DW3_USER_DIR the platform's folder is never asked for (SDL creates it when asked): prefCap 0 */
    havePref = portPathsPlatform(gExe, sizeof(gExe), pref, (user != NULL && user[0] != '\0') ? 0 : (int)sizeof(pref)) != 0;
    if (user != NULL && user[0] != '\0') {
        size_t n = strlen(user);

        if (n + 2 < sizeof(gBase)) {
            snprintf(gBase, sizeof(gBase), "%s%s", user, isSep(user[n - 1]) ? "" : "/");
        }
        return;
    }
    if (gExe[0] != '\0') {
        char probe[PP_CAP + 16];
        FILE *f;

        if (portJoin(probe, sizeof(probe), gExe, "portable.txt") && (f = fopen(probe, "rb")) != NULL) {
            fclose(f);
            gPortable = 1;
        }
    }
    if (!portPathsChoosePrefDir(gExe, gPortable, havePref ? pref : NULL, gBase, sizeof(gBase))) {
        gBase[0] = '\0';
    }
}

void portPathsReset(void) {
    gDone = 0;
}

const char *portPathsBase(void) {
    baseInit();
    return gBase[0] != '\0' ? gBase : NULL;
}

int portPathsIsPortable(void) {
    baseInit();
    return gPortable;
}

const char *portPathsExeDir(void) {
    baseInit();
    return gExe[0] != '\0' ? gExe : NULL;
}

/* ---- the items ---- */

const char *portPathsEnvName(PortDir d) {
    return (d >= 0 && d < PP_DIR_COUNT) ? ITEMS[d].env : NULL;
}

int portPathsResolveWith(PortDir d, const char *base, const char *override, char *out, int cap) {
    size_t n;

    if (d < 0 || d >= PP_DIR_COUNT) {
        return 0;
    }
    if (override != NULL && override[0] != '\0') {
        if (snprintf(out, (size_t)cap, "%s", override) >= cap) {
            return 0;
        }
        if (d != PP_SETTINGS) {
            n = strlen(out);
            while (n > 1 && isSep(out[n - 1])) {
                out[--n] = '\0';
            }
        }
        return 1;
    }
    if (base == NULL || base[0] == '\0') {
        return 0;
    }
    return portJoin(out, (size_t)cap, base, ITEMS[d].leaf);
}

int portPath(PortDir d, char *out, int cap) {
    if (d < 0 || d >= PP_DIR_COUNT) {
        return 0;
    }
    return portPathsResolveWith(d, portPathsBase(), getenv(ITEMS[d].env), out, cap);
}

int portPathIn(PortDir d, const char *leaf, char *out, int cap) {
    char dir[PP_CAP];

    return portPath(d, dir, sizeof(dir)) && portJoin(out, (size_t)cap, dir, leaf);
}

int portPathsViewDir(char *out, int cap) {
    return portPathIn(PP_SAVES, "view", out, cap);
}

/* Place `i` to look for the overlay: 0 the environment variable (or the working folder), 1 the executable's folder,
   2 the user folder's localization/ (Android installs the APK's copy there). 0 when that place is not defined. */
int portPathsL10nCandidate(int i, const char *env, const char *exeDir, const char *userDir, char *out, int cap) {
    if (i == 0) {
        const char *pick = (env != NULL && env[0] != '\0') ? env : "localization/es-419";

        return snprintf(out, (size_t)cap, "%s", pick) < cap;
    }
    if (i == 1 && exeDir != NULL) {
        return portJoin(out, (size_t)cap, exeDir, "localization/es-419");
    }
    if (i == 2 && userDir != NULL && userDir[0] != '\0') {
        return portJoin(out, (size_t)cap, userDir, "localization/es-419");
    }
    return 0;
}

int portPathsL10n(char *out, int cap) {
    const char *env = getenv("DW3_L10N_DIR");
    const char *user = NULL;
    int i;

#ifdef __ANDROID__
    user = portPathsBase();
#endif
    if (env != NULL && env[0] != '\0') {
        return portPathsL10nCandidate(0, env, NULL, NULL, out, cap);
    }
    for (i = 0; i < 3; i++) {
        char probe[PP_CAP];

        if (portPathsL10nCandidate(i, NULL, portPathsExeDir(), user, probe, sizeof(probe)) && portIsDir(probe)) {
            return snprintf(out, (size_t)cap, "%s", probe) < cap;
        }
    }
    return 0;
}

/* ---- import ---- */

static int endsMcd(const char *name) {
    size_t n = strlen(name);

    return n > 4 && sameNoCase(name + n - 4, ".mcd");
}

static int cmpNames(const void *a, const void *b) {
    const char *x = (const char *)a, *y = (const char *)b;

    while (*x != '\0' && *y != '\0') {
        int d = tolower((unsigned char)*x) - tolower((unsigned char)*y);

        if (d != 0) {
            return d;
        }
        x++;
        y++;
    }
    return (unsigned char)*x - (unsigned char)*y;
}

int portPathsImportCards(char out[][PP_CAP], int max) {
    const char *env = getenv("DW3_MEMCARD");
    char dir[PP_CAP];
    static char names[64][256];
    int count = 0, i, n = 0;
    DIR *d;
    struct dirent *e;

    if (max <= 0) {
        return 0;
    }
    if (env != NULL && env[0] != '\0') {
        return snprintf(out[0], PP_CAP, "%s", env) < PP_CAP ? 1 : 0;
    }
    if (!portPath(PP_IMPORT, dir, sizeof(dir)) || (d = opendir(dir)) == NULL) {
        return 0;
    }
    while ((e = readdir(d)) != NULL && count < 64) {
        if (endsMcd(e->d_name) && strlen(e->d_name) < sizeof(names[0])) {
            snprintf(names[count++], sizeof(names[0]), "%s", e->d_name);
        }
    }
    closedir(d);
    qsort(names, (size_t)count, sizeof(names[0]), cmpNames);
    for (i = 0; i < count && n < max; i++) {
        char full[PP_CAP];

        if (portJoin(full, sizeof(full), dir, names[i]) && portIsFile(full)) {
            snprintf(out[n++], PP_CAP, "%s", full);
        }
    }
    return n;
}

static int addDir(char out[][PP_CAP], int n, int max, const char *dir) {
    int i;

    if (n >= max || !portIsDir(dir)) {
        return n;
    }
    for (i = 0; i < n; i++) {
        if (strcmp(out[i], dir) == 0) {
            return n;
        }
    }
    snprintf(out[n], PP_CAP, "%s", dir);
    return n + 1;
}

int portPathsEmulatorDirs(char out[][PP_CAP], int max) {
    const char *env = getenv("DW3_EMU_DIRS");
    char item[PP_CAP];
    int n = 0;

    if (max <= 0) {
        return 0;
    }
    if (env != NULL && env[0] != '\0') {
        const char *p = env;

        while (*p != '\0') {
            size_t len = 0;

            while (p[len] != '\0' && p[len] != ':' && p[len] != ';') {
                len++;
            }
            if (len > 0 && len < sizeof(item)) {
                memcpy(item, p, len);
                item[len] = '\0';
                n = addDir(out, n, max, item);
            }
            p += len;
            if (*p != '\0') {
                p++;
            }
        }
        return n;
    }
#ifdef _WIN32
    {
        const char *home = getenv("USERPROFILE"), *app = getenv("APPDATA");

        if (home != NULL && home[0] != '\0') {
            snprintf(item, sizeof(item), "%s/Documents/DuckStation/memcards", home);
            n = addDir(out, n, max, item);
            snprintf(item, sizeof(item), "%s/Documents/ePSXe/memcards", home);
            n = addDir(out, n, max, item);
        }
        if (app != NULL && app[0] != '\0') {
            snprintf(item, sizeof(item), "%s/DuckStation/memcards", app);
            n = addDir(out, n, max, item);
        }
    }
#else
    {
        static const char *const rel[] = {
            ".local/share/duckstation/memcards", ".config/duckstation/memcards",
            ".var/app/org.duckstation.DuckStation/config/duckstation/memcards",
            ".var/app/org.duckstation.DuckStation/data/duckstation/memcards",
            ".config/retroarch/saves", ".config/mednafen/sav", ".mednafen/sav", ".pcsx/memcards", ".pcsxr/memcards", ".epsxe/memcards",
        };
        const char *home = getenv("HOME"), *data = getenv("XDG_DATA_HOME"), *conf = getenv("XDG_CONFIG_HOME");
        size_t i;

        if (data != NULL && data[0] != '\0') {
            snprintf(item, sizeof(item), "%s/duckstation/memcards", data);
            n = addDir(out, n, max, item);
        }
        if (conf != NULL && conf[0] != '\0') {
            snprintf(item, sizeof(item), "%s/duckstation/memcards", conf);
            n = addDir(out, n, max, item);
        }
        if (home != NULL && home[0] != '\0') {
            for (i = 0; i < sizeof(rel) / sizeof(rel[0]); i++) {
                snprintf(item, sizeof(item), "%s/%s", home, rel[i]);
                n = addDir(out, n, max, item);
            }
        }
    }
#endif
    return n;
}

int portPathsImportMarker(char *out, int cap) {
    const char *env = getenv("DW3_MEMCARD");

    if (env != NULL && env[0] != '\0') {
        char dir[PP_CAP];
        const char *p, *last = NULL;

        for (p = env; *p != '\0'; p++) {
            if (isSep(*p)) {
                last = p;
            }
        }
        if (last == NULL) {
            return snprintf(out, (size_t)cap, "imported.txt") < cap;
        }
        if ((size_t)(last - env) + 1 >= sizeof(dir)) {
            return 0;
        }
        memcpy(dir, env, (size_t)(last - env));
        dir[last - env] = '\0';
        return portJoin(out, (size_t)cap, dir[0] != '\0' ? dir : "/", "imported.txt");
    }
    return portPathIn(PP_IMPORT, "imported.txt", out, cap);
}

int portPathsEnsureTree(void) {
    static const PortDir dirs[] = {PP_SAVES, PP_IMPORT, PP_MODS, PP_DATA, PP_LOGS};
    char path[PP_CAP];
    int i, ok = 1;

    if (portPath(PP_SETTINGS, path, sizeof(path))) {
        ok = portMkdirsFor(path) && ok;
    }
    for (i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        if (portPath(dirs[i], path, sizeof(path))) {
            ok = portMkdirs(path) && ok;
        } else {
            ok = 0;
        }
    }
    return ok;
}
