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

#include "point_list.h"

/// \brief Thin wrapper around FindPointMapping
gboolean FindListMapping(PointList *list1, PointList *list2, gint *list_map, gint max_map, gfloat radius);

#endif