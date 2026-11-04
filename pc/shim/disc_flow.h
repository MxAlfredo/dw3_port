#ifndef PC_DISC_FLOW_H
#define PC_DISC_FLOW_H

/*
 * T7b: the pure decisions of the Android disc start, apart from every dialog and JNI call so a host test drives them
 * (pc/discflowtest.c): which source the disc comes from, what a picked document is, and the retry loop after a
 * cancelled or rejected pick.
 */

/* Where the disc comes from at an Android start, in this order */
typedef enum {
    DF_FROM_REMEMBERED, /* the document the player picked before (its URI in settings), still readable */
    DF_FROM_APP_FOLDER, /* an image found in the app's own folder */
    DF_ASK              /* nothing usable: explain, then open the system picker */
} DfSource;

/* rememberedOk: the remembered URI opened and passed the disc checks; appFolderFound: auto-detect found a valid image */
DfSource dfStartSource(int rememberedOk, int appFolderFound);

/* 1 when a settings value is a document URI ("content://...") and not a file path */
int dfIsContentUri(const char *s);

/* 1 when the picked document is a .cue sheet: by its display name, or the URI when the name is unknown (a .cue grants
   no access to the .bin beside it, so it cannot be used) */
int dfPickIsCue(const char *uri, const char *displayName);

typedef enum {
    DF_EXPLAIN,  /* the message that says which file to choose */
    DF_PICKING,  /* the system picker is open */
    DF_CANCELED, /* the player backed out of the picker: message with retry / quit */
    DF_REJECTED, /* the pick is not usable (.cue, wrong disc, unreadable): message with retry / quit */
    DF_DONE,     /* a valid image */
    DF_QUIT      /* the player gave up */
} DfPhase;

typedef enum {
    DFE_CHOOSE,   /* "choose" pressed in a message */
    DFE_QUIT,     /* "quit" pressed (or the message could not be shown) */
    DFE_PICKED,   /* the picker returned a valid image */
    DFE_CANCEL,   /* the picker returned nothing */
    DFE_BAD       /* the picker returned a file the game cannot use */
} DfEvent;

/* The next phase. Events that do not apply to a phase leave it as it is, so a stray event never starts a busy loop. */
DfPhase dfNext(DfPhase phase, DfEvent ev);

typedef enum { DFM_EXPLAIN, DFM_CANCELED, DFM_CUE, DFM_UNREADABLE } DfMsg;

/* The text of a phase's message (Spanish or English; UTF-8) */
const char *dfMessage(DfMsg m, int spanish, char *buf, int cap);

#endif
