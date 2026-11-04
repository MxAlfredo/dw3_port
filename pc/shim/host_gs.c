/*
 * Host shim for libgs (ADR-002: PsyCross does not provide it, so it is
 * ours). The routines the game calls are translations of the retail library
 * (asm/eu/main/psyq/libgs_*.s, generated locally from the owner's disc and
 * never committed); each function names the file it follows.
 *
 *   GsInitGraph, GsInit3D   record the graph size and reset the 3D state the
 *                           way the library does; on the host PsyCross's
 *                           libgpu already is the graph.
 *   GsGetTimInfo            main reads the boot TIM out of the overlay area
 *                           and uploads it with LoadImage.
 *   GsSetRefView2           builds the world->screen matrix from a view
 *                           (position, reference point, roll, super chain).
 *   GsSetFlatLight          fills the light and colour matrices.
 *   GsGetLw, GsMulCoord2/3  the coordinate-chain helpers the two above use.
 *
 * The 64-bit host differs from the PlayStation in two ways that matter here:
 * `long` is 64 bits (the game's GsRVIEW2 fields are read as 32-bit values) and
 * the 32-bit mult/mflo products of the library are reproduced with u32 math.
 * PsyCross's MulMatrix/MulMatrix2 also store a whole MATRIX (translation
 * included, from an uninitialised temporary) where the PsyQ routines write the
 * rotation only; the boot link redirects both to shim/host_gte.c
 * (-Wl,--wrap), which writes the rotation only, so the calls here are plain.
 *
 * This unit is compiled with the retail libgs.h (the game's headers ask for
 * it), so GsIMAGE/GsRVIEW2/GsF_LIGHT are the types the game's own translation
 * units mean, and PsyCross's libgte does the rotation math.
 *
 * The boot TIM: on the PlayStation the executable is followed by a 320x480
 * TIM, and main finds it at SUB_OVERLAY_ADDRESS (the stage/overlay area of
 * the memory map) -- it is the logo shown while the drive spins up. The host
 * binary has no such image: SUB_OVERLAY_ADDRESS is one of pc/boot.c's own
 * buffers, zeroed, and its bytes are not a TIM. So this shim reports the
 * TIM's structure the way the real one does when it is there, and an empty
 * region when it is not: the game's own LoadImage then clears that block of
 * VRAM instead of uploading a boot image. Nothing is invented -- the region
 * the boot image would occupy simply is not part of the port yet.
 */
#include "engine/game.h"

#include <libgs.h>

/* A TIM's block header: its size in bytes, then the VRAM position and size */
typedef struct HostTimBlock {
    u32 size;
    u16 x;
    u16 y;
    u16 w;
    u16 h;
} HostTimBlock;

/* main's BOOT_IMAGE_RECT: the 320x480 region LoadImage copies from pixel */
#define BOOT_IMAGE_PIXELS (320 * 480)

/*
 * What GsGetTimInfo hands back when there is no TIM: `pixel` still has to be
 * readable for BOOT_IMAGE_PIXELS halfwords -- main uploads the boot image
 * with its own fixed rect, not with the TIM's size -- so the empty region is
 * a buffer of zeros of exactly that size.
 */
static u16 EMPTY_IMAGE[BOOT_IMAGE_PIXELS];

/* The last graph size GsInitGraph was given, so the mode is visible */
s32 GS_SCREEN_WIDTH;
s32 GS_SCREEN_HEIGHT;

/* The last graph size GsInitGraph was given, so the mode is visible */
s32 GS_SCREEN_WIDTH;
s32 GS_SCREEN_HEIGHT;

/* The library's working space. GsWSMATRIX and GsLIGHTWSMATRIX are public
   (the retail headers declare them and the battle's own code reads them); the
   rest is what the library keeps in private globals. */
MATRIX GsWSMATRIX = { { { 0x1000, 0, 0 }, { 0, 0x1000, 0 }, { 0, 0, 0x1000 } },
                      { 0, 0, 0 } };
MATRIX GsLIGHTWSMATRIX = { { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                           { 0, 0, 0 } };
static MATRIX GS_VIEW_COPY;   /* D_80080AD0: GsSetRefView2's last result */
static MATRIX GS_COLOR_SAVED; /* D_80080AB0: the colour matrix GsSetFlatLight keeps */
static s32 GS_LIGHT_MODE;     /* D_80080A84 */
static s32 GS_FRAME_FLAG = 1; /* D_80080A70: the value a coordinate's flg must
                                 carry to count as computed this frame */

#define GS_ONE 0x1000
#define GS_COORD_CHAIN 100 /* the library's own chain limit (0x64) */

static void identityMatrix(MATRIX *m) {
    s32 i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            m->m[i][j] = (i == j) ? GS_ONE : 0;
        }
        m->t[i] = 0;
    }
}

/* TransposeMatrix (libgte_fgo_00.s) is not in PsyCross: rotation only, the
   translation is left alone. */
static void transposeRot(MATRIX *src, MATRIX *dst) {
    MATRIX tmp = *dst;
    s32 i, j;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            tmp.m[i][j] = src->m[j][i];
        }
    }
    *dst = tmp;
}

/* The library divides with the CPU's signed div; a zero divisor traps there
   and is never reached, the INT_MIN / -1 overflow wraps. */
static s32 divSigned(s32 a, s32 b) {
    if (b == 0) {
        return 0;
    }
    if (b == -1) {
        return (s32)(0u - (u32)a);
    }
    return a / b;
}

static s32 shl12(s32 v) {
    return (s32)((u32)v << 12);
}

/* The sum of three 32-bit squares, wrapping as the mult/mflo/addu chain does */
static s32 squares3(s32 a, s32 b, s32 c) {
    return (s32)((u32)a * (u32)a + (u32)b * (u32)b + (u32)c * (u32)c);
}

void GsInitGraph(unsigned short x, unsigned short y, unsigned short intmode,
                 unsigned short dith, unsigned short varmmode) {
    (void)intmode;
    (void)dith;
    (void)varmmode;
    GS_SCREEN_WIDTH = x;
    GS_SCREEN_HEIGHT = y;
    /* libgs_gs_001.s (the init body): the light and colour matrices start
       all-zero, and the per-frame flag starts at 1 */
    GsLIGHTWSMATRIX = (MATRIX){ { { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 } },
                                { 0, 0, 0 } };
    GS_COLOR_SAVED = GsLIGHTWSMATRIX;
    GS_FRAME_FLAG = 1;
}

/* libgs_gs_104.s: the draw offset and clip bookkeeping have no host
   counterpart (libgpu is PsyCross's); the light mode returns to 0. */
void GsInit3D(void) {
    GS_LIGHT_MODE = 0;
}

/* libgs_gs_108.s: modes 0..3 are stored, anything else is reported by the
   retail library and ignored. */
void GsSetLightMode(s32 mode) {
    if (mode >= 0 && mode <= 3) {
        GS_LIGHT_MODE = mode;
    }
}

/*
 * libgs_gs_107.s. Row idx of the light matrix is the light direction,
 * negated and normalised to 4096; column idx of the colour matrix is the
 * light's colour scaled from 0..255 to 0..4096. The colour matrix is kept for
 * the next call and handed to the GTE.
 */
s32 GsSetFlatLight(s32 idx, GsF_LIGHT *light) {
    MATRIX lights = GsLIGHTWSMATRIX;
    MATRIX colors = GS_COLOR_SAVED;
    s32 len = SquareRoot0(squares3(light->vx, light->vy, light->vz));
    s32 rgb[3];
    s32 i;

    if (len == 0) {
        return -1;
    }
    if (idx >= 0 && idx <= 2) {
        rgb[0] = light->r;
        rgb[1] = light->g;
        rgb[2] = light->b;
        lights.m[idx][0] = (s16)divSigned(shl12(-light->vx), len);
        lights.m[idx][1] = (s16)divSigned(shl12(-light->vy), len);
        lights.m[idx][2] = (s16)divSigned(shl12(-light->vz), len);
        for (i = 0; i < 3; i++) {
            /* x / 255 as the library's reciprocal multiply computes it */
            s32 x = rgb[i] << 12;
            s32 hi = (s32)(((s64)x * (s32)0x80808081) >> 32);

            colors.m[i][idx] = (s16)((hi + x) >> 7);
        }
    }
    GsLIGHTWSMATRIX = lights;
    GS_COLOR_SAVED = colors;
    SetColorMatrix(&colors);
    return 0;
}

/*
 * libgs_gs_123.s. An identity matrix with the sine/cosine of a rotation about
 * one axis: 'x'/'X', 'y'/'Y' or 'z'/'Z'; any other axis leaves the identity.
 */
static void makeAxisMatrix(MATRIX *m, s32 sin, s32 cos, s32 axis) {
    s32 which = (s32)(s8)(axis - 'X');

    identityMatrix(m);
    if (which == 0 || which == 'x' - 'X') {
        m->m[1][1] = cos;
        m->m[2][2] = cos;
        m->m[1][2] = -sin;
        m->m[2][1] = sin;
    } else if (which == 1 || which == 'y' - 'X') {
        m->m[0][0] = cos;
        m->m[2][2] = cos;
        m->m[0][2] = sin;
        m->m[2][0] = -sin;
    } else if (which == 2 || which == 'z' - 'X') {
        m->m[0][0] = cos;
        m->m[1][1] = cos;
        m->m[0][1] = -sin;
        m->m[1][0] = sin;
    }
}

/*
 * libgs_gs_119.s. Pre-multiplies m by a rotation about z; the angle is in
 * 1/360 of the 4096-per-turn unit (so 360 * 4096 is one turn).
 */
static void rotateZMatrix(MATRIX *m, s32 angle) {
    MATRIX roll;
    s32 turn;
    s32 c;
    s32 s;

    if (angle == 0) {
        return;
    }
    turn = angle / 360;
    c = rcos(turn);
    s = rsin(turn);
    identityMatrix(&roll);
    roll.m[0][0] = c;
    roll.m[0][1] = -s;
    roll.m[1][0] = s;
    roll.m[1][1] = c;
    MulMatrix(m, &roll);
}

/*
 * libgs_matrix9.s: b is the child in a's frame; a becomes a * b, rotation and
 * translation.
 */
static void mulCoord3(MATRIX *a, MATRIX *b) {
    VECTOR moved;

    ApplyMatrixLV(a, (VECTOR *)b->t, &moved);
    MulMatrix(a, b);
    a->t[0] += moved.vx;
    a->t[1] += moved.vy;
    a->t[2] += moved.vz;
}

/* libgs_matrix8.s: the same product, but it lands in b. */
void GsMulCoord2(MATRIX *a, MATRIX *b) {
    VECTOR moved;

    ApplyMatrixLV(a, (VECTOR *)b->t, &moved);
    MulMatrix2(a, b);
    b->t[0] = moved.vx + a->t[0];
    b->t[1] = moved.vy + a->t[1];
    b->t[2] = moved.vz + a->t[2];
}

void GsMulCoord3(MATRIX *a, MATRIX *b) {
    mulCoord3(a, b);
}

/*
 * libgs_gs_133.s: the local-to-world matrix of a coordinate system. The chain
 * of supers is walked up to the first level whose workm is current (its flg is
 * this frame's flag), or to the root; the levels below are then composed down
 * and stamped current. A level whose flg is 0 is marked changed: the walk
 * keeps the highest such level and starts from the one above it.
 */
void GsGetLw(GsCOORDINATE2 *coord, MATRIX *out) {
    GsCOORDINATE2 *chain[GS_COORD_CHAIN + 1];
    GsCOORDINATE2 *node = coord;
    s32 depth = 0;
    s32 changed = GS_COORD_CHAIN;
    s32 base;

    for (;;) {
        chain[depth] = node;
        if (node->super == NULL) {
            if (node->flg == (unsigned long)GS_FRAME_FLAG || node->flg == 0) {
                node->workm = node->coord;
                *out = node->workm;
                node->flg = GS_FRAME_FLAG;
                base = depth;
            } else if (changed == GS_COORD_CHAIN) {
                *out = chain[0]->workm;
                base = 0;
            } else {
                *out = chain[changed + 1]->workm;
                base = changed + 1;
            }
            break;
        }
        if (node->flg == (unsigned long)GS_FRAME_FLAG) {
            *out = node->workm;
            base = depth;
            break;
        }
        if (node->flg == 0) {
            changed = depth;
        }
        node = node->super;
        depth++;
        if (depth >= GS_COORD_CHAIN) {
            /* a super loop; the library would run off its table here */
            *out = chain[0]->workm;
            base = 0;
            break;
        }
    }
    while (base > 0) {
        GsCOORDINATE2 *below = chain[base - 1];

        mulCoord3(out, &below->coord);
        below->workm = *out;
        below->flg = GS_FRAME_FLAG;
        base--;
    }
}

/*
 * libgs_gs_131.s (and its helpers func_8002A188/A274/A33C): the view's
 * rotation looks from vp to vr (pitch about x, then yaw about y) with the
 * roll about z, and its translation is -vp seen through that rotation. With a
 * super coordinate the result is carried through the inverse of the super's
 * local-to-world. The result is also kept in GS_VIEW_COPY. It returns 1
 * (after clearing GsWSMATRIX to the roll alone) when vp and vr coincide.
 * GsLIGHTWSMATRIX is not part of this routine.
 */
s32 GsSetRefView2(GsRVIEW2 *view) {
    s32 p[6];
    s32 shift = 0;
    s32 peak = 0;
    s32 dx, dy, dz;
    s32 r, h, sinp, cosp, siny, cosy;
    MATRIX tmp;
    VECTOR eye;
    s32 i;

    identityMatrix(&GsWSMATRIX);
    rotateZMatrix(&GsWSMATRIX, -(s32)view->rz);

    p[0] = (s32)view->vpx;
    p[1] = (s32)view->vpy;
    p[2] = (s32)view->vpz;
    p[3] = (s32)view->vrx;
    p[4] = (s32)view->vry;
    p[5] = (s32)view->vrz;
    /* the six values are scaled together when they would not leave room for
       the squares: shifted right by (bit count of the largest magnitude) - 15 */
    for (i = 0; i < 6; i++) {
        s32 a = p[i] < 0 ? (s32)(0u - (u32)p[i]) : p[i];

        if (a > peak) {
            peak = a;
        }
    }
    while (peak > 0) {
        peak >>= 1;
        shift++;
    }
    if (shift >= 16) {
        for (i = 0; i < 6; i++) {
            p[i] >>= shift - 15;
        }
    }

    dx = p[3] - p[0];
    dy = p[4] - p[1];
    dz = p[5] - p[2];
    r = SquareRoot0(squares3(dx, dy, dz));
    if (r == 0) {
        return 1;
    }

    sinp = -divSigned(shl12(p[1] - p[4]), r);
    h = SquareRoot0(squares3(dx, 0, dz));
    cosp = divSigned(shl12(h), r);
    makeAxisMatrix(&tmp, (s16)sinp, (s16)cosp, 'x');
    MulMatrix(&GsWSMATRIX, &tmp);

    if (h != 0) {
        siny = divSigned(shl12(p[3] - p[0]), h);
        cosy = divSigned(shl12(p[5] - p[2]), h);
        makeAxisMatrix(&tmp, (s16)-siny, (s16)cosy, 'y');
        MulMatrix(&GsWSMATRIX, &tmp);
    }

    /* the translation uses the view point as given, not the scaled copy */
    eye.vx = -(s32)view->vpx;
    eye.vy = -(s32)view->vpy;
    eye.vz = -(s32)view->vpz;
    ApplyMatrixLV(&GsWSMATRIX, &eye, (VECTOR *)GsWSMATRIX.t);

    if (view->super != NULL) {
        MATRIX lw;
        MATRIX inv;
        VECTOR moved;

        GsGetLw(view->super, &lw);
        inv = lw;
        transposeRot(&lw, &inv);
        ApplyMatrixLV(&inv, (VECTOR *)lw.t, &moved);
        inv.t[0] = -moved.vx;
        inv.t[1] = -moved.vy;
        inv.t[2] = -moved.vz;
        GsMulCoord2(&GsWSMATRIX, &inv);
        GsWSMATRIX = inv;
    }
    GS_VIEW_COPY = GsWSMATRIX;
    return 0;
}

/*
 * libgs.h's projection store. The game's only writer-to-reader chain is
 * battle_camera.c's keepView -> layerLoadView (retail layer.c:366), and on
 * the GTE side a projection is the screen distance that every RTPS divides
 * by (the C2_H register; an InitGeom-run that nobody re-runs would leave it
 * at the default 1000). So the shim's routine lands the value there, the way
 * the GTE's own setter spells it: nothing else in the game calls the camera
 * distance routine directly.
 */
void GsSetProjection(long h) {
    SetGeomScreen((int)h);
}

/*
 * Reads the TIM that follows an ID word (main passes SUB_OVERLAY_ADDRESS + 1,
 * the word after the magic) into the game's GsIMAGE, the way the real routine
 * does: the flag word, the CLUT block when the flag's bit 3 is set, then the
 * image block (a size, a position, a size, the pixels).
 *
 * The bytes the host's overlay area holds are not a TIM (they are zero), and
 * a zero image block has no rows: when the image block has no width or height
 * there is nothing to upload, so `pixel` stays the empty region. That is the
 * only rule this shim adds, and it is what keeps the following LoadImage from
 * reading elsewhere.
 */
void GsGetTimInfo(unsigned long *im, GsIMAGE *tim) {
    u32 flag = (u32)im[0];
    HostTimBlock *block;
    u32 *at = (u32 *)im + 1;

    tim->pmode = flag & 7;
    tim->pixel = (unsigned long *)EMPTY_IMAGE;
    tim->px = 0;
    tim->py = 0;
    tim->pw = 0;
    tim->ph = 0;
    tim->clut = NULL;
    tim->cx = 0;
    tim->cy = 0;
    tim->cw = 0;
    tim->ch = 0;

    if (flag & 8) {
        block = (HostTimBlock *)at;
        tim->cx = block->x;
        tim->cy = block->y;
        tim->cw = block->w;
        tim->ch = block->h;
        tim->clut = (unsigned long *)((u8 *)block + block->size);
        at = (u32 *)((u8 *)block + block->size);
    }
    block = (HostTimBlock *)at;
    if (block->w == 0 || block->h == 0) {
        return;
    }
    tim->px = block->x;
    tim->py = block->y;
    tim->pw = block->w;
    tim->ph = block->h;
    tim->pixel = (unsigned long *)((u8 *)block + sizeof(HostTimBlock));
}
