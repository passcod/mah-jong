/* $Header: /home/jcb/MahJong/newmj/RCS/version.h.in,v 12.0 2009/06/28 20:43:31 jcb Rel $
 * version.h
 * Provide a version variable and defines.
 */

/* a string for the version number: the version is substituted
   in by the make-release script */
#define VERSION "1.12"
#define XSTRINGIFY(FOO) #FOO
#define STRINGIFY(FOO) XSTRINGIFY(FOO)
#include "protocol.h"

static const char version[] UNUSED = VERSION " (protocol version " STRINGIFY(PROTOCOL_VERSION) ")" ;
