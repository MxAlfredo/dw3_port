#ifndef PC_SAVE_FILE_H
#define PC_SAVE_FILE_H

/*
 * dw3save-v1: the port's own save file (ADR-008, option B). One file holds one save slot.
 * It is a tagged-section container with a CRC32 trailer; the game's original save block travels in
 * it untouched, and the port's extra state rides in sections of its own.
 *
 *   offset  size
 *   0       8     magic "DW3SAVE\0"
 *   8       4     format version (1; a newer one is refused, never guessed at)
 *   12      4     port save version (the port's own number, informational)
 *   16      4     section count
 *   20      4     total file size, trailer included
 *   24      ...   sections: tag[4], u32 size, bytes   (little endian, no padding)
 *   end-4   4     CRC32 (IEEE) of every byte before it
 *
 * Writing is atomic: the bytes go to <path>.tmp, are flushed to disk, the previous good file (if it
 * still decodes) moves to <path>.bak and the temporary file takes the name. A reader that finds the
 * main file damaged (a copy cut short, a flipped byte) falls back to <path>.bak and logs it on the
 * [saves] channel. A file this code cannot decode is never overwritten by guesswork.
 *
 * No game headers: the module is plain C and testable on its own.
 */

#include <stddef.h>
#include <stdint.h>

#define SAVEFILE_FORMAT_VERSION 1
#define SAVEFILE_PORT_VERSION 1
/* the format stores any count; this is what a SaveFile holds (the first builds wrote up to 8, a file with more than this
   reads as SAVEFILE_ERR_FORMAT, an add past it returns 0 and logs on [saves]) */
#define SAVEFILE_MAX_SECTIONS 32

enum {
    SAVEFILE_OK = 0,
    SAVEFILE_ERR_MISSING = -1, /* no such file */
    SAVEFILE_ERR_SHORT = -2,   /* cut short or smaller than its own header says */
    SAVEFILE_ERR_MAGIC = -3,   /* not a dw3save */
    SAVEFILE_ERR_VERSION = -4, /* a newer format */
    SAVEFILE_ERR_CRC = -5,     /* bytes changed */
    SAVEFILE_ERR_FORMAT = -6,  /* section table does not fit the file */
    SAVEFILE_ERR_IO = -7       /* cannot write */
};

typedef struct SaveSection {
    char tag[4];
    uint32_t size;
    const uint8_t *data;
} SaveSection;

typedef struct SaveFile {
    uint32_t formatVersion;
    uint32_t portVersion;
    int count;
    SaveSection sec[SAVEFILE_MAX_SECTIONS];
    uint8_t *owned; /* the buffer the sections point into after saveFileRead (freed by saveFileFree) */
    int usedBackup; /* saveFileRead: the main file was damaged and the .bak was used */
} SaveFile;

uint32_t saveCrc32(const void *data, size_t len);

/* Adds a section (the data is referenced, not copied); 0 (and a [saves] log line) when the table is full */
int saveFileAdd(SaveFile *f, const char tag[4], const void *data, size_t size);
/* The first section with this tag, or NULL */
const SaveSection *saveFileFind(const SaveFile *f, const char tag[4]);

/* The encoded size; the bytes are written to `out` only when `cap` is enough */
size_t saveFileEncode(const SaveFile *f, uint8_t *out, size_t cap);
/* Validates `buf` and points `out` into it; SAVEFILE_OK or a SAVEFILE_ERR_* */
int saveFileDecode(const uint8_t *buf, size_t len, SaveFile *out);

/* Atomic write (see above); SAVEFILE_OK or SAVEFILE_ERR_IO */
int saveFileWrite(const char *path, const SaveFile *f);
/* Reads and validates path, else path.bak; fills `out` (call saveFileFree). SAVEFILE_ERR_MISSING when
   neither exists, else the main file's error when both are unusable */
int saveFileRead(const char *path, SaveFile *out);
void saveFileFree(SaveFile *f);

const char *saveFileErrorName(int err);

#endif
