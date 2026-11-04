# libmain.so: the game. The compiling is pc/Makefile's (TARGET=android): this file asks it
# which objects, linker scripts and wraps the game has, has it build them, and links the shared library. So the
# Linux, Windows and Android builds cannot list different sources. SDL's Android glue loads libSDL2.so, then libmain.so,
# and calls SDL_main (the port's main()).
cmake_minimum_required(VERSION 3.22)
project(DW3_PORT C CXX)

set(DW3_ANDROID_PREFIX "" CACHE PATH "SDL2 + OpenAL Soft for the ABI (pc/android/build_deps.sh)")
if(NOT EXISTS "${DW3_ANDROID_PREFIX}/lib/libSDL2.so" OR NOT EXISTS "${DW3_ANDROID_PREFIX}/lib/libopenal.so")
    message(FATAL_ERROR "DW3_ANDROID_PREFIX must hold lib/libSDL2.so and lib/libopenal.so (run pc/android/build.sh deps)")
endif()

get_filename_component(PC_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)  # pc/android/app/jni -> pc
find_program(MAKE_PROGRAM make REQUIRED)
cmake_host_system_information(RESULT JOBS QUERY NUMBER_OF_LOGICAL_CORES)

# DW3_ANDROID_OPT (environment, e.g. -O2) is pc/Makefile's ANDROID_OPT: the optimization of the game's own objects
set(MAKE_ARGS -C "${PC_DIR}" TARGET=android "ANDROID_OPT=$ENV{DW3_ANDROID_OPT}" "ANDROID_NDK_HOME=${CMAKE_ANDROID_NDK}"
    "ANDROID_API=${ANDROID_PLATFORM_LEVEL}" "ANDROID_PREFIX=${DW3_ANDROID_PREFIX}")

# what the Makefile names, one item per line (no build happens for these)
function(dw3_make_list var target)
    execute_process(COMMAND "${MAKE_PROGRAM}" -s --no-print-directory ${MAKE_ARGS} ${target}
        OUTPUT_VARIABLE out RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "make ${target} failed (${rc})")
    endif()
    string(STRIP "${out}" out)
    string(REPLACE "\n" ";" out "${out}")
    set(${var} "${out}" PARENT_SCOPE)
endfunction()
dw3_make_list(GAME_OBJECTS print-android-objects)
dw3_make_list(GAME_LDFLAGS print-android-ldflags)
dw3_make_list(PSYCROSS_LIB print-android-psycross)

set(OVL_SECTIONS "${PC_DIR}/build-android/ovl_sections.ld")
set(OVL_STAGES "${PC_DIR}/build-android/ovl_stages.ld")

# builds the objects, the scope scripts and libpsycross.a with the NDK's clang (incremental: make decides)
add_custom_target(dw3_objects
    COMMAND "${MAKE_PROGRAM}" ${MAKE_ARGS} -j${JOBS} android-objs
    BYPRODUCTS ${GAME_OBJECTS} ${PSYCROSS_LIB} ${OVL_SECTIONS} ${OVL_STAGES}
    COMMENT "pc/Makefile TARGET=android android-objs"
    USES_TERMINAL)

set_source_files_properties(${GAME_OBJECTS} PROPERTIES EXTERNAL_OBJECT TRUE GENERATED TRUE)
add_library(main SHARED ${GAME_OBJECTS})
add_dependencies(main dw3_objects)
set_target_properties(main PROPERTIES LINKER_LANGUAGE CXX  # PsyCross is C++: the C++ runtime (static, ANDROID_STL) comes with it
    LINK_DEPENDS "${OVL_SECTIONS};${OVL_STAGES}")

add_library(SDL2 SHARED IMPORTED)
set_target_properties(SDL2 PROPERTIES IMPORTED_LOCATION "${DW3_ANDROID_PREFIX}/lib/libSDL2.so")
add_library(openal SHARED IMPORTED)
set_target_properties(openal PROPERTIES IMPORTED_LOCATION "${DW3_ANDROID_PREFIX}/lib/libopenal.so")

# SDL's activity calls SDL_main; the port's main() is boot.c's.
target_link_options(main PRIVATE ${GAME_LDFLAGS} "-Wl,--defsym=SDL_main=main")
target_link_libraries(main PRIVATE "${PSYCROSS_LIB}" SDL2 openal GLESv3 EGL log android m dl)

# the field stages' state scopes, checked on the linked library as on Linux (llvm-nm / llvm-objdump)
add_custom_command(TARGET main POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E env "CROSS_PREFIX=${ANDROID_TOOLCHAIN_ROOT}/bin/llvm-"
        python3 check_scopes.py check "$<TARGET_FILE:main>" build-android ovl_sections.ld build-android/ovl_stages.ld
    WORKING_DIRECTORY "${PC_DIR}"
    COMMENT "check_scopes.py check")
