/*
 * Retail-exact replacements for the few libgte matrix routines whose PsyCross
 * versions differ from the PsyQ library the game links (GTE audit G1/G2).
 * PsyCross is a submodule we never patch (ADR-003),
 * so the game's calls are redirected at link time with -Wl,--wrap (see the
 * boot link line in pc/Makefile): the unit's `ScaleMatrix(...)` becomes
 * `__wrap_ScaleMatrix(...)` whatever flags the unit was compiled with.
 *
 * Sources are the retail routines in the locally generated
 * asm/eu/main/psyq/libgte_*.s:
 *   - ScaleMatrix (mtx_08): scales the COLUMNS, m[i][j] = (m[i][j] * s[j]) >> 12.
 *     PsyCross scales the rows.
 *   - MulMatrix / MulMatrix2 (mtx_03 / mtx_04): rotation only; the destination
 *     translation is left alone. PsyCross stores a whole MATRIX back from an
 *     uninitialised temporary, wiping it.
 *   - RotMatrixZYX_gte (rmat_00): every product is shifted by 12 on its own,
 *     PsyCross rounds differently (up to 1/4096 per element).
 *   - InitGeom (DQA/DQB): the retail values, set by hostGeomInit().
 */
#include <libgte.h>

typedef int s32;

/* GTE IR saturation of an mvmva result (lm = 0): 16-bit signed. */
static short satIr(s32 v) {
    if (v < -32768) {
        return -32768;
    }
    if (v > 32767) {
        return 32767;
    }
    return (short)v;
}

/* rotation part of a * b into out (all 3x3, out may alias a or b), the GTE
   way: each element is a sum of three products shifted right by 12 once. */
static void mulRot(const MATRIX *a, const MATRIX *b, MATRIX *out) {
    short r[3][3];
    int i;
    int j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            long long sum = (long long)a->m[i][0] * b->m[0][j] +
                            (long long)a->m[i][1] * b->m[1][j] +
                            (long long)a->m[i][2] * b->m[2][j];
            r[i][j] = satIr((s32)(sum >> 12));
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out->m[i][j] = r[i][j];
        }
    }
}

MATRIX *__wrap_MulMatrix(MATRIX *m0, MATRIX *m1) {
    mulRot(m0, m1, m0);
    return m0;
}

MATRIX *__wrap_MulMatrix2(MATRIX *m0, MATRIX *m1) {
    mulRot(m0, m1, m1);
    return m1;
}

MATRIX *__wrap_ScaleMatrix(MATRIX *m, VECTOR *v) {
    s32 s[3];
    int i;
    int j;

    s[0] = (s32)v->vx;
    s[1] = (s32)v->vy;
    s[2] = (s32)v->vz;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            /* multu low 32 bits, sra 12, halfword store */
            unsigned int p = (unsigned int)(s32)m->m[i][j] * (unsigned int)s[j];
            m->m[i][j] = (short)((s32)p >> 12);
        }
    }
    return m;
}

/* sin/cos of a signed 12-bit-turn angle as the retail table lookup yields
   them: index |a| & 0xFFF, the sign applied to the sine afterwards. */
static void sinCos(int a, s32 *s, s32 *c) {
    int idx = (a < 0 ? -a : a) & 0xFFF;

    *s = rsin(idx);
    *c = rcos(idx);
    if (a < 0) {
        *s = (short)-*s;
    }
}

MATRIX *__wrap_RotMatrixZYX_gte(SVECTOR *r, MATRIX *m) {
    s32 sx;
    s32 cx;
    s32 sy;
    s32 cy;
    s32 sz;
    s32 cz;
    s32 a;
    s32 d;

    sinCos(r->vx, &sx, &cx);
    sinCos(r->vy, &sy, &cy);
    sinCos(r->vz, &sz, &cz);
    a = (sx * sy) >> 12;
    d = (cx * sy) >> 12;
    m->m[0][0] = (short)((cz * cy) >> 12);
    m->m[0][1] = (short)(((cz * a) >> 12) - ((cx * sz) >> 12));
    m->m[0][2] = (short)(((cz * d) >> 12) + ((sx * sz) >> 12));
    m->m[1][0] = (short)((sz * cy) >> 12);
    m->m[1][1] = (short)(((sz * a) >> 12) + ((cx * cz) >> 12));
    m->m[1][2] = (short)(((sz * d) >> 12) - ((sx * cz) >> 12));
    m->m[2][0] = (short)-sy;
    m->m[2][1] = (short)((cy * sx) >> 12);
    m->m[2][2] = (short)((cy * cx) >> 12);
    return m;
}

/* InitGeom, then the depth-cue registers the retail routine leaves
   (DQA = -0x1062, DQB = 0x1400000); PsyCross's InitGeom sets -98 / 340. */
void hostGeomInit(void) {
    InitGeom();
    SetDQA(-0x1062);
    SetDQB(0x1400000);
}
