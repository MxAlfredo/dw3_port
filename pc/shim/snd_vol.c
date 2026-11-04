/*
 * The player's volume sliders (feature controls-config, O4): the master, music,
 * sound effects and movie levels of the options menu's Audio tab. No game, SPU
 * or OpenAL dependency, so pc/sndtest.c and pc/inputtest.c drive it as is; the
 * mixer side (snd_spu.c for voices, host_xa.c for movies) reads the gains from
 * here whenever it sets a level. See snd_internal.h for the contract.
 */
#include "snd_internal.h"

/* percent 0..100, 100 = the game's own level; written by the menu thread, read
   by the sound tick, one aligned int each */
static volatile int sndUserPercent[SND_VOL_COUNT] = {100, 100, 100, 100};
static volatile unsigned sndVolGen;

int sndVolTaperPermille(int percent) {
    if (percent <= 0) {
        return 0;
    }
    if (percent >= 100) {
        return 1000;
    }
    return percent * percent / 10; /* (p/100)^2 * 1000 */
}

void sndSetUserVolume(int which, int percent) {
    if (which < 0 || which >= SND_VOL_COUNT) {
        return;
    }
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }
    sndUserPercent[which] = percent;
    sndVolGen++;
}

int sndUserVolume(int which) {
    return which >= 0 && which < SND_VOL_COUNT ? sndUserPercent[which] : 100;
}

int sndUserGainPermille(int which) {
    int master = sndVolTaperPermille(sndUserPercent[SND_VOL_MASTER]);

    if (which == SND_VOL_MASTER) {
        return master;
    }
    if (which < 0 || which >= SND_VOL_COUNT) {
        return 1000;
    }
    return master * sndVolTaperPermille(sndUserPercent[which]) / 1000;
}

short sndScaleVol(short vol, int permille) {
    return (short)((long)vol * permille / 1000);
}

unsigned sndVolGeneration(void) {
    return sndVolGen;
}
