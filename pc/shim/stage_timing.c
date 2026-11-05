/*
 * Stage change timing trace: the per-frame poll (see stage_timing.h). It watches the
 * game's own state from the port side, so no game file is touched: the mode, the field
 * task (FIELD_TASK_FIELD), the area banner (FIELD_TASK_BANNER) and FIELDSTG_state.frozen.
 */

#include "engine/game.h"
#include "engine/heap.h"
#include "battle/battle_mode.h"
#include "battle/normal_battle.h"
#undef OVL_NAME
#include "battle/report.h"
#undef OVL_NAME
#include "field/field_map.h"
#include "field/field_mode.h"
#include "host.h"

#include "fast_transitions.h"
#include "port_config.h"
#include "stage_banner.h"
#include "stage_timing.h"


typedef struct Seen {
    int present, state, substate, step;
} Seen;

/* Logs a task's appearance, vanishing and every state/substate change (and step, when asked: the
   banner's step counts its hold frame by frame, which would be a line per frame) */
static void pollTask(const char *name, int id, Seen *seen, int withStep) {
    Task *task = TASK_REGISTRY.funcs.find(id, -1, -1);

    if (task == NULL) {
        if (seen->present) {
            stageTimingLog("%s gone", name);
            seen->present = 0;
        }
        return;
    }
    if (!seen->present || task->state != seen->state || task->substate != seen->substate ||
        (withStep && task->step != seen->step)) {
        stageTimingLog("%s state=%d substate=%d step=%d", name, (int)task->state, (int)task->substate,
                       (int)task->step);
        seen->present = 1;
        seen->state = task->state;
        seen->substate = task->substate;
        seen->step = task->step;
    }
}

/*
 * The stage banner option's Fast mode (stage_banner.h), a per-frame poll of the game's own state
 * from the port side, so no game file is touched. The original keeps the field's loading step
 * (FIELDSTG_updateField, showField) waiting for the banner's TASK_DONE and the player frozen
 * until the banner's TASK_KILL. Fast mode, once the field has loaded (substate 4):
 *   - moves the banner to TASK_DONE (showField then lets the field run, and the banner starts
 *     its hold and its closing clip), with every box visible and the typing quick, so the name
 *     shows over the stage at once;
 *   - unfreezes the player when the field runs (state 1);
 *   - shortens the hold, but only after both name windows finished typing;
 *   - keeps the player frozen again if the field starts to close (field.c sets frozen=1 there
 *     and the banner's TASK_KILL, still to come, would clear it).
 * Anything else that froze the player is left alone: the poll only writes frozen while it owns
 * the banner it armed on.
 */
static int mapViewPending(int waited);

void stageBannerFrame(void) {
    static int armed;    /* a banner is on screen that this poll started to handle */
    static int forced;   /* the banner was moved to TASK_DONE */
    static int shown;    /* the stage was shown: every tile in its view was unpacked */
    static int waited;   /* frames the stage was held back for its tiles */
    static int released; /* the player was unfrozen */
    static int lastMode = -1;
    Task *banner;
    Task *field;
    AreaNameWindows *windows;
    int i;

    if (GAME.funcs.getMode() != lastMode) {
        /* a new stage starts hidden, whatever the last one left (a banner cut short by a door) */
        lastMode = GAME.funcs.getMode();
        armed = forced = shown = released = waited = 0;
        stageBannerSetShown(0);
    }
    banner = PC_IS_FIELD_MODE(GAME.funcs.getMode()) ? TASK_REGISTRY.funcs.find(FIELD_TASK_BANNER, -1, -1) : NULL;
    if (banner == NULL || stageBannerMode() != STAGE_BANNER_FAST) {
        armed = forced = shown = released = waited = 0;
        stageBannerSetShown(0);
        return;
    }
    if (!armed) {
        /* FIELDSTG_createBanner froze the player; a banner that did not is not ours */
        if (FIELDSTG_state.frozen == 0) {
            return;
        }
        armed = 1;
    }
    field = TASK_REGISTRY.funcs.find(FIELD_TASK_FIELD, -1, -1);
    windows = (AreaNameWindows *)banner->children;
    if (field == NULL || windows == NULL || windows->area == NULL || windows->place == NULL) {
        return; /* a banner with no names (key1 0) closes by itself */
    }
    if (!forced && banner->state == TASK_RUN && field->state == 0 && field->substate == 4) {
        windows->area->setTypeDelay(windows->area, pcStageBanner()->typeDelayFrames);
        windows->place->setTypeDelay(windows->place, pcStageBanner()->typeDelayFrames);
        for (i = 0; i < 10; i++) {
            BannerBox *box = &((AreaBanner *)banner)->boxes[i];

            box->visible = 1;
            if ((pcStageBanner()->hiddenBoxes >> i) & 1) {
                box->visible = 0; /* the bars the config hides (the vertical ones by default) */
                continue;
            }
            box->pos.vy -= pcStageBanner()->raisePx;
            if (box->stretch == 2) {
                box->from -= pcStageBanner()->raisePx;
                box->to -= pcStageBanner()->raisePx;
            }
        }
        windows->area->setPos(windows->area, windows->area->x, windows->area->y - pcStageBanner()->raisePx);
        windows->place->setPos(windows->place, windows->place->x, windows->place->y - pcStageBanner()->raisePx);
        banner->setState(banner, TASK_DONE);
        forced = 1;
    }
    /* the map is drawn only once no tile in its view is still the blue placeholder grid; the
       fast map option unpacks them in the frame after the field loaded, the classic one takes longer */
    if (forced && !shown) {
        if (stageBannerMayShow(mapViewPending(waited), waited)) {
            shown = 1;
            stageBannerSetShown(1);
            if (stageTimingOn()) {
                stageTimingLog("fast banner: stage shown (held %d frames for the map)", waited);
            }
        }
        waited++;
    }
    if (forced && shown && !released && field->state == 1) {
        FIELDSTG_state.frozen = 0;
        released = 1;
        if (stageTimingOn()) {
            stageTimingLog("fast banner: player released");
        }
    }
    if (released && field->state >= 2) {
        FIELDSTG_state.frozen = 1;
    }
    if (forced && banner->state == TASK_DONE && banner->substate == 1 &&
        (!windows->area->isFinished(windows->area) || !windows->place->isFinished(windows->place))) {
        /* the hold (the banner ends at step 60) counts only once the whole name is typed */
        banner->step = 60 - pcStageBanner()->holdFrames;
    }
}

/* ---- fast transitions (fast_transitions.h): the port adjusts the game's own task state after a tick ---- */

/* The first live task of a mode's heap with that update function, NULL if none */
static Task *findModeTask(void *update) {
    void *block;

    for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
        Task *task = (Task *)pcHeapBlockData(block);

        if (pcHeapBlockTag(block) == MEM_MODE && task->update == (void (*)(Task *, void *))update &&
            task->state != TASK_KILL) {
            return task;
        }
    }
    return NULL;
}

/* Door transition: FIELDSTG_closeField (field.c:223-288) shrinks the view by 10 x 7 pixels a frame while the
   cover fades in (substate 1), then opens the cover's clip 8 pixels a frame (substate 2). After each of its
   ticks the poll adds the other FAST_DOOR_FACTOR - 1 ticks' worth to the same fields, so the shapes and the end
   conditions are the game's own; a frame the game did not tick (the width did not move) adds nothing. */
static void fastDoorFrame(FieldTask *field) {
    static int lastSub, lastWidth;
    int extra = pcTransitions()->doorFactor - 1;

    if (field->substate != lastSub) {
        lastSub = field->substate;
        lastWidth = -1000;
    }
    if (field->width == lastWidth) {
        return;
    }
    if (field->substate == 1 && field->width > 0) {
        field->width -= 10 * extra;
        if (field->width < 1) {
            field->width = 1; /* the game's next tick takes it to 0 and ends the step */
        }
        field->height -= 7 * extra;
        if (field->height < 1) {
            field->height = 1;
        }
        field->fade += 0x400 * extra;
        if (field->fade > 0x8000) {
            field->fade = 0x8000; /* the game stops adding at exactly 0x8000 */
        }
    } else if (field->substate == 2 && field->width > 0) {
        field->width += 8 * extra;
        if (field->width > SCREEN_WIDTH / 2) {
            field->width = SCREEN_WIDTH / 2; /* the game's next tick passes it and requests the mode */
        }
    }
    lastWidth = field->width;
}

/* Battle transition: FIELDSTG_playBattleTransition (field.c:61-91) moves one tile a frame along
   FIELDSTG_tileMoves, then waits TRANSITION_WAIT frames. After each frame the game moved a tile, the poll
   moves FAST_BATTLE_TILES_PER_FRAME - 1 more with the game's own rule, and the first frame of the wait takes
   it to FAST_BATTLE_WAIT_FRAMES from the end. */
static void fastBattleEntryFrame(FieldTask *field) {
    static int lastCounter = -1, lastValue, waitCut;
    TileMove *move = &FIELDSTG_tileMoves[field->counter];
    int more;

    if (field->substate != 1 || field->step >= TRANSITION_DONE) {
        lastCounter = -1;
        waitCut = 0;
        return;
    }
    if (move->dir == 0) {
        if (!waitCut) {
            waitCut = 1;
            if (field->step < TRANSITION_WAIT - pcTransitions()->battleWaitFrames) {
                field->step = TRANSITION_WAIT - pcTransitions()->battleWaitFrames;
            }
        }
        return;
    }
    if (field->counter == lastCounter && *move->value == lastValue) {
        return; /* the game did not move a tile on this frame (GFX.buffer was busy) */
    }
    for (more = 1; more < pcTransitions()->battleTilesPerFrame; more++) {
        s32 speed = GFX.funcs.getFrameTime() * TRANSITION_SPEED;
        int moving;

        move = &FIELDSTG_tileMoves[field->counter];
        if (move->dir == 0) {
            break;
        }
        *move->value += speed * move->dir;
        moving = move->dir >= 0 ? move->limit > *move->value : *move->value > move->limit;
        if (!moving) {
            *move->value = TRANSITION_GONE;
            field->tickCounter(field);
        }
    }
    move = &FIELDSTG_tileMoves[field->counter];
    lastCounter = field->counter;
    lastValue = move->dir != 0 ? *move->value : 0;
}

/* Battle results: the report's panels open and close in `duration` frames (report.c:345, partner.c:451) and its
   closing screen fade takes 30. Once per report the poll divides those by FAST_RESULTS_FACTOR; the message
   pages (typing, button waits) and the rewards are not touched. */
static void fastResultsFrame(void) {
    static Task *scaledReport;
    static ScreenFade *scaledFade;
    FightReport *report = (FightReport *)findModeTask((void *)STFGTREP_updateReport);
    FightReportChildren *children;
    int i;

    if (report == NULL) {
        scaledReport = NULL;
        scaledFade = NULL;
        return;
    }
    children = (FightReportChildren *)report->children;
    if (children == NULL || report->state != TASK_RUN) {
        return;
    }
    if ((Task *)report != scaledReport) {
        scaledReport = (Task *)report;
        scaledFade = NULL;
        report->header.duration = report->header.duration / pcTransitions()->resultsFactor + 1;
        report->footer.duration = report->footer.duration / pcTransitions()->resultsFactor + 1;
        for (i = 0; i < report->count && i < 3; i++) {
            if (children->partners[i] != NULL) {
                children->partners[i]->fade.duration = children->partners[i]->fade.duration / pcTransitions()->resultsFactor + 1;
            }
        }
    }
    if (children->fade != NULL && children->fade != scaledFade && children->fade->state == TASK_RUN &&
        children->fade->substate == 1) {
        children->fade->levelStep *= pcTransitions()->resultsFactor;
        scaledFade = children->fade;
    }
}

/*
 * Map loading (fast_transitions.h): FIELDSTG_drawMapTiles hands one read tile a frame to the one decompressor and
 * unpacks it 0x2800 bytes a frame, so a 4 x 3 view takes about a second to fill and the blue cover grid shows
 * meanwhile. The poll runs after the frame was presented, so what it prepares is what the game's next tick draws:
 *   - the tile the decompressor is unpacking is finished (decompressorStep with no chunk limit) and uploaded
 *     (FIELDSTG_updateStream, which the next tick would do);
 *   - each tile in the view that is read (isLoaded) and has no VRAM slot gets one the way the game gives it (the
 *     slot unused longest, FIELDSTG_setStreamSource, the slot's previous tile forgotten), is unpacked at once and
 *     uploaded, with no cover fade on it. Slots of tiles in the view are never taken.
 * The reads, the 12 slots and the drawing are the game's own.
 */
typedef struct MapView {
    int count;
    int frame[12];
} MapView;

/* The 4 x 3 tiles FIELDSTG_drawMapTiles draws, from the scroll it read on the last tick */
static void mapViewTiles(MapStreamer *map, MapView *view) {
    int x0 = map->scroll.x >= 0x20 ? (map->scroll.x - 0x20) / 128 : 0;
    int y0 = map->scroll.y >= 8 ? (map->scroll.y - 8) / 128 : 0;
    int r, c;

    view->count = 0;
    for (r = 0; r < 3 && r + y0 < map->height; r++) {
        for (c = 0; c < 4 && c + x0 < map->width; c++) {
            view->frame[view->count++] = c + x0 + (r + y0) * map->width;
        }
    }
}

/* Whether a frame is one of the view's */
static int inMapView(const MapView *view, int frame) {
    int i;

    for (i = 0; i < view->count; i++) {
        if (view->frame[i] == frame) {
            return 1;
        }
    }
    return 0;
}

/* The slot a new tile takes: an empty one, else the one drawn longest ago that no tile in view holds; -1 if none */
static int pickMapSlot(MapStreamer *map, const MapView *view) {
    int j, best = -1;

    for (j = 0; j < 12; j++) {
        if (map->images[j].frame == -1) {
            return j;
        }
        if (!inMapView(view, map->images[j].frame) && (best < 0 || map->images[j].time < map->images[best].time)) {
            best = j;
        }
    }
    return best;
}

/* Unpacks what the decompressor holds to the end and lets the stream take it (its next update would, a frame on) */
static void finishMapUnpack(StreamPool *pool, StreamTask *stream) {
    if (pool->decompressor->substate != 0) {
        pool->decompressor->chunkSize = 0x10000000;
        decompressorStep(pool->decompressor);
    }
    FIELDSTG_updateStream(stream);
}

static void fastMapFrame(MapStreamer *map, StreamPool *pool, const MapView *view) {
    int i, done = 0;
    double start = stageTimingOn() ? stageTimingNowMs() : 0.0;

    for (i = 0; i < 30; i++) {
        StreamTask *stream = pool->tasks[i];

        if (stream->state == TASK_RUN && stream->substate == 1) {
            finishMapUnpack(pool, stream); /* the tile the game just started */
        }
    }
    for (i = 0; i < view->count && done < pcTransitions()->mapTilesPerFrame; i++) {
        StreamTask *stream = FIELDSTG_findStream(pool, view->frame[i]);
        StreamTask *old;
        int best;

        if (stream == NULL || stream->getSlot(stream) != -1 || !stream->isLoaded(stream) || stream->state != TASK_RUN ||
            stream->substate != 0) {
            continue;
        }
        best = pickMapSlot(map, view);
        if (best < 0) {
            break;
        }
        stream->setSource(stream, best, pool->decompressor);
        if (map->images[best].frame != -1) {
            old = FIELDSTG_findStream(pool, map->images[best].frame);
            if (old != NULL && old != stream) {
                old->clearSlot(old);
            }
        }
        map->images[best].frame = view->frame[i];
        map->images[best].time = GFX.funcs.getTime();
        map->images[best].cover = 0;
        finishMapUnpack(pool, stream);
        done++;
    }
    if (done > 0 && stageTimingOn()) {
        stageTimingLog("map pump tiles=%d ms=%.2f", done, stageTimingNowMs() - start);
    }
}

/* The tiles of the view the last frame covered: not yet unpacked (the blue grid) and fading in */
static void traceMapCover(MapStreamer *map, StreamPool *pool, const MapView *view) {
    static int lastGrid = -1, lastFade = -1;
    int i, grid = 0, fade = 0;

    for (i = 0; i < view->count; i++) {
        StreamTask *stream = FIELDSTG_findStream(pool, view->frame[i]);
        int slot = stream != NULL ? stream->getSlot(stream) : -1;

        if (map->tiles[view->frame[i]].size == 0) {
            continue;
        }
        if (slot == -1) {
            grid++;
        } else if (map->images[slot].cover != 0) {
            fade++;
        }
    }
    if (grid != lastGrid || fade != lastFade) {
        stageTimingLog("map view=%d grid=%d fading=%d", view->count, grid, fade);
        lastGrid = grid;
        lastFade = fade;
    }
}

/* Tiles of the view that have data and no VRAM slot yet: the game draws them as the blue grid */
static int mapViewPending(int waited) {
    MapStreamer *map = (MapStreamer *)TASK_REGISTRY.funcs.find(FIELD_TASK_MAP, -1, -1);
    StreamPool *pool;
    MapView view;
    int i, pending = 0;

    if (map == NULL) {
        /* the map streamer is created right after the field loaded; a stage that keeps the file cache
           may have none, which is not worth more than a moment */
        return waited < STAGE_BANNER_NO_MAP_WAIT;
    }
    if (map->state != TASK_RUN || map->children == NULL || map->tiles == NULL) {
        return 1; /* the map is still being set up */
    }
    pool = (StreamPool *)map->children;
    mapViewTiles(map, &view);
    for (i = 0; i < view.count; i++) {
        StreamTask *stream = FIELDSTG_findStream(pool, view.frame[i]);

        if (map->tiles[view.frame[i]].size != 0 && (stream == NULL || stream->getSlot(stream) == -1)) {
            pending++;
        }
    }
    return pending;
}

static void mapLoadingFrame(void) {
    MapStreamer *map = (MapStreamer *)TASK_REGISTRY.funcs.find(FIELD_TASK_MAP, -1, -1);
    StreamPool *pool;
    MapView view;

    if (map == NULL || map->state != TASK_RUN || map->children == NULL || map->tiles == NULL) {
        return;
    }
    pool = (StreamPool *)map->children;
    mapViewTiles(map, &view);
    if (stageTimingOn()) {
        traceMapCover(map, pool, &view);
    }
    if (fastMapMode() == FAST_MODE_FAST) {
        fastMapFrame(map, pool, &view);
    }
}

/* The per-frame poll of the fast modes (fast_transitions.h) */
void fastTransitionsFrame(void) {
    int mode = GAME.funcs.getMode();
    FieldTask *field;

    if (mode == MODE_BATTLE_REPORT) {
        if (fastResultsMode() == FAST_MODE_FAST) {
            fastResultsFrame();
        }
        return;
    }
    if (!PC_IS_FIELD_MODE(mode)) { /* 0x2xx and the stages mods add (0x3xx) */
        return;
    }
    mapLoadingFrame();
    field = (FieldTask *)TASK_REGISTRY.funcs.find(FIELD_TASK_FIELD, -1, -1);
    if (field == NULL || field->state != TASK_DONE) {
        return;
    }
    if (FIELDSTG_state.battleStarting != 0) {
        if (fastBattleEntryMode() == FAST_MODE_FAST) {
            fastBattleEntryFrame(field);
        }
    } else if (fastDoorMode() == FAST_MODE_FAST) {
        fastDoorFrame(field);
    }
}

/* The tasks of the battle and of its report the trace watches, found by their update function
   on the mode's heap (the same walk the harness autopilot makes) */
typedef struct Watched {
    const char *name;
    void *update;
    int count, state, substate, step;
} Watched;

static Watched watched[] = {
    {"battle menu", (void *)WFIGHTMN_updateMenu, 0, 0, 0, 0},
    {"battle camera", (void *)FIGHTSTG_updateCamera, 0, 0, 0, 0},
    {"player turn", (void *)FIGHTSTG_updatePlayerTurn, 0, 0, 0, 0},
    {"command menu", (void *)FIGHTSTG_updateCommandMenu, 0, 0, 0, 0},
    {"tech menu", (void *)FIGHTSTG_updateTechMenu, 0, 0, 0, 0},
    {"message box", (void *)FIGHTSTG_updateMessage, 0, 0, 0, 0},
    {"report", (void *)STFGTREP_updateReport, 0, 0, 0, 0},
    {"report partner", (void *)STFGTREP_updatePartner, 0, 0, 0, 0},
};

/* Logs the number of live tasks of each watched kind and the state of the first one, on a change.
   Only runs in the battle (0x600) and its report (0x1400): elsewhere the heap is not theirs. */
static void pollWatched(int mode) {
    int i;
    void *block;

    if (mode != MODE_BATTLE && mode != MODE_BATTLE_REPORT) {
        return;
    }
    for (i = 0; i < (int)(sizeof(watched) / sizeof(watched[0])); i++) {
        Watched *w = &watched[i];
        Task *first = NULL;
        int count = 0;

        for (block = pcHeapFirstBlock(); block != NULL; block = pcHeapNextBlock(block)) {
            Task *task = (Task *)pcHeapBlockData(block);

            if (pcHeapBlockTag(block) == MEM_MODE && task->update == (void (*)(Task *, void *))w->update &&
                task->state != TASK_KILL) {
                if (first == NULL) {
                    first = task;
                }
                count++;
            }
        }
        if (count != w->count || (first != NULL && (first->state != w->state || first->substate != w->substate ||
                                                    (first->step != w->step &&
                                                     (w->update == (void *)FIGHTSTG_updateCamera || w->update == (void *)WFIGHTMN_updateMenu))))) {
            stageTimingLog("%s n=%d state=%d substate=%d step=%d", w->name, count, first ? (int)first->state : -1,
                           first ? (int)first->substate : -1, first ? (int)first->step : -1);
            w->count = count;
            w->state = first ? first->state : 0;
            w->substate = first ? first->substate : 0;
            w->step = first ? first->step : 0;
        }
    }
}

/* DW3_STAGE_TIMING=3: once the player is free for exitProbeSettle frames, leave through the stage's
   first exit slot exactly as the door's cross press does (FIELDSTG_setOffTrigger, trigger.c:149-151:
   leaveField with the slot's mode and landing, the place set), so the closing clip, the mode change and
   the next stage's entry can be timed without a key press. The walk-in step of the door is the
   actor's own animation, which runs beside the close and is not part of it. */
#define EXIT_PROBE_SETTLE 30

/* DW3_EXIT_COUNT=<n> takes n exits one after the other (outside to inside and back); default 1 */
static int exitCount(void) {
    const char *env = getenv("DW3_EXIT_COUNT");

    return env != NULL && *env >= '1' && *env <= '9' ? *env - '0' : 1;
}

/* DW3_EXIT_SETTLE=<frames> moves the exit later (after the banner is gone, as when the player walked a while) */
static int exitProbeSettle(void) {
    const char *env = getenv("DW3_EXIT_SETTLE");
    int frames = 0;

    while (env != NULL && *env >= '0' && *env <= '9') {
        frames = frames * 10 + (*env++ - '0');
    }

    return frames > 0 ? frames : EXIT_PROBE_SETTLE;
}

static int exitProbe(int playableFrames) {
    StageSlot *slot;

    if (playableFrames != exitProbeSettle()) {
        return 0;
    }
    for (slot = FIELDSTG_state.slots; slot != NULL && slot->type != 0; slot++) {
        if (slot->type == SLOT_EXIT) {
            stageTimingLog("exit probe: door to mode 0x%X", (int)slot->arg);
            FIELDSTG_leaveField(slot->arg, -1, slot->x << 8, slot->y << 8, slot->dir);
            GAME.place = slot->place;
            GAME.placeArg = slot->placeArg;
            return 1;
        }
    }
    stageTimingLog("exit probe: this stage has no exit slot");
    return 0;
}

int stageTimingFrame(int frame) {
    static int lastMode = -1;
    static int lastFrozen = -1;
    static int lastFrame = -1;
    static double lastPoll = -1.0;
    double now;
    static Seen field, banner;
    static int bannerSeen;
    static int playable;       /* the stage is playable (the log line was written) */
    static int playableFrames; /* frames the player has been free */
    static int exitFired;      /* exits taken (DW3_EXIT_COUNT of them end the run) */
    static int exitDoneThisStage;
    int mode;
    int frozen;

    if (!stageTimingOn()) {
        return 0;
    }
    stageTimingFrameNo = frame;
    if (lastFrame < 0 && stageTimingBootMs >= 0.0) {
        stageTimingLog("first frame, %.0f ms after the process started", stageTimingNowMs() - stageTimingBootMs);
    }
    if (lastFrame >= 0 && frame - lastFrame != 1) {
        stageTimingLog("frame step %d", frame - lastFrame);
    }
    lastFrame = frame;
    /* a frame much longer than the 16.7 ms the 60 Hz pacing gives is work the port or the game did */
    now = stageTimingNowMs();
    if (lastPoll >= 0.0 && now - lastPoll > 25.0) {
        stageTimingLog("slow frame ms=%.1f", now - lastPoll);
    }
    lastPoll = now;
    mode = GAME.funcs.getMode();
    if (mode != lastMode) {
        stageTimingLog("mode 0x%X (next 0x%X)", mode, (int)GAME.nextMode);
        lastMode = mode;
        /* each field mode is a new stage: its banner and its playable moment count again */
        bannerSeen = 0;
        playable = 0;
        playableFrames = 0;
        exitDoneThisStage = 0;
    }
    pollTask("field", FIELD_TASK_FIELD, &field, 1);
    pollTask("banner", FIELD_TASK_BANNER, &banner, 0);
    pollWatched(mode);
    frozen = FIELDSTG_state.frozen;
    if (PC_IS_FIELD_MODE(mode) && frozen != lastFrozen) {
        stageTimingLog("frozen=%d", frozen);
    }
    lastFrozen = frozen;
    if (banner.present) {
        bannerSeen = 1;
    }
    if (PC_IS_FIELD_MODE(mode) && bannerSeen && !frozen && (!banner.present || stageBannerMode() == STAGE_BANNER_FAST)) {
        if (!playable) {
            stageTimingLog("playable");
            playable = 1;
            if (stageTimingLevel() == 2 || (stageTimingLevel() == 3 && exitFired >= exitCount())) {
                return 1;
            }
        }
        playableFrames++;
        if (stageTimingLevel() == 3 && !exitDoneThisStage && exitFired < exitCount() && exitProbe(playableFrames)) {
            exitFired++;
            exitDoneThisStage = 1;
        }
    }
    return 0;
}
