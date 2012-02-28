/* $Header: /home/jcb/MahJong/newmj/RCS/scoring.h,v 12.0 2009/06/28 20:43:13 jcb Rel $
 * scoring.h
 * header file for scoring routines.
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

#ifndef SCORING_H_INCLUDED
#define SCORING_H_INCLUDED 1

#include "game.h"

/* This structure is used to return score components.
   The value is either a number of points, or a number of doubles,
   as appropriate; the explanation is a human readable explanation
   of this score.
   The explanation must be copied before another scoring function
   is called.
*/
typedef struct _Score {
  int value;
  char *explanation;
} Score;

Score score_of_hand(Game *g, seats s);

/* this variable disables scoring for flowers and seasons */
extern int no_special_scores;

#endif /* SCORING_H_INCLUDED */
