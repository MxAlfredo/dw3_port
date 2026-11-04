#ifndef PC_DISC_FD_H
#define PC_DISC_FD_H

/*
 * T7b: the disc image the player picked with Android's document picker is a file descriptor, not a path the app may open
 * (re-opening /proc/self/fd/<n> goes through the file's own path and is refused for the app). Every reader of the disc
 * therefore opens it with dfdOpen: a path of the form "/proc/self/fd/<n>" becomes a read-only FILE over that descriptor
 * (pread, so any number of FILEs may read the one descriptor at their own positions); any other path is a plain
 * fopen(path, "rb"). One code path for the disc layer.
 */
#include <stdio.h>

/* 1 when path is "/proc/self/fd/<digits>"; the descriptor goes in *fd */
int dfdParse(const char *path, int *fd);

/* fopen(path, "rb"), or a read-only FILE over the descriptor of a /proc/self/fd/<n> path. The FILE owns a dup of the
   descriptor; the original stays open. NULL on failure. */
FILE *dfdOpen(const char *path);

#endif
