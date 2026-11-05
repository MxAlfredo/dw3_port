#ifndef PC_STATE_SCOPE_H
#define PC_STATE_SCOPE_H

/*
 * State scopes (host side): named groups of writable memory that the port resets when the game
 * enters the mode or sub-overlay that owns them.
 *
 * The PlayStation copies each mode's overlay from the disc on entry, so its variables start from
 * their initial values every time (src/engine/system/overlay.c). The port links everything into
 * one binary, so nothing resets them. A scope brings that back as a port feature instead of a
 * mechanical reload: it is a list of byte ranges, an optional parent, and hooks that host shims
 * and mods can register. The original only decides WHEN a scope resets (see stateScopeModeEntered
 * and stateScopeSubLoaded); what happens on entry is the port's own.
 *
 * Contract:
 *  - stateScopeAddRange snapshots the range at that moment. Call it after the C runtime is up
 *    (initializers run, PIE relocations applied); the bytes come from memory, never from the file.
 *  - stateScopeEnter(s): the onExit hooks of s and of its active descendants run (children first),
 *    then s and every descendant are restored to their snapshots, then s becomes active and its
 *    onEnter hooks run, in registration order. Entering an active scope re-enters it.
 *  - stateScopeLeave(s): onExit hooks of s and its active descendants, s becomes inactive.
 *  - Tables map a mode id (mode >> 8) and a sub-overlay file id to scopes. They grow as needed.
 *  - stateScopeModeEntered(mode): a mode scope resets only when mode >> 8 changes from the previous
 *    mode entry (field -> field with the same id does not reset). Entering a mode forgets the
 *    current sub-overlay, so its scope resets on the next load.
 *  - stateScopeSubLoaded(file): a sub scope resets only when the file id changes.
 */

#include <stddef.h>

typedef struct StateScope StateScope;
typedef void (*StateScopeHook)(StateScope *scope, void *user);

/* A new scope (parent may be NULL). The name must outlive the scope; it is how hooks find it. */
StateScope *stateScopeCreate(const char *name, StateScope *parent);
StateScope *stateScopeFind(const char *name);
const char *stateScopeName(const StateScope *scope);
int stateScopeIsActive(const StateScope *scope);

/* Adds [begin, end) to the scope and snapshots it. Returns 0 on success, -1 on a bad range or no memory. */
int stateScopeAddRange(StateScope *scope, void *begin, void *end);

/* Registers a hook; hooks run in registration order. Return 0 on success, -1 on no memory. */
int stateScopeOnEnter(StateScope *scope, StateScopeHook hook, void *user);
int stateScopeOnExit(StateScope *scope, StateScopeHook hook, void *user);

/* A range the reset leaves alone when the scope is entered by stateScopeModeEntered from the mode id `fromModeId` (at most 8 per scope): state
 * the original's overlay keeps from one mode to the next because that change loads no overlay, where the port's modes 0x2xx and 0x3xx share one
 * overlay but reset like two. Returns 0, or -1 on a bad range, no memory or a ninth carry. */
int stateScopeCarry(StateScope *scope, void *begin, size_t size, int fromModeId);

void stateScopeEnter(StateScope *scope);
void stateScopeLeave(StateScope *scope);

/* Tables: the scope a mode id (mode >> 8) or a sub-overlay file id enters. Rebinding replaces. */
int stateScopeBindMode(int modeId, StateScope *scope);
int stateScopeBindSub(int file, StateScope *scope);

/* The game's overlay loading rules, as bookkeeping the host calls on every mode entry and sub load. */
void stateScopeModeEntered(int mode);
void stateScopeSubLoaded(int file);

/* A named range with an opaque key, the shape of the tables the build generates from the linker
   sections (build/stage_scopes.c: one entry per field stage, key = its startStage function, ended by a
   NULL name). The host walks one to create and bind scopes without a hand-written list. */
typedef struct StateScopeRange {
    const char *name;
    const void *key;
    char *begin;
    char *end;
} StateScopeRange;

/* Drops every scope, hook and table (tests, and a host that wants to start over). */
void stateScopeClear(void);

#endif
