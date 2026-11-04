#ifndef PC_OPTIONS_LANGUAGE_H
#define PC_OPTIONS_LANGUAGE_H

/*
 * The options menu's Language row (feature controls-config, O3).
 *
 * [language] game = english | francais | deutsch | espanol | italiano | es419
 *
 * The language lives only here (O3c): outside the harness the game's own language menu is
 * always skipped and the game's LANGUAGE is set before the first mode. The first run (no
 * key) takes the system's language when the game has it, else English (optLangFirstRun),
 * and writes it; an old "ask" is migrated to that same default.
 * LANGUAGE is read by the text files (TEXT_FILE), the title art and the stages'
 * text file (FIELDSTG_state.textFile, set once when the stage is set up) and
 * the files are cached in FILE_CACHE under an index that includes LANGUAGE.
 * A change made in the menu is applied live once the menu is closed, at a safe
 * point (optLangLive): the field when nothing holds the player, the title by
 * restarting it; any other scene waits until the game is back in the field.
 * The mapping and the decisions below are pure (pc/inputtest.c drives them);
 * the game side lives in host_pad.c.
 *
 * es-419 (feature es419-live-option, L2c) is the sixth choice, `Espanol Latam` (the menu font has no parentheses), file token es419. It is
 * the Spanish files (LANGUAGE 6) with our overlay applied, so it is a text variant of language 6
 * (optLangVariant, tdSetVariant): a switch between Espanol and es-419 changes only the variant.
 * The owner approved it for the beta (L5): the row offers it whenever the es-419 data can be built
 * (optLangOffered); otherwise an `es419` value in the file falls back to Espanol, with a log line, and
 * the row cycles the five retail languages. The old `es419_preview` key of earlier builds is accepted and
 * dropped from the file without a word. A Latin American Spanish system locale picks es-419 on the first
 * run (optLangFromLocale); the row falls back to Espanol when the data cannot be built.
 */

enum { OPT_LANG_NONE = 0, OPT_LANG_EN, OPT_LANG_FR, OPT_LANG_DE, OPT_LANG_ES, OPT_LANG_IT, OPT_LANG_ES419, OPT_LANG_COUNT };

/* The game's LANGUAGE for a choice (2 English, 3 French, 5 German, 6 Spanish, 4 Italian),
   -1 for NONE or out of range */
int optLangGameValue(int choice);
/* The choice that sets LANGUAGE to `language`, or OPT_LANG_NONE when no row offers it (language 6 is the
   retail Espanol: es-419 never answers a bare LANGUAGE) */
int optLangFromGameValue(int language);
/* The text variant of a choice: 1 for es-419 (TD_VARIANT_ES419), else 0 (the disc's text) */
int optLangVariant(int choice);
/* 1 when the es-419 choice is offered: the es-419 data can be built */
int optLangOffered(int available);
/* The choice that applies: es-419 when it is not offered is Espanol, any other stays */
int optLangEffective(int choice, int offered);
/* 1 when the boot language menu is skipped: always, unless something scripted (keys, warp,
   mode requests) wants the menu or its own first mode */
int optLangSkipsMenu(int choice, int scripted);
/* The mode the menu's exit requests for a LANGUAGE: 1 = MODE_OPENING, 2 = MODE_OPENING_2
   (country_select.c: LANGUAGE 0 plays the first opening, any other the second) */
int optLangOpening(int language);

/* The choice for a locale ("es", "es_MX.UTF-8", "es-419", "DE"): the 2-letter language must be
   en, fr, de, es or it, case-insensitive; OPT_LANG_NONE for anything else. Spanish of Latin America
   (419, or MX AR CO CL PE VE EC GT CU BO DO HN PY SV NI CR PA UY PR US) is OPT_LANG_ES419, whether or
   not the es-419 data exists (optionsLanguageInit falls back to Espanol); es_ES and a bare es are Espanol */
int optLangFromLocale(const char *language);
/* First run: the first of `n` preferred language codes that the game has, else English */
int optLangFirstRun(const char *const *languages, int n);

/* What the language lane does this frame (padLanguageFire): `inCountrySelect` the game is in the language
   menu's mode; `changePending` a mode change was already requested (a warp's, or the title's NEW GAME);
   `entered` the first frame of a visit to the menu; `bootDone` the lane already set the language once
   this run; `scripted` keys, warp or mode requests want the menu or their own first mode.
   SET: write LANGUAGE and the text variant; SET_SKIP: also skip the menu. The first frames of a run take
   the language even when a warp's request is already pending (the warp used to swallow it); later
   pending changes (the title's NEW GAME) and the menu's own picks are left alone. */
enum { OPT_LANG_LANE_NONE = 0, OPT_LANG_LANE_SET, OPT_LANG_LANE_SET_SKIP };
int optLangLane(int inCountrySelect, int changePending, int entered, int bootDone, int choice, int scripted);

/* What to do with a language change made in the menu (live switching, O3b) */
enum { OPT_LANG_LIVE_NONE = -1, OPT_LANG_LIVE_WAIT = 0, OPT_LANG_LIVE_FIELD, OPT_LANG_LIVE_TITLE };
/* `pending`: the row changed since the last switch. `language` is the game's LANGUAGE and `variant` its
   text variant (a choice differs from the current text when either differs), `mode` the
   current game mode, `fieldIdle` 1 when no talk, event, menu or battle holds the field, `menuOpen`
   the options menu. NONE: nothing to do (clear the pending flag); WAIT: not yet (the menu is open,
   or the scene cannot refresh in place: battle, card game, shops, labs, movies); FIELD / TITLE:
   switch now, in the field / by restarting the title */
int optLangLive(int pending, int choice, int language, int variant, int mode, int fieldIdle, int menuOpen);
/* A stage's text file index (LANGUAGE + base) moved from language `from` to `to`; 0 stays 0 */
int optLangShiftFile(int file, int from, int to);
/* The row was changed by the player (not by the settings file) and is waiting for its switch */
int optLangPending(void);
void optLangClearPending(void);

/* The choice read from the settings file (OPT_LANG_NONE until the row exists) */
int optLangChoice(void);
/* 1 when the row offers es-419 (set by optionsLanguageInit) */
int optLangEs419Offered(void);

/* ---- the boot binary only ---- */

/* optLangFirstRun over the system's preferred languages (SDL_GetPreferredLocales, SDL 2.0.14+) */
int optLangSystem(void);

/* Adds the Language row (after inputCfgInit, so the file's values apply); `firstRun` is the choice for
   a file without a valid value (optLangFirstRun; es-419 becomes Espanol when it is not available);
   `es419Available` is 1 when the es-419 data can be built (the overlay and the disc's Spanish files),
   else the choice is not offered (the reason is logged) */
void optionsLanguageInit(int firstRun, int es419Available);

#endif
