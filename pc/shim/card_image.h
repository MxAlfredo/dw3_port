/*
 * PS1 memory card images of other programs (M2 / R4: import from inside the game). Pure standard C.
 *
 * The card the game uses is 128 KiB (16 blocks of 8 KiB, starting with "MC"). Emulators and tools store it as:
 *     raw        131072 bytes: DuckStation .mcd, ePSXe .mcr, PCSX .mcd/.mc, RetroArch/Mednafen .srm, Mednafen .mcr
 *     DexDrive   .gme: a 0xF40-byte header ("123-456-STD") and then the raw card
 *     VMP        .vmp (PS3 / PSP): a 0x80-byte header ("\0PMV") and then the raw card
 * cardImageNormalize turns any of them into the raw card, or says why it cannot.
 */
#ifndef CARD_IMAGE_H
#define CARD_IMAGE_H

#include <stddef.h>

#include "port_paths.h"

#define CARD_IMAGE_SIZE 0x20000
#define CARD_IMAGE_MAX_FILE (CARD_IMAGE_SIZE + 0x1000) /* the largest wrapped card (the .gme header is 0xF40) */

/* `in` (len bytes) to the raw card in `out` (CARD_IMAGE_SIZE bytes). 1 when it is a card; else 0 and *why is a short English reason */
int cardImageNormalize(const unsigned char *in, size_t len, unsigned char *out, const char **why);

/* Reads the file at `path` and normalizes it into `out`. 1 on success, else 0 and *why (also when the file cannot be opened) */
int cardImageRead(const char *path, unsigned char *out, const char **why);

/* Candidate card files, newest first (by modification time): the files of import/ (the drop folder) and of the emulators' own folders
   (portPathsEmulatorDirs) that look like a card (extension .mcd .mcr .mc .gme .vmp .srm, and a size a card can have). Full paths;
   returns how many (at most `max`). */
int cardImageFind(char out[][PP_CAP], int max);

/* True when the file name has one of the card extensions (case-insensitive) */
int cardImageHasExt(const char *name);

#endif
