/*
 * scriptmirrortest: the battle script converter (shim/script_mirror.c, P17a). Hand-made scripts, no game data: each command kind has its
 * length and its mirrored fields checked, the refusals have one line each, and a script converted to the other side and back is the same.
 */
#include <stdio.h>
#include <string.h>

#include "shim/script_mirror.h"

static int failures;
#define CHECK(c)                                                                                  \
    do {                                                                                          \
        if (!(c)) {                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                   \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

#define MAXW 128

typedef struct Script {
    unsigned char b[MAXW * 2];
    int n; /* words */
} Script;

static void words(Script *s, int count, const int *w) {
    int i;

    for (i = 0; i < count; i++) {
        s->b[2 * s->n] = (unsigned char)(w[i] & 0xFF);
        s->b[2 * s->n + 1] = (unsigned char)((w[i] >> 8) & 0xFF);
        s->n++;
    }
}
#define W(s, ...)                                      \
    do {                                               \
        static const int w_[] = {__VA_ARGS__};         \
        words(s, (int)(sizeof w_ / sizeof w_[0]), w_); \
    } while (0)

static int at(const unsigned char *b, int i) {
    return (short)(b[2 * i] | (b[2 * i + 1] << 8));
}

static const ScriptPlace kEnemyPlace = {2304, 2304}; /* a floating enemy */
static const ScriptPlace kPartnerPlace = {0, 3328};

/* an enemy's script with one of every command */
static void enemyScript(Script *s) {
    memset(s, 0, sizeof *s);
    W(s, 3, 1, 1014);                     /* 0..2   load a sprite effect */
    W(s, 6, 1, 321);                      /* 3..5   wait for an effect model's file */
    W(s, 2, 0, 0, 14);                    /* 6..9   own model: play motion 14 */
    W(s, 11, 65);                         /* 10..11 wait */
    W(s, 6, 0, 321, 0, 3968, -5248, 113, 0, 0); /* 12..20 effect model at the caster's mouth, rotated 113 */
    W(s, 3, 0, 1014, 0, -3968, 4992);     /* 21..26 sprite effect at the caster's mouth */
    W(s, 3, 0, 2, 0, 0, 32767);           /* 27..32 sprite effect on the screen */
    W(s, 3, 0, 9, 10, -900, -5120);       /* 33..38 sprite effect on the target's half */
    W(s, 5, 60, 0, 6);                    /* 39..42 camera 6 over 60 frames */
    W(s, 5, 0, 0, 1);                     /* 43..46 camera 1 */
    W(s, 5, 0, 0, 0);                     /* 47..50 camera 0 */
    W(s, 2, 2, 1);                        /* 51..53 hide the partner's model 0 (the target) */
    W(s, 2, 1, 4);                        /* 54..56 show the enemy's own model by its absolute type */
    W(s, 2, 3, 0, 100, 200, -300, 20);    /* 57..63 move the own model */
    W(s, 2, 3, 1, 100, 200, -300, 20);    /* 64..70 move the target's model */
    W(s, 2, 4, 0, 5, 300, 7, 0);          /* 71..77 turn the own model */
    W(s, 2, 5, 0, 4, 77);                 /* 78..82 jump of kind 4 with a distance */
    W(s, 2, 7, 0, 30);                    /* 83..86 move home */
    W(s, 1, 0);                           /* 87..88 hits */
    W(s, 10, 99, 0);                      /* 89..91 sound */
    W(s, 7, 0, 5);                        /* 92..94 fade */
    W(s, 4, 0, 56, 4, 4);                 /* 95..99 stage */
    W(s, 8);                              /* 100    no-op */
    W(s, 255);                            /* 101    end */
}

static int makeBack(const Script *in, Script *out, int fromEnemy, int toEnemy, char *why) {
    ScriptPlace a = fromEnemy ? kEnemyPlace : kPartnerPlace, b = toEnemy ? kEnemyPlace : kPartnerPlace;

    return scriptConvert(in->b, (size_t)in->n * 2, fromEnemy, a, toEnemy, b, out->b, sizeof out->b, why, 200);
}

static void testLength(void) {
    Script s;
    char why[200];

    enemyScript(&s);
    CHECK(scriptLength(s.b, (size_t)s.n * 2, why, sizeof why) == s.n * 2);
    CHECK(s.n == 102);
    /* a script cut short, a command that does not exist, one that never closes */
    CHECK(scriptLength(s.b, 19 * 2, why, sizeof why) == -1 && strstr(why, "ends") != NULL);
    {
        Script t;

        memset(&t, 0, sizeof t);
        W(&t, 3, 1, 5, 99, 0);
        CHECK(scriptLength(t.b, (size_t)t.n * 2, why, sizeof why) == -1 && strstr(why, "99") != NULL);
        memset(&t, 0, sizeof t);
        W(&t, 3, 1, 5, 11, 4);
        CHECK(scriptLength(t.b, (size_t)t.n * 2, why, sizeof why) == -1);
        memset(&t, 0, sizeof t);
        W(&t, 2, 9, 0, 0);
        CHECK(scriptLength(t.b, (size_t)t.n * 2, why, sizeof why) == 8);
        memset(&t, 0, sizeof t);
        W(&t, 2, 42, 0, 0, 0);
        CHECK(scriptLength(t.b, (size_t)t.n * 2, why, sizeof why) == -1 && strstr(why, "model command 42") != NULL);
    }
    /* the stage command that names no stage (-1) has no fade times; a camera with a view of its own is 17 words */
    {
        Script t;

        memset(&t, 0, sizeof t);
        W(&t, 4, 0, -1, 4, 1, 3, 255);
        CHECK(scriptLength(t.b, (size_t)t.n * 2, why, sizeof why) == 14);
        memset(&t, 0, sizeof t);
        W(&t, 5, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 0);
        CHECK(scriptLength(t.b, (size_t)t.n * 2, why, sizeof why) == 36);
    }
}

static void testToPartner(void) {
    Script s, o;
    char why[200];
    int n;
    unsigned char *p;

    enemyScript(&s);
    n = makeBack(&s, &o, 1, 0, why);
    CHECK(n == 102 * 2);
    if (n != 102 * 2) {
        printf("  why: %s\n", why);
        return;
    }
    p = o.b;
    /* loads, waits, the own model's motion and the commands with no side: untouched */
    CHECK(at(p, 0) == 3 && at(p, 1) == 1 && at(p, 2) == 1014 && at(p, 6) == 2 && at(p, 7) == 0 && at(p, 8) == 0 && at(p, 9) == 14);
    CHECK(at(p, 10) == 11 && at(p, 11) == 65);
    /* an effect model on the caster's half: y moves with the height (2304 -> 0), z mirrors and moves with the distance (2304 -> 3328),
       the yaw turns round (0 -> 0x800) and the pitch stays */
    CHECK(at(p, 12) == 6 && at(p, 13) == 0 && at(p, 14) == 321);
    CHECK(at(p, 15) == 0 && at(p, 16) == 3968 - 2304 && at(p, 17) == 5248 + 1024);
    CHECK(at(p, 18) == 113 && at(p, 19) == 2048 && at(p, 20) == 0);
    /* a sprite effect on the caster's half: raw positions */
    CHECK(at(p, 21) == 3 && at(p, 22) == 0 && at(p, 23) == 1014);
    CHECK(at(p, 24) == 0 && at(p, 25) == -3968 + 2304 && at(p, 26) == -4992 - 1024);
    /* a screen position stays; one on the target's half only mirrors */
    CHECK(at(p, 30) == 0 && at(p, 31) == 0 && at(p, 32) == 32767);
    CHECK(at(p, 36) == 10 && at(p, 37) == -900 && at(p, 38) == 5120);
    /* cameras: 6 -> 2, 1 -> 5, 0 stays; the fade time stays */
    CHECK(at(p, 39) == 5 && at(p, 40) == 60 && at(p, 41) == 0 && at(p, 42) == 2);
    CHECK(at(p, 46) == 5 && at(p, 50) == 0);
    /* model types: the partner's 1 becomes the enemy's 4 and the other way round */
    CHECK(at(p, 51) == 2 && at(p, 52) == 2 && at(p, 53) == 4);
    CHECK(at(p, 54) == 2 && at(p, 55) == 1 && at(p, 56) == 1);
    /* the own model's move: x as is, y and z with the caster; the target's: z mirrored only */
    CHECK(at(p, 59) == 0 && at(p, 60) == 100 && at(p, 61) == 200 - 2304 && at(p, 62) == 300 + 1024 && at(p, 63) == 20);
    CHECK(at(p, 66) == 4 && at(p, 67) == 100 && at(p, 68) == 200 && at(p, 69) == 300 && at(p, 70) == 20);
    /* the own model's turn: pitch 5 stays, yaw 300 -> -300 - 2048 (= 1748 round), roll 7 -> -7 */
    CHECK(at(p, 71) == 2 && at(p, 72) == 4 && at(p, 73) == 0 && at(p, 74) == 5 && at(p, 75) == 1748 && at(p, 76) == -7);
    /* jump, move home, hits, sound, fade, stage, no-op and end: untouched */
    CHECK(at(p, 78) == 2 && at(p, 79) == 5 && at(p, 80) == 0 && at(p, 81) == 4 && at(p, 82) == 77);
    CHECK(at(p, 83) == 2 && at(p, 84) == 7 && at(p, 86) == 30);
    CHECK(at(p, 87) == 1 && at(p, 88) == 0 && at(p, 89) == 10 && at(p, 90) == 99 && at(p, 92) == 7 && at(p, 95) == 4 && at(p, 98) == 4);
    CHECK(at(p, 100) == 8 && at(p, 101) == 255);
}

static void testSameSideAndBack(void) {
    Script s, o, back;
    char why[200];

    enemyScript(&s);
    /* the same side is a copy */
    CHECK(makeBack(&s, &o, 1, 1, why) == 102 * 2 && memcmp(s.b, o.b, 102 * 2) == 0);
    /* to the partner and back is the script it was */
    CHECK(makeBack(&s, &o, 1, 0, why) == 102 * 2);
    o.n = s.n;
    CHECK(makeBack(&o, &back, 0, 1, why) == 102 * 2);
    CHECK(memcmp(s.b, back.b, 102 * 2) == 0);
    if (memcmp(s.b, back.b, 102 * 2) != 0) {
        int i;

        for (i = 0; i < 102; i++) {
            if (at(s.b, i) != at(back.b, i)) printf("  word %d: %d, back %d\n", i, at(s.b, i), at(back.b, i));
        }
    }
}

static void testRefusals(void) {
    Script s, o;
    char why[200];

    memset(&s, 0, sizeof s);
    W(&s, 5, 0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 255);
    CHECK(makeBack(&s, &o, 1, 0, why) == -1 && strstr(why, "camera") != NULL && strstr(why, "view of its own") != NULL);
    /* the same side does not care */
    CHECK(makeBack(&s, &o, 1, 1, why) == 18 * 2);
    memset(&s, 0, sizeof s);
    W(&s, 5, 0, 0, 4, 255);
    CHECK(makeBack(&s, &o, 1, 0, why) == -1 && strstr(why, "camera 4") != NULL);
    /* the camera of an unknown id acts as the enemy's view (id 0): left as it is */
    memset(&s, 0, sizeof s);
    W(&s, 5, 0, 0, 9, 255);
    CHECK(makeBack(&s, &o, 1, 0, why) == 10 && at(o.b, 3) == 9);
    /* a position that does not fit 16 bits after moving */
    memset(&s, 0, sizeof s);
    W(&s, 3, 0, 1014, 0, 0, 32000, 255);
    {
        ScriptPlace far = {0, 30000}, near = {0, 0};

        CHECK(scriptConvert(s.b, (size_t)s.n * 2, 1, near, 0, far, o.b, sizeof o.b, why, sizeof why) == -1 && strstr(why, "16 bits") != NULL);
    }
    /* the output buffer is too small */
    enemyScript(&s);
    CHECK(scriptConvert(s.b, (size_t)s.n * 2, 1, kEnemyPlace, 0, kPartnerPlace, o.b, 10, why, sizeof why) == -1 && strstr(why, "room") != NULL);
}

int main(void) {
    testLength();
    testToPartner();
    testSameSideAndBack();
    testRefusals();
    if (failures != 0) {
        printf("scriptmirrortest: %d failure(s)\n", failures);
        return 1;
    }
    printf("scriptmirrortest: ok\n");
    return 0;
}
