#ifndef PC_AUTO_TEXT_H
#define PC_AUTO_TEXT_H

/*
 * Auto text (feature controls-config, O6): the port presses the dialogue's confirm button
 * for the player once a text box has been waiting at a "next page" prompt long enough.
 *
 * This file is the pure part: the speed setting, how long to wait for a page of a given
 * length, and the per-frame decision. The game side (finding the prompt in the task heap,
 * writing the one-frame press into the pad word) is in host_pad.c.
 *
 * Wait = (AUTO_TEXT_BASE_FRAMES + AUTO_TEXT_PER_CHAR_HALVES * characters / 2) * speed factor,
 * never under AUTO_TEXT_MIN_FRAMES. Frames are 60 Hz ones (the port's default), so Normal
 * is 0.5 s plus 25 ms per character; Slow is x2 and Fast x0.5. The wait starts once the
 * game has printed the whole page, so the reading time it adds is on top of the printing.
 */

enum { AUTO_TEXT_OFF = 0, AUTO_TEXT_SLOW, AUTO_TEXT_NORMAL, AUTO_TEXT_FAST, AUTO_TEXT_SPEEDS };

#define AUTO_TEXT_BASE_FRAMES 30     /* 0.5 s */
#define AUTO_TEXT_PER_CHAR_HALVES 3  /* 1.5 frames, 25 ms */
#define AUTO_TEXT_MAX_CHARS 400      /* longer pages count as this long */
#define AUTO_TEXT_MIN_FRAMES 20      /* 1/3 s, whatever the speed */

/* Frames to wait at a prompt that follows `chars` characters, or -1 when the speed is Off
   (or not a speed) */
int autoTextDelayFrames(int speed, int chars);

/* The wait in progress */
typedef struct AutoText {
    int key;    /* which prompt is being waited on (the caller's identity for it) */
    int waited; /* frames waited so far */
} AutoText;

/* One game frame. `waiting` says a plain next-page prompt is up (0 otherwise: the wait
   resets), `key` identifies it (a different key restarts the wait), `chars` is the page's
   length. Returns 1 on the frame the press should be sent (the wait then restarts). */
int autoTextTick(AutoText *a, int speed, int waiting, int key, int chars);

/* The setting the Display row writes and the game side reads */
void autoTextSetSpeed(int speed);
int autoTextSpeed(void);

/* ---- Text speed (O6b) ----
 * How fast a dialogue window types its page, independent of Auto text. Off = the window's own
 * (retail) delay on every page; On = the engine's floor, typeDelay 1, on every page including the
 * first. A cross press speeds up only the page it is pressed on. host_pad.c applies this to the
 * game's text windows; the decisions are pure and live here. */
enum { TEXT_SPEED_OFF = 0, TEXT_SPEED_ON, TEXT_SPEED_MODES };

/* The typing delay a dialogue window gets; `retailDelay` is the window's own */
int textSpeedDelay(int mode, int retailDelay);

/* The same while a page is boosted by a cross press (this page only) */
int textSpeedPageDelay(int mode, int retailDelay, int boosted);

/* 1 when a new page began: the window was just found, its start moved (page break, name code)
   or it left a wait (state 2 -> 1). States are the TextWindow's (1 = typing, 2 = waiting) */
int textSpeedPageStart(int prevState, int state, int prevStart, int start, int fresh);

/* 1 when a cross press should speed up the page: the window was already typing the frame
   before (the press that releases a wait must not speed up the next page) */
int textSpeedBoost(int prevState, int state, int pressed);

void textSpeedSetMode(int mode);
int textSpeedMode(void);

#endif
