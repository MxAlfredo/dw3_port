/*
 * The host's copy of the field's stream tasks (content-export E6b): one hook the upstream unit does not have, the rest copied verbatim
 * from src/field/field_mode/stream.c.
 *
 * Why: the tiles of a stage's Z_STAGE PACK are read with CD_READER.read into a heap buffer and unpacked by the Decompressor, so they never
 * pass through FILE_CACHE and the image layer's substitution (tim_loader_pc.c -> imgOverrideTim) cannot see them. FIELDSTG_loadStreamSprites
 * hands the tile's TIM to the TimLoader; here it first asks stageModTim (shim/stage_mod.c) for a mod's PNG of that tile (mods/<mod>/stages/
 * s<nnn>/tiles/<RR>_<CC>.png) and loads that TIM instead. Without a replacement stageModTim returns NULL and the original TIM is loaded: the
 * behaviour is byte-identical. ADR-003 keeps the game's own src/ untouched, hence the copy, the same pattern as pc/field/file_loader.c.
 *
 * Keep this file in sync with the upstream unit: apart from the hook (and this comment) its body is copied verbatim from
 * src/field/field_mode/stream.c. pc/Makefile drops that upstream unit from the field-mode glob and compiles this one in its place.
 */

/* The tasks that read a tile of the map from the CD and draw it */

#include "field/field_mode.h"

/* shim/stage_mod.c: the PNG replacement of a tile (NULL: none), for the tile `frame` of the PACK that starts at sector `lba` */
extern const unsigned char *stageModTim(long lba, int frame, const unsigned char *tim);

/* Marks a stream task used now (FIELDSTG_findOldestStream) */
void FIELDSTG_touchStream(StreamTask *task) {
    task->time = GFX.funcs.getTime();
}

/* Reads a frame of the map file, a tile, from the CD, unless it has it
   already */
void FIELDSTG_seekStream(StreamTask *task, s32 frame, s32 size) {
    if (task->frame != frame) {
        task->frame = frame;
        task->loaded = 0;
        task->loadedSlot = -1;
        task->sector = frame * task->frameSectors + 1;
        CD_READER.read(task->file, task->sector, size, task->buffer, &task->loaded);
        task->loadedTime = 0;
    }
    FIELDSTG_touchStream(task);
}

/* Whether a stream task's read is done */
s32 FIELDSTG_isStreamLoaded(StreamTask *task) {
    return task->loaded;
}

/* Draws a stream task's tile at (x, y) on a layer: its sprites at the three
   depths */
void FIELDSTG_drawStream(StreamTask *task, Layer *layer, s32 x, s32 y) {
    Vec2 scroll;
    PrimPtr prim;
    u_long *ot;
    s32 i;
    s32 j;

    layer->getScroll(layer, &scroll);
    prim.any = GFX.funcs.getPrim();
    for (i = 0; i < 3; i++) {
        ot = (u_long *)layer->getOtEntry(layer, FIELDSTG_spriteDepths[i]);
        for (j = 0; j < 5; j++) {
            if (task->sprites[i][j].visible) {
                SetSprt(prim.sprt);
                if (i == 2 || FIELDSTG_state.spriteColor.cd != 0) {
                    prim.sprt->r0 = FIELDSTG_state.spriteColor.r;
                    prim.sprt->g0 = FIELDSTG_state.spriteColor.g;
                    prim.sprt->b0 = FIELDSTG_state.spriteColor.b;
                } else {
                    prim.sprt->r0 = 0x80;
                    prim.sprt->g0 = 0x80;
                    prim.sprt->b0 = 0x80;
                }
                prim.sprt->x0 = task->sprites[i][j].x + x - scroll.x;
                prim.sprt->y0 = task->sprites[i][j].y + y - scroll.y;
                prim.sprt->w = task->sprites[i][j].w;
                prim.sprt->h = task->sprites[i][j].h;
                prim.sprt->clut = getClut(task->clutX, task->clutY);
                prim.sprt->u0 = task->sprites[i][j].u;
                prim.sprt->v0 = task->imageY + task->sprites[i][j].v;
                addPrim(ot, prim.any);
                prim.sprt++;
            }
        }
        SetDrawTPage(prim.tpage, 0, 1, GetTPage(1, 0, task->imageX, task->imageY));
        addPrim(ot, prim.any);
        prim.tpage++;
    }
    GFX.funcs.setPrim(prim.any);
    FIELDSTG_touchStream(task);
}

/* Decompresses a stream task's tile into slot */
void FIELDSTG_setStreamSource(StreamTask *task, s32 slot, Decompressor *source) {
    task->slot = slot;
    task->source = source;
    source->start(source, task->buffer, 0x2800);
    task->setSubstate(task, 1);
}

/* Takes a decompressed tile's sprites and loads its image into its slot's
   VRAM */
void FIELDSTG_loadStreamSprites(StreamTask *task) {
    TimLoader loader;
    s32 i;
    s32 j;
    s32 count;
    s32 *data = task->data;
    s32 slot = task->slot;
    s16 *p = (s16 *)(data + 1); /* the sprites of the three depths, after the first word */

    for (i = 0; i < 3; i++) {
        count = *(s32 *)p; /* a word: how many, then six halfwords each */
        p += 2;
        for (j = 0; j < count; j++) {
            task->sprites[i][j].visible = 1;
            task->sprites[i][j].x = *p++;
            task->sprites[i][j].y = *p++;
            task->sprites[i][j].u = *p++;
            task->sprites[i][j].v = *p++;
            task->sprites[i][j].w = *p++;
            task->sprites[i][j].h = *p++;
        }
        for (; j < 5; j++) {
            task->sprites[i][j].visible = 0;
        }
    }
    task->loadedSlot = slot;
    task->imageX = FIELDSTG_slotImages[slot].x;
    task->imageY = FIELDSTG_slotImages[slot].y;
    task->clutX = 0;
    task->clutY = slot + 0xF0;
    initTimLoader(&loader);
    loader.setImagePos(task->imageX, task->imageY);
    loader.setClutPos(task->clutX, task->clutY);
    {
        void *tim = FILE_CACHE.getArchiveEntry(0, data);
        const unsigned char *sub = stageModTim((long)FILE_TABLE.getSector(task->file), task->frame, (const unsigned char *)tim);

        loader.load(sub != NULL ? (void *)sub : tim);
    }
    FIELDSTG_touchStream(task);
}

/* Forgets a stream task's slot */
void FIELDSTG_clearStreamSlot(StreamTask *task) {
    task->loadedSlot = -1;
}

/* A stream task's slot, or -1 */
s32 FIELDSTG_getStreamSlot(StreamTask *task) {
    return task->loadedSlot;
}

/* A stream task's frame, the tile it holds */
s32 FIELDSTG_getStreamFrame(StreamTask *task) {
    return task->frame;
}

/* A stream task's update: takes its tile once decompressed, and notes when
   its read ends */
void FIELDSTG_updateStream(StreamTask *task) {
    switch (task->state) {
    case TASK_INIT:
    default:
        task->nextState(task);
        break;
    case TASK_RUN:
        switch (task->substate) {
        case 0:
            break;
        case 1:
            task->data = task->source->getData(task->source);
            if (task->data != NULL) {
                FIELDSTG_loadStreamSprites(task);
                task->setSubstate(task, 0);
            }
            break;
        }
        if (task->loadedTime == 0 && task->loaded != 0) {
            task->loadedTime = GFX.funcs.getTime();
        }
        break;
    case TASK_DONE:
        break;
    case TASK_KILL:
        HEAP.free(task->buffer);
        break;
    }
}

/* Creates a stream task of a file, with a buffer of size */
StreamTask *FIELDSTG_createStream(s32 size, s32 file) {
    StreamTask *task = createTask(FIELDSTG_updateStream, sizeof(StreamTask), 0);

    task->seek = FIELDSTG_seekStream;
    task->draw = FIELDSTG_drawStream;
    task->setSource = FIELDSTG_setStreamSource;
    task->getFrame = FIELDSTG_getStreamFrame;
    task->isLoaded = FIELDSTG_isStreamLoaded;
    task->getSlot = FIELDSTG_getStreamSlot;
    task->clearSlot = FIELDSTG_clearStreamSlot;
    task->updateTime = FIELDSTG_touchStream;
    task->file = file;
    task->frameSectors = size / 2048;
    task->buffer = HEAP.allocHigh(size, 2);
    task->frame = -1;
    task->loadedSlot = -1;
    task->loaded = 1;
    return task;
}
