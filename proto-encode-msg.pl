# $Header: /home/jcb/MahJong/newmj/RCS/proto-encode-msg.pl,v 12.1 2012/02/01 15:29:24 jcb Rel $
# proto-encode-msg.pl
# generate the protocol conversion switch for c/pmsgs
# Also, generate in cmsg_union.h a definition of a 
# massive union type.
# Also, in cmsg_size.c, a function to return the size of a given
# type of cmsg. If the size is -n, that indicates that the 
# last field (last sizeof(char*) bytes) is a char *.

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

open(IN,"<$infile") or die("No infile");

open(STDOUT,">enc_${l}msg.c");
open(UNION,">${l}msg_union.h");
open(SIZE,">${l}msg_size.c");

print UNION
"typedef union _${L}MsgUnion {
  /* Note that this type field relies on the fact that all
     messages have type as their first field */\n";
print UNION $msgtype, "MsgType type;\n";

print SIZE
"static int ${l}msg_size[] = {\n";

while ( ! eof(IN) ) {
  while ( <IN> ) {
    # unfortunately, we need to catch the enum values
    if ( /^\s*${L}Msg(\S+)\s*=\s*(\d+)/ ) { 
      $enums[$2] = $1;
    }
    next if ! /struct _${L}Msg/;
    chop;
    s/^.*${L}Msg//; s/Msg\s*\{// ;
    s,\s*/\*.*$,, ; # strip comment
    $type = $_ ;
    if ( ! $type ) { # reached the dummy type
      $doneall = 1;
      last;
    }
    print "  case ${L}Msg$type: \n";
    print "    { ${L}Msg${type}Msg *m UNUSED = (${L}Msg${type}Msg *) msg;\n";
    $format = "$type";
    # special hack for the Comment message
    if ( $type eq 'Comment' ) { $format = "#"; }
    $args = '';
    $dofinish = 1;
    $_ = <IN> ; # skip type
    while ( <IN> ) {
      chop;
      if ( s/^\s*int\s+// ) {
	$format .= " %d";
	s/;.*//;
	$args .= ", m->$_";
      } elsif ( s/^\s*bool\s+// ) {
	$format .= " %d";
	s/;.*//;
	print "    badfield = 0;\n";
	print "    if ( m->$_ < 0 || m->$_ > 1 ) { warn(\"bad boolean in message, assuming TRUE\"); badfield = 1 ; };\n";
	$args .= ", (badfield ? 1 : m->$_)";
      } elsif ( s/^\s*char \*// ) {
	s/;.*//;
	# if the argument happens to start with a space or backslash, then
	# we need to escape it with a backslash
	$format .= " %s%s";
	# if the argument is null, don't pass it to printf,
	# but pass the empty string instead
	$args .= ",(m->$_ && (m->$_\[0] == ' ' || m->$_\[0] == '\\\\')) ? \"\\\\\" : \"\", m->$_ ? m->$_ : \"\" ";
	# and flag this type:
	$charstar{$type} = $_; # see below ...
      } elsif ( s/^\s*word\d+\s+// ) {
	$format .= " %s";
	s/;.*//;
	$args .= ", m->$_ ";
      } elsif ( s/^\s*TileWind\s+// ) {
	$format .= " %c";
	s/;.*//;
	$args .= ", windletter(m->$_)";
      } elsif ( s/^\s*PlayerOption\s+// ) {
	$format .= " %s";
	s/;.*//;
	$args .= ", player_print_PlayerOption(m->$_)";
      } elsif ( s/^\s*Tile\s+// ) {
	$format .= " %s";
	s/;.*//;
	$args .= ", tile_code(m->$_)";
      } elsif ( s/^\s*ChowPosition\s+// ) {
	$format .= " %s";
	s/;.*//;
	$args .= ", cpos_string(m->$_)";
      } elsif ( s/^\s*GameOptionEntry\s+// ) {
	$format .= " %s";
	s/;.*//;
	$args .= ", protocol_print_GameOptionEntry(&m->$_)";
      } elsif ( /^\}/ ) { last ; }
      else { print STDERR "hope this is a comment: $_\n" if $debug; }
    }
    next if ! $dofinish;
    $format .= '\r\n';
    print "      while (1) {\n";
    print "        int size = snprintf(buf,buf_size,\"$format\"$args);\n";
    print "        if ((size >= 0)&&(size < (int)buf_size))\n";
    print "          break;\n";
    print "        buf_size = (size >= 0)?((size_t)size+1):(buf_size > 0)?(buf_size*2):1024;\n";
    print "        buf = realloc(buf, buf_size);\n";
    print "        if (!buf) {\n";
    print "          perror(\"encode_" . $l . "msg\");\n";
    print "          exit(-1);\n";
    print "        }\n";
    print "      }\n";
    print "    }\n";
    print "    break;\n";
  }
  last if $doneall;
}

for ( $i = 0; $i <= $#enums; $i++ ) {
  $t = $enums[$i];
  if ( $t ) {
    $s = $t;
    $s =~ tr/A-Z/a-z/;
    print UNION "${L}Msg${t}Msg $s;\n";
  }
  if ( defined($charstar{$t}) ) { $neg = '-' ; } else { $neg = '' ; }
  print SIZE +($t ? "${neg}(int)sizeof(${L}Msg${t}Msg)" : 0), ",\n";
}

print UNION "} ${L}MsgUnion;\n";
print SIZE 
"};

int ${l}msg_size_of(",$msgtype,"MsgType t) { return ${l}msg_size[t]; }\n\n";

# While we're at it, we'll also generate functions to do
# a deep copy (malloc'ing all space), and to deep free
# the structures.

print SIZE
  "${L}MsgMsg *${l}msg_deepcopy(${L}MsgMsg *m) {
  ${L}MsgMsg *n;
  int size;
  char *mc,*nc;

  size = ${l}msg_size_of(m->type);
  if ( size < 0 ) size *= -1;
  n = (${L}MsgMsg *)malloc(size);
  if ( ! n ) return n;

  memcpy((void *)n,(const void *)m,size);
  switch ( m->type ) {
";

foreach $k ( keys %charstar ) {
  $c = $charstar{$k};
  print SIZE
"  case ${L}Msg$k:
    mc = ((${L}Msg${k}Msg *)m)->$c;
    if ( mc ) {
      nc = (char *)malloc(strlen(mc)+1);
      if ( ! nc ) return NULL;
      strcpy(nc,mc);
    } else {
      nc = NULL;
    }
    ((${L}Msg${k}Msg *)n)->$c = nc;
    break;
";
}

print SIZE 
"  default:
      ;
  }
  return n;
}\n\n";

print SIZE
"void ${l}msg_deepfree(${L}MsgMsg *m) {
  switch ( m->type ) {
";

foreach $k ( keys %charstar ) {
  $c = $charstar{$k};
  print SIZE
"  case ${L}Msg$k:
    if ( ((${L}Msg${k}Msg *)m)->$c )
      free((void *)((${L}Msg${k}Msg *)m)->$c);
    break;
";
}

print SIZE 
"  default:
      ;
  }
  free((void *)m);
}\n\n";
