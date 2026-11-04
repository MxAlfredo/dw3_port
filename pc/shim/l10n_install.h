#ifndef PC_L10N_INSTALL_H
#define PC_L10N_INSTALL_H

/*
 * Android ships the es-419 overlay inside the APK (assets/localization/) and copies it into the user folder at start,
 * where portPathsL10n finds it. SDL cannot list assets, so the build writes
 * localization/index.txt: a line "stamp <text>" (changes with the content), then one "<size> <relative path>" line per
 * file. Pure C: the reader of the assets is a callback (SDL_RWFromFile on Android, a plain read in the host test).
 */
#include <stddef.h>
#include <stdint.h>

/* Reads one asset by name into a malloc'd buffer; 0 on success */
typedef int (*L10nRead)(const char *name, uint8_t **data, size_t *len);

/* The stamp of an index text; 0 when there is none */
int l10nIndexStamp(const char *text, char *out, int cap);
/* The next file line of an index text (advances *cursor); 0 at the end. Paths that are absolute or climb with ".." are skipped. */
int l10nIndexNext(const char **cursor, char *rel, int cap, long *size);

/*
 * Copies the overlay from `srcPrefix` (the assets' localization/ folder, trailing separator) into `<base>localization/`.
 * Nothing is copied when the installed stamp matches and every listed file exists; otherwise every listed file is written
 * (only files of the index: any other file of the folder is left alone) and the stamp last. Returns the number of files
 * written, 0 when there was nothing to do or no assets, -1 on a failure (the stamp is then not updated).
 */
int l10nInstall(const char *srcPrefix, const char *base, L10nRead read);

#ifdef __ANDROID__
/* l10nInstall over the APK's assets into the user folder (defined in port_paths_sdl.c) */
int l10nInstallAssets(void);
#endif

#endif
