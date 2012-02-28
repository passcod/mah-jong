# $Header: /home/jcb/MahJong/newmj/RCS/make-enums.pl,v 12.0 2009/06/28 20:43:12 jcb Rel $
# make-enums.pl

#****************** COPYRIGHT STATEMENT **********************
#* This file is Copyright (c) 2000 by J. C. Bradfield.       *
#* Distribution and use is governed by the LICENCE file that *
#* accompanies this file.                                    *
#* The moral rights of the author are asserted.              *
#*                                                           *
#***************** DISCLAIMER OF WARRANTY ********************
#* This code is not warranted fit for any purpose. See the   *
#* LICENCE file for further information.                     *
#*                                                           *
#*************************************************************/

# This is a utility script. It takes one argument, which should
# be a C header file foo.h .
# It then rips through it, and generates functions to convert
# between strings and all the enumerated types in the file: these
# functions are placed in foo-enums.c , and have names
# foo_print_Enum and foo_scan_Enum, where Enum is the type name.
# Header definitions are put in foo-enums.h .
# It is assumed that definitions start
# typedef enum {
# and end with
# } Enum ;
#
# If the body of the definition (between the braces) contains
# a single line C comment of the form
# /* make-enums sub { ... } */
# then the defined function will be applied to the C names
# of the type values to convert them to strings. N.B.
# the function should modify its argument in place.
#
$file = $ARGV[0];
open(STDIN,"<$file") || die("opening $file");

$stem = $file;
$stem =~ s/\.h//;

open(STDOUT,">$stem-enums.c") || die("opening stdout");
open(H,">$stem-enums.h") || die("opening stdout");

undef $/;
$text = <STDIN>;

# extract a typedef
while ( $text =~ s/typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;//s ) {
  $tdef = $1; $tname = $2;
  eval "sub modifier { }";
  # is there a modifier?
  if ( $tdef =~ m[/\*\s+make-enums\s*sub (.*\})\s+\*/] ) {
    eval "sub modifier $1";
  }
  # strip comments: see man perlop
  $tdef =~ s {
	      /\*     # Match the opening delimiter.
	      .*?     # Match a minimal number of characters.
	      \*/     # Match the closing delimiter.
	     } []gsx;
  $pfun = "char *${stem}_print_${tname}(const $tname t) {\n";
  $sfun = "$tname ${stem}_scan_${tname}(const char *s) {\n";
  print H "char *${stem}_print_${tname}(const $tname t);
$tname ${stem}_scan_${tname}(const char *s);\n\n";
  
  # eliminate any casts to unsigned 
  $tdef =~ s/\(\s*unsigned\s*\)//g;
  # we never need to use the initial values, so strip them 
  $tdef =~ s/=[^,\}]*//g;
  while ( $tdef =~ s/(\w+)\s*// ) {
    $value = $1; 
    $name = $value;
    &modifier($name);
    $pfun .= "  if ( t == $value ) return \"$name\";\n";
    $sfun .= "  if ( strcmp(s,\"$name\") == 0 ) return $value;\n";
  }
  $pfun .= "  return (char *)0;\n}\n";
  $sfun .= "  return -1;\n}\n";
  print $pfun;
  print "\n";
  print $sfun;
  print "\n\n";
}

