#ifndef PC_PORT_CONFIG_H
#define PC_PORT_CONFIG_H

/*
 * The port's data-driven settings (ADR-005, feature stage-banner T2): `config/<name>.json` files
 * with a "format": "<name>-v1", merged by key. Sources, lowest to highest priority:
 *
 *   1. the defaults in code (the macros of stage_banner.h and fast_transitions.h);
 *   2. <data>/config/<name>.json, but only when the user edited it. The port writes it as a reference
 *      copy (defaults plus a "notes" object); a copy that still matches its hash in
 *      <data>/config/manifest.json is the port's own and is not a source, so a new port version's
 *      defaults are never held back by an old reference. An edited copy is never overwritten or
 *      touched; delete it to get a fresh reference.
 *   3. every active mod's mods/<mod>/config/<name>.json, in mod order (mods.h), only the keys it lists.
 *
 * A file that is unreadable, not valid JSON, of another format version, or a key of the wrong type or
 * outside its range is logged (`config: <path> rejected: <reason>`) and the next source is used; the
 * game never stops for it. Loaded once at startup, after modsInit. One log line per config:
 * `config: stage_banner from default+data+mods/<mod>`.
 */

/* config/stage_banner.json */
typedef struct PcStageBanner {
    int typeDelayFrames; /* type_delay_frames: frames per typed character */
    int holdFrames;      /* hold_frames: how long the finished text stays */
    int raisePx;         /* raise_px: how far up the banner and its names move */
    int hiddenBoxes;     /* hidden_boxes: bit i set = bar box i is not drawn (the file lists the indices) */
} PcStageBanner;

/* config/transitions.json */
typedef struct PcTransitions {
    int doorFactor;           /* door_factor */
    int battleTilesPerFrame;  /* battle_tiles_per_frame */
    int battleWaitFrames;     /* battle_wait_frames */
    int resultsFactor;        /* results_factor */
    int mapTilesPerFrame;     /* map_tiles_per_frame */
} PcTransitions;

const PcStageBanner *pcStageBanner(void);
const PcTransitions *pcTransitions(void);

/* Load every config. dataDir may be NULL (no data folder: no reference copies, no data source). */
void pcConfigLoad(const char *dataDir);

#endif
