/*!
 * \file pattern_match.h
 * \brief Definitions for pattern matching functions.
 * \author Pierre van Heerden
 * \author Greg Cox
 *
 * Cobbled together from the pattern-matching code initially written by Greg Cox for the ACT.
 */

#ifndef __PATTERN_MATCH_H__
#define __PATTERN_MATCH_H__

#include <gtk/gtk.h>
#include "point_list.h"

#define DEFAULT_RADIUS  0.0148615804913014

typedef struct _point_map_t
{ 
  gint idx1, idx2;
  gfloat x1, x2;
  gfloat y1, y2;
} point_map_t;

/// \brief Thin wrapper around FindPointMapping
GSList *find_point_list_map(PointList *list1, PointList *list2, gfloat radius);
void point_list_map_calc_offset(GSList *map, gfloat *mean_x, gfloat *mean_y, gfloat *std_x, gfloat *std_y);
void point_list_map_free(GSList *map);

#endif