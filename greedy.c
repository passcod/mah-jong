/* $Header: /home/jcb/MahJong/newmj/RCS/greedy.c,v 12.1 2012/02/01 14:10:13 jcb Rel $
 * greedy.c
 * This is a computer player. Currently offensive only.
 * Options not documented.
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
 
static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/greedy.c,v 12.1 2012/02/01 14:10:13 jcb Rel $";

static int debugeval = 0;
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "client.h"
#include "sysdep.h"

#include "version.h"

static double magic[28+13] = 
  {
    12, 2, 1, 0.15, 1, 4, 4.97465626805558, 4, 1, 0.00638363133042069,
    0, 1, 0.025, 0.4, 0.34, 0.4, 0.033333333, 0.3, 
    2, 25, 2, 25, 90.2519293502827, 3, 0.99, 1, -0.0011160627477312, 0.25,

    6.0, 2.0, 25.0, 1.0, 12.0, 0.5, 3.0, 5.0, 0.5, 0.5, 0.5, 0.5,
    0.5 };

static double *magic2 = &magic[28];



/* is this tile a doubling tile (or scoring pair) */
#define is_doubler(t) (is_dragon(t) || (is_wind(t) && \
 (suit_of(t) == our_player->wind || suit_of(t) == the_game->round)))

Game *the_game;
int our_id = 0;
PlayerP our_player;
seats our_seat;

static char *password = NULL;

static FILE *debugf;

/* New style strategy */
typedef struct {
  double chowness; /* how much do we like/dislike chows? 
		      From -1.0 (no chows) to +1.0 (no pungs) */
  double hiddenness; /* how much do we want to keep things concealed?
			From 0.0 (don't care) to 1.0 (absolutely no claims) */
  double majorness; /* are we trying to get all majors? 
		       From 0.0 (don't care) to 1.0 (discard all minors) */
  double suitness; /* are we trying to collect one suit? From 0.0 to 1.0 */
  TileSuit suit;   /* if so, which? */
} strategy;

/* values to try out */
typedef enum { chowness, hiddenness, majorness, suitness } stratparamtype;
struct { int ncomps; double values[5]; }
 stratparams[suitness+1] =
 { { 3, { 0.0, -0.9, 0.5 } } , /* chowness */
   { 2, { 0.0, 0.8 } } , /* hiddenness */
   { 1, { 0.0 } } , /* majorness */
   { 2, { 0.0, 0.5 } } /* suitness */
 } ;

/* routine to parse a comma separated list of floats into
   the given strat param. Return num of comps, or -1 on error */
static int parsefloatlist(const char *fl, stratparamtype spt) {
  char *start, *end;
  int i;
  if ( ! fl ) { warn("null arg to parsefloatlist"); return -1; }
  start = (char *)fl;
  i = 0;
  while ( i < 5 ) {
    /* it is a feature that this will return zero on an empty
       string, since we shd have at least one value */
    stratparams[spt].values[i] = strtod(start,&end);
    i++;
    start = end;
    if ( ! *start ) break;
    if ( *start == ',' ) start++;
  }
  if ( *start ) warn("too many components, ignoring excess");
  stratparams[spt].ncomps = i;
  return i;
}



static strategy curstrat;
/* These are names for computed strategy values. See the awful mess
   below... */
enum {pungbase,pairbase,chowbase,seqbase,sglbase,
      partpung,partchow,exposedpungpenalty,exposedchowpenalty,
      suitfactor, mjbonus, kongbonus,weight};

/* the value of a new strategy must exceed the current
   by this amount for it to be chosen */
static double hysteresis = 4.0; 


/* Used to note availability of tiles */
static int tilesleft[MaxTile];
/* track discards of player to right */
static int rightdiscs[MaxTile]; /* disc_ser of last of each tile discarded */
static int strategy_chosen;
static int despatch_line(char *line);
static void do_something(void);
static void check_discard(PlayerP p,strategy *strat,int closed_kong);
static Tile decide_discard(PlayerP p, double *score, strategy *newstrat);
static void update_tilesleft(CMsgUnion *m);
static void maybe_switch_strategy(strategy *strat);
static double eval(Tile *tp, strategy *strat, double *stratpoints,int reclevel, int *ninc, int *npr, double *breadth);
static double evalhand(PlayerP p, strategy *strat);
static int chances_to_win(PlayerP p);
/* copy old tile array into new */
#define tcopy(new,old) memcpy((void *)new,(void *)old,(MAX_CONCEALED+1)*sizeof(Tile))
/* Convenience function */
#define send_packet(m) client_send_packet(the_game,(PMsgMsg *)m)

static void usage(char *pname,char *msg) {
  fprintf(stderr,"%s: %s\nUsage: %s [ --id N ] [ --name NAME ] [ --password PASSWORD ] [ --server ADDR ]\n\
  [ strategy options ]\n",
	  pname,msg,pname);
  exit(1);
}

int main(int argc, char *argv[]) {
  char buf[1000];
  char *l;
  int i;
  char *evalh = NULL ; /* just evaluate hand with default strategy
			  and all debugging, and exit */
  char *address = ":5000";
  char *name = NULL;

  debugf = stderr;

  /* options. I should use getopt ... */
  for (i=1;i<argc;i++) {
    if ( strcmp(argv[i],"--id") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --id");
      our_id = atoi(argv[i]);
    } else if ( strcmp(argv[i],"--server") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --server");
      address = argv[i];
    } else if ( strcmp(argv[i],"--address") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --address");
      address = argv[i];
    } else if ( strcmp(argv[i],"--name") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --name");
      name = argv[i];
    } else if ( strcmp(argv[i],"--password") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --password");
      password = argv[i];
    } else if ( strcmp(argv[i],"--debug") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --debug");
      debugeval = atoi(argv[i]);
    } else if ( strcmp(argv[i],"--eval") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --eval");
      evalh = argv[i];
      debugeval = 99;
    } else if ( strcmp(argv[i],"--hysteresis") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --hysteresis");
      hysteresis = atof(argv[i]);
    } else if ( strcmp(argv[i],"--chowness") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --chowness");
      parsefloatlist(argv[i],chowness);
    } else if ( strcmp(argv[i],"--hiddenness") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --hiddenness");
      parsefloatlist(argv[i],hiddenness);
    } else if ( strcmp(argv[i],"--majorness") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --majorness");
      parsefloatlist(argv[i],majorness);
    } else if ( strcmp(argv[i],"--suitness") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --suitness");
      parsefloatlist(argv[i],suitness);
    } else if ( strcmp(argv[i],"--magic") == 0 ) {
      FILE *f;
      unsigned int j;
      if ( ++i == argc ) usage(argv[0],"missing argument to --magic");
      f = fopen(argv[i],"r");
      if ( f == NULL ) {
	perror("can't open param file");
	exit(1);
      }
      for ( j = 0 ; j < sizeof(magic)/sizeof(double) ; j++ ) {
	if ( fscanf(f,"%lg",&magic[j]) != 1 ) {
	  fprintf(stderr,"%s","error in param file\n");
	  exit(1);
	}
      }
    } else if ( strcmp(argv[i],"--magic2") == 0 ) {
      FILE *f;
      int j;
      if ( ++i == argc ) usage(argv[0],"missing argument to --magic2");
      f = fopen(argv[i],"r");
      if ( f == NULL ) {
	perror("can't open param file");
	exit(1);
      }
      for ( j = 0 ; j < 13 ; j++ ) {
	if ( fscanf(f,"%lg",&magic2[j]) != 1 ) {
	  fprintf(stderr,"%s","error in param file\n");
	  exit(1);
	}
      }
    } else {
      fprintf(stderr,argv[i]);
      usage(argv[0],"unknown option or argument");
    }
  }

  srand(time(NULL));

  if ( evalh ) {
    Player pp;
    Game g;
    
    g.round = EastWind;
    the_game = &g;
    pp.wind = EastWind;
    initialize_player(&pp);
    set_player_tiles(&pp,evalh);
    our_player = &pp;
    evalhand(&pp,&curstrat);
    exit(0);
  }

  the_game = client_init(address);
  if ( ! the_game ) exit(1);

  sprintf(buf,"Robot(%d)",getpid());

  client_connect(the_game,our_id,name ? name : buf);

  while ( 1 ) {
   l = get_line(the_game->fd);
    if ( ! l ) {
      exit(1);
    }
    despatch_line(l);
  }
}

/* despatch_line: this is the mega-switch which deals with
   the input from the controller */
static int despatch_line(char *line) {
  CMsgMsg *cm;

  if ( line == NULL ) {

    warn("receive error on controller connexion\n");
    exit(1);
  }

  cm = decode_cmsg(line);
  if ( cm == NULL ) {
    warn("Protocol error on controller connexion; ignoring\n");
    return 0;
  }

  update_tilesleft((CMsgUnion *)cm);

  switch ( cm->type ) {
  case CMsgError:
    break; /* damn all we can do */
  case CMsgGameOver:
    exit(0);
  case CMsgReconnect:
    /* we should never receive this */
    warn("Received Reconnect command\n");
    exit(1);
  case CMsgInfoTiles:
    /* We ignore these. */
    break;
  case CMsgCanMahJong:
    /* Currently we ignore these, as we don't issue queries */
    break;
  case CMsgConnectReply:
    game_handle_cmsg(the_game,cm);
    our_id = the_game->players[0]->id;
    our_player = the_game->players[0];
    break;
  case CMsgAuthReqd:
    {
      CMsgAuthReqdMsg *carm = (CMsgAuthReqdMsg *)cm;
      PMsgAuthInfoMsg paim;
      paim.type = PMsgAuthInfo;
      if ( strncmp(carm->authtype,"basic",16) != 0 ) {
	warn("Asked for unknown authorization type %.16s",carm->authtype);
	exit(1);
      }
      strcpy(paim.authtype,"basic");
      if ( password == NULL ) {
	warn("Asked for password, don't have it");
	exit(1);
      }
      paim.authdata = password;
      send_packet(&paim);
    }
    break;
    /* In these cases, our seat might have changed, so we need to calculate it */
  case CMsgGame:
    /* In this case, we need to ask for the game options */
    {
      PMsgListGameOptionsMsg plgom;
      plgom.type = PMsgListGameOptions;
      plgom.include_disabled = 0;
      send_packet(&plgom);
    }
    /* and then ... */
  case CMsgNewRound:
  case CMsgNewHand:
    game_handle_cmsg(the_game,cm);
    our_seat = game_id_to_seat(the_game,our_id);
    if ( debugeval ) fprintf(debugf,"New hand\n");
    /* reset strategy to default */
    curstrat.chowness = stratparams[chowness].values[0];
    curstrat.hiddenness = stratparams[hiddenness].values[0];
    curstrat.majorness = stratparams[majorness].values[0];
    curstrat.suitness = stratparams[suitness].values[0];
    curstrat.suit = 0;
    break;
    /* in all these cases, game_handle_cmsg does all the work we want */
  case CMsgPlayer:
  case CMsgStopPlay:
  case CMsgClaimDenied:
  case CMsgPlayerDoesntClaim:
  case CMsgPlayerClaimsPung:
  case CMsgPlayerClaimsKong:
  case CMsgPlayerClaimsChow:
  case CMsgPlayerClaimsMahJong:
  case CMsgPlayerShowsTiles:
  case CMsgDangerousDiscard:
  case CMsgGameOption:
  case CMsgChangeManager:
  case CMsgWall:
  case CMsgComment:
  case CMsgStateSaved:
  case CMsgMessage:
    game_handle_cmsg(the_game,cm);
    break;
  case CMsgHandScore:
    /* if that was the winner, we should start scoring our hand */
    if ( ( game_handle_cmsg(the_game,cm) 
	   == the_game->players[the_game->player]->id)
	 && the_game->active )
      do_something();
    break;
    /* after a Settlement or Washout message, do something: start next hand */
  case CMsgWashOut:
  case CMsgSettlement:
    game_handle_cmsg(the_game,cm);
    if ( the_game->active ) do_something();
    break;
    /* likewise after a washout */
    /* after a MahJong message, we should do something: namely
       start making our scoring sets. */
  case CMsgPlayerRobsKong:
  case CMsgPlayerMahJongs:
    game_handle_cmsg(the_game,cm);
    if ( the_game->active ) do_something();
    break;
    /* in the case of a PlayerDeclaresSpecials message, we need to
       do something if it is now our turn; but this isn't given
       by the affected id.
       However, if the state is Discarding, and no tiles have
       so far been discarded, we shouldn't do something
       now, since we are about to be asked to pause.
    */
  case CMsgPlayerDeclaresSpecial:
    game_handle_cmsg(the_game,cm);
    if ( the_game->player == our_seat && the_game->active
	 && ! ( the_game->state == Discarding && the_game->serial == 0 ))
      do_something();
    break;
    /* in these cases, we need to do something if the message
       is addressed to us. */
  case CMsgPlayerDraws:
  case CMsgPlayerDrawsLoose:
  case CMsgPlayerPungs:
  case CMsgPlayerKongs:
  case CMsgPlayerChows:
  case CMsgPlayerFormsClosedPung:
  case CMsgPlayerFormsClosedChow:
  case CMsgPlayerPairs:
  case CMsgPlayerFormsClosedPair:
  case CMsgPlayerSpecialSet:
  case CMsgPlayerFormsClosedSpecialSet:
  case CMsgSwapTile:
    if ( game_handle_cmsg(the_game,cm) == our_id && the_game->active)
      do_something();
    break;
    /* in this case, we need to do something else if it's not our turn! */
  case CMsgPlayerDiscards:
    if ( game_handle_cmsg(the_game,cm) != our_id && the_game->active)
      check_discard(our_player,&curstrat,0);
    break;
    /* if this is us, we need to do something, and if it's 
       somebody else, we might be able to rob the kong */
  case CMsgPlayerDeclaresClosedKong:
    if ( game_handle_cmsg(the_game,cm) == our_id && the_game->active)
      do_something();
    else if ( the_game->active ) 
      check_discard(our_player,&curstrat,1); /* actually this checks the kong */
    break;
  case CMsgPlayerAddsToPung:
    if ( game_handle_cmsg(the_game,cm) == our_id && the_game->active)
      do_something();
    else if ( the_game->active ) 
      check_discard(our_player,&curstrat,0); /* actually this checks the kong */
    break;
    /* In this case, it depends on the state of the game */
  case CMsgStartPlay:
    /* We need to do something if the id is us, or 0. */
    { int id;
    id = game_handle_cmsg(the_game,cm);
    if ( id == our_id || id == 0 ) 
      do_something();
    }
    break;
    /* similarly */
  case CMsgPlayerReady:
    game_handle_cmsg(the_game,cm);
    if ( ! the_game->paused ) do_something();
    break;
  case CMsgPause:
    game_handle_cmsg(the_game,cm);
    do_something();
    break;
  case CMsgPlayerOptionSet:
    /* we don't recognize any options, so ignore it */
    break;
  case CMsgRedirect:
    warn("Redirect command not currently supported");
    exit(1);
  }
  return 1;
}

/* do something when it's our turn. 
*/
static void do_something(void) {
  int i;
  MJSpecialHandFlags mjspecflags;

  mjspecflags = 0;
  if ( game_get_option_value(the_game,GOSevenPairs,NULL).optbool )
    mjspecflags |= MJSevenPairs;

  /* if the game is paused, and we haven't said we're ready, say so */
  if ( the_game->paused ) {
    if ( !the_game->ready[our_seat] ) {
      PMsgReadyMsg pm;
      pm.type = PMsgReady;
      send_packet(&pm);
    }
    return;
  }

  /* if the game state is handcomplete, do nothing */
  if ( the_game->state == HandComplete ) return;

  /* If the game state is discarded, then it must mean this has
     been called in response to a StartPlay message after resuming
     an old hand. So actually we want to check the discard, unless
     of course we are the discarder, or we have already claimed. */
  if ( the_game->state == Discarded ) {
    if ( the_game->player != our_seat
	 && the_game->claims[our_seat] == UnknownClaim )
      check_discard(our_player,&curstrat,0);
    return;
  }

  /* If the game state is Dealing, then we should not do anything.
   */
  if ( the_game->state == Dealing ) return;

  /* if we're called in declaring specials or discarding, but it's
     not our turn, do nothing */
  if ( (the_game->state == DeclaringSpecials
	|| the_game->state == Discarding)
       && the_game->player != our_seat ) return;

  /* if we're waiting to draw another tile, do nothing */
  if ( the_game->needs != FromNone ) return;

  /* if we have a special, declare it. N.B. we'll
     be called again as a result of this, so only look for first.
  */
  for ( i=0; i < our_player->num_concealed
	  && ! is_special(our_player->concealed[i]) ; i++);
  if ( i < our_player->num_concealed ) {
    PMsgDeclareSpecialMsg m;
    m.type = PMsgDeclareSpecial;
    m.tile = our_player->concealed[i];
    send_packet(&m);
    return;
  }
  /* OK, no specials */
  if ( the_game->state == DeclaringSpecials ) {
    PMsgDeclareSpecialMsg m;
    m.type = PMsgDeclareSpecial;
    m.tile = HiddenTile;
    send_packet(&m);
    /* and at this point, we should decide our strategy */
    maybe_switch_strategy(&curstrat);
    return;
  }
  /* if the game is in the mahjonging state, and our hand is not declared,
     then we should declare a set. */
  if ( the_game->state == MahJonging ) {
    TileSet *tsp;
    PMsgUnion m;

    if ( pflag(our_player,HandDeclared) ) return;
    /* as courtesy, if we're not the winner, we shouldn't score until
       the winner has */
    if ( our_seat != the_game->player
	 && ! pflag(the_game->players[the_game->player],HandDeclared) ) return;

    /* get the list of possible decls */
    tsp = client_find_sets(our_player,
			   (the_game->player == our_seat
			    && the_game->mjpending)
			   ? the_game->tile : HiddenTile,
			   the_game->player == our_seat,
			   (PlayerP *)0,mjspecflags);
    if ( !tsp && our_player->num_concealed > 0 ) {
      m.type = PMsgShowTiles;
      send_packet(&m);
      return;
    }
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
    return;
  }
    
  /* if we can declare MahJong, do it */
  if ( player_can_mah_jong(our_player,HiddenTile,mjspecflags) ) {
    PMsgMahJongMsg m;
    m.type = PMsgMahJong;
    m.discard = 0;
    send_packet(&m);
    return;
  } else if ( the_game->whence != FromDiscard ) {
    /* check for concealed kongs and melded kongs. Just declare them. */
    int i;
    double val;
    Player pc;
    val = evalhand(our_player,&curstrat);
    /* a side effect of the above call is that our concealed tiles 
       are sorted (in reverse order), so we can avoid duplicating effort */
    for (i=0;i<our_player->num_concealed;i++) {
      /* don't look at same tile twice */
      if ( i && our_player->concealed[i] == our_player->concealed[i-1] ) continue;
      if ( player_can_declare_closed_kong(our_player,our_player->concealed[i]) ) {
	PMsgDeclareClosedKongMsg m;
	copy_player(&pc,our_player);
	player_declares_closed_kong(&pc,our_player->concealed[i]);
	if ( evalhand(&pc,&curstrat) > val ) {
	  m.type = PMsgDeclareClosedKong;
	  m.tile = our_player->concealed[i];
	  send_packet(&m);
	  return;
	}
      }
    }
    /* Now check for pungs we can meld to */
    for (i=0;i<MAX_TILESETS;i++) {
      if ( our_player->tilesets[i].type == Pung
	   && player_can_add_to_pung(our_player,our_player->tilesets[i].tile) ) {
	PMsgAddToPungMsg m;
	copy_player(&pc,our_player);
	player_adds_to_pung(&pc,our_player->tilesets[i].tile);
	if ( evalhand(&pc,&curstrat) > val ) {
	  m.type = PMsgAddToPung;
	  m.tile = our_player->tilesets[i].tile;
	  send_packet(&m);
	  return;
	}
      }
    }
  }
  /* if we get here, we have to discard */
  {
    PMsgDiscardMsg m;
    Tile t;
    
    /* strategy switching only after drawing tile from wall */
    if ( the_game->whence != FromDiscard || !strategy_chosen) {
      maybe_switch_strategy(&curstrat);
    }
  
    t = decide_discard(our_player,NULL,&curstrat);

    m.type = PMsgDiscard;
    m.tile = t;
    m.calling = 0; /* we don't bother looking for original call */
    send_packet(&m);
    return;
  }
}

/* Check if we want the discard, and claim it.
   Arg is strategy.
   Also called to check whether a kong can be robbed -
   the last arg says if being called on a closed kong */
static void check_discard(PlayerP p, strategy *strat,int closedkong) {
  PMsgUnion m;
  double bestval,val;
  int canmj;
  char buf[100];
  MJSpecialHandFlags mjspecflags;

  mjspecflags = 0;
  if ( game_get_option_value(the_game,GOSevenPairs,NULL).optbool )
    mjspecflags |= MJSevenPairs;

  if ( the_game->state == Discarding 
       || the_game->state == DeclaringSpecials ) {
    /* this means we're being called to check whether a kong
       can be robbed. Since robbing a kong gets us an extra double,
       this is probably always worth doing, unless we're trying for
       some limit hand */
    if ( player_can_mah_jong(p,the_game->tile,mjspecflags)
      && (!closedkong || player_can_thirteen_wonders(p,the_game->tile)) ) {
      m.type = PMsgMahJong;
      m.mahjong.discard = the_game->serial;
    } else {
      m.type = PMsgNoClaim;
      m.noclaim.discard = the_game->serial;
    }
    send_packet(&m);
    return;
  }

  if ( debugeval ) {
    player_print_tiles(buf,p,0);
    fprintf(debugf,"Hand: %s  ; discard %s\n",buf,tile_code(the_game->tile));
  }
  bestval = evalhand(p,strat);
  if ( debugeval ) {
    fprintf(debugf,"Hand value before claim %.3f\n",bestval);
  }

  canmj = player_can_mah_jong(p,the_game->tile,mjspecflags);
  m.type = PMsgNoClaim;
  m.noclaim.discard = the_game->serial;
  if ( player_can_kong(p,the_game->tile) ) {
      Player pc;

      copy_player(&pc,p);
      player_kongs(&pc,the_game->tile);
      val = evalhand(&pc,strat);
      /* we won't discard a tile here */
      if ( debugeval ) {
	fprintf(debugf,"Hand after kong  %.3f\n",val);
      }
      if ( val > bestval ) {
	m.type = PMsgKong;
	m.kong.discard = the_game->serial;
	bestval = val;
      }
      else 
	if ( debugeval ) {
	  fprintf(debugf,"Chose not to kong\n");
	}
  } 
  if ( player_can_pung(p,the_game->tile) ) {
    Player pc;

    copy_player(&pc,p);
    player_pungs(&pc,the_game->tile);
    decide_discard(&pc,&val,&curstrat);
    if ( debugeval ) {
      fprintf(debugf,"Hand after pung  %.3f\n",val);
    }
    if ( val > bestval ) {
      m.type = PMsgPung;
      m.pung.discard = the_game->serial;
      bestval = val;
    }
    else 
      if ( debugeval ) {
	fprintf(debugf,"Chose not to pung\n");
      }
    }
  if ( (canmj || our_seat == (the_game->player+1)%NUM_SEATS)
       && is_suit(the_game->tile) ) {
    ChowPosition cpos = (unsigned) -1;
    Player pc;
    int chowposs = 0;
    Tile t = the_game->tile;
    copy_player(&pc,p);
    if ( player_chows(&pc,t,Lower) ) {
      decide_discard(&pc,&val,&curstrat);
      if ( debugeval ) {
	fprintf(debugf,"Hand after lower chow: %.3f\n",val);
      }
      chowposs = 1;
      if ( val > bestval ) {
	bestval = val;
	cpos = Lower;
      }
      copy_player(&pc,p);
    }
    if ( player_chows(&pc,t,Middle) ) {
      decide_discard(&pc,&val,&curstrat);
      if ( debugeval ) {
	fprintf(debugf,"Hand after middle chow: %.3f\n",val);
      }
      chowposs = 1;
      if ( val > bestval ) {
	bestval = val;
	cpos = Middle;
      }
      copy_player(&pc,p);
    }
    if ( player_chows(&pc,t,Upper) ) {
      chowposs = 1;
      decide_discard(&pc,&val,&curstrat);
      if ( debugeval ) {
	fprintf(debugf,"Hand after upper chow: %.3f\n",val);
      }
      if ( val > bestval ) {
	bestval = val;
	cpos = Upper;
      }
      copy_player(&pc,p);
    }
    if ( cpos != (unsigned)-1 ) {
      m.type = PMsgChow;
      m.chow.discard = the_game->serial;
      m.chow.cpos = cpos;
    } 
    else
      if ( debugeval ) {
	if ( chowposs ) fprintf(debugf,"chose not to chow\n");
      }
  }
  /* mah jong */
  if ( canmj ) {
    m.type = PMsgMahJong;
    m.mahjong.discard = the_game->serial;

#if 1
    /* if we're following a concealed strategy, and we still have
       four chances (ex?cluding this one) of going out, then
       don't claim */
    /* instead of four chances, make it depend on number of tiles left */
    /* test: if hidden >1, then never claim */
    if ( (strat->hiddenness > 1.0) || (strat->hiddenness*chances_to_win(p) 
	 * (the_game->wall.live_end-the_game->wall.live_used)
	 / (the_game->wall.dead_end-the_game->wall.live_used) 
	 >= 1.5) ) {
      m.type = PMsgNoClaim;
      m.noclaim.discard = the_game->serial;
    }
#endif
    if ( m.type != PMsgNoClaim ) {
      if ( debugeval && strat->hiddenness > 0.0 ) fprintf(debugf,"claiming mahjong on hidden strategy\n");
      m.type = PMsgMahJong;
      m.mahjong.discard = the_game->serial;
    } else {
      if ( debugeval ) {
	fprintf(debugf,"CHOSE NOT TO MAHJONG\n");
      }
    }
  }
  if ( debugeval ) {
    fprintf(debugf,"Result: %s",encode_pmsg((PMsgMsg *)&m));
  }
  send_packet(&m);
  return;
}


/* Here is a data structure to track the number of (assumed)
   available tiles elsewhere. The update fn should be called on every CMsg. */

static void update_tilesleft(CMsgUnion *m) {
  int i;
  /* note that we don't need to check whether the tile is blank,
     since the HiddenTile entry of tilesleft has no meaning.
  */
  switch ( m->type ) {
  case CMsgNewHand:
    for (i=0; i < MaxTile; i++) {
      tilesleft[i] = 4;
      rightdiscs[i] = 0;
    }
    return;
  case CMsgPlayerDeclaresSpecial:
    return;
  case CMsgPlayerDraws:
    tilesleft[m->playerdraws.tile]--;
    return;
  case CMsgPlayerDrawsLoose:
    tilesleft[m->playerdrawsloose.tile]--;
    return;
  case CMsgPlayerDiscards:
    /* if this is us, we've already counted it */
    if ( m->playerdiscards.id != our_id )
      tilesleft[m->playerdiscards.tile]--;
    if ( game_id_to_seat(the_game,m->playerdiscards.id)
	 == (our_seat+1)%4 ) 
      rightdiscs[m->playerdiscards.tile] = m->playerdiscards.discard;
    return;
  case CMsgPlayerPungs:
    /* if somebody else pungs, then two more tiles become dead
       (the discard was already noted).
       If we pung, nothing new is known */
    if ( m->playerpungs.id != our_id )
      tilesleft[m->playerpungs.tile] -= 2;
    return;
  case CMsgPlayerKongs:
    /* if somebody else kongs, then three more tiles become dead
       (the discard was already noted). */
    if ( m->playerkongs.id != our_id )
      tilesleft[m->playerkongs.tile] -= 3;
    return;
  case CMsgPlayerDeclaresClosedKong:
    if ( m->playerdeclaresclosedkong.id != our_id )
      tilesleft[m->playerdeclaresclosedkong.tile] -= 4;
    return;
  case CMsgPlayerAddsToPung:
    if ( m->playeraddstopung.id != our_id )
      tilesleft[m->playeraddstopung.tile]--;
    return;
  case CMsgPlayerChows:
    if ( m->playerchows.id != our_id ) {
      Tile t = m->playerchows.tile;
      ChowPosition c = m->playerchows.cpos;
      tilesleft[(c==Lower)?t+1:(c==Middle)?t-1:t-2]--;
      tilesleft[(c==Lower)?t+2:(c==Middle)?t+1:t-1]--;
    }
    return;
  case CMsgSwapTile:
    tilesleft[m->swaptile.oldtile]++;
    tilesleft[m->swaptile.newtile]--;
    return;
  default:
    return;
  }
}


/* This sorts the tiles (as below) into reverse order.
   Why reverse order? So that HiddenTile terminates the hand.
*/
static void tsort(Tile *tp) {
  int i,m;
  Tile t;
  /* bubblesort */
  m = 1;
  while ( m ) {
    m = 0;
    for (i=0; i<MAX_CONCEALED+1-1;i++)
      if ( tp[i] < tp[i+1] ) {
	t = tp[i];
	tp[i] = tp[i+1];
	tp[i+1] = t;
	m = 1;
      }
  }
}


/* This function attempts to evaluate a partial hand.
   The input is a (pointer to an) array of tiles
   (length MAX_CONCEALED+1; some will be blank).
   The value is a floating point number, calculated thus:
   for the first tile in the set, form all possible sets
   and partial sets (incl. singletons) with that tile;
   then recurse on the remainder. The value is then the
   max over the partial sets of the inherent value of the
   set (explained below) and the recursive value of the remaining hand.
   This would have the slightly awkward consequence that pairs get counted
   twice in pungs, since given 222... we see 22 2 and also 2 22.
   Hence, if we form a pung, we don't form a pair also.
   (Nothing special is done about kongs; it should be.)
   **** The input is sorted in place. ****
*/

/* debugging format:
   SSTT:mmmm+ (recursive)
   tttt(nnnn)
   where SS is a set code (Pu,Pr,Ch,In,Ou,Si),
   TT is the tile code, mmmm is the set value,
   tttt is the total, nnnn is the residual
   Hence the recursive call should indent all lines except the first
   by reclevel*11 characters, and the base case call should emit
   a newline.
*/
/* other parameters:
   strat: current strategy
   reclevel: recursion level. Should start at 0.
   ninc: number of incomplete sets (including pairs) in
     the chain so far.
   npr: number of pairs in the chain so far.
   breadth is something to try to count the number of ways a hand
   can be evaluated. Every time we hit bottom, we'll increment
   breadth by 1/reclevel.
*/

static double eval(Tile *tp,strategy *strat,double *stratpoints,
		   int reclevel,
		   int *ninc,
		   int *npr,
		   double *breadth) {
  Tile copy[MAX_CONCEALED+1],copy2[MAX_CONCEALED+1];
  int i;
  double mval = 0, pval = 0, val = 0,
    pvalr = 0, mvalr = 0, valr = 0;
  int ppr, mpr; int spr;
  char prefix[200];
  static char totbuf[200];
  char tb[20];
  int notfirst=0;

  tsort(tp);
  
  if ( reclevel == -1 ) {
    /*evaluate each suit in turn*/
    mval = 0.0;
    tcopy(copy,tp);
    while ( suit_of(copy[0]) ) {
      tcopy(copy2,copy);
      for ( i = 0; suit_of(copy[i]) == suit_of(copy[0]); i++ );
      for ( ; i < MAX_CONCEALED+1 ; i++ ) copy[i] = HiddenTile;
      val = eval(copy,strat,stratpoints,0,ninc,npr,breadth);
      mval += val ;
      if ( debugeval ) fprintf(debugf,"Evaling suit %d returned v=%.2f, b=%.2f\n",
			      suit_of(copy[0]),val,*breadth);
      tcopy(copy,copy2);
      for ( i= 0; suit_of(copy[i]) == suit_of(copy2[0]) ; i++ )
	copy[i] = HiddenTile;
      tsort(copy);
    }
    if ( debugeval ) fprintf(debugf,"Level 0 returned %.2f\n",mval);
    return mval;
  }

  if ( debugeval ) {
    /* The total buffer. A call at recursion level n writes the total into bytes
       11(n-1) to 11n-1  of the buffer (right justified in the first 4 chars; 
       spaces in the rest.
       A base case terminates the total buffer with null.
       When a second set is printed, or when we terminate at level zero,
       then the total buffer is printed.
    */

    for (i=0;i<reclevel*11;i++) prefix[i]=' ';
    prefix[i]=0;
  }

  if ( tp[0] == HiddenTile ) {
    if ( debugeval > 1 ) {
      fprintf(debugf,"\n"); 
      if (reclevel) sprintf(&totbuf[11*(reclevel-1)+4],"\n");
    }
    /* if there is one "incomplete" set and one pair, we have a complete
       hand */
#if 0
    if ( ninc == 1 && npr == 1 ) val = stratpoints[mjbonus];
    else val = 0.0;
#else
    val = 0.0;
#endif
    if ( debugeval > 1 ) {
      sprintf(tb,"%4.1f",val);
      strncpy(&totbuf[11*reclevel],tb,4);
    }
    return val;
  }

  /* does it pung? */
  ppr = 0;
  if ( tp[1] == tp[0] ) {
    if ( tp[2] == tp[0] ) {
      val = 0.0;
      /* add the value of this set.
      */
      val += stratpoints[pungbase];
      /* if we're trying for a no score hand, scoring pairs are bad */
      if ( is_doubler(tp[0]) ) val += (-strat->chowness)  * magic2[0];
      if ( is_major(tp[0]) ) { 
	val += magic2[1];
      } else {
	val -= magic2[2]*strat->majorness;
      }
      if ( is_suit(tp[0]) && suit_of(tp[0]) != strat->suit ) 
	val *= stratpoints[suitfactor];
      if ( debugeval > 1 ) {
	fprintf(debugf,"%sPu%s:%4.1f+ ",notfirst++?prefix:"",tile_code(tp[0]),val);
      }
      /* remove the tiles and recurse */
      tcopy(copy,tp);
      copy[0] = copy[1] = copy[2] = HiddenTile;
      valr = eval(copy,strat,stratpoints,reclevel+1,ninc,&ppr,breadth);
      if ( debugeval > 1 ) {
	sprintf(tb,"%4.1f",val);
	strncpy(&totbuf[11*reclevel],tb,4);
      }
      val += valr;
      if ( val > pval ) { pval = val; pvalr = valr ; }
    } else {
      val = 0.0;
      /* A pair is worth something for itself, plus something
	 for its chances of becoming a pung.
	 A doubler pair is worth an extra 2 points.
	 A major pair is worth an extra point.
	 Beware the risk of arranging effect that a chow and 
	 a pair is worth much more than a pung and an inner sequence,
	 so that we'd break a pung rather than a chow.
      */
      ppr++;
      val += stratpoints[partpung]*stratpoints[pungbase] * tilesleft[tp[0]];
      /* doublers are bad for noscore */
      if ( is_doubler(tp[0]) ) val += 2 - (strat->chowness)*4.0;
      if ( is_major(tp[0]) ) {
	val += magic2[3];
      } else {
	val -= magic2[4]*strat->majorness;
      }
      if ( is_suit(tp[0]) && suit_of(tp[0]) != strat->suit ) 
	val *= stratpoints[suitfactor];
      if ( debugeval > 1 ) {
	fprintf(debugf,"%sPr%s:%4.1f+ ",notfirst++?prefix:"",tile_code(tp[0]),val);
      }
      /* remove the tiles and recurse */
      tcopy(copy,tp);
      copy[0] = copy[1] = HiddenTile;
      (*ninc)++;
      valr = eval(copy,strat,stratpoints,reclevel+1,ninc,&ppr,breadth);
      val += valr;
      if ( debugeval > 1 ) {
	sprintf(tb,"%4.1f",val);
	strncpy(&totbuf[11*reclevel],tb,4);
      }
      if ( val > pval ) { pval = val; pvalr = valr; }
    }
  }
  /* OK, now deal with chows. */
  mpr = 0;
  if ( is_suit(tp[0]) ) {
    Tile t1,t2; int i1,i2; /* other tiles, and their indices */
    /* NB tiles are in reverse order!!!!! */
    t1 = ((value_of(tp[0]) > 1) ? tp[0]-1 : 0);
    t2 = ((value_of(tp[0]) > 2) ? tp[0]-2 : 0);
    for (i1=0;t1 && tp[i1] && tp[i1]!=t1;i1++);
    if ( ! tp[i1] ) i1=0;
    for (i2=0;t2 && tp[i2] && tp[i2]!=t2;i2++);
    if ( ! tp[i2] ) i2=0;

    /* if we have a chow */
    if ( i1 && i2 ) {
      val = 0.0;
      /* A chow is deemed to be worth 12 points also.
	 (Quick and dirty ... hike pungbonus to avoid chows.)
      */
     val += stratpoints[chowbase];
      if ( is_suit(tp[0]) && suit_of(tp[0]) != strat->suit ) 
	val *= stratpoints[suitfactor];
     if ( debugeval > 1 ) {
       if ( notfirst ) fprintf(debugf,"%s%s",prefix,&totbuf[11*reclevel]);
       fprintf(debugf,"%sCh%s:%4.1f+ ",notfirst++?prefix:"",tile_code(tp[0]),val);
     }
      /* remove the tiles and recurse */
      tcopy(copy,tp);
      copy[0] = copy[i1] = copy[i2] = HiddenTile;
      valr = eval(copy,strat,stratpoints,reclevel+1,ninc,&mpr,breadth);
      val += valr;
      if ( debugeval > 1 ) {
	sprintf(tb,"%4.1f",val);
	strncpy(&totbuf[11*reclevel],tb,4);
      }
      if ( val > mval ) { mval = val; mvalr = valr; }
    }
    /* If we have an inner sequence... note that it's intentional
       that we do this as well, since maybe if we split the chow,
       we'll find a pung. But we'
    */
    if ( i1 ) {
      val = 0.0;
      /* An inner sequence is worth the number of chances of completion,
	 allowing for the fact that on average, the tiles in right
	 and opposite and half the tiles in the wall will not be available
	 for completing a chow.
	 NOTE: this needs to change when we're nearly at MahJong.
      */
      val += stratpoints[seqbase];
      if ( value_of(tp[0]) < 9 ) {
	val += stratpoints[partchow]*stratpoints[chowbase] * tilesleft[tp[0]+1];
      }
      if ( value_of(t1) > 1 ) {
	val += stratpoints[partchow]*stratpoints[chowbase] * tilesleft[t1-1];
      }
      if ( is_suit(tp[0]) && suit_of(tp[0]) != strat->suit ) 
	val *= stratpoints[suitfactor];
      if ( debugeval > 1 ) {
	if ( notfirst ) fprintf(debugf,"%s%s",prefix,&totbuf[11*reclevel]);
	fprintf(debugf,"%sIn%s:%4.1f+ ",notfirst++?prefix:"",tile_code(tp[0]),val);
      }
      /* remove the tiles and recurse */
      tcopy(copy,tp);
      copy[0] = copy[i1] = HiddenTile;
      valr = eval(copy,strat,stratpoints,reclevel+1,ninc,&mpr,breadth);
      val += valr;
      if ( debugeval > 1 ) {
	sprintf(tb,"%4.1f",val);
	strncpy(&totbuf[11*reclevel],tb,4);
      }
      if ( val > mval ) { mval = val; mvalr = valr; }
    }
    /* If we have a split sequence ... Here we don't do this if there's
       been a chow. Hmm, why not? I thought I had a reason. Fill it in
       sometime. */
    else if ( i2 ) {
      val = 0.0;
      /* Likewise */
      val += stratpoints[seqbase];
      val += stratpoints[partchow]*stratpoints[chowbase] * tilesleft[t1];
      if ( is_suit(tp[0]) && suit_of(tp[0]) != strat->suit ) 
	val *= stratpoints[suitfactor];
      if ( debugeval > 1 ) {
	if ( notfirst ) fprintf(debugf,"%s%s",prefix,&totbuf[11*reclevel]);
	fprintf(debugf,"%sOu%s:%4.1f+ ",notfirst++?prefix:"",tile_code(tp[0]),val);
      }
      /* remove the tiles and recurse */
      tcopy(copy,tp);
      copy[0] = copy[i2] = HiddenTile;
      valr = eval(copy,strat,stratpoints,reclevel+1,ninc,&mpr,breadth);
      val += valr;
      if ( debugeval > 1 ) {
	sprintf(tb,"%4.1f",val);
	strncpy(&totbuf[11*reclevel],tb,4);
      }
      if ( val > mval ) { mval = val; mvalr = valr; }
    }
  }
  /* Finally, the score for a singleton.
  */
  /* let's also add .25 times the number of neighbouring tiles */
  spr = 0;
  val = 0.0;
  val += stratpoints[sglbase]
    * (tilesleft[tp[0]]
       + ( is_suit(tp[0])
	  ? (magic2[5]*strat->chowness*(tilesleft[tp[0]-1]+ tilesleft[tp[0]+1])) : 0))
    * (magic2[6]-strat->chowness);
  if ( is_doubler(tp[0]) ) {
    if (tilesleft[tp[0]] == 0) val -= magic2[7]; /* completely useless */
    else {
      if ( (magic2[8]-strat->chowness) > 0 ) 
	val += (tilesleft[tp[0]])*stratpoints[sglbase]*(magic2[9]-strat->chowness);
      else
	val += (3-tilesleft[tp[0]])*stratpoints[sglbase]*(magic2[10]-strat->chowness);
    }
  }
  if ( is_suit(tp[0]) && suit_of(tp[0]) != strat->suit ) {
    /* val could be negative, in which case we don't want to shrink it.
       So just substract a constant */
    val -= magic2[11]*strat->suitness*stratpoints[sglbase];
  }
  if ( ! is_major(tp[0]) ) {
    val -= magic2[12]*strat->majorness*stratpoints[sglbase];
  }
  if ( debugeval > 1 ) {
    if ( notfirst ) fprintf(debugf,"%s%s",prefix,&totbuf[11*reclevel]);
    fprintf(debugf,"%sSi%s:%4.1f+ ",notfirst++?prefix:"",tile_code(tp[0]),val);
  }
  tcopy(copy,tp);
  copy[0] = HiddenTile;
  valr = eval(copy,strat,stratpoints,reclevel+1,ninc,&spr,breadth);
  val += valr;
  if ( debugeval > 1 ) {
    sprintf(tb,"%4.1f",val);
    strncpy(&totbuf[11*reclevel],tb,4);
  }
  /* at this point, we have pair/seq/single based scores. */

  if ( mval > pval ) {
    if ( val > mval ) {
      if ( spr ) (*npr)++;
      mval = val;
    } else {
      if ( mpr ) (*npr)++;
    }
  } else {
    if ( val > pval ) {
      mval = val;
      if ( spr ) (*npr)++;
    } else { 
      mval = pval;
      if ( ppr ) (*npr)++;
    }
  }
  if ( debugeval > 1 ) {
    if ( reclevel ) {
      sprintf(tb,"(%4.1f) ",mval);
      strncpy(&totbuf[11*(reclevel-1)+4],tb,7);
    }
    else fprintf(debugf,totbuf);
  }
  return mval;
}


/* This function uses the above to evaluate a player's hand, including
   the completed sets. */
static double evalhand(PlayerP p,strategy *strat) {
  Tile tcopy[MAX_CONCEALED+1];
  double val,breadth;
  int i, npr, ninc;
  double stratpoints[weight+1];

  if ( debugeval ) {
    char buf[100];
    fprintf(debugf,"eval with strat params c=%.1f, h=%.1f, m=%.1f, s=%.1f (%d)\n",
	   strat->chowness,strat->hiddenness,strat->majorness,
	   strat->suitness,strat->suit);
    player_print_tiles(buf,p,0);
    fprintf(debugf,"Hand: %s\n",buf);
  }
  /* calculate old style strategy values from new ones */
  stratpoints[pungbase] = 
    magic[0] - ( strat->chowness > 0 ? strat->chowness * magic[0] 
	     : strat->chowness * magic[1] );
  stratpoints[pungbase] *= (magic[2] + magic[3] * strat->hiddenness);
  stratpoints[pairbase] = magic[4]*(magic[5] + (magic[6]*fabs(strat->chowness)));
  stratpoints[chowbase] = 
    magic[0] + ( strat->chowness > 0 ? strat->chowness * magic[7]
	     : strat->chowness * magic[0] );
  stratpoints[chowbase] *= (magic[8] + magic[9] * strat->hiddenness)
    * (1.0 - strat->majorness);
  stratpoints[seqbase] = magic[10]*(magic[11] + ( strat->chowness < 0 ? strat->chowness : 0))
    * (1.0 - strat->majorness);
  stratpoints[sglbase] = magic[12];
  stratpoints[partpung] = magic[13]*(magic[14])*(1-magic[15]*strat->hiddenness);
  stratpoints[partchow] = magic[16]*(1-magic[17]*strat->hiddenness) * (1.0 - strat->majorness);
  stratpoints[exposedpungpenalty] = magic[18] + strat->hiddenness * stratpoints[pungbase]
    + ( (strat->chowness > 0) ? magic[19] * strat->chowness : 0.0 );
  stratpoints[exposedchowpenalty] = magic[20] + strat->hiddenness * stratpoints[chowbase]
    + ( (strat->chowness < 0) ? -1*magic[21] * strat->chowness : 0.0 )
    + magic[21] * strat->majorness;
  stratpoints[mjbonus] = magic[22];
  stratpoints[kongbonus] = magic[23];
  stratpoints[suitfactor] = 
    ((strat->suitness >= 1.0) ? 0.01 : (1.0 - magic[24]*strat->suitness));
  stratpoints[weight] = magic[25] + 
    (strat->suitness >= 1.0 ? magic[26]*(strat->suitness-1.0) : magic[26]*strat->suitness)
    + magic[27]*strat->majorness;

  for (i=0; i<p->num_concealed; i++)
    tcopy[i] = p->concealed[i];
  for ( ; i < MAX_CONCEALED+1; i++)
    tcopy[i] = HiddenTile;

  val = 0.0; 

  ninc = npr = 0; /* number of "incomplete"/pairs in hand */

  /* note that if we see any closed pungs in here, they are
     actually hacks representing hypothetical open sets */

  for (i=0; i<5; i++) {
    double sval = 0.0;
    switch (p->tilesets[i].type) {
    case Chow:
    case ClosedChow:
      sval -= stratpoints[exposedchowpenalty];
      sval += stratpoints[chowbase];
      break;
    case ClosedKong:
      sval += stratpoints[exposedpungpenalty]; /* cancel the penalty later */
    case Kong:
      sval += stratpoints[kongbonus];
    case Pung:
    case ClosedPung:
      sval -= stratpoints[exposedpungpenalty];
      sval += stratpoints[pungbase];
      /* these shadow evalhand above */
      if ( is_doubler(p->tilesets[i].tile) ) sval += (-strat->chowness)  * 6.0;
      if ( is_major(p->tilesets[i].tile) ) {
	sval += 2.0 ; /* small bonus for luck */
      } else {
	sval -= 25.0*strat->majorness;
      }
      break;
    case Pair: 
    case ClosedPair:
      ninc = npr = 1; /* for correct evaluation of pairs */
      sval += stratpoints[pairbase];
      break;
    default:
      ;
    }
    if ( p->tilesets[i].type != Empty 
	 && is_suit(p->tilesets[i].tile)
	 && suit_of(p->tilesets[i].tile) != strat->suit )
      sval -= 10.0*strat->suitness;
    if ( debugeval > 1 ) {
      fprintf(debugf,"Set %s: %.1f\n",player_print_TileSetType(p->tilesets[i].type),
	     sval);
    }
    val += sval;
  }
  breadth = 0.0;
  val += eval(tcopy,strat,stratpoints,-1,&ninc,&npr,&breadth);
  /* add the pairbase if we have at least one pair */
  if ( npr > 0 ) {
    if ( debugeval > 1 ) fprintf(debugf,"+pairbase %.1f\n",stratpoints[pairbase]);
    val += stratpoints[pairbase];
  }
  val *= stratpoints[weight];
#if 0 /* randomization seems to be a lose */
  /* now add a small random offset */
  r = (2.0*rand())/RAND_MAX - 1.0;
  r = 4*r*r;
  return val+r;
#else
  if ( debugeval ) fprintf(debugf,"Value %.2f\n",val);
  return val;
#endif
}

/* compute the number of ways a calling hand can go out */
static int chances_to_win(PlayerP p) {
  Tile t;
  int n;
  MJSpecialHandFlags mjspecflags;

  mjspecflags = 0;
  if ( game_get_option_value(the_game,GOSevenPairs,NULL).optbool )
    mjspecflags |= MJSevenPairs;

  t = HiddenTile;
  n = 0;
  while ( (t = tile_iterate(t,0)) != HiddenTile ) {
    if ( tilesleft[t] && player_can_mah_jong(p,t,mjspecflags) )
      n += tilesleft[t];
  }
  return n;
}

/* compute tile to discard. Return value is tile.
   Second arg returns score of resulting hand.
   Third arg returns the new strategy.
*/
static Tile decide_discard(PlayerP p, double *score, strategy *strat) {
  /* how do we choose a tile to discard? 
     remove the tile, and evaluate the remaining hand.
     Discard the tile giving the greatest residual value.
  */
  int i,best;
  double values[MAX_CONCEALED+1]; Tile tilesa[MAX_CONCEALED+1],tiles[MAX_CONCEALED+1] UNUSED;
  char buf[80];
  const Tile *t = p->concealed;
  
  
  for (i=0;i<MAX_CONCEALED+1;i++) values[i]=0;
  for (i=0;i<p->num_concealed;i++) tilesa[i] = t[i];
  for (;i<MAX_CONCEALED+1;i++) tilesa[i] = HiddenTile;
  
  if ( debugeval ) {
    player_print_tiles(buf,p,0);
    fprintf(debugf,"Hand: %s\n",buf);
  }
  best = 0;
  tsort(tilesa); /* no point in looking at same tile twice */
  for (i=0;i<p->num_concealed;i++) {
    Player cp;
    if ( i && tilesa[i] == tilesa[i-1] ) continue;
    if ( debugeval ) {
      fprintf(debugf,"Trying %s: \n",tile_code(tilesa[i]));
    }
    copy_player(&cp,p);
    player_discards(&cp,tilesa[i]);
    values[i] = evalhand(&cp,strat);
    /* add a bonus for tiles recently discarded by the player
       to the right */
    if ( rightdiscs[tilesa[i]] )
      values[i] += 0.5; /* /(the_game->serial-rightdiscs[tilesa[i]]) */;
    if ( is_suit(tilesa[i])
	 && ((value_of(tilesa[i]) < 7 
	      && rightdiscs[tilesa[i]+3])
	     || (value_of(tilesa[i]) > 3
		 && rightdiscs[tilesa[i]-3])) )
      values[i] += 0.4; /* the 1-4-7 argument */
    /* Best tile to discard leaves highest residual score */
    if ( values[i] > values[best] ) best = i;
    if ( debugeval ) {
	fprintf(debugf,"Tile %s has value %.1f\n",tile_code(tilesa[i]),values[i]);
    }
  }
  if ( debugeval ) {
    fprintf(debugf,"Discarding %s\n",tile_code(tilesa[best]));
  }
  if ( score ) *score = values[best];
  return tilesa[best];
}

/* Maybe switch strategy: takes a strat pointer as argument,
   and updates it in place. */
static void maybe_switch_strategy(strategy *strat) {
  strategy tmpstrat;
  int i,j;
  double val,oval, bestval;
  strategy beststrat;
  int dofast;
  PlayerP p = our_player;

  oval = evalhand(p,strat);

  bestval = -1000.0;
  tmpstrat.chowness = stratparams[chowness].values[0];
  tmpstrat.hiddenness = stratparams[hiddenness].values[0];
  tmpstrat.majorness = stratparams[majorness].values[0];
  /* it makes no sense to try to evaluate pung/chowness with
     a non-zero suitness, cos we don't know which suit, and
     evaluating with suit=0 is equiv to all honours! */
  /* tmpstrat.suitness = stratparams[suitness].values[0]; */
  tmpstrat.suitness = 0.0;
  tmpstrat.suit = 0;
  beststrat.chowness = tmpstrat.chowness;
  for ( i=0 ; i < stratparams[chowness].ncomps ; i++ ) {
    tmpstrat.chowness = stratparams[chowness].values[i];
    val = evalhand(p,&tmpstrat);
    if ( val > bestval ) { 
      bestval = val ; 
      beststrat.chowness = tmpstrat.chowness;
    }
  }
  tmpstrat.chowness = beststrat.chowness;
  beststrat.hiddenness = tmpstrat.hiddenness;
  /* from i = 1, since we had [0] just above */
  for ( i = 1 ; i < stratparams[hiddenness].ncomps ; i++ ) {
    tmpstrat.hiddenness = stratparams[hiddenness].values[i];
    val = evalhand(p,&tmpstrat);
    if ( val > bestval ) { 
      bestval = val ; 
      beststrat.hiddenness = tmpstrat.hiddenness;
    }
  }
  /* and for suit */
  tmpstrat.hiddenness = beststrat.hiddenness;
  /* now we need to reset the best val, since only now
     are we considering the values the user had */
  bestval = -1000;
  for ( i = 0; i < stratparams[suitness].ncomps; i++ ) {
    tmpstrat.suitness = stratparams[suitness].values[i];
    /* j = 0 corresponds to all honours, which we shd get
       if we can ?? */
    for ( j = 0 ; j <= ((tmpstrat.suitness == 0.0)? 0 : 3) ; j++ ) {
      tmpstrat.suit = j;
      val = evalhand(p,&tmpstrat);
      if ( val > bestval ) { 
	bestval = val ;
	beststrat = tmpstrat;
      }
    }
  }
  /* we'll now try for all majors... */
  /* however, there's no point in doing this unless we already
     think that a pung-based strategy is best */
  if ( beststrat.chowness < 0.0 ) {
    tmpstrat = beststrat;
    for ( i = 1; i < stratparams[majorness].ncomps; i++ ) {
      tmpstrat.majorness = stratparams[majorness].values[i];
      val = evalhand(p,&tmpstrat);
      if ( val > bestval ) { 
	bestval = val ;
	beststrat = tmpstrat;
      }
    }
  }
  /* if some other player has four sets declared, then switch to
     Fast */
  dofast = 0;
#if 0
  for ( i = 0 ; i < NUM_SEATS ; i++ ) {
    PlayerP p1 = the_game->players[i];
      if ( p1 == p ) continue;
      if ( p1->num_concealed <= 1 ) dofast = 1;
  }
#endif
  if ( dofast ) {
    /* strat = Fast; */
    if ( debugeval ) { fprintf(debugf,"Using fast\n"); }
  } else {
    if ( bestval >= oval + hysteresis ) {
      *strat = beststrat;
      if ( debugeval ) {
	static char buf[128];
	fprintf(debugf,"Switching to strat c=%.1f h=%.1f s=%.1f (%d) m=%.1f\n",
	       strat->chowness,strat->hiddenness,
	       strat->suitness,strat->suit,strat->majorness);
	  player_print_tiles(buf,p,0);
	  fprintf(debugf," with hand %s\n",buf);
      }
    }
  }
}
