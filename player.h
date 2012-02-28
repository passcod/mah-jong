/* $Header: /home/jcb/MahJong/newmj/RCS/player.h,v 12.0 2009/06/28 20:43:13 jcb Rel $
 * player.h
 * Contains datatypes representing the state of players.
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

#ifndef PLAYER_H_INCLUDED
#define PLAYER_H_INCLUDED

#include "tiles.h"

/* Repeated from game.h. Eventually, this should be a game option,
   if we ever want to play three handed */
#define NUM_SEATS 4

/* A TileSet represents a (potentially) declared set. It is represented
   as the type, which may be Empty, Chow, Pung, Kong, ClosedPung 
   (only really used at the end), or ClosedKong.
   Also Pair and ClosedPair (for scoring).
   The second element is a Tile, which is the tile for Pungs and Kongs,
   and the first tile for chows.
   To account for the Millington rule whereby a claimed kong counts
   as concealed for doubling purposes, there is a third element
   annexed which is true if a kong was made by adding to an existing pung.
   Note: the value of the annexed field is only meaningful if the type
   is Kong.
*/

typedef enum {
 Empty = 0,
 Chow = 1,
 Pung = 2,
 Kong = 3,
 ClosedPung = 4,
 ClosedKong = 5,
 ClosedChow = 6, /* used in scoring */
 Pair = 7, /* used in scoring etc */
 ClosedPair = 8
} TileSetType;

typedef struct _TileSet {
  TileSetType type;
  Tile tile;
  int annexed;
} TileSet;

/* Pointer to TileSet */
typedef TileSet *TileSetP;

/* The PlayerOption type names options that the player can ask
   the controller to apply.
*/
typedef enum {
  /* make-enums sub { $_[0] =~ s/^PO//; } */
  POUnknown = 0,
  POInfoTiles,  /* should we be sent InfoTiles messages at every event ? */
  PODelayTime,   /* A time in deciseconds. The controller is requested to
                   leave at least this time between successive actions
		   (where an action is a tile movement). This might be
                   just to slow robot players down to human speed, or
                   to allow time for animation.
		   The controller does not have to take any notice! */
  POLocalTimeouts, /* The client wishes to handle timeouts locally */
  PONumOptions  /* number of options available */
} PlayerOption;


/* num_tiles_in_set: takes a TileSetP and returns the number of tiles
   in it.
*/

int num_tiles_in_set(TileSetP tp);

/* The Player datatype represents the state of a player.
   See internal comments for elucidation.
   The structure is public for convenience, but should be
   considered read-only, and modified by the provided functions.
*/

typedef struct _Player {
  int id; /* unique identifier assigned by the master controller,
	     and used in communication. Always non-zero for a real player. */
  char *name; /* the name of this player */
  TileWind wind; /* which wind this player currently is */
  int num_concealed; /* how many concealed tiles */
#define MAX_CONCEALED 14
  Tile concealed[MAX_CONCEALED]; /* the concealed tiles (if known) */
  int discard_hint; /* this is used by programs to tell player_discards_tile
		       which tile to discard, when the order of identical
		       tiles is important, as is the case when maintaining
		       a display of the hand according to the user's 
		       preferences. */
#define MAX_TILESETS 7 /* may have up to seven pairs.
			  Note: this happens even without the seven
			  pairs hand, since the player_can_mah_jong
			  function blindly looks for pairs. Perhaps
			  it shouldn't. */
  TileSet tilesets[MAX_TILESETS]; /* exposed tiles */
  int num_specials; /* number of flowers and seasons held */
  Tile specials[8]; /* flowers and seasons */
  unsigned int flags; /* for bit definitions, see below */
  unsigned int dflags[NUM_SEATS]; /* see below */
  int cumulative_score; /* score so far in the game (excluding current hand)*/
  int hand_score; /* score in this hand. Set to -1 during hand. */
  char *err; /* used to return error messages by methods. Value is 
		only meaningful after a method has failed (including
		test functions returning false). */
  void *userdata; /* for client programs */
} Player ;

/* mask bit numbers for player flags */
typedef enum {
  Hidden, /* true if we can't see the player's hand */
  MahJongged, /* true if this hand has gone mah jongg */
  HandDeclared, /* true if the hand has been declared, and accordingly
		   the "concealed" tiles are no longer. Implies Hidden is
		   false */
  /* the next three flags are used to handle calling declarations */
  /* the next flag is set internally by the player module */
  NoDiscard, /* true before the first discard */
  /* the next two flags must be set by the user of this module,
     and are cleared by the player_newhand function */
  OriginalCall, /* true if the player has made an original call */
  Calling, /* true if the player has made a calling declaration 
	      (when implemented) */
       } PlayerFlags;


/* pointer to Player. In order to discourage people from modifying
   players directly, this is actually declared as const, unless this
   is the player module being compiled.
 */
#ifdef PLAYER_C
typedef Player *PlayerP;
#else
typedef const Player *PlayerP;
#endif

/* given a pointer to player, test the flag */
/* Cast to avoid problems */
#define pflag(p,f) (p->flags & (1 << f))
/* set, clear */
#define psetflag(p,f) (((Player *)p)->flags |= (1 << f))
#define pclearflag(p,f) (((Player *)p)->flags &= ~(1 << f))


/* The dflags field of the player is used for keeping track of
   dangerous discards. The element of the player's own seat records
   whether the player's hand is dangerous: the elements corresponding
   to the other players record when another player supplies a dangerous 
   discard.
   NOTE: these flags are maintained by the game module, and then
   only if checking is on.
   NOTE: these flags are all related and may be combined.
   They are therefore given as actual bits, so they can be combined
   with & and |.
*/
/* The dangerous hands for letting off a cannon */
typedef enum {
  DangerBamboo=1,
  DangerCharacter=2,
  DangerCircle=4,
  DangerWind=8,
  DangerDragon=16,
  DangerHonour=32,
  DangerGreen=64,
  DangerTerminal=128,
  DangerEnd=256 /* when the wall is nearly empty */
} DangerSignals;

#define pdflag(p,s,f) (p->dflags[s] & f)
/* set, clear */
#define psetdflags(p,s,f) (((Player *)p)->dflags[s] |= f)
#define pcleardflags(p,s,f) (((Player *)p)->dflags[s] &= ~f)
#define presetdflags(p,s) (((Player *)p)->dflags[s] = 0)



/* Some of these functions return 1 for success and 0 for failure.
   In the case of failure, they leave the argument player unchanged.
*/

/* initialize_player: takes a PlayerP and fills in the fields
   with initial values; most are zero, but the hand_score is set to -1.
*/
void initialize_player(PlayerP p);

/* player_newhand: sets up the player for the start of a new hand.
   The given wind is the wind of this player.
   (N.B. This is a TileWind, not a seat.)
*/
void player_newhand(PlayerP p,TileWind w);

/* copy_player: copy old player into new (which must already
   be allocated). Returns new.
*/

PlayerP copy_player(PlayerP newp,const PlayerP oldp);

/* set_player_id: sets the id of a player. Complains if the player
   already has an id (but continues).
*/
void set_player_id(PlayerP p, const int id);

/* set_player_name: sets the name of a player. The argument string
   is copied. Warning: any existing name is freed.
   The name may be NULL, which is not the same as an empty string.
*/
void set_player_name(PlayerP p, const char *n);

/* set_player_cumulative_score:
   sets the score of the player */
void set_player_cumulative_score(PlayerP p, int s);

/* change_player_cumulative_score:
   add d to the player's score */
void change_player_cumulative_score(PlayerP p, int d);

/* set_player_hand_score:
   set the hand_score field. */
void set_player_hand_score(PlayerP p, int h);

void set_player_userdata(PlayerP p, void *ud);

/* utility to count occurrences of tile in player's hand */

int player_count_tile(PlayerP p, Tile t);

/* player_draws_tile: first arg is a PlayerP.
   Second argument is a tile; it is HiddenTile if we can't see it.
   Returns 1 on success.
*/

int player_draws_tile(PlayerP p, Tile t);

/* player_draws_loose_tile: first arg is a PlayerP.
   Second argument is a tile; it is HiddenTile if we can't see it.
   Returns 1 on success.
*/

int player_draws_loose_tile(PlayerP p, Tile t);

/* player_declares_special: the player declares the special tile
   given as the second argument, which is removed from the concealed
   tiles and added to the special tiles. The player is given the tile
   specified by the third argument to replace the special; this is
   HiddenTile if we cannot observe it.
   May be called on players that are known or hidden.
*/
int player_declares_special(PlayerP p, Tile spec);

/* player_can_declare_special: just tests for legality of this move.
   Should only be called on players that are known.
*/
int player_can_declare_special(PlayerP p, Tile spec);

/* the following functions declare sets. If they leave the player
   with no concealed tiles, so that the hand is complete, they set
   the HandDeclared flag.
*/

/* player_pungs: player takes the tile, and uses it to form a pung.
 */
int player_pungs(PlayerP p, Tile d);

int player_can_pung(PlayerP p, Tile d);

/* player_pairs: player takes the tile, and uses it to form a pair.
   Only used after mahjong.
 */
int player_pairs(PlayerP p, Tile d);

int player_can_pair(PlayerP p, Tile d);

/* player_kongs: player takes tile, forms kong */
int player_kongs(PlayerP p, Tile d);

int player_can_kong(PlayerP p, Tile d);

/* player_declares_closed_kong: player declares a closed kong
   of d.
   NOTE that these functions do not check whether it's player's turn,
   since they do not have access to that information.
*/
int player_declares_closed_kong(PlayerP p, Tile d);

int player_can_declare_closed_kong(PlayerP p, Tile d);

/* player_adds_to_pung: player has drawn t, and adds it to an existing
   open pung.
*/
int player_adds_to_pung(PlayerP p, Tile t);

int player_can_add_to_pung(PlayerP p, Tile t);

/* player_kong_robbed: the player has formed a kong of t, and it is robbed */
int player_kong_is_robbed(PlayerP p, Tile t);

/* player_forms_closed_pung: player has a pung of d. This is
   used only when preparing hands for scoring. */

int player_forms_closed_pung(PlayerP p, Tile d);

int player_can_form_closed_pung(PlayerP p, Tile d);

/* player_forms_closed_pair: player has a pair of d. This is
   used only when preparing hands for scoring. */

int player_forms_closed_pair(PlayerP p, Tile d);

int player_can_form_closed_pair(PlayerP p, Tile d);

/* Chow declarations have to say where the claimed tile is being
   inserted. This is partly so that it's easy to check, and partly
   because some esoteric scoring rules need this information.

   The final argument is Lower, Middle, or Upper, depending on where
   the tile d is to go in the chow.

   The testing functions will accept an argument of AnyPos, in which
   case they will iterate over the three possible values.

   NOTE that these functions do not check that it's player's turn.
*/

/* NOTE NOTE: these values are assumed, and must not be changed.
   (Because a chow is defined by its lower tile, so 
   claimedtile = chowdefiningtile + chowposition.)
   The AnyPos value is defined to be MaxTile, so that adding it
   or subtracting it from a Tile is guaranteed to produce an error.
*/
typedef enum {
  Lower = 0,
  Middle = 1,
  Upper = 2,
  AnyPos = MaxTile 
} ChowPosition;

int player_chows(PlayerP p, Tile d, ChowPosition r);

int player_can_chow(PlayerP p, Tile d, ChowPosition r);

/* used only in scoring. Although in the client/server protocol,
   a closed chow is always specified as Lower, in evaluating hands
   it is often wanted to check for closed chows with a given tile
   in a given position. Therefore these functions take a position.
*/

int player_forms_closed_chow(PlayerP p, Tile d, ChowPosition r);

int player_can_form_closed_chow(PlayerP p, Tile d, ChowPosition r);

/* tests whether a player has a mah-jong hand. The d argument is
   the discard tile available to the player, or HiddenTile if the
   player already has 14 tiles.
   The third arg says which special hands should be tested for.
   Currently, thirteen wonders is always allowed, so the only
   possible value here is 0 or seven pairs.
*/
typedef enum {
  MJSevenPairs = 1
} MJSpecialHandFlags;

int player_can_mah_jong(PlayerP p, Tile d, MJSpecialHandFlags flags);

/* tests whether a player has the thirteen unique wonders.
   This is included in the above test, but is separately 
   exported as a convenience. */
int player_can_thirteen_wonders(PlayerP p, Tile d);

/* set the discard_hint: which concealed tile should be discarded
   (as opposed to the default choice of the first tile that is right).
   Values are -1 for no hint, or 0..num_concealed-1.
   The discard_hit is cleared on discard.
*/
int player_set_discard_hint(PlayerP p, int n);

/* player discards a tile */
int player_discards(PlayerP p, Tile t);

int player_can_discard(PlayerP p, Tile t);

/* utility to sort the concealed tiles. This sorts the tiles into
   increasing order; however, the only safe assumptions about the values
   of the tiles are those allowed by tiles.h. 
   Thus it *is* safe to assume that tile appear in increasing value and that
   tiles of the same suit appear together; but it is *not* safe to assume
   the suits appear in any particular order.
*/
void player_sort_tiles(PlayerP p);

/* utility to exchange two concealed tiles. Moves the tile in position old
   to position new */
int player_reorder_tile(PlayerP p, int old, int new);

/* player_print_tiles: puts into buf, which is required to be
   at least 80 characters, a human readable representation of
   the player's tiles. Each tile is represented by its code, and
   the hand is represented as:
   space separated concealed tiles;
   ' * ';
   space separated tilesets, represented as tile-tile
   for exposed sets and tile+tile for closed sets;
   ' * ';
   space separated specials.
   If the third argument is true, then the concealed tiles
   are printed as blanks. This happens anyway if they are hidden
   in the player structure.
*/
void player_print_tiles(char *buf, PlayerP p, int hide);

/* set_player_tiles: converse of the above. Takes a string description
   of the player's tiles, and forces the Player to agree.
   Forces the Hidden flag on or off if appropriate.
   Returns 1 on success, 0 on error. If error, all bets are off.
*/
int set_player_tiles(PlayerP p, char *desc);

/* player_shows_tiles: takes a string representation of a tile
   list (as above), and sets the player's concealed tiles to it.
   Also sets the HandDeclared flag.
*/
int player_shows_tiles(PlayerP p, char *desc);

/* player_swap_tile: swaps the oldtile for the newtile in the
   concealed tiles. Only used in testing, of course */
int player_swap_tile(PlayerP p, Tile oldtile, Tile newtile);

/* tileset_string: utility function, returning the printed 
   representation of a tileset, as above. N.B. this function
   returns a string in static storage, which will be overwritten
   on the next call.
*/
char *tileset_string(TileSetP tp);

/* diagnostic function: print a text representation of the player's
   state into a (possibly static) buffer and return it */
char *player_print_state(PlayerP p);

/* now import the functions for printing and scanning enums */
#include "player-enums.h"

#endif /* PLAYER_H_INCLUDED */
