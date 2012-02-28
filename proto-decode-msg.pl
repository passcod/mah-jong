# $Header: /home/jcb/MahJong/newmj/RCS/proto-decode-msg.pl,v 12.0 2009/06/28 20:43:13 jcb Rel $
# proto-decode-msg.pl
# script to generate body of decode_[cp]msg function in protocol.c
# it is not defensively programmed!
# The code it generates *is* defensively programmed, of course.
# Well, somewhat.

#***************** COPYRIGHT STATEMENT **********************
#* This file is Copyright (c) 2000 by J. C. Bradfield.       *
#* Distribution and use is governed by the LICENCE file that *
#* accompanies this file.                                    *
#* The moral rights of the author are asserted.              *
#*                                                           *
#***************** DISCLAIMER OF WARRANTY ********************
#* This code is not warranted fit for any purpose. See the   *
#* LICENCE file for further information.                     *
#*                                                           *
#*************************************************************

# debugging
$debug = $ENV{'DEBUG'};

# are we doing cmsg or pmsg ?
if ( $ARGV[0] eq '-cmsg' ) {
  $L = 'C' ; $l = 'c';
  $msgtype = "Controller";
  $infile = "protocol.h";
} elsif ( $ARGV[0] eq '-pmsg' ) {
  $L = 'P' ; $l = 'p' ;
  $msgtype = "Player";
  $infile = "protocol.h";
} elsif ( $ARGV[0] eq '-mcmsg' ) {
  $L = 'MC' ; $l = 'mc' ;
  $msgtype = "MController";
  $infile = "mprotocol.h";
} elsif ( $ARGV[0] eq '-mpmsg' ) {
  $L = 'MP' ; $l = 'mp' ;
  $msgtype = "MPlayer";
  $infile = "mprotocol.h";
} else {
  die("No function argument");
}

open(STDIN,"<$infile") or die("No infile");

open(STDOUT,">dec_${l}msg.c");

# The code we're trying to generate looks like this:
# if ( strcmp(type,"PlayerPungs") == 0) {
#   CMsgPlayerPungsMsg *m;
#
#   m = (CMsgPlayerPungsMsg *)malloc(sizeof(CMsgPlayerPungsMsg));
#   if ( ! m ) { warn("malloc failed\n") ; return (CMsgMsg *)0; }
#
#   m->type = CMsgPlayerPungs;
#
#   if ( sscanf(s,"%d%n",&an_int,&n) == 0 ) { warn("protocol error\n"); return (CMsgMsg *)0; }
#   m->id = an_int;
#   s += n;
#
#  and so on and so on. Note that for (char *) args, we need to 
#  malloc the space.

# a function to print the first few lines, given the type.
sub firstchunk {
  my($type) = $_[0];
  my($atext) = '';

  if ( $alias ) {
    $atext = " || strcmp(type,\"${alias}\") == 0 ";
  }
  print 
"  if ( strcmp(type,\"${type}\") == 0 ${atext}) {
    ${L}Msg${type}Msg *m;

    m = (${L}Msg${type}Msg *)malloc(sizeof(${L}Msg${type}Msg));
    if ( ! m ) { warn(\"malloc failed\\n\") ; return (${L}MsgMsg *)0; }

    m->type = ${L}Msg${type};

";
}

while ( ! eof(STDIN) ) {
  while ( <STDIN> ) {
    chop;
    # look for the beginning of the structure definition
    next unless s/^.*struct _${L}Msg// ;
    s/Msg \{//;
    # it is an undocumented fact that certain common commands have
    # short aliases, for ease in debugging via telnet sessions.
    # these are implemented via a comment of the form /* alias c */
    # on the typedef line.
    $alias = undef;
    if ( s,\s*/\*\s+alias\s+(\S+).*$,, ) { $alias = $1; }
    last if ! $_ ; # got to the dummy type
    $type = $_ ;
    $_ = <STDIN> ; # skip the type
    &firstchunk($type) ; # print the first few lines
    # now deal with the components
    while ( <STDIN> ) {
      chop;
      s/;.*$// ; # junk all but type and name
      if ( s/^\s*int\s+// ) {
	print '    if ( sscanf(s,"%d%n",&an_int,&n) == 0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    m->$_ = an_int;\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*bool\s+// ) {
	print '    if ( sscanf(s,"%d%n",&an_int,&n) == 0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
        print '    if ( an_int < 0 || an_int > 1 ) { warn("protocol error\n") ; return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    m->$_ = an_int;\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*PlayerOption\s+// ) {
	print '    if ( sscanf(s,"%31s%n",little_string,&n) ==0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    m->$_ = player_scan_PlayerOption(little_string);\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*TileWind\s+// ) {
	print '    if ( sscanf(s,"%c%n",&a_char,&n) == 0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    m->$_ = letterwind(a_char);\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*Tile\s+// ) {
	print '    if ( sscanf(s,"%2s%n",little_string,&n) == 0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    m->$_ = tile_decode(little_string);\n";
	print "    if ( m->$_ == ErrorTile ) { warn(\"protocol error\\n\"); return (${L}MsgMsg *)0; }\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*word(\d+)\s+// ) {
	$wordlen = $1;
	print '    if ( sscanf(s,"%',$wordlen,'s%n",little_string,&n) == 0 ) { warn("protocol error\n");  return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    strmcpy(m->$_,little_string,$wordlen);\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*ChowPosition\s+// ) {
	# the 31 field width is for safety. This string should always
	# be lower, middle or upper!
	# Argh. This code does not defend against protocol errors.
	# FIXME
	print '    if ( sscanf(s,"%31s%n",little_string,&n) == 0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    m->$_ = string_cpos(little_string);\n";
	print "    s += n;\n";
	print "    while ( isspace(*s) ) s++;\n\n";
	next;
      }
      if ( s/^\s*char\s+\*\s*// ) {
	# we are allowed to assume this is the last field
	print "    if ( strlen(s) == 0 ) m->$_ = (char *)0;\n";
	print "    else {\n";
	print "      m->$_ = (char *)malloc(strlen(s)+1);\n";
	print "      if ( ! m->$_ ) { warn(\"malloc failed\\n\"); return (${L}MsgMsg *)0; }\n";
	# we now need to do a little backslash unescaping.
        # At this level of protocol, all we do is say: if arg starts
	# with \, remove it - any initial backslash must be there
	# to protect the next character
	print "      strcpy(m->$_,s+(s[0] == '\\\\'));\n";
	print "    }\n";
	next;
      }
      if ( s/^\s*GameOptionEntry\s+// ) {
	print '    if ( (goe = protocol_scan_GameOptionEntry(s)) == 0 ) { warn("protocol error\n"); return (' . $L . 'MsgMsg *)0; }', "\n";
	print "    memcpy(&m->$_,goe,sizeof(GameOptionEntry));\n";
	next;
      }
      last if  s/^\}// ; # the end
      print STDERR "I hope this is a comment: $_\n" if $debug;
    }
    print "    return (${L}MsgMsg *) m;\n";
    print "  }\n";
  }
}
