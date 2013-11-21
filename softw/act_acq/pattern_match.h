/*!
 * \file pattern_match.h
 * \brief Definitions for pattern matching functions.
 * \author Pierre van Heerden
 * \author Greg Cox
 *
 * Cobbled together from the pattern-matching code initially written by Greg Cox for the ACT.
 */

#ifndef PATTERN_MATCH_H
#define PATTERN_MATCH_H

/** 
 * \brief Structure to hold information relevant to a blob on the acquisition image.
 *
 * A blob is a brighter-than-background patch of light on the acquisition image.
 */
struct blob
{
  /// X-coordinate of blob centre on acquisition image.
  float x;
  /// Y-coordinate of blob centre on acquisition image.
  float y;
  /// Size of blob
  float size;
  /// No idea
  float integral; 
};

/** 
 * \brief Structure to hold information relevant to a point (star) on the acquisition image.
 *
 * A point is the centre of a blob that corresponds to a star.
 */
struct point
{
  /// X-coordinate of point on acquisition image.
  float x;
  /// Y-coordinate of point on acquisition image.
  float y;
};

/** \brief Find blobs in acquisition image.
 * 
 * \param pfIamge
 *   Pointer to array of float that contains acquisition image.
 *
 * \param nRows
 *   Number of rows in acquisition image.
 *
 * \param nCols
 *   Number of columns in acquisition image.
 *
 * \param ppBlobList
 *   Pointer to array of struct blob where list of blobs will be saved. The necessary space for the list will be
 *   allocated. Function does not check whether array is empty.
 *
 * \param pnBlobs
 *   Pointer to integer to which number of blobs found in acquisition image will be stored.
 *
 * \return 0 on success, less than 0 otherwise.
 */
int FindBlobs2 (float* pfImage, int nRows, int nCols, struct blob** ppBlobList, int *pnBlobs);

/** \brief Convert list of blobs to list of points by removing the blobs which don't correspond to stars.
 * 
 * \param bloblist
 *   Pointer to array of struct blob that contains acquisition image.
 *
 * \param num_blobs
 *   Number of blobs in bloblist.
 *
 * \param min_blob_int
 *   Minimum integral for blob to be considered a point.
 *
 * \param pointlist
 *   Pointer to array of struct point where list of points will be saved. The necessary space for the list will be
 *   allocated. Function does not check whether array is empty.
 *
 * \param num_points
 *   Pointer to integer to which number of points found in list of blobs will be stored.
 *
 * \return (void)
 */
void Blobs2Points (struct blob* bloblist, int num_blobs, float min_blob_int, struct point **pointlist, int *num_points);

/** \brief Find mapping between two lists of points.
 * 
 * \param pList1
 *   Pointer to array of struct point that contains first list of points.
 *
 * \param nPts1
 *   Number of points in pList1.
 *
 * \param pList2
 *   Pointer to array of struct point that contains second list of points.
 *
 * \param nPts2
 *   Number of points in pList2.
 *
 * \param nMap
 *   Pointer to array of int that will contain mapping of points between pList1 and pList2. Stored such that
 *   pList2[nMap[n]] corresponds to pList1[n].
 *
 * \param nMatch
 *   Pointer to integer to which number of entries in the mapping will be stored. >0 if mapping found.
 *
 * \return (void)
 */
void FindPointMapping (struct point *pList1, int nPts1, struct point *pList2, int nPts2, int** nMap, int* nMatch);

#endif