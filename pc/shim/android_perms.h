#ifndef PC_ANDROID_PERMS_H
#define PC_ANDROID_PERMS_H

/*
 * Android keeps the app's external files folder (Android/data/<id>/files) shared with the shell's group (ext_data_rw): a PC
 * pushes mods and saves there with adb. Folders the app made under the default umask 027 were drwxr-s--- and refused those
 * writes. Pure POSIX, so the host test covers it; the Android entry calls it once at start.
 */
#include <sys/types.h>

/* Modes the Android start applies: folders rwxrwsrwx, files rw-rw-rw-. The folder lives in Android/data/<id>, which no other app
   can open (scoped storage), so "other" access only reaches the shell user. Group-only modes were not enough: the FUSE layer drops
   the setgid bit, so new files kept the app's own group and the shell (group ext_data_rw) got "Permission denied" on logs/. The
   setgid bit is still requested (it helps where the path honours it) but nothing depends on it. */
#define ANDROID_PERMS_DIR_MODE 02777
#define ANDROID_PERMS_FILE_MODE 0666

/* Walks `root` (without following symlinks) and chmods every folder to dirMode and every regular file to fileMode when it
   differs. Returns how many entries it changed; -1 when `root` is not a folder. Entries it cannot change are skipped. */
int androidPermsFix(const char *root, mode_t dirMode, mode_t fileMode);

/* 1 when `have` already has the permission bits of `want`; the setgid bit is ignored because the Android path may strip it. */
int androidPermsSame(mode_t have, mode_t want);

/* umask(000): what the process creates from now on (fopen's 0666, mkdir's 0777) is readable and writable by the shell user. Call first thing. */
void androidPermsUmask(void);

#endif
