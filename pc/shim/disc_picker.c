/*
 * W2: the player's first run (see disc_picker.h). Our file, not the game's: a native file dialog
 * (third_party/tinyfiledialogs), SDL message boxes for the reasons, and the checks that tell the
 * European disc from anything else, as pure functions over the buffers the image gives.
 */
#include "disc_picker.h"
#include "disc_fd.h"

#include <SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include "input_cfg.h"
#include "port_log.h"
#include "port_paths.h"
#ifdef __ANDROID__ /* no native file dialog there: the system's document picker (T7b), the image is read in place */
#include <jni.h>

#include "disc_flow.h"
#else
#include "tinyfiledialogs.h"
#endif

#define CD_FRAME 2352
#define CD_DATA 24
#define CD_SECTOR 2048
#define LOG_KEEP 10
#define LOG_NAME_LEN 23 /* dw3-YYYYMMDD-HHMMSS.log */

/* ---- pure ---- */

static int endsWithNoCase(const char *s, const char *suffix);

static int isSep(char c) {
    return c == '/' || c == '\\';
}

static int hasPrefixNoCase(const char *s, const char *prefix) {
    for (; *prefix != '\0'; s++, prefix++) {
        if (tolower((unsigned char)*s) != tolower((unsigned char)*prefix)) {
            return 0;
        }
    }
    return 1;
}

int dpCueBin(const char *cueText, const char *cuePath, char *out, int cap) {
    const char *p = cueText;
    char name[512];
    int n = 0;

    while (*p != '\0') {
        const char *line = p;

        while (*line == ' ' || *line == '\t') {
            line++;
        }
        if (hasPrefixNoCase(line, "FILE") && (line[4] == ' ' || line[4] == '\t')) {
            line += 4;
            while (*line == ' ' || *line == '\t') {
                line++;
            }
            if (*line == '"') {
                line++;
                while (*line != '\0' && *line != '"' && *line != '\n' && n < (int)sizeof(name) - 1) {
                    name[n++] = *line++;
                }
                if (*line != '"') {
                    return 0;
                }
            } else {
                while (*line != '\0' && !isspace((unsigned char)*line) && n < (int)sizeof(name) - 1) {
                    name[n++] = *line++;
                }
            }
            name[n] = '\0';
            if (n == 0) {
                return 0;
            }
            if (isSep(name[0]) || (isalpha((unsigned char)name[0]) && name[1] == ':')) {
                return snprintf(out, (size_t)cap, "%s", name) < cap;
            } else {
                const char *last = NULL;
                const char *c;
                int dirLen;

                for (c = cuePath; *c != '\0'; c++) {
                    if (isSep(*c)) {
                        last = c;
                    }
                }
                dirLen = last != NULL ? (int)(last - cuePath) + 1 : 0;
                return snprintf(out, (size_t)cap, "%.*s%s", dirLen, cuePath, name) < cap;
            }
        }
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
    return 0;
}

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int dpFindFile(const uint8_t *dir, size_t len, const char *name, uint32_t *lba, uint32_t *size) {
    size_t off = 0;
    size_t want = strlen(name);

    while (off + 34 <= len) {
        size_t reclen = dir[off];
        size_t nameLen;
        size_t i;

        if (reclen == 0) {
            off = (off / CD_SECTOR + 1) * CD_SECTOR; /* records never straddle a sector */
            continue;
        }
        nameLen = dir[off + 32];
        if (off + 33 + nameLen > len) {
            return 0;
        }
        for (i = 0; i < nameLen && dir[off + 33 + i] != ';'; i++) {
        }
        if (i == want) {
            size_t k;

            for (k = 0; k < i; k++) {
                if (toupper(dir[off + 33 + k]) != toupper((unsigned char)name[k])) {
                    break;
                }
            }
            if (k == i) {
                *lba = le32(dir + off + 2);
                *size = le32(dir + off + 10);
                return 1;
            }
        }
        off += reclen;
    }
    return 0;
}

int dpBootId(const char *cnf, size_t len, char *id, int cap) {
    size_t i;

    for (i = 0; i + 4 <= len; i++) {
        if ((i == 0 || cnf[i - 1] == '\n' || cnf[i - 1] == '\r') && hasPrefixNoCase(cnf + i, "BOOT")
            && !hasPrefixNoCase(cnf + i, "BOOT2")) {
            size_t j = i + 4;
            int n = 0;

            while (j < len && cnf[j] != '\n' && cnf[j] != ':') {
                j++;
            }
            if (j >= len || cnf[j] != ':') {
                return 0;
            }
            j++;
            while (j < len && (isSep(cnf[j]) || cnf[j] == ' ')) {
                j++;
            }
            while (j < len && cnf[j] != ';' && !isspace((unsigned char)cnf[j]) && n < cap - 1) {
                id[n++] = (char)toupper((unsigned char)cnf[j++]);
            }
            id[n] = '\0';
            return n > 0;
        }
    }
    return 0;
}

int dpIsSpanish(const char *locale) {
    return locale != NULL && tolower((unsigned char)locale[0]) == 'e' && tolower((unsigned char)locale[1]) == 's'
        && (locale[2] == '\0' || locale[2] == '_' || locale[2] == '-' || locale[2] == '.' || locale[2] == '@');
}

void dpLogName(char *out, int cap, const struct tm *t) {
    snprintf(out, (size_t)cap, "dw3-%04d%02d%02d-%02d%02d%02d.log", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
}

int dpLogIsName(const char *name) {
    int i;

    if (strlen(name) != LOG_NAME_LEN || strncmp(name, "dw3-", 4) != 0 || strcmp(name + 19, ".log") != 0 || name[12] != '-') {
        return 0;
    }
    for (i = 4; i < 19; i++) {
        if (i != 12 && !isdigit((unsigned char)name[i])) {
            return 0;
        }
    }
    return 1;
}

int dpLogPlan(const char *const *names, int count, int keep, int *drop) {
    int i, k, dropped = 0;

    for (i = 0; i < count; i++) {
        int newer = 0;

        drop[i] = 0;
        if (!dpLogIsName(names[i])) {
            continue;
        }
        for (k = 0; k < count; k++) {
            if (dpLogIsName(names[k]) && strcmp(names[k], names[i]) > 0) {
                newer++;
            }
        }
        if (newer >= keep) {
            drop[i] = 1;
            dropped++;
        }
    }
    return dropped;
}

static int sepEq(char a, char b) {
    if (isSep(a) && isSep(b)) {
        return 1;
    }
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

int dpRelativeTo(const char *folder, const char *path, char *out, int cap) {
    size_t n = strlen(folder), i;

    while (n > 0 && isSep(folder[n - 1])) {
        n--;
    }
    if (n == 0) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (path[i] == '\0' || !sepEq(folder[i], path[i])) {
            return 0;
        }
    }
    if (!isSep(path[n]) || path[n + 1] == '\0') {
        return 0;
    }
    path += n;
    while (isSep(*path)) {
        path++;
    }
    if (snprintf(out, (size_t)cap, "%s", path) >= cap) {
        return 0;
    }
    for (i = 0; out[i] != '\0'; i++) {
        if (out[i] == '\\') {
            out[i] = '/';
        }
    }
    return 1;
}

int dpResolveStored(const char *folder, const char *stored, char *out, int cap) {
    size_t n = strlen(folder);

    if (stored[0] == '\0') {
        return 0;
    }
    if (isSep(stored[0]) || (isalpha((unsigned char)stored[0]) && stored[1] == ':') || n == 0) {
        return snprintf(out, (size_t)cap, "%s", stored) < cap;
    }
    return snprintf(out, (size_t)cap, "%s%s%s", folder, isSep(folder[n - 1]) ? "" : "/", stored) < cap;
}

int dpCandidateRank(const char *name) {
    if (endsWithNoCase(name, ".cue")) {
        return 0;
    }
    if (endsWithNoCase(name, ".bin")) {
        return 1;
    }
    if (endsWithNoCase(name, ".iso")) {
        return 2;
    }
    return -1;
}

int dpCandidateCmp(int rankA, int folderA, const char *nameA, int rankB, int folderB, const char *nameB) {
    if (rankA != rankB) {
        return rankA < rankB ? -1 : 1;
    }
    if (folderA != folderB) {
        return folderA < folderB ? -1 : 1;
    }
    return strcmp(nameA, nameB);
}

const char *dpMessage(DpResult r, int es, const char *detected, char *buf, int cap) {
    int has = detected != NULL && detected[0] != '\0';

    switch (r) {
    case DP_CANT_OPEN:
        snprintf(buf, (size_t)cap, "%s", es ? "No se pudo abrir el archivo. Comprueba que existe y que puedes leerlo."
                                            : "The file could not be opened. Check that it exists and that you can read it.");
        break;
    case DP_NOT_PS1:
        snprintf(buf, (size_t)cap, "%s", es ? "Ese archivo no es una imagen de disco de PlayStation."
                                            : "That file is not a PlayStation disc image.");
        break;
    case DP_COOKED_ISO:
        snprintf(buf, (size_t)cap, "%s", es ? "Esta imagen no tiene el formato que necesita el juego (sectores de 2352 bytes, "
                                              "por ejemplo un .bin con su .cue). Un .iso de 2048 bytes por sector no sirve."
                                            : "This image is not in the format the game needs (2352-byte sectors, such as a "
                                              ".bin with its .cue). A 2048-byte-per-sector .iso will not work.");
        break;
    case DP_OTHER_DISC:
        if (has) {
            snprintf(buf, (size_t)cap, es ? "Ese disco no es Digimon World 3 (Europa). Disco detectado: %s."
                                          : "That disc is not Digimon World 3 (Europe). Detected disc: %s.", detected);
        } else {
            snprintf(buf, (size_t)cap, "%s", es ? "Ese disco no es Digimon World 3 (Europa)."
                                                : "That disc is not Digimon World 3 (Europe).");
        }
        break;
    default:
        buf[0] = '\0';
        break;
    }
    return buf;
}

const char *dpNoDiscMessage(int es, const char *folder, char *buf, int cap) {
    int has = folder != NULL && folder[0] != '\0';

    if (es) {
        snprintf(buf, (size_t)cap, "No se encontr\xC3\xB3 la imagen del disco. Copia tu disco de Digimon World 3 (Europa), un .bin con su .cue, "
                                   "a la carpeta%s%s del tel\xC3\xA9" "fono (por USB desde una PC) y reinicia el juego.",
                 has ? ":\n" : "", has ? folder : "");
    } else {
        snprintf(buf, (size_t)cap, "The disc image was not found. Copy your Digimon World 3 (Europe) disc, a .bin with its .cue, "
                                   "to the phone's folder%s%s (over USB from a PC) and restart the game.",
                 has ? ":\n" : "", has ? folder : "");
    }
    return buf;
}

/* ---- I/O ---- */

static int readData(FILE *f, long lba, uint8_t *out) {
    return fseek(f, lba * CD_FRAME + CD_DATA, SEEK_SET) == 0 && fread(out, 1, CD_SECTOR, f) == CD_SECTOR;
}

static int endsWithNoCase(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);

    return a >= b && hasPrefixNoCase(s + a - b, suffix);
}

DpResult dpCheckImage(const char *path, char *bin, int cap, char *detected, int dcap) {
    static uint8_t dir[8 * CD_SECTOR];
    uint8_t pvd[CD_SECTOR];
    uint8_t frame[CD_FRAME];
    static const uint8_t sync[12] = {0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0};
    uint32_t rootLba, rootSize, cnfLba, cnfSize;
    char cnf[CD_SECTOR + 1];
    char id[64];
    FILE *f;
    size_t got;

    if (detected != NULL && dcap > 0) {
        detected[0] = '\0';
    }
    if (endsWithNoCase(path, ".cue")) {
        char text[65536];
        size_t n;

        f = fopen(path, "rb");
        if (f == NULL) {
            return DP_CANT_OPEN;
        }
        n = fread(text, 1, sizeof(text) - 1, f);
        fclose(f);
        text[n] = '\0';
        if (!dpCueBin(text, path, bin, cap)) {
            return DP_NOT_PS1;
        }
    } else if (snprintf(bin, (size_t)cap, "%s", path) >= cap) {
        return DP_CANT_OPEN;
    }
    f = dfdOpen(bin);
    if (f == NULL) {
        return DP_CANT_OPEN;
    }
    if (fseek(f, 16L * CD_FRAME, SEEK_SET) != 0 || (got = fread(frame, 1, CD_FRAME, f)) != CD_FRAME
        || memcmp(frame, sync, 12) != 0 || memcmp(frame + CD_DATA + 1, "CD001", 5) != 0) {
        uint8_t cooked[8];
        int iso = fseek(f, 16L * CD_SECTOR + 1, SEEK_SET) == 0 && fread(cooked, 1, 5, f) == 5 && memcmp(cooked, "CD001", 5) == 0;

        (void)got;
        fclose(f);
        return iso ? DP_COOKED_ISO : DP_NOT_PS1;
    }
    memcpy(pvd, frame + CD_DATA, CD_SECTOR);
    rootLba = le32(pvd + 156 + 2);
    rootSize = le32(pvd + 156 + 10);
    if (rootSize == 0 || rootSize > sizeof(dir)) {
        rootSize = CD_SECTOR;
    }
    memset(dir, 0, sizeof(dir));
    {
        uint32_t s;

        for (s = 0; s * CD_SECTOR < rootSize; s++) {
            if (!readData(f, (long)(rootLba + s), dir + s * CD_SECTOR)) {
                fclose(f);
                return DP_NOT_PS1;
            }
        }
    }
    if (!dpFindFile(dir, rootSize, "SYSTEM.CNF", &cnfLba, &cnfSize) || !readData(f, (long)cnfLba, (uint8_t *)cnf)) {
        fclose(f);
        return DP_NOT_PS1;
    }
    fclose(f);
    cnf[CD_SECTOR] = '\0';
    if (cnfSize > CD_SECTOR) {
        cnfSize = CD_SECTOR;
    }
    if (!dpBootId(cnf, cnfSize, id, sizeof(id))) {
        return DP_NOT_PS1;
    }
    if (strcmp(id, DP_EXPECTED_BOOT) != 0) {
        if (detected != NULL && dcap > 0) {
            snprintf(detected, (size_t)dcap, "%s", id);
        }
        return DP_OTHER_DISC;
    }
    return DP_OK;
}

/* stdout and stderr to <logs>/dw3-<stamp>.log, keeping the newest LOG_KEEP */
static void startLog(const char *dirPath) {
    char file[1200], name[40];
    const char *names[256];
    char store[256][40];
    int drop[256];
    int count = 0, i;
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    DIR *d;
    struct dirent *e;

    portMkdirs(dirPath);
    if (t == NULL) {
        return;
    }
    dpLogName(name, sizeof(name), t);
    snprintf(file, sizeof(file), "%s/%s", dirPath, name);
    {
        FILE *fresh = fopen(file, "w"); /* empty, then both streams append: with "w" stderr wrote over stdout's lines */

        if (fresh == NULL) {
            return;
        }
        fclose(fresh);
    }
    if (plogTeeToLogcat(file)) {
        /* Android: the streams now feed the file and logcat (stdio of an app goes nowhere otherwise); the rest is the same */
    } else if (freopen(file, "a", stderr) == NULL || freopen(file, "a", stdout) == NULL) {
        return;
    } else {
        setvbuf(stderr, NULL, _IONBF, 0);
#ifdef _WIN32
        /* the Windows C runtime treats _IOLBF as full buffering: stdout lines would land after later stderr ones */
        setvbuf(stdout, NULL, _IONBF, 0);
#else
        setvbuf(stdout, NULL, _IOLBF, 4096);
#endif
    }
    d = opendir(dirPath);
    if (d == NULL) {
        return;
    }
    while ((e = readdir(d)) != NULL && count < 256) {
        if (dpLogIsName(e->d_name)) {
            snprintf(store[count], sizeof(store[count]), "%.39s", e->d_name);
            names[count] = store[count];
            count++;
        }
    }
    closedir(d);
    dpLogPlan(names, count, LOG_KEEP, drop);
    for (i = 0; i < count; i++) {
        if (drop[i]) {
            snprintf(file, sizeof(file), "%s/%s", dirPath, names[i]);
            remove(file);
        }
    }
}

static int spanishLocale(void) {
    SDL_Locale *loc = SDL_GetPreferredLocales();
    int es;

    if (loc != NULL) {
        es = loc[0].language != NULL && dpIsSpanish(loc[0].language);
        SDL_free(loc);
        return es;
    }
    {
        const char *env = getenv("LC_ALL");

        if (env == NULL || env[0] == '\0') {
            env = getenv("LANG");
        }
        return dpIsSpanish(env);
    }
}

#ifndef __ANDROID__
/* Linux: a file-dialog helper tinyfd can drive must exist; Windows always has its own */
static int haveDialogTool(void) {
#ifdef _WIN32
    return 1;
#else
    static const char *tools[] = {"zenity", "kdialog", "matedialog", "qarma", "yad"};
    const char *path = getenv("PATH");
    size_t i;

    if (path == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(tools) / sizeof(tools[0]); i++) {
        const char *p = path;

        while (*p != '\0') {
            const char *end = strchr(p, ':');
            size_t len = end != NULL ? (size_t)(end - p) : strlen(p);
            char probe[1100];

            if (len > 0 && len < 900) {
                snprintf(probe, sizeof(probe), "%.*s/%s", (int)len, p, tools[i]);
                if (access(probe, X_OK) == 0) {
                    return 1;
                }
            }
            p += len;
            if (*p == ':') {
                p++;
            }
        }
    }
    return 0;
#endif
}
#endif

#ifndef __ANDROID__
/* 1 = choose again, 0 = quit */
static int askAgain(const char *reason, int es) {
    SDL_MessageBoxButtonData buttons[2];
    SDL_MessageBoxData data;
    int chosen = 0;
    char text[800];

    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[0].buttonid = 0;
    buttons[0].text = es ? "Salir" : "Quit";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonid = 1;
    buttons[1].text = es ? "Elegir otro" : "Choose again";
    snprintf(text, sizeof(text), "%s", reason);
    data.flags = SDL_MESSAGEBOX_ERROR;
    data.window = NULL;
    data.title = "Digimon World 3";
    data.message = text;
    data.numbuttons = 2;
    data.buttons = buttons;
    data.colorScheme = NULL;
    if (SDL_ShowMessageBox(&data, &chosen) < 0) {
        return 0;
    }
    return chosen == 1;
}
#endif

#ifdef __ANDROID__
/* ---- Android: the system document picker (T7b) ----
   io.github.dw3port.DW3Activity (static methods) opens ACTION_OPEN_DOCUMENT at Downloads and hands a document's file
   descriptor to C. The image is never copied: every reader of the disc opens the path "/proc/self/fd/<n>" of that
   descriptor with fopen, like any other file, so the disc layer has one code path. */

static int sDiscFd = -1; /* the descriptor behind the current /proc/self/fd path */

static JNIEnv *jniEnv(jclass *cls) {
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject activity = (jobject)SDL_AndroidGetActivity();

    if (env == NULL || activity == NULL) {
        return NULL;
    }
    *cls = (*env)->GetObjectClass(env, activity);
    return *cls != NULL ? env : NULL;
}

/* The Java string a static String method returns, as a heap copy (NULL for null or an error) */
static char *callStringMethod(const char *name, const char *uri) {
    jclass cls;
    JNIEnv *env = jniEnv(&cls);
    jmethodID m;
    jstring arg = NULL, ret;
    const char *chars;
    char *out = NULL;

    if (env == NULL) {
        return NULL;
    }
    if (uri != NULL) {
        m = (*env)->GetStaticMethodID(env, cls, name, "(Ljava/lang/String;)Ljava/lang/String;");
        arg = (*env)->NewStringUTF(env, uri);
    } else {
        m = (*env)->GetStaticMethodID(env, cls, name, "()Ljava/lang/String;");
    }
    if (m == NULL) {
        (*env)->ExceptionClear(env);
        return NULL;
    }
    ret = uri != NULL ? (jstring)(*env)->CallStaticObjectMethod(env, cls, m, arg) : (jstring)(*env)->CallStaticObjectMethod(env, cls, m);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        ret = NULL;
    }
    if (ret != NULL) {
        chars = (*env)->GetStringUTFChars(env, ret, NULL);
        if (chars != NULL) {
            out = strdup(chars);
            (*env)->ReleaseStringUTFChars(env, ret, chars);
        }
    }
    return out;
}

static void callVoidMethod(const char *name, const char *uri) {
    jclass cls;
    JNIEnv *env = jniEnv(&cls);
    jmethodID m;

    if (env == NULL) {
        return;
    }
    m = uri != NULL ? (*env)->GetStaticMethodID(env, cls, name, "(Ljava/lang/String;)V") : (*env)->GetStaticMethodID(env, cls, name, "()V");
    if (m == NULL) {
        (*env)->ExceptionClear(env);
        return;
    }
    if (uri != NULL) {
        jstring arg = (*env)->NewStringUTF(env, uri);

        (*env)->CallStaticVoidMethod(env, cls, m, arg);
    } else {
        (*env)->CallStaticVoidMethod(env, cls, m);
    }
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
    }
}

/* Opens the document and gives its descriptor's path; a previous descriptor is closed. 1 on success */
static int openUri(const char *uri, char *path, int cap) {
    jclass cls;
    JNIEnv *env = jniEnv(&cls);
    jmethodID m;
    jint fd;
    jstring arg;

    if (env == NULL || (m = (*env)->GetStaticMethodID(env, cls, "openDisc", "(Ljava/lang/String;)I")) == NULL) {
        if (env != NULL) {
            (*env)->ExceptionClear(env);
        }
        return 0;
    }
    arg = (*env)->NewStringUTF(env, uri);
    fd = (*env)->CallStaticIntMethod(env, cls, m, arg);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        fd = -1;
    }
    if (fd < 0) {
        return 0;
    }
    if (sDiscFd >= 0) {
        close(sDiscFd);
    }
    sDiscFd = (int)fd;
    return snprintf(path, (size_t)cap, "/proc/self/fd/%d", sDiscFd) < cap;
}

static void closeCurrentFd(void) {
    if (sDiscFd >= 0) {
        close(sDiscFd);
        sDiscFd = -1;
    }
}

/* One step of the dialogs: a message with "Salir" / the choose button. 1 = choose, 0 = quit (or no box could be shown) */
static int messageChoose(const char *text, const char *chooseLabel, int es, int error) {
    SDL_MessageBoxButtonData buttons[2];
    SDL_MessageBoxData data;
    int chosen = 0;

    buttons[0].flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT;
    buttons[0].buttonid = 0;
    buttons[0].text = es ? "Salir" : "Quit";
    buttons[1].flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    buttons[1].buttonid = 1;
    buttons[1].text = chooseLabel;
    data.flags = error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION;
    data.window = NULL;
    data.title = "Digimon World 3";
    data.message = text;
    data.numbuttons = 2;
    data.buttons = buttons;
    data.colorScheme = NULL;
    return SDL_ShowMessageBox(&data, &chosen) >= 0 && chosen == 1;
}

/* Explains, opens the picker, validates the pick. Returns 1 with the image's /proc path in path and its URI in uri, 0
   when the player quit. Cancelling or a bad pick shows the reason with "choose again" / "quit": no crash, no busy loop. */
static int androidPick(int es, char *path, int pcap, char *uri, int ucap, char *bin, int cap) {
    DfPhase phase = DF_EXPLAIN;
    DfMsg why = DFM_EXPLAIN;
    char text[900] = "", detected[64];

    while (phase != DF_DONE && phase != DF_QUIT) {
        if (phase == DF_EXPLAIN || phase == DF_CANCELED || phase == DF_REJECTED) {
            int choose;

            if (text[0] == '\0') { /* no specific reason from the validation (dpMessage): the generic one for this phase */
                dfMessage(phase == DF_EXPLAIN ? DFM_EXPLAIN : phase == DF_CANCELED ? DFM_CANCELED : why, es, text, sizeof(text));
            }
            choose = messageChoose(text, phase == DF_EXPLAIN ? (es ? "Elegir archivo" : "Choose file") : (es ? "Elegir otro" : "Choose again"),
                                   es, phase != DF_EXPLAIN);
            phase = dfNext(phase, choose ? DFE_CHOOSE : DFE_QUIT);
            text[0] = '\0';
        } else { /* DF_PICKING */
            char *answer;
            char *name;
            DpResult r;

            callVoidMethod("pickDisc", NULL);
            while ((answer = callStringMethod("pollDisc", NULL)) == NULL) {
                SDL_Delay(100); /* the game thread waits; the UI thread owns the picker */
            }
            if (answer[0] == '\0') {
                free(answer);
                fprintf(stderr, "disc: the picker was cancelled\n");
                phase = dfNext(phase, DFE_CANCEL);
                continue;
            }
            name = callStringMethod("discName", answer);
            if (dfPickIsCue(answer, name)) {
                fprintf(stderr, "disc: a .cue was picked (%s); the .bin beside it is out of reach\n", name != NULL ? name : answer);
                why = DFM_CUE;
                text[0] = '\0';
                callVoidMethod("releaseDisc", answer);
                phase = dfNext(phase, DFE_BAD);
            } else if (!openUri(answer, path, pcap)) {
                fprintf(stderr, "disc: cannot open %s\n", answer);
                why = DFM_UNREADABLE;
                text[0] = '\0';
                callVoidMethod("releaseDisc", answer);
                phase = dfNext(phase, DFE_BAD);
            } else if ((r = dpCheckImage(path, bin, cap, detected, sizeof(detected))) != DP_OK) {
                dpMessage(r, es, detected, text, sizeof(text));
                fprintf(stderr, "disc: %s: %s\n", name != NULL ? name : answer, text);
                why = DFM_UNREADABLE;
                closeCurrentFd();
                callVoidMethod("releaseDisc", answer);
                phase = dfNext(phase, DFE_BAD);
            } else {
                snprintf(uri, (size_t)ucap, "%s", answer);
                fprintf(stderr, "disc: picked %s\n", name != NULL && name[0] != '\0' ? name : answer);
                phase = dfNext(phase, DFE_PICKED);
            }
            free(name);
            free(answer);
        }
    }
    return phase == DF_DONE;
}
#endif

typedef struct {
    int rank, folder;
    char name[256];
    char path[1100];
} Cand;

static int candCmp(const void *a, const void *b) {
    const Cand *x = (const Cand *)a, *y = (const Cand *)b;

    return dpCandidateCmp(x->rank, x->folder, x->name, y->rank, y->folder, y->name);
}

static int isDir(const char *path) {
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int scanFolder(const char *dirPath, int folderIdx, Cand *c, int count, int max, int *subs, char subNames[][256], int maxSubs) {
    DIR *d = opendir(dirPath);
    struct dirent *e;

    if (d == NULL) {
        return count;
    }
    while ((e = readdir(d)) != NULL) {
        char full[1100];
        int rank;

        if (e->d_name[0] == '.' || snprintf(full, sizeof(full), "%s/%s", dirPath, e->d_name) >= (int)sizeof(full)) {
            continue;
        }
        rank = dpCandidateRank(e->d_name);
        if (rank >= 0 && count < max) {
            c[count].rank = rank;
            c[count].folder = folderIdx;
            snprintf(c[count].name, sizeof(c[count].name), "%s", e->d_name);
            snprintf(c[count].path, sizeof(c[count].path), "%s", full);
            count++;
        } else if (rank < 0 && subs != NULL && *subs < maxSubs && isDir(full)) {
            snprintf(subNames[(*subs)++], 256, "%s", e->d_name);
        }
    }
    closedir(d);
    return count;
}

int dpAutoDetect(const char *folder, char *picked, int pcap, char *bin, int cap) {
    static Cand c[256];
    static char subNames[32][256];
    char root[1100], sub[1400], detected[64];
    int count, subs = 0, i;
    size_t n = strlen(folder);

    snprintf(root, sizeof(root), "%s", folder);
    while (n > 1 && isSep(root[n - 1])) {
        root[--n] = '\0';
    }
    count = scanFolder(root, 0, c, 0, 256, &subs, subNames, 32);
    for (i = 0; i < subs; i++) {
        snprintf(sub, sizeof(sub), "%s/%s", root, subNames[i]);
        count = scanFolder(sub, i + 1, c, count, 256, NULL, NULL, 0);
    }
    qsort(c, (size_t)count, sizeof(c[0]), candCmp);
    for (i = 0; i < count; i++) {
        if (dpCheckImage(c[i].path, bin, cap, detected, sizeof(detected)) == DP_OK) {
            return snprintf(picked, (size_t)pcap, "%s", c[i].path) < pcap;
        }
    }
    return 0;
}

int dpToolDisc(const char *arg, char *bin, int cap) {
    char remembered[1024], detected[64];

    if (arg == NULL) {
        if (!inputCfgReadDisc(remembered, sizeof(remembered))) {
            return 0;
        }
        arg = remembered;
    }
    return dpCheckImage(arg, bin, cap, detected, sizeof(detected)) == DP_OK;
}

int dpPlayerStart(char *bin, int cap) {
#ifndef __ANDROID__
    static const char *patterns[] = {"*.bin", "*.cue", "*.iso", "*.BIN", "*.CUE", "*.ISO"};
#endif
    char cand[1024], remembered[1024], detected[64], why[600], exeDir[1024] = "";
    char logs[PP_CAP];
    const char *pick = getenv("DW3_PICK_DISC");
    int es, headless = pick != NULL && pick[0] != '\0';
    int firstPrompt = 1;
    char rememberAs[1100] = ""; /* what settings.ini keeps when it is not the path itself: the document URI (Android) */

    if (portPath(PP_LOGS, logs, sizeof(logs))) {
        startLog(logs);
    }
    es = spanishLocale();
    cand[0] = remembered[0] = '\0';
    if (!inputCfgReadDisc(remembered, sizeof(remembered))) {
        remembered[0] = '\0';
    }
#ifdef __ANDROID__
    /* T7b: a remembered document URI, opened through the permission the system keeps for it. If it no longer opens (moved,
       deleted, permission revoked) or is not the disc, say why in the log and go on to the next source. */
    if (!headless && dfIsContentUri(remembered)) {
        char fdPath[64];

        snprintf(rememberAs, sizeof(rememberAs), "%s", remembered);
        remembered[0] = '\0';
        if (!openUri(rememberAs, fdPath, sizeof(fdPath))) {
            fprintf(stderr, "disc: the remembered document no longer opens (%s)\n", rememberAs);
            rememberAs[0] = '\0';
        } else {
            DpResult r = dpCheckImage(fdPath, bin, cap, detected, sizeof(detected));

            if (r == DP_OK) {
                snprintf(remembered, sizeof(remembered), "%s", fdPath);
            } else {
                dpMessage(r, es, detected, why, sizeof(why));
                fprintf(stderr, "disc: the remembered document is not usable: %s\n", why);
                closeCurrentFd();
                rememberAs[0] = '\0';
            }
        }
    }
#endif
    if (portPathsExeDir() != NULL) {
        snprintf(exeDir, sizeof(exeDir), "%s", portPathsExeDir());
    }
#ifdef __ANDROID__
    /* the folder the player copies the image into is the app's files folder (portPathsAndroidBase) */
    if (portPathsBase() != NULL) {
        snprintf(exeDir, sizeof(exeDir), "%s", portPathsBase());
    }
#endif
    if (remembered[0] != '\0' && exeDir[0] != '\0') {
        char full[1024];

        if (dpResolveStored(exeDir, remembered, full, sizeof(full))) {
            snprintf(remembered, sizeof(remembered), "%s", full);
        }
    }
    if (!headless && exeDir[0] != '\0'
        && (remembered[0] == '\0' || dpCheckImage(remembered, bin, cap, detected, sizeof(detected)) != DP_OK)) {
        /* W2b: nothing (valid) remembered: an image next to the game, or in a subfolder, needs no dialog */
        char found[1024];

        if (dpAutoDetect(exeDir, found, sizeof(found), bin, cap)) {
            fprintf(stderr, "disc: found %s\n", found);
            snprintf(remembered, sizeof(remembered), "%s", found);
        }
    }
    if (headless) {
        snprintf(cand, sizeof(cand), "%s", pick);
    } else {
        snprintf(cand, sizeof(cand), "%s", remembered);
    }
    for (;;) {
#ifndef __ANDROID__
        const char *chosen;
#endif

        if (cand[0] != '\0') {
            DpResult r = dpCheckImage(cand, bin, cap, detected, sizeof(detected));

            if (r == DP_OK) {
                {
                    char rel[1024];

                    if (rememberAs[0] != '\0') {
                        inputCfgRememberDisc(rememberAs);
                    } else
                    /* portable: a path inside the executable's folder is stored relative to it */
                    inputCfgRememberDisc(portPathsIsPortable() && exeDir[0] != '\0'
                                                 && dpRelativeTo(exeDir, cand, rel, sizeof(rel)) ? rel : cand);
                }
                fprintf(stderr, "disc: %s\n", bin);
                return 1;
            }
            dpMessage(r, es, detected, why, sizeof(why));
            fprintf(stderr, "disc: %s: %s\n", cand, why);
#ifdef __ANDROID__
            /* a remembered file that is gone or not the disc: the picker's own message explains what to choose (T7b) */
            if (headless) {
                return 0;
            }
            cand[0] = '\0';
#else
            if (headless || !askAgain(why, es)) {
                return 0;
            }
#endif
            firstPrompt = 0;
        }
        if (headless) {
            return 0;
        }
#ifdef __ANDROID__
        (void)firstPrompt;
        if (!androidPick(es, cand, sizeof(cand), rememberAs, sizeof(rememberAs), bin, cap)) {
            fprintf(stderr, "disc: no image chosen\n");
            return 0;
        }
        continue;
#else
        if (!haveDialogTool()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Digimon World 3",
                                     es ? "No hay un selector de archivos en este sistema (instala zenity o kdialog), o inicia el "
                                          "juego con la ruta de la imagen: boot <disco.bin>"
                                        : "No file picker is available on this system (install zenity or kdialog), or start the game "
                                          "with the image path: boot <disc.bin>",
                                     NULL);
            fprintf(stderr, "disc: no file dialog tool found\n");
            return 0;
        }
        if (firstPrompt) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Digimon World 3",
                                     es ? "Elige la imagen de tu disco de Digimon World 3 (Europa)."
                                        : "Select your Digimon World 3 (Europe) disc image.",
                                     NULL);
            firstPrompt = 0;
        }
        chosen = tinyfd_openFileDialog(es ? "Elige tu disco de Digimon World 3 (Europa)" : "Select your Digimon World 3 (Europe) disc",
                                       "", (int)(sizeof(patterns) / sizeof(patterns[0])), patterns,
                                       es ? "Imagen de disco (.bin, .cue, .iso)" : "Disc image (.bin, .cue, .iso)", 0);
        if (chosen == NULL) {
            fprintf(stderr, "disc: no image chosen\n");
            return 0;
        }
        snprintf(cand, sizeof(cand), "%s", chosen);
#endif
    }
}
