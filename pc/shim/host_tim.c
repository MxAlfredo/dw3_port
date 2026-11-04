/*
 * Host shim for the TIM loader's constructor (initTimLoader, in the game's
 * src/engine/gfx/tim_loader.c).
 *
 * font.c defines loadFont, which wants initTimLoader and the file cache; the
 * host only needs FONT and decodeChar out of font.c, and loadFont is never
 * called here: it loads the font's TIM into VRAM (which the host does not
 * have) and asks FILE_CACHE.getEntry for the font archive (getFileEntry casts
 * a pointer to 32 bits, which truncates on x86-64; see pc/README.md). The
 * loader's constructor is therefore a stub: it only clears the object, so a
 * later loadFont call is a visible crash (a NULL method) rather than silence.
 *
 * The game's tim_loader.c would not link here anyway: its load and
 * loadArchive methods call libgpu's LoadImage and DrawSync, which the host
 * does not have.
 */
#include "engine/gfx.h"

#include <string.h>

void initTimLoader(TimLoader *obj) {
    memset(obj, 0, sizeof(TimLoader));
}
