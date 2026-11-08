#ifndef PC_SCRIPT_MIRROR_H
#define PC_SCRIPT_MIRROR_H

/*
 * Battle scripts for the other side (P17a). A fighter's technique animation is a script of 16-bit commands in its effects archive
 * (src/battle/battle_mode/battle_script.c); an enemy's is written for a caster at +z that looks at -z and a partner's for one at -z
 * that looks at +z. scriptConvert turns one for the other side, so a partner form can play an enemy's own choreography (or the
 * other way round). Nothing here touches the game: it reads and writes bytes (little-endian s16 words).
 *
 * What changes (the table is also docs/formats.md, "Battle scripts"):
 *   - model commands (2): the model TYPE swaps sides (1-3 the partner's slots <-> 4-6 the enemy's; 0, the script's own, stays); a
 *     move (cmd 3) mirrors z and, for the caster's own model, moves with its place; a turn (cmd 4) mirrors the yaw, keeps the
 *     pitch and negates the roll. Jumps (cmd 5) and the walk to the other side (cmd 8) need nothing: the game mirrors them.
 *   - sprite effects (3, mode 0) and effect models (6, mode 0): z mirrors and y/z move with the caster's place for a position on
 *     the caster's half of the arena; one on the other half only mirrors. Positions with z 0x7FFF or -1 are screen positions and
 *     stay. An effect model's rotation follows the turn rule.
 *   - camera (5), views by id (mode 0): 1-3 <-> 5-7 (a partner's cameras 8-10 look at it from the middle, an enemy's 0-2 look at it
 *     from the middle: the same shot of the other fighter); 0 stays; 4 (a partner's top view) has no counterpart.
 *   - a camera with a view of its own (mode 1) cannot be mirrored.
 *   - hits, stage, sound, fade and wait commands do not depend on the side.
 */

#include <stddef.h>

/* where a script's caster stands: its fighter row's height and distance (FighterInfo.height, FighterInfo.distance) */
typedef struct ScriptPlace {
    int height;
    int distance;
} ScriptPlace;

/* the length in bytes of the script at `in` (up to and including its closing command), or -1 when it is not a script of known commands
   that ends inside `avail` bytes (`why` says what) */
int scriptLength(const unsigned char *in, size_t avail, char *why, size_t whyCap);

/* the script at `in` for a caster of the side `toEnemy` standing at `to`, written into `out` (`outCap` bytes, at least scriptLength's);
   the same side copies it as it is. Returns the length in bytes, or -1 with `why` set (one line: the command and what it cannot do) */
int scriptConvert(const unsigned char *in, size_t avail, int fromEnemy, ScriptPlace from, int toEnemy, ScriptPlace to, unsigned char *out,
                  size_t outCap, char *why, size_t whyCap);

#endif
