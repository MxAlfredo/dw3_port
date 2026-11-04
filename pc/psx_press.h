#ifndef PC_PSX_PRESS_H
#define PC_PSX_PRESS_H

/*
 * The libpress names the game's own movie player calls that PsyCross's
 * libpress.h does not declare (T18).
 *
 * PsyCross ships a libpress.h with seven `DecDCT*` prototypes and no
 * implementation; the game's movie player (src/menus/title/movie.c) also
 * calls three that header leaves out -- DecDCTvlc2, DecDCTvlcBuild and
 * DecDCToutCallback -- with exactly the signatures Psy-Q 4.7's libpress.h
 * gives them. This header is the host-side bridge for those three, the same
 * role pc/psx_compat.h plays for the drawing names, and it is force-included
 * (-include) only for movie.o, the one unit that names them. The shim that
 * defines them is pc/shim/host_press.c.
 *
 * The types come from psx_compat.h (and, ahead of it, ps1_types.h), both
 * force-included before this file: with `u_long` 32 bits wide, as it is on the
 * PlayStation, they match the `u_int *` PsyCross's libpress.h and libcd.h use.
 */
int DecDCTvlc2(u_long *bs, u_long *buf, u_short *table);
void DecDCTvlcBuild(u_short *table);
int DecDCToutCallback(void (*func)(void));

#endif /* PC_PSX_PRESS_H */
