/*
 * The host graphics engine (T15): the GFX state and the drawing Layers, the
 * two pieces the game's drawing units reach through `GFX.funcs` and
 * `layer->...`. main runs the game's own frame loop over them and the
 * country-select screen draws its own art through them.
 *
 * Why the game's own display.c and layer.c are not linked here (both are in
 * the tree and both compile for the host -- the reasons are semantic):
 *
 *   * src/engine/gfx/layer.c allocates each ordering table as
 *     `otLen * sizeof(u_long)` and walks it as an array of `u_long`. That is
 *     the PlayStation's layout, where a `u_long` and an OT entry are both 4
 *     bytes. On this host the OT entry is PsyCross's OT_TAG (12 bytes: an
 *     8-byte address plus the length), which is what makes the game's own
 *     primitives linkable at all (PsyCross's extended prim pointers), while
 *     `sizeof(u_long)` is 8. The table would be under-allocated and
 *     layerSkipEmptyOt would read every second entry as a word -- the same
 *     `u_long` fork T14 documented, one level up. So createLayer allocates
 *     OT_TAGs, which is the only change from the game's own newLayer.
 *   * src/engine/gfx/display.c's drawFrame ends by waiting for the vsync
 *     interrupt to flip its buffer (`while (FLIP_PENDING) {}`), which is how
 *     the PS1 shows a frame; PsyCross presents in PsyX_EndScene instead (T10:
 *     "nobody else calls it"), and its frame is a single window buffer, not
 *     the PS1's two VRAM areas at y and y+256. The host therefore presents
 *     once per main-loop frame here, exactly as milestones 4 and 5 do
 *     (PsyX_BeginScene / DrawOTag / PsyX_EndScene), and keeps one buffer.
 *
 * What is the game's own code here is the drawing: the mode's sprites come
 * from sprite_drawer.c, its TIMs from tim_loader.c, its drawing calls are
 * addPrim/DrawOTag on PsyCross's own ordering table, and its task tree is
 * task.c's. This file is the port's replacement for the display half, the
 * same role pc/shim/host_draw.c plays for the headless harnesses.
 *
 * The frame is also where the harness's screenshot and exit live: main() of
 * the game never returns (its loop is `for (;;)`), so the port's frame ends
 * by counting presented frames and, once the command's count is reached,
 * saving the glReadPixels capture and exiting.
 */
#include "engine/game.h"
#include "battle/battle_mode.h"
#undef OVL_NAME
#include "battle_speed.h"
#include "battle_trace.h"
#include "fast_transitions.h"
#include "stage_banner.h"
#include "stage_timing.h"
#include "field/field_map.h"
#include "host.h"
#include "fault_report.h"
#include "host_video.h"
#include "port_log.h"
#include "controls_screen.h"
#include "text_icons.h"

/* T5: the host GTE macros for the mesh projection check further down -- the
   shadow header the battle units use (pc/shim/battle_inc), whose primitives
   are PsyCross's emulated GTE in place of the game's MIPS assembler. */
#include "battle_inc/engine/gte.h"

extern MATRIX GsWSMATRIX; /* shim/host_gs.c keeps the world-screen one */

#include <PsyX/PsyX_public.h>
#include <PsyX/PsyX_render.h>
#include <PsyX/PsyX_overlay.h>

#include <stdio.h>
#include <stddef.h>

/* <stdlib.h> collides with the game's own atoi/random prototypes (gnu89
   headers), so the host-only mirror allocator spells its three functions out */
extern void *malloc(size_t size);
extern void *calloc(size_t nmemb, size_t size);
extern void free(void *ptr);

/*
 * The game's own engine/game.h declares atoi/strlen/random with the types the
 * game means, which clash with the host's <stdlib.h>; so this unit does not
 * include it and takes the names it needs by hand (as the other harnesses
 * do): exit, and getenv for the T26 callback-order diagnostic below.
 */
extern void exit(int status) __attribute__((noreturn));
extern char *getenv(const char *name);

/* The layer's ordering table length, by its otShift - 1 (engine/gfx.h's
   OT_LENGTHS, which layer.c carries on the PlayStation) */
s16 OT_LENGTHS[] = {
    0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200, 0x0400, 0x0800, 0x1000,
};

/* The frame the harness presents, and where it ends */
static s32 presentFrames;

/* forward declarations the T5 DW3_VIEW_DUMP helpers need (host_gs.c shares
   these through host.h) */
static s32 viewDumpArmed(s32 id);
static void viewDumpInitOffset(void);

static s32 hostLayerRunLayerId(Layer *layer);
static s32 hostEnvFrame(const char *name, s32 *read, s32 *frame);
void hostEnvFrameParse(const char *name, s32 *read, s32 *frame);
s32 hostPresentFrame(void);
static ptrdiff_t meshMatrixOff;
static s32 presentPrims;
static s32 presentTarget;
static const char *presentShot;

/*
 * T30: the multi-frame capture list (pc/shim/host.h). The list is one
 * optional path on top of the single-shot capture pcBootPresentInit arms:
 * each entry names a zero-based frame index the host saves as a separate
 * PNG, from the same presented frame and before the buffer swaps, in a
 * single run. `presentShot` is the screenshot path the user passed; the
 * per-index files are written under `<stem>_f<index>.png`, where `stem`
 * is `presentShot` with its last extension stripped. The list is sorted
 * by pcBootMultiCaptureInit so the frame-end check is one walk; a count
 * of 0 leaves the host on the single-shot path.
 */
static s32 captureCount;
static s32 captureIndices[PC_BOOT_MAX_CAPTURES];

/*
 * T19: the field demo's own stop, next to the frame budget. The field's own
 * loader reaches its first requirement a few frames after it starts (a NULL
 * FIELDSTG_state.slots read, see pc/README.md), so the demo's frame budget
 * alone would be a magic number tied to that; instead it stops a requested
 * number of frames after the game's own code has entered a requested mode,
 * which is the same whether the title's intro is short or long.
 */
static s32 stopMode;
static s32 stopModeFrames;
static s32 stopModeSeen;

void pcBootStopInMode(s32 mode, s32 frames) {
    stopMode = mode;
    stopModeFrames = frames;
}

void pcBootPresentInit(int frames, const char *shot) {
    presentTarget = frames;
    presentShot = shot;
    pcRefreshInit();
}

/*
 * T30: arm the multi-frame capture list. Copies the indices into the host's
 * own storage and sorts them ascending, so the per-frame scan at frame end is
 * one walk from the start. Negative or `count` greater than
 * `PC_BOOT_MAX_CAPTURES` are dropped silently: the boot CLI (pc/boot.c) is
 * the one that validates the user input up front, exits 2 on a bad list and
 * never reaches here with one. With `count` 0 the list is left empty and
 * the host stays on the single-shot path.
 */
void pcBootMultiCaptureInit(const int *indices, int count) {
    s32 i;

    if (count < 0) {
        count = 0;
    }
    if (count > PC_BOOT_MAX_CAPTURES) {
        count = PC_BOOT_MAX_CAPTURES;
    }
    for (i = 0; i < count; i++) {
        captureIndices[i] = indices[i];
    }
    /* insertion sort: small N, in place */
    for (i = 1; i < count; i++) {
        s32 v = captureIndices[i];
        s32 j = i - 1;

        while (j >= 0 && captureIndices[j] > v) {
            captureIndices[j + 1] = captureIndices[j];
            j--;
        }
        captureIndices[j + 1] = v;
    }
    captureCount = count;
}

/* Saves the presented frame and exits (the harness's own end) */

/*
 * T30: the path of one multi-capture PNG: `<stem>_f<index>.png`, where `stem`
 * is the user's screenshot path with its last extension stripped. The frame
 * number is zero-based, the same one presentFrames holds at frame end
 * before the increment. pc/boot.c validates that the indices it parses are
 * < the frame budget before the run starts, so a miss (an index the run
 * never reaches) cannot happen here. The caller (hostMultiCaptureCheck)
 * returns early when presentShot is NULL, so this function assumes it.
 *
 * Stripping rules match Python's pathlib.Path.with_suffix(""): the dot
 * separator is the LAST '.' that follows the LAST '/' in the basename;
 * a leading '.' (a dotfile like `.hidden.png`) is part of the stem, not
 * an extension separator. `dir.with.dots/shot.png` strips only `.png`
 * (the dot in the directory name is ignored).
 *
 * The function returns 1 on success, 0 on failure (the buffer cannot
 * hold the result, or `outSize` is invalid). The caller skips the
 * capture when the path cannot be built -- silent truncation would
 * produce a wrong-named file the user could not reconcile with the
 * requested index.
 */
#define HOST_CAPTURE_SUFFIX_MAX 16  /* _f<INT_MIN as decimal>\0 + ".png\0" */
static int hostCapturePath(char *out, s32 outSize, s32 index) {
    s32 srcLen;
    s32 baseLen;
    s32 dirEnd;
    s32 dot;
    s32 k;
    s32 needed;

    if (out == NULL || outSize <= 0) {
        return 0;
    }
    srcLen = 0;
    dirEnd = 0;
    while (presentShot[srcLen] != '\0') {
        if (presentShot[srcLen] == '/') {
            dirEnd = srcLen + 1;
        }
        srcLen++;
    }
    /* Find the LAST '.' at or after dirEnd; that is the extension. A '.' at
       dirEnd itself is part of a dotfile name (the leading dot), not an
       extension separator. */
    dot = -1;
    for (k = dirEnd; k < srcLen; k++) {
        if (presentShot[k] == '.' && k > dirEnd) {
            dot = k;
        }
    }
    baseLen = (dot > dirEnd) ? dot : srcLen;
    /* required: baseLen bytes for the stem + HOST_CAPTURE_SUFFIX_MAX for the
       suffix + 1 for '\0'. snprintf tells us how much it would have written;
       if that exceeds the remaining room, the result would have been
       truncated -- refuse. */
    needed = baseLen + HOST_CAPTURE_SUFFIX_MAX + 1;
    if (needed > outSize) {
        return 0;
    }
    for (srcLen = 0; srcLen < baseLen; srcLen++) {
        out[srcLen] = presentShot[srcLen];
    }
    {
        int written = snprintf(out + baseLen, (size_t)(outSize - baseLen),
                              "_f%d.png", (int)index);
        if (written < 0 || written >= outSize - baseLen) {
            /* snprintf would have produced a wrong-named file: refuse. */
            return 0;
        }
    }
    return 1;
}

/*
 * T30: at frame end, if the just-drawn frame (presentFrames, which has not
 * been incremented yet) is in the multi-capture list, save it as a PNG before
 * PsyX_EndScene swaps the buffer. The list is sorted ascending by
 * pcBootMultiCaptureInit, so one walk finds it (or does not).
 */
static void hostMultiCaptureCheck(void) {
    s32 width = 0;
    s32 height = 0;
    char path[1024];
    s32 lo = 0;
    s32 hi = captureCount;

    if (captureCount == 0 || presentShot == NULL) {
        return;
    }
    while (lo < hi) {
        s32 mid = (lo + hi) / 2;

        if (captureIndices[mid] == presentFrames) {
            if (!hostCapturePath(path, (s32)sizeof(path), presentFrames)) {
                fprintf(stderr,
                        "screenshot: capture path for frame %d does not fit "
                        "in the %u-byte buffer; skipping\n",
                        (int)presentFrames, (unsigned)sizeof(path));
                return;
            }
            PsyX_GetScreenSize(&width, &height);
            pcSaveShot(path, width, height);
            printf("screenshot: %s\n", path);
            return;
        }
        if (captureIndices[mid] < presentFrames) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
}

static void hostEndPresent(void) {
    int width = 0;
    int height = 0;

    PsyX_GetScreenSize(&width, &height);
    if (presentShot != NULL) {
        PsyX_OverlayRun(); /* the shot is read before PsyX_EndScene would draw the host overlay */
        pcSaveShot(presentShot, width, height);
        printf("screenshot: %s\n", presentShot);
    }
    printf("presented %d frames, %d primitives drawn by the game's own mode code\n",
           (int)presentFrames, (int)presentPrims);
    exit(0);
}

int pcBootPresented(void) {
    return presentFrames;
}

/* ---- the country-select art diagnostic (T16, see host.h) ---- */

#define ART_DUMP_REGIONS 4

static s32 artFrame = -1;
static PcArtRegion artRegions[ART_DUMP_REGIONS];
static s32 artRegionCount;

void pcBootArtInit(s32 frame, const PcArtRegion *regions, s32 count) {
    s32 i;

    if (count > ART_DUMP_REGIONS) {
        count = ART_DUMP_REGIONS;
    }
    artFrame = frame;
    artRegionCount = count;
    for (i = 0; i < count; i++) {
        artRegions[i] = regions[i];
    }
}

/* The layer's ordering table as PsyCross's entries (defined below) */
static OT_TAG *hostOt(Layer *layer);

/* FNV-1a of a byte range: the checksum pc/check_font.py already knows */
static u32 artChecksum(const u8 *data, s32 size) {
    u32 hash = 0x811C9DC5u;
    s32 i;

    for (i = 0; i < size; i++) {
        hash ^= data[i];
        hash *= 0x01000193u;
    }
    return hash;
}

/*
 * Reads every armed region back from VRAM with PsyCross's GR_ReadVRAM and
 * prints it with a checksum, then prints the frame's primitives the way
 * DrawOTag walks them: each DR_TPAGE's texture page and each SPRT's UV, size
 * and CLUT. It runs once, on the armed frame -- the country-select code uploads
 * its art during the mode's first frames and reaches its open, sprite-only
 * state shortly after (country_select.c's menu substates), so the frame the
 * check arms has both the art and the drawing to compare.
 */
static void hostArtDump(Layer *layer) {
    u8 *block;
    P_TAG *node;
    s32 i;
    s32 guard;
    s32 lastTpage = -1;

    for (i = 0; i < artRegionCount; i++) {
        s32 bytes = artRegions[i].w * artRegions[i].h * 2;

        block = HEAP.allocZeroed(bytes, MEM_MODE);
        GR_ReadVRAM((unsigned short *)block, artRegions[i].x, artRegions[i].y,
                    artRegions[i].w, artRegions[i].h);
        printf("art vram %d,%d %dx%d (%d bytes) sum=%08x\n",
               artRegions[i].x, artRegions[i].y, artRegions[i].w, artRegions[i].h,
               (int)bytes, (unsigned)artChecksum(block, bytes));
        { /* T32: raw region bytes for the palette/bit15 analysis (build/ is
             gitignored); build/artCheckRegionN.bin per armed region */
            char buf[64];
            FILE *f;
            snprintf(buf, sizeof(buf), "build/artCheckRegion%d.bin", i);
            f = fopen(buf, "wb");
            if (f != NULL) {
                fwrite(block, 1, bytes, f);
                fclose(f);
            }
        }
        HEAP.free(block);
    }

    /* the ABR the PS1 GPU would have active: each textured prim's blend mode
       comes from the last DR_TPAGE before it in the chain (T32) */
    node = (P_TAG *)&hostOt(layer)[layer->otLen - 1];
    for (guard = 0; guard < 4096; guard++) {
        if (node->len > 0) {
            if (node->code == 0xE1) {
                /* the same unpacking pc/font_frame.c does for DR_TPAGE */
                u_int word = ((DR_TPAGE *)node)->code[0];
                s32 tpage = (s32)((word & 0x1FFu) | ((word >> 2) & 0x200u));

                lastTpage = tpage;
                printf("art tpage code=%08x tpage=%d\n", (unsigned)word, (int)tpage);
            } else if ((node->code & 0xF0) == 0x20) {
                /* quads: the talk-box panel is a POLY_FT4 -- print vertices,
                   UVs and brightness the diagnosis needs (T32) */
                POLY_FT4 *poly = (POLY_FT4 *)node;   /* same prefix as F4 */

                printf("art poly code=%02x rgb=%02x,%02x,%02x u0=%d v0=%d "
                       "u3=%d v3=%d clut=%d env=%d at %d,%d %d,%d\n",
                       (int)node->code, (int)poly->r0, (int)poly->g0, (int)poly->b0,
                       (int)poly->u0, (int)poly->v0, (int)poly->u3, (int)poly->v3,
                       (int)poly->clut, lastTpage, (int)poly->x0, (int)poly->y0,
                       (int)poly->x3, (int)poly->y3);
            } else if ((node->code & 0xF0) == 0x60) {
                SPRT *sprt = (SPRT *)node;

                /* the code byte too: bit 1 is the sprite's semi-transparency
                   flag, which the blend diagnosis needs (T32) */
                printf("art sprite code=%02x rgb=%02x,%02x,%02x u=%d v=%d w=%d h=%d clut=%d env=%d at %d,%d\n",
                       (int)node->code, (int)sprt->r0, (int)sprt->g0, (int)sprt->b0,
                       (int)sprt->u0, (int)sprt->v0, (int)sprt->w, (int)sprt->h,
                       (int)sprt->clut, lastTpage, (int)sprt->x0, (int)sprt->y0);
            }
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)node->addr;
    }
}

/* ---- the layer's own methods ---- */

/*
 * Layer.setBgColor: the colour the layer's area is cleared to. The callers
 * pass ints (country_select.c: 0x1F, 0x1F, 0x1F), which is why the field is
 * declared without a prototype; the game's own layerSetBgColor takes them as
 * bytes, the same thing here.
 */
static void hostLayerSetBgColor(Layer *layer, int r, int g, int b) {
    layer->env.r0 = (u8)r;
    layer->env.g0 = (u8)g;
    layer->env.b0 = (u8)b;
    layer->env.isbg = (r | g | b) ? 1 : 0;
}

/* The last DRAWENV handed to the GPU and the layer it came from, and the
   DR_TPAGE counts of the last table prepared (hostOtDrawToDisplay): the T27
   diagnostic reads them (see hostEnvPrint) */
static DRAWENV envApplied;
static s32 envAppliedId;
static s32 envTpageCount;
static s32 envTpageDfe0;
static s32 envTpageDfe1;

/*
 * The DRAWENV a layer draws its ordering table with: the layer's own, with
 * the layer's offset on it. The game's own layerDraw builds exactly this
 * (src/engine/gfx/layer.c:131-137) before its SetDrawEnv.
 */
static void hostLayerEnvForFrame(Layer *layer, DRAWENV *env) {
    *env = layer->env;
    env->ofs[0] = layer->offsetX;
    env->ofs[1] = layer->offsetY;

    /* Hide the map only during its entry banner. frozen is also set while
       FIELDSTG_closeField animates the map layer's own shrinking clip. */
    if (FIELDSTG_state.frozen != 0 && stageBannerHidesMap() &&
        TASK_REGISTRY.funcs.find(FIELD_TASK_BANNER, -1, 0) != NULL &&
        layer == GFX.funcs.getLayer(FIELD_LAYER_MAP)) {
        env->clip.h = 0;
    }
    /* with the Fast door transition the exit wipe keeps the shrinking view over black; Classic draws the game's grid cover */
    if (layer == GFX.funcs.getLayer(FIELD_LAYER_COVER)) {
        Task *field = TASK_REGISTRY.funcs.find(FIELD_TASK_FIELD, -1, -1);

        if (field != NULL && stageExitCoverHidden(field->state == TASK_DONE, FIELDSTG_state.battleStarting, fastDoorMode() == FAST_MODE_FAST)) {
            env->clip.h = 0;
        }
    }
}

/*
 * Sends one layer's DRAWENV to the GPU.
 *
 * The game's layerDraw hands this to the GPU as a DR_ENV primitive at the
 * head of the ordering table. On the host that must go through PsyCross's
 * PutDrawEnv instead, because PsyCross reads a DR_ENV in the table from the
 * tag's length word and onwards: with its extended prim pointers the first
 * DR_AREA word sits at the tag's own length field, so a length of 5 leaks
 * into the clip's x (5, not 0) and the layer would draw clipped by 5 pixels.
 * The environment itself is the game's: the same DRAWENV, the same fields.
 *
 * `envApplied` keeps the last DRAWENV sent this way, and `envAppliedId` the
 * layer it came from, for the T27 diagnostic (see hostEnvPrint).
 */
static void hostLayerApplyEnv(Layer *layer, s32 id) {
    DRAWENV env;

    hostLayerEnvForFrame(layer, &env);
    envApplied = env;
    envAppliedId = id;
    PutDrawEnv(&env);
    PutDispEnv(&GFX.disp[GFX.dispBuffer]);
}

/*
 * The layer's ordering table of the frame being built, as PsyCross's own
 * entries. The game's own layer.c walks it as `u_long *` (`ot[buffer] +
 * depth`), which steps 8 bytes on this host while an OT entry is 12: the
 * table would be addressed through the wrong stride -- the same `u_long` fork
 * T14 found in the TIM loader, one level up. Every access below goes through
 * this.
 */
static OT_TAG *hostOt(Layer *layer) {
    return (OT_TAG *)layer->ot[GFX.buffer];
}

/*
 * The battle's mesh drawer indexes a layer's ordering table in the game's own
 * word size: model_mesh.c takes `layer->getOt(layer)` as a `u_long *` and
 * addresses entries as `otBase + otz` (drawMeshPolygons), the stride the
 * PlayStation's 4-byte OT entry gives. On this host an entry is a 12-byte
 * OT_TAG, so that raw indexing splats one entry over the next every three --
 * it was the hostOtDrawToDisplay SIGSEGV (rax split like two PS1 tag words).
 * The game's unit stays unedited (ADR-003): getOt hands it a MIRROR of the
 * table in its own word size, and the merge at draw time (hostOtMirrorMerge,
 * run from hostDrawFrame before the layer's table is sent) splices each
 * depth's mirrored chain back into the real table.
 *
 * A mirror entry starts at its depth's stub: an OT_TAG chain node whose
 * length is 0 (the walker parses nothing and follows the address), with each
 * stub handing on to the one below and stub 0 to prim_terminator --
 * the same shape ClearOTagR gives the real table. The battle's addPrim then
 * links at full-pointer stride 8 over 8-byte mirrors, exactly as
 * libgpu.h's macros write them, and prints nothing where it linked none.
 */
typedef struct HostOtMirror {
    Layer *layer;      /* the layer the tables belong to, NULL in a free slot */
    u_long *mirror[2]; /* the tables as the battle sees them, per buffer */
    OT_TAG *stubs[2];  /* the mirrors' initial chains, per buffer */
} HostOtMirror;

static HostOtMirror hostOtMirrors[LAYER_COUNT];

static HostOtMirror *hostOtMirrorFind(Layer *layer) {
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (hostOtMirrors[i].layer == layer) {
            return &hostOtMirrors[i];
        }
    }
    return NULL;
}

/* (Re)links one buffer's mirror to its stubs, as ClearOTagR links the real
   table: entry d hands on to the node below it, entry 0 to the terminator */
static void hostOtMirrorInit(HostOtMirror *m, s32 buffer) {
    s32 d;

    for (d = 0; d < m->layer->otLen; d++) {
        m->stubs[buffer][d].addr =
            (d == 0) ? (uintptr_t)&prim_terminator
                     : (uintptr_t)&m->stubs[buffer][d - 1];
        m->stubs[buffer][d].len = 0;
        m->stubs[buffer][d].pgxp_index = 0;
        m->mirror[buffer][d] = (u_long)&m->stubs[buffer][d];
    }
}

/* The mirror tables, beside the real ones (hostNewLayer) */
static void hostOtMirrorRegister(Layer *layer) {
    HostOtMirror *m;
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (hostOtMirrors[i].layer == NULL) {
            break;
        }
    }
    if (i == LAYER_COUNT) {
        fprintf(stderr, "gfx: no free OT mirror slot\n");
        return;
    }
    m = &hostOtMirrors[i];
    m->layer = layer;
    /* host-only tables: off the game's heap, which is a PSX-sized pool the
       battle fills for its own models -- ~192KiB of mirror per layer broke
       that budget (the camera's coordinate chain then read strings) */
    m->mirror[0] = (u_long *)calloc(layer->otLen, sizeof(u_long));
    m->mirror[1] = (u_long *)calloc(layer->otLen, sizeof(u_long));
    m->stubs[0] = (OT_TAG *)calloc(layer->otLen, sizeof(OT_TAG));
    m->stubs[1] = (OT_TAG *)calloc(layer->otLen, sizeof(OT_TAG));
    hostOtMirrorInit(m, 0);
    hostOtMirrorInit(m, 1);
}

/* Layer.getOt: the mirrored table when the layer carries one (the battle's
   mesh drawer), else the real table */
static u_long *hostLayerGetOt(Layer *layer) {
    HostOtMirror *m = hostOtMirrorFind(layer);

    if (m != NULL) {
        return m->mirror[GFX.buffer];
    }
    return layer->ot[GFX.buffer];
}

/* Layer.getOtEntry: the ordering table entry at `depth` */
static u_long *hostLayerGetOtEntry(Layer *layer, s32 depth) {
    return (u_long *)&hostOt(layer)[depth];
}

/* Layer.getOtEntryZ: the entry for a 16-bit Z, scaled to the table's length */
static u_long *hostLayerGetOtEntryZ(Layer *layer, s32 z) {
    return (u_long *)&hostOt(layer)[z >> (16 - layer->otShift)];
}

/* Layer.getOtShift */
static s32 hostLayerGetOtShift(Layer *layer) {
    return layer->otShift;
}

/* Layer.setClipPos */
static void hostLayerSetClipPos(Layer *layer, int x, int y) {
    layer->env.clip.x = (s16)x;
    layer->env.clip.y = (s16)y;
}

/* Layer.setClipSize */
static void hostLayerSetClipSize(Layer *layer, int w, int h) {
    layer->env.clip.w = (u16)w;
    layer->env.clip.h = (u16)h;
}

/* Layer.setOffset */
static void hostLayerSetOffset(Layer *layer, int x, int y) {
    layer->offsetX = (s16)x;
    layer->offsetY = (s16)y;
}

/* Layer.getScroll: the scroll position in pixels */
static void hostLayerGetScroll(Layer *layer, Vec2 *out) {
    out->x = layer->scrollX >> 8;
    out->y = layer->scrollY >> 8;
}

/* Layer.setScroll: the scroll position in 8.8 fixed point */
static void hostLayerSetScroll(Layer *layer, s32 x, s32 y) {
    layer->scrollX = x;
    layer->scrollY = y;
}

/* Layer.addScroll */
static void hostLayerAddScroll(Layer *layer, s32 dx, s32 dy) {
    layer->scrollX += dx;
    layer->scrollY += dy;
}

/* Layer.getViewRect: the part of the scrolled world the clip shows */
static void hostLayerGetViewRect(Layer *layer, Rect16 *rect) {
    rect->x = layer->env.clip.x - layer->offsetX + (layer->scrollX >> 8);
    rect->y = layer->env.clip.y - layer->offsetY + (layer->scrollY >> 8);
    rect->w = layer->env.clip.w;
    rect->h = layer->env.clip.h;
}

/* Layer.clearOt: the table the frame's primitives were added to */
static void hostLayerClearOt(Layer *layer) {
    HostOtMirror *m = hostOtMirrorFind(layer);

    ClearOTagR(layer->ot[GFX.buffer], layer->otLen);
    if (m != NULL) {
        hostOtMirrorInit(m, GFX.buffer);
    }
}

/*
 * Splices the frame's mirrored chains into the real table for `layer`. A
 * mirror entry that still sits at its stub linked no prim; otherwise it is
 * the head of the battle's chain for that depth, whose terminal node still
 * hands on to the depth's stub. That terminal takes the real entry's current
 * link (the engine's chain for the depth, or the next slot down) and the
 * real entry takes the battle's head: the engine's prims stay beneath the
 * battle's at that depth, and the walk then continues down the untouched
 * real table to prim_terminator.
 *
 * A head that is not a prim of the packet buffer cannot be walked (the
 * wireframe drawer encodes `word & 0xFFFFFF` links instead -- digimon_change's
 * wipes); it is skipped, as the mirror's real-table entries on the PS1 encode.
 */
static void hostOtMirrorMerge(Layer *layer) {
    HostOtMirror *m = hostOtMirrorFind(layer);
    OT_TAG *real;
    u8 *buf;
    u8 *bufEnd;
    u_long *mirror;
    s32 d;

    if (m == NULL) {
        return;
    }
    real = hostOt(layer);
    buf = (u8 *)GFX.primBufs[GFX.buffer];
    bufEnd = buf + GFX.primBufSize;
    mirror = m->mirror[GFX.buffer];
    for (d = 0; d < layer->otLen; d++) {
        u8 *head = (u8 *)mirror[d];
        s32 guard;

        if (head != (u8 *)&m->stubs[GFX.buffer][d] &&
            head >= buf && head < bufEnd) {
            P_TAG *node = (P_TAG *)head;

            for (guard = 0; guard < GFX.primBufSize; guard++) {
                u8 *next = (u8 *)node->addr;

                if (next < buf || next >= bufEnd) {
                    break;
                }
                node = (P_TAG *)next;
            }
            node->addr = real[d].addr;
            real[d].addr = (uintptr_t)head;
        }
    }
}

/*
 * T27: the game's own DR_TPAGEs carry dfe 0 -- SetDrawTPage(p, 0, 1, tpage),
 * e.g. src/engine/gfx/sprite_drawer.c:146. dfe is bit 10 of the tpage word,
 * "drawing to the display area" (0 = prohibited); the game sets it 0 because
 * on the PlayStation it draws its frames to the back buffer -- the two
 * display areas at y and y + 256, display.c:188-191, chosen by the
 * DRAWENV/DISPENV offsets and the flip, not by this bit -- and then flips.
 * PsyCross sends a split whose env has dfe 0 to its offscreen surface (a VRAM
 * back buffer, GR_SetOffscreenState) and only a dfe-1 split to the window.
 * This host draws ONE buffer, the window (see the file comment), so a prim
 * left on the offscreen surface is never presented: measured, the field's map
 * (all of whose prims follow DR_TPAGEs) was drawn there while the window kept
 * the clear and the few prims with no DR_TPAGE in front of them, and the
 * frame's capture read whichever surface the last split of the frame had left
 * bound.
 *
 * The host therefore sets the dfe bit of every DR_TPAGE word in the table it
 * is about to send, so every split draws to the window: one surface, which is
 * what the port's single buffer means. The words are the game's own packets
 * in the host's prim buffer, rebuilt every frame; nothing else in them
 * changes (the tpage, dtd and tw bits stay as the game set them). The counts
 * feed the T27 diagnostic (hostEnvPrintTpages): the table's DR_TPAGEs, how
 * many carried dfe 0 before this pass (the game's own words), and how many
 * carry dfe 1 after it (the words are read back after the pass, so a missing
 * flip shows as dfe1 0 -- the check asserts what the host really sent, not
 * its intent).
 *
 * The walk's bound is the table's own length plus the frame's packet buffer:
 * every node in the chain is one of the table's otLen entries or a primitive
 * in the packet buffer (getPrim hands out only those; a prim is at least one
 * byte), so otLen + primBufSize nodes is a complete bound. A fixed bound
 * could truncate the walk -- an otShift 0xC table has 4096 entries plus its
 * primitives -- and leave the tail's DR_TPAGEs on the offscreen surface. No
 * measured mode carries DR_TPAGEs on such a table; the bound is the table's
 * own, not a tuned number. (The one static-prim addPrim in the tree is battle
 * mode's cursor bar, src/battle/battle_mode/cursor.c:38-40, which this binary
 * does not link; if battle mode is ever linked, re-check the premise -- the
 * guard still bounds the walk either way.)
 */
static void hostOtDrawToDisplay(Layer *layer) {
    P_TAG *node = (P_TAG *)&hostOt(layer)[layer->otLen - 1];
    s32 guard;

    envTpageCount = 0;
    envTpageDfe0 = 0;
    envTpageDfe1 = 0;
    for (guard = 0; guard < layer->otLen + GFX.primBufSize; guard++) {
        if (node->len > 0 && node->code == 0xE1) {
            u_int *word = (u_int *)&node->pad0;

            envTpageCount++;
            if ((*word & 0x400) == 0) {
                envTpageDfe0++;
                *word |= 0x400;
            }
            if ((*word & 0x400) != 0) {
                envTpageDfe1++;
            }
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)node->addr;
    }
}

/*
 * Layer.draw: sends the frame's ordering table. The game's layerDraw also
 * adds the layer's DR_ENV at the head of the table; here the environment is
 * applied with PutDrawEnv immediately before this call (see
 * hostLayerApplyEnv for why and hostDrawFrame for where), and the table's
 * DR_TPAGEs have been set to draw to the display (hostOtDrawToDisplay), so
 * this only walks the table the game's own sprite drawer filled.
 */
static void hostLayerDraw(Layer *layer) {
    DrawOTag((u_long *)&hostOt(layer)[layer->otLen - 1]);
}

/* Layer.free: the tables, once the GPU is done with them */
static void hostLayerFree(Layer *layer) {
    HostOtMirror *m = hostOtMirrorFind(layer);

    DrawSync(0);
    if (m != NULL) {
        free(m->mirror[0]);
        free(m->mirror[1]);
        free(m->stubs[0]);
        free(m->stubs[1]);
        m->layer = NULL;
    }
    HEAP.free(layer->ot[0]);
    HEAP.free(layer->ot[1]);
    if (layer->callbackCap != 0) {
        HEAP.free(layer->callbacks);
    }
}

/*
 * Draw callbacks: the layer's list of functions to call when it is drawn.
 *
 * The node is the host's own, not the game's DrawCallback. The game's
 * DrawCallback.arg is `s32` -- on the PlayStation a pointer fits in 32 bits
 * (include/engine/gfx.h:120) -- so a pointer registered through it truncates
 * to its low 32 bits here, and every registration site of this path registers
 * a pointer: the field's map objects (map_objects.c:123), actors (actor.c:69)
 * and hidden spots (hidden_spots.c:279), the stage sprites and effects
 * (update_stage_effect.inc.c, update_sprite_pair.inc.c, the stage files) and,
 * in battle, the meshes and sprite anims. Every callback dereferences that
 * first argument; the third (`param`) is always a small index. The node below
 * widens `arg` to `void *` so the register value arrives whole, keeps `param`
 * s32, and is what `layer->callbacks` points at.
 *
 * Only the host reads these nodes: the game's own layer.c and display.c --
 * the units this file replaces -- are the only game code that traverses or
 * allocates them (grep "DrawCallback": src/engine/gfx/layer.c and
 * include/engine/gfx.h only; grep "callbacks" also hits display.c:47 and
 * unrelated CD-callback names in src/engine/file/; every other caller reaches
 * the list through layer->addSortedCallback/addCallback/allocCallbacks, the
 * function table). So the host owns the layout; `layer->callbacks` is a
 * DrawCallback * in the header and is only ever cast back here.
 *
 * T26: the list's ORDER is the game's too. addSortedCallback inserts by
 * priority exactly as layer.c:280-320 does (the sentinel head included), and
 * addCallback appends exactly as layer.c:323-338 does; see both below.
 */
typedef struct HostDrawCallback {
    s32 priority;
    s32 param;
    void (*func)();
    void *arg; /* the full pointer the game registers (the game's field is s32) */
    struct HostDrawCallback *next;
} HostDrawCallback;

static HostDrawCallback *hostCallbacks(Layer *layer) {
    return (HostDrawCallback *)layer->callbacks;
}

static void hostLayerResetCallbacks(Layer *layer) {
    HostDrawCallback *cb = hostCallbacks(layer);

    cb->priority = 0x7FFFFFFF;
    cb->param = 0;
    cb->func = NULL;
    cb->arg = NULL;
    cb->next = NULL;
    layer->callbackCount = 1;
}

static void hostLayerAllocCallbacks(Layer *layer, s32 count) {
    layer->callbacks = HEAP.alloc(count * sizeof(HostDrawCallback), MEM_MODE);
    layer->callbackCap = count;
    hostLayerResetCallbacks(layer);
}

/*
 * Layer.addSortedCallback: the game's own insert, by priority, highest first.
 * This is a port of src/engine/gfx/layer.c:280-320, which is the contract
 * (T26), not an insertion sort of the whole list: the list is the layer's node
 * array linked through `next`, `callbacks[0]` is the sentinel head (priority
 * 0x7FFFFFFF) and it takes part in the walk as a normal node, and the walk
 * stops at the first node whose priority is STRICTLY smaller than the new one
 * and inserts before it -- so a new entry goes after the entries it ties
 * with, and equal priorities draw in registration order.
 */
static void hostLayerAddSortedCallback(Layer *layer, void (*func)(), void *arg, s32 priority, s32 param) {
    HostDrawCallback *cur;
    HostDrawCallback *prev;
    HostDrawCallback *e;
    s32 i;
    s32 cap;

    if (layer->callbackCount < layer->callbackCap) {
        cur = hostCallbacks(layer);
        e = &cur[layer->callbackCount];
        e->func = func;
        e->arg = arg;
        e->priority = priority;
        e->param = param;
        prev = NULL;
        if (layer->callbackCount != 0) {
            cap = layer->callbackCap;
            for (i = 0; i < cap; i++) {
                if (cur->priority < priority) {
                    cur = prev;
                    break;
                }
                prev = cur;
                if (cur->next == NULL) {
                    break;
                }
                cur = cur->next;
            }
            if (cur->next == NULL) {
                cur->next = e;
                e->next = NULL;
            } else {
                e->next = cur->next;
                cur->next = e;
            }
        } else {
            e->next = NULL;
        }
        layer->callbackCount++;
    }
}

/*
 * Layer.addCallback: the game's own unsorted append (layer.c:323-338). It
 * takes the array's next slot with priority 0 and links it from the PREVIOUS
 * ARRAY SLOT (`cb[-1].next = cb`), which is the list's tail only while the
 * layer was registered in append order; a layer that mixed the two calls gets
 * exactly what the game's own code does with them, which is the point of the
 * mirror.
 */
static void hostLayerAddCallback(Layer *layer, void (*func)(), void *arg) {    HostDrawCallback *cb;

    if (layer->callbackCount < layer->callbackCap) {
        cb = &hostCallbacks(layer)[layer->callbackCount];
        cb->func = func;
        cb->arg = arg;
        cb->priority = 0;
        cb->param = 0;
        cb->next = NULL;
        if (layer->callbackCount != 0) {
            cb[-1].next = cb;
        }
        layer->callbackCount++;
    }
}

static s32 hostLayerRunLayerId(Layer *layer) {
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] == layer) {
            return GFX.layerIds[i];
        }
    }
    return -1;
}

static void hostLayerRunCallbacks(Layer *layer) {
    HostDrawCallback *cb;

    if (layer->callbackCount) {
        cb = hostCallbacks(layer);
        if (viewDumpArmed(hostLayerRunLayerId(layer))) {
            HostDrawCallback *meshCb;
            s32 i;

            for (i = 0, meshCb = hostCallbacks(layer);
                 i < layer->callbackCount; i++, meshCb++) {
                if (meshCb->arg == NULL) {
                    continue;
                }
                /* the queued argument is the game's Mesh (the struct this
                   shim compiles too, and its MATRIX's host offset): print
                   the matrix's translation, the mesh's world position */
                MATRIX *matrix = (MATRIX *)((char *)meshCb->arg
                                            + meshMatrixOff);

                printf("vdump %d layer 0x%X mesh 0x%08X t=(%d,%d,%d)\n",
                       (int)presentFrames, (int)hostLayerRunLayerId(layer),
                       (unsigned)(uintptr_t)meshCb->arg,
                       (int)matrix->t[0], (int)matrix->t[1], (int)matrix->t[2]);
                /* T5: the projection check for the same mesh, the exact
                   path the game's own draw takes: compose the world-screen
                   matrix with the mesh's local one, then the emulated
                   GTE's RTPS with the live screen registers (C2_H here,
                   C2_OFX/OFY the centre the game's single SetGeomOffset(0,
                   0) left). The mesh origin's screen point predicts where
                   the mesh's vertices cluster; the OT dump (DW3_OT_DUMP,
                   the same presented frame) walks what was really drawn,
                   its coordinate halves the same 12.4 fixed >> 12. */
                {
                    MATRIX composed;
                    VECTOR zero;
                    long sxy = 0;
                    long otz = 0;

                    gte_CompMatrix(&GsWSMATRIX, matrix, &composed);
                    gte_SetRotMatrix(&composed);
                    gte_SetTransMatrix(&composed);
                    zero.vx = 0;
                    zero.vy = 0;
                    zero.vz = 0;
                    gte_ldv0(&zero);
                    gte_rtps();
                    gte_stsxy(&sxy);
                    gte_stszotz(&otz);
                    printf("vpred %d layer 0x%X mesh 0x%08X "
                           "ct=(%d,%d,%d) org=(%d,%d) z=%d\n",
                           (int)presentFrames, (int)hostLayerRunLayerId(layer),
                           (unsigned)(uintptr_t)meshCb->arg, (int)composed.t[0],
                           (int)composed.t[1], (int)composed.t[2],
                           (short)sxy, (short)(sxy >> 16), (int)otz);
                }
            }
        }
        do {
            if (cb->func != NULL) {
                /* the game's callbacks are void (*)(pointer, layer, index):
                   call through that shape so the pointer is passed whole */
                ((void (*)(void *, void *, s32))cb->func)(cb->arg, layer, cb->param);
            }
            cb = cb->next;
        } while (cb != NULL);
        hostLayerResetCallbacks(layer);
    }
}

/*
 * T26: the callback-order diagnostic. The layer's list is rebuilt every frame
 * (runCallbacks resets it after running it), so one frame's list is the whole
 * comparison. The list's order must be the one the game's own
 * layerAddSortedCallback (src/engine/gfx/layer.c:280-320) produces for that
 * frame's registration sequence -- a descending-priority insert that puts a
 * new entry after the entries it ties with, with the sentinel head (priority
 * 0x7FFFFFFF) taking part as a normal node. DW3_CB_DUMP=<frame> prints, on
 * that frame, the registration order (the nodes' array, index 1 = the first
 * registration) and the list order runCallbacks walks, for every layer with a
 * callback array; pc/check_field.py runs the game's rule over the
 * registrations and requires the two to agree. Unset (the default), the run
 * prints nothing extra.
 *
 * The field's own frame is the in-vivo measurement, but its registration
 * sequence (frame 600: priorities 152, 176, 231) has no two equal priorities,
 * so the tie rule would go unmeasured; the synthetic probe below runs a fixed
 * sequence -- with ties and a registration at the sentinel's own priority --
 * through the SAME insert function and dumps its list too, so the check
 * measures the tie rule and the sentinel's part in the comparison as well.
 * The probe never runs a callback: it only inserts.
 */
static s32 cbDumpFrame = -1;
static s32 cbDumpRead;

/* The armed frame, read from the environment once; -1 when unset */
static s32 hostCallbackArmedFrame(void) {
    if (!cbDumpRead) {
        const char *env = getenv("DW3_CB_DUMP");

        cbDumpRead = 1;
        cbDumpFrame = -1;
        if (env != NULL) {
            s32 value = 0;

            while (*env >= '0' && *env <= '9') {
                value = value * 10 + (*env - '0');
                env++;
            }
            cbDumpFrame = value;
        }
    }
    return cbDumpFrame;
}

/* One list: the registrations in the array, then the order `next` walks */
static void hostCallbackDumpList(s32 id, const char *name, HostDrawCallback *nodes,
                                 s32 cap, s32 count) {
    HostDrawCallback *cb;
    s32 i;

    if (name != NULL) {
        printf("cb %d %s head cap=%d count=%d\n", (int)presentFrames, name, (int)cap, (int)count);
    } else {
        printf("cb %d 0x%X head cap=%d count=%d\n", (int)presentFrames, (int)id, (int)cap, (int)count);
    }
    for (i = 1; i < count; i++) {
        cb = &nodes[i];
        if (name != NULL) {
            printf("cb %d %s reg %d prio=%d func=%p param=%d\n", (int)presentFrames, name,
                   (int)(i - 1), (int)cb->priority, (void *)cb->func, (int)cb->param);
        } else {
            printf("cb %d 0x%X reg %d prio=%d func=%p param=%d\n", (int)presentFrames, (int)id,
                   (int)(i - 1), (int)cb->priority, (void *)cb->func, (int)cb->param);
        }
    }
    i = 0;
    for (cb = nodes; cb != NULL; cb = cb->next) {
        if (name != NULL) {
            printf("cb %d %s list %d prio=%d func=%p param=%d\n", (int)presentFrames, name,
                   (int)i, (int)cb->priority, (void *)cb->func, (int)cb->param);
        } else {
            printf("cb %d 0x%X list %d prio=%d func=%p param=%d\n", (int)presentFrames, (int)id,
                   (int)i, (int)cb->priority, (void *)cb->func, (int)cb->param);
        }
        i++;
    }
}

/* The fixed probe sequence: ties (three 9s), the sentinel's own priority
   (0x7FFFFFFF) and an ascending run, so the insert has to reverse it */
static const s32 cbProbePriorities[] = {7, 9, 9, 3, 0x7FFFFFFF, 9, 1};

static void hostCallbackDumpFrame(void) {
    Layer probe;
    HostDrawCallback nodes[1 + sizeof(cbProbePriorities) / sizeof(cbProbePriorities[0])];
    s32 i;

    if (hostCallbackArmedFrame() != presentFrames) {
        return;
    }

    probe.callbacks = (DrawCallback *)nodes;
    probe.callbackCap = (s32)(sizeof(nodes) / sizeof(nodes[0]));
    probe.callbackCount = 0;
    hostLayerResetCallbacks(&probe);
    for (i = 0; i < (s32)(sizeof(cbProbePriorities) / sizeof(cbProbePriorities[0])); i++) {
        hostLayerAddSortedCallback(&probe, (void (*)())hostLayerDraw, NULL, cbProbePriorities[i], i);
    }
    hostCallbackDumpList(0, "probe", nodes, probe.callbackCap, probe.callbackCount);

    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] != NULL && GFX.layers[i]->callbackCap != 0) {
            hostCallbackDumpList(GFX.layerIds[i], NULL, hostCallbacks(GFX.layers[i]),
                                 GFX.layers[i]->callbackCap, GFX.layers[i]->callbackCount);
        }
    }
}

/*
 * T27: the per-layer DRAWENV diagnostic. On the PlayStation each layer's
 * DRAWENV travels inside its own ordering table (layerDraw's DR_ENV, see
 * src/engine/gfx/layer.c:130-151), so each layer's clip and background reach
 * the GPU with that layer's own table. The host sends the env through
 * PsyCross's PutDrawEnv instead, which memcpys it into ONE global
 * (activeDrawEnv, external/psycross/src/psx/LIBGPU.C:311-315) that each OT
 * split records at parse time -- so which env a layer's table is drawn with
 * depends on when PutDrawEnv ran. DW3_ENV_DUMP=<frame> prints, on that frame;
 * DW3_ENV_DUMP2 may arm one additional frame:
 *
 *   env <frame> <id> own  clip=... ofs=... bg=... isbg=... ot=<entries>
 *   env <frame> <id> ot   clip=... ofs=... bg=... isbg=...
 *   env <frame> <id> tpage total=<n> dfe0=<n> dfe1=<n>
 *   env <frame> clear <id> clip=... ofs=... bg=... isbg=...
 *
 * the layer's own DRAWENV with the layer's offset and OT length, the DRAWENV
 * actually in effect when that layer's table was sent (the last one
 * PutDrawEnv received), the DR_TPAGEs the table carries -- how many of them
 * carried dfe 0 before the host's pass (the game's own words) and how many
 * carry dfe 1 after it, the words as sent (hostOtDrawToDisplay) -- and the
 * env the frame's clear (PsyX_BeginScene's GR_Clear) will use. Unset (the
 * default), the run prints nothing extra. pc/check_field.py reads the dump;
 * pc/README.md, "El entorno de dibujo por capa (T27)", has the measurement.
 */
static s32 envDumpFrame = -1;
static s32 envDumpRead;
static s32 envDumpFrame2 = -1;
static s32 envDumpRead2;


/*
 * T5: DW3_VIEW_DUMP=<frame> arms one frame; on it hostLayerRunCallbacks
 * prints each 3D layer's (0x1002 stage, 0x1004 models) queued mesh argument's
 * world matrix translation -- where the game actually stands the meshes when
 * the camera's view is applied -- and pc/shim/host_gs.c prints the view
 * GsSetRefView2 receives and the GsWSMATRIX it computes. Unset, nothing
 * prints.
 */
static s32 viewDumpFrame = -1;
static s32 viewDumpHi = -1;
static s32 viewDumpRead;
/* The host offset of Mesh.matrix (T5 DW3_VIEW_DUMP): the struct comes from
   the game's own header, and on the host its pointer members are 8 bytes,
   so the offset differs from the PSX layout comment. */
static void viewDumpInitOffset(void) {
#include "battle/battle_mode/model.h"
    meshMatrixOff = offsetof(Mesh, matrix);
}


/* Whether a mesh print is armed on this frame for the layer's id: the same
   "<lo>-<hi>" range GsSetRefView2's dump takes (shim/host_gs.c). */
static s32 viewDumpArmed(s32 id) {
    if (!viewDumpRead) {
        viewDumpInitOffset();
        const char *env = getenv("DW3_VIEW_DUMP");
        s32 value = 0;

        viewDumpRead = 1;
        if (env != NULL) {
            while (*env >= '0' && *env <= '9') {
                value = value * 10 + (*env - '0');
                env++;
            }
            viewDumpFrame = value;
            viewDumpHi = value;
            if (*env == '-') {
                env++;
                value = 0;
                while (*env >= '0' && *env <= '9') {
                    value = value * 10 + (*env - '0');
                    env++;
                }
                viewDumpHi = value;
            }
        } else {
            viewDumpFrame = 0x7FFFFFFF;
            viewDumpHi = -1;
        }
    }
    if (presentFrames < viewDumpFrame || presentFrames > viewDumpHi) {
        return 0;
    }
    return id == 0x1002 || id == 0x1004;
}


/* Read an optional frame number once; -1 when unset. */
static s32 hostEnvFrame(const char *name, s32 *read, s32 *frame) {
    if (!*read) {
        const char *env = getenv(name);

        *read = 1;
        *frame = -1;
        if (env != NULL) {
            s32 value = 0;

            while (*env >= '0' && *env <= '9') {
                value = value * 10 + (*env - '0');
                env++;
            }
            *frame = value;
        }
    }
    return *frame;
}

static s32 hostEnvArmedFrame(void) {
    return hostEnvFrame("DW3_ENV_DUMP", &envDumpRead, &envDumpFrame);
}

static s32 hostEnvArmedFrame2(void) {
    return hostEnvFrame("DW3_ENV_DUMP2", &envDumpRead2, &envDumpFrame2);
}

/* host.h's declarations for the shim's other units (host_gs.c's DW3_VIEW_DUMP) */
void hostEnvFrameParse(const char *name, s32 *read, s32 *frame) {
    hostEnvFrame(name, read, frame);
}

s32 hostPresentFrame(void) {
    return presentFrames;
}

static s32 hostEnvDumpActive(void) {
    return hostEnvArmedFrame() == presentFrames ||
           hostEnvArmedFrame2() == presentFrames;
}

/* One DRAWENV line; otLen < 0 leaves the OT length off */
static void hostEnvPrint(const char *what, s32 id, const DRAWENV *env, s32 otLen) {
    printf("env %d 0x%X %s clip=%d,%d %dx%d ofs=%d,%d bg=%d,%d,%d isbg=%d",
           (int)presentFrames, (int)id, what,
           (int)env->clip.x, (int)env->clip.y, (int)env->clip.w, (int)env->clip.h,
           (int)env->ofs[0], (int)env->ofs[1],
           (int)env->r0, (int)env->g0, (int)env->b0, (int)env->isbg);
    if (otLen >= 0) {
        printf(" ot=%d", (int)otLen);
    }
    printf("\n");
}

/* The DR_TPAGE line of a layer's table (hostOtDrawToDisplay's counts) */
static void hostEnvPrintTpages(s32 id) {
    printf("env %d 0x%X tpage total=%d dfe0=%d dfe1=%d\n",
           (int)presentFrames, (int)id, (int)envTpageCount, (int)envTpageDfe0,
           (int)envTpageDfe1);
}

/*
 * The keep-view state the battle's camera and lights tasks use (retail
 * layer.c:357-378): setKeepView snapshots GsWSMATRIX / GsLIGHTWSMATRIX for
 * the drawing buffer while the running task says so (battle_camera.c keeps
 * the tween's projection too), and drawFrame's per-layer loadView/loadLight
 * restores them just before that layer's own callbacks run (the mesh draw
 * callbacks read GsWSMATRIX/GsLIGHTWSMATRIX directly, model_mesh.c:517/835)
 * -- by then the task updates that came after the snapshot have moved the
 * globals away from the frame this layer draws.
 *
 * The projection the kept view carries is the GTE's screen distance: on the
 * host the divide every RTPS does reads the GTE's own C2_H register, and the
 * only routine in the whole game that sends one is libgs's GsSetProjection
 * (layerLoadView, retail layer.c:366) -- so the shim's GsSetProjection
 * (shim/host_gs.c) lands it on SetGeomScreen.
 */
extern MATRIX GsWSMATRIX;
extern MATRIX GsLIGHTWSMATRIX;
extern void GsSetProjection(long h);

static void hostLayerSetKeepView(Layer *layer, s32 enable, s32 projection) {
    layer->keepView = enable;
    if (enable) {
        layer->projection = projection;
        layer->view[GFX.buffer] = GsWSMATRIX;
    }
}

static void hostLayerLoadView(Layer *layer) {
    GsSetProjection(layer->projection);
    GsWSMATRIX = layer->view[GFX.buffer];
}

static void hostLayerSetKeepLightMatrix(Layer *layer, s32 enable) {
    layer->keepLightMatrix = enable;
    if (enable) {
        layer->lightMatrix[GFX.buffer] = GsLIGHTWSMATRIX;
    }
}

static void hostLayerLoadLightMatrix(Layer *layer) {
    GsLIGHTWSMATRIX = layer->lightMatrix[GFX.buffer];
}

/* ---- the layers GFX holds ---- */

/* The layer with id `id`, or NULL */
Layer *getLayer(s32 id) {
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] != NULL && GFX.layerIds[i] == id) {
            return GFX.layers[i];
        }
    }
    return NULL;
}

/* The slot of the layer with this id; id 0 finds a free slot */
s32 findLayerSlot(s32 id) {
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (id != 0) {
            if (GFX.layers[i] != NULL && GFX.layerIds[i] == id) {
                return i;
            }
        } else if (GFX.layers[i] == NULL) {
            return i;
        }
    }
    return -1;
}

/* Closes the gap of a removed layer in the draw order */
void removeLayerSlot(s32 index) {
    for (; index < LAYER_COUNT - 1; index++) {
        GFX.layers[index] = GFX.layers[index + 1];
        GFX.layerIds[index] = GFX.layerIds[index + 1];
    }
    GFX.layers[LAYER_COUNT - 1] = NULL;
}

/* Puts a layer at position `index` of the draw order, moving the later ones back */
void insertLayerSlot(s32 index, Layer *layer, s32 id) {
    s32 i;

    for (i = LAYER_COUNT - 1; i != index; i--) {
        GFX.layers[i] = GFX.layers[i - 1];
        GFX.layerIds[i] = GFX.layerIds[i - 1];
    }
    GFX.layers[index] = layer;
    GFX.layerIds[index] = id;
}

/* A layer's two ordering tables, as PsyCross's own entries (see the file
   comment: this is what the game's own newLayer sizes wrongly on the host) */
static Layer *hostNewLayer(DRAWENV *env, s32 otShift) {
    Layer *layer = HEAP.allocZeroed(sizeof(Layer), MEM_MODE);

    layer->env = *env;
    layer->otShift = otShift;
    layer->otLen = OT_LENGTHS[otShift - 1];
    layer->ot[0] = (u_long *)HEAP.alloc(layer->otLen * sizeof(OT_TAG), MEM_MODE);
    layer->ot[1] = (u_long *)HEAP.alloc(layer->otLen * sizeof(OT_TAG), MEM_MODE);
    ClearOTagR(layer->ot[0], layer->otLen);
    ClearOTagR(layer->ot[1], layer->otLen);
    hostOtMirrorRegister(layer);
    layer->setBgColor = hostLayerSetBgColor;
    layer->draw = hostLayerDraw;
    layer->clearOt = hostLayerClearOt;
    layer->getOtEntry = hostLayerGetOtEntry;
    layer->getOtEntryZ = hostLayerGetOtEntryZ;
    layer->free = hostLayerFree;
    layer->setClipPos = hostLayerSetClipPos;
    layer->setClipSize = hostLayerSetClipSize;
    layer->setOffset = hostLayerSetOffset;
    layer->setScroll = hostLayerSetScroll;
    layer->addScroll = hostLayerAddScroll;
    layer->getScroll = hostLayerGetScroll;
    layer->getViewRect = hostLayerGetViewRect;
    layer->getOt = hostLayerGetOt;
    layer->getOtShift = hostLayerGetOtShift;
    layer->allocCallbacks = hostLayerAllocCallbacks;
    layer->addSortedCallback = hostLayerAddSortedCallback;
    layer->addCallback = hostLayerAddCallback;
    layer->runCallbacks = hostLayerRunCallbacks;
    layer->setKeepView = hostLayerSetKeepView;
    layer->loadView = hostLayerLoadView;
    layer->setKeepLightMatrix = hostLayerSetKeepLightMatrix;
    layer->loadLightMatrix = hostLayerLoadLightMatrix;
    return layer;
}

/* A new layer drawing to `rect`, with an OT of OT_LENGTHS[otShift - 1] entries */
Layer *createLayer(RECT *rect, s32 otShift, s32 id) {
    DRAWENV env;
    s32 index = findLayerSlot(0);

    if (index != -1) {
        SetDefDrawEnv(&env, rect->x, rect->y, rect->w, rect->h);
        GFX.layerIds[index] = id;
        GFX.layers[index] = hostNewLayer(&env, otShift);
        PLOG("gfx", PLOG_TRACE, "gfx: layer id 0x%X, %dx%d at %d,%d, OT %d entries\n",
             (int)id, (int)rect->w, (int)rect->h, (int)rect->x, (int)rect->y,
             (int)OT_LENGTHS[otShift - 1]);
        return GFX.layers[index];
    }
    fprintf(stderr, "gfx: no free layer slot for id 0x%X\n", (int)id);
    return NULL;
}

/* Frees the layer with id `id`; 0 if there is none */
s32 destroyLayer(s32 id) {
    s32 index = findLayerSlot(id);

    if (index != -1) {
        GFX.layers[index]->free(GFX.layers[index]);
        HEAP.free(GFX.layers[index]);
        removeLayerSlot(index);
        return 1;
    }
    return 0;
}

/* Moves a layer to the position of another one (plus delta) in the draw order */
void moveLayer(s32 id, s32 targetId, s32 delta) {
    s32 from = findLayerSlot(id);
    s32 to = findLayerSlot(targetId);
    s32 pos;
    Layer *layer;
    s32 layerId;

    if (from != -1 && to != -1) {
        pos = to + delta;
        layer = GFX.layers[from];
        layerId = GFX.layerIds[from];
        if (pos <= 0) {
            pos = 0;
        }
        removeLayerSlot(from);
        insertLayerSlot(pos, layer, layerId);
    }
}

/* ---- the GFX state ---- */

/*
 * O7: the port always runs at 60 vblanks a second. The EU game already has a
 * 60 Hz path -- NTSC_MODE (src/engine/data/eu_settings.c) makes main() call
 * SetVideoMode(MODE_NTSC), display.c step its 8.8 time counters by
 * VSYNC_STEP_NTSC, the walk speed 0x400 instead of 0x4CC (actor.c:144) and
 * the music tick SS_NOTICK instead of SS_NOTICK | 50 (sound.c:427) -- so the
 * only host-side work is to set it before the game's main() runs. Called from
 * pcBootPresentInit, which boot.c runs right before pcGameMain. DW3_REFRESH=50
 * keeps the PAL timing as an internal debug switch (no menu entry).
 */
void pcRefreshInit(void) {
#if VERSION_EU
    const char *env = getenv("DW3_REFRESH");

    NTSC_MODE = (env != NULL && env[0] == '5' && env[1] == '0' && env[2] == '\0') ? 0 : 1;
#endif
}

/* The vsync step of the 8.8 time counters, as display.c uses it */
static s32 hostVsyncStep(void) {
#if VERSION_EU
    return NTSC_MODE ? VSYNC_STEP_NTSC : VSYNC_STEP_PAL;
#else
    return VSYNC_STEP_NTSC;
#endif
}

/*
 * GFX.funcs.reset: destroys every layer (the first call has none). The game
 * calls it at boot and again whenever a mode starts, to drop the previous
 * mode's drawing.
 */
static void hostResetGraphics(void) {
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] != NULL) {
            destroyLayer(GFX.layerIds[i]);
            i--;
        }
    }
}

/* The packet buffers (model-import I3). The game sizes them for the PlayStation (the battle: 0x19000 bytes, ~100 KB, which
   a model of 1692 polygons filled: the mesh drawer writes primitives without looking at the end, and on the host that was a
   heap corruption). The port reserves PRIM_CAPACITY for every mode instead: the pages of a block nobody writes are never
   committed (malloc maps a block this big on demand), so the idle cost is address space, and the one the frame needs is
   what the RSS shows. GFX.primBufSize is the capacity, which the table walkers below use as the buffer's end.

   The guard: the end of the usable part is PRIM_SLACK short of the capacity. A frame that writes past it (a mesh batch that
   ends beyond the limit, or the next one that starts there) is an overflow: the rest of the frame's primitives are written
   into a sink block that getPrim hands out again at every call, nothing is linked into a table the frame draws, the layers
   are not drawn that frame (a blank frame, not a corrupted heap) and one line says so. A single batch larger than the slack
   (an ordering table of the game writes the prims of one bone or sprite run between getPrim and setPrim) still cannot be
   caught, so the slack is far above any one bone's polygons (16 K of them). The next frame starts clean. */
#define PRIM_CAPACITY (8u << 20)
#define PRIM_SLACK (1u << 20)
#define PRIM_SINK_BYTES (1u << 20)

static int primOverflow;          /* this frame wrote past the usable part: the rest goes to the sink and the frame is not drawn */
static long primOverflowFrames;   /* frames lost this mode */
static size_t primHighWater;      /* the most bytes one frame used this mode */
static void *primSink;

static size_t primUsed(void *next) {
    return GFX.primBufs[GFX.buffer] != NULL ? (size_t)((u8 *)next - (u8 *)GFX.primBufs[GFX.buffer]) : 0;
}

static void primNoteOverflow(void) {
    if (!primOverflow) {
        primOverflow = 1;
        primOverflowFrames++;
        if (primOverflowFrames == 1 || primOverflowFrames == 10 || primOverflowFrames == 100 || primOverflowFrames % 1000 == 0) {
            plogLine("gfx", "gfx: the packet buffer (%d bytes) overflowed: the frame is dropped (%ld so far); a model or scene draws more "
                     "primitives than the port reserves for\n", (int)GFX.primBufSize, primOverflowFrames);
        }
    }
}

/* GFX.funcs.allocPrimBuffers: two packet buffers of PRIM_CAPACITY bytes each, whatever `size` the game asks. The host draws one
   frame at a time, so only the first is used. */
static void hostAllocPrimBuffers(s32 size) {
    s32 cap = (s32)(size > (s32)PRIM_CAPACITY ? (u32)size : PRIM_CAPACITY);

    GFX.primBufSize = cap;
    GFX.primBufs[0] = HEAP.allocHigh(cap, MEM_MODE);
    GFX.primBufs[1] = HEAP.allocHigh(cap, MEM_MODE);
    GFX.prim = GFX.primBufs[GFX.buffer];
    if (primSink == NULL) {
        primSink = malloc(PRIM_SINK_BYTES);
    }
    primOverflow = 0;
    primOverflowFrames = 0;
    primHighWater = 0;
    PLOG("gfx", PLOG_TRACE, "gfx: two packet buffers of %d bytes for the mode (the game asked for %d)\n", (int)cap, (int)size);
}

/* GFX.funcs.getPrim: the next free byte of the frame's packet buffer (the sink once the frame has overflowed) */
static void *hostGetPrim(void) {
    if (primOverflow && primSink != NULL) {
        return primSink;
    }
    if (primSink != NULL && GFX.primBufs[GFX.buffer] != NULL && primUsed(GFX.prim) > (size_t)GFX.primBufSize - PRIM_SLACK) {
        primNoteOverflow();
        return primSink;
    }
    return GFX.prim;
}

/* GFX.funcs.setPrim: where the next primitive goes */
static void hostSetPrim(void *next) {
    size_t used;

    if (primOverflow) {
        return; /* what was written went to the sink */
    }
    used = primUsed(next);
    if (used > primHighWater) {
        primHighWater = used;
    }
    if (primSink != NULL && GFX.primBufs[GFX.buffer] != NULL && used > (size_t)GFX.primBufSize - PRIM_SLACK) {
        primNoteOverflow();
        return;
    }
    GFX.prim = next;
}

/* GFX.funcs.freePrimBuffers */
static void hostFreePrimBuffers(void) {
    if (GFX.primBufs[0] != NULL) {
        PLOG("gfx", PLOG_TRACE, "gfx: packet buffers freed, the most one frame used was %d bytes\n", (int)primHighWater);
        HEAP.free(GFX.primBufs[0]);
    }
    if (GFX.primBufs[1] != NULL) {
        HEAP.free(GFX.primBufs[1]);
    }
    GFX.primBufs[0] = NULL;
    GFX.primBufs[1] = NULL;
    GFX.prim = NULL;
}

/* GFX.funcs.startVSync: drawFrame presents; the host does not install a
   vsync interrupt (see the file comment). */
static void hostStartVSync(void) {
}

/* shim/host_gte.c: InitGeom plus the retail DQA/DQB */
extern void hostGeomInit(void);

/* GFX.funcs.setDisplayMode: one 320x240 buffer, shown from the same VRAM area
   it is drawn to (a single window buffer on the host, see the file comment). */
static void hostSetDisplayMode(s32 w, s32 h, s32 hires, s32 interlace) {
    (void)hires;
    (void)interlace;
    SetDefDispEnv(&GFX.disp[0], 0, 0, w, h);
    SetDefDispEnv(&GFX.disp[1], 0, 0, w, h);
    GFX.dispBuffer = 0;
    GFX.buffer = 0;
    PutDispEnv(&GFX.disp[0]);
    hostGeomInit();
    SetGeomOffset(0, 0);
}

/* GFX.funcs.setDisplayArea: shows a VRAM area from the (single) buffer */
static void hostSetDisplayArea(s32 x, s32 y, s32 w, s32 h) {
    /* T43: the movie player's TASK_KILL calls this as its last step, when it
       puts the display back to the game's size. The presenter's quads draw
       every frame until something disarms them, and after the kill nothing
       runs DecDCTin/DecDCTReset any more, so pressInvalidate's
       hostVideoDisable never fires and the stale quads draw over the next
       mode's picture (the title's bands, measured 2026-10-22). This is the
       one host-visible moment the movie ended. A later movie turns the
       source back on through pressPresentFrame. */
    hostVideoDisable();
    SetDefDispEnv(&GFX.disp[0], x, y, w, h);
    SetDefDispEnv(&GFX.disp[1], x, y, w, h);
    PutDispEnv(&GFX.disp[0]);
}

static s32 hostGetFrameCount(void) {
    return GFX.frameCount;
}

static s32 hostGetTime(void) {
    return GFX.time;
}

static s32 hostGetFrameTime(void) {
    return GFX.frameTime;
}

/*
 * The frame's primitives, by kind, walked the way DrawOTag walks them. The
 * kind is the primitive's own type nibble, the way PsyCross's parser reads it
 * (`code & 0xF0`): the low bits a primitive carries (semi-transparency, raw
 * texture) do not change what it is.
 */
static void hostReportOt(Layer *layer, s32 *sprites, s32 *polys, s32 *env, s32 *other, s32 *prims) {
    P_TAG *node = (P_TAG *)&hostOt(layer)[layer->otLen - 1];
    s32 guard;

    *sprites = 0;
    *polys = 0;
    *env = 0;
    *other = 0;
    *prims = 0;
    for (guard = 0; guard < 4096; guard++) {
        if (node->len > 0) {
            (*prims)++;
            switch (node->code & 0xF0) {
            case 0x60:
            case 0x70:
                (*sprites)++;
                textIconsNoteSprite(((SPRT *)node)->clut);
                break;
            case 0x20:
            case 0x30:
                (*polys)++;
                break;
            case 0xE0:
                (*env)++;
                break;
            default:
                (*other)++;
                break;
            }
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)node->addr;
    }
}

/*
 * T43: what a layer's table carries, primitive by primitive, and which texture
 * page is in force when each one is reached -- a sprite has no page of its own,
 * it uses the DR_TPAGE that precedes it, so the page is tracked in step with
 * the walk. DW3_OT_DUMP=<frame> prints, on that presented frame, every
 * primitive of every layer in the order DrawOTag walks them (the frame count
 * is the same one DW3_CB_DUMP and DW3_ENV_DUMP use). Unset (the default), the
 * run prints nothing extra.
 */
static s32 otDumpFrame = -1;
static s32 otDumpRead;

/* The armed frame, read from the environment once; -1 when unset */
static s32 hostOtArmedFrame(void) {    if (!otDumpRead) {
        const char *env = getenv("DW3_OT_DUMP");

        otDumpRead = 1;
        otDumpFrame = -1;
        if (env != NULL) {
            s32 value = 0;

            while (*env >= '0' && *env <= '9') {
                value = value * 10 + (*env - '0');
                env++;
            }
            otDumpFrame = value;
        }
    }
    return otDumpFrame;
}

/* One primitive's line: the fields its own type carries */
static void hostOtDumpLayer(Layer *layer, s32 id) {    P_TAG *node = (P_TAG *)&hostOt(layer)[layer->otLen - 1];
    u_long tpage = 0;
    s32 index = 0;
    s32 guard;

    for (guard = 0; guard < layer->otLen + GFX.primBufSize; guard++) {
        if (node->len > 0) {
            switch (node->code & 0xF0) {
            case 0xE0: {
                u_int *word = (u_int *)&node->pad0;

                tpage = *word;
                printf("ot %d layer 0x%X prim %d TPAGE word=0x%08X x=%d y=%d depth=%d "
                       "dfe=%d\n",
                       (int)presentFrames, (int)id, (int)index, (unsigned)*word,
                       (int)(*word & 0xF), (int)((*word >> 4) & 1),
                       (int)((*word >> 7) & 3), (int)((*word >> 10) & 1));
                break;
            }
            case 0x60:
            case 0x70: {
                SPRT *sprite = (SPRT *)node;

                printf("ot %d layer 0x%X prim %d SPRT 0x%02X x=%d y=%d w=%d h=%d "
                       "u=%d v=%d clut=0x%04X tpage=0x%08X x16=%d y=%d\n",
                       (int)presentFrames, (int)id, (int)index, (unsigned)node->code,
                       (int)sprite->x0, (int)sprite->y0, (int)sprite->w, (int)sprite->h,
                       (int)sprite->u0, (int)sprite->v0, (unsigned)sprite->clut,
                       (unsigned)tpage, (int)(tpage & 0xF), (int)((tpage >> 4) & 1));
                break;
            }
            case 0x20:
            case 0x30: {
                POLY_FT4 *quad = (POLY_FT4 *)node;

                if ((node->code & 0xFC) == 0x2C) {
                    printf("ot %d layer 0x%X prim %d FT4 0x%02X (%d,%d) (%d,%d) (%d,%d) "
                           "(%d,%d) uv=(%d,%d) (%d,%d) (%d,%d) (%d,%d) clut=0x%04X "
                           "tpage=0x%08X x16=%d y=%d\n",
                           (int)presentFrames, (int)id, (int)index, (unsigned)node->code,
                           (int)quad->x0, (int)quad->y0, (int)quad->x1, (int)quad->y1,
                           (int)quad->x2, (int)quad->y2, (int)quad->x3, (int)quad->y3,
                           (int)quad->u0, (int)quad->v0, (int)quad->u1, (int)quad->v1,
                           (int)quad->u2, (int)quad->v2, (int)quad->u3, (int)quad->v3,
                           (unsigned)quad->clut, (unsigned)tpage,
                           (int)(tpage & 0xF), (int)((tpage >> 4) & 1));
                } else {
                    printf("ot %d layer 0x%X prim %d POLY 0x%02X len=%d\n",
                           (int)presentFrames, (int)id, (int)index, (unsigned)node->code,
                           (int)node->len);
                }
                break;
            }
            default:
                printf("ot %d layer 0x%X prim %d 0x%02X len=%d\n",
                       (int)presentFrames, (int)id, (int)index, (unsigned)node->code,
                       (int)node->len);
                break;
            }
            index++;
        }
        if (isendprim(node)) {
            break;
        }
        node = (P_TAG *)node->addr;
    }
}

/*
 * T43: the VRAM the frame sampled, as PsyCross holds it. DW3_VRAM_DUMP=<file>
 * saves the whole 1024x512 page through PsyCross's own GR_SaveVRAM on the
 * DW3_OT_DUMP frame (the two share the frame, so the dump and the primitives
 * that read it are the same frame's), and does nothing when unset.
 */
static const char *vramDumpPath;
static s32 vramDumpRead;

static const char *hostVramDumpPath(void) {
    if (!vramDumpRead) {
        vramDumpRead = 1;
        vramDumpPath = getenv("DW3_VRAM_DUMP");
    }
    return vramDumpPath;
}

/*
 * T27: the frame's initial clear. On the PlayStation each layer's DRAWENV
 * travels inside its own ordering table (layerDraw's DR_ENV,
 * src/engine/gfx/layer.c:130-151) and the layers are sent bottom to top, so
 * the first layer that carries a background clears its area first (its
 * DR_ENV's isbg); the ones above clear theirs on top of it. The host has a
 * single clear -- PsyX_BeginScene's GR_Clear reads the one active DRAWENV --
 * so it applies the env of the first layer with a background: the first clear
 * the game's own order would run. The field's bottom layer (FIELD_LAYER_BACK,
 * field.c:315-316) is that layer; the area banner above it (field.c:321-322)
 * carries the same colour and its own shrinking clip, so its clear is a no-op
 * over the bottom layer's. A mode whose layers carry no background
 * (setBgColor(0,0,0) sets isbg 0) gets the bottom layer's env and
 * PsyX_BeginScene clears nothing, as the game's own DR_ENVs would not either.
 */
static void hostLayerApplyClearEnv(void) {
    s32 i;

    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] != NULL && GFX.layers[i]->env.isbg != 0) {
            hostLayerApplyEnv(GFX.layers[i], GFX.layerIds[i]);
            return;
        }
    }
    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] != NULL) {
            hostLayerApplyEnv(GFX.layers[i], GFX.layerIds[i]);
            return;
        }
    }
}

/*
 * GFX.funcs.drawFrame: the frame the game's own mode built. Each layer's
 * environment goes in immediately before that layer's own ordering table --
 * the order the game's own layerDraw has (its DR_ENV at the head of its own
 * table, layer.c:130-151) and what makes PsyCross record that layer's clip
 * for its split (see hostLayerApplyEnv) -- and the frame's clear uses the env
 * the game's own order would have active first (hostLayerApplyClearEnv). It
 * is the same sequence the milestone 4 and 5 harnesses run, with the layers'
 * draw methods in place of their single table.
 */
/* battle-speed: 1 while a battle turn is running its attack and technique animations. The
   battle menu task (WFIGHTMN_runTurn is its state 1) is up, the player is not choosing a command
   (the player-turn task is gone or at substate 0) and the engine's own slow camera turn is off.
   FIGHTSTG_isPlayerChoosing does not look for a missing task, so this does. */
static int battleSpeedGate(void) {
    Task *menu;
    PlayerTurn *turn;
    int gate;

    if (battleSpeedMode() == BATTLE_SPEED_NORMAL || GAME.funcs.getMode() != MODE_BATTLE) {
        return 0;
    }
    menu = TASK_REGISTRY.funcs.find(BATTLE_TASK_MENU, -1, -1);
    turn = TASK_REGISTRY.funcs.find(BATTLE_TASK_PLAYER_TURN, -1, -1);
    gate = menu != NULL && menu->state == 1 && (turn == NULL || turn->substate == 0) &&
           FIGHTSTG_battle.speed.mode == 0;
    {
        static int last = -1;
        static const char *log = (const char *)1;

        if (log == (const char *)1) {
            log = getenv("DW3_BATTLE_SPEED_LOG");
        }
        if (log != NULL && gate != last) {
            fprintf(stderr, "battle-speed: gate %d frame %d\n", gate, (int)(GFX.frameCounter >> 8));
            last = gate;
        }
    }
    return gate;
}

static void hostDrawFrame(s32 draw) {
    s32 i;
    Layer *layer;

    /* T43: the one-shot ordering-table dump, armed from the environment */
    hostOtArmedFrame();
    DrawSync(0);

    if (GFX.prim != NULL) {
        if (draw) {
            /* T26: the one-shot callback-order dump, off unless DW3_CB_DUMP
               is set; before runCallbacks, which empties the lists */
            hostCallbackDumpFrame();
            for (i = 0; i < LAYER_COUNT; i++) {
                layer = GFX.layers[i];
                if (layer != NULL) {
                    /* retail drawFrame (display.c:61-65): a layer whose
                       callback set asks for it re-loads the view state it
                       kept, before its own callbacks draw with it */
                    if (layer->keepView != 0) {
                        layer->loadView(layer);
                    }
                    if (layer->keepLightMatrix != 0) {
                        layer->loadLightMatrix(layer);
                    }
                    layer->runCallbacks(layer);
                }
            }
            hostLayerApplyClearEnv();
            /* controls-config C5: the font's face-button icons follow the player's device, in
               VRAM, before anything is drawn from it */
            textIconsFrame();
            /* T27: the env the frame's clear will use (hostEnvPrint) */
            if (hostEnvDumpActive()) {
                hostEnvPrint("clear", envAppliedId, &envApplied, -1);
            }
            PsyX_BeginScene();
            for (i = 0; i < LAYER_COUNT; i++) {
                layer = GFX.layers[i];
                if (layer != NULL && !primOverflow) { /* an overflowed frame's tables are not safe to walk */
                    s32 sprites, polys, env, other, prims;

                    /* T27: this layer's own env goes in just before its own
                       table, and the table's DR_TPAGEs are set to draw to
                       the display, so PsyCross records the env for this
                       layer's split on the host's one surface */
                    /* the battle's mirrored chains go into the real table
                       before the DR_TPAGE fixup walk sees it (and before the
                       table is sent) */
                    hostOtMirrorMerge(layer);
                    hostLayerApplyEnv(layer, GFX.layerIds[i]);
                    hostOtDrawToDisplay(layer);
                    if (hostEnvDumpActive()) {
                        DRAWENV own;

                        own = layer->env;
                        own.ofs[0] = layer->offsetX;
                        own.ofs[1] = layer->offsetY;
                        hostEnvPrint("own", GFX.layerIds[i], &own, layer->otLen);
                        hostEnvPrint("ot", GFX.layerIds[i], &envApplied, -1);
                        hostEnvPrintTpages(GFX.layerIds[i]);
                    }
                    /* T43 experiment: with DW3_SKIP_OT_DRAW set, the layer's
                       ordering table is never sent, so nothing but the frame
                      's clear may reach the display. A capture that still
                       shows art proves the fault is in the capture or in
                       which frame it reads, not in PsyCross's drawing. */
                    if (getenv("DW3_SKIP_OT_DRAW") == NULL) {
                        layer->draw(layer);
                    }
                    hostReportOt(layer, &sprites, &polys, &env, &other, &prims);
                    presentPrims += prims;
                    if (presentFrames == otDumpFrame) {
                        hostOtDumpLayer(layer, GFX.layerIds[i]);
                    }
                    if (presentFrames == artFrame) {
                        hostArtDump(layer);
                    }
                    PLOG("gfx", PLOG_TRACE, "frame %03d: layer 0x%X prims=%d (sprite=%d poly=%d env=%d other=%d) bytes=%d\n",
                         (int)presentFrames, (int)GFX.layerIds[i], (int)prims, (int)sprites,
                         (int)polys, (int)env, (int)other,
                         (int)((u8 *)GFX.prim - (u8 *)GFX.primBufs[GFX.buffer]));
                }
            }
            /* T43: the VRAM those primitives read, once per frame */
            if (presentFrames == otDumpFrame) {
                const char *path = hostVramDumpPath();

                if (path != NULL) {
                    GR_SaveVRAM(path, 0, 0, VRAM_WIDTH, VRAM_HEIGHT, 0);
                    printf("vram %d saved to %s\n", (int)presentFrames, path);
                }
            }
            /* T39: the movie frame the MDEC shim left in VRAM goes on top of
               the mode's own layers, as textured quads -- PsyCross never draws
               VRAM out to the window (see shim/host_video.c). A no-op unless a
               movie frame is set. */
            hostVideoDraw();
            /* The battle cursor bar's VRAM->screen moves (shim/host_battle.c) */
            hostBattleMovesDraw();
            /* controls-config C3: the controls screen, over everything */
            controlsScreenDraw();
            /* T30: the multi-frame capture. If the just-drawn frame is in
               the capture list, save it as a PNG before PsyX_EndScene swaps
               the back buffer (the harness reads with glReadPixels, like the
               single-shot capture at run end). The list is sorted, so this
               is one walk. */
            hostMultiCaptureCheck();
        }

        /* T17: what the game's own pad code had this frame (the menu read it at
           the top of the loop). This is also where the port calls
           PsyX_UpdateInput -- whose libpad writes the buffer the game's own
           PAD.update reads a few lines later in main -- types the scripted key
           of the automated demo and prints the state. See shim/host_pad.c. */
        pcBootPadFrame(presentFrames);

        presentFrames++;
        if ((presentFrames & 255) == 0) {
            faultReportRefreshModules(); /* the audio stack's libraries load after the start: a crash report names their frames */
        }
        /* T19: the field demo's stop (see pcBootStopInMode). The mode is read
           the same way the game reads it, and the count only starts once the
           mode has been reached. */
        if (stopMode != 0 && GAME.funcs.getMode() == stopMode) {
            stopModeSeen++;
        }
        if ((presentTarget > 0 && presentFrames >= presentTarget) ||
            (stopModeFrames > 0 && stopModeSeen >= stopModeFrames)) {
            hostEndPresent();
        }
        if (draw) {
            PsyX_EndScene();
        }
    }
    PsyX_WaitForTimestep(1);

    /* The counters of display.c's drawFrame, which the game's menus read
       through GFX.funcs.getTime / getFrameTime */
    GFX.frameCounter += hostVsyncStep();
    GFX.timeCounter += hostVsyncStep();
    /* opt-in battle event trace and scripted player (DW3_BATTLE_EVENT_LOG, DW3_BATTLE_SCRIPT) */
    battleTraceFrame();
    /* the stage banner option's fast mode (stage_banner.h) */
    stageBannerFrame();
    /* the fast door, battle entry and battle results options (fast_transitions.h) */
    fastTransitionsFrame();
    /* opt-in stage change timing (DW3_STAGE_TIMING) */
    if (stageTimingFrame((int)(GFX.frameCounter >> 8))) {
        hostEndPresent();
    }
    /* only the frame-time clock (what the battle's animations run on) is scaled; 8.8 fixed point,
       so x1.5 alternates one and two frames. The UI blink clock and play time stay as they were. */
    GFX.frameTimeCounter += battleSpeedStep(battleSpeedMode(), battleSpeedGate(), hostVsyncStep());
    GFX.frameCount = GFX.frameCounter >> 8;
    GFX.time = GFX.timeCounter >> 8;
    GFX.frameTime = GFX.frameTimeCounter >> 8;
    GFX.frameTimeCounter &= 0xFF;
    /* vsyncCallback's play-time step, which updatePlayTime turns into seconds */
    GAME.playTime.frames += hostVsyncStep();
    GAME.funcs.updatePlayTime();
    /* display.c runs the vsync callback right after the counters (the battle
       menu's cursor bar is drawn from it) */
    if (GFX.vsyncFunc != NULL) {
        GFX.vsyncFunc(GFX.vsyncArg);
    }

    GFX.prim = GFX.primBufs[GFX.buffer];
    primOverflow = 0; /* the next frame starts clean (clearOt below empties the tables the dropped one linked) */
    for (i = 0; i < LAYER_COUNT; i++) {
        if (GFX.layers[i] != NULL) {
            GFX.layers[i]->clearOt(GFX.layers[i]);
        }
    }
}

/* GFX: the graphics state and the methods the game calls through it. The
   state's layout is the game's own (engine/gfx.h); only its methods differ
   from display.c's (see the file comment). */
GfxState GFX = {
    .funcs = {
        hostResetGraphics, hostAllocPrimBuffers, hostGetPrim, hostSetPrim, hostFreePrimBuffers,
        hostStartVSync, hostDrawFrame, createLayer, destroyLayer, hostSetDisplayMode, hostSetDisplayArea,
        getLayer, moveLayer, hostGetFrameCount, hostGetTime, hostGetFrameTime,
    },
};
