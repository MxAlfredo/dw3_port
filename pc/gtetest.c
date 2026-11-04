/*
 * Unit test for the retail-exact libgte replacements in pc/shim/host_gte.c (GTE audit G2). The expected values are worked out
 * from the retail routines in asm/eu/main/psyq/libgte_*.s (mtx_08 ScaleMatrix,
 * mtx_03/mtx_04 MulMatrix/MulMatrix2, rmat_00 RotMatrixZYX_gte) and from the
 * InitGeom constants, not from PsyCross. No disc, no window.
 *
 * `make -C pc gtetest` links the shim and the --wrap redirects the game's
 * boot link uses (GREEN); `make -C pc gtetest-base` builds the same checks
 * against plain PsyCross (RED, the behavior before the shim).
 *
 * Exit 0: every check held. Exit 1: at least one differed (listed on stderr).
 */
#include <stdio.h>
#include <string.h>
#include <psx/gtereg.h>

#ifdef GTETEST_BASELINE
static void hostGeomInit(void) {
    InitGeom();
}
#else
extern void hostGeomInit(void);
#endif

static int failures;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    }
}

static int rotEq(const MATRIX *m, const short e[3][3]) {
    return memcmp(m->m, e, sizeof(short) * 9) == 0;
}

static void setRot(MATRIX *m, const short r[3][3], int t0, int t1, int t2) {
    memcpy(m->m, r, sizeof(short) * 9);
    m->t[0] = t0;
    m->t[1] = t1;
    m->t[2] = t2;
}

static void testScale(void) {
    static const short in[3][3] = {{100, 200, 300}, {400, 500, 600}, {700, 800, 900}};
    /* columns scaled by (0.5, 1.0, 2.0): m[i][j] * s[j] >> 12 */
    static const short col[3][3] = {{50, 200, 600}, {200, 500, 1200}, {350, 800, 1800}};
    static const short neg[3][3] = {{-50, 200, 600}, {-200, 500, 1200}, {-350, 800, 1800}};
    MATRIX m;
    VECTOR s;

    setRot(&m, in, 11, 22, 33);
    s.vx = 2048;
    s.vy = 4096;
    s.vz = 8192;
    check(ScaleMatrix(&m, &s) == &m, "ScaleMatrix returns its matrix");
    check(rotEq(&m, col), "ScaleMatrix scales columns, not rows");
    check(m.t[0] == 11 && m.t[1] == 22 && m.t[2] == 33, "ScaleMatrix keeps translation");

    setRot(&m, in, 0, 0, 0);
    s.vx = -2048;
    check((ScaleMatrix(&m, &s), rotEq(&m, neg)), "ScaleMatrix negative scale (arithmetic shift)");
}

static void testMul(void) {
    /* a = rotation 90 about Z-ish and b = scale: the product is easy to hand-check */
    static const short a[3][3] = {{0, -4096, 0}, {4096, 0, 0}, {0, 0, 4096}};
    static const short b[3][3] = {{2048, 0, 0}, {0, 1024, 0}, {0, 0, 4096}};
    /* a*b */
    static const short ab[3][3] = {{0, -1024, 0}, {2048, 0, 0}, {0, 0, 4096}};
    static const short sq[3][3] = {{-4096, 0, 0}, {0, -4096, 0}, {0, 0, 4096}}; /* a*a */
    MATRIX m0;
    MATRIX m1;

    setRot(&m0, a, 1, 2, 3);
    setRot(&m1, b, 7, 8, 9);
    check(MulMatrix(&m0, &m1) == &m0, "MulMatrix returns m0");
    check(rotEq(&m0, ab), "MulMatrix rotation m0 = m0*m1");
    check(m0.t[0] == 1 && m0.t[1] == 2 && m0.t[2] == 3, "MulMatrix keeps m0 translation");
    check(m1.t[0] == 7 && m1.t[1] == 8 && m1.t[2] == 9, "MulMatrix leaves m1 alone");

    setRot(&m0, a, 1, 2, 3);
    setRot(&m1, b, 7, 8, 9);
    check(MulMatrix2(&m0, &m1) == &m1, "MulMatrix2 returns m1");
    check(rotEq(&m1, ab), "MulMatrix2 rotation m1 = m0*m1");
    check(m1.t[0] == 7 && m1.t[1] == 8 && m1.t[2] == 9, "MulMatrix2 keeps m1 translation");
    check(rotEq(&m0, a) && m0.t[0] == 1 && m0.t[2] == 3, "MulMatrix2 leaves m0 alone");

    setRot(&m0, a, 4, 5, 6);
    MulMatrix(&m0, &m0);
    check(rotEq(&m0, sq) && m0.t[0] == 4 && m0.t[1] == 5 && m0.t[2] == 6,
          "MulMatrix in place (m0 * m0), translation kept");
    setRot(&m1, a, 4, 5, 6);
    MulMatrix2(&m1, &m1);
    check(rotEq(&m1, sq) && m1.t[0] == 4 && m1.t[1] == 5 && m1.t[2] == 6,
          "MulMatrix2 in place (m1 = m1 * m1), translation kept");
}

/* the retail RotMatrixZYX_gte (rmat_00), written out per element */
static void zyxRef(const SVECTOR *r, short e[3][3]) {
    int s[3];
    int c[3];
    int i;
    const int ang[3] = {r->vx, r->vy, r->vz};
    int A;
    int D;

    for (i = 0; i < 3; i++) {
        int idx = (ang[i] < 0 ? -ang[i] : ang[i]) & 0xFFF;
        s[i] = rsin(idx);
        c[i] = rcos(idx);
        if (ang[i] < 0) {
            s[i] = -s[i];
        }
    }
    A = (s[0] * s[1]) >> 12;
    D = (c[0] * s[1]) >> 12;
    e[0][0] = (c[2] * c[1]) >> 12;
    e[0][1] = ((c[2] * A) >> 12) - ((c[0] * s[2]) >> 12);
    e[0][2] = ((c[2] * D) >> 12) + ((s[0] * s[2]) >> 12);
    e[1][0] = (s[2] * c[1]) >> 12;
    e[1][1] = ((s[2] * A) >> 12) + ((c[0] * c[2]) >> 12);
    e[1][2] = ((s[2] * D) >> 12) - ((s[0] * c[2]) >> 12);
    e[2][0] = -s[1];
    e[2][1] = (c[1] * s[0]) >> 12;
    e[2][2] = (c[1] * c[0]) >> 12;
}

static void testZyx(void) {
    static const short angles[][3] = {
        {0x200, 0, 0},         {0x100, 0x200, 0x300}, {-0x123, 0x2A5, -0x7F1},
        {0x1234, -0x1F00, 0x0FFF}, {0x3C7, 0x155, 0x0AA},
    };
    /* hand-worked: rx = 45 deg only (sin = cos = 2896), exact per the asm */
    static const short rx45[3][3] = {{4096, 0, 0}, {0, 2896, -2896}, {0, 2896, 2896}};
    unsigned i;
    MATRIX m;
    SVECTOR r;
    short e[3][3];
    char what[64];

    memset(&m, 0, sizeof(m));
    m.t[0] = 5;
    m.t[1] = 6;
    m.t[2] = 7;
    r.vx = 0x200;
    r.vy = 0;
    r.vz = 0;
    RotMatrixZYX_gte(&r, &m);
    check(rotEq(&m, rx45), "RotMatrixZYX_gte rx=45deg hand-worked elements");
    check(m.t[0] == 5 && m.t[1] == 6 && m.t[2] == 7, "RotMatrixZYX_gte keeps translation");

    for (i = 0; i < sizeof(angles) / sizeof(angles[0]); i++) {
        r.vx = angles[i][0];
        r.vy = angles[i][1];
        r.vz = angles[i][2];
        zyxRef(&r, e);
        RotMatrixZYX_gte(&r, &m);
        sprintf(what, "RotMatrixZYX_gte angles %d,%d,%d (retail per-element rounding)",
                angles[i][0], angles[i][1], angles[i][2]);
        check(rotEq(&m, e), what);
    }
}

static void testDq(void) {
    hostGeomInit();
    check(C2_DQA == -0x1062, "geometry init: DQA = -0x1062");
    check(C2_DQB == 0x1400000, "geometry init: DQB = 0x1400000");
}

int main(void) {
    testScale();
    testMul();
    testZyx();
    testDq();
    if (failures) {
        fprintf(stderr, "gtetest: %d check(s) failed\n", failures);
        return 1;
    }
    printf("gtetest: all checks passed\n");
    return 0;
}
