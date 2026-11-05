# Host (x86-64, Linux) build of the game's own code: the RLEN unpacker
# (rlen_tool), the disc file layer (file_dump), the text engine (text_measure),
# the drawing harness (draw_order), the first frame on screen (first_frame), the
# font in VRAM (font_frame) and the game's own boot path (boot).
#
# This is not the PlayStation build and does not touch it: `make -C pc` only
# compiles a handful of the game's own translation units (src/**, unmodified)
# with the host gcc, links them against the small shim in shim/ and the
# harnesses in rlen_tool.c / file_dump.c / text_measure.c / draw_order.c /
# first_frame.c / font_frame.c / boot.c. See pc/README.md for what it proves
# and what it does not.
#
# It also drives the PsyCross static library (T11) through the `psycross` and
# `check-packets` targets: that part is a separate CMake project under
# pc/psycross/ and never patches the external/psycross submodule.
#
# GNU make: the object rules below share a target namespace, so name the
# sources distinctly.

ROOT := $(abspath $(CURDIR)/..)
CC ?= gcc
BUILD := build

# W1: TARGET=windows cross-builds boot.exe with MinGW-w64 into its own tree (build-win), PsyCross
# included (build/psycross-win, pc/win/mingw-w64.cmake); the Linux tree in build/ is untouched.
# The SDL2 and OpenAL development packages come from pc/win/fetch_deps.sh (~/.cache/dw3-win).
# Only `make -C pc TARGET=windows` (the boot.exe) is meant to work for it, not the harnesses.
# TARGET=appimage is the Linux build in its own trees (build-linux, build/psycross-linux), made inside
# the old-glibc container of pc/linux/make_appimage.sh; everything else is the Linux build's.
TARGET ?= linux
EXE :=
BOOT_NAME := boot
GL_LIB := -lGL
ifeq ($(GLES),1)
GL_LIB := -lGLESv2 -lEGL
endif
HARNESS_GL_LIB := $(GL_LIB)
ifeq ($(TARGET),windows)
HARNESS_GL_LIB := -lGL
endif
BOOT_LDFLAGS := -rdynamic
NM := nm
OBJCOPY := objcopy
ifeq ($(TARGET),appimage)
BUILD := build-linux
endif

# Android prerequisites, both opt-in and Linux-only; the default build is unchanged.
#   LINKER=lld  links the game with LLVM lld (-fuse-ld=lld, the linker of the Android NDK) instead of GNU ld.
#   GLES=1      builds PsyCross on its OpenGL ES 3 renderer (PSYX_FORCE_GLES) and links GLESv2 + EGL instead of GL.
# Each knob has its own trees (build-lld, build-gles, build-lld-gles; build/psycross-<same>), so none mixes objects.
LINKER ?= bfd
GLES ?= 0
BUILD_TAG :=
LINK_FLAGS :=
ifeq ($(LINKER),lld)
BUILD_TAG := $(BUILD_TAG)-lld
LINK_FLAGS := -fuse-ld=lld
endif
ifeq ($(GLES),1)
BUILD_TAG := $(BUILD_TAG)-gles
GLES_DEFS := -DPSYX_FORCE_GLES
else
GLES_DEFS :=
endif
ifneq ($(BUILD_TAG),)
ifeq ($(TARGET),linux)
BUILD := build$(BUILD_TAG)
endif
endif
ifeq ($(TARGET),windows)
CROSS := x86_64-w64-mingw32-
CC := $(CROSS)gcc-posix
BUILD := build-win
EXE := .exe
NM := $(CROSS)nm
OBJCOPY := $(CROSS)objcopy
DW3_WIN_CACHE ?= $(HOME)/.cache/dw3-win
WIN_INC := -I$(DW3_WIN_CACHE)/sdl2/include/SDL2 -I$(DW3_WIN_CACHE)/sdl2/include -I$(DW3_WIN_CACHE)/openal/include
# SDL_MAIN_HANDLED: the port's own main() stays a plain main (the MinGW CRT calls it for both
# __USE_MINGW_ANSI_STDIO: MinGW's own printf family at run time (msvcrt's has no %zu). -Wno-format:
# gcc's checker still uses the msvcrt archetype for printf-like functions and flags %zu; the real
# mismatches are the Linux build's to report.
WIN_DEFS := -D__USE_MINGW_ANSI_STDIO=1 -Wno-format -DSDL_MAIN_HANDLED -D_WIN32_WINNT=0x0601 -include $(CURDIR)/win/compat.h $(WIN_INC)
GL_LIB := -lopengl32
# libgcc, libstdc++ and winpthreads are linked in: the beta ships boot.exe + SDL2.dll + OpenAL32.dll only.
# W2: the player build is a GUI-subsystem program (no console window; the log goes to a file).
# CONSOLE=1 links the developer's console variant, build-win/boot-console.exe, same objects.
CONSOLE ?= 0
ifeq ($(CONSOLE),1)
WIN_SUBSYSTEM := -mconsole
BOOT_NAME := boot-console
else
WIN_SUBSYSTEM := -mwindows
BOOT_NAME := boot
endif
BOOT_LDFLAGS := $(WIN_SUBSYSTEM) -lcomdlg32 -lole32 -lshell32 -luser32 -lgdi32 -L$(DW3_WIN_CACHE)/sdl2/lib -L$(DW3_WIN_CACHE)/openal/lib \
	-static-libgcc -static-libstdc++ -Wl,-Bstatic -lwinpthread -Wl,-Bdynamic
export CROSS_PREFIX := $(CROSS)
.DEFAULT_GOAL := $(BUILD)/$(BOOT_NAME)$(EXE)
endif

# Android: TARGET=android compiles the game's objects with the NDK's clang for arm64-v8a, in
# their own trees (build-android, build/psycross-android). It does not link: pc/android/app/jni/src/CMakeLists.txt links
# libmain.so from `print-android-objects` (so the Gradle build and this Makefile cannot list different sources), and
# runs check_scopes.py after the link. Only `make -C pc TARGET=android android-objs` is meant to work for it.
# Meant to run inside pc/android/Dockerfile via pc/android/build.sh (ANDROID_NDK_HOME is set there).
ifeq ($(TARGET),android)
ANDROID_NDK_HOME ?= $(HOME)/Android/ndk
ANDROID_API ?= 29
# SDL2 and OpenAL Soft cross-built by pc/android/build_deps.sh
ANDROID_PREFIX ?= $(HOME)/.cache/dw3-android/prefix/arm64-v8a
ANDROID_BIN := $(ANDROID_NDK_HOME)/toolchains/llvm/prebuilt/linux-x86_64/bin
CC := $(ANDROID_BIN)/aarch64-linux-android$(ANDROID_API)-clang
BUILD := build-android
NM := $(ANDROID_BIN)/llvm-nm
OBJCOPY := $(ANDROID_BIN)/llvm-objcopy
# check_scopes.py runs "<prefix>nm" and "<prefix>objdump"
export CROSS_PREFIX := $(ANDROID_BIN)/llvm-
# Optimization of the game's own objects (Linux and Windows build them without -O; the shims already use -O2, PsyCross is
# RelWithDebInfo). ANDROID_OPT=-O2 adds it to every object of this target: `pc/android/build.sh apk` passes it through
# DW3_ANDROID_OPT.
ANDROID_OPT ?=
# WIN_DEFS is the per-platform flags slot of every rule below (the name is the Windows build's)
WIN_DEFS := -fPIC $(ANDROID_OPT) -I$(ANDROID_PREFIX)/include/SDL2 -I$(ANDROID_PREFIX)/include
GL_LIB := -lGLESv3 -lEGL
HARNESS_GL_LIB := $(GL_LIB)
GLES_DEFS := -DPSYX_FORCE_GLES
endif

# T9: opt-in ES419 host build. Default 0 keeps the upstream EU build (the host
# reads the retail Spanish file from the disc); ES419=1 makes file_table.c
# include es419_table.h and redirects the USA slots (2+7k) to the es-419
# overlay's sectors in DUMMY. The generated header lives at
# $(ROOT)/build/eu/es419/es419_table.h (tools/es419/gen_table.py) and is git-
# ignored. Changing the value requires `make -C pc clean` first because the
# object rules below do not track this define for recompilation.
ES419 ?= 0

# T11: PsyCross (the external/psycross submodule, MIT, unmodified) as a static
# library. It has its own CMake project under pc/psycross/ (our file, because
# PsyCross's own CMakeLists.txt does not configure on Linux); the build stays
# out of tree, in build/psycross at the repository root (git-ignored).
PSYCROSS_BUILD := $(ROOT)/build/psycross
ifeq ($(TARGET),windows)
PSYCROSS_BUILD := $(ROOT)/build/psycross-win
endif
ifeq ($(TARGET),appimage)
PSYCROSS_BUILD := $(ROOT)/build/psycross-linux
endif
ifeq ($(TARGET),android)
PSYCROSS_BUILD := $(ROOT)/build/psycross-android
endif
ifneq ($(BUILD_TAG),)
ifeq ($(TARGET),linux)
PSYCROSS_BUILD := $(ROOT)/build/psycross$(BUILD_TAG)
endif
endif

# The game's include paths, as the PS1 build's Makefile gives them (INC). It
# puts include/ and src/ first, but the PsyQ headers come after the host's
# own with -idirafter: the port wants the host's stdlib/stdio, and PsyQ's
# directory still resolves the SDK headers (<libgte.h>, <libgpu.h>, ...) that
# only it has. ES419_INC adds the directory of the generated es419_table.h
# only when ES419=1; it stays empty otherwise.
GAME_INC = -I$(ROOT)/include \
	-I$(ROOT)/src \
	-idirafter $(ROOT)/external/psyq_headers/psyq_lib47/include \
	$(ES419_INC)

# The dialect switches of the PS1 build that still mean something on the
# host: gnu89 (old-style definitions), no builtins (the game's memcpy and
# friends are PsyQ's), wrap on signed overflow, char is signed, and one
# definition per unit (task.c's TASK_REGISTRY). -Wno-unused-parameter is the
# PS1 build's -Wno-unused, kept close.
HOST_CFLAGS := -std=gnu89 -fno-builtin -fwrapv -fsigned-char -fno-common \
	-fno-strict-aliasing -fno-delete-null-pointer-checks \
	-Wall -Wno-unused-parameter -MMD -MP -g $(WIN_DEFS) $(GLES_DEFS)

# The game's own units used to need -Wno-pointer-to-int-cast and
# -Wno-int-to-pointer-cast: file_cache.c and friends cast pointers to 32-bit
# ints (getFileEntry, getArchiveEntry, freeFilesFrom), which is right on the
# PlayStation and warns on a 64-bit host. Those casts are fixed in place now
# (docs/decisions/ADR-003-upstream-coexistence.md), so the game's units keep
# the full -Wall, as the shims and harnesses do.
GAME_CFLAGS := $(HOST_CFLAGS)

# The text engine is the first game unit with a signedness warning (its
# convertText passes a u8 * to the game's own `int strlen(const char *)`); it
# is stopped per unit, not for every game unit, so the rest keep the full
# -Wall.
TEXT_CFLAGS := $(GAME_CFLAGS) -Wno-pointer-sign

# The sound engine has the same kind of warning, once: initSound passes its
# u8 buffer to libsnd's `SsSetTableSize(char *, ...)`. Also stopped per unit.
SOUND_CFLAGS := $(GAME_CFLAGS) -Wno-pointer-sign

# Exactly what the game's version.h wants (one VERSION_*) plus SKIP_ASM: every
# translation unit that includes common.h reaches include_asm.h, which
# otherwise emits a top-level `__asm__(".include \"include/labels.inc\"")`
# for the MIPS assembler. SKIP_ASM is the upstream switch (used by m2c and
# the permuter) that turns the INCLUDE_ASM macro into a no-op; on a host
# assembler that MIPS include would fail, so the host build sets it.
#
# USE_PGXP=0 goes with the PsyCross library's own build (pc/psycross/CMakeLists.txt):
# with PGXP on, PsyCross's C++ side makes VERTTYPE a 16-bit float, and the
# game's C primitives (plain s16 coordinates) would be read as half-floats.
# Both sides must agree on the define, so it is here too.
GAME_DEFS := -DVERSION_EU=1 -DES419=$(ES419) -DASM_DIR='"asm/eu"' -DSKIP_ASM=1 -DUSE_PGXP=0 -DDW3_PC=1

# T9: when ES419=1, file_table.c includes `es419_table.h`, which tools/es419/
# gen_table.py writes to $(ROOT)/build/eu/es419/. That directory must be on
# the include path; with the default ES419=0 it adds nothing.
ifeq ($(ES419),1)
ES419_INC := -I$(ROOT)/build/eu/es419
ES419_TABLE_H := $(ROOT)/build/eu/es419/es419_table.h
else
ES419_INC :=
ES419_TABLE_H :=
endif

# The game's own translation units, unmodified:
# - rlen_tool (milestone 0): the RLEN decompressor and the task constructor;
# - file_dump (milestone 1): the disc file table and the file cache;
# - text_measure (milestone 2): the text helpers (measureText, getString, the
#   European glyph tables) and the font (FONT, FONT_STYLES, decodeChar).
GAME_DECOMP_SRC := $(ROOT)/src/engine/file/decompressor.c $(ROOT)/src/engine/task/task.c
GAME_FILE_SRC := $(ROOT)/src/engine/file/file_table.c $(ROOT)/src/engine/file/file_cache.c
GAME_TEXT_SRC := $(ROOT)/src/engine/text/text_tools.c $(ROOT)/src/engine/text/font.c

# Our replacements (pc/shim/): the heap, the empty task registry, the host CD
# reader (libcd), the graphics state's clock and the TIM loader's constructor.
SHIM_CORE_SRC := shim/heap.c shim/task_registry.c
SHIM_CD_SRC := shim/host_cd.c shim/host_gfx.c
SHIM_TIM_SRC := shim/host_tim.c

DECOMP_OBJS := $(BUILD)/decompressor.o $(BUILD)/task.o
FILE_OBJS := $(BUILD)/file_table.o $(BUILD)/file_cache.o
TEXT_OBJS := $(BUILD)/text_tools.o $(BUILD)/font.o
SHIM_CORE_OBJS := $(BUILD)/heap.o $(BUILD)/task_registry.o
# host_xa.o joins the CD reader: host_cd.c's CdlSetloc/CdRead2/CdlPause
# handlers (T36) call into the XA stream driver, so anything that links the
# CD reader needs it (and, for host_xa.c's own OpenAL sink, PSYCROSS_LIBS).
# host_st.o joins for the same reason as of T38: those commands also arm, start
# and stop the St video pump.
SHIM_CD_OBJS := $(BUILD)/host_cd.o $(BUILD)/disc_fd.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/port_fs.o $(BUILD)/image_data.o $(BUILD)/sprite_data.o $(BUILD)/content.o $(BUILD)/png_write.o $(BUILD)/png_read.o $(BUILD)/host_gfx.o $(BUILD)/host_xa.o $(BUILD)/snd_vol.o $(BUILD)/host_st.o $(BUILD)/file_cache_pc.o
SHIM_TIM_OBJS := $(BUILD)/host_tim.o

# Milestone 3: the game's own drawing unit. text_window.c is compiled against
# PsyCross's headers, not the retail Psy-Q ones, because its glyph packets are
# PsyCross SPRT/POLY_FT4 and its ordering table is linked with PsyCross's
# addPrim (the retail and host packet layouts are not compatible). Unlike the
# other game units it needs C11, not gnu89: PsyCross's libgpu.h uses the
# file-scope static_assert macro that <assert.h> defines from C11 on. Our
# psx_compat.h is force-included first (-include, before text_window.c's own
# first line) to bring <assert.h> and the few names PsyCross spells
# differently (ONE, RECT, RotMatrixYXZ_gte); include/psx goes by -idirafter so
# PsyCross's psx/strings.h does not shadow glibc's <strings.h>.
PSYCROSS_ROOT := $(ROOT)/external/psycross
DRAW_CFLAGS := $(HOST_CFLAGS) -std=gnu11 -include $(CURDIR)/psx_compat.h -Wno-pointer-sign
# src/field/stages is the upstream build's own include path (its INC): a stage
# in its area's folder includes the stages' shared code as common/<file>.inc.c.
DRAW_INC := -I$(ROOT)/include -I$(ROOT)/src -I$(ROOT)/src/field/stages \
	-I$(PSYCROSS_ROOT)/include \
	-idirafter $(PSYCROSS_ROOT)/include/psx \
	-idirafter $(ROOT)/external/psyq_headers/psyq_lib47/include

# T19: the field (FIELDSTG) and its stage overlays are drawing units like the
# country select and the title: they build their primitives (POLY_F4, SPRT,
# SPR_TPAGE) with libgpu/libgte and add them to a Layer's ordering table, so
# they compile against PsyCross's headers and the host packet layout (the same
# reason the country select does). Two of them cast a pointer to s32 and back
# (field_mode/event.c stores a task pointer in an s32; field_mode/map.c builds
# the map's parts with 32-bit pointer arithmetic, MAP_PART), the one warning
# class this path shares with the game units T6 already fixed in place; it is
# stopped per unit, as -Wno-pointer-sign is for the text engine, and the
# truncation it hides is reported in pc/README.md (T19).
FIELD_CFLAGS := $(DRAW_CFLAGS) -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast

DRAW_OBJS := $(BUILD)/text_window.o $(BUILD)/host_draw.o $(BUILD)/draw_order.o

# Milestone 5: the TIM loader (tim_loader.c) walks a TIM's words through a
# `u_long *`, and `u_long` is 32 bits on the PS1 against 64 on the host; so
# ps1_types.h is force-included BEFORE psx_compat.h (which is what pulls
# psx/types.h), and the two units' pointer types agree.
TIM_CFLAGS := $(HOST_CFLAGS) -std=gnu11 -include $(CURDIR)/ps1_types.h \
	-include $(CURDIR)/psx_compat.h -Wno-pointer-sign

# T18: the title overlay's movie player (src/menus/title/movie.c) drives
# libpress (DecDCT*) and libcd's streaming half (St*), whose PsyCross
# prototypes take `u_int *`. ps1_types.h is force-included so that `u_long`
# means 32 bits there, as it does on the PlayStation, and the game's
# `u_long *` and PsyCross's `u_int *` are the same type; psx_press.h declares
# the three libpress functions PsyCross's libpress.h omits
# (DecDCTvlc2/DecDCTvlcBuild/DecDCToutCallback). Same flags for the shim that
# defines all of them (shim/host_press.c).
PRESS_CFLAGS := $(HOST_CFLAGS) -std=gnu11 -include $(CURDIR)/ps1_types.h \
	-include $(CURDIR)/psx_compat.h -include $(CURDIR)/psx_press.h -Wno-pointer-sign

# Everything the harness needs from the game and the earlier shims. task.o
# comes from the RLEN milestone: text_window.c creates its task with createTask.
DRAW_GAME_OBJS := $(TEXT_OBJS) $(FILE_OBJS) $(SHIM_CORE_OBJS) $(SHIM_CD_OBJS) $(SHIM_TIM_OBJS) $(BUILD)/task.o

# Milestone 5 (T14): the font in VRAM and the game's own text on screen.
# font_frame.c is the harness; the unit it adds over milestone 4 is the game's
# real TIM loader (src/engine/gfx/tim_loader.c), which loadFont (font.c) drives
# to upload FILE_FONT's TIMs to VRAM through libgpu's LoadImage/DrawSync. It is
# a drawing unit like text_window.c (PsyCross headers, C11) and it REPLACES the
# initTimLoader stub (shim/host_tim.c) for this target only: the headless
# harnesses keep the stub, because they never load a font and do not want to
# link the GPU. Its <libgs.h>/<libsnd.h> resolve to the retail Psy-Q headers
# (the three PsyQ libraries it includes for the PS1 build are not used in these
# two methods); PsyCross has no Gs* or Ss* at all.
FONTFRAME_OBJS := $(BUILD)/font_frame.o $(BUILD)/text_window.o $(BUILD)/host_draw.o \
	$(BUILD)/host_shot.o
FONTFRAME_GAME_OBJS := $(TEXT_OBJS) $(FILE_OBJS) $(SHIM_CORE_OBJS) $(SHIM_CD_OBJS) \
	$(BUILD)/tim_loader.o $(BUILD)/task.o

# text_shot: one PNG per game string (talk box windows), es419 through the port's own text layer.
TEXTSHOT_OBJS := $(BUILD)/text_shot.o $(BUILD)/text_window.o $(BUILD)/host_draw.o $(BUILD)/host_shot.o

# Milestone 6 (T15): the game's own main() and the country-select screen on
# the host. This is the largest harness: it links the game's boot unit
# (main.c, with its entry point renamed by -Dmain=pcGameMain so that boot.c
# can be the program's entry point), the engine units its frame loop needs
# (the file layer, the task system, the game state and its party/items/cards
# data, the pad, the sound, the memory card, the random numbers and the
# settings), the country select and the drawing units it uses, and the shims
# the two libraries PsyCross does not provide ask for:
#
#   * libgs (shim/host_gs.c): GsInitGraph/GsInit3D/GsGetTimInfo, the three the
#     boot path calls;
#   * libsnd (shim/host_snd.c): the public surface; the one-shot SPU path is
#     real through shim/snd_spu.c (T34), the SEP/sequence side is still stubbed;
#   * the GFX state and the drawing layers (shim/gfx_engine.c), which replace
#     display.c and layer.c for the reasons the file explains;
#   * the task registry (shim/task_registry.c), now a real registry: main's
#     loop runs the mode's whole task tree through it.
#
# The units of the drawing world are compiled with the PsyCross headers, as
# milestones 3-5 already do; the engine units keep the retail Psy-Q headers
# and the flags the earlier milestones verified. The layouts both worlds
# share (GFX, GAME, PAD, SOUND, FILE_CACHE, HEAP, Task) are identical under
# the two header sets -- measured, pc/README.md records the numbers -- so the
# two halves agree about the structures they pass each other.
# T18: the title overlay (STDWTITL, src/menus/title/). It is linked as a whole
# -- its scene, its three data objects and the units its scene, title screen
# and movies name -- because STDWTITL_start's own dispatch (on the mode's low
# byte) picks between the title screen, a movie and the still screen, and all
# three live in the same overlay on the PlayStation. Its units are drawing
# units (PsyCross headers, C11), like the country select; movie.o additionally
# needs PRESS_CFLAGS (see above). The three data objects get a `_data` suffix
# because two of their source names collide with a unit of the same name one
# directory up (data/title.c vs title.c, data/movie.c vs movie.c).
TITLE_ROOT := $(ROOT)/src/menus/title
TITLE_OBJS := $(BUILD)/scene.o $(BUILD)/movie.o $(BUILD)/title_loader.o \
	$(BUILD)/title.o $(BUILD)/background.o $(BUILD)/logo.o $(BUILD)/glint.o \
	$(BUILD)/slides.o $(BUILD)/menu.o $(BUILD)/edge_fade.o $(BUILD)/splash.o \
	$(BUILD)/stdwtitl_data.o $(BUILD)/title_data.o $(BUILD)/movie_data.o

# T44: STPLNMET, the player's name entry overlay (src/menus/player_name/). The
# intro's cutscene chain lands here once the second stage's event leaves the
# field (MODE_PLAYER_NAME 0x500 -> entry index 5): the game asks for the
# player's name and the partners before going back to the field. Drawing units
# (PsyCross headers, C11), like the title's; every object name is prefixed
# plnmet_ because name.c-for-name counts would collide with the title's and
# the field's scene.o/confirm.o objects in the same build directory.
PLAYER_ROOT := $(ROOT)/src/menus/player_name
PLAYER_OBJS := $(BUILD)/plnmet_scene.o $(BUILD)/plnmet_screen.o \
	$(BUILD)/plnmet_backdrop.o $(BUILD)/plnmet_welcome.o \
	$(BUILD)/plnmet_name_entry.o $(BUILD)/plnmet_choice.o \
	$(BUILD)/plnmet_confirm.o $(BUILD)/plnmet_data.o

# T19: the field mode (FIELDSTG, src/field/) -- where a new game starts
# (MODE_NEW_GAME, 0x2D7 -> index 2) -- and its stage overlays.
#
# The executable's own units the field names and the boot path did not have
# yet: the flags (FLAGS_00, game/events.c), the mode loader the field reads
# (OVERLAY_LOADER is a host shim, see below, not the game's overlay.c), the
# menu and text constructors its speech, menus and fades call (cursor.c,
# menu/field_menu.c, menu/inn.c, text/message_box.c, gfx/screen_fade.c,
# text/talk_box.c), the decompressor its map streamer creates and the game's
# .bss the field menu's choice lives in (data/game_bss.c). They are the
# executable's units, so they keep the retail Psy-Q headers and the gnu89
# flags the rest of the boot engine uses; text_window.o is the drawing world's
# (PsyCross headers, C11), as in milestones 3-5. Only talk_box.c warns (an
# unused stack variable the game keeps on purpose, and one signedness pass
# like the text engine's), stopped per unit.
BOOT_FIELD_ENGINE_OBJS := $(BUILD)/events.o $(BUILD)/cursor.o $(BUILD)/field_menu.o \
	$(BUILD)/inn.o $(BUILD)/message_box.o $(BUILD)/screen_fade.o $(BUILD)/talk_box.o \
	$(BUILD)/game_bss.o $(BUILD)/decompressor.o

# The field mode's own units and data, and its stage overlays. They are
# drawing units (PsyCross headers, C11, FIELD_CFLAGS, DRAW_INC): the field
# builds POLY_F4/POLY_FT4/SPRT and its Layers with libgpu/libgte, and its
# stages were measured to name no Gs*, Ss* or gte_* call at all (pc/README.md,
# T19). Its object names live under build/field/ so that field_mode/state.c
# does not collide with the executable's game/state.c, and field_mode/banner.c
# does not have to share its object with the milestone-4 harness.
FIELD_ROOT := $(ROOT)/src/field/field_mode
FIELD_STAGE_ROOT := $(ROOT)/src/field/stages
# T3: the executable's matrix data the battle's meshes name (IDENTITY_MATRIX,
# the root bone's parent; sdk's own comment: nothing else reads the file's
# other matrices, so it is a -G0 unit and gcc drops nothing either).
GAME_MATRIX_SRC := $(ROOT)/src/engine/data/matrices.c
# T22: the field's own file loader needs one host-side guard the upstream unit
# does not have (see pc/field/file_loader.c). It dereferences
# FIELDSTG_state.slots, which is NULL for a stage without slots -- the field
# itself guards that read at field.c, the loader does not -- and the PlayStation
# tolerates the read while the host faults. The upstream unit is dropped from
# the glob and the host's copy (byte-identical apart from the guard) takes its
# place, under the same object name.
# battle-entry T1: the balloons get the same treatment (pc/field/balloon.c):
# a balloon command can leave its actor NULL and the draw reads through it.
# content-export E6b: the stream tasks too (pc/field/stream.c): the tiles of a stage's PACK reach VRAM through them and a mod's PNG of a tile
# is asked for there (shim/stage_mod.c).
FIELD_MODE_OBJS := $(filter-out $(BUILD)/field/file_loader.o $(BUILD)/field/balloon.o $(BUILD)/field/state.o $(BUILD)/field/stream.o,\
	$(patsubst $(FIELD_ROOT)/%.c,$(BUILD)/field/%.o,\
	$(shell find $(FIELD_ROOT) -maxdepth 1 -name '*.c'))) \
	$(BUILD)/field/fieldstg.o $(BUILD)/field/file_loader.o $(BUILD)/field/balloon.o \
	$(BUILD)/field/state.o $(BUILD)/field/stream.o
# The stage overlays the version has, from config/<version>/stages.txt's own
# C: each area's <name>.c and the one head data unit (wstag924_head.c). Every
# global of a stage is renamed with its name at build time (WSTAG###_*) the way
# the PS1 build's linker does it (the Makefile's `<stage>_syms.ld` rule): the
# stages name their functions alike, so without the prefix two of them would
# collide in one host binary.
FIELD_STAGE_TUS := $(patsubst $(FIELD_STAGE_ROOT)/%.c,%,$(shell find $(FIELD_STAGE_ROOT) -name '*.c' ! -name '*.inc.c'))
FIELD_STAGE_OBJS := $(addprefix $(BUILD)/stages/,$(addsuffix .o,$(FIELD_STAGE_TUS)))
BOOT_FIELD_OBJS := $(FIELD_MODE_OBJS) $(FIELD_STAGE_OBJS)

# T3: FIGHTSTG, the battle mode (MODE_BATTLE 0x600). src/battle/battle_mode is
# the mode's own overlay (its root task, src/battle/battle_mode/start.c);
# src/battle/normal_battle is WFIGHTMN, the battle menu its zero argument
# runs, and src/debug/battle_test/battle_test.c is WFIGHTTS, the battle test
# its nonzero argument runs (pc/boot.c's pcStartBattle). src/battle/report is
# STFGTREP, the report's own mode (0x1400), not a unit of this one -- nothing
# in what the battle links names its symbols, so it stays out. The units are
# drawing units like the field's (PsyCross headers, C11, they build POLY_F4/
# SPRT quads and gte matrices), and the pointer-int casts the T19 note
# describes show up here too, stopped per unit. The data unit of normal_battle
# carries WFIGHTMN.PRO's tables, so the units keep their sections (-fdata-
# sections/-ffunction-sections) like the field's do -- the link can gc them
# when nothing references them. cursor.c's one direct-GPU path passes the
# game's u_long * ordering table to PsyCross's u_int * ContinueDraw prototype
# (u_long is 64 bits on this host): the values are the same pointers the PS1
# passed, the routine they bind resolves to the shim host_battle.c (PsyCross
# provides neither ContinueDraw nor IsIdleGPU), so the warning is the host
# type width, not a bug, and it is stopped only as far as needed.
BATTLE_SRC_ROOT := $(ROOT)/src/battle
BATTLE_TEST_ROOT := $(ROOT)/src/debug/battle_test
BATTLE_CFLAGS := $(DRAW_CFLAGS) -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
	-Wno-error=incompatible-pointer-types
BATTLE_TUS := $(patsubst $(BATTLE_SRC_ROOT)/%.c,%,$(shell find $(BATTLE_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
BATTLE_MODE_OBJS := $(addprefix $(BUILD)/battle/,$(addsuffix .o,$(BATTLE_TUS))) \
	$(BUILD)/battle/battle_test.o

# M1: STSTATUS, the field menu's screens (MODE_STATUS 0x1000 -> index 16);
# src/menus/field_menu/ is all C (its tables are in helpers.c and menu.c) and
# its units are drawing units like the battle's. Sections are kept so the link
# can gc what nothing references.
FMENU_SRC_ROOT := $(ROOT)/src/menus/field_menu
FMENU_CFLAGS := $(BATTLE_CFLAGS)
FMENU_TUS := $(patsubst $(FMENU_SRC_ROOT)/%.c,%,$(shell find $(FMENU_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
FMENU_OBJS := $(addprefix $(BUILD)/fmenu/,$(addsuffix .o,$(FMENU_TUS)))

# M2: STGMCARD, the memory card screen (MODE_CONTINUE 0xC00 -> index 12): the
# load screen and the save list. The card itself is shim/host_memcard.c.
MCARD_SRC_ROOT := $(ROOT)/src/menus/memory_card
MCARD_TUS := $(patsubst $(MCARD_SRC_ROOT)/%.c,%,$(shell find $(MCARD_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
MCARD_OBJS := $(addprefix $(BUILD)/mcard/,$(addsuffix .o,$(MCARD_TUS)))

# M5: the Digimon Lab (STGDGLAB, MODE_DIGI_LAB 0xD00 -> index 13), the partner
# name entry (STDGNAME, MODE_NAMING 0xB00 -> 11) and the gym (STGTRAIN,
# MODE_TRAINING 0xA00 -> 10). Same treatment as the field menu's units.
DLAB_SRC_ROOT := $(ROOT)/src/menus/digimon_lab
DLAB_TUS := $(patsubst $(DLAB_SRC_ROOT)/%.c,%,$(shell find $(DLAB_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
DLAB_OBJS := $(addprefix $(BUILD)/dlab/,$(addsuffix .o,$(DLAB_TUS)))
PNAME_SRC_ROOT := $(ROOT)/src/menus/partner_name
PNAME_TUS := $(patsubst $(PNAME_SRC_ROOT)/%.c,%,$(shell find $(PNAME_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
PNAME_OBJS := $(addprefix $(BUILD)/pname/,$(addsuffix .o,$(PNAME_TUS)))
GYM_SRC_ROOT := $(ROOT)/src/menus/gym
GYM_TUS := $(patsubst $(GYM_SRC_ROOT)/%.c,%,$(shell find $(GYM_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
GYM_OBJS := $(addprefix $(BUILD)/gym/,$(addsuffix .o,$(GYM_TUS)))

# M4: the item shop (STITSHOP, MODE_ITEM_SHOP 0xF00 -> index 15) and the card
# shop (STCRDSHP, MODE_CARD_SHOP 0x1300 -> index 19). Same treatment as the
# field menu's units.
ISHOP_SRC_ROOT := $(ROOT)/src/menus/item_shop
ISHOP_TUS := $(patsubst $(ISHOP_SRC_ROOT)/%.c,%,$(shell find $(ISHOP_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
ISHOP_OBJS := $(addprefix $(BUILD)/ishop/,$(addsuffix .o,$(ISHOP_TUS)))
CSHOP_SRC_ROOT := $(ROOT)/src/cardgame/card_packs
CSHOP_TUS := $(patsubst $(CSHOP_SRC_ROOT)/%.c,%,$(shell find $(CSHOP_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
CSHOP_OBJS := $(addprefix $(BUILD)/cshop/,$(addsuffix .o,$(CSHOP_TUS)))

# M6a: the deck editor (STCRDDEK, MODE_DECK_EDITOR 0x400 -> index 4) and the
# card album (STCRDABM, MODE_CARD_ALBUM 0x1200 -> index 18).
DECK_SRC_ROOT := $(ROOT)/src/cardgame/deck_editor
DECK_TUS := $(patsubst $(DECK_SRC_ROOT)/%.c,%,$(shell find $(DECK_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
DECK_OBJS := $(addprefix $(BUILD)/deck/,$(addsuffix .o,$(DECK_TUS)))
ALBUM_SRC_ROOT := $(ROOT)/src/cardgame/album
ALBUM_TUS := $(patsubst $(ALBUM_SRC_ROOT)/%.c,%,$(shell find $(ALBUM_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
ALBUM_OBJS := $(addprefix $(BUILD)/album/,$(addsuffix .o,$(ALBUM_TUS)))

# M6b: the card battle (CARDGAME, MODE_CARD_GAME 0x700 -> index 7).
CGAME_SRC_ROOT := $(ROOT)/src/cardgame/card_battle
CGAME_TUS := $(patsubst $(CGAME_SRC_ROOT)/%.c,%,$(shell find $(CGAME_SRC_ROOT) -name '*.c' ! -name '*.inc.c'))
CGAME_OBJS := $(addprefix $(BUILD)/cgame/,$(addsuffix .o,$(CGAME_TUS)))

BOOT_ENGINE_OBJS := $(FILE_OBJS) $(TEXT_OBJS) $(SHIM_CORE_OBJS) $(BUILD)/task.o \
	$(BUILD)/state.o $(BUILD)/items.o $(BUILD)/party.o $(BUILD)/partner.o \
	$(BUILD)/stats.o $(BUILD)/digimon.o $(BUILD)/play_time.o \
	$(BUILD)/pad.o $(BUILD)/port.o $(BUILD)/demo.o $(BUILD)/sound.o \
	$(BUILD)/memcard.o $(BUILD)/random.o $(BUILD)/eu_settings.o $(BUILD)/all_files_pattern.o \
	$(BUILD)/get_digimon.o $(BUILD)/matrices.o $(BOOT_FIELD_ENGINE_OBJS)
BOOT_SHIM_OBJS := $(BUILD)/gym_labels.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/item_ext.o $(BUILD)/card_ext.o $(BUILD)/game_tables.o $(BUILD)/tech_ext.o $(BUILD)/digimon_ext.o $(BUILD)/model_bake.o $(BUILD)/model_export.o $(BUILD)/video_export.o $(BUILD)/stage_export.o $(BUILD)/stage_mod.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/stage_mods.o $(BUILD)/mod_stages.o $(BUILD)/stage_new.o $(BUILD)/field_ext.o $(BUILD)/map_layers.o $(BUILD)/map_pack.o $(BUILD)/raw_export.o $(BUILD)/audio_export.o $(BUILD)/wav_write.o $(BUILD)/wav_read.o $(BUILD)/vag_encode.o $(BUILD)/audio_mod.o $(BUILD)/midi_write.o $(BUILD)/jpeg_write.o $(BUILD)/avi_write.o $(BUILD)/jpeg_read.o $(BUILD)/avi_read.o $(BUILD)/video_mod.o $(BUILD)/save_view.o $(BUILD)/save_file.o $(BUILD)/talk_box_wide.o $(BUILD)/text_fit.o $(BUILD)/host_cd.o $(BUILD)/file_cache_pc.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/mods.o $(BUILD)/mod_tools.o $(BUILD)/port_log.o $(BUILD)/port_fs.o $(BUILD)/image_data.o $(BUILD)/sprite_data.o $(BUILD)/png_write.o $(BUILD)/png_read.o $(BUILD)/port_config.o $(BUILD)/table_mods.o $(BUILD)/host_gte.o $(BUILD)/host_gs.o $(BUILD)/host_snd.o $(BUILD)/snd_spu.o $(BUILD)/app_lifecycle.o $(BUILD)/app_lifecycle_sdl.o $(BUILD)/touch_pad.o $(BUILD)/touch_router.o $(BUILD)/touch_menu.o $(BUILD)/touch_tap.o $(BUILD)/touch_sdl.o \
	$(BUILD)/snd_sep.o $(BUILD)/snd_vol.o $(BUILD)/host_xa.o $(BUILD)/host_press.o $(BUILD)/host_mdec.o \
	$(BUILD)/host_st.o $(BUILD)/host_overlay.o $(BUILD)/host_battle.o $(BUILD)/host_memcard.o $(BUILD)/card_image.o $(BUILD)/input_cfg.o $(BUILD)/port_paths_sdl.o $(BUILD)/controls_screen.o $(BUILD)/options_video.o $(BUILD)/options_language.o $(BUILD)/options_audio.o $(BUILD)/options_system.o $(BUILD)/options_mods.o $(BUILD)/auto_text.o $(BUILD)/battle_speed.o $(BUILD)/stage_banner.o $(BUILD)/fast_transitions.o $(BUILD)/state_scope.o \
	$(BUILD)/input_device.o $(BUILD)/prompt_icons.o $(BUILD)/prompt_atlas_data.o $(BUILD)/text_icons.o $(BUILD)/menu_frame.o \
	$(BUILD)/disc_picker.o $(BUILD)/disc_fd.o $(BUILD)/tinyfiledialogs.o
ifeq ($(TARGET),android)
# no native file dialog on Android: shim/disc_picker.c opens the system's document picker (T7b, decisions in shim/disc_flow.c)
BOOT_SHIM_OBJS := $(filter-out $(BUILD)/tinyfiledialogs.o,$(BOOT_SHIM_OBJS)) $(BUILD)/l10n_install.o $(BUILD)/android_perms.o $(BUILD)/disc_flow.o # the APK carries the es-419 overlay (shim/l10n_install.c)
endif
BOOT_DRAW_OBJS := $(BUILD)/gfx_engine.o $(BUILD)/battle_trace.o $(BUILD)/stage_timing.o $(BUILD)/host_shot.o $(BUILD)/host_video.o $(BUILD)/controls_draw.o $(BUILD)/text_icons_draw.o \
	$(BUILD)/sprite_drawer.o $(BUILD)/card_drawer.o \
	$(BUILD)/country_select.o $(BUILD)/tim_loader.o $(BUILD)/host_pad.o $(BUILD)/boot.o \
	$(TITLE_OBJS) $(PLAYER_OBJS) $(BUILD)/text_window.o $(BOOT_FIELD_OBJS) \
	$(BATTLE_MODE_OBJS) $(FMENU_OBJS) $(MCARD_OBJS) \
	$(DLAB_OBJS) $(PNAME_OBJS) $(GYM_OBJS) $(ISHOP_OBJS) $(CSHOP_OBJS) $(DECK_OBJS) $(ALBUM_OBJS) $(CGAME_OBJS)

# Milestone 4 (T13): the area name banner, the game's first drawable that needs
# no texture. banner.c is a drawing unit like text_window.c (PsyCross headers,
# C11, psx_compat.h). data/fieldstg.c is linked ONLY for FIELDSTG_bannerBoxes and
# FIELDSTG_areaNames: FIELDSTG.PRO's data names 488 game symbols this harness
# does not have, so it is compiled with -ffunction-sections/-fdata-sections and
# the link keeps --gc-sections, which drops every section nothing references
# instead of building all of FIELDSTG.PRO. eu_settings.c is the game's own
# LANGUAGE that the banner's TEXT_FILE() reads (its data, not a shim).
GAME_FIELD_SRC := $(ROOT)/src/field/field_mode/data/fieldstg.c
GAME_SETTINGS_SRC := $(ROOT)/src/engine/data/eu_settings.c

FRAME_OBJS := $(BUILD)/first_frame.o $(BUILD)/banner.o $(BUILD)/text_window.o $(BUILD)/host_draw.o
FRAME_GAME_OBJS := $(TEXT_OBJS) $(FILE_OBJS) $(SHIM_CORE_OBJS) $(SHIM_CD_OBJS) \
	$(SHIM_TIM_OBJS) $(BUILD)/task.o $(BUILD)/fieldstg.o $(BUILD)/eu_settings.o

.PHONY: all clean check check-file check-text check-draw first-frame font-frame text-shot check-font check-country psycross check-packets boot input-demo check-input title-demo check-title movie-demo field-demo check-field sndtest inputtest texttest mdectest sttest battletest

all: $(BUILD)/rlen_tool $(BUILD)/file_dump $(BUILD)/text_measure $(BUILD)/draw_order $(BUILD)/first_frame $(BUILD)/font_frame $(BUILD)/boot

$(BUILD)/rlen_tool: $(DECOMP_OBJS) $(SHIM_CORE_OBJS) $(BUILD)/rlen_tool.o $(BUILD)/port_log.o
	$(CC) $^ -o $@

# The file layer is the game's own: FILE_TABLE/FILE_CACHE (file_table.c,
# file_cache.c) over HEAP and the host CD reader. The reader now pulls the XA
# stream driver (SHIM_CD_OBJS), so the link needs PsyCross's libraries.
$(BUILD)/file_dump: $(FILE_OBJS) $(SHIM_CORE_OBJS) $(SHIM_CD_OBJS) $(BUILD)/file_dump.o
	$(CC) $^ $(PSYCROSS_LIBS) -o $@

# The text engine is the game's own too: text_tools.c and font.c over the same
# file layer (the harness loads a language file from the disc with it).
$(BUILD)/text_measure: $(TEXT_OBJS) $(FILE_OBJS) $(SHIM_CORE_OBJS) $(SHIM_CD_OBJS) $(SHIM_TIM_OBJS) $(BUILD)/text_measure.o
	$(CC) $^ $(PSYCROSS_LIBS) -o $@

# The drawing harness (milestone 3): the game's own text window
# (text_window.o, compiled against PsyCross) draws into the host shim's packet
# buffer and ordering table, over the game's file layer. It links PsyCross's
# static library for the GTE (RotMatrixYXZ, ScaleMatrix, ApplyMatrixSV) and
# the GPU helper (SetDrawTPage) the drawing code calls. Pulling those archive
# members drags the library's SDL2 and OpenAL users (PsyX_GPU, PsyX_pad,
# PsyX_main), so the link needs the same libraries PsyCross's own CMake links;
# nothing is initialised at run time -- the harness never calls PsyX_Initialise.
PSYCROSS_LIBS := -lSDL2 -lopenal -lm
ifeq ($(TARGET),windows)
PSYCROSS_LIBS := -lSDL2 -lOpenAL32 -lwinmm -lm
endif

$(BUILD)/draw_order: $(DRAW_OBJS) $(DRAW_GAME_OBJS) psycross
	$(CC) $(DRAW_OBJS) $(DRAW_GAME_OBJS) $(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) -o $@

# Milestone 4 (T13): the frame on screen. Same PsyCross library and game units
# as the drawing harness, plus the banner and the boxes' data. --gc-sections is
# what makes data/fieldstg.o usable: see the note above FRAME_OBJS.
$(BUILD)/first_frame: $(FRAME_OBJS) $(FRAME_GAME_OBJS) psycross
	$(CC) -Wl,--gc-sections $(FRAME_OBJS) $(FRAME_GAME_OBJS) $(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) $(HARNESS_GL_LIB) -o $@

# Milestone 5 (T14): the font in VRAM and the text drawn from it. Same PsyCross
# library as milestone 4, plus the game's real TIM loader instead of its stub.
$(BUILD)/font_frame: $(FONTFRAME_OBJS) $(FONTFRAME_GAME_OBJS) psycross
	$(CC) $(FONTFRAME_OBJS) $(FONTFRAME_GAME_OBJS) $(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) $(HARNESS_GL_LIB) -o $@

$(BUILD)/text_shot: $(TEXTSHOT_OBJS) $(FONTFRAME_GAME_OBJS) psycross
	$(CC) $(TEXTSHOT_OBJS) $(FONTFRAME_GAME_OBJS) $(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) $(HARNESS_GL_LIB) -o $@

# Milestone 6 (T15): the game's own main() and the country-select screen. The
# game's boot unit, the engine units its loop runs and the country select's
# drawing units, over the shims for libgs, libsnd, the GFX engine and the task
# registry (see BOOT_ENGINE_OBJS above). fno-strict-aliasing (in HOST_CFLAGS)
# matters here: the game's drawing code writes packet fields as whole words.
# The field stages' state scopes (one per WSTAG###): a linker fragment and a table of ranges, generated
# from the stage objects (pc/check_scopes.py gen) and checked together with pc/ovl_sections.ld.
$(BUILD)/stage_scopes.c: check_scopes.py $(FIELD_STAGE_OBJS)
	python3 check_scopes.py gen $(BUILD)/ovl_stages.ld $@ $(FIELD_STAGE_OBJS)

$(BUILD)/ovl_stages.ld: $(BUILD)/stage_scopes.c
	@:

$(BUILD)/stage_scopes.o: $(BUILD)/stage_scopes.c shim/state_scope.h
	$(CC) $(HOST_CFLAGS) -I$(CURDIR) -c $< -o $@

# port-close S2: the table of the stages' setup and start entry points and the names of their C functions (shim/stage_data.h SD_SETUPS), read
# from the (renamed) stage objects, like the scopes above. A stage's own name is WSTAG<nnn>; the common steppers every stage includes are listed too.
$(BUILD)/stage_setups.c: $(FIELD_STAGE_OBJS) Makefile
	@echo "  GEN $@"
	@decl=$@.decl; rows=$@.rows; : > $$decl; : > $$rows; \
	for o in $(FIELD_STAGE_OBJS); do \
		b=$$(basename $$o .o); \
		case $$b in wstag[0-9][0-9][0-9]) ;; *) continue;; esac; \
		S=$$(echo $$b | tr a-z A-Z); num=$${b#wstag}; su=0; st=0; list=""; \
		for f in $$($(NM) --defined-only $$o | awk '$$2 ~ /^[Tt]$$/ { print $$3 }' | sort -u); do \
			case $$f in \
				$${S}_setupStage) su=1;; \
				$${S}_startStage) st=1;; \
				$${S}_*) list="$$list,$${f#$${S}_}";; \
				.*|__*) ;; \
				*) list="$$list,$$f";; \
			esac; \
		done; \
		sp=0; tp=0; \
		if [ $$su = 1 ]; then echo "extern void $${S}_setupStage(void);" >> $$decl; sp=$${S}_setupStage; fi; \
		if [ $$st = 1 ]; then echo "extern void *$${S}_startStage(void *owner);" >> $$decl; tp=$${S}_startStage; fi; \
		echo "    { \"$$S\", $$num, $$sp, $$tp, \"$${list#,}\" }," >> $$rows; \
	done; \
	{ echo '/* Generated by pc/Makefile (the stage_setups rule). */'; echo '#include "shim/stage_data.h"'; cat $$decl; \
	  echo 'const SdSetup SD_SETUPS[] = {'; cat $$rows; echo '    { 0, 0, 0, 0, 0 }'; echo '};'; } > $@; \
	rm -f $$decl $$rows

$(BUILD)/stage_setups.o: $(BUILD)/stage_setups.c shim/stage_data.h
	$(CC) $(HOST_CFLAGS) -I$(CURDIR) -c $< -o $@

# Scope capture: Linux feeds the ELF fragments straight to ld (INSERT, one output section per
# scope); Windows derives one PE script from the same statements (check_scopes.py pe, see its doc).
ifeq ($(TARGET),windows)
SCOPE_LD := $(BUILD)/ovl_pe.ld
SCOPE_LDFLAGS := -Wl,-T,$(SCOPE_LD)
$(SCOPE_LD): ovl_sections.ld $(BUILD)/ovl_stages.ld check_scopes.py
	python3 check_scopes.py pe $@ $(BUILD) ovl_sections.ld $(BUILD)/ovl_stages.ld
else ifneq ($(filter appimage android,$(TARGET))$(BUILD_TAG),)
# the script's object patterns say `*build/`: this tree is build-linux (or build-lld...), so the linker gets a copy that says so
SCOPE_LD := $(BUILD)/ovl_sections.ld
SCOPE_LDFLAGS := -Wl,-T,$(CURDIR)/$(SCOPE_LD) -Wl,-T,$(BUILD)/ovl_stages.ld
$(SCOPE_LD): ovl_sections.ld
	sed 's|\*build/|*$(BUILD)/|' $< > $@
else
SCOPE_LD :=
SCOPE_LDFLAGS := -Wl,-T,$(CURDIR)/ovl_sections.ld -Wl,-T,$(BUILD)/ovl_stages.ld
endif

# The Windows executable's icon resource: the same picture as the installer's (win/installer.png)
ifeq ($(TARGET),windows)
BOOT_RES := $(BUILD)/dw3_res.o
$(BUILD)/dw3.ico: win/installer.png win/installer_art.py
	python3 win/installer_art.py --icon $< $@

$(BOOT_RES): win/dw3.rc $(BUILD)/dw3.ico
	x86_64-w64-mingw32-windres -I$(BUILD) $< -o $@
else
BOOT_RES :=
endif

$(BUILD)/$(BOOT_NAME)$(EXE): ovl_sections.ld check_scopes.py $(BUILD)/ovl_stages.ld $(SCOPE_LD) $(BUILD)/stage_scopes.o $(BUILD)/stage_setups.o $(BUILD)/main.o $(BOOT_ENGINE_OBJS) $(BOOT_SHIM_OBJS) $(BOOT_DRAW_OBJS) $(BOOT_RES) psycross
	$(CC) $(BUILD)/main.o $(BOOT_ENGINE_OBJS) $(BOOT_SHIM_OBJS) $(BOOT_DRAW_OBJS) $(BOOT_RES) \
		$(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) $(GL_LIB) \
		$(BOOT_LDFLAGS) $(LINK_FLAGS) -Wl,--wrap=PadChkVsync $(GTE_WRAPS) $(SCOPE_LDFLAGS) $(BUILD)/stage_scopes.o $(BUILD)/stage_setups.o -o $@
	python3 check_scopes.py check $@ $(BUILD) ovl_sections.ld $(BUILD)/ovl_stages.ld

# TARGET=android: the game's objects and the linker inputs, for pc/android's CMake (which links libmain.so).
# The print-* targets only name things (no build), one item per line, so CMake can read them at configure time.
.PHONY: android-objs print-android-objects print-android-ldflags print-android-psycross
ANDROID_LINK_OBJS := $(BUILD)/main.o $(BOOT_ENGINE_OBJS) $(BOOT_SHIM_OBJS) $(BOOT_DRAW_OBJS) $(BUILD)/stage_scopes.o $(BUILD)/stage_setups.o
android-objs: ovl_sections.ld check_scopes.py $(BUILD)/ovl_stages.ld $(SCOPE_LD) $(ANDROID_LINK_OBJS) psycross
print-android-objects:
	@for o in $(ANDROID_LINK_OBJS); do echo $(CURDIR)/$$o; done
# the overlay scripts and the wraps of the boot rule above, as the linker takes them
print-android-ldflags:
	@for f in -Wl,--wrap=PadChkVsync $(GTE_WRAPS) -Wl,-T,$(CURDIR)/$(SCOPE_LD) -Wl,-T,$(CURDIR)/$(BUILD)/ovl_stages.ld; do echo $$f; done
print-android-psycross:
	@echo $(PSYCROSS_BUILD)/libpsycross.a

# The three units of the drawing harness go through the PsyCross headers and
# C11; the rules are explicit so they win over the gnu89 pattern rules below.
$(BUILD)/text_window.o: $(ROOT)/src/engine/text/text_window.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T3: host_gs.o stands in for the retail libgs the battle's units use, so it
# must see the SAME types they do. The plain shim rule (HOST_CFLAGS) would
# pull MATRIX (GsCOORDINATE2's two members) from the retail libgte.h, whose
# `long t[3]` is 8 bytes a member on this host -- GsCOORDINATE2 then puts
# `super` at +112 where the battle's units (which compile libgte.h from
# PsyCross first, whose MATRIX is the PS1's 32 bytes) wrote it at +80, and
# GsSetRefView2 walked a chain out of adjacent stack memory (the camera's
# MulMatrix0 SIGSEGV). The battle's flags give the retail header the same
# MATRIX, because the guards make libgs.h's own <libgte.h> a no-op there.
$(BUILD)/host_gs.o: shim/host_gs.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# GTE audit G2: retail-exact ScaleMatrix/MulMatrix/MulMatrix2/RotMatrixZYX_gte
# (shim/host_gte.c). PsyCross's differ and the submodule is never patched, so
# the game's calls are redirected at link time, whatever flags a unit has.
GTE_WRAPS := -Wl,--wrap=ScaleMatrix -Wl,--wrap=MulMatrix -Wl,--wrap=MulMatrix2 \
	-Wl,--wrap=RotMatrixZYX_gte

$(BUILD)/host_gte.o: shim/host_gte.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# gtetest checks those against values from the retail asm; gtetest-base is the
# same test against plain PsyCross (expected to fail: it is the RED run).
.PHONY: gtetest gtetest-base
gtetest: $(BUILD)/gtetest
	$(BUILD)/gtetest

$(BUILD)/gtetest: gtetest.c $(BUILD)/host_gte.o psycross
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) gtetest.c $(BUILD)/host_gte.o \
		$(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) $(HARNESS_GL_LIB) $(LINK_FLAGS) $(GTE_WRAPS) -o $@

gtetest-base: psycross
	$(CC) $(DRAW_CFLAGS) -DGTETEST_BASELINE $(GAME_DEFS) $(DRAW_INC) gtetest.c \
		$(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) $(HARNESS_GL_LIB) -o $(BUILD)/gtetest_base
	$(BUILD)/gtetest_base

$(BUILD)/host_draw.o: shim/host_draw.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/draw_order.o: draw_order.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# Milestone 4: the banner's own drawing unit (PsyCross headers, C11) and its
# data (FIELDSTG.PRO's, sectioned so the link can drop the rest).
$(BUILD)/banner.o: $(ROOT)/src/field/field_mode/banner.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/first_frame.o: first_frame.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# Milestone 5: the game's real TIM loader, which is our first GPU unit that is
# not a text unit: it uploads TIMs to VRAM with LoadImage/DrawSync. Same
# PsyCross headers and C11 as the other drawing units, plus ps1_types.h ahead of
# psx_compat.h: that unit walks a TIM's words through a `u_long *`, and `u_long`
# is 64 bits on the host against 32 on the PS1 (see the header).
# The port's copy of the game's tim_loader.c (shim/tim_loader_pc.c): the same loader with the hooks
# that put a mod's or the user's PNG in place of an image before it goes to VRAM (menu-art-png T3).
$(BUILD)/tim_loader.o: shim/tim_loader_pc.c shim/image_data.h $(CURDIR)/psx_compat.h $(CURDIR)/ps1_types.h
	@mkdir -p $(dir $@)
	$(CC) $(TIM_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/text_shot.o: text_shot.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -I$(CURDIR) -c $< -o $@

$(BUILD)/font_frame.o: font_frame.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/fieldstg.o: $(GAME_FIELD_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) -fdata-sections -ffunction-sections $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# Milestone 6 (T15): the game's boot unit, with its entry point renamed so
# that pc/boot.c holds the program's main (the game's own main is the one the
# port must run, and a C program's entry point cannot be named anything
# else).
$(BUILD)/main.o: $(ROOT)/src/engine/system/main.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -Dmain=pcGameMain -c $< -o $@

# The sound engine, with the one signedness warning stopped (see SOUND_CFLAGS)
$(BUILD)/sound.o: $(ROOT)/src/engine/sound/sound.c
	@mkdir -p $(dir $@)
	$(CC) $(SOUND_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# Milestone 6 (T15): the country select (a drawing unit: PsyCross headers, C11)
$(BUILD)/country_select.o: $(ROOT)/src/menus/country_select/country_select.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/sprite_drawer.o: $(ROOT)/src/engine/gfx/sprite_drawer.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# M4: the card drawer (the card shop draws cards through it)
$(BUILD)/card_drawer.o: $(ROOT)/src/engine/gfx/card_drawer.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# Milestone 6 (T15): the host GFX engine (the GFX state and the drawing
# layers) and the shared screenshot writer, both drawing units.
$(BUILD)/battle_trace.o: shim/battle_trace.c shim/battle_trace.h shim/state_scope.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/stage_timing.o: shim/stage_timing.c shim/stage_timing.h shim/stage_banner.h shim/fast_transitions.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/gfx_engine.o: shim/gfx_engine.c shim/stage_banner.h shim/fast_transitions.h shim/stage_timing.h shim/battle_trace.h shim/battle_speed.h shim/host_video.h shim/controls_screen.h shim/text_icons.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T39: the port's movie presenter (shim/host_video.c). A drawing unit: it builds
# POLY_FT4 quads and a PsyCross ordering table, so PsyCross headers and C11.
$(BUILD)/host_video.o: shim/host_video.c shim/host_video.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/host_shot.o: shim/host_shot.c
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T17: the host side of the game's own input path (shim/host_pad.c): the wrapped
# PadChkVsync, the per-frame PsyX_UpdateInput, the in-process scripted pad and
# the per-frame line that reports what the game's own pad code saw. A drawing
# world unit (PsyCross headers, C11); no X11 and no XTest: the demos script the pad in process (T21).
$(BUILD)/host_pad.o: shim/host_pad.c shim/touch_sdl.h shim/input_cfg.h shim/controls_screen.h shim/options_video.h shim/options_language.h shim/options_audio.h shim/input_device.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/boot.o: boot.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T18: the title overlay's units (see TITLE_OBJS). They go through the PsyCross
# headers and C11, like the country select; movie.o takes PRESS_CFLAGS instead
# (its libpress/libcd calls, see above), and the three data objects of
# src/menus/title/data/ get their own rules and their `_data` names because two
# of them would collide with a unit of the same name one directory up.
$(BUILD)/%.o: $(TITLE_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/movie.o: $(TITLE_ROOT)/movie.c $(CURDIR)/ps1_types.h $(CURDIR)/psx_compat.h $(CURDIR)/psx_press.h
	@mkdir -p $(dir $@)
	$(CC) $(PRESS_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/stdwtitl_data.o: $(TITLE_ROOT)/data/stdwtitl.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/title_data.o: $(TITLE_ROOT)/data/title.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/movie_data.o: $(TITLE_ROOT)/data/movie.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T44: the name entry's units (see PLAYER_OBJS). The same drawing-unit
# treatment as the title's (PsyCross headers, C11), one rule per source
# because the object names are prefixed to stay distinct (see note above).
$(BUILD)/plnmet_scene.o: $(PLAYER_ROOT)/scene.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_screen.o: $(PLAYER_ROOT)/screen.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_backdrop.o: $(PLAYER_ROOT)/backdrop.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_welcome.o: $(PLAYER_ROOT)/welcome.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_name_entry.o: $(PLAYER_ROOT)/name_entry.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_choice.o: $(PLAYER_ROOT)/choice.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_confirm.o: $(PLAYER_ROOT)/confirm.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/plnmet_data.o: $(PLAYER_ROOT)/data/stplnmet.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T18: the libpress surface over the port's MDEC (shim/host_press.c, real since
# T39). It answers the movie player's calls with PsyCross's own prototypes (see
# PRESS_CFLAGS) and decodes through shim/host_mdec.c.
$(BUILD)/host_press.o: shim/host_press.c shim/host_mdec.h shim/host_st.h shim/host_video.h shim/video_mod.h $(CURDIR)/ps1_types.h $(CURDIR)/psx_compat.h $(CURDIR)/psx_press.h
	@mkdir -p $(dir $@)
	$(CC) $(PRESS_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T38: the St stream source (shim/host_st.c): libcd's streaming half answers
# the game's own movie player with whole frames, under the same 32-bit u_long
# and the same libpress prototypes as movie.o and host_press.o.
$(BUILD)/host_st.o: shim/host_st.c shim/host_st.h $(CURDIR)/ps1_types.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(PRESS_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# T19: the field mode's own units (build/field/, see FIELD_MODE_OBJS). They are
# drawing units: PsyCross headers, C11, FIELD_CFLAGS (see above), and the
# game's own include/ and src/ plus src/field/stages (its stages' shared
# code). -ffunction-sections/-fdata-sections keeps the field's sections
# separate, as the milestone-4 data object already is.
$(BUILD)/field/%.o: $(FIELD_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# T22: the host's copy of the field's file loader (pc/field/file_loader.c),
# built under the object name the upstream glob used to own. This explicit rule
# wins over the pattern rule above, so FIELD_MODE_OBJS' substitution compiles
# the host unit instead of src/field/field_mode/file_loader.c (see the file for
# the one guard it adds).
$(BUILD)/field/file_loader.o: field/file_loader.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# battle-entry T1: the host's copy of the balloons (pc/field/balloon.c), same
# arrangement as the file loader above.
$(BUILD)/field/balloon.o: field/balloon.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# digilab hang: the host's copy of the field's state methods (pc/field/state.c),
# whose pickStage clears FieldState up to defaultStart by name, not 100 bytes.
$(BUILD)/field/state.o: field/state.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# content-export E6b: the host's copy of the stream tasks (pc/field/stream.c), with the hook that lets a mod's PNG replace a stage tile.
$(BUILD)/field/stream.o: field/stream.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/field/fieldstg.o: $(FIELD_ROOT)/data/fieldstg.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# T3: the matrix data (see GAME_MATRIX_SRC): a plain engine-data unit, retail
# headers and gnu89 (it builds no primitives).
$(BUILD)/matrices.o: $(GAME_MATRIX_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T19: the stage overlays (build/stages/, see FIELD_STAGE_OBJS). Each is
# compiled like the field's units and then its globals are renamed with the
# stage's name, WSTAG###_, the way the PS1 build's `<stage>_syms.ld` rule
# renames every global of a stage overlay (nm | awk '$$2 ~ /^[TDRBSG]$$/'): the
# stages give their startStage, setupStage and shared steppers the same names,
# so without the prefix two stages linked into one host binary would collide.
# Only the defined globals are renamed; the references to FIELDSTG_* and to
# PsyQ stay as they are. The stage's name is the file's, uppercased, without a
# head suffix (a stage may have one head data unit, wstag924_head.c). A name starting with a dot
# (MinGW's COMDAT `.refptr.<sym>` stubs) is never renamed: the linker finds its COMDAT by name.
stage_prefix = $(shell echo $(notdir $(basename $(1))) | sed 's/_head$$//' | tr a-z A-Z)
$(BUILD)/stages/%.o: $(FIELD_STAGE_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@
	$(NM) --defined-only $@ | awk -v s=$(call stage_prefix,$*) '$$2 ~ /^[TDRBSG]$$/ && $$3 !~ /^\./ { printf "%s %s_%s\n", $$3, s, $$3 }' > $@.syms
	$(OBJCOPY) --redefine-syms=$@.syms $@

# WSTAG924 is the one stage with a head unit (wstag924_head.c defines
# stageColor), and wstag924.c reads that symbol by the name the head gives it.
# The PS1 link renames both at once; here each object is renamed on its own, so
# the unit's reference gets the same prefix explicitly.
$(BUILD)/stages/amaterasu_city/wstag924.o: $(FIELD_STAGE_ROOT)/amaterasu_city/wstag924.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FIELD_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@
	$(NM) --defined-only $@ | awk '$$2 ~ /^[TDRBSG]$$/ && $$3 !~ /^\./ { printf "%s WSTAG924_%s\n", $$3, $$3 }' > $@.syms
	printf 'stageColor WSTAG924_stageColor\n' >> $@.syms
	$(OBJCOPY) --redefine-syms=$@.syms $@

# T3: the battle mode's units (build/battle/, see BATTLE_MODE_OBJS). The same
# drawing-unit treatment as the field's: PsyCross headers, C11, BATTLE_CFLAGS,
# and the game's own include/ and src/. The battle's data names the retail
# libgs's GsCOORDINATE2/GsRVIEW2 types and calls GsSetRefView2 and friends,
# which the shim host_gs.c (also compiled against the retail libgs.h) stands
# in for -- PsyCross has no Gs* at all. Its units keep their sections (like
# the field's), because WFIGHTMN's data unit alone would otherwise drag the
# whole battle world's unreachable globals into the link.
$(BUILD)/battle/%.o: $(BATTLE_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# M1: the field menu's units (build/fmenu/, see FMENU_OBJS)
$(BUILD)/fmenu/%.o: $(FMENU_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# M2: the memory card screen's units (build/mcard/, see MCARD_OBJS)
$(BUILD)/mcard/%.o: $(MCARD_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h $(CURDIR)/menus/inc/menus/memory_card.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) -I$(CURDIR)/menus/inc $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# M5: the lab's, the partner name entry's and the gym's units
$(BUILD)/dlab/%.o: $(DLAB_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -I$(dir $<) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/pname/%.o: $(PNAME_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# STGTRAIN_requestFile clears `0x2D8` bytes from &STGTRAIN_state.data: on the
# PS1 that is data, fileIndex and sets[9] (up to `trainings`, offset 0x2DC), on
# the host the pointers make the same span 0x2D8 bytes of something else (the
# sets come out dirty, or the pointers after them get zeroed). Compiled from
# the source with that one size rewritten to the span between the members; the
# rule fails when the line is not there any more.
GYM_CLEAR_OLD := HEAP\.zero(&STGTRAIN_state\.data, 0x2D8)
GYM_CLEAR_NEW := HEAP.zero(\&STGTRAIN_state.data, __builtin_offsetof(TrainState, trainings) - __builtin_offsetof(TrainState, data))
$(BUILD)/gym/files.o: $(GYM_SRC_ROOT)/files.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	sed 's/$(GYM_CLEAR_OLD)/$(GYM_CLEAR_NEW)/' $< > $(BUILD)/gym/files.host.c
	grep -q __builtin_offsetof $(BUILD)/gym/files.host.c
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -I$(dir $<) -fdata-sections -ffunction-sections -c $(BUILD)/gym/files.host.c -o $@

$(BUILD)/gym/%.o: $(GYM_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# M4: the item shop's and the card shop's units
$(BUILD)/ishop/%.o: $(ISHOP_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/cshop/%.o: $(CSHOP_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# M6a: the deck editor's and the card album's units
$(BUILD)/deck/%.o: $(DECK_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/album/%.o: $(ALBUM_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# M6b: the card battle's units
$(BUILD)/cgame/%.o: $(CGAME_SRC_ROOT)/%.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# T3: WFIGHTTS, the battle test (src/debug/battle_test/), the unit
# FIGHTSTG_updateRoot starts when the mode's argument is nonzero.
# battle-entry T11: the host's copy of WFIGHTMN's menu (pc/battle/menu.c), one
# guard against `% 0` in WFIGHTMN_endBattle. An explicit rule, so it wins over
# the pattern rule above; same flags as the upstream unit's.
$(BUILD)/battle/normal_battle/menu.o: battle/menu.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/battle/battle_test.o: $(BATTLE_TEST_ROOT)/battle_test.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -fdata-sections -ffunction-sections -c $< -o $@

# T3: the three GTE units of the battle (draw.c, interp.c, model_mesh.c)
# include the game's engine/gte.h, whose primitives are MIPS assembler on the
# PS1 (see the header): the shim's battle_inc/ stands in for the whole header,
# PsyCross's host-emulated GTE instead of the asm, with the same include
# shape (inline_c.h + gtemac.h, the primitives and the composites both keep
# their original names). It goes ahead of the game's include/ so the quoted
# "engine/gte.h" resolves here; only these rules use BATTLE_INC, so no other
# unit ever meets the shadow.
BATTLE_INC := -I$(CURDIR)/shim/battle_inc $(DRAW_INC)

$(BUILD)/battle/battle_mode/draw.o: $(BATTLE_SRC_ROOT)/battle_mode/draw.c $(CURDIR)/psx_compat.h $(CURDIR)/shim/battle_inc/engine/gte.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(BATTLE_INC) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/battle/battle_mode/interp.o: $(BATTLE_SRC_ROOT)/battle_mode/interp.c $(CURDIR)/psx_compat.h $(CURDIR)/shim/battle_inc/engine/gte.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(BATTLE_INC) -fdata-sections -ffunction-sections -c $< -o $@

$(BUILD)/battle/battle_mode/model_mesh.o: $(BATTLE_SRC_ROOT)/battle_mode/model_mesh.c $(CURDIR)/psx_compat.h $(CURDIR)/shim/battle_inc/engine/gte.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(BATTLE_INC) -fdata-sections -ffunction-sections -c $< -o $@

# T19: the field's menu, text and fade units. src/engine/menu/ and
# src/engine/gfx/ have no pattern rule of their own (only
# gfx/{sprite_drawer,tim_loader}.c had one), and src/engine/text/'s pattern
# rule is the retail world's. These six build GPU primitives (POLY_F4/POLY_FT4,
# SPRT, DR_TPAGE) and Layers for the field, so they are drawing units like the
# field itself: PsyCross's headers and its host packet layout, C11 -- a retail
# primitive on a PsyCross ordering table is a different struct (T12). Only
# talk_box.c warns, and for the same reason the text engine does (an unused
# stack variable the game keeps on purpose, and a char*/u8* pass), stopped per
# unit. The field's other engine units (game/events.c, data/game_bss.c,
# file/decompressor.c) build no primitives and keep the retail world's rules.
$(BUILD)/field_menu.o: $(ROOT)/src/engine/menu/field_menu.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/inn.o: $(ROOT)/src/engine/menu/inn.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/screen_fade.o: $(ROOT)/src/engine/gfx/screen_fade.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/cursor.o: $(ROOT)/src/engine/text/cursor.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/message_box.o: $(ROOT)/src/engine/text/message_box.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/talk_box.o: $(ROOT)/src/engine/text/talk_box.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) -Wno-unused-variable $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# L4b: the wider talk box. speech.c (the only caller of createTalkBox) is compiled with the call
# renamed to pcCreateTalkBox, the wrapper in shim/talk_box_wide.c (a drawing unit: it reads the game's
# TalkBox through the same headers talk_box.c does). The upstream sources stay untouched.
$(BUILD)/talk_box_wide.o: shim/talk_box_wide.c shim/talk_box_wide.h shim/text_data.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -Ishim -c $< -o $@

$(BUILD)/field/speech.o: FIELD_CFLAGS += -DcreateTalkBox=pcCreateTalkBox

# L4c: single-line fields fit their text by auto-scale. The objects that build the fields (item names,
# equipment rows, deck rows...) are compiled with createTextWindow renamed to their own wrapper in
# shim/text_fit.c (a drawing unit, like talk_box_wide.o); the upstream sources stay untouched. After
# changing one of these -D flags delete the .o: Make does not track them.
$(BUILD)/text_fit.o: shim/text_fit.c shim/text_fit.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -Ishim -c $< -o $@

$(BUILD)/ishop/item_list.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_ishop_list
$(BUILD)/ishop/shop.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_ishop_shop -DinitSpriteDrawer=pcFitSprite_ishop
$(BUILD)/fmenu/item_list.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_fmenu_items
$(BUILD)/fmenu/equip_panel.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_fmenu_equip
$(BUILD)/fmenu/status_screen.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_fmenu_status
$(BUILD)/fmenu/item_screen.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_fmenu_iscreen
$(BUILD)/deck/editor.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_deck_editor
$(BUILD)/deck/screen.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_deck_screen
$(BUILD)/cshop/shop.o: FMENU_CFLAGS += -DcreateTextWindow=pcFitWin_card_shop -DinitSpriteDrawer=pcFitSprite_cshop

# The game's own random numbers (system/random.c), the one unit from
# src/engine/system/ the boot path needs. It gets its own explicit rule
# because `system/heap.c` and the shim's `shim/heap.c` would both want to be
# build/heap.o: a pattern rule for that directory would shadow the shim's.
$(BUILD)/random.o: $(ROOT)/src/engine/system/random.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# The game's own party, items, cards, Digimon and play time (game/*.c), the
# pad (pad/*.c) and the memory card (memcard/memcard.c). The rules go after
# the shim's below? They are here, before it, and their object names do not
# collide with any of the shim's -- see the note on random.o.
$(BUILD)/%.o: $(ROOT)/src/engine/game/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/src/engine/pad/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/src/engine/memcard/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T9: file_table.c is the only unit that knows about the es-419 redirect. Its
# pattern rule (just below) compiles it without a header dependency, so we
# override it with one whose prerequisites include the generated header when
# ES419=1: a stale es419_table.h would otherwise leave file_table.o referring
# to the old sectors. ES419=0 leaves the dependency empty.
$(BUILD)/file_table.o: $(ROOT)/src/engine/file/file_table.c $(ES419_TABLE_H)
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/src/engine/file/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/src/engine/data/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/src/engine/task/%.c
	@mkdir -p $(dir $@)
	$(CC) $(GAME_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: $(ROOT)/src/engine/text/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TEXT_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# The battle cursor bar's DR_MOVE recorder/presenter: a drawing unit (PsyCross
# headers, POLY_FT4 quads and an ordering table).
$(BUILD)/host_battle.o: shim/host_battle.c shim/host.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/%.o: shim/%.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# The card's default path comes from SDL_GetPrefPath (next to the settings).
$(BUILD)/host_memcard.o: shim/host_memcard.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. $(SDL_CFLAGS) -c $< -o $@

# M5: the card's move from the old Linux place (memcardtest.c includes the unit).
.PHONY: memcardtest
memcardtest: $(BUILD)/memcardtest
	$(BUILD)/memcardtest

$(BUILD)/memcardtest: memcardtest.c shim/mod_stages.h shim/host_memcard.c shim/save_file.h shim/save_view.h shim/ext_state.h shim/card_image.h shim/host_memcard.h $(BUILD)/ext_state.o $(BUILD)/save_file.o $(BUILD)/save_view.o $(BUILD)/text_codec.o $(BUILD)/port_log.o $(BUILD)/port_paths.o $(BUILD)/card_image.o $(BUILD)/mod_stages.o $(BUILD)/stage_mods.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/text_data.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o $(BUILD)/map_layers.o $(BUILD)/map_pack.o
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) $(SDL_CFLAGS) -I. memcardtest.c $(BUILD)/ext_state.o $(BUILD)/save_file.o $(BUILD)/save_view.o $(BUILD)/text_codec.o $(BUILD)/port_log.o $(BUILD)/port_paths.o $(BUILD)/card_image.o $(BUILD)/mod_stages.o $(BUILD)/stage_mods.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/text_data.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o $(BUILD)/map_layers.o $(BUILD)/map_pack.o -o $@

# extended-state T2 (ADR-008 option B): dw3save-v1, the port's own save file (shim/save_file.c; plain C, no game headers) and its
# host test (container, CRC, torn-write fallback) together with the extension chunk of shim/ext_state.c.
$(BUILD)/save_file.o: shim/save_file.c shim/save_file.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: savetest
savetest: $(BUILD)/savetest
	$(BUILD)/savetest

$(BUILD)/savetest: savetest.c shim/save_file.h shim/save_view.h shim/ext_state.h $(BUILD)/save_file.o $(BUILD)/save_view.o $(BUILD)/text_codec.o $(BUILD)/ext_state.o $(BUILD)/port_log.o
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. savetest.c $(BUILD)/save_file.o $(BUILD)/save_view.o $(BUILD)/text_codec.o $(BUILD)/ext_state.o $(BUILD)/port_log.o -o $@

# extended-state T3b: the read-only readable view of a save (shim/save_view.c); game headers for GameState.
$(BUILD)/save_view.o: shim/save_view.c shim/save_view.h shim/ext_state.h shim/text_codec.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# content core (ADR-009): the shared machinery of the content types (shim/content.c)
$(BUILD)/content.o: shim/content.c shim/content.h shim/mods.h shim/text_data.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# extended techs T2/T3: new techniques from content mods (shim/tech_ext.c): registry, ids, store, learnedBy, names.
$(BUILD)/tech_ext.o: shim/tech_ext.c shim/tech_ext.h shim/content.h shim/ext_state.h shim/mods.h shim/text_data.h shim/port_log.h $(ROOT)/include/engine/tech_access.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# Digimon slice 1 (ADR-009): changes to existing Digimon from content mods (shim/digimon_ext.c).
$(BUILD)/digimon_ext.o: shim/digimon_ext.c shim/digimon_ext.h shim/model_bake.h shim/content.h shim/tech_ext.h shim/item_ext.h shim/mods.h shim/text_data.h shim/port_log.h $(ROOT)/include/engine/tech_access.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# extended-state T3: new items from content mods (shim/item_ext.c): registry, ids, ITEM_FUNCS, names.
$(BUILD)/item_ext.o: shim/item_ext.c shim/item_ext.h shim/content.h shim/ext_state.h shim/mods.h shim/text_data.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# U8b: new cards from content mods (shim/card_ext.c): registry, ids, card image and effect records, names.
$(BUILD)/card_ext.o: shim/card_ext.c shim/card_ext.h shim/content.h shim/ext_state.h shim/mods.h shim/text_data.h shim/png_read.h shim/port_log.h $(ROOT)/include/cardgame/card_access.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# content-export E5a: the equipment and item effect tables (shim/game_tables.c).
$(BUILD)/game_tables.o: shim/game_tables.c shim/game_tables.h shim/table_mods.h shim/item_ext.h shim/content.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T34: the sound shim's SPU backend (shim/snd_spu.c) speaks to PsyCross's own
# SPU renderer through PsyCross's libspu.h, so it compiles with that include
# path in front of the game's retail one: libspu.h, SpuVoiceAttr and the
# SPU_VOICE_* masks all come from PsyCross (external/psycross), whose LIBSPU.C
# is already the one linked into these binaries. The retail PsyQ headers
# promise the same wrappers; PsyCross's are what its own C keeps.
SNDSPU_CFLAGS := $(HOST_CFLAGS) -I$(PSYCROSS_ROOT)/include

$(BUILD)/snd_spu.o: shim/snd_spu.c shim/snd_internal.h shim/audio_mod.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(SNDSPU_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T35: the SEP parser and player (shim/snd_sep.c) builds next to the SPU
# backend with the same include path and calls into it (the real sink's
# sndPickTone/sndKeyOn live there), so it shares the object flags.
$(BUILD)/snd_sep.o: shim/snd_sep.c shim/snd_internal.h shim/audio_mod.h
	@mkdir -p $(dir $@)
	$(CC) $(SNDSPU_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# O4: the player's volume sliders (shim/snd_vol.c), pure: sndtest and inputtest link it too.
$(BUILD)/snd_vol.o: shim/snd_vol.c shim/snd_internal.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T36: the XA stream driver (shim/host_xa.c): the pure classifier/decoder and
# the injectable driver (host_cd.c installs the disc reader, host_snd.c the
# OpenAL sink) in one unit. OpenAL comes from the system headers, so it builds
# with the plain shim flags; its OpenAL sink is never touched by sndtest,
# which injects a recording one.
$(BUILD)/host_xa.o: shim/host_xa.c shim/host_xa.h shim/snd_internal.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T34: the unit test of that backend. It builds a synthetic VH/VB fixture in
# code (documented in sndtest.c) and drives sndParseVab, sndVabOpen/TransBody,
# sndNotePrepare and a key-on/key-off pair through them; T35 adds the SEP
# fixture (sndParseSep, sndSepOpen and the player through a recording sink);
# T36 adds the XA fixture (sndXaClassify/sndXaDecodeAudio and the stream driver
# over a fake source and a recording sink, no OpenAL). It needs the PsyCross
# library for the real PsyX_SPUAL side, exactly as the harnesses above do. No
# disc and no window: this runs anywhere.
.PHONY: sndtest
sndtest: $(BUILD)/sndtest

$(BUILD)/sndtest: $(BUILD)/sndtest.o $(BUILD)/snd_spu.o $(BUILD)/snd_sep.o $(BUILD)/snd_vol.o $(BUILD)/host_snd.o $(BUILD)/host_xa.o $(BUILD)/audio_mod.o $(BUILD)/wav_read.o $(BUILD)/vag_encode.o $(BUILD)/port_log.o psycross
	$(CC) $(BUILD)/sndtest.o $(BUILD)/snd_spu.o $(BUILD)/snd_sep.o $(BUILD)/snd_vol.o $(BUILD)/host_snd.o \
		$(BUILD)/audio_mod.o $(BUILD)/wav_read.o $(BUILD)/vag_encode.o $(BUILD)/port_log.o $(BUILD)/host_xa.o $(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) -o $@

# SDL's own headers (nothing else in pc/ includes SDL.h directly). The input
# units include no game header, so they skip psx_compat.h and the game's
# strlen/memcpy declarations that clash with <string.h>.
SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo -I/usr/include/SDL2)
ifneq ($(filter windows android,$(TARGET)),)
SDL_CFLAGS :=
endif
INPUT_CFLAGS := $(HOST_CFLAGS) -std=gnu11 -fbuiltin

# Feature controls-config (C1/C2): the controls of settings.ini and press-to-bind
# (shim/input_cfg.c). The unit is a drawing-world one (PsyCross's PsyX_public.h
# and SDL's headers); inputtest drives its pure functions and the capture state
# machine with synthetic SDL events, so no window, pad or disc is needed. It
# links the PsyCross library for g_cfg_keyboardMapping/g_cfg_controllerMapping.
$(BUILD)/input_cfg.o: shim/input_cfg.c shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# C3: the controls screen's model and input (no game or drawing dependency, so
# inputtest drives it) and its drawing (the game's font, a drawing unit).
$(BUILD)/controls_screen.o: shim/controls_screen.c shim/controls_screen.h shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# O2: the Video and Display rows (PsyCross globals and SDL; pure helpers inputtest drives)
$(BUILD)/options_video.o: shim/options_video.c shim/options_video.h shim/options_system.h shim/controls_screen.h shim/auto_text.h shim/battle_speed.h shim/stage_banner.h shim/fast_transitions.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# system-menu: the System tab (Restart game, Quit game) and its confirmation model (inputtest drives it)
$(BUILD)/options_system.o: shim/options_system.c shim/options_system.h shim/options_mods.h shim/controls_screen.h shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# modding-ux U6: the Mods tab (one row per mod from the report of modsInit; inputtest drives it)
$(BUILD)/options_mods.o: shim/options_mods.c shim/options_mods.h shim/mods.h shim/controls_screen.h shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# battle-speed: the setting and the scaled clock step (the gate is in gfx_engine.c)
$(BUILD)/battle_speed.o: shim/battle_speed.c shim/battle_speed.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# fast-transitions: the three options' settings, rows and Fast timings (the game side is in stage_timing.c)
$(BUILD)/fast_transitions.o: shim/fast_transitions.c shim/fast_transitions.h shim/controls_screen.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# stage-banner: the option's setting and Fast timings (the game side is in stage_timing.c / gfx_engine.c)
$(BUILD)/stage_banner.o: shim/stage_banner.c shim/stage_banner.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# O6: auto text (the pure timing and decision; the game side is in host_pad.c)
$(BUILD)/auto_text.o: shim/auto_text.c shim/auto_text.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# O3: the Language row (pure mapping to the game's LANGUAGE; the game side is in host_pad.c)
$(BUILD)/options_language.o: shim/options_language.c shim/options_language.h shim/controls_screen.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# O4: the Audio rows (the sliders of shim/snd_vol.c; pure mapping inputtest drives)
$(BUILD)/options_audio.o: shim/options_audio.c shim/options_audio.h shim/controls_screen.h shim/snd_internal.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# C4: the last used device and controller family, the button icon lookup and the atlas pixels
# (generated by gen_prompt_atlas.py from Kenney's pack, committed). No game dependency.
$(BUILD)/input_device.o: shim/input_device.c shim/input_device.h shim/controls_screen.h shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/prompt_icons.o: shim/prompt_icons.c shim/prompt_icons.h shim/input_device.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/prompt_atlas_data.o: shim/prompt_atlas_data.c shim/prompt_icons.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# C5: the game's own button icons inside its text: the plan and the pixels (no game dependency, inputtest
# drives it) and the VRAM side (a drawing unit, called once a frame by gfx_engine.c)
$(BUILD)/text_icons.o: shim/text_icons.c shim/text_icons.h shim/prompt_icons.h shim/input_device.h shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

# O5b: the window art of the options menu (the game's panel and cursor read from the disc): pure parsing, layout and
# timing, no game dependency (inputtest drives it); the VRAM side is in controls_draw.c
$(BUILD)/menu_frame.o: shim/menu_frame.c shim/menu_frame.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -c $< -o $@

$(BUILD)/text_icons_draw.o: shim/text_icons_draw.c shim/text_icons.h shim/host_video.h shim/input_device.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/controls_draw.o: shim/controls_draw.c shim/controls_screen.h shim/options_video.h shim/host_video.h shim/input_device.h shim/prompt_icons.h shim/menu_frame.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(DRAW_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

# W2: the player's first run (shim/disc_picker.c: pure checks inputtest drives, plus the dialogs) and the
# vendored native file dialog (zlib, unmodified; its own warnings are not ours).
TFD_DIR := $(ROOT)/third_party/tinyfiledialogs
$(BUILD)/disc_picker.o: shim/disc_picker.c shim/disc_picker.h shim/disc_fd.h shim/disc_flow.h shim/input_cfg.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -I$(TFD_DIR) -c $< -o $@

$(BUILD)/tinyfiledialogs.o: $(TFD_DIR)/tinyfiledialogs.c $(TFD_DIR)/tinyfiledialogs.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -w -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/inputtest.o: inputtest.c shim/disc_picker.h shim/input_cfg.h shim/controls_screen.h shim/options_video.h shim/options_language.h shim/options_audio.h shim/options_system.h shim/snd_internal.h shim/input_device.h shim/prompt_icons.h shim/text_icons.h shim/menu_frame.h shim/auto_text.h shim/battle_speed.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -I. -c $< -o $@

.PHONY: inputtest
inputtest: $(BUILD)/inputtest

$(BUILD)/inputtest: $(BUILD)/inputtest.o $(BUILD)/disc_fd.o $(BUILD)/disc_picker.o $(BUILD)/tinyfiledialogs.o $(BUILD)/input_cfg.o $(BUILD)/port_paths.o $(BUILD)/port_paths_sdl.o $(BUILD)/port_log.o $(BUILD)/controls_screen.o $(BUILD)/options_video.o $(BUILD)/options_language.o $(BUILD)/options_audio.o $(BUILD)/options_system.o $(BUILD)/options_mods.o $(BUILD)/auto_text.o $(BUILD)/battle_speed.o $(BUILD)/stage_banner.o $(BUILD)/fast_transitions.o $(BUILD)/state_scope.o $(BUILD)/snd_vol.o $(BUILD)/mods.o $(BUILD)/text_data.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o \
		$(BUILD)/input_device.o $(BUILD)/prompt_icons.o $(BUILD)/prompt_atlas_data.o $(BUILD)/text_icons.o $(BUILD)/menu_frame.o psycross
	$(CC) $(BUILD)/inputtest.o $(BUILD)/disc_fd.o $(BUILD)/disc_picker.o $(BUILD)/tinyfiledialogs.o $(BUILD)/input_cfg.o $(BUILD)/port_paths.o $(BUILD)/port_paths_sdl.o $(BUILD)/port_log.o $(BUILD)/controls_screen.o $(BUILD)/options_video.o $(BUILD)/options_language.o $(BUILD)/options_audio.o $(BUILD)/options_system.o $(BUILD)/options_mods.o $(BUILD)/auto_text.o $(BUILD)/battle_speed.o $(BUILD)/stage_banner.o $(BUILD)/fast_transitions.o $(BUILD)/state_scope.o $(BUILD)/snd_vol.o $(BUILD)/mods.o $(BUILD)/text_data.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o \
		$(BUILD)/input_device.o $(BUILD)/prompt_icons.o $(BUILD)/prompt_atlas_data.o $(BUILD)/text_icons.o $(BUILD)/menu_frame.o $(PSYCROSS_BUILD)/libpsycross.a \
		$(PSYCROSS_LIBS) $(HARNESS_GL_LIB) -o $@

# The System tab's restart handoff on the real process APIs: the new process must start only after the old one is gone.
# Native: execv (nothing overlaps). `make -C pc TARGET=windows check-restart` runs the .exe through Wine (CreateProcess + --wait-pid).
$(BUILD)/restarttest.o: restarttest.c shim/options_system.h
	@mkdir -p $(dir $@)
	$(CC) $(INPUT_CFLAGS) $(GAME_DEFS) $(DRAW_INC) $(SDL_CFLAGS) -I. -c $< -o $@

$(BUILD)/restarttest$(EXE): $(BUILD)/restarttest.o $(BUILD)/options_system.o
	$(CC) $^ -L$(DW3_WIN_CACHE)/sdl2/lib -lSDL2 -o $@

.PHONY: check-restart
check-restart: $(BUILD)/restarttest$(EXE)
	@out=$(BUILD)/restarttest.out; : > $$out; : > $$out.trace; \
	RT_OUT=$(CURDIR)/$$out $(if $(filter windows,$(TARGET)),WINEDEBUG=-all wine) $(BUILD)/restarttest$(EXE) >/dev/null 2>&1; \
	for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20; do grep -q new-start $$out 2>/dev/null && break; sleep 0.5; done; \
	cat $$out; sleep 1; cat $$out.trace; \
	$(if $(filter windows,$(TARGET)),grep -q "the log is open" $$out.trace ||,true ||) { echo "check-restart: FAILED (the trace lacks the new process's steps)"; exit 1; }; \
	end=$$(awk '/old-end/ {print $$1}' $$out); st=$$(awk '/new-start/ {print $$1}' $$out); \
	$(if $(filter windows,$(TARGET)),test -n "$$end" &&) test -n "$$st" && { test -z "$$end" || test "$$st" -ge "$$end"; } && echo "check-restart: ok (old ended at $$end, new started at $$st)" \
	  || { echo "check-restart: FAILED (the new process did not wait, or never started)"; exit 1; }

# G1: the battle's element boost (pc/battletest.c). Links the real damage.c unit; the rest of the
# battle is not needed (getElementBoost only reads the battle state), so the unresolved references
# of that unit are ignored at link time.
.PHONY: battletest
battletest: $(BUILD)/battletest

$(BUILD)/battletest.o: battletest.c $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(BATTLE_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

$(BUILD)/battletest: $(BUILD)/battletest.o $(BUILD)/battle/battle_mode/damage.o $(BUILD)/battle/battle_mode/battle.o
	$(CC) $^ -Wl,--unresolved-symbols=ignore-all -o $@

# L2a: the text codec (shim/text_codec.c), pure stdlib: texttest runs the synthetic cases and, given
# `<disc-dir> <overlay-dir> <work-dir>`, the local-only parity run on the owner's disc.
.PHONY: texttest
texttest: $(BUILD)/texttest

$(BUILD)/text_codec.o: shim/text_codec.c shim/text_codec.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/text_data.o: shim/text_data.c shim/port_log.h shim/port_fs.h shim/talk_box_wide.h shim/text_fit.h shim/text_data.h shim/text_codec.h shim/text_reflow.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# L4a: the automatic reflow (shim/text_reflow.c). Pure stdlib; it reads the game's glyph tables through
# weak references, so texttest (no game objects) links too and the boot/text_measure binaries get them.
$(BUILD)/text_reflow.o: shim/text_reflow.c shim/text_reflow.h shim/text_codec.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/texttest.o: texttest.c shim/talk_box_wide.h shim/text_fit.h shim/text_codec.h shim/text_data.h shim/text_reflow.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/texttest: $(BUILD)/texttest.o $(BUILD)/text_codec.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_reflow.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# ADR-005: the mods scan and the flat JSON reader (shim/mods.c) and the config files (shim/port_config.c).
# Pure stdlib plus the file helpers of text_data.c, so configtest and the boot binary share them.
$(BUILD)/mods.o: shim/mods.c shim/mods.h shim/text_data.h shim/port_version.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# U5: --list-mods, --validate-mod, --new-mod (shim/mod_tools.c): pure stdlib plus mods.h, table_mods.h and text_data.h.
$(BUILD)/mod_tools.o: shim/mod_tools.c shim/mod_tools.h shim/mods.h shim/table_mods.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/port_config.o: shim/port_config.c shim/port_config.h shim/mods.h shim/text_data.h shim/stage_banner.h shim/fast_transitions.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# ADR-010: the path layer (shim/port_paths.c, pure C; shim/port_paths_sdl.c is its platform hook) and its host test.
$(BUILD)/port_paths.o: shim/port_paths.c shim/port_paths.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/port_paths_sdl.o: shim/port_paths_sdl.c shim/port_paths.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) $(SDL_CFLAGS) -c $< -o $@

.PHONY: pathstest
pathstest: $(BUILD)/pathstest
	$(BUILD)/pathstest

$(BUILD)/pathstest.o: pathstest.c shim/port_paths.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/pathstest: $(BUILD)/pathstest.o $(BUILD)/port_paths.o
	$(CC) $^ -o $@

# Android T5: the es-419 overlay shipped as APK assets and installed into the user folder (shim/l10n_install.c, pure C).
$(BUILD)/l10n_install.o: shim/l10n_install.c shim/l10n_install.h shim/port_paths.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: l10ntest
l10ntest: $(BUILD)/l10ntest
	$(BUILD)/l10ntest

$(BUILD)/l10ntest.o: l10ntest.c shim/l10n_install.h shim/port_paths.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/l10ntest: $(BUILD)/l10ntest.o $(BUILD)/l10n_install.o $(BUILD)/port_paths.o
	$(CC) $^ -o $@

# Android: the app's folders group-writable for a PC's adb push (shim/android_perms.c, pure POSIX).
$(BUILD)/android_perms.o: shim/android_perms.c shim/android_perms.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/app_lifecycle.o: shim/app_lifecycle.c shim/app_lifecycle.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/app_lifecycle_sdl.o: shim/app_lifecycle_sdl.c shim/app_lifecycle.h shim/app_lifecycle_sdl.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. $(SDL_CFLAGS) -c $< -o $@

.PHONY: applifetest
applifetest: $(BUILD)/applifetest
	$(BUILD)/applifetest

$(BUILD)/applifetest.o: applifetest.c shim/app_lifecycle.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/applifetest: $(BUILD)/applifetest.o $(BUILD)/app_lifecycle.o
	$(CC) $^ -o $@

# android T7a: the on-screen virtual controller. touch_router.c / touch_pad.c are pure (no SDL); touch_sdl.c is the event
# watch, the Display rows and the drawing through PsyCross' overlay hook. touchtest drives the pure parts.
$(BUILD)/touch_router.o: shim/touch_router.c shim/touch_router.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/touch_pad.o: shim/touch_pad.c shim/touch_pad.h shim/touch_router.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/touch_menu.o: shim/touch_menu.c shim/touch_menu.h shim/touch_router.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/touch_tap.o: shim/touch_tap.c shim/touch_tap.h shim/touch_router.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/touch_sdl.o: shim/touch_sdl.c shim/touch_sdl.h shim/touch_menu.h shim/touch_tap.h shim/touch_pad.h shim/touch_router.h shim/controls_screen.h $(PSYCROSS_ROOT)/include/PsyX/PsyX_overlay.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I$(PSYCROSS_ROOT)/include -I. $(SDL_CFLAGS) -c $< -o $@

.PHONY: touchtest
touchtest: $(BUILD)/touchtest
	$(BUILD)/touchtest

$(BUILD)/touchtest.o: touchtest.c shim/touch_pad.h shim/touch_router.h shim/touch_menu.h shim/touch_tap.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/touchtest: $(BUILD)/touchtest.o $(BUILD)/touch_pad.o $(BUILD)/touch_router.o $(BUILD)/touch_menu.o $(BUILD)/touch_tap.o
	$(CC) $^ -lm -o $@

# T7b: the Android disc start's pure decisions (start order, .cue detection, retry loop, messages).
.PHONY: discflowtest
discflowtest: $(BUILD)/discflowtest
	$(BUILD)/discflowtest

$(BUILD)/disc_flow.o: shim/disc_flow.c shim/disc_flow.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/disc_fd.o: shim/disc_fd.c shim/disc_fd.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/discflowtest.o: discflowtest.c shim/disc_flow.h shim/disc_fd.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/discflowtest: $(BUILD)/discflowtest.o $(BUILD)/disc_flow.o $(BUILD)/disc_fd.o
	$(CC) $^ -o $@

.PHONY: permstest
permstest: $(BUILD)/permstest
	$(BUILD)/permstest

$(BUILD)/permstest.o: permstest.c shim/android_perms.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/permstest: $(BUILD)/permstest.o $(BUILD)/android_perms.o
	$(CC) $^ -o $@

# port-log T1: the one log (shim/port_log.c): levels, channels, environment overrides, legacy shortcuts.
$(BUILD)/port_log.o: shim/port_log.c shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: logtest
logtest: $(BUILD)/logtest
	$(BUILD)/logtest

$(BUILD)/logtest.o: logtest.c shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/logtest: $(BUILD)/logtest.o $(BUILD)/port_log.o
	$(CC) $^ -o $@

# extended-state T1 (ADR-008): the port's extension of the per-id arrays in GAME (shim/ext_state.c) and its host test:
# original ids hit GAME, ids above hit the growable store, an out-of-range id never writes GAME.
$(BUILD)/ext_state.o: shim/ext_state.c shim/ext_state.h shim/content.h shim/port_log.h $(ROOT)/include/engine/item_access.h $(ROOT)/include/engine/tech_access.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

.PHONY: extstatetest
extstatetest: $(BUILD)/extstatetest
	$(BUILD)/extstatetest

$(BUILD)/extstatetest.o: extstatetest.c shim/ext_state.h shim/content.h shim/item_ext.h shim/tech_ext.h shim/mods.h shim/table_mods.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/extstatetest: $(BUILD)/extstatetest.o $(BUILD)/table_mods.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/item_ext.o $(BUILD)/card_ext.o $(BUILD)/png_read.o $(BUILD)/png_write.o $(BUILD)/tech_ext.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# Host test of the field-healing accessor (pcTechIsFieldHeal): original ids and mod techniques based on them.
.PHONY: fieldtechtest
fieldtechtest: $(BUILD)/fieldtechtest
	$(BUILD)/fieldtechtest

$(BUILD)/fieldtechtest.o: fieldtechtest.c shim/ext_state.h $(ROOT)/include/engine/tech_access.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/fieldtechtest: $(BUILD)/fieldtechtest.o $(BUILD)/table_mods.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/item_ext.o $(BUILD)/card_ext.o $(BUILD)/png_read.o $(BUILD)/png_write.o $(BUILD)/tech_ext.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# Host test of the item base accessor (pcItemBase): original ids and mod items based on them.
.PHONY: itembasetest
itembasetest: $(BUILD)/itembasetest
	$(BUILD)/itembasetest

$(BUILD)/itembasetest.o: itembasetest.c shim/ext_state.h $(ROOT)/include/engine/item_access.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/itembasetest: $(BUILD)/itembasetest.o $(BUILD)/table_mods.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/item_ext.o $(BUILD)/card_ext.o $(BUILD)/png_read.o $(BUILD)/png_write.o $(BUILD)/tech_ext.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# Host test of the armory's equip guard (STITSHOP_canEquip on a non-equipment item), with stubbed item data.
.PHONY: shopguardtest
shopguardtest: $(BUILD)/shopguardtest
	$(BUILD)/shopguardtest

$(BUILD)/shopguardtest.o: shop_guard_test.c
	@mkdir -p $(dir $@)
	$(CC) $(FMENU_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -I. -c $< -o $@

$(BUILD)/shopguardtest: $(BUILD)/shopguardtest.o $(BUILD)/ishop/equip.o
	$(CC) $^ -o $@

# Host test of the Digimon content type: validation, patching and restoring DIGIMON_DATA, the battle table patch.
.PHONY: digimontest
digimontest: $(BUILD)/digimontest
	$(BUILD)/digimontest

$(BUILD)/digimontest.o: digimontest.c shim/digimon_ext.h shim/content.h shim/item_ext.h shim/tech_ext.h shim/mods.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/digimontest: $(BUILD)/digimontest.o $(BUILD)/digimon_ext.o $(BUILD)/model_bake.o $(BUILD)/png_read.o $(BUILD)/table_mods.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/item_ext.o $(BUILD)/tech_ext.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@ -lm

# Model baker (model-import I5, ADR-011): the neutral source (glTF + PNG + model.json) to the game's battle model at load. It is
# a port of tools/model_import.py that must give the same bytes: no FMA contraction, so every double operation rounds as
# Python's does.
$(BUILD)/model_export.o: shim/model_export.c shim/model_export.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -MMD -MP -g $(WIN_DEFS) $(SDL_CFLAGS) -I. -Ishim -c $< -o $@

$(BUILD)/model_bake.o: shim/model_bake.c shim/model_bake.h shim/model_export.h shim/content.h shim/mods.h shim/port_fs.h shim/port_log.h shim/port_paths.h shim/png_read.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -MMD -MP -g $(WIN_DEFS) -I. -Ishim -c $< -o $@

# Host test of the model baker: the committed demo robot against the blob tools/model_import.py makes, the parity cases that the
# tool's test file writes (every conversion branch, if python3 is there), the cache, the registry, bad input and a byte-flip fuzz.
.PHONY: modeltest
modeltest: $(BUILD)/modeltest
	@if command -v python3 >/dev/null 2>&1; then \
		rm -rf $(BUILD)/parity && python3 -I $(ROOT)/tools/test_model_import.py --parity $(BUILD)/parity >/dev/null && \
		rm -rf $(BUILD)/xparity && $(BUILD)/modeltest test_fixtures/model $(BUILD)/parity $(BUILD)/xparity && \
		python3 -I $(ROOT)/tools/test_model_export.py --parity $(BUILD)/xparity; \
	else $(BUILD)/modeltest test_fixtures/model; fi

$(BUILD)/modeltest.o: modeltest.c shim/model_export.h shim/model_bake.h shim/content.h shim/mods.h shim/port_fs.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -MMD -MP -g $(WIN_DEFS) $(SDL_CFLAGS) -I. -Ishim -c $< -o $@

$(BUILD)/modeltest: $(BUILD)/modeltest.o $(BUILD)/model_bake.o $(BUILD)/model_export.o $(BUILD)/png_read.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@ -lm -lSDL2

# content core (ADR-009, content-core C1/C2): the shared machinery of the content types, with a made-up type.
.PHONY: contenttest
contenttest: $(BUILD)/contenttest
	$(BUILD)/contenttest

$(BUILD)/contenttest.o: contenttest.c shim/content.h shim/mods.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/contenttest: $(BUILD)/contenttest.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# configtest: defaults, reference copies, merge across data and mods in order, rejections (scratch under $$TMPDIR).
.PHONY: configtest
configtest: $(BUILD)/configtest
	$(BUILD)/configtest

$(BUILD)/configtest.o: configtest.c shim/mods.h shim/mod_tools.h shim/table_mods.h shim/port_config.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/configtest: $(BUILD)/configtest.o $(BUILD)/mods.o $(BUILD)/mod_tools.o $(BUILD)/table_mods.o $(BUILD)/port_log.o $(BUILD)/port_config.o $(BUILD)/port_fs.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o
	$(CC) $^ -o $@

# ADR-007: the data table patch engine (shim/table_mods.c). Pure stdlib plus mods.h and the file helpers of text_data.c.
$(BUILD)/table_mods.o: shim/table_mods.c shim/table_mods.h shim/mods.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# tabletest (also the equipment/effect table definitions of shim/game_tables.c, with stubbed game data): parse, operations, clamp, wildcard precedence, per-field rejection, mod order, idempotence, export (scratch under $$TMPDIR).
# gymlabeltest: the gym's intensity labels (shim/gym_labels.c), pure, synthetic strings in the game's encoding.
.PHONY: gymlabeltest
gymlabeltest: $(BUILD)/gymlabeltest
	$(BUILD)/gymlabeltest

$(BUILD)/gym_labels.o: shim/gym_labels.c shim/gym_labels.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) -O2 -Wextra -I. -c $< -o $@

$(BUILD)/gymlabeltest: gymlabeltest.c $(BUILD)/gym_labels.o shim/gym_labels.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) -O2 -Wextra -I. gymlabeltest.c $(BUILD)/gym_labels.o -o $@

.PHONY: tabletest
tabletest: $(BUILD)/tabletest
	$(BUILD)/tabletest

$(BUILD)/tabletest.o: tabletest.c shim/mods.h shim/table_mods.h shim/text_data.h shim/game_tables.h shim/item_ext.h shim/content.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -O2 -Wextra -Wno-unused-parameter -I. -c $< -o $@

$(BUILD)/tabletest: $(BUILD)/tabletest.o $(BUILD)/game_tables.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/table_mods.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# content-export E2a: the movie export (shim/jpeg_write.c, avi_write.c, video_export.c). videotest builds a synthetic raw disc (DC-only MDEC
# frames, XA audio sectors of made-up bytes), exports it and writes the files that pc/test_fixtures/video/check_video.py then checks
# on its own: PIL decodes every JPEG (PSNR), the AVI structure is parsed byte by byte (sizes, idx1, stream headers, interleave, audio).
# host_xa.o is linked for its pure XA decoder only (its OpenAL sink is never touched).
VIDEO_CFLAGS := -std=gnu11 -O2 -ffp-contract=off -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -MMD -MP -g $(WIN_DEFS) $(SDL_CFLAGS) -I. -Ishim

.PHONY: videotest
videotest: $(BUILD)/videotest
	rm -rf $(BUILD)/video-test && mkdir -p $(BUILD)/video-test
	@if command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" >/dev/null 2>&1; then python3 -I test_fixtures/video/gen_fixtures.py $(BUILD)/video-test; fi
	$(BUILD)/videotest $(BUILD)/video-test
	@if command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" >/dev/null 2>&1; then \
		python3 -I test_fixtures/video/check_video.py $(BUILD)/video-test; \
	else echo "videotest: python3 with PIL not found, the independent decode and AVI parse were skipped"; fi

$(BUILD)/jpeg_write.o: shim/jpeg_write.c shim/jpeg_write.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

$(BUILD)/avi_write.o: shim/avi_write.c shim/avi_write.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

# content-export E2b: the readers and the movie replacement (shim/jpeg_read.c, avi_read.c, video_mod.c)
$(BUILD)/jpeg_read.o: shim/jpeg_read.c shim/jpeg_read.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

$(BUILD)/avi_read.o: shim/avi_read.c shim/avi_read.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

$(BUILD)/video_mod.o: shim/video_mod.c shim/video_mod.h shim/avi_read.h shim/jpeg_read.h shim/video_export.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

$(BUILD)/video_export.o: shim/video_export.c shim/video_export.h shim/jpeg_write.h shim/avi_write.h shim/host_mdec.h shim/host_xa.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

$(BUILD)/videotest.o: videotest.c shim/video_export.h shim/jpeg_write.h shim/avi_write.h shim/jpeg_read.h shim/avi_read.h shim/video_mod.h shim/host_mdec.h shim/host_xa.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(VIDEO_CFLAGS) -c $< -o $@

$(BUILD)/videotest: $(BUILD)/videotest.o $(BUILD)/video_export.o $(BUILD)/jpeg_write.o $(BUILD)/avi_write.o $(BUILD)/jpeg_read.o $(BUILD)/avi_read.o $(BUILD)/video_mod.o $(BUILD)/host_mdec.o $(BUILD)/host_xa.o $(BUILD)/snd_vol.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o $(BUILD)/mods.o
	$(CC) $^ -o $@ -lSDL2 -lopenal -lpthread -lm

# videotest again with AddressSanitizer and UndefinedBehaviorSanitizer: the fuzz of the JPEG decoder, the AVI reader and the movie
# replacement (flipped and cut bytes) then shows a bad read or write instead of passing by luck. Builds everything in one go.
VIDEO_ASAN_SRC := videotest.c shim/video_export.c shim/jpeg_write.c shim/avi_write.c shim/jpeg_read.c shim/avi_read.c shim/video_mod.c shim/host_mdec.c shim/host_xa.c shim/snd_vol.c shim/port_log.c shim/text_data.c shim/port_paths.c shim/text_codec.c shim/text_reflow.c shim/port_fs.c shim/mods.c

.PHONY: videotest-asan
videotest-asan:
	@mkdir -p $(BUILD)/asan
	$(CC) $(VIDEO_CFLAGS) -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer $(VIDEO_ASAN_SRC) -o $(BUILD)/asan/videotest -lSDL2 -lopenal -lpthread -lm
	rm -rf $(BUILD)/video-test-asan && mkdir -p $(BUILD)/video-test-asan
	@if command -v python3 >/dev/null 2>&1 && python3 -c "import PIL" >/dev/null 2>&1; then python3 -I test_fixtures/video/gen_fixtures.py $(BUILD)/video-test-asan; fi
	$(BUILD)/asan/videotest $(BUILD)/video-test-asan

# content-export E3a/E3b: the sound bank export (shim/wav_write.c, midi_write.c, audio_export.c). audiotest builds a tiny synthetic bank
# (VAB header, VAG samples, SEP records) in code, exports it and writes the files that pc/test_fixtures/audio/check_audio.py then checks on
# its own: its own ADPCM decode against every WAV, its own MIDI reader against what the port's SEP parser decoded, the JSON fields.
# snd_spu.o and snd_sep.o are linked for the port's own VH and SEP parsers (they need the PsyCross library, as sndtest does). The mod
# tests (E3c/E3d) load a bank and play sequences through the real SPU backend, so it runs on OpenAL's null device, with no sound.
AUDIO_CFLAGS := $(VIDEO_CFLAGS)

.PHONY: audiotest
audiotest: $(BUILD)/audiotest
	rm -rf $(BUILD)/audio-test && mkdir -p $(BUILD)/audio-test
	ALSOFT_DRIVERS=null SDL_AUDIODRIVER=dummy $(BUILD)/audiotest $(BUILD)/audio-test
	@if command -v python3 >/dev/null 2>&1; then \
		python3 -I test_fixtures/audio/check_audio.py $(BUILD)/audio-test; \
	else echo "audiotest: python3 not found, the independent decode and MIDI parse were skipped"; fi

$(BUILD)/wav_write.o: shim/wav_write.c shim/wav_write.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

# content-export E3c/E3d: a mod's WAV files in place of samples and songs (wav_read.c, vag_encode.c, audio_mod.c). audio_mod.o is linked by
# everything that links snd_spu.o and snd_sep.o (sndtest, audiotest, the game), with port_log.o.
$(BUILD)/wav_read.o: shim/wav_read.c shim/wav_read.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

$(BUILD)/vag_encode.o: shim/vag_encode.c shim/vag_encode.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

$(BUILD)/audio_mod.o: shim/audio_mod.c shim/audio_mod.h shim/wav_read.h shim/vag_encode.h shim/port_log.h shim/snd_internal.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

$(BUILD)/midi_write.o: shim/midi_write.c shim/midi_write.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

$(BUILD)/audio_export.o: shim/audio_export.c shim/audio_export.h shim/wav_write.h shim/midi_write.h shim/snd_internal.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

$(BUILD)/audiotest.o: audiotest.c shim/audio_export.h shim/wav_write.h shim/midi_write.h shim/snd_internal.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(AUDIO_CFLAGS) -c $< -o $@

$(BUILD)/audiotest: $(BUILD)/audiotest.o $(BUILD)/audio_export.o $(BUILD)/wav_write.o $(BUILD)/wav_read.o $(BUILD)/vag_encode.o $(BUILD)/audio_mod.o $(BUILD)/midi_write.o $(BUILD)/snd_spu.o $(BUILD)/snd_sep.o $(BUILD)/snd_vol.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o $(BUILD)/mods.o psycross
	$(CC) $(filter %.o,$^) $(PSYCROSS_BUILD)/libpsycross.a $(PSYCROSS_LIBS) -lpthread -o $@

# Port-filesystem T1: the common file layer (shim/port_fs.c): the number -> disc path map, the raw overrides
# of mods/<mod>/files/ and the precedence over the text and image layers. Pure stdlib plus mods.h.
$(BUILD)/port_fs.o: shim/port_fs.c shim/port_fs.h shim/mods.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# fstest: synthetic ISO directory, override order across mods, offset reads, size changes, precedence (scratch under $$TMPDIR).
.PHONY: fstest
fstest: $(BUILD)/fstest
	$(BUILD)/fstest

$(BUILD)/fstest.o: fstest.c shim/mods.h shim/port_fs.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/fstest: $(BUILD)/fstest.o $(BUILD)/port_fs.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o
	$(CC) $^ -o $@

# Menu art as PNG (feature menu-art-png T2): the indexed PNG writer and the TIM extraction. Pure stdlib
# plus the manifest helpers of text_data.c; imagetest uses synthetic TIMs only (scratch under $$TMPDIR).
$(BUILD)/png_write.o: shim/png_write.c shim/png_write.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/png_read.o: shim/png_read.c shim/png_read.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/image_data.o: shim/image_data.c shim/image_data.h shim/port_fs.h shim/sprite_data.h shim/png_write.h shim/png_read.h shim/mods.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# Field sprites as PNG with their animations (feature field-sprites-png, T2)
$(BUILD)/sprite_data.o: shim/sprite_data.c shim/sprite_data.h shim/image_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -MMD -MP -g $(WIN_DEFS) -c $< -o $@

# content-export E6b: the field stage tiles as PNG + JSON (shim/stage_export.c) and their replacement (shim/stage_mod.c). stagetest builds
# synthetic PACK files (RLEN and plain tiles) on a raw disc image in memory; SDL is linked for the worker thread only.
STAGE_CFLAGS := $(VIDEO_CFLAGS)

$(BUILD)/stage_export.o: shim/stage_export.c shim/stage_export.h shim/image_data.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(STAGE_CFLAGS) -c $< -o $@

$(BUILD)/stage_mod.o: shim/stage_mod.c shim/stage_mod.h shim/stage_export.h shim/image_data.h shim/mods.h shim/port_log.h shim/port_paths.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(STAGE_CFLAGS) -c $< -o $@

.PHONY: stagetest
stagetest: $(BUILD)/stagetest
	$(BUILD)/stagetest

$(BUILD)/stagetest.o: stagetest.c shim/stage_export.h shim/stage_mod.h shim/image_data.h shim/png_read.h shim/mods.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) $(STAGE_CFLAGS) -c $< -o $@

$(BUILD)/stagetest: $(BUILD)/stagetest.o $(BUILD)/stage_export.o $(BUILD)/stage_mod.o $(BUILD)/image_data.o $(BUILD)/sprite_data.o $(BUILD)/content.o $(BUILD)/png_write.o $(BUILD)/png_read.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/port_fs.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o
	$(CC) $^ -o $@ -lSDL2 -lpthread -lm

# port-close S1/S2: the stage code vocabulary and the stage data model (shim/stage_codes.c, shim/stage_data.c); portable C, no game. stagedatatest
# round-trips every code and script form and a synthetic stage through the stage-v1 JSON, and checks the reference export's manifest rule.
$(BUILD)/stage_codes.o: shim/stage_codes.c shim/stage_codes.h shim/content.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/stage_data.o: shim/stage_data.c shim/stage_data.h shim/stage_codes.h shim/content.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: stagedatatest
stagedatatest: $(BUILD)/stagedatatest
	$(BUILD)/stagedatatest

$(BUILD)/stagedatatest.o: stagedatatest.c shim/stage_data.h shim/stage_codes.h shim/content.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/stagedatatest: $(BUILD)/stagedatatest.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# new-stages T2: the SnnnTMPK.BIN map layer codec (shim/map_layers.c); portable C99, no game header, no other port file. maplayertest checks
# encode/decode, dedup, limits, damaged input and the archive helpers, and sweeps the extracted disc when it is there (skipped otherwise).
$(BUILD)/map_layers.o: shim/map_layers.c shim/map_layers.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: maplayertest
maplayertest: $(BUILD)/maplayertest
	$(BUILD)/maplayertest

$(BUILD)/maplayertest.o: maplayertest.c shim/map_layers.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/maplayertest: $(BUILD)/maplayertest.o $(BUILD)/map_layers.o
	$(CC) $^ -o $@

# new-stages T3: the Z_STAGE SnnnPACK.BIN codec (shim/map_pack.c); portable C99, no game header, no other port file. mappacktest checks RLEN,
# one tile (layout, limits), the container (sector math, the 255 tile limit) and sweeps the 237 PACK files of the extracted disc when it is there.
$(BUILD)/map_pack.o: shim/map_pack.c shim/map_pack.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: mappacktest
mappacktest: $(BUILD)/mappacktest
	$(BUILD)/mappacktest

$(BUILD)/mappacktest.o: mappacktest.c shim/map_pack.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/mappacktest: $(BUILD)/mappacktest.o $(BUILD)/map_pack.o
	$(CC) $^ -o $@

# port-close S4/S5: the stage patch layer and the mod flag codes (shim/stage_mods.c; portable C, no game header, it reads the active mods through
# mods.h and the named variables through ext_state.h). stagemodtest checks the merge, the codes and the variables with their save chunk.
$(BUILD)/stage_mods.o: shim/stage_mods.c shim/stage_mods.h shim/stage_data.h shim/stage_codes.h shim/content.h shim/mods.h shim/ext_state.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: stagemodtest
stagemodtest: $(BUILD)/stagemodtest
	$(BUILD)/stagemodtest

$(BUILD)/stagemodtest.o: stagemodtest.c shim/stage_mods.h shim/stage_data.h shim/stage_codes.h shim/ext_state.h shim/mods.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/stagemodtest: $(BUILD)/stagemodtest.o $(BUILD)/stage_mods.o $(BUILD)/mod_stages.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# new-stages T5b: the new field encounters of the mods (shim/field_ext.c; portable C, no game header: it reads tables/field_encounters.json of the active
# mods and the host turns its rows into the game's Encounter and BattleReward). fieldexttest checks the rows, their numbers, the rewards and every refusal.
$(BUILD)/field_ext.o: shim/field_ext.c shim/field_ext.h shim/content.h shim/mods.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: fieldexttest
fieldexttest: $(BUILD)/fieldexttest
	$(BUILD)/fieldexttest

$(BUILD)/fieldexttest.o: fieldexttest.c shim/field_ext.h shim/mods.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/fieldexttest: $(BUILD)/fieldexttest.o $(BUILD)/field_ext.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o
	$(CC) $^ -o $@

# new-stages T4: the registry of the stages that mods add (shim/mod_stages.c; portable C, no game function: it needs port_fs.h for the virtual files, ext_state.h
# for the visited variable and stage_codes.h for the name rule). modstagetest checks the registry, the visited flag, the save chunk, the names in slots and
# warp actions and a synthetic stage's TMPK and PACK read back as virtual files.
$(BUILD)/mod_stages.o: shim/mod_stages.c shim/mod_stages.h shim/ext_state.h shim/port_fs.h shim/stage_codes.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

.PHONY: modstagetest
modstagetest: $(BUILD)/modstagetest
	$(BUILD)/modstagetest

$(BUILD)/modstagetest.o: modstagetest.c shim/mod_stages.h shim/stage_mods.h shim/stage_codes.h shim/ext_state.h shim/mods.h shim/port_fs.h shim/map_layers.h shim/map_pack.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/modstagetest: $(BUILD)/modstagetest.o $(BUILD)/mod_stages.o $(BUILD)/stage_mods.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o $(BUILD)/map_layers.o $(BUILD)/map_pack.o
	$(CC) $^ -o $@

# new-stages T5: the loader of the stages that mods add (shim/stage_new.c; portable C: stage.json and the PNGs of a mod's new-stages/<name>/ into a TMPK, a PACK and a
# registered stage). newstagetest builds a stage from generated PNGs, round-trips its layers and pack, and checks the refusals and the checks of the map against the sections.
$(BUILD)/stage_new.o: shim/stage_new.c shim/stage_new.h shim/stage_mods.h shim/mod_stages.h shim/map_layers.h shim/map_pack.h shim/png_read.h shim/mods.h shim/port_fs.h shim/port_log.h shim/stage_codes.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

.PHONY: newstagetest
newstagetest: $(BUILD)/newstagetest
	$(BUILD)/newstagetest

$(BUILD)/newstagetest.o: newstagetest.c shim/stage_new.h shim/mod_stages.h shim/stage_mods.h shim/map_layers.h shim/map_pack.h shim/png_write.h shim/png_read.h shim/mods.h shim/port_fs.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -I. -c $< -o $@

$(BUILD)/newstagetest: $(BUILD)/newstagetest.o $(BUILD)/stage_new.o $(BUILD)/mod_stages.o $(BUILD)/stage_mods.o $(BUILD)/stage_data.o $(BUILD)/stage_codes.o $(BUILD)/ext_state.o $(BUILD)/content.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o $(BUILD)/port_fs.o $(BUILD)/map_layers.o $(BUILD)/map_pack.o $(BUILD)/png_read.o $(BUILD)/png_write.o
	$(CC) $^ -o $@

# content-export E7: the disc files no other exporter covers, as raw copies (shim/raw_export.c). rawtest builds a synthetic raw disc image in
# memory (covered files, uncovered files, files outside AAA/); no SDL: the job is synchronous.
$(BUILD)/raw_export.o: shim/raw_export.c shim/raw_export.h shim/image_data.h shim/sprite_data.h shim/port_log.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

.PHONY: rawtest
rawtest: $(BUILD)/rawtest
	$(BUILD)/rawtest

$(BUILD)/rawtest.o: rawtest.c shim/raw_export.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -Ishim -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/rawtest: $(BUILD)/rawtest.o $(BUILD)/raw_export.o $(BUILD)/image_data.o $(BUILD)/sprite_data.o $(BUILD)/content.o $(BUILD)/png_write.o $(BUILD)/png_read.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/port_fs.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o
	$(CC) $^ -o $@

.PHONY: imagetest
imagetest: $(BUILD)/imagetest
	$(BUILD)/imagetest

$(BUILD)/imagetest.o: imagetest.c shim/image_data.h shim/sprite_data.h shim/png_write.h shim/png_read.h shim/mods.h shim/text_data.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu11 -O2 -Wall -Wextra -I. -MMD -MP -g $(WIN_DEFS) -c $< -o $@

$(BUILD)/imagetest: $(BUILD)/imagetest.o $(BUILD)/image_data.o $(BUILD)/sprite_data.o $(BUILD)/content.o $(BUILD)/png_write.o $(BUILD)/png_read.o $(BUILD)/mods.o $(BUILD)/port_log.o $(BUILD)/port_fs.o $(BUILD)/text_data.o $(BUILD)/port_paths.o $(BUILD)/text_codec.o $(BUILD)/text_reflow.o
	$(CC) $^ -o $@

# T37/T39: the MDEC decoder (shim/host_mdec.c). mdectest and sttest drive it
# directly; the boot binary reaches it through the libpress shim
# (shim/host_press.c). It has no PsyQ or PsyCross dependency: plain host flags.
$(BUILD)/host_mdec.o: shim/host_mdec.c shim/host_mdec.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -c $< -o $@

# T37: the MDEC decoder's own check (pc/mdectest.c). It reads one movie frame
# from the owner's disc, decodes it with shim/host_mdec.c and writes a PNG with
# the offline half of shim/host_shot.c. No PsyCross and no window: only GL is
# linked because host_shot.o still contains the back-buffer capture.
.PHONY: mdectest
mdectest: $(BUILD)/mdectest

$(BUILD)/mdectest: $(BUILD)/mdectest.o $(BUILD)/host_mdec.o $(BUILD)/host_shot.o
	$(CC) $(BUILD)/mdectest.o $(BUILD)/host_mdec.o $(BUILD)/host_shot.o $(HARNESS_GL_LIB) -lm -o $@

# T38: the St stream source's check (pc/sttest.c). It replays the opening
# movie's sectors through shim/host_st.c and compares the assembled frame with
# the direct read, payload and decoded RGB24 alike, then repeats with the game's
# own 32-sector ring to prove backpressure and the wrap. No disc image (it
# reads the extracted STR), no PsyCross, no window.
.PHONY: sttest
sttest: $(BUILD)/sttest

$(BUILD)/sttest: $(BUILD)/sttest.o $(BUILD)/host_st.o $(BUILD)/host_mdec.o
	$(CC) $(BUILD)/sttest.o $(BUILD)/host_st.o $(BUILD)/host_mdec.o -lm -o $@

$(BUILD)/sttest.o: sttest.c shim/host_st.h shim/host_mdec.h $(CURDIR)/ps1_types.h $(CURDIR)/psx_compat.h
	@mkdir -p $(dir $@)
	$(CC) $(PRESS_CFLAGS) $(GAME_DEFS) $(DRAW_INC) -c $< -o $@

check: all
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_rlen.py

# The file layer needs the owner's own disc image: pass it, nothing of it is
# stored in the repository (ADR-001).
check-file: all
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-file DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_file.py "$(DISC)"

# The text engine also needs the owner's own disc image (it loads a language
# file from it); nothing of it is stored in the repository (ADR-001).
check-text: all
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-text DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_text.py "$(DISC)"

# Milestone 3: the game's own drawing code builds a draw order and the check
# inspects it structurally (kinds, counts, PsyCross's host packet sizes).
check-draw: all
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-draw DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_draw.py "$(DISC)"

# Milestone 5 (T14): the font's TIMs in VRAM and the glyph primitives the game's
# own text window draws, both checked against the disc and the glyph table.
check-font: $(BUILD)/font_frame
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-font DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_font.py "$(DISC)"

# T16: the country select's art in VRAM and the left panel's own sprite, against
# an independent read of CNTSELWN.BIN (file 0x8A3) and the bank's part table.
check-country: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-country DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_country.py "$(DISC)"

# Milestone 5 (T14): the font in VRAM and the game's own text on screen. Opens
# the PsyCross window, loads FILE_FONT from the disc with the game's own loader,
# draws a language file's string with the game's own text window, presents
# $(FRAMES) frames and exits on its own. SHOT is where the glReadPixels capture
# goes (a PNG this harness writes itself). FILE and STRING default to the
# Spanish SSSTNAME (the area names) and the string the G-041 notes measure.
FILE ?= 189
STRING ?= 71
FONT_SHOT ?= $(ROOT)/build/t14_font_frame.png
font-frame: $(BUILD)/font_frame
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc font-frame DISC=<path to a .bin>"; exit 1; }
	$(BUILD)/font_frame "$(DISC)" $(FILE) $(STRING) $(FRAMES) "$(FONT_SHOT)"

# text-shot: PNGs of specific strings (headless). make -C pc text-shot DISC=<.bin> LANG_SHOT=es419 SHOTS="SSTALK00:0:15 ..."
LANG_SHOT ?= es419
SHOT_DIR ?= $(ROOT)/build/l6/shots
text-shot: $(BUILD)/text_shot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc text-shot DISC=<path to a .bin>"; exit 1; }
	cd $(ROOT) && SDL_VIDEODRIVER=offscreen ALSOFT_DRIVERS=null SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1/0x1 $(BUILD)/text_shot "$(DISC)" $(LANG_SHOT) "$(SHOT_DIR)" $(SHOTS)

# Milestone 4 (T13): the banner on screen, drawn by the game's own code. Opens
# the PsyCross window, presents $(FRAMES) frames and exits on its own; it needs
# a display, no disc and nothing of the game's assets. Override the length with
# FRAMES=<n>.
FRAMES ?= 300
first-frame: $(BUILD)/first_frame
	$(BUILD)/first_frame $(FRAMES)

# Milestone 6 (T15): the game's own boot path, run on the host. It needs the
# owner's disc (like the other checks) and a display: it opens the PsyCross
# window, runs the game's main() and the mode it reaches (the country select),
# presents $(FRAMES) frames and exits on its own, saving the glReadPixels
# capture in BOOT_SHOT. The country select's art is read from the disc (file
# 0x8A3, AAA/DAT/COUNTRY/CNTSELWN.BIN); nothing of it is stored here.
# BOOT_ART is the optional fourth argument: the frame on which the harness
# reads the country select's VRAM art back and prints it with the frame's
# sprites (T16, check-country). 0 leaves it off, which is what a plain run wants.
BOOT_SHOT ?= $(ROOT)/build/t15_country_select.png
BOOT_ART ?= 0
boot: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc boot DISC=<path to a .bin>"; exit 1; }
	$(BUILD)/boot "$(DISC)" $(FRAMES) "$(BOOT_SHOT)" $(BOOT_ART)

# T17, the input demo: the same game's own boot path, but the harness types a
# scripted pad sequence, written in process (T21: the demos never touch the
# keyboard or the session; the pad state goes straight into the buffer the game
# reads). Each frame prints what the game's own pad code saw and which option
# the menu highlights, and the run ends on its own with the glReadPixels capture
# in INPUT_SHOT.
#
# The language the demos leave the country select on (T20): the list is ENGLISH,
# FRANÇAIS, DEUTSCH, ITALIANO, ESPAÑOL -- and ESPAÑOL LATINO on the es-419 disc --
# so the number of 'down' presses is the option's index. Spanish by default,
# because the owner reads Spanish and not French; LANG_KEYS= leaves it on
# English.
LANG_KEYS ?= down,down,down,down
PAD_KEYS ?= $(LANG_KEYS)
INPUT_SHOT ?= $(ROOT)/build/t17_input_demo.png
input-demo: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc input-demo DISC=<path to a .bin>"; exit 1; }
	$(BUILD)/boot "$(DISC)" $(FRAMES) "$(INPUT_SHOT)" 0 "$(PAD_KEYS)"

# T17, the input check: runs the demo with the scripted sequence and verifies
# from its own output that the game's pad code saw each press and that the menu's
# highlighted option moved down, down and back up (pc/check_input.py).
check-input: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-input DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_input.py "$(DISC)"

# T18, the title demo: the same game's own main, but the scripted keys walk past
# the country select and through the title overlay's opening movie to the title
# screen. 'down' moves the language highlight off ENGLISH (LANGUAGE 0 would
# request MODE_OPENING instead of MODE_OPENING_2), the first 'start' confirms it,
# 'wait' holds until the game's own mode changes -- the overlay's movie player
# asks for MODE_OPENING_2 (0xE02), which STDWTITL_start turns into its second
# opening movie -- and the second 'start' skips that movie the way a player does.
# The movie does show its picture since T39 (libpress over the port's MDEC, see
# pc/shim/host_press.c); the demo skips it only to reach the title screen. The
# game's own movie task then asks for MODE_TITLE (0xE00) and the title screen is
# drawn by its own code from the disc. The run presents TITLE_FRAMES frames,
# captures with glReadPixels and exits on its own.
TITLE_KEYS ?= $(LANG_KEYS),start,wait,start
TITLE_FRAMES ?= 320
TITLE_SHOT ?= $(ROOT)/build/t18_title_screen.png
title-demo: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc title-demo DISC=<path to a .bin>"; exit 1; }
	$(BUILD)/boot "$(DISC)" $(TITLE_FRAMES) "$(TITLE_SHOT)" 0 "$(TITLE_KEYS)"

# T18/T39, the check: runs the same title demo and verifies, from its own output,
# that the game's own code walked the country select (0x1600), the title
# overlay's opening movie (0xE02) and the title screen (0xE00), that the movie
# decoded and presented its frames and that the frame captured while it played
# is a picture (MDEC_DEBUG + the PNG, see pc/check_title.py), that the title
# screen drew its own sprites on its own layer and that the run captured a
# 320x240 PNG.
check-title: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-title DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_title.py "$(DISC)"

# T39, the movie demo: boots to the opening movie (the country select's ESPAÑOL
# branch, MODE_OPENING_2 0xE02) and stays in it -- no 'start' to skip it -- while
# the port decodes and presents every frame. MDEC_DEBUG=1 prints one line per
# decoded frame plus the movie's totals at teardown. The capture list is boot's
# 7th argument (T30): the run writes <stem>_f<index>.png for each index, so the
# owner can look at several frames of the movie without re-running it. The frames
# are the owner's disc through the port; nothing of it is stored here (ADR-001).
MOVIE_KEYS ?= $(LANG_KEYS),start,wait
MOVIE_TOTAL ?= 900
MOVIE_SHOT ?= $(ROOT)/build/t39_movie.png
MOVIE_CAPTURES ?= 300,360,420,480
movie-demo: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "movie-demo needs your game disc image (a .bin, do not commit it)."; echo "Usage: make -C pc movie-demo DISC=\"/path/to/Digimon World 2003 (Europe).bin\""; exit 1; }
	MDEC_DEBUG=1 $(BUILD)/boot "$(DISC)" $(MOVIE_TOTAL) "$(MOVIE_SHOT)" 0 "$(MOVIE_KEYS)" "$(MOVIE_CAPTURES)"

# T19, the field demo: the same game's own main, with a longer script that also
# navigates the title screen's own menu and confirms a new game. 'down' and the
# first 'start' walk the country select (as title-demo), 'wait' holds until the
# game's own movie mode (0xE02), 'skipmovie' presses Start until the game
# leaves it (the movie player does not finish on its own in this build, so a
# lost key event would stall the script), 'waittitle' holds until the title's
# menu waits for Start ("PRESS START"), 'startuntil' presses Start until that
# menu shows its two options (new game / continue), 'waitoptions' holds until
# the menu navigates them, 'newgame' presses Up until the highlight is on NEW
# GAME and 'confirm' presses cross until the game leaves the title's mode. The
# 'until' steps retry every key gap, so a dropped X key event does not stall
# the demo. The game's own code then requests MODE_NEW_GAME (0x2D7), the
# field's entry point runs (FIELDSTG_start, index 2), its stage overlay is the
# linked one (WSTAG780, 0x1E9) and the field's own code draws. One extra cross
# press advances its opening event far enough to exercise the map walk-step path.
#
# T22: the demo runs its full frame budget (FIELD_TOTAL) with no artificial
# stop. The field's loader used to reach its first requirement a few frames in
# -- a NULL FIELDSTG_state.slots read (a stage without slots) that the
# PlayStation tolerates and the host faults on -- and T19 stopped the harness
# there (pcBootStopInMode); T22 added the missing guard in the host's copy of
# the loader (pc/field/file_loader.c) and removed that stop. T23, T24 and T26
# took the truncated pointers and the callback order, and T27 sends each
# layer's own DRAWENV with its own table: the field now draws its map and runs
# its whole budget, with a capture that is a picture (pc/README.md).
FIELD_KEYS ?= $(LANG_KEYS),start,wait,skipmovie,waittitle,startuntil,waitoptions,newgame,confirm,cross
FIELD_TOTAL ?= 1000
FIELD_SHOT ?= $(ROOT)/build/t19_field.png
field-demo: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "field-demo needs your game disc image (a .bin, do not commit it)."; echo "Usage: make -C pc field-demo DISC=\"/path/to/Digimon World 2003 (Europe).bin\""; exit 1; }
	$(BUILD)/boot "$(DISC)" $(FIELD_TOTAL) "$(FIELD_SHOT)" 0 "$(FIELD_KEYS)"

# T19/T22-T27, the check: runs the field demo and verifies, from its own output,
# that the game's own code walked the country select, the title overlay's movie
# and title screen, that the title's own menu showed its options and moved its
# highlight, that the game requested MODE_NEW_GAME and the field's entry point
# ran with the linked stage, that the field's own code drew on its own layers
# PAST the frames T19's harness stopped at on purpose (463), T22's loader fault
# (468) and the truncated-pointer callbacks T23/T24 fixed (474, 562), that each
# layer's own DRAWENV reached its own ordering table and the game's DR_TPAGEs
# were set to draw to the port's one surface (T27), that the run reaches the
# harness's own frame budget (FIELD_TOTAL) and exits on its own, and that the
# capture it writes is a picture, not a blank (pc/check_field.py).
check-field: $(BUILD)/boot
	@test -n "$(DISC)" || { echo "give the disc image: make -C pc check-field DISC=<path to a .bin>"; exit 1; }
	$(ROOT)/.venv/bin/python $(ROOT)/pc/check_field.py "$(DISC)"

# T11: build the PsyCross static library from our own CMakeLists.txt, without
# patching the submodule. Configure + build are idempotent, so this runs
# every time and always reflects the submodule's current sources.
psycross:
ifeq ($(TARGET),windows)
	$(CURDIR)/win/fetch_deps.sh >/dev/null
	cmake -S psycross -B $(PSYCROSS_BUILD) -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/win/mingw-w64.cmake
else ifeq ($(TARGET),android)
	cmake -S psycross -B $(PSYCROSS_BUILD) -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DCMAKE_TOOLCHAIN_FILE=$(ANDROID_NDK_HOME)/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a \
		-DANDROID_PLATFORM=android-$(ANDROID_API) -DCMAKE_FIND_ROOT_PATH=$(ANDROID_PREFIX) \
		-DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DPSYX_FORCE_GLES=ON
else
	cmake -S psycross -B $(PSYCROSS_BUILD) $(if $(filter 1,$(GLES)),-DPSYX_FORCE_GLES=ON)
endif
	cmake --build $(PSYCROSS_BUILD) -j

# T11 acceptance: the C11 consumer that pins PsyCross's host packet layout
# (P_TAG 16, OT_TAG 12, POLY_F3 28) and links a symbol out of the library.
check-packets: psycross
	$(PSYCROSS_BUILD)/check_packets

clean:
	rm -rf $(BUILD)

# Header dependencies: -MMD (in HOST_CFLAGS) writes a .d next to each object,
# so a changed header rebuilds the objects that include it instead of leaving
# a stale one behind (a struct change once crashed sndtest that way).
-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)

# filecachetest: the game's file_cache.c with the port's batched update (shim/file_cache_pc.c, its own test main under
# FILE_CACHE_PC_TEST) over a fake file table and CD reader, no disc.
.PHONY: filecachetest
filecachetest: $(BUILD)/filecachetest
	$(BUILD)/filecachetest

$(BUILD)/file_cache_pc_test.o: shim/file_cache_pc.c shim/stage_timing.h shim/port_log.h
	@mkdir -p $(dir $@)
	$(CC) $(HOST_CFLAGS) $(GAME_DEFS) $(GAME_INC) -DFILE_CACHE_PC_TEST -c $< -o $@

$(BUILD)/filecachetest: $(FILE_OBJS) $(SHIM_CORE_OBJS) $(filter-out $(BUILD)/file_cache_pc.o,$(SHIM_CD_OBJS)) $(BUILD)/file_cache_pc_test.o
	$(CC) $^ $(PSYCROSS_LIBS) -o $@
