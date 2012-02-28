/* $Header: /home/jcb/MahJong/newmj/RCS/game.c,v 12.2 2012/02/01 14:10:35 jcb Rel $
 * game.c
 * Routines for manipulating game data structures.
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
 
static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/game.c,v 12.2 2012/02/01 14:10:35 jcb Rel $";

#include "game.h"
#include "sysdep.h"

/* This macro implements "tiles are equal, or one is unknown".
   It is used to check consistency when handling cmsgs */
#define teq(t1,t2) (t1 == t2 || !t1 || !t2)
/* next seat */
#define nextseat(s) ((s+1)%NUM_SEATS)
#define prevseat(s) ((s+3)%NUM_SEATS)

static void set_danger_flags(Game *g UNUSED, PlayerP p); /* at end */
static void mark_dangerous_discards(Game *g); /* at end */

/* game_id_to_player: return the player with the given id in the given game */

PlayerP game_id_to_player(Game *g,int id) {
  int i;
  for ( i = 0 ; i < NUM_SEATS ; i++ )
    if ( g->players[i]->id == id ) return g->players[i];
  return 0;
}

/* game_id_to_seat: convert an id to a seat position */

seats game_id_to_seat(Game *g, int id) {
  seats i;
  for ( i = 0 ; i < NUM_SEATS ; i++ )
    if ( g->players[i]->id == id ) return i;
  return -1;
}

/* game_draw_tile: draw a tile from the game's live wall.
   Return ErrorTile if no wall.
*/
Tile game_draw_tile(Game *g) {
  if ( g->wall.live_used >= g->wall.live_end ) return ErrorTile;
  return g->wall.tiles[g->wall.live_used++];
}

/* game_peek_tile: return the next tile from the game's live wall.
   Return ErrorTile if no wall.
*/
Tile game_peek_tile(Game *g) {
  if ( g->wall.live_used >= g->wall.live_end ) return ErrorTile;
  return g->wall.tiles[g->wall.live_used];
}

/* game_draw_loose_tile: draw one of the loose tiles,
   or ErrorTile if no wall
*/
Tile game_draw_loose_tile(Game *g) {
  if ( game_get_option_value(g,GODeadWall,NULL).optbool && g->wall.dead_end == g->wall.live_end ) return ErrorTile;
  /* we have a loose tile; adjust the wall */
  g->wall.dead_end--; /* end of dead wall moves back */
  if ( game_get_option_value(g,GODeadWall,NULL).optbool ) {
    /* Now if we are in the !DeadWall16 (non-Millington case),
       extend the dead wall if necessary. It's OK if this
       goes past the beginning of the live wall; we'll get
       an error on next live draw, which is how we understand it.
       (The alternative would be to say there has to be a washout
       now, which I don't like.)
    */
    if ( !game_get_option_value(g,GODeadWall16,NULL).optbool
	 && ( g->wall.dead_end - g->wall.live_end < 13) )
      g->wall.live_end -= 2;
    /* if we're in the millington case, do nothing */
  } else {
    /* we're not playing with a dead wall. So the live end moves
       back as well */
    g->wall.live_end--;
  }
  return g->wall.tiles[g->wall.dead_end]; /* the tile at the end */
}

/* game_peek_loose_tile: look at next loose tile
   or ErrorTile if no wall
*/
Tile game_peek_loose_tile(Game *g) {
  if ( game_get_option_value(g,GODeadWall,NULL).optbool ) {
    if ( g->wall.dead_end == g->wall.live_end ) return ErrorTile;
  } else {
    if ( g->wall.dead_end == g->wall.live_used ) return ErrorTile;
  }
  return g->wall.tiles[g->wall.dead_end-1];
}

/* defines used in the following table */
#define DBL 10000
#define LIM 100000000

static GameOptionEntry default_optiontable[] = {
  /* a real option has a name. This is just a filler. */
  { GOUnknown, "" , GOTBool, 1000000, 0, { 0 }, "", 0 }, 
  { GOTimeout, "Timeout" , GOTNat, 0, 0, { 15 }, "time limit for claims",0 },
  { GOTimeoutGrace, "TimeoutGrace", GOTNat, 0, 0, { 5 }, "grace period when clients handle timeouts" , 0},
  { GOScoreLimit, "ScoreLimit", GOTNat, 0, 0, { 1000 }, "limit on hand score", 0 },
  { GONoLimit, "NoLimit", GOTBool, 0, 0, { 0 }, "no-limit game", 0 },
  { GOMahJongScore, "MahJongScore", GOTScore, 0, 0, { 20 }, "base score for going out", 0 },
  { GOSevenPairs, "SevenPairs", GOTBool, 1020, 0, { 0 }, "seven pairs hand allowed", 0 },
  { GOSevenPairsVal, "SevenPairsVal", GOTScore, 1020, 0, { 20 }, "score for a seven pair hand", 0 },
  { GOFlowers, "Flowers", GOTBool, 1020, 0, { 1 }, "play using flowers and seasons", 0 },
  { GOFlowersLoose, "FlowersLoose", GOTBool, 0, 0, { 0 }, "flowers replaced by loose tiles", 0 },
  { GOFlowersOwnEach, "FlowersOwnEach", GOTScore, 0, 0, { 0 }, "score for each own flower or season", 0 },
  { GOFlowersOwnBoth, "FlowersOwnBoth", GOTScore, 0, 0, { DBL }, "score for own flower and own season", 0 },
  { GOFlowersBouquet, "FlowersBouquet", GOTScore, 0, 0, { DBL }, "score for all four flowers or all four seasons", 0 },
  { GODeadWall, "DeadWall", GOTBool, 1020, 0, { 1 }, "there is a dead wall", 0 },
  { GODeadWall16, "DeadWall16", GOTBool, 1020, 0, { 1 }, "dead wall is 16 tiles, unreplenished", 0 },
  { GOConcealedFully, "ConcealedFully", GOTScore, 0, 0, { DBL }, "score for fully concealed hand", 0 },
  { GOConcealedAlmost, "ConcealedAlmost", GOTScore, 0, 0, { 0 }, "score for almost concealed hand", 0 },
  { GOLosersPurity, "LosersPurity", GOTBool, 0, 0, { 0 }, "losing hands score doubles for pure, concealed etc.", 0 },
  { GOKongHas3Types, "KongHas3Types", GOTBool, 1034, 0, { 0 }, "claimed kongs count as concealed for doubling", 0 },
  { GOEastDoubles, "EastDoubles", GOTBool, 0, 0, { 1 }, "east pays and receives double", 0 },
  { GOLosersSettle, "LosersSettle", GOTBool, 0, 0, { 1 }, "losers pay each other", 0 },
  { GODiscDoubles, "DiscDoubles", GOTBool, 0, 0, { 0 }, "the discarder pays double", 0 },
  { GOShowOnWashout, "ShowOnWashout", GOTBool, 0, 0, { 0}, "tiles revealed on washout", 0 },
  { GONumRounds, "NumRounds", GOTNat, 0, 0, { 4 }, "number of rounds to play", 0 },
  { GOEnd, "End" , GOTBool, 0, 0, { 1 }, "end of option list", 0 }
};

GameOptionTable game_default_optiontable = 
{ default_optiontable, GONumOptions } ;

/* game_clear_option_table: clear an option table, freeing
   the storage */
int game_clear_option_table(GameOptionTable *got)
{
  free(got->options);
  got->options = NULL;
  got->numoptions = 0;
  return 0;
}

/* game_copy_option_table: copy old option table into new.
   Will refuse to work on new table with existing data.
*/
int game_copy_option_table(GameOptionTable *newt, GameOptionTable *oldt)
{
  if (newt->options) {
    warn("game_copy_option_table called on non-empty target");
    return 0;
  }
  newt->options = calloc(oldt->numoptions,oldt->numoptions*sizeof(GameOptionEntry));
  if ( newt->options == NULL ) {
    warn("failed to allocate option table");
    return 0;
  }
  memcpy(newt->options,oldt->options,oldt->numoptions*sizeof(GameOptionEntry));
  newt->numoptions = oldt->numoptions;
  return 1;
}

/* game_set_options_from_defaults: set the option table to be 
   a copy of the default, freeing any existing table; mark options
   enabled according to the value of g->protversion.
   Also convert Nat options to Int if the protocol version is low.
 */
int game_set_options_from_defaults(Game *g) {
  int i;

  game_clear_option_table(&g->option_table);
  g->option_table.options = calloc(GONumOptions,sizeof(GameOptionEntry));
  if ( g->option_table.options == NULL ) {
    warn("failed to allocate option table");
    return 0;
  }
  memcpy(g->option_table.options,default_optiontable,GONumOptions*sizeof(GameOptionEntry));
  g->option_table.numoptions = GONumOptions;
  /* now go through the option list marking options enabled */
  for ( i=0; i < g->option_table.numoptions; i++ ) {
    if ( g->protversion >= g->option_table.options[i].protversion )
      g->option_table.options[i].enabled = 1;
    /* and convert nat to int */
    if ( g->protversion < 1020 && g->option_table.options[i].type == GOTNat )
      g->option_table.options[i].type = GOTInt;
  }
  return 1;
}

/* find an option entry in an option table, searching by integer (if known)
   or name (if known) */
GameOptionEntry *game_get_option_entry_from_table(GameOptionTable *t, GameOption option, char *name) {
  GameOptionEntry *goe;
  int i;
  /* usually, the option will be in the right slot, so check for this first */
  if ( option && (option < (unsigned int)t->numoptions) && option == t->options[option].option ) return &t->options[option];
  /* do we  have this option in the table? */
  for (i=0; i<t->numoptions; i++) {
    goe = &t->options[i];
    if ( (option && goe->option == option)
	 /* we may have options that we don't know about, in which
	    case we have to check the names */
	 || (!(option && goe->option)
	     && name && strcmp(goe->name,name) == 0) ) return goe;
  }
  return NULL;
}

GameOptionEntry *game_get_option_entry(Game *g, GameOption option, char *name) {
  return game_get_option_entry_from_table(&g->option_table,option,name);
}


/* find an option entry in the default table, searching by integer (if known)
   or name */
GameOptionEntry *game_get_default_option_entry(GameOption option, char *name) {
  return game_get_option_entry_from_table(&game_default_optiontable,
				    option,name);
}

/* see game.h for spec */
GameOptionValue game_get_option_value(Game *g, GameOption option, char *name)
{
  GameOptionEntry *goe = NULL;
  if ( g ) goe = game_get_option_entry(g,option,name);
  if ( ! goe ) goe = game_get_default_option_entry(option,name);
  if ( ! goe ) return (GameOptionValue)0;
  if ( goe->type == GOTString ) 
    return (GameOptionValue) goe->value.optstring;
  else
    return (GameOptionValue) goe->value.optint;
}


/* game_set_option_in_table: set an option.
   Function will allocate space as required. */
int game_set_option_in_table(GameOptionTable *t, GameOptionEntry *e) {
  GameOptionEntry *goe = NULL;
  int i;
  /* do we already have this option in the table? */
  for (i=0; i<t->numoptions; i++) {
    goe = &t->options[i];
    if ( (goe->option && goe->option == e->option)
	 /* we may have options that we don't know about, in which
	    case we have to check the names */
	 || (goe->option == GOUnknown
	     && e->option == GOUnknown
	     && strcmp(goe->name,e->name) == 0) ) break;
  }
  if ( i == t->numoptions ) {
    /* allocate more space */
    GameOptionEntry *ngoe;
    t->numoptions++;
    if ( t->options ) {
      ngoe = realloc(t->options,t->numoptions*sizeof(GameOptionEntry));
    } else {
      ngoe = malloc(t->numoptions*sizeof(GameOptionEntry));
    }
    if ( t == NULL ) {
      warn("unable to extend option table");
      return 0;
    }
    t->options = ngoe;
    goe = &t->options[i];
    /* fill in fields for new option */
    goe->option = e->option;
    strmcpy(goe->name,e->name,16);
    goe->type = e->type;
    goe->protversion = e->protversion;
    strmcpy(goe->desc,e->desc,128);
    goe->userdata = NULL; /* NOT copied */
  } else {
    /* do a typecheck. */
    if ( goe->type != e->type ) {
      warn("type clash when setting option");
      return 0;
    }
  }
  /* set fields that might have changed */
  goe->enabled = e->enabled;
  memcpy(goe->value.optstring,e->value.optstring,sizeof(e->value.optstring));
  /* don't set description if e has null value */
  if ( e->desc[0] ) {
    strmcpy(goe->desc,e->desc,128);
  }
  return 1;
}

int game_set_option(Game *g, GameOptionEntry *e) {
  if ( ! game_set_option_in_table(&g->option_table,e) ) {
    g->cmsg_err = "Error setting option";
    return 0;
  }
  return 1;
}

/* act on CMsg: see spec in header file */
/* convenience macro to setup s and p for the id */
#define setups p = game_id_to_player(g,m->id); \
 s = game_id_to_seat(g,m->id); affected_id = m->id
int game_handle_cmsg(Game *g, CMsgMsg *cm) 
{
  PlayerP p;
  seats s;
  int i;
  const int chk = g->cmsg_check;
  int affected_id;
  MJSpecialHandFlags mjspecflags; /* special hands allowed in this game */

  affected_id = 0;

  mjspecflags = 0;
  if ( game_get_option_value(g,GOSevenPairs,NULL).optbool )
    mjspecflags |= MJSevenPairs;

  switch ( cm->type ) {
  case CMsgReconnect:
    /* nothing to do - we'll be destroyed in a moment anyway */
    return 0;
  case CMsgConnectReply:
    {
      CMsgConnectReplyMsg *m = (CMsgConnectReplyMsg *)cm;
      /* It is assumed here that the game seats point to four player
	 structures, and that "we" are in the east seat.
	 Clients are responsible for making sure this is true.
      */
      if ( m->id == 0 ) return affected_id; /* connection refused */
      set_player_id(g->players[0],m->id);
      affected_id = m->id;
      return affected_id;
    }
  case CMsgPlayer:
    {
      CMsgPlayerMsg *m = (CMsgPlayerMsg *)cm;
      /* do we already know this player? */
      setups;
      if ( p ) {
	/* after protocol version 1034, a player message with
	   an empty name is defined to mean "delete this player".
	   Note that we have to refer to the global protocol_version,
	   since the game protversion field will not be set at
	   the only plausible times to get this command */
	if ( (m->name && m->name[0]) || protocol_version <= 1034 ) {
	  set_player_name(p,m->name);
	} else {
	  /* player message with empty name means delete it */
	  /* if this happens at any time other than during initial
	     connection, something's wrong. We do not allow the
	     deletion of a player from a game. (Maybe we should,
	     to allow substitution, but not yet!)
	     At pversion 1038, this was changed to allow
	     deletion until the game actually starts, rather than
	     only until the game is set up.
	  */
	  if ( game_has_started(g) ) {
	    warn("Can't delete a player after game has started");
	    return -1;
	  }
	  initialize_player(p);
	}
      }
      else {
	if ( protocol_version > 1034 && ! ( m->name && m->name[0] ) ) {
	  warn("trying to delete non-existent player");
	  return -1;
	}
	/* find first free player */
	p = game_id_to_player(g,0);
	if ( ! p ) {
	  warn("received Player message when table full");
	  return -1;
	}
	initialize_player(p);
	set_player_id(p,m->id);
	set_player_name(p,m->name);
      }
    }
    return affected_id;
  case CMsgGame:
    {
      CMsgGameMsg *m = (CMsgGameMsg *)cm;
      PlayerP players[NUM_SEATS];

      /* First, we need to rearrange the players so they're in
	 the given seats */
      players[east] = game_id_to_player(g,m->east);
      players[south] = game_id_to_player(g,m->south);
      players[west] = game_id_to_player(g,m->west);
      players[north] = game_id_to_player(g,m->north);
      for ( i=0; i < NUM_SEATS; i++ ) {
	if ( !players[i] ) {
	  g->cmsg_err = "Starting game with unknown player";
	  return -1;
	}
	g->players[i] = players[i];
      }
      /* now set the round */
      g->round = m->round;
      /* and so on */
      g->hands_as_east = m->hands_as_east;
      g->firsteast = m->firsteast;
      set_player_cumulative_score(g->players[east],m->east_score);
      set_player_cumulative_score(g->players[south],m->south_score);
      set_player_cumulative_score(g->players[west],m->west_score);
      set_player_cumulative_score(g->players[north],m->north_score);
      /* initialize state */
      g->state = HandComplete;
      g->player = noseat;
      g->serial = 0;
      for (i=0;i<NUM_SEATS;i++) g->claims[i] = 0;
      g->protversion = m->protversion;
      g->manager = m->manager;
      game_set_options_from_defaults(g);
      /* and games always start inactive */
      g->active = 0;
    }
    return affected_id;
  case CMsgInfoTiles:
    /* Note that this always forces the player into compliance.
       It's up to the client not to call this routine when
       it's not wanted */
    {
      CMsgInfoTilesMsg *m = (CMsgInfoTilesMsg *)cm;

      setups;
      set_player_tiles(p,m->tileinfo);
    }
    return affected_id;
  case CMsgNewRound:
    {
      CMsgNewRoundMsg *m = (CMsgNewRoundMsg *)cm;

      g->round = m->round;
      g->hands_as_east = 0; /* superfluous */
    }
    return affected_id;
  case CMsgPause:
    {
      CMsgPauseMsg *m = (CMsgPauseMsg *)cm;
      int i;
      seats s;
      
      if ( g->paused ) {
	/* it is legitimate to start a new pause before one is complete */
	free(g->paused);
      }
      g->paused = (char *)malloc(strlen(m->reason)+1);
      if ( g->paused == NULL ) {
	warn("unable to malloc space for pause reason");
	return -2;
      }
      strcpy(g->paused,m->reason);
      for ( i=0; i < NUM_SEATS; i++) g->ready[i] = 0;
      s = game_id_to_seat(g,m->exempt);
      if ( s != noseat ) g->ready[s] = 1;
      affected_id = 0; /* affects everybody, really */
    }
    return affected_id;
  case CMsgPlayerReady:
    {
      CMsgPlayerReadyMsg *m = (CMsgPlayerReadyMsg *)cm;
      int i,ready;

      setups;
      affected_id = m->id;
      if ( g->paused ) {
	g->ready[s] = 1;
	ready = 1;
	for ( i=0 ; i < NUM_SEATS; i++ ) ready = (ready && g->ready[i]);
	if ( ready ) {
	  free(g->paused);
	  g->paused = NULL;
	}
      }
    }
    return affected_id;
  case CMsgNewHand:
    {
      CMsgNewHandMsg *m = (CMsgNewHandMsg *)cm;
      PlayerP players[NUM_SEATS];
      int n;

      if ( chk ) {
	if ( g->state != HandComplete ) {
	  g->cmsg_err = "Still playing current hand";
	  return -1;
	}
      }

      /* If east has changed, reset hands_as_east */
      if ( m->east != g->players[east]->id )
	g->hands_as_east = 0;
      /* first we have to rotate the players so that the current
	 east is correct. */
      for (i=0; i < NUM_SEATS; i++) players[i] = g->players[i];
      n = game_id_to_seat(g,m->east);
      for (i=0; i < NUM_SEATS; i++) g->players[i] = players[(i+n)%NUM_SEATS];
      /* game state */
      g->state = Dealing;
      /* reset the hand scores and clear the flags */
      for ( i = 0 ; i < NUM_SEATS ; i++ ) {
	/* Warning: this knows TileWind = seats+1 */
	player_newhand(g->players[i],i+1);
      }
      /* clear game flags */
      g->flags = 0;
      g->konging = NotKonging;
      g->wall.live_used = 0;
      g->wall.size = game_get_option_value(g,GOFlowers,NULL).optbool ? 144 : 136;
      g->wall.dead_end = g->wall.size;
      g->wall.live_end = g->wall.dead_end - 
	(game_get_option_value(g,GODeadWall,NULL).optbool ? (game_get_option_value(g,GODeadWall16,NULL).optbool ? 16 : 14) : 0) ;
      g->serial = 0; /* must be initialized */
      for ( i = 0 ; i < MaxTile ; i++ ) {
	g->exposed_tile_count[i] = 0;
	g->discarded_tile_count[i] = 0;
      }
    }
    return affected_id;
  case CMsgPlayerDraws:
    {
      CMsgPlayerDrawsMsg *m = (CMsgPlayerDrawsMsg *)cm;
      Tile t;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* can this player draw? */
	if ( g->state == Dealing ) {
	  /* ought to check, but currently don't */
	} else if ( g->state == Discarded ) {
	  seats i;
	  seats ds = g->player;
	  /* legal if it's our turn and all claims have
	     been received */
	  if ( s != nextseat(ds) ) {
	    g->cmsg_err = "Drawing out of turn";
	    return -1;
	  }
	  for ( i = 0 ; i < NUM_SEATS ; i++ )
	    if ( i != ds && g->claims[i] == UnknownClaim ) {
	      g->cmsg_err = "Drawing before all claims received";
	      return -1;
	    }
	  /* better not be a claim */
	  for ( i = 0 ; i < NUM_SEATS ; i++ )
	    if ( i != ds && g->claims[i] > NoClaim ) {
	      g->cmsg_err = "Somebody has claimed the discard";
	      return -1;
	    }
	} else if ( g->state == DeclaringSpecials ) {
	  /* legal if it's our turn and we need a tile */
	  if ( s != g->player ) {
	    g->cmsg_err = "Can't draw out of turn";
	    return -1;
	  }
	  if ( g->needs != FromWall ) {
	    g->cmsg_err = "Don't need a tile now";
	    return -1;
	  }
	} else if ( g->state == Discarding ) {
	  /* only legal if we're drawing a replacement for
	     a special under the rules where this comes
	     from the live wall */
	  if ( g->needs != FromWall ) {
	    g->cmsg_err = "Already drawn or claimed a tile";
	    return -1;
	  }
	} else {
	  /* In any other state */
	  g->cmsg_err = "Can't draw a tile now";
	  return -1;
	}
      } /* end of legality checking */
	  
      t = game_draw_tile(g);
      if ( t == ErrorTile ) {
	g->cmsg_err = "Wall exhausted";
	return -1;
      }
      if ( chk && !teq(t,m->tile) ) {
	g->cmsg_err = "Drawing tile not the first in wall";
	warn(g->cmsg_err);
	return -2;
      }

      /* if this fails, we're in a mess */
      if ( ! player_draws_tile(p,m->tile) ) {
	g->cmsg_err = "Unexpected failure drawing tile (player)";
	warn(g->cmsg_err);
	return -2;
      }
      /* next state of game */
      if ( g->state == Dealing ) {
	/* if this is east, store the tile */
	if ( s == east ) g->tile = m->tile;
	/* if deal is complete ... */
	if ( g->players[east]->num_concealed == MAX_CONCEALED
	     && g->players[south]->num_concealed == MAX_CONCEALED-1
	     && g->players[west]->num_concealed == MAX_CONCEALED-1
	     && g->players[north]->num_concealed == MAX_CONCEALED-1 ) {
	  g->state = DeclaringSpecials;
	  /* g->tile   was set before if nec */
	  g->needs = FromNone;
	  g->player = east;
	  g->whence = FromWall;
	}
      } else {
	if ( g->state == Discarded ) {
	  g->state = Discarding;
	}
	g->tile = m->tile;
	g->needs = FromNone;
	g->player = s; 
	g->whence = FromWall;
	g->konging = NotKonging;
      }
    }
    return affected_id;
  case CMsgPlayerDrawsLoose:
    {
      CMsgPlayerDrawsLooseMsg *m = (CMsgPlayerDrawsLooseMsg *)cm;
      Tile t;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* can this player draw a loose tile? */
	/* can only happen when it's currently our turn */
	if ( s != g->player ) {
	  g->cmsg_err = "Can't draw a loose tile out of turn";
	  return -1;
	} else if ( (g->state == Discarding || g->state == DeclaringSpecials)
		    && g->needs == FromLoose ) {
	  /* OK */
	} else {
	  /* In any other state */
	  g->cmsg_err = "Can't draw a tile now";
	  return -1;
	}
      } /* end of legality checking */
	  
      t = game_draw_loose_tile(g);
      if ( t == ErrorTile ) {
	g->cmsg_err = "Dead wall exhausted";
	return -1;
      }
      if ( chk && !teq(t,m->tile) ) {
	g->cmsg_err = "Drawing tile not the first loose tile";
	warn(g->cmsg_err);
	return -2;
      }
      /* if this fails, we're in a mess */
      if ( ! player_draws_loose_tile(p,m->tile) ) {
	g->cmsg_err = "Unexpected failure drawing tile (player)";
	warn(g->cmsg_err);
	return -2;
      }
      /* next state of game is unchanged apart from tile info */
      g->needs = FromNone;
      g->whence = FromLoose;
      g->tile = m->tile;
      g->konging = NotKonging;
    }
    return affected_id;
  case CMsgPlayerDeclaresSpecial:
    {
      CMsgPlayerDeclaresSpecialMsg *m = (CMsgPlayerDeclaresSpecialMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* Specials may be declared during the initial phase,
	 or if it is the player's turn to discard, and at
	 no other time.
	 Morever, all rules I've seen say that specials can
	 only be declared after drawing from the wall. Silly,
	 but there it is.
	 Pre-release version didn't have this, so we need
	 to keep it for compatability.
	*/
	if ( g->state == DeclaringSpecials 
	     || (g->state == Discarding
		 && (g->protversion < 1010 
		     || g->whence != FromDiscard) ) ) {
	  if ( g->player != s ) {
	    g->cmsg_err = "Can only declare in turn";
	    return -1;
	  }
	} else {
	  g->cmsg_err = "Can't discard specials now";
	  return -1;
	}
      }

      if ( m->tile == HiddenTile ) {
	if ( chk && g->state == Discarding ) {
	  /* this shouldn't happen. Although it's harmless,
	     we will nonetheless return an error */
	  g->cmsg_err = "Can't declare blank special now";
	  return -1;
	}
	if ( s == north ) {
	  g->state = Discarding;
	  g->konging = NotKonging;
	  g->needs = FromNone;
	  /* we need to set whence. It could be from loose or wall,
	     according to what happened during declaring specials. 
	     However, if east is going to go out immediately, it doesn't
	     matter, since that's a limit anyway. So we just set it to
	     FromWall.
	     FIXME: in fact the only reason we need to do this is
	     because the controller deals itself, rather than passing
	     cmsgs to the game. Sometime we should fix the controller.
	     But that probably means making this routine fill in 
	     details of cmsgs: which seems right anyway.
	  */
	  g->whence = FromWall;
	  g->player = east;
	} else {
	  /* why do we set the state? So that resumption works:
	     otherwise, there's no cue to enter the ds state */
	  g->state = DeclaringSpecials;
	  g->player = nextseat(s);
	}
      } else {
	if ( ! player_declares_special(p,m->tile) ) {
	  /* This can only fail if the player doesn't
	     hold the tile */
	  /* Actually, that's not true. It can also fail
	     if the "hiddenness" is inconsistent. In this
	     case, player will scream anyway. */
	  g->cmsg_err = "Don't have that special";
	  return -1;
	}
	if ( game_get_option_value(g,GOFlowersLoose,NULL).optbool )
	  g->needs = FromLoose;
	else
	  g->needs = FromWall;
	g->exposed_tile_count[m->tile]++; /* who's counting specials ? */
      }
    }
    return affected_id;
  case CMsgStartPlay:
    affected_id = ((CMsgStartPlayMsg *)cm)->id;
    g->active = 1;
    return affected_id;
  case CMsgStopPlay:
    g->active = 0;
    return affected_id;
  case CMsgPlayerDiscards:
    {
      CMsgPlayerDiscardsMsg *m = (CMsgPlayerDiscardsMsg *)cm;
      int nodiscard;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* if it's not time to discard, return error */
	if ( ! (g->state == Discarding
	      && g->player == s) ) {
	  g->cmsg_err = "Can't discard now";
	  return -1;
	}
	/* give a more helpful error message when the player
	   is waiting for a tile */
	if ( g->needs ) {
	  g->cmsg_err = "Need to draw a tile before discarding";
	  return -1;
	}
	/* players aren't allowed to discard if the game is paused */
	if ( g->paused ) {
	  g->cmsg_err = "Can't discard until everybody's ready";
	  return -1;
	}
	/* check legitimacy of calling declaration */
	/* must be no sets declared */
	if ( m->calling && p->num_concealed < 14 ) {
	  g->cmsg_err = "Must have concealed hand to declare calling";
	  return -1;
	}
	/* multiple calling declarations are silly */
	if ( m->calling && pflag(p,Calling) ) {
	  g->cmsg_err = "Already calling";
	  return -1;
	}
	/* at present, only original call allowed */
	if ( m->calling && ! pflag(p,NoDiscard) ) {
	  g->cmsg_err = "Only Original Call declaration allowed";
	  return -1;
	}
	/* if player is calling, can only discard the tile drawn.
	   It is supposed to be impossible for a calling player
	   to have claimed from a discard; but just as a precaution
	   we'll check for it and panic. */
	if ( pflag(p,Calling) ) {
	  if ( g->whence == FromDiscard ) {
	    g->cmsg_err = "Calling player discarding after claim??";
	    warn("Calling player discarding after claim");
	    return -2;
	  }
	  if ( m->tile != g->tile ) {
	    g->cmsg_err = "Calling: can't discard that tile";
	    return -1;
	  }
	}
      }

      nodiscard = pflag(p,NoDiscard); /* save as will be cleared by this */
      if ( ! player_discards(p,m->tile) ) {
	g->cmsg_err = "You can't discard that tile";
	return -1;
      }

      g->state = Discarded;
      g->player = s;
      g->tile = m->tile;
      g->serial = m->discard;
      for ( i = 0 ; i < NUM_SEATS ; i++ )
	g->claims[i] = UnknownClaim;
      g->chowpending = 0;
      game_clearflag(g,GFKong);
      game_clearflag(g,GFKongUponKong);
      game_clearflag(g,GFDangerousDiscard);
      game_clearflag(g,GFNoChoice);
      if ( m->calling ) {
	psetflag(p,Calling);
	if ( nodiscard ) { psetflag(p,OriginalCall); }
      }
      g->exposed_tile_count[m->tile]++;
      g->discarded_tile_count[m->tile]++;
      return affected_id;
    }
  case CMsgPlayerDoesntClaim:
    {
      CMsgPlayerDoesntClaimMsg *m = (CMsgPlayerDoesntClaimMsg *)cm;
      
      setups;
      affected_id = m->id;
      /* because clients might easily send noclaims after
	 a timeout, we do not return error for bad noclaims.
	 However, this is a problem for entirely human clients,
	 if there is no timeout, since then a typo will hang
	 the game.
	 FIXME: we should return error if the game does not
	 have a time out set.
      */
      if ( chk ) {
	/* noclaim is legitimate while konging */
	if ( ! ( (g->state == Discarding
		  || g->state == DeclaringSpecials) && g->konging ) ) {
	  if ( g->state != Discarded ) {
	    g->cmsg_err = "No discard to pass on";
	    return affected_id;
	  }
	  if ( g->serial != m->discard ) {
	    g->cmsg_err = "Too late to pass on that discard";
	    return affected_id;;
	  }
	}
      }

      /* We should only record this no claim if the discard 
	 serial is correct. (Otherwise it's probably a duplicate
	 caused by a race condition.) 
	 If it doesn't match, just ignore it, don't return error. */
      if ( g->serial == m->discard )
	g->claims[s] = NoClaim;
      return affected_id;
    }
  case CMsgDangerousDiscard:
    { 
      CMsgDangerousDiscardMsg *m = (CMsgDangerousDiscardMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( m->discard != g->serial ) {
	  g->cmsg_err = "Not current discard (dangerous discard message)";
	  return -1;
	}
	if ( m->id != g->players[g->supplier]->id ) {
	  g->cmsg_err = "Dangerous discarder doesn't match supplier";
	  return -1;
	}
      }
      game_setflag(g,GFDangerousDiscard);
      if ( m->nochoice ) game_setflag(g,GFNoChoice);
      return affected_id;
    }
  case CMsgPlayerClaimsPung:
    {
      CMsgPlayerClaimsPungMsg *m = (CMsgPlayerClaimsPungMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( g->state != Discarded ) {
	  g->cmsg_err = "Can't pung now";
	  return -1;
	}
	if ( g->serial != m->discard ) {
	  g->cmsg_err = "Too late: next discard has been made";
	  return -1;
	}
	if ( g->player == s ) {
	  g->cmsg_err = "Can't claim own discard!";
	  return -1;
	}
	if ( ! player_can_pung(p,g->tile) ) {
	  g->cmsg_err = "Can't pung that tile";
	  return -1;
	}
	if ( pflag(p,Calling) ) {
	  g->cmsg_err = "Calling players can only claim Mah-Jong";
	  return -1;
	}
      } /* end of checking */
      g->claims[s] = PungClaim;
      return affected_id;
    }
  case CMsgPlayerClaimsKong:
    {
      CMsgPlayerClaimsKongMsg *m = (CMsgPlayerClaimsKongMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( g->state != Discarded ) {
	  g->cmsg_err = "Can't kong now";
	  return -1;
	}
	if ( g->serial != m->discard ) {
	  g->cmsg_err = "Too late: next discard has been made";
	  return -1;
	}
	if ( g->player == s ) {
	  g->cmsg_err = "Can't claim own discard!";
	  return -1;
	}
	if ( ! player_can_kong(p,g->tile) ) {
	  g->cmsg_err = "Can't kong that tile";
	  return -1;
	}
	if ( pflag(p,Calling) ) {
	  g->cmsg_err = "Calling players can only claim Mah-Jong";
	  return -1;
	}
      } /* end of checking */
      g->claims[s] = KongClaim;
      return affected_id;
    }
  case CMsgPlayerClaimsChow:
    {
      CMsgPlayerClaimsChowMsg *m = (CMsgPlayerClaimsChowMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( g->state != Discarded ) {
	  g->cmsg_err = "Can't chow now";
	  return -1;
	}
	if ( g->serial != m->discard ) {
	  g->cmsg_err = "Too late: next discard has been made";
	  return -1;
	}
	if ( g->player == s ) {
	  g->cmsg_err = "Can't claim own discard!";
	  return -1;
	}
	if ( s != nextseat(g->player) ) {
	  g->cmsg_err = "Can't chow out of turn";
	  return -1;
	}
	if ( ! player_can_chow(p,g->tile,m->cpos) ) {
	  g->cmsg_err = "Can't make that chow";
	  return -1;
	}
	if ( pflag(p,Calling) ) {
	  g->cmsg_err = "Calling players can only claim Mah-Jong";
	  return -1;
	}
      } /* end of checking */
      g->claims[s] = ChowClaim;
      g->cpos = m->cpos;
      return affected_id;
    }
  case CMsgPlayerClaimsMahJong:
    {
      CMsgPlayerClaimsMahJongMsg *m = (CMsgPlayerClaimsMahJongMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( g->state == Discarded ) {
	if ( chk ) {
	  if ( g->serial != m->discard ) {
	    g->cmsg_err = "Too late: next discard has been made";
	    return -1;
	  }
	  if ( g->player == s ) {
	    g->cmsg_err = "Can't claim own discard!";
	    return -1;
	  }
	  if ( ! player_can_mah_jong(p,g->tile,mjspecflags) ) {
	    g->cmsg_err = "Can't mah-jong with that tile";
	    return -1;
	  }
	}
	g->claims[s] = MahJongClaim;
      } else if ( (g->state == Discarding 
		   || g->state == DeclaringSpecials)
		  && g->konging ) {
	/* trying to rob a kong */
	/* arguably one should be able to abandon one's kong and 
	   claim mahjong instead. However, at present we have no
	   provision for retracting other claims, so we don't have it
	   here either. */
	if ( chk ) {
	  if ( g->serial != m->discard ) {
	    g->cmsg_err = "Too late: next discard has been made";
	    return -1;
	  }
	  if ( g->player == s ) {
	    g->cmsg_err = "Can't rob own kong";
	    return -1;
	  }
	  if ( ! player_can_mah_jong(p,g->tile,mjspecflags) ) {
	    g->cmsg_err = "Can't mah-jong with that tile";
	    return -1;
	  }
	  /* closed kongs can be robbed only for thirteen unique
	     wonders */
	  if ( g->konging == DeclaringKong ) {
	    if ( ! player_can_thirteen_wonders(p,g->tile) ) {
	      g->cmsg_err = "Can only rob a closed kong for Thirteen Wonders";
	      return -1;
	    }
	  }
	  /* in principle, we ought to check that the robbee actually
	     has the kong. However, that would be a consistency error,
	     and in that case we'll die soon enough anyway! */
	}
	g->claims[s] = MahJongClaim;
      } else {
	g->cmsg_err = "Nothing to claim";
	return -1;
      }
      return affected_id;
    }
  case CMsgClaimDenied:
    return affected_id; /* nothing to do */
  case CMsgStateSaved:
    return affected_id; /* nothing to do */
  case CMsgPlayerPairs:
    {
      CMsgPlayerPairsMsg *m = (CMsgPlayerPairsMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	/* sanity checking */
	/* Note that normally we assume the legality of
	   the pair was previously checked at claim time */
	if ( ! ( g->state == MahJonging
		 && g->mjpending
		 && s == g->player ) ) {
	  g->cmsg_err = "Nothing to pair";
	  return -1;
	}
	if ( m->tile != g->tile ) {
	  g->cmsg_err = "Paired tile isn't discard";
	  return -2;
	}
      }

      if ( g->state == MahJonging ) {
	/* checking is complex: not only must
	   the player be able make the pair, but it
	   must then be able to complete the hand */
	Player cp;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( ! player_pairs(p,m->tile) ) {
	  g->cmsg_err = "Can't pair that tile";
	  return -1;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "Can't go out with that pair";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	g->mjpending = 0;
	if ( chk ) mark_dangerous_discards(g);
      } else {
	g->cmsg_err = "Nothing to pair";
	return -1;
      }
      return affected_id;
    }
/* this is a bit awkward: the discard is exposed, so really
   the special set is exposed. But we have no means of
   putting a special set into the player's hand.
   I think we will rely on the external logic using the
   whence game field to distinguish */
  case CMsgPlayerSpecialSet:
    {
      CMsgPlayerSpecialSetMsg *m = (CMsgPlayerSpecialSetMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	/* sanity checking */
	/* Note that normally we assume the legality of
	   the set was previously checked at claim time */
	if ( ! ( g->state == MahJonging
		 && g->mjpending
		 && s == g->player ) ) {
	  g->cmsg_err = "Nothing to claim";
	  return -1;
	}
	if ( m->tile != g->tile ) {
	  g->cmsg_err = "Claimed tile isn't discard";
	  return -2;
	}
      }

      if ( g->state == MahJonging ) {
	/* It is a rule that the special set must be formed last,
	   and use up all the remaining tiles */
	Player cp; int res;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( chk ) {
	  /* only currently recognized special hand is 13 wonders */
	  if ( ! player_can_thirteen_wonders(p,g->tile) ) {
	    g->cmsg_err = "No special hand to form";
	    return -1;
	  }
	}
	/* first we use the given tiles as in a ShowTiles */
	res = player_shows_tiles(p,m->tiles);
	if ( ! res ) {
	  /* we are in deep trouble */
	  g->cmsg_err = "player_shows_tiles failed";
	  return -2;
	}
	if ( chk ) {
	  /* better be the same number of tiles before and after */
	  if ( p->num_concealed != cp.num_concealed ) {
	    g->cmsg_err = "Wrong number of tiles in special set";
	    copy_player(p,&cp); /* restore status quo */
	    return -1;
	  }
	}
	/* now we'll add the discard to the hand */
	if ( ! player_draws_tile(p,m->tile) ) {
	  /* How can this happen ? */
	  g->cmsg_err = "Error in player_draws_tile";
	  return -2;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "No mah jong hand!";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	g->mjpending = 0;
      } else {
	g->cmsg_err = "Can't declare special set now";
	return -1;
      }
      return affected_id;
    }
  case CMsgPlayerPungs:
    {
      CMsgPlayerPungsMsg *m = (CMsgPlayerPungsMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	/* sanity checking */
	/* Note that normally we assume the legality of
	   the pung was previously checked at claim time */
	if ( ! (g->state == Discarded
		|| (g->state == MahJonging
		    && g->mjpending
		    && g->player == s) ) ) {
	  g->cmsg_err = "Nothing to pung";
	  return -1;
	}
	if ( m->tile != g->tile ) {
	  g->cmsg_err = "Punged tile isn't discard";
	  return -2;
	}
      }

      /* what happens now depends on whether this is
	 a claim for MahJong or not */
      if ( g->state == Discarded ) {
	if ( ! player_pungs(p,m->tile) ) {
	  g->cmsg_err = "Can't pung that tile";
	  return -1;
	}
	g->state = Discarding;
	g->konging = NotKonging;
	g->tile = m->tile;
	g->needs = FromNone;
	g->supplier = g->player;
	g->player = s;
	g->whence = FromDiscard;
	g->exposed_tile_count[m->tile] += 2;
      } else if ( g->state == MahJonging ) {
	/* checking is more complex: not only must
	   the player be able make the pung, but it
	   must then be able to complete the hand */
	Player cp;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( ! player_pungs(p,m->tile) ) {
	  g->cmsg_err = "Can't pung that tile";
	  return -1;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "Can't go out with that pung";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	g->mjpending = 0;
      } else {
	g->cmsg_err = "Nothing to chow";
	return -1;
      }
      if ( chk ) {
	mark_dangerous_discards(g);
	set_danger_flags(g,p); /* the player may now be dangerous */
      }
      return affected_id;
    }
  case CMsgPlayerKongs:
    {
      CMsgPlayerKongsMsg *m = (CMsgPlayerKongsMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* we'll assume the legality of the claim was checked */
	if ( g->state != Discarded ) {
	  g->cmsg_err = "Nothing to kong";
	  return -1;
	}
      }

      if ( ! player_kongs(p,m->tile) ) {
	g->cmsg_err = "Can't kong that tile";
	return -2;
	/* This is a consistency error. */
      }

      g->state = Discarding;
      g->needs = FromLoose;
      g->supplier = g->player;
      g->player = s;
      g->konging = NotKonging; /* yes! This is not robbable. */
      if ( game_flag(g,GFKong) ) { game_setflag(g,GFKongUponKong); }
      game_setflag(g,GFKong);
      g->exposed_tile_count[m->tile] += 3;
      if ( chk ) set_danger_flags(g,p); /* the player may now be dangerous */
      return affected_id;
    }
  case CMsgPlayerChows:
    {
      CMsgPlayerChowsMsg *m = (CMsgPlayerChowsMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* sanity checking */
	/* Note that normally we assume the legality of
	   the chow was previously checked at claim time */
	if ( ! (g->state == Discarded
		|| (g->state == MahJonging
		    && g->mjpending
		    && g->player == s) ) ) {
	  g->cmsg_err = "Nothing to chow";
	  return -1;
	}
	if ( m->tile != g->tile ) {
	  g->cmsg_err = "Chowed tile isn't discard";
	  return -2;
	}
      }

      /* if the position is AnyPos, then this is addressed
	 to a successful claimant to tell it to specify the position.
	 There is therefore nothing to do here,
	 except set the pending flag.
	 Oops! Actually, if we're mah-jonging, we have to check that
	 the player can actually go out by chowing the tile.
      */
      if ( m->cpos == AnyPos ) {
	if ( chk && g->state == MahJonging ) {
	/* checking is more complex: not only must
	   the player be able make the chow in some position, but it
	   must then be able to complete the hand */
	  ChowPosition pos;
	  int ok = 0;
	  for ( pos = Lower; pos <= Upper; pos++ ) {
	    Player cp;
	    copy_player(&cp,p); 
	    if ( ! player_chows(&cp,m->tile,pos) ) {
	      continue;
	    }
	    if ( player_can_mah_jong(&cp,HiddenTile,mjspecflags) ) {
	      ok = 1;
	      break;
	    }
	  }
	  if ( ! ok ) { 
	    g->cmsg_err = "Can't go out by chowing";
	    return -1;
	  }
	}
	g->chowpending = 1;
	return affected_id;
      }

      /* is there a Mah Jong pending? */
      if ( g->state == Discarded ) {
	if ( ! player_chows(p,m->tile,m->cpos) ) {
	  g->cmsg_err = "Can't chow that tile that way";
	  return -1;
	}
	g->state = Discarding;
	g->konging = NotKonging;
	g->tile = m->tile;
	g->needs = FromNone;
	g->supplier = g->player;
	g->player = s;
	g->whence = FromDiscard;
	/* g->exposed_tile_count[m->tile] is unchanged */
	switch ( m->cpos ) {
	case Lower:
	  g->exposed_tile_count[m->tile+1]++;
	  g->exposed_tile_count[m->tile+2]++;
	  break;
	case Middle:
	  g->exposed_tile_count[m->tile+1]++;
	  g->exposed_tile_count[m->tile-1]++;
	  break;
	case Upper:
	  g->exposed_tile_count[m->tile-1]++;
	  g->exposed_tile_count[m->tile-2]++;
	  break;
	default:
	  warn(g->cmsg_err = "Impossible chowposition");
	  return -2;
	}
      } else if ( g->state == MahJonging ) {
	/* checking is more complex: not only must
	   the player be able make the chow, but it
	   must then be able to complete the hand */
	Player cp;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( ! player_chows(p,m->tile,m->cpos) ) {
	  g->cmsg_err = "Can't chow that tile that way";
	  return -1;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "Can't go out with that chow";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	g->chowpending = 0;
	g->mjpending = 0;
      } else {
	g->cmsg_err = "Nothing to chow";
	return -1;
      }
      if ( chk ) {
	mark_dangerous_discards(g);
	set_danger_flags(g,p); /* the player may now be dangerous */
      }
      return affected_id;
    }
  case CMsgWashOut:
    g->state = HandComplete;
    g->player = noseat;
    for (i=0;i<NUM_SEATS;i++) g->claims[i] = 0;
    return affected_id;
  case CMsgPlayerMahJongs:
    {
      CMsgPlayerMahJongsMsg *m = (CMsgPlayerMahJongsMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( m->tile != HiddenTile ) {
	/* should be currently in the Discarding state */
	if ( chk ) {
	  if ( ! (g->state == Discarding
		  && g->player == s ) ) {
	    g->cmsg_err = "Don't have a complete hand";
	    return -1;
	  }
	  if ( !player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	    g->cmsg_err = "Don't have a Mah Jong";
	    return -1;
	  }
	  /* consistency check */
	  if ( g->tile != HiddenTile
	       && m->tile != g->tile ) {
	    g->cmsg_err = "Tile in mah-jong claim doesn't match drawn tile";
	    return -2;
	  }
	}
	/* g->supplier is unchanged */
	/* g->whence is unchanged */
	/* g->player is unchanged */
	/* g->tile is unchanged */
	g->mjpending = 0;
	g->chowpending = 0;
	g->state = MahJonging;
      } else {
	/* We should be currently in the Discarded state */
	if ( chk ) {
	  if ( ! g->state == Discarded ) {
	    g->cmsg_err = "No discard to use";
	    return -1;
	  }
	}
	g->supplier = g->player;
	g->whence = FromDiscard;
	g->player = s;
	/* g->tile is unchanged */
	g->mjpending = 1;
	g->chowpending = 0;
	g->state = MahJonging;
       }
      return affected_id;
    }
  case CMsgPlayerFormsClosedPair:
    {
      CMsgPlayerFormsClosedPairMsg *m = (CMsgPlayerFormsClosedPairMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( g->state != MahJonging ) {
	  g->cmsg_err = "Not scoring now";
	  return -1;
	}
      }
      /* if this is a non-mah-jonging player, it's
	 easy. */
      if ( s != g->player ) {
	if ( ! player_forms_closed_pair(p,m->tile) ) {
	  g->cmsg_err = "Don't have that pair" ;
	  return -1;
	}
      } else {
	/* checking is complex: not only must
	   the player be able make the pair, but it
	   must then be able to complete the hand */
	Player cp;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( ! player_forms_closed_pair(p,m->tile) ) {
	  g->cmsg_err = "Don't have that pair";
	  return -1;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "Can't go out with that pair";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
      }
      return affected_id;
    }
  case CMsgPlayerFormsClosedSpecialSet:
    {
      CMsgPlayerFormsClosedSpecialSetMsg *m = (CMsgPlayerFormsClosedSpecialSetMsg *)cm;
      
      setups;
      affected_id = m->id;
      if ( chk ) {
	/* sanity checking */
	/* Note that normally we assume the legality of
	   the set was previously checked at claim time */
	if ( ! ( g->state == MahJonging
		 && s == g->player ) ) {
	  g->cmsg_err = "Not going out";
	  return -1;
	}
      }

      if ( g->state == MahJonging ) {
	/* It is a rule that the special set must be formed last,
	   and use up all the remaining tiles */
	Player cp; int res;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( chk ) {
	  /* only currently recognized special hand is 13 wonders */
	  if ( ! player_can_thirteen_wonders(p,HiddenTile) ) {
	    g->cmsg_err = "No special hand to form";
	    return -1;
	  }
	}
	/* first we use the given tiles as in a ShowTiles */
	res = player_shows_tiles(p,m->tiles);
	if ( ! res ) {
	  /* we are in deep trouble */
	  g->cmsg_err = "player_shows_tiles failed";
	  return -2;
	}
	if ( chk ) {
	  /* better be the same number of tiles before and after */
	  if ( p->num_concealed != cp.num_concealed ) {
	    g->cmsg_err = "Wrong number of tiles in special set";
	    copy_player(p,&cp); /* restore status quo */
	    return -1;
	  }
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "No mah jong hand!";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
      } else {
	g->cmsg_err = "Can't declare special set now";
	return -1;
      }
      return affected_id;
    }
  case CMsgPlayerFormsClosedPung:
    {
      CMsgPlayerFormsClosedPungMsg *m = (CMsgPlayerFormsClosedPungMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( g->state != MahJonging ) {
	  g->cmsg_err = "Not scoring now";
	  return -1;
	}
      }
      /* if this is a non-mah-jonging player, it's
	 easy. */
      if ( s != g->player ) {
	if ( ! player_forms_closed_pung(p,m->tile) ) {
	  g->cmsg_err = "Don't have that pung" ;
	  return -1;
	}
      } else {
	/* checking is complex: not only must
	   the player be able make the pung, but it
	   must then be able to complete the hand */
	Player cp;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( ! player_forms_closed_pung(p,m->tile) ) {
	  g->cmsg_err = "Don't have that pung";
	  return -1;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "Can't go out with that pung";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
      }
      return affected_id;
    }
  case CMsgPlayerFormsClosedChow:
    {
      CMsgPlayerFormsClosedChowMsg *m = (CMsgPlayerFormsClosedChowMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( g->state != MahJonging ) {
	  g->cmsg_err = "Not scoring now";
	  return -1;
	}
      }
      /* if this is a non-mah-jonging player, it's
	 easy. */
      if ( s != g->player ) {
	if ( ! player_forms_closed_chow(p,m->tile,Lower) ) {
	  g->cmsg_err = "Don't have that chow" ;
	  return -1;
	}
      } else {
	/* checking is complex: not only must
	   the player be able make the chow, but it
	   must then be able to complete the hand */
	Player cp;
	if ( chk ) copy_player(&cp,p); /* in case next fails */
	if ( ! player_forms_closed_chow(p,m->tile,Lower) ) {
	  g->cmsg_err = "Don't have that chow";
	  return -1;
	}
	if ( chk && ! player_can_mah_jong(p,HiddenTile,mjspecflags) ) {
	  g->cmsg_err = "Can't go out with that chow";
	  copy_player(p,&cp); /* restore status quo */
	  return -1;
	}
	if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
      }
      return affected_id;
    }
  case CMsgPlayerDeclaresClosedKong:
    {
      CMsgPlayerDeclaresClosedKongMsg *m = (CMsgPlayerDeclaresClosedKongMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( ! (((g->state == Discarding 
		  && (g->protversion < 1010 || g->whence != FromDiscard))
		 || g->state == DeclaringSpecials)
		&& g->player == s ) ) {
	  g->cmsg_err = "Can't declare a closed kong now";
	  return -1;
	}
	/* We have to use the test function here, because
	   if either kong or tile drawing fails we must
	   leave the game unchanged */
	if ( !player_can_declare_closed_kong(p,m->tile) ) {
	  g->cmsg_err = "Don't have that kong";
	  return -1;
	}
      }

      /* This can't fail in the chking case */
      player_declares_closed_kong(p,m->tile);
      g->needs = FromLoose;
      g->konging = DeclaringKong;
      g->tile = m->tile;
      g->serial = m->discard;
      for (i=0;i<NUM_SEATS;i++) g->claims[i] = 0;
      if ( game_flag(g,GFKong) ) { game_setflag(g,GFKongUponKong); }
      game_setflag(g,GFKong);
      g->exposed_tile_count[m->tile] += 4;
      if ( chk ) set_danger_flags(g,p); /* the player may now be dangerous */
      return affected_id;
    }
  case CMsgPlayerAddsToPung:
    {
      CMsgPlayerAddsToPungMsg *m = (CMsgPlayerAddsToPungMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( ! (g->state == Discarding
	    && g->player == s
	    /* before 1010, adding to pung was allowed anytime, which
	       it shouldn't have been.
	       Since version 1.11 (protocol 1110), 
	       it is allowed to add to a pung that you have just claimed.
	    */
	    && (g->protversion < 1010 
	      || g->whence != FromDiscard
	      || (g->protversion >= 1110
		/* the last discard was claimed by us for Pung */
		/* these two conditions are not prima facie enough
		   to guarantee that the last thing we did was actually
		   to make the pung, but in fact they are. There's no
		   way we can reach this state apart from that. */
		&& g->claims[s] == PungClaim
		/* the pung just claimed must be the one we're
		   adding the tile to */
		&& g->tile == m->tile)
		)
		) ) {
	  g->cmsg_err = "Can't make a kong now";
	  return -1;
	}
	/* We have to use the test function here, because
	   if either kong or tile drawing fails we must
	   leave the game unchanged */
	if ( !player_can_add_to_pung(p,m->tile) ) {
	  g->cmsg_err = "Don't have the tiles for that kong";
	  return -1;
	}
      }

      /* This can't fail in the chking case */
      player_adds_to_pung(p,m->tile);
      g->needs = FromLoose;
      g->konging = AddingToPung;
      g->tile = m->tile;
      g->serial = m->discard;
      for (i=0;i<NUM_SEATS;i++) g->claims[i] = 0;
      if ( game_flag(g,GFKong) ) { game_setflag(g,GFKongUponKong); }
      game_setflag(g,GFKong);
      g->exposed_tile_count[m->tile]++ ;
      if ( chk ) set_danger_flags(g,p); /* the player may now be dangerous */
      /* a kong can be robbed, so is like a discard */
      game_clearflag(g,GFDangerousDiscard);
      game_clearflag(g,GFNoChoice);
      return affected_id;
    }
  case CMsgCanMahJong:
    /* this just a reply to a query, and requires no action */
    return affected_id;
  case CMsgPlayerRobsKong:
    {
      CMsgPlayerRobsKongMsg *m = (CMsgPlayerRobsKongMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	if ( ! ((g->state == Discarding
		 || g->state == DeclaringSpecials)
		&& g->konging ) ) {
	  g->cmsg_err = "No kong to rob";
	  return -1;
	}
	if ( m->tile != g->tile ) {
	  g->cmsg_err = "Claimed tile doesn't match that of kong";
	  return -1;
	}
	if ( ! player_can_mah_jong(p,m->tile,mjspecflags) ) {
	  g->cmsg_err = "Can't mah-jong with that tile";
	  return -1;
	}
      }
      /* To rob a kong, we will rob the player, and then move into
	 the MahJonging state with a pending "discard", since the winning
	 player needs to say which set it's forming. */
      if ( ! player_kong_is_robbed(g->players[g->player],m->tile) ) {
	g->cmsg_err = "Victim doesn't have that kong!";
	return -1;
      }
      g->state = MahJonging;
      g->whence = FromRobbedKong;
      g->needs = FromNone; /* this was set by the AddToPung that we robbed */
      g->supplier = g->player;
      g->player = s;
      g->tile = m->tile;
      g->mjpending = 1;
      g->chowpending = 0;
      g->konging = NotKonging;
      game_clearflag(g,GFKong);
      game_clearflag(g,GFKongUponKong);
      return affected_id;
    }
  case CMsgPlayerShowsTiles:
    {
      CMsgPlayerShowsTilesMsg *m = (CMsgPlayerShowsTilesMsg *)cm;

      setups;
      affected_id = m->id;
      if ( chk ) {
	/* it's not allowed to show tiles twice */
	if ( pflag(p,HandDeclared) ) {
	  g->cmsg_err = "Hand already declared";
	  return -1;
	}
	if ( p->num_concealed > 0 ) {
	  /* the HandComplete case arises when tiles are revealed
	     after a washout */
	  if ( g->state != MahJonging && g->state != HandComplete ) {
	    g->cmsg_err = "Can't reveal your tiles now!";
	    return -1;
	  }
	  if ( g->state == MahJonging && s == g->player ) {
	    g->cmsg_err = "Must finish making your sets";
	    return -1;
	  }
	}
      }
      
      if ( ! player_shows_tiles(p,m->tiles) && chk ) {
	g->cmsg_err = "Couldn't show those tiles";
	return -1;
      }
      /* the player_shows_tiles fn already set the HandDeclared flag */
      return affected_id;
    }
  case CMsgSwapTile:
    {
      CMsgSwapTileMsg *m = (CMsgSwapTileMsg *)cm;

      setups;
      affected_id = m->id;
      if ( ! player_swap_tile(p,m->oldtile,m->newtile) ) {
	g->cmsg_err = p->err;
	return -1;
      }
      return affected_id;
    }
	
  case CMsgHandScore:
    {
      CMsgHandScoreMsg *m = (CMsgHandScoreMsg *)cm;

      setups;
      affected_id = m->id;
      set_player_hand_score(p,m->score);
      return affected_id;
    }
  case CMsgSettlement:
    {
      CMsgSettlementMsg *m = (CMsgSettlementMsg *)cm;

      change_player_cumulative_score(g->players[east],m->east);
      change_player_cumulative_score(g->players[south],m->south);
      change_player_cumulative_score(g->players[west],m->west);
      change_player_cumulative_score(g->players[north],m->north);
      g->state = HandComplete;
      for ( i=0;i<NUM_SEATS;i++) g->claims[i] = 0;
      /* g->player is still correct */
      if ( g->player == east ) g->hands_as_east++;
      return affected_id;
    }
  case CMsgError:
    /* it is an error to pass this function an error */
    g->cmsg_err = "Game shouldn't be given error messages";
    return -1;
  case CMsgGameOver:
    /* nothing to do: up to client */
    return affected_id;
  case CMsgGameOption:
    {
      CMsgGameOptionMsg *m = (CMsgGameOptionMsg *)cm;

      setups;
      if ( chk && (g->manager != 0) && g->manager != m->id ) {
	g->cmsg_err = "Not authorized to set game options";
	return -1;
      }
      if ( game_set_option(g,&m->optentry) == 0 && chk ) {
	return -1;
      }
      return affected_id;
    }
  case CMsgChangeManager:
    {
      CMsgChangeManagerMsg *m = (CMsgChangeManagerMsg *)cm;

      setups;
      if ( chk && (g->manager != 0) && g->manager != m->id ) {
	g->cmsg_err = "Not authorized to change the manager";
	return -1;
      }
      g->manager = m->manager;
      return affected_id;
    }
  case CMsgWall:
    {
      CMsgWallMsg *m = (CMsgWallMsg *)cm;
      int i; int n;
      char tn[5];
      char *tp;
      Tile t;

      if ( g->state != HandComplete ) {
	g->cmsg_err = "Can only set the wall between hands";
	return -1;
      }
      for ( i = 0, tp = m->wall ; i < MAX_WALL_SIZE ; i++, tp += n ) {
	if ( sscanf(tp,"%2s%n",tn,&n) == 0 ) break;
	t = tile_decode(tn);
	if ( t == ErrorTile ) {
	  g->cmsg_err = "bad wall in WallMsg";
	  return -1;
	}
	g->wall.tiles[i] = t;
      }
      return 0;
    }
  case CMsgMessage:
    /* nothing to do for us */
    return ((CMsgMessageMsg *)cm)->addressee;
  case CMsgComment:
    return 0;
  case CMsgPlayerOptionSet:
    /* This should be dealt with by client code, not us */
    g->cmsg_err = "Player options are not dealt with by the game code";
    return -1;
  case CMsgAuthReqd:
    /* This should be dealt with by client code, not us */
    g->cmsg_err = "Authorization are not dealt with by the game code";
    return -1;
  case CMsgRedirect:
    /* This should be dealt with by client code, not us */
    g->cmsg_err = "Redirection is not dealt with by the game code";
    return -1;
  }
  /* if we get to here, we were passed a bad CMsg type, which should
     be impossible */
  warn("Unknown Controller message type %d",cm->type);
  return -2;
}

/* say whether a game has started or not */
int game_has_started(Game *g) {
  if ( g == NULL ) return 0;
  if ( g->round == UnknownWind ) return 0;
  if ( g->round != EastWind ) return 1;
  /* this next one is tricky: if there's no player in east,
     we've already deleted it, so it must be OK to delete others */
  if ( g->players[east]->id == 0 ) return 0;
  if ( g->players[east]->id != g->firsteast ) return 1;
  if ( g->hands_as_east > 0 ) return 1;
  if ( g->state != HandComplete ) return 1;
  return 0;
}

/* this internal function sets the danger signal flags for a player.
   It does NOT deal with the DangerEnd flag.
   The game argument is currently unused.
   It should be called whenever a player declares a set. */

static void set_danger_flags(Game *g UNUSED, PlayerP p) {
  int i,s;
  int numchows,numpungs,numkongs,allhonours,
    allterminals,dragonsets,windsets,
    allgreen,allbamboo,allcharacter,allcircle,
    allbamboohonour,allcharacterhonour,allcirclehonour;

  numchows = 0;
  numkongs = 0;
  numpungs = 0;
  allterminals = 1;
  allhonours = 1;
  windsets = 0;
  dragonsets = 0;
  allgreen = 1;
  allbamboo = 1;
  allcharacter = 1;
  allcircle = 1;
  allbamboohonour = 1;
  allcharacterhonour = 1;
  allcirclehonour = 1;

  s = p->wind-1; /* player's seat */
  presetdflags(p,s); /* clear all flags */

  /* go through collecting information.
   This is distressingly similar to code in scoring.c */
  for ( i = 0; i < MAX_TILESETS; i++ ) {
    TileSetP t = (TileSetP) &p->tilesets[i];

    if ( t->type == Empty ) continue;

    if ( t->type == Chow )
      numchows++,allterminals=allhonours=0;
    if ( t->type == Pung ) numpungs++;
    if ( t->type == Kong ) numkongs++;

    dragonsets += is_dragon(t->tile);
    windsets += is_wind(t->tile);

    switch ( suit_of(t->tile) ) {
    case BambooSuit:
      allcharacter = allcharacterhonour = 0;
      allcircle = allcirclehonour = 0;
      switch ( t->type ) {
      case Chow:
	if ( !is_green(t->tile) 
	  || !is_green(t->tile+1)
	  || !is_green(t->tile+2) ) allgreen = 0;
	break;
      default:
	if ( !is_green(t->tile) ) allgreen = 0;
      }
      break;
    case CharacterSuit:
      allbamboo = allbamboohonour = 0;
      allcircle = allcirclehonour = 0;
      allgreen = 0;
      break;
    case CircleSuit:
      allbamboo = allbamboohonour = 0;
      allcharacter = allcharacterhonour = 0;
      allgreen = 0;
      break;
    case DragonSuit:
      allbamboo = allcharacter = allcircle = 0;
      if ( !is_green(t->tile) ) allgreen = 0;
      break;
    case WindSuit:
      allbamboo = allcharacter = allcircle = 0;
      allgreen = 0;
      break;
    default:
      warn("Strange suit seen in hand");
    }

    if ( !is_honour(t->tile) ) allhonours = 0;
    if ( !is_terminal(t->tile) ) allterminals = 0;
  }

  /* now set the flags again */
  if ( numpungs+numkongs+numchows >= 3  &&  allbamboo ) 
    psetdflags(p,s,DangerBamboo);
  if ( numpungs+numkongs+numchows >= 3  &&  allcharacter ) 
    psetdflags(p,s,DangerCharacter);
  if ( numpungs+numkongs+numchows >= 3  &&  allcircle ) 
    psetdflags(p,s,DangerCircle);
  if ( windsets >= 3 ) psetdflags(p,s,DangerWind);
  if ( dragonsets >= 2 ) psetdflags(p,s,DangerDragon);
  if ( numpungs+numkongs+numchows >= 3  &&  allhonours ) 
    psetdflags(p,s,DangerHonour);
  if ( numpungs+numkongs+numchows >= 3  &&  allgreen ) 
    psetdflags(p,s,DangerGreen);
  if ( numpungs+numkongs+numchows >= 3  &&  allterminals ) 
    psetdflags(p,s,DangerTerminal);
  /* This is always set (for convenience); it's not a property of
     the player, but of the game as a whole, and will only be applied
     if a dangerous discard is made. */
  psetdflags(p,s,DangerEnd); 
}

/* This internal function sets the flags for the supply of a dangerous
   discard. It is called (by handle_cmsg) after a claim has been implemented,
   but before the claiming player's danger flags are re-evaluated. */

static void mark_dangerous_discards(Game *g) {
  PlayerP p;
  seats s,sup; /* seat of player, seat of supplier */
  Tile t; /* the discard claimed */
  unsigned int danger;
  int nochoice;

  p = g->players[g->player]; 
  s = p->wind-1;
  sup = g->supplier;
  t = g->tile;

  danger = 0;
  nochoice = 0;

  if ( pdflag(p,s,DangerBamboo) 
       && suit_of(t) == BambooSuit ) danger |= DangerBamboo;

  if ( pdflag(p,s,DangerCharacter) 
       && suit_of(t) == CharacterSuit ) danger |= DangerCharacter;
  
  if ( pdflag(p,s,DangerCircle) 
       && suit_of(t) == CircleSuit ) danger |= DangerCircle;
  
  if ( pdflag(p,s,DangerWind) 
       && suit_of(t) == WindSuit ) danger |= DangerWind;

  if ( pdflag(p,s,DangerDragon) 
       && suit_of(t) == DragonSuit ) danger |= DangerDragon;
  
  if ( pdflag(p,s,DangerHonour) 
       && is_honour(t) ) danger |= DangerHonour;
  
  if ( pdflag(p,s,DangerTerminal) 
       && is_terminal(t) ) danger |= DangerTerminal;
  
  if ( pdflag(p,s,DangerGreen) 
       && is_green(t) ) danger |= DangerGreen;

  /* End danger is special */
  if ( g->wall.live_end - g->wall.live_used <= 4 
       && g->discarded_tile_count[t] <= 1 ) danger |= DangerEnd;

  /* if we've determined that the discard was dangerous,
     we now have to see whether the supplier had no choice */
  if ( danger && !pflag(g->players[sup],Hidden) ) {
    seats i;
    int j;
    PlayerP psup = g->players[sup];
    PlayerP pp;
    Tile t;
    int dangerous;

    nochoice = 1;
    for ( j = 0; j < psup->num_concealed ; j++ ) {
      t = psup->concealed[j];

      dangerous = 0;
      for ( i = 0 ; i < NUM_SEATS; i++ ) {
	if ( i == sup ) continue;
	pp = g->players[i];
	if ( 
	    ( pdflag(pp,i,DangerBamboo) && suit_of(t) == BambooSuit )
	    || ( pdflag(pp,i,DangerCharacter) && suit_of(t) == CharacterSuit )
	    || ( pdflag(pp,i,DangerCircle) && suit_of(t) == CircleSuit ) 
	    || ( pdflag(pp,i,DangerWind) && suit_of(t) == WindSuit )
	    || ( pdflag(pp,i,DangerDragon) && suit_of(t) == DragonSuit )
	    || ( pdflag(pp,i,DangerHonour) && is_honour(t) )
	    || ( pdflag(pp,i,DangerTerminal) && is_terminal(t) )
	    || ( pdflag(pp,i,DangerGreen) && is_green(t) )
	    || ( g->wall.live_end - g->wall.live_used <= 4 
		 && g->discarded_tile_count[t] <= 1 ) 
	    ) dangerous = 1;
      }
      if ( ! dangerous ) {
	nochoice = 0;
	break;
      }
    }
  }

  if ( danger && ! nochoice ) {
    seats i;
    /* The supplier has supplied a dangerous discard without excuse */
    psetdflags(p,sup,danger);
    /* and this discharges the liability of any other player that
       previously made a dangerous discard */
    for ( i = 0 ; i < NUM_SEATS ; i++ ) {
      if ( i != s && i != sup ) presetdflags(p,i);
    }
  }
  
  /* set the game danger flag */
  if ( danger ) game_setflag(g,GFDangerousDiscard);
  if ( danger && nochoice ) game_setflag(g,GFNoChoice);
}

/* dump the game state in human-readable form into a large internal
   buffer and return it.
   This is intended for use in possibly dire situations, so it doesn't
   do any memory management. As a service to the caller, it returns in the 
   second argument (if non-null) the number of spare bytes in the returned 
   buffer (counting from after the string terminator)
*/
char *game_print_state(Game *g, int *bytes_left) {
  static char buf[4096];
  static char claims[NUM_SEATS*15], ready[NUM_SEATS*3], flags[128];
  static char walltiles[MAX_WALL_SIZE*4];
  int i;
  int n; char *b;
  int tot;

  if ( g == NULL ) { strcat(buf,"NO GAME\n"); return buf; }

  claims[0] = 0; ready[0] = 0; flags[0] = 0;
  for (i=0; i<NUM_SEATS; i++) {
    char buf[15];
    strcat(claims,nullprotect(game_print_Claims(g->claims[i])));
    strcat(claims," ");
    sprintf(buf,"%d ",g->ready[i]);
    strcat(ready,buf);
  }
  for ( i = 0; i < 32; i++ ) {
    if ( game_flag(g,i) ) {
      strcat(flags,nullprotect(game_print_GameFlags(i)));
      strcat(flags," ");
    }
  }
  walltiles[0] = 0;
  for ( i = 0; i < g->wall.size; i++ ) {
    strcat(walltiles,tile_code(g->wall.tiles[i]));
    strcat(walltiles," ");
  }

  buf[0] = 0;
  n = sprintf(buf,""
"Game State:\n"
"players: (see later)\n"
"round: %s\n"
"hands_as_east: %d\n"
"firsteast: %d\n"
"state: %s\n"
"active: %d\n"
"paused: %s\n"
"player: %s\n"
"whence: %s\n"
"supplier: %s\n"
"tile: %s\n"
"needs: %s\n"
"serial: %d\n"
"claims: %s\n"
"cpos: %s\n"
"chowpending: %d\n"
"mjpending: %d\n"
"konging: %s\n"
"ready: %s\n"
"flags: %s\n"
"wall.tiles: %s\n"
"wall.live_used: %d\n"
"wall.live_end: %d\n"
"wall.dead_end: %d\n"
"wall.size: %d\n"
"cmsg_check: %d\n"
"cmsg_err: %s\n"
"protversion: %d\n"
"manager: %d\n"
"option_table: (see later)\n"
"fd: %d\n"
"cseqno: %d\n"
"userdata: %p\n",
    nullprotect(tiles_print_TileWind(g->round)),
    g->hands_as_east,
    g->firsteast,
    nullprotect(game_print_GameState(g->state)),
    g->active,
    g->paused ? g->paused : "-no-",
    nullprotect(game_print_seats(g->player)),
    nullprotect(game_print_Whence(g->whence)),
    nullprotect(game_print_seats(g->supplier)),
    tile_code(g->tile),
    nullprotect(game_print_Whence(g->needs)),
    g->serial,
    claims,
    nullprotect(player_print_ChowPosition(g->cpos)),
    g->chowpending,
    g->mjpending,
    nullprotect(game_print_Konging(g->konging)),
    ready,
    flags,
    walltiles,
    g->wall.live_used,
    g->wall.live_end,
    g->wall.dead_end,
    g->wall.size,
    g->cmsg_check,
    g->cmsg_err ? g->cmsg_err : "-none-",
    g->protversion,
    g->manager,
    g->fd,
    g->cseqno,
    g->userdata
    );
  b = buf + n;
  tot = n;
  for ( i = 0; i < NUM_SEATS; i++ ) {
    n = sprintf(b,"%s",g->players[i] ? player_print_state(g->players[i]) :
      "NO PLAYER\n");
    b += n;
    tot += n;
  }
  if ( bytes_left ) *bytes_left = n-1;
  return buf;
}

#include "game-enums.c"
