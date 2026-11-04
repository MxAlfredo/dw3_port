#ifndef PC_FAST_TRANSITIONS_H
#define PC_FAST_TRANSITIONS_H

/*
 * Fast transitions (feature fast-transitions): the dead time the game spends on fixed-clock
 * animations, shortened in the port without changing how they look. Each option has two modes:
 *
 *   Fast     (default) the same animation, run faster; only the frames that wait for no button
 *   Classic  the original timing, untouched
 *
 * Options (Enhancements tab, [enhancements] in settings.ini):
 *   Door transition     the field closing when you leave a stage through a door (door_transition)
 *   Battle transition   the tile spiral from the field to a battle (battle_transition)
 *   Battle results      the padding of the victory report that waits for no button (battle_results)
 *   Map loading         the field's 128-pixel map tiles appear at once instead of one a frame (map_loading)
 *
 * This file is the pure part: the settings, the rows and every Fast timing (the one place to tune
 * them as defaults; the values the game uses come from config/transitions.json, see port_config.h). The game side is the per-frame
 * poll fastTransitionsFrame (stage_timing.c, called from hostDrawFrame), which only adjusts the
 * game's own task state and writes nothing in Classic.
 */

enum { FAST_MODE_FAST = 0, FAST_MODE_CLASSIC, FAST_MODES };

/* ---- Fast timings (the defaults), in game frames (60 per second) and the factors that shorten them ---- */

/* Door transition: the close of the field (FIELDSTG_closeField) runs this many times faster: the view
   shrinks by 10 x 3 and 7 x 3 pixels a frame, the cover fade steps 3 times as far, and the cover's
   opening clip moves 8 x 3 pixels a frame. The same shapes in a third of the frames (56 -> ~20). */
#define FAST_DOOR_FACTOR 3

/* Battle transition: the tile spiral completes this many tiles on every frame it moves (the original
   does one, 30 tiles in 42 frames) and the wait after the last tile is this many frames (the original
   waits TRANSITION_WAIT = 16). The same spiral, each tile sliding off as before. */
#define FAST_BATTLE_TILES_PER_FRAME 3
#define FAST_BATTLE_WAIT_FRAMES 4

/* Battle results: the opening and closing animations of the report (its panels and the fade-out),
   which wait for no button, run this many times faster; the message pages and every button wait are
   left alone. */
#define FAST_RESULTS_FACTOR 3

/* Map loading: the field streams its map in 128-pixel tiles and unpacks one a frame (FIELDSTG_drawMapTiles), so
   the blue cover grid shows for about a second on entry and while scrolling. Fast unpacks and uploads, after each
   frame, up to this many of the view's tiles that are read but not unpacked (the view holds 12 at most, so 12
   means all of them); the 12 VRAM slots and the drawing are the game's own. */
#define FAST_MAP_TILES_PER_FRAME 12

/* The settings the options rows write and the game side reads; a bad mode is Fast */
void fastDoorSetMode(int mode);
int fastDoorMode(void);
void fastBattleEntrySetMode(int mode);
int fastBattleEntryMode(void);
void fastResultsSetMode(int mode);
int fastResultsMode(void);
void fastMapSetMode(int mode);
int fastMapMode(void);

/* Adds the four rows to the Enhancements tab (called by optionsVideoInit, after the older rows) */
void fastTransitionsRows(void);

/* The per-frame poll of the game side (stage_timing.c): runs the Fast modes; nothing in Classic */
void fastTransitionsFrame(void);

#endif
