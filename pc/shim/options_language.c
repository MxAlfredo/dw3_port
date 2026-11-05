/* The options menu's Language row (feature controls-config, O3); see options_language.h */
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "controls_screen.h"
#include "mod_stages.h"
#include "options_language.h"

static int choice_ = OPT_LANG_NONE;
static int pending_;
static int ready_; /* the row exists: later changes come from the player */
static int offered_; /* the row offers es-419 */

/* The mode numbers of include/engine/game_state.h (this unit has no game dependency) */
enum { LANG_MODE_TITLE = 0xE00, LANG_MODE_COUNTRY_SELECT = 0x1600 };

int optLangGameValue(int choice) {
    switch (choice) {
    case OPT_LANG_EN: return 2;
    case OPT_LANG_FR: return 3;
    case OPT_LANG_DE: return 5;
    case OPT_LANG_ES: return 6;
    case OPT_LANG_IT: return 4;
    case OPT_LANG_ES419: return 6;
    default: return -1;
    }
}

int optLangVariant(int choice) {
    return choice == OPT_LANG_ES419;
}

int optLangOffered(int available) {
    return available != 0;
}

int optLangEffective(int choice, int offered) {
    return choice == OPT_LANG_ES419 && !offered ? OPT_LANG_ES : choice;
}

int optLangLane(int inCountrySelect, int changePending, int entered, int bootDone, int choice, int scripted) {
    if (optLangGameValue(choice) < 0) {
        return OPT_LANG_LANE_NONE;
    }
    if (!inCountrySelect || changePending) {
        return bootDone ? OPT_LANG_LANE_NONE : OPT_LANG_LANE_SET;
    }
    if (!entered) {
        return OPT_LANG_LANE_NONE;
    }
    return optLangSkipsMenu(choice, scripted) ? OPT_LANG_LANE_SET_SKIP : OPT_LANG_LANE_SET;
}

int optLangFromGameValue(int language) {
    int c;

    for (c = OPT_LANG_EN; c < OPT_LANG_ES419; c++) { /* es-419 shares LANGUAGE 6: the retail Espanol answers */
        if (optLangGameValue(c) == language) {
            return c;
        }
    }
    return OPT_LANG_NONE;
}

int optLangSkipsMenu(int choice, int scripted) {
    return optLangGameValue(choice) >= 0 && !scripted;
}

int optLangOpening(int language) {
    return language == 0 ? 1 : 2;
}

/* 1 when a locale's region (after the language code and one '_' or '-') is Latin American Spanish */
static int latamRegion(const char *locale) {
    static const char *const regions[] = {"419", "MX", "AR", "CO", "CL", "PE", "VE", "EC", "GT", "CU", "BO",
                                          "DO", "HN", "PY", "SV", "NI", "CR", "PA", "UY", "PR", "US"};
    size_t i, n;

    if (locale[2] != '_' && locale[2] != '-') {
        return 0;
    }
    for (n = 0; isalnum((unsigned char)locale[3 + n]); n++) {
    }
    for (i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
        if (n == strlen(regions[i]) && SDL_strncasecmp(locale + 3, regions[i], n) == 0) {
            return 1;
        }
    }
    return 0;
}

int optLangFromLocale(const char *language) {
    static const char codes[] = "en\0fr\0de\0es\0it";
    static const int choices[] = {OPT_LANG_EN, OPT_LANG_FR, OPT_LANG_DE, OPT_LANG_ES, OPT_LANG_IT};
    int i;

    if (language == NULL || !isalpha((unsigned char)language[0]) || !isalpha((unsigned char)language[1]) ||
        isalpha((unsigned char)language[2])) {
        return OPT_LANG_NONE;
    }
    for (i = 0; i < 5; i++) {
        if (tolower((unsigned char)language[0]) == codes[i * 3] && tolower((unsigned char)language[1]) == codes[i * 3 + 1]) {
            return choices[i] == OPT_LANG_ES && latamRegion(language) ? OPT_LANG_ES419 : choices[i];
        }
    }
    return OPT_LANG_NONE;
}

int optLangFirstRun(const char *const *languages, int n) {
    int i, c;

    for (i = 0; languages != NULL && i < n; i++) {
        c = optLangFromLocale(languages[i]);
        if (c != OPT_LANG_NONE) {
            return c;
        }
    }
    return OPT_LANG_EN;
}

int optLangChoice(void) {
    return choice_;
}

int optLangEs419Offered(void) {
    return offered_;
}

/* The row's value v is the choice v + 1 */
static void applyLanguage(int value) {
    choice_ = value + 1;
    if (ready_) {
        pending_ = 1;
    }
}

int optLangLive(int pending, int choice, int language, int variant, int mode, int fieldIdle, int menuOpen) {
    int target = optLangGameValue(choice);

    if (!pending || target < 0 || (target == language && optLangVariant(choice) == variant) || mode == LANG_MODE_COUNTRY_SELECT) {
        return OPT_LANG_LIVE_NONE;
    }
    if (menuOpen) {
        return OPT_LANG_LIVE_WAIT;
    }
    if (mode == LANG_MODE_TITLE) {
        return OPT_LANG_LIVE_TITLE;
    }
    /* the field's modes are the stage numbers, 0x2xx, and 0x3xx for the stages mods add */
    return MS_IS_FIELD_MODE(mode) && fieldIdle ? OPT_LANG_LIVE_FIELD : OPT_LANG_LIVE_WAIT;
}

int optLangShiftFile(int file, int from, int to) {
    return file == 0 ? 0 : file + to - from;
}

int optLangPending(void) {
    return pending_;
}

void optLangClearPending(void) {
    pending_ = 0;
}

int optLangSystem(void) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    const char *codes[16];
    char tags[16][24]; /* "es_MX": the language and the country, when there is one */
    int n = 0, choice;
    SDL_Locale *list = SDL_GetPreferredLocales(), *it;

    for (it = list; it != NULL && it->language != NULL && n < 16; it++) {
        snprintf(tags[n], sizeof(tags[n]), it->country != NULL ? "%.8s_%.8s" : "%.8s", it->language, it->country != NULL ? it->country : "");
        codes[n] = tags[n];
        n++;
    }
    choice = optLangFirstRun(codes, n);
    if (n > 0) {
        printf("pad: system language %s -> first-run choice %d\n", codes[0], optLangGameValue(choice));
    }
    SDL_free(list);
    return choice;
#else
    return OPT_LANG_EN;
#endif
}

void optionsLanguageInit(int firstRun, int es419Available) {
    /* The font draws no accents: the names (and the file's values) are plain ASCII */
    static const char *const names[OPT_LANG_COUNT - 1] = {"English", "Francais", "Deutsch", "Espanol", "Italiano", "Espanol Latam"};
    static const char *const tokens[OPT_LANG_COUNT - 1] = {"english", "francais", "deutsch", "espanol", "italiano", "es419"};
    /* the switch of the preview builds: read and dropped without a word */
    CsWidget retired = {.kind = CS_ROW_TOGGLE, .key = "es419_preview", .hidden = 1, .retired = 1, .label = "es-419 preview"};
    CsWidget w = {.kind = CS_ROW_CYCLE, .label = "Game language", .key = "game", .names = names, .tokens = tokens,
                  .count = OPT_LANG_COUNT - 2, .apply = applyLanguage, .obsolete = "ask",
                  .alias = "es419", .aliasValue = OPT_LANG_ES - 1,
                  .desc = "Applies when you close this menu."};

    csWidgetAdd(CS_TAB_LANGUAGE, &retired);
    offered_ = optLangOffered(es419Available);
    if (!offered_) {
        fprintf(stderr, "settings: [language] the es-419 data cannot be built (needs the overlay folder and the disc); Espanol (Latam) is not offered\n");
    }
    if (offered_) {
        w.count = OPT_LANG_COUNT - 1; /* the sixth value is es-419 and the alias is not needed */
        w.alias = NULL;
        w.comment = "Game language: english, francais, deutsch, espanol, italiano or es419. The first run uses the system language.";
    } else {
        w.comment = "Game language: english, francais, deutsch, espanol or italiano. The first run uses the system language.";
    }
    if (firstRun < OPT_LANG_EN || firstRun >= OPT_LANG_COUNT) {
        firstRun = OPT_LANG_EN;
    }
    firstRun = optLangEffective(firstRun, offered_); /* a Latin American locale without the es-419 data */
    choice_ = firstRun; /* a file without a valid value keeps this */
    w.value = firstRun - 1;
    csWidgetAdd(CS_TAB_LANGUAGE, &w);
    pending_ = 0;
    ready_ = 1;
}
