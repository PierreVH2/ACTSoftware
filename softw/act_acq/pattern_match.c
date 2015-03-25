#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include "pattern_match.h"

struct point
{
  /// X-coordinate of point on acquisition image.
  float x;
  /// Y-coordinate of point on acquisition image.
  float y;
};

void FindPointMapping (struct point *pList1, int nPts1, struct point *pList2, int nPts2, int** nMap, int* nMatch, float radius);

gboolean FindListMapping(PointList *list1, PointList *list2, gint *list_map, gint max_map, gfloat radius)
{
  int num1 = point_list_get_num_used(list1), num2 = point_list_get_num_used(list2);
  if (max_map <= num1)
    return FALSE;
  int *point_map, num_map, i;
  struct point *points1=calloc(num1, sizeof(struct point)), *points2=calloc(num2, sizeof(struct point));
  gdouble x, y;
  for (i=0; i<num1; i++)
  {
    if (!point_list_get_coord(list1, i, &x, &y))
      continue;
    points1[i].x = x;
    points1[i].y = y;
  }
  for (i=0; i<num2; i++)
  {
    if (!point_list_get_coord(list2, i, &x, &y))
      continue;
    points2[i].x = x;
    points2[i].y = y;
  }
  FindPointMapping (points1, num1, points2, num2, &point_map, &num_map, radius);
  if (num_map <= 1)
    return FALSE;
  if (num_map >= max_map)
    return FALSE;
  memcpy((void *)list_map, (void *)point_map, num_map*sizeof(int));
  return TRUE;
}

void FindPointMapping (struct point *pList1, int nPts1, struct point *pList2, int nPts2, int** nMap, int* nMatch, float radius)
{
  struct point *dList;
  int *dCount, *cList, *cBest;
  int nDist;
  int i, j, d, m;
  int nFound;
  float dx, dy;
  int nMax, nPairs, nPairsBest;
  
  dList = (struct point*) calloc( nPts1*nPts2, sizeof(struct point) );
  dCount = (int*) calloc( nPts1*nPts2, sizeof(int) );
  cList = (int*) calloc( nPts1*nPts2, sizeof(int) );
  cBest = (int*) calloc( nPts1*nPts2, sizeof(int) );
  
  if ((dList == NULL) || (dCount == NULL) || (cList == NULL) || (cBest == NULL))
  {
    *nMap = NULL;
    *nMatch = -1;
    return;
  }
  
  /* find and cluster all interpoint distances */
  nDist = 0;
  for (i=0; i<nPts1; i++)
  {
    for (j=0; j<nPts2; j++)
    {
      dx = pList1[i].x - pList2[j].x;
      dy = pList1[i].y - pList2[j].y;
      
      nFound = 0;
      for (d=0; d<nDist; d++)
      {
        if ( (fabs(dList[d].x/dCount[d] - dx) < radius) 
          && (fabs(dList[d].y/dCount[d] - dy) < radius) )
        {
          nFound = 1;
          
          dList[d].x += dx;
          dList[d].y += dy;                                       
          dCount[d]++;
        }
      }
      
      if (!nFound)
      {
        dList[nDist].x = dx;
        dList[nDist].y = dy;
        dCount[nDist] = 1;
        
        nDist++;
      }
    }
  }
    
  /* get means of distance clusters */
  for (d=0; d<nDist; d++)
  {
    dList[d].x /= dCount[d];
    dList[d].y /= dCount[d];
  }
  
  #ifdef DEBUG
  for (i=0; i<nDist; i++)
    printf( "%4d  %9.3f  %9.3f %d\n", i, dList[i].x, dList[i].y, dCount[i] );
  #endif
  
  /* find largest point mapping */
  nPairsBest = 0;
  while (1)
  {
    m = -1;
    nMax = 1;
    for (d=0; d<nDist; d++)
    {
      if (nMax < dCount[d])
      {
        m = d;
        nMax = dCount[d];
      }
    }
    
    /* stop if no more possibilities exist */
    if (m == -1)
      break;
    
    #ifdef DEBUG
      printf( "Offset (%f,%f)\n", dList[m].x, dList[m].y );
      #endif
      
      /* only examine this option if it can possibly be larger than the current best */
      if (dCount[m] > nPairsBest) 
      {
        nPairs = 0;
        for (i=0; i<nPts1; i++)
        {
          cList[i] = -1;
          for (j=0; j<nPts2; j++)
          {
            dx = pList1[i].x - pList2[j].x - dList[m].x;
            dy = pList1[i].y - pList2[j].y - dList[m].y;
            
            if ( fabs(dx) < radius && fabs(dy) < radius )
            {
              cList[i] = j;
              nPairs++;
              break;
            }
          }
        }
        
        if (nPairs > nPairsBest)
        {
          int* tmp;
          
          tmp = cBest;
          cBest = cList;
          cList = tmp;
          
          nPairsBest = nPairs;
        }
      }
      
      /* don't try this option again */
      dCount[m] = 0;
  }
  
  #ifdef DEBUG
  for (i=0; i<nPts1; i++)
    printf( "%4d -> %4d\n", i, cBest[i] );
  #endif
  
  if (nPairsBest < 2)
    *nMatch = 0;
  else
    *nMatch = nPairsBest;
  
  *nMap = cBest;
  
  free( dList );
  free( dCount );
  free( cList );
}
