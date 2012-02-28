/* $Header: /home/jcb/MahJong/newmj/RCS/controller.h,v 12.0 2009/06/28 20:43:12 jcb Rel $
 * controller.h
 * Contains type definitions etc used by the controller program.
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

#ifndef CONTROLLER_H_INCLUDED
#define CONTROLLER_H_INCLUDED 1

#include "tiles.h"
#include "player.h"
#include "protocol.h"
#include "game.h"

/* extra data in the game */
typedef struct {
  PlayerP caller; /* used to keep a copy of the winning player just
		     before mah-jong */
  /* array of pointers to CMsgs issued in this hand. This is
     used when reconnecting. The array is 512, since the maximum
     number of messages that can (relevantly) be issued in a hand
     is around 4 per tile, but that's highly unlikely ever to be
     reached -- but it can be, so say 1024. This ought to be dynamic... */
  int histcount;
  CMsgMsg *history[1024];
  /* this is used to keep the state at the start of the last
     hand, so we can print out the hand just completed */
  /* needs one Game Message and as many options as there may be */
  int prehistcount;
  CMsgMsg *prehistory[1+GOEnd];
  int completed_rounds; /* number of completed rounds. This ought to be
			   added to the Game structure and message, but
			   that's too much work, and we only really need it
			   internally. */
} GameExtras;
#define gextras(g) ((GameExtras *)(g->userdata))

#endif /* CONTROLLER_H_INCLUDED */
