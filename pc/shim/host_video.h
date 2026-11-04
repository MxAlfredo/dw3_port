#ifndef HOST_VIDEO_H
#define HOST_VIDEO_H

/*
 * The port's movie presenter (T39): the frame the MDEC shim uploaded to VRAM,
 * drawn on the window as textured quads. See shim/host_video.c for why this
 * exists and how the frame is split into texture pages.
 *
 * hostVideoSetSource is called by shim/host_press.c right after it LoadImages
 * a whole movie frame into VRAM; hostVideoDisable when that frame is no longer
 * valid (DecDCTReset, the next DecDCTin); hostVideoDraw once per frame by the
 * port's own frame builder (shim/gfx_engine.c), after the layers are drawn.
 */

/* The VRAM rect holding the movie frame, in display space (the movie's display
   area is 320x480 at VRAM (0,0), so a display pixel is a VRAM pixel). */
void hostVideoSetSource(int vramX, int vramY, int w, int h);

/* Forget the frame: hostVideoDraw becomes a no-op. */
void hostVideoDisable(void);

/* Draw the frame, if one is set. A no-op when hostVideoSetSource was not
   called or hostVideoDisable came after it. */
void hostVideoDraw(void);

/* Whether a movie frame is set (the controls screen does not open over it) */
int hostVideoActive(void);

#endif /* HOST_VIDEO_H */
