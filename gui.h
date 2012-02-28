/* $Header: /home/jcb/MahJong/newmj/RCS/gui.h,v 12.1 2011/11/27 21:47:22 jcb Rel $
 * gui.h
 * type defns and forward declarations for the gui module.
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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include "lazyfixed.h"
#include "vlazyfixed.h"
#include "sysdep.h"
#include <errno.h>
#include "client.h"
#include <math.h>

/*********************
  TYPE DEFINITIONS
*********************/

/* a tilesetbox is a TileSet and a widget that boxes it.
   The TileSet is stored to avoid unnecessary updating of the box
*/
typedef struct _TileSetBox {
  TileSet set;
  GtkWidget *widget; /* the widget itself */
  GtkWidget *tiles[4]; /* the children buttons */
} TileSetBox;

/* This struct represents the display of a player. It gives
   access to relevant widgets without requiring excessive casting
   and pointer chasing.
*/
typedef struct _PlayerDisp {
  PlayerP player; /* pointer to the game player structure */
  /* this is the whole box representing the player */
  GtkWidget *widget;
  /* the orientation says which way round tiles go
     (equivalently, which player we are in the table).
     The tilepixmaps are those appropriate assuming that players
     look at their own tiles. If the players politely display
     their sets to be readable by the others, then the orientation
     of exposed tiles is flipped from these.
     that the orientations are:
     0:  upright (for our own tiles)
     1:  top to left: player to the right (south, when we're east)
     2:  upside down (player opposite)
     3:  top to right (north).
  */
  int orientation;
  /* and accordingly the following macro can be used if concealed
     and exposed tiles are oriented differently */
  /* # define flipori(ori) ((ori+2)%4) */
#define flipori(ori) ori
  /* this is an array of buttons representing the concealed tiles.
     For initial size calculation, it should be filled with blanks.
     (Ideally, unmapped, but I can't see how to do that.)
     They appear from the left in the "concealed" row.
  */
  GtkWidget *conc[MAX_CONCEALED];
  /* This is an array representing the declared specials
     that have overflowed into the concealed row.
     Initially, exactly five of these should be shown: that makes
     the whole "concealed" row 19 tiles wide, which gives enough
     room for reasonable collections of specials, and makes the "exposed"
     row big enough for four kongs and a pair.
     They appear to the right of the concealed row.
  */
  GtkWidget *extras[8];
  /* These are the specials that have found room where we want
     them, at the end of the exposed row */
  GtkWidget *spec[8];
  /* each of these is a box representing a declared set.
     Each set is packed into the exposed row, from the left,
     with a 1/4 tile spacing between them (thus fitting
     4 kongs and a pair in exactly).
     Size is determined by the concealed row, so there is
     no need to show these initially.
  */
  /* This is the tong box. It appears to the right of the exposed row specials*/
  GtkWidget *tongbox;
  TileSetBox esets[MAX_TILESETS];
  /* each of these is a box representing a concealed set.
     They appear to the left of the concealed tile set, with
     the 1/4 tile spacing. They are initially hidden, and
     appear only in scoring.
  */
  TileSetBox csets[MAX_TILESETS];
  
  /* these is not actually in the player area, but to do with 
     discards */
  GtkWidget *discards[32]; /* buttons for this player's discards */
  int dx[32], dy[32]; /* save calculated posns of discard tiles */
  int num_discards;
  gint16 x,y; /* x and y for next discard */
  int row; /* which row is next discard in */
  gint16 xmin[5],xmax[5]; /* in each row, first point used, leftmost point free*/
  int plane; /* if desperate, we start stacking tiles */

  /* This is the window for pung! claims etc. */
  GtkWidget *claimw;
  GtkWidget *claimlab; /* and the label inside it */
  int claim_serial; /* the discard for which this window was popped up */
  int claim_time; /* time popped up (in millisecs since start of program) */

  /* this is a label widget which is put the right of each player when
     the wall is shown, to use the otherwise empty space. It displays the
     same info as the info window. It's only used in GTK2, because in order to
     make it clear which label goes with which, we use rotated text. */
  GtkLabel *infolab;
} PlayerDisp;

/* This is a one use structure representing the dialog box
   used for claiming discards. It has two personalities:
   normally it shows buttons
   Noclaim  Chow  Pung  Kong  MahJong
   but after a mahjong claim it shows
   Eyes  Chow  Pung  Special Hand
*/
typedef struct {
  GtkWidget *widget; /* the box itself */
  GtkWidget *tilename; /* the label for the tile name */
  GtkWidget *tiles[4]; /* the buttons for the tiles as discarded
			  by each player. Element 0 is unused. */
  /* the various buttons */
  GtkWidget *noclaim;
  GtkWidget *eyes;
  GtkWidget *chow;
  GtkWidget *pung;
  GtkWidget *special;
  GtkWidget *kong;
  GtkWidget *mahjong;
  GtkWidget *robkong;
  int mode; /* 0 normally, 1 in mahjong mode, 2 for robbing kongs */
} DiscardDialog;

/* this is used by functions that display or remove tiles to pass
   back information to the animation routines. The structure contains
   information about the tiles.
*/
typedef struct {
  GtkWidget *target;  /* the widget to which this information refers,
		    if it is being newly displayed. In this case,
		    the widget is not shown, and it is the animator's
		    job to show it at the end of animation.
		    If target is null, then the widget is being 
		    undisplayed. */
  Tile t;        /* the tile displayed by the widget */
  int ori;       /* and its orientation */
  int x, y;      /* x and y coordinates relative to the boardframe */
} AnimInfo;

/* enums used in the dialog popup function to specify position */
typedef enum {
  DPCentred, /* centered over main window */
  DPOnDiscard, /* bottom left corner in same place as discard dialog */
  DPErrorPos, /* for error dialogs: centred over top of main window */
  DPNone, /* don't touch the positioning at all */
  DPCentredOnce, /* centre it on first popup, then don't fiddle */
  DPOnDiscardOnce, /* on discard dialog first time, then don't fiddle */
} DPPosn;

/* Where to put the dialog boxes */
typedef enum {
 DialogsUnspecified = 0,
 DialogsCentral = 1,
 DialogsBelow = 2,
 DialogsPopup = 3 } DialogPosition;

/* when to sort the tiles in our hand */
typedef enum {
  SortAlways = 0,
  SortDeal = 1,
  SortNever = 2,
} SortTiles;

/* Whether to file fault reports over the network */
typedef enum {
  DebugReportsUnspecified = 0,
  DebugReportsNever = 1,
  DebugReportsAsk = 2,
  DebugReportsAlways = 3 } DebugReports;

/* The rcfile is used to store assorted persistent data. We often
   need to read or update only selected parts of it. This set of
   flag bits identifies the groups that can be independently 
   read or updated. */
typedef enum {
  XmjrcNone = 0x00,
  XmjrcDisplay = 0x01, /* preferences regarding the display */
  XmjrcPlayer = 0x02, /* player option settings */
  XmjrcGame = 0x04, /* game option preferences */
  XmjrcMisc = 0x08, /* odds and sods */
  XmjrcOpen = 0x10, /* sticky fields in the connection dialogs */
  XmjrcPlaying = 0x20, /* playing preferences */
  XmjrcAll = 0xFFFFFFFF /* everything */
} XmjrcGroup;

/* extra data in the game */
typedef struct {
  int orig_live_end; /* value of wall.live_end at start of hand */
} GameExtras;
#define gextras(g) ((GameExtras *)(g->userdata))

/*****************
   FORWARD DECLARATIONS
*****************/

/* FUNCTIONS */

void apply_game_prefs(void);
GtkWidget *build_or_refresh_option_panel(GameOptionTable *got,GtkWidget *panel);
GtkWidget *build_or_refresh_prefs_panel(GameOptionTable *got,GtkWidget *panel);
void button_set_tile(GtkWidget *b, Tile t, int ori);
void chow_dialog_init(void);
void close_connection(int keepconnection);
void close_saving_posn(GtkWidget *w);
void conc_callback(GtkWidget *w, gpointer data);
void continue_dialog_init(void);
void continue_dialog_popup(void);
void control_server_processing(int on);
void create_dialogs(void);
void create_display(void);
void debug_options_dialog_popup(void);
void destroy_dialogs(void);
void destroy_display(void);
void dialog_popup(GtkWidget *dialog, DPPosn posn);
void disc_callback(GtkWidget *w, gpointer data);
void discard_dialog_init(void);
void discard_dialog_popup(Tile t, int ori, int mode);
void do_chow(GtkWidget *w, gpointer data);
gint doubleclicked(GtkWidget *w, GdkEventButton *eb,gpointer data);
void ds_dialog_init(void);
void ds_dialog_popup(void);
void end_dialog_init(void);
void end_dialog_popup(void);
void error_dialog_init(void);
void error_dialog_popup(char *msg);
void game_option_init(void);
void game_prefs_init(void);
void info_dialog_popup(char *msg);
GtkWidget *menubar_create(void);
GtkWidget *make_or_refresh_option_updater(GameOptionEntry *goe, int prefsp);
void messagewindow_init(void);
void nag_popup(void);
void option_reset_callback(GtkWidget *w, gpointer data);
void option_updater_callback(GtkWidget *w, gpointer data);
void open_connection(GtkWidget *w UNUSED, gpointer data, int fd, char *newaddr);
void open_dialog_init(char *idt, char *nt);
void open_dialog_popup(GtkWidget *w UNUSED, gpointer data);
void password_showraise(void);
void playing_prefs_init(void);
void prefs_updater_callback(GtkWidget *w, gpointer data);
int read_or_update_rcfile(char *rcfile,
  XmjrcGroup read_groups,
  XmjrcGroup update_groups);
void scoring_dialog_init(void);
void scoring_dialog_popup(void);
void set_dialog_posn(DialogPosition p);
void set_animation(int a);
void setup_dialogs(void);
void showraise(GtkWidget *w);
void status_init(void);
void status_showraise(void);
void status_update(int game_over);
void textwindow_init(void);
void tilesetbox_highlight_nth(TileSetBox *tb,int n);
void tilesetbox_init(TileSetBox *tb, int ori,GtkSignalFunc func,gpointer func_data);
void tilesetbox_set(TileSetBox *tb, const TileSet *ts, int ori);
void turn_dialog_init(void);
void turn_dialog_popup(void);
void usage(char *pname,char *msg);
void warningwindow_init(void);
int log_msg_add(LogLevel l,char *warning);
void warning_clear(void);
/* Convenience function */
#define send_packet(m) client_send_packet(the_game,(PMsgMsg *)m)

/* VARIABLES */

/* in gui.c */
extern int debug;
extern int server_pversion; /* protocol version that server speaks
			       (independently of game) */
extern PlayerP our_player;
extern int our_id;
extern seats our_seat; /* our seat in the game */
extern Game *the_game;
extern int selected_button; /* the index of the user's selected
			     tile, or -1 if none */

extern int ptimeout; /* claim timeout time in milliseconds */
extern int local_timeouts; /* are we handling timeouts ourselves? */

extern int monitor; /* if 1, send no messages other than in direct
		       response to user action. Used for monitoring
		       a stream in debugging. */

extern int game_over; /* if the game is over, but we're waiting for the user
			 to confirm the disconnect. In this state, we do
			 not close our connection when the other end closes.
		      */

extern GdkPixmap **tilepixmaps[]; /* pixmaps for the tiles */

extern GtkWidget *topwindow; /* main window */
extern GtkWidget *menubar; /* menubar */
extern GtkWidget *board; /* the table area itself */
extern GtkWidget *boardframe; /* fixed widget wrapping the board */
extern GtkWidget *outerframe; /* the outermost frame widget */
extern GtkStyle *tablestyle; /* for the dark green stuff */
extern GtkStyle *highlightstyle; /* to highlight tiles */
extern GtkWidget *highlittile; /* the unique highlighted tile */
extern GdkFont *fixed_font; /* a fixed width font */
extern GdkFont *big_font; /* big font for claim windows */

extern GtkWidget *dialoglowerbox; /* encloses dialogs when dialogs are below */

/* Why an array? So I can pass pointers around */
extern DiscardDialog discard_dialog[1];

/* dialog box for specifying chows */
extern GtkWidget *chow_dialog;

/* dialog box for declaring specials */
extern GtkWidget *ds_dialog;

/* dialog box for continuing with next hand */
extern GtkWidget *continue_dialog;

/* dialog box for disconnect at end of game */
extern GtkWidget *end_dialog;

/* dialog for opening connection */
extern GtkWidget *open_dialog;
extern GtkWidget *openmenuentry, *newgamemenuentry, *resumegamemenuentry, 
  *savemenuentry, *saveasmenuentry, *closemenuentry, *gameoptionsmenuentry;
extern GtkWidget *openallowdisconnectbutton,*opensaveonexitbutton,*openrandomseatsbutton,
  *openplayercheckboxes[3],*openplayernames[3],*openplayeroptions[3],*opentimeoutspinbutton;

/* dialog box for action when it's our turn.
   Actions: Discard  Kong  Add to Pung  Mah Jong
*/
extern GtkWidget *turn_dialog;

/* dialog box for closed sets when scoring.
   Actions: Eyes  Chow  Pung  Done
*/
extern GtkWidget *scoring_dialog;

/* window for game status display */
extern GtkWidget *status_window;

/* an array of text widgets for displaying scores etc.
   Element 4 is for settlements.
   The others are for each player: currently, I think
   these should be table relative.
*/
extern GtkWidget *scoring_notebook;
extern GtkWidget *textpages[5];
extern GtkWidget *textlabels[5]; /* labels for the pages */
extern GtkWidget *textwindow; /* and the window for it */

/* option stuff */
extern char robot_names[3][128];
extern char robot_options[3][128];

/* The window for messages, and the display text widget */
extern GtkWidget *messagewindow, *messagetext;
/* and for warnings */
extern GtkWidget *warningwindow, *warningtext;
extern GtkWidget *info_box; /* box to hold info windows */
/* completed games, for nagging */
extern int completed_games;
extern int nag_state; /* has user been nagged to pay? */
/* window to nag for donations */
extern GtkWidget *nag_window;

/* This gives the width of a player display in units
   of tiles */

extern int pdispwidth;
extern int display_size; /* and this is the preference value for pdispwidth */
extern int square_aspect; /* force a square table */
extern char address[]; /* server address */
extern char name[]; /* player's name */
extern char main_font_name[]; /* font used in all dialogs etc */
extern char text_font_name[]; /* font used for text in windows */
extern char fallback_text_font_name[]; /* one that worked */
#ifdef GTK2
extern int use_system_gtkrc; /* as it says */
extern char gtk2_rcfile[512]; /* gtkrc file to use */
#endif
extern char table_colour_name[]; /* colour of the table background */
extern int animate; /* do fancy animation */
extern int nopopups; /* suppress automatic popup of message/scoring windows */
extern int tiletips; /* show tiletips constantly */
extern int showwall; /* show the wall or not */
extern int sort_tiles; /* when to sort tiles */
extern int info_windows_in_main; /* should the message and game info windows
				    be included in the main window, or
				    be separate popups ? */
extern int iconify_dialogs_with_main; /* as it says ... */
extern char *tileset, *tileset_path; /* tile sets */
extern int pref_showwall; /* preferred value of showwall */

/* playing options */
extern int playing_auto_declare_specials;
extern int playing_auto_declare_losing;
extern int playing_auto_declare_winning;

/* debug options */
extern DebugReports debug_reports; /* send debugging reports? */

/* the player display areas */
extern PlayerDisp pdisps[NUM_SEATS];

extern int calling; /* disgusting global flag */

/* the widget in which we put discards */
extern GtkWidget *discard_area;
/* This is an allocation structure in which we
   note its allocated size, at some point when we
   know the allocation is valid.
*/
extern GtkAllocation discard_area_alloc;

extern GtkWidget *just_doubleclicked; /* yech yech yech. See doubleclicked */

/* space round edge of dialog boxes */
extern const gint dialog_border_width;
/* horiz space between buttons */
extern const gint dialog_button_spacing;
/* vert space between text and buttons etc */
extern const gint dialog_vert_spacing;

/* space around player boxes. N.B. this should only
   apply to the outermost boxes */
extern const int player_border_width;

extern DialogPosition dialogs_position;

/* in gui-dial.c */
extern GtkWidget *openfile, *openhost, *openport,
  *openfiletext, *openhosttext, *openporttext, *openidtext, *opennametext, *opengamefiletext;
extern GtkWidget *display_option_dialog;
extern GtkWidget *game_option_dialog;
extern GtkWidget *game_prefs_dialog;
extern GtkWidget *game_option_panel;
extern GtkWidget *game_prefs_panel;
extern GameOptionTable prefs_table;
extern GtkWidget *playing_prefs_dialog;
/* static for each module */
#include "version.h"

