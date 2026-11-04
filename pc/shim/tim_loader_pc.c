/*
 * The port's copy of src/engine/gfx/tim_loader.c (feature menu-art-png T3). Keep it in step with the
 * game's file: the only differences are the two substitution hooks marked PORT below, which swap in the
 * image a mod or the user edited (shim/image_data.c) just before the TIM goes to VRAM. The game's own
 * file stays untouched and is what the PS1 build compiles.
 */
#include "engine/game.h"
#include <libgs.h>
#include <libetc.h>
#include <libsnd.h>

#include "image_data.h"

/* PORT: where does `p` sit in the file cache? The disc sector of the file and the byte offset in it. */
static int locateInCache(const void *p, long *lba, size_t *off) {
    FileSlot *slot = FILE_CACHE.slots;
    s32 i;

    for (i = 0; i < FILE_CACHE_SLOTS; i++, slot++) {
        if (slot->file != 0 && slot->state == FILE_LOADED && slot->data != NULL) {
            size_t size = (size_t)FILE_TABLE.getSectorCount(slot->file) * CD_SECTOR_SIZE;

            if ((const u8 *)p >= (const u8 *)slot->data && (size_t)((const u8 *)p - (const u8 *)slot->data) < size) {
                *lba = FILE_TABLE.getSector(slot->file);
                *off = (size_t)((const u8 *)p - (const u8 *)slot->data);
                return 1;
            }
        }
    }
    return 0;
}

/* PORT: the substituted TIM for the image stored at `p` in the file cache, or `p` itself */
static void *substituted(void *p) {
    long lba;
    size_t off, len;
    const u8 *over;

    if (locateInCache(p, &lba, &off) && (over = imgOverrideTim(lba, off, &len)) != NULL) {
        return (void *)over;
    }
    return p;
}

/* Small variables, addressed through $gp (see the Makefile) */
static TimLoader *TIM_LOADER;

/* Makes `obj` the TIM loader the methods work on */
void bindTimLoader(TimLoader *obj) {
    TIM_LOADER = obj;
}

/* TIM loader method: where in VRAM the next image goes */
void timLoaderSetImagePos(s32 x, s32 y) {
    TIM_LOADER->imageX = x;
    TIM_LOADER->imageY = y;
}

/* TIM loader method: where in VRAM the next CLUT goes */
void timLoaderSetClutPos(s32 x, s32 y) {
    TIM_LOADER->clutX = x;
    TIM_LOADER->clutY = y;
}

static void loadTim(void *tim);

/* TIM loader method: sends a TIM's CLUT (4 and 8-bit images) and image to VRAM */
void timLoaderLoad(void *tim) {
    loadTim(substituted(tim)); /* PORT */
}

static void loadTim(void *tim) {
    RECT clut;
    RECT image;
    TimCursor p;
    s32 flag;
    s32 mode;
    s32 hasClut;

    p.word = tim;
    p.word++;
    flag = *p.word++;
    hasClut = flag & 8;
    mode = flag & 7;
    if (hasClut) {
        switch (mode) {
        case 0:
        case 1:
            clut.x = TIM_LOADER->clutX;
            clut.y = TIM_LOADER->clutY;
            clut.w = p.block->w;
            clut.h = p.block->h;
            LoadImage(&clut, p.block->pixels);
            break;
        }
        p.byte += p.block->size;
    }
    image.x = TIM_LOADER->imageX;
    image.y = TIM_LOADER->imageY;
    image.w = p.block->w;
    image.h = p.block->h;
    LoadImage(&image, p.block->pixels);
    TIM_LOADER->w = image.w;
    TIM_LOADER->h = image.h;
}

/*
 * TIM loader method: loads every TIM of an archive side by side, 0x40 halfwords apart, unpacking
 * the RLEN ones
 */
void timLoaderLoadArchive(void *archive) {
    u8 *buf = HEAP.alloc(TIM_LOADER->bufferSize, MEM_MODE);
    s32 i;
    s32 compressed;
    u8 *data;
    u8 *src;
    u8 *dst;
    s32 c;
    s32 n;
    s32 k;

    for (i = 0;; i++) {
        data = FILE_CACHE.getArchiveEntry(i, archive);
        /* the table ends with an offset of 0: the entry is the archive itself */
        if (data == archive) {
            break;
        }
        /* PORT: an image a mod or the user replaced goes up as it is, whatever its RLEN packing was */
        if (substituted(data) != data) {
            loadTim(substituted(data));
            DrawSync(0);
            TIM_LOADER->imageX += 0x40;
            continue;
        }
        src = data;
        compressed = *(u32 *)src == RLEN_MAGIC; /* the entry's first word */
        dst = data;
        if (compressed) {
            dst = buf;
            src += 8;
            while ((c = *src) != 0) {
                if (c & 0x80) {
                    n = c & 0x7F;
                    src++;
                    for (k = 0; k < n; k++) {
                        *dst++ = *src;
                    }
                    src++;
                } else {
                    n = *src++;
                    for (k = 0; k < n; k++) {
                        *dst++ = *src++;
                    }
                }
            }
            dst = buf;
        }
        loadTim(dst);
        DrawSync(0);
        TIM_LOADER->imageX += 0x40;
    }
    HEAP.free(buf);
}

/* TIM loader method: the buffer that RLEN TIMs are unpacked to */
void timLoaderSetBufferSize(s32 size) {
    TIM_LOADER->bufferSize = size;
}

/* Clears a TIM loader, gives it its methods and binds it */
void initTimLoader(TimLoader *obj) {
    HEAP.zero(obj, sizeof(TimLoader));
    obj->load = timLoaderLoad;
    obj->setClutPos = timLoaderSetClutPos;
    obj->setImagePos = timLoaderSetImagePos;
    obj->bind = bindTimLoader;
    obj->loadArchive = timLoaderLoadArchive;
    obj->setBufferSize = timLoaderSetBufferSize;
    bindTimLoader(obj);
    obj->bufferSize = 0xA800;
}
