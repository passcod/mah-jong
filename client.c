/* $Header: /home/jcb/MahJong/newmj/RCS/client.c,v 12.0 2009/06/28 20:43:12 jcb Rel $
 * client.c
 * Provides generic client support. It connects to the controller,
 * and maintains a game data structure in response to the Controller
 * messages. After updating the game structure, it (will) invokes a callback
 * installed by its user.
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/client.c,v 12.0 2009/06/28 20:43:12 jcb Rel $";

#include "sysdep.h"
#include <assert.h>
#include "client.h"

/* local for below two fns */
static Game *_client_init(char *address, int reinit, int oldfd) {
  int fd;
  int i;
  Game *g;

  if ( reinit ) {
    fd = oldfd;
  } else {
    if ( strcmp("-",address) == 0 ) {
      fd = STDOUT_FILENO;
    } else {
      fd = connect_to_host(address);
      if ( fd == (int)INVALID_SOCKET ) {
	perror("client_init: connect_to_host failed");
	return 0;
      }
    }
  }

  /* alloc the structures */

  g = (Game *) malloc(sizeof(Game));
  if ( g == NULL ) {
    warn("Couldn't malloc game structure");
    exit(1);
  }
  memset((void *)g,0,sizeof(Game));

  for ( i=0 ; i < NUM_SEATS ; i++ ) {
    if ( (g->players[i]
	  = (PlayerP) malloc(sizeof(Player)))
	 == NULL ) {
      warn("couldn't malloc player structure");
      exit(1);
    }
    memset((void *)g->players[i],0,sizeof(Player));
  }
  
  g->fd = fd;
  if ( ! reinit ) g->cseqno = 0;
  return g;
}

/* client_init: establish a connection to a controller */
Game *client_init(char *address) {
  return _client_init(address,0,0);
}

/* client_reinit: as above, but use the existing fd or handle passed
   as an argument */
Game *client_reinit(int fd) {
  return _client_init(NULL,1,fd);
}

/* client_connect: take an id and a name, and send a connect message.
   Return 1 on success, or 0 on failure. */
int client_connect(Game *g, int id, char *name) {
  PMsgConnectMsg cm;

  cm.type = PMsgConnect;
  cm.pvers = PROTOCOL_VERSION;
  cm.last_id = id;
  cm.name = name;

  client_send_packet(g,(PMsgMsg *)&cm);
  /* stash info in our own player record */
  assert(g->players[0]);
  initialize_player(g->players[0]);
  set_player_id(g->players[0],id);
  set_player_name(g->players[0],name);
  return 1;
}

/* internal for below */
static Game *_client_close(Game *g,int close) {
  int i;

  if ( close ) close_socket(g->fd);
  for ( i = 0; i < NUM_SEATS; i++ ) {
    set_player_name(g->players[i],NULL);
    free((void *)(g->players[i]));
  }
  free(g);
  return NULL;
}

/* close connection and free storage */
Game *client_close(Game *g) {
  return _client_close(g,1);
}

/* free storage, but don't actually close connection */
Game *client_close_keepconnection(Game *g) {
  return _client_close(g,0);
}


/* client_send_packet: send the given packet out on the game fd.
   Return sequence number of packet. */
int client_send_packet(Game *g, PMsgMsg *m) {
  char *l;

  if ( ! g ) {
    warn("client_send_packet: null game");
    return 0;
  }
  l = encode_pmsg(m);
  if ( l == NULL ) {
    /* this shouldn't happen */
    warn("client_send_packet: protocol conversion failed");
    return 0;
  }
  if ( put_line(g->fd,l) < 0 ) {
    warn("client_send_packet: write failed");
    /* maybe we should shutdown the descriptor here? */
    return 0;
  }
  return ++g->cseqno;
}

/* see client.h for spec. */

TileSet *client_find_sets(PlayerP p, Tile d, int mj, PlayerP *pcopies,
			  MJSpecialHandFlags flags) {
  static TileSet ts[14]; /* should be safe, but FIXME */
  static Player copies[14];
  TileSet *tsp = ts;
  Tile t;
  PlayerP cp = copies;
  char seen[MaxTile]; /* to note tile denominations already done */
  int j;

  /* check the discard */
  if ( d != HiddenTile ) {
    /* Can we kong? */
    if ( !mj && player_can_kong(p,d) ) {
      tsp->type = Kong;
      tsp->tile = d;
      tsp++; cp++;
    }

    /* Can we pung it? */
    copy_player(cp,p);
    if ( player_pungs(cp,d) && (!mj || player_can_mah_jong(cp,HiddenTile,flags)) ) {
      tsp->type = Pung;
      tsp->tile = d;
      tsp++; cp++;
    }
    

    /* Can we pair it? */
    copy_player(cp,p);
    if ( player_pairs(cp,d) && (!mj || player_can_mah_jong(cp,HiddenTile,flags)) ) {
      tsp->type = Pair;
      tsp->tile = d;
      tsp++; cp++;
    }

    /* Can we chow it? */
    for ( j = Lower; j <= Upper; j++ ) {
      copy_player(cp,p);
      if ( player_chows(cp,d,j) && (!mj || player_can_mah_jong(cp,HiddenTile,flags))) {
	tsp->type = Chow;
	tsp->tile = make_tile(suit_of(d),value_of(d)-j);
	tsp++; cp++;
      }
    }
  } else {
    /* no discard. Look for closed sets */
    /* Because we should return pungs, then chows, then pairs,
       we have to go through the tiles three times, which is tedious. */
    for (j=0; j < MaxTile; j++) seen[j] = 0;
    for (j=0; j < p->num_concealed; j++) {
      t = p->concealed[j];
      if ( seen[t]++ ) continue; /* don't enter sets twice */
      copy_player(cp,p);
      if ( player_forms_closed_pung(cp,t) && (!mj || player_can_mah_jong(cp,HiddenTile,flags))) {
	tsp->type = ClosedPung;
	tsp->tile = t;
	tsp++; cp++;
      }
    }    
    for (j=0; j < MaxTile; j++) seen[j] = 0;
    for (j=0; j < p->num_concealed; j++) {
      t = p->concealed[j];
      if ( seen[t]++ ) continue; /* don't enter sets twice */
      copy_player(cp,p);
      if ( player_forms_closed_chow(cp,t,Lower) && (!mj || player_can_mah_jong(cp,HiddenTile,flags))) {
	tsp->type = ClosedChow;
	tsp->tile = t;
	tsp++; cp++;
      }
    }    
    for (j=0; j < MaxTile; j++) seen[j] = 0;
    for (j=0; j < p->num_concealed; j++) {
      t = p->concealed[j];
      if ( seen[t]++ ) continue; /* don't enter sets twice */
      copy_player(cp,p);
      if ( player_forms_closed_pair(cp,t) && (!mj || player_can_mah_jong(cp,HiddenTile,flags))) {
	tsp->type = ClosedPair;
	tsp->tile = t;
	tsp++; cp++;
      }
    }
  }

  /* have we found anything? */
  if ( tsp == ts ) return (TileSet *)0;
  
  tsp->type = Empty;
  if ( pcopies ) *pcopies = copies;
  return ts;
}
