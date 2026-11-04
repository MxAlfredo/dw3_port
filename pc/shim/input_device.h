#ifndef PC_INPUT_DEVICE_H
#define PC_INPUT_DEVICE_H

/*
 * The last input device the player used and the controller family (feature
 * controls-config, C4), so the options menu can show the right button icons.
 *
 * An SDL event watch records the device of every key press, controller button press
 * or stick push past the dead zone: keyboard or controller, and for a controller the
 * family from SDL_GameControllerGetType (Xbox 360/One/Series, PlayStation 3/4/5,
 * Switch Pro / Joy-Con, else generic). The "Button icons" setting (Display tab,
 * `button_icons` in settings.ini) is Auto (follow the detected family) or forces one.
 * The pure parts (type -> family, setting -> family, event -> device) have no window,
 * so pc/inputtest.c drives them.
 */
#include "input_cfg.h"

enum { INPUT_FAMILY_GENERIC = 0, INPUT_FAMILY_XBOX, INPUT_FAMILY_PLAYSTATION, INPUT_FAMILY_SWITCH, INPUT_FAMILY_COUNT };

/* The "Button icons" setting: Auto, then the families in the order Xbox, PlayStation, Switch */
enum { INPUT_ICONS_AUTO = 0, INPUT_ICONS_XBOX, INPUT_ICONS_PLAYSTATION, INPUT_ICONS_SWITCH, INPUT_ICONS_COUNT };

/* The family of an SDL_GameControllerType value (an int so callers need no SDL) */
int inputFamilyFromType(int sdlControllerType);
/* The family the icons use: the setting's own when forced, else the detected one */
int inputIconFamilyFor(int setting, int detected);
const char *inputFamilyName(int family);

/* Feeds an SDL event: returns the device it shows (INPUT_DEV_KEYBOARD / INPUT_DEV_PAD),
   -1 for an event that is not a press. Records it as the last device, and for a
   controller its family. Safe from SDL's event-watch thread. */
union SDL_Event;
int inputDeviceNote(const union SDL_Event *ev);
/* The same without an event (the harness, tests); `family` is used for INPUT_DEV_PAD
   and may be -1 to keep the detected one */
void inputDeviceSet(int device, int family);

int inputDeviceCurrent(void);    /* INPUT_DEV_KEYBOARD (at boot) or INPUT_DEV_PAD */
int inputDeviceFamily(void);     /* the family of the last controller used (generic if none yet) */
int inputIconSetting(void);      /* INPUT_ICONS_* */
void inputIconSettingSet(int setting);
int inputIconFamily(void);       /* inputIconFamilyFor(setting, detected family): what to draw */

/* Installs the event watch (idempotent) */
void inputDeviceInit(void);
/* Adds the "Button icons" row to the Display tab (after inputCfgInit, like optionsVideoInit) */
void inputDeviceRowsInit(void);

#endif
