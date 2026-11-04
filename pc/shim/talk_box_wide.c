/*
 * L4b: the wider talk box (see talk_box_wide.h). speech.c is compiled with
 * -DcreateTalkBox=pcCreateTalkBox (pc/Makefile), so the field's speech reaches this wrapper; the
 * real createTalkBox (src/engine/text/talk_box.c, untouched) runs first and sizes the box to
 * 95..139 px. With a wider maximum chosen the wrapper measures the page again, widens the box
 * (task->w is what the frame, the panel, the zoom outlines and the arrow read) and keeps it on the
 * screen. With the retail maximum (the default) it returns the real box as it is.
 */
#define TBW_GAME_UNIT 1
#include "engine/game.h"
#include "talk_box_wide.h"

/* text_data.c (the host's headers cannot be mixed with the game's here) */
extern int tdTalkBoxWidth(void);
extern void tdTalkBoxLog(int type, int x, int y, int w, int h);
extern int tdTalkBoxFlip(void);

#undef createTalkBox /* never defined here; the real function is the one the game links */

static void (*realSetPos)(TalkBox *box, s32 x, s32 y);

static int flipShift; /* DW3_TALK_BOX_FLIP: the speech's own position is for a left box, the box is a right one */

static void wideSetPos(TalkBox *task, s32 x, s32 y)
{
    x -= flipShift;
    realSetPos(task, tbwClampX(task->type, x, task->w), y);
}

TalkBox *pcCreateTalkBox(s32 id, s16 x, s16 y, void *strings, s32 index, u32 type)
{
    TalkBox *task;
    int max = tdTalkBoxWidth();
    int flip = tdTalkBoxFlip() && type < 2; /* a capture aid: the same speaker's box as a right-growing one (type + 2) */

    /* the speech places type 2/3 boxes 22 px left of where it places 0/1 ones (speech.c) */
    flipShift = flip ? 22 : 0;
    task = createTalkBox(id, (s16)(x - flipShift), y, strings, index, flip ? type + 2 : type);
    if ((max > TBW_ORIGINAL || flip) && task != NULL) {
        TalkBoxChildren *children = task->children;
        TextTools fn;
        int measured;

        if (max > TBW_ORIGINAL) {
            initTextTools(&fn);
            measured = fn.measure(children->windows[1]->text, children->windows[1]->style, 0);
            task->w = (s16)tbwBoxWidth(measured, max);
        }
        realSetPos = task->setPos;
        task->setPos = wideSetPos;
        task->setPos(task, task->x + flipShift, task->y); /* the windows follow the new width; the box stays on screen */
    }
    if (task != NULL) {
        tdTalkBoxLog((int)task->type, (int)task->x, (int)task->y, (int)task->w, (int)task->h);
    }
    return task;
}
