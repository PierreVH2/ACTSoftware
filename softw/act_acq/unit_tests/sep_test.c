#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include <mysql/mysql.h>
#include "act_site.h"
#include "act_timecoord.h"
#include "act_positastro.h"
#include "../sep/sep.h"
#include "../point_list.h"
#include "../acq_store.h"

/// Width of CCD in pixels in full-frame mode (no prebinning, no windowing)
#define   WIDTH_PX     407
/// Height of CCD in pixels in full-frame mode (no prebinning, no windowing)
#define   HEIGHT_PX    288
/// On-sky width of CCD in full-frame mode (no prebinning, no windowing), in arcseconds
// #define   RA_WIDTH     942.66949776100614
#define   RA_WIDTH     931.137703710392
/// On-sky height of CCD in full-frame mode (no prebinning, no windowing), in arcseconds
// #define   DEC_HEIGHT   673.8384959198274
#define   DEC_HEIGHT   686.799827043267

/// X centre of aperture on acquisition image in pixels
#define XAPERTURE 172
/// Y centre of aperture on acquisition image in pixels
#define YAPERTURE 110

#define MIN_NUM_OBJ      2
#define MIN_MATCH_FRAC   0.4

#define RADIUS 5.0

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

int get_pattern(MYSQL *conn, double tel_ra, double tel_dec, struct point **point_list, int *num_points);
int get_image_info(MYSQL *conn, int img_id, int *img_width, int *img_height, float *epoch, float *tel_ra, float *tel_dec, float *img_mean, float *img_std);
int get_image(MYSQL *conn, int img_id, float **image, int *num_pix);
void FindPointMapping (struct point *pList1, int nPts1, struct point *pList2, int nPts2, int** nMap, int* nMatch);
void print_points(const char *pref, int img_id, struct point *point_list, int num_points);
void print_map(const char *pref, int img_id, int num, int *map, struct point *list1, struct point *list2);

int main(int argc, char **argv)
{
  int prog = 0, ret;
  MYSQL * conn = NULL;
  int img_id=0, i;
  int num_pix=0, num_stars=0, num_pat=0, num_match=0;
  int img_width, img_height;
  float img_epoch;
  float *image = NULL;
  sepobj *obj = NULL;
  struct point *img_points = NULL;
  struct point *pat_points = NULL;
  int *map = NULL;
  float conv[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};
  float mean=0.0, stddev=0.0;
  float tel_ra=0.0, tel_dec=0.0;
  float corr_ra=0.0, corr_dec=0.0;
  double rashift=0.0, decshift=0.0, raerr=0.0, decerr=0.0;
  struct rastruct tmp_ra;
  struct decstruct tmp_dec;
  
  if (argc != 2)
  {
    fprintf(stderr,"Incorrect usage. Please specify an image identifier.\n");
    return 2;
  }
  ret = sscanf(argv[1], "%d", &img_id);
  if (ret != 1)
  {
    fprintf(stderr,"Failed to extract image ID from command line (%s)\n", argv[1]);
    return 2;
  }
  fprintf(stderr, "Processing image %d\n", img_id);
  
  conn = mysql_init(NULL);
  if (conn == NULL)
    fprintf(stderr,"Error initialising MySQL connection handler - %s.\n", mysql_error(conn));
  else if (mysql_real_connect(conn, "localhost", "root", "nsW3", "tmp_sep", 0, NULL, 0) == NULL)
  {
    fprintf(stderr,"Error connecting to MySQL database - %s.\n", mysql_error(conn));
    conn = NULL;
    prog = 1;
    goto cleanup;
  }
  
  ret = get_image_info(conn, img_id, &img_width, &img_height, &img_epoch, &tel_ra, &tel_dec, &mean, &stddev);
  if (ret < 0)
  {
    fprintf(stderr,"Failed to fetch information for image %d\n", img_id);
    prog = 1;
    goto cleanup;
  }
  fprintf(stderr,"Image mean %f  dev %f\n", mean, stddev);
  convert_H_HMSMS_ra(tel_ra/15.0, &tmp_ra);
  convert_D_DMS_dec(tel_dec, &tmp_dec);
  fprintf(stderr,"Telescope RA, Dec:   %s   %s\n", ra_to_str(&tmp_ra), dec_to_str(&tmp_dec));
  
  ret = get_image (conn, img_id, &image, &num_pix);
  if (ret < 0)
  {
    fprintf(stderr,"Failed to retrieve image %d\n", img_id);
    prog = 1;
    goto cleanup;
  }
  
  ret = sep_extract((void *)image, NULL, SEP_TFLOAT, SEP_TFLOAT, 0, WIDTH_PX, HEIGHT_PX, mean+2.0*stddev, 5, conv, 3, 3, 32, 0.005, 1, 1.0, &obj, &num_stars);
  fprintf(stderr,"%d stars extracted\n", num_stars);
  if (num_stars < MIN_NUM_OBJ)
  {
    fprintf(stderr,"Too few stars extracted from image. Not continuing.\n");
    prog = 1;
    goto cleanup;
  }
  img_points = malloc(num_stars*sizeof(struct point));
  for (i=0; i<num_stars; i++)
  {
    int j, max_flux = 0;
    for (j=1; j<num_stars; j++)
    {
      if (obj[j].flux > obj[max_flux].flux)
        max_flux = j;
    }
    img_points[i].x = (obj[max_flux].x-XAPERTURE)*RA_WIDTH/WIDTH_PX/cos(tel_dec*ONEPI/180.0);
    img_points[i].y = (-obj[max_flux].y+YAPERTURE)*DEC_HEIGHT/HEIGHT_PX;
    obj[max_flux].flux = 0.0;
  }
  print_points("obj", img_id, img_points, num_stars);
  
  ret = get_pattern(conn, tel_ra, tel_dec, &pat_points, &num_pat);
  if (ret < 0)
  {
    fprintf(stderr,"Failed to get pattern.\n");
    prog = 1;
    goto cleanup;
  }
  fprintf(stderr,"%d pattern points returned.\n", num_pat);
  print_points("pat", img_id, pat_points, num_pat);
  
  FindPointMapping (img_points, num_stars, pat_points, num_pat, &map, &num_match);
  fprintf(stderr,"%d points matched.\n", num_match);
  if (num_match / (float)num_stars < MIN_MATCH_FRAC)
  {
    fprintf(stderr,"Too few points matched.\n");
    prog = 1;
    goto cleanup;
  }
  print_map("map", img_id, num_stars, map, img_points, pat_points);
  
  rashift = decshift = 0.0;
  for (i=0; i<num_stars; i++)
  {
    if (map[i] < 0)
      continue;
    rashift += img_points[i].x - pat_points[map[i]].x;
    decshift += img_points[i].y - pat_points[map[i]].y;
  }
  rashift /= num_match;
  decshift /= num_match;
  raerr = decerr = 0.0;
  for (i=0; i<num_stars; i++)
  {
    if (map[i] < 0)
      continue;
    raerr += fabs(rashift - img_points[i].x + pat_points[map[i]].x);
    decerr += fabs(decshift - img_points[i].y + pat_points[map[i]].y);
  }
  raerr /= num_match;
  decerr /= num_match;
  fprintf(stderr,"Shift:  RA %6.1f\" ~%6.1f  DEC %6.1f\" ~%6.1f\n", rashift, raerr, decshift, decerr);
  
  corr_ra = tel_ra - rashift/3600.0;
  corr_dec = tel_dec - decshift/3600.0;
  convert_H_HMSMS_ra(corr_ra/15.0, &tmp_ra);
  convert_D_DMS_dec(corr_dec, &tmp_dec);
  fprintf(stderr,"Match coord:    %s    %s\n", ra_to_str(&tmp_ra), dec_to_str(&tmp_dec));
  
//   printf("%4d  %6.2f %6.2f  %10.5f %10.5f  %3d %3d %3d  %7.2f %7.2f %7.2f %7.2f  %10.5f %10.5f\n", img_id, mean, stddev, tel_ra, tel_dec, num_stars, num_pat, num_match, rashift, raerr, decshift, decerr, corr_ra, corr_dec);
  
  cleanup:
  if (conn != NULL)
    mysql_close(conn);
  if (image != NULL)
    free(image);
  if (obj != NULL)
    free(obj);
  if (img_points != NULL)
    free(img_points);
  if (pat_points != NULL)
    free(pat_points);
  
  printf("%4d  %6.2f %6.2f  %10.5f %10.5f  %3d %3d %3d  %7.2f %7.2f %7.2f %7.2f  %10.5f %10.5f\n", img_id, mean, stddev, tel_ra, tel_dec, num_stars, num_pat, num_match, rashift, raerr, decshift, decerr, corr_ra, corr_dec);
  fprintf(stderr, "\n");
  
  return prog;
}

int get_pattern(MYSQL *conn, double tel_ra, double tel_dec, struct point **point_list, int *num_points)
{
  (void) conn;
  AcqStore *store = acq_store_new("localhost");
  if (store == NULL)
  {
    fprintf(stderr, "Failed to create store object.\n");
    return -1;
  }
  struct rastruct tmp_ra;
  struct decstruct tmp_dec;
  convert_H_HMSMS_ra(convert_DEG_H(tel_ra), &tmp_ra);
  convert_D_DMS_dec(tel_dec, &tmp_dec);
  PointList *pattern_map = acq_store_get_tycho_pattern(store, &tmp_ra, &tmp_dec, 2014.0, 1.0);
  g_object_unref(store);
  store = NULL;
  if (pattern_map == NULL)
  {
    fprintf(stderr, "Error retrieving star pattern from database.\n");
    return -1;
  }

  struct point *tmp_list = malloc(point_list_get_num_used(pattern_map)*sizeof(struct point));
  if (tmp_list == NULL)
  {
    fprintf(stderr, "Error creating point list.\n");
    g_object_unref(pattern_map);
    return -1;
  }
  int i, ret = 0;
  for (i=0; i<(int)point_list_get_num_used(pattern_map); i++)
  {
    gdouble tmp_x, tmp_y;
    if (!point_list_get_coord(pattern_map, i, &tmp_x, &tmp_y))
    {
      ret = -1;
      break;
    }
    tmp_list[i].x = tmp_x;
    tmp_list[i].y = tmp_y;
    ret++;
  }
  g_object_unref(pattern_map);
  if (ret <= 0)
  {
    fprintf(stderr, "Error copying pattern.\n");
    return ret;
  }
  *point_list = tmp_list;
  *num_points = ret;
  return 0;
}

int get_image_info(MYSQL *conn, int img_id, int *img_width, int *img_height, float *epoch, float *tel_ra, float *tel_dec, float *img_mean, float *img_std)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  char sqlqry[256];
  snprintf(sqlqry, sizeof(sqlqry), "SELECT IFNULL(merlin_img.id,0), win_width, win_height, start_date, start_time_h, tel_ha_h, tel_dec_d, AVG(value), STD(value) FROM merlin_img INNER JOIN merlin_img_data ON merlin_img_data.merlin_img_id=merlin_img.id WHERE merlin_img_id=%d", img_id);
  mysql_query(conn, sqlqry);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    fprintf(stderr,"Error occurred while attempting to retrieve image %d information from SQL database - %s.\n", img_id, mysql_error(conn));
    return -1;
  }
  int rowcount = mysql_num_rows(result);
  if (rowcount <= 0)
  {
    fprintf(stderr,"No data returned while attempting to retrieve image %d information from SQL database.\n", img_id);
    mysql_free_result(result);
    return -1;
  }
  
  if ((row=mysql_fetch_row(result)) == NULL)
  {
    fprintf(stderr,"Failed to fetch row of information for image %d from database.\n", img_id);
    mysql_free_result(result);
    return -1;
  }
  int tmp_id;
  int width, height;
  int year, month, day;
  double time_h, ha_h, dec_d;
  double mean, std;
  int ret = 0;
  ret = sscanf(row[0], "%d", &tmp_id);
  if ((ret != 1) || (tmp_id != img_id))
  {
    if (tmp_id == 0)
      fprintf(stderr, "No image available.\n");
    else
      fprintf(stderr, "No/invalid results returned.\n");
    mysql_free_result(result);
    return -1;
  }
  ret += sscanf(row[1], "%d", &width);
  ret += sscanf(row[2], "%d", &height);
  ret += sscanf(row[3], "%d-%d-%d", &year, &month, &day);
  ret += sscanf(row[4], "%lf", &time_h);
  ret += sscanf(row[5], "%lf", &ha_h);
  ret += sscanf(row[6], "%lf", &dec_d);
  ret += sscanf(row[7], "%lf", &mean);
  ret += sscanf(row[8], "%lf", &std);
  mysql_free_result(result);
  if (ret != 11)
  {
    fprintf(stderr,"Failed to extract all needed information while attempting to retrieve image %d information from SQL database.\n", img_id);
    return -1;
  }
  
  struct datestruct start_date = { .day = day-1, .month = month-1, .year = year };
  struct timestruct start_ut;
  convert_H_HMSMS_time(time_h, &start_ut);
  struct hastruct ha;
  convert_H_HMSMS_ha(ha_h, &ha);
  double jd = calc_GJD (&start_date, &start_ut);
  double sidt_h = calc_SidT (jd);
  struct timestruct sidt;
  convert_H_HMSMS_time(sidt_h, &sidt);
  struct rastruct ra;
  calc_RA (&ha, &sidt, &ra);
  
  *img_width = width;
  *img_height = height;
  *epoch = year + (float)(month-1)/12.0;
  *tel_ra = convert_HMSMS_H_ra(&ra) * 15.0;
  *tel_dec = dec_d;
  *img_mean = mean;
  *img_std = std;
 
  return 0;
}

int get_image(MYSQL *conn, int img_id, float **image, int *num_pix)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  char sqlqry[256];
  snprintf(sqlqry, sizeof(sqlqry), "SELECT value FROM merlin_img_data WHERE merlin_img_id=%d ORDER BY x,y", img_id);
  mysql_query(conn, sqlqry);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    fprintf(stderr,"Error occurred while attempting to retrieve image %d from SQL database - %s.", img_id, mysql_error(conn));
    return -1;
  }
  if (mysql_num_fields(result) != 1)
  {
    fprintf(stderr,"Invalid data returned while attempting to retrieve image %d from SQL database (result has %d fields instead of 1)", img_id, mysql_num_fields(result));
    mysql_free_result(result);
    return -1;
  }
  int rowcount = mysql_num_rows(result);
  if (rowcount <= 0)
  {
    fprintf(stderr,"No data returned while attempting to retrieve image %d from SQL database.", img_id);
    mysql_free_result(result);
    return -1;
  }
  
  *image = malloc(rowcount*sizeof(float));
  unsigned char pix;
  int count = 0;
  while ((row=mysql_fetch_row(result)) != NULL)
  {
    if (sscanf(row[0], "%hhu", &pix) != 1)
    {
      fprintf(stderr,"Failed to extract image pixel %d - %s\n", count, row[0]);
      continue;
    }
    (*image)[count] = pix;
    count++;
  }
  *num_pix = count;
  
  mysql_free_result(result);
  return 1;
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
      fprintf(stderr, "%4d  %9.3f  %9.3f %d\n", i, dList[i].x, dList[i].y, dCount[i] );
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
      fprintf(stderr, "Offset (%f,%f)\n", dList[m].x, dList[m].y );
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
      fprintf(stderr, "%4d -> %4d\n", i, cBest[i] );
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

void print_points(const char *pref, int img_id, struct point *list, int num)
{
  char fname[256];
  sprintf(fname, "img%d_%s.dat", img_id, pref);
  FILE *fp = fopen(fname, "w");
  int i;
  
  for (i=0; i<num; i++)
    fprintf(fp, "%6.1f  %6.1f\n", list[i].x, list[i].y);
  fclose(fp);
}

void print_map(const char *pref, int img_id, int num, int *map, struct point *list1, struct point *list2)
{
  char fname[256];
  sprintf(fname, "img%d_%s.dat", img_id, pref);
  FILE *fp = fopen(fname, "w");
  int i;
  for (i=0; i<num; i++)
  {
    if (map[i] < 0)
      continue;
    fprintf(fp, "%2d    %6.1f  %6.1f  %6.1f    %6.1f  %6.1f  %6.1f\n", map[i], list1[i].x, list2[i].x, list1[i].x-list2[i].x, list1[i].y, list2[map[i]].y, list1[i].y - list2[map[i]].y);
  }
  fclose(fp);
}
