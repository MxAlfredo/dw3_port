/*
 * In-game import of a PS1 memory card (M2 / R4). The automatic first-start import stays in host_memcard.c; this is the player's own
 * action ("Import memory card" in the System tab), for Linux and later Android (Windows imports in the installer).
 */
#ifndef HOST_MEMCARD_H
#define HOST_MEMCARD_H

#include <stddef.h>

/* Imports the newest card found in import/ or an emulator's folder (card_image.h) that holds a usable Digimon World 3 save, replacing the
   saves of this profile. Nothing is touched unless a card validates; the current saves are copied to saves/<profile>/backup/<stamp>/ first.
   Returns 1 when saves were imported; *msg is a one-line English result either way (also logged on [saves]). */
int hostMemcardImportNow(char *msg, size_t cap);

/* The same for one named file (the tests, and a path the player was given) */
int hostMemcardImportFile(const char *path, char *msg, size_t cap);

/* Where the player drops a card: the import folder (never empty when the user folder resolves, else "") */
const char *hostMemcardDropDir(void);

#endif
