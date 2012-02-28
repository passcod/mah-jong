/* $Header: /home/jcb/MahJong/newmj/RCS/sysdep.c,v 12.2 2012/02/01 15:29:02 jcb Rel $
 * sysdep.c
 * By intention, this file contains all functions that might need
 * fiddling with to cope with different variants of Unix.
 * In particular, all the networking code is in here.
 * Some parts of the other code assume POSIXish functions for file
 * descriptors and the like; this file is responsible for providing them
 * if they're not native. All such cases that occurred to me as I was
 * writing them can be found by searching for sysdep.c in the other
 * files.
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/sysdep.c,v 12.2 2012/02/01 15:29:02 jcb Rel $";

#include "sysdep.h"
#include <string.h>
/* This is missing in my windows mingw */
#ifdef WIN32
char *index(const char *s, int c) {
  while ( 1 ) {
    if (*s == 0) return NULL;
    else if (*s == c) return (char *)s;
    s++;
  }
}
#endif

#include <fcntl.h>
#include <errno.h>

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
/* needed for HP-UX 11 with _XOPEN_SOURCE_EXTENDED */
#include <arpa/inet.h>
#include <sys/un.h>
/* this is modern, I think. */
#include <netdb.h>
#include <pwd.h>
#endif /* not WIN32 */

#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <signal.h>

#ifdef WIN32
#include <io.h>
#include <fcntl.h>
#endif

/* warn: = fprintf(stderr,...), with automatic new line */
/* log_msg is a generalized version with a syslog like warning
   level. */


int (*log_msg_hook)(LogLevel l,char *);

char *(*log_msg_add_note_hook)(LogLevel l);

static char *log_prefixes[] = { "" , "-I- ", "+W+ ", "*E* " };

/* internal function */
static int vlog_msg(LogLevel l, char *format,va_list args) {
  char buf[1024];
  char *note = NULL;
  int ret;
  int n;

  if ( l > LogError ) l = LogError;
  strcpy(buf,log_prefixes[l]);
  n = strlen(log_prefixes[l]);
  /* reserve 1 char for extra '\n' */
  ret = vsnprintf(buf+n,sizeof(buf)-n-1,format,args);
  buf[1023] = 0;
  n = strlen(buf);
  /* do quick check for terminators */
#ifdef WIN32
  if ( buf[n-2] == '\r' ) {
    /* OK */
    ;
  } else if ( buf[n-1] == '\n' ) {
    /* already have unix terminator */
    buf[n-1] = '\r' ; buf[n] = '\n' ; buf[n+1] = 0 ;
  } else {
    /* no terminator */
    strcat(buf,"\r\n");
  }
#else
  if ( buf[n-1] != '\n' ) strcat(buf,"\n");
#endif
  /* add program supplied note? */
  n = 0;
  if ( log_msg_add_note_hook ) {
    note = (*log_msg_add_note_hook)(l);
    if ( note ) n = strlen(note);
  }
  {
    char nbuf[1024+n+1];
    strcpy(nbuf,buf);
    if ( n > 0 ) { strcat(nbuf,note); }
    /* call the hook */
    if ( log_msg_hook == NULL || log_msg_hook(l,nbuf) == 0 ) {
      fprintf(stderr,"%s",nbuf);
    }
  }
  return ret;
}

int log_msg(LogLevel l,char *format,...) {
  va_list args;
  va_start(args,format);
  return vlog_msg(l,format,args);
}

int log_error(const char *function, const char *file, const int line, const char *format,...) {
  char nformat[1024];
  va_list args;
  sprintf(nformat,"Error occurred in %s(), at %s:%d\n",function,file,line);
  strmcat(nformat,format,1023-(strlen(nformat)+strlen(format)));
  va_start(args,format);
  return vlog_msg(LogError,nformat,args);
}

int warn(char *format,...) {
  va_list args;
  va_start(args,format);
  return vlog_msg(LogWarning,format,args);
}

int info(char *format,...) {
  va_list args;
  va_start(args,format);
  return vlog_msg(LogInfo,format,args);
}

/* ignore the SIGPIPE signal. Return 0 on success, -1 on error */
int ignore_sigpipe(void) {
#ifdef WIN32
  return 0;
#else
  /* this is just posix at present */
  struct sigaction act;

  act.sa_handler = SIG_IGN;
  sigemptyset(&act.sa_mask);
  act.sa_flags = 0;
  return sigaction(SIGPIPE,&act,NULL);
#endif /* WIN32 */
}

/* functions to be applied by the socket routines */
static void *(*skt_open_transform)(SOCKET) = NULL;
static int (*skt_closer)(void *) = NULL;
static int (*skt_reader)(void *, void *, size_t) = NULL;
static int (*skt_writer)(void *, const void *,size_t) = NULL;
static int sockets_initialized = 0;

#ifdef WIN32
static void shutdown_sockets(void) {
  WSACleanup();
  /* I don't care what it returns */
}
#endif /* WIN32 */

/* initialize sockets */
int initialize_sockets(void *(*open_transform)(SOCKET),
		       int (*closer)(void *),
		       int (*reader)(void *, void *, size_t),
		       int (*writer)(void *, const void *,size_t)) {
  skt_open_transform = open_transform;
  skt_closer = closer;
  skt_reader = reader;
  skt_writer = writer;
#ifdef WIN32

  if ( ! sockets_initialized ) {
    WORD version;
    WSADATA data;
    int err;
    
    version = MAKEWORD( 2, 2 );
    
    err = WSAStartup( version, &data );
    if (err != 0) {
      return 0;
    }
    if ((LOBYTE( data.wVersion ) != 2) || (HIBYTE( data.wVersion ) != 2)) {
      WSACleanup();
      return 0;
    }
    if ( atexit(shutdown_sockets) != 0 ) {
      warn("couldn't register socket shutdown routine");
    }
  }
#endif /* WIN32 */

  sockets_initialized = 1;
  return 1;
}

/* this function takes a string and parses it as an address.
   It returns 0 for an Internet address, 1 for a Unix address.
   For Internet addresses, the host name and port are placed
   in the given variables (the string had better be long enough);
   for Unix, the file name is copied to the given variable.
*/
static int parse_address(const char *address,
			 char *ret_host,
			 unsigned short *ret_port,
			 char *ret_file, size_t n) {
  if ( strchr(address,':') ) {
    /* grrr */
    if ( address[0] == ':' ) {
      strcpy(ret_host,"localhost");
      sscanf(address,":%hu",ret_port);
    } else {
      sscanf(address,"%[^:]:%hu",ret_host,ret_port);
    }
    return 0;
  } else {
    if ( ret_file) strmcpy(ret_file,address,n);
    return 1;
  }
}

/* set_up_listening_socket:
   Set up a socket listening on the given address.
   Return its fd.
*/
SOCKET set_up_listening_socket(const char *address) {
  SOCKET sfd;
  struct protoent *prstruc = NULL;
  struct sockaddr_in inaddr;
#ifndef WIN32
  struct sockaddr_un unaddr;
#endif
  int unixsockets;
  unsigned short port;
  char name[256];
  int sockopt;

  if ( ! sockets_initialized ) initialize_sockets(NULL,NULL,NULL,NULL);

  unixsockets = 
#ifdef WIN32
    parse_address(address,name,&port,NULL,0);
#else
    parse_address(address,name,&port,unaddr.sun_path,sizeof(unaddr.sun_path));
#endif /* WIN32 */

#ifdef WIN32
  if ( unixsockets ) {
    warn("unix sockets not available on Windows");
    return INVALID_SOCKET;
  }
#endif /* WINDOWS */

  if ( !unixsockets ) {
    prstruc = getprotobyname("tcp");
    if ( prstruc == NULL ) {
      perror("getprotobyname failed");
      return INVALID_SOCKET;
    }
  }
  sfd = socket(unixsockets ? AF_UNIX : AF_INET,
	       SOCK_STREAM,
	       unixsockets ? 0 : prstruc->p_proto);
  if ( sfd == INVALID_SOCKET ) {
    perror("socket failed");
    return INVALID_SOCKET;
  }

  sockopt=1;
  setsockopt(sfd,SOL_SOCKET,SO_REUSEADDR,(void *)&sockopt,sizeof(int));

#ifndef WIN32
  if ( unixsockets ) {
    unaddr.sun_family = AF_UNIX;
    unlink(unaddr.sun_path); /* need to check success FIXME */
    if ( bind(sfd,(struct sockaddr *)&unaddr,sizeof(struct sockaddr_un)) < 0 ) {
      perror("bind failed");
      return INVALID_SOCKET;
    }
  } else {
#else
  if ( 1 ) {
#endif /* WIN32 */
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(port);
    inaddr.sin_addr.s_addr = INADDR_ANY;
    if ( bind(sfd,(struct sockaddr *)&inaddr,sizeof(struct sockaddr_in)) < 0 ) {
      perror("bind failed");
      return INVALID_SOCKET;
    }
  }

  if ( listen(sfd,5) < 0 ) {
    perror("listen failed");
    return INVALID_SOCKET;
  }
  
  if ( skt_open_transform ) {
    return (SOCKET) skt_open_transform(sfd);
  } else {
    return sfd;
  }
}
  
  

/* accept_new_connection:
   A connection has arrived on the socket fd.
   Accept the connection, and return the new fd,
   or INVALID_SOCKET on error.
*/

SOCKET accept_new_connection(SOCKET fd) {
  SOCKET newfd;
  struct sockaddr saddr;
  struct linger l = { 1, 1000 } ; /* linger on close for 10 seconds */
  socklen_t saddrlen = sizeof(saddr);

  if ( ! sockets_initialized ) initialize_sockets(NULL,NULL,NULL,NULL);

  newfd = accept(fd,&saddr,&saddrlen);
  if ( newfd == INVALID_SOCKET ) { perror("accept failed"); }
  /* we should make sure that data is sent when this socket is closed */
  if ( setsockopt(newfd,SOL_SOCKET,SO_LINGER,(void *)&l,sizeof(l)) < 0 ) {
    warn("setsockopt failed");
  }
  if ( skt_open_transform ) {
    return (SOCKET) skt_open_transform(newfd);
  } else {
    return newfd;
  }
}

/* connect_to_host:
   Establish a connection to the given address.
   Return the file descriptor or INVALID_SOCKET on error.
   If unixsockets, only the port number is used in making the name
   The returned socket is marked close-on-exec .
*/

static SOCKET _connect_to_host(int plain, const char *address) {
  SOCKET fd;
  struct sockaddr_in inaddr;
#ifndef WIN32
  struct sockaddr_un unaddr;
#endif
  char name[512];
  int unixsockets;
  unsigned short port;
  struct hostent *hent = NULL;
  struct protoent *prstruc = NULL;

  if ( ! sockets_initialized ) initialize_sockets(NULL,NULL,NULL,NULL);

#ifdef WIN32
  unixsockets = parse_address(address,name,&port,NULL,0);
  if ( unixsockets ) {
    warn("Unix sockets not supported on Windows");
    return INVALID_SOCKET;
  }
#else
  unixsockets = parse_address(address,name,&port,unaddr.sun_path,sizeof(unaddr.sun_path));
#endif /* WIN32 */

  if ( !unixsockets ) {
    hent = gethostbyname(name);
    
    if ( ! hent ) {
      perror("connect_to_host: gethostbyname failed");
      return INVALID_SOCKET;
    }
    
    prstruc = getprotobyname("tcp");
    if ( prstruc == NULL ) {
      perror("connect_to_host: getprotobyname failed");
      return INVALID_SOCKET;
    }
  }

  fd = socket(unixsockets ? AF_UNIX : AF_INET,
	      SOCK_STREAM,
	      unixsockets ? 0 : prstruc->p_proto);
  if ( fd == INVALID_SOCKET ) {
    perror("connect_to_host: socket failed");
    return INVALID_SOCKET;
  }

#ifndef WIN32
  if ( unixsockets ) {
    unaddr.sun_family = AF_UNIX;
    if ( connect(fd,(struct sockaddr *)&unaddr,sizeof(struct sockaddr_un)) < 0 ) {
      perror("connect_to_host: connect failed");
      return INVALID_SOCKET;
    }
  } else {
#else
  if ( 1 ) {
#endif /* WIN32 */
    inaddr.sin_family = AF_INET;
    inaddr.sin_port = htons(port);
    inaddr.sin_addr = *((struct in_addr *)hent->h_addr);
    if ( connect(fd,(struct sockaddr *)&inaddr,sizeof(struct sockaddr_in)) < 0 ) {
      perror("connect_to_host: connect failed");
      return INVALID_SOCKET;
    }
  }
  
#ifndef WIN32
  /* mark close on exec */
  fcntl(fd,F_SETFD,1);
#endif /* WIN32 */

  /* Set a five-minute keepalive in order to dissuade natting routers
     from dropping the connection */
  { int data = 1;
    int n;
    n = setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&data, sizeof(int));
    if ( n < 0 ) {
      perror("connect_to_host: setsockopt keepalive failed");
    } else {
#ifdef TCP_KEEPIDLE
      /* this call exists on modern Linuxes, but not everywhere */
      data = 300;
      n = setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &data, sizeof(int));
      if ( n < 0 ) {
	/* of course, this is not supported for unix sockets. We could
	   have checked the address family, but it's probably simpler
	   just to ignore errors */
	if ( errno != ENOTSUP ) {
	  perror("connect_to_host: setsockopt keepidle failed");
	}
      }
#endif
    }
  }

  if ( !plain && skt_open_transform ) {
    return (SOCKET) skt_open_transform(fd);
  } else {
    return fd;
  }
}

SOCKET connect_to_host(const char *address) {
  return _connect_to_host(0,address);
}

SOCKET plain_connect_to_host(const char *address) {
  return _connect_to_host(1,address);
}


/* close a network connection */
static int _close_socket(int plain, SOCKET s) {
  if ( !plain && skt_closer ) {
    return skt_closer((void *)s);
  } else {
    return closesocket(s);
  }
}

int close_socket(SOCKET s) {
  return _close_socket(0,s);
}

int plain_close_socket(SOCKET s) {
  return _close_socket(1,s);
}

/* read a line, up to lf/crlf terminator, from the socket,
   and return it.
   Internal backslashes are removed, unless doubled.
   If the line ends with an unescaped backslash, replace by newline,
   and append the next line.
   If there is an error or end of file before seeing a terminator,
   NULL is returned. At present, error includes reading a partial line.
   The returned string is valid until the next call of get_line.
   This is the internal function implementing the next two;
   the second arg says whether the first is int or SOCKET.
*/
static char *_get_line(SOCKET fd,int is_socket) {
  int length,i,j;
  int offset=0;
  static char *buffer = NULL;
  static size_t buffer_size = 0;

  while ( 1 ) {
    while ( 1 ) {
      while (offset+1 >= (int)buffer_size) { /* +1 to keep some space for '\0' */
	buffer_size = (buffer_size == 0) ? 1024 : (2 * buffer_size);
	buffer = realloc(buffer, buffer_size);
	if (!buffer) {
	  perror("get_line");
	  exit(-1);
	}
      }
      length = (is_socket ?
	(skt_reader ? skt_reader((void *)fd,buffer+offset,1)
		  : 
#ifdef WIN32
		  recv(fd,buffer+offset,1,0)
#else
		  read(fd,buffer+offset,1)
#endif
	 ) : read(fd,buffer+offset,1));
      if ((length <= 0) || (buffer[offset] == '\n'))
	break;
      offset += length;
    }
    if ( offset == 0 ) return NULL;
    if ( length <= 0 ) {
      fprintf(stderr,"get_line read partial line");
      return NULL;
    }
    offset += length;
    buffer[offset] = '\0';
    /* now check for newline */
    if ( buffer[offset-1] == '\n' ) {
      if ( buffer[offset-2] == '\r' && buffer[offset-3] == '\\' ) {
        /* zap the CR */
	buffer[offset-2] = '\n';
	buffer[offset-1] = '\0';
	offset -= 1;
        /* and go onto next clause */
      }
      if ( buffer[offset-2] == '\\' ) {
	/* we have to decide whether this is an escaped backslash */
	int j = 1,k=offset-3;
	while ( k >= 0 && buffer[k--] == '\\' ) j++;
	if ( j & 1 ) {
	  /* odd number, not escaped */
	  buffer[offset-2] = '\n' ;
	  buffer[--offset] = '\0' ;
	  /* read another line */
	} else break; /* return this line */
      } else break; /* return this line */
    } /* no newline, keep going */
  }
  /* now remove internal backslashes */
  for ( i = j = 0; 1; i++, j++ ) {
    if ( buffer[j] == '\\' ) {
      j++;
    }
    buffer[i] = buffer[j];
    if ( !buffer[i] ) break;
  }
  return buffer;
}

char *get_line(SOCKET fd) {
  /* are we actually being asked to read from stdout? Then read
     from stdin */
  if ( fd == STDOUT_FILENO ) return _get_line(STDIN_FILENO,0);
  return _get_line((int)fd,1);
}

char *fd_get_line(int fd) {
  return _get_line(fd,0);
}

static int _put_line_aux(SOCKET fd, char* line, int length, int is_socket) {
  int n;
  if ( is_socket ) {
    n = skt_writer ? skt_writer((void *)fd,line,length)
      :
#ifdef WIN32
      send(fd,line,length,0)
#else
      write(fd,line,length)
#endif
      ;
  } else {
    n = write(fd,line,length);
  }
  if ( n < length ) {
    perror("put_line: write failed");
    return -1;
  }
  return n;
}

/* write a line (which is assumed to have any terminator included)
   to the given socket. Return -1 on error, length of line on success.
   Replace internal newlines by backslash CRLF and write as lines.
   Double all internal backslashes.
   Third arg says whether first arg is really an int, or a SOCKET.
   This is the internal function implemented the next two.
*/
static int _put_line(int fd,char *line,int is_socket) {
  int length,n;
  static char *buffer = NULL;
  static size_t buf_size = 0;
  int i,j;

  /* the backslash/newline escaping can at worst treble the size
     of the input */
  length = strlen(line) + 1; /* include null */
  while ( (int)buf_size <= 3*length ) {
    buf_size = buf_size ? 2*buf_size : 1024;
    buffer = realloc(buffer,buf_size);
  }

  /* FIXME: should retry if only write partial line */
  i = j = 0;
  while ( *line ) {
    if ( *line == '\\' ) { buffer[i++] = '\\' ; }
    if ( *line == '\n' && *(line+1) ) {
      buffer[i++] = '\\' ;
      buffer[i++] = '\r' ;
      buffer[i++] = *(line++) ;
      n = _put_line_aux(fd,buffer,i,is_socket);
      if ( n < 0 ) { return -1 ; }
      i = 0;
    } else {
      buffer[i++] = *(line++);
    }
  }
  return _put_line_aux(fd,buffer,i,is_socket);
}

int put_line(SOCKET fd,char *line) {
  /* are we asked to write to stdout? */
  if ( fd == STDOUT_FILENO ) return _put_line(STDOUT_FILENO,line,0);
  return _put_line((int)fd,line,1);
}

int fd_put_line(int fd,char *line) {
  return _put_line(fd,line,0);
}

int put_data(SOCKET fd, char *data, int len) {
  int n;
  
  if ( len == 0 ) {
#   ifdef WIN32
    n = shutdown(fd, SD_SEND);
#   else
    n = shutdown(fd, SHUT_WR);
#   endif
    if ( n < 0 ) {
      warn("put_data failed to shutdown: %s",strerror(errno));
    }
    return n;
  }
# ifdef WIN32
  n = send(fd,data,len,0);
# else
  n = write(fd,data,len);
# endif
  if ( n < 0 ) {
    warn("put_data failed: %s",strerror(errno));
  }
  return n;
}

int get_data(SOCKET fd, char *data, int len) {
  int n;
# ifdef WIN32
  n = recv(fd,data,len,0);
# else
  n = read(fd,data,len);
# endif
  if ( n < 0 ) {
    warn("put_data failed: %s",strerror(errno));
  }
  return n;
}


/* random integer from 0 to top inclusive */
static int seed = 0;

unsigned int rand_index(int top) {

  if ( seed == 0 ) {
    /* seed the generator */
    rand_seed(0);
  }

  return (unsigned int)((1.0+top)*rand()/(RAND_MAX+1.0));
}

/* and seed it */
void rand_seed(int s) {
  if ( s == 0 ) s = time(NULL);
  srand(seed = s);
}

/* This is like strmcat, but it also quotes the string (in a system
   dependent way) for adding it to a shell command line.
*/
char *qstrmcat(char *dest, const char *src, int len) {
  char *d = dest;
  char *s = (char *)src;
  while ( *d ) d++;
  if ( len == 0 ) return dest;
#ifdef WIN32
  *d = '"'; d++; len--;
#else
  *d = '\''; d++; len--;
#endif
  while ( *s && len > 0 ) {
#ifdef WIN32
    if ( *s == '"' ) {
      *d++ = '"'; len--;
      if ( len > 0 ) {
	*d++ = *s++; len--;
      }
    } else {
      *d++ = *s++; len--;
    }
#else
    if ( *s == '\'' || *s == '\\' ) {
      *d++ = '\''; len--;
      if ( len > 2 ) {
	*d++ = '\\'; len--;
	*d++ = *s++; len--;
	*d++ = '\''; len--;
      }
    } else {
      *d++ = *s++; len--;
    }
#endif
  }
  if ( len > 0 ) {
#ifdef WIN32
    *d++ = '"'; len--;
#else
    *d++ = '\''; len--;
#endif
  }
  if ( len == 0 ) d--;
  *d = 0;
  return dest;
}


/* utility function for following: return "NULL" for null string */
char *nullprotect(char *s) { return s ? s : "NULL"; }

/* feed a command to the system to be started in background.
   No fancy argument processing, just pass to shell or equiv.
   Return 1 on success, 0 on failure. */
int start_background_program(const char *cmd) {
  char buf[1024];
# ifdef WIN32
  int res;
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  
  strmcpy(buf,cmd,1023); buf[1023] = 0;
  memset((void *)&si,0,sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);
  res = CreateProcess(NULL,buf,NULL,NULL,
		0,DETACHED_PROCESS,NULL,NULL,&si,&pi);
  if ( res ) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else {
    warn("Failed to start process %s",cmd);
  }
  return res;
# else
  strmcpy(buf,cmd,sizeof(buf)-strlen(buf)-1);
  strcat(buf," &");
# endif
  return ! system(buf);
}

/* unfortunately we need a slightly more sophisticated one where
   we can get at the output. This passes out a write handle for the
   new process's stdin, and a read handle for its stdout */
#ifndef WIN32
int start_background_program_with_io(const char *cmd,int *childin,
  int *childout)
{
  int tochild[2], fromchild[2];
  int cpid;
  
  if ( pipe(tochild) < 0 ) {
    perror("Unable to create pipe");
    return 0;
  }
  if ( pipe(fromchild) < 0 ) {
    perror("Unable to create pipe: %s");
    return 0;
  }
  cpid = fork();
  if ( cpid < 0 ) {
    perror("Unable to fork: %s");
  }
  if ( cpid ) {
    /* parent */
    close(tochild[0]);
    close(fromchild[1]);
    *childin = tochild[1];
    *childout = fromchild[0];
    return 1;
  } else {
    close(tochild[1]);
    close(fromchild[0]);
    dup2(tochild[0],0);
    close(tochild[0]);
    dup2(fromchild[1],1);
    close(fromchild[1]);
    execl("/bin/sh","sh","-c",cmd,NULL);
    /* what can we do if we get here? Not a lot... */
    perror("Exec of child failed");
    exit(1);
  }
  return 0;
}
#else
/* This windows version is loosely based on the example
   Creating a Child Process with Redirected Input and Output
   from the MSDN Library. See that example for all the comments
   explaining what's going on. 
   In fact it's (a) easier than that, since instead of saving, setting
   and inheriting handles, we can put the pipes into the startup
   info of the CreateProcess call. (Thanks to somebody on Usenet,
   no thanks to Microsoft!); (b) harder, because we need to pass
   back C-style file descriptors, not Windows handles. Hence the mystic
   call to _open_osfhandle. */
int start_background_program_with_io(const char *cmd,int *childin,
  int *childout)
{
  int res;
  char buf[1024];
  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  SECURITY_ATTRIBUTES sa;
  HANDLE tochildr, tochildw, tochildw2, 
    fromchildr, fromchildr2, fromchildw;
  
  sa.nLength = sizeof(SECURITY_ATTRIBUTES);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = NULL;
  if ( ! CreatePipe(&fromchildr,&fromchildw,&sa,0 ) ) {
    perror("couldn't create pipe");
    return 0;
  }
  DuplicateHandle(GetCurrentProcess(),
    fromchildr,GetCurrentProcess(),&fromchildr2,0,
    FALSE, DUPLICATE_SAME_ACCESS);
  CloseHandle(fromchildr);

  if ( ! CreatePipe(&tochildr,&tochildw,&sa,0 ) ) {
    perror("couldn't create pipe");
    return 0;
  }
  DuplicateHandle(GetCurrentProcess(),
    tochildw,GetCurrentProcess(),&tochildw2,0,
    FALSE, DUPLICATE_SAME_ACCESS);
  CloseHandle(tochildw);

  strmcpy(buf,cmd,1023); buf[1023] = 0;
  memset((void *)&si,0,sizeof(STARTUPINFO));
  si.cb = sizeof(STARTUPINFO);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = fromchildw;              
  si.hStdInput  = tochildr;           
  si.hStdError  = 0;
  res = CreateProcess(NULL,buf,NULL,NULL,
		TRUE,DETACHED_PROCESS,NULL,NULL,&si,&pi);
  if ( res ) {
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
  } else {
    warn("Failed to start process %s",cmd);
  }
  /* I think the mingw has the wrong type for _open_osfhandle ? */
  *childin = _open_osfhandle((int)tochildw2,_O_WRONLY);
  *childout = _open_osfhandle((int)fromchildr2,_O_RDONLY);
  /* do we, as in unix, close our versions of the handles we don't want? */
  if ( 1 ) {
    CloseHandle(tochildr);
    CloseHandle(fromchildw);
  }
  return res;
}
#endif

/* return a home directory */
char *get_homedir(void) {
  char *p;
# ifdef WIN32
  static char buf[512];
  buf[0] = 0;
  if ( (p = getenv("HOMEDRIVE")) ) strcpy(buf,p);
  if ( (p = getenv("HOMEPATH")) ) strcat(buf,p);
  if ( buf[0] ) return buf;
  return NULL;
# else
  struct passwd *pw;
  if ( (p = getenv("HOME")) ) return p;
  if ( (pw = getpwuid(getuid())) ) return pw->pw_dir;
  return NULL;
# endif
}

/* stuff for Windows */
#ifdef WIN32
int gettimeofday(struct timeval *tv, void *tz UNUSED) {
  long sec, usec;
  LONGLONG ll;
  FILETIME ft;

  GetSystemTimeAsFileTime(&ft);
  ll = ft.dwHighDateTime;
  ll <<= 32;
  ll |= ft.dwLowDateTime;
  /* this magic number comes from Microsoft's knowledge base ' */
  ll -= 116447360000000000LL;  /* 100 nanoseconds since 1970 */
  ll /= 10; /* microseconds since 1970 */
  sec = (long)(ll / 1000000);
  usec = (long)(ll - 1000000*((LONGLONG) sec));
  if ( tv ) {
    tv->tv_sec = sec;
    tv->tv_usec = usec;
  }
  return 0;
}

char *getlogin(void) { 
  static char buf[128];
  DWORD len = 128;

  buf[0] = 0;
  GetUserName(buf,&len);
  return buf;
}
 
int getpid(void) {
  return GetCurrentProcessId();
}

int unlink(const char *f) {
  return DeleteFile(f); 
}

unsigned int sleep(unsigned int t) { Sleep(1000*t); return 0; }

/* I don't think we really need submillisecond resolution ... */
unsigned int usleep(unsigned int t) { Sleep(t/1000); return 0; }


#endif /* WIN32 */
