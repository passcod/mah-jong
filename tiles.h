/* $Header: /home/jcb/MahJong/newmj/RCS/tiles.h,v 12.0 2009/06/28 20:43:13 jcb Rel $
 * tiles.h
 * Declares the datatype representing a tile, and the access functions.
 * At present, the access functions are actually macros, but you're not
 * supposed to know that.
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

#ifndef TILES_H_INCLUDED
#define TILES_H_INCLUDED

/* A tile is actually a (signed) short. */

typedef short Tile;

/* The internal representation is viewed as a two digit decimal number
   (for ease of debugging, ahem; but why not?).
   The first digit is the suit, the second the value. Specials hacked in.
   The code -1 is returned as an error.
   The code 0 denotes a blank tile. (Distinguished from White Dragon!).
   Otherwise: first digit 1,2,3 denotes characters,bamboo,circles,
   and second digit 1-9 gives the value;
   first digit 4 is winds, second digit 1,2,3,4 is E,S,W,N;
   first digit 5 is dragons, second digit 1,2,3 is Red,White,Green;
   first digit 6 is flowers, 1,2,3,4 is erm...whatever
   first digit 7 is seasons, 1,2,3,4 is erm...what order are they?
   (The fact that Winds, Dragons, etc are honorary suits is public
   information; but the is_suit function refers to real suits.
   Hm. This is maybe wrong.)

   The following commitments are made about these datatype; external
   code that relies on anything else is asking for trouble:
   (1) Tile is a signed integer (of some length) datatype.
   (2) the HiddenTile value is 0.
   (3) the ErrorTile value is -1.
   (4) within each suit, the tile values form consecutive integers;
       in particular, 1 or 2 can be added to/subtracted from a suit tile
       to obtain the tile 1 or 2 away.
   (5) for wind and dragon tiles, the value_of is the appropriate
       wind or dragon enum.
   (7) the flowers and seasons appear in the order corresponding to the winds,
       and the value_of a flower or season is the Wind corresponding to it.
*/

#define ErrorTile ((Tile) -1)
#define HiddenTile ((Tile) 0)
/* MaxTile is guaranteed to be bigger than any real tile value, and so
   can be used to specify the sizes of arrays that want to be indexed by
   tile values */
#define MaxTile 80

/* these values are not guaranteed */
typedef enum {
  BambooSuit = 1,
  CharacterSuit = 2,
  CircleSuit = 3,
  WindSuit = 4,
  DragonSuit = 5,
  FlowerSuit = 6,
  SeasonSuit = 7
} TileSuit ;

/* These values are guaranteed */
typedef enum {
  UnknownWind = 0, /* for convenience ... */
  EastWind = 1,
  SouthWind = 2,
  WestWind = 3,
  NorthWind = 4
} TileWind ;

/* These values are guaranteed */
typedef enum {
  RedDragon = 1,
  WhiteDragon = 2,
  GreenDragon = 3
} TileDragon ;

/* These values are guaranteed, by the requirement that
   flowers/seasons match their winds 
*/

typedef enum {
  Plum = 1,
  Orchid = 2,
  Chrysanthemum = 3,
  Bamboo = 4
} TileFlower;

typedef enum {
  Spring = 1,
  Summer = 2,
  Autumn = 3,
  Winter = 4
} TileSeason;

#define suit_of(t) ((TileSuit)((short) t/10))
#define value_of(t) ((short) t % 10)

#define is_suit(t) (suit_of(t) >= 1 && suit_of(t) <= 3)
#define is_wind(t) (suit_of(t) == (short)WindSuit)
#define is_dragon(t) (suit_of(t) == (short)DragonSuit)
#define is_season(t) (suit_of(t) == (short)SeasonSuit)
#define is_flower(t) (suit_of(t) == (short)FlowerSuit)

#define is_normal(t) (is_suit(t)||is_wind(t)||is_dragon(t))
#define is_special(t) (is_season(t)||is_flower(t))
#define is_honour(t) (is_wind(t)||is_dragon(t))
#define is_terminal(t) (is_suit(t) && (value_of(t) == 1 || value_of(t) == 9))
#define is_simple(t) (is_suit(t) && (value_of(t) >= 2 || value_of(t) <= 8))
#define is_minor is_simple
#define is_major(t) (is_honour(t) || is_terminal(t))
/* this says whether a tile is green in the sense of Imperial Jade */
int is_green(Tile t);

/* this is an array of thirteen tiles listing the 13 unique wonders */
extern Tile thirteen_wonders[];

/* this function is a convenience for iterating over all tiles.
   The initial argument should be HiddenTile; if then fed its result,
   it will generate all the tiles (in some order), finishing by 
   returning HiddenTile.
   The second argument says whether to include the flowers and seasons.
*/
Tile tile_iterate(Tile t,int includespecials);

/* return the next wind in playing order */
TileWind next_wind(TileWind w);

/* and the previous */
TileWind prev_wind(TileWind w);

/* make_tile: takes a suit and a value, and returns the tile.
   The value arg is an int, as it will frequently be calculated */

Tile make_tile(TileSuit s,int v);

/* tile_name: takes a tile and returns a string describing it
   The returned string is guaranteed to be in static storage and
   not to change.*/

const char *tile_name(Tile t);

/* tile_code: returns a two letter code for the tile.
   Result is static and immutable. 
*/
const char *tile_code(Tile t);

/* tile_decode: turn a two-letter code into a tile. */
Tile tile_decode(const char *c);

/* random_tiles: the argument should point to an array of tiles
   big enough to take all the required tiles.
   This function will deal tiles into the array randomly.
   The second argument says whether to include flowers and seasons.
*/
void random_tiles(Tile *tp,int includespecials);

#include "tiles-enums.h"

#endif /* TILES_H_INCLUDED */
