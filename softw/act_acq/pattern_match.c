#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pattern_match.h"

#define DS 5
#define DL 33
#define RADIUS 3.0


/***********************************************************************
 * 
 *  Routine Name: Subtract
 *          Date: 16 August 1995      
 *       Purpose: Subtracts one image from another
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Float images only
 * Modifications:
 *
 ***********************************************************************/
int Subtract (float *pfImage1, float *pfImage2, float *pfImageDest, int nPels)
{
  if ((pfImage1 == NULL) || (pfImage2 == NULL) || (pfImageDest == NULL) || (nPels <= 0))
    return -1;
  
  int i;
  for (i=0; i<nPels; i++)
    pfImageDest[i] = pfImage1[i] - pfImage2[i];
  
  return 0;
}

void ComputeBlobFeatures( float* pfDiff, unsigned short* psSegment, int nBlobs, int nRows, int nCols, struct blob* pBlob )
{
  float fWeight;
  int i, r, c;
  
  #ifdef DEBUG
  printf( "%d\n", nBlobs );
  #endif
  
  for (i=1; i<=nBlobs; i++)
  {
    pBlob[i-1].x = pBlob[i-1].y = pBlob[i-1].size = pBlob[i-1].integral = 0.0;
    
    for (r=0; r<nRows; r++) 
      for (c=0; c<nCols; c++)
        if (psSegment[r*nCols+c] == i)
        {
          fWeight = pfDiff[r*nCols+c];
          
          pBlob[i-1].y += r*fWeight;
          pBlob[i-1].x += c*fWeight;
          pBlob[i-1].integral += fWeight;
          pBlob[i-1].size++;
        }
        
        pBlob[i-1].x /= pBlob[i-1].integral;
        pBlob[i-1].y /= pBlob[i-1].integral;
  }
}

/***********************************************************************
 * 
 *  Routine Name: Multiply
 *          Date: 27 May 1996     
 *       Purpose:  Multiplies one image by another
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Float images only
 * Modifications:
 *
 ***********************************************************************/
int Multiply(float *pfImage1, float *pfImage2, float *pfImageDest, int nPels )
{
  if ((pfImage1 == NULL) || (pfImage2 == NULL) || (pfImageDest == NULL) || (nPels <= 0))
    return -1;
  
  int i;
  for (i=0; i<nPels; i++)
    pfImageDest[i] = pfImage1[i] * pfImage2[i];
  
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: ThresholdToShort
 *          Date: 9 September 1998
 *       Purpose: Thresholds an image
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Float images only
 * Modifications:
 *
 ***********************************************************************/
int ThresholdToShort (float* pfIn, float fThresh, unsigned short* psOut, int nPels)
{
  if ((pfIn == NULL) || (psOut == NULL) || (nPels <= 0))
    return 0;
  
  int i;
  for (i=0; i<nPels; i++)
  {
    if (pfIn[i] > fThresh)
      psOut[i] = 1;
    else
      psOut[i] = 0;
  }
  
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: FastLocalMean
 *          Date:  22 April 1996
 *       Purpose:  Calculates an image of local means (fast)
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Only for square masks
 *                
 *   Modifications:
 *   1996/07/19 nicolls
 *   Fixed up an alignment problem which was causing all pixels to be
 *   displaced one pixel downwards and to the right.
 *
 ***********************************************************************/
int FastLocalMean (float* fImgIn, int nDim, int nRows, int nCols, float* fMean)
{
  if ((fImgIn == NULL) || (fMean == NULL) || (nDim <= 0) || (nRows <= 0) || (nCols <= 0))
    return -1;
  
  int r, c;
  double fColTotal;
  int rBuf, rBufPrev, rBufTop;
  int nHalfDim, nBufferRows, nPels;
  
  nBufferRows = nDim+1;
  nHalfDim = nDim/2;
  nPels = nDim*nDim;
  
  double fColCum[nCols], fRowCum[nCols * nBufferRows];
  
  if ((fColCum == NULL) || (fRowCum == NULL))
    return -1;
  
  for (r=0; r<nDim; r++)
  {
    rBuf = r % nBufferRows;
    rBufPrev = (r-1) % nBufferRows;
    fColCum[0] = fImgIn[r*nCols];
    
    for (c=1; c<nDim; c++)
      fColCum[c] = fColCum[c-1] + fImgIn[c + r*nCols];
    
    for (c=nDim; c<nCols; c++)
    {
      fColCum[c] = fColCum[c-1] + fImgIn[c + r*nCols];
      fColTotal = fColCum[c] - fColCum[c - nDim];
      if (r>0)
        fRowCum[c + rBuf*nCols] = fRowCum[c + rBufPrev*nCols] + fColTotal;
      else
        fRowCum[c + rBuf*nCols] = fColTotal;
    }
  }
  
  for (r=nDim; r<nRows; r++)
  {
    rBuf = r % nBufferRows;
    rBufPrev = (r-1) % nBufferRows;
    rBufTop = (r-nDim) % nBufferRows;
    
    fColCum[0] = fImgIn[r*nCols];
    
    for (c=1; c<nDim; c++)
      fColCum[c] = fColCum[c-1] + fImgIn[c + r*nCols];
    
    for (c=nDim; c<nCols; c++)
    {
      /* accumulate this pixel value */
      fColCum[c] = fColCum[c-1] + fImgIn[c + r*nCols];
      
      /* total over this horizontal line of the mask */
      fColTotal = fColCum[c] - fColCum[c - nDim];
      
      /* accumulate this horizontal ine of the mask */
      fRowCum[c + rBuf*nCols] = fRowCum[c + rBufPrev*nCols] + fColTotal;
      
      /* subtract mask average from the appropriate image pixel value */
      fMean[(c-(nDim-1)+nHalfDim) + (r-(nDim-1)+nHalfDim)*nCols] = (float) ((fRowCum[c + rBuf*nCols] - fRowCum[c + rBufTop*nCols])/nPels);
    }
  }               
  
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: FastLocalVariance
 *          Date: 9 September 1998
 *       Purpose: Calculates an image of local variances (fast)
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Only for square masks
 *                
 *   Modifications:
 *
 ***********************************************************************/
int FastLocalVariance (float* pfImage, float* pfMean, int nDim, int nRows, int nCols, float* pfVar)
{
  if ((pfImage == NULL) || (pfMean == NULL) || (pfVar == NULL) || (nDim <= 0) || (nRows <= 0) || (nCols <= 0))
    return -1;
  
  float pfSquare[nRows*nCols], pfSumSq[nRows*nCols];
  
  Multiply (pfImage, pfImage, pfSquare, nRows*nCols);
  FastLocalMean (pfSquare, nDim, nRows, nCols, pfSumSq);
  /* MultiplyByScalar( pfSumSq, (float) (nDim*nDim), pfSumSq, nRows*nCols ); */
  
  Multiply (pfMean, pfMean, pfSquare, nRows*nCols);
  /* MultiplyByScalar( pfSquare, (float) (nDim*nDim), pfSquare, nRows*nCols ); */
  
  Subtract( pfSumSq, pfSquare, pfVar, nRows*nCols );
  
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: SquareRoot
 *          Date: 16 August 1995      
 *       Purpose: Square root of the pixels in an image
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Float images only
 * Modifications:
 *
 ***********************************************************************/
int SquareRoot (float *pfImage, float *pfSquareRoot, int nPels)
{
  if ((pfImage == NULL) || (pfSquareRoot == NULL) || (nPels <= 0))
    return -1;
  
  int i;
  for (i=0; i<nPels; i++)
    pfSquareRoot[i] = (float) sqrt( (double) pfImage[i] );
  
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: Divide
 *          Date: 27 May 1996
 *       Purpose:  Multiplies one image by another
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Float images only
 * Modifications:
 *
 ***********************************************************************/
int Divide (float *pfImage1, float *pfImage2, float *pfImageDest, int nPels)
{
  if ((pfImage1 == NULL) || (pfImage2 == NULL) || (pfImageDest == NULL) || (nPels <= 0))
    return -1;
  
  int i;
  for (i=0; i<nPels; i++)
    pfImageDest[i] = pfImage1[i] / pfImage2[i];
  
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: MaskRectangle
 *          Date:  27 May 1996  
 *       Purpose: Masks a rectangular region in the image.  Pixels out-
 *                     side the rectangle are set to zero.
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: - Float images only
 *                   - in place operation
 * Modifications:
 *
 ***********************************************************************/
int MaskRectangle (float *pfImage, int nTop, int nLeft, int nBottom, int nRight, int nRows, int nCols)
{
  if ((pfImage == NULL) || (nRows <= 0) || (nCols <= 0))
    return -1;
  // ??? Check other parameters?
    
  int r, c;
  for (r=0; r<nRows; r++)
  {
    for (c=0; c<nCols; c++)
    {
      if ((r>nBottom) || (r<nTop) || (c<nLeft) || (c>nRight))
        *(pfImage + c + r*nCols) = 0.0;
    }
  }
      
  return 0;
}

/***********************************************************************
 * 
 *  Routine Name: Clip
 *          Date: 9 September 1998
 *       Purpose: Clips an image to upper and lower bounds
 *
 *         Calls:  
 *
 *    Written By: Greg Cox
 *          Note: Float images only
 * Modifications:
 *
 ***********************************************************************/
int Clip (float* pfIn, float fLower, float fLVal, float fUpper, float fUVal, float* pfOut, int nPels)
{
  if ((pfIn == NULL) || (pfOut == NULL) || (nPels <= 0))
    return -1;
  
  int i;
  for (i=0; i<nPels; i++)
  {
    pfOut[i] = pfIn[i];
    
    if (pfIn[i] > fUpper)
      pfOut[i] = fUVal;
    
    if (pfIn[i] < fLower)
      pfOut[i] = fLVal;
  }
  
  return 0;
}

int SegmentBlobs (float* pfImage, int nRows, int nCols, float* pfDiff)
{
  if ((pfImage == NULL) || (pfDiff == NULL) || (nRows <= 0) || (nCols <= 0))
    return -1;
  float pfMeanS[nRows*nCols], pfMeanL[nRows*nCols], pfVar[nRows*nCols];
  int ret;
  
  ret = FastLocalMean (pfImage, DS, nRows, nCols, pfMeanS);
  if (ret < 0)
    return ret;
  ret = FastLocalMean (pfImage, DL, nRows, nCols, pfMeanL);
  if (ret < 0)
    return ret;
  ret = Subtract (pfMeanS, pfMeanL, pfDiff, nRows*nCols);
  if (ret < 0)
    return ret;
  
  FastLocalVariance (pfImage, pfMeanL, DL, nRows, nCols, pfVar);
  SquareRoot (pfVar, pfVar, nRows*nCols);
  
  Divide (pfDiff, pfVar, pfDiff, nRows*nCols);
  
  MaskRectangle (pfDiff, DL/2, DL/2, nRows-DL/2, nCols-DL/2, nRows, nCols);
  
  Clip (pfDiff, 0.8f, 0.0f, 10.0f, 10.0f, pfDiff, nRows*nCols);
  return 0;
}

int CheckNeighbour (unsigned short *pusImage, int nRow, int nCol, int nDR, int nDC, int nCols)               
{
  if ((pusImage == NULL) || (nRow <= 0) || (nCol <= 0) || (nCols <= 0))
    return -1;
  
  if (
      (pusImage[nCol+nDC + (nRow+nDR)*nCols] != 0) &&
      (pusImage[nCol+nDC + (nRow+nDR)*nCols] != pusImage[nCol + nCols*nRow])
     )
  {
    if (pusImage[nCol+nDC + (nRow+nDR)*nCols] > pusImage[nCol + nCols*nRow])
      pusImage[nCol+nDC + (nRow+nDR)*nCols] = pusImage[nCol + nCols*nRow];
    else
      pusImage[nCol + nCols*nRow] = pusImage[nCol+nDC + (nRow+nDR)*nCols];
    return 1;
  }
  return 0;
}

int Binary2LabelledRegions (unsigned short *pusBinary, int nRows, int nCols, int *nFoundSites)
{ 
  if ((pusBinary == NULL) || (nRows <= 0) || (nCols <= 0))
    return -1;
  
  int c, r;
  int uiSiteNumber=2;                  /* Start site numbering from 2 */
  int nLabelChange=0, nEquivIterations=0;
  
  /* Reset pixel-wide border to zero */
  // ??? replace with memset?
  for (r=0, c=0; r<nRows; r++)
    pusBinary[c + r*nCols] = 0;
  for (r=0, c=nCols-1; r<nRows; r++)
    pusBinary[c + r*nCols] = 0;
  for (c=0, r=0; c<nCols; c++)
    pusBinary[c + r*nCols] = 0;
  for (c=0, r=nRows-1; c<nCols; c++)
    pusBinary[c + r*nCols] = 0;
  
  /* Scan binary image pixel by pixel and number sites */
  uiSiteNumber=2; /* Label first site with a 2 */
  for (r=1; r<nRows-1; r++)
  {
    for (c=1; c<nCols-1; c++)
    {
      if (pusBinary[c + r*nCols]==1)     /* pixel belongs to a site */
      {
        /* Check for 8-connectivity : pixel belongs to previously numbered site.*/
        if (pusBinary[c-1 + (r-1)*nCols] != 0)
          pusBinary[c + r*nCols] = pusBinary[c-1 + (r-1)*nCols];
        if (pusBinary[c+1 + (r-1)*nCols] != 0)
          pusBinary[c + r*nCols] = pusBinary[c+1 + (r-1)*nCols];
        if (pusBinary[c + (r-1)*nCols] != 0)
          pusBinary[c + r*nCols] = pusBinary[c + (r-1)*nCols];
        if (pusBinary[c-1 + r*nCols] != 0)
          pusBinary[c + r*nCols] = pusBinary[c-1 + r*nCols]; 
        
        /* Pixel belongs to a new, as yet unnumbered, site */
        if (pusBinary[c + r*nCols]==1)
          pusBinary[c + r*nCols] = uiSiteNumber++ ;
      }
    }
  }
    
  /* Eliminate equivalencies in the labelling */
  do
  {
    nLabelChange = 0; /* reset label change flag */
    nEquivIterations ++;
    for (r=1; r<nRows-1; r++)
    {
      for (c=1; c<nCols-1; c++)
      {
        if (pusBinary[c + r*nCols] != 0)
        {
          if ( 
              CheckNeighbour (pusBinary, r, c, -1, -1, nCols) ||
              CheckNeighbour (pusBinary, r, c, -1, +0, nCols) ||
              CheckNeighbour (pusBinary, r, c, -1, +1, nCols) ||
              CheckNeighbour (pusBinary, r, c, +0, -1, nCols) ||
              CheckNeighbour (pusBinary, r, c, +0, +1, nCols) ||
              CheckNeighbour (pusBinary, r, c, +1, -1, nCols) ||
              CheckNeighbour (pusBinary, r, c, +1, +0, nCols) ||
              CheckNeighbour (pusBinary, r, c, +1, +1, nCols) 
             )
            nLabelChange=1;
        }
      }
    }/* endfor step through image */
  } while (nLabelChange==1); /* iterate until no more changes have taken  place */

  if (nFoundSites != NULL)
    *nFoundSites = uiSiteNumber-2;
  return uiSiteNumber-2;
}    

int RenumberRegions (unsigned short *pusBinary, int nRows, int nCols, int *nFoundSites, unsigned short *pnLabelReplace)
{ 
  if ((pusBinary == NULL) | (pnLabelReplace == NULL) || (nRows <= 0) || (nCols <= 0))
    return -1;
  
  int r, c, n;
  for (n=0; n < (*nFoundSites); n++)
    pnLabelReplace[n]=0;      /* Reset label replacement table */
  *nFoundSites = 0;

  /* Renumber sites sequentially starting at 1 */    
  for (r=1; r<nRows-1; r++)
  {
    for (c=1; c<nCols-1; c++)
    {
      if (pusBinary[c + r*nCols] != 0)
      {
        /* look for a remaining unlabelled site */
        if (pusBinary[c + r*nCols] == 1)
        {
          printf ("Binary2LabelledRegions: warning - unlabelled pixel (%d,%d)!\n", r, c );
          pusBinary[c + r*nCols] = 0;
        }
        
        /* find replacement label */
        for (n=0; (pnLabelReplace[n] != pusBinary[c + r*nCols]) && (pnLabelReplace[n] != 0); n++)
          ; /* do nothing */
        
        if (n+1>*nFoundSites)
          *nFoundSites=n+1;
        
        if (pnLabelReplace[n]==0) /* none found */
          /* Add site to replacement table */
          pnLabelReplace[n] = pusBinary[c + r*nCols];
        
        pusBinary[c + r*nCols] = n+1;      
      }
    }
  }
  return 0;
}

int FindBlobs (float* pfImage, float *pfClean, unsigned short* psSegment, int nRows, int nCols, struct blob** ppBlobList, int *nBlobs)
{
  if ((pfImage == NULL) || (pfClean == NULL) || (psSegment == NULL) || (nRows <= 0) || (nCols <= 0))
    return -1;
  
  int ret;
  ret = SegmentBlobs (pfImage, nRows, nCols, pfClean);
  if (ret < 0)
    return ret;
  ret = ThresholdToShort (pfClean, 0.5, psSegment, nRows*nCols);
  if (ret < 0)
    return ret;
  ret = Binary2LabelledRegions (psSegment, nRows, nCols, nBlobs);
  if (ret < 0)
    return ret;
  
  unsigned short psTmp[(*nBlobs)];
  RenumberRegions (psSegment, nRows, nCols, nBlobs, psTmp);
  
  *ppBlobList = (struct blob *) malloc((*nBlobs) * sizeof(struct blob));
  
  ComputeBlobFeatures (pfClean, psSegment, *nBlobs, nRows, nCols, *ppBlobList);
  return 0;
}

int FindBlobs2(float* pfImage, int nRows, int nCols, struct blob** ppBlobList, int *pnBlobs)
{
  if (pfImage == NULL)
    return -1;

  float pfClean[nRows*nCols];
  unsigned short psSegment[nRows*nCols];
  
  return FindBlobs(pfImage, pfClean, psSegment, nRows, nCols, ppBlobList, pnBlobs);
}

void Blobs2Points (struct blob* bloblist, int num_blobs, float min_blob_int, struct point **pointlist, int *num_points)
{
  /* count valid stars */
  int point_counter = 0;
  int i;
  for (i=0; i<num_blobs; i++)
  {
    if (bloblist[i].integral > min_blob_int)
      point_counter++;
  }
  *num_points = point_counter;
  
  struct point *tmp_points = malloc (point_counter*sizeof(struct point));
  
  point_counter = 0;
  for (i=0; i<num_blobs; i++)
  {
    if (bloblist[i].integral > min_blob_int)
    {
      if (point_counter > *num_points)
      {
        *num_points = -1;
        *pointlist = NULL;
        return;
      }
      tmp_points[point_counter].x = bloblist[i].x;
      tmp_points[point_counter].y = bloblist[i].y;
      point_counter++;
    }
  }
  
  *pointlist = tmp_points;
}

void FindPointMapping (struct point *pList1, int nPts1, struct point *pList2, int nPts2, int** nMap, int* nMatch)
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
    for (j=0; j<nPts2; j++)
    {
      dx = pList1[i].x - pList2[j].x;
      dy = pList1[i].y - pList2[j].y;
      
      nFound = 0;
      for (d=0; d<nDist; d++)
      {
        if ( (fabs(dList[d].x/dCount[d] - dx) < RADIUS) 
          && (fabs(dList[d].y/dCount[d] - dy) < RADIUS) )
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
              
              if ( fabs(dx) < RADIUS && fabs(dy) < RADIUS )
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
