/*
 * Battle event trace and scripted player (see battle_trace.h). Host side only:
 * the game's own code is untouched; the hooks swap function pointers the game
 * calls through (GAME.funcs.requestMode, FIGHTSTG_events.funcs.pop,
 * FIGHTSTG_battleFuncs.testParalysis), the same way host_pad.c wraps PAD.
 */
#include "engine/game.h"
#include "battle/battle_mode.h"
#undef OVL_NAME
#include "battle_trace.h"
#include "state_scope.h"
#include "port_log.h"

#include <stdio.h>

extern char *getenv(const char *name);
extern long strtol(const char *nptr, char **endptr, int base);

#define TRACE_SCRIPT_MAX 16

static int traceInit;
static int traceLog;
static int traceWrapped;
static int traceLead = -1;
static int traceEnemy = -1;
static int traceEnemyLevel = 5;
static int traceEnemyHp = -1;
static int traceEnemyStrength = 16;
static int traceSeed = -1;
static int traceForm = -1;
static int traceParalyze = -1; /* strength of a paralysis put on the player at the first pop (test of the mechanism) */
static int traceParalyzed;
static char traceScript[TRACE_SCRIPT_MAX]; /* 'd' digivolve, 'a' attack */
static int traceScriptNames[TRACE_SCRIPT_MAX]; /* dv:<nameId>, -1 for any form */
static int traceScriptCount;
static int traceTurn;       /* player turns the script has answered */
static int tracePops;
static int traceLastPeekKey = -1;

static void (*origRequestMode)(s32 mode, s32 arg);
static s32 (*origPop)(void);
static s32 (*origParalysis)(u8 side);

static int envInt(const char *name, int def) {
    const char *v = getenv(name);

    return v != NULL && v[0] != '\0' ? (int)strtol(v, NULL, 0) : def;
}

static void traceDump(const char *what, s32 type) {
    int i;
    BattleFighter *p = &FIGHTSTG_battle.state.fighters[0][FIGHTSTG_battle.state.active[0]];
    BattleFighter *e = &FIGHTSTG_battle.state.fighters[1][FIGHTSTG_battle.state.active[1]];
    int ps = -1, es = -1;

    if (p->id != 0 && e->id != 0) {
        ps = FIGHTSTG_battleFuncs.computeStats(0, 1, FIGHTSTG_battle.state.active[0])->stats[BATTLE_STAT_SPEED];
        es = FIGHTSTG_battleFuncs.computeStats(SIDE_ENEMY, 1, FIGHTSTG_battle.state.active[1])->stats[BATTLE_STAT_SPEED];
    }
    fprintf(stderr, "battle-trace: f=%d #%d %s type=%d idx=%d args=%d,%d | P id=%d hp=%d fl=%02x spd=%d | "
            "E id=%d hp=%d fl=%02x spd=%d | q=",
            (int)(GFX.frameCounter >> 8), tracePops, what, (int)type, (int)FIGHTSTG_events.curIndex,
            (int)FIGHTSTG_events.events[(int)FIGHTSTG_events.curIndex < 0 ? 0 : FIGHTSTG_events.curIndex].args[0],
            (int)FIGHTSTG_events.events[(int)FIGHTSTG_events.curIndex < 0 ? 0 : FIGHTSTG_events.curIndex].args[1],
            p->id, p->hp, p->flags, ps, e->id, e->hp, e->flags, es);
    for (i = 0; i < 99; i++) {
        QueuedEvent *q = &FIGHTSTG_events.events[i];

        if (q->type != 0) {
            fprintf(stderr, "[%d@%d:%d,%d]", q->type, q->time, (int)q->args[0], (int)q->args[1]);
        }
    }
    fprintf(stderr, "\n");
}

static s32 wrapPop(void) {
    s32 type = origPop();

    if (type == EVENT_PLAYER_TURN && traceParalyze >= 0 && traceParalyzed == 0) {
        traceParalyzed = 1;
        FIGHTSTG_inflictParalysis(0, 0, (u8)traceParalyze);
        fprintf(stderr, "battle-trace: f=%d forced paralysis strength %d\n", (int)(GFX.frameCounter >> 8), traceParalyze);
    }
    if (type != 0 && traceLog) {
        QueuedEvent *q = &FIGHTSTG_events.events[(int)FIGHTSTG_events.curIndex];

        /* a peeked event (modes -1) stays in its slot and is returned again every frame:
           log it once */
        if (q->type != 0) {
            int key = FIGHTSTG_events.curIndex * 1000 + type;

            if (key == traceLastPeekKey) {
                return type;
            }
            traceLastPeekKey = key;
        } else {
            traceLastPeekKey = -1;
        }
        tracePops++;
        traceDump(type == EVENT_PLAYER_TURN ? "POP PLAYER" : type == EVENT_ENEMY_TURN ? "POP ENEMY" : "POP other", type);
    }
    return type;
}

static s32 wrapParalysis(u8 side) {
    s32 lost = origParalysis(side);

    if (traceLog) {
        fprintf(stderr, "battle-trace: f=%d paralysis roll side=%d turn %s\n", (int)(GFX.frameCounter >> 8), side,
                lost ? "LOST" : "kept");
    }
    return lost;
}

static void wrapRequestMode(s32 mode, s32 arg) {
    if (mode == MODE_BATTLE && arg == 0) {
        if (traceLead >= 0 && traceLead < PC_PARTNER_COUNT) {
            s32 i;

            for (i = 0; i < 3; i++) {
                if (GAME.party[i] == traceLead) {
                    GAME.party[i] = GAME.party[0];
                    break;
                }
            }
            GAME.party[0] = traceLead;
            PC_PARTNER(traceLead).unlocked = traceLead + 3;
            fprintf(stderr, "battle-trace: party %d/%d/%d\n", (int)GAME.party[0], (int)GAME.party[1],
                    (int)GAME.party[2]);
        }
        if (envInt("DW3_BATTLE_PARTY_HP", -1) > 0) {
            PartnerStats *lp = &PC_PARTNER(GAME.party[0]).info;

            lp->stats[STAT_HP] = lp->stats[STAT_MAX_HP] = (s16)envInt("DW3_BATTLE_PARTY_HP", 150);
        }
        if (traceForm >= 0) {
            /* give the lead partner the form whose name string is traceForm as battle slot 0 */
            s32 lead = GAME.party[0];
            PartnerStats *ps = &PC_PARTNER(lead).info;
            int d, e, id = -1;

            for (d = 0; d < 55; d++) {
                if (PC_DIGIMON(d).nameId == traceForm) {
                    id = PC_DIGIMON(d).id; /* the form's own row of the table */
                }
            }
            if (id < 0) {
                for (d = FIRST_ENTRY_ID; d < 0x200; d++) {
                    DigimonData *dd = GET_DIGIMON(d);

                    if (dd != NULL && dd->id == d && dd->nameId == traceForm) {
                        id = d;
                        break;
                    }
                }
            }
            for (e = 0; id >= 0 && e < PARTNER_ENTRY_COUNT; e++) {
                if (ps->entries[e].id == id) {
                    break;
                }
            }
            for (e = e < PARTNER_ENTRY_COUNT ? e : 0; id >= 0 && e < PARTNER_ENTRY_COUNT; e++) {
                if (ps->entries[e].id == id || ps->entries[e].id < FIRST_ENTRY_ID) {
                    ps->entries[e].id = id;
                    ps->entries[e].level = 1;
                    ps->slots[0] = id;
                    break;
                }
            }
            fprintf(stderr, "battle-trace: lead partner %d slot 0 = form id %d (nameId %d)\n", (int)lead, id,
                    traceForm);
        }
        if (traceEnemy >= 0) {
            BattleTableEntry *table = FILE_CACHE.load(FILE_BATTLE_TABLE);
            int i;

            if (envInt("DW3_BATTLE_ENEMY_LIST", 0)) {
                for (i = 0; table[i].id != 0; i++) {
                    if (table[i].nameId == traceEnemy) {
                        fprintf(stderr, "battle-trace: table entry %d id %d techs %d,%d,%d eff %d,%d,%d\n", i, table[i].id,
                                (int)table[i].techs[0], (int)table[i].techs[1], (int)table[i].techs[2],
                                table[i].techs[0] > 0 ? PC_TECH(table[i].techs[0] - 1).effect : -1,
                                table[i].techs[1] > 0 ? PC_TECH(table[i].techs[1] - 1).effect : -1,
                                table[i].techs[2] > 0 ? PC_TECH(table[i].techs[2] - 1).effect : -1);
                    }
                }
            }
            for (i = 0; table[i].id != 0; i++) {
                if (table[i].nameId == traceEnemy) {
                    BATTLE_SETUP.enemies[0].fighter = table[i].id;
                    BATTLE_SETUP.enemies[0].level = traceEnemyLevel;
                    BATTLE_SETUP.enemies[0].hp = traceEnemyHp >= 0 ? traceEnemyHp : 48 * (traceEnemyLevel + 1);
                    BATTLE_SETUP.enemies[0].mp = 9999;
                    BATTLE_SETUP.enemies[0].strength = traceEnemyStrength;
                    BATTLE_SETUP.enemies[1].fighter = 0;
                    BATTLE_SETUP.enemies[2].fighter = 0;
                    BATTLE_SETUP.ambushChance = 0;
                    fprintf(stderr, "battle-trace: enemy table entry %d (id %d, nameId %d) level %d\n", i,
                            table[i].id, traceEnemy, traceEnemyLevel);
                    {
                        int k;

                        fprintf(stderr, "battle-trace: enemy techs:");
                        for (k = 0; k < 3; k++) {
                            s32 tn = table[i].techs[k];

                            if (tn > 0) {
                                fprintf(stderr, " %d(effect %d chance %d)", tn, PC_TECH(tn - 1).effect,
                                        PC_TECH(tn - 1).effectChance);
                            }
                        }
                        fprintf(stderr, "; actions:");
                        for (k = 0; k < 4; k++) {
                            fprintf(stderr, " [t%d c%d a%d]", table[i].actions[k].target,
                                    table[i].actions[k].condition, table[i].actions[k].conditionArg);
                        }
                        fprintf(stderr, "\n");
                    }
                    break;
                }
            }
            if (table[i].id == 0) {
                fprintf(stderr, "battle-trace: no battle table entry with nameId %d\n", traceEnemy);
            }
        }
        if (traceSeed >= 0) {
            RANDOM.seed(traceSeed);
        }
        traceTurn = 0;
        traceParalyzed = 0;
    }
    origRequestMode(mode, arg);
}

/* Wraps the battle's event pop and paralysis roll. Saves the current functions as the originals
   each time, and never wraps its own wrapper. */
static void traceWrapBattle(StateScope *scope, void *user) {
    if (FIGHTSTG_events.funcs.pop != wrapPop) {
        origPop = FIGHTSTG_events.funcs.pop;
        FIGHTSTG_events.funcs.pop = wrapPop;
    }
    if (FIGHTSTG_battleFuncs.testParalysis != wrapParalysis) {
        origParalysis = FIGHTSTG_battleFuncs.testParalysis;
        FIGHTSTG_battleFuncs.testParalysis = wrapParalysis;
    }
}

static void traceInstall(void) {
    const char *s;

    traceInit = 1;
    traceLog = envInt("DW3_BATTLE_EVENT_LOG", 0) || plogOn("battle", PLOG_TRACE);
    traceLead = envInt("DW3_BATTLE_LEAD", -1);
    traceEnemy = envInt("DW3_BATTLE_ENEMY", -1);
    traceEnemyLevel = envInt("DW3_BATTLE_ENEMY_LEVEL", 5);
    traceEnemyHp = envInt("DW3_BATTLE_ENEMY_HP", -1);
    traceEnemyStrength = envInt("DW3_BATTLE_ENEMY_STRENGTH", 16);
    traceSeed = envInt("DW3_BATTLE_SEED", -1);
    traceForm = envInt("DW3_BATTLE_FORM", -1);
    traceParalyze = envInt("DW3_BATTLE_PARALYZE", -1);
    s = getenv("DW3_BATTLE_SCRIPT");
    while (s != NULL && *s != '\0' && traceScriptCount < TRACE_SCRIPT_MAX) {
        char c = *s;

        traceScriptNames[traceScriptCount] = -1;
        if (c == 'd') {
            while (*s != '\0' && *s != ',' && *s != ':') {
                s++;
            }
            if (*s == ':') {
                traceScriptNames[traceScriptCount] = (int)strtol(s + 1, (char **)&s, 0);
            }
        }
        traceScript[traceScriptCount++] = c;
        while (*s != '\0' && *s != ',') {
            s++;
        }
        if (*s == ',') {
            s++;
        }
    }
    if (traceLog || traceLead >= 0 || traceEnemy >= 0 || traceSeed >= 0 || traceForm >= 0 || getenv("DW3_BATTLE_PARTY_HP") != NULL) {
        origRequestMode = GAME.funcs.requestMode;
        GAME.funcs.requestMode = wrapRequestMode;
        traceWrapped = 1;
    }
    if (traceLog) {
        StateScope *battle = stateScopeFind("fightstg");

        /* The battle's statics go back to their initial values on every entry, which undoes the
           wrappers: install them again whenever the scope is entered (and now, if a battle is
           already running when the trace arms). */
        if (battle != NULL) {
            stateScopeOnEnter(battle, traceWrapBattle, NULL);
            if (stateScopeIsActive(battle)) {
                traceWrapBattle(battle, NULL);
            }
        } else {
            traceWrapBattle(NULL, NULL);
        }
    }
}

/* The scripted player: answers the command menu (and the digivolve menu it opens) by writing the
   result the menu would have written, the way the player's pick does (player_turn.c). The turn
   count advances when the command menu is seen waiting after having been answered. */
static void traceScriptStep(void) {
    PlayerTurn *turn;
    PlayerTurnChild *kids;
    char cmd;

    if (traceScriptCount == 0 || GAME.funcs.getMode() != MODE_BATTLE) {
        return;
    }
    turn = TASK_REGISTRY.funcs.find(BATTLE_TASK_PLAYER_TURN, -1, -1);
    if (turn == NULL || turn->substate == 0 || turn->result != -1) {
        return;
    }
    kids = (PlayerTurnChild *)turn->children;
    cmd = traceScript[traceTurn < traceScriptCount ? traceTurn : traceScriptCount - 1];
    if (turn->step == 0 && turn->counter == 1) {
        if (kids[1].task == NULL) {
            return;
        }
        turn->result = cmd == 'd' ? BATTLE_COMMAND_DIGIVOLVE : BATTLE_COMMAND_ATTACK;
        fprintf(stderr, "battle-trace: f=%d script: player turn %d command %s\n", (int)(GFX.frameCounter >> 8),
                traceTurn + 1, cmd == 'd' ? "DIGIVOLVE" : "ATTACK");
        if (cmd != 'd') {
            traceTurn++;
        }
    } else if (turn->step == 1 && turn->counter == 1 && cmd == 'd' && kids[3].task != NULL) {
        DigivolveMenu *menu = kids[3].digivolveMenu;
        int want = traceScriptNames[traceTurn < traceScriptCount ? traceTurn : traceScriptCount - 1];
        int i, pick = -1;

        for (i = 0; i < menu->count && i < 4; i++) {
            int id = menu->ids[i];

            if (i > 0 && pick < 0 && (want < 0 || GET_DIGIMON(id)->nameId == want)) {
                pick = id;
            }
        }
        if (pick >= 0) {
            turn->result = pick;
            fprintf(stderr, "battle-trace: script: digivolve into digimon %d\n", pick);
            traceTurn++;
        }
    }
}

void battleTraceFrame(void) {
    if (traceInit == 0) {
        traceInstall();
    }
    if (traceScriptCount != 0) {
        traceScriptStep();
    }
}
