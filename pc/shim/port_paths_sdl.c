/*
 * The platform part of the path layer (see port_paths.h): where the executable is and where the platform keeps
 * per-user files. This is the single place with a per-platform branch; on Android it is the app's files folder.
 */
#include <stdio.h>

#include <SDL.h>

#include "port_paths.h"
#ifdef __ANDROID__
#include <stdlib.h>
#include "l10n_install.h"
#endif

int portPathsPlatform(char *exeDir, int exeCap, char *pref, int prefCap) {
    exeDir[0] = '\0';
    if (prefCap > 0) {
        pref[0] = '\0';
    }
#ifdef __ANDROID__
    /* No executable folder and no portable mode: the app's own files folder, which a PC reaches over USB as
       Android/data/<id>/files (data, saves, logs, mods, settings, and the disc image). Created by SDL when asked. */
    (void)exeCap;
    if (prefCap <= 0) {
        return 0;
    }
    return portPathsAndroidBase(SDL_AndroidGetExternalStoragePath(), SDL_AndroidGetInternalStoragePath(), pref, prefCap);
#else
    char *base = SDL_GetBasePath();
    char *dir;
    int ok = 0;


    if (base != NULL) {
        snprintf(exeDir, (size_t)exeCap, "%s", base);
        SDL_free(base);
    }
    if (prefCap <= 0) {
        return 0;
    }
    dir = SDL_GetPrefPath("dw3port", "dw3");
    if (dir != NULL) {
        ok = snprintf(pref, (size_t)prefCap, "%s", dir) < prefCap;
        SDL_free(dir);
    }
    return ok;
#endif
}

#ifdef __ANDROID__
/* SDL_RWFromFile reads a relative name from the APK's assets on Android */
static int assetRead(const char *name, uint8_t **data, size_t *len) {
    SDL_RWops *rw = SDL_RWFromFile(name, "rb");
    Sint64 n;

    if (rw == NULL) {
        return -1;
    }
    n = SDL_RWsize(rw);
    *data = n >= 0 ? (uint8_t *)malloc((size_t)n + 1) : NULL;
    if (*data == NULL || SDL_RWread(rw, *data, 1, (size_t)n) != (size_t)n) {
        free(*data);
        *data = NULL;
        SDL_RWclose(rw);
        return -1;
    }
    SDL_RWclose(rw);
    *len = (size_t)n;
    return 0;
}

int l10nInstallAssets(void) {
    const char *base = portPathsBase();

    return base != NULL ? l10nInstall("localization/", base, assetRead) : 0;
}
#endif
