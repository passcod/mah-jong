/* $Header: /home/jcb/MahJong/newmj/RCS/gui-dial.c,v 12.6 2012/02/01 15:29:39 jcb Rel $
 * gui-dial.c
 * dialog box functions.
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
#ifdef GTK2
#define GTK_WINDOW_DIALOG GTK_WINDOW_TOPLEVEL
#endif

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/gui-dial.c,v 12.6 2012/02/01 15:29:39 jcb Rel $";

static void continue_callback(GtkWidget *w, gpointer data);
static void turn_callback(GtkWidget *w, gpointer data);

static void debug_report_query_popup(void);

/* check box to keep keyboard focus in message window */
static GtkWidget *mfocus;

/* This grabs focus, unless the chat window is in the main window
   and has focus. This stops focus being lost from the chat entry
   box every time a dialog pops up. (Requested by users, to avoid
   accidental discards.
   Moreover, if the appropriate checkbox is set, it
   keeps the focus in the message window.
*/
static void grab_focus_if_appropriate(GtkWidget *w);

/* Used sordidly and internally */
static GtkRequisition discard_req = { 0, 0};
/* Why an array? So I can pass pointers around */
DiscardDialog discard_dialog[1];

/* dialog box for specifying chows */
GtkWidget *chow_dialog;
/* Stores the three TileSetBoxes */
static TileSetBox chowtsbs[3];
/* and the three buttons */
static GtkWidget *chowbuttons[3];

/* dialog box for declaring specials */
GtkWidget *ds_dialog;

/* dialog box for continuing with next hand */
GtkWidget *continue_dialog;

/* for end of game */
GtkWidget *end_dialog;

/* dialog for opening connection */
GtkWidget *open_dialog;
GtkWidget *openmenuentry, *newgamemenuentry, *resumegamemenuentry, 
  *savemenuentry, *saveasmenuentry, *closemenuentry, *gameoptionsmenuentry;
/* entry for showing warnings window */
static GtkWidget *warningentry;

/* dialog box for action when it's our turn.
   Actions: Discard  Kong  Mah Jong
   As of 11.80, Kong includes adding a tile to an existing pung
*/
GtkWidget *turn_dialog;
GtkWidget *turn_dialog_label; /* used to display number of tiles left */

/* dialog box for closed sets when scoring.
   Actions: Eyes  Chow  Pung  Done
*/
GtkWidget *scoring_dialog;

/* window for game status display */
GtkWidget *status_window;

/* window for "about" information */
GtkWidget *about_window;
/* window to nag for donations */
GtkWidget *nag_window;

/* an array of text widgets for displaying scores etc.
   Element 4 is for settlements.
   The others are for each player: currently, I think
   these should be table relative.
*/
GtkWidget *scoring_notebook;
GtkWidget *textpages[5];
GtkWidget *textlabels[5]; /* labels for the pages */
GtkWidget *textwindow; /* and the window for it */

/* The window for messages, and the display text widget */
GtkWidget *messagewindow, *messagetext;

#ifdef GTK2
GtkTextBuffer *messagetextbuf;
GtkTextIter messagetextiter;
#endif

/* Warning window */
GtkWidget *warningwindow, *warningtext;

#ifdef GTK2
GtkTextBuffer *warningtextbuf;
GtkTextIter warningtextiter;
#endif

/* The Save As.. dialog */
GtkWidget *save_window;
/* and its text entry widget */
GtkWidget *save_text;

/* Password entry dialog */
GtkWidget *password_window;
/* and its text entry widget */
GtkWidget *password_text;

/* The window for display options */
GtkWidget *display_option_dialog = NULL;

/* the window for updating the game options */
GtkWidget *game_option_dialog = NULL;
GtkWidget *game_option_panel = NULL;
/* and some of its buttons */
GtkWidget *game_option_apply_button = NULL;
GtkWidget *game_option_prefs_button = NULL;

/* and a very similar one for option preferences */
GtkWidget *game_prefs_dialog = NULL;
GtkWidget *game_prefs_panel = NULL;

/* and one for playing preferences */
GtkWidget *playing_prefs_dialog = NULL;

/* and one for debugging options */
static GtkWidget *debug_options_dialog = NULL;

/* message entry widget */
static GtkWidget *message_entry = NULL;

/* time at which progress bar was started */
static struct timeval pstart;
static int pinterval = 25; /* timeout interval in ms */
static intptr_t pbar_timeout_instance = 0; /* track dead timeouts */
static GtkWidget *pbar;

/* timeout handler for the dialog progress bar */
static int pbar_timeout(gpointer instance) {
  int timeleft;
  struct timeval timenow;

  if ( pbar_timeout_instance != (intptr_t) instance ) return FALSE; /* dead timeout */
  if ( ! GTK_WIDGET_VISIBLE(discard_dialog->widget) ) return FALSE;
  gettimeofday(&timenow,NULL);
  timeleft = ptimeout-(1000*(timenow.tv_sec-pstart.tv_sec)
	      +(timenow.tv_usec-pstart.tv_usec)/1000);
  if ( timeleft <= 0 ) {
    /* we should not hide the claim dialog: the timeout is really
       controlled by the server, not us */
    /* However, if we are supposed to be handling timeouts locally,
       we'd better send a noclaim! */
    if ( local_timeouts ) {
      disc_callback(NULL,(gpointer)NoClaim);
    }
    return FALSE;
  }
  gtk_progress_bar_update(GTK_PROGRESS_BAR(pbar),1.0-(timeleft+0.0)/(ptimeout+0.0));
  return TRUE;
}

/* popup the discard dialog. Arguments:
   Tile, player whence it came (as an ori),
   mode = 0 (normal), 1 (claiming tile for mah jong), 2 (claiming from kong) */
void discard_dialog_popup(Tile t, int ori, int mode) {
  int i;
  char buf[128];
  static Tile lastt; static int lastori, lastmode;

  /* So that we don't work if it's already popped up: */
  if ( GTK_WIDGET_VISIBLE(discard_dialog->widget)
       && t == lastt && lastori == ori && lastmode == mode ) return;
  lastt = t; lastori = ori; lastmode = mode;

  if ( mode != discard_dialog->mode ) {
    discard_dialog->mode = mode;
    if ( mode == 0 ) {
      gtk_widget_show(discard_dialog->noclaim);
      gtk_widget_hide(discard_dialog->eyes);
      gtk_widget_show(discard_dialog->chow);
      gtk_widget_show(discard_dialog->pung);
      gtk_widget_hide(discard_dialog->special);
      gtk_widget_show(discard_dialog->kong);
      gtk_widget_show(discard_dialog->mahjong);
      gtk_widget_hide(discard_dialog->robkong);
    } else if ( mode == 1 ) {
      gtk_widget_hide(discard_dialog->noclaim);
      gtk_widget_show(discard_dialog->eyes);
      gtk_widget_show(discard_dialog->chow);
      gtk_widget_show(discard_dialog->pung);
      gtk_widget_show(discard_dialog->special);
      gtk_widget_hide(discard_dialog->kong);
      gtk_widget_hide(discard_dialog->mahjong);
      gtk_widget_hide(discard_dialog->robkong);
    } else {
      gtk_widget_show(discard_dialog->noclaim);
      gtk_widget_hide(discard_dialog->eyes);
      gtk_widget_hide(discard_dialog->chow);
      gtk_widget_hide(discard_dialog->pung);
      gtk_widget_hide(discard_dialog->special);
      gtk_widget_hide(discard_dialog->kong);
      gtk_widget_hide(discard_dialog->mahjong);
      gtk_widget_show(discard_dialog->robkong);
    }
  }
  if ( mode == 0 ) grab_focus_if_appropriate(discard_dialog->noclaim);

  /* set the appropriate tile */
  for ( i=1 ; i < 4 ; i++ ) {
    if ( i == ori ) {
      button_set_tile(discard_dialog->tiles[i],t,i);
      gtk_widget_show(discard_dialog->tiles[i]);
    } else {
      gtk_widget_hide(discard_dialog->tiles[i]);
    }
  }
  gtk_widget_hide(discard_dialog->tilename);
  if ( mode == 1 ) {
    gtk_label_set_text(GTK_LABEL(discard_dialog->tilename),
		     "Claim discard for:");
  } else {
    /* if not showing wall, say how many tiles left */
    if ( ! showwall ) {
      if ( ori == 1 ) sprintf(buf,"(%d tiles left) %s",
			      the_game->wall.live_end-the_game->wall.live_used,
			      tile_name(the_game->tile));
      else sprintf(buf,"%s (%d tiles left)",
		   tile_name(the_game->tile),
		   the_game->wall.live_end-the_game->wall.live_used);
    } else { 
      strcpy(buf,tile_name(the_game->tile));
    }
    gtk_label_set_text(GTK_LABEL(discard_dialog->tilename),buf);
  }
  if ( mode == 0 ) {
    static const gfloat xal[] = { 0.5,1.0,0.5,0.0 }; 
    gtk_misc_set_alignment(GTK_MISC(discard_dialog->tilename),
			  xal[ori],0.5);
  } else {
    gtk_misc_set_alignment(GTK_MISC(discard_dialog->tilename),
			  0.5,0.5);
  }
  gtk_widget_show(discard_dialog->tilename);
  dialog_popup(discard_dialog->widget,DPCentred);
  /* and start the progress bar timeout if appropriate */
  if ( the_game->state != MahJonging && ptimeout > 0 ) {
    gtk_widget_show(pbar);
    gettimeofday(&pstart,NULL);
    /* we may as well calculate an appropriate value of pbar_timeout
       each time... we want it to update every half pixel, or 40 times
       a second, whichever is slower */
    if ( pbar->allocation.width > 1 ) {
      /* in case it isn't realized yet */
      pinterval = ptimeout/(2*pbar->allocation.width);
    }
    if ( pinterval < 25 ) pinterval = 25;
    gtk_timeout_add(pinterval,pbar_timeout,(gpointer) ++pbar_timeout_instance);
  } else {
    gtk_widget_hide(pbar);
  }
}

/* this macro generates a variable for an accelerator group,
   and a function to add or remove it. The macro argument is included 
   in the name of the variable and function */
#define ACCELFUN(NAME) \
static GtkAccelGroup *NAME ## _accel;\
static void add_or_remove_ ## NAME ## _accels(GtkWidget *w UNUSED, gpointer data)\
{\
  static int there = 0;\
  if ( NAME ## _accel == NULL ) return;\
  if ( data ) {\
    if (GTK_HAS_FOCUS & GTK_WIDGET_FLAGS(message_entry)) return;\
    if ( ! there )\
      gtk_window_add_accel_group(GTK_WINDOW(topwindow),NAME ## _accel);\
    there = 1;\
  } else {\
    if ( there )\
      gtk_window_remove_accel_group(GTK_WINDOW(topwindow),NAME ## _accel);\
    there = 0;\
  }\
}

/* an accelerator group  for the discard dialog */
ACCELFUN(discard)

/* initialize it */
/* Structure:
   If the dialog is in the middle:
      lefttile opptile righttile
           tile name
         progress bar
      Pass/Draw Chow Pung Kong MahJong
   otherwise: 
      tilename   progress bar
         buttons
*/
void discard_dialog_init(void) {
  GtkWidget *box, *tilebox, *left, *opp, *right, *lbl,
      *butbox, *but, *pixm;
  DiscardDialog *dd = &discard_dialog[0];

  if ( dd->widget ) {
    gtk_widget_destroy(dd->widget);
    dd->widget = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    /* event box so there's a window to have background */
    dd->widget = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),dd->widget,0,0);
    /* it'll be moved later */
    break;
  case DialogsBelow:
    dd->widget = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(dialoglowerbox),dd->widget,1,0,0);
    /* show it, so that the top window includes it when first mapped */
    gtk_widget_show(dd->widget);
    break;
  case DialogsPopup:
    dd->widget = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (dd->widget), "delete_event",
                               GTK_SIGNAL_FUNC (gtk_widget_hide), NULL);
  }

  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(dd->widget),box);
  dd->mode = -1; /* so that personality will be set */
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);

  tilebox = gtk_hbox_new(0,0);
  if ( dialogs_position != DialogsBelow ) gtk_widget_show(tilebox);
  
  left = gtk_button_new();
  GTK_WIDGET_UNSET_FLAGS(left,GTK_CAN_FOCUS);
  gtk_widget_show(left);
  pixm = gtk_pixmap_new(tilepixmaps[3][HiddenTile],NULL);
  gtk_widget_show(pixm);
  gtk_container_add(GTK_CONTAINER(left),pixm);
  opp = gtk_button_new();
  GTK_WIDGET_UNSET_FLAGS(opp,GTK_CAN_FOCUS);
  gtk_widget_show(opp);
  pixm = gtk_pixmap_new(tilepixmaps[2][HiddenTile],NULL);
  gtk_widget_show(pixm);
  gtk_container_add(GTK_CONTAINER(opp),pixm);
  right = gtk_button_new();
  GTK_WIDGET_UNSET_FLAGS(right,GTK_CAN_FOCUS);
  gtk_widget_show(right);
  pixm = gtk_pixmap_new(tilepixmaps[1][HiddenTile],NULL);
  gtk_widget_show(pixm);
  gtk_container_add(GTK_CONTAINER(right),pixm);
  gtk_box_pack_start(GTK_BOX(tilebox),left,0,0,0);
  gtk_box_pack_start(GTK_BOX(tilebox),opp,1,0,0);
  gtk_box_pack_end(GTK_BOX(tilebox),right,0,0,0);
  dd->tiles[1] = right;
  dd->tiles[2] = opp;
  dd->tiles[3] = left;
  
  lbl = gtk_label_new("name of tile");
  gtk_widget_show(lbl);
  dd->tilename = lbl;

  butbox = gtk_hbox_new(1,dialog_button_spacing); /* homogeneous, spaced */
  gtk_widget_show(butbox);
  
  /* accelerators for discard dialog actions */
  discard_accel = gtk_accel_group_new();

  but = gtk_button_new_with_label("No claim");
  gtk_widget_show(but);
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked",
    GTK_SIGNAL_FUNC(disc_callback),(gpointer)NoClaim);
  dd->noclaim = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_n,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_n,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Eyes");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  /* not shown in normal state */
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked"
    ,GTK_SIGNAL_FUNC(disc_callback),(gpointer)PairClaim);
  dd->eyes = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_e,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_e,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Chow");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  gtk_widget_show(but);
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked"
    ,GTK_SIGNAL_FUNC(disc_callback),(gpointer)ChowClaim);
  dd->chow = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_c,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_c,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Pung");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  gtk_widget_show(but);
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked",GTK_SIGNAL_FUNC(disc_callback),(gpointer)PungClaim);
  dd->pung = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_p,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_p,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Special Hand");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  /* gtk_widget_show(but); */ /* don't show this; uses other space */
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked",GTK_SIGNAL_FUNC(disc_callback),(gpointer)SpecialSetClaim);
  dd->special = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_s,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_s,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Kong");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  gtk_widget_show(but);
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked",GTK_SIGNAL_FUNC(disc_callback),(gpointer)KongClaim);
  dd->kong = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_k,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_k,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Mah Jong");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  gtk_widget_show(but);
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked",GTK_SIGNAL_FUNC(disc_callback),(gpointer)MahJongClaim);
  dd->mahjong = but;
#ifdef GTK2
  gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_m,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_m,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  but = gtk_button_new_with_label("Rob the Kong - Mah Jong!");
  GTK_WIDGET_UNSET_FLAGS(but,GTK_CAN_FOCUS);
  /* gtk_widget_show(but); */ /* don't show this; it uses the space of others */
  gtk_box_pack_start(GTK_BOX(butbox),but,1,1,0);
  gtk_signal_connect(GTK_OBJECT(but),"clicked",GTK_SIGNAL_FUNC(disc_callback),(gpointer)MahJongClaim);
  dd->robkong = but;
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(but),"clicked",discard_accel,GDK_r,0,0);
#else
  gtk_accel_group_add(discard_accel,GDK_r,0,0,GTK_OBJECT(but),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(but)->child),"_");

  pbar = gtk_progress_bar_new();
  gtk_widget_show(pbar);

  /* These are packed in reverse order so they float to the bottom */
  gtk_box_pack_end(GTK_BOX(box),butbox,0,0,0);
  gtk_box_pack_end(GTK_BOX(box),lbl,0,0,0);
  gtk_box_pack_end(GTK_BOX(box),pbar,0,0,0);
  gtk_box_pack_end(GTK_BOX(box),tilebox,0,0,0);
  
  /* OK, now ask its size: store the result, and keep it
     this size for ever more */
  gtk_widget_size_request(dd->widget,&discard_req);
  gtk_widget_set_usize(dd->widget,discard_req.width,discard_req.height);
  
  /* we have to ensure that the accelerators are only available when
     the widget is popped up */
  gtk_signal_connect(GTK_OBJECT(discard_dialog->widget),"hide",GTK_SIGNAL_FUNC(add_or_remove_discard_accels),(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(discard_dialog->widget),"show",GTK_SIGNAL_FUNC(add_or_remove_discard_accels),(gpointer)1);

}

static GtkWidget *turn_dialog_discard_button;
static GtkWidget *turn_dialog_calling_button;

void turn_dialog_popup(void) {
  /* only original call is allowed, so hide the calling
     button after first discard */
  if ( pflag(our_player,NoDiscard) ) 
    gtk_widget_show(turn_dialog_calling_button);
  else 
    gtk_widget_hide(turn_dialog_calling_button);
  /* if not showing wall, put tiles left in label */
  if ( ! showwall ) {
    char buf[128];
    sprintf(buf,"(%d tiles left)  Select tile and:",
	    the_game->wall.live_end-the_game->wall.live_used);
    gtk_label_set_text(GTK_LABEL(turn_dialog_label),buf);
  } else {
    gtk_label_set_text(GTK_LABEL(turn_dialog_label),"Select tile and:");
  }
  dialog_popup(turn_dialog,DPOnDiscardOnce);
  grab_focus_if_appropriate(turn_dialog_discard_button);
}

/* callback when we toggle one of our tiles.
   data is our index number.
   If data also has bit 7 set, force the tile active.
   If called with NULL widget, clear selection 
*/

void conc_callback(GtkWidget *w, gpointer data) {
  int active;
  int i;
  int force=0, index=-1;
  static GtkWidget *selected = NULL; /* for radiogroup functionality */

  active = (w && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)));
  if ( active ) index = ((int)(intptr_t)data)& 127;
  force = (w && (((int)(intptr_t)data) & 128));


  if ( w && just_doubleclicked == w) force = 1;

  /* make sure all other tiles are unselected, if we're active,
     or if we're called to clear: we don't just rely
     on the selected variable, since under some circumstances we
     can end up with two tiles active, by accident as it were */
  /* FIXME: this relies on induced callbacks being executed synchronously */
  if ( active || w == NULL) {
    for ( i = 0; i<14; i++ ) {
      if ( pdisps[0].conc[i] && pdisps[0].conc[i] != w) {
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pdisps[0].conc[i]),
				     FALSE);
      }
    }
  }
  selected_button = index;
  selected = NULL;
  if ( active ) selected = w;

  if ( w == NULL ) return;

  /* change the label on the discard button to be declare if
     this is a flower */
  gtk_label_set_text(GTK_LABEL(GTK_BIN(turn_dialog_discard_button)->child),
    is_special(our_player->concealed[index]) ? "Declare" : "Discard");

  if ( force ) {
    /* if it's not active, set it active: we'll then
       be invoked normally, so we just return now. */
    if ( ! active ) {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),TRUE);
      return;
    }
  }

  /* if we were double clicked, invoke the turn callback directly */
  if ( w && just_doubleclicked == w) {
    just_doubleclicked = 0;
    turn_callback(w,(gpointer)PMsgDiscard);
  }
}

/* This detects doubleclicks on the concealed buttons */
gint doubleclicked(GtkWidget *w, GdkEventButton *eb,gpointer data UNUSED) {
  if ( eb->type != GDK_2BUTTON_PRESS ) return FALSE;
  /* This is disgusting. We set a global doubleclicked flag,
     which is noticed by the toggle callback */
  just_doubleclicked = w;
  return FALSE;
}

/* callback attached to the buttons of the discarding dialog.
   They pass PMsgDiscard, PMsgDeclareClosedKong,
   PMsgAddToPung, or PMsgMahJong.
   As of 11.80, PMsgDeclareClosedKong may mean that or AddToPung,
   and we must work out which here.
   Passed PMsgDiscard + 1000000 to declare calling.
   Also invoked by the declaring special callback:
    with DeclareSpecial to declare a special, Kong if appropriate,
    and NoClaim to indicate the end of declaration.
   Also invoked by scoring dialog with appropriate values */

static void turn_callback(GtkWidget *w UNUSED, gpointer data) {
  PMsgUnion m;
  Tile selected_tile;

  /* it is possible for this to be invoked when it shouldn't be,
     for example double-clicking on a tile (or hitting space on a tile?)
     when no dialog is up. So let's check that one of possible dialogs
     is up */
  if ( ! (GTK_WIDGET_VISIBLE(turn_dialog)
	 || GTK_WIDGET_VISIBLE(ds_dialog)
	 || GTK_WIDGET_VISIBLE(scoring_dialog) ) ) return;

  selected_tile = (selected_button < 0) ? HiddenTile : our_player->concealed[selected_button];

  m.type = (PlayerMsgType)data;

  if ( m.type == PMsgMahJong ) {
    // set the discard to 0 for cleanliness.
    m.mahjong.discard = 0;
    send_packet(&m);
    return;
  }
  if ( m.type == PMsgDeclareClosedKong ) {
    if ( player_can_declare_closed_kong(our_player,selected_tile) ) {
      /* that's fine */
    } else {
      /* assume they mean add to pung */
      m.type = PMsgAddToPung;
    }
  }
  if ( m.type == PMsgShowTiles
       || m.type == PMsgFormClosedSpecialSet ) {
    send_packet(&m);
    return;
  }

  if ( the_game->state == DeclaringSpecials
       && m.type == PMsgNoClaim ) {
    m.type = PMsgDeclareSpecial;
    conc_callback(NULL,NULL); /* clear the selection */
  }

  /* in declaring specials, use this to finish */
  if ( selected_tile == HiddenTile ) {
    if ( the_game->state == DeclaringSpecials )
      m.type = PMsgDeclareSpecial;
    else {
      error_dialog_popup("No tile selected!");
      return;
    }
  }
  if ( is_special(selected_tile) )
    m.type = PMsgDeclareSpecial;

  switch ( m.type ) {
  case PMsgDeclareSpecial:
    m.declarespecial.tile = selected_tile;
    break;
  case PMsgDiscard:
    if ( selected_button >= 0 )
      player_set_discard_hint(our_player,selected_button);
    m.discard.tile = selected_tile;
    m.discard.calling = 0;
    break;
  case PMsgDiscard+1000000:
    if ( selected_button >= 0 )
      player_set_discard_hint(our_player,selected_button);
    m.type = PMsgDiscard;
    m.discard.tile = selected_tile;
    m.discard.calling = 1;
    break;
  case PMsgDeclareClosedKong:
    m.declareclosedkong.tile = selected_tile;
    break;
  case PMsgAddToPung:
    m.addtopung.tile = selected_tile;
    break;
  case PMsgFormClosedPair:
    m.formclosedpair.tile = selected_tile;
    break;
  case PMsgFormClosedChow:
    m.formclosedchow.tile = selected_tile;
    break;
  case PMsgFormClosedPung:
    m.formclosedpung.tile = selected_tile;
    break;
  default:
    warn("bad type in turn_callback");
    return;
  }
  send_packet(&m);
}
  
/* callback when one of the discard dialog buttons is clicked */
void disc_callback(GtkWidget *w UNUSED, gpointer data) {
  PMsgUnion m;

  switch ( (intptr_t) data) {
  case NoClaim:
    m.type = PMsgNoClaim;
    m.noclaim.discard = the_game->serial;
    break;
  case ChowClaim:
    m.type = PMsgChow;
    m.chow.discard = the_game->serial;
    m.chow.cpos = AnyPos; /* worry about it later */
    break;
  case PairClaim:
    m.type = PMsgPair;
    break;
  case SpecialSetClaim:
    m.type = PMsgSpecialSet;
    break;
  case PungClaim:
    m.type = PMsgPung;
    m.pung.discard = the_game->serial;
    break;
  case KongClaim:
    m.type = PMsgKong;
    m.kong.discard = the_game->serial;
    break;
  case MahJongClaim:
    m.type = PMsgMahJong;
    m.mahjong.discard = the_game->serial;
    break;
  default:
    warn("disc callback called with unexpected data");
    return;
  }

  /* If the server has a protocol version before 1050, we mustn't
     send an AnyPos while mahjonging, as it doesn't know how to
     handle it; so pop up the chow dialog directly */
  if ( server_pversion < 1050 && the_game->state == MahJonging && m.type == PMsgChow ) {
    do_chow(NULL,(gpointer)AnyPos);
  } else {
    send_packet(&m);
  }
}

ACCELFUN(chow)

  /* Now create the Chow dialog box:
     Structure: three boxes, each containing a tileset showing
     a possible chow. Below that, a label "which chow?".
     Below that, three buttons to select.
  */
void chow_dialog_init(void)  {
  GtkWidget *box,*u,*v; int i;

  if ( chow_dialog ) {
    gtk_widget_destroy(chow_dialog);
    chow_dialog = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    chow_dialog = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),chow_dialog,0,0);
    break;
  case DialogsPopup:
  case DialogsBelow:
    chow_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (chow_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
    /* This one is allowed to shrink, and should */
    gtk_window_set_policy(GTK_WINDOW(chow_dialog),1,1,1);
  }
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(chow_dialog),box);
  u = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(u);
  gtk_box_pack_start(GTK_BOX(box),u,0,0,0);
  for (i=0;i<3;i++) {
    v = gtk_hbox_new(0,0);
    gtk_widget_show(v);
    gtk_box_pack_start(GTK_BOX(u),v,0,0,0);
    chowtsbs[i].widget = v;
    tilesetbox_init(&chowtsbs[i],0,GTK_SIGNAL_FUNC(do_chow),(gpointer)(intptr_t)i);
  }
  u = gtk_label_new("Which chow?");
  gtk_widget_show(u);
  gtk_box_pack_start(GTK_BOX(box),u,0,0,0);
  u = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(u);
  gtk_box_pack_start(GTK_BOX(box),u,0,0,0);

  chow_accel = gtk_accel_group_new();

  for (i=0;i<3;i++) {
    static int keys[] = { GDK_l, GDK_m, GDK_u };
    v = gtk_button_new_with_label(player_print_ChowPosition(i));
    GTK_WIDGET_UNSET_FLAGS(v,GTK_CAN_FOCUS);
    gtk_widget_show(v);
    gtk_box_pack_start(GTK_BOX(u),v,1,1,0);
    chowbuttons[i] = v;
    gtk_signal_connect(GTK_OBJECT(v),"clicked",GTK_SIGNAL_FUNC(do_chow),(gpointer)(intptr_t)i);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(v),"clicked",chow_accel,keys[i],0,0);
#else
    gtk_accel_group_add(chow_accel,keys[i],0,0,GTK_OBJECT(v),"clicked");
#endif
    gtk_label_set_pattern(GTK_LABEL(GTK_BIN(v)->child),"_");
  }

  gtk_signal_connect(GTK_OBJECT(chow_dialog),"hide",GTK_SIGNAL_FUNC(add_or_remove_chow_accels),(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(chow_dialog),"show",GTK_SIGNAL_FUNC(add_or_remove_chow_accels),(gpointer)1);

}

/* now create the declaring specials dialog.
   Contains a button for declare (special), kong, and done.
   If no flowers, just kongs.
*/
static GtkWidget *ds_dialog_declare_button;
static GtkWidget *ds_dialog_finish_button;

ACCELFUN(ds)

void ds_dialog_init(void) {
  GtkWidget *box, *bbox, *w;

  if ( ds_dialog ) {
    gtk_widget_destroy(ds_dialog);
    ds_dialog = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    ds_dialog = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),ds_dialog,0,0);
    break;
  case DialogsBelow:
    ds_dialog = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(dialoglowerbox),ds_dialog,1,0,0);
    break;
  case DialogsPopup:
    ds_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (ds_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  }
  
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(ds_dialog),box);
  
  bbox = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(bbox);
  gtk_box_pack_end(GTK_BOX(box),bbox,0,0,0);

  if ( game_get_option_value(the_game,GOFlowers,NULL).optbool )
    w = gtk_label_new("Declare Flowers/Seasons (and kongs)\nSelect tile and:");
  else
    w = gtk_label_new("Declare kongs\nSelect tile and:");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);
  
  ds_accel = gtk_accel_group_new();

  if ( game_get_option_value(the_game,GOFlowers,NULL).optbool ) {
    ds_dialog_declare_button = w = gtk_button_new_with_label("Declare");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(bbox),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgDeclareSpecial);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",ds_accel,GDK_d,0,0);
#else
    gtk_accel_group_add(ds_accel,GDK_d,0,0,GTK_OBJECT(w),"clicked");
#endif
    gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");
  }

  w = gtk_button_new_with_label("Kong");
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(bbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgDeclareClosedKong);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",ds_accel,GDK_k,0,0);
#else
  gtk_accel_group_add(ds_accel,GDK_k,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");

  ds_dialog_finish_button = w = gtk_button_new_with_label("Finish");
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(bbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgNoClaim);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",ds_accel,GDK_f,0,0);
#else
  gtk_accel_group_add(ds_accel,GDK_f,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");

  gtk_signal_connect(GTK_OBJECT(ds_dialog),"hide",GTK_SIGNAL_FUNC(add_or_remove_ds_accels),(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(ds_dialog),"show",GTK_SIGNAL_FUNC(add_or_remove_ds_accels),(gpointer)1);
}

void ds_dialog_popup(void) {
  dialog_popup(ds_dialog,DPCentredOnce);
  if ( game_get_option_value(the_game,GOFlowers,NULL).optbool )
    grab_focus_if_appropriate(ds_dialog_declare_button);
  else 
    grab_focus_if_appropriate(ds_dialog_finish_button);
}
/* dialog to ask to continue with next hand */
static GtkWidget *continue_dialog_continue_button;
static GtkWidget *continue_dialog_label;

void continue_dialog_init(void) {
  GtkWidget *box, *w;

  if ( continue_dialog ) {
    gtk_widget_destroy(continue_dialog);
    continue_dialog = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    continue_dialog = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),continue_dialog,0,0);
    break;
  case DialogsBelow:
    continue_dialog = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(dialoglowerbox),continue_dialog,1,0,0);
    break;
  case DialogsPopup:
    continue_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (continue_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  }
  
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(continue_dialog),box);
  
  continue_dialog_continue_button = w = gtk_button_new_with_label("Ready");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(continue_callback),(gpointer)(intptr_t)PMsgDiscard);

  /* created and packed second so things float to bottom */

  continue_dialog_label = w = gtk_label_new("Here is some dummy text to space");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);
  
}

void continue_dialog_popup(void) {
  static char buf[256];
  /* the text of the display depends on whether we've said we're ready */
  if ( ! the_game->active ) {
    strcpy(buf,"Waiting for game to (re)start");
    gtk_label_set_text(GTK_LABEL(continue_dialog_label),buf);
    gtk_widget_hide(continue_dialog_continue_button);
  }
  else if ( the_game->ready[our_seat] ) {
    strcpy(buf,"Waiting for others ");
    strcat(buf,the_game->paused);
    gtk_label_set_text(GTK_LABEL(continue_dialog_label),buf);
    gtk_widget_hide(continue_dialog_continue_button);
  } else {
    strcpy(buf,"Ready ");
    strcat(buf,the_game->paused);
    strcat(buf,"?");
    gtk_label_set_text(GTK_LABEL(continue_dialog_label),buf);
    gtk_widget_show(continue_dialog_continue_button);
    grab_focus_if_appropriate(continue_dialog_continue_button);
  }
  dialog_popup(continue_dialog,DPCentredOnce);
}

static void continue_callback(GtkWidget *w UNUSED, gpointer data UNUSED) {
  PMsgReadyMsg rm;
  rm.type = PMsgReady;
  send_packet(&rm);
}

/* dialog to ask to close at end of game */
static GtkWidget *end_dialog_end_button;
static GtkWidget *end_dialog_label;

static void end_callback(GtkWidget *w UNUSED, gpointer data UNUSED);

void end_dialog_init(void) {
  GtkWidget *box, *w;

  if ( end_dialog ) {
    gtk_widget_destroy(end_dialog);
    end_dialog = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    end_dialog = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),end_dialog,0,0);
    break;
  case DialogsBelow:
    end_dialog = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(dialoglowerbox),end_dialog,1,0,0);
    break;
  case DialogsPopup:
    end_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (end_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  }
  
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(end_dialog),box);
  
  end_dialog_end_button = w = gtk_button_new_with_label("Done");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(end_callback),(gpointer)(intptr_t)PMsgDiscard);

  /* created and packed second so things float to bottom */

  end_dialog_label = w = gtk_label_new("GAME OVER");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);
  
}

void end_dialog_popup(void) {
  grab_focus_if_appropriate(end_dialog_end_button);
  dialog_popup(end_dialog,DPCentredOnce);
}

static void end_callback(GtkWidget *w UNUSED, gpointer data UNUSED) {
  close_connection(0);
  gtk_widget_hide(end_dialog);
  status_showraise();
}

/* how many are up; used for positioning - not multi-thread safe! */
static int num_error_dialogs = 0; 

/* function called to close an error dialog */
static void kill_error(GtkObject *data) {
  gtk_widget_destroy(GTK_WIDGET(data));
  num_error_dialogs--;
}

/* popup an error box (new for each message),
   or an info box (which doesn't have error tile in it)' */
static void _error_dialog_popup(char *msg,int iserror) {
  GtkWidget *box, *hbox, *w, *error_dialog, *error_message;
  
  if ( topwindow == NULL ) {
    warn("error_dialog_popup: windows not initialized");
    return;
  }

  if ( num_error_dialogs >= 10 ) {
    warn("Too many error dialogs up to print error: %s",msg);
    return;
  }

  error_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_signal_connect_object(GTK_OBJECT (error_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_destroy), 
			    GTK_OBJECT(error_dialog));
  gtk_container_set_border_width(GTK_CONTAINER(error_dialog),
				 dialog_border_width);
  
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(error_dialog),box);
  
  hbox = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(box),hbox,0,0,0);

  if ( iserror ) {
    w = gtk_pixmap_new(tilepixmaps[0][ErrorTile],NULL);
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(hbox),w,0,0,0);
  }

  error_message = gtk_label_new(msg);
  gtk_widget_show(error_message);
  gtk_box_pack_start(GTK_BOX(hbox),error_message,0,0,0);

  w = gtk_button_new_with_label("OK");
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(box),w,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(kill_error),GTK_OBJECT(error_dialog));
  gtk_window_set_focus(GTK_WINDOW(error_dialog),w);

  dialog_popup(error_dialog,DPErrorPos);
  num_error_dialogs++;
  gdk_window_raise(error_dialog->window);
}

void error_dialog_popup(char *msg) {
  _error_dialog_popup(msg,1);
}

void info_dialog_popup(char *msg) {
  _error_dialog_popup(msg,0);
}



GtkWidget *openfile, *openhost, *openport,
  *openfiletext, *openhosttext, *openporttext, *openidtext, *opennametext,
  *opengamefile, *opengamefiletext, *opentimeout;

GtkWidget *openallowdisconnectbutton,*opensaveonexitbutton,*openrandomseatsbutton,
  *openplayercheckboxes[3],*openplayernames[3],*openplayeroptions[3],*opentimeoutspinbutton;
static GtkWidget *opengamepanel,*openplayeroptionboxes[3],
  *openconnectbutton,*openstartbutton, *openresumebutton, *openhosttoggle, *openunixtoggle;

/* callback used in next function */
static void openbut_callback(GtkWidget *w, gpointer data) {
  int active = GTK_TOGGLE_BUTTON(w)->active;

  switch ( (int)(intptr_t)data ) {
  case 1:
    if ( active ) {
      /* selected network */
      gtk_widget_set_sensitive(openfile,0);
      gtk_widget_set_sensitive(openhost,1);
      gtk_widget_set_sensitive(openport,1);
    }
    break;
  case 2:
    if ( active ) {
      /* selected Unix socket */
      gtk_widget_set_sensitive(openfile,1);
      gtk_widget_set_sensitive(openhost,0);
      gtk_widget_set_sensitive(openport,0);
    }
    break;
  case 10:
  case 11:
  case 12:
    gtk_widget_set_sensitive(openplayeroptionboxes[((int)(intptr_t)data)-10],active);
    break;
  }
}

/* the dialog for opening a connection.
   Arguments are initial values for id, name 
   text entry fields, */
void open_dialog_init(char *idt, char *nt) {
  /* Layout of the box:
     x  Connect to host
     x  Use Unix socket
     Hostname
     ........
     Port 
     ....
     Filename
     ....
     Player ID
     ....
     Name
     ....
     OPEN CANCEL
  */
  int i;
  GtkWidget *box, *bbox, *but1, *but2, *w1, *w2;
  open_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_policy(GTK_WINDOW(open_dialog),FALSE,FALSE,TRUE);
  gtk_signal_connect (GTK_OBJECT (open_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(open_dialog),
				 dialog_border_width);
  
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(open_dialog),box);
  
  w1 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  openhosttoggle = but1 = gtk_radio_button_new_with_label(NULL,"Internet server");
  GTK_WIDGET_UNSET_FLAGS(but1,GTK_CAN_FOCUS);
#ifndef WIN32
  gtk_widget_show(but1);
#endif
  gtk_box_pack_start(GTK_BOX(w1),but1,0,0,0);
  openunixtoggle = but2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"Unix socket server");
  GTK_WIDGET_UNSET_FLAGS(but2,GTK_CAN_FOCUS);
#ifndef WIN32
  gtk_widget_show(but2);
#endif
  gtk_box_pack_end(GTK_BOX(w1),but2,0,0,0);

  gtk_signal_connect(GTK_OBJECT(but1),"toggled",GTK_SIGNAL_FUNC(openbut_callback),(gpointer)1);
  gtk_signal_connect(GTK_OBJECT(but2),"toggled",GTK_SIGNAL_FUNC(openbut_callback),(gpointer)2);

  w2 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(box),w2,0,0,0);
  openhost = gtk_hbox_new(0,0);
  gtk_widget_show(openhost);
  gtk_box_pack_start(GTK_BOX(w2),openhost,0,0,0);
  w1 = gtk_label_new("Host: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(openhost),w1,0,0,0);
  openhosttext = gtk_entry_new();
  gtk_widget_show(openhosttext);
  gtk_box_pack_start(GTK_BOX(openhost),openhosttext,0,0,0);

  openport = gtk_hbox_new(0,0);
  gtk_widget_show(openport);
  gtk_box_pack_start(GTK_BOX(w2),openport,0,0,0);
  w1 = gtk_label_new("Port: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(openport),w1,0,0,0);
  openporttext = gtk_entry_new_with_max_length(5);
  gtk_widget_set_usize(openporttext,75,0);
  gtk_widget_show(openporttext);
  gtk_box_pack_start(GTK_BOX(openport),openporttext,0,0,0);

  openfile = gtk_hbox_new(0,0);
#ifndef WIN32
  gtk_widget_show(openfile);
#endif
  gtk_box_pack_start(GTK_BOX(box),openfile,0,0,0);
  w1 = gtk_label_new("Socket file: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(openfile),w1,0,0,0);
  openfiletext = gtk_entry_new();
  gtk_widget_show(openfiletext);
  gtk_box_pack_start(GTK_BOX(openfile),openfiletext,0,0,0);

  opengamefile = gtk_hbox_new(0,0);
  gtk_widget_show(opengamefile);
  gtk_box_pack_start(GTK_BOX(box),opengamefile,0,0,0);
  w1 = gtk_label_new("Saved game file: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(opengamefile),w1,0,0,0);
  opengamefiletext = gtk_entry_new();
  gtk_widget_show(opengamefiletext);
  gtk_box_pack_start(GTK_BOX(opengamefile),opengamefiletext,0,0,0);

  w2 = gtk_hbox_new(0,0);
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(box),w2,0,0,0);
  w1 = gtk_label_new("Player ID: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
  openidtext = gtk_entry_new();
  gtk_widget_show(openidtext);
  gtk_entry_set_text(GTK_ENTRY(openidtext),idt);
  gtk_box_pack_start(GTK_BOX(w2),openidtext,0,0,0);

  w2 = gtk_hbox_new(0,0);
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(box),w2,0,0,0);
  w1 = gtk_label_new("Name: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
  opennametext = gtk_entry_new();
  gtk_widget_show(opennametext);
  if ( ! nt || !nt[0] ) nt = getenv("LOGNAME");
  if ( ! nt ) nt = getlogin(); /* may need to be in sysdep.c */
  gtk_entry_set_text(GTK_ENTRY(opennametext),nt);
  gtk_box_pack_start(GTK_BOX(w2),opennametext,0,0,0);

  /* Now some stuff for when this panel is in its personality as
     a start game panel */
  opengamepanel = bbox = gtk_vbox_new(0,dialog_vert_spacing);
  /* gtk_widget_show(bbox); */
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  for ( i = 0 ; i < 3 ; i++ ) {
    static const char *playerlabs[] = { "Second player:" , "Third player:",
					"Fourth player:" };

    w2 = gtk_hbox_new(0,dialog_button_spacing);
    gtk_widget_show(w2);
    gtk_box_pack_start(GTK_BOX(bbox),w2,0,0,0);
    w1 = gtk_label_new(playerlabs[i]);
    gtk_widget_show(w1);
    gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
    openplayercheckboxes[i] = w1 = 
      gtk_check_button_new_with_label("Start computer player   ");
    gtk_widget_show(w1);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w1),1);
    gtk_box_pack_end(GTK_BOX(w2),w1,0,0,0);
    gtk_signal_connect(GTK_OBJECT(w1),"toggled",GTK_SIGNAL_FUNC(openbut_callback),(gpointer)(intptr_t)(10+i));
    openplayeroptionboxes[i] = w2 = gtk_hbox_new(0,dialog_button_spacing);
    gtk_widget_show(w2);
    gtk_box_pack_start(GTK_BOX(bbox),w2,0,0,0);
    w1 = gtk_label_new(" Name:");
    gtk_widget_show(w1);
    gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
    openplayernames[i] = w1 = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w1),robot_names[i]);
    gtk_widget_show(w1);
    gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
    w1 = gtk_label_new("  Options:");
    gtk_widget_show(w1);
    gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
    openplayeroptions[i] = w1 = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(w1),robot_options[i]);
    gtk_widget_show(w1);
    gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
  }

  openallowdisconnectbutton = w1 =
    gtk_check_button_new_with_label("Allow disconnection from game");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  opensaveonexitbutton = w1 =
    gtk_check_button_new_with_label("Save game state on exit");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  openrandomseatsbutton = w1 =
    gtk_check_button_new_with_label("Seat players randomly");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  opentimeoutspinbutton = w1 = 
    gtk_spin_button_new(GTK_ADJUSTMENT(gtk_adjustment_new(1.0*game_get_option_entry_from_table(&prefs_table,GOTimeout,NULL)->value.optnat,0.0,300.0,1.0,10.0,0.0)),
			0.0,0);
  gtk_widget_show(w1);
  opentimeout = w2 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
  w1 = gtk_label_new("seconds allowed for claims");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
  gtk_box_pack_start(GTK_BOX(bbox),w2,0,0,0);

  w1 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  openconnectbutton = w2 = gtk_button_new_with_label("Connect");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(open_connection),0);
  openstartbutton = w2 = gtk_button_new_with_label("Start Game");
  /* gtk_widget_show(w2); */
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(open_connection),(gpointer)1);
  openresumebutton = w2 = gtk_button_new_with_label("Resume Game");
  /* gtk_widget_show(w2); */
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(open_connection),(gpointer)2);
  w2 = gtk_button_new_with_label("Cancel");
  gtk_widget_show(w2);
  gtk_box_pack_end(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(open_dialog));
  /* initialize dialog values */
  open_dialog_popup(NULL,(gpointer)-1);
}

/* if data is -1, just set the values of the open dialog fields,
   to be picked up by do_connect */
void open_dialog_popup(GtkWidget *w UNUSED, gpointer data) {
  int new = 0, join = 0, resume = 0;
  char buf[256];
  char ht[256], pt[10], ft[256], idt[10];
  int usehost = 1;
  ht[0] = pt[0] = idt[0] = ft[0] = 0;
  if ( strchr(address,':') ) {
    /* grrr */
    if ( address[0] == ':' ) {
      strcpy(ht,"localhost");
      strcpy(pt,address+1);
    } else {
      sscanf(address,"%[^:]:%s",ht,pt);
    }
  } else {
    strcpy(ft,address);
    usehost = 0;
  }
  /* set the default id to be our current id */
  sprintf(buf,"%d",our_id);

  if ( (int)(intptr_t)data == 1 ) new = 1;
  if ( (int)(intptr_t)data == 0 ) join = 1;
  if ( (int)(intptr_t)data == 2 ) resume = 1;
  gtk_entry_set_text(GTK_ENTRY(openidtext),buf);
  /* set the host and port etc from the address */
  gtk_widget_set_sensitive(openhost,usehost);
  gtk_entry_set_text(GTK_ENTRY(openhosttext),ht);
  gtk_widget_set_sensitive(openport,usehost);
  gtk_entry_set_text(GTK_ENTRY(openporttext),pt);
  gtk_widget_set_sensitive(openfile,!usehost);
  gtk_entry_set_text(GTK_ENTRY(openfiletext),ft);

  if ( (int)(intptr_t)data == -1 ) return;

  if ( join ) {
    gtk_widget_show(openhost);
  } else {
    gtk_widget_hide(openhost);
    gtk_entry_set_text(GTK_ENTRY(openhosttext),"localhost");
  }

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(usehost ? openhosttoggle : openunixtoggle),1);


  if ( resume ) gtk_widget_show(opengamefile);
  else gtk_widget_hide(opengamefile);
  if ( new || resume ) gtk_widget_show(opengamepanel);
  else gtk_widget_hide(opengamepanel);
  if ( new ) gtk_widget_show(opentimeout);
  else gtk_widget_hide(opentimeout);
  if ( new ) gtk_widget_show(openrandomseatsbutton);
  else gtk_widget_hide(openrandomseatsbutton);
  if ( new ) gtk_widget_show(openstartbutton);
  else gtk_widget_hide(openstartbutton);
  if ( join ) gtk_widget_show(openconnectbutton);
  else gtk_widget_hide(openconnectbutton);
  if ( resume ) gtk_widget_show(openresumebutton);
  else gtk_widget_hide(openresumebutton);

  dialog_popup(open_dialog,DPCentredOnce);

  if ( new ) gtk_widget_grab_focus(openstartbutton);
  if ( join ) gtk_widget_grab_focus(openconnectbutton);
  if ( resume ) gtk_widget_grab_focus(openresumebutton);
}


ACCELFUN(turn)

/* the turn dialog: buttons for Discard (also declares specs),
   Kong (concealed or adding to pung), Mah Jong */
void turn_dialog_init(void) {
  GtkWidget *box, *butbox, *w;

  if ( turn_dialog ) {
    gtk_widget_destroy(turn_dialog);
    turn_dialog = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    turn_dialog = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),turn_dialog,0,0);
    break;
  case DialogsBelow:
    turn_dialog = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(dialoglowerbox),turn_dialog,1,0,0);
    break;
  case DialogsPopup:
    turn_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (turn_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  }

  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(turn_dialog),box);

  butbox = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(butbox);
  gtk_box_pack_end(GTK_BOX(box),butbox,0,0,0);
    
  turn_dialog_label = w = gtk_label_new("Select tile and:");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);

  turn_accel = gtk_accel_group_new();

  w = gtk_button_new_with_label("Discard");
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  /* so other function can set it */
  turn_dialog_discard_button = w;
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgDiscard);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",turn_accel,GDK_d,0,0);
#else
  gtk_accel_group_add(turn_accel,GDK_d,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");
    
  w = gtk_button_new_with_label("& Calling");
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  /* so other function can set it */
  turn_dialog_calling_button = w;
  /* this assumes knowledge that protocol enums don't go above
     1000000 */
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)(PMsgDiscard+1000000));
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",turn_accel,GDK_c,0,0);
#else
  gtk_accel_group_add(turn_accel,GDK_c,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"  _");
    
  w = gtk_button_new_with_label("Kong");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgDeclareClosedKong);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",turn_accel,GDK_k,0,0);
#else
  gtk_accel_group_add(turn_accel,GDK_k,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");
    
  w = gtk_button_new_with_label("Mah Jong!");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgMahJong);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",turn_accel,GDK_m,0,0);
#else
  gtk_accel_group_add(turn_accel,GDK_m,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");

  gtk_signal_connect(GTK_OBJECT(turn_dialog),"hide",GTK_SIGNAL_FUNC(add_or_remove_turn_accels),(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(turn_dialog),"show",GTK_SIGNAL_FUNC(add_or_remove_turn_accels),(gpointer)1);

}
      
/* dialog for scoring phase: forming closed sets */
static GtkWidget *scoring_done, *scoring_special;

ACCELFUN(scoring)

void scoring_dialog_init(void) {
  GtkWidget *box, *butbox, *w;
  
  if ( scoring_dialog ) {
    gtk_widget_destroy(scoring_dialog);
    scoring_dialog = NULL;
  }

  switch ( dialogs_position ) {
  case DialogsCentral:
  case DialogsUnspecified:
    scoring_dialog = gtk_event_box_new();
    gtk_fixed_put(GTK_FIXED(discard_area),scoring_dialog,0,0);
    break;
  case DialogsBelow:
    scoring_dialog = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(dialoglowerbox),scoring_dialog,1,0,0);
    break;
  case DialogsPopup:
    scoring_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_signal_connect (GTK_OBJECT (scoring_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  }
  
  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(box),
				 dialog_border_width);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(scoring_dialog),box);

  butbox = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(butbox);
  gtk_box_pack_end(GTK_BOX(box),butbox,0,0,0);
  
  w = gtk_label_new("Declare concealed sets\nSelect 1st tile and:");
  gtk_widget_show(w);
  gtk_box_pack_end(GTK_BOX(box),w,0,0,0);

  scoring_accel = gtk_accel_group_new();

  w = gtk_button_new_with_label("Eyes");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgFormClosedPair);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",scoring_accel,GDK_e,0,0);
#else
  gtk_accel_group_add(scoring_accel,GDK_e,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");

  
  w = gtk_button_new_with_label("Chow");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgFormClosedChow);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",scoring_accel,GDK_c,0,0);
#else
  gtk_accel_group_add(scoring_accel,GDK_c,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");
  
  w = gtk_button_new_with_label("Pung");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgFormClosedPung);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",scoring_accel,GDK_p,0,0);
#else
  gtk_accel_group_add(scoring_accel,GDK_p,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");
  
  scoring_special = w = gtk_button_new_with_label("Special Hand");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  gtk_widget_show(w);
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgFormClosedSpecialSet);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",scoring_accel,GDK_s,0,0);
#else
  gtk_accel_group_add(scoring_accel,GDK_s,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");
  
  scoring_done = w = gtk_button_new_with_label("Finished");
  GTK_WIDGET_UNSET_FLAGS(w,GTK_CAN_FOCUS);
  /* gtk_widget_show(w); */ /* uses same space as special hand */
  gtk_box_pack_start(GTK_BOX(butbox),w,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(turn_callback),(gpointer)(intptr_t)PMsgShowTiles);
#ifdef GTK2
 gtk_widget_add_accelerator(GTK_WIDGET(w),"clicked",scoring_accel,GDK_f,0,0);
#else
  gtk_accel_group_add(scoring_accel,GDK_f,0,0,GTK_OBJECT(w),"clicked");
#endif
  gtk_label_set_pattern(GTK_LABEL(GTK_BIN(w)->child),"_");

  gtk_signal_connect(GTK_OBJECT(scoring_dialog),"hide",GTK_SIGNAL_FUNC(add_or_remove_scoring_accels),(gpointer)0);
  gtk_signal_connect(GTK_OBJECT(scoring_dialog),"show",GTK_SIGNAL_FUNC(add_or_remove_scoring_accels),(gpointer)1);

}

void scoring_dialog_popup(void) {
  if ( the_game->player == our_seat ) {
    gtk_widget_show(scoring_special);
    gtk_widget_hide(scoring_done);
  } else {
    gtk_widget_hide(scoring_special);
    gtk_widget_show(scoring_done);
  }
  dialog_popup(scoring_dialog,DPCentredOnce);
}

/* used above - it removes all the accelerators when the focus
   enters the chat widget, to avoid accidents */
static gint mentry_focus_callback(GtkWidget *w UNUSED,GdkEventFocus e UNUSED,gpointer u UNUSED) {
  add_or_remove_discard_accels(NULL,0);
  add_or_remove_chow_accels(NULL,0);
  add_or_remove_ds_accels(NULL,0);
  add_or_remove_turn_accels(NULL,0);
  add_or_remove_scoring_accels(NULL,0);
  return 0;
}

/* close a widget, saving its position for next open */
void close_saving_posn(GtkWidget *w) {
  gint x,y;
  gdk_window_get_deskrelative_origin(w->window,&x,&y);
  gtk_widget_set_uposition(w,x,y);
  /* gtk2 seems to lose the information over unmap/map */
  gtk_object_set_data(GTK_OBJECT(w),"position-set",(gpointer)1);
  gtk_object_set_data(GTK_OBJECT(w),"position-x",(gpointer)(intptr_t)x);
  gtk_object_set_data(GTK_OBJECT(w),"position-y",(gpointer)(intptr_t)y);
  gtk_widget_hide(w);
}

/* the textwindow for scoring information etc */
void textwindow_init(void) {
  int i;
  GtkWidget 
#ifndef GTK2
    *sbar, 
#endif
    *obox, *box, *tmp, *lbl,*textw;

  textwindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (textwindow), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  /* must allow shrinking */
  gtk_window_set_policy(GTK_WINDOW(textwindow),TRUE,TRUE,FALSE);
  gtk_window_set_title(GTK_WINDOW(textwindow),"Scoring calculations");

  // we used to set the size here, but I think that's better done
  // on popup

  obox = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(obox),dialog_border_width);
  gtk_widget_show(obox);
  gtk_container_add(GTK_CONTAINER(textwindow),obox);

  scoring_notebook = gtk_notebook_new();
  GTK_WIDGET_UNSET_FLAGS(scoring_notebook,GTK_CAN_FOCUS);
  gtk_notebook_set_homogeneous_tabs(GTK_NOTEBOOK(scoring_notebook),1);
  gtk_widget_show(scoring_notebook);
  gtk_box_pack_start(GTK_BOX(obox),scoring_notebook,1,1,0);
  
  for (i=0;i<5;i++) {
#ifdef GTK2
    textw = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(textw),FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(textw),FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(textw),GTK_WRAP_WORD_CHAR);
#else
    GtkStyle *s;
    textw = gtk_text_new(NULL,NULL);
#endif
    gtk_widget_show(textw);
    textpages[i] = textw;
#ifdef GTK2
    box = gtk_scrolled_window_new(NULL,NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(box),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(box),textw);
#else
    sbar = gtk_vscrollbar_new(GTK_TEXT (textw)->vadj);
    gtk_widget_show(sbar);
    box = gtk_hbox_new(0,0);
    gtk_box_pack_start(GTK_BOX(box),sbar,0,0,0);
    gtk_box_pack_start(GTK_BOX(box),textw,1,1,0);
#endif
    gtk_widget_show(box);
    lbl = gtk_label_new((i==4) ? "Settlement" : "");
    gtk_widget_show(lbl);
    textlabels[i] = lbl;
    gtk_notebook_append_page(GTK_NOTEBOOK(scoring_notebook),box,lbl);
    gtk_widget_realize(textw);
#ifndef GTK2
    if ( fixed_font ) {
      s = gtk_style_copy(gtk_widget_get_style(textw));
      s->font = fixed_font;
      gtk_widget_set_style(textw,s);
    }
#endif
  }

  tmp = gtk_button_new_with_label("Close");
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(textwindow));
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(obox),tmp,0,0,0);
  gtk_widget_grab_focus(tmp);
}

/* the callback when user hits return in the message composition window */
static void mentry_callback(GtkWidget *widget UNUSED,GtkWidget *mentry) {
  const gchar *mentry_text;
  PMsgSendMessageMsg smm;
  mentry_text = gtk_entry_get_text(GTK_ENTRY(mentry));
  smm.type = PMsgSendMessage;
  smm.addressee = 0; /* broadcast only at present ... */
  smm.text = (char *)mentry_text;
  send_packet(&smm);
  gtk_entry_set_text(GTK_ENTRY(mentry),"");
}

static gint mentry_focus_callback(GtkWidget *w,GdkEventFocus e,gpointer u);

/* the window for messages */
void messagewindow_init(void) {
  GtkWidget *obox, *box, *tmp, *mentry, *label;
#ifndef GTK2
  GtkWidget *sbar;
  GtkStyle *s;
#endif

  if ( !info_windows_in_main ) {
    messagewindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_signal_connect (GTK_OBJECT (messagewindow), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
    /* must allow shrinking */
    gtk_window_set_policy(GTK_WINDOW(messagewindow),TRUE,TRUE,FALSE);
    gtk_window_set_title(GTK_WINDOW(messagewindow),"Messages");
    /* reasonable size is ... */
    gtk_window_set_default_size(GTK_WINDOW(messagewindow),400,300);
  }

  obox = gtk_vbox_new(0,info_windows_in_main ? 0 : dialog_vert_spacing);
  if ( !info_windows_in_main ) {
    gtk_container_set_border_width(GTK_CONTAINER(obox),dialog_border_width);
  }
  gtk_widget_show(obox);
  if ( info_windows_in_main ) {
    messagewindow = obox;
    gtk_box_pack_end(GTK_BOX(info_box),messagewindow,1,1,0);
  } else {
    gtk_container_add(GTK_CONTAINER(messagewindow),obox);
  }

#ifdef GTK2
  messagetext = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(messagetext),FALSE);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(messagetext),FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(messagetext),GTK_WRAP_WORD_CHAR);
  messagetextbuf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (messagetext));
  gtk_text_buffer_get_iter_at_offset (messagetextbuf, &messagetextiter, 0);
#else
  messagetext = gtk_text_new(NULL,NULL);
#endif
  if ( info_windows_in_main ) {
    gtk_widget_set_usize(messagetext,0,50);
  }
  gtk_widget_show(messagetext);
#ifdef GTK2
  box = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(box),messagetext);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(box),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
#else
  sbar = gtk_vscrollbar_new(GTK_TEXT (messagetext)->vadj);
  gtk_widget_show(sbar);
  box = gtk_hbox_new(0,0);
  gtk_box_pack_start(GTK_BOX(box),sbar,0,0,0);
  gtk_box_pack_start(GTK_BOX(box),messagetext,1,1,0);
#endif
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(obox),box,1,1,0);
  gtk_widget_realize(messagetext);
#ifndef GTK2
  if ( fixed_font ) {
    s = gtk_style_copy(gtk_widget_get_style(messagetext));
      s->font = fixed_font;
    gtk_widget_set_style(messagetext,s);
  }
#endif

  GTK_WIDGET_UNSET_FLAGS(messagetext,GTK_CAN_FOCUS); /* entry widget shd focus */

  label = gtk_label_new(info_windows_in_main ? "Chat:" : "Send message:");
  GTK_WIDGET_UNSET_FLAGS(label,GTK_CAN_FOCUS);
  gtk_misc_set_alignment(GTK_MISC(label),0.0,0.5);
  gtk_widget_show(label);

  /* the thing for sending messages */
  message_entry = mentry = gtk_entry_new();
  gtk_widget_show(mentry);
  gtk_signal_connect(GTK_OBJECT(mentry),"activate",GTK_SIGNAL_FUNC(mentry_callback),mentry);
  gtk_signal_connect(GTK_OBJECT(mentry),"focus-in-event",GTK_SIGNAL_FUNC(mentry_focus_callback),mentry);

  if ( !info_windows_in_main ) {
    gtk_box_pack_start(GTK_BOX(obox),label,0,0,0);
    gtk_box_pack_start(GTK_BOX(obox),mentry,0,0,0);
  } else {
    GtkWidget *w = gtk_hbox_new(0,0);
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(w),label,0,0,0);
    gtk_box_pack_start(GTK_BOX(w),mentry,0,0,0);
    mfocus = gtk_check_button_new_with_label("keep cursor here");
    GTK_WIDGET_UNSET_FLAGS(mfocus,GTK_CAN_FOCUS);
    gtk_widget_show(mfocus);
    gtk_box_pack_start(GTK_BOX(w),mfocus,0,0,0);
    gtk_box_pack_start(GTK_BOX(obox),w,0,0,0);
  }


  if ( !info_windows_in_main ) {
    tmp = gtk_button_new_with_label("Close");
    GTK_WIDGET_UNSET_FLAGS(tmp,GTK_CAN_FOCUS); /* entry widget shd focus */
    gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(messagewindow));
    gtk_widget_show(tmp);
    gtk_box_pack_end(GTK_BOX(obox),tmp,0,0,0);
    /* god knows how focus would work if this happened in the topwindow! */
    gtk_widget_grab_focus(mentry);
  }
}

/* the window for showing warnings */
void warningwindow_init(void) {
  GtkWidget *obox, *box, *tmp, *label;
#ifndef GTK2
  GtkWidget *sbar;
  GtkStyle *s;
#endif

  if ( warningwindow ) { gtk_widget_destroy(warningwindow); }
  warningwindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (warningwindow), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  /* must allow shrinking */
  gtk_window_set_policy(GTK_WINDOW(warningwindow),TRUE,TRUE,FALSE);
  gtk_window_set_title(GTK_WINDOW(warningwindow),"Warnings");
  /* reasonable size is ... */
  gtk_window_set_default_size(GTK_WINDOW(warningwindow),400,300);

  obox = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(obox),dialog_border_width);
  gtk_widget_show(obox);
  gtk_container_add(GTK_CONTAINER(warningwindow),obox);

  label = gtk_label_new(
    "Apart from when a player disconnects,\n"
      "warnings usually indicate a bug or other\n"
      "unexpected situation. If in doubt,\n"
      "please mail mahjong@stevens-bradfield.com\n"
      "with the warning text and a description\n"
      "of the situation in which it occurred.");
  GTK_WIDGET_UNSET_FLAGS(label,GTK_CAN_FOCUS);
  gtk_widget_show(label);
  gtk_box_pack_start(GTK_BOX(obox),label,0,0,0);

#ifdef GTK2
  warningtext = gtk_text_view_new();
  warningtextbuf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (warningtext));
  gtk_text_buffer_get_iter_at_offset (warningtextbuf, &warningtextiter, 0);
#else
  warningtext = gtk_text_new(NULL,NULL);
#endif
  gtk_widget_show(warningtext);
#ifdef GTK2
  box = gtk_scrolled_window_new(NULL,NULL);
  gtk_container_add(GTK_CONTAINER(box),warningtext);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(box),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
#else
  sbar = gtk_vscrollbar_new(GTK_TEXT (warningtext)->vadj);
  gtk_widget_show(sbar);
  box = gtk_hbox_new(0,0);
  gtk_box_pack_start(GTK_BOX(box),sbar,0,0,0);
  gtk_box_pack_start(GTK_BOX(box),warningtext,1,1,0);
#endif
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(obox),box,1,1,0);
  gtk_widget_realize(warningtext);
#ifndef GTK2
  if ( fixed_font ) {
    s = gtk_style_copy(gtk_widget_get_style(warningtext));
      s->font = fixed_font;
    gtk_widget_set_style(warningtext,s);
  }
#endif

  GTK_WIDGET_UNSET_FLAGS(warningtext,GTK_CAN_FOCUS);

  box = gtk_hbox_new(0,0);
  gtk_widget_show(box);
  gtk_box_pack_end(GTK_BOX(obox),box,0,0,0);

  tmp = gtk_button_new_with_label("Clear Warnings");
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(warning_clear),NULL);
  gtk_widget_show(tmp);
  gtk_box_pack_start(GTK_BOX(box),tmp,1,1,0);

  tmp = gtk_button_new_with_label("Close");
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(warningwindow));
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(box),tmp,1,1,0);

  /* check for cached warnings */
  log_msg_add(0,NULL);
}

static void warningraise(void) {
  showraise(warningwindow);
}

static int warning_numbers = 0;


void warning_clear(void) {
  warning_numbers = 0;
#ifdef GTK2
  gtk_text_buffer_set_text(GTK_TEXT_BUFFER(warningtextbuf),"",0);
  gtk_text_buffer_get_iter_at_offset (warningtextbuf, &warningtextiter, 0);
#else
  gtk_editable_delete_text(GTK_EDITABLE(warningtext),0,-1);
#endif
  gtk_widget_hide(warningentry);
  close_saving_posn(warningwindow);
}

static GtkWidget *debug_options_reporting; /* query dialog */

static void file_report(char *warning) {
  SOCKET fd;
  static int in_progress = 0;
  static int waiting_permission = 0;
  /* don't enter this routine twice.
     However, if we're waiting for permission, the permission is
     given by calling this routine with a non-null or null argument.
  */
  if ( in_progress && ! waiting_permission ) return;
  if ( debug_reports == DebugReportsNever ) return;
  in_progress = 1;
  if ( debug_reports != DebugReportsAlways ) {
    static char saved_warning[50000];
    if ( waiting_permission  ) {
      waiting_permission = 0;
      gtk_widget_hide(debug_options_reporting);
      control_server_processing(1);
      if ( warning ) {
	// permission received
	warning = saved_warning;
      } else {
	// permission denied
	in_progress = 0;
	return;
      }
    } else {
      // need to ask for permission
      waiting_permission = 1;
      // save the warning text
      strmcpy(saved_warning,warning,49999);
      // disable processing of input from the server, to avoid confusion
      control_server_processing(0);
      debug_report_query_popup();
      return;
    }
  }
  // now file the report
  fd = plain_connect_to_host("www.stevens-bradfield.com:9000");
  if ( fd == INVALID_SOCKET ) {
    warn("unable to file error report");
  } else {
    static char header[] = "From: <nobody@nobody.nobody>\n"
      "To: mahjong@stevens-bradfield.com\n"
      "Subject: XMJ internal error report\n"
      "\n"
      "XMJ version: ";
    char msg[1024];
    int n;
    put_data(fd,header,strlen(header));
    put_data(fd,(char *)version,strlen(version));
    put_data(fd,"\n",1);
    put_data(fd,warning,strlen(warning));
    put_data(fd,NULL,0);
    n = get_data(fd,msg,1023);
    if ( n > 0 ) {
      info_dialog_popup(msg);
    }
    plain_close_socket(fd);
  }
  in_progress = 0;
}


/* a second argument of NULL just checks for stashed warnings to be
   transferred to the window */
int log_msg_add(LogLevel l,char *warning) {
  static char buf[1024] = "";
  if ( warning && l >= LogWarning) warning_numbers++;
  /* if the warning window is not currently available,
     cache the warning until it is */
  if ( warningwindow == NULL ) {
    if ( warning ) {
      strncat(buf,warning,1023-strlen(buf));
    }
  } else {
    if ( buf[0] ) {
#ifdef GTK2
      gtk_text_buffer_insert(warningtextbuf,&warningtextiter,buf,-1);
      gtk_text_buffer_get_end_iter(warningtextbuf,&warningtextiter);
#else
      gtk_text_insert(GTK_TEXT(warningtext),NULL,NULL,NULL,buf,-1);
#endif
      buf[0] = 0;
    }
    if ( warning ) {
#ifdef GTK2
      gtk_text_buffer_insert(warningtextbuf,&warningtextiter,warning,-1);
      gtk_text_buffer_get_end_iter(warningtextbuf,&warningtextiter);
#else
      gtk_text_insert(GTK_TEXT(warningtext),NULL,NULL,NULL,warning,-1);
#endif
    }
    if ( warning_numbers > 0 ) gtk_widget_show(warningentry);
    /* if the log level exceeds warning, try to file a report,
       and give user a message */
    if ( l > LogWarning ) {
      file_report(warning);
      error_dialog_popup("The program has encountered an internal error.\n"
	"This will probably cause it to crash very soon. Sorry!");
    }
  }
  /* tell warn to print the warning anyway */
  return 0;
}

/* callback for saving as */
static void save_callback(GtkWidget *w UNUSED, gpointer data UNUSED) {
  PMsgSaveStateMsg m;

  m.type = PMsgSaveState;
  m.filename = (char *)gtk_entry_get_text(GTK_ENTRY(save_text));
  send_packet(&m);
  close_saving_posn(save_window);
}

/* window for Save As ... function */
static void save_init(void) {
  GtkWidget *box, *bbox, *w1, *tmp;


  save_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (save_window), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(save_window),
				 dialog_border_width);

  bbox = gtk_vbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_container_add(GTK_CONTAINER(save_window),bbox);

  w1 = gtk_label_new("Save as:");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  box = gtk_hbox_new(0,0);
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(bbox),box,0,0,0);

  save_text = gtk_entry_new();
  gtk_widget_show(save_text);
  gtk_box_pack_start(GTK_BOX(box),save_text,0,0,0);

  w1 = gtk_label_new(".mjs");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);

  box = gtk_hbox_new(0,0);
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(bbox),box,0,0,0);

  tmp = gtk_button_new_with_label("Save");
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(save_callback),0);
  gtk_widget_show(tmp);
  gtk_box_pack_start(GTK_BOX(box),tmp,1,1,0);

  tmp = gtk_button_new_with_label("Cancel");
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(save_window));
  gtk_widget_show(tmp);
  gtk_box_pack_start(GTK_BOX(box),tmp,1,1,0);

}

/* callback for password */
static void password_callback(GtkWidget *w UNUSED, gpointer data UNUSED) {
  PMsgAuthInfoMsg m;

  m.type = PMsgAuthInfo;
  strcpy(m.authtype,"basic");
  m.authdata = (char *)gtk_entry_get_text(GTK_ENTRY(password_text));
  send_packet(&m);
  close_saving_posn(password_window);
}

/* window for password request */
static void password_init(void) {
  GtkWidget *box, *bbox, *w1, *tmp;

  password_window = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_signal_connect (GTK_OBJECT (password_window), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(password_window),
				 dialog_border_width);

  bbox = gtk_vbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_container_add(GTK_CONTAINER(password_window),bbox);

  w1 = gtk_label_new("Password required:");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  box = gtk_hbox_new(0,0);
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(bbox),box,0,0,0);

  password_text = gtk_entry_new();
  gtk_widget_show(password_text);
  gtk_box_pack_start(GTK_BOX(box),password_text,0,0,0);

  box = gtk_hbox_new(0,0);
  gtk_widget_show(box);
  gtk_box_pack_start(GTK_BOX(bbox),box,0,0,0);

  tmp = gtk_button_new_with_label("OK");
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(password_callback),0);
  gtk_widget_show(tmp);
  gtk_box_pack_start(GTK_BOX(box),tmp,1,1,0);

  gtk_widget_show(tmp);
  gtk_box_pack_start(GTK_BOX(box),tmp,1,1,0);

}


/* radio buttons for dialog positions */
static GtkWidget *display_option_dialog_dialogposn[DialogsPopup+1];
/* checkbox for animation */
static GtkWidget *display_option_dialog_animation;
/* checkbox for nopopups */
static GtkWidget *display_option_dialog_nopopups;
/* checkbox for tiletips */
static GtkWidget *display_option_dialog_tiletips;
/* check box for iconify option */
static GtkWidget *display_option_dialog_iconify;
/* check box for info in main */
static GtkWidget *display_option_dialog_info_in_main;
/* radiobuttons for show wall */
static GtkWidget *display_option_dialog_showwall[3];
static GtkWidget *display_option_dialog_tileset, *display_option_dialog_tileset_path;
/* text entry for size */
static GtkWidget *display_option_size_entry;
/* radio buttons for sort tiles */
static GtkWidget *display_option_dialog_sort_tiles[3];
#ifdef GTK2
/* text entry for gtk2rc file */
static GtkWidget *display_option_dialog_gtk2_rcfile;
/* checkbox entry for use_system_gtkrc */
static GtkWidget *display_option_dialog_use_system_gtkrc;
#endif

static void display_option_dialog_apply(void);
static void display_option_dialog_save(void);
static void display_option_dialog_refresh(void);

/* things used below */
static GtkWidget *mfontwindow;
static GtkWidget *mfont_selector;
static void mfontsel_callback(GtkWidget *w UNUSED, gpointer data) {
  if ( data ) {
    /* use default */
    main_font_name[0] = 0;
  } else {
    /* use selection */
    strmcpy(main_font_name,gtk_font_selection_get_font_name(GTK_FONT_SELECTION(mfont_selector)),256);
  }
  gtk_widget_hide(mfontwindow);
}

static GtkWidget *tfontwindow;
static GtkWidget *tfont_selector;
static void tfontsel_callback(GtkWidget *w UNUSED, gpointer data) {
  if ( data ) {
    /* use default */
    text_font_name[0] = 0;
  } else {
    /* use selection */
    strmcpy(text_font_name,gtk_font_selection_get_font_name(GTK_FONT_SELECTION(tfont_selector)),256);
  }
  gtk_widget_hide(tfontwindow);
}

static GtkWidget *tcolwindow;
static GtkWidget *tcolor_selector;
static void tcolsel_callback(GtkWidget *w UNUSED, gpointer data) {
  if ( data ) {
    /* use default */
    table_colour_name[0] = 0;
  } else {
    /* use selection */
    gdouble c[4];
    gtk_color_selection_get_color(GTK_COLOR_SELECTION(tcolor_selector),c);
#ifdef GTK2
    sprintf(table_colour_name,"#%04X%04X%04X",(int)(0xFFFF*c[0]),(int)(0xFFFF*c[1]),(int)(0xFFFF*c[2]));
#else
    sprintf(table_colour_name,"rgb:%04X/%04X/%04X",(int)(0xFFFF*c[0]),(int)(0xFFFF*c[1]),(int)(0xFFFF*c[2]));
#endif
  }
  gtk_widget_hide(tcolwindow);
}

/* window for display options */
static void display_option_dialog_init(void) {
  GtkWidget *box, *bbox, *hbox, *but1, *but2, *but3, *w1, *w2;

  display_option_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_signal_connect (GTK_OBJECT (display_option_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(display_option_dialog),
				 dialog_border_width);

  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(display_option_dialog),box);

  bbox = gtk_vbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  w1 = gtk_label_new("Position of action dialogs:");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  hbox = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(bbox),hbox,0,0,0);

  but1 = gtk_radio_button_new_with_label(NULL,"central");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(hbox),but1,0,0,0);
  display_option_dialog_dialogposn[DialogsCentral] = but1 ;

  but2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"below");
  gtk_widget_show(but2);
  gtk_box_pack_start(GTK_BOX(hbox),but2,0,0,0);
  display_option_dialog_dialogposn[DialogsBelow] = but2;

  but3 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"popup");
  gtk_widget_show(but3);
  gtk_box_pack_start(GTK_BOX(hbox),but3,0,0,0);
  display_option_dialog_dialogposn[DialogsPopup] = but3;

  /* animation */
  display_option_dialog_animation = but1 = 
    gtk_check_button_new_with_label("Animation");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* info in main */
  display_option_dialog_info_in_main = but1 =
    gtk_check_button_new_with_label("Display status and messages in main window");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* nopopups */
  display_option_dialog_nopopups = but1 =
    gtk_check_button_new_with_label("Don't popup scoring/message windows");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* tiletips */
  display_option_dialog_tiletips = but1 = 
    gtk_check_button_new_with_label("Tiletips always shown");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* display size */
  hbox = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(box),hbox,0,0,0);
  w1 = gtk_label_new("Display size (in tile-widths):");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(hbox),w1,0,0,0);
  w1 = gtk_combo_new();
  gtk_entry_set_max_length(GTK_ENTRY(GTK_COMBO(w1)->entry),6);
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(hbox),w1,0,0,0);
  { GList *gl = NULL;
  gl = g_list_append(gl,"19");
  gl = g_list_append(gl,"18");
  gl = g_list_append(gl,"17");
  gl = g_list_append(gl,"16");
  gl = g_list_append(gl,"15");
  gl = g_list_append(gl,"14");
  gl = g_list_append(gl,"(auto)");
  gtk_combo_set_popdown_strings(GTK_COMBO(w1),gl);
  }
  gtk_entry_set_editable(GTK_ENTRY(GTK_COMBO(w1)->entry),FALSE);
  display_option_size_entry = GTK_COMBO(w1)->entry;

  /* showwall setting */
  bbox = gtk_vbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  w1 = gtk_label_new("Show the wall:");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  hbox = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(bbox),hbox,0,0,0);

  but1 = gtk_radio_button_new_with_label(NULL,"always");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(hbox),but1,0,0,0);
  display_option_dialog_showwall[1] = but1 ;

  but2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"when room");
  gtk_widget_show(but2);
  gtk_box_pack_start(GTK_BOX(hbox),but2,0,0,0);
  display_option_dialog_showwall[2] = but2;

  but3 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"never");
  gtk_widget_show(but3);
  gtk_box_pack_start(GTK_BOX(hbox),but3,0,0,0);
  display_option_dialog_showwall[0] = but3;

  /* sort tiles setting */
  bbox = gtk_vbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  w1 = gtk_label_new("Sort tiles in hand:");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  hbox = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(hbox);
  gtk_box_pack_start(GTK_BOX(bbox),hbox,0,0,0);

  but1 = gtk_radio_button_new_with_label(NULL,"always");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(hbox),but1,0,0,0);
  display_option_dialog_sort_tiles[SortAlways] = but1 ;

  but2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"during deal");
  gtk_widget_show(but2);
  gtk_box_pack_start(GTK_BOX(hbox),but2,0,0,0);
  display_option_dialog_sort_tiles[SortDeal] = but2;

  but3 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but1)),"never");
  gtk_widget_show(but3);
  gtk_box_pack_start(GTK_BOX(hbox),but3,0,0,0);
  display_option_dialog_sort_tiles[SortNever] = but3;

  /* iconify */
  display_option_dialog_iconify = but1 = 
    gtk_check_button_new_with_label("Iconify all windows with main");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* tileset name */
  bbox = gtk_hbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  w1 = gtk_label_new("Tileset: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);
  
  display_option_dialog_tileset = w1 = gtk_entry_new();
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);
  
  /* tileset path */
  bbox = gtk_hbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  w1 = gtk_label_new("Tileset Path: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);
  
  display_option_dialog_tileset_path = w1 = gtk_entry_new();
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  /* deal with the selection of the system font */
  { GtkWidget *box, *tmp, *w, *fsdial;
    if ( mfontwindow ) { gtk_widget_destroy(mfontwindow); }
    mfontwindow = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_window_set_title(GTK_WINDOW(mfontwindow),"Main font selection");
    box = gtk_vbox_new(0,dialog_vert_spacing);
    gtk_widget_show(box);
    gtk_container_add(GTK_CONTAINER(mfontwindow),box);
    mfont_selector = fsdial = gtk_font_selection_new();
    gtk_widget_show(fsdial);
    gtk_font_selection_set_preview_text(GTK_FONT_SELECTION(fsdial),
      "I hope you're enjoying the game");
    gtk_box_pack_start(GTK_BOX(box),fsdial,0,0,0);
    tmp = gtk_hbox_new(1,dialog_button_spacing);
    gtk_widget_show(tmp);
    gtk_box_pack_start(GTK_BOX(box),tmp,0,0,0);

    w = gtk_button_new_with_label("Select");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(mfontsel_callback),(gpointer) 0);
    
    w = gtk_button_new_with_label("Use default");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(mfontsel_callback),(gpointer) 1);
    
    w = gtk_button_new_with_label("Cancel");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect_object(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(gtk_widget_hide),GTK_OBJECT(mfontwindow));
  }

  w1 = gtk_button_new_with_label("Main font selection...");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w1),"clicked",GTK_SIGNAL_FUNC(showraise),GTK_OBJECT(mfontwindow));

  /* deal with the selection of the text font */
  { GtkWidget *box, *tmp, *w, *fsdial;
    if ( tfontwindow ) { gtk_widget_destroy(tfontwindow); }
    tfontwindow = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_window_set_title(GTK_WINDOW(tfontwindow),"Text font selection");
    box = gtk_vbox_new(0,dialog_vert_spacing);
    gtk_widget_show(box);
    gtk_container_add(GTK_CONTAINER(tfontwindow),box);
    tfont_selector = fsdial = gtk_font_selection_new();
    gtk_widget_show(fsdial);
    gtk_font_selection_set_preview_text(GTK_FONT_SELECTION(fsdial),
      "I hope you're enjoying the game");
    gtk_box_pack_start(GTK_BOX(box),fsdial,0,0,0);
    tmp = gtk_hbox_new(1,dialog_button_spacing);
    gtk_widget_show(tmp);
    gtk_box_pack_start(GTK_BOX(box),tmp,0,0,0);

    w = gtk_button_new_with_label("Select");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(tfontsel_callback),(gpointer) 0);
    
    w = gtk_button_new_with_label("Use default");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(tfontsel_callback),(gpointer) 1);
    
    w = gtk_button_new_with_label("Cancel");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect_object(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(gtk_widget_hide),GTK_OBJECT(tfontwindow));
  }

  w1 = gtk_button_new_with_label("Text font selection...");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w1),"clicked",GTK_SIGNAL_FUNC(showraise),GTK_OBJECT(tfontwindow));

  /* deal with selection of table colour */
  { GtkWidget *box, *tmp, *w, *csdial;
    if ( tcolwindow ) { gtk_widget_destroy(tcolwindow); }
    tcolwindow = gtk_window_new(GTK_WINDOW_DIALOG);
    gtk_window_set_title(GTK_WINDOW(tcolwindow),"Table colour selection");
    box = gtk_vbox_new(0,dialog_vert_spacing);
    gtk_widget_show(box);
    gtk_container_add(GTK_CONTAINER(tcolwindow),box);
    tcolor_selector = csdial = gtk_color_selection_new();
    gtk_widget_show(csdial);
    gtk_box_pack_start(GTK_BOX(box),csdial,0,0,0);
    tmp = gtk_hbox_new(1,dialog_button_spacing);
    gtk_widget_show(tmp);
    gtk_box_pack_start(GTK_BOX(box),tmp,0,0,0);

    w = gtk_button_new_with_label("Select");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(tcolsel_callback),(gpointer) 0);
    
    w = gtk_button_new_with_label("Use default");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(tcolsel_callback),(gpointer) 1);
    
    w = gtk_button_new_with_label("Cancel");
    gtk_widget_show(w);
    gtk_box_pack_start(GTK_BOX(tmp),w,1,1,0);
    gtk_signal_connect_object(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(gtk_widget_hide),GTK_OBJECT(tcolwindow));
  }


  w1 = gtk_button_new_with_label("Table colour selection...");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w1),"clicked",GTK_SIGNAL_FUNC(showraise),GTK_OBJECT(tcolwindow));

#ifdef GTK2
  /* gtk2rcfile */
  bbox = gtk_hbox_new(0,0);
  gtk_widget_show(bbox);
  gtk_box_pack_start(GTK_BOX(box),bbox,0,0,0);

  w1 = gtk_label_new("Gtk2 Rcfile: ");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);
  
  display_option_dialog_gtk2_rcfile = w1 = gtk_entry_new();
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(bbox),w1,0,0,0);

  /* use_system_gtkrc  */
  display_option_dialog_use_system_gtkrc = but1 = 
    gtk_check_button_new_with_label("Use system gtkrc");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

#endif

  /* apply, save and close buttons */
  w1 = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(w1);
  gtk_box_pack_end(GTK_BOX(box),w1,0,0,0);

  w2 = gtk_button_new_with_label("Save & Apply");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(display_option_dialog_save),NULL);

  w2 = gtk_button_new_with_label("Apply (no save)");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,1,1,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(display_option_dialog_apply),NULL);

  w2 = gtk_button_new_with_label("Cancel");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,1,1,0);
  gtk_signal_connect_object(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(gtk_widget_hide),GTK_OBJECT(display_option_dialog));
}

static char old_main_font_name[256];
static char old_text_font_name[256];
static char old_table_colour_name[256];

/* make the panel match reality */
static void display_option_dialog_refresh(void) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_dialogposn[dialogs_position]),1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_animation),animate);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_nopopups),nopopups);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_info_in_main),info_windows_in_main);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_tiletips),tiletips);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_showwall[pref_showwall == -1 ? 2 : pref_showwall]),1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_sort_tiles[sort_tiles]),1);
  if ( display_size == 0 ) {
    gtk_entry_set_text(GTK_ENTRY(display_option_size_entry),"(auto)");
  } else {
    char buf[5];
    sprintf(buf,"%d",display_size);
    gtk_entry_set_text(GTK_ENTRY(display_option_size_entry),buf);
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_iconify),iconify_dialogs_with_main);
  gtk_entry_set_text(GTK_ENTRY(display_option_dialog_tileset),
    tileset ? tileset : "");
#ifdef GTK2
  gtk_entry_set_text(GTK_ENTRY(display_option_dialog_gtk2_rcfile),
    gtk2_rcfile);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(display_option_dialog_use_system_gtkrc),use_system_gtkrc);
#endif
  gtk_entry_set_text(GTK_ENTRY(display_option_dialog_tileset_path),
    tileset_path ? tileset_path : "");
  strmcpy(old_main_font_name,main_font_name,256);
  if ( main_font_name[0] ) {
    gtk_font_selection_set_font_name(GTK_FONT_SELECTION(mfont_selector),
      main_font_name);
  }
  strmcpy(old_text_font_name,text_font_name,256);
  if ( text_font_name[0] ) {
    gtk_font_selection_set_font_name(GTK_FONT_SELECTION(tfont_selector),
      text_font_name);
  } else if ( fallback_text_font_name[0] ) {
    gtk_font_selection_set_font_name(GTK_FONT_SELECTION(tfont_selector),
      fallback_text_font_name);
  }
  strmcpy(old_table_colour_name,table_colour_name,256);
}

/* apply the selected options */
static void display_option_dialog_apply(void) {
  int i;
  unsigned int newdp = dialogs_position;
  int old;
  char *newt;
  GtkWidget *wwindow;
  int restart = 0; /* set to 1 if we need to recreate the display */
  
  /* if we produce any warnings now, and then restart, they won't be seen
     in the gui, since the warning window will be destroyed.
     So disable the warning window, and reinstate it later */
  wwindow = warningwindow;
  warningwindow = NULL;

  for (i=DialogsCentral; i <= DialogsPopup; i++) {
    if ( GTK_TOGGLE_BUTTON(display_option_dialog_dialogposn[i])->active ) 
      newdp = i;
  }
  if ( newdp != dialogs_position ) restart = 1;
  dialogs_position = newdp;
  set_animation(GTK_TOGGLE_BUTTON(display_option_dialog_animation)->active);
  old = info_windows_in_main;
  info_windows_in_main = GTK_TOGGLE_BUTTON(display_option_dialog_info_in_main)->active;
  if ( old != info_windows_in_main ) restart = 1;
  nopopups = GTK_TOGGLE_BUTTON(display_option_dialog_nopopups)->active;
  tiletips = GTK_TOGGLE_BUTTON(display_option_dialog_tiletips)->active;
  iconify_dialogs_with_main = GTK_TOGGLE_BUTTON(display_option_dialog_iconify)->active;
  i = 
    (GTK_TOGGLE_BUTTON(display_option_dialog_showwall[0])->active ? 0 :
     GTK_TOGGLE_BUTTON(display_option_dialog_showwall[1])->active ? 1 : -1 );
  if ( pref_showwall != i ) {
    restart = 1;
    showwall = pref_showwall = i;
  }
  for (i=0;i<3 
	 && !GTK_TOGGLE_BUTTON(display_option_dialog_sort_tiles[i])->active;
       i++);
  if ( i < 3 ) sort_tiles = i;
  old = display_size;
  newt = (char *)gtk_entry_get_text(GTK_ENTRY(display_option_size_entry));
  if ( strcmp(newt,"(auto)") == 0 ) {
    display_size = 0;
  } else {
    sscanf(newt,"%d",&display_size);
  }
  if ( display_size != old ) restart = 1;
  newt = (char *)gtk_entry_get_text(GTK_ENTRY(display_option_dialog_tileset));
  if ( ! tileset ) tileset = "";
  if ( strcmp(newt,tileset) != 0 ) {
    restart = 1;
    tileset = newt;
  }
  newt = (char *)gtk_entry_get_text(GTK_ENTRY(display_option_dialog_tileset_path));
  if ( ! tileset_path ) tileset_path = "";
  if ( strcmp(newt,tileset_path) != 0 ) {
    restart = 1;
    tileset_path = newt;
  }
#ifdef GTK2
  newt = (char *)gtk_entry_get_text(GTK_ENTRY(display_option_dialog_gtk2_rcfile));
  if ( strcmp(newt,gtk2_rcfile) != 0 ) {
    /* we can't restart for this one */
    error_dialog_popup("Restart xmj for gtk2_rcfile change to take effect");
    strmcpy(gtk2_rcfile,newt,sizeof(gtk2_rcfile));
  }
  old = use_system_gtkrc;
  use_system_gtkrc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(display_option_dialog_use_system_gtkrc));
  if ( old != use_system_gtkrc ) {
    /* we can't restart for this one */
    error_dialog_popup("Restart xmj for use_system_gtkrc change to take effect");
  }
#endif
  if ( strncmp(old_main_font_name,main_font_name,256) != 0 ) {
#ifdef GTK2
    char c[300];
    strcpy(c,"gtk-font-name = \"");
    char *d = c + strlen(c);
    strmcpy(d,main_font_name,256);
    strcat(c,"\"");
    gtk_rc_parse_string(c);
    gtk_rc_reset_styles(gtk_settings_get_default());
#else
    restart = 1;
#endif
  }
  if ( strncmp(old_text_font_name,text_font_name,256) != 0 ) {
#ifdef GTK2
    char c[300];
    strcpy(c,"style \"text\" { font_name = \"");
    char *d = c + strlen(c);
    strmcpy(d,text_font_name,256);
    strcat(c,"\" }");
    gtk_rc_parse_string(c);
    gtk_rc_reset_styles(gtk_settings_get_default());
#else
    restart = 1;
#endif
  }
  if ( strncmp(old_table_colour_name,table_colour_name,256) != 0 ) {
#ifdef GTK2
    char c[300];
    strcpy(c,"style \"table\" { bg[NORMAL] = \"");
    char *d = c + strlen(c);
    strmcpy(d,table_colour_name,256);
    strcat(c,"\" }");
    gtk_rc_parse_string(c);
    gtk_rc_reset_styles(gtk_settings_get_default());
#else
    restart = 1;
#endif
  }
  close_saving_posn(display_option_dialog);
  if ( restart ) {
    /* need to destroy the warning window ourselves, since
       we've nulled it */
    if ( wwindow ) gtk_widget_destroy(wwindow);
    destroy_display();
    create_display();
  } else {
    /* re-instate warnings */
    warningwindow = wwindow;
    log_msg_add(0,NULL);
  }
}

/* save options */
static void display_option_dialog_save(void) {
  /* first apply */
  display_option_dialog_apply();
  /* then save */
  if ( read_or_update_rcfile(NULL,XmjrcNone,XmjrcDisplay) == 0 ) {
    error_dialog_popup("Error updating rc file");
  }
}

/* various widgets needed in the playing prefs dialog */
static GtkWidget *playing_prefs_auto_specials;
static GtkWidget *playing_prefs_auto_losing;
static GtkWidget *playing_prefs_auto_winning;

static void playing_prefs_dialog_apply(void);
static void playing_prefs_dialog_save(void);
static void playing_prefs_dialog_refresh(void);

/* dialog for specifying playing preferences */
static void playing_prefs_dialog_init(void) {
  GtkWidget *box, *but1, *w1, *w2;

  playing_prefs_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_signal_connect (GTK_OBJECT (playing_prefs_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(playing_prefs_dialog),
				 dialog_border_width);

  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(playing_prefs_dialog),box);

  w1 = gtk_label_new("Automatic declarations:");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);

  /* declaring specials */
  playing_prefs_auto_specials = but1 = 
    gtk_check_button_new_with_label("flowers and seasons");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* declaring losing hands */
  playing_prefs_auto_losing = but1 = 
    gtk_check_button_new_with_label("losing hands");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* declaring winning hands */
  playing_prefs_auto_winning = but1 = 
    gtk_check_button_new_with_label("winning hands");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);

  /* apply, save and close buttons */
  w1 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w1);
  gtk_box_pack_end(GTK_BOX(box),w1,0,0,0);

  w2 = gtk_button_new_with_label("Save & Apply");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(playing_prefs_dialog_save),NULL);

  w2 = gtk_button_new_with_label("Apply (no save)");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(playing_prefs_dialog_apply),NULL);

  w2 = gtk_button_new_with_label("Cancel");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(gtk_widget_hide),GTK_OBJECT(playing_prefs_dialog));
}

/* make the playing prefs panel match reality */
static void playing_prefs_dialog_refresh(void) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playing_prefs_auto_specials),
    playing_auto_declare_specials);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playing_prefs_auto_losing),
    playing_auto_declare_losing);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(playing_prefs_auto_winning),
    playing_auto_declare_winning);

  gtk_widget_hide(playing_prefs_dialog);
}

/* apply the selected playing options */
static void playing_prefs_dialog_apply(void) {
  playing_auto_declare_specials = GTK_TOGGLE_BUTTON(playing_prefs_auto_specials)->active;
  playing_auto_declare_losing = GTK_TOGGLE_BUTTON(playing_prefs_auto_losing)->active;
  playing_auto_declare_winning = GTK_TOGGLE_BUTTON(playing_prefs_auto_winning)->active;
  gtk_widget_hide(playing_prefs_dialog);
}

/* save playing options */
static void playing_prefs_dialog_save(void) {
  /* first apply */
  playing_prefs_dialog_apply();
  /* then save */
  if ( read_or_update_rcfile(NULL,XmjrcNone,XmjrcPlaying) == 0 ) {
    error_dialog_popup("Error updating rc file");
  }
}

/* debug options dialog */

static void debug_options_dialog_apply(void);
static void debug_options_dialog_save(void);
static void debug_options_dialog_refresh(void);

static GtkWidget *debug_options_report_buttons[DebugReportsAlways+1];

static void debug_options_dialog_init(void);

static void debug_report_query_popup(void) {
  if ( ! debug_options_dialog ) debug_options_dialog_init();
  dialog_popup(debug_options_reporting,DPCentred);
}

static void debug_options_dialog_init(void) {
  GtkWidget *box, *but0, *but1, *but2, *but3, *w1, *w2;

  debug_options_dialog = gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_signal_connect (GTK_OBJECT (debug_options_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(debug_options_dialog),
				 dialog_border_width);

  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(debug_options_dialog),box);
  w1 = gtk_label_new("Fault reporting:\n"
    "When certain internal errors occur, this program\n"
    "can file a debugging report over the Internet to the author.\n"
    "This report includes the current game status; it may include\n"
    "the entire game history. Therefore the report may contain\n"
    "sensitive information such as your username and password for the\n"
    "www.TUMJ.com game server.\n"
    "When may a debugging report be sent over the Internet?");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  
  but0 = gtk_radio_button_new_with_label(NULL,"unspecified");
  /* gtk_widget_show(but0); */
  gtk_box_pack_start(GTK_BOX(box),but0,0,0,0);
  debug_options_report_buttons[DebugReportsUnspecified] = but0;

  but1 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but0)),"never");
  gtk_widget_show(but1);
  gtk_box_pack_start(GTK_BOX(box),but1,0,0,0);
  debug_options_report_buttons[DebugReportsNever] = but1;

  but2 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but0)),"ask each time");
  gtk_widget_show(but2);
  gtk_box_pack_start(GTK_BOX(box),but2,0,0,0);
  debug_options_report_buttons[DebugReportsAsk] = but2;

  but3 = gtk_radio_button_new_with_label(gtk_radio_button_group(GTK_RADIO_BUTTON(but0)),"always");
  gtk_widget_show(but3);
  gtk_box_pack_start(GTK_BOX(box),but3,0,0,0);
  debug_options_report_buttons[DebugReportsAlways] = but3;

  /* apply, save and close buttons */
  w1 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w1);
  gtk_box_pack_end(GTK_BOX(box),w1,0,0,0);

  w2 = gtk_button_new_with_label("Save & Apply");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(debug_options_dialog_save),NULL);

  w2 = gtk_button_new_with_label("Apply (no save)");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(debug_options_dialog_apply),NULL);

  w2 = gtk_button_new_with_label("Cancel");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(gtk_widget_hide),GTK_OBJECT(debug_options_dialog));


  /* Now create the dialog that is popped up each time (if necessary)
     to ask whether to file a report */
  if ( debug_options_reporting ) {
    gtk_widget_destroy(debug_options_reporting);
  }

  debug_options_reporting =  gtk_window_new(GTK_WINDOW_DIALOG);
  gtk_signal_connect (GTK_OBJECT (debug_options_reporting), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_container_set_border_width(GTK_CONTAINER(debug_options_reporting),
				 dialog_border_width);

  box = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(box);
  gtk_container_add(GTK_CONTAINER(debug_options_reporting),box);
  w1 = gtk_label_new("This program has encountered an unexpected error.\n"
    "It can file a debugging report over the Internet to the author.\n"
    "This report includes the current game status; it may include\n"
    "the entire game history. Therefore the report may contain\n"
    "sensitive information such as your username and password for the\n"
    "www.TUMJ.com game server.\n"
    "May the report be sent?");
  gtk_widget_show(w1);
  gtk_box_pack_start(GTK_BOX(box),w1,0,0,0);
  
  w1 = gtk_hbox_new(0,dialog_button_spacing);
  gtk_widget_show(w1);
  gtk_box_pack_end(GTK_BOX(box),w1,0,0,0);

  w2 = gtk_button_new_with_label("Send report");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(file_report),(gpointer)1);

  w2 = gtk_button_new_with_label("Cancel");
  gtk_widget_show(w2);
  gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
  gtk_signal_connect_object(GTK_OBJECT(w2),"clicked",GTK_SIGNAL_FUNC(file_report),NULL);

}

static void debug_options_dialog_refresh(void) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(debug_options_report_buttons[debug_reports]),1);
}

static void debug_options_dialog_apply(void) {
  int i;
  for ( i=DebugReportsNever; i <= DebugReportsAlways; i++ ) {
    if ( GTK_TOGGLE_BUTTON(debug_options_report_buttons[i])->active ) 
      debug_reports = i;
  }
  gtk_widget_hide(debug_options_dialog);
}

static void debug_options_dialog_save(void) {
  /* first apply */
  debug_options_dialog_apply();
  /* then save */
  if ( read_or_update_rcfile(NULL,XmjrcNone,XmjrcMisc) == 0 ) {
    error_dialog_popup("Error updating rc file");
  }
}



/* callback used below */
static void enabler_callback(GtkWidget *w UNUSED, gpointer data)
{
  GameOptionEntry *goe = data;
  goe->enabled = ! goe->enabled;
  make_or_refresh_option_updater(goe,0);
}

/* ditto */
static void make_sensitive(GtkWidget *w)
{
  gtk_widget_set_sensitive(w,1);
}

/* given a GameOptionEntry, create
   or refresh an updater widget, stored in the userdata
   field of the entry.
   Second arg says if this is preference panel (or option panel)
*/
GtkWidget *make_or_refresh_option_updater(GameOptionEntry *goe,int prefsp)
{
  /*      description of option 
          current value   new value  update
  */
  GtkWidget *vbox = NULL, *hbox = NULL, *ohbox, *w1 = NULL, *w2,
    *old, *lim, *halflim, *nolim, *resetter;
  GtkAdjustment *adj;
  int dbls, pts;
  int centilims;

  old = goe->userdata;
  if ( ! old ) {
    resetter = gtk_button_new_with_label("Reset"); /* needed early */
  } else {
    resetter = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(old),"reset");
  }
  if ( ! old ) {
    vbox = gtk_vbox_new(0,5);
    gtk_widget_show(vbox);
    if ( prefsp ) {
      w2 = gtk_hbox_new(0,5);
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(vbox),w2,0,0,0);
      w1 = gtk_button_new_with_label("Add pref");
      GTK_WIDGET_UNSET_FLAGS(w1,GTK_CAN_FOCUS);
      gtk_object_set_data(GTK_OBJECT(vbox),"enabled",(gpointer) w1);
      gtk_signal_connect(GTK_OBJECT(w1),"clicked",GTK_SIGNAL_FUNC(enabler_callback),(gpointer) goe);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
      w1 = gtk_label_new(goe->desc);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(w2),w1,0,0,0);
    } else {
      w1 = gtk_label_new(goe->desc);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(vbox),w1,0,0,0);
    }
    if ( prefsp ) {
      ohbox = gtk_hbox_new(0,0);
      gtk_widget_show(ohbox);
      gtk_box_pack_start(GTK_BOX(vbox),ohbox,0,0,0);
      hbox = gtk_hbox_new(0,0);
      gtk_object_set_data(GTK_OBJECT(vbox),"hbox",(gpointer) hbox);
      gtk_widget_show(hbox);
      gtk_box_pack_start(GTK_BOX(ohbox),hbox,1,1,0);
    } else {
      hbox = gtk_hbox_new(0,0);
      gtk_widget_show(hbox);
      gtk_box_pack_start(GTK_BOX(vbox),hbox,0,0,0);
    }
  } else {
    vbox = old;
  }
  switch ( goe->type ) {
  case GOTBool:
    if ( ! old ) {
      w1 = gtk_hbox_new(0,0);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(hbox),w1,0,0,5);
      w2 = gtk_check_button_new_with_label("on/off");
      gtk_signal_connect_object(GTK_OBJECT(w2),"toggled",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"checkbox",w2);
    } else {
      w2 = gtk_object_get_data(GTK_OBJECT(vbox),"checkbox");
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w2),
				 goe->value.optbool);
    break;
  case GOTNat:
  case GOTInt:
    if ( ! old ) {
      w1 = gtk_hbox_new(0,0);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(hbox),w1,0,0,5);
      adj = (GtkAdjustment *)gtk_adjustment_new(goe->value.optint,
						(goe->type == GOTNat ) ? 0 : -1000000,1000000,
						1,10,0);
      w2 = gtk_spin_button_new(adj,1.0,0);
      gtk_widget_set_usize(w2,70,0);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(w2),1);
      gtk_signal_connect_object(GTK_OBJECT(w2),"changed",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"spinbutton",w2);
    } else {
      w2 = gtk_object_get_data(GTK_OBJECT(vbox),"spinbutton");
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w2),goe->value.optint);
    }
    break;
  case GOTString:
    if ( ! old ) {
      w1 = gtk_hbox_new(0,0);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(hbox),w1,0,0,5);
      w2 = gtk_entry_new();
      gtk_signal_connect_object(GTK_OBJECT(w2),"changed",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"entry",w2);
    } else {
      w2 = gtk_object_get_data(GTK_OBJECT(vbox),"entry");
    }
    gtk_entry_set_text(GTK_ENTRY(w2),goe->value.optstring);
    break;
  case GOTScore:
    pts = goe->value.optscore % 10000;
    dbls = (goe->value.optscore / 10000) % 100;
    centilims = goe->value.optscore/1000000;
    if ( ! old ) {
      w1 = gtk_hbox_new(0,0);
      gtk_widget_show(w1);
      gtk_box_pack_start(GTK_BOX(hbox),w1,0,0,5);
      lim = w2 = gtk_radio_button_new_with_label(NULL,"lim");
      gtk_signal_connect_object(GTK_OBJECT(w2),"toggled",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"lim",w2);
      halflim = w2 = gtk_radio_button_new_with_label(gtk_radio_button_group (GTK_RADIO_BUTTON(lim)),"1/2 lim");
      gtk_signal_connect_object(GTK_OBJECT(w2),"toggled",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"halflim",w2);
      nolim = w2 = gtk_radio_button_new_with_label(gtk_radio_button_group (GTK_RADIO_BUTTON(lim)),"");
      gtk_signal_connect_object(GTK_OBJECT(w2),"toggled",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"nolim",w2);
    } else {
      lim = gtk_object_get_data(GTK_OBJECT(vbox),"lim");
      halflim = gtk_object_get_data(GTK_OBJECT(vbox),"halflim");
      nolim = gtk_object_get_data(GTK_OBJECT(vbox),"nolim");
    }
    switch ( centilims ) {
    case 100:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lim),1);
      break;
    case 50:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(halflim),1);
      break;
    case 0:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(nolim),1);
      break;
    default:
      warn("Unexpected fraction (%d) of a limit in score; setting to limit",
	   centilims);
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(lim),1);
      break;
    }

    if ( ! old ) {
      adj = (GtkAdjustment *)gtk_adjustment_new(dbls,
						0,100,
						1,10,0);
      w2 = gtk_spin_button_new(adj,1.0,0);
      gtk_widget_set_usize(w2,40,0);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(w2),1);
      gtk_signal_connect_object(GTK_OBJECT(w2),"changed",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"dbls",w2);
      w2 = gtk_label_new("dbls");
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
    } else {
      w2 = gtk_object_get_data(GTK_OBJECT(vbox),"dbls");
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w2),dbls);
    }

    if ( ! old ) {
      adj = (GtkAdjustment *)gtk_adjustment_new(pts,
						0,10000,
						1,10,0);
      w2 = gtk_spin_button_new(adj,1.0,0);
      gtk_widget_set_usize(w2,60,0);
      gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(w2),1);
      gtk_signal_connect_object(GTK_OBJECT(w2),"changed",GTK_SIGNAL_FUNC(make_sensitive),GTK_OBJECT(resetter));
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,5);
      gtk_object_set_data(GTK_OBJECT(vbox),"pts",w2);
      w2 = gtk_label_new("pts");
      gtk_widget_show(w2);
      gtk_box_pack_start(GTK_BOX(w1),w2,0,0,0);
    } else {
      w2 = gtk_object_get_data(GTK_OBJECT(vbox),"pts");
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(w2),pts);
    }
    break;
  }
  if ( ! old ) {
    /* The button is created at the top so we can feed it to callbacks *
       w1 = gtk_button_new_with_label("Reset");
    */
    w1 = resetter; 
    gtk_widget_show(w1);
    gtk_box_pack_end(GTK_BOX(hbox),w1,0,0,5);
    gtk_object_set_data(GTK_OBJECT(vbox),"reset",w1);
    gtk_signal_connect(GTK_OBJECT(w1),"clicked",GTK_SIGNAL_FUNC(option_reset_callback),
		       (gpointer) goe);
  }
  if ( ! old ) goe->userdata = vbox;
  /* If there's an enabled button, deal with it */
  w1 = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(vbox),"enabled");
  if ( w1 ) {
    hbox = (GtkWidget *)gtk_object_get_data(GTK_OBJECT(vbox),"hbox");
    if ( goe->enabled ) {
      gtk_widget_show(hbox);
      gtk_label_set_text(GTK_LABEL(GTK_BIN(w1)->child),"Remove pref");
    } else { 
      gtk_widget_hide(hbox);
      gtk_label_set_text(GTK_LABEL(GTK_BIN(w1)->child),"Add pref");
    }
  }
  /* make the reset button insensitive */
  gtk_widget_set_sensitive(resetter,0);
  return vbox;
}

/* given an optiontable, pack a list of updating widgets into
   a vbox; or just refresh if already there (2nd arg)
   If third arg is 1, this is prefs panel, else options panel
 */
static GtkWidget *build_or_refresh_optionprefs_panel(GameOptionTable *got, GtkWidget *panel, int prefsp)
{
  int i;
  GtkWidget *vbox,*hs,*u;
  int first = 1;
  
  if ( ! got ) {
    /* destroy the panel's children, if any */
    if ( panel ) {
      gtk_container_foreach(GTK_CONTAINER(panel),(GtkCallback) gtk_widget_destroy,NULL);
    }
  }

  if ( ! panel ) {
    vbox = gtk_vbox_new(0,0);
    gtk_widget_show(vbox);
  } else {
    vbox = panel;
  }
  for ( i = 0 ; got && i < got->numoptions; i++ ) {
    /* if this is actually a filler unknown option, skip it */
    if ( got->options[i].name[0] == 0 ) continue;
    /* likewise for end */
    if ( got->options[i].option == GOEnd ) continue;
    /* if this is the options panel, and the option is not enabled, skip */
    if ( ! prefsp && ! got->options[i].enabled ) continue;
    u = got->options[i].userdata;
    if ( ! first && u == NULL ) {
      hs = gtk_hseparator_new();
      gtk_widget_show(hs);
      gtk_box_pack_start(GTK_BOX(vbox),hs,0,0,0);
    } else first = 0;
    if ( u ) {
      make_or_refresh_option_updater(&got->options[i],prefsp);
    } else {
      u = make_or_refresh_option_updater(&got->options[i],prefsp);
      gtk_box_pack_start(GTK_BOX(vbox),u,0,0,5);
    }
  }
  return vbox;
}

GtkWidget *build_or_refresh_option_panel(GameOptionTable *got, GtkWidget *panel)
{
  return build_or_refresh_optionprefs_panel(got,panel,0);
}

GtkWidget *build_or_refresh_prefs_panel(GameOptionTable *got, GtkWidget *panel)
{
  return build_or_refresh_optionprefs_panel(got,panel,1);
}

void option_reset_callback(GtkWidget *w UNUSED, gpointer data)
{
  make_or_refresh_option_updater((GameOptionEntry *)data,0);
}

void option_updater_callback(GtkWidget *w UNUSED, gpointer data UNUSED)
{
  GameOptionTable *got;
  PMsgSetGameOptionMsg psgom;
  char buf[1024];
  int centilims,dbls,pts;
  int i, changed, val;
  
  if ( ! the_game ) return;
  got = &the_game->option_table;
  for ( i = 0 ; i < got->numoptions ; i++ ) {
    w = got->options[i].userdata;
    if ( ! w ) continue;
    changed = 0;
    psgom.type = PMsgSetGameOption;
    strncpy(psgom.optname,got->options[i].name,16);
    switch ( got->options[i].type ) {
    case GOTBool:
      val = GTK_TOGGLE_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"checkbox"))->active;
      psgom.optvalue = val ? "1" : "0";
      if ( val != got->options[i].value.optbool ) changed = 1;
      break;
    case GOTNat:
    case GOTInt:
      gtk_spin_button_update(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"spinbutton")));
      val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"spinbutton")));
      sprintf(buf,"%d",val);
      psgom.optvalue = buf;
      if ( val != got->options[i].value.optint ) changed = 1;
      break;
    case GOTString:
      psgom.optvalue =
	(char *)gtk_entry_get_text(GTK_ENTRY(gtk_object_get_data(GTK_OBJECT(w),"entry")));
      if ( strcmp(psgom.optvalue,got->options[i].value.optstring) != 0 ) changed = 1;
      break;
    case GOTScore:
      centilims = 100*GTK_TOGGLE_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"lim"))->active;
      centilims += 50 * GTK_TOGGLE_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"halflim"))->active;
      gtk_spin_button_update(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"dbls")));
      dbls = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"dbls")));
      gtk_spin_button_update(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"pts")));
      pts = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"pts")));
      val = 1000000*centilims + 10000 * dbls + pts;
      sprintf(buf,"%d",val);
      psgom.optvalue = buf;
      if ( val != got->options[i].value.optscore ) changed = 1;
      break;
    }
    if ( changed ) send_packet(&psgom);
  }
  gtk_widget_hide(game_option_dialog);
}

void prefs_updater_callback(GtkWidget *w UNUSED, gpointer data UNUSED)
{
  GameOptionTable *got;
  char buf[1024];
  int centilims,dbls,pts;
  int i, changed, val;
  char *vals;
  
  got = &prefs_table;
  for ( i = 0 ; i < got->numoptions ; i++ ) {
    w = got->options[i].userdata;
    if ( ! w ) continue;
    changed = 0;
    switch ( got->options[i].type ) {
    case GOTBool:
      val = GTK_TOGGLE_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"checkbox"))->active;
      if ( val != got->options[i].value.optbool ) changed = 1;
      got->options[i].value.optbool = val;
      break;
    case GOTNat:
    case GOTInt:
      gtk_spin_button_update(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"spinbutton")));
      val = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"spinbutton")));
      sprintf(buf,"%d",val);
      if ( val != got->options[i].value.optint ) changed = 1;
      got->options[i].value.optint = val;
      break;
    case GOTString:
      vals =
	(char *)gtk_entry_get_text(GTK_ENTRY(gtk_object_get_data(GTK_OBJECT(w),"entry")));
      if ( strcmp(vals,got->options[i].value.optstring) != 0 ) changed = 1;
      strmcpy(got->options[i].value.optstring,vals,128);
      break;
    case GOTScore:
      centilims = 100*GTK_TOGGLE_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"lim"))->active;
      centilims += 50*GTK_TOGGLE_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"halflim"))->active;
      gtk_spin_button_update(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"dbls")));
      dbls = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"dbls")));
      gtk_spin_button_update(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"pts")));
      pts = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(gtk_object_get_data(GTK_OBJECT(w),"pts")));
      val = 1000000*centilims + 10000 * dbls + pts;
      if ( val != got->options[i].value.optscore ) changed = 1;
      got->options[i].value.optscore = val;
      break;
    }
    if ( changed ) make_or_refresh_option_updater(&got->options[i],1);
  }
  /* now actually save it */
  read_or_update_rcfile(NULL,XmjrcNone,XmjrcGame);
  /* and close */
  gtk_widget_hide(game_prefs_dialog);
}

static void apply_game_prefs_callback(GtkWidget *w UNUSED, gpointer data UNUSED)
{
  apply_game_prefs();
  gtk_widget_hide(game_prefs_dialog);
}


void game_option_init(void)
{
  GtkWidget *sbar, *obox, *bbox, *tmp;

  game_option_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (game_option_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  /* must allow shrinking */
  gtk_window_set_policy(GTK_WINDOW(game_option_dialog),TRUE,TRUE,FALSE);
  gtk_window_set_title(GTK_WINDOW(game_option_dialog),"Current Game Options");
  /* reasonable size is ... */
  gtk_window_set_default_size(GTK_WINDOW(game_option_dialog),450,400);

  obox = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(obox),dialog_border_width);
  gtk_widget_show(obox);
  gtk_container_add(GTK_CONTAINER(game_option_dialog),obox);

  game_option_panel = build_or_refresh_option_panel(the_game ? 
						     &the_game->option_table : NULL,
						     game_option_panel);
  sbar = gtk_scrolled_window_new(NULL,NULL);
  gtk_widget_show(sbar);
  gtk_box_pack_start(GTK_BOX(obox),sbar,1,1,0);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sbar),
					game_option_panel);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sbar),
				 GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);

  bbox = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(bbox);
  gtk_box_pack_end(GTK_BOX(obox),bbox,0,0,0);

  tmp = gtk_button_new_with_label("Close");
  GTK_WIDGET_UNSET_FLAGS(tmp,GTK_CAN_FOCUS); /* entry widget shd focus */
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(game_option_dialog));
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(bbox),tmp,1,1,0);

  game_option_prefs_button = tmp = gtk_button_new_with_label("Apply preferences");
  GTK_WIDGET_UNSET_FLAGS(tmp,GTK_CAN_FOCUS); /* entry widget shd focus */
  gtk_signal_connect(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(apply_game_prefs_callback),NULL);
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(bbox),tmp,1,1,0);

  game_option_apply_button = tmp = gtk_button_new_with_label("Apply changes");
  GTK_WIDGET_UNSET_FLAGS(tmp,GTK_CAN_FOCUS); /* entry widget shd focus */
  gtk_signal_connect(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(option_updater_callback),NULL);
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(bbox),tmp,1,1,0);
}

void game_prefs_init(void)
{
  GtkWidget *sbar, *obox, *bbox, *tmp;

  game_prefs_dialog = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (game_prefs_dialog), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  /* must allow shrinking */
  gtk_window_set_policy(GTK_WINDOW(game_prefs_dialog),TRUE,TRUE,FALSE);
  gtk_window_set_title(GTK_WINDOW(game_prefs_dialog),"Game Preferences");
  /* reasonable size is ... */
  gtk_window_set_default_size(GTK_WINDOW(game_prefs_dialog),450,400);

  obox = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_container_set_border_width(GTK_CONTAINER(obox),dialog_border_width);
  gtk_widget_show(obox);
  gtk_container_add(GTK_CONTAINER(game_prefs_dialog),obox);

  game_prefs_panel = build_or_refresh_prefs_panel(&prefs_table,
						     game_prefs_panel);
  sbar = gtk_scrolled_window_new(NULL,NULL);
  gtk_widget_show(sbar);
  gtk_box_pack_start(GTK_BOX(obox),sbar,1,1,0);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(sbar),
					game_prefs_panel);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sbar),
				 GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);

  bbox = gtk_hbox_new(1,dialog_button_spacing);
  gtk_widget_show(bbox);
  gtk_box_pack_end(GTK_BOX(obox),bbox,0,0,0);

  tmp = gtk_button_new_with_label("Cancel");
  GTK_WIDGET_UNSET_FLAGS(tmp,GTK_CAN_FOCUS); /* entry widget shd focus */
  gtk_signal_connect_object(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(game_prefs_dialog));
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(bbox),tmp,1,1,0);

  tmp = gtk_button_new_with_label("Save changes");
  GTK_WIDGET_UNSET_FLAGS(tmp,GTK_CAN_FOCUS); /* entry widget shd focus */
  gtk_signal_connect(GTK_OBJECT(tmp),"clicked",GTK_SIGNAL_FUNC(prefs_updater_callback),NULL);
  gtk_widget_show(tmp);
  gtk_box_pack_end(GTK_BOX(bbox),tmp,1,1,0);
}

/* set the dialog position, changing if necessary */
void set_dialog_posn(DialogPosition p) {
  if ( p != dialogs_position ) {
    /* if the new position is not below, we need to allow the top
       window to shrink down */
    if ( p != DialogsBelow ) {
      gtk_window_set_policy(GTK_WINDOW(topwindow),1,1,1);
    } else {
      /* don't let it shrink */
      gtk_window_set_policy(GTK_WINDOW(topwindow),0,1,0);
    }
    dialogs_position = p;
    create_dialogs();
  }
}

void set_animation(int a) {
  animate = a;
  /* request appropriate pause time */
  if ( !monitor && the_game ) {
    PMsgSetPlayerOptionMsg spom;
      spom.type = PMsgSetPlayerOption;
    spom.option = PODelayTime;
    spom.ack = 0;
    spom.value = animate ? 10 : 5; /* 0.5 or 1 second min delay */
    spom.text = NULL;
    send_packet(&spom);
  }
}
/* do_chow: if there's only one possible chow, do it, otherwise
   put up a dialog box to choose. Sneakily, this is defined as
   a callback, so that the choosing buttons can call this specifying
   the chow pos, and the main program can call it with AnyPos.
*/

void do_chow(GtkWidget *widg UNUSED, gpointer data) {
  ChowPosition cpos = (ChowPosition)data;
  PMsgChowMsg cm;
  int i,n,j;
  static int lastdiscard;
  TileSet ts;

  cm.type = PMsgChow;
  cm.discard = the_game->serial;

  if ( !monitor && cpos == AnyPos ) {
    /* we want to avoid working and popping up again if
       for some reason we're already popped up. (So that
       this procedure is idempotent.) */
    if ( GTK_WIDGET_VISIBLE(chow_dialog) && cm.discard == lastdiscard )
      return;
    lastdiscard = cm.discard;
    ts.type = Chow;
    /* set up the boxes */
    for (n=0,i=0,j=0;i<3;i++) {
      if ( player_can_chow(our_player,the_game->tile,i) ) {
	ts.tile = the_game->tile - i;
	tilesetbox_set(&chowtsbs[i],&ts,0);
	tilesetbox_highlight_nth(&chowtsbs[i],i);
	gtk_widget_show(chowbuttons[i]);
	n++; j = i;
      } else {
	gtk_widget_hide(chowtsbs[i].widget);
	gtk_widget_hide(chowbuttons[i]);
      }
    }
    /* if there is only one possibility, don't bother to ask.
       Also if there is no possibility: we'll then get an error
       from the server, which saves us having to worry about it */
    if ( n <= 1 ) {
      cpos = j;
    } else {
      /* pop down the discard dialog */
      gtk_widget_hide(discard_dialog->widget);
      dialog_popup(chow_dialog,DPCentred);
    }
  }

  /* Now we might have found the position */
  if ( cpos != AnyPos ) {
    cm.cpos = cpos;
    send_packet(&cm);
    /* if the chowpending flag isn't set, we must be working with an
       old server and be in the mahjonging state. Pop ourselves down,
       so that if something goes wrong it's the discard dialog that
       comes back up */
    if ( ! the_game->chowpending ) gtk_widget_hide(chow_dialog);
  }
}

/* Generic popup centered over main window.
   Positioning is given by
   DPCentred - centered over main window 
   DPOnDiscard - bottom left corner in same place as discard dialog
   DPErrorPos - for error dialogs: centred over top of main window,
    and offset by a multiple of num_error_dialogs
   DPNone - don't touch the positioning at all '
   DPCentredOnce - centre it on first popup, then don't fiddle '
   DPOnDiscardOnce - on discard dialog first time, then don't fiddle '
   If the widget is not a window, then DPCentredOnce and DPNone
   are equivalent to DPCentred, and DPOnDiscardOnce is equivalent to
   DPOnDiscard.
*/
void dialog_popup(GtkWidget *dialog,DPPosn posn) {
  gint x,y,w,h;
  GtkRequisition r = { 0, 0};

  /* So that we don't work if it's already popped up: */
  if ( GTK_WIDGET_VISIBLE(dialog) ) return;

  /* if the position has been set, don't mess */
  if ( GTK_IS_WINDOW(dialog)
    && gtk_object_get_data(GTK_OBJECT(dialog),"position-set") ) {
    gtk_widget_set_uposition(dialog,
      (gint)(intptr_t)gtk_object_get_data(GTK_OBJECT(dialog),"position-x"),
      (gint)(intptr_t)gtk_object_get_data(GTK_OBJECT(dialog),"position-y"));
    gtk_widget_show(dialog);
    // This ought to work, but seems to confuse my wm
    //gtk_window_present(GTK_WINDOW(dialog));
    return;
  }

  if ( ! GTK_IS_WINDOW(dialog) ) {
    if ( posn == DPCentredOnce ) posn = DPCentred;
    if ( posn == DPOnDiscardOnce ) posn = DPOnDiscard;
    if ( posn == DPNone ) posn = DPCentred;
  }

  /* get size of discard widget if necessary */
  if ( (posn == DPOnDiscard || posn == DPOnDiscardOnce) 
       && discard_req.width == 0 ) {
    gtk_widget_size_request(discard_dialog->widget,&discard_req);
    gtk_widget_set_usize(discard_dialog->widget,
			 discard_req.width,
			 discard_req.height);
  }

  gtk_widget_size_request(dialog,&r);

  if ( GTK_IS_WINDOW(dialog) ) {
    gdk_window_get_size(topwindow->window,&w,&h);
    gdk_window_get_deskrelative_origin(topwindow->window,&x,&y);
  } else {
    w = discard_area_alloc.width;
    h = discard_area_alloc.height;
    x = y = 0;
  }

  if ( dialogs_position != DialogsBelow || GTK_IS_WINDOW(dialog) ) {
    if ( posn == DPOnDiscard ) {
      if ( GTK_IS_WINDOW(dialog) )
	gtk_widget_set_uposition(dialog,
	  x + w/2 - r.width/2
	  - (discard_req.width - r.width)/2,
	  y + h/2 - r.height/2
	  + (discard_req.height - r.height)/2);
      else
	gtk_fixed_move(GTK_FIXED(discard_area),dialog,
	  x + w/2 - r.width/2
	  - (discard_req.width - r.width)/2,
	  y + h/2 - r.height/2
	  + (discard_req.height - r.height)/2);
    } else if ( posn == DPOnDiscardOnce ) {
      if ( gtk_object_get_data(GTK_OBJECT(dialog),"position-set") ) {
	/* do nothing */
      } else {
	if ( GTK_IS_WINDOW(dialog) )
	  gtk_widget_set_uposition(dialog,
	    x + w/2 - r.width/2
	    - (discard_req.width - r.width)/2,
	    y + h/2 - r.height/2
	    + (discard_req.height - r.height)/2);
	else
	  gtk_fixed_move(GTK_FIXED(discard_area),dialog,
	    x + w/2 - r.width/2
	    - (discard_req.width - r.width)/2,
	    y + h/2 - r.height/2
	    + (discard_req.height - r.height)/2);
	gtk_object_set_data(GTK_OBJECT(dialog),"position-set",(gpointer)1);
      }
    } else if ( posn == DPCentred ) {
      if ( GTK_IS_WINDOW(dialog) )
	gtk_widget_set_uposition(dialog,
	  x + w/2 - r.width/2,
	  y + h/2 - r.height/2);
      else
	gtk_fixed_move(GTK_FIXED(discard_area),dialog,
	  x + w/2 - r.width/2,
	  y + h/2 - r.height/2);
    } else if ( posn == DPCentredOnce ) {
      if ( gtk_object_get_data(GTK_OBJECT(dialog),"position-set") ) {
	/* do nothing */
#ifdef GTK2
	if ( GTK_IS_WINDOW(dialog) ) {
	  gtk_widget_set_uposition(dialog,
	    (gint)(intptr_t)gtk_object_get_data(GTK_OBJECT(dialog),"position-x"),
	    (gint)(intptr_t)gtk_object_get_data(GTK_OBJECT(dialog),"position-y"));
	}
#endif
      } else {
	if ( GTK_IS_WINDOW(dialog) )
	  gtk_widget_set_uposition(dialog,
	    x + w/2 - r.width/2,
	    y + h/2 - r.height/2);
	else
	  gtk_fixed_move(GTK_FIXED(discard_area),dialog,
	    x + w/2 - r.width/2,
	    y + h/2 - r.height/2);
	gtk_object_set_data(GTK_OBJECT(dialog),"position-set",(gpointer)1);
      }
    } else if ( posn == DPErrorPos ) {
      if ( GTK_IS_WINDOW(dialog) )
	gtk_widget_set_uposition(dialog,
	  x + w/2 - r.width/2,
	  y + h/4 - r.height/2 + 10*num_error_dialogs);
      else
	 gtk_fixed_move(GTK_FIXED(discard_area),dialog,
	   x + w/2 - r.width/2,
	   y + h/4 - r.height/2 + 10*num_error_dialogs);
    }
  }
  gtk_widget_show(dialog);
  if ( GTK_IS_WINDOW(dialog) ) {
    // This ought to work, but seems to confuse my wm
    // gtk_window_present(GTK_WINDOW(dialog));
  } else {
    // This ought to work, but seems to confuse my wm
    // gtk_window_present(GTK_WINDOW(topwindow));
  }
}

/* window to display game status. 
     0      1    2...  3....    
   We are WIND; id: n  Name: name
     total score: nnn
   and for right, opposite, left
*/
static char *status_poses[] = { "We are ", "Right is ", "Opp. is ", "Left is " };
static GtkWidget *status_poslabels[4];
static GtkWidget *status_windlabels[4];
static GtkWidget *status_idlabels[4];
static GtkWidget *status_namelabels[4];
static GtkWidget *status_scorelabels[4];
static GtkWidget *status_pwind;
static GtkWidget *status_status;
static GtkWidget *status_suspended;
static GtkWidget *status_tilesleft;
/* and for the short version */
static GtkWidget *status_chairs[4];

void status_init(void) {
  int i;
  GtkWidget *table, *w;

  if ( info_windows_in_main ) {
    status_window = gtk_hbox_new(0,0);
    gtk_widget_show(status_window);
    gtk_box_pack_start(GTK_BOX(info_box),status_window,0,0,0);
  } else {
    status_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_signal_connect(GTK_OBJECT (status_window), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
    gtk_window_set_title(GTK_WINDOW(status_window),"Game information");
  }
  gtk_container_set_border_width(GTK_CONTAINER(status_window),
    dialog_border_width);

  /* this is what we used to do, and will still do for separate 
     windows */
  if ( ! info_windows_in_main ) {
    table = gtk_table_new(12,4,0);
    gtk_widget_show(table);
    gtk_container_add(GTK_CONTAINER(status_window),table);
    for ( i = 0 ; i < 4 ; i++ ) {
      status_poslabels[i] = w = gtk_label_new(status_poses[i]);
      gtk_widget_show(w);
      gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_LEFT);
      gtk_table_attach_defaults(GTK_TABLE(table),w,
	0,1,2*i,2*i+1);
      status_windlabels[i] = w = gtk_label_new("none");
      gtk_widget_show(w);
      gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_LEFT);
      gtk_table_attach_defaults(GTK_TABLE(table),w,
	1,2,2*i,2*i+1);
      status_idlabels[i] = w = gtk_label_new("  ID: 0");
      gtk_widget_show(w);
      gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_LEFT);
      gtk_table_attach_defaults(GTK_TABLE(table),w,
	2,3,2*i,2*i+1);
      status_namelabels[i] = w = gtk_label_new("  Name: unknown");
      gtk_widget_show(w);
      gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_LEFT);
      gtk_table_attach_defaults(GTK_TABLE(table),w,
	3,4,2*i,2*i+1);
      status_scorelabels[i] = w = gtk_label_new("total score:     0");
      gtk_widget_show(w);
      gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_LEFT);
      gtk_table_attach_defaults(GTK_TABLE(table),w,
	1,4,2*i+1,2*i+2);
      
    }
    status_pwind = w = gtk_label_new("Prevailing wind: none");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,
      0,4,8,9);
    
    status_status = w = gtk_label_new("no game");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,
      0,4,9,10);
    
    status_tilesleft = w = gtk_label_new("");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,
      0,4,10,11);
    
    w = gtk_button_new_with_label("Close");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,
      0,4,11,12);
    gtk_signal_connect_object(GTK_OBJECT(w),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(status_window));

  } else {
    /* and this is for the new in-window information */
    table = gtk_table_new(4,7,0);
    gtk_widget_show(table);
    gtk_container_add(GTK_CONTAINER(status_window),table);
    /* opposite player */
    status_chairs[2] = w = gtk_label_new("(seat empty)");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,1,4,0,1);
    /* left player */
    status_chairs[3] = w = gtk_label_new("(seat empty)");
    gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_LEFT);
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,0,3,1,2);
    /* right player */
    status_chairs[1] = w = gtk_label_new("(seat empty)");
    gtk_label_set_justify(GTK_LABEL(w),GTK_JUSTIFY_RIGHT);
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,2,5,2,3);
    /* us */
    status_chairs[0] = w = gtk_label_new("(seat empty)");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,1,4,3,4);
    /* spacing */
    w = gtk_label_new("     ");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,5,6,0,1);
    /* round */
    status_pwind = w = gtk_label_new("     ");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,6,7,0,1);
    /* game status */
    status_status = w = gtk_label_new("no game");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,6,7,1,2);
    /* tiles left */
    status_tilesleft = w = gtk_label_new("");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,6,7,2,3);
    /* game suspended */
    status_suspended = w = gtk_label_new("");
    gtk_widget_show(w);
    gtk_table_attach_defaults(GTK_TABLE(table),w,6,7,3,4);
  }
}

void status_update(int game_over) {
  int i,s;
  char *pn;
  PlayerP p;
  static char buf[256];
  static char *windnames[] = { "none", "East", "South", "West", "North" };
  static char *shortwindnames[] = { " ", "E", "S", "W", "N" };

  if ( ! the_game ) return;

  for ( i=0 ; i < 4 ; i++ ) {
    s = (our_seat+i)%NUM_SEATS;
    p = the_game->players[s];
    if ( !info_windows_in_main ) {
      gtk_label_set_text(GTK_LABEL(status_windlabels[i]),
	windnames[p->wind]);
      sprintf(buf,"  ID: %d",p->id);
      gtk_label_set_text(GTK_LABEL(status_idlabels[i]),buf);
      snprintf(buf,256,"  Name: %s",p->name);
      gtk_label_set_text(GTK_LABEL(status_namelabels[i]),buf);
      sprintf(buf,"total score: %5d",p->cumulative_score);
      gtk_label_set_text(GTK_LABEL(status_scorelabels[i]),buf);
    } else {
      snprintf(buf,256,p->hand_score >= 0 ? "(%s) %s[%d]: %d (%d)"
	: "(%s) %s[%d]: %d",
	shortwindnames[p->wind],p->name,p->id,p->cumulative_score,
	p->hand_score);
      gtk_label_set_text(GTK_LABEL(status_chairs[i]),buf);
    }
#ifdef GTK2
    if ( showwall ) {
      snprintf(buf,256,p->hand_score >= 0 ? "%s [%d]\n%s\nTotal: %d\nThis hand: %d" : "%s [%d]\n%s\nTotal: %d\nThis hand:   ",
	p->name,p->id,windnames[p->wind],p->cumulative_score,
	p->hand_score);
      gtk_label_set_text(pdisps[i].infolab,buf);
    }
#endif
  }

  sprintf(buf,info_windows_in_main ? "%s round" : "Prevailing wind: %s",
    windnames[the_game->round]);
  gtk_label_set_text(GTK_LABEL(status_pwind),buf);

  pn = (info_windows_in_main ? shortwindnames : windnames)[the_game->player+1]; /* +1 for seat to wind */
  switch (the_game->state) {
  case Dealing:
    sprintf(buf,"Dealing");
    break;
  case DeclaringSpecials:
    sprintf(buf,"%s declaring specials",pn);
    break;
  case Discarding:
    sprintf(buf,"%s to discard",pn);
    break;
  case Discarded:
    sprintf(buf,"%s has discarded",pn);
    break;
  case MahJonging:
    sprintf(buf,"%s has Mah Jong",pn);
    break;
  case HandComplete:
    sprintf(buf,"Hand finished");
    break;
  }
  if ( !info_windows_in_main ) {
    if ( !the_game->active ) strcat(buf," (Play suspended)");
  }
  if ( game_over ) strcpy(buf,"GAME OVER");
  gtk_label_set_text(GTK_LABEL(status_status),buf);
  if ( ! info_windows_in_main ) {
    sprintf(buf,"%3d tiles left + %2d dead tiles",
      the_game->wall.live_end-the_game->wall.live_used,
      the_game->wall.dead_end-the_game->wall.live_end);
  } else {
    sprintf(buf,"%3d tiles left + %2d dead tiles",
      the_game->wall.live_end-the_game->wall.live_used,
      the_game->wall.dead_end-the_game->wall.live_end);
  }
  gtk_label_set_text(GTK_LABEL(status_tilesleft),buf);
  if ( info_windows_in_main ) {
    gtk_label_set_text(GTK_LABEL(status_suspended),
      the_game->active ? "" : "Play suspended");
  }
}

void showraise(GtkWidget *w) {
  if ( GTK_IS_WINDOW(w)
    && gtk_object_get_data(GTK_OBJECT(w),"position-set") ) {
    gtk_widget_set_uposition(w,
      (gint)(intptr_t)gtk_object_get_data(GTK_OBJECT(w),"position-x"),
      (gint)(intptr_t)gtk_object_get_data(GTK_OBJECT(w),"position-y"));
  }
  gtk_widget_show(w);
  gdk_window_raise(w->window);
#ifndef GTK2
  /* I forget why this is there - but it seems to break position
     saving etc. in gtk2 */
  gdk_window_show(w->window);
#endif
}

static void about_init(void) {
  GtkWidget *closebutton, *textw, *vbox;
  static char *abouttxt = 
"This is  xmj , part of the Mah-Jong for Unix (etc)\n"
"set of programs.\n"
"Copyright (c) J. C. Bradfield 2000-2001.\n"
"Distributed under the Gnu General Public License.\n"
"This is version " VERSION " (protocol version " STRINGIFY(PROTOCOL_VERSION) ").\n"
"User documentation is in the  xmj  manual page.\n"
"Latest versions, information etc. may be found at\n"
" http://www.stevens-bradfield.com/MahJong/  .\n"
"Comments and suggestions should be mailed to\n"
" mahjong@stevens-bradfield.com" ;
  
  about_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (about_window), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_window_set_title(GTK_WINDOW(about_window),"About xmj");
  gtk_container_set_border_width(GTK_CONTAINER(about_window),
				 dialog_border_width);

  vbox = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(vbox);
  gtk_container_add(GTK_CONTAINER(about_window),vbox);

#ifdef GTK2
  GtkTextBuffer *textwbuf;
  GtkTextIter textwiter;

  textw = gtk_text_view_new();
  textwbuf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textw));
  gtk_text_buffer_get_iter_at_offset (textwbuf, &textwiter, 0);
#else
  textw = gtk_text_new(NULL,NULL);
#endif
  // let it choose its own
  // gtk_widget_set_usize(textw,300,200);
#ifdef GTK2
  gtk_text_buffer_insert(textwbuf,&textwiter,abouttxt,-1);
#else
  gtk_text_insert(GTK_TEXT(textw),NULL,NULL,NULL,abouttxt,-1);
#endif
  gtk_widget_show(textw);
  GTK_WIDGET_UNSET_FLAGS(textw,GTK_CAN_FOCUS);
  gtk_box_pack_start(GTK_BOX(vbox),textw,0,0,0);
  
  closebutton = gtk_button_new_with_label("Close");
  gtk_widget_show(closebutton);
  gtk_signal_connect_object(GTK_OBJECT(closebutton),"clicked",GTK_SIGNAL_FUNC(close_saving_posn),GTK_OBJECT(about_window));
  gtk_box_pack_start(GTK_BOX(vbox),closebutton,0,0,0);

}

static void nag_callback(GtkWidget *widg UNUSED, gpointer data) {
  nag_state = (int)(intptr_t) data;
  read_or_update_rcfile(NULL,XmjrcNone,XmjrcMisc);
  gtk_widget_hide(nag_window);
} 

static void nag_init(void) {
  GtkWidget *yesbutton, *maybebutton, *nobutton, *textw, *vbox;

#ifdef GTK2
  GtkTextBuffer *textwbuf;
  GtkTextIter textwiter;
#endif

  char buf[1024];
  static char *nagtxt = ""
"Congratulations: you've completed %d full games using\n"
"this set of programs.\n"
"That suggests that you are getting some enjoyment\n"
"out of them.\n"
"\n"
"If you haven't already, perhaps you would like to\n"
"think about making a small donation to me by way of thanks?\n"
"\n"
"You can do this with a credit card via the home page\n"
"hhtp://www.stevens-bradfield.com/MahJong/";
  
  nag_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_signal_connect (GTK_OBJECT (nag_window), "delete_event",GTK_SIGNAL_FUNC(gtk_widget_hide), NULL);
  gtk_window_set_title(GTK_WINDOW(nag_window),"mu4 juan1");
  gtk_container_set_border_width(GTK_CONTAINER(nag_window),
				 dialog_border_width);

  vbox = gtk_vbox_new(0,dialog_vert_spacing);
  gtk_widget_show(vbox);
  gtk_container_add(GTK_CONTAINER(nag_window),vbox);

#ifdef GTK2
  textw = gtk_text_view_new();
#else
  textw = gtk_text_new(NULL,NULL);
#endif
  // Better not to set this, I think
  //gtk_widget_set_usize(textw,400,200);
  sprintf(buf,nagtxt,completed_games);
#ifdef GTK2
  textwbuf = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textw));
  gtk_text_buffer_get_iter_at_offset (textwbuf, &textwiter, 0);
  gtk_text_buffer_insert(textwbuf,&textwiter,buf,-1);
#else
  gtk_text_insert(GTK_TEXT(textw),NULL,NULL,NULL,buf,-1);
#endif
  gtk_widget_show(textw);
  GTK_WIDGET_UNSET_FLAGS(textw,GTK_CAN_FOCUS);
  gtk_box_pack_start(GTK_BOX(vbox),textw,0,0,0);
  
  yesbutton = gtk_button_new_with_label("Yes, I have/will");
  gtk_widget_show(yesbutton);
  gtk_signal_connect(GTK_OBJECT(yesbutton),"clicked",GTK_SIGNAL_FUNC(nag_callback),(gpointer)2);
  gtk_box_pack_start(GTK_BOX(vbox),yesbutton,0,0,0);

  maybebutton = gtk_button_new_with_label("Maybe: remind me again later");
  gtk_widget_show(maybebutton);
  gtk_signal_connect(GTK_OBJECT(maybebutton),"clicked",GTK_SIGNAL_FUNC(nag_callback),(gpointer)0);
  gtk_box_pack_start(GTK_BOX(vbox),maybebutton,0,0,0);

  nobutton = gtk_button_new_with_label("No, I won't");
  gtk_widget_show(nobutton);
  gtk_signal_connect(GTK_OBJECT(nobutton),"clicked",GTK_SIGNAL_FUNC(nag_callback),(gpointer)1);
  gtk_box_pack_start(GTK_BOX(vbox),nobutton,0,0,0);

}

void nag_popup(void) {
  if ( ! nag_window ) nag_init();
  dialog_popup(nag_window,DPCentred);
}

/* create the dialogs that depend on --dialog-XXX */
void create_dialogs(void) {

  if ( dialogs_position == DialogsBelow ) {
    GtkWidget *t;
    t = gtk_event_box_new(); /* so there's a window to have a style */
    gtk_widget_show(t);
    dialoglowerbox = gtk_hbox_new(0,0);
    gtk_widget_show(dialoglowerbox);
    gtk_container_add(GTK_CONTAINER(t),dialoglowerbox);
    gtk_box_pack_end(GTK_BOX(outerframe),t,0,0,0);
    gtk_widget_show(dialoglowerbox->parent);
  }

  /* create the discard dialog */
  discard_dialog_init();

  /* and the chow dialog */
  chow_dialog_init();

  /* and the declaring specials dialog */
  ds_dialog_init();

  /* and create the turn dialog */
  turn_dialog_init();

  /* now create the scoring dialog */
  scoring_dialog_init();
      
  /* and the continue dialog */
  continue_dialog_init();

  /* and the end dialog */
  end_dialog_init();

}

/* and destroy them */
void destroy_dialogs(void)
{
#define zapit(w) if ( w ) gtk_widget_destroy(w) ; w = NULL ;
  zapit(discard_dialog->widget);
  zapit(chow_dialog);
  zapit(ds_dialog);
  zapit(turn_dialog);
  zapit(scoring_dialog);
  zapit(continue_dialog);
}

static void scoring_showraise(void) { showraise(textwindow); }
static void message_showraise(void) { showraise(messagewindow); }
static void about_showraise(void) { 
  if ( ! about_window ) about_init();
  showraise(about_window); 
}

static void save_showraise(void) {
  if ( ! save_window ) save_init();
  showraise(save_window);
}

void password_showraise(void) {
  if ( ! password_window ) password_init();
  showraise(password_window);
}

void status_showraise(void) { 
  if (!nopopups) {
    showraise(status_window);
  }
  status_update(0) ; 
}

static void display_option_dialog_popup(void) {
  if ( ! display_option_dialog ) display_option_dialog_init();
  display_option_dialog_refresh();
  dialog_popup(display_option_dialog,DPCentredOnce) ; 
}

static void playing_prefs_dialog_popup(void) {
  if ( ! playing_prefs_dialog ) playing_prefs_dialog_init();
  playing_prefs_dialog_refresh();
  dialog_popup(playing_prefs_dialog,DPCentredOnce) ; 
}

void debug_options_dialog_popup(void) {
  if ( ! debug_options_dialog ) debug_options_dialog_init();
  debug_options_dialog_refresh();
  dialog_popup(debug_options_dialog,DPCentredOnce) ; 
}

static void game_option_popup(void) {
  int b;
  if ( ! the_game ) return;
  if ( ! game_option_dialog ) game_option_init();
  game_option_panel = build_or_refresh_option_panel(&the_game->option_table,
						    game_option_panel);
  /* we can't apply options if somebody else is the manager */
  b = (the_game->manager == 0 || the_game->manager == our_id);
  gtk_widget_set_sensitive(game_option_apply_button,b);
  gtk_widget_set_sensitive(game_option_prefs_button,b);
  dialog_popup(game_option_dialog,DPNone);
}

static void game_prefs_popup(void) {
  if ( ! game_prefs_dialog ) game_prefs_init();
  read_or_update_rcfile(NULL,XmjrcGame,XmjrcNone);
  game_prefs_panel = build_or_refresh_prefs_panel(&prefs_table,game_prefs_panel);
  dialog_popup(game_prefs_dialog,DPNone);
}

static void save_state(void) {
  PMsgSaveStateMsg m;
  m.type = PMsgSaveState;
  m.filename = NULL;
  send_packet(&m);
}

/* see comment at declaration */
static void grab_focus_if_appropriate(GtkWidget *w) {
  if ( info_windows_in_main ) {
    if ( GTK_TOGGLE_BUTTON(mfocus)->active ) {
      gtk_widget_grab_focus(message_entry);
    } else if ( ! (GTK_HAS_FOCUS & GTK_WIDGET_FLAGS(message_entry)) ) {
      gtk_widget_grab_focus(w);
    }
  } else {
    gtk_widget_grab_focus(w);
  }
}

#ifdef GTK2
#define NULLEX , NULL
#else
#define NULLEX
#endif
/* the menu items */
static GtkItemFactoryEntry menu_items[] = {
  { "/_Game", NULL, NULL, 0, "<Branch>" NULLEX},
  { "/Game/_New local game...", NULL, GTK_SIGNAL_FUNC(open_dialog_popup), 1, NULL NULLEX},
  { "/Game/_Join server...", NULL, GTK_SIGNAL_FUNC(open_dialog_popup), 0, NULL NULLEX},
  { "/Game/_Resume game...", NULL, GTK_SIGNAL_FUNC(open_dialog_popup), 2, NULL NULLEX},
  { "/Game/_Save", NULL, save_state, 0, NULL NULLEX},
  { "/Game/Save _as...", NULL, save_showraise, 0, NULL NULLEX},
  { "/Game/_Close", NULL, GTK_SIGNAL_FUNC(close_connection), 0, NULL NULLEX},
  { "/Game/_Quit", NULL, GTK_SIGNAL_FUNC(exit), 0, NULL NULLEX},
  { "/_Show", NULL, NULL, 0, "<Branch>" NULLEX},
  { "/Show/_Scoring info", NULL, scoring_showraise, 0, NULL NULLEX},
  { "/Show/_Game info", NULL, status_showraise, 0, NULL NULLEX},
  { "/Show/_Messages", NULL, message_showraise, 0, NULL NULLEX},
  { "/Show/_Warnings", NULL, warningraise, 0, NULL NULLEX},
  { "/_Options",NULL,NULL,0,"<Branch>" NULLEX},
  { "/Options/_Display Options...", NULL, display_option_dialog_popup,0,NULL NULLEX},
  { "/Options/_Playing Preferences...",NULL, playing_prefs_dialog_popup,0,NULL NULLEX},
  { "/Options/_Game Option Preferences...", NULL, game_prefs_popup,0,NULL NULLEX},
  { "/Options/_Current Game Options...", NULL, game_option_popup,0,NULL NULLEX},
  { "/Options/_Debugging Options...", NULL, debug_options_dialog_popup,0,NULL NULLEX},
#ifdef GTK2
  /* a button in the title bar doesn't quite work properly in gtk2.
     It seems somehow to lose a click. But since ItemFactory is deprecated,
     there's no chance of a fix. */
  { "/Show _Warnings", NULL, NULL,0,"<Branch>" NULLEX},
  { "/Show _Warnings/Show _Warnings", NULL, warningraise,0,NULL NULLEX},
#else
  { "/Show _Warnings", NULL, warningraise,0,NULL NULLEX},
#endif
  { "/_About", NULL, NULL, 0, "<LastBranch>" NULLEX},
  { "/About/_About xmj", NULL, about_showraise, 0, NULL NULLEX},
};

static const int nmenu_items = sizeof(menu_items)/sizeof(GtkItemFactoryEntry);

/* create the menubar, and return it */
GtkWidget *menubar_create(void) {
  GtkItemFactory *item_factory;
  GtkAccelGroup *accel;
  GtkWidget *m;
  int connected = the_game && the_game->fd;

  accel = gtk_accel_group_new();
  item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR,
				      "<main>",accel);
  gtk_item_factory_create_items(item_factory,nmenu_items,
				menu_items,NULL);
  gtk_window_add_accel_group(GTK_WINDOW(topwindow),accel);

  m = gtk_item_factory_get_widget(item_factory,"<main>");
  gtk_widget_show(m);
  openmenuentry = gtk_item_factory_get_widget(item_factory,
					      "/Game/Join server...");
  assert(openmenuentry);
  gtk_widget_set_sensitive(openmenuentry,!connected);
  newgamemenuentry = gtk_item_factory_get_widget(item_factory,
					      "/Game/New local game...");
  assert(newgamemenuentry);
  gtk_widget_set_sensitive(newgamemenuentry,!connected);
  resumegamemenuentry = gtk_item_factory_get_widget(item_factory,
					      "/Game/Resume game...");
  assert(resumegamemenuentry);
  gtk_widget_set_sensitive(resumegamemenuentry,!connected);
  savemenuentry = gtk_item_factory_get_widget(item_factory,
					      "/Game/Save");
  assert(savemenuentry);
  gtk_widget_set_sensitive(savemenuentry,connected);
  saveasmenuentry = gtk_item_factory_get_widget(item_factory,
					      "/Game/Save as...");
  assert(saveasmenuentry);
  gtk_widget_set_sensitive(saveasmenuentry,connected);
  closemenuentry = gtk_item_factory_get_widget(item_factory,"/Game/Close");
  assert(closemenuentry);
  gtk_widget_set_sensitive(closemenuentry,connected);
  gameoptionsmenuentry = gtk_item_factory_get_widget(item_factory,"/Options/Current Game Options...");
  gtk_widget_set_sensitive(gameoptionsmenuentry,connected);

  warningentry = gtk_item_factory_get_item(item_factory,
    "/Show Warnings");
  assert(warningentry);
  /* This doesn't actually work. But we'll leave it here
     in case it works in gtk 2.0 */
  gtk_menu_item_right_justify(GTK_MENU_ITEM(warningentry));
  gtk_widget_set_name(warningentry,"warningentry");

  /* it's very tedious simply to set this to have red text */
  { 
#ifdef GTK2
    GdkColor c;
    gdk_color_parse("red",&c);
    gtk_widget_modify_fg(gtk_bin_get_child(GTK_BIN(warningentry)),GTK_STATE_NORMAL,&c);
#else
    GtkStyle *s;
    s = gtk_style_copy(gtk_widget_get_default_style());
    gdk_color_parse("red",&(s->fg[GTK_STATE_NORMAL]));
    gtk_widget_set_style(GTK_BIN(warningentry)->child,s);
#endif
  }

  gtk_widget_hide(warningentry);

  /* if the relevant windows are in the main window, zap the menu entries */
  if ( info_windows_in_main ) {
    gtk_widget_destroy(gtk_item_factory_get_widget(item_factory,"/Show/Game info"));
    gtk_widget_destroy(gtk_item_factory_get_widget(item_factory,"/Show/Messages"));
  }

  return m;
}


