#ifndef PC_TEXT_CODEC_H
#define PC_TEXT_CODEC_H

#include <stddef.h>
#include <stdint.h>

/*
 * Text codec (feature es419-live-option, L2a): the game's lang files and lang packs as plain
 * C, a port of tools/es419/lang.py (which stays the reference and the test oracle).
 *
 * Lang file: u32 count N, N u32 offsets (from file start), then NUL-terminated strings, each
 * starting 4-byte aligned. Lang pack (SSDMG*): u32 offset table whose first entry is its own
 * size; each entry points to an embedded lang file (a "part") or to raw data.
 *
 * The codec is pure (stdlib only, no game or PsyCross dependency). A parsed document keeps
 * every entry's bytes plus what an exact rebuild needs (the gap after the header, strings
 * that share storage, non-canonical tails), so an untouched file rebuilds byte for byte.
 * Text is UTF-8 with the lang.py token syntax: bytes outside the charmap are `{xx}` or
 * `{01xx}`; the overlay's readable aliases are `{hablante}` (0207), `{pagina}` (0202 0202 03),
 * `{fin}` (0202 02) and "\n" (02 01).
 *
 * Every function returning int gives 0 on success and -1 on failure; when it takes an
 * `err` buffer it describes the failure there (may be NULL). Anything returned as `char *`
 * or `uint8_t *` is malloc'ed and owned by the caller.
 */

typedef struct TcEntry {
    uint8_t *body;     /* encoded text, without the terminator */
    uint32_t bodyLen;
    int hasTail;       /* 0: the canonical tail (NUL plus padding); 1: `tail` as read */
    uint8_t *tail;     /* non-canonical bytes after the body (may be empty) */
    uint32_t tailLen;
    uint32_t slot;     /* entries with the same slot share one stored string */
} TcEntry;

typedef struct TcLang {
    int count;
    TcEntry *entries;
    uint8_t *gap;      /* bytes between the offset table and the first string */
    uint32_t gapLen;
} TcLang;

typedef struct TcPart {
    int isLang;        /* 1: `lang` is used; 0: opaque bytes in raw/rawLen */
    TcLang lang;
    uint8_t *raw;
    uint32_t rawLen;
} TcPart;

/* A parsed file: a plain lang file (isPack 0, text in `lang`) or a pack (`parts`) */
typedef struct TcDoc {
    int isPack;
    TcLang lang;
    int partCount;
    TcPart *parts;
} TcDoc;

/* ---- charmap ---- */

/* Bytes -> UTF-8 text (lang.py decode_text). Returns a malloc'ed string. */
char *tcDecode(const uint8_t *data, size_t len);

/* UTF-8 text -> bytes (lang.py encode_text); `{xx}`/`{xxxx}` tokens (lowercase hex) become raw
   bytes. Fails on a character the charmap lacks (the message names it). */
int tcEncode(const char *text, uint8_t **out, size_t *outLen, char *err, size_t errSize);

/* Raw control tokens -> readable aliases, and the inverse (lang.py prettify/uglify) */
char *tcPrettify(const char *text);
char *tcUglify(const char *text);

/* ---- lang files and packs ---- */

/* Parse a file (copies the data). Fails when it is neither a lang file nor a lang pack. */
int tcDocParse(const uint8_t *data, size_t len, TcDoc *doc, char *err, size_t errSize);
uint8_t *tcDocBuild(const TcDoc *doc, size_t *outLen);
void tcDocFree(TcDoc *doc);

/* The lang file an overlay key refers to: `part` is -1 for plain lang files. NULL when the
   key does not fit (part on a plain file, missing or raw part). */
TcLang *tcDocLang(TcDoc *doc, int part);

/* Entry text as raw tokens (tcDecode of the body); malloc'ed */
char *tcEntryText(const TcLang *lang, int index);

/* Replace one string (lang.py set_text): `text` is raw-token text, the entry stops sharing
   storage and gets a canonical tail. Fails on a bad index or an unencodable character. */
int tcSetText(TcLang *lang, int index, const char *text, char *err, size_t errSize);

/* ---- JSON ----
   Schema: {"file": "NAME.BIN", "format": "...", "strings": [{"index": N, ["part": P,] "text": "..."}]}
   Whole files are written with format TC_FORMAT_FULL and EVERY entry (text prettified);
   the overlay files of localization/es-419 use TC_FORMAT_OVERLAY and list only the changes.
   Other keys ("base", "note", ...) are skipped on read. */
#define TC_FORMAT_FULL "text-v1"
#define TC_FORMAT_OVERLAY "overlay-v1"

typedef struct TcJsonString {
    int index;
    int part;          /* -1 when absent */
    char *text;        /* as written in the JSON (prettified aliases allowed) */
} TcJsonString;

typedef struct TcJson {
    char *file;
    char *format;
    int count;
    TcJsonString *strings;
} TcJson;

/* A whole document as JSON (UTF-8, indented); NULL on memory failure */
char *tcDocToJson(const TcDoc *doc, const char *fileName);

/* Strict reader of that schema. Fails with a message on malformed input. */
int tcJsonParse(const char *json, size_t len, TcJson *out, char *err, size_t errSize);
void tcJsonFree(TcJson *j);

/* ---- merge ---- */

/* Apply a JSON (overlay or whole file) onto a parsed base: for every listed string,
   tcSetText(tcUglify(text)). Fails on a missing part, an out-of-range index or an
   unencodable character; the document may then be partly changed. Same as build_text.py. */
int tcApplyJson(TcDoc *doc, const TcJson *json, char *err, size_t errSize);

#endif
