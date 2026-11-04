/*
 * Host shim for libsnd (ADR-002: PsyCross does not provide it, so it is
 * ours).
 *
 * The SPU side is real: the VAB headers the game's own loader hands over
 * are parsed and the VAB bodies land in PsyCross's SPU RAM (PsyX_SPUAL
 * through LIBSPU), so SsUtKeyOn starts real voices, and as of T35 the
 * SEP/sequence side plays too: the 'pQES' packages the game's loader opens
 * are decoded (shim/snd_sep.c) and their key-ons go through the same
 * voices, once the game's own SsSeqCalledTbyT ticks each frame.
 *
 * What stays stubbed here on purpose (T35 scope):
 *   - the serial ports and the reverb unit (PsyX exposes SpuSetReverb*
 *     hooks a later milestone could route them to);
 *   - SsSetTickMode itself: the EU build ticks through SsSeqCalledTbyT
 *     (SsSetTickMode(SS_NOTICK | 50)), so the shim ticks the SEP player
 *     there regardless of the mode the game asks for.
 *
 * The game's sound unit (src/engine/sound/sound.c) runs unmodified on top
 * of this: SOUND.init, the bank loader (SOUND.loadBank / updateLoading /
 * isLoading) and SOUND.playSound, which is now audible on both paths.
 *
 * T36 adds a third path that does NOT go through libsnd at all: the movie
 * streams' CD-XA audio (shim/host_xa.c), driven by the CD control commands
 * the movie player already sends (shim/host_cd.c) and played through its own
 * OpenAL source so it never competes for the 24 SPU voices. This file only
 * installs that streamer's real sink (sndXaUseOpenAl) and pumps it from the
 * same vsync hook the SEP tick uses. SsSetSerialAttr/SsSetSerialVol stay
 * no-ops: XA does not route through libsnd's serial port.
 *
 * T40: the XA pump is demand-driven (shim/host_xa.h): sndXaTick decodes only
 * while the OpenAL sink reports room, so the stream stays fed even when the
 * game's main loop is blocked by the movie's video pacer. The pacer also calls
 * sndXaTick from its own thread; the driver serialises the two, so this hook
 * needs no extra locking.
 */
#include "engine/game.h"

#include <libetc.h>
#include <libsnd.h>

#include "audio_mod.h"
#include "host_xa.h"
#include "snd_internal.h"

/* The game's own EU build ticks the sequencer inside display.c's
   vsyncCallback; this port's drawing loop (shim/gfx_engine.c) drives its
   frames without one, so the SEP player's tick rides PsyX's own vsync
   thread instead (VSyncCallback; 60 Hz, 50 with DW3_REFRESH=50). SsSeqCalledTbyT
   stays a plain forward for anyone who ticks the old way (pc/sndtest.c).
   T36's XA stream is pumped from the same vsync hook; since T40 that tick is
   demand-driven (it decodes only while the OpenAL sink reports room), so the
   queue stays fed even when the main loop is blocked by the movie pacer. */
static void sndSepVsyncHook(void) {
    sndSepTbyT();
    sndEnvTick(); /* A4: the SPU ADSR envelopes, one tick (1/rate s) */
    sndXaTick();
    audioModPump(); /* E3d: the songs a mod put in place of sequences */
}

/* The SPU backing starts with the first libsnd call the boot sequence
   reaches (sndInit is idempotent; PsyX_SPUAL's own init is safe to race
   with none of these) */
void SsInit(void) {
    sndInit();
    sndSepInit();
    sndXaUseOpenAl();
    VSyncCallback(sndSepVsyncHook);
}

void SsSetTableSize(char *table, short seqCount, short sepCount) {
    (void)table;
    (void)seqCount;
    (void)sepCount;
    sndInit();
    sndSepInit();
    sndXaUseOpenAl();
    VSyncCallback(sndSepVsyncHook);
}

void SsSetTickMode(long tickMode) {
    /* The shim ticks the SEP player from the vsync hook (once a vblank)
       whatever the mode says; O7: the mode only sets how many ticks a second
       that hook stands for, as libsnd_sstick.s does: SS_NOTICK is 60 (the
       NTSC path the port always runs), SS_NOTICK | 50 is the PAL build's 50. */
    sndSetTickHz(sndTickHzFromMode(tickMode));
}

void SsStart2(void) {
    sndInit();
    sndSepInit();
    sndXaUseOpenAl();
    VSyncCallback(sndSepVsyncHook);
}

void SsSeqCalledTbyT(void) {
    sndSepTbyT();
}

/* The master volume of the SEP side (the key-on levels carry the one-shot
   side as T34 did) */
void SsSetMVol(short left, short right) {
    sndSetMasterVol(left, right);
}

void SsSetSerialAttr(char port, char attr, char value) {
    (void)port;
    (void)attr;
    (void)value;
}

void SsSetSerialVol(char port, short left, short right) {
    (void)port;
    (void)left;
    (void)right;
}

/* The reverb unit (T35+: PsyX's SpuSetReverb* hooks) */
short SsUtSetReverbType(short type) {
    (void)type;
    return 0;
}

void SsUtSetReverbDepth(short left, short right) {
    (void)left;
    (void)right;
}

void SsUtReverbOn(void) {
}

/* Voices: the one-shot path, real through snd_spu.c's key-on */
short SsUtKeyOn(short vabId, short prog, short tone, short note, short fine, short volL, short volR) {
    return sndKeyOn(vabId, prog, tone, note, fine, volL, volR);
}

short SsUtKeyOff(short voice, short vabId, short prog, short tone, short note) {
    return sndKeyOff(voice, vabId, prog, tone, note);
}

void SsUtAllKeyOff(short mode) {
    sndAllKeyOff(mode);
}

/*
 * The VAB side: the game's own loader opens a bank per SOUND slot with the
 * address SOUND_SPU_ADDRS reserves, streams the body in and only walks on
 * when the transfer is done. snd_spu.c parses the header, writes the body
 * into PsyX's SPU RAM at that address and reports the id the game stores
 * (the slot + 1). The SEQ keys that follow keep using those ids.
 */
short SsVabOpenHeadSticky(unsigned char *head, short slot, unsigned long spuAddr) {
    return sndVabOpen(head, slot, spuAddr);
}

short SsVabTransBody(unsigned char *body, short vabId) {
    return sndVabTransBody(body, vabId);
}

short SsVabTransCompleted(short immediate) {
    return sndVabTransCompleted(immediate);
}

void SsVabClose(short vabId) {
    sndVabClose(vabId);
}

/* The SEP (sequence) side: the game's loader opens a package per bank SEP
   from the head buffer it already copied, plays records of it by the MUSIC
   id's own low byte and ticks them once a frame (this file's
   SsSeqCalledTbyT). The packages' key-ons land in the same voices the
   one-shots use (snd_sep.c's real sink through sndPickTone). */
short SsSepOpen(unsigned long *sep, short vabId, short sepCount) {
    /* T35 simplification: the package's length is not part of the retail
       call; sndSepOpen scans under the head buffers' bound */
    (void)sepCount;
    return sndSepOpen((unsigned char *)sep, vabId);
}

void SsSepClose(short seqId) {
    sndSepClose(seqId);
}

void SsSepPlay(short seqId, short sepId, char playMode, short loopCount) {
    /* the game plays with the volume it last set (SsSepSetVol right
       before, SOUND_VOLUME_MAX in every playSound) */
    sndSepPlayStart(seqId, sepId, playMode, loopCount, 127, 127);
}

void SsSepStop(short seqId, short sepId) {
    sndSepPlayStop(seqId, sepId);
}

void SsSepSetVol(short seqId, short sepId, short volL, short volR) {
    sndSepPlaySetVol(seqId, sepId, volL, volR);
}

void SsSepSetDecrescendo(short seqId, short sepId, short vol, long frames) {
    sndSepPlayDecrescendo(seqId, sepId, vol, frames);
}
