/* T7b: the pure decisions of the Android disc start (see disc_flow.h). No SDL, no JNI. */
#include "disc_flow.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

DfSource dfStartSource(int rememberedOk, int appFolderFound) {
    if (rememberedOk) {
        return DF_FROM_REMEMBERED;
    }
    return appFolderFound ? DF_FROM_APP_FOLDER : DF_ASK;
}

int dfIsContentUri(const char *s) {
    return s != NULL && strncmp(s, "content://", 10) == 0;
}

static int endsCue(const char *s) {
    size_t n = s != NULL ? strlen(s) : 0;

    return n >= 4 && tolower((unsigned char)s[n - 4]) == '.' && tolower((unsigned char)s[n - 3]) == 'c'
        && tolower((unsigned char)s[n - 2]) == 'u' && tolower((unsigned char)s[n - 1]) == 'e';
}

int dfPickIsCue(const char *uri, const char *displayName) {
    if (displayName != NULL && displayName[0] != '\0') {
        return endsCue(displayName);
    }
    return endsCue(uri);
}

DfPhase dfNext(DfPhase phase, DfEvent ev) {
    switch (phase) {
    case DF_EXPLAIN:
        return ev == DFE_CHOOSE ? DF_PICKING : ev == DFE_QUIT ? DF_QUIT : phase;
    case DF_PICKING:
        return ev == DFE_PICKED ? DF_DONE : ev == DFE_CANCEL ? DF_CANCELED : ev == DFE_BAD ? DF_REJECTED : phase;
    case DF_CANCELED:
    case DF_REJECTED:
        return ev == DFE_CHOOSE ? DF_PICKING : ev == DFE_QUIT ? DF_QUIT : phase;
    default:
        return phase;
    }
}

const char *dfMessage(DfMsg m, int es, char *buf, int cap) {
    const char *text;

    switch (m) {
    case DFM_EXPLAIN:
        text = es ? "Elige la imagen .bin del juego (por ejemplo en Descargas). Digimon World 3 (Europa) se lee desde donde est\xC3\xA9, sin copiarla."
                  : "Choose the game's .bin image (for example in Downloads). Digimon World 3 (Europe) is read from where it is, without copying it.";
        break;
    case DFM_CANCELED:
        text = es ? "No se eligi\xC3\xB3 ning\xC3\xBAn archivo. Sin la imagen .bin del disco el juego no puede iniciar."
                  : "No file was chosen. The game cannot start without the disc's .bin image.";
        break;
    case DFM_CUE:
        text = es ? "Elegiste un archivo .cue, y desde \xC3\xA9l no se puede llegar al .bin. Elige el archivo .bin (o .iso) del disco."
                  : "You chose a .cue file, and the .bin beside it cannot be reached from it. Choose the disc's .bin (or .iso) file instead.";
        break;
    default:
        text = es ? "No se pudo abrir ese archivo. Comprueba que existe y vuelve a elegirlo."
                  : "That file could not be opened. Check that it exists and choose it again.";
        break;
    }
    snprintf(buf, (size_t)cap, "%s", text);
    return buf;
}
