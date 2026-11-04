#ifndef PC_BATTLE_SPEED_H
#define PC_BATTLE_SPEED_H

/*
 * Battle speed (feature battle-speed): the port runs the game's battle clock faster while
 * attacks and techniques play. This is the pure part: the setting and the scaled step.
 * The game side (the gate, and the clock it scales) is in gfx_engine.c's hostDrawFrame.
 */

enum { BATTLE_SPEED_NORMAL = 0, BATTLE_SPEED_X15, BATTLE_SPEED_X2, BATTLE_SPEED_MODES };

/* The clock step (8.8 fixed point, 0x100 = one vsync) for this frame: `baseStep` times the
   mode's factor (1, 1.5, 2) when `gate` is nonzero, else `baseStep` unchanged. */
int battleSpeedStep(int mode, int gate, int baseStep);

/* The setting the Display row writes and the game side reads; a bad mode is Normal */
void battleSpeedSetMode(int mode);
int battleSpeedMode(void);

#endif
