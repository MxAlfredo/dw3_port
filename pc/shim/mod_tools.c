/*
 * The mod tools (U5): --list-mods, --new-mod and the validator. See mod_tools.h.
 */
#include "mod_tools.h"

#include "mods.h"
#include "table_mods.h"
#include "text_data.h"

#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <strings.h>
#include <unistd.h>

/* ---- the command line ---- */

int modToolsTakeArgs(int *argc, char **argv, ModToolsCmd *cmd)
{
    int i, out = 1, found = 0, haveKind = 0;

    memset(cmd, 0, sizeof *cmd);
    if (!argc || !argv) return 0;
    for (i = 1; i < *argc; i++) {
        const char *a = argv[i];
        int takes = !strcmp(a, "--validate-mod") || !strcmp(a, "--new-mod") || !strcmp(a, "--kind");
        if (!strcmp(a, "--list-mods") || !strcmp(a, "--validate-mods")) {
            found = 1;
            if (!cmd->kind) cmd->kind = !strcmp(a, "--list-mods") ? MT_LIST : MT_VALIDATE_ALL;
        } else if (takes) {
            found = 1;
            if (i + 1 >= *argc || !strncmp(argv[i + 1], "--", 2)) {
                if (!cmd->err[0]) snprintf(cmd->err, sizeof cmd->err, "%s needs a value", a);
                continue;
            }
            i++;
            if (!strcmp(a, "--kind")) {
                haveKind = 1;
                snprintf(cmd->kinds, sizeof cmd->kinds, "%.95s", argv[i]);
            } else {
                cmd->kind = !strcmp(a, "--new-mod") ? MT_NEW : MT_VALIDATE;
                snprintf(cmd->name, sizeof cmd->name, "%.299s", argv[i]);
            }
        } else {
            argv[out++] = argv[i];
        }
    }
    if (found) {
        argv[out] = NULL;
        *argc = out;
        if (haveKind && cmd->kind != MT_NEW && !cmd->err[0]) snprintf(cmd->err, sizeof cmd->err, "--kind only goes with --new-mod");
        if (!cmd->kind && !cmd->err[0]) snprintf(cmd->err, sizeof cmd->err, "no tool flag");
    }
    return found;
}

int modToolsSplitTarget(const char *target, char *root, size_t rootCap, char *name, size_t nameCap)
{
    const char *slash = NULL, *p;
    size_t n;

    root[0] = name[0] = 0;
    for (p = target; *p; p++) {
        if (*p == '/' || *p == '\\') slash = p;
    }
    if (!slash) {
        snprintf(name, nameCap, "%s", target);
        return 0;
    }
    n = (size_t)(slash - target);
    if (slash[1] == 0) { /* a trailing slash: "mods/demo/" */
        const char *q = slash;
        while (q > target && (q[-1] != '/' && q[-1] != '\\')) q--;
        snprintf(name, nameCap, "%.*s", (int)(slash - q), q);
        n = q > target ? (size_t)(q - 1 - target) : 0;
    } else {
        snprintf(name, nameCap, "%s", slash + 1);
    }
    if (n == 0) snprintf(root, rootCap, "%s", "/");
    else snprintf(root, rootCap, "%.*s", (int)n, target);
    return 1;
}

/* ---- --list-mods ---- */

static const char *stateWord(int status)
{
    switch (status) {
    case MODS_REP_ACTIVE: return "enabled";
    case MODS_REP_DISABLED: return "disabled";
    case MODS_REP_REJECTED: return "rejected";
    case MODS_REP_NOT_LISTED: return "off";
    default: return "missing";
    }
}

int modToolsList(const char *root, FILE *out)
{
    int i, n = modsReportCount(), active = 0;

    for (i = 0; i < n; i++) active += modsReportAt(i)->status == MODS_REP_ACTIVE;
    if (n == 0) {
        fprintf(out, "no mods in %s\n", (root && root[0]) ? root : "(no mods folder)");
        fprintf(out, "make one with: --new-mod <name>\n");
        return 0;
    }
    fprintf(out, "%d mod%s in %s, %d enabled\n", n, n == 1 ? "" : "s", (root && root[0]) ? root : "?", active);
    fprintf(out, "%-4s %-28s %-9s %-8s %s\n", "pos", "name", "version", "kind", "state");
    for (i = 0; i < n; i++) {
        const ModsReport *r = modsReportAt(i);
        char pos[12];
        if (r->position > 0) snprintf(pos, sizeof pos, "%d", r->position);
        else snprintf(pos, sizeof pos, "-");
        fprintf(out, "%-4s %-28s %-9s %-8s %s%s%s\n", pos, r->name, r->status == MODS_REP_REJECTED || r->status == MODS_REP_MISSING ? "-" : r->version,
                r->status == MODS_REP_REJECTED || r->status == MODS_REP_MISSING ? "-" : r->kind, stateWord(r->status), r->reason[0] ? ": " : "",
                r->reason);
    }
    return 0;
}

/* ---- --new-mod and the template ---- */

static const char kModJson[] =
    "{\n"
    "  \"format\": \"mod-v1\",\n"
    "  \"name\": \"@NAME@\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"author\": \"your name\",\n"
    "  \"description\": \"What it does, in one line.\",\n"
    "  \"kind\": \"@KIND@\",\n"
    "  \"port\": \">=0.4\"\n"
    "}\n";

static const char kReadme[] =
    "Mod del port de Digimon World 3\n"
    "================================\n"
    "\n"
    "Esta carpeta es un mod. Instalarlo es copiarla en <preferencias>/mods/; quitarlo es borrarla.\n"
    "El puerto nunca escribe aqui. Un archivo que el puerto rechaza queda en el log (carpeta logs/) con el motivo.\n"
    "\n"
    "mod.json            obligatorio. \"format\": \"mod-v1\"; \"kind\": \"assets\" (reemplaza recursos) o \"content\"\n"
    "                    (agrega objetos, tecnicas, Digimon o cartas). \"version\" son digitos y puntos (1.2.0).\n"
    "                    \"port\": \">=0.4\" pide una version minima del puerto; \"requires\": [\"otro-mod\"] lo\n"
    "                    hace depender de otros mods (por nombre de carpeta).\n"
    "tables/<tabla>.json  parches de tablas del juego (recompensas, botin, precios, tiendas...).\n"
    "content/<tipo>.json  contenido nuevo: items.json, techs.json, digimon.json, cards.json (solo con \"kind\": \"content\").\n"
    "card/<llave>.png    arte de las cartas nuevas de content/cards.json (PNG indexado 32x32).\n"
    "config/              tiempos y banner (stage_banner.json, transitions.json); solo las claves que cambias.\n"
    "text/<idioma>/       textos del juego, un JSON por archivo de texto.\n"
    "images/              imagenes PNG indexadas, con el mismo tamano que la original.\n"
    "audio/<BANCO>/       efectos (vag_NNN.wav) y musica (seq_P_RR.wav) en WAV de 16 bits.\n"
    "video/<NOMBRE>.avi   peliculas AVI con MJPEG.\n"
    "files/               archivos del disco, sin cambios de formato, por su ruta sin el AAA/.\n"
    "\n"
    "Probar el mod sin abrir el juego:\n"
    "  boot \"<disco>.bin\" --validate-mod <nombre>     (un problema por linea; codigo de salida distinto de 0 si hay errores)\n"
    "  boot --list-mods                               (que mods hay, si estan activos y en que orden)\n"
    "\n"
    "Orden de carga: en settings.ini, [mods] order = a, b (el ultimo gana). Sin esa clave, todos en orden alfabetico.\n"
    "La guia completa esta en docs/modding/README.md (README.en.md en ingles). No incluyas datos del juego\n"
    "(textos, imagenes, audio) en un mod que compartas.\n";

const char *modToolsTemplateFile(int i, const char **text)
{
    if (i == 0) { *text = kModJson; return "mod.json"; }
    if (i == 1) { *text = kReadme; return "README.txt"; }
    return NULL;
}

static int validName(const char *name)
{
    size_t i, n = strlen(name);
    if (n == 0 || n >= 64 || name[0] == '.') return 0;
    for (i = 0; i < n; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return 0;
    }
    return 1;
}

static const char *const kFolders[] = {"config", "content", "tables", "text", "images", "audio", "video", "files"};
#define NFOLDERS ((int)(sizeof kFolders / sizeof kFolders[0]))

/* "content,tables" into which folders to make and whether the mod is a content mod; 0, or -1 with the unknown word in bad */
static int parseKinds(const char *kinds, int make[NFOLDERS], int *content, char *bad, size_t badCap)
{
    char buf[100], *tok, *save = NULL;
    int i, any = 0;

    memset(make, 0, NFOLDERS * sizeof(int));
    *content = 0;
    if (!kinds || !kinds[0]) kinds = "assets";
    snprintf(buf, sizeof buf, "%s", kinds);
    for (tok = strtok_r(buf, ", ", &save); tok; tok = strtok_r(NULL, ", ", &save)) {
        int known = 0;
        any = 1;
        if (!strcmp(tok, "assets")) {
            known = 1;
            for (i = 0; i < NFOLDERS; i++) make[i] |= strcmp(kFolders[i], "content") != 0;
        } else if (!strcmp(tok, "content")) {
            known = 1;
            *content = 1;
            for (i = 0; i < NFOLDERS; i++) make[i] |= !strcmp(kFolders[i], "content") || !strcmp(kFolders[i], "tables");
        } else {
            for (i = 0; i < NFOLDERS; i++) {
                if (!strcmp(tok, kFolders[i])) {
                    known = 1;
                    make[i] = 1;
                    if (!strcmp(tok, "content")) *content = 1;
                }
            }
        }
        if (!known) {
            snprintf(bad, badCap, "%s", tok);
            return -1;
        }
    }
    return any ? 0 : -1;
}

static char *fill(const char *tpl, const char *name, const char *kind)
{
    size_t cap = strlen(tpl) + 2 * (strlen(name) + strlen(kind)) + 8, n = 0;
    char *out = (char *)malloc(cap);
    const char *p;
    if (!out) return NULL;
    for (p = tpl; *p;) {
        const char *with = NULL;
        if (!strncmp(p, "@NAME@", 6)) with = name;
        else if (!strncmp(p, "@KIND@", 6)) with = kind;
        if (with) {
            size_t k = strlen(with);
            memcpy(out + n, with, k);
            n += k;
            p += 6;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return out;
}

/* a folder of the mods root whose name equals `name` ignoring case (so Foo and foo never both exist): its real name, or "" */
static void caseClash(const char *root, const char *name, char *found, size_t cap)
{
    DIR *d = opendir(root);
    struct dirent *e;
    found[0] = 0;
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        const char *a = e->d_name, *b = name;
        while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) { a++; b++; }
        if (!*a && !*b) snprintf(found, cap, "%s", e->d_name);
    }
    closedir(d);
}

int modToolsNew(const char *root, const char *name, const char *kinds, FILE *out, FILE *err)
{
    int make[NFOLDERS], content, i;
    char bad[100] = "", dir[1300], path[1400], clash[300];
    struct stat st;

    if (!root || !root[0]) {
        fprintf(err, "new-mod: there is no mods folder to put it in\n");
        return 1;
    }
    if (!validName(name)) {
        fprintf(err, "new-mod: \"%s\" is not a usable folder name (1-63 letters, digits, . _ -, not starting with a dot)\n", name);
        return 1;
    }
    if (parseKinds(kinds, make, &content, bad, sizeof bad)) {
        if (bad[0]) fprintf(err, "new-mod: unknown kind \"%s\" (assets, content, or: config content tables text images audio video files)\n", bad);
        else fprintf(err, "new-mod: --kind is empty\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s/%s", root, name);
    if (stat(dir, &st) == 0) {
        fprintf(err, "new-mod: %s already exists; nothing was touched\n", dir);
        return 1;
    }
    caseClash(root, name, clash, sizeof clash);
    if (clash[0]) {
        fprintf(err, "new-mod: %s/%s already exists and differs only in case from \"%s\"; nothing was touched\n", root, clash, name);
        return 1;
    }
    if (tdMakeDirs(dir)) {
        fprintf(err, "new-mod: cannot create %s\n", dir);
        return 1;
    }
    for (i = 0;; i++) {
        const char *text;
        const char *file = modToolsTemplateFile(i, &text);
        char *body;
        FILE *f;
        if (!file) break;
        body = fill(text, name, content ? "content" : "assets");
        snprintf(path, sizeof path, "%s/%s", dir, file);
        f = body ? fopen(path, "wb") : NULL;
        if (!f) {
            fprintf(err, "new-mod: cannot write %s\n", path);
            free(body);
            return 1;
        }
        fputs(body, f);
        fclose(f);
        free(body);
    }
    for (i = 0; i < NFOLDERS; i++) {
        if (!make[i]) continue;
        snprintf(path, sizeof path, "%s/%s", dir, kFolders[i]);
        if (tdMakeDirs(path)) {
            fprintf(err, "new-mod: cannot create %s\n", path);
            return 1;
        }
    }
    fprintf(out, "created %s\n", dir);
    fprintf(out, "  mod.json and README.txt, kind \"%s\"; folders:", content ? "content" : "assets");
    for (i = 0; i < NFOLDERS; i++) {
        if (make[i]) fprintf(out, " %s/", kFolders[i]);
    }
    fprintf(out, "\n  next: edit mod.json, add a file, then --validate-mod %s\n", name);
    return 0;
}

/* ---- capturing the log ---- */

static int gSavedErr = -1;
static FILE *gCapture;

void modToolsCaptureBegin(void)
{
    if (gSavedErr >= 0) return;
    fflush(stderr);
    gCapture = tmpfile();
    if (!gCapture) return;
    gSavedErr = dup(2);
    if (gSavedErr < 0 || dup2(fileno(gCapture), 2) < 0) {
        if (gSavedErr >= 0) close(gSavedErr);
        gSavedErr = -1;
        fclose(gCapture);
        gCapture = NULL;
    }
}

/* the captured text (malloc'ed, never NULL), the capture off */
static char *captureEnd(void)
{
    char *text;
    long n;
    if (gSavedErr < 0 || !gCapture) return strdup("");
    fflush(stderr);
    dup2(gSavedErr, 2);
    close(gSavedErr);
    gSavedErr = -1;
    fseek(gCapture, 0, SEEK_END);
    n = ftell(gCapture);
    rewind(gCapture);
    text = (char *)malloc((size_t)(n > 0 ? n : 0) + 1);
    if (text) {
        size_t got = n > 0 ? fread(text, 1, (size_t)n, gCapture) : 0;
        text[got] = 0;
    }
    fclose(gCapture);
    gCapture = NULL;
    return text ? text : strdup("");
}

void modToolsCaptureEnd(void) { free(captureEnd()); }

/* ---- the validator ---- */

#define MT_MAX_PROBLEMS 2000

typedef struct Problem {
    int mod;         /* index in the report, or -1: not tied to a mod */
    int warning;     /* 0 error, 1 warning, 2 not checked */
    char text[500];
} Problem;

static Problem *gProb;
static int gProbN;

static void addProblem(int mod, int level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void addProblem(int mod, int level, const char *fmt, ...)
{
    va_list ap;
    Problem *p;
    int i;
    if (gProbN >= MT_MAX_PROBLEMS) return;
    p = &gProb[gProbN];
    p->mod = mod;
    p->warning = level;
    va_start(ap, fmt);
    vsnprintf(p->text, sizeof p->text, fmt, ap);
    va_end(ap);
    for (i = 0; i < gProbN; i++) { /* the same line twice (a loader that logs on every read) is one problem */
        if (gProb[i].mod == mod && gProb[i].warning == level && !strcmp(gProb[i].text, p->text)) return;
    }
    gProbN++;
}

static int isDirPath(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int nameChar(int c) { return isalnum(c) || c == '-' || c == '_' || c == '.'; }

/* `text` mentions folder `name` as "mods/<name>" or "<name>:" at a word start, or `dir` */
static int mentions(const char *text, const ModsReport *r)
{
    const char *p;
    size_t n = strlen(r->name);
    if (r->dir[0] && strstr(text, r->dir)) return 1;
    for (p = text; (p = strstr(p, r->name)) != NULL; p++) {
        int afterMods = p - text >= 5 && !strncmp(p - 5, "mods/", 5);
        if (afterMods && !nameChar((unsigned char)p[n])) return 1;
        if (p == text && (p[n] == ':' || p[n] == ' ')) return 1; /* "<mod>: <file> rejected" */
        if (p > text && p[-1] == ' ' && p[n] == ':') return 1;
    }
    return 0;
}

static int looksLikeProblem(const char *m)
{
    static const char *const words[] = {" rejected", " disabled", " refused", " ignored", " not registered", " dropped", "cannot ", " skipped",
                                       " invalid", " unknown ", "failed"};
    size_t i;
    for (i = 0; i < sizeof words / sizeof words[0]; i++) {
        if (!strstr(m, words[i])) continue;
        if (!strcmp(words[i], "failed") && strstr(m, "0 failed")) continue; /* a summary that counts no failure */
        return 1;
    }
    return 0;
}

/* "mods: ..." lines repeat what the report says; a line of a loader goes to the mod it names */
static void classifyLog(const char *log, const char *only)
{
    const char *p = log;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        char line[700];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p = nl ? nl + 1 : p + len;
        if (line[0] == '[' && strchr(line, ']') && looksLikeProblem(line) && strncmp(line, "[mods]", 6)) {
            const char *msg = strchr(line, ']') + 1;
            int i, owner = -1, n = modsReportCount();
            while (*msg == ' ') msg++;
            for (i = 0; i < n && owner < 0; i++) {
                const ModsReport *r = modsReportAt(i);
                if (r->status != MODS_REP_MISSING && mentions(msg, r)) owner = i;
            }
            if (owner >= 0) {
                const ModsReport *r = modsReportAt(owner);
                char shown[700];
                const char *at;
                snprintf(shown, sizeof shown, "%s", msg);
                /* "<dir>/tables/x.json" -> "tables/x.json" */
                while (r->dir[0] && (at = strstr(shown, r->dir)) != NULL) {
                    size_t dl = strlen(r->dir);
                    size_t skip = dl + (at[dl] == '/' ? 1 : 0);
                    memmove((char *)at, at + skip, strlen(at + skip) + 1);
                }
                if (!strncmp(shown, r->name, strlen(r->name)) && shown[strlen(r->name)] == ':' && shown[strlen(r->name) + 1] == ' ')
                    memmove(shown, shown + strlen(r->name) + 2, strlen(shown + strlen(r->name) + 2) + 1); /* "<mod>: " is the mod's own header */
                if (only && strcmp(only, r->name)) continue;
                addProblem(owner, strstr(shown, " ignored") || strstr(shown, " skipped") ? 1 : 0, "%s", shown);
            } else if (!only) {
                addProblem(-1, 1, "%s", line);
            }
        }
    }
}

/* ---- what the loaders skip without a word ---- */

static int editDistance(const char *a, const char *b)
{
    int la = (int)strlen(a), lb = (int)strlen(b), i, j;
    int d[64][64];
    if (la > 63 || lb > 63) return 99;
    for (i = 0; i <= la; i++) d[i][0] = i;
    for (j = 0; j <= lb; j++) d[0][j] = j;
    for (i = 1; i <= la; i++) {
        for (j = 1; j <= lb; j++) {
            int c = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            if (d[i - 1][j] + 1 < c) c = d[i - 1][j] + 1;
            if (d[i][j - 1] + 1 < c) c = d[i][j - 1] + 1;
            d[i][j] = c;
        }
    }
    return d[la][lb];
}

static const char *nearest(const char *word, const char *const *list, int n)
{
    int i, best = 4;
    const char *pick = NULL;
    for (i = 0; i < n; i++) {
        int d = editDistance(word, list[i]);
        if (d < best) { best = d; pick = list[i]; }
    }
    return pick;
}

static const char *const kKnownFolders[] = {"config", "content", "card", "tables", "text", "images", "files", "video", "audio", "z_stage", "stages"};
static const char *const kContentFiles[] = {"items", "techs", "digimon", "cards"};
static const char *const kModKeys[] = {"format", "name", "version", "author", "description", "kind", "port", "requires", "$schema"};

static int hasStem(const char *file, char *stem, size_t cap)
{
    size_t n = strlen(file);
    if (n < 6 || strcmp(file + n - 5, ".json")) return 0;
    if (n - 5 >= cap) return 0;
    memcpy(stem, file, n - 5);
    stem[n - 5] = 0;
    return 1;
}

static void checkModJson(int mi, const ModsReport *r)
{
    char path[1300], err[160];
    uint8_t *data;
    size_t len;
    PjVal v[PJ_MAX_KEYS];
    int n, i, k;

    snprintf(path, sizeof path, "%s/mod.json", r->dir);
    if (tdReadFile(path, &data, &len)) return;
    n = pjParseFlat((const char *)data, (unsigned long)len, v, PJ_MAX_KEYS, err, sizeof err);
    free(data);
    for (i = 0; i < n; i++) {
        int known = 0;
        for (k = 0; k < (int)(sizeof kModKeys / sizeof kModKeys[0]); k++) known |= !strcmp(v[i].key, kModKeys[k]);
        if (!known) {
            const char *sug = nearest(v[i].key, kModKeys, (int)(sizeof kModKeys / sizeof kModKeys[0]));
            if (sug) addProblem(mi, 1, "mod.json key \"%s\" is ignored (did you mean \"%s\"?)", v[i].key, sug);
            else addProblem(mi, 1, "mod.json key \"%s\" is ignored", v[i].key);
        }
    }
}

static const char *notCheckedWhy(const char *folder)
{
    if (!strcmp(folder, "text")) return "the game reads each text file when it opens it";
    if (!strcmp(folder, "z_stage")) return "the tile's palette is checked while the map loads (the PNG format and, with exported files, the size are checked here)";
    if (!strcmp(folder, "images")) return "needs the disc (run with the disc image)";
    if (!strcmp(folder, "audio") || !strcmp(folder, "video")) return "needs the disc and the sound/video code (run with the disc image)";
    return "only a running game reads it";
}

static void checkTree(int mi, const ModsReport *r, const ModToolsLayers *L);

static void walkFiles(int mi, const ModsReport *r, const char *rel, const ModToolsLayers *L)
{
    char dir[1500];
    DIR *d;
    struct dirent *e;
    snprintf(dir, sizeof dir, "%s/files%s%s", r->dir, rel[0] ? "/" : "", rel);
    d = opendir(dir);
    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        char sub[400], full[1700];
        if (e->d_name[0] == '.') continue;
        snprintf(sub, sizeof sub, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(full, sizeof full, "%s/files/%s", r->dir, sub);
        if (isDirPath(full)) {
            walkFiles(mi, r, sub, L);
        } else {
            char disc[420];
            size_t i;
            snprintf(disc, sizeof disc, "AAA/%s", sub);
            for (i = 0; disc[i]; i++) disc[i] = (char)toupper((unsigned char)disc[i]);
            if (!L->discHas(L->ud, disc)) addProblem(mi, 0, "files/%s: no such file on the disc (looked for %s)", sub, disc);
        }
    }
    closedir(d);
}

static int covers(const ModToolsLayers *L, const char *folder)
{
    const char *p = L->covered;
    size_t n = strlen(folder);
    while (p && *p) {
        if (!strncmp(p, folder, n) && (p[n] == ',' || p[n] == 0)) return 1;
        p = strchr(p, ',');
        if (p) p++;
    }
    return 0;
}

static void checkTree(int mi, const ModsReport *r, const ModToolsLayers *L)
{
    DIR *d = opendir(r->dir);
    struct dirent *e;
    char path[1400];
    int k;

    if (!d) return;
    while ((e = readdir(d)) != NULL) {
        int known = 0;
        if (e->d_name[0] == '.' || !strcmp(e->d_name, "mod.json")) continue;
        snprintf(path, sizeof path, "%s/%s", r->dir, e->d_name);
        if (!isDirPath(path)) continue; /* README and the like */
        for (k = 0; k < (int)(sizeof kKnownFolders / sizeof kKnownFolders[0]); k++) known |= !strcmp(e->d_name, kKnownFolders[k]);
        if (!known) {
            const char *sug = nearest(e->d_name, kKnownFolders, (int)(sizeof kKnownFolders / sizeof kKnownFolders[0]));
            if (sug) addProblem(mi, 1, "folder %s/ is never read (did you mean %s/?)", e->d_name, sug);
            else addProblem(mi, 1, "folder %s/ is never read", e->d_name);
            continue;
        }
        if (!strcmp(e->d_name, "files")) {
            if (L->discHas) walkFiles(mi, r, "", L);
            else addProblem(mi, 2, "files/ not checked: needs the disc (run with the disc image)");
        } else if (!covers(L, e->d_name)) {
            addProblem(mi, 2, "%s/ not checked: %s", e->d_name, notCheckedWhy(e->d_name));
        }
    }
    closedir(d);

    /* tables/<name>.json must name a registered table */
    snprintf(path, sizeof path, "%s/tables", r->dir);
    if ((d = opendir(path)) != NULL) {
        const char *names[256];
        int nn = tmTableCount() < 256 ? tmTableCount() : 256;
        for (k = 0; k < nn; k++) names[k] = tmTableName(k);
        while ((e = readdir(d)) != NULL) {
            char stem[80];
            int found = 0;
            if (e->d_name[0] == '.') continue;
            if (!hasStem(e->d_name, stem, sizeof stem)) {
                addProblem(mi, 1, "tables/%s is ignored (a patch is tables/<table>.json)", e->d_name);
                continue;
            }
            if (nn == 0) {
                addProblem(mi, 2, "tables/%s not checked: no table is registered in this run", e->d_name);
                continue;
            }
            for (k = 0; k < nn; k++) found |= !strcmp(stem, names[k]);
            if (!found) {
                const char *sug = nearest(stem, names, nn);
                if (sug) addProblem(mi, 0, "tables/%s: there is no table called \"%s\" (did you mean \"%s\"?)", e->d_name, stem, sug);
                else addProblem(mi, 0, "tables/%s: there is no table called \"%s\"", e->d_name, stem);
            }
        }
        closedir(d);
    }
    /* stages/<number>.json: a stage patch (stage-patch-v1); what is inside is checked by the loader (its "rejected" lines), the names here */
    snprintf(path, sizeof path, "%s/stages", r->dir);
    if ((d = opendir(path)) != NULL) {
        while ((e = readdir(d)) != NULL) {
            char stem[80];
            size_t i;
            int digits;
            if (e->d_name[0] == '.') continue;
            digits = hasStem(e->d_name, stem, sizeof stem) && stem[0] != 0 && strlen(stem) <= 4;
            for (i = 0; digits && stem[i]; i++) digits = isdigit((unsigned char)stem[i]) != 0;
            if (!digits) addProblem(mi, 1, "stages/%s is ignored (a stage patch is stages/<stage number>.json, e.g. stages/330.json)", e->d_name);
        }
        closedir(d);
    }
    /* content/<kind>.json: only a "content" mod is read, and only these files */
    snprintf(path, sizeof path, "%s/content", r->dir);
    if ((d = opendir(path)) != NULL) {
        if (strcmp(r->kind, "content")) addProblem(mi, 1, "content/ is ignored: this mod's kind is \"%s\" (set \"kind\": \"content\" in mod.json)", r->kind);
        while ((e = readdir(d)) != NULL) {
            char stem[80], sub[1700];
            int found = 0;
            if (e->d_name[0] == '.') continue;
            snprintf(sub, sizeof sub, "%s/%s", path, e->d_name);
            if (isDirPath(sub)) continue; /* content/models/ and the like belong to a type */
            if (!hasStem(e->d_name, stem, sizeof stem)) continue;
            for (k = 0; k < (int)(sizeof kContentFiles / sizeof kContentFiles[0]); k++) found |= !strcmp(stem, kContentFiles[k]);
            if (!found) {
                const char *sug = nearest(stem, kContentFiles, (int)(sizeof kContentFiles / sizeof kContentFiles[0]));
                if (sug) addProblem(mi, 1, "content/%s is ignored (did you mean content/%s.json?)", e->d_name, sug);
                else addProblem(mi, 1, "content/%s is ignored (the kinds are items, techs, digimon, cards)", e->d_name);
            }
        }
        closedir(d);
    }
}

int modToolsValidate(const char *target, const ModToolsLayers *layers, FILE *out)
{
    char *log;
    int i, j2, n = modsReportCount(), shown = 0, errMods = 0, errors = 0, warns = 0, unchecked = 0, targetIdx = -1;
    static const ModToolsLayers none = {NULL, NULL, "", NULL};

    if (!layers) layers = &none;
    if (layers->run) layers->run(layers->ud);
    log = captureEnd();
    if (target) {
        for (i = 0; i < n; i++) {
            if (!strcmp(modsReportAt(i)->name, target) && modsReportAt(i)->status != MODS_REP_MISSING) targetIdx = i;
        }
        if (targetIdx < 0) {
            fprintf(out, "validate: no mod called \"%s\" (--list-mods shows what is installed)\n", target);
            free(log);
            return 2;
        }
    }
    gProb = (Problem *)calloc(MT_MAX_PROBLEMS, sizeof(Problem));
    gProbN = 0;
    if (!gProb) {
        free(log);
        return 2;
    }
    for (i = 0; i < n; i++) {
        const ModsReport *r = modsReportAt(i);
        if ((target && i != targetIdx) || r->status == MODS_REP_MISSING) continue;
        shown++;
        for (j2 = 0; j2 < n; j2++) { /* two folders that differ only in case: fine on Linux, one folder on Windows */
            const ModsReport *o = modsReportAt(j2);
            if (j2 > i && o->status != MODS_REP_MISSING && !strcasecmp(o->name, r->name))
                addProblem(j2, 1, "the folder name differs from \"%s\" only in case; Windows and macOS cannot hold both", r->name);
        }
        if (r->status == MODS_REP_REJECTED) {
            addProblem(i, 0, "%s%s", strncmp(r->reason, "mod.json", 8) ? "mod.json: " : "", r->reason);
            continue;
        }
        if (r->status == MODS_REP_DISABLED) addProblem(i, 0, "mod is disabled: %s", r->reason);
        checkModJson(i, r);
        checkTree(i, r, layers);
    }
    classifyLog(log, target ? modsReportAt(targetIdx)->name : NULL);
    free(log);

    fprintf(out, "validating %d mod%s\n", shown, shown == 1 ? "" : "s");
    for (i = 0; i < n; i++) {
        const ModsReport *r = modsReportAt(i);
        int j, e = 0, w = 0, u = 0;
        if ((target && i != targetIdx) || r->status == MODS_REP_MISSING) continue;
        if (r->status == MODS_REP_REJECTED) fprintf(out, "mod %s\n", r->name);
        else fprintf(out, "mod %s (%s, %s%s)\n", r->name, r->version, r->kind, r->position ? "" : ", not enabled");
        for (j = 0; j < gProbN; j++) {
            if (gProb[j].mod != i) continue;
            fprintf(out, "  %s %s\n", gProb[j].warning == 0 ? "error  " : gProb[j].warning == 1 ? "warning" : "skipped", gProb[j].text);
            if (gProb[j].warning == 0) e++;
            else if (gProb[j].warning == 1) w++;
            else u++;
        }
        if (e) errMods++;
        errors += e;
        warns += w;
        unchecked += u;
        if (!e && !w && !u) fprintf(out, "  ok\n");
        else if (!e) fprintf(out, "  ok%s\n", w ? " (with warnings)" : "");
    }
    {
        int any = 0;
        for (i = 0; i < gProbN; i++) {
            if (gProb[i].mod >= 0) continue;
            if (!any) fprintf(out, "not tied to a mod\n");
            any = 1;
            fprintf(out, "  note    %s\n", gProb[i].text);
        }
    }
    fprintf(out, "summary: %d mod%s, %d with errors, %d error%s, %d warning%s, %d layer%s not checked\n", shown, shown == 1 ? "" : "s", errMods, errors,
            errors == 1 ? "" : "s", warns, warns == 1 ? "" : "s", unchecked, unchecked == 1 ? "" : "s");
    free(gProb);
    gProb = NULL;
    return errors ? 1 : 0;
}
