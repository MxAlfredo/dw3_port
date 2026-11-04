/*
 * Fast transitions: the settings and the options rows (see fast_transitions.h). Pure code, linked
 * into the inputtest as well; the game side is fastTransitionsFrame in stage_timing.c.
 */
#include "controls_screen.h"
#include "fast_transitions.h"

static int doorMode = FAST_MODE_FAST;
static int battleEntryMode = FAST_MODE_FAST;
static int resultsMode = FAST_MODE_FAST;
static int mapMode = FAST_MODE_FAST;

static int cleanMode(int mode) {
    return (mode >= 0 && mode < FAST_MODES) ? mode : FAST_MODE_FAST;
}

void fastDoorSetMode(int mode) {
    doorMode = cleanMode(mode);
}

int fastDoorMode(void) {
    return doorMode;
}

void fastBattleEntrySetMode(int mode) {
    battleEntryMode = cleanMode(mode);
}

int fastBattleEntryMode(void) {
    return battleEntryMode;
}

void fastResultsSetMode(int mode) {
    resultsMode = cleanMode(mode);
}

int fastResultsMode(void) {
    return resultsMode;
}

void fastMapSetMode(int mode) {
    mapMode = cleanMode(mode);
}

int fastMapMode(void) {
    return mapMode;
}

void fastTransitionsRows(void) {
    static const char *const modeNames[] = {"Fast", "Classic"};
    CsWidget w;

    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Door transition", .key = "door_transition", .names = modeNames,
                   .count = FAST_MODES, .apply = fastDoorSetMode,
                   .desc = "Fast: the screen closes quickly. Classic: original.",
                   .comment = "Leaving a stage through a door: fast (the same closing, in about a third of the time) or classic (the original pace)."};
    csWidgetAdd(CS_TAB_ENHANCEMENTS, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Battle transition", .key = "battle_transition", .names = modeNames,
                   .count = FAST_MODES, .apply = fastBattleEntrySetMode,
                   .desc = "Fast: shorter tile spiral into battle. Classic: original.",
                   .comment = "Entering a battle from the field: fast (the same tile spiral, in less than half the time) or classic (the original pace)."};
    csWidgetAdd(CS_TAB_ENHANCEMENTS, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Battle results", .key = "battle_results", .names = modeNames,
                   .count = FAST_MODES, .apply = fastResultsSetMode,
                   .desc = "Fast: shorter report animations. Classic: original.",
                   .comment = "Victory report: fast (its opening and closing animations are shorter; every message still waits for your button, rewards unchanged) or classic (the original pace)."};
    csWidgetAdd(CS_TAB_ENHANCEMENTS, &w);
    w = (CsWidget){.kind = CS_ROW_CYCLE, .label = "Map loading", .key = "map_loading", .names = modeNames,
                   .count = FAST_MODES, .apply = fastMapSetMode,
                   .desc = "Fast: the map shows at once. Classic: original.",
                   .comment = "Field map tiles: fast (every tile in view is unpacked in the same frame, so the blue grid does not show on entering a stage or scrolling) or classic (one tile a frame, as on the console)."};
    csWidgetAdd(CS_TAB_ENHANCEMENTS, &w);
}
