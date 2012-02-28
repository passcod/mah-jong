/* $Header: /home/jcb/MahJong/newmj/RCS/gui.c,v 12.11 2012/02/02 08:25:12 jcb Rel $
 * gui.c
 * A graphical interface, using gtk.
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


#include "gui.h"
#ifndef WIN32
#ifndef GTK2
/* this is a bit sordid. This is needed for iconifying windows */
#include <gdk/gdkx.h>
#endif
#endif

/* for a debugging hack */
#ifndef WIN32
#include <signal.h>
#endif

/* string constants for gtk setup */
#include "gtkrc.h"


/* local forward declarations */
static AnimInfo *adjust_wall_loose(int tile_drawn);
static gint check_claim_window(gpointer data);
static void create_wall(void);
static void clear_wall(void);
static int get_relative_posn(GtkWidget *w, GtkWidget *z, int *x, int *y);
static AnimInfo *playerdisp_discard(PlayerDisp *pd,Tile t);
static void playerdisp_init(PlayerDisp *pd, int ori);
static void playerdisp_update(PlayerDisp *pd, CMsgUnion *m);
static int playerdisp_update_concealed(PlayerDisp *pd,Tile t);
static void playerdisp_clear_discards(PlayerDisp *pd);
static void iconify_window(GtkWidget *w);
static void uniconify_window(GtkWidget *w);
static int read_rcfile(char *rcfile);
static void rotate_pixmap(GdkPixmap *east, GdkPixmap **south,
			  GdkPixmap **west, GdkPixmap **north);
static void change_tile_selection(GtkWidget *w,gpointer data);
static void move_selected_tile(GtkWidget *w,gpointer data);
static void server_input_callback(gpointer data, gint source,
				  GdkInputCondition condition);
static void start_animation(AnimInfo *source,AnimInfo *target);
static void stdin_input_callback(gpointer data, gint source, 
				 GdkInputCondition condition);
/* currently used only in this file */
void button_set_tile_without_tiletip(GtkWidget *b, Tile t, int ori);


/* This global variable contains XPM data for fallback tiles.
   It is defined in fbtiles.c */
extern char **fallbackpixmaps[];

/*********************
  GLOBAL VARIABLES 
*********************/

/* The search path for tile sets */
/* Confusing terminology: this is nothing to do with tilesets
   as in pungs. */
#ifndef TILESETPATH
#define TILESETPATH "."
#endif
/* And the name of the tile set */
#ifndef TILESET
#define TILESET NULL
#endif
/* separator character in search paths */
#ifdef WIN32
#define PATHSEP ';'
#else
#define PATHSEP ':'
#endif
/* the tileset search path given on the command line or via
   options. */
char *tileset_path = TILESETPATH;
/* ditto for the tile set */
char *tileset = TILESET;

static char *tilepixmapdir; /* where to find tiles */
int debug = 0;
int server_pversion;
PlayerP our_player;
int our_id;
seats our_seat; /* our seat in the game */
/* convert from a game seat to a position on our display board */
#define game_to_board(s) ((s+NUM_SEATS-our_seat)%NUM_SEATS)
/* and conversely */
#define board_to_game(s) ((s+our_seat)%NUM_SEATS)
/* convert from game wall position to board wall position */
#define wall_game_to_board(i) ((i + wallstart) % WALL_SIZE)
/* and the other way */
#define wall_board_to_game(i) ((i + WALL_SIZE - wallstart)%WALL_SIZE)
Game *the_game;
int selected_button; /* the index of the user's selected
			     tile, or -1 if none */

int monitor = 0; /* are we playing normally, or instrumenting
		    somebody else? */

int game_over = 0;

static int do_connect = 0; /* --connect given? */

/* tag for the server input callback */
static gint server_callback_tag = 0;
static gint stdin_callback_tag = 0; /* and stdin */

/* This is a list of windows (i.e. GTK windows) that should be iconified along
   with the main window. It's null-terminated, for simplicity.
   The second element of each struct records whether the window
   is visible at the time the main window is iconified.
*/

static struct { GtkWidget **window; int visible; } windows_following_main[] = 
  { {&textwindow,0}, {&messagewindow,0}, {&status_window,0},
    /* these will only be relevant when they're standalone */
    {&discard_dialog->widget,0}, {&chow_dialog,0}, {&ds_dialog,0},
    {&continue_dialog,0}, {&turn_dialog,0},
    {&scoring_dialog,0}, 
    /* these should always be relevant */
    {&open_dialog,0}, {&nag_window,0},
    {&display_option_dialog,0}, {&game_option_dialog,0}, 
    {&game_prefs_dialog,0}, {&playing_prefs_dialog,0},
    {NULL,0} };

/* callback attached to [un]map_event */
static void vis_callback(GtkWidget *w UNUSED, GdkEvent *ev, gpointer data UNUSED)
{
  if ( ev->type == GDK_MAP ) {
    int i;

    /* map previously open subwindows */
    if ( iconify_dialogs_with_main ) {
      /* unfortunately, if we have a wm that raises on map, but waits
	 for the map before raising, we will find that the main window
	 is raised over the subwindows, which is probably not intended.
	 So we sleep a little to try to wait for it. */
      usleep(50000);
      for ( i = 0; windows_following_main[i].window ; i++ ) {
	GtkWidget *ww = *windows_following_main[i].window;
	if ( ww == NULL ) continue;
	if ( windows_following_main[i].visible )
	  uniconify_window(ww);
      }
    }
  } else if ( ev->type == GDK_UNMAP ) {
    int i = 0;

    /* unmap any auxiliary windows that are open, and keep state */
    if ( iconify_dialogs_with_main ) {
      for ( i = 0; windows_following_main[i].window ; i++ ) {
	GtkWidget *ww = *windows_following_main[i].window;
	if ( ww == NULL ) continue;
	if ( 
#ifdef GTK2
	  ! (gdk_window_get_state(gtk_widget_get_window(ww)) && GDK_WINDOW_STATE_ICONIFIED) 
#else
	    // this doesn't really work, but it's the best we can do in gtk1
	    GTK_WIDGET_VISIBLE(ww)
#endif
	  ) {
	  iconify_window(ww);
	  windows_following_main[i].visible = 1;
	} else {
	  windows_following_main[i].visible = 0;
	}
      }
    }
  } else {
    warn("vis_callback called on unexpected event");
  }
}

/* These are used as time stamps. now_time() is the time since program
   start measured in milliseconds. start_tv is the start time.
*/

static struct timeval start_tv;
static int now_time(void) {
  struct timeval now_tv;
  gettimeofday(&now_tv,NULL);
  return (now_tv.tv_sec-start_tv.tv_sec)*1000
	  +(now_tv.tv_usec-start_tv.tv_usec)/1000;
}


int ptimeout = 15000; /* claim timeout time in milliseconds [global] */
int local_timeouts = 0; /* should we handle timeouts ourselves? */

#ifdef GTK2
/* gtk2 rc file to use instead of default */
char gtk2_rcfile[512];
/* whether to use system gtkrc */
int use_system_gtkrc = 0;
#endif

/* These are the height and width of a tile (including
   the button border. The spacing is currently
   1/4 the width of an actual tile pixmap. */
static int tile_height = 0;
static int tile_width = 0;
static int tile_spacing = 0;

/* variable concerned with the display of the wall */
int showwall = -1;
/* preference value for this */
int pref_showwall = -1; 

/* do the message and game info windows get wrapped into the main ? */
int info_windows_in_main = 0;

/* preference values for robot names */
char robot_names[3][128];
/* and robot options */
char robot_options[3][128];

/* address of server */
char address[256] = "localhost:5000";
/* hack: we don't want to save in the rc file the address
   if we've been redirected */
static int redirected = 0;
static char origaddress[256];
/* name of player */
char name[256] = "" ;

/* name of the main system font (empty if use default) */
char main_font_name[256] = "";
/* and the "fixed" font used for text display */
char text_font_name[256] = "";
/* one that we believe to work */
char fallback_text_font_name[256] = "";
/* name of the colour of the table */
char table_colour_name[256] = "";

/* playing options */
int playing_auto_declare_specials = 0;
int playing_auto_declare_losing = 0;
int playing_auto_declare_winning = 0;
/* local variable to note when we are in the middle of an auto-declare */
static int auto_declaring_special = 0;
static int auto_declaring_hand = 0;
/* to override the suppression of declaring specials dialog when
   we have a kong in hand */
static int may_declare_kong = 0;

/* a count of completed games, for nagging purposes */
int completed_games = 0;
int nag_state = 0; /* nag state */
time_t nag_date = 0; /* time of last nag, as a time(). We won't worry
		     about the rollover! */

/* array of buttons for the tiles in the wall.
   button 0 is the top rightmost tile in our wall, and 
   we count clockwise as the tiles are dealt. */
static GtkWidget  *wall[MAX_WALL_SIZE]; 
/* This gives the wall size with a sensible value when there's no
   game or when there's no wall established */
#define WALL_SIZE ((the_game && the_game->wall.size) ? the_game->wall.size : MAX_WALL_SIZE)
static int wallstart; /* where did the live wall start ? */
/* given a tile number in the wall, what orientation shd it have? */
#define wall_ori(n) (n < WALL_SIZE/4 ? 0 : n < 2*WALL_SIZE/4 ? 3 : n < 3*WALL_SIZE/4 ? 2 : 1)


/* Pixmaps for tiles. We need one for each orientation.
   The reason for this slightly sick procedure is so that the
   error tile can also have a pixmap here, with the indexing
   working as expected
*/
static GdkPixmap *_tilepixmaps[4][MaxTile+1];

/* this is global */
GdkPixmap **tilepixmaps[4] = {
  &_tilepixmaps[0][1], &_tilepixmaps[1][1],
  &_tilepixmaps[2][1], &_tilepixmaps[3][1] };

/* pixmaps for tong box, and mask */
static GdkPixmap *tongpixmaps[4][4]; /* [ori,wind-1] */
static GdkBitmap *tongmask;

GtkWidget *topwindow; /* main window */
/* used to remember the position of the top level window when 
   recreating it */
static gint top_x, top_y, top_pos_set;
GtkWidget *menubar; /* menubar */
GtkWidget *info_box; /* box to hold info windows */
GtkWidget *board; /* the table area itself */
GtkWidget *boardfixed; /* fixed widget enclosing boardframe */
GtkWidget *boardframe; /* event box widget wrapping the board */
GtkWidget *outerframe; /* outermost frame */
#ifndef GTK2
GtkStyle *tablestyle; /* for the dark green stuff */
#endif
#ifndef GTK2
GtkStyle *highlightstyle; /* to highlight tiles */
#else
static GdkColor highlightcolor;
#endif
GtkWidget *highlittile = NULL ; /* the unique highlighted tile */
GtkWidget *dialoglowerbox = NULL; /* encloses dialogs when dialogs are below */
GdkFont *main_font;
GdkFont *fixed_font; /* a fixed width font */
GdkFont *big_font; /* big font for claim windows */
static GtkStyle *original_default_style;
#ifndef GTK2
static GtkStyle *defstyle;
#endif
/* option table for our game preferences */
GameOptionTable prefs_table;

/* This gives the width of a player display in units
   of tiles */

int pdispwidth = 0;
int display_size = 0;
int square_aspect = 0; /* force a square table */
int animate = 0; /* do fancy animation */
/* This variable determines whether animated tiles are done by
   moving a tile within the board, which is preferable since it
   means tiles don't mysteriously float above other higher windows;
   or by moving top-level popup windows. The only reason for the
   latter is that it seems to make the animation smoother under Windows.
   This variable is local and read-only now; it may become an option later.
*/
#ifdef WIN32
static int animate_with_popups = 1;
#else
static int animate_with_popups = 0;
#endif

int sort_tiles = SortAlways; /* when to sort tiles in our hand */
int iconify_dialogs_with_main = 1;
int nopopups = 0; /* suppress popups */
int tiletips = 0; /* show tile tips all the time */

DebugReports debug_reports = DebugReportsUnspecified; /* send debugging reports? */

/* record to keep copy of server input to add to debugging reports */
struct databuffer {
  char *data; /* malloced buffer */
  int alloced; /* length of malloced buffer */
  int length; /* length used (including termination null of strings) */
} server_history;

static void databuffer_clear(struct databuffer *b) {
  if ( b->data ) free(b->data);
  b->data = NULL;
  b->alloced = 0;
  b->length = 0;
}

/* extend a databuffer to at least the given length. Returns
   1 on success, 0 on failure */
static int databuffer_extend(struct databuffer *b,int new) {
  char *bnew;
  int anew;
  anew = b->alloced;
  if ( new <= anew ) return 1;
  if ( anew == 0 ) anew = 1024;
  while ( anew < new ) anew *= 2;
  bnew = realloc(b->data,anew);
  if ( bnew == NULL ) {
    warn("databuffer_extend: out of memory");
    return 0;
  }
  b->data = bnew;
  b->alloced = anew;
  return 1;
}

/* returns 1 on success, 0 on failure.
   Assuming the databuffer is a null-terminated string (which is not
   checked), extend it by the argument string */
static int databuffer_add_string(struct databuffer *b, char *new) {
  int l;
  l = strlen(new);
  if ( ! databuffer_extend(b,b->length+l) ) return 0;
  if ( b->length == 0 ) {
    b->data[0] = 0;
    b->length = 1;
  }
  strcpy(b->data + b->length - 1, new);
  b->length += l;
  return 1;
}



/* the player display areas */
PlayerDisp pdisps[NUM_SEATS];

int calling = 0; /* disgusting global flag */

/* the widget in which we put discards */
GtkWidget *discard_area;
/* This is an allocation structure in which we
   note its allocated size, at some point when we
   know the allocation is valid.
*/
GtkAllocation discard_area_alloc;

/* This is used to keep a history of the discard actions in
   the current hand, so we can reposition the discards if we
   change size etc. in mid hand */
struct {
  int count;
  struct { PlayerDisp *pd ; Tile t ; } hist[2*MAX_WALL_SIZE];
} discard_history;

GtkWidget *just_doubleclicked = NULL; /* yech yech yech. See doubleclicked */

/* space round edge of dialog boxes */
const gint dialog_border_width = 10;
/* horiz space between buttons */
const gint dialog_button_spacing = 10;
/* vert space between text and buttons etc */
const gint dialog_vert_spacing = 10;

/* space around player boxes. N.B. this should only
   apply to the outermost boxes */
const gint player_border_width = 10;

DialogPosition dialogs_position = DialogsUnspecified;


int pass_stdin = 0; /* 1 if commands are to read from stdin and
			      passed to the server. */

void usage(char *pname,char *msg) {
  fprintf(stderr,"%s: %s\nUsage: %s [ --id N ]\n\
  [ --server ADDR ]\n\
  [ --name NAME ]\n\
  [ --connect ]\n\
  [ --[no-]show-wall ]\n\
  [ --size N ]\n\
  [ --animate ]\n\
  [ --tile-pixmap-dir DIR ]\n\
  [ --dialogs-popup | --dialogs-below | --dialogs-central ]\n"
#ifdef GTK2
"  [ --gtk2-rcfile FILE ]\n"
#endif
"  [ --monitor ]\n\
  [ --echo-server ]\n\
  [ --pass-stdin ]\n",
	  pname,msg,pname);
}

#ifdef WIN32
/* In Windows, we can't use sockets as they come, but have to convert
   them to glib channels. The following functions are passed to the
   sysdep.c initialize_sockets routine to do this. */

static void *skt_open_transform(SOCKET s) {
  GIOChannel *chan;

  chan = g_io_channel_unix_new(s); /* don't know what do if this fails */
  if ( ! chan ) {
    warn("Couldn't convert socket to channel");
    return NULL;
  }
  return (void *)chan;
}

static int skt_closer(void *chan) {
  g_io_channel_close((GIOChannel *)chan);
  g_io_channel_unref((GIOChannel *)chan); /* unref the ref at creation */
  return 1;
}

static int skt_reader(void *chan,void *buf, size_t n) {
  int len = 0; int e;
  while ( 1 ) {
    e = g_io_channel_read((GIOChannel *)chan,buf,n,&len); 
    if ( e == G_IO_ERROR_NONE ) 
    /* fprintf(stderr,"g_io_channel_read returned %d bytes\r\n",len); */
      return len;
    if ( e != G_IO_ERROR_AGAIN )
      return -1;
  }
}

static int skt_writer(void *chan, const void *buf, size_t n) {
  int len = 0;
  if ( g_io_channel_write((GIOChannel *)chan, (void *)buf,n,&len) 
       == G_IO_ERROR_NONE ) 
    return len;
  else 
    return -1;
}

/* a glib level wrapper for the server input callback */

static gboolean gcallback(GIOChannel   *source UNUSED,
			     GIOCondition  condition UNUSED,
			    gpointer      data UNUSED) {
  server_input_callback(NULL,0,GDK_INPUT_READ);
  return 1;
}



#endif /* WIN32 */

/* this is a little hack to make it easy to provoke errors for testing */
#ifndef WIN32
static int signalled = 0;

static void sighandler(int n UNUSED) {
  signalled = 1;
}
#endif

/* used to keep the last line received from controller */
static char last_line[1024];

static char *log_msg_dump_state(LogLevel l) {
  static struct databuffer r;
  if ( l > LogWarning ) {
    int bytes_left;
    static char head[] = "Last cmsg: ";
    static char head1[] = "Server history:\n";
    char *ret;
    ret = game_print_state(the_game,&bytes_left);
    databuffer_clear(&r);
    if ( ! databuffer_extend(&r,strlen(head) 
	+ strlen(last_line) + 2 + strlen(ret)
	+ strlen(head1) + server_history.length + 1) ) {
      warn("Unable to build fault report");
      return NULL;
    }
    databuffer_add_string(&r,head);
    databuffer_add_string(&r,last_line);
    databuffer_add_string(&r,"\n\n");
    databuffer_add_string(&r,ret);
    databuffer_add_string(&r,head1);
    databuffer_add_string(&r,server_history.data);
    return r.data;
  } else {
    return NULL;
  }
}

int
main (int argc, char *argv[])
{
  int i;
  
  /* install the warning hook for gui users */
  log_msg_hook = log_msg_add;

  /* add a dump of the game state to errors */
  log_msg_add_note_hook = log_msg_dump_state;

  /* we have to set up the default preferences table before
     reading the rcfile */
  game_copy_option_table(&prefs_table,&game_default_optiontable);

  /* read rc file. We should have an option to suppress this */
  read_rcfile(NULL);
  showwall = pref_showwall;

  /* options. I should use getopt ... */
  for (i=1;i<argc;i++) {
    if ( strcmp(argv[i],"--id") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --id");
      our_id = atoi(argv[i]);
    } else if ( strcmp(argv[i],"--server") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --server");
      strmcpy(address,argv[i],255);
    } else if ( strcmp(argv[i],"--name") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --name");
      strmcpy(name,argv[i],255);
    } else if ( strcmp(argv[i],"--connect") == 0 ) {
      do_connect = 1;
    } else if ( strcmp(argv[i],"--show-wall") == 0 ) {
      pref_showwall = showwall = 1;
    } else if ( strcmp(argv[i],"--no-show-wall") == 0 ) {
      pref_showwall = showwall = 0;
    } else if ( strcmp(argv[i],"--size") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --size");
      display_size = atoi(argv[i]);
    } else if ( strcmp(argv[i],"--animate") == 0 ) {
      animate = 1;
    } else if ( strcmp(argv[i],"--no-animate") == 0 ) {
      animate = 0;
    } else if ( strcmp(argv[i],"--tileset") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --tileset");
      tileset = argv[i];
    } else if ( strcmp(argv[i],"--tileset-path") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --tileset-path");
      tileset_path = argv[i];
#ifdef GTK2
    } else if ( strcmp(argv[i],"--use-system-gtkrc") == 0 ) {
      use_system_gtkrc = 1;
    } else if ( strcmp(argv[i],"--no-use-system-gtkrc") == 0 ) {
      use_system_gtkrc = 0;
    } else if ( strcmp(argv[i],"--gtk2-rcfile") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --gtk2-rcfile");
      strmcpy(gtk2_rcfile,argv[i],512);
#endif
    } else if ( strcmp(argv[i],"--echo-server") == 0 ) {
      debug = 1;
    } else if ( strcmp(argv[i],"--monitor") == 0 ) {
      monitor = 1;
    } else if ( strcmp(argv[i],"--dialogs-popup") == 0 ) {
      dialogs_position = DialogsPopup;
    } else if ( strcmp(argv[i],"--dialogs-below") == 0 ) {
      dialogs_position = DialogsBelow;
    } else if ( strcmp(argv[i],"--dialogs-central") == 0 ) {
      dialogs_position = DialogsCentral;
    } else if ( strcmp(argv[i],"--pass-stdin") == 0 ) {
      pass_stdin = 1;
    } else {
      usage(argv[0],"unknown option or argument");
      exit(1);
    }
  }

#ifdef GTK2
  /* The Gtk rc mechanism is totally insane. Contrary to the documentation,
     the rc files are not parsed at the end of gtk_init, but on demand when
     the first style is created. Therefore, we can't parse a string here that
     relies on things in the rc files. But we can't add a string to the rc
     file list, we can only parse it (which also adds it to the rc file list!). 
     So we have to force the parsing of rc files by making a style. */

#ifdef WIN32
  /* make sure we pick up modules in the distribution directory */
  putenv("GTK_PATH=.;");
#endif
  gchar *rc_files = { NULL };
  if ( ! use_system_gtkrc ) gtk_rc_set_default_files(&rc_files);
  gtk_init (&argc, &argv);
  if (gtk2_rcfile[0]) {
    gtk_rc_parse_string(gtkrc_minimal_styles);
    if ( access(gtk2_rcfile,R_OK) == 0 )
      gtk_rc_add_default_file(gtk2_rcfile);
    else
      warn("Can't read gtk2-rcfile %s - display will be broken",gtk2_rcfile);
  } else if ( ! use_system_gtkrc ) {
    // do we have clearlooks? This seems to be the only way to find out.
    if ( gtk_rc_find_module_in_path(
	   "libclearlooks."
#ifdef WIN32
	     "dll"
#else
	     "so"
#endif
	   ) ) {
#ifdef WIN32
      /* Installing clearlooks properly is far too difficult. So we
	 just ship the theme gtkrc with the libraries and load it
	 explicitly */
      /* FIXME - we don't do this, because I haven't yet managed to
	 build a clearlooks library for Windows. */
      gtk_rc_parse("cgtkrc");
#else
      gtk_rc_parse_string("gtk-theme-name = \"Clearlooks\"");
#endif
      gtk_widget_get_default_style(); /* force style to be calculated */
      gtk_rc_parse_string(gtkrc_clearlooks_styles);
    } else {
      gtk_rc_parse_string(gtkrc_plain_styles);
    }
    gtk_rc_parse_string(gtkrc_apply_styles);
  }
#else
  gtk_init (&argc, &argv);
#endif
  
#ifdef WIN32
  initialize_sockets(skt_open_transform,skt_closer,
		     skt_reader,skt_writer);
#endif

  /* stash the unmodified default style, so we know what the
     default font is */
  original_default_style = gtk_style_copy(gtk_widget_get_default_style());
  /* Now we've done basic setup, create all the windows.
     This also involves reading tile pixmaps etc */

  /* These two signals are bound to left and right arrow accelerators
     in order to move the tile selection left or right */
#ifdef GTK2
  gtk_signal_new("selectleft",GTK_RUN_ACTION,GTK_TYPE_WIDGET,0,
    gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
  gtk_signal_new("selectright",GTK_RUN_ACTION,GTK_TYPE_WIDGET,0,
    gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
#else
  gtk_object_class_user_signal_new(
    GTK_OBJECT_CLASS(gtk_type_class(GTK_TYPE_WIDGET)), 
      "selectleft", GTK_RUN_FIRST, gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
  gtk_object_class_user_signal_new(
    GTK_OBJECT_CLASS(gtk_type_class(GTK_TYPE_WIDGET)), 
      "selectright", GTK_RUN_FIRST, gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
#endif
  /* These two signals are bound to shift left and right arrow accelerators
     in order to move the selected tile left or right */
#ifdef GTK2
  gtk_signal_new("moveleft",GTK_RUN_ACTION,GTK_TYPE_WIDGET,0,
    gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
  gtk_signal_new("moveright",GTK_RUN_ACTION,GTK_TYPE_WIDGET,0,
    gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
#else
  gtk_object_class_user_signal_new(
    GTK_OBJECT_CLASS(gtk_type_class(GTK_TYPE_WIDGET)), 
      "moveleft", GTK_RUN_FIRST, gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
  gtk_object_class_user_signal_new(
    GTK_OBJECT_CLASS(gtk_type_class(GTK_TYPE_WIDGET)), 
      "moveright", GTK_RUN_FIRST, gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
#endif
  create_display();

#if 0  /* not wanted at present, but left here so we remember how to do it */
  /* we create a new popup signal which we will send to dialogs when
     they're popped up and we want them to set focus to the appropriate
     button */
  gtk_object_class_user_signal_new(GTK_OBJECT_CLASS(gtk_type_class(gtk_type_from_name("GtkWidget"))), "popup", GTK_RUN_FIRST, gtk_signal_default_marshaller, GTK_TYPE_NONE,0);
#endif

  /* now check the dialog state */
  setup_dialogs();

  if ( do_connect ) open_connection(0,0,0,NULL);

  /* initialize time stamps */
  gettimeofday(&start_tv,NULL);

# ifndef WIN32
  signal(SIGUSR1,sighandler);
# endif

  /* if this is the first time this version with debug reports
     has been run, popup the window to set options */
  if ( debug_reports == DebugReportsUnspecified ) {
    debug_options_dialog_popup();
  }

  gtk_main ();
  
  return 0;
}


static int querykong = 0; /* used during kong robbing query */


static void server_input_callback(gpointer data UNUSED,
                            gint              sourceparam UNUSED, 
			   GdkInputCondition conditionparam UNUSED) {
  char *l;
  CMsgUnion *m;
  int id;
  int i;

  /* hack for testing error routines */
# ifndef WIN32
  if ( signalled ) {
    signalled = 0;
    error("We got a (Unix) signal!");
  }
# endif

  l = get_line(the_game->fd);
  strmcpy(last_line,(l ? l : "NULL LINE\n"),sizeof(last_line));
  databuffer_add_string(&server_history,l ? l : "NULL LINE\n");
  if ( debug ) {
    put_line(1,l);
  }
  if ( ! l ) {
    if ( game_over ) {
      /* not an error - just stop listening */
      control_server_processing(0);
    } else {
      fprintf(stderr,"receive error from controller\n");
      error_dialog_popup("Lost connection to server");
      close_connection(0);
    }
    return;
  }

  m = (CMsgUnion *)decode_cmsg(l);

  if ( ! m ) {
    warn("protocol error from controller: %s\n",l);
    return;
  }

  if ( m->type == CMsgConnectReply ) {
    our_id = ((CMsgConnectReplyMsg *)m)->id;
    /* need to set this again if this is a second connectreply
       in response to loadstate */
    our_player = the_game->players[0];
    if ( our_id == 0 ) {
      char s[1024];
      char refmsg[] = "Server refused connection because: ";
      close_connection(0);
      strcpy(s,refmsg);
      strmcat(s,((CMsgConnectReplyMsg *)m)->reason,sizeof(s)-sizeof(refmsg));
      error_dialog_popup(s);
      return;
    } else {
      PMsgSetPlayerOptionMsg spom;
      /* stash the server's protocol version */
      server_pversion = m->connectreply.pvers;
      /* set our name again, in case we got deleted! */
      set_player_name(our_player,name);
      if ( !monitor ) {
	/* since we have a human driving us, ask the controller to slow down
	   a bit; especially if we're animating */
	spom.type = PMsgSetPlayerOption;
	spom.option = PODelayTime;
	spom.ack = 0;
	spom.value = animate ? 10 : 5; /* 0.5 or 1 second min delay */
	spom.text = NULL;
	send_packet(&spom);
	/* if the controller is likely to understand it,
	   request that we may handle timeouts locally */
	if ( m->connectreply.pvers >= 1036 ) {
	  spom.option = POLocalTimeouts;
	  spom.value = 1;
	  send_packet(&spom);
	}
      }
    }
  }

  if ( m->type == CMsgReconnect ) {
    int fd = the_game->fd;
    close_connection(1);
    open_connection(0,(gpointer)3,fd,NULL);
    return;
  }

  if ( m->type == CMsgRedirect ) {
    char buf[256];
    int id = our_id;
    close_connection(0);
    our_id = id;
    open_connection(0,(gpointer)4,0,((CMsgRedirectMsg *)m)->dest);
    sprintf(buf,"Server has redirected us to %.230s",address);
    info_dialog_popup(buf);
    return;
  }

  if ( m->type == CMsgAuthReqd ) {
    password_showraise();
    return;
  }

  /* skip InfoTiles */
  if ( m->type == CMsgInfoTiles ) return;

  /* error messages */
  if ( m->type == CMsgError ) {
    error_dialog_popup(m->error.error);
    /* better set up dialogs here */
    setup_dialogs();
    return;
  }    

  /* player options */
  if ( m->type == CMsgPlayerOptionSet ) {
    /* the only player option that is significant for us
       is LocalTimeouts */
    if ( m->playeroptionset.option == POLocalTimeouts ) {
      local_timeouts = m->playeroptionset.value;
      if ( monitor ) return;
      if ( ! m->playeroptionset.ack ) {
	/* this was a command from the server -- acknowledge it */
	PMsgSetPlayerOptionMsg spom;
	spom.type = PMsgSetPlayerOption;
	spom.option = POLocalTimeouts;
	spom.value = local_timeouts;
	spom.text = NULL;
	spom.ack = 1;
	send_packet(&spom);
      }
    }
    /* any other option we just ignore, since we have no corresponding
       state */
    return;
  }

  id = game_handle_cmsg(the_game,(CMsgMsg *)m);
  /* it should not, I think be possible to get an error here. But
     if we do, let's report it and handle it */
  if ( id < 0 ) {
    error("game_handle_cmsg returned %d; ignoring it",id);
    return;
  }

  calling = 0; /* awful global flag to detect calling */
  /* This is the place where we take intelligent action in
     response to the game. As a matter of principle, we don't do this, 
     but there is an exception: if there is a kong to be robbed,
     we will check to see whether we can actually go out with the tile,
     and if we can't, we'll send in NoClaim directly, rather than
     asking the user */

  /* We can't assume that we know whether we can go mahjong.
     The server might allow special winning hands that we don't know
     about. So we have to ask the server whether we can go MJ.
  */

  /* we might get the kong message as part of the replay. So we have
     to respond the first time we get a message after a kong and
     the game is active */

  if ( !monitor 
    && the_game->active && id != our_id && ! querykong
    && (the_game->state == Discarding 
      || the_game->state == DeclaringSpecials)
    && the_game->konging 
    && the_game->claims[our_seat] == UnknownClaim ) {
    PMsgQueryMahJongMsg qmjm;
    qmjm.type = PMsgQueryMahJong;
    qmjm.tile = the_game->tile;
    send_packet(&qmjm);
    querykong = 1;
  }
  /* and if this is the reply, send no claim if appropriate */
  if ( !monitor
    && the_game->active && (the_game->state == Discarding 
      || the_game->state == DeclaringSpecials)
    && the_game->konging 
    && m->type == CMsgCanMahJong
    && ! m->canmahjong.answer) {
    disc_callback(NULL,(gpointer) NoClaim); /* sends the message */
    the_game->claims[our_seat] = NoClaim; /* hack to stop the window coming up */
  }
  if ( m->type == CMsgCanMahJong ) querykong = 0;

  status_update(m->type == CMsgGameOver);

  if ( id == our_id ) {
    /* do we want to sort the tiles? */
    if ( sort_tiles == SortAlways
      || (sort_tiles == SortDeal
	&& (the_game->state == Dealing || the_game->state == DeclaringSpecials )))
      player_sort_tiles(our_player);
  }

  /* some more "intelligent action" comes now */

  /* if we should be auto-declaring specials, deal with it */
  if ( !monitor && playing_auto_declare_specials ) {
    if ( m->type == CMsgPlayerDeclaresSpecial 
      && id == our_id ) {
      auto_declaring_special = 0;
      may_declare_kong = 0;
    }
    if ( the_game->active
      && ! the_game->paused
      && ( the_game->state == DeclaringSpecials
	|| the_game->state == Discarding )
      && the_game->needs == FromNone
      && the_game->player == our_seat
      && !auto_declaring_special ) {
      PMsgDeclareSpecialMsg pdsm;
      pdsm.type = PMsgDeclareSpecial;
      /* if we have a special, declare it. Now that we allow non-sorting,
	 we have to look for specials */
      /* if we're declaring specials, but have none, send the finished
	 message */
      assert(our_player->num_concealed > 0);
      int j;
      for (j=0;j<our_player->num_concealed && !is_special(our_player->concealed[j]);j++);
      if ( j < our_player->num_concealed ) {
	pdsm.tile = our_player->concealed[j];
	send_packet(&pdsm);
	auto_declaring_special = 1;
      } else if ( the_game->state == DeclaringSpecials ) {
	/* First, we have to check whether we have a kong in hand,
	   since then the human must have the option to declare it */
	int i;
	for ( i = 0; i < our_player->num_concealed; i++ ) {
	  if ( player_can_declare_closed_kong(our_player,
		 our_player->concealed[i]) ) {
	    may_declare_kong = 1;
	    break;
	  }
	}
	if ( ! may_declare_kong ) {
	  pdsm.tile = HiddenTile;
	  send_packet(&pdsm);
	  auto_declaring_special = 1;
	} else {
	  /* select the kong we found */
	  selected_button = i;
	  conc_callback(pdisps[0].conc[i],(gpointer)(intptr_t)(i | 128));
	}
      }
    }
  }

  /* if we should be auto-declaring hands, deal with it */
  if ( !monitor && (playing_auto_declare_losing || playing_auto_declare_winning) ) {
    /* this is mind-numbingly tedious. There has to be a better way. */
    if ( (m->type == CMsgPlayerPungs
	   || m->type == CMsgPlayerFormsClosedPung
	   || m->type == CMsgPlayerKongs
	   || m->type == CMsgPlayerDeclaresClosedKong
	   || m->type == CMsgPlayerChows
	   || m->type == CMsgPlayerFormsClosedChow
	   || m->type == CMsgPlayerPairs
	   || m->type == CMsgPlayerFormsClosedPair
	   || m->type == CMsgPlayerSpecialSet
	   || m->type == CMsgPlayerFormsClosedSpecialSet )
      && id == our_id ) auto_declaring_hand = 0;

    /* and to make sure it does get cleared */
    if ( pflag(our_player,HandDeclared) ) auto_declaring_hand = 0;

    if ( !auto_declaring_hand
      && the_game->active
      && ! the_game->paused
      && ( the_game->state == MahJonging )
      && !(the_game->mjpending && the_game->player != our_seat)
      && !pflag(our_player,HandDeclared)
       && ((the_game->player == our_seat && playing_auto_declare_winning)
	   || (pflag(the_game->players[the_game->player],HandDeclared) 
	       && playing_auto_declare_losing) ) ) {
      TileSet *tsp;
      MJSpecialHandFlags mjspecflags;
      PMsgUnion m;

      mjspecflags = 0;
      if ( game_get_option_value(the_game,GOSevenPairs,NULL).optbool )
	mjspecflags |= MJSevenPairs;

      /* OK, let's find a concealed set to declare */
      auto_declaring_hand = 1;
      /* following code nicked from greedy.c */
      /* get the list of possible decls */
      tsp = client_find_sets(our_player,
	(the_game->player == our_seat
	  && the_game->mjpending)
	? the_game->tile : HiddenTile,
	the_game->player == our_seat,
	(PlayerP *)0,mjspecflags);
      if ( !tsp && our_player->num_concealed > 0 ) {
	/* Can't find a set to declare, and still have concealed
	   tiles left. Either we're a loser, or we have Thirteen
	   Orphans (or some other special hand). */
	if ( the_game->player == our_seat ) {
	  if ( the_game->mjpending ) {
	    m.type = PMsgSpecialSet;
	  } else {
	    m.type = PMsgFormClosedSpecialSet;
	  }
	} else {
	  m.type = PMsgShowTiles;
	}
	send_packet(&m);
      } else {
	/* just do the first one */
	switch ( tsp->type ) {
	case Kong:
	  /* we can't declare a kong now, so declare the pung instead */
	case Pung:
	  m.type = PMsgPung;
	  m.pung.discard = 0;
	  break;
	case Chow:
	  m.type = PMsgChow;
	  m.chow.discard = 0;
	  m.chow.cpos = the_game->tile - tsp->tile;
	  break;
	case Pair:
	  m.type = PMsgPair;
	  break;
	case ClosedPung:
	  m.type = PMsgFormClosedPung;
	  m.formclosedpung.tile = tsp->tile;
	  break;
	case ClosedChow:
	  m.type = PMsgFormClosedChow;
	  m.formclosedchow.tile = tsp->tile;
	  break;
	case ClosedPair:
	  m.type = PMsgFormClosedPair;
	  m.formclosedpair.tile = tsp->tile;
	  break;
	case Empty: /* can't happen, just to suppress warning */
	case ClosedKong: /* ditto */
	  ;
	}
	send_packet(&m);
      }
    }
  }

  /* now deal with the display. There are basically two sorts
     of things to do: some triggered by a message, such as
     displaying draws, claims, etc; and some depending only on game state,
     such as making sure the right dialogs are up.
     We'll deal with message-dependent things first.
  */

  switch ( m->type ) {
  case CMsgError:
    /* this has been dealt with above, and cannot happen */
    warn("Error message in impossible place");
    break;
  case CMsgAuthReqd:
    /* this has been dealt with above, and cannot happen */
    warn("AuthReqd message in impossible place");
    break;
  case CMsgRedirect:
    /* this has been dealt with above, and cannot happen */
    warn("Redirect message in impossible place");
    break;
  case CMsgPlayerOptionSet:
    /* this has been dealt with above, and cannot happen */
    warn("PlayerOptionSet message in impossible place");
    break;
  case CMsgStopPlay:
    { char buf[512];
    char suspmsg[] = "Server has suspended play because:\n";
    strcpy(buf,suspmsg);
    strmcat(buf,(m->stopplay.reason),sizeof(buf)-sizeof(suspmsg));
    error_dialog_popup(buf);
    }
    break;
  case CMsgGameOption:
    /* update the timeout value */
    if ( m->gameoption.optentry.option == GOTimeout ) {
      ptimeout = 1000*m->gameoption.optentry.value.optint;
    }
    /* if this is the Flowers option, recreate the declaring
       specials dialog */
    if ( m->gameoption.optentry.option == GOFlowers ) {
      ds_dialog_init();
    }
    /* and refresh the game option panel */
    game_option_panel = build_or_refresh_option_panel(&the_game->option_table,game_option_panel);
    break;
  case CMsgGame:
    /* now we know the seating order: make the userdata field of
       each player point to the appropriate pdisp */
    our_seat = game_id_to_seat(the_game,our_id);
    for ( i = 0; i < NUM_SEATS; i++ ) {
      set_player_userdata(the_game->players[i],
			  (void *)&pdisps[game_to_board(i)]);
      /* and make the pdisp point to the player */
      pdisps[game_to_board(i)].player = the_game->players[i];
    }
    /* zap the game option dialog, because the option table has
       been reset */
    game_option_panel = build_or_refresh_option_panel(NULL,game_option_panel);
    /* ask what the current options are */
    if ( ! monitor ) {
      PMsgListGameOptionsMsg plgom;
      plgom.type = PMsgListGameOptions;
      plgom.include_disabled = 0;
      send_packet(&plgom);
    }
    /* and enable the menu item */
    gtk_widget_set_sensitive(gameoptionsmenuentry,1);
    /* set the info on the scoring window */
    for ( i=0; i < 4; i++ ) {
      gtk_label_set_text(GTK_LABEL(textlabels[i]),
			 the_game->players[board_to_game(i)]->name);
    }
    break;
  case CMsgNewRound:
    /* update seat when it might change */
    our_seat = game_id_to_seat(the_game,our_id);
    break;
  case CMsgNewHand:
    /* update seat when it might change */
    our_seat = game_id_to_seat(the_game,our_id);
    /* After a new hand message, clear the display */
    for ( i=0; i < 4; i++ ) {
      playerdisp_update(&pdisps[i],m);
    }
    discard_history.count = 0;
    /* need to set wallstart even if not showing wall, in case
       we change options */
    wallstart = m->newhand.start_wall*(the_game->wall.size/4) + m->newhand.start_stack*2;
    gextras(the_game)->orig_live_end = the_game->wall.live_end;
    if ( showwall ) {
      create_wall();
      adjust_wall_loose(the_game->wall.dead_end); /* set the loose tiles in place */
    }
    break;
  case CMsgPlayerDiscards:
    /* if calling declaration, set flag */
    if ( m->playerdiscards.calling ) calling = 1;
    /* and then update */
  case CMsgPlayerFormsClosedPung:
  case CMsgPlayerDeclaresClosedKong:
  case CMsgPlayerAddsToPung:
  case CMsgPlayerFormsClosedChow:
  case CMsgPlayerFormsClosedPair:
  case CMsgPlayerShowsTiles:
  case CMsgPlayerFormsClosedSpecialSet:
  case CMsgPlayerDraws:
  case CMsgPlayerDrawsLoose:
  case CMsgPlayerDeclaresSpecial:
  case CMsgPlayerPairs:
  case CMsgPlayerChows:
  case CMsgPlayerPungs:
  case CMsgPlayerKongs:
  case CMsgPlayerSpecialSet:
  case CMsgPlayerClaimsPung:
  case CMsgPlayerClaimsKong:
  case CMsgPlayerClaimsChow:
  case CMsgPlayerClaimsMahJong:
  case CMsgSwapTile:
    {
      PlayerP p;
      p = game_id_to_player(the_game,id);
      if ( p == NULL ) {
	error("gui.c error 1: no player found for id %d\n",id);
      } else {
	if ( p->userdata == NULL ) {
	  error("gui.c error 2: no player disp for player id %d\n",id);
	} else {
	  playerdisp_update((PlayerDisp *)p->userdata,m);
	}
      }
    }
    break;
  case CMsgGameOver:
    /* we don't close the connection, we pop up a box to do it */
    game_over = 1;
    end_dialog_popup();
    status_showraise();
    /* increment and save the game count */
    if ( completed_games >= 0 ) {
      completed_games++;
      read_or_update_rcfile(NULL,XmjrcNone,XmjrcAll);
    }
    if ( nag_state == 0 && completed_games % 10 == 0 && time(NULL) - nag_date >= 30*24*3600) {
      nag_date = time(NULL);
      nag_popup();
    }
    return;
    /* in the following case, we need do nothing (in this switch) */
  case CMsgInfoTiles:
  case CMsgConnectReply:
  case CMsgReconnect:
  case CMsgPlayer:
  case CMsgPause:
  case CMsgPlayerReady:
  case CMsgClaimDenied:
  case CMsgPlayerDoesntClaim:
  case CMsgDangerousDiscard:
  case CMsgCanMahJong:
  case CMsgHandScore:
  case CMsgSettlement:
  case CMsgChangeManager:
  case CMsgWall:
  case CMsgComment:
    break;
  case CMsgStateSaved:
    { static char buf[512];
    char savmsg[] = "Game state saved in ";
    strcpy(buf,savmsg);
    strmcat(buf,m->statesaved.filename,sizeof(buf)-sizeof(savmsg)-2);
    info_dialog_popup(buf);
    break;
    }
  case CMsgStartPlay:
    /* If this is a new game, try to apply our option preferences */
    if ( the_game->state == HandComplete
	 && the_game->round == EastWind
	 && the_game->players[east]->id == the_game->firsteast
	 && the_game->hands_as_east == 0 
	 && (the_game->manager == 0 || the_game->manager == our_id ) )
      apply_game_prefs();
    break;
  case CMsgMessage:
    { 
#ifdef GTK2
    GtkTextBuffer *messagetextbuf;
    GtkTextIter messagetextiter;
    GtkTextMark *messagemark;
    messagetextbuf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (messagetext));
    gtk_text_buffer_get_end_iter(messagetextbuf, &messagetextiter);
    messagemark = gtk_text_buffer_create_mark(messagetextbuf,
      "end",&messagetextiter,FALSE);

    gtk_text_buffer_insert(messagetextbuf,&messagetextiter,
		    "\n",-1);
    gtk_text_buffer_get_end_iter(messagetextbuf, &messagetextiter);
    gtk_text_buffer_insert(messagetextbuf,&messagetextiter,
		    m->message.sender == 0 ? "CONTROLLER" : game_id_to_player(the_game,m->message.sender)->name,-1);
    gtk_text_buffer_get_end_iter(messagetextbuf, &messagetextiter);
    gtk_text_buffer_insert(messagetextbuf,&messagetextiter,
		    "> ",-1);
    gtk_text_buffer_get_end_iter(messagetextbuf, &messagetextiter);
    if ( m->message.text ) {
      gtk_text_buffer_insert(messagetextbuf,&messagetextiter,
	m->message.text,-1);
    }
    gtk_text_buffer_get_end_iter(messagetextbuf, &messagetextiter);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(messagetext),
      messagemark);
    gtk_text_buffer_delete_mark(messagetextbuf,messagemark);
#else
    gtk_text_insert(GTK_TEXT(messagetext),0,0,0,
		    "\n",-1);
    gtk_text_insert(GTK_TEXT(messagetext),0,0,0,
		    m->message.sender == 0 ? "CONTROLLER" : game_id_to_player(the_game,m->message.sender)->name,-1);
    gtk_text_insert(GTK_TEXT(messagetext),0,0,0,
		    "> ",-1);
    if ( m->message.text ) {
      gtk_text_insert(GTK_TEXT(messagetext),0,0,0,
	m->message.text,-1);
    }
#endif
    if ( !nopopups && !info_windows_in_main) {
      showraise(messagewindow);
    }
    }
    break;
  case CMsgPlayerRobsKong:
    /* we need to update the victim */
    playerdisp_update(&pdisps[game_to_board(the_game->supplier)],m);
    /* and now fall through and do the mah-jong stuff, since the
       protocol does not issue a MahJongs in addition */
  case CMsgPlayerMahJongs:
    /* this could be from hand.
       Clear all the texts in the scoring displays.
       Unselect tiles (if we're about to claim the discard,
       it's visually confusing seeing the first tile get selected;
       so instead, we'll select the first tile when we pop up
       the "declare closed sets" dialog. */
    {
      int i;
      if ( the_game->player == our_seat ) 
	conc_callback(NULL,NULL);
      for (i=0;i<5;i++)
#ifdef GTK2
	gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textpages[i]))),"",0);
#else
	gtk_editable_delete_text(GTK_EDITABLE(textpages[i]),0,-1);
#endif
    }
    playerdisp_update((PlayerDisp *)game_id_to_player(the_game,id)->userdata,m);
    break;
  case CMsgWashOut:
    /* display a message in every window of the scoring info */
    {
      CMsgWashOutMsg *wom = (CMsgWashOutMsg *)m;
      int i,npos=0;
      char text[1024] = "This hand is a WASH-OUT:\n";
      text[1023] = 0;
      if ( wom->reason ) {
	strmcat(text,wom->reason,1023-strlen(text));
      }
      for (i=0;i<5;i++) {
	npos=0;
#ifdef GTK2
	GtkTextBuffer *g = GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textpages[i])));
	gtk_text_buffer_set_text(g,"",0);
	gtk_text_buffer_insert_at_cursor(g,
	  text,strlen(text));
#else
	gtk_editable_delete_text(GTK_EDITABLE(textpages[i]),0,-1);
	gtk_editable_insert_text(GTK_EDITABLE(textpages[i]),
	  text,strlen(text),&npos);
#endif
      }
      if (!nopopups) {
	showraise(textwindow);
      }
    }
  }

  /* check the state of all the dialogs */
  /* As a special case, if the message was a Message, we won't
     check the dialog state, since that forces focus to the control
     dialogs. If we send a message from a chat window incorporated
     into the main table, we probably do not expect to lose focus
     when our message arrives */
  if ( m->type != CMsgMessage ) {
    setup_dialogs();
  }

  /* after a HandScore message, stick the text in the display */
  if ( m->type == CMsgHandScore || m->type == CMsgSettlement ) {
    static char bigbuf[8192];
    CMsgHandScoreMsg *hsm = (CMsgHandScoreMsg *)m;
    CMsgSettlementMsg *sm = (CMsgSettlementMsg *)m;
    int pos;
#ifndef GTK2
    int npos;
#endif
    char *text;

    if ( m->type == CMsgHandScore ) {
      pos = (game_id_to_seat(the_game,hsm->id)+NUM_SEATS-our_seat)%NUM_SEATS;
      text = hsm->explanation;
    } else {
      pos = 4;
      text = sm->explanation;
    }

    expand_protocol_text(bigbuf,text,8192);
#ifdef GTK2
    gtk_text_buffer_insert_at_cursor(GTK_TEXT_BUFFER(gtk_text_view_get_buffer(GTK_TEXT_VIEW(textpages[pos]))),
			     bigbuf,strlen(bigbuf));
#else
    gtk_editable_insert_text(GTK_EDITABLE(textpages[pos]),
			     bigbuf,strlen(bigbuf),&npos);
#endif
    if ( m->type == CMsgSettlement ) {
      if ( ! nopopups ) {
	/* Since we have our own player selected in the notebook, when
	   this pop up, it takes its size from the first page. This is
	   usually wrong. So find the size requests of each page, and
	   make page 0 ask for the maximum */
	GtkRequisition r; int i; int w=-1,h=-1;
	GtkAllocation a = { 0, 0, 2000, 2000 };
	gtk_widget_size_allocate(scoring_notebook,&a);
	for (i=0; i < 5; i++) {
	  gtk_widget_size_request(textpages[i],&r);
	  if ( r.width > w && r.width > 0 ) w = r.width;
	  if ( r.height > h && r.height > 0 ) h = r.height;
	}
	if ( h > 0 ) h += 10; /* something not quite right */
#ifdef GTK2	
	gtk_widget_set_size_request(textpages[0],w,h);
	gtk_notebook_set_current_page(GTK_NOTEBOOK(scoring_notebook),0);
#else
	gtk_widget_set_usize(textpages[0],w,h);
	gtk_notebook_set_page(GTK_NOTEBOOK(scoring_notebook),0);
#endif
	gtk_widget_show(textwindow);
	gdk_window_show(textwindow->window); /* uniconify */
      }
    }
  }
}

/* setup the dialogs appropriate to the game state */
void setup_dialogs(void) {
  if ( ! the_game ) {
    gtk_widget_hide(discard_dialog->widget);
    /* that might have been left up */
    return;
  }

  if ( game_over ) {
    end_dialog_popup();
    return;
  }

  /* in the discarded state, the dialog box should be up if somebody
     else discarded and we haven't claimed. If we've claimed,
     it should be down.
     Further, if we have a chowclaim in and the chowpending flag is set,
     the chow dialog should be up. */
  if ( the_game->state == Discarded
       && the_game->player != our_seat ) {
    int s;
    /* which player display is it? */
    s = the_game->player; /* seat */
    s = (s+NUM_SEATS-our_seat)%NUM_SEATS; /* posn relative to us */
    if ( the_game->active && !the_game->paused && the_game->claims[our_seat] == UnknownClaim ) 
      discard_dialog_popup(the_game->tile,s,0);
    else
      gtk_widget_hide(discard_dialog->widget);
    if ( the_game->active && !the_game->paused && the_game->claims[our_seat] == ChowClaim
	 && the_game->chowpending )
      do_chow(NULL,(gpointer)AnyPos);
  }

  /* check whether to pop down the claim windows */
  {
    int i;
    for (i=0;i<4;i++) {
      if ( ! pdisps[i].claimw || ! GTK_WIDGET_VISIBLE(pdisps[i].claimw) ) continue;
      check_claim_window(&pdisps[i]);
    }
  }

  /* in discarded or mahjonging states, the turn dialog should be down */
  if ( the_game->state == Discarded || the_game->state == MahJonging )
    gtk_widget_hide(turn_dialog);

  /* in the discarding state, make sure the chow and ds dialogs are down.
     If it's our turn, the turn dialog should be up (unless we are in a 
     konging state, when we're waiting for other claims), otherwise down.
     If there's a kong in progress, and we haven't claimed, the discard
     dialog should be up */
  if ( the_game->state == Discarding ) {
    gtk_widget_hide(chow_dialog);
    gtk_widget_hide(ds_dialog);
    if ( the_game->active && !the_game->paused
	 && the_game->player == our_seat
	 && ! the_game->konging )
      turn_dialog_popup();
    else
      gtk_widget_hide(turn_dialog);
    if ( the_game->active && !the_game->paused && the_game->player != our_seat
	 && the_game->konging
	 && ! querykong  /* wait till we know whether we can claim */
	 && the_game->claims[our_seat] == UnknownClaim ) {
      seats s;
      /* which player display is it? */
      s = the_game->player; /* seat */
      s = (s+NUM_SEATS-our_seat)%NUM_SEATS; /* posn relative to us */
      discard_dialog_popup(the_game->tile,s,2);
    } else gtk_widget_hide(discard_dialog->widget);
  }

  /* If it is declaring specials time, and our turn,
     make sure the ds dialog is up. If it's not our turn,
     make sure it's down.
     Also make sure the turn and discard dialogs are down
     If somebody else has a kong in progress, and we haven't claimed,
     then the claim box should be up.
  */
  if ( the_game->active && !the_game->paused && the_game->state == DeclaringSpecials ) {
    if ( the_game->player == our_seat ) {
      if ( !playing_auto_declare_specials || may_declare_kong ) ds_dialog_popup();
    } else {
      gtk_widget_hide(ds_dialog);
    }
    gtk_widget_hide(turn_dialog);
    if ( the_game->konging 
	 && the_game->player != our_seat
	 && the_game->claims[our_seat] == UnknownClaim ) {
      seats s;
      /* which player display is it? */
      s = the_game->player; /* seat */
      s = (s+NUM_SEATS-our_seat)%NUM_SEATS; /* posn relative to us */
      discard_dialog_popup(the_game->tile,s,2);
    } else
      gtk_widget_hide(discard_dialog->widget);
  } else
    gtk_widget_hide(ds_dialog);

  
    
  /* In the mahjonging state, if we're the winner:
     If the pending flag is set, pop up the discard dialog if 
     the chowpending is not set, otherwise the chowdialog.
     Otherwise, pop down both it and the chow dialog.
     Pop down the turn dialog.
  */
  if ( the_game->state == MahJonging
       && the_game->player == our_seat ) {
    gtk_widget_hide(turn_dialog);
    if ( the_game->active  && !the_game->paused
	 && the_game->mjpending ) {
      int s;
      /* which player display is it? */
      s = the_game->supplier;
      s = (s+NUM_SEATS-our_seat)%NUM_SEATS; /* posn relative to us */
      if ( the_game->chowpending ) {
	do_chow(NULL,(gpointer)AnyPos);
      } else {
	discard_dialog_popup(the_game->tile,s,1);
      }
    } else {
      gtk_widget_hide(discard_dialog->widget);
      gtk_widget_hide(chow_dialog);
    }
  }

  /* In the mahjonging state, if our hand is not declared,
     and the pending flag is not set, and if either we
     are the winner or the winner's hand is declared
     popup the scoring dialog, otherwise down */
  if ( !auto_declaring_hand
    && the_game->active && !the_game->paused
    && the_game->state == MahJonging
    && !the_game->mjpending
    && !pflag(our_player,HandDeclared)
    && (the_game->player == our_seat
      || pflag(the_game->players[the_game->player],HandDeclared) ) ) {
    /* if we don't have a tile selected and we're the winner, select the first */
    if ( selected_button < 0 && the_game->player == our_seat ) 
      conc_callback(pdisps[0].conc[0],(gpointer)(0 | 128));
    scoring_dialog_popup();
  } else {
    gtk_widget_hide(scoring_dialog);
  }

  /* If the game is not active, or active but paused, 
     pop up the continue dialog */ 
  if ( (!the_game->active) || the_game->paused )
    continue_dialog_popup();
  else
    gtk_widget_hide(continue_dialog);

  /* make sure the discard dialog (or turn) is down */
  if ( the_game->state == HandComplete ) {
    gtk_widget_hide(discard_dialog->widget);
    gtk_widget_hide(turn_dialog);
  }

}

static void stdin_input_callback(gpointer          data UNUSED,
			  gint              source, 
			  GdkInputCondition condition  UNUSED) {
  char *l;

  l = get_line(source);
  if ( l ) {
    put_line(the_game->fd,l);
    /* we're bypassing the client routines, so we must update the
       sequence number ourselves */
    the_game->cseqno++;
  }
}

#ifdef GTK2
/* functions to achieve moving tiles around by dragging */
static int drag_tile = -1; /* index of tile being dragged */
/* call back when a drag starts */
static void drag_begin_callback(GtkWidget *w UNUSED, GdkDragContext *c UNUSED, gpointer u) {
  drag_tile = (int)(intptr_t)u;
}
/* motion callback */
static gboolean drag_motion_callback(GtkWidget *w UNUSED, GdkDragContext *c,
  gint x UNUSED, gint y UNUSED, guint ts, gpointer u UNUSED) {
  gdk_drag_status(c,c->suggested_action,ts);
  return TRUE;
}
/* call back when a drop occurs */
static gboolean drag_drop_callback(GtkWidget *w UNUSED, GdkDragContext *c,
  gint x, gint y UNUSED, guint ts, gpointer u) {
  int new = (int)(intptr_t)u;
  /* if the tile is moving right, and the drop is in the left half,
     the new position is one less than here, otherwise it's here.
     If the tile is moving left, and the drop is in the left half,
     the new position is here, otherwise one more than here */
  if ( new == drag_tile ) return FALSE;
  gtk_drag_finish(c,TRUE,FALSE,ts);
  if ( new > drag_tile ) {
    if ( x < tile_width/2 ) new--;
    assert(new >= 0);
  } else {
    if ( x > tile_width/2 ) new++;
    assert(new < our_player->num_concealed);
  }
  player_reorder_tile(our_player,drag_tile,new);
  playerdisp_update_concealed(&pdisps[0],HiddenTile);
  /* clear selection to avoid confusion */
  conc_callback(NULL,NULL);
  return TRUE;
}


#endif

/* given an already allocated PlayerDisp* and an orientation, 
   initialize a player display.
   The player widget is NOT shown, but everything else (appropriate) is */
static void playerdisp_init(PlayerDisp *pd, int ori) {
  /* here is the widget hierarchy we want:
     w -- the parent widget
       concealed  -- the concealed row
         csets[MAX_TILESETS]  -- five boxes which will have tiles packed in them
	 conc  -- the concealed & special tile subbox, with...
           conc[14] -- 14 tile buttons
           extras[8] -- more spaces for specials if we run out.
	                pdispwidth-14 are initially set, to get the spacing.
       exposed  -- the exposed row 
           esets[MAX_TILESETS] -- with sets, each of which has
                       tile buttons as appropriate.
           specbox --
             spec[8]  -- places for specials
	               (Not all of these can always be used; so we may
		       need to balance dynamically between this and extras.)
	     tongbox  -- the tong box pixmap widget 
  */
  GtkWidget *concealed, *exposed, *conc, *c, *b, *pm, *w,*specbox;
  int i;
  int tb,br,bl;
  int eori; /* orientation of exposed tiles, if different */


  /* pd->player = NULL; */ /* don't do this: playerdisp_init is
     also called when rebuilding the display, in which case we
     shouldn't null the player. The player field should be nulled
     when the PlayerDisp is created */
  pd->orientation = ori;
  eori = flipori(ori);

  /* sometimes it's enough to know whether the player is top/bottom,
     or the left/right */
  tb = ! (ori%2); 
  /* and sometimes the bottom and right players are similar
     (they are to the increasing x/y of their tiles */
  br = (ori < 2);
  /* and sometimes the bottom and left are similar (their right
     is at the positive and of the box */
  bl = (ori == 0 || ori == 3);

  /* homogeneous to force exposed row to right height */
  if ( tb )
    w = gtk_vbox_new(1,tile_spacing);
  else
    w = gtk_hbox_new(1,tile_spacing);
#ifndef GTK2
  gtk_widget_set_style(w,tablestyle);
#endif
  gtk_widget_set_name(w,"table");

  pd->widget = w;
  gtk_container_set_border_width(GTK_CONTAINER(w),player_border_width);

  if ( tb )
    concealed = gtk_hbox_new(0,tile_spacing);
  else
    concealed = gtk_vbox_new(0,tile_spacing);
#ifndef GTK2
  gtk_widget_set_style(concealed,tablestyle);
#endif
  gtk_widget_set_name(concealed,"table");
  gtk_widget_show(concealed);
  /* the concealed box is towards the player */
  /* This box needs to stay expanded to the width of the surrounding,
     which should be fixed at start up.*/
  if ( br )
    gtk_box_pack_end(GTK_BOX(w),concealed,1,1,0);
  else
    gtk_box_pack_start(GTK_BOX(w),concealed,1,1,0);

  /* players put closed (nonkong) tilesets in to the left of their
     concealed tiles */
  for ( i=0; i < MAX_TILESETS; i++) {
    if ( tb )
      c = gtk_hbox_new(0,0);
    else
      c = gtk_vbox_new(0,0);
    if ( bl )
      gtk_box_pack_start(GTK_BOX(concealed),c,0,0,0);
    else
      gtk_box_pack_end(GTK_BOX(concealed),c,0,0,0);
    pd->csets[i].widget = c;
    tilesetbox_init(&pd->csets[i],ori,NULL,0);
  }

  if ( tb )
    conc = gtk_hbox_new(0,0);
  else 
    conc = gtk_vbox_new(0,0);
#ifndef GTK2
  gtk_widget_set_style(conc,tablestyle);
#endif
  gtk_widget_set_name(conc,"table");
  gtk_widget_show(conc);

  /* the concealed tiles are to the right of the concealed sets */
  /* This also needs to stay expanded */
  if ( bl )
    gtk_box_pack_start(GTK_BOX(concealed),conc,1,1,0);
  else
    gtk_box_pack_end(GTK_BOX(concealed),conc,1,1,0);

  for ( i=0; i < 14; i++ ) {
    /* normally, just buttons, but for us toggle buttons */
    if ( ori == 0 )
      b = gtk_toggle_button_new();
    else
      b = gtk_button_new();
    /* for gtk2 style info */
    gtk_widget_set_name(b, (ori == 0) ? "mytile" : "tile");
    /* we don't want any of the buttons taking the focus */
    GTK_WIDGET_UNSET_FLAGS(b,GTK_CAN_FOCUS);
    gtk_widget_show(b);
    pm = gtk_pixmap_new(tilepixmaps[ori][HiddenTile],NULL);
    gtk_widget_show(pm);
    gtk_container_add(GTK_CONTAINER(b),pm);
    /* the concealed tiles are placed from the player's left */
    if ( bl )
      gtk_box_pack_start(GTK_BOX(conc),b,0,0,0);
    else
     gtk_box_pack_end(GTK_BOX(conc),b,0,0,0);
    pd->conc[i] = b;
    /* if this is our player (ori = 0), attach a callback */
    if ( ori == 0 ) {
      gtk_signal_connect(GTK_OBJECT(b),"toggled",
	GTK_SIGNAL_FUNC(conc_callback),(gpointer)(intptr_t)i);
      gtk_signal_connect(GTK_OBJECT(b),"button_press_event",
			 GTK_SIGNAL_FUNC(doubleclicked),0);
#ifdef GTK2
      /* and set as a drag source */
      /* Why do I have to use ACTION_MOVE ? Why can't I use 
	 ACTION_PRIVATE, which just doesn't work ? */
      gtk_drag_source_set(b,GDK_BUTTON1_MASK,NULL,0,GDK_ACTION_MOVE);
      /* and a drag destination, or at least an emitter of events */
      gtk_drag_dest_set(b,0,NULL,0,0);
      gtk_drag_dest_set_track_motion(b,TRUE);
      gtk_signal_connect(GTK_OBJECT(b),"drag_begin",
	GTK_SIGNAL_FUNC(drag_begin_callback),(gpointer)(intptr_t)i);
      gtk_signal_connect(GTK_OBJECT(b),"drag_motion",
	GTK_SIGNAL_FUNC(drag_motion_callback),(gpointer)(intptr_t)i);
      gtk_signal_connect(GTK_OBJECT(b),"drag_drop",
	GTK_SIGNAL_FUNC(drag_drop_callback),(gpointer)(intptr_t)i);
#endif
    }
  }

  /* to the right of the concealed tiles, we will place
     up to 8 other tiles. These will be used for flowers
     and seasons when we run out of room in the exposed row.
     By setting pdispwidth-14 (default 5) of them initially,
     it also serves the purpose of fixing the row to be
     the desired 19 (currently) tiles wide */
  for ( i=0; i < 8; i++ ) {
    b = gtk_button_new();
    gtk_widget_set_name(b,"tile");
    /* we don't want any of the buttons taking the focus */
    GTK_WIDGET_UNSET_FLAGS(b,GTK_CAN_FOCUS);
    if ( i < pdispwidth-14 ) gtk_widget_show(b);
    pm = gtk_pixmap_new(tilepixmaps[ori][HiddenTile],NULL);
    gtk_widget_show(pm);
    gtk_container_add(GTK_CONTAINER(b),pm);
    /* these extra tiles are placed from the player's right */
    if ( bl )
      gtk_box_pack_end(GTK_BOX(conc),b,0,0,0);
    else
      gtk_box_pack_start(GTK_BOX(conc),b,0,0,0);
    pd->extras[i] = b;
  }



  
  if ( tb )
    exposed = gtk_hbox_new(0,tile_spacing);
  else 
    exposed = gtk_vbox_new(0,tile_spacing);
#ifndef GTK2
  gtk_widget_set_style(exposed,tablestyle);
#endif
  gtk_widget_set_name(exposed,"table");
  gtk_widget_show(exposed);
  /* the exposed box is away from the player */
  if ( br )
    gtk_box_pack_start(GTK_BOX(w),exposed,0,0,0);
  else
    gtk_box_pack_end(GTK_BOX(w),exposed,0,0,0);

  /* the exposed tiles will be in front of the player, from
     the left */
  for ( i=0; i < MAX_TILESETS; i++ ) {
    if ( tb )
      c = gtk_hbox_new(0,0);
    else
      c = gtk_vbox_new(0,0);
#ifndef GTK2
    gtk_widget_set_style(c,tablestyle);
#endif
    gtk_widget_set_name(c,"table");
    gtk_widget_show(c);
    if ( bl )
      gtk_box_pack_start(GTK_BOX(exposed),c,0,0,0);
    else
      gtk_box_pack_end(GTK_BOX(exposed),c,0,0,0);
    pd->esets[i].widget = c;
    tilesetbox_init(&pd->esets[i],eori,NULL,0);
  }
  /* the special tiles are at the right of the exposed row */
  if ( tb )
    specbox = gtk_hbox_new(0,0);
  else
    specbox = gtk_vbox_new(0,0);
#ifndef GTK2
  gtk_widget_set_style(specbox,tablestyle);
#endif
  gtk_widget_set_name(specbox,"table");
  gtk_widget_show(specbox);
  if ( bl )
    gtk_box_pack_end(GTK_BOX(exposed),specbox,0,0,0);
  else
    gtk_box_pack_start(GTK_BOX(exposed),specbox,0,0,0);

  /* at the right of the spec box, we place a tongbox pixmap.
     This is not initially shown */
  pd->tongbox = gtk_pixmap_new(tongpixmaps[ori][east],tongmask);
  if ( bl )
    gtk_box_pack_end(GTK_BOX(specbox),pd->tongbox,0,0,0);
  else
    gtk_box_pack_start(GTK_BOX(specbox),pd->tongbox,0,0,0);

  for ( i=0 ; i < 8; i++ ) {
    b = gtk_button_new();
    gtk_widget_set_name(b,"tile");
    /* we don't want any of the buttons taking the focus */
    GTK_WIDGET_UNSET_FLAGS(b,GTK_CAN_FOCUS);
    pm = gtk_pixmap_new(tilepixmaps[eori][HiddenTile],NULL);
    gtk_widget_show(pm);
    gtk_container_add(GTK_CONTAINER(b),pm);
    if ( bl )
      gtk_box_pack_end(GTK_BOX(specbox),b,0,0,0);
    else
      gtk_box_pack_start(GTK_BOX(specbox),b,0,0,0);
    pd->spec[i] = b;
  }

  /* the player's discard buttons are created as required,
     so just zero them */
  for ( i=0; i<32; i++ ) pd->discards[i] = NULL;
  pd->num_discards = 0;
  /* also initialize the info kept by the discard routine */
  pd->x = pd->y = 0;
  pd->row = 0;
  pd->plane = 0;
  for (i=0;i<5;i++) {
    pd->xmin[i] = 10000;
    pd->xmax[i] = 0;
  }

  pd->claimw = NULL;
  /* the claim window is initialized on first popup, because it
     needs to have all the rest of the board set out to get the size right */
}

/* event callback used below. The widget is a tile button, and the
   callback data is its tiletip window. For use below, a NULL event
   means always pop it up */
static gint maybe_popup_tiletip(GtkWidget *w, GdkEvent *ev, gpointer data)
{
  GtkWidget *tiletip = (GtkWidget *)data;

  /* popup if this is an enternotify with the right button already
     down, or if this is a button press with the right button */
  if ( ev == NULL
    || ( ev->type == GDK_ENTER_NOTIFY 
      && ( (((GdkEventCrossing *)ev)->state & GDK_BUTTON3_MASK)
	|| tiletips ) )
    || ( ev->type == GDK_BUTTON_PRESS
      && ((GdkEventButton *)ev)->button == 3 ) ) {
    /* pop it up just above, right, left, below the tile */
    int ori = 0;
    gint x, y;
    gint ttx = 0, tty = 0;
    GtkRequisition r;
    get_relative_posn(boardfixed,w,&x,&y);
    gtk_widget_size_request(tiletip,&r);
    ori = (int)(intptr_t)gtk_object_get_data(GTK_OBJECT(w),"ori");
    switch ( ori ) {
    case 0: ttx = x; tty = y-r.height; break;
    case 1: ttx = x - r.width; tty = y; break;
    case 2: ttx = x; tty = y + tile_height; break;
    case 3: ttx = x + tile_height; tty = y; break;
    default: warn("impossible ori in maybe_popup_tiletip");
    }
    vlazy_fixed_move(VLAZY_FIXED(boardfixed),tiletip,ttx,tty);
    gtk_widget_show(tiletip);
  }
  /* if it's a leavenotify, and the tile is not selected, pop down,
     unless the tile is a selected button */
  if ( ev && ev->type == GDK_LEAVE_NOTIFY ) {
    if ( tiletips
      && GTK_WIDGET_VISIBLE(w)
      && GTK_IS_TOGGLE_BUTTON(w) 
      && GTK_TOGGLE_BUTTON(w)->active ) {
      ; // leave it up 
    } else {
      gtk_widget_hide(tiletip);
    }
  }
  return 0;
}
/* signal callback used to popup tiletips on selecting a tile */
static gint maybe_popup_tiletip_when_selected(GtkWidget *w, gpointer data)
{
  GtkWidget *tiletip = (GtkWidget *)data;
  if ( tiletips && GTK_IS_TOGGLE_BUTTON(w) && GTK_TOGGLE_BUTTON(w)->active && GTK_WIDGET_VISIBLE(w) ) {
    maybe_popup_tiletip(w,NULL,data);
  } else {
    gtk_widget_hide(tiletip);
  }
  return 0;
}


/* given a button, tile, and orientation, set the pixmap (and tooltip when
   we have them. Stash the tile in the user_data field.
   Stash the ori as ori.
   Last arg is true if a tiletip should be installed */
static void button_set_tile_aux(GtkWidget *b, Tile t, int ori,int use_tiletip) {
  intptr_t ti;
  GtkWidget *tiletip, *lab;
  gtk_pixmap_set(GTK_PIXMAP(GTK_BIN(b)->child),
    tilepixmaps[ori][t],NULL);
  ti = t;
  gtk_object_set_user_data(GTK_OBJECT(b),(gpointer)ti);
  gtk_object_set_data(GTK_OBJECT(b),"ori",(gpointer)(intptr_t)ori);
  if ( use_tiletip ) {
    /* update, create or delete the tiletip */
    tiletip = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(b),"tiletip");
    if ( t == HiddenTile ) {
      if ( tiletip ) gtk_widget_destroy(tiletip);
      gtk_object_set_data(GTK_OBJECT(b),"tiletip",0);
    } else {
      if ( tiletip ) {
	/* update label */
	gtk_label_set_text(GTK_LABEL(GTK_BIN(tiletip)->child),
	  tile_name(t));
      } else {
	/* create tiletip */
	tiletip = gtk_event_box_new();
	/* it will be a child of board fixed, but its window
	   will be moved around, and its widget position ignored */
	vlazy_fixed_put(VLAZY_FIXED(boardfixed),tiletip,0,0);
	gtk_object_set_data(GTK_OBJECT(b),"tiletip",tiletip);
	lab = gtk_label_new(tile_name(t));
	gtk_widget_show(lab);
	gtk_container_add(GTK_CONTAINER(tiletip),lab);
	/* install callbacks on the button, which will die when
	   the popup is killed */
	/* callback on enter */
	gtk_signal_connect_while_alive(GTK_OBJECT(b),"enter_notify_event",
	  (GtkSignalFunc)maybe_popup_tiletip,
	  tiletip,
	  GTK_OBJECT(tiletip));
	/* callback on button press */
	gtk_signal_connect_while_alive(GTK_OBJECT(b),"button_press_event",
	  (GtkSignalFunc)maybe_popup_tiletip,
	  tiletip,
	  GTK_OBJECT(tiletip));
	/* leaving may pop down the window */
	gtk_signal_connect_while_alive(GTK_OBJECT(b),"leave_notify_event",
	  (GtkSignalFunc)maybe_popup_tiletip,
	  tiletip,
	  GTK_OBJECT(tiletip));
	/* toggling may require tile tips */
	if ( GTK_IS_TOGGLE_BUTTON(b) ) {
	  gtk_signal_connect_while_alive(GTK_OBJECT(b),"toggled",
	    (GtkSignalFunc)maybe_popup_tiletip_when_selected,
	    tiletip,
	    GTK_OBJECT(tiletip));
	}
	/* also want to make sure tip goes away on being hidden. */
	gtk_signal_connect_object_while_alive(GTK_OBJECT(b),"hide",
	  (GtkSignalFunc)gtk_widget_hide,GTK_OBJECT(tiletip));
	/* and if the tile is destroyed, the tip had better be */
	gtk_signal_connect_object_while_alive(GTK_OBJECT(b),"destroy",
	  (GtkSignalFunc)gtk_widget_destroy,GTK_OBJECT(tiletip));
      }
    }
  }
}

void button_set_tile(GtkWidget *b, Tile t, int ori)
{
  button_set_tile_aux(b,t,ori,1);
}

void button_set_tile_without_tiletip(GtkWidget *b, Tile t, int ori)
{
  button_set_tile_aux(b,t,ori,0);
}

/* given a pointer to a TileSetBox, and an orientation,
   and a callback function and data
   initialize the box by creating the widgets and attaching
   the signal function.
*/
void tilesetbox_init(TileSetBox *tb, int ori,
		    GtkSignalFunc func,gpointer func_data) {
  int i;
  GtkWidget *b, *pm;

  for (i=0; i<4; i++) {
    /* make a button */
    b = gtk_button_new();
    gtk_widget_set_name(b,"tile");
    tb->tiles[i] = b;
    if ( func )
      gtk_signal_connect(GTK_OBJECT(b),"clicked",
			 func,func_data);
    /* we don't want any of the tiles taking the focus */
    GTK_WIDGET_UNSET_FLAGS(b,GTK_CAN_FOCUS);
    pm = gtk_pixmap_new(tilepixmaps[ori][HiddenTile],NULL);
    gtk_widget_show(pm);
    gtk_container_add(GTK_CONTAINER(b),pm);
    /* we want chows to grow from top and left for left and bottom */
    if ( ori == 0 || ori == 3 )
      gtk_box_pack_start(GTK_BOX(tb->widget),b,0,0,0);
    else 
      gtk_box_pack_end(GTK_BOX(tb->widget),b,0,0,0);
  }

  tb->set.type = Empty;
}

/* given a pointer to a TileSetBox, and a pointer to a TileSet,
   and an orientation,
   make the box display the tileset.
*/
void tilesetbox_set(TileSetBox *tb, const TileSet *ts, int ori) {
  int n, i, ck, millingkong;
  Tile t[4];

  if (tb->set.type == ts->type 
    && (ts->type == Empty || tb->set.tile == ts->tile) ) {
    /* nothing to do, except show for safety */
    if ( ts->type == Empty )
      gtk_widget_hide(tb->widget);
    else
      gtk_widget_show(tb->widget);
    return;
  }

  tb->set = *ts;
  
  n = 2; /* for pairs etc */
  ck = 0; /* closed kong */
  millingkong = 0; /* playing Millington's rules, and the kong
		      is claimed rather than annexed */
  switch (ts->type) {
  case ClosedKong:
    ck = 1;
  case Kong:
    n++;
    if ( !ck && ! ts->annexed
      && the_game && game_get_option_value(the_game,GOKongHas3Types,NULL).optbool )
      millingkong = 1;
  case Pung:
  case ClosedPung:
    n++;
  case Pair:
  case ClosedPair:
    for ( i=0 ; i < n ; i++ ) {
      t[i] = ts->tile;
      if ( ck && ( i == 0 || i == 3 ) ) t[i] = HiddenTile;
      if ( millingkong && i == 3 ) t[i] = HiddenTile;
    }
    break;
  case Chow:
  case ClosedChow:
    n = 3;
    for (i=0; i<n; i++) {
      t[i] = ts->tile+i;
    }
    break;
  case Empty:
    n = 0;
  }

  for (i=0; i<n; i++) {
    button_set_tile(tb->tiles[i],t[i],ori);
    gtk_widget_show(tb->tiles[i]);
  }
  for ( ; i < 4; i++ )
    if ( tb->tiles[i] ) gtk_widget_hide(tb->tiles[i]);

  if ( ts->type == Empty )
    gtk_widget_hide(tb->widget);
  else
    gtk_widget_show(tb->widget);
}

/* utility used by the chow functions. Given a TileSetBox,
   highlight the nth tile by setting the others insensitive.
   (n counts from zero). */
void tilesetbox_highlight_nth(TileSetBox *tb,int n) {
  int i;
  for (i=0; i<4; i++)
    if ( tb->tiles[i] )
      gtk_widget_set_sensitive(tb->tiles[i],i==n);
}


/* two routines used internally */

/* update the concealed tiles of the playerdisp.
   Returns the *index* of the last tile matching the third argument.
*/
static int playerdisp_update_concealed(PlayerDisp *pd,Tile t) {
  PlayerP p = pd->player;
  int tindex = -1;
  int i;
  int cori,eori;

  cori = pd->orientation;
  eori = flipori(cori);
  if ( p && pflag(p,HandDeclared) ) cori = eori;

  for ( i=0 ; p && i < p->num_concealed ; i++ ) {
    button_set_tile(pd->conc[i],p->concealed[i],cori);
    gtk_widget_show(pd->conc[i]);
    if ( p->concealed[i] == t ) tindex = i;
  }
  for ( ; i < MAX_CONCEALED ; i++ ) {
    gtk_widget_hide(pd->conc[i]);
  }
  return tindex;
}

/* update the exposed tiles of the playerdisp.
   Returns the _button widget_ of the last tile that
   matched the second argument.
   The third argument causes special treatment: an exposed pung matching
   the tile is displayed as a kong.
*/
static GtkWidget *playerdisp_update_exposed(PlayerDisp *pd,Tile t,int robbingkong){
  int i,scount,excount,ccount,ecount,qtiles;
  GtkWidget *w;
  TileSet emptyset;
  PlayerP p = pd->player;
  int cori, eori; /* orientations of concealed and exposed tiles */

  cori = pd->orientation;
  eori = flipori(cori);
  emptyset.type = Empty;
  if ( p && pflag(p,HandDeclared) ) cori = eori;

  w = NULL; /* destination widget */
  /* We need to count the space taken up by the exposed tilesets,
     in order to know where to put the specials */
  qtiles = 0; /* number of quarter tile widths */
  ccount = ecount = 0;
  for ( i=0; p && i < MAX_TILESETS; i++) {
    switch (p->tilesets[i].type) {
    case ClosedPair:
    case ClosedChow:
      /* in the concealed row, so no space */
      /* NB, these are declared, they are oriented as
	 exposed sets, despite the name */
      tilesetbox_set(&pd->csets[ccount++],&p->tilesets[i],eori);
      break;
    case ClosedPung:
      /* in the special case that we are in the middle of robbing
	 a kong (13 wonders), and this is the robbed set, we keep
	 it in the exposed row displayed as a kong */
      if ( robbingkong && t == p->tilesets[i].tile) {
	TileSet ts = p->tilesets[i];
	ts.type = ClosedKong;
	tilesetbox_set(&pd->esets[ecount],&ts,eori);
	w = pd->esets[ecount].tiles[3];
	qtiles += 16; /* four tiles */
	ecount++;
      } else {
	tilesetbox_set(&pd->csets[ccount++],&p->tilesets[i],eori);
      }
      break;
    case ClosedKong:
    case Kong:
      tilesetbox_set(&pd->esets[ecount],&p->tilesets[i],eori);
      if ( t == p->tilesets[i].tile )
	w = pd->esets[ecount].tiles[3];
      qtiles += 16; /* four tiles */
      ecount++;
      break;
    case Pung:
      if ( robbingkong && t == p->tilesets[i].tile) {
	TileSet ts = p->tilesets[i];
	ts.type = Kong;
	tilesetbox_set(&pd->esets[ecount],&ts,eori);
	w = pd->esets[ecount].tiles[3];
	qtiles += 16; /* four tiles */
      } else {
	tilesetbox_set(&pd->esets[ecount],&p->tilesets[i],eori);
	if ( t == p->tilesets[i].tile )
	  w = pd->esets[ecount].tiles[2];
	qtiles += 12;
      }
      ecount++;
      break;
    case Chow:
      tilesetbox_set(&pd->esets[ecount],&p->tilesets[i],eori);
      if ( t >= p->tilesets[i].tile
	   && t <= p->tilesets[i].tile+2 )
	w = pd->esets[ecount].tiles[t-p->tilesets[i].tile];
      qtiles += 12;
      ecount++;
      break;
    case Pair:
      tilesetbox_set(&pd->esets[ecount],&p->tilesets[i],eori);
      if ( t == p->tilesets[i].tile )
	w = pd->esets[ecount].tiles[1];
      qtiles += 9; /* for the two tiles and space */
      ecount++;
      break;
    case Empty:
      ;
    }
  }
  for ( ;  ccount < MAX_TILESETS; )
    tilesetbox_set(&pd->csets[ccount++],&emptyset,eori);
  for ( ;  ecount < MAX_TILESETS; )
    tilesetbox_set(&pd->esets[ecount++],&emptyset,eori);
  /* if we are the dealer, the tongbox takes up
     about 1.5 tiles */
  if ( p && p->wind == EastWind ) {
    gtk_pixmap_set(GTK_PIXMAP(pd->tongbox),tongpixmaps[cori][the_game->round-1],
		   tongmask);
    gtk_widget_show(pd->tongbox);
    qtiles += 6;
  } else {
    gtk_widget_hide(pd->tongbox);
  }
  
  /* for the special tiles, we put as many as
     possible in the exposed row (specs),
     and then overflow into the concealed row */
  qtiles = (qtiles+3)/4; /* turn quarter tiles into tiles */
  scount = excount = 0;
  for ( i = 0; p && i < p->num_specials
	  && i <= pdispwidth - qtiles ; i++ ) {
    button_set_tile(pd->spec[i],p->specials[i],eori);
    gtk_widget_show(pd->spec[i]);
    if ( t == p->specials[i] ) w = pd->spec[i];
    scount++;
  }
  for ( ; p && i < p->num_specials ; i++ ) {
    button_set_tile(pd->extras[i-scount],p->specials[i],eori);
    gtk_widget_show(pd->extras[i-scount]);
    if ( t == p->specials[i] ) w = pd->extras[i-scount];
    excount++;
  }
  for ( ; scount < 8; scount ++ )
    gtk_widget_hide(pd->spec[scount]);
  for ( ; excount < 8; excount ++ )
    gtk_widget_hide(pd->extras[excount]);
  
  return w;
}

/* given a playerdisp, update the hand.
   The second argument gives the message prompting this.
   Animation is handled entirely in here.
*/


static void playerdisp_update(PlayerDisp *pd, CMsgUnion *m) {
  Tile t; int tindex;
  static AnimInfo srcs[4], dests[4];
  TileSet emptyset;
  int numanims = 0;
  static GtkWidget *robbedfromkong; /* yech */
  PlayerP p = pd->player;
  int deftile = -1; /* default tile to be discarded */
  int cori, eori; /* orientations of concealed and exposed tiles */
  int updated = 0; /* flag to say we've done our stuff */

  cori = pd->orientation;
  eori = flipori(cori);
  emptyset.type = Empty;
  if ( p && pflag(p,HandDeclared) ) cori = eori;

  if ( m == NULL ) {
    /* just update */
    playerdisp_update_concealed(pd,HiddenTile);
    playerdisp_update_exposed(pd,HiddenTile,0);
    return;
  }

  if ( m->type == CMsgNewHand && p) {
    playerdisp_clear_discards(pd);
    playerdisp_update_concealed(pd,HiddenTile);
    playerdisp_update_exposed(pd,HiddenTile,0);
    return;
  }

  if ( m->type == CMsgPlayerDraws || m->type == CMsgPlayerDrawsLoose) {
    updated = 1;
    t = (m->type == CMsgPlayerDraws) ? m->playerdraws.tile : 
      m->playerdrawsloose.tile;
    tindex = playerdisp_update_concealed(pd,t);
    assert(tindex >= 0);
    /* do we want to select a tile? Usually, but in declaring
       specials state, we want to select a special if there is one,
       and otherwise not */
    if ( p == our_player ) {
      if ( the_game->state == DeclaringSpecials ) {
	int i;
	for (i=0; i<p->num_concealed && !is_special(p->concealed[i]);i++);
	if ( i < p->num_concealed ) {
	  deftile = i;
	  conc_callback(pd->conc[deftile],(gpointer)(intptr_t)(deftile | 128));
	} else {
	  conc_callback(NULL,NULL);
	}
      } else {
	deftile = tindex;
	conc_callback(pd->conc[deftile],(gpointer)(intptr_t)(deftile | 128));
      }
    }
    dests[numanims].target = pd->conc[tindex];
    dests[numanims].t = t;
    get_relative_posn(boardframe,dests[numanims].target,&dests[numanims].x,&dests[numanims].y);
    /* that was the destination, now the source */
    if ( showwall ) {
      if ( m->type == CMsgPlayerDraws ) {
	int i;
	/* wall has already been updated, so it's -1 */
	i = wall_game_to_board(the_game->wall.live_used-1);
	srcs[numanims].target = NULL;
	srcs[numanims].t = m->playerdraws.tile; /* we may as well see our tile now */
	srcs[numanims].ori = wall_ori(i);
	get_relative_posn(boardframe,wall[i],&srcs[numanims].x,&srcs[numanims].y);
	gtk_widget_destroy(wall[i]);
	wall[i] = NULL;
      } else {
	/* draws loose */
	srcs[numanims] = *adjust_wall_loose(the_game->wall.dead_end);
	/* in fact, we may as well see the tile if we're drawing */
	srcs[numanims].t = t;
      }
    } else {
      /* if we're animating, we'll just have the tile spring up from
	 nowhere! */
      srcs[numanims].target = NULL;
      srcs[numanims].t = t; /* we may as well see our tile now */
      srcs[numanims].ori = cori;
      /* these should be corrected for orientation */
      srcs[numanims].x = boardframe->allocation.width/2 - tile_width/2;
      srcs[numanims].y = boardframe->allocation.height/2 - tile_height/2;
    }
    numanims++;
  } /* end of Draws and DrawsLoose */
  if ( m->type == CMsgPlayerDiscards ) {
    updated = 1;
    /* update the concealed tiles */
    playerdisp_update_concealed(pd,HiddenTile);
    /* the source for animation is the previous rightmost tile
       for other players, or the selected tile, for us.
       Note that selected button may not be set, if we're
       replaying history. In that case, we won't bother to
       set the animation position, in the knowledge that 
       it won't actually be animated.
       This is far too convoluted. 
    */
    /* FIXME: actually the selected button could be wrong, as the user
       might have moved it between sending the discard request and 
       getting this controller message */
    tindex = (p == our_player) ? selected_button : p->num_concealed;
    srcs[numanims].t = m->playerdiscards.tile;
    srcs[numanims].ori = eori;
    if ( tindex >= 0 ) {
      get_relative_posn(boardframe,pd->conc[tindex],&srcs[numanims].x,
			&srcs[numanims].y);
    }
    /* now display the new discard */
    dests[numanims] = *playerdisp_discard(pd,m->playerdiscards.tile);
    discard_history.hist[discard_history.count].pd = pd;
    discard_history.hist[discard_history.count].t = m->playerdiscards.tile;
    discard_history.count++;
    /* if we're not animating, highlight the tile */
    if ( ! (animate && the_game->active) ) {
#ifdef GTK2
      gtk_widget_modify_bg(dests[numanims].target,GTK_STATE_NORMAL,&highlightcolor);
#else
      gtk_widget_set_style(dests[numanims].target,highlightstyle);
#endif

      highlittile = dests[numanims].target;
    }
    /* if we're discarding, clear the selected tile */
    if ( p == our_player ) conc_callback(NULL,NULL);
    numanims++;
  } /* end of CMsgPlayerDiscards */
  if ( m->type == CMsgPlayerDeclaresSpecial ) {
    updated = 1;
    if ( m->playerdeclaresspecial.tile != HiddenTile ) {
      GtkWidget *w = NULL;
      /* the source tile in this case is either the tile
	 after the rightmost concealed tile (for other players)
	 or the selected tile (for us) */
      /* FIXME: again, the selected tile is not necessarily right */
      if ( p == our_player ) {
	if ( selected_button >= 0 ) w = pd->conc[selected_button];
	/* else we're probably replaying history */
      } else {
	w = pd->conc[p->num_concealed];
      }
      if ( w ) get_relative_posn(boardframe,w,&srcs[numanims].x,&srcs[numanims].y);
      srcs[numanims].t = m->playerdeclaresspecial.tile;
      srcs[numanims].ori = cori;
      /* now update the special tiles, noting the destination */
      w = playerdisp_update_exposed(pd,m->playerdeclaresspecial.tile,0);
      assert(w);
      /* this forces a resize calculation so that the special tiles
	 are in place */
      gtk_widget_size_request(pd->widget,NULL);
      gtk_widget_size_allocate(pd->widget,&pd->widget->allocation);
      dests[numanims].target = w;
      dests[numanims].ori = eori;
      get_relative_posn(boardframe,w,&dests[numanims].x,&dests[numanims].y);
      numanims++; /* that's it */
    } else {
      /* blank tile. nothing to do except select a tile if it's now
	 our turn to declare */
      if ( the_game->state == DeclaringSpecials
	   && the_game->player == our_seat ) {
	if ( is_special(our_player->concealed[our_player->num_concealed-1]) ) {
	  deftile = our_player->num_concealed-1;
	  conc_callback(pdisps[0].conc[deftile],(gpointer)(intptr_t)(deftile | 128));
	} else {
	  conc_callback(NULL,NULL);
	}
      }
    }
  } /* end of CMsgPlayerDeclaresSpecial */
  if ( m->type == CMsgPlayerClaimsPung 
	    || m->type == CMsgPlayerClaimsKong 
	    || m->type == CMsgPlayerClaimsChow 
	    || m->type == CMsgPlayerClaimsMahJong
            || m->type == CMsgPlayerMahJongs 
            || (m->type == CMsgPlayerDiscards
		&& m->playerdiscards.calling) ) {
    /* popup a claim window.
       This is a real pain, cos we have to work out where to put it.
       To keep it simple, we'll put it half way along the appropriate
       edge, centered two tileheights in. */
    const char *lab;
    GtkRequisition r;
    gint x,y,w,h;
    
    /* first of all, create the claim window if not already done */
    if ( pd->claimw == NULL )  { 
      GtkWidget *box;
      // all this should be handled by styles 
#ifndef GTK2
      static GtkStyle *s;
      GdkColor c;
      if ( !s ) {
	s = gtk_style_copy(defstyle);
	gdk_color_parse("yellow",&c);
	s->bg[GTK_STATE_NORMAL] = c;
	if ( big_font ) 
	  s->font = big_font;
      }
      gtk_widget_push_style(s);
#endif // ifndef GTK2
      pd->claimw = gtk_event_box_new();
      gtk_widget_set_name(GTK_WIDGET(pd->claimw),"claim");
      box = gtk_hbox_new(0,0);
      gtk_widget_show(box);
      gtk_widget_set_name(box,"claim");
      gtk_container_add(GTK_CONTAINER(pd->claimw),box);
      /* should parametrize this some time */
      gtk_container_set_border_width(GTK_CONTAINER(box),7);
      pd->claimlab = gtk_label_new("");
      gtk_widget_set_name(pd->claimlab,"claim");
      pd->claim_serial = -1;
      pd->claim_time = -10000;
      gtk_widget_show(pd->claimlab);
      gtk_box_pack_start(GTK_BOX(box),pd->claimlab,0,0,0);
#ifndef GTK2
      gtk_widget_pop_style();
#endif
      vlazy_fixed_put(VLAZY_FIXED(boardfixed),pd->claimw,0,0);
    }

    /* set the label */
    updated = 1;
    switch ( m->type ) {
    case CMsgPlayerClaimsPung: lab = "Pung!"; break;
    case CMsgPlayerClaimsKong: lab = "Kong!"; break;
    case CMsgPlayerClaimsChow: lab = "Chow!"; break;
    case CMsgPlayerMahJongs:
    case CMsgPlayerClaimsMahJong: lab = "Mah Jong!"; break;
    case CMsgPlayerDiscards: lab = "Calling!" ; break;
    default: assert(0);
    }
    gtk_label_set_text(GTK_LABEL(pd->claimlab),lab);
    pd->claim_serial = the_game->serial;
    pd->claim_time = now_time();

    /* calculate the position given the current label */
    gtk_widget_size_request(board,&r);
    w = r.width; h = r.height;
    gtk_widget_size_request(pd->claimw,&r);
    switch ( cori ) {
    case 0:
      x = w/2 - r.width/2;
      y = h - r.height/2 - 2*tile_height;
      break;
    case 1:
      x = w - r.width/2 - 2*tile_height;
      y = h/2 - r.height/2;
      break;
    case 2:
      x = w/2 - r.width/2;
      y = - r.height/2 + 2*tile_height;
      break;
    case 3:
      x = - r.width/2 + 2*tile_height;
      y = h/2 - r.height/2;
      break;
    default:
      /* to shut up warnings */
      x = 0; y = 0;
      error("bad value of cori in switch in playerdisp_update");
    }
    vlazy_fixed_move(VLAZY_FIXED(boardfixed),pd->claimw,x,y);

    /* if not already popped up */
    if ( !GTK_WIDGET_VISIBLE(pd->claimw) ) {
      gtk_widget_show(pd->claimw);
    }
  } /* end of the claims */
  /* On receiving a successful claim, withdraw the last discard.
     And if mahjonging, select the first tile if this is us */
  if ( (m->type == CMsgPlayerChows
	&& ((CMsgPlayerChowsMsg *)m)->cpos != AnyPos)
       || m->type == CMsgPlayerPungs
       || m->type == CMsgPlayerKongs
       || m->type == CMsgPlayerPairs
       || m->type == CMsgPlayerSpecialSet ) {
    GtkWidget *w;

    updated = 1;
    /* withdraw discard, or use the saved widget if a kong was robbed */
    if ( the_game->whence == FromRobbedKong ) {
      srcs[numanims].t = the_game->tile;
      get_relative_posn(boardframe,robbedfromkong,
			&srcs[numanims].x,&srcs[numanims].y);
      playerdisp_update_exposed(&pdisps[game_to_board(the_game->supplier)],HiddenTile,0);
    } else {
      srcs[numanims] = *playerdisp_discard(&pdisps[game_to_board(the_game->supplier)],HiddenTile);
      discard_history.hist[discard_history.count].pd = &pdisps[game_to_board(the_game->supplier)];
      discard_history.hist[discard_history.count].t = HiddenTile;
      discard_history.count++;
    }
    /* update exposed tiles, and get the destination widget */
    w = playerdisp_update_exposed(pd,the_game->tile,0);
    /* In the case of a special set, the destination tile will be
       in the concealed tiles */
    /* update the concealed tiles. At present we don't animate the
       movement of tiles from hand to exposed sets, so just update */
    if ( m->type == CMsgPlayerSpecialSet ) {
      w = 
	pd->conc[playerdisp_update_concealed(pd,the_game->tile)];
    } else {
      playerdisp_update_concealed(pd,HiddenTile);
    }
    assert(w);
    /* this should force a resize calculation so that 
       the declared tiles are in place. It seems to work. */
    gtk_widget_size_request(pd->widget,NULL);
    gtk_widget_size_allocate(pd->widget,&pd->widget->allocation);
    dests[numanims].target = w;
    srcs[numanims].ori = dests[numanims].ori = eori; 
    get_relative_posn(boardframe,w,&dests[numanims].x,&dests[numanims].y);
    numanims++; /* that's it */

    if ( the_game->state == MahJonging
	 && the_game->player == our_seat 
	 && p == our_player
	 && p->num_concealed > 0 )
      conc_callback(pd->conc[0],(gpointer)(intptr_t)(0 | 128));
  } /* end of the successful claim cases */
  else if ( m->type == CMsgPlayerRobsKong ) {
    GtkWidget *t;
    /* This should be called on the ROBBED player, so we
       can stash away the tile that will be robbed */
    t = playerdisp_update_exposed(pd,m->playerrobskong.tile,1);
    /* if we found the tile, then this is it. Otherwise, we were
       looking at somebody else! */
    if ( t ) robbedfromkong = t;
    updated = 1;
  } /* end of robbing kong */
  /* if not yet handled, just update the tiles */
  if ( ! updated ) {
    playerdisp_update_concealed(pd,HiddenTile);
    playerdisp_update_exposed(pd,HiddenTile,0);
  }
  /* now kick off the animations (or not) */
  while ( numanims-- > 0 ) {
    if ( animate 
      && the_game->active 
      && the_game->state != Dealing ) {
      gtk_widget_unmap(dests[numanims].target);
      start_animation(&srcs[numanims],&dests[numanims]);
    } 
  }
}

/* utility function: given a gdkpixmap, create rotated versions
   and put them in the last three arguments.
*/
static void rotate_pixmap(GdkPixmap *east,
			  GdkPixmap **south,
			  GdkPixmap **west,
			  GdkPixmap **north) {
  static GdkVisual *v = NULL;
  int w=-1, h=-1;
  int x, y;
  GdkImage *im;
  GdkPixmap *p1;
  GdkImage *im1;
  GdkGC *gc;
  
  if ( v == NULL ) {
    v = gdk_window_get_visual(topwindow->window);
  }

  gdk_window_get_size(east,&w,&h);
  /* remember to destroy these */
  im = gdk_image_get(east,0,0,w,h);
  gc = gdk_gc_new(east);

  /* do the south */
  im1 = gdk_image_new(GDK_IMAGE_NORMAL,v,h,w);
  for ( x=0; x < w; x++ )
    for ( y=0; y < h; y++ )
      gdk_image_put_pixel(im1,y,w-1-x,gdk_image_get_pixel(im,x,y));
  p1 = gdk_pixmap_new(NULL,h,w,im->depth);
  gdk_draw_image(p1,gc,im1,0,0,0,0,h,w);
  *south = p1;
  /* do the north */
  for ( x=0; x < w; x++ )
    for ( y=0; y < h; y++ )
      gdk_image_put_pixel(im1,h-1-y,x,gdk_image_get_pixel(im,x,y));
  p1 = gdk_pixmap_new(NULL,h,w,im->depth);
  gdk_draw_image(p1,gc,im1,0,0,0,0,h,w);
  *north = p1;

  /* and the west */
  gdk_image_destroy(im1);
  im1 = gdk_image_new(GDK_IMAGE_NORMAL,v,w,h);
  for ( x=0; x < w; x++ )
    for ( y=0; y < h; y++ )
      gdk_image_put_pixel(im1,w-1-x,h-1-y,gdk_image_get_pixel(im,x,y));
  p1 = gdk_pixmap_new(NULL,w,h,im->depth);
  gdk_draw_image(p1,gc,im1,0,0,0,0,w,h);
  *west = p1;

  /* clean up */
  gdk_image_destroy(im1);
  gdk_image_destroy(im);
  gdk_gc_destroy(gc);
}

/* new animation routines. The previous way was all very object-oriented,
   but not very good: having separate timeouts for every animated tile
   tends to slow things done. So we will instead maintain a global list
   of animations in progress (of which there will be at most five!),
   and have a single timeout that deals with all of them */
/* struct for recording information about an animation in progress */
struct AnimProg {
  GtkWidget *floater; /* the moving window. Set to NULL if this record
			 is not in use */
  gint x1,y1,x2,y2; /* start and end posns relative to either boardframe
		     or root, depending on animate_with_popups */
  gint steps, n; /* total number of steps, number completed */
  GtkWidget *target; /* target widget */
};

struct AnimProg animlist[5];
int animator_running = 0;

/* timeout function that moves all running animations by one step */
static gint discard_animator(gpointer data UNUSED) {
  struct AnimProg *animp;
  int i; int numinprogress = 0;

  for ( i = 0 ; i < 5 ; i++ ) {
    animp = &animlist[i];
    if ( ! animp->floater ) continue;
    if ( animp->n == 0 ) {
      gtk_widget_destroy(animp->floater);
      animp->floater = NULL;
      gtk_widget_show(animp->target);
      gtk_widget_map(animp->target);
    } else {
      numinprogress++;
      animp->n--;
      /* Here we move the widget, *and* we move the window.
	 Why?
	 If we move the widget, gtk doesn't actually move its window,
	 as we've arranged for vlazy_fixed_move not to call
	 queue_resize, so relayout doesn't get done. Thus we have
	 to move the window.
	 However, if we don't move the widget, then GTK uses its
	 original position for calculating event deliveries, resulting
	 in total confusion and leave/enter loop if the pointer is
	 in the button.
	 But if we're using popup windows, we only move the window.
      */
      if ( ! animate_with_popups ) {
	vlazy_fixed_move(VLAZY_FIXED(boardfixed),animp->floater,
	  (animp->n*animp->x1 
	    + (animp->steps-animp->n)*animp->x2)
	  /animp->steps,
	  (animp->n*animp->y1 
	    + (animp->steps-animp->n)*animp->y2)
	  /animp->steps);
      }
      gdk_window_move(animp->floater->window,
	(animp->n*animp->x1 
	  + (animp->steps-animp->n)*animp->x2)
	/animp->steps,
	(animp->n*animp->y1 
	  + (animp->steps-animp->n)*animp->y2)
	/animp->steps);
    }
  }
  if ( numinprogress > 0 ) { return TRUE ; }
  else { animator_running = 0; return FALSE; }
}

/* kick off an animation from the given source and target info */
static void start_animation(AnimInfo *source,AnimInfo *target) {
    GtkWidget *floater,*b;
    int i;
    struct AnimProg *animp;
    GtkWidget *p;

    /* find a free slot in the animation list */
    for ( i = 0 ; animlist[i].floater && i < 5 ; i++ ) ;
    if ( i == 5 ) {
      warn("no room in animation list; not animating");
      gtk_widget_map(target->target);
      return;
    }
    animp = &animlist[i];

    /* create the floating button */
    b = gtk_button_new();
    gtk_widget_set_name(b,"tile");
    gtk_widget_show(b);
    GTK_WIDGET_UNSET_FLAGS(b,GTK_CAN_FOCUS);
    p = gtk_pixmap_new(tilepixmaps[0][HiddenTile],NULL);
    gtk_widget_show(p);
    gtk_container_add(GTK_CONTAINER(b),p);
    button_set_tile_without_tiletip(b,source->t,source->ori);
    if ( animate_with_popups ) {
      floater = gtk_window_new(GTK_WINDOW_POPUP);
      gtk_container_add(GTK_CONTAINER(floater),b);
    } else {
#ifdef GTK2
      /* in GTK2, buttons don't have windows */
      floater = gtk_event_box_new();
      gtk_container_add(GTK_CONTAINER(floater),b);
#else
      floater = b;
#endif
    }
    animp->x1 = source->x;
    animp->x2 = target->x;
    animp->y1 = source->y;
    animp->y2 = target->y;
    if ( animate_with_popups ) {
      int x,y;
      gdk_window_get_deskrelative_origin(boardframe->window,&x,&y);
      animp->x1 += x;
      animp->x2 += x;
      animp->y1 += y;
      animp->y2 += y;
    }
    /* how many steps? the frame interval is 20ms.
       We want it to move across the screen in about a second max.
       So say 1600 pixels in 1 second, i.e. 50 frames.
       However, we don't want it to move instantaneously over short distances,
       so let's say a minimum of 10 frames. Hence
       numframes = 10 + 40 * dist/1600 */
    animp->steps =  10 + floor(hypot(source->x-target->x,source->y-target->y))*40/1600;
    animp->n = animp->steps;
    animp->target = target->target;
    if ( animate_with_popups ) {
      gtk_widget_set_uposition(floater,animp->x1,animp->y1);
    } else {
      vlazy_fixed_put(VLAZY_FIXED(boardfixed),floater,animp->x1,animp->y1);
    }
    gtk_widget_show(floater);
    animp->floater = floater;
    if ( ! animator_running ) {
      animator_running = 1;
      gtk_timeout_add(/* 1000 */ 20 ,discard_animator,(gpointer)NULL);
    }
}

/* local variable used in next two. Tells playerdisp_discard
   whether to compute some information */
static int playerdisp_discard_recompute = 1;

/* playerdisp_clear_discards: clear the discard area for the player */
static void playerdisp_clear_discards(PlayerDisp *pd) {
  int i;

  /* destroy ALL widgets, since there might be widgets after
     the current last discard (and will be, if the last mah jong
     was from the wall */
  for (i=0; i < 32; i++) {
    if ( pd->discards[i] ) {
      gtk_widget_destroy(pd->discards[i]);
      pd->discards[i] = NULL;
    }
  }
  highlittile = NULL;
  pd->num_discards = 0;
  pd->x = pd->y = pd->row = pd->plane = 0;
  for (i=0;i<5;i++) { pd->xmin[i] = 10000; pd->xmax[i] = 0; }
  playerdisp_discard_recompute = 1;
}
    
/* Given a player disp and a tile, display the tile as 
   the player's next discard. If HiddenTile, then withdraw
   the last discard. Also, if pd is NULL: assume pd is the
   last player on which we were called.
   Buttons are created as required.
   Tiles are packed in rows, putting a 1/4 tile spacing between them
   horizontally and vertically. 
   In the spirit of Mah Jong, each player grabs the next available space,
   working away from it; a little cleverness detects clashes.
   Returns an animinfo which, in the normal case, is the position
   of the new discard.
   In the withdrawing case, it's the discard being withdrawn.
*/

static AnimInfo *playerdisp_discard(PlayerDisp *pd,Tile t) {
  static PlayerDisp *last_pd = NULL;
  static gint16 dw,dh; /* width and height of discard area */
  static gint16 bw,bh; /* width and height of tile button in ori 0 */
  static gint16 spacing;
  gint16 w,h; /* width and height of discard area viewed by this player */
  int row,plane; /* which row and plane are we in */
  int i;
  gint16 x,y,xx,yy;
  PlayerDisp *left,*right; /* our left and right neighbours */
  GtkWidget *b;
  int ori, eori;
  static AnimInfo anim;

  if ( pd == NULL ) pd = last_pd;
  last_pd = pd;
  ori = pd->orientation;
  eori = flipori(ori);

  if ( highlittile ) {
#ifdef GTK2
    gtk_widget_modify_bg(highlittile,GTK_STATE_NORMAL,NULL);
#else
    gtk_widget_restore_default_style(highlittile);
#endif
    highlittile = NULL;
  }

  if ( t == HiddenTile ) {
    if ( pd == NULL ) {
      warn("Internal error: playerdisp_discard called to clear null discard");
      return NULL;
    }
    b = pd->discards[--pd->num_discards];
    if ( b == NULL ) {
      warn("Internal error: playerdisp_discard clearing nonexistent button");
      return NULL;
    }
    anim.t = (int)(intptr_t) gtk_object_get_user_data(GTK_OBJECT(b));
    anim.target = b;
    anim.ori = eori;
    get_relative_posn(boardframe,b,&anim.x,&anim.y);
    gtk_widget_hide(b);
    return &anim;
  }

  ori = pd->orientation;
  eori  = flipori(ori);
  b = pd->discards[pd->num_discards];
  left = &pdisps[(ori+3)%4];
  right = &pdisps[(ori+1)%4];

  if ( b == NULL ) {
    GtkWidget *p;
    b = pd->discards[pd->num_discards] = gtk_button_new();
    gtk_widget_set_name(b,"tile");
    GTK_WIDGET_UNSET_FLAGS(b,GTK_CAN_FOCUS);
    p = gtk_pixmap_new(tilepixmaps[0][HiddenTile],NULL);
    gtk_widget_show(p);
    gtk_container_add(GTK_CONTAINER(b),p);
    /* if this is the first time we've ever been called, we need
       to compute some information */
    if ( playerdisp_discard_recompute ) {
      playerdisp_discard_recompute = 0;
      /* This discard area alloc was computed at a known safe time
	 in the main routine */
      dw = discard_area_alloc.width
	- 2 * GTK_CONTAINER(discard_area)->border_width;
      dh = discard_area_alloc.height
	- 2 * GTK_CONTAINER(discard_area)->border_width;
      bw = tile_width;
      bh = tile_height;
      spacing = tile_spacing;
      /* if we're showing the wall, the effective dimensions are
	 is reduced by 2*(tileheight+3*tilespacing)  --
         each wall occupies tile_height+2*tile_spacing, and we
	 also want some space */
      if ( showwall ) {
	dw -= 2*(tile_height+3*tile_spacing);
	dh -= 2*(tile_height+3*tile_spacing);
	/* moreover, the wall might be smaller than the max wall
	   we used to set the discard area */
	dw -= ((MAX_WALL_SIZE-WALL_SIZE)/8)*tile_width;
	dh -= ((MAX_WALL_SIZE-WALL_SIZE)/8)*tile_width;
      }
    }

    /* Now we need to work out where to put this button.
       To make life easier, we will always look at it as if
       we were at the bottom, and then rotate the answer later */
    /* we can't assume the discard area is square, so swap w and h
       if we're working for a side player */
    if ( ori == 0 || ori == 2 ) {
      w = dw; h = dh;
    } else {
      w = dh; h = dw;
    }
    /* get the positions from last time */
    x = pd->x; y = pd->y; row = pd->row; plane = pd->plane;
    /* working in the X coordinate system is too painful.
       We work in a normal Cartesian system and convert
       only at the last moment */
    /* x,y is the pos of the bottom left corner */
    /* repeat until success */
    while ( 1 ) {
      /* Does the current position intrude into the opposite area?
	 If so, we're stuffed, and need to think what to do */
      /* also if we've run out of rows */
      if ( row >= 5 || y+bh > h/2 ) {
	/* start stacking tiles */
	plane++;
	row = 0;
	x = 0 + plane*(bw+spacing)/2; /* two planes is enough, surely! */
	y = 0 + plane*(bh+spacing)/2;
	continue;
      }
      /* Does the current position intrude into the left hand neighbour?
	 The left hand neighbour has stored in its xmax array
	 the rightmost points it's used in each row. So for each of
	 its rows, we see if either our top left or top right intrude
	 on it, and if so we move along */
      for (i=0; i<5; i++) {
	if ( (
	      /* our top left is in the part occupied by the tile */
	      (x >= i*(bh+spacing) && x < i*(bh+spacing)+bh)
	      /* ditto for top right */
	      || (x+bw >= i*(bh+spacing) && x+bw < i*(bh+spacing)+bh) )
	     /* and our top intrudes */
	     && y+bh > h - left->xmax[i] )
	  x = i*(bh+spacing)+bh+spacing/2; /* half space all that's needed, I think */
      }
      
      /* Now see if the current position interferes with the right
	 neighbour similarly 
       */
      for (i=0; i<5; i++) {
	if ( (
	      /* our top left is in the column */
	      (x >= w-(i*(bh+spacing)+bh) && x < w - i*(bh+spacing))
	      /* ditto for top right */
	      || (x+bw >= w-(i*(bh+spacing)+bh) && x+bw < w - i*(bh+spacing)))
	     /* and our top intrudes */
	     && y+bh > right->xmin[i] ) {
	  /* we have to move up to the next row */
	  row++;
	  x = 0;
	  y += (bh+spacing);
	  i=-1; /* signal */
	  break;
	}
      }
      /* and restart the loop if we moved */
      if ( i < 0 ) continue;

      /* Phew, we've found it, if we get here */
      /* store our information for next time */
      pd->row = row;
      pd->plane = plane;
      pd->x = x+bw+spacing;
      pd->y = y;

      if ( x < pd->xmin[row] ) pd->xmin[row] = x;
      if ( x+bw > pd->xmax[row] ) pd->xmax[row] = x+bw;
      break;
    }
    
    /* Now we have to rotate for the appropriate player.
       This means giving a point for the top left corner.
       And switching y round */
    xx = x; yy = y;
    switch ( ori ) {
    case 0: x = xx; y = dh-yy-bh; break;
    case 1: y = dh - xx - bw; x = dw - yy - bh; break;
    case 2: x = dw - xx - bw; y = yy; break;
    case 3: y = xx; x = yy; break;
    }
    /* and if we are showing the wall, we have to shift everything
       down and right by the wall space */
    if ( showwall ) {
	x += (tile_height+3*tile_spacing);
	y += (tile_height+3*tile_spacing);
	/* and adjust for wall size */
	x += ((MAX_WALL_SIZE-WALL_SIZE)/8)*tile_width/2;
	y += ((MAX_WALL_SIZE-WALL_SIZE)/8)*tile_width/2;
    }

    gtk_fixed_put(GTK_FIXED(discard_area),b,x,y);
    pd->dx[pd->num_discards] = x;
    pd->dy[pd->num_discards] = y;
  } else {
    x = pd->dx[pd->num_discards];
    y = pd->dy[pd->num_discards];
  }

  /* now we have a button created and in place: set it */
  button_set_tile(b,t,eori);

  /* now we need to compute the animation info to return */
  /* position is that of the button we've just created */
  /* unfortunately, if we've just created the button, it won't
     actually be in position (since it hasn't been realized),
     so we have to use our calculated position relative to the
     discard area */
  get_relative_posn(boardframe,discard_area,&anim.x,&anim.y);
  anim.x += x; anim.y += y;
  /* and don't forget the border of the discard area */
  anim.x += GTK_CONTAINER(discard_area)->border_width;
  anim.y += GTK_CONTAINER(discard_area)->border_width;
  anim.target = b;
  anim.t = t;
  anim.ori = eori;
  gtk_widget_show(b);
  pd->num_discards++;
  return &anim;
}

/* open_connection: connect to a server and announce,
   using values from the open dialog box where available.
   If data is one, start local server and players.
   If data is 2, load game from file in opengamefiletext.
   If data is 3, reconnect using existing fd in third arg.
   If data is 4, open to host:port in fourth arg, where :NNNN means new port on same host. */
void open_connection(GtkWidget *w UNUSED, gpointer data, int oldfd, char *newaddr) {
  char buf[256], buf2[256];
  int i;
  int mode = (int)(intptr_t)data;
  int toserver, fromserver;
  FILE *serverstream;
  
  databuffer_clear(&server_history);

  if ( mode < 3 || mode == 4 ) {
    if ( GTK_WIDGET_SENSITIVE(openfile) ) {
      sprintf(address,"%s",gtk_entry_get_text(GTK_ENTRY(openfiletext)));
      /* strip any initial spaces */
      while ( address[0] == ' ' ) {
	int jj;
	int ll = strlen(address);
	for (jj=0;jj<ll;jj++) address[jj] = address[jj+1];
      }
      /* if the field is blank, make up something */
      if ( address[0] == 0 ) {
	sprintf(address,"/tmp/mj-%d",getpid());
      }
    } else {
      /* out of kindness we'll strip any trailing spaces */
      char t[256];
      int i;
      t[255] = 0;
      if ( mode == 4 && ! redirected ) strcpy(origaddress,address);
      strmcpy(t,gtk_entry_get_text(GTK_ENTRY(openhosttext)),255);
      for ( i = strlen(t) - 1; isspace(t[i]); i-- ) t[i] = 0;
      if ( mode == 4 && newaddr[0] != ':' ) {
	sscanf(newaddr,"%255[^:]",t);
      }
      if ( mode == 4 ) { newaddr = index(newaddr,':')+1; }
      sprintf(address,"%s:%s",t,(mode == 4) ? newaddr : gtk_entry_get_text(GTK_ENTRY(openporttext)));
    }
    
    if ( mode != 4 ) our_id = atoi(gtk_entry_get_text(GTK_ENTRY(openidtext)));
    strmcpy(name,gtk_entry_get_text(GTK_ENTRY(opennametext)),256);
    for ( i = 0 ; i <= 2; i++ ) {
      strmcpy(robot_names[i],gtk_entry_get_text(GTK_ENTRY(openplayernames[i])),128);
      strmcpy(robot_options[i],gtk_entry_get_text(GTK_ENTRY(openplayeroptions[i])),128);
    }
    
    if ( mode == 4 ) { redirected = 1; }
    if ( mode < 3 ) { redirected = 0; }

    /* save the values */
    read_or_update_rcfile(NULL,XmjrcNone,XmjrcOpen);
    
    /* start server ? */
    if ( mode > 0 && mode < 4) {
      char cmd[1024];
      char ibuf[10];
      
      strcpy(cmd,"mj-server --id-order-seats --server ");
      strmcat(cmd,address,sizeof(cmd)-strlen(cmd));
      if ( ! GTK_TOGGLE_BUTTON(openallowdisconnectbutton)->active ) {
	strmcat(cmd, " --exit-on-disconnect",sizeof(cmd)-strlen(cmd));
      }
      if ( GTK_TOGGLE_BUTTON(opensaveonexitbutton)->active ) {
	strmcat(cmd, " --save-on-exit",sizeof(cmd)-strlen(cmd));
      }
      if ( GTK_TOGGLE_BUTTON(openrandomseatsbutton)->active ) {
	strmcat(cmd, " --random-seats",sizeof(cmd)-strlen(cmd));
      }
      if ( mode == 1 ) {
	/* if resuming a game, timeout was set initially */
	gtk_spin_button_update(GTK_SPIN_BUTTON(opentimeoutspinbutton));
	strcat(cmd, " --timeout ");
	sprintf(ibuf,"%d",
		gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(opentimeoutspinbutton)));
	strmcat(cmd,ibuf,sizeof(cmd)-strlen(cmd));
      }
      if ( mode == 2 ) {
	const char *gfile; FILE *foo;
	/* if we're restarting a game, allow the robots to take over
	   the previous players */
	strmcat(cmd, " --no-id-required",sizeof(cmd)-strlen(cmd));
	gfile = gtk_entry_get_text(GTK_ENTRY(opengamefiletext));
	/* check that we can actually read the file */
	if ( !gfile || !(foo = fopen(gfile,"r")) ) {
	  char rdmsg[] = "Can't read game file: ";
	  sprintf(buf,rdmsg);
	  strmcat(buf,strerror(errno),sizeof(buf)-sizeof(rdmsg));
	  error_dialog_popup(buf);
	  return;
	}
	fclose(foo);
	strmcat(cmd, " --load-game ",sizeof(cmd)-strlen(cmd));
	strmcat(cmd, gfile,sizeof(cmd)-strlen(cmd));
      }
      if ( ! start_background_program_with_io(cmd,&toserver,&fromserver) ) {
	error_dialog_popup("Couldn't start server; unexpected failure");
	return;
      }
      close(toserver);
      serverstream = fdopen(fromserver,"r");
      fgets(buf2,255,serverstream);
      /* if the server managed to open its socket, the first
	 thing it writes to stdout is 
	 OK: server-address */
      if ( strncmp(buf2,"OK",2) != 0 ) {
	error_dialog_popup("server failed to start");
	return;
      }
    }
  }

  if ( mode == 3 ) {
    the_game = client_reinit(oldfd);
  } else {
    the_game = client_init(address);
  }

  if ( the_game == NULL ) {
    sprintf(buf,"Couldn't connect to server:\n%s",strerror(errno));
    if ( data ) {
      strcat(buf,"\nPerhaps server didn't start -- check error messages");
    }
    error_dialog_popup(buf);
    return;
  }
  the_game->userdata = malloc(sizeof(GameExtras));
  if ( the_game->userdata == NULL ) {
    warn("Fatal error: couldn't malloc game extra data");
    exit(1);
  }
  our_player = the_game->players[0];

  gtk_widget_set_sensitive(openmenuentry,0);
  gtk_widget_set_sensitive(newgamemenuentry,0);
  gtk_widget_set_sensitive(resumegamemenuentry,0);
  gtk_widget_set_sensitive(savemenuentry,1);
  gtk_widget_set_sensitive(saveasmenuentry,1);
  gtk_widget_set_sensitive(closemenuentry,1);
  close_saving_posn(open_dialog);

  control_server_processing(1);
  if ( monitor ) {
    // do everything from client_connect except send packet. Yech.
    initialize_player(the_game->players[0]);
    set_player_id(the_game->players[0],our_id);
    set_player_name(the_game->players[0],name);
  } else {
    client_connect(the_game,our_id,name);
  }

  if ( mode == 1 || mode == 2 ) {
    for ( i = 0 ; i < 3 ; i++ ) {
      if ( GTK_TOGGLE_BUTTON(openplayercheckboxes[i])->active ) {
	char cmd[1024];
	strcpy(cmd,"mj-player --server ");
	strmcat(cmd,address,sizeof(cmd)-strlen(cmd));
	sprintf(cmd+strlen(cmd)," --id %d ",i+2);
	strmcat(cmd," ",sizeof(cmd)-strlen(cmd));
	if ( robot_names[i][0] ) {
	  strmcat(cmd," --name ",sizeof(cmd)-strlen(cmd));
	  qstrmcat(cmd,robot_names[i],sizeof(cmd)-strlen(cmd));
	}
	if ( robot_options[i][0] ) {
	  strmcat(cmd," ",sizeof(cmd)-strlen(cmd));
	  /* options are not quoted, just added as is */
	  strmcat(cmd,robot_options[i],sizeof(cmd)-strlen(cmd));
	}
	if ( ! start_background_program(cmd) ) {
	  error_dialog_popup("Unexpected error starting player");
	  return;
	}
	if ( i < 2 ) usleep(250*1000);
      }
    }
  }
}

/* shut down the connection - unless arg is given, in which case
   do everything except actually closing the connection */
void close_connection(int keepalive) {
  int i;
  game_over = 0; /* cos there is now no game to be over */
  control_server_processing(0);
  free(the_game->userdata);
  if ( keepalive ) 
    client_close_keepconnection(the_game);
  else
    client_close(the_game);
  the_game = NULL;
  gtk_widget_set_sensitive(openmenuentry,1);
  gtk_widget_set_sensitive(newgamemenuentry,1);
  gtk_widget_set_sensitive(resumegamemenuentry,1);
  gtk_widget_set_sensitive(savemenuentry,0);
  gtk_widget_set_sensitive(saveasmenuentry,0);
  gtk_widget_set_sensitive(closemenuentry,0);
  /* clear display */
  for ( i=0; i < 4; i++ ) {
    pdisps[i].player = NULL;
    playerdisp_update(&pdisps[i],0);
    playerdisp_clear_discards(&pdisps[i]);
  }
  clear_wall();
  /* and pop down irrelevant dialogs that are up */
  gtk_widget_hide(chow_dialog);
  gtk_widget_hide(ds_dialog);
  gtk_widget_hide(discard_dialog->widget);
  gtk_widget_hide(continue_dialog);
  gtk_widget_hide(turn_dialog);
  gtk_widget_hide(scoring_dialog);
  /* zap the game option dialog */
  game_option_panel = build_or_refresh_option_panel(NULL,game_option_panel);
  gtk_widget_set_sensitive(gameoptionsmenuentry,0);
}

/* convenience function: given a widget w and a descendant z, 
   give the position of z relative to w.
   To make sense of this function, one needs to know the insane
   conventions of gtk, which of course are entirely undocumented.
   To wit, the allocation of a widget is measured relative to its
   window, i.e. the window of the nearest ancestor widget that has one.
   The origin viewed *externally* of a widget is the top left of its
   border; the origin viewed *internally* is the top of the non-border.
   Returns true on success, false on failure */
static int get_relative_posn_aux(GtkWidget *w, GtkWidget *z, int *x, int *y) {
  GtkWidget *target = z;

  /* base case */
  if ( z == w ) {
    /* I don't think this should ever happen, unless we actually
       call the main function with z == w. Let's check */
    assert(0);
    return 1;
  }
  /* find the nearest ancestor of the target widget with a window */
  z = z->parent;
  while ( z && z != w && GTK_WIDGET_NO_WINDOW(z) ) z = z->parent;
  /* if z has a window, then the target position relative to
     z is its allocation */
  if ( ! z ) {
    /* something wrong */
    /* whoops. Original z wasn't a child of w */
    warn("get_relative_posn: z is not a descendant of w");
    return 0;
  } else if ( z == w ) {
    /* the position of the target is its allocation minus the
       allocation of w, if w is not a window; */
    *x += target->allocation.x;
    *y += target->allocation.y;
    if ( GTK_WIDGET_NO_WINDOW(w) ) {
      *x -= w->allocation.x;
      *y -= w->allocation.y;
      /* and minus the borderwidth of w */
      if ( GTK_IS_CONTAINER(w) ) {
	*x -= GTK_CONTAINER(w)->border_width;
	*y -= GTK_CONTAINER(w)->border_width;
      }
    }
    /* and that's it */
    return 1;
  } else if ( !GTK_WIDGET_NO_WINDOW(z) ) {
    *x += target->allocation.x;
    *y += target->allocation.y;
  } 
  /* if we get here, z is an ancestor window, but a descendant of
     w. So we have to pass through it.
     Previously, I thought we had to add in z's borderwidth, but
     this appears not to be the case. */
  /* now recurse */
  return get_relative_posn_aux(w,z,x,y);
}
  
static int get_relative_posn(GtkWidget *w, GtkWidget *z, int *x, int *y) {
#ifdef GTK2
  return gtk_widget_translate_coordinates(z,w,0,0,x,y);
#endif
  *x = 0; *y = 0; return ((w == z) || get_relative_posn_aux(w,z,x,y));
}
  
/* utility function for wall display: given the tile in 
   wall  ori, stack j, and top layer (k = 0), bottom layer (k = 1)
   or loose tile (k = -1), return its x and y coordinates in the
   discard area.
   If the isdead argument is one, the tile is to be displaced as
   in the dead wall. This assumes that the board widget has a border
   into which we can shift the wall a little bit.
*/

static void wall_posn(int ori, int j, int k, int isdead, int *xp, int *yp) {
  int x=0, y=0;
  /* calculate basic position with respect to discard area */
  /* because there are possibly three stacks (the two layers
     and the loose tiles), which we want to display in a
     formalized perspective, by displacing layers by 
     tile_spacing from each other, each wall occupies 
     tile_height + 2*tile_spacing in the forward direction */
  switch (ori) {
  case 0:
    x = ((WALL_SIZE/4)/2 - (j+1))*tile_width +tile_spacing;
    y = (WALL_SIZE/4)/2 * tile_width  + (k+1)*tile_spacing ;
    break;
  case 1:
    x = (WALL_SIZE/4)/2 * tile_width + (-k)*tile_spacing + 2*tile_spacing;
    y = tile_height + 2*tile_spacing + j*tile_width;
    break;
  case 2:
    x = tile_height + 2*tile_spacing + j*tile_width;
    y = (k+1)*tile_spacing;
    break;
  case 3:
    x = (k+1)*tile_spacing;
    y = ((WALL_SIZE/4)/2 - (j+1))*tile_width + tile_spacing;
    break;
  }
  /* the board has been sized assuming MAX_WALL_SIZE; so if we
     are playing with a smaller wall, we should centre the walls */
  x += tile_width*((MAX_WALL_SIZE - WALL_SIZE)/8)/2;
  y += tile_width*((MAX_WALL_SIZE - WALL_SIZE)/8)/2;
  if ( isdead ) {
    switch ( wall_ori(wall_game_to_board(gextras(the_game)->orig_live_end)) ) {
    case 0: x -= tile_spacing; break;
    case 1: y += tile_spacing; break;
    case 2: x += tile_spacing; break;
    case 3: y -= tile_spacing; break;
    }
  }
  *xp = x; *yp = y;
}

static void clear_wall(void) {
  int i;
  /* destroy all existing widgets */
  for ( i=0; i<MAX_WALL_SIZE; i++ ) {
    if ( wall[i] ) gtk_widget_destroy(wall[i]);
    wall[i] = NULL;
  }
}

/* create a new wall */
static void create_wall(void) {
  int i,j,k,n,ori,x,y;
  GtkWidget *w;
  GtkWidget *pm;

  clear_wall();
  /* create buttons and place in discard area */
  for ( i = 0; i < 4; i++ ) {
    for ( j = 0; j < (WALL_SIZE/4)/2 ; j++ ) {
      /* j is stack within wall */
      /* 0 is top, 1 is bottom */
      for (k=1; k >= 0 ; k-- ) {
	n = i*(WALL_SIZE/4)+(2*j)+k;
	ori = wall_ori(n);
	wall_posn(ori,j,k,the_game 
		  && wall_board_to_game(n) >= the_game->wall.live_end,&x,&y);
	w = gtk_button_new();
	gtk_widget_set_name(w,"tile");
	GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
	pm = gtk_pixmap_new(tilepixmaps[ori][HiddenTile],NULL);
	gtk_widget_show(pm);
	gtk_container_add(GTK_CONTAINER(w),pm);
	button_set_tile(w,HiddenTile,ori);
	gtk_widget_show(w);
	gtk_fixed_put(GTK_FIXED(discard_area),w,x,y);
	wall[n] = w;
      }
    }
  }
}

/* adjust the loose tiles.
   The argument is the tile that *has just* been drawn, which
   is normally  the_game->wall.dead_end */
static AnimInfo *adjust_wall_loose (int tile_drawn) {
  int i,n,m,ori,j,x,y;
  AnimInfo *retval = NULL;
  static AnimInfo anim;

  i = wall_game_to_board(tile_drawn);
  if ( tile_drawn < the_game->wall.size ) {
    /* which tile gets drawn? If dead_end was even before
       (i.e. it's now odd), the tile drawn was the 2nd to last;
       otherwise it's the last */
    if ( tile_drawn%2 == 1 ) {
      /* tile drawn was 2nd to last */
      i = wall_game_to_board(tile_drawn-1);
    } else {
      /* tile drawn was actually beyond the then dead end */
      i = wall_game_to_board(tile_drawn+1);
    }
    assert(i < the_game->wall.size && wall[i]);
    anim.target = NULL;
    anim.t = HiddenTile;
    anim.ori = wall_ori(i);
    get_relative_posn(boardframe,wall[i],
		      &anim.x,&anim.y);
    retval = &anim;
    gtk_widget_destroy(wall[i]); 
    wall[i] = NULL;
    /* if we are playing with a 14 tile wall, and the dead wall
       is now 14 again, then move two tiles from the live wall
       into the dead wall. */
    if ( !game_get_option_value(the_game,GODeadWall16,NULL).optbool 
	 && game_get_option_value(the_game,GODeadWall,NULL).optbool
	 && (tile_drawn-the_game->wall.live_end == 14) ) {
      /* the bottom tile that gets moved: now second tile in dead wall */
      n = wall_game_to_board(the_game->wall.live_end+1);
      /* if tile exists, reposition */
      if ( the_game->wall.live_used < the_game->wall.live_end+1 ) {
	ori = wall_ori(n);
	j = (n - (the_game->wall.size/4)*(n/(the_game->wall.size/4)))/2;
	wall_posn(ori,j,1,1,&x,&y);
	gtk_fixed_move(GTK_FIXED(discard_area),wall[n],x,y);
      }
      /* and the top tile */
      n = wall_game_to_board(the_game->wall.live_end);
      /* if tile exists, reposition */
      if ( the_game->wall.live_used < the_game->wall.live_end ) {
	ori = wall_ori(n);
	j = (n - (the_game->wall.size/4)*(n/(the_game->wall.size/4)))/2;
	wall_posn(ori,j,0,1,&x,&y);
	gtk_fixed_move(GTK_FIXED(discard_area),wall[n],x,y);
      }
    }
  }
  /* if the number of loose tiles remaining is even, move the
     last two into position */
  if ( tile_drawn%2 == 0 ) {
    /* number of the top loose tile */
    n = wall_game_to_board(tile_drawn-2);
    /* it gets moved back three stacks */
    m = ((n-6)+the_game->wall.size)%the_game->wall.size;
    ori = wall_ori(m);
    j = (m - (the_game->wall.size/4)*(m/(the_game->wall.size/4)))/2;
    wall_posn(ori,j,-1,game_get_option_value(the_game,GODeadWall,NULL).optbool && 1,&x,&y);
    gtk_fixed_move(GTK_FIXED(discard_area),wall[n],x,y);
    gdk_window_raise(wall[n]->window);
    button_set_tile(wall[n],HiddenTile,ori);
    /* and now the bottom tile, gets moved back one stack */
    n = wall_game_to_board(tile_drawn-1);
    m = ((n-2)+the_game->wall.size)%the_game->wall.size;
    ori = wall_ori(m);
    j = (m - (the_game->wall.size/4)*(m/(the_game->wall.size/4)))/2;
    wall_posn(ori,j,-1,game_get_option_value(the_game,GODeadWall,NULL).optbool && 1,&x,&y);
    gtk_fixed_move(GTK_FIXED(discard_area),wall[n],x,y);
    gdk_window_raise(wall[n]->window);
    button_set_tile(wall[n],HiddenTile,ori);
  }
  return retval;
}


/* check_claim_window: see whether a claim window should be
   popped down. This calls itself as a timeout, hence the prototype.
   If a claim window refers to an old discard, or we are in state
   handcomplete, pop it down unconditionally.
   Otherwise, if it's more than two seconds old, and we're not
   in the state Discarded, pop it down; if we're not in the discarded
   state, but it's less than two seconds old, check again later.
*/
static gint check_claim_window(gpointer data) {
  PlayerDisp *pd = data;

  /* Unconditional popdown */
  if ( pd->claim_serial < the_game->serial
       || the_game->state == HandComplete ) {
    gtk_widget_hide(pd->claimw);
    pd->claim_serial = -1;
    pd->claim_time = -10000;
  } else if ( the_game->state != Discarded ) {
    int interval = now_time() - pd->claim_time;
    if ( interval > 2000 ) {
      /* pop down now */
      gtk_widget_hide(pd->claimw);
      pd->claim_serial = -1;
      pd->claim_time = -10000;
    } else {
      /* schedule a time out to check this window again when
	 the two seconds have expired */
      gtk_timeout_add(interval+100,check_claim_window,(gpointer)pd);
    }
  }
  return FALSE; /* don't run *this* timeout again! */
}

/* read_rcfile: given a file name, parse it as an rcfile */
/* at the moment, this just copies options, but doesn't act
   on them. This should be fixed for the display options at least.
   The arguments say which options are to be read or updated,
   using groups defined by the XmjrcGroup enum flag bits.
   It is an error both to read and update a group.
   */
int read_or_update_rcfile(char *rcfile,
  XmjrcGroup read_groups,
  XmjrcGroup update_groups)
{
  char inbuf[1024];
  char rcfbuf[1024];
  char nrcfile[1024];
  char *inptr;
  int i;
  int usingrc = 0;
  FILE *rcf = NULL;
  FILE *ncf = NULL;

# ifndef WIN32
  const char *nl = "\n";
# else
  const char *nl = "\r\n";
# endif

  /* sanity check */
  if ( read_groups & update_groups ) {
    warn("read_or_update_rcfile called with simultaneous read and update");
    return 0;
  }

  if ( ! rcfile ) {
    char *hd = get_homedir();
    if ( hd == NULL ) {
#   ifdef WIN32
      // no home dir. If we can write into the execution directory
      // use that. Otherwise, use root.
      if ( access(".",W_OK) ) {
	info("no home directory, using execution directory");
	hd = ".";
      } else {
	info("no home directory, can't write exec directory, using root");
	hd = "C:";
      }
#   else
      // no homedir, just use tmp
      info("no home diretory, using /tmp");
      hd = "/tmp";
#   endif
    }
    strmcpy(rcfbuf,hd,sizeof(rcfbuf));  // $HOME can trigger overflow...
#   ifdef WIN32
    strmcat(rcfbuf,"/xmj.ini",sizeof(rcfbuf)-strlen(rcfbuf));
#   else
    strmcat(rcfbuf,"/.xmjrc",sizeof(rcfbuf)-strlen(rcfbuf));
#   endif
    rcfile = rcfbuf;
    usingrc = 1; /* not an error not to find the standard rc file */
  }
  rcf = fopen(rcfile,"r");
  // on Windows, if this failed on the standard file,
  // look in the root, in case there's an old file there.
  // If that fails, look in the exec directory, in case there's
  // an ini file shipped with the distribution.
# ifdef WIN32
  if ( !rcf && usingrc ) {
    rcf = fopen("C:/xmj.ini","r");
    if ( rcf ) {
      info("Found old xmj.ini in root directory");
    } else if ( (rcf = fopen("./xmj.ini","r")) ) {
      info("Found old/system xmj.ini in exec directory");
    }
  }
# endif
  if ( ! rcf && ! usingrc ) {
    warn("Couldn't open rcfile %s: %s",rcfile,strerror(errno));
    return 0;
  }
  if ( update_groups ) {
    strmcpy(nrcfile,rcfile,1020);
#   ifdef WIN32
    /* I don't know whether this helps on Windows ME */
    if ( strcmp(nrcfile + strlen(nrcfile) - 4, ".ini") == 0 ) {
      strcpy(nrcfile + strlen(nrcfile) - 4,".new");
    } else {
      strcat(nrcfile,".new");
    }
#   else
    strcat(nrcfile,".new");
#   endif
    ncf = fopen(nrcfile,"w");
    if ( ! ncf ) {
      warn("Couldn't open new rcfile %s: %s",nrcfile,strerror(errno));
      if ( rcf ) fclose(rcf);
      return 0;
    }
  }
  while ( rcf && ! feof(rcf) ) {
    if ( ! fgets(inbuf,1024,rcf) ) {
      if ( feof(rcf) ) break;
      warn("Unexpected error from fgets: %s",strerror(errno));
      fclose(rcf);
      return 0;
    }
    inptr = inbuf;
    while ( isspace(*inptr) ) inptr++;
    if ( *inptr == '#' ) {
      ; /* comment line */
    } else if ( ( (strncmp(inptr,"XmjOption",9) == 0) && (inptr += 9) )
		|| ( (strncmp(inptr,"Display",7) == 0) && (inptr += 7) ) ) {
      /* if updating display, don't even copy this, since
	 we'll be writing it from UI later */
      if ( update_groups & XmjrcDisplay ) continue;
      /* if not reading display, skip */
      if ( read_groups & XmjrcDisplay ) {
	while ( isspace(*inptr) ) inptr++;
	if ( strncmp(inptr,"Animate",7) == 0 ) {
	  inptr += 7;
	  sscanf(inptr,"%d",&animate);
	} else if ( strncmp(inptr,"DialogPosition",14) == 0 ) {
	  inptr += 14;
	  while ( isspace(*inptr) ) inptr++;
	  if ( strncmp(inptr,"central",7) == 0 ) {
	    dialogs_position = DialogsCentral;
	  } else if ( strncmp(inptr,"below",5) == 0 ) {
	    dialogs_position = DialogsBelow;
	  } else if ( strncmp(inptr,"popup",5) == 0 ) {
	    dialogs_position = DialogsPopup;
	  } else {
	    warn("unknown argument for Display DialogPosition: %s",
		 inptr);
	  }
	} else if ( strncmp(inptr,"NoPopups",8) == 0 ) {
	  inptr += 8;
	  sscanf(inptr,"%d",&nopopups);
	} else if ( strncmp(inptr,"Tiletips",8) == 0 ) {
	  inptr += 8;
	  sscanf(inptr,"%d",&tiletips);
	} else if ( strncmp(inptr,"IconifyDialogs",14) == 0 ) {
	  inptr += 14;
	  sscanf(inptr,"%d",&iconify_dialogs_with_main);
	} else if ( strncmp(inptr,"InfoInMain",10) == 0 ) {
	  inptr += 10;
	  sscanf(inptr,"%d",&info_windows_in_main);
	} else if ( strncmp(inptr,"MainFont",8) == 0 ) {
	  inptr += 8;
	  while ( isspace(*inptr) ) inptr++;
	  sscanf(inptr,"%255[^\r\n]",main_font_name);
	} else if ( strncmp(inptr,"TextFont",8) == 0 ) {
	  inptr += 8;
	  while ( isspace(*inptr) ) inptr++;
	  sscanf(inptr,"%255[^\r\n]",text_font_name);
	} else if ( strncmp(inptr,"TableColour",11) == 0 ) {
	  inptr += 11;
	  while ( isspace(*inptr) ) inptr++;
	  sscanf(inptr,"%255[^\r\n]",table_colour_name);
#ifdef GTK2
	  /* handle colour name formats from gtk1 versions,
	     but not vice versa */
	  if ( strncmp("rgb:",table_colour_name,4) == 0 ) {
	    char* p = table_colour_name+4;
	    char buf[256];
	    buf[0] = '#';
	    int j=1;
	    while ( *p && (*p != '/' || p++)) buf[j++] = *(p++);
	    buf[j] = 0;
	    strcpy(table_colour_name,buf);
	  }
#endif
	} else if ( strncmp(inptr,"ShowWall",8) == 0 ) {
	  inptr += 8;
	  while ( isspace(*inptr) ) inptr++;
	  if ( strncmp(inptr,"always",6) == 0 ) {
	    pref_showwall = 1;
	  } else if ( strncmp(inptr,"when-room",9) == 0 ) {
	    pref_showwall = -1;
	  } else if ( strncmp(inptr,"never",5) == 0 ) {
	    pref_showwall = 0;
	  } else {
	    warn("unknown argument for Display ShowWall: %s",
		 inptr);
	  }
	} else if ( strncmp(inptr,"Size",4) == 0) {
	  inptr += 4;
	  sscanf(inptr,"%d",&display_size);
	} else if ( strncmp(inptr,"SortTiles",9) == 0) {
	  inptr += 9;
	  while ( isspace(*inptr) ) inptr++;
	  if ( strncmp(inptr,"always",6) == 0 ) {
	    sort_tiles = SortAlways;
	  } else if ( strncmp(inptr,"deal",4) == 0 ) {
	    sort_tiles = SortDeal;
	  } else if ( strncmp(inptr,"never",5) == 0 ) {
	    sort_tiles = SortNever;
	  } else {
	    warn("unknown argument for Display SortTiles: %s",
	      inptr);
	  }
	  /* N.B. This has to be tested before Tileset ! */
	} else if ( strncmp(inptr,"TilesetPath",11) == 0 ) {
	  inptr += 11;
	  while ( isspace(*inptr) ) inptr++;
	  tileset_path = malloc(strlen(inptr+1));
	  /* FIXME: memory leak */
	  if ( ! tileset_path ) {
	    warn("out of memory");
	    exit(1);
	  }
	  sscanf(inptr,"%[^\r\n]",tileset_path);
	} else if ( strncmp(inptr,"Tileset",7) == 0 ) {
	  inptr += 7;
	  while ( isspace(*inptr) ) inptr++;
	  tileset = malloc(strlen(inptr+1));
	  /* FIXME: memory leak */
	  if ( ! tileset ) {
	    warn("out of memory");
	    exit(1);
	  }
	  sscanf(inptr,"%[^\r\n]",tileset);
	} else if ( strncmp(inptr,"RobotName",9) == 0 ) {
	  /* This is obsoleted -- it's been moved to the
	     OpenDialog settings. We won't take any notice
	     of it, but we won't complain about it either. */
#ifdef GTK2
	} else if ( strncmp(inptr,"Gtk2Rcfile",10) == 0 ) {
	  inptr += 10;
	  while ( isspace(*inptr) ) inptr++;
	  char *buf =  malloc(strlen(inptr+1));
	  sscanf(inptr,"%[^\r\n]",buf);
	  strmcpy(gtk2_rcfile,buf,sizeof(gtk2_rcfile));
	  free(buf);
	} else if ( strncmp(inptr,"UseSystemGtkrc",14) == 0 ) {
	  inptr += 14;
	  sscanf(inptr,"%d",&use_system_gtkrc);
#endif
	} else {
	  warn("unknown Display: %s",inptr);
	}
      }
    } else if ( strncmp(inptr,"GameOption",10) == 0 ) {
      CMsgGameOptionMsg *gom;
      if ( update_groups & XmjrcGame ) continue;
      if ( read_groups & XmjrcGame ) {
	gom = (CMsgGameOptionMsg *)decode_cmsg(inptr);
	if ( gom ) {
	  game_set_option_in_table(&prefs_table,&gom->optentry);
	} else {
	  warn("Error reading game option from rcfile; deleting option");
	}
      }
    } else if ( strncmp(inptr,"CompletedGames",14) == 0 ) {
      if ( update_groups & XmjrcMisc ) continue;
      if ( read_groups & XmjrcMisc ) {
	inptr += 14;
	sscanf(inptr,"%d",&completed_games);
      }
    } else if ( strncmp(inptr,"NagDate",7) == 0 ) {
      if ( update_groups & XmjrcMisc ) continue;
      if ( read_groups & XmjrcMisc ) {
	inptr += 7;
	/* there's no portable way to scan a time_t ... */
	long long ttt;
	sscanf(inptr,"%lld",&ttt);
	nag_date = (time_t)ttt;
      }
    } else if ( strncmp(inptr,"NagState",8) == 0 ) {
      if ( update_groups & XmjrcMisc ) continue;
      if ( read_groups & XmjrcMisc ) {
	inptr += 8;
	sscanf(inptr,"%d",&nag_state);
      }
    } else if ( strncmp(inptr,"DebugReports",12) == 0 ) {
      if ( update_groups & XmjrcMisc ) continue;
      if ( read_groups & XmjrcMisc ) {
	inptr += 12;
	while ( isspace(*inptr) ) inptr++;
	if ( strncmp(inptr,"never",5) == 0 ) {
	  debug_reports = DebugReportsNever;
	} else if ( strncmp(inptr,"ask",3) == 0 ) {
	  debug_reports = DebugReportsAsk;
	} else if ( strncmp(inptr,"always",6) == 0 ) {
	  debug_reports = DebugReportsAlways;
	} else {
	  warn("unknown DebugReports value %s",inptr);
	}
      }
    } else if ( strncmp(inptr,"OpenDialog",10) == 0 ) {
      inptr += 10;
      if ( update_groups & XmjrcOpen ) continue;
      if ( read_groups & XmjrcOpen ) {
	while ( isspace(*inptr) ) inptr++;
	if ( strncmp(inptr,"Address",7) == 0 ) {
	  inptr += 7;
	  sscanf(inptr,"%255s",address);
	} else if ( strncmp(inptr,"Name",4) == 0 ) {
	  inptr += 4;
	  sscanf(inptr," %255[^\r\n]",name);
	} else if ( strncmp(inptr,"RobotName",9) == 0 ) {
	  int i;
	  char buf[128];
	  buf[127] = 0;
	  inptr += 9;
	  sscanf(inptr,"%d %127[^\r\n]",&i,buf);
	  if ( i < 2 || i > 4 ) {
	    warn("bad robot argument to OpenDialog RobotName");
	  } else {
	    strcpy(robot_names[i-2],buf);
	  }
	} else if ( strncmp(inptr,"RobotOptions",12) == 0 ) {
	  int i;
	  char buf[128];
	  buf[127] = 0;
	  inptr += 12;
	  sscanf(inptr,"%d %127[^\r\n]",&i,buf);
	  if ( i < 2 || i > 4 ) {
	    warn("bad robot argument to OpenDialog RobotOptions");
	  } else {
	    strcpy(robot_options[i-2],buf);
	  }
	}
      }
    } else if ( strncmp(inptr,"Playing",7) == 0 ) {
      inptr += 7;
      if ( update_groups & XmjrcPlaying ) continue;
      if ( read_groups & XmjrcPlaying ) {
	while ( isspace(*inptr) ) inptr++;
	if ( strncmp(inptr,"AutoSpecials",12) == 0 ) {
	  inptr += 12;
	  sscanf(inptr,"%d",&playing_auto_declare_specials);
	} else if ( strncmp(inptr,"AutoLosing",10) == 0 ) {
	  inptr += 10;
	  sscanf(inptr,"%d",&playing_auto_declare_losing);
	} else if ( strncmp(inptr,"AutoWinning",11) == 0 ) {
	  inptr += 11;
	  sscanf(inptr,"%d",&playing_auto_declare_winning);
	}
      }
    } else {
      warn("unknown setting in rcfile %s: %s",rcfile,inptr);
    }
    if ( update_groups ) {
      if ( fputs(inbuf,ncf) == EOF ) {
	warn("Error writing to new rcfile %s: %s",nrcfile,strerror(errno));
	fclose(rcf); fclose(ncf);
	return 0;
      }
    }
  }
  if ( rcf ) fclose(rcf);
  if ( update_groups & XmjrcDisplay ) {
    fprintf(ncf,"Display Animate %d%s",animate,nl);
    fprintf(ncf,"Display DialogPosition %s%s",
		  (dialogs_position == DialogsBelow)
		  ? "below" :
		  (dialogs_position == DialogsPopup)
		  ? "popup" :
		  "central", nl);
    fprintf(ncf,"Display NoPopups %d%s",nopopups,nl);
    fprintf(ncf,"Display Tiletips %d%s",tiletips,nl);
    fprintf(ncf,"Display IconifyDialogs %d%s",iconify_dialogs_with_main,nl);
    fprintf(ncf,"Display InfoInMain %d%s",info_windows_in_main,nl);
    if ( main_font_name[0] ) {
      fprintf(ncf,"Display MainFont %s%s",main_font_name,nl);
    }
    if ( text_font_name[0] ) {
      fprintf(ncf,"Display TextFont %s%s",text_font_name,nl);
    }
    if ( table_colour_name[0] ) {
      fprintf(ncf,"Display TableColour %s%s",table_colour_name,nl);
    }
    fprintf(ncf,"Display ShowWall %s%s",
	    pref_showwall == 1 ? "always" :
	    pref_showwall == 0 ? "never" :
	    "when-room", nl);
    if ( display_size ) {
      fprintf(ncf,"Display Size %d%s",display_size,nl);
    }
    static const char *stlist[] = { "always", "deal", "never" };
    fprintf(ncf,"Display SortTiles %s%s",stlist[sort_tiles],nl);
    if ( tileset && tileset[0] ) 
      fprintf(ncf,"Display Tileset %s%s",tileset,nl);
    if ( tileset_path && tileset_path[0] )
      fprintf(ncf,"Display TilesetPath %s%s",tileset_path,nl);
#ifdef GTK2
    if ( gtk2_rcfile[0] ) {
      fprintf(ncf,"Display Gtk2Rcfile %s%s",gtk2_rcfile,nl);
    }
    if ( use_system_gtkrc ) {
      fprintf(ncf,"Display UseSystemGtkrc %d%s",use_system_gtkrc,nl);
    }
#endif
  }
  if ( update_groups & XmjrcGame ) {
    char *pline;
    CMsgGameOptionMsg gom;
    gom.type = CMsgGameOption;
    gom.id = 0;
    for ( i = 0 ; i < prefs_table.numoptions ; i++ ) {
      if ( ! prefs_table.options[i].enabled ) continue;
      gom.optentry = prefs_table.options[i];
      pline = encode_cmsg((CMsgMsg *)&gom);
#     ifndef WIN32
      pline[strlen(pline)-1] = 0;
      pline[strlen(pline)-1] = '\n';
#     endif
      fprintf(ncf,"%s",pline);
    }
  }
  if ( update_groups & XmjrcMisc ) {
    fprintf(ncf,"CompletedGames %d%s",completed_games,nl);
    fprintf(ncf,"NagState %d%s",nag_state,nl);
    /* no portable way to deal with time_t */
    long long ttt = nag_date;
    fprintf(ncf,"NagDate %lld%s",ttt,nl);
    fprintf(ncf,"DebugReports %s%s",
      debug_reports == DebugReportsNever ? "never" :
      debug_reports == DebugReportsAsk ? "ask" :
      debug_reports == DebugReportsAlways ? "always" :
      "unspecified",nl);
  }
  if ( update_groups & XmjrcOpen ) {
    /* don't save if it's the default */
    if ( strcmp(redirected ? origaddress : address,"localhost:5000") != 0 ) {
      fprintf(ncf,"OpenDialog Address %s%s",redirected ? origaddress : address,nl);
    }
    /* don't save name if it's empty */
    if ( name[0] ) {
      fprintf(ncf,"OpenDialog Name %s%s",name,nl);
    }
    for ( i = 0; i < 3; i++ ) {
      if ( robot_names[i][0] ) 
	fprintf(ncf,"OpenDialog RobotName %d %s%s",i+2,robot_names[i],nl);
      if ( robot_options[i][0] ) 
	fprintf(ncf,"OpenDialog RobotOptions %d %s%s",i+2,robot_options[i],nl);
    }
  }
  if ( update_groups & XmjrcPlaying ) {
    fprintf(ncf,"Playing AutoSpecials %d%s",playing_auto_declare_specials,nl);
    fprintf(ncf,"Playing AutoLosing %d%s",playing_auto_declare_losing,nl);
    fprintf(ncf,"Playing AutoWinning %d%s",playing_auto_declare_winning,nl);
  }
  if ( update_groups ) {
    fclose(ncf);
    ncf = fopen(nrcfile,"r");
    if ( ! ncf ) {
      warn("Couldn't re-open new rc file %s: %s",nrcfile,strerror(errno));
      return 0;
    }
    rcf = fopen(rcfile,"w");
    if ( ! rcf ) {
      warn("couldn't open rcfile %s for update: %s",rcfile,strerror(errno));
      fclose(ncf);
      return 0;
    }
    while ( ! feof(ncf) ) {
      fgets(inbuf,1024,ncf);
      if ( feof(ncf) ) continue;
      if ( fputs(inbuf,rcf) == EOF ) {
	warn("Error writing to rcfile %s: %s",rcfile,strerror(errno));
	fclose(rcf); fclose(ncf);
	return 0;
      }
    }
    fclose(rcf);
    fclose(ncf);
    unlink(nrcfile);
  }
  return 1;
}

static int read_rcfile(char *rcfile) {
  return read_or_update_rcfile(rcfile,XmjrcAll,XmjrcNone);
}

/* Apply the game options from the preferences to the current game */
void apply_game_prefs(void)
{
  int i;
  GameOptionEntry *goe;
  PMsgSetGameOptionMsg psgom;
  char buf[256];

  if ( ! the_game ) return;
  psgom.type = PMsgSetGameOption;
  for ( i = 0; i < prefs_table.numoptions; i++ ) {
    goe = &prefs_table.options[i];
    if ( ! goe->enabled ) continue; /* not in preferences */
    if ( goe->protversion > the_game->protversion ) continue; /* can't use it */
    strmcpy(psgom.optname,goe->name,16);
    switch ( goe->type ) {
    case GOTInt:
      sprintf(buf,"%d",goe->value.optint);
      break;
    case GOTNat:
      sprintf(buf,"%d",goe->value.optnat);
      break;
    case GOTBool:
      sprintf(buf,"%d",goe->value.optbool);
      break;
    case GOTScore:
      sprintf(buf,"%d",goe->value.optscore);
      break;
    case GOTString:
      sprintf(buf,"%s",goe->value.optstring);
      break;
    }
    psgom.optvalue = buf;
    send_packet(&psgom);
  }
}

void create_display(void)
{
  GtkWidget *tmpw, *inboard;
#ifndef GTK2
  GtkAccelGroup *tile_accel;
#endif
  int i;

#ifdef GTK2
  if ( main_font_name[0] ) {
    char c[300];
    strcpy(c,"gtk-font-name = \"");
    char *d = c + strlen(c);
    strmcpy(d,main_font_name,256);
    strcat(c,"\"");
    gtk_rc_parse_string(c);
  }
  if ( text_font_name[0] ) {
    char c[300];
    strcpy(c,"style \"text\" { font_name = \"");
    char *d = c + strlen(c);
    strmcpy(d,text_font_name,256);
    strcat(c,"\" }");
    gtk_rc_parse_string(c);
    /* having redefined the styles, we have to recompute them */
    gtk_rc_reset_styles(gtk_settings_get_default());
  }
  if ( table_colour_name[0] ) {
    char c[300];
    strcpy(c,"style \"table\" { bg[NORMAL] = \"");
    char *d = c + strlen(c);
    strmcpy(d,table_colour_name,256);
    strcat(c,"\" }");
    gtk_rc_parse_string(c);
    /* having redefined the styles, we have to recompute them */
    gtk_rc_reset_styles(gtk_settings_get_default());
  }
#else
  /* deal with fonts */
  defstyle = gtk_widget_get_default_style();

  if ( main_font_name[0] ) {
    main_font = gdk_font_load(main_font_name);
    if ( ! main_font ) {
      /* whoops */
      warn("Unable to load specified main font -- using default");
      main_font = original_default_style->font;
    }
  } else {
    main_font = original_default_style->font;
  }
  defstyle->font = main_font;

  /* It is completely $@#$@# insane that I can't specify these
     things with resources. Sometimes I think the gtk developers
     have never heard of networked systems. */
  /* try to get a fixed font. Courier for preference */
#ifdef WIN32
  fixed_font = gdk_font_load("courier new");
  strcpy(fallback_text_font_name,"courier new");
  if ( ! fixed_font ) {
    fixed_font = gdk_font_load("courier");
    strcpy(fallback_text_font_name,"courier");
  }
#else
  fixed_font = gdk_font_load("-*-courier-medium-r-normal-*-*-120-*-*-*-*-*-*");
  strcpy(fallback_text_font_name,"-*-courier-medium-r-normal-*-*-120-*-*-*-*-*-*");
#endif
  /* if that didn't work, try fixed */
  if ( ! fixed_font ) {
    fixed_font = gdk_font_load("-misc-fixed-medium-r-normal-*-*-120-*-*-*-*-*-*");
    strcpy(fallback_text_font_name,"-misc-fixed-medium-r-normal-*-*-120-*-*-*-*-*-*");
  }
  /* if that didn't work, try just "fixed" */
  if ( ! fixed_font ) {
    fixed_font = gdk_font_load("fixed");
    strcpy(fallback_text_font_name,"fixed");
  }
  /* if that didn't work; what can we do? */
  if ( ! fixed_font ) {
    warn("unable to load a fixed font for scoring window etc");
    fallback_text_font_name[0] = 0;
  }

  /* having hopefully found one that works, to use as a fallback,
     now get what the user asked for */
  if ( text_font_name[0] ) {
    GdkFont *f;
    f = gdk_font_load(text_font_name);
    if ( ! f ) {
      /* whoops */
      warn("Unable to load specified main font -- using default");
    } else {
      fixed_font = f;
    }
  }

  /* And now try to get a big font for claim windows. It seems that
     this does actually work on both X and Windows... */
  if ( !big_font ) {
    big_font = gdk_font_load("-*-helvetica-bold-r-*-*-*-180-*-*-*-*-iso8859-*");
  }
  if ( !big_font )
    big_font = gdk_font_load("-*-fixed-bold-r-*-*-*-140-*-*-*-*-iso8859-*");
  if ( !big_font )
    big_font = gdk_font_load("12x24");
  if ( !big_font )
    big_font = gdk_font_load("9x15bold");


  /* all the board widgets want a dark green background 
     -- or whatever the user asked for */
  tablestyle = gtk_style_copy(defstyle);

  gdk_color_parse(table_colour_name[0] ? table_colour_name : "darkgreen",
    &(tablestyle->bg[GTK_STATE_NORMAL]));
  /* to highlight discarded tiles */
#endif /* end of ifndef GTK2 a while ago */

#ifdef GTK2
  gdk_color_parse("red",&highlightcolor);
#else
  highlightstyle = gtk_style_copy(defstyle);
  gdk_color_parse("red",&(highlightstyle->bg[GTK_STATE_NORMAL]));
#endif

  /* set the size if not given by user */
  pdispwidth = display_size;
  if ( pdispwidth == 0 ) {
    pdispwidth = 19;
#ifdef WIN32
    if ( gdk_screen_width() <= 800 ) {
      pdispwidth = 16;
      if (info_windows_in_main) pdispwidth = 14;
    }
#else
    if ( gdk_screen_width() <= 800 ) pdispwidth = 17;
#endif
  }
  /* choose a sensible value for show-wall */
  if ( showwall < 0 ) {
    showwall = ( gdk_screen_height() >= ((dialogs_position == DialogsBelow)
				 ? 1000 : 900));
  }

  if ( dialogs_position == DialogsUnspecified )
    dialogs_position = DialogsCentral;

  topwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_widget_set_name(topwindow,"topwindow");
  gtk_signal_connect (GTK_OBJECT (topwindow), "delete_event",
                               GTK_SIGNAL_FUNC (gtk_main_quit), NULL);
  /* (Un)iconify our auxiliary windows when we are
  */
  gtk_signal_connect (GTK_OBJECT (topwindow), "map_event",
                               GTK_SIGNAL_FUNC (vis_callback), NULL);
  gtk_signal_connect (GTK_OBJECT (topwindow), "unmap_event",
                               GTK_SIGNAL_FUNC (vis_callback), NULL);

  /* do we have a position remembered ?
     There will some downward drift because of the window manager
     decorations, but I really can't be bothered to work round that. */
  if ( top_pos_set ) {
    gtk_widget_set_uposition(topwindow,top_x,top_y);
  }

  /* Why do we realize the window? Because we want to use it
     in creating pixmaps. */
  gtk_widget_realize(topwindow);
 
  /* create the tile pixmaps */
  {
    char pmfile[512];
    const char *code;
    const char *wl[] = { "E", "S", "W", "N" };
    GtkRequisition r;
    GtkWidget *b,*p;
    Tile i;
    int numfailures = 0;

    /* specifying tileset as the empty string is equivalent
       to having it null, meaning don't look for tiles */
    if ( tileset && tileset[0] == 0 ) tileset = 0;

    /* Now try to find a tile set: we look for a directory named
       tileset in the tileset_path search path. If it has a file
       called 1B.xpm (to choose at random) we'll assume it's 
       a tileset directory */
    tilepixmapdir = 0;
    if ( tileset ) {
      char *start, *end, *tpend;
      int lastcmpt = 0;
      int nopath = 0;
      static char tp[1024];
      if ( tileset_path ) start = tileset_path;
      else start = "";
      /* if the tileset is given as an absolute path, we
	 shouldn't use the tileset-path at all */
#     ifdef WIN32
      if ( tileset[0] == '/' || tileset[0] == '\\' || tileset[1] == ':' )
	{ start = ""; nopath = 1; }
#     else
      if ( tileset[0] == '/' ) { start = ""; nopath = 1; }
#     endif
      while ( ! lastcmpt ) {
	end = strchr(start,PATHSEP);
	if ( end == 0 ) { end = start+strlen(start); lastcmpt = 1; }
	if ( start == end && ! nopath ) { strcpy(tp,"."); }
	else { strmcpy(tp,start,end-start+1); }
	strcat(tp,"/");
	strcat(tp,tileset);
	tpend = tp + strlen(tp);
	strcat(tp,"/1B.xpm");
	if ( access(tp,R_OK) == 0 ) {
	  *tpend = 0; /* blank out the /1B.xpm */
	  tilepixmapdir = tp;
	  break;
	}
	/* failed; next component */
	start = end+1;
      }
      if ( ! tilepixmapdir ) {
	warn("Can't find tileset called %s - using fallback",tileset);
      }
    }

    for ( i = -1; i < MaxTile; i++ ) {
      if ( tilepixmapdir ) {
	strmcpy(pmfile,tilepixmapdir,sizeof(pmfile)-1);
	strcat(pmfile,"/");
      }
      code = tile_code(i);
      /* only create the error pixmaps once! */
      if ( i >= 0 && strcmp(code,"XX") == 0 ) {
	tilepixmaps[0][i] = tilepixmaps[0][-1];
	tilepixmaps[1][i] = tilepixmaps[1][-1];
	tilepixmaps[2][i] = tilepixmaps[2][-1];
	tilepixmaps[3][i] = tilepixmaps[3][-1];
      }
      else {
	if ( tilepixmapdir ) {
	  strmcat(pmfile,code,sizeof(pmfile)-strlen(pmfile));
	  strmcat(pmfile,".xpm",sizeof(pmfile)-strlen(pmfile));
	  tilepixmaps[0][i] = gdk_pixmap_create_from_xpm(topwindow->window,
						    NULL,
						    NULL,
						    pmfile);
	}
	/* If this fails, use the fallback data. The error tile
	   is in position 99, the others are in their natural position */
	if ( tilepixmaps[0][i] == NULL ) {
	  int j;
	  if ( i < 0 ) j = 99; else j = i;
	  tilepixmaps[0][i] =
	    gdk_pixmap_create_from_xpm_d(topwindow->window,
					 NULL,
					 NULL,fallbackpixmaps[j]);
	  numfailures++;
	}
	rotate_pixmap(tilepixmaps[0][i],
		      &tilepixmaps[1][i],
		      &tilepixmaps[2][i],
		      &tilepixmaps[3][i]);
      }
    }
    /* and the tong pixmaps */
    for (i=0; i<4; i++) {
      if ( tilepixmapdir ) {
	strmcpy(pmfile,tilepixmapdir,sizeof(pmfile)-1);
	strcat(pmfile,"/");
	strmcat(pmfile,"tong",sizeof(pmfile)-strlen(pmfile));
	strmcat(pmfile,wl[i],sizeof(pmfile)-strlen(pmfile));
	strmcat(pmfile,".xpm",sizeof(pmfile)-strlen(pmfile));
	tongpixmaps[0][i] = gdk_pixmap_create_from_xpm(topwindow->window,
						    i>0 ? NULL : &tongmask,
						    NULL,
						    pmfile);
      }
      if ( tongpixmaps[0][i] == NULL ) {
	tongpixmaps[0][i] =
	  gdk_pixmap_create_from_xpm_d(topwindow->window,
					 i>0 ? NULL : &tongmask,
					 NULL,fallbackpixmaps[101+i]);
	numfailures++;
      }
      rotate_pixmap(tongpixmaps[0][i],
		      &tongpixmaps[1][i],
		      &tongpixmaps[2][i],
		      &tongpixmaps[3][i]);
    }

    if ( tilepixmapdir && numfailures > 0 ) {
      char buf[50];
      sprintf(buf,"%d tile pixmaps not found: using fallbacks",numfailures);
      warn(buf);
    }

    /* and compute the tile spacing */
    gdk_window_get_size(tilepixmaps[0][0],&tile_spacing,NULL);
    tile_spacing /= 4;
    b = gtk_button_new();
    gtk_widget_set_name(b,"tile");
    p = gtk_pixmap_new(tilepixmaps[0][HiddenTile],NULL);
    gtk_widget_show(p);
    gtk_container_add(GTK_CONTAINER(b),p);
#ifdef GTK2
    /* we have to ensure that when we get the size of a tile button,
       it has the style that will be used on the table */
    gtk_container_add(GTK_CONTAINER(topwindow),b);
    gtk_widget_ensure_style(p);
#endif
    gtk_widget_size_request(b,&r);
    tile_width = r.width;
    tile_height = r.height;
    gtk_widget_destroy(b);
  }
    
  /* the outerframe contains the menubar and the board, and if
     dialogs are at the bottom, a box for them */
  /* between the menubar and the board, there is a box for
     the message and info windows, if info_windows_in_main is set */
  outerframe = gtk_vbox_new(0,0);
  gtk_container_add(GTK_CONTAINER(topwindow),outerframe);
  gtk_widget_show(outerframe);
  menubar = menubar_create();
  gtk_box_pack_start(GTK_BOX(outerframe),menubar,0,0,0);
  if ( info_windows_in_main ) {
    info_box = gtk_hbox_new(0,0);
    gtk_widget_show(info_box);
    gtk_box_pack_start(GTK_BOX(outerframe),info_box,0,0,0);
  } else {
    info_box = NULL;
  }

  /* The structure of the whole board is (at present)
     an hbox containing leftplayer, vbox, rightplayer
     with the vbox containing
     topplayer discardarea bottomplayer (us).
     The left/right players are actually sandwiched inside
     vboxes so that they will centre if we force a square
     aspect ratio.
     If the wall is shown, player info labels are inserted into the
     vboxes containing the left and right players.
  */

  /* 2009-06-17. There was something very odd here. Why was the
     boardframe event box inside the boardfixed rather than the 
     other way round? */

  boardfixed = vlazy_fixed_new();
  gtk_widget_set_name(boardfixed,"table");
#ifndef GTK2
  gtk_widget_set_style(boardfixed,tablestyle);
#endif
  gtk_widget_show(boardfixed);
  boardframe = gtk_event_box_new();
  gtk_widget_show(boardframe);
  gtk_widget_set_name(boardframe,"table");
  gtk_box_pack_start(GTK_BOX(outerframe),boardframe,0,0,0);
  gtk_container_add(GTK_CONTAINER(boardframe),boardfixed);
#ifndef GTK2
  gtk_widget_set_style(boardframe,tablestyle);
#endif
  gtk_widget_set_name(boardframe,"table");
  board = gtk_hbox_new(0,0);
#ifndef GTK2
  gtk_widget_set_style(board,tablestyle);
#endif
  gtk_widget_set_name(board,"table");
  gtk_widget_show(board);
  vlazy_fixed_put(VLAZY_FIXED(boardfixed),board,0,0);


  /* the left player */
  playerdisp_init(&pdisps[3],3);
  gtk_widget_show(pdisps[3].widget);
  tmpw = gtk_vbox_new(0,0);
#ifndef GTK2
  gtk_widget_set_style(tmpw,tablestyle);
#endif
  gtk_widget_set_name(tmpw,"table");
  gtk_widget_show(tmpw);
  gtk_box_pack_start(GTK_BOX(board),tmpw,0,0,0);
#ifdef GTK2
  /* label for top player */
  if ( showwall ) {
    GtkWidget *l = gtk_label_new("Player 2");
    gtk_widget_set_name(l,"playerlabel");
    gtk_label_set_justify(GTK_LABEL(l),GTK_JUSTIFY_CENTER);
    gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END);
    pdisps[2].infolab = GTK_LABEL(l);
    gtk_widget_show(l);
    gtk_box_pack_start(GTK_BOX(tmpw),l,1,1,0);
  }
#endif
  gtk_box_pack_start(GTK_BOX(tmpw),pdisps[3].widget,1,0,0);
#ifdef GTK2
  /* label for left player */
  if ( showwall ) {
    GtkWidget *l = gtk_label_new("Player 3");
    gtk_widget_set_name(l,"playerlabel");
    gtk_label_set_justify(GTK_LABEL(l),GTK_JUSTIFY_CENTER);
    // gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END);
    pdisps[3].infolab = GTK_LABEL(l);
    gtk_widget_show(l);
    gtk_label_set_angle(GTK_LABEL(l),270);
    gtk_box_pack_start(GTK_BOX(tmpw),l,1,1,0);
  }
#endif

  /* the inner box */
  inboard = gtk_vbox_new(0,0);
#ifndef GTK2
  gtk_widget_set_style(inboard,tablestyle);
#endif
  gtk_widget_set_name(inboard,"table");
  gtk_widget_show(inboard);
  /* The inner box needs to expand to fit the outer, or else
     it will shrink to fit the top and bottom players */
  gtk_box_pack_start(GTK_BOX(board),inboard,1,1,0);

  /* the right player */
  playerdisp_init(&pdisps[1],1);
  gtk_widget_show(pdisps[1].widget);
  tmpw = gtk_vbox_new(0,0);
#ifndef GTK2
  gtk_widget_set_style(tmpw,tablestyle);
#endif
  gtk_widget_set_name(tmpw,"table");
  gtk_widget_show(tmpw);
  gtk_box_pack_start(GTK_BOX(board),tmpw,0,0,0);
#ifdef GTK2
  /* label for right player */
  if ( showwall ) {
    GtkWidget *l = gtk_label_new("Player 1");
    gtk_widget_set_name(l,"playerlabel");
    gtk_label_set_justify(GTK_LABEL(l),GTK_JUSTIFY_CENTER);
    // gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END);
    pdisps[1].infolab = GTK_LABEL(l);
    gtk_widget_show(l);
    gtk_label_set_angle(GTK_LABEL(l),90);
    gtk_box_pack_start(GTK_BOX(tmpw),l,1,1,0);
  }
#endif
  gtk_box_pack_start(GTK_BOX(tmpw),pdisps[1].widget,1,0,0);
#ifdef GTK2
  /* label for bottom player */
  if ( showwall ) {
    GtkWidget *l = gtk_label_new("Player 0");
    gtk_widget_set_name(l,"playerlabel");
    gtk_label_set_justify(GTK_LABEL(l),GTK_JUSTIFY_CENTER);
    gtk_label_set_ellipsize(GTK_LABEL(l),PANGO_ELLIPSIZE_END);
    pdisps[0].infolab = GTK_LABEL(l);
    gtk_widget_show(l);
    gtk_box_pack_start(GTK_BOX(tmpw),l,1,1,0);
  }
#endif

  /* the top player */
  playerdisp_init(&pdisps[2],2);
  gtk_widget_show(pdisps[2].widget);
  gtk_box_pack_start(GTK_BOX(inboard),pdisps[2].widget,0,0,0);

  /* the discard area */
  discard_area = lazy_fixed_new();
#ifndef GTK2
  gtk_widget_set_style(discard_area,tablestyle);
#endif
  gtk_widget_set_name(discard_area,"table");
  gtk_container_set_border_width(GTK_CONTAINER(discard_area),dialog_border_width);
  gtk_widget_show(discard_area);
  gtk_box_pack_start(GTK_BOX(inboard),discard_area,1,1,0);

  /* the bottom player (us) */
  playerdisp_init(&pdisps[0],0);
  gtk_widget_show(pdisps[0].widget);
  gtk_box_pack_end(GTK_BOX(inboard),pdisps[0].widget,0,0,0);

  /* we'll install two signals on the player widget, which deal
     with changing the tile selection, and likewise to move */
#ifndef GTK2
  gtk_signal_connect(GTK_OBJECT(pdisps[0].widget),"selectleft",
    (GtkSignalFunc)change_tile_selection,(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(pdisps[0].widget),"selectright",
    (GtkSignalFunc)change_tile_selection,(gpointer)1);
  gtk_signal_connect(GTK_OBJECT(pdisps[0].widget),"moveleft",
    (GtkSignalFunc)move_selected_tile,(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(pdisps[0].widget),"moveright",
    (GtkSignalFunc)move_selected_tile,(gpointer)1);
#else
  /* install on the top window */
  gtk_signal_connect(GTK_OBJECT(topwindow),"selectleft",
    (GtkSignalFunc)change_tile_selection,(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(topwindow),"selectright",
    (GtkSignalFunc)change_tile_selection,(gpointer)1);
  gtk_signal_connect(GTK_OBJECT(topwindow),"moveleft",
    (GtkSignalFunc)move_selected_tile,(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(topwindow),"moveright",
    (GtkSignalFunc)move_selected_tile,(gpointer)1);
#endif

#ifndef GTK2
  /* now create the accelerator group and install it on the top
     window */
  tile_accel = gtk_accel_group_new();
  gtk_accel_group_add(tile_accel,
#ifdef WIN32
    GDK_l,
#else
    GDK_Left,
#endif
    0,0,GTK_OBJECT(pdisps[0].widget),"selectleft");
  gtk_accel_group_add(tile_accel,
#ifdef WIN32
    GDK_r,
#else
    GDK_Right,
#endif
    0,0,GTK_OBJECT(pdisps[0].widget),"selectright");
  gtk_accel_group_add(tile_accel,
#ifdef WIN32
    GDK_l,
#else
    GDK_Left,
#endif
    GDK_SHIFT_MASK,0,GTK_OBJECT(pdisps[0].widget),"moveleft");
  gtk_accel_group_add(tile_accel,
#ifdef WIN32
    GDK_r,
#else
    GDK_Right,
#endif
    GDK_SHIFT_MASK,0,GTK_OBJECT(pdisps[0].widget),"moveright");
  gtk_window_add_accel_group(GTK_WINDOW(topwindow),tile_accel);
#endif

  /* At this point, all the boxes are where we want them.
     So to prevent everything being completely mucked up, we now
     lock the sizes of the player boxes.
     This works, without showing the window, because we're asking
     the widgets what they want.
  */
  for (i=0;i<4;i++) {
    GtkRequisition r;
    GtkWidget *w;
    w = pdisps[i].widget;
    gtk_widget_size_request(w,&r);
    gtk_widget_set_usize(w,r.width,r.height);
  }

  /* if we are showing the wall, create it */
  if ( showwall ) create_wall();

  /* now we can force square aspect ratio */
  if ( square_aspect ) {
    GtkRequisition r;
    GtkWidget *w;
    w = board;
    gtk_widget_size_request(w,&r);
    gtk_widget_set_usize(w,r.width,r.width);
  }

  /* these dialogs may be part of the top level window,
     so they get created now */
  create_dialogs();

  /* create the other dialogs */

  /* the scoring message thing */
  textwindow_init();

  /* and the message window */
  messagewindow_init();

  /* and the warning window */
  warningwindow_init();

  /* and the status window */
  status_init();

  /* and the open dialog */
  {
    char idt[10];
    idt[0] = 0;
    sprintf(idt,"%d",our_id);
    open_dialog_init(idt,name);
  }


  /* It's essential that we map the main window now,
     since if we are resuming an interrupted game, messages
     will arrive quickly; and the playerdisp_discard
     needs to know the allocation of the discard area.
     If we look at the wrong time, before it's mapped, we get
     -1: the discard_area won't get its allocation until after
     the topwindow has got its real size from the WM */
  /* Note. In GTK1, it was harmless, but not effective, to do
     gtk_widget_show_now. In GTK2, not only doesn't it work as desired,
     it messes things up. So we only show, and then process events.*/
  gtk_widget_show(topwindow);

  /* Now we have to wait until stuff is mapped and placed, and I
     don't know how that happens.
     So we'll just iterate through the main loop until we
     find some sensible values in here. It seems that the non
     sensible values include 1. */
  while ( discard_area->allocation.width <= 1 )
    gtk_main_iteration();

  discard_area_alloc = discard_area->allocation;

  /* dissuade the discard area from shrinking */
  gtk_widget_set_usize(discard_area,discard_area_alloc.width,discard_area_alloc.height);

  /* now expand the left and right players to match the bottom */
  /* now we set the vertical size of the left and right players to
     match the horizontal size of the top and bottom */
  gtk_widget_set_usize(pdisps[1].widget,
    pdisps[1].widget->allocation.width,
    pdisps[0].widget->allocation.width);
  gtk_widget_set_usize(pdisps[3].widget,
    pdisps[3].widget->allocation.width,
    pdisps[0].widget->allocation.width);

  /* The window is now built, and sizes calculated We now
     clear everything. I don't quite know what the order of events
     is here, but it seems to work with no flashing. */
  for (i=0;i<4;i++) playerdisp_update(&pdisps[i],NULL);

  /* if there's a wall, and a game, make the wall match reality.
     We should check that there is a non-zero wall! */
  if ( the_game && showwall && the_game->wall.size > 0 ) {
    int i;
    for ( i = 0; i < the_game->wall.live_used; i++ ) {
      gtk_widget_destroy(wall[wall_game_to_board(i)]);
      wall[wall_game_to_board(i)] = NULL;
    }
    for ( i = the_game->wall.size;
	  i >= the_game->wall.dead_end; i-- )
      adjust_wall_loose(i);
  }

  /* if there's a game, remake the discards */
  playerdisp_discard_recompute = 1;
  if ( the_game ) {
    int i;
    for ( i = 0; i < discard_history.count; i++ )
      playerdisp_discard(discard_history.hist[i].pd,discard_history.hist[i].t);
  }

  /* set up appropriate dialogs */
  setup_dialogs();

  /* add the callbacks. It's important that we do this after 
     doing main loop processing to fix the discard area! */

  if ( pass_stdin ) 
    stdin_callback_tag = gdk_input_add(0,GDK_INPUT_READ,stdin_input_callback,NULL);

  /* and if there's a current connection, set callback */
  control_server_processing(1);

}

/* This function destroys the display and frees everything created
   for it, preparatory to creating it again with new parameters */
void destroy_display(void)
{
  int i,ori;
  const char *code;

  /* first of all remove the input callbacks, since we don't
     want to process them with no display */
  control_server_processing(0);
  if ( pass_stdin ) {
    gtk_input_remove(stdin_callback_tag);
    stdin_callback_tag = 0;
  }

  /* destroy and zero all the dialogs known publicly */
  destroy_dialogs();

  /* destroy and clear the wall */
  clear_wall();

  /* save the position of the top level window */
  gdk_window_get_deskrelative_origin(topwindow->window,&top_x,&top_y);
  top_pos_set = 1;

  /* destroy the message, warning, status and scoring windows (which may
     be top-level, or may be in the topwindow */
  if ( messagewindow ) gtk_widget_destroy(messagewindow); messagewindow = NULL;
  if ( status_window ) gtk_widget_destroy(status_window); status_window = NULL;
  if ( textwindow ) gtk_widget_destroy(textwindow); textwindow = NULL;
  if ( warningwindow ) gtk_widget_destroy(warningwindow); warningwindow = NULL;

  gtk_widget_destroy(topwindow); /* kills most everything else */

  /* get rid of the tile pixmaps */
  for ( i = -1 ; i < MaxTile; i++ ) {
    code = tile_code(i);
    /* only destroy the error pixmaps once ... */
    if ( tilepixmaps[0][i] && (i < 0 || strcmp(code,"XX") != 0) ) {
      for ( ori = 0; ori < 4; ori++ ) {
	gdk_pixmap_unref(tilepixmaps[ori][i]);
	tilepixmaps[ori][i] = 0;
      }
    }
  }
  for ( i = 0; i < 4 ; i++ ) {
    for ( ori = 0; ori < 4; ori++ ) {
      gdk_pixmap_unref(tongpixmaps[ori][i]);
      tongpixmaps[ori][i] = 0;
    }
  }
  /* not necessary to destroy dialogs, as create_dialogs does that */

}

/* control server processing: disables or enables the processing
   of input from the game server */
void control_server_processing(int on) {
  if ( on && the_game && the_game->fd != (int)INVALID_SOCKET ) {
#ifdef WIN32
    if ( the_game->fd == STDOUT_FILENO ) {
      server_callback_tag
	= gdk_input_add(STDIN_FILENO,GDK_INPUT_READ,server_input_callback,NULL);
    } else {
      /* the socket routines have already converted the socket into
	 a GIOChannel *. */
      server_callback_tag
	= g_io_add_watch((GIOChannel *)the_game->fd,
	  G_IO_IN|G_IO_ERR,gcallback,NULL);
    }
#else  
    if ( the_game->fd == STDOUT_FILENO ) {
      server_callback_tag
	= gdk_input_add(STDIN_FILENO,GDK_INPUT_READ,server_input_callback,NULL);
    } else {
      server_callback_tag
	= gdk_input_add(the_game->fd,GDK_INPUT_READ,server_input_callback,NULL);
    }
#endif
  } else {
    if ( server_callback_tag ) {
      gdk_input_remove(server_callback_tag);
      server_callback_tag = 0;
    }
  }
}

/* This function moves the tile selection for our player left
   (if data = 0) or right. If no tile is selected, it selects
   the leftmost or rightmost (not sure whether this is the right
   way round...) */
static void change_tile_selection(GtkWidget *w UNUSED,gpointer right)
{
  int i = selected_button;
  if ( !our_player || our_player->num_concealed == 0 ) return;
  if ( i < 0 ) {
    i = right ? 0 : our_player->num_concealed - 1 ;
  } else {
    if ( !right ) i = (i == 0) ?  our_player->num_concealed - 1 : i - 1;
    else i = (i >= our_player->num_concealed - 1) ? 0 : i + 1;
  }
  conc_callback(pdisps[0].conc[i],(gpointer)(intptr_t)(i | 128));
}

/* This function moves the selected tile left or right in the hand */
static void move_selected_tile(GtkWidget *w UNUSED,gpointer right)
{
  int i = selected_button;
  if ( !our_player || our_player->num_concealed == 0 ) return;
  if ( i < 0 ) return;
  if ( right ) {
    player_reorder_tile(our_player,i,
      (i == our_player->num_concealed-1) ? 0 : i+1);
  } else {
    player_reorder_tile(our_player,i,
      (i == 0) ? our_player->num_concealed-1 : i-1);
  }
  playerdisp_update_concealed(&pdisps[0],HiddenTile);
  change_tile_selection(w,right);
}

/* Would you ****ing believe it? GTK thinks you aren't allowed to
   use arrow keys as accelerators, because it knows best about what
   to do with them.
   Well, **** that for a lark. We'll override some internal code
   right here. */
/* This stuff (a) doesn't work in GTK2, because they have prevented the
   function from being overridden, (b) is unnecessary, because the desired
   effect can be obtained simply by binding Left and Right in the top window.
   Pity that wasn't available in GTK1
*/

#ifndef GTK2
gboolean
gtk_accelerator_valid (guint		  keyval,
		       GdkModifierType	  modifiers)
{
  static const guint invalid_accelerator_vals[] = {
    GDK_BackSpace, GDK_Delete, GDK_KP_Delete,
    GDK_Shift_L, GDK_Shift_R, GDK_Shift_Lock, GDK_Caps_Lock, GDK_ISO_Lock,
    GDK_Control_L, GDK_Control_R, GDK_Meta_L, GDK_Meta_R,
    GDK_Alt_L, GDK_Alt_R, GDK_Super_L, GDK_Super_R, GDK_Hyper_L, GDK_Hyper_R,
    GDK_Mode_switch, GDK_Num_Lock, GDK_Multi_key,
    GDK_Scroll_Lock, GDK_Sys_Req, 
    /* I don't care what gtk thinks, I want to use these 
    GDK_Up, GDK_Down, GDK_Left, GDK_Right, GDK_Tab, GDK_ISO_Left_Tab,
    GDK_KP_Up, GDK_KP_Down, GDK_KP_Left, GDK_KP_Right, GDK_KP_Tab,
    */
    GDK_First_Virtual_Screen, GDK_Prev_Virtual_Screen,
    GDK_Next_Virtual_Screen, GDK_Last_Virtual_Screen,
    GDK_Terminate_Server, GDK_AudibleBell_Enable,
    0
  };
  const guint *ac_val;

  modifiers &= GDK_MODIFIER_MASK;
    
  if (keyval <= 0xFF)
    return keyval >= 0x20;

  ac_val = invalid_accelerator_vals;
  while (*ac_val)
    {
      if (keyval == *ac_val++)
	return FALSE;
    }

  return TRUE;
}
#endif

static void iconify_window(GtkWidget *w)
{
#ifdef GTK2
  if ( ! GTK_IS_WINDOW(w) ) return;
  return gtk_window_iconify(GTK_WINDOW(w));
#else
#ifndef WIN32
  XEvent ev;
  static guint32 atom = 0;
  if ( ! GTK_IS_WINDOW(w) ) return;
  if ( atom == 0 ) {
    atom = gdk_atom_intern("WM_CHANGE_STATE",FALSE);
  }
  ev.xclient.type = ClientMessage;
  ev.xclient.display = gdk_display;
  ev.xclient.format = 32;
  ev.xclient.data.l[0] = 3; /* iconic state */
  ev.xclient.message_type = (guint32) atom;
  ev.xclient.window = GDK_WINDOW_XWINDOW(w->window);
  gdk_send_xevent(gdk_root_window,False,SubstructureRedirectMask|SubstructureNotifyMask, &ev);
#endif
#endif
}

static void uniconify_window(GtkWidget *w)
{
  if ( ! GTK_IS_WINDOW(w) ) return;
  /* if the widget is not visible in the gtk sense, somebody's
     closed it while we thought it should be iconified.
     So don't raise it. */
  if ( ! GTK_WIDGET_VISIBLE(w) ) return;
  gdk_window_show(w->window);
}
