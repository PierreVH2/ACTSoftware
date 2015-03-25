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
#include "../pattern_match.h"

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

#define MIN_NUM_OBJ      6
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

PointList *get_pattern(MYSQL *conn, double tel_ra, double tel_dec);
int get_image_info(MYSQL *conn, int img_id, int *img_width, int *img_height, float *epoch, float *tel_ra, float *tel_dec, float *img_mean, float *img_std);
int get_image(MYSQL *conn, int img_id, float *image, int num_pix);
PointList *image_extract_stars(void *image, int width, int height, float tel_ra, float tel_dec, float cutoff);
void print_points(const char *pref, int img_id, PointList *list);
void print_map(const char *pref, int img_id, int *map, int max_map, PointList *list1, PointList *list2);

int main(int argc, char **argv)
{
  int prog = 0, ret;
  MYSQL * conn = NULL;
  int img_id=0, i;
  int num_match=0;
  int img_width, img_height;
  float img_epoch;
  float *image = NULL;
  PointList *img_pts = point_list_new();
  PointList *pat_pts = point_list_new();
  int *map = NULL;
  int max_map=0;
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
  
  image = malloc(img_width*img_height*sizeof(float));
  ret = get_image (conn, img_id, image, img_width*img_height);
  if (ret < 0)
  {
    fprintf(stderr,"Failed to retrieve image %d\n", img_id);
    prog = 1;
    goto cleanup;
  }
  if (ret != img_width*img_height)
  {
    fprintf(stderr, "Could not extract all pixels from SQL databse (%d extracted, should be %d).\n", ret, img_width*img_height);
    prog = 1;
    goto cleanup;
  }
  
  g_object_unref(G_OBJECT(img_pts));
  img_pts = image_extract_stars((void*)image, img_width, img_height, tel_ra, tel_dec, mean+2.0*stddev);
  if (point_list_get_num_used(img_pts) < MIN_NUM_OBJ)
  {
    fprintf(stderr, "Too few stars extracted from image. Not continuing.\n");
    prog = 1;
    goto cleanup;
  }
  fprintf(stderr,"%d image points extracted.\n", point_list_get_num_used(img_pts));
  print_points("obj", img_id, img_pts);
  
  g_object_unref(G_OBJECT(pat_pts));
  pat_pts = get_pattern(conn, tel_ra, tel_dec);
  if (point_list_get_num_used(pat_pts) < MIN_NUM_OBJ)
  {
    fprintf(stderr, "Error extracting stars from Tycho catalog database (or too few stars available). Not continuing.\n");
    prog = 1;
    goto cleanup;
  }
  fprintf(stderr,"%d pattern points extracted.\n", point_list_get_num_used(pat_pts));
  print_points("pat", img_id, pat_pts);
  
  max_map = point_list_get_num_used(img_pts);
  map = malloc(max_map*sizeof(int));
  if (map == NULL)
  {
    fprintf(stderr, "Failed to allocate memory for point mapping.\n");
    prog = 1;
    goto cleanup;
  }
  ret = FindListMapping(img_pts, pat_pts, map, max_map, RADIUS);
  if (ret == 0)
  {
    fprintf(stderr, "Failed to create point mapping.\n");
    prog = 1;
    goto cleanup;
  }
  print_map("map", img_id, map, max_map, img_pts, pat_pts);
  
  rashift = decshift = 0.0;
  num_match = 0;
  for (i=0; i<max_map; i++)
  {
    if (map[i] < 0)
      continue;
    static gdouble x1, x2, y1, y2;
    ret = point_list_get_coord(img_pts, i, &x1, &y1) && point_list_get_coord(pat_pts, map[i], &x2, &y2);
    if (!ret)
      break;
    rashift += x1 - x2;
    decshift += y1 - y2;
    num_match++;
  }
  if (!ret)
  {
    fprintf(stderr, "Failed to calculate mapping offset.\n");
    prog = 1;
    goto cleanup;
  }
  rashift /= num_match;
  decshift /= num_match;
  raerr = decerr = 0.0;
  for (i=0; i<max_map; i++)
  {
    if (map[i] < 0)
      continue;
    static gdouble x1, x2, y1, y2;
    ret = point_list_get_coord(img_pts, i, &x1, &y1) && point_list_get_coord(pat_pts, map[i], &x2, &y2);
    if (!ret)
      break;
    raerr += fabs(rashift - x1 + x2);
    decerr += fabs(decshift - y1 + y2);
  }
  raerr /= num_match;
  decerr /= num_match;
  fprintf(stderr,"Shift:  RA %6.1f\" ~%6.1f  DEC %6.1f\" ~%6.1f\n", rashift, raerr, decshift, decerr);
  
  corr_ra = tel_ra - rashift/3600.0;
  corr_dec = tel_dec - decshift/3600.0;
  convert_H_HMSMS_ra(corr_ra/15.0, &tmp_ra);
  convert_D_DMS_dec(corr_dec, &tmp_dec);
  fprintf(stderr,"Match coord:    %s    %s\n", ra_to_str(&tmp_ra), dec_to_str(&tmp_dec));
  
  cleanup:
  if (conn != NULL)
    mysql_close(conn);
  if (image != NULL)
    free(image);
  
  printf("%4d  %6.2f %6.2f  %10.5f %10.5f  %3d %3d %3d  %7.2f %7.2f %7.2f %7.2f  %10.5f %10.5f\n", img_id, mean, stddev, tel_ra, tel_dec, point_list_get_num_used(img_pts), point_list_get_num_used(pat_pts), num_match, rashift, raerr, decshift, decerr, corr_ra, corr_dec);
  fprintf(stderr, "\n");
  
  g_object_unref(G_OBJECT(img_pts));
  g_object_unref(G_OBJECT(pat_pts));
  
  return prog;
}

PointList *get_pattern(MYSQL *conn, double tel_ra, double tel_dec)
{
  (void) conn;
  AcqStore *store = acq_store_new("localhost");
  if (store == NULL)
  {
    fprintf(stderr, "Failed to create store object.\n");
    return point_list_new();
  }
  PointList *pattern_map = acq_store_get_tycho_pattern(store, tel_ra, tel_dec, 2014.0, 1.0);
  g_object_unref(store);
  store = NULL;
  if (pattern_map == NULL)
  {
    fprintf(stderr, "Error retrieving star pattern from database.\n");
    return point_list_new();
  }
  return pattern_map;
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

int get_image(MYSQL *conn, int img_id, float *image, int num_pix)
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
  if (rowcount > num_pix)
  {
    fprintf(stderr, "Insufficient space in \"image\" array. Cannot extract image %d from SQL database.", img_id);
    mysql_free_result(result);
    return -1;
  }
  
  unsigned char pix;
  int count = 0;
  while ((row=mysql_fetch_row(result)) != NULL)
  {
    if (sscanf(row[0], "%hhu", &pix) != 1)
    {
      fprintf(stderr,"Failed to extract image pixel %d - %s\n", count, row[0]);
      continue;
    }
    image[count] = pix;
    count++;
  }
  
  mysql_free_result(result);
  return count;
}

PointList *image_extract_stars(void *image, int width, int height, float tel_ra, float tel_dec, float cutoff)
{
  (void) tel_ra;
  sepobj *obj = NULL;
  float conv[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};
  int i, ret, num_stars;
  
  ret = sep_extract(image, NULL, SEP_TFLOAT, SEP_TFLOAT, 0, width, height, cutoff, 5, conv, 3, 3, 32, 0.005, 1, 1.0, &obj, &num_stars);
  if (ret != 0)
  {
    fprintf(stderr, "Failed to extract stars from image - SEP error code %d", ret);
    return point_list_new();
  }
  PointList *star_list = point_list_new_with_length(num_stars);
  
  for (i=0; i<num_stars; i++)
  {
    static double x, y;
    x = (obj[i].x-XAPERTURE)*RA_WIDTH/WIDTH_PX/cos(convert_DEG_RAD(tel_dec));
    y = (-obj[i].y+YAPERTURE)*DEC_HEIGHT/HEIGHT_PX;
    if (!point_list_append(star_list, x, y))
      fprintf(stderr, "Failed to add identified star %d to stars list.", i);
  }
  return star_list;
}

void print_points(const char *pref, int img_id, PointList *list1)
{
  char fname[256];
  sprintf(fname, "img%d_%s.dat", img_id, pref);
  FILE *fp = fopen(fname, "w");
  int i, len=point_list_get_num_used(list1);
  gdouble x,y;
  
  for (i=0; i<len; i++)
  {
    if (!point_list_get_coord(list1, i, &x, &y))
      continue;
    fprintf(fp, "%6.1f  %6.1f\n", x, y);
  }
  fclose(fp);
}

void print_map(const char *pref, int img_id, int *map, int max_map, PointList *list1, PointList *list2)
{
  char fname[256];
  sprintf(fname, "img%d_%s.dat", img_id, pref);
  FILE *fp = fopen(fname, "w");
  int i;
  gdouble x1,y1,x2,y2;
  for (i=0; i<max_map; i++)
  {
    if (map[i] < 0)
      continue;
    if (!point_list_get_coord(list1, i, &x1, &y1))
      continue;
    if (!point_list_get_coord(list2, map[i], &x2, &y2))
      continue;
    fprintf(fp, "%2d    %6.1f  %6.1f  %6.1f    %6.1f  %6.1f  %6.1f\n", map[i], x1, x2, x1-x2, y1, y2, y1 - y2);
  }
  fclose(fp);
}
