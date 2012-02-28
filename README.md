This is the README file for the Unix mah-jong programs by
[J. C. Bradfield](http://mahjong.julianbradfield.org/).

NOTICES:
--------

Please see the file LICENCE for terms and conditions of these
programs.

### Source distribution only:

The code in the  tiles-v1/  directory was not written by me; see 
tiles-v1/README  for information and licensing.
The file MANIFEST contains a description of all the files you 
should find in this distribution.

### Binary distributions only:

In the distribution directory, you will find the following files:

 * __mj-server__:        the controller program. ( __mj-server.exe__ in Windows )
 * __mj-player__:        a computer player.      ( __mj-player.exe__ in Windows )
 * __xmj__:              a graphical client.     ( __xmj.exe__ in Windows )
 * __README__:           this file
 * __LICENCE__:          the licence
 * __rules.txt__:        the rules as implemented by the programs
 * __use.txt__:          documentation on how to use the programs
 * __CHANGES__:          summary of changes between releases
 * __xmj.1__:            man page (not in Windows)
 * __mj-player.1__:      man page (not in Windows)
 * __mj-server.1__:      man page (not in Windows)
 * __tiles-numbered__:   an alternative tileset with some arabic numbers
 * __tiles-small__:      a set of smaller (3/4 size) tiles
 * __gtkrc-minimal__:    sample gtkrc files
 * __gtkrc-plain__
 * __gtkrc-clearlooks__

DESCRIPTION:
------------

These programs allow the user of a Unix (or Windows) computer to play
Mah-Jong, in the Chinese Classical style, against other users (locally
or over the Internet), against programmed players, or both.


OBTAINING:
----------

Latest releases should be available from:
 http://mahjong.julianbradfield.org/MahJong/


CONTACTING:
-----------

If you need to contact me about these programs, please mail me at
mahjong@stevens-bradfield.com .


INSTALLATION - BUILDING FROM SOURCE:
------------------------------------

### Systems:

These programs should, in theory, compile on any reasonably modern
32-bit Unix with an ANSI C compiler and the X Window System.
They have been known to compile (and run) under GNU/Linux, recent Solaris
(on Sparc), and recent Irix (on Silicon Graphics workstations).
They also compile and run on MacOS 10 with X Windows.
If you try to compile on any "reasonably modern" system, and have a
problem, do please tell me.
In addition, they should compile and run under Win32 systems with
GNU compilers and utilities; they are known to work under NT 4.0 with
the mingw compiler (you must use the version with the MSVCRT dll).
[Adam Kao](mahjong@ideogram.com) got a pre-release version to compile
with Microsoft Visual C and nmake, but as I don't have these I don't
support them. If you wish to try to do this, the file __makefile.msvc.old__
is the makefile Adam used; however, it has not been updated to match the
current release, and is now extremely old.

### Prerequisites:

On Unix, as of version 1.10 of xmj, you need GTK+ 2.0 or later,
preferably with the Clearlooks theme installed. If you are running on
a very old system indeed, or you dislike GTK+2, you can also compile
against GTK+ 1.2 by making the appropriate change in the __Makefile__.
GTK+ 1.2 specific bugs are unlikely to be fixed in future.

If you do not have GTK+, obtain it from www.gtk.org or mirrors.

On Windows, you need the GTK+ 2 development bundle (note that
GTK+ 2 relies on a lot of other stuff. A bundle is available from
www.gtk.org.)  To compile against GTK+1, you need a 2004 or later
version 1.3 of the gtk+ Windows port.

To compile the program, you need Perl, version 5 (version 4 is almost
certainly OK, but I haven't checked). Perl should be installed on any
decent system. (It is, of course not installed by default on Windows, but
if you do any development, you've probably already installed it.)
You do not need any Perl modules.

You need the GNU make utility. If you do not have this, you will need
to remove the GNU-specific features from the __Makefile__, as directed therein.
(This may be hard. There is no good reason not to use GNU make, as far
as I know.)
(Windows: see also note above about __makefile.msvc.old__.)

### Installing:

Unpack the distribution, and cd into the distribution directory.

Edit the Makefile, and change as directed by the comments.

Then
    make

You should not see any warnings or errors. If you do get any warning
or error, please complain to me (unless you've switched on lots of
superfluous message via the C debug flags, in which case you know what 
you're doing, so don't complain to me!).

Then (Unix only) `su` to an appropriate user if you need this for
installation, and 
    make install
will install the binaries in the place you chose. Alternatively,
just copy the three binaries `mj-server`, `mj-player`, `xmj`
into your chosen directory.

    make install.man  # (Unix only)
will install the man pages into the appropriate directory.

You may also wish to put the pre-formatted text files __use.txt__
and __rules.txt__ (which are basically the two halves of the man page)
somewhere.


INSTALLATION - UNIX BINARY DISTRIBUTIONS:
-----------------------------------------

Unpack the distribution, and cd into the distribution directory.

Just copy the three binaries `mj-server`, `mj-player`, `xmj`
into your chosen directory. If you don't have GTK+, then see above.

The user documentation is __use.txt__ and __rules.txt__; put these in
an appropriate place.

Alternatively, or as well, copy the man pages `xmj.1`, `mj-server.1`,
`mj-player.1` to the appropriate man directory (e.g. `/usr/local/man/man1`).

Make sure that the binary directory is in your PATH; in particular,
if you run from the source directory, you need to have it in your 
PATH either explicitly or as `.` .


INSTALLATION - WINDOWS BINARY DISTRIBUTION:
-------------------------------------------

Unpack the distribution. 

Put the three `*.exe` files into an appropriate directory.

Creating menu items and shortcuts is up to you...

You must have the DLLs for the GTK+ libraries, as specified above.
For your convenience, these can be found in the file __gtk2dlls.zip__
in the xmj download directory. Put the DLLs in the same directory as
the xmj binaries.
If you are using the GTK+ 1 version, you need instead the file
__gtkdlls1.zip__.

Put the documentation __use.txt__ and __rules.txt__ somewhere sensible.


RUNNING:
--------

Please see the file __use.txt__ in the distribution for usage
information. The file __rules.txt__ describes the rules applied by
the programs. (Or see the man page.)


PAYMENT & REGISTRATION:
-----------------------

What payment? What registration? These programs are free. However, if
you would like to encourage me to continue development, or thank me
for what I have already done, you can

 + make a donation to your local branch of Amnesty International
   (please send me a note if you do); or,

 + send me a donation, either via PayPal, or by credit card by
   the link on the xmj home page.

If you want to be informed when new versions are released, etc.,
please sign up via the web site.


WINDOWS:
--------

The Windows port exists thanks to the efforts of
[Adam Kao](mahjong@ideogram.com), who ported the
pre-release version, and thus showed me what to do.

Only Win32 is catered for.

I do not have a proper development environment for
Windows, so Windows-specific bugs will not have a high priority.


SUGGESTIONS:
------------

xmj is ***no longer under development***. You're welcome to send me suggestions
for improvements, but I'm unlikely to act on them. If I do do any further
work, it will be to develop a new version, with much more flexibility.

BUGS:
-----
Please report any bugs, or anything that might be a bug.
Any known bugs that cannot be fixed are documented in the user manual.

