/*
 * The options menu's Audio rows (feature controls-config, O4); see
 * options_audio.h. Each row is a slider whose apply callback hands the percent
 * to snd_vol.c; the row's key puts it in settings.ini ([audio]).
 */
#include "controls_screen.h"
#include "options_audio.h"
#include "snd_internal.h"

static void applyMaster(int v) {
    sndSetUserVolume(SND_VOL_MASTER, v);
}

static void applyMusic(int v) {
    sndSetUserVolume(SND_VOL_MUSIC, v);
}

static void applyEffects(int v) {
    sndSetUserVolume(SND_VOL_EFFECTS, v);
}

static void applyMovies(int v) {
    sndSetUserVolume(SND_VOL_MOVIE, v);
}

void optionsAudioInit(void) {
    CsWidget w;

    w = (CsWidget){.kind = CS_ROW_SLIDER, .label = "Master volume", .key = "master_volume", .min = 0, .max = 100,
                   .step = OPT_AUDIO_STEP, .value = OPT_AUDIO_DEFAULT, .apply = applyMaster,
                   .desc = "All sound. 100 is the game's own level.",
                   .comment = "Master volume in percent (0-100), on top of everything else. 100 is the game's own level."};
    csWidgetAdd(CS_TAB_AUDIO, &w);
    w = (CsWidget){.kind = CS_ROW_SLIDER, .label = "Music volume", .key = "music_volume", .min = 0, .max = 100,
                   .step = OPT_AUDIO_STEP, .value = OPT_AUDIO_DEFAULT, .apply = applyMusic,
                   .desc = "Background music only.",
                   .comment = "Music volume in percent (0-100): the game's background music sequences."};
    csWidgetAdd(CS_TAB_AUDIO, &w);
    w = (CsWidget){.kind = CS_ROW_SLIDER, .label = "Effects volume", .key = "effects_volume", .min = 0, .max = 100,
                   .step = OPT_AUDIO_STEP, .value = OPT_AUDIO_DEFAULT, .apply = applyEffects,
                   .desc = "Menu, battle and field sound effects.",
                   .comment = "Sound effects volume in percent (0-100): every sound that is not background music."};
    csWidgetAdd(CS_TAB_AUDIO, &w);
    w = (CsWidget){.kind = CS_ROW_SLIDER, .label = "Movie volume", .key = "movie_volume", .min = 0, .max = 100,
                   .step = OPT_AUDIO_STEP, .value = OPT_AUDIO_DEFAULT, .apply = applyMovies,
                   .desc = "The sound of the videos.",
                   .comment = "Movie volume in percent (0-100): the audio track of the videos."};
    csWidgetAdd(CS_TAB_AUDIO, &w);
}
