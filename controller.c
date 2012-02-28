/* $Header: /home/jcb/MahJong/newmj/RCS/controller.c,v 12.2 2012/02/01 14:10:29 jcb Rel $
 * controller.c
 * This program implements the master controller, which accepts
 * connections from players and runs the game.
 * At present, this is designed around the assumption that it is
 * only running one game. Realistically, I see no immediate need
 * to be more general. However, I trust that nothing in the design
 * would make that awkward.
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/controller.c,v 12.2 2012/02/01 14:10:29 jcb Rel $";

#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "controller.h"
#include "scoring.h"

#include "sysdep.h"

#include "version.h"

/* extra data in players */
typedef struct {
  /* option settings */
  int options[PONumOptions];
  /* this variable is set to one to indicate that the player
     has been disconnected or otherwise out of touch, and therefore needs
     to be told the current state of the game. */
  int disconnected;
  int auth_state; /* authorization state */
#define AUTH_NONE 0
#define AUTH_PENDING 1
#define AUTH_DONE 2
  char *auth_data; /* point to relevant auth data */
  int protversion; /* protocol version supported by this player */
  int localtimeouts; /* 1 if this player has requested LocalTimeouts */
} PlayerExtras;

#define popts(p) ((PlayerExtras *)(p->userdata))->options
#define pextras(p) ((PlayerExtras *)(p->userdata))

/* authorization data (basic only) */
typedef struct {
  int id;
  char passwd[16];
} authinfo;

#define AUTH_NUM 4
authinfo auth_info[AUTH_NUM];

/* this is The Game that we will use */

Game *the_game = NULL;

int num_connected_players;
#define NUM_PLAYERS 4

int first_id = 0; /* id assigned to first player to connect */

/* This array stores information about current connections.
   When a new connection arrives, we use the first free slot.
*/
#define MAX_CONNECTIONS 8
struct {
  int inuse; /* slot in use */
  SOCKET skt; /* the system level socket of this connection. */
  PlayerP player; /* player, if any */
  int seqno; /* sequence number of messages on this connection */
  PMsgMsg *cm; /* stored copy of the player's connect message */
} connections[MAX_CONNECTIONS];


/* This is the fd of the socket on which we listen */
SOCKET socket_fd;

/* This is the master set of fds on which we are listening
   for events. If we're on some foul system that doesn't have
   this type, it is sysdep.h's responsibility to make sure we do.
*/

fd_set event_fds;

/* this is used by auxiliary functions to pass back an error message */
char *failmsg;

/* This is a logfile, if specified */
FILE *logfile = NULL;

/* This is the name of a game option file */
char *optfilename = NULL;

/* forward declarations */
static void despatch_line(int cnx, char *line);
static int close_connection(int cnx);
static int new_connection(SOCKET skt); /* returns new connection number */
static void setup_maps(int cnx, PlayerP p);
static void remove_from_maps(int cnx);
static void send_packet(int cnx,CMsgMsg *m, int logit); /* not much used except in next two */
static void _send_id(int id,CMsgMsg *m, int logit);
static void _send_all(Game *g,CMsgMsg *m);
static void _send_others(Game *g, int id,CMsgMsg *m);
static void _send_seat(Game *g, seats s, CMsgMsg *m);
static void _send_other_seats(Game *g, seats s, CMsgMsg *m);
/* always casting is boring, so ... */
/* send_id logs (if logging is enabled */
#define send_id(id,m) _send_id(id,(CMsgMsg *)(m),1)
#define send_all(g,m) _send_all(g,(CMsgMsg *)(m))
#define send_others(g,id,m) _send_others(g,id,(CMsgMsg *)(m))
#define send_seat(g,s,m) _send_seat(g,s,(CMsgMsg *)(m))
#define send_other_seats(g,s,m) _send_other_seats(g,s,(CMsgMsg *)(m))
static void resend(int id,CMsgMsg *m);
static int load_state(Game *g);
static char *save_state(Game *g, char *filename);
static void save_and_exit(void); /* save if save-on-exit, and quit */
static int load_wall(char *wfname, Game *g);
static int id_to_cnx(int id);
static int cnx_to_id(int cnx);
static void clear_history(Game *g);
static void history_add(Game *g, CMsgMsg *m);

/* N.B. we do not use the game_id_to_player function, since
   we don't always have players in a game.
*/
static PlayerP id_to_player(int id);

/* A convenience definition. */
#define id_to_seat(id) game_id_to_seat(the_game,id)
/* and another */
#define handle_cmsg(g,m) game_handle_cmsg(g,(CMsgMsg *)m)

static int start_hand(Game *g);
static void check_claims(Game *g);
static void send_infotiles(PlayerP p);
static void score_hand(Game *g, seats s);
static void washout(char *reason);
static void handle_cnx_error(int cnx);

static int hand_history = 0; /* dump history file for each hand */
static int loadstate = 0; /* if we have to load state */
/* order in which to seat players */
typedef enum {
  SODefault = 0,
  SORandom = 1,
  SOIdOrder = 2,
} SeatingOrder;
static SeatingOrder seating_order = SODefault;
static int noidrequired = 0; /* allow arbitrary joining on resumption */
static int nomanager = 0; /* forbid managers */
static char *wallfilename = NULL; /*file to load wall from*/
static char loadfilename[1024]; /* file to load game from */
static int debug = 0; /* allow debugging messages */
static int usehist = 1; /* if we keep history to allow resumption */
static int end_on_disconnect = 0; /* end game (gracefully) on disconnect */
/* penalties for disconnection */
static int disconnect_penalty_end_of_round = 0;
static int disconnect_penalty_end_of_hand = 0;
static int disconnect_penalty_in_hand = 0;
static int exit_on_disconnect = 0; /* if we die on losing a player */
static int save_on_exit = 0; /* save on exit other than at end of game */
static int localtimeouts = 0; /* are we using local timeouts ? */

static int game_over = 0; /* we hang around so players can still exchange
			     messages, and quit on first disconnect */

static void usage(char *pname,char *msg) {
  fprintf(stderr,"%s: %s\nUsage: %s [ --server ADDR ]"
"  [ --timeout N ]\n"
"  [ --pause N ]\n"
"  [ --random-seats | --id-order-seats ]\n"
"  [ --debug ]\n"
"  [ --disconnect-penalties N1,N2,N3 ]\n"
"  [ --end-on-disconnect ]\n"
"  [ --exit-on-disconnect ]\n"
"  [ --save-on-exit ]\n"
"  [ --option-file FILE ]\n"
"  [ --load-game FILE ]\n"
"  [ --no-id-required ]\n"
"  [ --auth-basic AUTHINFO ]\n"
"  [ --no-manager ]\n"
"  [ --logfile FILE]\n"
"  [ --hand-history ]\n"
"  [ --no-special-scores ]\n"
"  [ --seed N ]\n"
"  [ --wallfile FILE ]\n"
"  [ --nohist ]\n",
	  pname,msg,pname);
  exit(1);
}

/* This global is used to specify a timeout for claims
   in milliseconds. If it is non-zero on entry to the select
   call, then it will be used as the timeout for select; and if
   data is read before timeout, it will be adjusted appropriately
   for the next call. If a select expires due to timeout, then
   the timeout_handler is called. */
static void timeout_handler(Game *g);
static int timeout = 0;
/* this is the user level timeout in seconds.*/
static int timeout_time = 15;
/* this function extracts the timeout_time from a game
   according the Timeout and TimeoutGrace options, and
   the passed in value of localtimeouts */
static int get_timeout_time(Game *g,int localtimeouts);
/* this is the minimum time between discards and draws,
   keep the game down to human speed */
static int min_time = 0; /* deciseconds */
/* and this is the value requested on the command line */
static int min_time_opt = 0;
/* A player has disconnected, and we're cleaning up the
   mess. Used to keep the game going during MahJonging */
static int end_on_disconnect_in_progress = 0;
/* This is for a hack: if we get two savestate requests while
   a hand is completed, on the second one we save the history of 
   the hand, rather than just the game state. So this variable
   gets incremented when save_state is called in handcomplete,
   and is reset by start_hand.
*/
static int save_state_hack = 0;


int main(int argc, char *argv[]) {
  char *address = ":5000";
  char *logfilename = NULL;
  int seed = 0;
  int nfds; 
  struct timeval timenow, timethen, timetimeout;
  fd_set rfds, efds;
  int i;

  /* clear auth_info */
  for ( i=0; i<AUTH_NUM; i++ ) { 
    auth_info[i].id = 0; 
    auth_info[i].passwd[0] = 0;
  }

  /* options. I should use getopt ... */
  for (i=1;i<argc;i++) {
    if ( strcmp(argv[i],"--nohist") == 0 ) {
      usehist = 0;
    } else if ( strcmp(argv[i],"--debug") == 0 ) {
      debug = 1;
    } else if ( strcmp(argv[i],"--random-seats") == 0 ) {
      seating_order = SORandom;
    } else if ( strcmp(argv[i],"--id-order-seats") == 0 ) {
      seating_order = SOIdOrder;
    } else if ( strcmp(argv[i],"--no-id-required") == 0 ) {
      noidrequired = 1;
    } else if ( strcmp(argv[i],"--no-manager") == 0 ) {
      nomanager = 1;
    } else if ( strcmp(argv[i],"--disconnect-penalties") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --disconnect-penalties");
      sscanf(argv[i],"%d,%d,%d",
	&disconnect_penalty_in_hand,
	&disconnect_penalty_end_of_hand,
	&disconnect_penalty_end_of_round);
    } else if ( strcmp(argv[i],"--end-on-disconnect") == 0 ) {
      end_on_disconnect = 1;
    } else if ( strcmp(argv[i],"--exit-on-disconnect") == 0 ) {
      exit_on_disconnect = 1;
    } else if ( strcmp(argv[i],"--save-on-exit") == 0 ) {
      save_on_exit = 1;
    } else if ( strcmp(argv[i],"--no-special-scores") == 0 ) {
      no_special_scores = 1;
    } else if ( strcmp(argv[i],"--hand-history") == 0 ) {
      hand_history = 1 ;
    } else if ( strcmp(argv[i],"--auth-basic") == 0 ) {
      int j = 0;
      while ( auth_info[j].id != 0 && j < AUTH_NUM ) j++;
      if ( j == AUTH_NUM ) usage(argv[0], "too many --auth-basic's");
      if ( ++i == argc ) usage(argv[0],"missing argument to --auth-basic");
      sscanf(argv[i],"%d:%15s",&auth_info[j].id,auth_info[j].passwd);
    } else if ( strcmp(argv[i],"--address") == 0 ) {
      /* N.B. the --address option is retained only for backward compatibility */
      if ( ++i == argc ) usage(argv[0],"missing argument to --address");
      address = argv[i];
    } else if ( strcmp(argv[i],"--server") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --server");
      address = argv[i];
    } else if ( strcmp(argv[i],"--timeout") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --timeout");
      timeout_time = atoi(argv[i]);
    } else if ( strcmp(argv[i],"--pause") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --pause");
      min_time_opt = min_time = atoi(argv[i]); /* N.B. Time in deciseconds */
    } else if ( strcmp(argv[i],"--seed") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --seed");
      seed = atoi(argv[i]);
    } else if ( strcmp(argv[i],"--logfile") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --logfile");
      logfilename = argv[i];
    } else if ( strcmp(argv[i],"--option-file") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --option-file");
      optfilename = argv[i];
    } else if ( strcmp(argv[i],"--load-game") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --load-game");
      strmcpy(loadfilename,argv[i],1023);
      loadstate = 1;
    } else if ( strcmp(argv[i],"--wallfile") == 0 ) {
      if ( ++i == argc ) usage(argv[0],"missing argument to --wallfile");
      wallfilename = argv[i];
    } else usage(argv[0],"unknown option or argument");
  }

  if ( logfilename ) {
    if ( strcmp(logfilename,"-") == 0 ) {
      logfile = stdout;
    } else {
      logfile = fopen(logfilename,"a");
      if ( ! logfile ) {
	perror("couldn't open logfile");
	exit(1);
      }
    }
  }

  /* we don't want to get SIGPIPE */
  ignore_sigpipe();

  socket_fd = set_up_listening_socket(address);
  if ( socket_fd == INVALID_SOCKET ) {
    printf("FAILED: couldn't set up socket\n");
    perror("couldn't set up listening socket");
    exit(1);
  }
  printf("OK: %s\n",address);
  fflush(stdout);

  /* stick it in the connection list */
  new_connection(socket_fd);
  
  /* seed the random number generator */
  rand_seed(seed);

  /* initialize data */
  num_connected_players = 0;

  /* malloc space for the game and four players.
     We will shortly load the state if we're resuming; if we're not
     resuming, initialization happens later */
  the_game = (Game *) malloc(sizeof(Game));
  if ( the_game == NULL ) {
    warn("Couldn't malloc game structure");
    exit(1);
  }
  memset((void *)the_game,0,sizeof(Game));
  the_game->userdata = malloc(sizeof(GameExtras));
  if ( the_game->userdata == NULL ) {
    warn("Couldn't malloc game extra structure");
    exit(1);
  }
  gextras(the_game)->histcount = gextras(the_game)->prehistcount = 0;
  gextras(the_game)->caller = (PlayerP) malloc(sizeof(Player));
  if ( gextras(the_game)->caller == NULL ) {
    warn("Couldn't malloc game extra structure");
    exit(1);
  }
  gextras(the_game)->completed_rounds = 0;

  for ( i=0 ; i < NUM_SEATS ; i++ ) {
    void *tmp;
    if ( (the_game->players[i] = (PlayerP) malloc(sizeof(Player)))
	 == NULL ) {
      warn("couldn't malloc player structure");
      exit(1);
    }
    memset((void *)the_game->players[i],0,sizeof(Player));
    if ( (tmp = malloc(sizeof(PlayerExtras)))
	 == NULL ) {
      warn("couldn't malloc player options");
      exit(1);
    }
    memset((void *)tmp,0,sizeof(PlayerExtras));
    set_player_userdata(the_game->players[i],tmp);
  }

  /* some game initialization happens here */
  the_game->protversion = PROTOCOL_VERSION; /* may be reduced by players */
  game_set_options_from_defaults(the_game);

  if ( loadstate ) {
    if ( load_state(the_game) == 0 ) {
      warn("Couldn't load the game state");
      loadstate = 0;
    }
  }

  /* enter the main loop */
  while ( 1 ) {
    struct timeval *tvp;
    rfds = efds = event_fds;

    tvp = NULL;
    if ( the_game->active && !the_game->paused && timeout > 0 ) {
      gettimeofday(&timethen,NULL);
      tvp = &timetimeout;
      tvp->tv_sec = timeout/1000; tvp->tv_usec = (timeout%1000)*1000;
    }
      
    nfds = select(32,&rfds,NULL,&efds,tvp); /* n, read, write, except, timeout */

    if ( tvp ) {
      gettimeofday(&timenow,NULL);
      timeout -= (timenow.tv_sec*1000 + timenow.tv_usec/1000)
	- (timethen.tv_sec*1000 + timethen.tv_usec/1000);
    }

    if ( nfds < 0 ) {
      perror("select failed");
      exit(1);
    }

    /* if the timeout has gone negative, that means that something
       is throwing input at us so fast we never get round to cancelling
       the timeout. This probably means something is in a loop. So
       call the timeout handler anyway */

    if ( nfds == 0 || timeout < 0 ) {
      timeout_handler(the_game);
      continue;
    }

    for ( i = 0 ; i < MAX_CONNECTIONS ; i++ ) {
      SOCKET fd;
      if ( ! connections[i].inuse ) continue;
      fd = connections[i].skt;

      if ( FD_ISSET(fd,&efds) ) {
	handle_cnx_error(i);
	continue;
      }
      if ( FD_ISSET(fd,&rfds) ) {
	if ( fd == socket_fd ) {
	  SOCKET newfd;

          newfd = accept_new_connection(fd);
	  if (newfd == INVALID_SOCKET) {
	    warn("failure in accepting connection");
	    exit(1);
	  }
	  /* if we are shutting down after a disconnect,
	     accept no more connections */
	  if ( end_on_disconnect_in_progress ) {
	    CMsgErrorMsg em;
	    em.type = CMsgError;
	    em.seqno = 0;
	    em.error = "Game shutting down; no reconnect allowed";
	    put_line(newfd,encode_cmsg((CMsgMsg *)&em));
	    closesocket(newfd);
	  } else
	  /* add to the connection list */
	  if ( new_connection(newfd) < 0) {
	    CMsgErrorMsg em;
	    em.type = CMsgError;
	    em.seqno = 0;
	    em.error = "Unable to accept new connections";
	    put_line(newfd,encode_cmsg((CMsgMsg *)&em));
	    closesocket(newfd);
	  }
	}
	else {
	  char *line;
	  /* get_line will fail if there is only a partial line
	     available. This is wrong, since it is possible for
	     lines to arrive fragmentedly (not that it should 
	     normally happen). However, it would also be wrong to
	     wait forever for a partial line to complete. There
	     should be a timeout.
	  */
	  /* get_line provides us with a pointer to the text line,
	     which we can mess with as we like, provided we don't
	     expect it to survive the next call to get_line
	  */
	  line = get_line(connections[i].skt); /* errors dealt with by despatch line */
	  /* except that we want to know the error code */
	  if ( line == NULL ) {
	    info("get_line returned null: %s",strerror(errno));
	  }
	  despatch_line(i,line);
	}
      }
    }
  }
}

/* function to act on Player Messages. NB it takes a connection number,
   not an id, since ids may be unassigned.
*/
static void handle_pmsg(PMsgMsg *pmp, int cnx);

/* despatch_line: process the line of input received on cnx. */

static void despatch_line(int cnx, char *line) {
  PMsgMsg *pmp;

  if ( logfile ) {
    fprintf(logfile,"<cnx%d %s",cnx,line);
  }

  if ( line == NULL ) {
    if ( game_over ) exit(0);
    warn("receive error on cnx %d, player id %d\n",cnx, cnx_to_id(cnx));
    handle_cnx_error(cnx);
    return;
  }

  pmp = decode_pmsg(line);
  /* increment the sequence number */
  connections[cnx].seqno++;

  if ( pmp == NULL ) {
    CMsgErrorMsg em;
    em.type = CMsgError;
    em.error = "Protocol error";
    em.seqno = connections[cnx].seqno;
    send_packet(cnx,(CMsgMsg *)&em,1);
    warn("Protocol error on cnx %d, player id %d; ignoring\n",cnx, cnx_to_id(cnx));
    return;
  }

  handle_pmsg(pmp,cnx);
}

/* little function to check that the minimum time has passed 
   since the last tile moving activity.
   The argument multiplies the usual delay time between the current
   call and the next call. It's used to increase the delay after
   claim implementations. bloody emacs ' */
static void check_min_time(float factor) {
  static struct timeval disctime, timenow;
  static int min_time_this_time;
  if ( min_time_this_time > 0 ) {
    int n;
    if ( disctime.tv_sec > 0 ) { /* not firsttime */
      gettimeofday(&timenow,NULL);
      n = (timenow.tv_sec * 1000 + timenow.tv_usec/1000)
	- (disctime.tv_sec * 1000 + disctime.tv_usec/1000);
      n = 100*min_time_this_time - n;
      if ( n > 0 ) usleep(1000*n);
    }
    gettimeofday(&disctime,NULL);
  }
  min_time_this_time = (int)(min_time*factor);
}

static void handle_pmsg(PMsgMsg *pmp, int cnx) 
{

  /* We mustn't act on most requests if the game is not active.
     There's a race possible otherwise: we can suspend the game, and
     meanwhile a player sends a request: we then act on this, but it
     doesn't get into the history records of the suspended players. */
  if ( ! the_game->active ) {
    CMsgErrorMsg em;
    em.type = CMsgError;
    em.error = "Game not active";
    em.seqno = connections[cnx].seqno;
    switch ( pmp->type ) {
    case PMsgSaveState:
    case PMsgLoadState:
    case PMsgConnect:
    case PMsgDisconnect:
    case PMsgAuthInfo:
    case PMsgRequestReconnect:
    case PMsgSetPlayerOption:
    case PMsgSendMessage:
    case PMsgQueryGameOption:
    case PMsgListGameOptions:
      break; /* these are OK */
    default:
      send_packet(cnx,(CMsgMsg *)&em,0);
      return;
    }
  }

  /* check for debugging messages */
  if ( ! debug && pmp->type >= DebugMsgsStart ) {
    CMsgErrorMsg em;
    em.type = CMsgError;
    em.seqno = connections[cnx].seqno;
    em.error = "Debugging not enabled";
    send_packet(cnx,(CMsgMsg *)&em,0);
    return;
  }

  switch ( pmp->type ) {
  case PMsgSaveState:
    { 
      PMsgSaveStateMsg *m = (PMsgSaveStateMsg *)pmp;
      CMsgErrorMsg em;
      PlayerP p; int id; seats seat;
      char *res;
      
      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);
      
      em.type = CMsgError;
      em.seqno = connections[cnx].seqno;
      
      if ( (res = save_state(the_game,m->filename)) ) {
	if ( the_game->protversion >= 1025 ) {
	  CMsgStateSavedMsg ssm;
	  ssm.type = CMsgStateSaved;
	  ssm.id = id;
	  ssm.filename = res;
	  send_all(the_game,&ssm);
	}
      } else {
	em.error = "Unable to save state";
	send_id(id,&em);
      }
      return;
    }
  case PMsgLoadState:
    {
      PMsgLoadStateMsg *m = (PMsgLoadStateMsg *) pmp;
      CMsgErrorMsg em;
      PlayerP p; int id; seats seat;
      
      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);
      
      em.type = CMsgError;
      em.seqno = connections[cnx].seqno;
      em.error = NULL; 

      /* I think we can load state any time before the game starts,
	 and only then */
      if ( protocol_version < 1038 ) {
	em.error = "Not all players support dynamic state loading";
      } else if ( game_has_started(the_game) ) {
	em.error = "Can't load game state when already playing";
      } else if ( m->filename == NULL || m->filename[0] == 0 ) {
	em.error = "No game file specified in LoadState";
      } else {
	int i;
	CMsgPlayerMsg pm;
	loadstate = 1;
	strmcpy(loadfilename,m->filename,1023);
	/* first delete all players from the clients */
	pm.type = CMsgPlayer;
	pm.name = NULL;
	for ( i = 0 ; i < MAX_CONNECTIONS ; i++ ) {
	  if ( connections[i].inuse && connections[i].player ) {
	    pm.id = connections[i].player->id;
	    send_all(the_game,&pm);
	  }
	}
	/* now remove from maps ... */
	for ( i = 0 ; i < MAX_CONNECTIONS ; i++ ) {
	  if ( connections[i].inuse && connections[i].player ) {
	    remove_from_maps(i);
	    num_connected_players--;
	  }
	}
	if ( ! load_state(the_game) ) {
	  em.error = "Loading game state failed";
	}
	the_game->active = 0;
	/* now reprocess connection messages */
	for ( i = 0 ; i < MAX_CONNECTIONS ; i++ ) {
	  if ( connections[i].inuse && connections[i].cm) {
	    handle_pmsg(connections[i].cm,i);
	  }
	}
      }
      if ( em.error ) {
	send_id(id,&em);
      }
      return;
    }
  case PMsgDisconnect:
    {
      close_connection(cnx);
      return;
    }
  case PMsgRequestReconnect:
    {
      CMsgReconnectMsg rm;
      CMsgErrorMsg em;
      PlayerP p; int id; seats seat;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      rm.type = CMsgReconnect;
      em.type = CMsgError;

      /* no reason to refuse, so far */
      
      /* first, stop play */
      { char msg[1024];
	CMsgStopPlayMsg spm;
	spm.type = CMsgStopPlay;
	sprintf(msg,"Player %s requested reconnect - please wait...",
		p->name);
	spm.reason = msg;
	send_all(the_game,&spm);
	the_game->active = 0;
      }

      /* now send the reconnect */
      send_id(id,&rm);

      /* remove player from maps and decrement player count */
      remove_from_maps(cnx);
      num_connected_players--;

      return;
    }
  case PMsgNewAuthInfo:
    {
      CMsgErrorMsg em;
      int id;
 
      id = cnx_to_id(cnx);
 
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = "Password changing not supported";
      send_id(id,&em);
      return;
    }
  case PMsgAuthInfo:
    {
      PMsgAuthInfoMsg *m = (PMsgAuthInfoMsg *) pmp;
      int id;
      PlayerP p = NULL;
      CMsgConnectReplyMsg cm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);

      /* if auth fails, we'll send a negative connection reply.
	 Otherwise, we'll drop through and re-process the connect.
      */
      cm.type = CMsgConnectReply;
      if ( strcmp(m->authdata,pextras(p)->auth_data) != 0 ) {
	cm.reason = "Authentication failure";
	send_packet(cnx,(CMsgMsg *)&cm,1);
	close_connection(cnx);
	return;
      }
      pextras(p)->auth_state = AUTH_DONE;
      /* now fake reprocessing the connect message */
      pmp = connections[cnx].cm;
    }
  case PMsgConnect:
    {
      PMsgConnectMsg *m = (PMsgConnectMsg *) pmp;
      PlayerP p = NULL;
      CMsgConnectReplyMsg cm;
      CMsgPlayerMsg thisplayer;
      CMsgGameMsg gamemsg;
      char *refusal = NULL;
      static int player_id = 0;
      int i;

      /* start filling in reply message */
      cm.type = CMsgConnectReply;
      cm.pvers = PROTOCOL_VERSION;
      if ( m->pvers/1000 != PROTOCOL_VERSION/1000 ) refusal = "Protocol major version mismatch";
      /* we may be seeing the message for the second time, so allocation
	 may already have been done */
      if ( cnx_to_id(cnx) < 0 && num_connected_players == NUM_PLAYERS )
	refusal = "Already have players";
      /* it's reasonable to insist on a name being supplied */
      if ( m->name == NULL ) 
	refusal = "A non-empty name must be given";
      if ( cnx_to_id(cnx) >= 0 && cnx_to_id(cnx) != m->last_id )
        refusal = "You already have a player ID";

      /* If we have a previous game state loaded, we need to associate
	 this player with one in the game. Match on ids */
      if ( loadstate && !refusal ) {
	/* if no id specified, match on names */
	if ( m->last_id != 0 ) {
	  for ( i = 0; i < NUM_SEATS
		  && m->last_id != the_game->players[i]->id ; i++ ) ;
	} else {
	  for ( i = 0; i < NUM_SEATS
		  && (strcmp(m->name,the_game->players[i]->name) != 0) ; i++ ) ;
	}
	/* if no id required, just match this to the first player
	   not currently connected */
	if ( i == NUM_SEATS && noidrequired ) {
	  for ( i = 0; i < NUM_SEATS 
		  && id_to_cnx(the_game->players[i]->id) >= 0 ; i++ );
	}
	if ( i == NUM_SEATS ) {
	  refusal = "Can't find you in the resumed game";
	} else {
	  /* better check that this player isn't already connected */
	  if ( id_to_cnx(m->last_id) >= 0 && id_to_cnx(m->last_id) != cnx )
	    refusal = "Your id is already connected elsewhere";
	  else {
	    p = the_game->players[i];
	    set_player_name(p,m->name); /* not preserved by saving */
	    if ( cnx_to_id(cnx) < 0 ) {
	      setup_maps(cnx,p); /* set up maps between id and cnx etc*/
	      num_connected_players++;
	    }
	  }
	}
      } else if ( ! refusal) { /* not loadstate */
	/* in change from previous versions, honour the last_id field */
	if ( m->last_id ) {
	  /* better check that this player isn't already connected */
	  if ( id_to_cnx(m->last_id) >= 0 && id_to_cnx(m->last_id) != cnx )
	    refusal = "Your id is already connected elsewhere";
	} else {
	  while ( id_to_cnx(++player_id) >= 0 ) { }
	  m->last_id = player_id;
	}
	if ( ! refusal && cnx_to_id(cnx) < 0 ) {
	  /* the second test is because we may already be initialized */
	  p = game_id_to_player(the_game,0); /* get free player structure */
	  if ( p == NULL ) {
	    warn("can't get player structure; exiting");
	    exit(1);
	  }
	  initialize_player(p);
	  num_connected_players++;
	  set_player_id(p,m->last_id);
	  set_player_name(p,m->name);
	  /* store the id of the first human player to connect.
	     This assumes that computer players are called Robot.. */
	  if ( ! first_id 
	    && strncmp(p->name,"Robot",5) != 0 ) first_id = num_connected_players;
	  setup_maps(cnx,p); /* set up maps between id and cnx etc*/
	}
      }
      if (refusal) {
	cm.id = 0;
	cm.reason = refusal;
	send_packet(cnx,(CMsgMsg *)&cm,1);
	close_connection(cnx);
	return;
      }
      /* set the player in case this is second pass */
      if ( ! p ) p = game_id_to_player(the_game,m->last_id);
      /* now do basic auth checking */
      if ( auth_info[0].id && pextras(p)->auth_state != AUTH_DONE ) {
	int j = 0;
	while ( j < AUTH_NUM && auth_info[j].id != p->id ) j++;
	if ( j == AUTH_NUM ) {
	  refusal = "id not authorized for this game";
	} else {
	  pextras(p)->auth_state = AUTH_PENDING;
	  pextras(p)->auth_data = auth_info[j].passwd;
	  if ( pextras(p)->auth_data[0] == 0 )
	    pextras(p)->auth_state = AUTH_DONE;
	} 
      } else { 
	pextras(p)->auth_state = AUTH_DONE; 
      }
      if (refusal) {
	cm.id = 0;
	cm.reason = refusal;
	send_packet(cnx,(CMsgMsg *)&cm,1);
	close_connection(cnx);
	return;
      }

      /* store the protocol version of this player */
      pextras(p)->protversion = m->pvers;

      /* Yes, this is right: the fact that this player is connecting
	 means that it has been disconnected! */
      pextras(p)->disconnected = 1;

      /* keep a copy of the message if not already there */
      if ( connections[cnx].cm == NULL ) {
	connections[cnx].cm = pmsg_deepcopy(pmp);
      }

      if ( pextras(p)->auth_state == AUTH_DONE ) {
	/* send the reply */
	cm.id = p->id;
	cm.reason = NULL;
	send_packet(cnx,(CMsgMsg *)&cm,1);
      } else {
	/* ask for authorization, and then bail out.
	   When we get authorization, we'll come back through
	   all this code again. */
	CMsgAuthReqdMsg cm;
	cm.type = CMsgAuthReqd;
	strcpy(cm.authtype,"basic");
	cm.authdata = NULL;
	send_packet(cnx,(CMsgMsg *)&cm,0); /* don't log this */
	return;
      }

      /* Now we need to tell this player who's already here, and
	 tell the others about this one */
      thisplayer.type = CMsgPlayer;
      thisplayer.id = p->id;
      thisplayer.name = p->name;

      for ( i = 0; i < NUM_SEATS; i++ ) {
	CMsgPlayerMsg pm;
	PlayerP q;

	q = the_game->players[i];

	if ( q->id == 0 ) continue;
	
	if ( q->id == p->id ) continue;

	pm.type = CMsgPlayer;
	pm.id = q->id;
	pm.name = q->name;
	  
	send_packet(cnx,(CMsgMsg *)&pm,1);
	send_id(q->id,&thisplayer); /* fails harmlessly if not connected */
      }

      /* set the protocol version to the greatest supported version.
	 We inspect all players again in case somebody disconnected
	 before the game was set up */
      protocol_version = PROTOCOL_VERSION;
      for ( i = 0; i < NUM_SEATS ; i++ ) {
	PlayerP q = the_game->players[i];
	if ( q->id == 0 ) continue;
	if ( protocol_version > pextras(q)->protversion )
	  protocol_version = pextras(q)->protversion;
      }
      /* if not everybody is connected, just wait for the next */
      if ( num_connected_players < NUM_SEATS ) return;

      /* otherwise, set up the game state (if we aren't 
	 already in the middle of an interrupted game */

      if ( ! loadstate ) {
	GameOptionEntry goe;
	/* set up the seating order */
	switch ( seating_order ) {
	default: break;
	case SORandom:
	  { int i,j,n;
	    PlayerP temp[NUM_SEATS];
	    for (i=0; i<NUM_SEATS; i++) temp[i] = the_game->players[i];
	    for (i=0; i<NUM_SEATS; i++) {
	      /* random number between 0 and seatsleft-1 */
	      n = rand_index(NUM_SEATS-(i+1));
	      /* skip already used */
	      j = 0;
	      while ( (!temp[j]) || j < n ) {
		if ( !temp[j] ) n++;
		j++;
	      }
	      the_game->players[i] = temp[j];
	      temp[j] = NULL;
	    }
	  }
	  break;
	case SOIdOrder:
	  { int i,j; PlayerP t;
	    for ( i = 0 ; i < NUM_SEATS-1 ; i++ ) {
	      if ( the_game->players[i+1]->id < the_game->players[i]->id ) {
		/* sink it to the bottom */
		for ( j = i+1 ; 
		      j > 0 &&
		      the_game->players[j]->id < the_game->players[j-1]->id;
		      j-- ) {
		  t =  the_game->players[j-1]; 
		  the_game->players[j-1] =  the_game->players[j]; 
		  the_game->players[j] = t;
		}
	      }
	    }
	  }
	  break;
	}
	the_game->state = HandComplete;
	the_game->player = noseat;
	the_game->round = EastWind;
	the_game->hands_as_east = 0;
	the_game->firsteast = the_game->players[east]->id;
	/* protocol_version has been maintained during the
	   connection process */
	the_game->protversion = protocol_version;
	the_game->manager = nomanager ? -1 : first_id;
	game_set_options_from_defaults(the_game);
	/* initialize the timeout time from the command line option */
	goe = *game_get_option_entry(the_game,GOTimeout,NULL);
	goe.value.optint = timeout_time;
	game_set_option(the_game,&goe);
	/* And I think we will have the normal dead wall as default,
	   rather than millington style */
	goe = *game_get_option_entry(the_game,GODeadWall16,NULL);
	if ( goe.enabled ) {
	  goe.value.optbool = 0;
	  game_set_option(the_game,&goe);
	}
	/* Now load the game option file, if there is one */
	if ( optfilename ) {
	  FILE *optfile;
	  char buf[1024];
	  optfile = fopen(optfilename,"r");
	  if ( optfile ) {
	    CMsgMsg *m;
	    while ( ! feof(optfile) ) {
	      fgets(buf,1024,optfile);
	      m = decode_cmsg(buf);
	      if ( ! m ) {
		warn("Error decoding game option file entry %s",buf);
	      } else {
		if ( handle_cmsg(the_game,m) < 0 ) {
		  warn("Error applying game option (%s)",the_game->cmsg_err);
		}
		cmsg_deepfree(m);
	      }
	    }
	  } else {
	    warn("couldn't open game option file %s (%s)",
		 optfilename,strerror(errno));
	  }
	}
      }
      the_game->cmsg_check = 1;

      gamemsg.type = CMsgGame;
      gamemsg.east = the_game->players[east]->id;
      gamemsg.south = the_game->players[south]->id;
      gamemsg.west = the_game->players[west]->id;
      gamemsg.north = the_game->players[north]->id;
      gamemsg.round = the_game->round;
      gamemsg.hands_as_east = the_game->hands_as_east;
      gamemsg.firsteast = the_game->firsteast;
      gamemsg.east_score = the_game->players[east]->cumulative_score;
      gamemsg.south_score = the_game->players[south]->cumulative_score;
      gamemsg.west_score = the_game->players[west]->cumulative_score;
      gamemsg.north_score = the_game->players[north]->cumulative_score;
      gamemsg.protversion = the_game->protversion;
      gamemsg.manager = the_game->manager;
      
      /* we only send the game message to the players who
	 have been disconnected. (In the usual case, this
	 is all players...) */

      for ( i=0 ; i < NUM_SEATS; i++ ) {
	PlayerP p = the_game->players[i];
	PMsgListGameOptionsMsg plgo;
	if ( ! pextras(p)->disconnected ) continue;

	send_id(p->id,&gamemsg);
	/* we also have to tell them the game options now, so that
	   they do any necessary setup corrrectly before the new
	   hand message. This is most easily done by faking a 
	   ListOptions request. */
	plgo.type = PMsgListGameOptions;
	plgo.include_disabled = 0;
	handle_pmsg((PMsgMsg *)&plgo,id_to_cnx(p->id));

	/* we don't actually clear the history records
	   until the NewHand message is sent. But we don't
	   want to send history for a complete hand.
	   However, if the game is paused, we need to tell the player so. */
	if ( the_game->state == HandComplete ) {
	  if ( the_game->paused ) {
	    CMsgPauseMsg pm;
	    CMsgPlayerReadyMsg prm;
	    int i;
	    pm.type = CMsgPause;
	    pm.exempt = 0;
	    pm.requestor = 0;
	    pm.reason = the_game->paused;
	    send_id(p->id,&pm);
	    prm.type = CMsgPlayerReady;
	    for ( i = 0; i < NUM_SEATS; i++ ) {
	      if ( the_game->ready[i] ) {
		prm.id = the_game->players[i]->id;
		send_id(p->id,&prm);
	      }
	    }
	  }
	}
      }
      /* now we need to send the history to disconnected players.*/
      if ( the_game->state != HandComplete ) {
	int j;
	if ( ! usehist ) {
	  CMsgErrorMsg em;
	  em.type = CMsgError;
	  em.seqno = connections[cnx].seqno;
	  em.error = "No history kept: resumption not supported";
	  send_all(the_game,&em);
	  warn(em.error);
	  exit(1);
	}
	for ( j=0; j < gextras(the_game)->histcount; j++ ) {
	  int sleeptime;
	  for ( i=0 ; i < NUM_SEATS; i++ ) {
	    PlayerP p = the_game->players[i];
	    if ( ! pextras(p)->disconnected ) continue;
	    resend(p->id,gextras(the_game)->history[j]);
	  }
	  /* this is an undocumented feature to make things 
	     a bit nicer for humans reconnecting: we'll 
	     unilaterally slow down the feed, by adding a 
	     0.15 second delay between items, or 1 second after
	     a claim implementation. */
	  switch ( gextras(the_game)->history[j]->type ) {
	  case CMsgPlayerPairs:
	  case CMsgPlayerChows:
	  case CMsgPlayerPungs:
	  case CMsgPlayerKongs:
	  case CMsgPlayerSpecialSet:
	    sleeptime = 1000; /* ms */
	    break;
	  default:
	    sleeptime = 150;
	  }
	  usleep(sleeptime*1000);
	}
      }
      /* and now mark those players connected again */
      for ( i=0 ; i < NUM_SEATS; i++ ) {
	PlayerP p = the_game->players[i];
	if ( ! pextras(p)->disconnected ) continue;
	pextras(p)->disconnected = 0;
      }

      /* Now we should tell somebody to do something. Namely:
	 In state HandComplete: tell everybody, and start a hand
	 if things aren't paused.
	 In state Dealing: everybody.
	 In Discarded, several players might want to do something,
	 so we don't specify the id.
	 Ditto in MahJonging.
         Otherwise, it's the player.
      */
      {
	CMsgStartPlayMsg sm;
	CMsgPauseMsg pm;
	sm.type = CMsgStartPlay;
	switch (the_game->state) {
	case HandComplete:
	case Dealing:
	case Discarded:
	case MahJonging:
	  sm.id = 0;
	  break;
	default:
	  sm.id = the_game->players[the_game->player]->id;
	  break;
	}
	/* now we should ask the players whether they are ready to
	   continue. We send the pause request *before* making the
	   game active */
	pm.type = CMsgPause;
	pm.reason = (the_game->state == HandComplete)
	  ? "to start hand" : "to continue play";
	pm.exempt = 0;
	pm.requestor = 0;
	handle_cmsg(the_game,&pm);
	send_all(the_game,&pm);
	send_all(the_game,&sm);
	/* and set the game active */
	the_game->active = 1;
      }


      /* now set loadstate */
      loadstate = 1;

      return;
    } /* end of case PMsgConnect */
  case PMsgRequestPause:
    { PMsgRequestPauseMsg *m = (PMsgRequestPauseMsg *)pmp;
      PlayerP p; int id; seats seat;
      CMsgPauseMsg pm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      pm.type = CMsgPause;
      pm.exempt = 0;
      pm.requestor = id;
      pm.reason = m->reason;
      
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      if ( !(the_game->state == Discarding
	     || the_game->state == HandComplete) ) {
	em.error = "Not reasonable to pause at this point";
	send_id(id,&em);
	return;
      }
      
      send_all(the_game,&pm);
      break;
    } /* end of PMsgRequestPauseMsg */
  case PMsgSetPlayerOption:
    {
      PMsgSetPlayerOptionMsg *m = (PMsgSetPlayerOptionMsg *)pmp;
      PlayerP p; int id; seats seat;
      CMsgPlayerOptionSetMsg posm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      posm.type = CMsgPlayerOptionSet;
      posm.option = m->option;
      posm.value = m->value;
      posm.text = m->text;
      posm.ack = 1;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      if ( m->option == (unsigned)-1 ) m->option = POUnknown;
      if ( m->option == POUnknown ) em.error = "Unknown option";
      else switch ( m->option ) {
	/* validity checking  */
	/* boolean options */
      case POInfoTiles:
	if ( m->value < 0 || m->value > 1 ) em.error = "Bad value for InfoTiles";
	break;
      case POLocalTimeouts:
	/* This is a bit messy. As a matter of policy, we only allow
	   a client to do local timeouts if everybody else is too.
	   So we simply remember the requests, unless all have requested,
	   in which case we ack everybody. */
	/* if this is an ack, ignore it. We should only get acks
	   after we've forcibly set things to zero, and then
	   we don't care about the ack. */
	if ( m->ack ) break;
	/* if this is a request to start local timeouts: */
	if ( m->value ) {
	  int i,lt;
	  pextras(p)->localtimeouts = 1;
	  for ( i=0, lt=1; i < NUM_SEATS; i++ ) {
	    lt = (lt && pextras(the_game->players[i])->localtimeouts);
	  }
	  if ( ! lt ) return; /* just wait */
	  localtimeouts = 1;
	  timeout_time = get_timeout_time(the_game,localtimeouts);
	  for ( i=0; i < NUM_SEATS; i++ ) {
	    popts(the_game->players[i])[m->option] = 1;
	  }
	  send_all(the_game,&posm);
	  /* and that's it -- we don't want to drop through */
	  return;
	} else {
	  seats i;
	  /* request to disable local timeouts */
	  localtimeouts = 0;
	  timeout_time = get_timeout_time(the_game,localtimeouts);
	  pextras(p)->localtimeouts = 0;
	  /* instruct all the other players to drop local timeouts */
	  posm.ack = 0;
	  for ( i=0; i < NUM_SEATS; i++ ) {
	    if ( i != seat ) { send_seat(the_game,i,&posm); }
	  }
	  posm.ack = 1;
	  /* now drop through to ack and set this one. */
	}
	break;
	/* non-boolean options */
      case PODelayTime:
	/* players *can* request any positive value */
	if ( m->value < 0 ) em.error = "Bad value for DelayTime";
	break;
      default:
	;
      }
      if ( em.error ) {
	send_id(id,&em);
	return;
      }
      if ( ! m->ack ) { send_id(id,&posm); }
      popts(p)[m->option] = m->value;
      /* action may now be required */
      switch ( m->option ) {
	int i;
      case PODelayTime:
	/* find the highest value requested by any player, or option */
	min_time = min_time_opt;
	for ( i = 0; i < NUM_SEATS; i++ ) {
	  if ( popts(the_game->players[i])[PODelayTime] > min_time )
	    min_time = popts(the_game->players[i])[PODelayTime];
	}
	/* highest reasonable value is 5 seconds */
	if ( min_time > 50 ) min_time = 50;
	break;
      default:
	;
      }
      return;
    }
  case PMsgReady:
    {
      PlayerP p; int id; seats seat;
      CMsgPlayerReadyMsg prm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      prm.type = CMsgPlayerReady;
      prm.id = id;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      /* if the player is already ready, ignore this message */
      if ( the_game->ready[seat] ) return;

      if ( handle_cmsg(the_game,&prm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }

      send_all(the_game,&prm);

      /* if state is handcomplete and no longer pausing, 
	 start the next hand */
      if ( the_game->state == HandComplete && ! the_game->paused ) start_hand(the_game);
      /* and in the discarded or konging state, reinstate the timeout */
      if ( the_game->state == Discarded 
	   || (the_game->state == Discarding && the_game->konging ) )
	timeout = 1000*timeout_time;
      return;
    } /* end of case PMsgReady */
  case PMsgDeclareSpecial:
    {
      PMsgDeclareSpecialMsg *m = (PMsgDeclareSpecialMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerDeclaresSpecialMsg pdsm;
      CMsgPlayerDrawsMsg  pdlm; /* FIXME */
      int res;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      pdlm.id = pdsm.id = id;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      pdsm.type = CMsgPlayerDeclaresSpecial;
      pdlm.type = CMsgPlayerDraws;
      em.error = NULL;

      /* Legality checking is done by handle cmsg, so
	 just set up the cmsg and apply it */
      pdsm.tile = m->tile;

      res = handle_cmsg(the_game,&pdsm);

      if ( res < -1 ) {
	warn("Consistency error: giving up");
	exit(1);
      }
      if ( res < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      check_min_time(1);
      send_all(the_game,&pdsm);
      if ( pdsm.tile == HiddenTile ) {
	/* if we've now started play (i.e. state == Discarding),
	   ask everybody (except east) if they're ready */
	if ( the_game->state == Discarding ) {
	  CMsgPauseMsg pm;
	  pm.type = CMsgPause;
	  pm.reason = "to start play";
	  pm.exempt = the_game->players[east]->id;
	  pm.requestor = 0;
	  if ( handle_cmsg(the_game,&pm) >= 0 ) {
	    send_all(the_game,&pm);
	  } else {
	    warn("Failed to pause at start of play: %s",the_game->cmsg_err);
	  }
	}
	return;
      }
      /* and now draw the replacement */
      if ( game_get_option_value(the_game,GOFlowersLoose,NULL).optbool ) {
	pdlm.tile = game_peek_loose_tile(the_game);
	pdlm.type = CMsgPlayerDrawsLoose;
      } else {
	pdlm.tile = game_peek_tile(the_game);
	pdlm.type = CMsgPlayerDraws;
      }
      if ( pdlm.tile == ErrorTile ) {
	washout(NULL);
	return;
      }

      if ( the_game->state == Discarding ) {
	/* don't do this if declaring specials */
	/* stash a copy of the player, in case it goes mahjong */
	copy_player(gextras(the_game)->caller,p);
      }

      if ( handle_cmsg(the_game,&pdlm) < 0 ) {
	/* error should be impossible */
	warn("Consistency error: giving up");
	exit(1);
      }
      send_id(id,&pdlm);
      pdlm.tile = HiddenTile;
      check_min_time(1);
      send_others(the_game,id,&pdlm);
      send_infotiles(p);
      return;
      assert(0);
    } /* end of case PMsgDeclareSpecial */
  case PMsgDiscard:
    {
      PMsgDiscardMsg *m = (PMsgDiscardMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerDiscardsMsg pdm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pdm.type = CMsgPlayerDiscards;
      pdm.id = id;
      pdm.tile = m->tile;
      pdm.discard = the_game->serial+1;
      pdm.calling = m->calling;
      if ( handle_cmsg(the_game,&pdm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      check_min_time(1);
      send_all(the_game,&pdm);
      send_infotiles(p);
      /* set the timeout */
      timeout = 1000*timeout_time;
      return;
    } /* end of case PMsgDiscard */
  case PMsgNoClaim:
    {
      PMsgNoClaimMsg *m = (PMsgNoClaimMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerDoesntClaimMsg pdcm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pdcm.type = CMsgPlayerDoesntClaim;
      pdcm.id = id;
      pdcm.discard = m->discard;
      pdcm.timeout = 0;

      if ( handle_cmsg(the_game,&pdcm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      /* handle_cmsg ignores noclaims in wrong state, so
	 we need to check it */
      if ( ! (the_game->state == Discarded 
	   || ( (the_game->state == Discarding 
		 || the_game->state == DeclaringSpecials)
		&& the_game->konging ) ) ) return;
      /* acknowledge to the player only */
      send_id(id,&pdcm);
      /* if all claims received, process */
      check_claims(the_game);
      return;
    } /* end of case PMsgNoClaim */
  case PMsgPung:
    {
      PMsgPungMsg *m = (PMsgPungMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      if ( the_game->state != MahJonging ) {
	CMsgPlayerClaimsPungMsg pcpm;
	pcpm.type = CMsgPlayerClaimsPung;
	pcpm.id = id;
	pcpm.discard = m->discard;
	if ( handle_cmsg(the_game,&pcpm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	send_all(the_game,&pcpm);
	/* if all claims received, process */
	check_claims(the_game);
	return;
      } else { /* MahJonging */
	CMsgPlayerPungsMsg ppm;

	ppm.type = CMsgPlayerPungs;
	ppm.id = id;
	ppm.tile = the_game->tile;
	
	if ( handle_cmsg(the_game,&ppm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	check_min_time(1);
	send_all(the_game,&ppm);
	send_infotiles(p);
	/* if that came from a dangerous discard, tell the other players */
	if ( game_flag(the_game,GFDangerousDiscard) ) {
	  CMsgDangerousDiscardMsg ddm;
	  ddm.type = CMsgDangerousDiscard;
	  ddm.id = the_game->players[the_game->supplier]->id;
	  ddm.discard = the_game->serial;
	  ddm.nochoice = game_flag(the_game,GFNoChoice);
	  send_all(the_game,&ddm);
	}
	/* do scoring if we've finished */
	if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
	return;
      }
    } /* end of case PMsgPung */
  case PMsgPair:
    {
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerPairsMsg ppm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      ppm.type = CMsgPlayerPairs;
      ppm.id = id;
      ppm.tile = the_game->tile;

      if ( handle_cmsg(the_game,&ppm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      check_min_time(1);
      send_all(the_game,&ppm);
      send_infotiles(p);
      /* if that came from a dangerous discard, tell the other players */
      if ( game_flag(the_game,GFDangerousDiscard) ) {
	CMsgDangerousDiscardMsg ddm;
	ddm.type = CMsgDangerousDiscard;
	ddm.id = the_game->players[the_game->supplier]->id;
	ddm.discard = the_game->serial;
	ddm.nochoice = game_flag(the_game,GFNoChoice);
	send_all(the_game,&ddm);
      }
      /* do scoring if we've finished */
      if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
      return;
    } /* end of case PMsgPair */
  case PMsgSpecialSet:
    {
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerSpecialSetMsg pssm;
      char tiles[100];
      int i;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pssm.type = CMsgPlayerSpecialSet;
      pssm.id = id;
      pssm.tile = the_game->tile;
	
      tiles[0] = '\000';
      for ( i = 0; i < p->num_concealed; i++ ) {
	if ( i > 0 ) strcat(tiles," ");
	strcat(tiles,tile_code(p->concealed[i]));
      }
      pssm.tiles = tiles;

      if ( handle_cmsg(the_game,&pssm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      send_all(the_game,&pssm);
      send_infotiles(p);
      /* do scoring if we've finished */
      if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
      return;
    } /* end of case PMsgSpecialSet */
  case PMsgFormClosedPair:
    {
      PMsgFormClosedPairMsg *m = (PMsgFormClosedPairMsg *) pmp;
      CMsgPlayerFormsClosedPairMsg pfcpm;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pfcpm.type = CMsgPlayerFormsClosedPair;
      pfcpm.id = id;
      pfcpm.tile = m->tile;

      if ( handle_cmsg(the_game,&pfcpm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      check_min_time(1);
      send_all(the_game,&pfcpm);
      send_infotiles(p);
      /* do scoring if we've finished */
      if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
      return;
    } /* end of case PMsgFormClosedPair */
  case PMsgFormClosedSpecialSet:
    {
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerFormsClosedSpecialSetMsg pfcssm;
      char tiles[100];
      int i;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pfcssm.type = CMsgPlayerFormsClosedSpecialSet;
      pfcssm.id = id;
      tiles[0] = '\000';
      for ( i = 0; i < p->num_concealed; i++ ) {
	if ( i > 0 ) strcat(tiles," ");
	strcat(tiles,tile_code(p->concealed[i]));
      }
      pfcssm.tiles = tiles;

      if ( handle_cmsg(the_game,&pfcssm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      check_min_time(1);
      send_all(the_game,&pfcssm);
      send_infotiles(p);
      /* do scoring if we've finished */
      if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
      return;
    } /* end of case PMsgFormClosedSpecialSet */
  case PMsgFormClosedPung:
    {
      PMsgFormClosedPungMsg *m = (PMsgFormClosedPungMsg *) pmp;
      CMsgPlayerFormsClosedPungMsg pfcpm;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pfcpm.type = CMsgPlayerFormsClosedPung;
      pfcpm.id = id;
      pfcpm.tile = m->tile;

      if ( handle_cmsg(the_game,&pfcpm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }

      check_min_time(1);
      send_all(the_game,&pfcpm);
      send_infotiles(p);
      /* do scoring if we've finished */
      if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
      return;
    } /* end of case PMsgFormClosedPung */
  case PMsgFormClosedChow:
    {
      PMsgFormClosedChowMsg *m = (PMsgFormClosedChowMsg *) pmp;
      CMsgPlayerFormsClosedChowMsg pfccm;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pfccm.type = CMsgPlayerFormsClosedChow;
      pfccm.id = id;
      pfccm.tile = m->tile;
	
      if ( handle_cmsg(the_game,&pfccm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }

      check_min_time(1);
      send_all(the_game,&pfccm);
      send_infotiles(p);
      /* do scoring if we've finished */
      if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
      return;
    } /* end of case PMsgFormClosedChow */
  case PMsgKong:
    {
      PMsgKongMsg *m = (PMsgKongMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgPlayerClaimsKongMsg pckm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pckm.type = CMsgPlayerClaimsKong;
      pckm.id = id;
      pckm.discard = m->discard;
      if ( handle_cmsg(the_game,&pckm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      send_all(the_game,&pckm);
      /* if all claims received, process */
      check_claims(the_game);
      return;
    } /* end of case PMsgKong */
  case PMsgChow:
    {
      PMsgChowMsg *m = (PMsgChowMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgPlayerClaimsChowMsg pccm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pccm.type = CMsgPlayerClaimsChow;
      pccm.id = id;
      pccm.discard = m->discard;
      pccm.cpos = m->cpos;

      if ( the_game->state == Discarded ) {
	/* special case: if chowpending is set, then this claim should
	   be to specify the previously unspecified position
	   after we've told the player that its claim has
	   succeeded.
	   So implement the chow and announce */
	if ( the_game->chowpending ) {
	  CMsgPlayerChowsMsg pcm;
	  
	  pcm.type = CMsgPlayerChows;
	  pcm.id = id;
	  pcm.tile = the_game->tile;
	  pcm.cpos = m->cpos;
	  if ( handle_cmsg(the_game,&pcm) < 0 ) {
	    em.error = the_game->cmsg_err;
	    send_id(id,&em);
	    return;
	  }
	  check_min_time(1);
	  send_all(the_game,&pcm);
	  return;
	}
	if ( handle_cmsg(the_game,&pccm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	send_all(the_game,&pccm);
	/* if all claims received, process */
	check_claims(the_game);
	return;
      } else { /* This had better be mahjonging */
	CMsgPlayerChowsMsg pcm;

	pcm.type = CMsgPlayerChows;
	pcm.id = id;
	pcm.tile = the_game->tile;
	pcm.cpos = m->cpos;
	if ( handle_cmsg(the_game,&pcm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	/* if any pos, just tell the player */
	if ( m->cpos == AnyPos ) {
	  send_id(id,&pcm);
	} else {
	  check_min_time(1);
	  send_all(the_game,&pcm);
	  send_infotiles(p);
	  /* if that came from a dangerous discard, tell the other players */
	  if ( game_flag(the_game,GFDangerousDiscard) ) {
	    CMsgDangerousDiscardMsg ddm;
	    ddm.type = CMsgDangerousDiscard;
	    ddm.id = the_game->players[the_game->supplier]->id;
	    ddm.discard = the_game->serial;
	    ddm.nochoice = game_flag(the_game,GFNoChoice);
	    send_all(the_game,&ddm);
	  }
	  if ( pflag(p,HandDeclared) ) score_hand(the_game,seat);
	}
	return;
      }
    } /* end of case PMsgChow */
  case PMsgDeclareWashOut:
    {
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      /* at present, we don't have any rules allowing a player
	 to declare a washout */
      em.error = "Can't declare a Wash-Out";
      send_id(id,&em);
      return;
    } /* end of case PMsgDeclareWashOut */
  case PMsgMahJong:
    {
      PMsgMahJongMsg *m = (PMsgMahJongMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgPlayerClaimsMahJongMsg pcmjm;
      CMsgPlayerMahJongsMsg pmjm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;
      pcmjm.type = CMsgPlayerClaimsMahJong;
      pcmjm.id = id;
      pmjm.type = CMsgPlayerMahJongs;
      pmjm.id = id;

      if ( the_game->state == Discarded
	   || ((the_game->state == Discarding
		|| the_game->state == DeclaringSpecials)
	       && the_game->konging ) ) {
	pcmjm.discard = m->discard;
	if ( handle_cmsg(the_game,&pcmjm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	send_all(the_game,&pcmjm);
	/* if all claims received, process */
	check_claims(the_game);
	return;
      } else { /* this should be discarding */
	pmjm.tile = the_game->tile; /* that's the tile drawn */
	if ( handle_cmsg(the_game,&pmjm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	send_all(the_game,&pmjm);
	/* the players will now start declaring their hands */
	return;
      }
    } /* end of case PMsgMahJong */
  case PMsgDeclareClosedKong:
    {
      PMsgDeclareClosedKongMsg *m = (PMsgDeclareClosedKongMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerDeclaresClosedKongMsg pdckm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      pdckm.id = id;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      pdckm.type = CMsgPlayerDeclaresClosedKong;
      em.error = NULL;

      pdckm.tile = m->tile;
      pdckm.discard = the_game->serial+1;

      if ( handle_cmsg(the_game,&pdckm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }

      check_min_time(1);
      send_all(the_game,&pdckm);
      send_infotiles(p);

      /* now we need to wait for people to try to rob the kong */
      timeout = 1000*timeout_time;
      return;

    } /* end of case PMsgDeclareClosedKong */
  case PMsgAddToPung:
    {
      PMsgAddToPungMsg *m = (PMsgAddToPungMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerAddsToPungMsg patpm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      patpm.id = id;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      patpm.type = CMsgPlayerAddsToPung;
      em.error = NULL;

      patpm.tile = m->tile;
      patpm.discard = the_game->serial+1;
      if ( handle_cmsg(the_game,&patpm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }

      check_min_time(1);
      send_all(the_game,&patpm);
      send_infotiles(p);

      /* now we need to wait for people to try to rob the kong */
      timeout = 1000*timeout_time;
      return;

      assert(0);
    } /* end of case PMsgAddToPung */
  case PMsgQueryMahJong:
    {
      PMsgQueryMahJongMsg *m = (PMsgQueryMahJongMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgCanMahJongMsg cmjm;
      MJSpecialHandFlags mjf;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      cmjm.type = CMsgCanMahJong;
      cmjm.tile = m->tile;
      mjf = 0;
      if ( (int)game_get_option_value(the_game,GOSevenPairs,NULL).optbool )
	mjf |= MJSevenPairs;
      cmjm.answer = player_can_mah_jong(p,m->tile,mjf);
      send_id(id,&cmjm);
      return;
    }
  case PMsgShowTiles:
    {
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgPlayerShowsTilesMsg pstm;

      id = cnx_to_id(cnx);
      p = id_to_player(id);
      seat = id_to_seat(id);

      /* fill in basic fields of possible replies */
      pstm.id = id;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      pstm.type = CMsgPlayerShowsTiles;
      em.error = NULL;

      /* if there are no concealed tiles, just ignore the message;
	 this can only happen from the mahjonging player or by duplication */
      if ( p->num_concealed > 0 ) {
	char tiles[100];
	int i;
	
	tiles[0] = '\000';
	for ( i = 0; i < p->num_concealed; i++ ) {
	  if ( i > 0 ) strcat(tiles," ");
	  strcat(tiles,tile_code(p->concealed[i]));
	}
	pstm.tiles = tiles;
	if ( handle_cmsg(the_game,&pstm) < 0 ) {
	  em.error = the_game->cmsg_err;
	  send_id(id,&em);
	  return;
	}
	check_min_time(1);
	send_all(the_game,&pstm);
	score_hand(the_game,seat);
	return;
      }
      return;
    } /* end of case PMsgShowTiles */
  case PMsgSetGameOption:
    {
      PMsgSetGameOptionMsg *m = (PMsgSetGameOptionMsg *)pmp;
      PlayerP p; int id;
      CMsgErrorMsg em;
      CMsgGameOptionMsg gom;
      GameOptionEntry *goe;

      id = cnx_to_id(cnx);

      /* fill in basic fields of possible replies */
      gom.type = CMsgGameOption;
      gom.id = id;
      p = id_to_player(id);
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      /* do we have the option ? */
      goe = game_get_option_entry(the_game,GOUnknown,m->optname);

      if ( ! goe ) {
	em.error = "Trying to set unknown option";
	send_id(id,&em);
	return;
      }
      /* if the option is actually unknown or end, ignore it */
      if ( goe->option == GOUnknown || goe->option == GOEnd ) break;

      if ( goe->enabled == 0 ) {
	em.error = "Option not available in this game";
	send_id(id,&em);
	return;
      }

      gom.optentry = *goe;

      switch ( goe->type ) {
      case GOTBool:
	if ( sscanf(m->optvalue,"%d",&gom.optentry.value.optbool) == 0 ) {
	  em.error = "No boolean value found for option";
	  send_id(id,&em);
	  return;
	}
	if ( gom.optentry.value.optbool != 0 
	     && gom.optentry.value.optbool != 1 ) {
	  em.error = "No boolean value found for option";
	  send_id(id,&em);
	  return;
	}
	break;
      case GOTNat:
	if ( sscanf(m->optvalue,"%u",&gom.optentry.value.optnat) == 0 ) {
	  em.error = "No integer value found for option";
	  send_id(id,&em);
	  return;
	}
	break;
      case GOTInt:
	if ( sscanf(m->optvalue,"%d",&gom.optentry.value.optint) == 0 ) {
	  em.error = "No integer value found for option";
	  send_id(id,&em);
	  return;
	}
	break;
      case GOTScore:
	if ( sscanf(m->optvalue,"%d",&gom.optentry.value.optscore) == 0 ) {
	  em.error = "No score value found for option";
	  send_id(id,&em);
	  return;
	}
	break;
      case GOTString:
	strmcpy(gom.optentry.value.optstring,
            m->optvalue,
            sizeof(gom.optentry.value));
	break;
      }

      /* specific validity checking not done by handle_cmsg */
      if ( gom.optentry.option == GOTimeout
	   && gom.optentry.value.optint < 0 ) {
	em.error = "Can't set negative timeout!";
	send_id(id,&em);
	return;
      }

      if ( gom.optentry.option == GONumRounds
	&& ( gom.optentry.value.optnat == 0
	  || gom.optentry.value.optnat == 3
	  || (gom.optentry.value.optnat > 4
	    && gom.optentry.value.optnat % 4 != 0) ) ) {
	em.error = "Number of rounds must be 1, 2 or a multiple of 4";
	send_id(id,&em);
	return;
      }
      if ( handle_cmsg(the_game,&gom) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
      }
      send_all(the_game,&gom);
      if ( gom.optentry.option == GOTimeout
	   || gom.optentry.option == GOTimeoutGrace ) {
	timeout_time = get_timeout_time(the_game,localtimeouts);
      }
      break;
    }
  case PMsgQueryGameOption:
    {
      PMsgQueryGameOptionMsg *m = (PMsgQueryGameOptionMsg *)pmp;
      int id;
      CMsgErrorMsg em;
      CMsgGameOptionMsg gom;
      GameOptionEntry *goe;

      id = cnx_to_id(cnx);

      /* fill in basic fields of possible replies */
      gom.type = CMsgGameOption;
      gom.id = 0;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      goe = game_get_option_entry(the_game,GOUnknown,m->optname);
      if ( goe == NULL ) {
	em.error = "Option not known";
	send_id(id,&em);
	return;
      }
      gom.optentry = *goe;
      send_id(id,&gom);
      break;
    }
  case PMsgListGameOptions:
    {
      PMsgListGameOptionsMsg *m = (PMsgListGameOptionsMsg *)pmp;
      int id;
      CMsgGameOptionMsg gom;
      GameOptionEntry *goe;
      unsigned int i;

      id = cnx_to_id(cnx);

      /* fill in basic fields of possible replies */
      gom.type = CMsgGameOption;
      gom.id = 0;

      /* this relies on the fact that we know our option table
	 contains only known options in numerical order. This
	 would not necessarily be the case for clients, but it
	 is for us, since we don't allow unknown options to be set */
      for ( i = 1 ; i <= GOEnd ; i++ ) {
	goe = &the_game->option_table.options[i];
	if ( !goe->enabled && ! m->include_disabled ) continue;
	gom.optentry = *goe;
	send_id(id,&gom);
      }
      break;
    }
  case PMsgChangeManager:
    {
      PMsgChangeManagerMsg *m = (PMsgChangeManagerMsg *)pmp;
      int id;
      CMsgChangeManagerMsg cmm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);

      /* fill in basic fields of possible replies */
      cmm.type = CMsgChangeManager;
      cmm.id = id;
      cmm.manager = m->manager;
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      if ( handle_cmsg(the_game,&cmm) < 0 ) {
	em.error = the_game->cmsg_err;
	send_id(id,&em);
	return;
      }
      send_all(the_game,&cmm);
      break;
    }
  case PMsgSendMessage:
    {
      PMsgSendMessageMsg *m = (PMsgSendMessageMsg *)pmp;
      int id;
      CMsgMessageMsg mm;
      CMsgErrorMsg em;

      id = cnx_to_id(cnx);

      mm.type = CMsgMessage;
      mm.sender = id;
      mm.addressee = m->addressee;
      mm.text = m->text;
      em.type = CMsgError;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      if ( mm.addressee == 0 ) {
	send_all(the_game,&mm);
      } else {
	if ( id_to_cnx(mm.addressee) < 0 ) {
	  em.error = "Addressee not found in game";
	  send_id(id,&em);
	  return;
	} else {
	  send_id(mm.addressee,&mm);
	}
      }
      break;
    }
  case PMsgSwapTile:
    {
      PMsgSwapTileMsg *m = (PMsgSwapTileMsg *) pmp;
      PlayerP p; int id; seats seat;
      CMsgErrorMsg em;
      CMsgSwapTileMsg stm;
      int i;

      id = cnx_to_id(cnx);

      /* fill in basic fields of possible replies */
      stm.type = CMsgSwapTile;
      stm.id = m->id;
      stm.oldtile = m->oldtile;
      stm.newtile = m->newtile;
      p = id_to_player(stm.id);
      seat = id_to_seat(stm.id);
      em.type = CMsgError ;
      em.seqno = connections[cnx].seqno;
      em.error = NULL;

      if ( p == NULL ) {
	em.error = "SwapTile: no such player";
	send_id(id,&em);
	return;
      }

      /* find the new tile in the wall. Because this is only used
	 for debugging and testing, we don't care that we're diving
	 inside the game structure!
	 Look for the tile from the end, so as to avoid depleting
	 the early wall.
      */
      for ( i = the_game->wall.dead_end-1 ;
	    the_game->wall.tiles[i] != m->newtile 
	      && i >= the_game->wall.live_used ;
	    i-- ) ;
      if ( i < the_game->wall.live_used ) em.error = "No new tile in wall";
      else {
	if ( handle_cmsg(the_game,&stm) < 0 ) {
	  em.error = the_game->cmsg_err;
	} else {
	  send_id(stm.id,&stm);
	  the_game->wall.tiles[i] = stm.oldtile;
	}
      }
      if ( em.error )
	send_id(id,&em);
      return;
    } /* end of case PMsgSwapTile */
  }
}



/* setup_maps: we need to be able to map between connections,
   player structures, and player ids.
   player to id is in the player structure, so we need the others.
   It's a pity we can't declare private variables here.
*/

/* set up data for a new connection */
static int new_connection(SOCKET skt) {
  int i;

  for (i= 0; i < MAX_CONNECTIONS; i++) {
    if ( connections[i].inuse ) continue;
    connections[i].inuse = 1;
    connections[i].skt = skt;
    connections[i].player = NULL;
    connections[i].seqno = 0;
    /* add to the event set */
    FD_SET(skt,&event_fds);
    return i;
  }
  warn("No space for new connection");
  return -1;
}

/* close connection */
static int close_connection(int cnx) {
  if ( connections[cnx].player ) {
    /* clear maps and decrement counter */
    remove_from_maps(cnx);
    num_connected_players--;
  }
  FD_CLR(connections[cnx].skt,&event_fds);
  closesocket(connections[cnx].skt);
  connections[cnx].inuse = 0;
  return 1;
}
static int cnx_to_id(int cnx) {
  PlayerP p;

  p = connections[cnx].player;
  return p ? p->id : -1;
}

static int id_to_cnx(int id) {
  int i;

  i = 0;
  while ( i < MAX_CONNECTIONS ) {
    if ( connections[i].inuse
	 && connections[i].player
	 && connections[i].player->id == id ) return i;
    i++;
  }
  return -1;
}

static PlayerP id_to_player(int id) {
  int f;

  f = id_to_cnx(id);
  if ( f < 0 ) return NULL;
  return connections[f].player;
}

static void setup_maps(int cnx, PlayerP p) {
  connections[cnx].player = p;
}

static void remove_from_maps(int cnx) {
  /* clear authorization data */
  pextras(connections[cnx].player)->auth_state = AUTH_NONE;
  connections[cnx].player = (PlayerP) 0;
}

/* send_packet: send the given packet out on the given cnx, perhaps logging */
static void send_packet(int cnx,CMsgMsg *m, int logit) {
  char *l;

  if ( cnx < 0 ) return;

  l = encode_cmsg(m);
  if ( l == NULL ) {
    /* this shouldn't happen */
    warn("send_packet: protocol conversion failed");
    /* in fact, it so much shouldn't happen that we'll dump core */
    assert(0);
    return;
  }
  if ( logit && logfile ) {
    fprintf(logfile,">cnx%d %s",cnx,l);
    fflush(logfile);
  }
  if ( put_line(connections[cnx].skt,l) < 0 ) {
    warn("send_packet: write on cnx %d failed",cnx);
    /* maybe we should shutdown the descriptor here? */
    return;
  }
}

/* send player id a packet. Maybe log it.
   Enter the packet into the player's history, if appropriate */
static void _send_id(int id,CMsgMsg *m,int logit) {
  int cnx = id_to_cnx(id);
  if ( cnx < 0 ) return;
  send_packet(cnx,m,logit);
  if ( usehist && logit && the_game->active ) {
    history_add(the_game,m);
  } 
}

/* send to all players in a game, logging one copy */
static void _send_all(Game *g,CMsgMsg *m) {
  _send_id(g->players[east]->id,m,1);
  _send_id(g->players[south]->id,m,0);
  _send_id(g->players[west]->id,m,0);
  _send_id(g->players[north]->id,m,0);
}

/* send to all players in a game EXCEPT the id. Don't log it. */
static void _send_others(Game *g, int id,CMsgMsg *m) {
  if ( g->players[east]->id != id ) _send_id(g->players[east]->id,m,0);
  if ( g->players[south]->id != id ) _send_id(g->players[south]->id,m,0);
  if ( g->players[west]->id != id ) _send_id(g->players[west]->id,m,0);
  if ( g->players[north]->id != id ) _send_id(g->players[north]->id,m,0);
}

/* send to a particular seat in a game. Logit. */
static void _send_seat(Game *g, seats s, CMsgMsg *m) {
  _send_id(g->players[s]->id,m,1);
}

/* send to seats other than s.
   Don't log it: a more informative copy will have gone to
   another player. */
static void _send_other_seats(Game *g, seats s, CMsgMsg *m) {
  seats i;
  for ( i = 0 ; i < NUM_SEATS ; i++ )
    if ( i != s ) _send_id(g->players[i]->id,m,0);
}


/* finish the game: send GameOver message, and print scores */
static void finish_game(Game *g) {
  seats i,j;
  CMsgGameOverMsg gom;
  gom.type = CMsgGameOver;
  send_all(g,&gom);
  fprintf(stdout,"Game over. Final scores:\n");
  /* May as well print these in the order of original easts */
  for (i=0;i<NUM_SEATS;i++) {
    j = (i+1)%NUM_SEATS;
    fprintf(stdout,"%5d (%-20s)  %5d\n",
      g->players[j]->id,
      g->players[j]->name,
      g->players[j]->cumulative_score);
  }
#ifdef WIN32
  sleep(10); /* give the client time to close the connection */
#endif
  game_over = 1;
  the_game->active = 0;
}

/* start_hand: start a new hand.
   The game should be in state HandComplete.
   Send a NewHand message to each player.
   Shuffle and build the new wall.
   Deal the initial tiles to each player.
   Draw east's 14th tile.
   Enter state DeclaringSpecials, with player set to east.

   Return 1 on success.
   On error, return 0, and put human readable explanation in
   global variable failmsg.
*/

static int start_hand(Game *g) {
  int stack,wall;
  CMsgNewHandMsg cm;

  cm.type = CMsgNewHand;

  if ( g->state != HandComplete ) {
    failmsg = "start_hand called when hand not complete.";
    return 0;
  }

  save_state_hack = 0; /* see explanation by variable declaration */

  cm.east = g->players[east]->id; /* assume no change */

  /* Does the deal rotate? */
  if ( g->player != noseat /* there was a mah jong */
       && (g->player != east /* and either it wasn't east */
	   || g->hands_as_east == 13)) { /* or this was east's 13th win */
    
    /* does the round change? (the current south will be the next east;
       if that is the firsteast, the round changes) */
    if ( g->players[south]->id == g->firsteast ) {
      CMsgNewRoundMsg nrm;
      
      /* with num_rounds code, this is more complex. Don't duplicate */
#if 0
      if ( g->round == NorthWind ) {
	assert(0); /* this should have been caught earlier, now */
	finish_game(g);
      }
#endif
      nrm.round = next_wind(g->round);
      nrm.type = CMsgNewRound;
      handle_cmsg(g,&nrm);
      gextras(g)->completed_rounds++;
      send_all(g,&nrm);
    }
    /* rotate the seats. The new east is the current south */
    cm.east = g->players[south]->id;
  }

  /* we haven't yet processed the newhand message */

  clear_history(g);

  /* now set up the prehistory: game message and options */
  {
    CMsgGameMsg gamemsg;
    CMsgGameOptionMsg gom;
    GameOptionEntry *goe;
    int i;
 
    gamemsg.type = CMsgGame;
    gamemsg.east = g->players[east]->id;
    gamemsg.south = g->players[south]->id;
    gamemsg.west = g->players[west]->id;
    gamemsg.north = g->players[north]->id;
    gamemsg.round = g->round;
    gamemsg.hands_as_east = g->hands_as_east;
    gamemsg.firsteast = g->firsteast;
    gamemsg.east_score = g->players[east]->cumulative_score;
    gamemsg.south_score = g->players[south]->cumulative_score;
    gamemsg.west_score = g->players[west]->cumulative_score;
    gamemsg.north_score = g->players[north]->cumulative_score;
    gamemsg.protversion = g->protversion;
    gamemsg.manager = g->manager;

    gextras(g)->prehistory[gextras(g)->prehistcount++] = cmsg_deepcopy((CMsgMsg *)&gamemsg);
   
    gom.type = CMsgGameOption;
    gom.id = 0;
  
    /* this relies on the fact that we know our option table
       contains only known options in numerical order. This
       would not necessarily be the case for clients, but it
       is for us, since we don't allow unknown options to be set */
    for ( i = 1 ; i <= GOEnd ; i++ ) {
      goe = &g->option_table.options[i];
      gom.optentry = *goe;
      gextras(g)->prehistory[gextras(g)->prehistcount++] = cmsg_deepcopy((CMsgMsg *)&gom);
    }
  }

  /* process and send a new hand msg */
  if ( wallfilename ) {
    if ( load_wall(wallfilename,g) == 0 ) {
      warn("Unable to load wall");
      exit(1);
    }
  } else {
    /* set up the wall. Should this be done by the game routine?
       I think not, on the whole. */
    random_tiles(g->wall.tiles,game_get_option_value(g,GOFlowers,NULL).optbool);
  }
  /* we're also supposed to choose the place where the deal starts.
     Just for fun, we'll follow the real procedure. */
  wall = 1+rand_index(5) + 1+rand_index(5);
  stack = wall + 1+rand_index(5) + 1+rand_index(5);
  stack++; /* the dice give the end of the dead wall */
  if ( stack > (g->wall.size/NUM_SEATS)/2 ) {
    stack -= (g->wall.size/NUM_SEATS)/2;
    wall++;
  }

  cm.start_wall = (wall-1) % NUM_SEATS;
  cm.start_stack = stack -1; /* convert from 1 counting to 0 counting */
  if ( handle_cmsg(g,&cm) < 0 ) {
    warn("handle_cmsg of NewHand message return error; dumping core");
    abort();
    }
  send_all(g,&cm);
  
  /* deal out the tiles into deal messages.
     We will actually deal in the traditional way: if we ever get
     round to implementing a semi-random shuffle, instead of a really
     random re-deal, this will make a difference.
  */
  { CMsgPlayerDrawsMsg pdm;
  int i,j;
  seats s;
  pdm.type = CMsgPlayerDraws;

  for ( j=0; j < 3; j++ ) {
    for ( s=east; s<=north; s++) {
      check_min_time(1);
      pdm.id = g->players[s]->id;
      for ( i = 0; i < 4; i++ ) {
	pdm.tile = game_draw_tile(g);
	send_id(pdm.id,&pdm);
	player_draws_tile(g->players[s],pdm.tile);
	pdm.tile = HiddenTile;
	send_others(g,pdm.id,&pdm);
      }
    }
  }
  for ( s=east; s<=north; s++) {
    check_min_time(1);
    pdm.id = g->players[s]->id;
    pdm.tile = game_draw_tile(g);
    send_id(pdm.id,&pdm);
    player_draws_tile(g->players[s],pdm.tile);
    pdm.tile = HiddenTile;
    send_others(g,pdm.id,&pdm);
  }
  /* at this point, we should copy east to the caller slot in case
     it wins with heaven's blessing; otherwise scoring.c will be
     unhappy. (Yes, this happened.) */
  copy_player(gextras(g)->caller,g->players[east]);

  s=east;
  pdm.id = g->players[s]->id;
  pdm.tile = game_draw_tile(g);
  check_min_time(1);
  send_id(pdm.id,&pdm);
  player_draws_tile(g->players[s],pdm.tile);
  pdm.tile = HiddenTile;
  send_others(g,pdm.id,&pdm);
  }

  /* send out info messages */
  send_infotiles(g->players[east]);
  send_infotiles(g->players[south]);
  send_infotiles(g->players[west]);
  send_infotiles(g->players[north]);

  /* Enter the next state and return */
  g->state = DeclaringSpecials;
  g->player = east;
  return 1;
}



/* timeout_handler: force no claims to be sent, and
   then check claims */
static void timeout_handler(Game *g) {
  seats i;
  CMsgPlayerDoesntClaimMsg pdcm;

  if ( g->state != Discarded
       && ! ( g->state == Discarding && g->konging ) ) {
    warn("timeout_handler called in unexpected state");
    return;
  }

  pdcm.type = CMsgPlayerDoesntClaim;
  pdcm.discard = g->serial;
  pdcm.timeout = 1;
  for ( i=0; i < NUM_SEATS; i++ ) {
    if ( i == g->player ) continue;
    if ( g->claims[i] != UnknownClaim ) continue;
    pdcm.id = g->players[i]->id;
    handle_cmsg(g,&pdcm);
    send_id(pdcm.id,&pdcm);
  }
  check_claims(g);
}


/* check_claims: when a player makes a claim on the discard,
   (or tries to rob a kong)
   we need to check whether all claims have been lodged, and
   if so, process them. This function does that */

static void check_claims(Game *g) {
  seats i, s, ds;
  CMsgClaimDeniedMsg cdm;

  if ( g->state != Discarded
       && ! ( (g->state == Discarding 
	       || g->state == DeclaringSpecials) 
	      && g->konging ) ) {
    warn("check_claims: called in wrong game state");
    return;
  }

  /* ds is the seat of the discarder/konger */
  ds = g->player;

  /* return if not all claims are lodged */
  for ( i = 0 ; i < NUM_SEATS ; i++ )
    if ( i != ds && g->claims[i] == UnknownClaim ) return;

  /* if we are in a chowpending state, return silently now: it must be
     a duplicate claim that triggered this */
  if ( g->chowpending ) {
    info("check_claim: Duplicate claim received, ignoring");
    return;
  }

  /* OK, process. First, cancel timeout */
  timeout = 0;

  /* s will be the successful claim */
  s = noseat ;
  /* is there a mah-jong? */
  for ( i = 0 ; i < NUM_SEATS ; i++ )
    if ( g->claims[(ds+i)%NUM_SEATS]
	  == MahJongClaim ) { s = (ds+i)%NUM_SEATS ; break ; }

  /* is there a kong? */
  if ( s == noseat )
    for ( i = 0 ; i < NUM_SEATS ; i++ )
      if ( g->claims[(ds+i)%NUM_SEATS]
	   == KongClaim ) { s = (ds+i)%NUM_SEATS ; break ; }

  /* is there a pung ? */
  if ( s == noseat )
    for ( i = 0 ; i < NUM_SEATS ; i++ )
      if ( g->claims[(ds+i)%NUM_SEATS]
	   == PungClaim ) { s = (ds+i)%NUM_SEATS ; break ; }
  
  /* or is there a chow? */
  if ( s == noseat )
    if ( g->claims[(ds+1)%NUM_SEATS]
	   == ChowClaim ) { s = (ds+1)%NUM_SEATS ; }

  /* finished checking; now process */

  if ( s == noseat ) {
    if ( g->state == Discarded ) {
      /* no claim; play passes to the next player, who draws */
      CMsgPlayerDrawsMsg m;
      s = (ds+1)%NUM_SEATS;
      m.type = CMsgPlayerDraws;
      m.id = g->players[s]->id;
      /* peek at the tile: we'll pass the CMsg to the game
	 to handle state changes */
      m.tile = game_peek_tile(g);
      
      if ( m.tile == ErrorTile ) {
	/* run out of wall; washout */
	washout(NULL);
	return;
      }
    
      /* stash a copy of the player, in case it goes mahjong */
      copy_player(gextras(g)->caller,g->players[s]);
      
      if ( handle_cmsg(g,&m) < 0 ) {
	warn("Consistency error drawing tile");
	abort();
      }
      check_min_time(1);
      send_seat(g,s,&m);
      m.tile = HiddenTile;
      send_other_seats(g,s,&m);
      send_infotiles(g->players[s]);
      return;
    } else {
      /* the player formed a kong, and (surprise) nobody tried to 
	 rob it */
      CMsgPlayerDrawsLooseMsg pdlm;

      pdlm.type = CMsgPlayerDrawsLoose;
      pdlm.id = g->players[ds]->id;
      /* find the loose tile. */
      pdlm.tile = game_peek_loose_tile(the_game);
      if ( pdlm.tile == ErrorTile ) {
	washout(NULL);
	return;
      }

      /* stash a copy of the player, in case it goes mahjong */
      copy_player(gextras(g)->caller,g->players[ds]);

      if ( handle_cmsg(the_game,&pdlm) < 0 ) {
	/* error should be impossible */
	warn("Consistency error: giving up");
	exit(1);
      }
      check_min_time(1);
      send_id(pdlm.id,&pdlm);
      pdlm.tile = HiddenTile;
      send_others(the_game,pdlm.id,&pdlm);
      send_infotiles(g->players[ds]);
      return;
    }
  }

  if ( g->claims[s] == ChowClaim ) {
    /* there can be no other claims, so just implement this one */
    CMsgPlayerChowsMsg pcm;

    pcm.type = CMsgPlayerChows;
    pcm.id = g->players[s]->id;
    pcm.tile = g->tile;
    pcm.cpos = g->cpos;

    /* if the cpos is AnyPos, then we instruct the player (only)
       to send in another chow claim, and just wait for it */
    if ( pcm.cpos == AnyPos ) {
      if ( handle_cmsg(g,&pcm) < 0 ) {
	CMsgErrorMsg em;
	em.type = CMsgError;
	em.seqno = connections[id_to_cnx(pcm.id)].seqno;
	em.error = g->cmsg_err;
	send_id(pcm.id,&em);
	return;
      }
      send_id(pcm.id,&pcm);
      return;
    }

    /* stash a copy of the player, in case it goes mahjong */
    copy_player(gextras(g)->caller,g->players[s]);

    if ( handle_cmsg(g,&pcm) < 0 ) {
      warn("Consistency error: failed to implement claimed chow.");
      exit(1);
    }

    check_min_time(2);
    send_all(g,&pcm);
    send_infotiles(g->players[s]);
    /* if that came from a dangerous discard, tell the other players */
    if ( game_flag(the_game,GFDangerousDiscard) ) {
      CMsgDangerousDiscardMsg ddm;
      ddm.type = CMsgDangerousDiscard;
      ddm.discard = the_game->serial;
      ddm.id = g->players[g->supplier]->id;
      ddm.nochoice = game_flag(g,GFNoChoice);
      send_all(the_game,&ddm);
    }
    return;
  }

  /* In the remaining cases, there may be other claims; send
     denial messages to them */
  cdm.type = CMsgClaimDenied;
  cdm.reason = "There was a prior claim";
  
  for ( i = 0 ; i < NUM_SEATS ; i++ ) {
    if ( i != ds && i != s && g->claims[i] != NoClaim) {
      cdm.id = g->players[i]->id;
      send_id(cdm.id,&cdm);
    }
  }

  if ( g->claims[s] == PungClaim ) {
    CMsgPlayerPungsMsg ppm;

    ppm.type = CMsgPlayerPungs;
    ppm.id = g->players[s]->id;
    ppm.tile = g->tile;

    /* stash a copy of the player, in case it goes mahjong */
    copy_player(gextras(g)->caller,g->players[s]);

    if ( handle_cmsg(g,&ppm) < 0 ) {
      warn("Consistency error: failed to implement claimed pung.");
      exit(1);
    }

    check_min_time(2);
    send_all(g,&ppm);
    send_infotiles(g->players[s]);
    /* if that came from a dangerous discard, tell the other players */
    if ( game_flag(the_game,GFDangerousDiscard) ) {
      CMsgDangerousDiscardMsg ddm;
      ddm.type = CMsgDangerousDiscard;
      ddm.id = g->players[g->supplier]->id;
      ddm.discard = the_game->serial;
      ddm.nochoice = game_flag(g,GFNoChoice);
      send_all(the_game,&ddm);
    }
    return;
  }

  if ( g->claims[s] == KongClaim ) {
    CMsgPlayerKongsMsg pkm;
    CMsgPlayerDrawsLooseMsg pdlm;

    pdlm.type = CMsgPlayerDrawsLoose;
    pkm.type = CMsgPlayerKongs;
    pdlm.id = pkm.id = g->players[s]->id;
    pkm.tile = g->tile;

    if ( handle_cmsg(g,&pkm) < 0 ) {
      warn("Consistency error: failed to implement claimed kong.");
      exit(1);
    }

    check_min_time(2);
    send_all(g,&pkm);

    /* find the loose tile. */
    pdlm.tile = game_peek_loose_tile(g);
    if ( pdlm.tile == ErrorTile ) {
      washout(NULL);
      return;
    }

    /* stash a copy of the player, in case it goes mahjong */
    copy_player(gextras(g)->caller,g->players[s]);

    if ( handle_cmsg(the_game,&pdlm) < 0 ) {
      /* error should be impossible */
      warn("Consistency error: giving up");
      exit(1);
    }
    check_min_time(1);
    send_id(pdlm.id,&pdlm);
    pdlm.tile = HiddenTile;
    send_others(g,pdlm.id,&pdlm);
    send_infotiles(g->players[s]);
    return;
  }

  if ( g->claims[s] == MahJongClaim ) {
    /* stash a copy of the player before it grabs the discard */
    copy_player(gextras(g)->caller,g->players[s]);

    if ( g->state == Discarded ) {
      /* the normal situation */
      CMsgPlayerMahJongsMsg pmjm;
      
      pmjm.type = CMsgPlayerMahJongs;
      pmjm.id = g->players[s]->id;
      pmjm.tile = HiddenTile;
      
      if ( handle_cmsg(g,&pmjm) < 0 ) {
	/* this should be impossible */
	CMsgErrorMsg em;
	em.type = CMsgError;
	em.seqno = connections[id_to_cnx(pmjm.id)].seqno;
	em.error = g->cmsg_err;
	send_id(pmjm.id,&em);
	return;
      }
      send_all(g,&pmjm);
      return;
    } else {
      /* the kong's been robbed */
      CMsgPlayerRobsKongMsg prkm;

      prkm.type = CMsgPlayerRobsKong;
      prkm.id = g->players[s]->id;
      prkm.tile = g->tile;

      if ( handle_cmsg(g,&prkm) < 0 ) {
	/* this should be impossible */
		CMsgErrorMsg em;
	em.type = CMsgError;
	em.seqno = connections[id_to_cnx(prkm.id)].seqno;
	em.error = g->cmsg_err;
	send_id(prkm.id,&em);
	return;
      }
      check_min_time(2);
      send_all(g,&prkm);
      return;
    }
  }

  /* NOTREACHED */
}


/* This function sends an InfoTiles message about the given player
   to all players who are receiving info tiles */
static void send_infotiles(PlayerP p) {
  static CMsgInfoTilesMsg itm;
  PlayerP q;
  static char buf[128];
  int i;

  itm.type = CMsgInfoTiles;
  itm.id = p->id;
  if ( popts(p)[POInfoTiles] ) {
    player_print_tiles(buf,p,0);
    itm.tileinfo = buf;
    send_id(itm.id,&itm);
  }
  player_print_tiles(buf,p,1);
  for ( i = 0 ; i < NUM_SEATS ; i++ ) {
    q = the_game->players[i];
    if ( q != p && popts(q)[POInfoTiles] ) {
      send_id(q->id,&itm);
    }
  }
}


/* Save state in the file.
   The state is saved as a sequence of CMsgs.
   First, four Players;
   then a Game;
   then GameOptions;
   then if a hand is in progress,
   Wall
   NewHand 
   the history of the hand.
*/
static char *save_state(Game *g, char *filename) {
  int fd;
  int i,j; /* unsigned to suppress warnings */
  static char gfile[512]; 
  CMsgGameMsg gamemsg;
  CMsgGameOptionMsg gom;
  GameOptionEntry *goe;
  
  if ( filename && filename[0] ) {
    char *f;
    /* strip any directories, for security */
    f = strrchr(filename,'/');
    if ( f ) filename = f+1;
    f = strrchr(filename,'\\');
    if ( f ) filename = f+1;
    strmcpy(gfile,filename,500);
    /* if it doesn't already end with .mjs, add it */
    if ( strlen(gfile) < 4 || strcmp(gfile+strlen(gfile)-4,".mjs") ) {
      strcat(gfile,".mjs");
    }
  } else {
    /* if we have no name already in use, construct one */
    if ( gfile[0] == 0 ) {
      if ( loadfilename[0] ) {
	strmcpy(gfile,loadfilename,510);
      } else {
	struct tm *ts;
	time_t t;
	int i;
	struct stat junk;
	t = time(NULL);
	ts = localtime(&t);
	i = 0;
	while ( i < 10 ) {
	  sprintf(gfile,"game-%d-%02d-%02d-%d.mjs",
	    ts->tm_year+1900,ts->tm_mon+1,ts->tm_mday,i);
	  if ( stat(gfile,&junk) != 0 ) break;
	  i++;
	}
      }
    }
  }
  fd = open(gfile,O_WRONLY|O_CREAT|O_TRUNC,0777);
  if ( fd < 0 ) {
    warn("Unable to write game state file: %s",strerror(errno));
    return 0;
  }
  for ( i = 0; i < NUM_SEATS; i++ ) {
    CMsgPlayerMsg pm;
    PlayerP q;
    
    q = g->players[i];
    if ( q->id == 0 ) continue;
    pm.type = CMsgPlayer;
    pm.id = q->id;
    pm.name = q->name;
    fd_put_line(fd,encode_cmsg((CMsgMsg *)&pm));
  }

  /* Normally, we write out a game message describing the current
     state of the game, and then the history. However, if this
     is the second time we've been called at the end of the same
     hand, we need to print the prehistory instead. */
  if ( g->state == HandComplete ) save_state_hack++;

  if ( save_state_hack > 1 ) {
    int j;
    for ( j=0; j < gextras(g)->prehistcount; j++ ) {
      fd_put_line(fd,encode_cmsg(gextras(g)->prehistory[j]));
    }
  } else {
    gamemsg.type = CMsgGame;
    gamemsg.east = g->players[east]->id;
    gamemsg.south = g->players[south]->id;
    gamemsg.west = g->players[west]->id;
    gamemsg.north = g->players[north]->id;
    gamemsg.round = g->round;
    gamemsg.hands_as_east = g->hands_as_east;
    gamemsg.firsteast = g->firsteast;
    gamemsg.east_score = g->players[east]->cumulative_score;
    gamemsg.south_score = g->players[south]->cumulative_score;
    gamemsg.west_score = g->players[west]->cumulative_score;
    gamemsg.north_score = g->players[north]->cumulative_score;
    gamemsg.protversion = g->protversion;
    gamemsg.manager = g->manager;
    
    fd_put_line(fd,encode_cmsg((CMsgMsg *)&gamemsg));
    
    /* save the number of completed rounds in a comment */
    CMsgCommentMsg cm;
    cm.type = CMsgComment;
    char c[] = "CompletedRounds nnnnnn";
    sprintf(c,"CompletedRounds %6d",gextras(g)->completed_rounds);
    fd_put_line(fd,encode_cmsg((CMsgMsg *)&cm));

    gom.type = CMsgGameOption;
    gom.id = 0;
    
    /* this relies on the fact that we know our option table
       contains only known options in numerical order. This
       would not necessarily be the case for clients, but it
       is for us, since we don't allow unknown options to be set */
    for ( i = 1 ; i <= GOEnd ; i++ ) {
      goe = &g->option_table.options[i];
      gom.optentry = *goe;
      fd_put_line(fd,encode_cmsg((CMsgMsg *)&gom));
    }
  }

  /* We are saving state for resumption here, so we do not need
     to save the history of a complete hand */
  if ( g->state != HandComplete || save_state_hack > 1 ) {
    CMsgWallMsg wm;
    char wall[MAX_WALL_SIZE*3+1];
    wm.type = CMsgWall;
    wm.wall = wall;
    wall[0] = 0;
    for ( i = 0 ; i < g->wall.size ; i++ ) {
      if ( i ) strcat(wall," ");
      strcat(wall,tile_code(g->wall.tiles[i]));
    }
    fd_put_line(fd,encode_cmsg((CMsgMsg *)&wm));
  
    for ( j=0; j < gextras(g)->histcount; j++ ) {
      fd_put_line(fd,encode_cmsg(gextras(g)->history[j]));
    }
  }

  close(fd);
  return gfile;
}

/* Load the state into the (pre-allocated) game pointed to by g.
   N.B. The player structures must also be allocated.
*/
static int load_state(Game *g) {
  int fd;
  char *l;
  CMsgMsg *m;
  int manager = 0;

  fd = open(loadfilename,O_RDONLY);
  if ( fd < 0 ) {
    warn("unable to open game state file: %s",strerror(errno));
    return 0;
  }
  
  clear_history(g);
  g->cmsg_check = 1;
  while ( (l = fd_get_line(fd)) ) {
    m = decode_cmsg(l);
    if ( ! m ) {
      warn("Bad cmsg in game state file, line %s",l);
      return 0;
    }
    if ( m->type == CMsgComment ) {
      char *p = ((CMsgCommentMsg *)m)->comment;
      if ( strncmp("CompletedRounds ",p,16) == 0 ) {
	if ( sscanf(p,"%d",&(gextras(g)->completed_rounds)) == 0 )
	  warn("unable to read number of completed rounds in game state file line %s",l);
      }
      continue;
    }
    if ( game_handle_cmsg(g,m) < 0 ) {
      warn("Error handling cmsg: line %s, error %s",l,g->cmsg_err);
      return 0;
    }
    /* if the game has a manager, clear it temporarily, so that
       the gameoptions we're about to process do get processed */
    if ( m->type == CMsgGame ) {
      manager = g->manager;
      g->manager = 0;
    }
    history_add(g,m);
  }

  /* reinstate manager */
  g->manager = manager;
  close(fd);
  /* We need to copy the timeout option value from the resumed game */
  timeout_time = get_timeout_time(g,localtimeouts);
  return 1;
}

/* This function is responsible for handling the scoring.
   It is called in the following circumstances:
   The mahjonging player has made all their sets, or
   a non-mahjonging player has issued a ShowTiles message
   (the showing of the tiles will already have been done).
   It then (a) computes the score for the hand, together
   with a text description of the computation, and announces
   it.
   (b) if all players are now declared, computes the settlements,
   announces the net change for each player, together with
   a text description of the calculation; implements the settlements;
   and moves to the state HandComplete, incrementing hands_as_east
   if necessary.
   If the game is being terminated early because of disconnect,
   it will finish the game at Settlement.
   Arguments: g is the game, s is the *seat* of the player on
   which something has happened.
*/
static void score_hand(Game *g, seats s) {
  PlayerP p = g->players[s];
  Score score;
  char buf[1000], tmp[100];
  static char *seatnames[] = { "East ", "South", "West ", "North" };
  static int changes[NUM_SEATS];
  CMsgHandScoreMsg hsm;
  CMsgSettlementMsg sm;
  seats i,j;
  int m;
  int cannon=0;
  seats cannoner = noseat;
  bool eastdoubles;
  bool loserssettle;
  bool discarderdoubles;

  buf[0] = 0;
  score = score_of_hand(g,s);
  hsm.type = CMsgHandScore;
  hsm.id = p->id;
  hsm.score = score.value;
  hsm.explanation = score.explanation;

  send_all(g,&hsm);

  if ( s == g->player ) {
    psetflag(p,MahJongged);
  }
  set_player_hand_score(p,score.value);

  /* have we scored all the players ? */
  for (i=0; i < NUM_SEATS; i++)
    if ( g->players[i]->hand_score < 0 ) return;

  /* now compute the settlements. East pays and receives double, etc? */
  eastdoubles = game_get_option_value(g,GOEastDoubles,NULL).optbool;
  loserssettle = game_get_option_value(g,GOLosersSettle,NULL).optbool;
  discarderdoubles = game_get_option_value(g,GODiscDoubles,NULL).optbool;

  for ( i = 0; i < NUM_SEATS; i++ ) changes[i] = 0;

  /* first the winner; let's re-use s and p */
  s = g->player;
  p = g->players[s];

  /* Firstly, check to see if somebody let off a cannon. At this point,
     we assume we know that the dflags field is a bit field! */
  for ( i = 0; i < NUM_SEATS; i++ ) {
    if ( i == s ) continue;
    if ( p->dflags[i] & p->dflags[s] ) {
      /* Only one person can be liable, so let's have a little
	 consistency check */
      if ( cannon ) {
	warn("Two people found letting off a cannon!");
	exit(1);
      }
      cannon=1;
      cannoner=i;
    }
  }

  if ( cannon ) {
    sprintf(tmp,"%s let off a cannon\n\n",seatnames[cannoner]);
    strcat(buf,tmp);
  }
  for ( i = 0; i < NUM_SEATS; i++ ) {
    if ( i == s ) continue;
    m = g->players[s]->hand_score;
    if ( eastdoubles && i*s == 0 ) m *= 2; /* sneaky test for one being east */
    /* singaporean rules: discarder pays double, on self-draw
       (including loose - query, is this right?) everybody pays double */
    if ( discarderdoubles
      && (g->whence != FromDiscard
	|| i == g->supplier) ) m *= 2;
    changes[s] += m; changes[cannon ? cannoner : i] -= m;
    sprintf(tmp,"%s pays %s",seatnames[cannon ? cannoner : i],seatnames[s]);
    strcat(buf,tmp);
    if ( cannon && cannoner != i ) {
      sprintf(tmp," (for %s)",seatnames[i]);
      strcat(buf,tmp);
    }
    sprintf(tmp," %5d\n",m);
    strcat(buf,tmp);
  }
  sprintf(tmp,"%s GAINS %5d\n\n",seatnames[s],changes[s]);
  strcat(buf,tmp);

  /* and now the others */
  for ( i = 0; i < NUM_SEATS; i++ ) {
    if ( i == s ) continue;
    for ( j = i+1; j < NUM_SEATS; j++ ) {
      if ( j == s ) continue;
      if ( cannon ) continue;
      if ( ! loserssettle ) continue;
      m = g->players[i]->hand_score - g->players[j]->hand_score;
      if ( eastdoubles && i*j == 0 ) m *= 2;
      changes[i] += m; changes[j] -= m;
      if ( m > 0 ) {
	sprintf(tmp,"%s pays %s %5d\n",seatnames[j],seatnames[i],m);
      } else if ( m < 0 ) {
	sprintf(tmp,"%s pays %s %5d\n",seatnames[i],seatnames[j],-m);
      } else {
	sprintf(tmp,"%s and %s are even\n",seatnames[i],seatnames[j]);
      }
      strcat(buf,tmp);
    }
    sprintf(tmp,"%s %s %5d\n\n",seatnames[i],
	    changes[i] >= 0 ? "GAINS" : "LOSES",
	    abs(changes[i]));
    strcat(buf,tmp);
  }
  
  sm.type = CMsgSettlement;
  sm.east = changes[east];
  sm.south = changes[south];
  sm.west = changes[west];
  sm.north = changes[north];
  sm.explanation = buf;
  handle_cmsg(g,&sm);
  send_all(g,&sm);
  
  /* dump the hand history to a log file if requested */
  if ( hand_history ) {
    char buf[256];
    /* hack hack hack FIXME */
    static int handnum = 1;
    sprintf(buf,"hand-%02d.mjs",handnum++);
    save_state_hack = 1;
    save_state(g,buf);
  }

  /* finish game if terminating on disconnect */
  if ( end_on_disconnect_in_progress ) finish_game(g);

  /* is the game over? */
  if ( g->player != noseat /* there was a mah jong */
    && (g->player != east /* and either it wasn't east */
      || g->hands_as_east == 13) /* or this was east's 13th win */
    /* does the round change? (the current south will be the next east;
       if that is the firsteast, the round changes) */
    && g->players[south]->id == g->firsteast
    && gextras(g)->completed_rounds == game_get_option_value(g,GONumRounds,NULL).optnat - 1 ) // it's the last round
    ////    && g->round == NorthWind ) /* and it's the last round already */
    finish_game(g);

  /* otherwise pause for the next hand */
  {
    CMsgPauseMsg pm;
    pm.type = CMsgPause;
    pm.exempt = 0;
    pm.requestor = 0;
    pm.reason = "to start next hand";
    handle_cmsg(g,&pm);
    send_all(g,&pm);
  }
}

/* implement washout. The argument is the reason for the washout;
   if NULL, the default reason of "wall exhausted" is assumed. */
static void washout(char *reason) {
  CMsgPauseMsg pm;
  CMsgWashOutMsg wom;
  wom.type = CMsgWashOut;
  if ( reason ) wom.reason = reason;
  else wom.reason = "wall exhausted";
  handle_cmsg(the_game,&wom);
  send_all(the_game,&wom);
  /* if ShowOnWashout is set, show all tiles */
  if ( game_get_option_value(the_game,GOShowOnWashout,NULL).optbool ) {
    int i;
    PlayerP p;
    CMsgPlayerShowsTilesMsg pstm;
    
    /* fill in basic fields of possible replies */
    pstm.type = CMsgPlayerShowsTiles;
    
    for ( i = 0; i < NUM_SEATS; i++ ) {
      p = the_game->players[i];
      
      /* if there are no concealed tiles, skip */
      if ( p->num_concealed > 0 ) {
	char tiles[100];
	int j;
	
	tiles[0] = '\000';
	for ( j = 0; j < p->num_concealed; j++ ) {
	  if ( j > 0 ) strcat(tiles," ");
	  strcat(tiles,tile_code(p->concealed[j]));
	}
	pstm.id = p->id;
	pstm.tiles = tiles;
	if ( handle_cmsg(the_game,&pstm) < 0 ) {
	  warn("Error handling cmsg in ShowOnWashout: error %s",the_game->cmsg_err);
	  return;
	}
	check_min_time(1);
	send_all(the_game,&pstm);
      }
    }
  }
  /* and now pause for the next hand */
  pm.type = CMsgPause;
  pm.exempt = 0;
  pm.requestor = 0;
  pm.reason = "to start next hand";
  handle_cmsg(the_game,&pm);
  send_all(the_game,&pm);
}

/* called for an error on an cnx. Removes the cnx,
   suspends the game if appropriate, etc */
static void handle_cnx_error(int cnx) {
  CMsgStopPlayMsg spm;
  char emsg[100];
  int id;
  /* for the moment, die noisily. Ultimately, just notify
     other players, and re-open the listening socket */
  if ( connections[cnx].skt == socket_fd ) {
    /* hard to see how this could happen, but ... */
    warn("Exception condition on listening fd\n");
    exit(1);
  }
  warn("Exception/eof on cnx %d, player %d\n",cnx, cnx_to_id(cnx));
  spm.type = CMsgStopPlay;
  /* We clear the player's entry before sending the message
     so that it fails harmlessly on the player's file instead
     of writing to a broken pipe. */
  id = cnx_to_id(cnx);
  /* we need to keep the connection in the table for use below.
     This will cause some writes to broken pipes, but we're
     ignoring those anyway */
  if ( id >= 0 ) {
    /* if the game hasn't yet started, and all the players support
       the necessary protocol version, we do not need to exit;
       we can just delete the disconnecting player.
       Of course, we don't need the disconnecting player to support
       the necessary protocol version, but I can't be bothered to
       do that.
       However, I think if --exit-on-disconnect is given, we
       should exit anyway, since the purpose of that option is
       to stop unwanted servers hanging around.
    */
    if ( the_game->round == UnknownWind 
      && ! exit_on_disconnect
      && protocol_version > 1034 ) {
      CMsgPlayerMsg pm;
      pm.type = CMsgPlayer;
      pm.id = id;
      pm.name = NULL;
      handle_cmsg(the_game,&pm);
      close_connection(cnx);
      send_all(the_game,&pm);
      return;
    }
    sprintf(emsg,"Lost connection to player %d%s",
	    id,exit_on_disconnect ? "quitting" : loadstate ? "; game suspended" : "");
    spm.reason = emsg;
    if ( ! end_on_disconnect ) {
      send_all(the_game,&spm);
      the_game->active = 0;
    } else {
      int washed_out = 0;
      int dcpen = 0; /* apply disconnection penalty ? */
      CMsgMessageMsg mm;
      mm.type = CMsgMessage;
      mm.sender = mm.addressee = 0;
      mm.text = emsg;
      sprintf(emsg,"Lost connection to player %d - ",id);
      /* to handle disconnection gracefully ... */
      if ( the_game->state == HandComplete ) {
	/* nothing to do at this point */
	strcat(emsg,"game will end now");
      } else if ( the_game->state == MahJonging ) {
	PlayerP p;
	seats s;
	/* if it's the mahjonging player we've lost, and they
	   haven't yet declared their hand, I think that's
	   just tough -- we don't want to do auto-declaration */
	s = id_to_seat(id);
	p = id_to_player(id);
	if ( the_game->player == s && !pflag(p,HandDeclared) ) {
	  CMsgWashOutMsg wom;
	  wom.type = CMsgWashOut;
	  wom.reason = "Lost the winner";
	  washed_out = 1;
	  handle_cmsg(the_game,&wom);
	  send_all(the_game,&wom);
	  strcat(emsg,"ending game");
	} else {
	  PMsgShowTilesMsg stm;
	  /* we have to continue processing, since other players
	     may continue to declare tiles. We fake a showtiles
	     for this player, and handle the end of game later */
	  end_on_disconnect_in_progress = 1;
	  if ( !pflag(p,HandDeclared) ) {
	    stm.type = PMsgShowTiles;
	    handle_pmsg((PMsgMsg *)&stm,cnx);
	  }
	  strcat(emsg,"game will end after scoring this hand");
	}
      } else {
	/* not much we can do except declare a washout */
	CMsgWashOutMsg wom;
	wom.type = CMsgWashOut;
	wom.reason = "Lost a player";
	handle_cmsg(the_game,&wom);
	send_all(the_game,&wom);
	strcat(emsg,"ending game");
	washed_out = 1;
      }
      send_all(the_game,&mm);
      /* do we need to apply a disconnection penalty?
	 If the state is MahJonging, then it's morally the
	 same as HandComplete. Well, not if it's the winner
	 who dc'ed, but we'll quietly pass over that.
	 The washouts will have set handcomplete, so we must
	 apply the usual logic */
	/* are we at the end of a hand ? */
      /* If the game hasn't actually got under way, we won't charge
	 a penalty */
      if ( the_game->state == HandComplete
	&& the_game->round == EastWind
	&& the_game->players[east]->id == the_game->firsteast
	&& the_game->hands_as_east == 0
	   && ! washed_out ) {
	dcpen = 0;
      } else if ( (the_game->state == HandComplete
	     || the_game->state == MahJonging)
	/* It's a pain that I can't test for this easily
	   at the *end* of a hand. Have to duplicate logic
	   elsewhere */
	&& the_game->player != noseat /* there was a mah jong */ ) {
	/* are we at the end of a round? */
	if ( (the_game->player != east /* and either it wasn't east */
	   || the_game->hands_as_east == 13) /* or this was east's 13th win */
	  /* does the round change? (the current south will be the next east;
	     if that is the firsteast, the round changes) */
	  && the_game->players[south]->id == the_game->firsteast ) {
	  if ( the_game->round == NorthWind ) {
	    /* end of game. No penalty can apply */
	  } else {
	    dcpen = disconnect_penalty_end_of_round;
	  }
	} else {
	  /* in middle of round, but end of hand */
	  dcpen = disconnect_penalty_end_of_hand;
	}
      } else {
	dcpen = disconnect_penalty_in_hand;
      }
      if ( dcpen > 0 ) {
	info("Disconnect penalty for player %d: %d",id,dcpen);
	change_player_cumulative_score(id_to_player(id),-dcpen);
      }
      /* if we have nothing further to do, we should end the game */
      if ( ! end_on_disconnect_in_progress ) {
	finish_game(the_game);
      }
    }
    timeout = 0; /* clear timeout */
  }
  if ( exit_on_disconnect ) save_and_exit() ;
  /* and close the offending connection */
  close_connection(cnx);
}

/* loads a wall from a file; puts stack and wall of start in last two
   arguments */
static int load_wall(char *wfname, Game *g) {
  FILE *wfp;
  int i;
  char tn[5];
  Tile t;

  wfp = fopen(wfname,"r");
  if ( wfp == NULL ) {
    warn("couldn't open wall file");
    return 0;
  }
  for ( i = 0 ; i < g->wall.size ; i++ ) {
    fscanf(wfp,"%2s",tn);
    t = tile_decode(tn);
    if ( t == ErrorTile ) {
      warn("Error parsing wall file");
      return 0;
    }
    g->wall.tiles[i] = t;
  }
  return 1;
}

/* resend: given a player id and a message, send it to the player
   if appropriate, censoring as required. */
static void resend(int id, CMsgMsg *m)
{
  switch ( m->type ) {
    /* The following messages are always sent */
  case CMsgNewHand:
  case CMsgPlayerDeclaresSpecial:
  case CMsgPause:
  case CMsgPlayerReady:
  case CMsgPlayerDiscards:
  case CMsgDangerousDiscard:
  case CMsgPlayerClaimsPung:
  case CMsgPlayerPungs:
  case CMsgPlayerFormsClosedPung:
  case CMsgPlayerClaimsKong:
  case CMsgPlayerKongs:
  case CMsgPlayerDeclaresClosedKong:
  case CMsgPlayerAddsToPung:
  case CMsgPlayerRobsKong:
  case CMsgPlayerClaimsChow:
  case CMsgPlayerChows:
  case CMsgPlayerFormsClosedChow:
  case CMsgWashOut:
  case CMsgPlayerClaimsMahJong:
  case CMsgPlayerMahJongs:
  case CMsgPlayerPairs:
  case CMsgPlayerFormsClosedPair:
  case CMsgPlayerShowsTiles:
  case CMsgPlayerSpecialSet:
  case CMsgPlayerFormsClosedSpecialSet:
  case CMsgHandScore:
  case CMsgSettlement:
    send_id(id,m);
    break;
    /* The following messages are sent only to the player concerned */
  case CMsgPlayerDoesntClaim:
    if ( id == ((CMsgPlayerDoesntClaimMsg *)m)->id ) send_id(id,m);
    break;
  case CMsgSwapTile:
    if ( id == ((CMsgSwapTileMsg *)m)->id ) send_id(id,m);
    break;
    /* The following messages are sent as is to the player concerned,
       and with censoring to other players */
  case CMsgPlayerDraws:
    if ( id == ((CMsgPlayerDrawsMsg *)m)->id ) {
      send_id(id,m);
    } else {
      CMsgPlayerDrawsMsg mm = *(CMsgPlayerDrawsMsg *)m;
      mm.tile = HiddenTile;
      send_id(id,&mm);
    }
    break;
  case CMsgPlayerDrawsLoose:
    if ( id == ((CMsgPlayerDrawsLooseMsg *)m)->id ) {
      send_id(id,m);
    } else {
      CMsgPlayerDrawsLooseMsg mm = *(CMsgPlayerDrawsLooseMsg *)m;
      mm.tile = HiddenTile;
      send_id(id,&mm);
    }
    break;
    /* The following message can occur in history, but
       is not sent to clients */
  case CMsgComment:
    break;
    /* The following should not occur in history */
  case CMsgCanMahJong:
  case CMsgClaimDenied:
  case CMsgConnectReply:
  case CMsgReconnect:
  case CMsgRedirect:
  case CMsgAuthReqd:
  case CMsgError:
  case CMsgGame:
  case CMsgGameOption:
  case CMsgGameOver:
  case CMsgInfoTiles:
  case CMsgNewRound:
  case CMsgPlayer:
  case CMsgPlayerOptionSet:
  case CMsgMessage:
  case CMsgChangeManager:
  case CMsgStartPlay:
  case CMsgStopPlay:
  case CMsgWall:
  case CMsgStateSaved:
    abort();
  }
}

static void clear_history(Game *g) {
  /* now clear the history records */
  while ( gextras(g)->histcount > 0 ) {
      cmsg_deepfree(gextras(g)->history[--gextras(g)->histcount]);
  }
  /* and the prehistory */
  while ( gextras(g)->prehistcount > 0 ) {
      cmsg_deepfree(gextras(g)->prehistory[--gextras(g)->prehistcount]);
  }
}

/* add a cmsg (if appropriate) to the history of a game */
static void history_add(Game *g, CMsgMsg *m) {
  switch ( m->type ) {
    /* These messages do go into history */
  case CMsgNewHand:
  case CMsgPlayerDeclaresSpecial:
  case CMsgPause:
  case CMsgPlayerReady:
  case CMsgPlayerDraws:
  case CMsgPlayerDrawsLoose:
  case CMsgPlayerDiscards:
  case CMsgPlayerDoesntClaim:
  case CMsgDangerousDiscard:
  case CMsgPlayerClaimsPung:
  case CMsgPlayerPungs:
  case CMsgPlayerFormsClosedPung:
  case CMsgPlayerClaimsKong:
  case CMsgPlayerKongs:
  case CMsgPlayerDeclaresClosedKong:
  case CMsgPlayerAddsToPung:
  case CMsgPlayerRobsKong:
  case CMsgPlayerClaimsChow:
  case CMsgPlayerChows:
  case CMsgPlayerFormsClosedChow:
  case CMsgWashOut:
  case CMsgPlayerClaimsMahJong:
  case CMsgPlayerMahJongs:
  case CMsgPlayerPairs:
  case CMsgPlayerFormsClosedPair:
  case CMsgPlayerShowsTiles:
  case CMsgPlayerSpecialSet:
  case CMsgPlayerFormsClosedSpecialSet:
  case CMsgHandScore:
  case CMsgSettlement:
  case CMsgComment:
  case CMsgSwapTile:
    gextras(g)->history[gextras(g)->histcount++] = cmsg_deepcopy(m);
    break;
    /* These messages do not go into history */
  case CMsgStateSaved:
  case CMsgCanMahJong:
  case CMsgClaimDenied:
  case CMsgConnectReply:
  case CMsgReconnect:
  case CMsgAuthReqd:
  case CMsgRedirect:
  case CMsgError:
  case CMsgGame:
  case CMsgGameOption:
  case CMsgGameOver:
  case CMsgInfoTiles:
  case CMsgNewRound:
  case CMsgPlayer:
  case CMsgPlayerOptionSet:
  case CMsgMessage:
  case CMsgChangeManager:
  case CMsgStartPlay:
  case CMsgStopPlay:
  case CMsgWall:
    ;
  }
}

static void save_and_exit() {
  if ( save_on_exit ) save_state(the_game,NULL);
  exit(0);
}

/* this function extracts the timeout_time from a game
   according the Timeout and TimeoutGrace options, and
   the passed in value of localtimeouts */
static int get_timeout_time(Game *g,int localtimeouts) {
  return (game_get_option_value(g,GOTimeout,NULL).optnat)
    + (localtimeouts ? game_get_option_value(g,GOTimeoutGrace,NULL).optnat : 0);
}
