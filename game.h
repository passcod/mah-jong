/* $Header: /home/jcb/MahJong/newmj/RCS/game.h,v 12.1 2012/02/01 14:10:41 jcb Rel $
 * game.h
 * This file contains the game data structure and associated definitions,
 * and prototypes for some functions that work on it.
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

#ifndef GAME_H_INCLUDED
#define GAME_H_INCLUDED 1

#include "tiles.h"
#include "player.h"

/* At this point, we should have option definitions. However,
   they're needed by the protocol, and therefore included in
   protocol.h, not this file.
*/

#include "protocol.h"

/* A game is a finite state machine. This type gives the states. */

typedef enum {
  HandComplete = 0, /* a hand has been finished, scored, and scores settled,
		   but the deal has not yet rotated. The player member
		   contains the seat of the player who went mah jong,
		   or -1 if the hand was drawn.
		   This is also the initial state (with seat -1) after
		   a Game message is handled.
		   The claims array is used to note which
		   players have requested to start the next hand.
		    */
  Dealing = 1, /* the deal is in progress */
  DeclaringSpecials = 2, /* the deal is complete, and we are declaring
			flowers and seasons. The player member
			contains the seat (NB) of the player due to declare */
  Discarding = 3, /* the current player is due to discard.
                 The player member contains the current
		 player as a seat. 
		 The tile member notes the tile that was drawn.
		 The whence member tells where the extra tile
		 obtained by the player came from:
		 FromWall, FromDiscard, FromLoose, FromRobbedKong.
		 If it was FromDiscard, the supplier holds
		 the *seat* of the discarder. This is needed for
		 scoring systems that punish such people.
		 The needs records if the player needs to draw
		 another tile, because of, say, declaring specials
		 or kongs. It is FromNone, FromWall, or FromLoose.
		 The konging records if the player has just
		 melded to a pung (or declared a concealed kong) 
		 which is potentially robbable. It is
		 NotKonging (unset), AddingToPung, DeclaringKong; if it is
		 set, then tile is the tile of the kong.
		 The claims record may contain MahJongClaims to indicate
		 a desire to rob the kong.
	      */
  Discarded = 4,  /* the current player has just discarded; other players are
		 making claims. The info record contains the following
		 members:
		 player the player that has discarded
		 tile   the tile that was discarded
		 serial  serial number of this discard
		 claims[seats]  contains
		   Discard/Chow/Pung/Kong/MahJong/NoClaim accordingly, or 0
		   if no claim has yet been received.
		 cpos  the ChowPosition, if the right hand player
                   has made a chow claim
		 chowpending  true if an unspecified chow claim has been
		          granted, but no specified claim has yet
                          been received.
	      */
  MahJonging = 5, /* the current player has  mah jong (which we've verified) and we
                 are declaring the hands
                 The info record contains the following members:
                 player  the player going mah jong
		 whence  where did the mahjong tile come from
		 supplier  if a discard/kong, who discarded (a seat)
		 tile    the tile that completed the hand
		 mjpending  if true, the mahjong was claimed
		          from a discard or robbed kong, but the discard
			  has not yet been declared in a set
		 chowpending mjpending, and the player has declared 
		          a chow without specifying the position.
	      */
} GameState;

/* This is how we index arrays of the players in the current game.
   We used to have  noseat  #defined to be (seats)-1.
   However, negative enums are allowed, so use them! */

typedef enum { east = 0, south = 1, west = 2, north = 3, noseat = -1 } seats;
#define NUM_SEATS 4

/* typedef for where the extra tile came from */
typedef enum { FromNone = 0, FromWall = 1, FromDiscard = 2, 
	       FromLoose = 3, FromRobbedKong = 4 } Whence;

/* typedef for robbing kong info */
typedef enum { NotKonging = 0, AddingToPung = 1, DeclaringKong = 2 } Konging;

/* this typedef represents the possible claims on a discard */
/* It is defined that these are in priority order up to MahJong claim,
   since these are the claims that can be made during the game.
   The remaining values are for use during the claim of the last discard,
   after the MahJong has been granted.
*/
typedef enum { UnknownClaim = 0, NoClaim = 1, ChowClaim = 2,
	       PungClaim = 3, KongClaim = 4, MahJongClaim = 5,
	       PairClaim = 6, SpecialSetClaim = 7
} Claims;

/* this is a bunch of miscellaneous flags used to keep track of
   stuff used in scoring. They are all zeroed at start of hand */
typedef enum {
  /* these two flags are used to detect kong-upon-kong. They are
     both cleared on every discard; Kong is set upon a kong being
     made, and KongUponKong upon a kong being made with Kong set */
  GFKong, GFKongUponKong,
  /* the following two flags relate to letting off a cannon.
     If checking is on, they are set by the routines that check
     for dangerous discards. Otherwise, they should be set in 
     response to a dangerousdiscard message from the controller.
  */
  GFDangerousDiscard, /* cleared on discard, and set if the discard
			 is dangerous and claimed */
  GFNoChoice /* likewise, set if the dangerous discarder had no choice */
} GameFlags;

/* given a pointer to game, test the flag. This returns a boolean. */
#define game_flag(g,f) ((g->flags & (1 << f)) && 1)
/* set, clear */
#define game_setflag(g,f) (g->flags |= (1 << f))
#define game_clearflag(g,f) (g->flags &= ~(1 << f))

/* define a game option table */
typedef struct {
  GameOptionEntry *options;
  int numoptions;
} GameOptionTable;
  
/* This structure represents an active game.
   This is like a protocol GameMsg, plus some other stuff.
*/
typedef struct _Game {
  PlayerP players[NUM_SEATS]; /* the players */
  TileWind round; /* wind of the current round */
  int hands_as_east; /* number of hands completed with this dealer
		      (excluding current hand, except in state
		      HandComplete) */
  int firsteast; /* id of player who was east in first hand of game */
  GameState state; /* the state (see above) of the game */
  /* state dependent extra information */
  int active; /* players may not move unless the game is active */
  char *paused; /* This also stops moves; but it is a pause waiting
		   for players' permission to receive. The non-NULL
		   value is the reason for the pause.
		   If this is set, the ready array records
		   who we're still waiting for */
  /* The next fields are used for lots of auxiliary state information */
  seats player; /* the player doing something */
  Whence whence; /* where did the player's last tile come from? */
  seats supplier; /* and if from a discard, who supplied it? */
  Tile tile; /* and what was it? */
  Whence needs; /* where does the player need to get a tile from? */
  int serial; /* serial number of current discard/kong */
  Claims claims[NUM_SEATS]; /* who's claimed for the current discard */
  ChowPosition cpos; /* if a chow has been claimed, for which position? */
  int chowpending; /* is a chow in progress? */
  int mjpending; /* is a mah-jong from discard in progress? */
  Konging konging; /* is a kong in progress? */
  int ready[NUM_SEATS]; /* who is ready in a pause ? */
  unsigned int flags; /* misc flags. This will be zeroed at start of hand */
  /* This represents the wall. 
     There is a hard-wired array, which needs to be big enough
     to accommodate all walls we might see.
     The  live_used  member gives the number of tiles used from the
     live wall; the live_end gives the end of the live wall;
     and the dead_end gives the end of the dead wall.
     Thus the live wall goes from live_used to (live_end-1)
     and the dead wall from live_end to (dead_end-1).
     Game options (will) determine how these are changed.
     The current setting is a fixed 16-tile kong box.
  */
  struct {
    Tile tiles[MAX_WALL_SIZE]; /* the tiles */
    int live_used; /* number of live tiles drawn */
    int live_end; /* start of dead wall */
    int dead_end; /* end of wall */
    int size; /* size of wall as dealt */
  } wall;
  /* This is a convenience to track the number of tiles of each value
     displayed on the table. It is (always) maintained by handle_cmsg. */
  Tile exposed_tile_count[MaxTile];
  /* This is the same, but tracks only discards */
  Tile discarded_tile_count[MaxTile];
  /* the following can be set to 1 to make the handle_cmsg
     function check the legality of all the messages.
     Otherwise, it will apply them blindly.
     (So client programs using this module will probably leave it zero.)
  */
  int cmsg_check;
  /* This is used by handle_cmsg to return error messages
     when it fails. */
  char *cmsg_err;
  int protversion; /* protocol version level for this game */
  int manager; /* id of the player who owns this game and
		  can set the game options. By default, 0,
		  in which case any player can set options.
		  In that case, any player can claim ownership, first
		  come first served.
		  Can be set to -1 to prohibit any use of
		  managerial functions.
	       */
  /* option table. */
  GameOptionTable option_table;
  int fd; /* for use by client programs */
  int cseqno; /* for use by the client_ routines */
  void *userdata; /* for client extensions */
} Game;

/* game_id_to_player: return the player with the given id in the given game */
/* N.B. If passed an id of zero, will return the first free player
   structure. This is a feature. */
PlayerP game_id_to_player(Game *g,int id);

/* game_id_to_seat: convert an id to a seat position */
/* N.B. If passed an id of zero, will return the seat of the first 
   free player structure. This is a feature. */
seats game_id_to_seat(Game *g, int id);

/* game_draw_tile: draw a tile from the game's live wall.
   Return ErrorTile if no wall.
   If the wall contents are unknown to us, this function
   returns HiddenTile (assuming the game's wall is correctly
   initialized!).
*/
Tile game_draw_tile(Game *g);

/* game_peek_tile: as above, but just returns the next tile
   without actually updating the wall */
Tile game_peek_tile(Game *g);

/* game_draw_loose_tile: draw one of the loose tiles,
   or ErrorTile if no wall. Returns HiddenTile if we don't know
   the wall.
*/
Tile game_draw_loose_tile(Game *g);

/* game_peek_loose_tile: look at loose tile
   or ErrorTile if no wall. Returns HiddenTile if we don't know
   the wall.
*/
Tile game_peek_loose_tile(Game *g);

/* game_handle_cmsg: takes a CMsg, and updates the game to implement
   the CMsg. There are some important things to note:
   The function only checks the legality of moves if the
   game's cmsg_check field is positive. In this case,
   this function enforces the rules of the game, at considerable
   cost. There is no point in doing this (or trying to do this)
   in a game structure where we don't have full knowledge; hence
   client programs should not check cmsgs. Much of the logic
   that used to be in the controller program has migrated into
   this file, to avoid some code duplication.
   The return value is a convenience:
   the id of the affected player, for messages that affect one player;
   0, for messages that affect all or no players;
   -1 on legality error.
   If there is an error, the game's cmsg_err field points
   to a human readable error message; and the game is
   guaranteed to be unchanged.
   -2 on consistency errors. These are "this can't happen"
   errors. In this case the game state is not guaranteed to
   be unchanged, but it was inconsistent before.
*/

int game_handle_cmsg(Game *g, CMsgMsg *m);

/* game_has_started: returns true if the game has started
   (i.e. dealing of first hand has started), false otherwise */
int game_has_started(Game *g);

/* This is mainly for the server. Clients should 
   query the server to determine the available options */
extern GameOptionTable game_default_optiontable; /* the default
						     option table */

/* game_clear_option_table: clear an option table, freeing
   the storage */
int game_clear_option_table(GameOptionTable *t);

/* game_copy_option_table: copy old option table into new.
   Will refuse to work on new table with existing data.
*/
int game_copy_option_table(GameOptionTable *newt, GameOptionTable *oldt);

/* game_set_options_from_defaults: set the option table to be 
   a copy of the default, freeing any existing table; mark options
   enabled according to the value of g->protversion */
int game_set_options_from_defaults(Game *g);

/* find an option entry in an option table, searching by integer (if known)
   or name */
GameOptionEntry *game_get_option_entry_from_table(GameOptionTable *t, GameOption option, char *name);
/* find an option entry in the game table, searching by integer (if known)
   or name */
GameOptionEntry *game_get_option_entry(Game *g, GameOption option, char *name);

/* find an option entry in the default table, searching by integer (if known)
   or name */
GameOptionEntry *game_get_default_option_entry(GameOption option, char *name);

/* get an option value. First look in the game table; if that fails,
   look in the default table; if that fails return the default default.
*/

GameOptionValue game_get_option_value(Game *g, GameOption option, char *name);

/* set an option in table. Function will allocate space as required */
int game_set_option_in_table(GameOptionTable *t, GameOptionEntry *e);
/* set an option. Function will allocate space as required */
int game_set_option(Game *g, GameOptionEntry *e);

/* debugging function: return a (possibly static) buffer holding a
   textual dump of the game state. Second argument returns number
   of spare bytes in the returned buffer
*/
char *game_print_state(Game *g, int *bytes_left);

/* include enum parsing options */
#include "game-enums.h"

#endif /* GAME_H_INCLUDED */
