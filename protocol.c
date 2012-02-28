/* $Header: /home/jcb/MahJong/newmj/RCS/protocol.c,v 12.0 2009/06/28 20:43:13 jcb Rel $
 * protocol.c 
 * implements the over-the-wire protocol for messages
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/protocol.c,v 12.0 2009/06/28 20:43:13 jcb Rel $";

#include "protocol.h"
#include "tiles.h"
#include "sysdep.h"

/* See protocol.h for documentation */
int protocol_version = PROTOCOL_VERSION;

/* The protocol is, for all the usual reasons, text-based,
   and human readable. (So all you need to play is telnet...)
   A message usually occupies a single line, terminated by CRLF or LF.
   To transmit messages containing newlines, the internal newlines
   are replaced by backslash-newline pairs; thus a telnet line ending
   in a backslash indicates newline followed by the next telnet line.
   Integers are represented in decimal.
   The message type is given in text, obtained by removing the CMsg
   or PMsg prefix from the type name.
   Tiles are represented as a 2-character code, as follows:
   1C ... 9C   characters
   1D ... 9D   circles (dots)
   1B ... 9B   bamboos
   EW ... NW   winds
   RD, WD, GD  dragons
   1S ... 4S   seasons
   1F ... 4F   flowers
   --          blank
   Winds are represented as single letters.
   The code for representing tiles in this way comes from the tiles 
   module, as it has uses outside this protocol.
   ChowPositions are represented as the words Lower, Middle, Upper,AnyPos.
   Strings are deemed to extend to end of line. This is safe because
   the current protocol has at most one string field per message, which
   is always final. There is unlikely to be a reason to change this
   assumption.
*/

/* Takes a TileWind and returns the corresponding letter */
static char windletter(TileWind w) {
  switch ( w ) {
  case EastWind: return 'E';
  case SouthWind: return 'S';
  case WestWind: return 'W';
  case NorthWind: return 'N';
  default: return '?';
  }
}

/* given a wind letter, return the wind */
static TileWind letterwind(char c) {
  if ( c == 'E' ) return EastWind;
  if ( c == 'S' ) return SouthWind;
  if ( c == 'W' ) return WestWind;
  if ( c == 'N' ) return NorthWind;
  return UnknownWind;
}
				     
/* given a chowposition string, return the chowposition */
/* it is an undocumented :-) fact that this function will
   accept l,m,u,a for Lower, Upper, Middle, AnyPos */
static ChowPosition string_cpos(char *s) {
  if ( strcmp(s,"Lower") == 0 ) return Lower;
  if ( strcmp(s,"l") == 0 ) return Lower;
  if ( strcmp(s,"Middle") == 0 ) return Middle;
  if ( strcmp(s,"m") == 0 ) return Middle;
  if ( strcmp(s,"Upper") == 0 ) return Upper;
  if ( strcmp(s,"u") == 0 ) return Upper;
  if ( strcmp(s,"AnyPos") == 0 ) return AnyPos;
  if ( strcmp(s,"a") == 0 ) return AnyPos;
  return -1;
}


/* return a string code for a chowposition */
static char *cpos_string(ChowPosition r) {
  switch ( r ) {
  case Lower: return "Lower";
  case Middle: return "Middle";
  case Upper: return "Upper";
  case AnyPos: return "AnyPos";
  }
  return "error";
}

/* functions to print and scan a GameOptionEntry; at the end */

static char *protocol_print_GameOptionEntry(const GameOptionEntry *t);
/* This function uses static storage; it's not reentrant */
static GameOptionEntry* protocol_scan_GameOptionEntry(const char *s);

/* This function takes a pointer to a controller message,
   and returns a string (including terminating CRLF) encoding it.
   The returned string is in static storage, and will be overwritten
   the next time this function is called.
*/

char *encode_cmsg(CMsgMsg *msg) {
  static char *buf = NULL;
  static size_t buf_size = 0;
  int badfield; /* used by internal code */

  switch ( msg->type ) {
    /* The body of this switch statement is automatically generated
       from protocol.h by proto-enc-cmsg.pl; */
#   include "enc_cmsg.c"
  default:
    warn("unknown message type %d\n",msg->type);
    return (char *)0;
  }
  return buf;
}

/* This function takes a string, which is expected to be a complete
   line (including terminators, although their absence is ignored).
   It returns a pointer to a message structure.
   Both the message structure and any strings it contains are malloc'd.
   The argument string will be destroyed by this function.
*/
    
CMsgMsg *decode_cmsg(char *arg) {
  char *s;
  char *type;
  int n;
  int i;
  int an_int;
  char a_char;
  char little_string[32];
  GameOptionEntry *goe;

  /* first eliminate the white space from the end of the string */
  i=strlen(arg) - 1;
  while ( i >= 0 && isspace(arg[i]) ) arg[i--] = '\000' ;
  /* first get the type of the message, which is the first word */
  /* SPECIAL hack: the comment message has a non alpha num type */
  type = arg;
  i = 0;
  while ( isalnum(arg[i]) || (arg[i] == '#') ) i++;
  arg[i++] = '\000' ; /* if this wasn't white space, summat's wrong anyway */
  type = arg;
  while ( isspace(arg[i]) ) i++; /* may as well advance to next item */
  s = arg+i; /* s is what the rest of the code uses */
  /* The rest of this function body is generated automatically from
     protocol.h by proto-dec-cmsg.pl */
# include "dec_cmsg.c"
  /* if we come out of this, it's an error because we haven't found
     the keyword */
  warn("decode_cmsg: unknown key in  %s",arg);
  return NULL;
}

/* The next two functions do the same for pmsgs */

/* This function takes a pointer to a player message,
   and returns a string (including terminating CRLF) encoding it.
   The returned string is in static storage, and will be overwritten
   the next time this function is called.
*/

char *encode_pmsg(PMsgMsg *msg) {
  static char *buf = NULL;
  static size_t buf_size = 0;
  int badfield; /* used by internal code */

  switch ( msg->type ) {
    /* The body of this switch statement is automatically generated
       from protocol.h by proto-enc-pmsg.pl; */
#   include "enc_pmsg.c"
  default:
    warn("unknown message type\n");
    return (char *)0;
  }
  return buf;
}

/* This function takes a string, which is expected to be a complete
   line (including terminators, although their absence is ignored).
   It returns a pointer to a message structure.
   Both the message structure and any strings it contains are malloc'd.
   The argument string will be destroyed by this function.
*/
    
PMsgMsg *decode_pmsg(char *arg) {
  char *s;
  char *type;
  int n;
  int i;
  int an_int;
  char little_string[32];

  /* first eliminate the white space from the end of the string */
  i=strlen(arg)-1;
  while ( i >= 0 && isspace(arg[i]) ) arg[i--] = '\000' ;
  /* first get the type of the message, which is the first word */
  type = arg;
  i = 0;
  while ( isalnum(arg[i]) ) i++;
  arg[i++] = '\000' ; /* if this wasn't white space, summat's wrong anyway */
  type = arg;
  while ( isspace(arg[i]) ) i++; /* may as well advance to next item */
  s = arg+i; /* s is what the rest of the code uses */
  /* The rest of this function body is generated automatically from
     protocol.h by proto-dec-pmsg.pl */
# include "dec_pmsg.c"
  /* if we come out of this, it's an error because we haven't found
     the keyword */
  warn("decode_pmsg: unknown key in  %s",arg);
  return NULL;
}

/* the size functions */
#include "cmsg_size.c"
#include "pmsg_size.c"

/* enum functions */
#include "protocol-enums.c"

/* functions for GameOptionEntry */
static GameOptionEntry *protocol_scan_GameOptionEntry(const char *s) {
  const char *sp;
  char tmp[100];
  static GameOptionEntry goe;
  int n;
  sp = s;
  
  if ( sscanf(sp,"%15s%n",goe.name,&n) == 0 ) {
    warn("protocol error");
    return NULL;
  }
  sp += n;

  goe.option = protocol_scan_GameOption(goe.name);
  if ( goe.option == (GameOption)(-1) ) goe.option = GOUnknown;

  if ( sscanf(sp,"%15s%n",tmp,&n) == 0 )  {
    warn("protocol error");
    return NULL;
  }
  sp += n;

  goe.type = protocol_scan_GameOptionType(tmp);
  if ( goe.type == (GameOptionType)(-1) ) {
    warn("unknown game option type");
    return NULL;
  }

  if ( sscanf(sp,"%d%n",&goe.protversion,&n) == 0 )  {
    warn("protocol error");
    return NULL;
  }
  sp += n;

  if ( sscanf(sp,"%d%n",&goe.enabled,&n) == 0 )  {
    warn("protocol error");
    return NULL;
  }
  sp += n;

  switch ( goe.type ) {
  case GOTBool:
    if ( sscanf(sp,"%d%n",&goe.value.optbool,&n) == 0 )  {
      warn("protocol error");
      return NULL;
    }
    if ( goe.value.optbool != 0 && goe.value.optbool != 1 ) {
      warn("bad value for boolean game option");
      return NULL;
    }
    sp += n;
    break;
  case GOTNat:
  case GOTInt:
  case GOTScore:
    if ( sscanf(sp,"%d%n",&goe.value.optint,&n) == 0 )  {
      warn("protocol error");
      return NULL;
    }
    sp += n;
    break;
  case GOTString:
    if ( sscanf(sp,"%127s%n",goe.value.optstring,&n) == 0 )  {
      warn("protocol error");
      return NULL;
    }
    sp += n;
    break;
  }
  if ( sscanf(sp," %127[^\r\n]",goe.desc) == 0 ) {
    warn("protocol error");
    return NULL;
  }
  goe.userdata = NULL;
  return &goe;
}

static char *protocol_print_GameOptionEntry(const GameOptionEntry *goe) {
  static char tmp[2*sizeof(GameOptionEntry)];
  sprintf(tmp,"%s %s %d %d ",
	  goe->name,
	  protocol_print_GameOptionType(goe->type),
	  goe->protversion,
	  goe->enabled);
  switch (goe->type) {
  case GOTBool:
    sprintf(tmp+strlen(tmp),"%d ",goe->value.optbool);
    break;
  case GOTNat:
  case GOTInt:
  case GOTScore:
    sprintf(tmp+strlen(tmp),"%d ",goe->value.optint);
    break;
  case GOTString:
    sprintf(tmp+strlen(tmp),"%s ",goe->value.optstring);
    break;
  }
  strcat(tmp,goe->desc);
  return tmp;
}

  
/* basic_expand_protocol_text: do the expansion of protocol text
   according to the rules set out in protocol.h. We don't recognize
   any codes or types. bloody emacs ' */
int basic_expand_protocol_text(char *dest,const char *src,int n)
{
  int destlen = 0; /* chars placed in destination string */
  const char *argp[10]; /* pointers to start of argument texts (excluding {);
                     arg 0 is replacement text */
  const char *curarg = NULL;
  int i,j;
  int numargs = 0;
  int state= 0; 
  int done;
  
  /* sanity check */
  if ( !dest || !n ) {
    warn("basic_expand_protocol_text: strange args");
    return -1;
  }

  /* null source expands to empty string */
  if ( !src ) {
    dest[0] = 0;
    return 0;
  }

  /* initialization */
  for ( i=0 ; i<10 ; argp[i++] = NULL );
  j = 0; /* current arg */

  done = 0;
  while ( !done ) {
    switch ( state ) {
    case 0: /* outside macro */
      if ( ! *src ) {  done=1 ; break ; }
      if ( *src == '\\' ) {
	src++;
	if ( *src ) dest[destlen++] = *(src++);
      } else if ( *src == '{' ) {
	src++;
	state = 1; /* reading macro defn */
	/* skip the code */
	while ( *src 
		&& (*src != '#') && (*src != ':') && (*src != '}') ) src++;
	numargs = 0;
      }
      else { dest[destlen++] = *(src++); }
      break;
    case 1: /* reading macro definition */
      if ( ! *src ) { done=1; break; }
      if ( *src == '#' ) {
	numargs++;
	src++;
	/* skip the type */
	while ( *src 
		&& (*src != '#') && (*src != ':') && (*src != '}') ) src++;
      } else if ( *src == ':' ) {
	state = 2;
	src++;
	argp[0] = src;
      } else if ( *src == '}' ) {
	state = 2;
	/* and read it again */
      }
      break;
    case 2: /* reading replacement text */
      /* do nothing; we've stored a pointer to it */
      if ( !*src ) { done=1; break;}
      if ( *src == '\\' ) {
	src++;
	if ( *src ) src++;
      } else if ( *src == '}' ) {
	src++;
	/* are there any arguments? */
	if ( numargs > 0 ) {
	  state = 3; /* reading argument texts */
	  j = 1; /* current arg */
	} else {
	  state = 4; /* expanding macro */
	}
      } else src++;
      break;
    case 3: /* reading argument texts */
      if ( !*src ) { done=1; break;}
      if ( *src == '{' ) {
	src++;
	argp[j] = src;
      } else if ( *src == '}' ) {
	src++;
	if ( j == numargs ) {
	  state = 4; /* expand the macro */
	} else j++;
      } else src++;
      break;
    case 4: /* expand the macro */
      /* now we're looking at argp[0] */
      if ( ! *argp[0] ) { state = 0; }
      else if ( *argp[0] == '\\' ) {
	argp[0]++;
	if ( *argp[0] ) dest[destlen++] = *argp[0];
	argp[0]++;
      } else if ( *argp[0] == '}' ) {
	state = 0;
      } else if ( *argp[0] == '#' ) {
	/* expand the argument */
	argp[0]++;
	j = *argp[0] - '0';
	if ( j >= 1 && j <= 9 ) {
	  argp[0]++;
	  curarg = argp[j];
	  state = 5;
	}
      } else dest[destlen++] = *(argp[0]++);
      break;
    case 5: /* copying an argument */
      if ( ! *curarg ) { state = 4; }
      else if ( *curarg == '\\' ) {
	curarg++;
	if ( *curarg ) { dest[destlen++] = *(curarg++); }
      } else if ( *curarg == '}' ) {
	state = 4; 
      } else dest[destlen++] = *(curarg++);
      break;
    }
    if ( destlen >= n - 1 ) { break ; }
  }
  if ( destlen > n-1 ) {
    dest[n-1] = '\000';
    return -1;
  }
  dest[destlen] = '\000';
  if ( state > 0 ) {
    warn("basic_expand_protocol_text: some error in expansion");
    return 0;}
  return 1;
}

int expand_protocol_text(char *dest,const char *src,int n)
{
  return basic_expand_protocol_text(dest,src,n);
}
