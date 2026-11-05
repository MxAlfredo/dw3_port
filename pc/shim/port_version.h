#ifndef PC_PORT_VERSION_H
#define PC_PORT_VERSION_H

/*
 * The port's version, the one place it is written (mods check their "port" requirement against it,
 * docs/modding/README.md). Digits and dots, then an optional "-suffix" (compared as the digits only:
 * 0.4.0-dev counts as 0.4.0). Release scripts name their downloads from their own argument
 * (pc/make_release.sh <version>); keep the two in step when a release is cut.
 */
#define PORT_VERSION "1.1.0"

#endif
