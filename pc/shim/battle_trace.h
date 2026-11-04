#ifndef PC_BATTLE_TRACE_H
#define PC_BATTLE_TRACE_H

/*
 * Battle event trace and scripted player (host side, opt-in, off by default).
 * Called once per presented frame from hostDrawFrame; every part is armed by an
 * environment variable and costs one flag test when none is set.
 *
 *   DW3_BATTLE_EVENT_LOG=1   one stderr line per event popped from the battle's queue
 *                            (battle-trace:), per lost paralysis roll and per command
 *   DW3_BATTLE_SCRIPT=<list> the player's commands, one per player turn, last one repeats:
 *                            "dv" digivolve into the first form that is not the current one
 *                            (or "dv:<nameId>" for the form whose name string is nameId), "atk" attack
 *   DW3_BATTLE_LEAD=<n>      partner index put first in the party for the next battle
 *   DW3_BATTLE_ENEMY=<n>     first enemy: the battle-table entry whose nameId is n
 *   DW3_BATTLE_ENEMY_LEVEL=<n>  its level, with hp from the same ratio as the field's (default 5)
 *   DW3_BATTLE_FORM=<n>      gives the lead partner the form with name string n as its battle slot 0 (a digivolve target)
 *   DW3_BATTLE_ENEMY_HP / DW3_BATTLE_ENEMY_STRENGTH   its hp and strength (16 = 1.0, in 16ths)
 *   DW3_BATTLE_PARTY_HP=<n>  the lead partner's hp and max hp (to keep it alive through a long fight)
 *   DW3_BATTLE_PARALYZE=<n>  paralyzes the player at its first turn with strength n (checks the lost-turn path)
 *   DW3_BATTLE_SEED=<n>      RANDOM.index at the start of the battle
 */
void battleTraceFrame(void);

#endif
