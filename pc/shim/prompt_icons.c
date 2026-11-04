/* Which atlas cell shows a button or key (feature controls-config, C4); see prompt_icons.h. */
#include "prompt_icons.h"

#include <SDL.h>

int promptIconPad(int family, int button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        return family == INPUT_FAMILY_XBOX ? PI_XB_A : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_CROSS : PI_SW_A);
    case SDL_CONTROLLER_BUTTON_B:
        return family == INPUT_FAMILY_XBOX ? PI_XB_B : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_CIRCLE : PI_SW_B);
    case SDL_CONTROLLER_BUTTON_X:
        return family == INPUT_FAMILY_XBOX ? PI_XB_X : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_SQUARE : PI_SW_X);
    case SDL_CONTROLLER_BUTTON_Y:
        return family == INPUT_FAMILY_XBOX ? PI_XB_Y : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_TRIANGLE : PI_SW_Y);
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return PI_DPAD_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return PI_DPAD_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return PI_DPAD_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return PI_DPAD_RIGHT;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return family == INPUT_FAMILY_XBOX ? PI_XB_LB : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_L1 : PI_SW_L);
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return family == INPUT_FAMILY_XBOX ? PI_XB_RB : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_R1 : PI_SW_R);
    case SDL_CONTROLLER_BUTTON_START:
        return family == INPUT_FAMILY_XBOX ? PI_XB_MENU : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_OPTIONS
                                                          : (family == INPUT_FAMILY_SWITCH ? PI_SW_PLUS : PI_NONE));
    case SDL_CONTROLLER_BUTTON_BACK:
        return family == INPUT_FAMILY_XBOX ? PI_XB_VIEW : (family == INPUT_FAMILY_PLAYSTATION ? PI_PS_SELECT
                                                          : (family == INPUT_FAMILY_SWITCH ? PI_SW_MINUS : PI_NONE));
    default:
        return PI_NONE;
    }
}

int promptIconKey(int scancode) {
    switch (scancode) {
    case SDL_SCANCODE_ESCAPE:
        return PI_KEY_ESC;
    case SDL_SCANCODE_F1:
        return PI_KEY_F1;
    case SDL_SCANCODE_Q:
        return PI_KEY_Q;
    case SDL_SCANCODE_E:
        return PI_KEY_E;
    case SDL_SCANCODE_UP:
        return PI_KEY_UP;
    case SDL_SCANCODE_DOWN:
        return PI_KEY_DOWN;
    case SDL_SCANCODE_LEFT:
        return PI_KEY_LEFT;
    case SDL_SCANCODE_RIGHT:
        return PI_KEY_RIGHT;
    default:
        return PI_NONE;
    }
}

int promptIconUV(int cell, int *u, int *v) {
    if (cell < 0 || cell >= PI_CELL_COUNT) {
        return 0;
    }
    *u = (cell % PI_COLS) * PI_CELL;
    *v = (cell / PI_COLS) * PI_CELL;
    return 1;
}

int promptIconAction(int kb, int family, int act, const char **text) {
    static const char *const kbText[PI_ACT_COUNT] = {"Q", "E", "Enter", "Esc", "F1", "Left", "Right"};
    static const char *const padText[PI_ACT_COUNT] = {"L1", "R1", "A", "B", "Select", "Left", "Right"};
    /* Enter has no icon in the pack (its key cap is two tiles wide): written out */
    static const int kbKey[PI_ACT_COUNT] = {SDL_SCANCODE_Q, SDL_SCANCODE_E, 0, SDL_SCANCODE_ESCAPE, SDL_SCANCODE_F1,
                                            SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
    static const int padButton[PI_ACT_COUNT] = {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                                                SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B,
                                                SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_DPAD_LEFT,
                                                SDL_CONTROLLER_BUTTON_DPAD_RIGHT};

    if (act < 0 || act >= PI_ACT_COUNT) {
        *text = "";
        return PI_NONE;
    }
    *text = kb ? kbText[act] : padText[act];
    if (kb) {
        return kbKey[act] != 0 ? promptIconKey(kbKey[act]) : PI_NONE;
    }
    return promptIconPad(family, padButton[act]);
}
