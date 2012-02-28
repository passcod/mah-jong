/* $Header: /home/jcb/MahJong/newmj/RCS/client.h,v 12.0 2009/06/28 20:43:12 jcb Rel $
 * client.h
 * header file for client support routines.
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

#ifndef CLIENT_H_INCLUDED
#define CLIENT_H_INCLUDED 1

#include "game.h"

/* client_init: takes an address, attempts to connect to
   a controller on that address. Internally, allocates game structure etc.
   Returns a pointer to the allocated game structure, or NULL.
   It's a documented feature that if the address is "-",
   then the game fd will be set to be STDOUT. The get_line
   function will automatically read from STDIN if this is the case.
*/
Game  *client_init(char *address);

/* client_reinit: the same, but uses already established network
   connection passed as fd/handle */
Game *client_reinit(int fd);

/* client_connect: take an id and a name, and send a connect message.
   Return 1 on success, or 0 on failure. */
int client_connect(Game *g, int id, char *name);

/* client_close: given game, close the connection and deallocate
   the storage. Returns NULL.
*/
Game *client_close(Game *g);

/* client_close_keepconnection: as above, but doesn't actually
   close the connection */
Game *client_close_keepconnection(Game *g);

/* client_send_packet sends a packet to the controller.
   The return value is 0 on failure, or the sequence number of the packet 
   on success. N.B. If packets are sent bypassing this routine, the
   sequence numbers will not be correct, unless the user updates the
   game's cseqno field explicitly.
*/
int client_send_packet(Game *g, PMsgMsg *m);

/* client_find_sets: this takes a player and a discard (or HiddenTile)
   and finds sets that can be declared.
   If a discard is supplied, only sets involving the discard
   are returned.
   It returns a pointer to an array of TileSets, terminated by
   an Empty TileSet; but if no sets are found, NULL is returned.
   The returned array is in static storage, and is valid
   until the next call of this function.
   If the mj flag is 1, then the hand is assumed to be complete,
   and the sets must lead to a complete hand. Otherwise, all possible
   declarations are returned.
   The sets are returned with pungs first, then pairs, then chows.
   This is so that scores for fishing the eyes are obtained when
   this function is used for auto-declaration.
   Prior to 11.4, chows were returned before pairs.
   The return value is a TileSet: empty if no set could be found.
   NOTE: kongs are never returned if either there is no discard (obviously)
   or if the mj flag is true (equally obviously).
   As a convenience, if the pcopies argument is nonnull, then it will be
   updated to point to an array of players, which are the results of
   executing the related sets. These are also in static storage.
   (N.B. For a kong declaration, the associated player is not valid.)
   Final arg is flags for non-standard hands.
*/
TileSet *client_find_sets(PlayerP p, Tile d, int mj, PlayerP *pcopies,
			  MJSpecialHandFlags flags);
#endif /* CLIENT_H_INCLUDED */
