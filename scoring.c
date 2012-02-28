/* $Header: /home/jcb/MahJong/newmj/RCS/scoring.c,v 12.1 2012/02/01 14:10:22 jcb Rel $
 * scoring.c 
 * This file contains the routines to handle the scoring
 * of hands.
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/scoring.c,v 12.1 2012/02/01 14:10:22 jcb Rel $";

#include "tiles.h"
#include "player.h"
#include "controller.h"
#include "scoring.h"
#include "sysdep.h"

int no_special_scores = 0; /* disable scoring of flowers and seasons */

/* score_of_tileset: takes a TileSetP, and returns a score. *
   It needs to know the wind of this player, and the prevailing wind.
   It puts the left text, score and right text in the following ptrs; they
   are valid until the next call */
static char *tsltext,*tsrtext; static int tsscore;

static int score_of_tileset(Game *g, TileSetP tp, TileWind own, TileWind prevailing) {
  static char buf[100];
  static char tmp[50] ;
  int n,d;

  buf[0] = 0;
  tmp[0] = 0;

  if ( tp->type == Empty ) {
    /* I don't think this should happen */
    warn("score_of_tileset: called on Empty tileset");
    tsltext = NULL; tsrtext = NULL;
    return 0;
  }

  d = is_major(tp->tile) ? 2 : 1;

  n = 0;
  switch ( tp->type ) {
  case Chow:
  case ClosedChow:
    n = 0; break;
  case Pung:
    n = 2*d;
    strcat(tmp,"open pung of ");
    strcat(tmp, (d==2) ? "majors" : "minors");
    break;
  case ClosedPung:
    n = 4*d;
    strcat(tmp,"closed pung of ");
    strcat(tmp, (d==2) ? "majors" : "minors");
    break;
  case Kong:
    n = 8*d;
    /* if we are using Millington's kong rules, we should distinguish
       claimed and annexed kongs here */
    if ( game_get_option_value(g,GOKongHas3Types,NULL).optbool ) {
      if ( tp->annexed )
	strcat(tmp,"annexed kong of ");
      else
	strcat(tmp,"claimed kong of ");
    } else {
      strcat(tmp,"open kong of ");
    }
    strcat(tmp, (d==2) ? "majors" : "minors");
    break;
  case ClosedKong:
    n = 16*d;
    strcat(tmp,"closed kong of ");
    strcat(tmp, (d==2) ? "majors" : "minors");
    break;
  case Pair:
  case ClosedPair:
    n = 0;
    if ( is_dragon(tp->tile) ) {
      n = 2;
      strcat(tmp,"pair of dragons");
    } else if ( is_wind(tp->tile)
		&& (TileWind)value_of(tp->tile) == own ) {
      if ( own == prevailing ) {
	n = 4;
	strcat(tmp,"pair of own and prevailing wind");
      } else {
	n = 2;
	strcat(tmp,"pair of own wind");
      }
    } else if ( is_wind(tp->tile)
		&& (TileWind)value_of(tp->tile) == prevailing ) {
      n = 2;
      strcat(tmp,"pair of prevailing wind");
    }
    break;
  case Empty: break; /* to suppress warning */
  }
  tsltext = tileset_string(tp);
  tsscore = n;
  tsrtext = tmp;
  return n;
}

/* score_of_hand: score the given seat in the given game.
   In addition, set the danger flags to indicate if the player
   achieved any of the dangerous hands. */
Score score_of_hand(Game *g, seats s) {
  Score tot;
  /* there are three buffers in which scoring info is built up:
     a points buffer, a doubles buffer, and a limit buffer */
  static char buf[2048], dblsbuf[1024], limsbuf[1024];
  char tbuf[100];
  PlayerP p;
  int limit, nolimit;
  int winner;
  int i,doubles,centilims;
  MJSpecialHandFlags mjspecflags;
  Tile eyestile; int claimedpair;
  int numchows,numpairs,numpungs,numkongs,allclosed,noscore,allhonours,
    allmajors,allterminals,dragonsets,windsets,dragonpairs,windpairs,
    numclosedpks,allgreen,allbamboo,allcharacter,allcircle,
    allbamboohonour,allcharacterhonour,allcirclehonour;

  
  p = g->players[s];
  assert(g->state == MahJonging);

  limit = game_get_option_value(g,GOScoreLimit,NULL).optnat;
  nolimit = game_get_option_value(g,GONoLimit,NULL).optnat;

  mjspecflags = 0;
  if ( game_get_option_value(g,GOSevenPairs,NULL).optbool )
    mjspecflags |= MJSevenPairs;

  buf[0] = 0;
  tot.explanation = buf;
  tot.value = 0;
  dblsbuf[0] = 0;
  limsbuf[0] = 0;
  doubles = 0;
  centilims = 0;

  /* This macro takes three args: text for left,
     score, and text for right. It then looks at the score
     and adds appropriate bits to the relevant buffers and
     scoring info */
#define doscore(ltext,score,rtext) \
  { int pts, dbls, clims; \
    pts = (score) % 10000; \
    dbls = ((score) / 10000) % 100; \
    clims = ((score) / 1000000); \
    if ( (!clims || nolimit) && pts ) { \
      tot.value += pts; sprintf(tbuf,"%-12s %2d (%s)\n",ltext,pts,rtext); \
      strmcat(buf,tbuf,sizeof(buf)-strlen(buf)); \
    } \
    if ( (!clims || nolimit) && dbls ) { \
      doubles += dbls; sprintf(tbuf,"%-12s  %1d dbl (%s)\n",ltext,dbls,rtext); \
      strmcat(dblsbuf,tbuf,sizeof(dblsbuf)-strlen(dblsbuf)); \
    } \
    if ( clims && !(nolimit && (pts || dbls)) ) { \
      if ( clims > centilims ) centilims = clims; \
      if ( clims == 100 ) { \
        sprintf(tbuf,"%-10s %4d (%s)\n","Limit Hand",limit,rtext); \
      } else { \
        sprintf(tbuf,"%4g Limit %4d (%s)\n",clims*1.0/100,clims*limit/100,rtext); \
      } \
      strmcat(limsbuf,tbuf,sizeof(limsbuf)-strlen(limsbuf)); \
    } \
  }
  /* and a couple of defines for hard-coded scores */
#define DBLS *10000
#define LIMIT 100000000

  /* variables for detecting special hands */
  numchows = 0;
  numkongs = 0;
  numpairs = 0;
  numpungs = 0;
  allclosed = 1;
  noscore = 1;
  allterminals = 1;
  allhonours = 1;
  allmajors = 1;
  windsets = 0;
  dragonsets = 0;
  windpairs = 0;
  dragonpairs = 0;
  allgreen = 1;
  allbamboo = 1;
  allcharacter = 1;
  allcircle = 1;
  allbamboohonour = 1;
  allcharacterhonour = 1;
  allcirclehonour = 1;
  numclosedpks = 0; /* closed pungs/kongs */
  eyestile = HiddenTile; claimedpair = 0;

  /* Basic points */
  winner = 0;
  if ( s == g->player ) {
    doscore("basic",game_get_option_value(g,GOMahJongScore,NULL).optscore,
	    "going Mah Jong");
    winner = 1;
  }

  if ( winner ) { 
    presetdflags(p,s);
    psetdflags(p,s,DangerEnd);
  }

  /* Now, get the points for the tilesets */
  /* Also, stash information for doubles */
  for ( i = 0; i < MAX_TILESETS; i++ ) {
    TileSetP t = (TileSetP) &p->tilesets[i];

    if ( t->type == Empty ) continue;

    score_of_tileset(g,t,p->wind,g->round);

    if ( tsscore ) {
      noscore = 0;
      doscore(tsltext,tsscore,tsrtext);
    }
    
    if ( t->type == Chow )
      numchows++,allclosed=0,allterminals=allhonours=allmajors=0;
    if ( t->type == ClosedChow )
      numchows++,allterminals=allhonours=allmajors=0;
    if ( t->type == Pung ) allclosed=0,numpungs++;
    if ( t->type == ClosedPung ) numpungs++,numclosedpks++;
    if ( t->type == Kong ) {
      numkongs++ ;
      /* in Millington's rules, a claimed kong counts as a closed
	 kong for doubling purposes */
      if ( game_get_option_value(g,GOKongHas3Types,NULL).optbool
	&& ! t->annexed ) {
	numclosedpks++;
      } else {
	allclosed=0;
      }
    }
    if ( t->type == ClosedKong ) numkongs++,numclosedpks++;
    if ( t->type == Pair ) {
      numpairs++;
      allclosed=0;
      claimedpair = 1 ;
      eyestile = t->tile; /* this fails for seven pairs, but we don't care */
    }
    if ( t->type == ClosedPair ) {
      numpairs++;
      eyestile = t->tile; /* this fails for seven pairs, but we don't care */
    }

    if ( num_tiles_in_set(t) >= 3 ) {
      dragonsets += is_dragon(t->tile);
      windsets += is_wind(t->tile);
    } else {
      dragonpairs += is_dragon(t->tile);
      windpairs += is_wind(t->tile);
    }

    switch ( suit_of(t->tile) ) {
    case BambooSuit:
      allcharacter = allcharacterhonour = 0;
      allcircle = allcirclehonour = 0;
      switch ( t->type ) {
      case Chow:
      case ClosedChow:
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
    if ( !is_major(t->tile) ) allmajors = 0;
  }
  /* now go through the concealed tiles, in case we're playing some
     weird game where losers are allowed to score for pure and concealed
     hands. Seems a pity to duplicate all this code, but there we are */
  /* There's another reason to go through the concealed tiles: the
     existence of special hands, where the concealed tiles are
     a declared set */
  for ( i = 0; i < p->num_concealed; i++ ) {
    Tile ti = p->concealed[i];

    switch ( suit_of(ti) ) {
    case BambooSuit:
      allcharacter = allcharacterhonour = 0;
      allcircle = allcirclehonour = 0;
      if ( !is_green(ti) ) allgreen = 0;
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
      if ( !is_green(ti) ) allgreen = 0;
      break;
    case WindSuit:
      allbamboo = allcharacter = allcircle = 0;
      allgreen = 0;
      break;
    default:
      warn("Strange suit seen in hand");
    }

    if ( !is_honour(ti) ) allhonours = 0;
    if ( !is_terminal(ti) ) allterminals = 0;
    if ( !is_major(ti) ) allmajors = 0;
  }

  /* if we have seven pairs, give the base points */
  if ( numpairs == 7 ) {
    doscore("basic points",game_get_option_value(g,GOSevenPairsVal,NULL).optscore,"seven pairs");
    noscore = 0;
  }

  /* Now points for flowers and seasons (which don't count
     to stop a noscore hand) */
  if ( p->num_specials > 0 && ! no_special_scores ) {
    char s[16];
    sprintf(s,"%d special%s",p->num_specials,p->num_specials > 1 ? "s" : "");
    doscore(s,4*p->num_specials,"flowers and seasons");
  }

  /* odds and sods */
  if ( winner
       && g->whence == FromWall ) {
    doscore("extra",2,"winning from wall");
  }

  /* fishing the eyes: if we claimed a pair to go out, then
     the tile had better match the discard.
     Otherwise, the tiles of the (necessarily closed) pair
     had better match the tile drawn from the wall.
     We don't give this for seven pairs (or other multi pair hands).
  */
  if ( winner && eyestile == g->tile
       && numpairs == 1
       && ( claimedpair 
	    || g->whence == FromWall || g->whence == FromLoose ) ) {
    doscore("extra",is_major(eyestile) ? 4 : 2,"fishing the eyes");
  }

  /* this ridiculous amount of code deals with filling the only place.
     All this work for two points... */
  /* this is a sanity check; if this fails, the controller code
     has not put the right player in the caller slot */
  /* we don't give this for seven pairs either */
  if ( numpairs == 1 ) {
    if ( winner ) {
      if ( p->id != gextras(g)->caller->id ) {
	warn("Wrong player found in caller slot of game structure!");
      } else {
	PlayerP pp = gextras(g)->caller;
	int n = 0;
	int i; Tile t;
	/* see how many tiles can complete the hand in theory */
	t = HiddenTile;
	while ( (t = tile_iterate(t,0)) != HiddenTile ) {
	  /* if all four copies of the tile are exposed, it 
	     doesn't count as available */
	  /* However, if this is the tile that was claimed
	     for Mah-Jong, it wasn't all exposed when we called,
	     even if the exposed count is now 4. */
	  if ( g->exposed_tile_count[t] == 4  &&  g->tile != t ) continue;
	  i = player_can_mah_jong(pp,t,mjspecflags);
	  if ( i < 0 ) { warn("error in player_can_mah_jong while checking only place") ; }
	  else n += i;
	}
	if ( n <= 1 ) {
	  doscore("extra",2,"filling the only place");
	}
      }
    }
  }

#define dbl(n,ex) doscore("",n DBLS,ex)

  /* Now start looking for doubles from the tilesets */
  for ( i = 0; i < MAX_TILESETS; i++ ) {
    TileSetP t = (TileSetP) &p->tilesets[i];

    if ( t->type == Empty ) continue;

    if ( num_tiles_in_set(t) >= 3 ) {
      if ( is_dragon(t->tile) ) 
	doscore(tileset_string(t),1 DBLS,"pung/kong of dragons");
      if ( is_wind(t->tile) && (TileWind)value_of(t->tile) == p->wind )
	doscore(tileset_string(t),1 DBLS,"pung/kong of own wind");
      if ( is_wind(t->tile) && (TileWind)value_of(t->tile) == g->round )
	doscore(tileset_string(t),1 DBLS,"pung/kong of prevailing wind");
    }
  }

/* We may want to suppress these when comparing strategies,
   as they introduce a lot of randomness */
  /* Flower and season doubles */
  if ( ! no_special_scores ) {
    int ownflower = 0, ownseason = 0;
    int numflowers = 0;
    int numseasons = 0;
    int i;

    for ( i = 0; i < p->num_specials; i++ ) {
      if ( suit_of(p->specials[i]) == FlowerSuit ) {
	numflowers++;
	if ( (TileWind)value_of(p->specials[i]) == p->wind ) ownflower = 1;
      }
      if ( suit_of(p->specials[i]) == SeasonSuit ) {
	numseasons++;
	if ( (TileWind)value_of(p->specials[i]) == p->wind ) ownseason = 1;
      }
    }
    if ( ownflower ) 
      doscore("",game_get_option_value(g,GOFlowersOwnEach,NULL).optscore,
	      "own flower");
    if ( ownseason ) 
      doscore("",game_get_option_value(g,GOFlowersOwnEach,NULL).optscore,
	      "own season");
    if ( ownflower && ownseason ) 
      doscore("",game_get_option_value(g,GOFlowersOwnBoth,NULL).optscore,
	      "own flower and season");
    if ( numflowers == 4 ) 
      doscore("",game_get_option_value(g,GOFlowersBouquet,NULL).optscore,
	      "all four flowers");
    if ( numseasons == 4 ) 
      doscore("",game_get_option_value(g,GOFlowersBouquet,NULL).optscore,
	      "all four seasons");
  }


  /* doubles applying to all hands */
  /* Little/Big Three Dragons */
  if ( dragonsets == 3 ) {
    dbl(2,"Big Three Dragons");
    if ( winner ) psetdflags(p,s,DangerDragon);
  } else if ( dragonsets == 2 && dragonpairs ) {
    dbl(1,"Little Three Dragons");
    if ( winner ) psetdflags(p,s,DangerDragon);
  }
  /* Little/Big Four Joys */
  if ( windsets == 4 ) {
    dbl(2,"Big Four Joys");
    if ( winner ) psetdflags(p,s,DangerWind);
  } else if ( windsets == 3 && windpairs ) {
    dbl(1,"Little Four Joys");
    if ( winner ) psetdflags(p,s,DangerWind);
  }

  /* three concealed pungs */
  if ( numclosedpks >= 3 ) { dbl(1,"three concealed pungs"); }

  /* other doubles mostly applying only to the mahjong hand */
  if ( winner ) {
    if ( numchows == 0 && numpairs == 1 ) dbl(1,"no chows");
    if ( noscore ) dbl(1,"no score hand");
  }
  /* some losers want these doubles to apply to losing hands also */
  if ( winner || game_get_option_value(g,GOLosersPurity,NULL).optbool ) {
    if ( allhonours ) {
      /* This should score 2 doubles, and then get a double for
	 all majors. However, we must be careful not to give it
	 the double for one-suit-with-honours! */
      dbl(2,"all honours");
      psetdflags(p,s,DangerHonour);
    } else if ( allterminals ) {
      /* This should score 2 doubles, and then get a double for
	 all majors. */
      dbl(2,"all terminals");
      psetdflags(p,s,DangerTerminal);
    }
    if ( allmajors ) {
      dbl(1,"all majors");
    }
  }
  /* double for fully concealed hand */
  if ( winner ) {
    /* we may have claimed a discard for a special set */
    if ( g->whence == FromDiscard ) allclosed = 0;
    if ( allclosed ) 
      doscore("",game_get_option_value(
		g,GOConcealedFully,NULL).optscore,"concealed hand");
  }
  /* double for almost concealed hand. This is a pain */
  if ( winner && !allclosed ) {
    int semiclosed = 1;
    /* need to check the hand before mahjong. Hooray, another use
       for the caller slot ! */
    if ( p->id != gextras(g)->caller->id ) {
      warn("Wrong player found in caller slot of game structure!");
      semiclosed = 0;
    } else {
      PlayerP pp = gextras(g)->caller;
      int i;
      for ( i = 0; i < MAX_TILESETS; i++ ) {
	switch ( pp->tilesets[i].type ) {
	case Chow:
	case Pung:
	case Kong:
	case Pair:
	  semiclosed = 0;
	  break;
	default:
	  break; /* keep ANSI quiet */
	}
      }
    }
    if ( semiclosed )
      doscore("",game_get_option_value(
		g,GOConcealedAlmost,NULL).optscore,"almost concealed hand");
  } else if ( !winner && allclosed 
    && game_get_option_value(g,GOLosersPurity,NULL).optbool ) {
    doscore("",game_get_option_value(
	      g,GOConcealedAlmost,NULL).optscore,"almost concealed hand");
  }

  /* we may give these to losers, if somebody really wants */
  if ( winner || game_get_option_value(g,GOLosersPurity,NULL).optbool ) {
    /* NB. If no suit tiles, then we already have all honours,
       and don't give another double! */
    if ( allbamboo || allcharacter || allcircle ) {
      dbl(3,"one suit only");
      if ( allbamboo ) psetdflags(p,s,DangerBamboo);
      if ( allcharacter ) psetdflags(p,s,DangerCharacter);
      if ( allcircle ) psetdflags(p,s,DangerCircle);
    } else if ( !allhonours && (allbamboohonour || allcharacterhonour || allcirclehonour) ) {
      dbl(1,"one suit with honours");
    }
  }

  if ( winner ) {
    /* Should the following two be exclusive? */
    /* loose tile */
    if ( g->whence == FromLoose ) dbl(1,"winning with loose tile");
    /* last tile. This is >=, not ==, because if playing with 
       a replenishing dead wall, it's possible to move the end of the
       live wall back past tiles already taken, if the last tile is
       a redeemed for a loose tile with 13 in the dead wall. */
    if ( g->wall.live_used >= g->wall.live_end ) dbl(1,"winning with last tile");
    /* robbing a kong */
    if ( g->whence == FromRobbedKong ) dbl(1,"robbing a kong");
    /* original call */
    if ( pflag(p,OriginalCall) ) dbl(1,"completing Original Call");
  }

  /* having done all that work, we now look for limit hands.
     Note that all the danger signals except all green have already
     been set while we were looking for doubles */

#define limh(l) doscore("",LIMIT,l)

  /* we may as well record all the attained limits! */
  if ( winner ) {
    if ( g->player == east && pflag(p,NoDiscard) ) {
      /* heaven's blessing */
      limh("Heaven's Blessing");
    }
    if ( g->player != east 
		&& pflag(p,NoDiscard)
		&& g->whence == FromDiscard ) {
      seats s;
      int eb = 1;
      /* might be earth's blessing, but we need to check that
	 nobody else has discarded in between */
      for ( s = south; s < NUM_SEATS; s++ ) {
	if ( !pflag(g->players[s],NoDiscard) ) eb = 0;
      }
      if ( eb ) limh("Earth's Blessing");
    }
    if ( g->whence == FromLoose
		&& g->tile == make_tile(CircleSuit,5) ) {
      limh("Gathering Plum Blossom from the Roof");
    }
    if ( g->tile == make_tile(CircleSuit,1)
		&& g->wall.live_used == g->wall.live_end
		&& g->whence == FromWall ) {
      limh("Catching the Moon from the Bottom of the Sea");
    }
    if ( g->whence == FromRobbedKong
		&& g->tile == make_tile(BambooSuit,2) ) {
      limh("Scratching a Carrying Pole");
    }
    if ( game_flag(g,GFKongUponKong) ) {
      limh("Kong upon Kong");
    }
    if ( numkongs == 4 ) {
      limh("Fourfold Plenty");
    }
    if ( allclosed && numpungs+numkongs == 4 ) {
      limh("Buried Treasure");
    }
    if ( player_can_thirteen_wonders(p,HiddenTile) ) {
      limh("Thirteen Unique Wonders");
    }
    if ( dragonsets == 3 && numchows == 0 ) {
      limh("Three Great Scholars");
    }
    if ( windsets == 4 ) {
      limh("Four Blessing o'er the Door");
    }
    if ( allhonours ) {
      limh("All Honours");
    }
    if ( allterminals ) {
      limh("Heads and Tails");
    }
    if ( allclosed && (allbamboo || allcharacter || allcircle) ) {
      limh("Concealed Clear Suit");
    }
    if ( g->hands_as_east == 12 && g->player == east ) { /* sic */
      limh("East's 13th consecutive win");
    }
    if ( allgreen ) {
      limh("Imperial Jade");
      psetdflags(p,s,DangerGreen);
    }
    /* Nine Gates */
    if ( allbamboo || allcharacter || allcircle ) {
      /* worth checking in this case */
      PlayerP pp = gextras(g)->caller;
      int ng = 1;

      if ( pp->id != p->id ) {
	warn("Wrong player found in caller slot of game structure!");
	ng = 0;
      } else {
	int i;
	if ( is_honour(g->tile) ) {
	  ng = 0;
	} else {
	  int s = suit_of(g->tile);
	  player_sort_tiles(pp);
	  if ( pp->num_concealed < 13 ) ng = 0;
	  for ( i = 0 ; i < 3 ; i++ ) {
	    if ( pp->concealed[i] != make_tile(s,1) ) ng = 0;
	  }
	  for ( i = 2 ; i < 11 ; i++ ) {
	    if ( pp->concealed[i] != make_tile(s,i-1) ) ng = 0;
	  }
	  for ( i = 11 ; i < 13 ; i++ ) {
	    if ( pp->concealed[i] != make_tile(s,9) ) ng = 0;
	  }
	}
      }
      if ( ng ) limh("Nine Gates");
    }
    /* The Wriggling Snake, another silly hand */
    if ( allbamboo || allcharacter || allcircle ) {
      /* check for it */
      int i;
      int set1=0,set9=0,pair2=0,pair5=0,pair8=0,
	chow2=0,chow3=0,chow5=0,chow6=0;
      for ( i = 0 ; i < MAX_TILESETS ; i++ ) {
	switch( p->tilesets[i].type ) {
	case Empty:
	  break;
	case Pung:
	case ClosedPung:
	case Kong:
	case ClosedKong:
	  set1 |= (value_of(p->tilesets[i].tile) == 1);
	  set9 |= (value_of(p->tilesets[i].tile) == 9);
	  break;
	case Pair:
	case ClosedPair:
	  pair2 |= (value_of(p->tilesets[i].tile) == 2);
	  pair5 |= (value_of(p->tilesets[i].tile) == 5);
	  pair8 |= (value_of(p->tilesets[i].tile) == 8);
	  break;
	case Chow:
	case ClosedChow:
	  chow2 |= (value_of(p->tilesets[i].tile) == 2);
	  chow3 |= (value_of(p->tilesets[i].tile) == 3);
	  chow5 |= (value_of(p->tilesets[i].tile) == 5);
	  chow6 |= (value_of(p->tilesets[i].tile) == 6);
	  break;
	}
      }
      if ( set1 && set9 &&
	   ( (pair2 && chow3 && chow6)
	     || (pair5 && chow2 && chow6)
	     || (pair8 && chow2 && chow5) ) ) limh("Wriggling Snake");
    }	  
  }

  /* display the total points so far */
  sprintf(tbuf,"%-11s %3d\n","total pts",tot.value);
  strmcat(buf,tbuf,sizeof(buf)-strlen(buf));

  /* add the double descriptions */
  strmcat(buf,dblsbuf,sizeof(buf)-strlen(buf));

  /* calculate the doubles */
  sprintf(tbuf,"%-12s %2d\n","total dbls",doubles);
  strmcat(buf,tbuf,sizeof(buf)-strlen(buf));
  /* to avoid arithmetic overflow when people set crazy options,
     we'll put a hard limit of 1E8 in */
  while ( doubles-- > 0 ) {
    tot.value *= 2;
    if ( tot.value > 100000000 ) tot.value = 100000000 ;
  }
  if ( ! nolimit && tot.value > limit ) {
    sprintf(tbuf,"%-9s %5d (over limit)\n","score:",tot.value);
    strmcat(buf,tbuf,sizeof(buf)-strlen(buf));
    tot.value = limit;
  }

  /* and the limit descriptions */
  strmcat(buf,limsbuf,sizeof(buf)-strlen(buf));
  /* calculate the limit */

  if ( centilims ) {
    if ( nolimit ) {
      if ( centilims*limit/100 > tot.value ) tot.value = centilims*limit/100;
    } else {
      tot.value = centilims*limit/100;
      /* all the scoring info is irrelevant */
      strcpy(buf,limsbuf);
    }
  }
  sprintf(tbuf,"%-9s %5d","SCORE:",tot.value);
  strmcat(buf,tbuf,sizeof(buf)-strlen(buf));

  /* just return what we've got, to see if it's working */
  return tot;
}
