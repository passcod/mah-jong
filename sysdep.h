/* $Header: /home/jcb/MahJong/newmj/RCS/sysdep.h,v 12.1 2012/02/01 15:29:10 jcb Rel $
 * sysdep.h
 * Miscellaneous system stuff that may require attention
 * by configuration procedures.
 */
/****************** COPYRIGHT STATEMENT **********************
 * This file is Copyright (c) 2000 by J. C. Bradfield.       *
 * Distribution and use is governed by the LICENCE file that *
 * accompanies this file.                                    *
 * The moral rights of the author are asserted.              *
 *                                                           *
 ***************** DISCLAIMER OF WARRANTY ********************
 * This code is not warranted fit for any purpose. See the   *
 * LICENCE file for further information.                     *
 *                                                           *
 *************************************************************/

/* network conversion utilities */

#ifndef SYSDEP_H_INCLUDED
#define SYSDEP_H_INCLUDED

#include <unistd.h>
/* The following define prevents HP-UX from using the antiquated
   version of select() with ints instead of fd_sets */
#define _XOPEN_SOURCE_EXTENDED 1
#include <time.h>
#ifdef WIN32
#include <winsock.h>
typedef int socklen_t;
/* following functions don't exist in Windows, so we need to emulate them */
/* Windows doesn't define the struct timezone, so just use void * */
int gettimeofday(struct timeval *tv, void *tz);
char *getlogin(void);
unsigned int sleep(unsigned int t);
unsigned int usleep(unsigned int t);
int getpid(void);
int unlink(const char *f);
#else
#include <netinet/in.h>
/* need this for gettimeofday */
#include <sys/time.h>
#endif /* WIN32 */

#include <stdlib.h>
#include <stdio.h>

/* In the mingw I use, these functions are missing from the headers */
#ifdef WIN32
int _snprintf(char *str, size_t n, const char *format, ...);
int _vsnprintf(char *buffer,size_t count,const char *format,va_list argptr);
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#endif

/* for memset and friends */
#include <string.h>
/* This is missing in my windows mingw */
#ifdef WIN32
char * index(const char *s, int c);
#endif
#include <assert.h>
#include <ctype.h>

/* define SOCKET types so we can also work with windows */
#ifndef WIN32
typedef intptr_t SOCKET;
#define INVALID_SOCKET -1
#define closesocket close
#endif

/* If we're using GNU CC, we can suppress warnings for unused variables
   with this.
*/
#if defined( __GNUC__) && !defined(__GNUG__)
# define UNUSED __attribute ((unused))
#else
# define UNUSED
#endif

/* functions in sysdep.c */

/* warn is  fprintf(stderr,...) .
*/

typedef enum { LogInfo = 1, LogWarning = 2, LogError = 3 } LogLevel;

/* If the global variable log_msg_add_note_hook is non-null,
   it is called with the level, and returns a (char *) which
   will be appended to the log message. The returned value should
   be static, or at least long-lived enough.
*/
extern char *(*log_msg_add_note_hook)(LogLevel l);

/* If the global variable log_msg_hook is non-null, it should
   be a pointer to a function which will be passed the formatted
   warning. If the hook returns non-zero, the warning will not
   be printed to stderr.
*/
extern int (*log_msg_hook)(LogLevel l,char *);

/* log_msg is a generalized version with a syslog like warning
   level. */
int log_msg(LogLevel l, char *format,...);

/* and here are abbreviations for the levels */
int info(char *format,...);

int warn(char *format,...);

/* The error function is called log_error, because on error we
   almost certainly want to add CPP information, which we pass in via
   a macro */

int log_error(const char *function, const char *file, const int line, const char *format,...);

/* like this: */
#define error(format, args...) log_error(__FUNCTION__,__FILE__,__LINE__,format,## args)

/* ignore the SIGPIPE signal. Return 0 on success
, -1 on error */
int ignore_sigpipe(void);

/* Initialize the socket library functions. This is only needed
   under Windows. It need not be called explicitly, as the library
   functions will call it (with null arguments) if necessary.
   The arguments, if non-NULL, are used by programs that need to do strange
   things to the sockets, such as gtk applications under windows.
   open_transform is a function which is applied to all sockets created,
   and had better return something that fits in
   a (void *); all functions that return or store SOCKET, will instead
   return or store the result of open_transform, cast to a SOCKET.
   (So SOCKET had better fit a void*; this is true on all systems
   I know of.)
   closer is a function to close the resulting object;
   reader is a function to read from it; it should have the same
   type as the system read function, mutatis mutandis;
   writer is a function to write. */
int initialize_sockets(void *(*open_transform)(SOCKET),
		       int (*closer)(void *),
		       int (*reader)(void *, void *, size_t),
		       int (*writer)(void *, const void *,size_t));

/* In the following function, a socket address is a string.
   If it contains a colon, then it is assumed to be a TCP
   address host:port (if host is omitted, it is localhost).
   Otherwise, it is assumed to be a Unix filename, and 
   Unix domain sockets are used.
*/

/* set_up_listening_socket:
   Set up a socket, listening on the given address.
   Return its system level SOCKET.
*/
SOCKET set_up_listening_socket(const char *address);

/* accept_new_connection:
   A connection has arrived on the socket fd.
   Accept the connection, and return the new fd,
   or INVALID_SOCKET on error.
*/

SOCKET accept_new_connection(SOCKET fd);

/* connect_to_host:
   Establish a connection to the given address.
   Return the file descriptor or INVALID_SOCKET on error.
   The file descriptor is marked close-on-exec.
*/
SOCKET connect_to_host(const char *address);

/* same as above, but ignores the socket transformers */
SOCKET plain_connect_to_host(const char *address);


/* close a network connection with or without transforms */
int close_socket(SOCKET s);
int plain_close_socket(SOCKET s);

/* read a line, up to lf/crlf terminator, from the socket,
   and return it.
   If there is an error or end of file before seeing a terminator,
   NULL is returned. At present, error includes reading a partial line.
   The returned string is valid until the next call of get_line.
   As a special hack, if fd is STDOUT_FILENO, then read from
   STDIN_FILENO as an fd.
*/
char *get_line(SOCKET fd);

/* write a line (which should include any terminator) to a socket.
   Return -1 on error, length of line on success.
   If fd is equal to STDOUT_FILENO, then print to it as an fd.
*/
int put_line(SOCKET fd,char *line);

/* These are the same, but they take a file descriptor
   rather than a socket. (I hate Windows) */

char *fd_get_line(int fd);
int fd_put_line(int fd, char *line);

/* stuff arbitrary data down a socket. If len is zero, send EOF */
int put_data(SOCKET fd, char *data, int len);
/* and read it - data must be preallocated */
int get_data(SOCKET fd, char *data, int len);

/* rand_index: return a random integer from 0 to arg (inclusive).
 */
unsigned int rand_index(int top);

/* rand_seed: if the random number generation has a seed value,
   use this argument, if non-zero; if zero, seed with a system
   dependent variable value (actually time(), but needn't be).
   If this function is not called explicitly, it will be called
   with arg 0 on first use of rand_index.
*/
void rand_seed(int seed);

/* utility function: this is just like strncpy, except that it 
   guarantees that the target is null-terminated.
   The m parameter is the length of the target field INCLUDING
   the terminating null.
*/
#define strmcpy(dest,src,m) { strncpy(dest,src,m) ; dest[m-1] = '\000' ; }
/* similarly */
#define strmcat(dest,src,m) { strncat(dest,src,m) ; dest[m-1] = '\000' ; }

/* This is like strmcat, but it also quotes the string (in a system
   dependent way) for adding it to a shell command line.
*/
char *qstrmcat(char *dest, const char *src, int len);

/* another utility function: return arg, or "NULL" if it's null */
char *nullprotect(char *s);


/* feed a command to the system to be started in background.
   No fancy argument processing, just pass to shell or equiv
   Return 1 on success, 0 on failure */
int start_background_program(const char *cmd);

/* unfortunately we need a slightly more sophisticated one where
   we can get at the output. This passes out a write handle for the
   new process's stdin, and a read handle for its stdout */
/* Under Windows, this is fiddled to return "file descriptors" */
int start_background_program_with_io(const char *cmd,int *childin,
  int *childout);

/* return a path for the home directory, or equivalent concept.
   Returns NULL if no sensible homedir is available
   (i.e. HOME is unset on Unix, or HOMEPATH is unset on Windows) */
char *get_homedir(void);

#endif /* SYSDEP_H_INCLUDED */
