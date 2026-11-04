#ifndef PC_PORT_LOG_H
#define PC_PORT_LOG_H

/*
 * The port's one log (feature port-log). Everything goes to stderr, which the launcher already
 * keeps in the session log (logs/), as `[channel] message`.
 *
 * Three levels, chosen in the System tab ("Log") or in settings.ini:
 *
 *     [debug]
 *     log = off | mods | trace
 *     channels = tables, files     (optional; empty = every channel)
 *
 *   off    only the startup summaries and the problems (plogLine / plogRaw: always printed)
 *   mods   also the short lines that say what an active mod changes while the game runs
 *   trace  also the technical detail of each channel (every file read, every image load...)
 *
 * `channels` limits the mods and trace lines to the listed channels. Always-on lines ignore it.
 *
 * The environment overrides the file (scripts, one-off runs):
 *   DW3_LOG=off|mods|trace         the level
 *   DW3_LOG_CHANNELS=a,b           the channel list
 *   DW3_MOD_LOG=1                  the level is at least mods
 *   DW3_TABLE_TRACE, DW3_FS_TRACE, DW3_IMG_TRACE, DW3_STAGE_TIMING (>= 1), DW3_MEMCARD_TRACE,
 *   DW3_TEXT_TRACE, DW3_BATTLE_EVENT_LOG
 *                                  shortcuts: trace on the channel tables / files / images / stage /
 *                                  memcard / text / battle only, whatever the level and the channel
 *                                  list say
 *
 * Channels: tables files images config mods stage (mods and startup lines), plus the per-frame and
 * per-event detail at trace: gfx (layers, packet buffers, one line per drawn layer per frame), pad
 * (the pad word and the menu state every frame), field (the player's position every frame), tasks
 * (actor register/unregister/find), overlay (stage overlay loads), text (text files served, reflow
 * totals, face-button icons), memcard, battle (the battle event trace).
 *
 * Cost: PLOG tests plogMax first, so a line that is off never formats its arguments.
 */

#include <stddef.h>

enum { PLOG_OFF = 0, PLOG_MODS = 1, PLOG_TRACE = 2 };

/* The highest level any channel could be at; -1 until the environment was read. Read by PLOG only. */
extern int plogMax;

/* 1 when a line of `channel` at `level` (PLOG_MODS or PLOG_TRACE) is wanted */
int plogOn(const char *channel, int level);

/* Prints `[channel] message` (the message carries its own newline). No level test: use PLOG. */
void plogLine(const char *channel, const char *fmt, ...);
/* Prints the rest of a line that plogLine started (no prefix) */
void plogRaw(const char *fmt, ...);

/* A line of `channel` at `level`, only when wanted */
#define PLOG(channel, level, ...)                                  \
    do {                                                           \
        if ((plogMax < 0 || plogMax >= (level)) && plogOn((channel), (level))) { \
            plogLine((channel), __VA_ARGS__);                      \
        }                                                          \
    } while (0)

/* "off", "mods" or "trace" (any case, also on/true/1 = mods, off/false/0): the level, or -1 */
int plogParseLevel(const char *text);
const char *plogLevelName(int level);

/* The settings.ini side (input_cfg.c): the level and the channel list ("a, b" or "" for all) */
void plogSetSettings(int level, const char *channels);

/* 1 when `channel` is in `list` ("a, b"; case-insensitive; an empty list has every channel) */
int plogChannelListed(const char *list, const char *channel);

/* Android: stdout and stderr are discarded there. plogTeeToLogcat points both at `logFile` (appended) AND at logcat (tag "dw3",
   one entry per line), through a pipe and a reader thread. 1 when set up; 0 on every other platform or on failure (the
   streams are then untouched). */
int plogTeeToLogcat(const char *logFile);
/* The reader's pure part: appends data[0..n) to the partial line in buf (used bytes, capacity cap), calls emit once per complete
   line (without the newline; carriage returns dropped). A line that fills the buffer is emitted as it is. Returns the lines emitted. */
int plogLineSplit(char *buf, size_t *used, size_t cap, const char *data, size_t n, void (*emit)(const char *line, void *ud), void *ud);

/* Test hook: forget the environment and the settings, read the environment again at the next use */
void plogReset(void);

#endif
