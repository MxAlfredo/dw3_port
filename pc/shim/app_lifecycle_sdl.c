/*
 * App lifecycle, SDL side (Android): SDL delivers SDL_APP_* events from the UI thread through the event watch, so the
 * freeze happens at once, even while the game thread is blocked by SDL's own pause handling (SDL_HINT_ANDROID_BLOCK_ON_PAUSE
 * stops the thread that pumps events, but not PsyCross' vblank thread nor OpenAL Soft's mixer thread: they kept
 * running and the music went on). Pure rules: shim/app_lifecycle.c.
 */
#include <SDL.h>
#include <stdio.h>

#include <AL/al.h>
#include <AL/alc.h>


#include "shim/app_lifecycle.h"
#include "shim/app_lifecycle_sdl.h"
extern void inputCapFrame(void); /* shim/input_cfg.h, not included: it needs the PsyQ types */

#ifdef __ANDROID__
#include <android/log.h>
#define LIFE_LOG(...) __android_log_print(ANDROID_LOG_INFO, "dw3", __VA_ARGS__)
#else
#define LIFE_LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\n', stderr))
#endif

/* PsyX_public.h, not included: it needs the PsyQ types */
extern void PsyX_Sys_SetSuspended(int suspended);
extern int PsyX_Sys_GetVBlankCount(void);

static AppLife life;
static unsigned long long frozenAt;

typedef void (*PauseFn)(ALCdevice *);

static void audioPause(int pause) {
    ALCcontext *ctx = alcGetCurrentContext();
    ALCdevice *dev = ctx != NULL ? alcGetContextsDevice(ctx) : NULL;
    PauseFn fn;

    if (dev == NULL) {
        return;
    }
    fn = (PauseFn)alcGetProcAddress(dev, pause ? "alcDevicePauseSOFT" : "alcDeviceResumeSOFT");
    if (fn != NULL) {
        fn(dev);
    }
}

static int lifeWatch(void *userdata, SDL_Event *e) {
    AppLifeEvent ev;
    int act;

    (void)userdata;
    switch (e->type) {
    case SDL_APP_WILLENTERBACKGROUND: ev = APPLIFE_WILL_BACKGROUND; break;
    case SDL_APP_DIDENTERBACKGROUND: ev = APPLIFE_DID_BACKGROUND; break;
    case SDL_APP_WILLENTERFOREGROUND: ev = APPLIFE_WILL_FOREGROUND; break;
    case SDL_APP_DIDENTERFOREGROUND: ev = APPLIFE_DID_FOREGROUND; break;
    default: return 0;
    }
    act = appLifeEvent(&life, ev);
    if (act & APPLIFE_ACT_FREEZE) {
        PsyX_Sys_SetSuspended(1);
        audioPause(1);
        inputCapFrame(); /* writes settings.ini when a change is still pending */
        frozenAt = (unsigned long long)PsyX_Sys_GetVBlankCount();
        LIFE_LOG("lifecycle: background, frozen at vblank %llu (clock, audio stopped, settings saved)", frozenAt);
    }
    if (act & APPLIFE_ACT_THAW) {
        audioPause(0);
        PsyX_Sys_SetSuspended(0);
        LIFE_LOG("lifecycle: foreground, resumed at vblank %llu (was %llu)", (unsigned long long)PsyX_Sys_GetVBlankCount(),
                 frozenAt);
    }
    return 0;
}

void appLifecycleInit(void) {
    static int inited;

    if (!inited) {
        inited = 1;
        appLifeInit(&life);
        SDL_AddEventWatch(lifeWatch, NULL);
    }
}
