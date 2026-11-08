/*
 * Test of PsyCross's SPU voices (external/psycross/src/audio/PsyX_SPUAL.cpp) as the port drives them: a sample is written to
 * the emulated SPU memory, a voice is keyed on, and the sample that went into the voice's OpenAL buffer is read back through
 * PsyX_SPUAL_GetVoiceSample. It runs on OpenAL's null device (no sound, no disc, no window).
 *
 * Why it exists: on Android the mixer thread of OpenAL Soft 1.24.3 crashed (SIGSEGV, ASan: heap-buffer-overflow in
 * LoadBufferStatic) in the falling-tiles scene of WSTAG680. Its loading of a looping static buffer is not cut at the loop end
 * when the play position has run past it, so a short loop read past the buffer (see SPU_MIN_LOOP_SAMPLES). The scene's
 * sample was 2772 samples long and looped over its last 1596, played at pitch 1.77.
 *
 *   1. a short loop is repeated, a whole number of times, until it is long enough, and still ends the sample
 *   2. a long loop and a sample without loop are left alone
 *   3. a sample without an end marker is cut at the decode buffer instead of running over it
 *
 * Exit 0: every assertion held.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <AL/alc.h>

#include "PsyX_SPUAL.h"

#define BLOCK_SAMPLES 28
#define SAMPLE_ADDR 0x1010u
#define LOOP_MIN 8192
#define DECODE_CAPACITY (512 * 1024) /* the samples one voice's decode buffer holds */

static int failures;

#define CHECK(cond)                                                                                                   \
    do {                                                                                                              \
        if (!(cond)) {                                                                                                \
            fprintf(stderr, "spualtest: FAIL line %d: %s\n", __LINE__, #cond);                                        \
            failures++;                                                                                               \
        }                                                                                                             \
    } while (0)

/* `blocks` ADPCM blocks (shift 4, filter 0, a changing nibble pattern) from `addr`. loopFrom < 0: no loop flags; else the block
   loopFrom starts the loop and the last block ends it (flags 0x04, 0x03). endFlag 0 leaves the sample without end marker. */
static void writeSample(unsigned addr, int blocks, int loopFrom, int endFlag) {
    unsigned char *vag = calloc((size_t)blocks, 16);
    int b, i;

    for (b = 0; b < blocks; b++) {
        unsigned char *blk = vag + b * 16;

        blk[0] = 0x04;
        for (i = 2; i < 16; i++) {
            blk[i] = (unsigned char)(b * 7 + i * 13);
        }
        if (b == loopFrom) {
            blk[1] |= 0x04;
        }
        if (b == blocks - 1 && endFlag) {
            blk[1] |= (loopFrom >= 0) ? 0x03 : 0x01;
        }
    }
    PsyX_SPUAL_SetTransferStartAddr(addr);
    PsyX_SPUAL_Write(vag, (unsigned)blocks * 16);
    free(vag);
}

/* keys voice 0 on at `addr` and reads its sample back */
static void keyOn(unsigned addr, int *len, int *loopStart, int *loopLen) {
    SpuVoiceAttr attr;

    memset(&attr, 0, sizeof attr);
    attr.voice = SPU_VOICECH(0);
    attr.mask = SPU_VOICE_WDSA | SPU_VOICE_LSAX;
    attr.addr = addr;
    attr.loop_addr = addr;
    PsyX_SPUAL_SetVoiceAttr(&attr);
    PsyX_SPUAL_SetKey(1, SPU_VOICECH(0));
    PsyX_SPUAL_GetVoiceSample(0, len, loopStart, loopLen);
}

/* Registered before InitSound, so it runs after the library's own exit handler: by then the audio device must be closed. On
   Android the process keeps running for a while after exit() and the OpenSL thread kept mixing while libopenal's static
   destructors ran (the Quit game crash: "pthread_mutex_lock called on a destroyed mutex" in the AudioTrack thread). */
static void checkClosedAtExit(void) {
    if (alcGetCurrentContext() != NULL) {
        fprintf(stderr, "spualtest: FAIL: the audio context is still open after the exit handlers\n");
        _exit(3);
    }
}

int main(int argc, char **argv) {
    int len, loopStart, loopLen;

    if (argc > 1 && strcmp(argv[1], "--exit") == 0) {
        atexit(checkClosedAtExit);
        if (!PsyX_SPUAL_InitSound()) {
            return 2;
        }
        writeSample(SAMPLE_ADDR, 99, 42, 1);
        keyOn(SAMPLE_ADDR, &len, &loopStart, &loopLen);
        exit(0); /* a sound is playing and nobody called a shutdown: the library closes the audio on its own */
    }

    if (!PsyX_SPUAL_InitSound()) {
        fprintf(stderr, "spualtest: no OpenAL device (run with ALSOFT_DRIVERS=null)\n");
        return 2;
    }

    /* 1. the falling-tiles sample's shape: 99 blocks = 2772 samples, loop over the last 1596 (57 blocks) */
    writeSample(SAMPLE_ADDR, 99, 42, 1);
    keyOn(SAMPLE_ADDR, &len, &loopStart, &loopLen);
    CHECK(loopStart == 42 * BLOCK_SAMPLES);
    CHECK(loopLen >= LOOP_MIN);
    CHECK(loopLen % (57 * BLOCK_SAMPLES) == 0);
    CHECK(loopStart + loopLen == len);
    CHECK(len <= DECODE_CAPACITY);

    /* the whole sample is the loop: still a loop of enough samples */
    writeSample(SAMPLE_ADDR, 10, 0, 1);
    keyOn(SAMPLE_ADDR, &len, &loopStart, &loopLen);
    CHECK(loopStart == 0);
    CHECK(loopLen >= LOOP_MIN && loopLen % (10 * BLOCK_SAMPLES) == 0);
    CHECK(len == loopLen);

    /* 2. a loop that is long already, and a sample that does not loop */
    writeSample(SAMPLE_ADDR, 400, 20, 1); /* loop of 380 blocks = 10640 samples */
    keyOn(SAMPLE_ADDR, &len, &loopStart, &loopLen);
    CHECK(len == 400 * BLOCK_SAMPLES);
    CHECK(loopStart == 20 * BLOCK_SAMPLES);
    CHECK(loopLen == 380 * BLOCK_SAMPLES);

    writeSample(SAMPLE_ADDR, 99, -1, 1);
    keyOn(SAMPLE_ADDR, &len, &loopStart, &loopLen);
    CHECK(len == 99 * BLOCK_SAMPLES);
    CHECK(loopLen == 0);

    /* 3. no end marker: the SPU memory after the sample is decoded as far as the decode buffer goes, not further */
    writeSample(SAMPLE_ADDR, 30000, -1, 0);
    keyOn(SAMPLE_ADDR, &len, &loopStart, &loopLen);
    CHECK(len == DECODE_CAPACITY);
    CHECK(loopLen == 0);

    PsyX_SPUAL_ShutdownSound();
    if (failures) {
        fprintf(stderr, "spualtest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("spualtest: ok\n");
    return 0;
}
