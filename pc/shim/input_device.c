/*
 * The last used input device and controller family (feature controls-config, C4); see
 * input_device.h. No game dependency: SDL's event types and the options menu's rows.
 */
#include "input_device.h"

#include <SDL.h>
#include <stdlib.h>

#include "controls_screen.h"

#define STICK_ON 16000 /* a stick past this is a deliberate push, as in controls_screen.c */

static SDL_atomic_t device_; /* INPUT_DEV_KEYBOARD or INPUT_DEV_PAD */
static SDL_atomic_t family_; /* of the last controller used */
static int setting_;

int inputFamilyFromType(int type) {
    switch (type) {
    case SDL_CONTROLLER_TYPE_XBOX360:
    case SDL_CONTROLLER_TYPE_XBOXONE: /* Series pads report this too */
        return INPUT_FAMILY_XBOX;
    case SDL_CONTROLLER_TYPE_PS3:
    case SDL_CONTROLLER_TYPE_PS4:
    case SDL_CONTROLLER_TYPE_PS5:
        return INPUT_FAMILY_PLAYSTATION;
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
#if SDL_VERSION_ATLEAST(2, 0, 16)
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
    case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
#endif
        return INPUT_FAMILY_SWITCH;
    default:
        return INPUT_FAMILY_GENERIC;
    }
}

int inputIconFamilyFor(int setting, int detected) {
    switch (setting) {
    case INPUT_ICONS_XBOX:
        return INPUT_FAMILY_XBOX;
    case INPUT_ICONS_PLAYSTATION:
        return INPUT_FAMILY_PLAYSTATION;
    case INPUT_ICONS_SWITCH:
        return INPUT_FAMILY_SWITCH;
    default:
        return detected >= 0 && detected < INPUT_FAMILY_COUNT ? detected : INPUT_FAMILY_GENERIC;
    }
}

const char *inputFamilyName(int family) {
    static const char *const names[INPUT_FAMILY_COUNT] = {"generic", "xbox", "playstation", "switch"};

    return family >= 0 && family < INPUT_FAMILY_COUNT ? names[family] : "generic";
}

static int familyOfInstance(SDL_JoystickID id) {
    SDL_GameController *pad = SDL_GameControllerFromInstanceID(id);

    return pad != NULL ? inputFamilyFromType((int)SDL_GameControllerGetType(pad)) : INPUT_FAMILY_GENERIC;
}

int inputDeviceNote(const union SDL_Event *ev) {
    const SDL_Event *e = (const SDL_Event *)ev;

    switch (e->type) {
    case SDL_KEYDOWN:
        SDL_AtomicSet(&device_, INPUT_DEV_KEYBOARD);
        return INPUT_DEV_KEYBOARD;
    case SDL_CONTROLLERBUTTONDOWN:
        SDL_AtomicSet(&family_, familyOfInstance(e->cbutton.which));
        SDL_AtomicSet(&device_, INPUT_DEV_PAD);
        return INPUT_DEV_PAD;
    case SDL_CONTROLLERAXISMOTION:
        if (abs(e->caxis.value) > STICK_ON && e->caxis.axis < SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            SDL_AtomicSet(&family_, familyOfInstance(e->caxis.which));
            SDL_AtomicSet(&device_, INPUT_DEV_PAD);
            return INPUT_DEV_PAD;
        }
        return -1;
    default:
        return -1;
    }
}

void inputDeviceSet(int device, int family) {
    if (device == INPUT_DEV_KEYBOARD || device == INPUT_DEV_PAD) {
        SDL_AtomicSet(&device_, device);
    }
    if (family >= 0 && family < INPUT_FAMILY_COUNT) {
        SDL_AtomicSet(&family_, family);
    }
}

int inputDeviceCurrent(void) { return SDL_AtomicGet(&device_); }
int inputDeviceFamily(void) { return SDL_AtomicGet(&family_); }
int inputIconSetting(void) { return setting_; }

void inputIconSettingSet(int setting) {
    setting_ = setting >= 0 && setting < INPUT_ICONS_COUNT ? setting : INPUT_ICONS_AUTO;
}

int inputIconFamily(void) {
    return inputIconFamilyFor(setting_, SDL_AtomicGet(&family_));
}

static int deviceWatch(void *userdata, SDL_Event *e) {
    (void)userdata;
    inputDeviceNote((const union SDL_Event *)e);
    return 0;
}

void inputDeviceInit(void) {
    static int inited;

    if (!inited) {
        inited = 1;
        SDL_AddEventWatch(deviceWatch, NULL);
    }
}

/* ---- the row ---- */

static void applyIcons(int value) {
    inputIconSettingSet(value);
}

void inputDeviceRowsInit(void) {
    static const char *const names[INPUT_ICONS_COUNT] = {"Auto", "Xbox", "PlayStation", "Switch"};
    CsWidget w = {.kind = CS_ROW_CYCLE, .label = "Button icons", .key = "button_icons", .names = names,
                  .count = INPUT_ICONS_COUNT, .apply = applyIcons,
                  .desc = "Auto follows the last pad used.",
                  .comment = "Controller button icons in the menu: follow the last pad used (auto) or force one family."};

    inputIconSettingSet(INPUT_ICONS_AUTO);
    csWidgetAdd(CS_TAB_DISPLAY, &w);
}
