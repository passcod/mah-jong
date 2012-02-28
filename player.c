/* $Header: /home/jcb/MahJong/newmj/RCS/player.c,v 12.0 2009/06/28 20:43:13 jcb Rel $
 * player.c
 * Implements functions on players.
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/player.c,v 12.0 2009/06/28 20:43:13 jcb Rel $";

#define PLAYER_C /* so that PlayerP is not const */
#include "player.h"
#include "sysdep.h"

/* At present, this function is not exported. If it becomes
   exported, the name should perhaps be made better.
   player_has_mah_jong: return true if the player's hand in its
   current state of organization is a complete mah-jong hand.
   (That is, the hand has been organized into four sets and a pair, etc.)
   The last arg is flags for seven pairs etc.
*/
static int player_has_mah_jong(PlayerP p,MJSpecialHandFlags flags);

/* utility function: copy player record into (already allocated)
   space. Returns new (for consistency with memcpy) */
PlayerP copy_player(PlayerP new,const PlayerP old) {
  return (PlayerP) memcpy((void *)new, (void *)old, sizeof(Player));
}
  

int num_tiles_in_set(TileSetP tp) {
  switch ( tp->type ) {
  case Empty:
    return 0;
  case Pair:
  case ClosedPair:
    return 2;
  case Chow:
  case ClosedChow:
  case Pung:
  case ClosedPung:
    return 3;
  case Kong:
  case ClosedKong:
    return 4;
  default:
    warn("num_tiles_in_set: unknown tile set type %d\n",tp->type);
    /* this will cause chaos, but that's the idea ... */
    return -1;
  }
}


void initialize_player(PlayerP p) {
  p->err = NULL;
  p->id = 0;
  p->name = NULL;
  p->wind = UnknownWind;
  p->flags = 0;
  p->cumulative_score = 0;
  player_newhand(p,UnknownWind);
}

void player_newhand(PlayerP p,TileWind w) {
  int i;
  p->err = NULL;
  p->wind = w;
  p->num_concealed = 0;
  p->discard_hint = -1;
  for ( i = 0; i < MAX_CONCEALED; i++ ) (p->concealed)[i] = HiddenTile;
  for ( i = 0; i < MAX_TILESETS; i++ ) {
    (p->tilesets)[i].type = Empty;
    (p->tilesets)[i].tile = HiddenTile;
    (p->tilesets)[i].annexed = 0;
  }
  p->num_specials = 0;
  for ( i = 0; i < 8; i++ ) (p->specials)[i] = HiddenTile;
  psetflag(p,Hidden);
  pclearflag(p,HandDeclared);
  pclearflag(p,MahJongged);
  psetflag(p,NoDiscard);
  pclearflag(p,Calling);
  pclearflag(p,OriginalCall);
  for (i=0; i < NUM_SEATS; i++) p->dflags[i] = 0;
  p->hand_score = -1;
}

void set_player_id(PlayerP p, int id) {
  p->err = NULL;
  p->id = id;
}

void set_player_name(PlayerP p, const char *n) {
  p->err = NULL;
  if ( p->name ) free(p->name);
  if ( n ) {
    p->name = (char *)malloc(strlen(n)+1);
    strcpy(p->name,n);
  } else p->name = (char *)0;
}

void set_player_cumulative_score(PlayerP p, int s) {
  p->err = NULL;
  p->cumulative_score = s;
}

void change_player_cumulative_score(PlayerP p, int d) {
  p->err = NULL;
  p->cumulative_score += d;
}

void set_player_hand_score(PlayerP p, int h) {
  p->err = NULL;
  p->hand_score = h;
}

void set_player_userdata(PlayerP p, void *ud) {
  p->err = NULL;
  p->userdata = ud;
}

/* utility functions. Maybe they should be exported.
   But we should probably adhere to the convention that exported
   functions do not leave data structures in intermediate states,
   as these do.
*/

/* player_count_tile: looks for copies of t in p's concealed tiles.
   returns number found or -1 on error.
   Should not be called on unknown players.
*/
int player_count_tile(PlayerP p, Tile t) {
  int n,i;

  p->err = NULL;
  if ( pflag(p,Hidden) ) {
    p->err = "Can't count tiles of hidden player";
    return -1;
  }

  for ( i = 0, n = 0 ; i < p->num_concealed ; i++ ) {
    if ( p->concealed[i] == t ) n++ ;
  }
  return n;
}


/* add_tile: adds t to p's concealed tiles. Should not be called
   on hidden players.
*/
static int add_tile(PlayerP p, Tile t) {

  assert(!pflag(p,Hidden)); /* nonsense to call this on hidden player */

  //  assert(p->num_concealed < MAX_CONCEALED);
  if ( ! (p->num_concealed < MAX_CONCEALED) ) {
    error("player.c:add_tile(): player id %d has too many tiles: num_concealed = %d\n",p->id,p->num_concealed);
    return 0;
  }
  p->concealed[p->num_concealed++] = t;
  return 1;
}

int player_set_discard_hint(PlayerP p, int h) {
  if ( h < 0 || h >= p->num_concealed ) {
    warn("player_discard_hint: out of range hint %d",h);
    return 0;
  }
  p->discard_hint = h;
  return 1;
}

/* remove_tile: removes one instance of t from p's concealed tiles.
   Returns 1 on success, 0 on failure.
   Should not be called on hidden players.
*/
static int remove_tile(PlayerP p, Tile t) {
  int i,d;

  assert(!pflag(p,Hidden)); /* nonsense to call this on hidden player */

  i = 0;
  d = p->discard_hint;
  p->discard_hint = -1; /* clear it whatever happens next */
  if ( d >= 0 ) {
    if ( d >= p->num_concealed ) {
      warn("Bad discard hint %d -- too big",d);
      d = -1;
    } else if ( p->concealed[d] != t ) {
      warn("Bad discard hint -- tile %d is not %d",d,t);
      d = -1;
    }
  }
  if ( d >= 0 ) 
    i = d;
  else
    while ( i < p->num_concealed && p->concealed[i] != t ) i++ ;
  if ( i == p->num_concealed ) return 0;
  while ( i+1 < p->num_concealed ) {
    p->concealed[i] = p->concealed[i+1];
    i++ ;
  }
  p->num_concealed -= 1 ;
  return 1;
}

/* add_tileset: adds a tile set of type ty and tile t to p.
   Returns 1 on success, 0 on failure.
   Always sets the annexed flag to 0.
*/
static int add_tileset(PlayerP p, TileSetType ty, Tile t) {
  int s = 0;

  while ( s < MAX_TILESETS && p->tilesets[s].type != Empty ) s++ ;

  assert(s < MAX_TILESETS); /* serious problem if run out of slots */

  p->tilesets[s].type = ty;
  p->tilesets[s].tile = t;
  p->tilesets[s].annexed = 0;
  return 1;
}


int player_draws_tile(PlayerP p, Tile t) {
  p->err = NULL;
  if ( p->num_concealed == 0 ) {
    if ( t == HiddenTile ) psetflag(p,Hidden);
    else pclearflag(p,Hidden);
  }
  if ( pflag(p,Hidden) ) p->num_concealed++;
  else {
    if ( t == HiddenTile ) {
      p->err = "player_draws_tile: HiddenTile, but we know the player!";
      return 0;
    }
    if ( add_tile(p,t) == 0 ) {
      p->err = "player_draws_tile: add_tile failed";
      return 0;
    }
  }
  return 1;
}

int player_draws_loose_tile(PlayerP p, Tile t) {
  p->err = NULL;
  if ( pflag(p,Hidden) ) p->num_concealed++;
  else {
    if ( t == HiddenTile ) {
      p->err = "player_draws_loose_tile: HiddenTile, but we know the player!";
      return 0;
    }
    if ( add_tile(p,t) == 0 ) {
      p->err = "player_draws_loose_tile: add_tile failed";
      return 0;
    }
  }
  return 1;
}


/* NOTE: it is a required feature of the following functions that
   if they fail, they leave the player structure unchanged. */

/* implements the two following functions */
static int int_pds(PlayerP p, Tile spec, int testonly) {
  p->err = NULL;

  if ( ! is_special(spec) ) {
    p->err = "player_declares_special: the special tile isn't special\n";
    return 0;
  }
  /* paranoid, but why not? */
  if ( p->num_specials == 8 ) {
    p->err = "player_declares_special: player already has all specials!";
    return 0;
  }
  if ( pflag(p,Hidden) ) {
    if ( p->num_concealed < 1 ) {
      p->err = "player_can_declare_special: player has no tiles\n";
      return 0;
    }
    if ( testonly ) return 1;
    p->specials[p->num_specials++] = spec;
    p->num_concealed--;
    return 1;
  }

  if ( player_count_tile(p,spec) < 1 ) return 0;
  if ( testonly ) return 1;
  /* now do it */
  p->specials[p->num_specials++] = spec;
  remove_tile(p,spec);
  return 1;
}

int player_declares_special(PlayerP p, Tile spec) {
  return int_pds(p,spec,0);
}

int player_can_declare_special(PlayerP p, Tile spec) {
  return int_pds(p,spec,1);
}

/* implements the several pung functions.
   Determines whether p can use d to form a pung; and if not testonly,
   does it.
   If the last arg is 0, then d is a discard; otherwise, if the last arg is 
   1, d is also to be found in hand, and we are forming a closed pung
   (this is used in scoring).
*/
   
static int int_pp(PlayerP p, Tile d, int testonly, int tileinhand) {
  p->err = NULL;

  if ( pflag(p,Hidden) ) {
    /* just assume it can be done */
    if ( p->num_concealed < (tileinhand ? 3 : 2) ) {
      p->err = "player_pungs: can't pung with too few concealed tiles\n";
      return 0;
    }
    if ( testonly ) return 1;
    p->num_concealed -= (tileinhand ? 3: 2);
    add_tileset(p,(tileinhand ? ClosedPung : Pung),d);
    if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
    return 1;
  }

  /* otherwise, we know the player, and need to check legality */
  if ( player_count_tile(p,d) < (tileinhand ? 3 : 2) ) {
    p->err = "player_pungs: not enough matching tiles\n";
    return 0;
  }
  /* OK, it's legal */
  if ( testonly ) return 1;
  /* add the tileset */
  add_tileset(p, (tileinhand ? ClosedPung : Pung), d);
  /* remove the tiles from the hand */
  remove_tile(p,d);
  remove_tile(p,d);
  if ( tileinhand ) remove_tile(p,d);
  if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
  return 1;
}

int player_pungs(PlayerP p, Tile d) {
  return int_pp(p,d,0,0);
}

int player_can_pung(PlayerP p, Tile d) {
  return int_pp(p,d,1,0);
}
  
int player_forms_closed_pung(PlayerP p, Tile d) {
  return int_pp(p, d, 0, 1);
}

int player_can_form_closed_pung(PlayerP p, Tile d) {
  return int_pp(p, d, 1, 1);
}

/* likewise implements the several pair functions.
   Determines whether p can use d to form a pair; and if not testonly,
   does it.
   If the last arg is 0, then d is a discard; otherwise, if the last arg is 
   1, d is also to be found in hand, and we are forming a closed pair
   (this is used in scoring).
*/
   
static int int_ppr(PlayerP p, Tile d, int testonly, int tileinhand) {

  p->err = NULL;
  
  if ( pflag(p,Hidden) ) {
    /* just assume it can be done */
    if ( p->num_concealed < (tileinhand ? 2 : 1) ) {
      p->err = "player_pairs: can't pair with too few concealed tiles\n";
      return 0;
    }
    p->num_concealed -= (tileinhand ? 2: 1);
    add_tileset(p,(tileinhand ? ClosedPair : Pair),d);
    if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
    return 1;
  }

  /* otherwise, we know the player, and need to check legality */
  if ( player_count_tile(p,d) < (tileinhand ? 2 : 1) ) {
    p->err = "player_pairs: not enough matching tiles\n";
    return 0;
  }
  /* OK, it's legal */
  if ( testonly ) return 1;
  /* add the tileset */
  add_tileset(p, (tileinhand ? ClosedPair : Pair), d);
  /* remove the tiles from the hand */
  remove_tile(p,d);
  if ( tileinhand ) remove_tile(p,d);
  if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
  return 1;
}

int player_pairs(PlayerP p, Tile d) {
  return int_ppr(p,d,0,0);
}

int player_can_pair(PlayerP p, Tile d) {
  return int_ppr(p,d,1,0);
}

int player_forms_closed_pair(PlayerP p, Tile t) {
  return int_ppr(p,t,0,1);
}

int player_can_form_closed_pair(PlayerP p, Tile t) {
  return int_ppr(p,t,1,1);
}
  
/* implements several following functions;
   testonly and tileinhand as before */

static int int_pk(PlayerP p, Tile d, int testonly, int tileinhand) {

  p->err = NULL;
  if ( pflag(p,Hidden) ) {
    /* just assume it can be done */
    if ( p->num_concealed < (tileinhand ? 4 : 3) ) {
      p->err = "player_kongs: can't kong with too few concealed tiles\n";
      return 0;
    }
    p->num_concealed -= (tileinhand ? 4 : 3); /* lose three/four, gain replacement */
    add_tileset(p,(tileinhand ? ClosedKong : Kong), d);
    return 1;
  }

  /* otherwise, we know the player, and need to check legality */
  if ( player_count_tile(p,d) < (tileinhand ? 4 : 3) ) {
    p->err = "player_kongs: not enough matching tiles\n";
    return 0;
  }
  /* OK, it's legal */
  if ( testonly ) return 1;
  /* add the tileset */
  add_tileset(p,(tileinhand ? ClosedKong : Kong),d);
  /* remove the tiles from the hand */
  remove_tile(p,d);
  remove_tile(p,d);
  remove_tile(p,d);
  if ( tileinhand ) remove_tile(p,d);
  return 1;
}

int player_kongs(PlayerP p, Tile d) {
  return int_pk(p,d,0,0);
}

int player_can_kong(PlayerP p, Tile d) {
  return int_pk(p, d, 1, 0);
}

int player_declares_closed_kong(PlayerP p, Tile d) {
  return int_pk(p, d, 0, 1);
}

int player_can_declare_closed_kong(PlayerP p, Tile d) {
  return int_pk(p, d, 1, 1);
}

/* Implements two following functions. */
static int int_pap(PlayerP p, Tile t, int testonly) {
  int i;
  
  p->err = NULL;
  /* First check that the player has an exposed pung of the tile */
  for ( i=0 ; i < MAX_TILESETS ; i++ ) {
    if ( p->tilesets[i].type == Pung && p->tilesets[i].tile == t )
      break;
  }
  if ( i == MAX_TILESETS ) {
    p->err = "player_adds_to_pung called with no pung";
    return 0;
  }
  /* now check (if poss) that the tile is in hand */
  if ( pflag(p,Hidden) ) {
    /* can't see the tiles, so trust it */
    if ( p->num_concealed < 1 ) {
      /* err, this is actually impossible */
      p->err = "player_adds_to_pung: no concealed tiles";
      return 0;
    }
    /* lose one concealed tile */
    p->num_concealed--;
    p->tilesets[i].type = Kong;
    p->tilesets[i].annexed = 1;
    return 1;
  }

  /* otherwise, we know the player and need to check legality */
  if ( player_count_tile(p,t) < 1 ) {
    p->err = "player_adds_to_pung: tile not found in hand";
    return 0;
  }
    /* OK, it's legal */
  if ( testonly ) return 1;

  /* modify the tileset */
  p->tilesets[i].type = Kong;
  p->tilesets[i].annexed = 1;
  /* remove the tile from the hand */
  remove_tile(p,t);
  return 1;
}

int player_adds_to_pung(PlayerP p, Tile t) {
  return int_pap(p,t,0);
}

int player_can_add_to_pung(PlayerP p,Tile t) {
  return int_pap(p,t,1);
}

/* player_kong_robbed: the player has formed a kong of t, and it is robbed */
int player_kong_is_robbed(PlayerP p, Tile t) {
  int i;

  p->err = NULL;
  /* find the kong in question */
  for ( i=0 ; i < MAX_TILESETS ; i++ ) {
    if ( p->tilesets[i].tile == t 
	 && ( p->tilesets[i].type == Kong
	      || p->tilesets[i].type == ClosedKong ) )
      break;
  }
  if ( i == MAX_TILESETS ) {
    p->err = "player_kong_is_robbed called with no kong";
    return 0;
  }
  /* and downgrade it */
  if ( p->tilesets[i].type == Kong ) p->tilesets[i].type = Pung ;
  else p->tilesets[i].type = ClosedPung;
  p->tilesets[i].annexed = 0;
  return 1;
}

/* the chow handling function */
static int int_pc(PlayerP p, Tile d, ChowPosition r, int testonly, int tileinhand) {
  Tile low, mid, high;

  p->err = NULL;
  if ( ! is_suit(d) ) {
    p->err = "Can't chow a non-suit tile\n";
    return 0;
  }

  if ( r == AnyPos ) {
    if ( !testonly ) {
      p->err = "Can't make a chow with AnyPos";
      return 0;
    }
    return
      int_pc(p,d,Lower,testonly,tileinhand)
      || int_pc(p,d,Middle,testonly,tileinhand)
      || int_pc(p,d,Upper,testonly,tileinhand);
  }

  if ( r == Lower ) {
    if ( value_of(d) > 7 ) {
      p->err = "Impossible chow\n";
      return 0;
    }
    low = d;
    mid = d+1;
    high = d+2;
  } else if ( r == Middle ) {
    if ( value_of(d) == 1 || value_of(d) == 9 ) {
      p->err = "Impossible chow\n";
      return 0;
    }
    mid = d;
    low = d-1;
    high = d+1;
  } else /* r == Upper */ {
    if ( value_of(d) < 3 ) {
      p->err = "Impossible chow\n";
      return 0;
    }
    high = d;
    mid = d-1;
    low = d-2;
  }

  if ( pflag(p,Hidden) ) {
    if ( testonly ) return 1;
    p->num_concealed -= (tileinhand ? 3 : 2);
    add_tileset(p,(tileinhand ? ClosedChow : Chow),low);
    if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
    return 1;
  }

  /* Check we have the tiles */
  if ( tileinhand || r != Lower ) {
    if ( player_count_tile(p,low) < 1 ) {
      p->err = "Don't have the other tiles for the chow";
      return 0;
    }
  }
  if ( tileinhand || r != Middle ) {
    if ( player_count_tile(p,mid) < 1 ) {
      p->err = "Don't have the other tiles for the chow";
      return 0;
    }
  }
  if ( tileinhand || r != Upper ) {
    if ( player_count_tile(p,high) < 1 ) {
      p->err = "Don't have the other tiles for the chow";
      return 0;
    }
  }
  /* OK, we're ready to go */
  if ( testonly ) return 1;
  /* this might fail if the state is inconsistent, in which case
     we should leave p unchanged. Hence it comes first.
  */
  if ( !add_tileset(p,(tileinhand ? ClosedChow : Chow),low) ) {
    p->err = "int_pc: add_tileset failed!";
    return 0;
  }
  /* remove the tiles */ 
  if ( tileinhand || r != Lower ) {
    remove_tile(p,low);
  }
  if ( tileinhand || r != Middle ) {
    remove_tile(p,mid);
  }
  if ( tileinhand || r != Upper ) {
    remove_tile(p,high);
  }
  if ( p->num_concealed == 0 ) psetflag(p,HandDeclared);
  return 1;
}

int player_chows(PlayerP p, Tile d, ChowPosition r) {
  return int_pc(p,d,r,0,0);
}

int player_can_chow(PlayerP p, Tile d, ChowPosition r) {
  return int_pc(p,d,r,1,0);
}

int player_forms_closed_chow(PlayerP p, Tile d, ChowPosition r) {
  return int_pc(p,d,r,0,1);
}

int player_can_form_closed_chow(PlayerP p, Tile d, ChowPosition r) {
  return int_pc(p,d,r,1,1);
}

/* discarding a tile */
static int int_pd(PlayerP p, Tile t, int testonly) {
  p->err = NULL;
  if ( pflag(p,Hidden) ) {
    if ( p->num_concealed < 1 ) {
      p->err = "No tiles to discard!\n";
      return 0;
    }
    if ( testonly ) return 1;
    p->num_concealed -= 1;
    pclearflag(p,NoDiscard);
    return 1;
  }
  if ( player_count_tile(p,t) < 1 ) {
    p->err = "Tile not found in hand\n";
    return 0;
  }
  if ( testonly ) return 1;
  remove_tile(p,t);
  pclearflag(p,NoDiscard);
  return 1;
}

int player_discards(PlayerP p, Tile t) {
  return int_pd(p,t,0);
}

int player_can_discard(PlayerP p, Tile t) {
  return int_pd(p,t,1);
}

/* player_can_mah_jong: determine whether this hand (perhaps with
   an added discard tile) is a mah-jong hand.
   This function is completely brain-dead about finding a possible
   mah jong; given the small search space, there seems no point in
   being at all clever.
*/
int player_can_mah_jong(PlayerP p,Tile d,MJSpecialHandFlags flags) {
  Player pcopy; /* we will manipulate this copy of the player */
  PlayerP pcp = &pcopy;
  int answer = 0;
  Tile t;

  p->err = NULL;
  /* Technique is depth-first search of possible hands: just
     try applying making all possible tilesets until we succeed */

  /* The base case of the recursion */
  if ( d == HiddenTile && p->num_concealed <= 1 ) {
    answer = player_has_mah_jong(p,flags);
    goto done;
  }

  /* otherwise, try making tilesets.
     First deal with the discard tile, if it exists.
     For each possible way of using the discard,
     make a copy of the player, use the discard, and see
     if the result is mah-jongable.
  */
  if ( d != HiddenTile ) {
    copy_player(pcp,p);
    answer = (player_pungs(pcp,d)
	      && player_can_mah_jong(pcp,HiddenTile,flags));
    if (answer) goto done;

    copy_player(pcp,p);
    answer = (player_chows(pcp,d,Lower)
	      && player_can_mah_jong(pcp,HiddenTile,flags));
    if (answer) goto done;

    copy_player(pcp,p);
    answer = (player_chows(pcp,d,Middle)
	      && player_can_mah_jong(pcp,HiddenTile,flags));
    if (answer) goto done;

    copy_player(pcp,p);
    answer = (player_chows(pcp,d,Upper)
	      && player_can_mah_jong(pcp,HiddenTile,flags));
    if (answer) goto done;

    copy_player(pcp,p);
    answer = (player_pairs(pcp,d)
	      && player_can_mah_jong(pcp,HiddenTile,flags));
    goto done;
  } else {
    /* otherwise, for each concealed tile, try making something of it */
    int i;

    for ( i = 0; i < p->num_concealed; i++ ) {
      t = p->concealed[i];

      copy_player(pcp,p);
      answer = (player_forms_closed_pung(pcp,t)
		&& player_can_mah_jong(pcp,HiddenTile,flags));
      if (answer) goto done;

      copy_player(pcp,p);
      answer = (player_forms_closed_chow(pcp,t,Lower)
		&& player_can_mah_jong(pcp,HiddenTile,flags));
      if (answer) goto done;
      
      copy_player(pcp,p);
      answer = (player_forms_closed_pair(pcp,t)
		&& player_can_mah_jong(pcp,HiddenTile,flags));
      if (answer) goto done;
    }
  }
 done:
  /* if all else fails, try 13 unique wonders */
  if ( (! answer)
       && ((p->num_concealed == 13 && d != HiddenTile)
	   || (p->num_concealed == 14 && d == HiddenTile)))
    answer = player_can_thirteen_wonders(p,d);
  return answer;
}

/* does the player have 13 unique wonders? */
int player_can_thirteen_wonders(PlayerP p, Tile d) {
  Player pcopy; /* we will manipulate this copy of the player */
  PlayerP pcp = &pcopy;
  int answer = 0;
  int i, havetwo, n;

  if ( d != HiddenTile ) {
    copy_player(pcp,p);
    answer = (player_draws_tile(pcp,d)
	      && player_can_thirteen_wonders(pcp,HiddenTile));
  } else {
    answer = 1;
    havetwo = 0;
    for ( i=0; i < 13; i++ ) {
      n = player_count_tile(p,thirteen_wonders[i]);
      if ( n > 2 ) { answer = 0; break; }
      else if ( n == 2 ) {
	if ( havetwo ) { answer = 0; break; }
	else { havetwo = 1; }
      } else if ( n == 0 ) { answer = 0; break; }
    }
    if ( ! havetwo ) answer = 0;
  }
  return answer;
}


/* This internal function defines the mah jong hands.
   (Currently it does not include thirteen wonders).
   Last arg allows seven pairs etc.
*/
static int player_has_mah_jong(PlayerP p,MJSpecialHandFlags flags) {
  int i,s,n;
  int answer;

  p->err = NULL;
  /* all tiles must be in sets */
  if ( p->num_concealed > 0 ) return 0;

  answer = 0;
  /* there must be exactly  five non-empty sets, exactly
     one of which must be a pair */
  for (i=0, n=0, s=0; i<MAX_TILESETS; i++) {
    if ( p->tilesets[i].type != Empty ) s++;
    if ( num_tiles_in_set(&(p->tilesets[i])) == 2) n++;
  }
  if ( s == 5 && n == 1 ) answer = 1;
  /* seven pairs may be allowed */
  if ( (flags & MJSevenPairs) && s == 7 && n == 7 ) answer = 1;

  /* that's it */
  return answer;
}
  
/* sort the player's concealed tiles.
*/
void player_sort_tiles(PlayerP p) {
  int nconc; int i,j;
  Tile *tp = p->concealed;
  Tile t;

  p->err = NULL;
  if ( pflag(p,Hidden) ) return; /* pretty dumb ... */
  nconc = p->num_concealed;

  /* bubble sort */
  for ( i = 0 ; i < nconc-1 ; i++ ) {
    if ( tp[i+1] < tp[i] ) {
      /* sink it to the bottom */
      for ( j = i+1 ; tp[j] < tp[j-1] && j > 0 ; j-- ) {
	t = tp[j-1]; tp[j-1] = tp[j]; tp[j] = t;
      }
    }
  }
}

/* move a tile */
int player_reorder_tile(PlayerP p, int old, int new) {
  int i;
  if ( old < 0 || new < 0 
    || old >= p->num_concealed || new >= p->num_concealed ) {
    warn("player_reorder_tile: arguments out of range %d, %d",old,new);
    return 0;
  }
  if ( old == new ) return 1;
  Tile t = p->concealed[old];
  if ( new > old ) {
    for (i = old+1; i <= new; i++)
      p->concealed[i-1] = p->concealed[i];
    p->concealed[new] = t;
  } else {
    for (i = old-1; i >= new; i--)
      p->concealed[i+1] = p->concealed[i];
    p->concealed[new] = t;
  }
  return 1;
}

/* This function generates a string repn of a players tiles. */
void player_print_tiles(char *buf, PlayerP p, int hide) {
  int i,j;
  Tile *tp; Tile t;
  Tile ctiles[MAX_CONCEALED]; int nconc;
  
  p->err = NULL;
  if ( pflag(p,Hidden) ) hide = 1; /* can't see the tiles anyway */

  /* copy the concealed files and sort them.
     This assumes knowledge that Tiles are shorts.
  */
  nconc = p->num_concealed;
  if ( ! hide ) {
    memcpy(ctiles,p->concealed,nconc*sizeof(Tile));
    tp = ctiles;
    /* bubble sort */
    for ( i = 0 ; i < nconc-1 ; i++ ) {
      if ( tp[i+1] < tp[i] ) {
	/* sink it to the bottom */
	for ( j = i+1 ; tp[j] < tp[j-1] && j > 0 ; j-- ) {
	  t = tp[j-1]; tp[j-1] = tp[j]; tp[j] = t;
	}
      }
    }
  }

  buf[0] = '\000' ;
  for (i=0; i < nconc; i++) {
    if ( i > 0 ) strcat(buf," ");
    strcat(buf, hide ? "--" : tile_code(ctiles[i]));
  }
  strcat(buf," *");
  
  for (i = 0; i<MAX_TILESETS; i++) {
    if ( p->tilesets[i].type == Empty ) continue;
    strcat(buf," ");
    strcat(buf,tileset_string(&p->tilesets[i]));
  }

  strcat(buf," * ");
  for (i=0; i < p->num_specials; i++) {
    if ( i > 0 ) strcat(buf," ");
    strcat(buf,tile_code(p->specials[i]));
  }
}

int set_player_tiles(PlayerP p,char *desc) {
  int i = 0;
  char tc[3]; /* for tile codes */
  int ts;
  int closed, annexed;
  Tile tile;
  TileSetType ttype;
  
  p->err = NULL;
  /* clear the current information */
  p->num_concealed = 0;
  for (i=0; i < MAX_TILESETS ; i++) p->tilesets->type = Empty;
  p->num_specials = 0;
  pclearflag(p,Hidden);
  
  while ( *desc && isspace(*desc) ) desc++;
  /* read the tiles in hand */
  while ( *desc && *desc != '*' ) {
    tc[0] = *(desc++);
    tc[1] = *(desc++);
    tc[2] = '\000';
    tile = tile_decode(tc);
    if ( tile == ErrorTile ) {
      warn("format error in set_player_tile");
      return 0;
    }
    if ( tile == HiddenTile ) {
      p->num_concealed++;
      psetflag(p,Hidden);
    } else {
      p->concealed[p->num_concealed++] = tile;
    }
    while ( *desc && isspace(*desc) ) desc++;
  }
  if ( ! *desc ) {
    warn("format error in set_player_tiles");
    return 0;
  }
  desc++; /* skip the * */
  while ( *desc && isspace(*desc) ) desc++;

  /* parse the tilesets */
  ts = 0;
  while ( *desc && *desc != '*' ) {
    annexed = 0;
    closed = 0;
    tc[0] = *(desc++);
    tc[1] = *(desc++);
    tc[2] = 0;
    tile = tile_decode(tc);
    if ( tile == ErrorTile ) {
      warn("format error in set_player_tiles");
      return 0;
    }
    if ( *(desc++) == '+' ) closed = 1;
    /* we're lazy now: if the next tile is different,
       we know it's a chow; otherwise we just skip to 
       the next white space, counting letters */
    tc[0] = *(desc++);
    tc[1] = *(desc++);
    if ( tile_decode(tc) != tile ) {
      ttype = closed ? ClosedChow : Chow;
      desc += 3; /* skip last tile */
    } else if ( isspace(*desc) ) {
      /* two tiles; it's a pair */
      ttype = closed ? ClosedPair : Pair;
    } else {
      /* skip next tile */
      desc += 3;
      if ( isspace(*desc) ) {
	/* it's a pung */
	ttype = closed ? ClosedPung : Pung;
      } else {
	/* it's a kong */
	ttype = closed ? ClosedKong : Kong;
	/* millington rules ? */
	if ( ! closed )
	  annexed = (*desc == '-');
	desc += 3;
      }
    }
    p->tilesets[ts].type = ttype;
    p->tilesets[ts].tile = tile;
    p->tilesets[ts].annexed = annexed;
    ts++;
    while ( *desc && isspace(*desc) ) desc++;
  } /* end of matching tilesets loop */

  if ( ! *desc ) {
    warn("format error in set_player_tiles");
    return 0;
  }
  desc++; /* skip the * */

  while ( *desc && isspace(*desc) ) desc++;
  /* parse the specials */
  while ( *desc && *desc != '*' ) {
    tc[0] = *(desc++);
    tc[1] = *(desc++);
    tc[2] = '\000';
    tile = tile_decode(tc);
    if ( tile == ErrorTile ) {
      warn("format error in set_player_tile");
      return 0;
    }
    p->specials[p->num_specials++] = tile;
    while ( *desc && isspace(*desc) ) desc++;
  }
  /* phew */
  return 1;
}

int player_shows_tiles(PlayerP p, char *desc) {
  char tc[3];
  Tile tile;
  
  p->err = NULL;
  /* If the player is not hidden, then we needn't
     do anything to the tiles */
  if ( pflag(p,Hidden) ) {
    /* clear the current information */
    p->num_concealed = 0;
    pclearflag(p,Hidden);
    
    while ( *desc && isspace(*desc) ) desc++;
    /* read the tiles in hand */
    while ( *desc ) {
      tc[0] = *(desc++);
      tc[1] = *(desc++);
      tc[2] = '\000';
      tile = tile_decode(tc);
      if ( tile == ErrorTile ) {
	warn("format error in player_shows_tiles");
	return 0;
      }
      p->concealed[p->num_concealed++] = tile;
      while ( *desc && isspace(*desc) ) desc++;
    }
  }
  psetflag(p,HandDeclared);
  return 1;
}

/* player_swap_tile: swaps the oldtile for the newtile in the
   concealed tiles. Only used in testing, of course */
int player_swap_tile(PlayerP p, Tile oldtile, Tile newtile) {
  p->err = NULL;
  if ( ! remove_tile(p,oldtile) ) {
    p->err = "player doesn't have old tile";
    return 0;
  }
  add_tile(p,newtile);
  return 1;
}

/* return string representing a tileset */
char *tileset_string(TileSetP tp) {
  char *sep;
  static char buf[20];
  short v; TileSuit s;
  int j;

  buf[0] = '\000';

  sep = "-";
  switch ( tp->type ) {
  case ClosedPung:
  case ClosedKong:
  case ClosedPair:
    sep = "+";
  case Pung:
  case Kong:
  case Pair:
    for ( j = 0; j < num_tiles_in_set(tp); j++ ) {
      if ( tp->type == Kong 
	&& !tp->annexed 
	&& j == num_tiles_in_set(tp) - 1 )
	sep = "+";
      if ( j > 0 ) strcat(buf,sep);
      strcat(buf,tile_code(tp->tile));
    }
    break;
  case ClosedChow:
    sep = "+";
  case Chow:
    v = value_of(tp->tile);
    s = suit_of(tp->tile);
    strcat(buf,tile_code(tp->tile));
    strcat(buf,sep);
    strcat(buf,tile_code(make_tile(s,v+1)));
    strcat(buf,sep);
    strcat(buf,tile_code(make_tile(s,v+2)));
    break;
  case Empty:
    /* errrr */
    return "";
  }
  return buf;
}

/* print into an internal buffer a representation of the player */
char *player_print_state(PlayerP p) {
  static char buf[256],tiles[80],flags[80],dflags[80];
  int i,j;

  flags[0] = 0;
  for ( i = 0; i < 32; i++ ) {
    if ( pflag(p,i) ) {
      strcat(flags,nullprotect(player_print_PlayerFlags(i)));
      strcat(flags," ");
    }
  }
  dflags[0] = 0;
  for ( j = 0; j < NUM_SEATS; j++ ) {
    for ( i = 0; i < 32; i++ ) {
      if ( pdflag(p,j,1 << i) ) {
	strcat(dflags,nullprotect(player_print_DangerSignals(1 << i)));
	strcat(dflags," ");
      }
    }
    strcat(dflags,"; ");
  }
  player_print_tiles(tiles,p,0);

  sprintf(buf,"Player:\n"
"id: %d\n"
"name: %.100s\n"
"wind: %s\n"
"tiles: %s\n"
"flags: %s\n"
"dflags: %s\n"
"cumulative_score: %d\n"
"hand_score: %d\n"
"err: %s\n"
"userdata: %p\n",
    p->id,
    nullprotect(p->name),
    nullprotect(tiles_print_TileWind(p->wind)),
    tiles,
    flags,
    dflags,
    p->cumulative_score,
    p->hand_score,
    nullprotect(p->err),
    p->userdata);
  return buf;
}

/* enum functions */
#include "player-enums.c"
