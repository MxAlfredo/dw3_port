#ifndef PC_OPTIONS_AUDIO_H
#define PC_OPTIONS_AUDIO_H

/*
 * The options menu's Audio rows (feature controls-config, O4): master, music,
 * sound effects and movie volume sliders, 0..100 % in steps of 5, saved in
 * settings.ini's [audio] section and applied live. The sliders feed
 * shim/snd_vol.c, which the mixer side (snd_spu.c, host_xa.c) reads at the end
 * of its chain: they multiply on top of the game's own volumes and never
 * replace them. Music is every SEP sequence the game starts as its exclusive
 * (background) sound; effects are SsUtKeyOn sounds and the other sequences;
 * movies are the XA audio of the videos. See docs/design/options-menu.md.
 */

/* The rows, in menu order (the Audio tab's row indices) */
enum { OPT_AUDIO_MASTER = 0, OPT_AUDIO_MUSIC, OPT_AUDIO_EFFECTS, OPT_AUDIO_MOVIES, OPT_AUDIO_ROWS };

#define OPT_AUDIO_STEP 5
#define OPT_AUDIO_DEFAULT 100

/* Adds the Audio rows (after inputCfgInit, so the file's values apply) */
void optionsAudioInit(void);

#endif
