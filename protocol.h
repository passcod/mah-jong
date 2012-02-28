/* $Header: /home/jcb/MahJong/newmj/RCS/protocol.h,v 12.2 2012/02/01 14:10:58 jcb Rel $
 * protocol.h
 * defines the messages passed between players and controller
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

#ifndef PROTOCOL_H_INCLUDED
#define PROTOCOL_H_INCLUDED

#include "tiles.h"
#include "player.h" /* required for ChowPosition */

/* some constants */

/* This is the largest wall that this level of the protocol supports */
#define MAX_WALL_SIZE 144

/* a bool is an integer constrained to be 0 or 1 */
typedef int bool;

/* a word16 is a word of at most 15 (to allow for final null) characters,
   without white space */
typedef char word16[16];

/* First we have the definitions for options. These are logically
   part of the game module, but they are also needed by the this
   module. Unfortunately, protocol.h can't just include game.h,
   since game.h depends on this, and I can't see how to remove
   the circularity.
*/

/* here are the names of the options. Option 0 for unknown.
   The End option also has special reserved meaning.
   Names (omitting GO) shd be 15 chars or less */
typedef enum {
  /* make-enums sub { $_[0] =~ s/^GO//; } */
  GOUnknown=0,
  GOTimeout, /* the timeout in seconds for claims. Default: 15 */
  GOTimeoutGrace, /* grace period when clients handle timeouts locally */
  GOScoreLimit, /* limit for score. Default 1000 */
  GONoLimit, /* true if no limit on scores. Then ScoreLimit is
	       notional value of limit hand */
  GOMahJongScore, /* base points for going out */
  GOSevenPairs, /* seven pairs hand allowed? */
  GOSevenPairsVal, /* value of a seven pair hand */
    /* the following group of options concerns flowers, by
       which is meant flowers and seasons as in the classical
       rules */
  GOFlowers, /* are flowers to be dealt? */
  GOFlowersLoose, /* flowers replaced from dead wall? */
  GOFlowersOwnEach, /* score for each own flower or season */
  GOFlowersOwnBoth, /* score for own flower and season. N.B.
		       In this case there will also be two 
		       scores of FlowersOwnEach */
  GOFlowersBouquet, /* all four flowers or all four seasons */
  GODeadWall, /* is there a dead wall, or do we play to the end? */
  GODeadWall16, /* is the dead wall 16 tiles unreplenished (per Millington)
		   or the normal 14 replenished. */
  GOConcealedFully, /* score for a fully concealed hand */
  GOConcealedAlmost, /* score for hand concealed up to mah jong,
			or to end of game (if next option applies) */
  GOLosersPurity, /* do losers score the doubles for pure and almost 
		       concealed hand? */
  GOKongHas3Types, /* do we use Millington's 3-way kong distinction? */
  GOEastDoubles, /* does East pay and receive double (as in Chinese Classical)? */
  GOLosersSettle, /* do losers pay among themselves? */
  GODiscDoubles, /* does the discarder pay a double score? */
  GOShowOnWashout, /* are tiles revealed after a washout? */
  GONumRounds, /* how many rounds in a game? */
  GOEnd } GameOption;
#define GONumOptions (GOEnd+1)

/* and the types of options: printed as bool, int, string etc.
   The string option is limited to 127 characters, and may
   not contain white space. (I can't
   remember why I thought it might be wanted.) */
typedef enum {
  /* make-enums sub { $_[0] =~ s/^GOT// ; $_[0] =~ y/A-Z/a-z/; } */
  GOTBool, 
  GOTInt, 
  GOTNat, /* non-negative integer -- introduced at proto 1020 */
  GOTScore, /* a score is an int, but with a special interpretation:
	       if < 10000, it's a number of points; if a multiple of
	       10000, it's that number of doubles (up to 100); and 
	       a fractional (/100) multiple of 100000000 indicates so
	       many limits. It may combine a limit with a number
	       of doubles, in which case the number of doubles is
	       used only in a no limit game. */
  GOTString } GameOptionType;

typedef struct _GameOptionEntry {
  GameOption option; /* integer option number */
  char name[16]; /* short name: shd be same as name of option number */
  GameOptionType type; /* type of option value */
  int protversion; /* least protocol version required to use this
		      option. N.B. This may well be before the introduction
		      of the option, as with many options clients do not
		      have to know about them, and can use the server provided
		      text to allow the user to set them. */
  bool enabled; /* is this option understood in this game? */
  union { bool optbool; int optint; unsigned int optnat; int optscore; char optstring[128]; } value;
  char desc[128]; /* short description */
  void *userdata; /* field for client use */
} GameOptionEntry;

/* This type is for use externally. It's the same as the union type in the 
   option entry, except that the string is returned as a pointer.
*/
typedef union _GameOptionValue {
  bool optbool;
  int optint;
  unsigned int optnat;
  int optscore;
  char *optstring;
} GameOptionValue;

/* The fundamental principle is that players cannot change the state
   of the game---including their own hand. They send a message to
   the controller declaring what they want to do; the controller then
   issues the message implementing it. 
   These messages should NOT be seen as request and reply, but simply
   as event deliveries.
*/

/* Protocol version is an integer of the form 1000*major+minor.
   The protocol major version should be incremented only when
   it is impossible for an earlier client to play with the new
   protocol. Normally, servers and players should be able to 
   downgrade to the lowest common minor version.
   Note: in the earlier versions, including the alpha binary release,
   the version was just 1,2,3, corresponding to our current major.
   However, we will view all those as major 0, and start again at 1000.
   Note that this is not really just the protocol version; it's more
   a version of the protocol plus options etc.
*/

/* This define gives the protocol version implemented in this file */
#define PROTOCOL_VERSION 1110

/* This global variable, found in protocol.c, is used by many functions
   to determine the protocol version currently being used.
   It would be better if this were an argument to every function that
   used it, or if every function were passed a Game structure, but this
   would be too inconvenient. So this global variable is used instead.
   If we ever program with different games in the same process, care will
   have to be taken to manage this.
*/
extern int protocol_version;

/* These structures are the programmer's view of the messages.
   The protocol is actually defined by the wire protocol, not by
   these structs.
*/

/* NOTE: for bad reasons, these enums should always be less
   than 1000000 (to allow space for flag bits) */

/* types of message from controller to player.
   For convenience in debugging, the actual values are widely spaced.
*/
typedef enum {
  CMsgError = 0,
  CMsgInfoTiles = 1,
  CMsgStateSaved = 2,
  CMsgConnectReply = 10,
  CMsgReconnect = 11,
  CMsgAuthReqd = 12,
  CMsgRedirect = 13,
  CMsgPlayer = 20,
  CMsgNewRound = 29,
  CMsgGame = 30,
  CMsgNewHand = 31,
  CMsgPlayerDeclaresSpecial = 34,
  CMsgStartPlay = 35,
  CMsgStopPlay = 36,
  CMsgPause = 37,
  CMsgPlayerReady = 38,
  CMsgPlayerDraws = 40,
  CMsgPlayerDrawsLoose = 41,
  CMsgPlayerDiscards = 50,
  CMsgClaimDenied = 51,
  CMsgPlayerDoesntClaim = 52,
  CMsgDangerousDiscard = 55,
  CMsgPlayerClaimsPung = 60,
  CMsgPlayerPungs = 61,
  CMsgPlayerFormsClosedPung = 62,
  CMsgPlayerClaimsKong = 70,
  CMsgPlayerKongs = 71,
  CMsgPlayerDeclaresClosedKong = 80,
  CMsgPlayerAddsToPung = 81,
  CMsgPlayerRobsKong = 85,
  CMsgCanMahJong = 87,
  CMsgPlayerClaimsChow = 90,
  CMsgPlayerChows = 91,
  CMsgPlayerFormsClosedChow = 92,
  CMsgWashOut = 99,
  CMsgPlayerClaimsMahJong = 100,
  CMsgPlayerMahJongs = 101,
  CMsgPlayerPairs = 102,
  CMsgPlayerFormsClosedPair = 103,
  CMsgPlayerShowsTiles = 105,
  CMsgPlayerSpecialSet = 106,
  CMsgPlayerFormsClosedSpecialSet = 107,
  CMsgPlayerOptionSet = 110,
  CMsgHandScore = 115,
  CMsgSettlement = 120,
  CMsgGameOver = 200,
  CMsgGameOption = 300,
  CMsgChangeManager = 310,
  CMsgMessage = 400,
  CMsgWall = 900,
  CMsgComment = 999,
  CMsgSwapTile = 1000,
} ControllerMsgType;

/* Types of message sent by player. The numbers more or less
   match the corresponding controller messages.
*/
typedef enum {
  PMsgSaveState = 1,
  PMsgLoadState = 2,
  PMsgConnect = 10,
  PMsgRequestReconnect = 11,
  PMsgAuthInfo = 12,
  PMsgNewAuthInfo = 13,
  PMsgDisconnect = 14,
  PMsgDeclareSpecial = 33,
  PMsgRequestPause = 36,
  PMsgReady = 37,
  PMsgDiscard = 50,
  PMsgNoClaim = 51,
  PMsgPung = 60,
  PMsgFormClosedPung = 62,
  PMsgKong = 70,
  PMsgDeclareClosedKong = 80,
  PMsgAddToPung = 81,
  PMsgQueryMahJong = 87,
  PMsgChow = 90,
  PMsgFormClosedChow = 92,
  PMsgDeclareWashOut = 99,
  PMsgMahJong = 100,
  PMsgPair = 102,
  PMsgFormClosedPair = 103,
  PMsgShowTiles = 105,
  PMsgSpecialSet = 106,
  PMsgFormClosedSpecialSet = 107,
  PMsgSetPlayerOption = 110,
  PMsgSetGameOption = 300,
  PMsgQueryGameOption = 301,
  PMsgListGameOptions = 302,
  PMsgChangeManager = 310,
  PMsgSendMessage = 400,
  PMsgSwapTile = 1000,
} PlayerMsgType;
#define DebugMsgsStart 1000

/* NOTE: it is currently assumed (by the wire protocol) that these
   structures have at most one (char *) component, and if they do,
   it's the final element in the structure.
   If this assumption becomes invalid, work will have to be done on
   the wire protocol code; especially since much of the conversion
   routines is automatically generated from this file.
   For the same reason, when adding new structures, the layout
   and naming convention should be the same as the existing ones.
*/

/* message sent by controller to player to signal an error.
   The string is a human readable description.
*/

typedef struct _CMsgErrorMsg {
  ControllerMsgType type; /* CMsgError */
  int seqno; /* sequence number of player message provoking error, or 0 */
  char *error; /* human readable explanation */
} CMsgErrorMsg;


/* This message requests the controller to save the state
   of the current game. */
/* Note: in protocol versions before 1025, the filename field
   did not exist. This should require no special handling. */
typedef struct _PMsgSaveStateMsg {
  PlayerMsgType type; /* PMsgSaveState */
  char *filename; /* name of file to save in; subject to interpretation
		     and overriding by server. May be null. */
} PMsgSaveStateMsg;

/* This message informs players that the state of the game
   has been saved. */
/* This message was introduced at pversion 1025. */
typedef struct _CMsgStateSavedMsg {
  ControllerMsgType type; /* CMsgStateSaved */
  int id; /* player who requested save */
  char *filename; /* file in which saved. Server dependent.
		     May include full path information in 
		     system dependent form */
} CMsgStateSavedMsg;

/* This message requests the server to load the game state
   from a saved file. There is no reply on success; an error
   is returned on failure. */
/* This message was introduced at pversion 1038. */
typedef struct _PMsgLoadStateMsg {
  PlayerMsgType type; /* PMsgSaveState */
  char *filename; /* name of file to load; subject to interpretation
		     and overriding by server. */
} PMsgLoadStateMsg;

/* This message is purely informational, and is mainly useful
   for debugging or for very dumb clients. It may be sent to
   a player whenever its hand changes, and contains a human
   readable presentation of the player's tiles.
*/
typedef struct _CMsgInfoTilesMsg {
  ControllerMsgType type; /* CMsgInfoTiles */
  int id; /* id of player---in teaching situation might send
	     info on other players too */
  char *tileinfo;
} CMsgInfoTilesMsg;

/* Message sent by player after connecting to controller. */

typedef struct _PMsgConnectMsg {
  PlayerMsgType type; /* PMsgConnect */
  int pvers;  /* protocol version used by player */
  int last_id;  /* (if non-zero) identifier from last session;
		   controller is not bound to keep this, but will try */
  char *name;  /* player's name */
} PMsgConnectMsg;

/* Message sent by controller to player on receipt of ConnectMsg */
typedef struct _CMsgConnectReplyMsg {
  ControllerMsgType type; /* CMsgConnectReply */
  int pvers;  /* protocol version of server */
  int id; /* id assigned by controller, or zero if connection refused */
  char *reason; /* human readable reason for refusal (if refused) */
} CMsgConnectReplyMsg;

/* Message sent by controller to request authentication.
   This message was introduced at pversion 1100. */
typedef struct _CMsgAuthReqdMsg {
  ControllerMsgType type; /* CMsgAuthReqd */
  word16 authtype; /* type of authorization, currenly just: basic */
  char *authdata; /* any data to be included */
} CMsgAuthReqdMsg;

/* Message sent by player to provide authentication.
   This message was introduced at pversion 1100. */
typedef struct _PMsgAuthInfoMsg {
  PlayerMsgType type; /* PMsgAuthInfo */
  word16 authtype; /* type of authorization, currenly just: basic */
  char *authdata; /* any data to be included */
} PMsgAuthInfoMsg;

/* Message sent by player to provide new authentication info.
   This message was introduced at pversion 1100. */
typedef struct _PMsgNewAuthInfoMsg {
  PlayerMsgType type; /* PMsgNewAuthInfo */
  word16 authtype; /* type of authorization, currenly just: basic */
  char *authdata; /* any data to be included */
} PMsgNewAuthInfoMsg;

/* Message sent by controller to tell player to close connection
   and connect to another port. The player should connect with
   the same id and name that it currently has.
   This message was introduced at pversion 1100. */
typedef struct _CMsgRedirectMsg {
  ControllerMsgType type; /* CMsgRedirect */
  char *dest; /* new connection, in the standard form host:port.
                 If host is omitted, use the same host. */
} CMsgRedirectMsg;

/* This message requests a "reconnection": that is, the equivalent
   of disconnecting and connection, without actually dropping the
   network connection. If a positive reply is received, the client's
   next act should be to resend its connect message.
   This message was introduced at pversion 1070.
*/
typedef struct _PMsgRequestReconnectMsg {
  PlayerMsgType type; /* PMsgRequestReconnect */
} PMsgRequestReconnectMsg;

/* This message grants a reconnection request.
   This message was introduced at pversion 1070.
*/
typedef struct _CMsgReconnectMsg {
  ControllerMsgType type; /* CMsgReconnect */
} CMsgReconnectMsg;

/* This message is an explicit disconnect to quit the game.
   This message was introduced at pversion 1100. */
typedef struct _PMsgDisconnectMsg {
  PlayerMsgType type; /* PMsgDisconnect */
} PMsgDisconnectMsg;

/* Message sent by controller to inform players of each other's existence.
   When a player connects, it receives one such message for every already
   existing player; and each existing player receives a message for the
   new player.
   This message is also sent if a player changes its name.
   After protocol version 1034, if the name is empty, this means
   "delete the player". (Empty names were previously permitted,
   but this was always a mistake.)
*/
typedef struct _CMsgPlayerMsg {
  ControllerMsgType type; /* CMsgPlayer */
  int id; /* id of player */
  char *name; /* name of player */
} CMsgPlayerMsg;

/* Message sent by controller to initiate a game (possibly continued
   from a previous session). Contains the initial wind assignment.
*/

typedef struct _CMsgGameMsg {
  ControllerMsgType type; /* CMsgGame */
  int east; /* id of player in east */
  int south; /* id of south */
  int west; /* id of west */
  int north; /* id of north */
  TileWind round; /* wind of the current round */
  int hands_as_east; /* number of hands completed with this dealer */
  int firsteast; /* id of player who was east in first hand of game */
  int east_score; /* east's score in game */
  int south_score;
  int west_score;
  int north_score;
  int protversion; /* protocol version to be used in this game */
  int manager; /* id of player owning this game */
} CMsgGameMsg;

/* Message to say that the wind of the round has changed */
typedef struct _CMsgNewRoundMsg {
  ControllerMsgType type; /* CMsgNewRound */
  TileWind round; /* wind of the new round */
} CMsgNewRoundMsg;

/* Message sent by controller to start new hand.
   It also tells where the live wall notionally starts;
   this is completely irrelevant to the actual implementation,
   but can be used to give players a consistent view of the wall.
*/
typedef struct _CMsgNewHandMsg {
  ControllerMsgType type; /* CMsgNewHand */
  int east; /* id of east player for this hand: just to check */
  int start_wall; /* the wall (as a seat number) in which the live wall starts*/
  int start_stack; /* the stack (from right) at which the live wall starts */
} CMsgNewHandMsg;

/* Message sent by controller to draw one tile for player.
   Other players will receive this message with a blank tile.
*/
typedef struct _CMsgPlayerDrawsMsg {
  ControllerMsgType type; /* CMsgPlayerDraws */
  int id; /* player drawing tile */
  Tile tile; /* tile drawn (HiddenTile if not known to us) */
} CMsgPlayerDrawsMsg;

/* Message sent by controller to draw a loose tile for player.
   Other players will receive this message with a blank tile.
*/
typedef struct _CMsgPlayerDrawsLooseMsg {
  ControllerMsgType type; /* CMsgPlayerDrawsLoose */
  int id; /* player drawing tile */
  Tile tile; /* tile drawn (HiddenTile if not known to us) */
} CMsgPlayerDrawsLooseMsg;

/* Message sent by the player to declare a special tile.
   If the tile is blank, player has no more specials. (Of course,
   the controller knows this anyway.)
   This message is also used during play.
*/
typedef struct _PMsgDeclareSpecialMsg { /* alias ds */
  PlayerMsgType type; /* PMsgDeclareSpecial */
  Tile tile; /* tile being declared, or blank */
} PMsgDeclareSpecialMsg;

/* Message sent by controller to implement special declaration. */
typedef struct _CMsgPlayerDeclaresSpecialMsg {
  ControllerMsgType type; /* CMsgPlayerDeclaresSpecial */
  int id; /* player making declaration */
  Tile tile; /* tile being declared (blank if player finished) */
} CMsgPlayerDeclaresSpecialMsg;

/* This message is sent to all players, to tell them they may
   start normal play. This happens either when a game starts,
   or to restart a game that was suspended. */

typedef struct _CMsgStartPlayMsg {
  ControllerMsgType type; /* CMsgStartPlay */
  int id; /* not strictly necessary: id of player due to do something */
} CMsgStartPlayMsg;

/* This message is sent to all players to stop play.
   This may be (for example) because a player has lost its
   connection.
*/
typedef struct _CMsgStopPlayMsg {
  ControllerMsgType type; /* CMsgStopPlay */
  char *reason; /* human readable explanation */
} CMsgStopPlayMsg;

/* This message is sent by a player to request a pause in play.
   If granted, all players (including the pausing player) must
   give permission to continue again. */
typedef struct _PMsgRequestPauseMsg {
  PlayerMsgType type; /* PMsgRequestPause */
  char *reason; /* reason for pause; will be passed to other players */
} PMsgRequestPauseMsg;

/* This message is sent by the controller when it wishes to gain
   the permission of players to proceed. This happens typically at:
   the end of declaring specials, before play proper starts;
   at the end of a hand, before proceeding to the next hand. */
typedef struct _CMsgPauseMsg {
  ControllerMsgType type; /* CMsgPause */
  int exempt; /* 0, or the id of the one player who is NOT being asked
	     to confirm readiness (typically because it's them to proceed) */
  int requestor; /* 0, or id of player who requested pause */
  char *reason; /* reason for pause. This will (it happens) be phrased
		   as an infinitive, e.g. "to start play" */
} CMsgPauseMsg;

/* and this is the reply */
typedef struct _PMsgReadyMsg {
  PlayerMsgType type; /* PMsgReady */
} PMsgReadyMsg;

/* and this tells other players who's ready (and bloody emacs gets
   confused by that quote, so here's another) */
typedef struct _CMsgPlayerReadyMsg {
  ControllerMsgType type; /* CMsgPlayerReady */
  int id;
} CMsgPlayerReadyMsg;

/* Message sent by player to announce discard */
typedef struct _PMsgDiscardMsg { /* alias d */
  PlayerMsgType type; /* PMsgDiscard */
  Tile tile; /* tile to be discarded */
  bool calling; /* player is making a Calling declaration */
} PMsgDiscardMsg;

/* Message sent by controller to implement discard */
typedef struct _CMsgPlayerDiscardsMsg {
  ControllerMsgType type; /* CMsgPlayerDiscards */
  int id; /* player discarding */
  Tile tile; /* tile being discarded */
  int discard; /* a serial number for this discard */
  bool calling; /* player is making a Calling declaration */
} CMsgPlayerDiscardsMsg;

/* This message is sent by a player to say that it has no claim
   on the current discard. It implies a request to draw a tile if
   it's the player's turn.
*/
typedef struct _PMsgNoClaimMsg { /* alias n */
  PlayerMsgType type; /* PMsgNoClaim */
  int discard; /* serial number of discard */
} PMsgNoClaimMsg;

/* Message sent by controller to say that a player is making
   no claim on the current discard. Usually this is sent
   only to the player concerned.
*/
typedef struct _CMsgPlayerDoesntClaimMsg {
  ControllerMsgType type; /* CMsgPlayerDoesntClaim */
  int id; /* player */
  int discard; /* current discard serial */
  bool timeout; /* 1 if this is generated by timeout in controller */
} CMsgPlayerDoesntClaimMsg;

/* This message is informational, and is sent by the controller
   after a claim has been implemented to announce that the discard
   was dangerous.
*/
typedef struct _CMsgDangerousDiscardMsg {
  ControllerMsgType type; /* CMsgDangerousDiscard */
  int id; /* player who discarded */
  int discard; /* serial number of the dangerous discard */
  bool nochoice; /* discard was dangerous, but discarder had no choice */
} CMsgDangerousDiscardMsg;

/* Message sent by player to claim a pung.
   In the usual rules, it is possible to claim a pung up to the time
   the *next* tile is discarded. At the moment, we don't implement that,
   but we should in due course. Hence a pung (etc.) claim includes
   an integer identifying the discard that is being claimed---the integer
   was assigned by the controller. This provides some protection against
   delays.
*/
typedef struct _PMsgPungMsg { /* alias p */
  PlayerMsgType type; /* PMsgPung */
  int discard; /* id of discard being claimed */
} PMsgPungMsg;

/* Message sent by controller to other players to inform them of
   a pung claim (NOT of its success).
*/
typedef struct _CMsgPlayerClaimsPungMsg {
  ControllerMsgType type; /* CMsgPlayerClaimsPung */
  int id; /* player claiming */
  int discard; /* discard being claimed */
} CMsgPlayerClaimsPungMsg;

/* generic refusal message sent by controller to deny a claim. */
typedef struct _CMsgClaimDeniedMsg {
  ControllerMsgType type; /* CMsgClaimDenied */
  int id; /* player who made claim */
  char *reason; /* if non-NULL, human readable explanation */
} CMsgClaimDeniedMsg;

/* Message sent by controller to implement a successful pung claim. */
typedef struct _CMsgPlayerPungsMsg {
  ControllerMsgType type; /* CMsgPlayerPungs */
  int id; /* player who made claim */
  Tile tile; /* tile of pung---redundant, but included for safety */
} CMsgPlayerPungsMsg;

/* Message to declare a closed pung during scoring */
typedef struct _PMsgFormClosedPungMsg {
  PlayerMsgType type; /* PMsgFormClosedPung */
  Tile tile;
} PMsgFormClosedPungMsg;

typedef struct _CMsgPlayerFormsClosedPungMsg {
  ControllerMsgType type; /* CMsgPlayerFormsClosedPung */
  int id;
  Tile tile;
} CMsgPlayerFormsClosedPungMsg;

/* now the same for kongs */
typedef struct _PMsgKongMsg { /* alias k */
  PlayerMsgType type; /* PMsgKong */
  int discard; /* id of discard being claimed */
} PMsgKongMsg;

/* Message sent by controller to other players to inform them of
   a pung claim (NOT of its success).
*/
typedef struct _CMsgPlayerClaimsKongMsg {
  ControllerMsgType type; /* CMsgPlayerClaimsKong */
  int id; /* player claiming */
  int discard; /* discard being claimed */
} CMsgPlayerClaimsKongMsg;

/* the generic refusal message applies */

/* Message sent by controller to implement a successful pung claim. */
typedef struct _CMsgPlayerKongsMsg {
  ControllerMsgType type; /* CMsgPlayerKongs */
  int id; /* player who made claim */
  Tile tile; /* tile of kong---redundant, but included for safety */
} CMsgPlayerKongsMsg;


/* Message sent by player to announce a concealed kong. */
typedef struct _PMsgDeclareClosedKongMsg { /* alias ck */
  PlayerMsgType type; /* PMsgDeclareClosedKong */
  Tile tile; /* which kong */
} PMsgDeclareClosedKongMsg;


/* Message sent by controller to implement a concealed kong
   Because the kong may possibly be robbed, it gets 
   a discard serial number. */
typedef struct _CMsgPlayerDeclaresClosedKongMsg {
  ControllerMsgType type; /* CMsgPlayerDeclaresClosedKong */
  int id; /* player declaring kong */
  Tile tile; /* tile being konged */
  int discard; /* serial */
} CMsgPlayerDeclaresClosedKongMsg;


/* Message sent by player to add drawn tile to exposed pung */
typedef struct _PMsgAddToPungMsg {
  PlayerMsgType type; /* PMsgAddToPung */
  Tile tile; /* which pung */
} PMsgAddToPungMsg;

/* Message sent by controller to implement adding to a pung
   Because the kong may possibly be robbed, it gets 
   a discard serial number. */
typedef struct _CMsgPlayerAddsToPungMsg {
  ControllerMsgType type; /* CMsgPlayerAddsToPung */
  int id; /* player */
  Tile tile; /* the tile being added */
  int discard; /* serial */
} CMsgPlayerAddsToPungMsg;

/* Message sent by controller to say that the player has
   robbed the just declared kong */
typedef struct _CMsgPlayerRobsKongMsg {
  ControllerMsgType type; /* CMsgPlayerRobsKong */
  int id; /* player */
  Tile tile; /* tile being robbed (redundant) */
} CMsgPlayerRobsKongMsg;

/* Message sent by player to ask whether it has a mah-jong hand.
   Needed because only the server knows fully what hands are allowed:
   the client code might be an earlier version. */
typedef struct _PMsgQueryMahJongMsg {
  PlayerMsgType type; /* PMsgQueryMahJong */
  Tile tile; /* do I have mahjong with this tile added? HiddenTile means no tile */
} PMsgQueryMahJongMsg;

/* And the reply */
typedef struct _CMsgCanMahJongMsg {
  ControllerMsgType type; /* CMsgCanMahJong */
  Tile tile; /* the tile specified */
  bool answer; /* yes or no */
} CMsgCanMahJongMsg;

/* Message sent by player to claim chow.
   The player should specify where the chowed tile will go
   (i.e. lower, upper, middle). However, for added realism,
   it is permissible to specify "any" (AnyPos) for the position.
   If this is done, and the chow claim is successful, then
   the controller will send to the chowing player (and only to
   the chowing player) a PlayerChows message with the cpos set
   to AnyPos. The player must then send another Chow message with
   the position specified.
*/
typedef struct _PMsgChowMsg { /* alias c */
  PlayerMsgType type; /* PMsgChow */
  int discard; /* discard being claimed */
  ChowPosition cpos; /* where the discard will go */
} PMsgChowMsg;

/* Message sent by controller to announce a chow claim */
typedef struct _CMsgPlayerClaimsChowMsg {
  ControllerMsgType type; /* CMsgPlayerClaimsChow */
  int id; /* player claiming */
  int discard; /* discard being claimed */
  ChowPosition cpos; /* chowposition specified (may be AnyPos) */
} CMsgPlayerClaimsChowMsg;

/* the general refusal message applies */

/* Message sent by controller to implement a chow claim.
   See above for meaning of this message with cpos == AnyPos.
 */
typedef struct _CMsgPlayerChowsMsg {
  ControllerMsgType type; /* CMsgPlayerChows */
  int id; /* player claiming */
  Tile tile; /* tile being claimed (redundant, but...) */
  ChowPosition cpos; /* position of claimed tile in chow */
} CMsgPlayerChowsMsg;

/* To form a closed chow during scoring.
   As all tiles are in hand, we require that the chow
   is specified by the lower tile.
*/
typedef struct _PMsgFormClosedChowMsg {
  PlayerMsgType type; /* PMsgFormClosedChow */
  Tile tile;
} PMsgFormClosedChowMsg;

typedef struct _CMsgPlayerFormsClosedChowMsg {
  ControllerMsgType type; /* CMsgPlayerFormsClosedChow */
  int id;
  Tile tile;
} CMsgPlayerFormsClosedChowMsg;

/* some rules allow a player to declare a washout under certain
   circumstances.
*/
typedef struct _PMsgDeclareWashOutMsg {
  PlayerMsgType type; /* PMsgDeclareWashOut */
} PMsgDeclareWashOutMsg;

/* Message sent by controller to declare a dead hand */
typedef struct _CMsgWashOutMsg {
  ControllerMsgType type; /* CMsgWashOut */
  char *reason; /* reason for washout */
} CMsgWashOutMsg;

/* Message sent by player to claim Mah-Jong.
   Applies to both claiming a discard and mah-jong from wall. 
*/
typedef struct _PMsgMahJongMsg {
  PlayerMsgType type; /* PMsgMahJong */
  int discard; /* discard being claimed, or 0 if from wall */
} PMsgMahJongMsg;

/* Message sent by controller to announce mah-jong claim */
typedef struct _CMsgPlayerClaimsMahJongMsg {
  ControllerMsgType type; /* CMsgPlayerClaimsMahJong */
  int id; /* player claiming */
  int discard; /* discard being claimed */
} CMsgPlayerClaimsMahJongMsg;


/* Message sent by controller to announce successful mah-jong */
typedef struct _CMsgPlayerMahJongsMsg {
  ControllerMsgType type; /* CMsgPlayerMahJongs */
  int id; /* player claiming */
  Tile tile; /* If this is HiddenTile, then the mahjong is by claiming
		the discard (the tile will be named in a later claim).
		If the mahjong is from the wall, then this is the tile
		that was drawn; this allows the other players to see
		the legitimacy of scoring claims. */
} CMsgPlayerMahJongsMsg;

/* Take the mah-jonged discard for a pair. Since this can only
   happen at mah-jong, there is no need to specify a tile or
   discard number */
typedef struct _PMsgPairMsg {
  PlayerMsgType type; /* PMsgPair */
} PMsgPairMsg;

typedef struct _CMsgPlayerPairsMsg {
  ControllerMsgType type; /* CMsgPlayerPairs */
  int id;
  Tile tile; /* for convenience of other players */
} CMsgPlayerPairsMsg;

/* Form a closed pair during scoring */
typedef struct _PMsgFormClosedPairMsg {
  PlayerMsgType type; /* PMsgFormClosedPair */
  Tile tile;
} PMsgFormClosedPairMsg;

typedef struct _CMsgPlayerFormsClosedPairMsg {
  ControllerMsgType type; /* CMsgPlayerFormsClosedPair */
  int id;
  Tile tile;
} CMsgPlayerFormsClosedPairMsg;

/* These messages handle the case of special hands such as
   thirteen unique wonders */
/* Claim the discard (during scoring) to form a special hand */
typedef struct _PMsgSpecialSetMsg {
  PlayerMsgType type; /* PMsgSpecialSet */
} PMsgSpecialSetMsg;

/* Player claims the discard to form a special set with all remaining tiles;
   this is a ShowTiles as well */
typedef struct _CMsgPlayerSpecialSetMsg {
  ControllerMsgType type ; /* CMsgPlayerSpecialSet */
  int id;
  Tile tile; /* the discard tile */
  char *tiles; /* string repn of the concealed tiles */
} CMsgPlayerSpecialSetMsg;

/* Form a special set in hand */
typedef struct _PMsgFormClosedSpecialSetMsg {
  PlayerMsgType type; /* PMsgFormClosedSpecialSet */
} PMsgFormClosedSpecialSetMsg;

/* Player forms a special set in hand from the concealed tiles */
typedef struct _CMsgPlayerFormsClosedSpecialSetMsg {
  ControllerMsgType type ; /* CMsgPlayerFormsClosedSpecialSet */
  int id;
  char *tiles; /* string repn of concealed tiles */
} CMsgPlayerFormsClosedSpecialSetMsg;

/* This message is sent by a losing player after it has finished
   making scoring combinations: it reveals the concealed tiles,
   and triggers the calculation of a score. */
typedef struct _PMsgShowTilesMsg {
  PlayerMsgType type; /* PMsgShowTiles */
} PMsgShowTilesMsg;

/* This message reveals a player's concealed tiles.
   It currently just has a string repn, because I'm lazy.
   This ought to be a variable array of tiles.
*/
typedef struct _CMsgPlayerShowsTilesMsg {
  ControllerMsgType type; /* CMsgPlayerShowsTiles */
  int id;
  char *tiles; /* string repn of concealed tiles */
} CMsgPlayerShowsTilesMsg;

/* This message gives the calculated score for a hand.
*/
typedef struct _CMsgHandScoreMsg {
  ControllerMsgType type; /* CMsgHandScore */
  int id;
  int score; /* score of this hand */
  char *explanation; /* human readable calculation of the score */
} CMsgHandScoreMsg;

/* This message announces the change in cumulative score for each player.
   The explanation is text, which should be interpreted cumulatively
   with any previous settlement explanations.
*/
typedef struct _CMsgSettlementMsg {
  ControllerMsgType type; /* CMsgSettlement */
  int east;
  int south;
  int west;
  int north;
  char *explanation;
} CMsgSettlementMsg;

/* This message requests the controller to set a player option,
   or acknowledges a server set. */
typedef struct _PMsgSetPlayerOptionMsg {
  PlayerMsgType type; /* PMsgSetPlayerOption */
  PlayerOption option; /* which option is being set */
  bool ack; /* 1 if this is acknowledging a server request */
  int value; /* value of option if int or bool */
  char *text; /* value of option if string */
} PMsgSetPlayerOptionMsg;

/* This informs the player that an option has been set.
   It may be a response to a request; it may also be autonomous,
   in which case the player should send a SetPlayerOption with
   the given value to acknowledge.*/
typedef struct _CMsgPlayerOptionSetMsg {
  ControllerMsgType type; /* CMsgPlayerOptionSet */
  PlayerOption option; /* which option is being set */
  bool ack; /* 1 if acknowledging a client request */
  int value; /* value of option if int */
  char *text; /* value of option if string */
} CMsgPlayerOptionSetMsg;

/* Message to tell players game is over */
typedef struct _CMsgGameOverMsg {
  ControllerMsgType type; /* CMsgGameOver */
} CMsgGameOverMsg;

/* Message to set a game option */
typedef struct _PMsgSetGameOptionMsg {
  PlayerMsgType type; /* PMsgSetGameOption */
  word16 optname;
  char *optvalue; /* this *is* a char*, it's a text representation of value */
} PMsgSetGameOptionMsg;

/* Message to inform players of a game option value */
typedef struct _CMsgGameOptionMsg {
  ControllerMsgType type; /* CMsgGameOption */
  int id; /* player who set it, if any */
  GameOptionEntry optentry;
} CMsgGameOptionMsg;

/* Message to query a game option. Controller will reply
 with a GameOptionMsg */
typedef struct _PMsgQueryGameOptionMsg {
  PlayerMsgType type; /* PMsgQueryGameOption */
  word16 optname; /* name of option to query */
} PMsgQueryGameOptionMsg;

/* Message to query all game options. Controller will reply
   with a sequence of GameOptionMsgs, finishing with the End pseudo-option */
typedef struct _PMsgListGameOptionsMsg {
  PlayerMsgType type; /* PMsgListGameOptions */
  bool include_disabled; /* send even disabled options */
} PMsgListGameOptionsMsg;

/* Request new manager for the game. */
typedef struct _PMsgChangeManagerMsg {
  PlayerMsgType type; /* PMsgChangeManager */
  int manager; /* new manager, or 0 if none */
} PMsgChangeManagerMsg;

/* Give new manager for the game. */
typedef struct _CMsgChangeManagerMsg {
  ControllerMsgType type; /* CMsgChangeManager */
  int id; /* player requesting change */
  int manager; /* new manager, or 0 if none */
} CMsgChangeManagerMsg;

/* Send a message to other player(s). */
typedef struct _PMsgSendMessageMsg {
  PlayerMsgType type; /* PMsgSendMessage */
  int addressee; /* id of addressee, or 0 for broadcast */
  char *text;
} PMsgSendMessageMsg;

/* Message from controller or other player. */
typedef struct _CMsgMessageMsg {
  ControllerMsgType type; /* CMsgMessage */
  int sender; /* id of sender, or 0 for message from controller */
  int addressee; /* 0 for broadcast, id of recipient otherwise */
  char *text;
} CMsgMessageMsg;

/* This message is primarily used by the controller internally.
   It sets up the wall with the given tiles */
typedef struct _CMsgWallMsg {
  ControllerMsgType type; /* CMsgWall */
  char *wall; /* wall as space separated tile codes */
} CMsgWallMsg;

/* This message serves no purpose in the protocol; it allows comments
   to be included in files of protocol commands. It is specially 
   hacked in the proto-encode scripts so that the message name
   is a hash sign # rather than the word Comment; for the decode
   script, we use the undocumented alias capability.
   Introduced at protocol 1030.
*/
typedef struct _CMsgCommentMsg { /* alias # */
  ControllerMsgType type; /* CMsgComment */
  char *comment;
} CMsgCommentMsg;

/* This message is for debugging and testing. It asks the controller
   to swap the old tile for the new tile. This is implemented by
   finding the new tile in the wall and swapping it with the old tile.
*/
typedef struct _PMsgSwapTileMsg { /* alias st */
  PlayerMsgType type; /* PMsgSwapTile */
  int id; /* player to be changed */
  Tile oldtile; 
  Tile newtile;
} PMsgSwapTileMsg;

typedef struct _CMsgSwapTileMsg {
  ControllerMsgType type; /* CMsgSwapTile */
  int id; /* player to be changed */
  Tile oldtile; 
  Tile newtile;
} CMsgSwapTileMsg;

/* dummy just to get the type field. This MUST come after all
 the real types (for auto-generation of printing and parsing) */
typedef struct _CMsgMsg {
  ControllerMsgType type;
} CMsgMsg;

typedef struct _PMsgMsg {
  PlayerMsgType type;
} PMsgMsg;

/* Two humungous unions for when they're wanted. Auto generated. */
#include "cmsg_union.h"

#include "pmsg_union.h"

/* after all that, some functions to handle conversion from/to
   the wire protocol */

/* The application programmer need only view the protocol as a sequence
   of lines. Lines may be terminated by CRLF, or LF.
   If the last character before the terminator is \ (backslash), then
   a newline is to be included in the "line", and the following
   wire line appended.
   The protocol is in fact human readable; it is described in protocol.c
*/

/* This function takes a pointer to a controller message,
   and returns a string (including terminating CRLF) encoding it.
   The returned string is in static storage, and will be overwritten
   the next time this function is called.
*/

char *encode_cmsg(CMsgMsg *msg);

/* This function takes a string, which is expected to be a complete
   line (including terminators, although their absence is ignored).
   It returns a pointer to a message structure.
   Both the message structure and any strings it contains are malloc'd.
   The argument string will be mutilated by this function.
*/
    
CMsgMsg *decode_cmsg(char *arg);


/* Text (that is, char *) elements of the protocol may contain
   special sequences of the form
   {CODE#TYPE...#TYPE:TEXT}{ARG}...{ARG}
   These are macros, which can be used to provide marked-up, rather than
   formatted messages.
   The CODE identifies the function the text serves (e.g. Pung claim,
   line of scoring details, etc.); each #TYPE notes an argument, with
   its type; and then the arguments follow. The TEXT provides a default
   text which can be used by a client that does not recognize the CODE;
   any occurrences of #n, for n = 1..9, in the TEXT should be replaced
   by the corresponding ARG, formatted according to its type; if the
   client does not recognize the type, arguments should be substituted
   as is.
   Each CODE or TYPE should be alphanumeric; they need not start
   with a letter. They may have a maximum length of 15 characters.
   Code definitions and type definitions are not strictly
   part of the protocol, and do not *require* protocol version changes.
   However, as a convenience, this file includes a function to do
   expansion with the currently defined codes and types, and the
   protocol minor version will be incremented when new codes and
   types are defined.
   All participants are required always to provide suitable default
   texts.  However, the following function is provided to do basic
   expansion of macros, recognizing no codes or types.
   NOTE: currently, no types or codes are defined.
   As a consequence of the above, any occurrence of {, }, # or \
   in text must be escaped with \.
   Note that substition is NOT recursive, nor are braces balanced:
   "{CODE: one { two } three }" will expand to
   " one { two  three }", possibly with a warning about an unescaped }.
*/

/* basic_expand_protocol_text:
   Perform text expansion as above, recognizing no codes or types.
   dest is the destination string, n its length (including terminating
   null).
   Returns 1 on success, 0 on internal errors (e.g. missing arguments),
   and -1 if dest is not long enough. dest will always be null-terminated.
   If src is null, expands to the empty string.
*/

int basic_expand_protocol_text(char *dest,const char *src,int n);

/* expand_protocol_text:
   Ditto, recognizing all currently defined codes and types.
*/
int expand_protocol_text(char *dest,const char *src,int n);

/* Similar functions for player messages */

char *encode_pmsg(PMsgMsg *msg);
PMsgMsg *decode_pmsg(char *arg);

/* functions given the size of a message type.
   If the size is negative, the last field is a char*
*/
int cmsg_size_of(ControllerMsgType t);
int pmsg_size_of(PlayerMsgType t);

/* functions to copy messages, mallocing space and 
   also mallocing any internal char stars, and to free
*/
CMsgMsg *cmsg_deepcopy(CMsgMsg *m);
PMsgMsg *pmsg_deepcopy(PMsgMsg *m);
void cmsg_deepfree(CMsgMsg *m);
void pmsg_deepfree(PMsgMsg *m);

/* enum parsing and printing functions (mainly for game options) */
#include "protocol-enums.h"

#endif /* PROTOCOL_H_INCLUDED */
