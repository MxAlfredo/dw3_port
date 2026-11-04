#ifndef PC_STAGE_BANNER_H
#define PC_STAGE_BANNER_H

/*
 * Stage banner option (feature stage-banner): what the port does with the area name banner
 * the field shows on every stage change.
 *
 *   Fast     (default) the player moves as soon as the stage is loaded; the place name is typed
 *            over the visible stage and goes away by itself.
 *   Classic  the original: the stage stays hidden and the player frozen until the banner ends.
 *
 * This file is the pure part (the setting and the Fast timings). The game side is the per-frame
 * poll stageBannerFrame (stage_timing.c) and the map hiding test in gfx_engine.c.
 */

enum { STAGE_BANNER_FAST = 0, STAGE_BANNER_CLASSIC, STAGE_BANNER_MODES };

/* Fast-mode timings, in game frames (60 per second): the defaults. The values the game uses come
   from config/stage_banner.json (port_config.h, ADR-005), which falls back to these. */
#define STAGE_BANNER_FAST_TYPE_DELAY 1  /* frames per typed character (the original uses 5) */
#define STAGE_BANNER_FAST_HOLD_FRAMES 45 /* how long the finished text stays (the original holds 60) */

/* Fast-mode layout: the banner's horizontal bars and its two names move up by this many pixels,
   and its vertical bars (boxes 2, 3, 4 and 7 of FIELDSTG_bannerBoxes) are not drawn */
#define STAGE_BANNER_FAST_RAISE 14

/* The setting the options row writes and the game side reads; a bad mode is Fast */
void stageBannerSetMode(int mode);
int stageBannerMode(void);

/* Fast mode: nonzero once the stage is shown (the map is drawn from then on). The poll writes it;
   gfx_engine.c reads it through stageBannerHidesMap. */
void stageBannerSetShown(int shown);

/* Fast mode shows the stage only once every tile in its view is unpacked, so the map's blue
   placeholder grid is never presented. A tile that never comes (a map that cannot load it) gives up
   after this many frames, so the stage is not hidden for good. */
#define STAGE_BANNER_SHOW_WAIT_MAX 600
#define STAGE_BANNER_NO_MAP_WAIT 30 /* frames to wait for a map task that is not there yet */
int stageBannerMayShow(int pendingTiles, int waitedFrames);

/* Leaving a stage through a door (FIELDSTG_closeField) fades in the game's blue circuit-grid cover
   over the old stage. It is the tile placeholder's texture, so it reads as a flash of unloaded
   tiles: with the Fast door transition the port does not draw the cover layer then (the shrinking
   view over black stays); Classic draws it as the game does.
   Nonzero while the field is closing for a stage change (not for a battle) in Fast. */
int stageExitCoverHidden(int fieldClosing, int battleStarting, int fastDoor);

/* Nonzero while the field map layer must not be drawn during the entry banner: always in Classic
   (the port's adaptation of the original's black entry), until the stage is loaded in Fast */
int stageBannerHidesMap(void);

/* The per-frame poll of the game side (stage_timing.c): runs the Fast mode; nothing in Classic */
void stageBannerFrame(void);

#endif
