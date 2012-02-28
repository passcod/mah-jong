/* $Header: /home/jcb/MahJong/newmj/RCS/tiles.c,v 12.0 2009/06/28 20:43:13 jcb Rel $
 * tiles.c
 * contains the access functions for the tile datatype
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

static const char rcs_id[] = "$Header: /home/jcb/MahJong/newmj/RCS/tiles.c,v 12.0 2009/06/28 20:43:13 jcb Rel $";

#include "tiles.h"

/* This is used only to get a couple of defines */
#include "protocol.h"

/* required for host to network format conversion functions,
   and the warn function */

#include "sysdep.h"

/* see tiles.h for specifications of public functions */

#define tileit(s,v) ((Tile) (s*10 + v))

int is_green(Tile t) {
  int green;
  switch ( suit_of(t) ) {
  case DragonSuit:
    green = (value_of(t) == GreenDragon);
    break;
  case BambooSuit:
    switch ( value_of(t) ) {
    case 2:
    case 3:
    case 4:
    case 6:
    case 8:
      green = 1;
      break;
    default:
      green = 0;
    }
    break;
  default:
    green = 0;
  }
  return green;
}

Tile thirteen_wonders[] =
{ tileit(BambooSuit,1),
  tileit(BambooSuit,9),
  tileit(CharacterSuit,1),
  tileit(CharacterSuit,9),
  tileit(CircleSuit,1),
  tileit(CircleSuit,9),
  tileit(WindSuit,EastWind),
  tileit(WindSuit,SouthWind),
  tileit(WindSuit,WestWind),
  tileit(WindSuit,NorthWind),
  tileit(DragonSuit,RedDragon),
  tileit(DragonSuit,GreenDragon),
  tileit(DragonSuit,WhiteDragon)
};

TileWind next_wind(TileWind w) {
  switch ( w ) {
  case EastWind: return SouthWind;
  case SouthWind: return WestWind;
  case WestWind: return NorthWind;
  case NorthWind: return EastWind;
  default: return UnknownWind;
  }
}

TileWind prev_wind(TileWind w) {
  switch ( w ) {
  case EastWind: return NorthWind;
  case SouthWind: return EastWind;
  case WestWind: return SouthWind;
  case NorthWind: return WestWind;
  default: return UnknownWind;
  }
}

Tile make_tile(TileSuit s,int v) {
  if ( 1 <= s && s <= 3 ) {
    if ( 1 <= v && v <= 9 ) return tileit(s,v);
    else warn("make_tile: Bad value %d for suit %d\n",v,s);
    /* fall through to return of error at end ... */
  } else if ( s == WindSuit ) {
    if ( 1 <= v && v <= 4 ) return tileit(s,v);
    else warn("make_tile: Bad value %d for winds\n",v);
  } else if ( s == DragonSuit ) {
    if ( 1 <= v && v <= 3 ) return tileit(s,v);
    else warn("make_tile: Bad value %d for dragons\n",v);
  } else if ( s == FlowerSuit ) {
    if ( 1 <= v && v <= 4 ) return tileit(s,v);
    else warn("make_tile: Bad value %d for flowers\n",v);
  } else if ( s == SeasonSuit ) {
    if ( 1 <= v && v <= 4 ) return tileit(s,v);
    else warn("make_tile: Bad value %d for seasons\n",v);
  }
  return ErrorTile;
}

/* This wastes quite a lot of memory, in old-timers' terms; but these
   days, who worries about a few hundred bytes?
   tilenames is a [suit][value] array.
*/

#define MAXTNLENGTH 20 /* max length of a tile name string, including
			the null terminator
			WARNING: inittilenames assumes this value
			is indeed a bound */

static char tilenames[8][10][MAXTNLENGTH];

static int tilenamesinited = 0;

static void inittilenames(void) {
  int i;

  for ( i = 1 ; i <= 9 ; i++ ) {
    sprintf((char *)(tilenames[CharacterSuit][i]),
	    "%d of Characters",i);
    sprintf((char *)(tilenames[BambooSuit][i]),
	    "%d of Bamboo",i);
    sprintf((char *)(tilenames[CircleSuit][i]),
	    "%d of Circles",i);
  }

  strcpy((char *)(tilenames[DragonSuit][RedDragon]),"Red Dragon");
  strcpy((char *)(tilenames[DragonSuit][GreenDragon]),"Green Dragon");
  strcpy((char *)(tilenames[DragonSuit][WhiteDragon]),"White Dragon");

  strcpy((char *)(tilenames[WindSuit][EastWind]),"East Wind");
  strcpy((char *)(tilenames[WindSuit][SouthWind]),"South Wind");
  strcpy((char *)(tilenames[WindSuit][WestWind]),"West Wind");
  strcpy((char *)(tilenames[WindSuit][NorthWind]),"North Wind");

  strcpy((char *)(tilenames[FlowerSuit][Plum]),"Plum");
  strcpy((char *)(tilenames[FlowerSuit][Orchid]),"Orchid");
  strcpy((char *)(tilenames[FlowerSuit][Chrysanthemum]),"Chrysanthemum");
  strcpy((char *)(tilenames[FlowerSuit][Bamboo]),"Bamboo");

  strcpy((char *)(tilenames[SeasonSuit][Spring]),"Spring");
  strcpy((char *)(tilenames[SeasonSuit][Summer]),"Summer");
  strcpy((char *)(tilenames[SeasonSuit][Autumn]),"Autumn");
  strcpy((char *)(tilenames[SeasonSuit][Winter]),"Winter");

  strcpy((char *)(tilenames[0][0]),"blank");

  tilenamesinited = 1;
}


const char *tile_name(Tile t) {

  if ( ! tilenamesinited ) inittilenames();

  if (t == ErrorTile) {
    static const char erstr[] = "Error";
    warn("tile_name: called on error tile\n");
    return erstr;
  }
  return tilenames[suit_of(t)][value_of(t)];
}

/* These functions give two letter codes for tiles. They are
   mainly used in the protocol module.
*/

/* two letter codes for tiles */
/* N.B. The zero values are errors. */
static char *chartiles[] = { "XX", "1C", "2C", "3C", "4C", "5C", "6C", "7C", "8C", "9C" }; 
static char *dottiles[] = { "XX", "1D", "2D", "3D", "4D", "5D", "6D", "7D", "8D", "9D" }; 
static char *bamtiles[] = { "XX", "1B", "2B", "3B", "4B", "5B", "6B", "7B", "8B", "9B" }; 


/* Given a tile, return its two letter string. The string must be
   in static storage and immutable.
*/
const char *tile_code(Tile t) {
  if ( t == HiddenTile ) return "--";
  if ( suit_of(t) == CharacterSuit ) return chartiles[value_of(t)];
  if ( suit_of(t) == CircleSuit ) return dottiles[value_of(t)];
  if ( suit_of(t) == BambooSuit ) return bamtiles[value_of(t)];
  if ( suit_of(t) == DragonSuit )
    switch ( value_of(t) ) {
    case RedDragon: return "RD";
    case GreenDragon: return "GD";
    case WhiteDragon: return "WD";
    }
  if ( suit_of(t) == WindSuit )
    switch ( value_of(t) ) {
    case EastWind: return "EW";
    case SouthWind: return "SW";
    case WestWind: return "WW";
    case NorthWind: return "NW";
    }
  if ( suit_of(t) == SeasonSuit )
    switch ( value_of(t) ) {
    case Spring: return "1S";
    case Summer: return "2S";
    case Autumn: return "3S";
    case Winter: return "4S";
    }
  if ( suit_of(t) == FlowerSuit )
    switch ( value_of(t) ) {
    case Plum: return "1F";
    case Orchid: return "2F";
    case Chrysanthemum: return "3F";
    case Bamboo: return "4F";
    }
  return "XX" ; /* error */
}

/* Given a two letter string, return a tile */
Tile tile_decode(const char *s) {
  /* errors will anyway generate ErrorTile; we don't need
     to validate the string ourselves */
  if ( s[1] == 'B' ) return make_tile(BambooSuit, s[0]-'0');
  if ( s[1] == 'C' ) return make_tile(CharacterSuit, s[0]-'0');
  if ( s[1] == 'D' && isdigit(s[0]) ) return make_tile(CircleSuit, s[0]-'0');
  if ( s[1] == 'D' ) return make_tile(DragonSuit,
				      s[0] == 'R' ? RedDragon :
				      s[0] == 'G' ? GreenDragon :
				      s[0] == 'W' ? WhiteDragon :
				      0);
  if ( s[1] == 'W' ) return make_tile(WindSuit,
				      s[0] == 'E' ? EastWind :
				      s[0] == 'S' ? SouthWind :
				      s[0] == 'W' ? WestWind :
				      s[0] == 'N' ? NorthWind :
				      0);
  if ( s[1] == 'F' ) return make_tile(FlowerSuit,
				      s[0] == '1' ? Plum :
				      s[0] == '2' ? Orchid :
				      s[0] == '3' ? Chrysanthemum :
				      s[0] == '4' ? Bamboo :
				      0);
  if ( s[1] == 'S' ) return make_tile(SeasonSuit,
				      s[0] == '1' ? Spring :
				      s[0] == '2' ? Summer :
				      s[0] == '3' ? Autumn :
				      s[0] == '4' ? Winter :
				      0);
  if ( s[0] == '-' && s[1] == '-' ) return HiddenTile;
  return ErrorTile;
}

/* see tiles.h
*/
void random_tiles(Tile *tp, int includespecials) {
  Tile tmp[MAX_WALL_SIZE],t;
  int i;
  int j;
  int tiles_left;

  /* fill the tmp array with all the tiles */
  i = 0;
  t = HiddenTile;
  while ( (t = tile_iterate(t,includespecials)) != HiddenTile ) {
    /* if it's a special we want one of them, else we want four */
    tmp[i++] = t;
    if ( ! is_special(t) ) {
      tmp[i++] = t;
      tmp[i++] = t;
      tmp[i++] = t;
    }
  }

  /* Now pick a tile randomly, and put in it in the next slot
     of the argument array */
  tiles_left = i;
  i = 0;
  while ( tiles_left > 0 ) {
    if ( tiles_left == 1 ) j = 0;
    else j = rand_index(tiles_left-1); /* random int from 0 to arg */
    tp[i++] = tmp[j];
    /* copy top tile into this place, and decrement top */
    tmp[j] = tmp[--tiles_left];
  }
}

Tile tile_iterate(Tile t,int includespecials) {
  int s,v;

  if ( t == HiddenTile ) return make_tile(BambooSuit,1);
  s = suit_of(t);
  v = value_of(t);
  if ( s == BambooSuit ) {
    if ( v < 9 ) return make_tile(s,v+1);
    else return make_tile(CharacterSuit,1);
  } else if ( s == CharacterSuit ) {
    if ( v < 9 ) return make_tile(s,v+1);
    else return make_tile(CircleSuit,1);
  } else if ( s == CircleSuit ) {
    if ( v < 9 ) return make_tile(s,v+1);
    else return make_tile(WindSuit,1);
  } else if ( s == WindSuit ) {
    if ( v < 4 ) return make_tile(s,v+1);
    else return make_tile(DragonSuit,1);
  } else if ( s == DragonSuit ) {
    if ( v < 3 ) return make_tile(s,v+1);
    else return includespecials ? make_tile(FlowerSuit,1) : HiddenTile;
  } else if ( s == FlowerSuit ) {
    if ( v < 4 ) return make_tile(s,v+1);
    else return make_tile(SeasonSuit,1);
  } else if ( s == SeasonSuit ) {
    if ( v < 4 ) return make_tile(s,v+1);
    else return HiddenTile;
  }
  warn("Bad argument to tile_iterate");
  return ErrorTile;
}

#include "tiles-enums.c"
