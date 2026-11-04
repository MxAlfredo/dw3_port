#ifndef PC_DISC_PICKER_H
#define PC_DISC_PICKER_H

/*
 * W2: the player's first run. Started with no arguments, the port asks for the player's own
 * Digimon World 3 (Europe) disc image (a native file dialog), checks it, remembers it in
 * settings.ini ([disc] path) and logs to <pref>/logs. The developer's command line (a disc path
 * given) never reaches the dialogs.
 *
 * The checks are pure functions over buffers (tested by pc/inputtest.c); the dialogs are thin.
 * DW3_PICK_DISC=<path> stands in for the dialog (tests, scripts): the path is validated like a
 * pick and a bad one is reported on stderr instead of in a message box.
 */
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* The boot file the European disc names in SYSTEM.CNF (SLES-03936) */
#define DP_EXPECTED_BOOT "SLES_039.36"

typedef enum {
    DP_OK = 0,
    DP_CANT_OPEN,   /* the file does not open or is too short */
    DP_NOT_PS1,     /* opens, but is not a PlayStation disc image */
    DP_COOKED_ISO,  /* a 2048-byte-sector image: the port needs the raw 2352-byte .bin */
    DP_OTHER_DISC   /* a PlayStation disc, but not this game / region (the id is reported) */
} DpResult;

/* ---- pure ---- */

/* The .bin a .cue names (its first FILE line), joined to the .cue's folder unless absolute. 1 on success */
int dpCueBin(const char *cueText, const char *cuePath, char *out, int cap);
/* The file named (without ";1"; upper case) in an ISO 9660 directory block: its extent and size. 1 if found */
int dpFindFile(const uint8_t *dir, size_t len, const char *name, uint32_t *lba, uint32_t *size);
/* The boot file id of a SYSTEM.CNF text ("BOOT = cdrom:\SLES_039.36;1" -> "SLES_039.36"). 1 on success */
int dpBootId(const char *cnf, size_t len, char *id, int cap);
/* 1 when a locale string ("es_MX.UTF-8", "es-419", "es") is Spanish */
int dpIsSpanish(const char *locale);
/* "dw3-YYYYMMDD-HHMMSS.log" */
void dpLogName(char *out, int cap, const struct tm *t);
/* 1 when name is one of ours ("dw3-YYYYMMDD-HHMMSS.log") */
int dpLogIsName(const char *name);
/* Which of the sorted-or-not names to delete so that only the newest `keep` log names remain:
   drop[i] = 1 for the older ones; names that are not ours are never marked. Returns how many. */
int dpLogPlan(const char *const *names, int count, int keep, int *drop);
/* W2b: portable mode. If path lies inside folder (either separator, case-insensitive), out gets it relative
   with forward slashes and 1 is returned. */
int dpRelativeTo(const char *folder, const char *path, char *out, int cap);
/* A stored path to a usable one: relative ones are joined to folder, absolute ones (/x, \x, C:x) kept. 1 on success */
int dpResolveStored(const char *folder, const char *stored, char *out, int cap);
/* Auto-detect order of an image file name: .cue 0, .bin 1, .iso 2, anything else -1 */
int dpCandidateRank(const char *name);
/* strcmp-like order of two candidates (rank, then the folder index, then name) */
int dpCandidateCmp(int rankA, int folderA, const char *nameA, int rankB, int folderB, const char *nameB);
/* The text of a failed check, in Spanish or English; detected may be NULL */
const char *dpMessage(DpResult r, int spanish, const char *detected, char *buf, int cap);

/* Android has no file dialog: the text that tells the player to copy the disc image into `folder` (the app's files
   folder, as the PC sees it over USB) and start again. folder may be NULL. */
const char *dpNoDiscMessage(int spanish, const char *folder, char *buf, int cap);

/* ---- I/O ---- */

/* Opens the .bin (or the .bin a .cue names; bin receives the path) and checks it is the EU disc.
   detected receives the boot id of another PlayStation disc when known (may be NULL). */
DpResult dpCheckImage(const char *path, char *bin, int cap, char *detected, int dcap);

/* W2b: the first valid European image in folder and its direct subfolders (.cue first, then .bin, .iso).
   bin receives its .bin; the picked file (the .cue or .bin) goes in picked. 1 when found. */
int dpAutoDetect(const char *folder, char *picked, int pcap, char *bin, int cap);

/* The whole player flow for a start with no arguments: logs, remembered disc, dialogs. Returns 1
   with the .bin path in bin, 0 when the player quit or no image could be chosen. */
int dpPlayerStart(char *bin, int cap);

/* The disc for a command-line tool (--validate-mod): arg (a .bin or .cue) when given, else the one settings.ini
   remembers; no dialogs. Returns 1 with the checked .bin path in bin. */
int dpToolDisc(const char *arg, char *bin, int cap);

#endif
