/* Compile from local directory with:
 * gcc -Wall -Wextra `pkg-config --cflags gtk+-2.0 gtkglext-1.0` -I../ -I../../../libs/ 
 * ./patmatch_test.c ../pattern_match.c ../ccd_img.c ../sep/*.c ../point_list.c ../../../libs/act_timecoord.c 
 * `pkg-config --libs gtk+-2.0 gtkglext-1.0` -lmysqlclient -lm 
 * -o patmatch_test
 */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <mysql/mysql.h>

#include <act_log.h>
#include <act_site.h>
#include <act_timecoord.h>
#include <point_list.h>
#include <pattern_match.h>
#include <view_param_dialog.h>
#include <ccd_img.h>
#include <imgdisp.h>
#include <sep/sep.h>

#define IMGDB_HOST    "actphot.suth.saao.ac.za"
#define IMGDB_UNAME   "act_acq"
#define IMGDB_PASSWD  NULL

#define MIN_NUM_STARS            5
#define MIN_MATCH_FRAC           0.4
#define PAT_SEARCH_RADIUS        1.0
#define PAT_MATCH_RADIUS         10./3600.0

void process_image(gint img_id);
gint get_img_info(gint img_id, CcdImg *img);
gint get_img_data(gint img_id, CcdImg *img);
PointList *get_img_stars(CcdImg *img);
PointList *get_pat_points(CcdImg *img, gfloat radius_d);
void precess_fk5(gdouble ra_in, gdouble dec_in, gdouble eq_in, gdouble *ra_d_fk5, gdouble *dec_d_fk5);
void print_image(gint img_id, CcdImg *img);
void print_stars(gint img_id, PointList *list);
void print_pat(gint img_id, PointList *list);
void print_map(gint img_id, GSList *map);


MYSQL *conn = NULL;

int main(int argc, char **argv)
{
  gtk_init(&argc, &argv);

  conn = mysql_init(NULL);
  if (conn == NULL)
  {
    fprintf(stderr, "Error initialising MySQL database connection handler - %s.\n", mysql_error(conn));
    return 2;
  }
  if (mysql_real_connect(conn, IMGDB_HOST, IMGDB_UNAME, IMGDB_PASSWD, "act", 0, NULL, 0) == NULL)
  {
    fprintf(stderr, "Error establishing connection to MySQL database - %s.\n", mysql_error(conn));
    mysql_close(conn);
    return 2;
  }
  
  MYSQL_RES *img_res;
  MYSQL_ROW img_row;
  mysql_query(conn, "SELECT id FROM ccd_img WHERE type=2");
  img_res = mysql_store_result(conn);
  if (img_res == NULL)
  {
    fprintf(stderr, "Failed to retrieve image ID list - %s.\n", mysql_error(conn));
    mysql_close(conn);
    return 2;
  }
  
  gint img_id;
  while ((img_row = mysql_fetch_row(img_res)) != NULL)
  {
    if (sscanf(img_row[0], "%d", &img_id) != 1)
    {
      fprintf(stderr, "Failed to extract image ID (%s)\n", img_row[0]);
      continue;
    }
    fprintf(stderr, "Processing image %d\n", img_id);
    process_image(img_id);
  }
  
  mysql_close(conn);
  return 0;
}

void process_image(gint img_id)
{
  CcdImg *img = NULL;
  PointList *img_stars = NULL, *pat_stars = NULL;
  GSList *map = NULL;
  gint num_stars=0, num_pat=0, num_match=0;
  gfloat tel_ra=0.0, tel_dec=0.0, tel_eq=0.0, shift_ra=0.0, shift_dec=0.0;
  gdouble tel_ra_fk5=0.0, tel_dec_fk5=0.0;
  
  img = g_object_new (ccd_img_get_type(), NULL);
  ccd_img_set_img_type(img, IMGT_ACQ_OBJ);
  ccd_img_set_pixel_size(img, 2.30680331438202, 2.23796007923311);
  if (get_img_info(img_id, img) < 0)
  {
    fprintf(stderr, "Failed to get image %d info.\n", img_id);
    g_object_unref(img);
    img = NULL;
    goto finalise;
  }
  if (get_img_data(img_id, img) != 0)
  {
    fprintf(stderr, "Failed to get image %d data.\n", img_id);
    g_object_unref(img);
    img = NULL;
    goto finalise;
  }
  ccd_img_get_tel_pos(img, &tel_ra, &tel_dec);
  tel_eq = ccd_img_get_start_datetime(img);
  precess_fk5(tel_ra, tel_dec, tel_eq, &tel_ra_fk5, &tel_dec_fk5);
    
  img_stars = get_img_stars(img);
  num_stars = point_list_get_num_used(img_stars);
  pat_stars = get_pat_points(img, PAT_SEARCH_RADIUS);
  num_pat = point_list_get_num_used(pat_stars);
  if ((num_stars < MIN_NUM_STARS) || (num_pat < MIN_NUM_STARS))
  {
    fprintf(stderr, "Too few stars available (%d, %d)\n", num_stars, num_pat);
    goto finalise;
  }
  
  map = find_point_list_map(img_stars, pat_stars, PAT_MATCH_RADIUS);
  if (map == NULL)
  {
    fprintf(stderr, "Failed to find point mapping for image %d.\n", img_id);
    goto finalise;
  }
  num_match = g_slist_length(map);
  if (num_match / (float)num_stars < MIN_MATCH_FRAC)
  {
    fprintf(stderr, "Too few stars mapped to pattern (%d mapped, %d required)\n", num_match, (int)(MIN_MATCH_FRAC*num_stars));
    goto finalise;
  }
  point_list_map_calc_offset(map, &shift_ra, &shift_dec, NULL, NULL);
  
  finalise:
  printf("%5d\t%12.6f  %12.6f  %6.1f\t%12.6f  %12.6f  %6.1f\t%6d  %6d  %6d\t%6.1f  %6.1f\t%12.6f  %12.6f  %6.1f\n", img_id, tel_ra, tel_dec, tel_eq, tel_ra_fk5, tel_dec_fk5, 2000.0, num_stars, num_pat, num_match, shift_ra*3600.0, shift_dec*3600.0, tel_ra_fk5-shift_ra, tel_dec_fk5-shift_dec, 2000.0);
  
  print_image(img_id, img);
  print_stars(img_id, img_stars);
  print_pat(img_id, pat_stars);
  print_map(img_id, map);
  
  if (img != NULL)
    g_object_unref(img);
  if (img_stars != NULL)
    point_list_clear(img_stars);
  if (pat_stars != NULL)
    point_list_clear(pat_stars);
  if (map != NULL)
  {
    point_list_map_free(map);
    g_slist_free(map);
  }
}

gint get_img_info(gint img_id, CcdImg *img)
{
  char qrystr[256];
  sprintf(qrystr, "SELECT exp_t_s, win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y, tel_ra_h, tel_dec_d, DATEDIFF(start_date,\"2000-01-01\") FROM ccd_img WHERE ccd_img.id=%d", img_id);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    fprintf(stderr, "Failed to retrieve information for image %d - %s.\n", img_id, mysql_error(conn));
    return -1;
  }
  gint rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    if (rowcount == 0)
      fprintf(stderr, "No image found with identifier %d.\n", img_id);
    else
      fprintf(stderr, "Too many images found with identifier %d - something is very wrong.\n", img_id);
    return -1;
  }
  row = mysql_fetch_row(result);
  gint read_x, read_y, read_width, read_height, read_pbnx, read_pbny, read_days;
  gdouble exp_t_s, read_ra, read_dec;
  gint ret = 0;
  ret += (sscanf(row[0], "%lf", &exp_t_s     ) == 1);
  ret += (sscanf(row[1], "%d",  &read_x      ) == 1);
  ret += (sscanf(row[2], "%d",  &read_y      ) == 1);
  ret += (sscanf(row[3], "%d",  &read_width  ) == 1);
  ret += (sscanf(row[4], "%d",  &read_height ) == 1);
  ret += (sscanf(row[5], "%d",  &read_pbnx   ) == 1);
  ret += (sscanf(row[6], "%d",  &read_pbny   ) == 1);
  ret += (sscanf(row[7], "%lf", &read_ra     ) == 1);
  ret += (sscanf(row[8], "%lf", &read_dec    ) == 1);
  ret += (sscanf(row[9], "%d",  &read_days   ) == 1);
  mysql_free_result(result);
  if (ret != 10)
  {
    fprintf(stderr, "Failed to extract all necessary image information for image %d (%d parameters extracted)\n.", img_id, ret);
    return -1;
  }
  
  ccd_img_set_integ_t(img, exp_t_s);
  ccd_img_set_window(img, read_x, read_y, read_width, read_height, read_pbnx, read_pbny);
  ccd_img_set_tel_pos(img, read_ra*15.0, read_dec);
  ccd_img_set_start_datetime(img, 2000.0 + (gdouble)read_days / 365.25);
  
  return 0;
}

gint get_img_data(gint img_id, CcdImg *img)
{
  char qrystr[256];
  sprintf(qrystr, "SELECT y*%hu+x, value FROM ccd_img_data WHERE ccd_img_id=%d", ccd_img_get_img_width(img), img_id);
  MYSQL_RES *result;
  MYSQL_ROW res_row;
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    fprintf(stderr, "Failed to retrieve pixel data for image %d - %s.\n", img_id, mysql_error(conn));
    return -1;
  }
  gint rowcount = mysql_num_rows(result);
  if (rowcount != (gint)ccd_img_get_img_width(img) * ccd_img_get_img_height(img))
  {
    fprintf(stderr, "Image size mismatch (database sent %d pixels, should be %d), image %d\n", (gint)ccd_img_get_img_width(img) * ccd_img_get_img_height(img), rowcount, img_id);
    mysql_free_result(result);
    return -1;
  }
  gint pixnum;
  gfloat pixval;
  gint ret = 0;
  gfloat *img_data = malloc(rowcount*sizeof(gfloat));
  while ((res_row = mysql_fetch_row(result)) != NULL)
  {
    if ((sscanf(res_row[0], "%d", &pixnum) != 1) || (sscanf(res_row[1], "%f", &pixval) != 1))
      break;
    img_data[pixnum] = pixval;
    ret++;
  }
  mysql_free_result(result);
  if (ret != rowcount)
  {
    fprintf(stderr, "Not all pixels read (%d should be %d)\n", ret, rowcount);
    free(img_data);
    return -1;
  }
  ccd_img_set_img_data(img, rowcount, img_data);
  free(img_data);
  return 0;
}

PointList *get_img_stars(CcdImg *img)
{
  gfloat mean=0.0;
  gint i, num_pix=ccd_img_get_img_len(img);
  gfloat const *img_data = ccd_img_get_img_data(img);
  for (i=0; i<num_pix; i++)
    mean += img_data[i];
  mean /= num_pix;
  
  sepobj *obj = NULL;
  gint ret, num_stars=0;
  ret = sep_extract((void *)img_data, NULL, SEP_TFLOAT, SEP_TFLOAT, 0, ccd_img_get_win_width(img), ccd_img_get_win_height(img), 1.5*mean, 5, NULL, 0, 0, 32, 0.005, 1, 1.0, &obj, &num_stars);
  if (ret != 0)
  {
    fprintf(stderr, "Failed to extract stars from image - SEP error code %d\n", ret);
    return point_list_new();
  }
  if (num_stars <= 0)
  {
    fprintf(stderr, "No stars extracted from image (%d).\n", num_stars);
    return point_list_new();
  }
  
  PointList *star_list = point_list_new_with_length(num_stars);
  gfloat star_ra, star_dec, tel_eq = ccd_img_get_start_datetime(img);
  gdouble star_ra_fk5, star_dec_fk5;
  
  for (i=0; i<num_stars; i++)
  {
    ccd_img_get_pix_coord(img, obj[i].x, obj[i].y, &star_ra, &star_dec);
    if (ret < 0)
    {
      fprintf(stderr, "Failed to calculate RA and Dec of star %d in star list.\n", i);
      continue;
    }
    precess_fk5(star_ra, star_dec, tel_eq, &star_ra_fk5, &star_dec_fk5);
    ret = point_list_append(star_list, star_ra_fk5, star_dec_fk5);
    if (!ret)
      fprintf(stderr, "Failed to add identified star %d to stars list.\n", i);
  }
  
  free(obj);
  return star_list;
}

PointList *get_pat_points(CcdImg *img, gfloat radius_d)
{
  char qrystr[256], constr_str[256], reg_list[256];
  MYSQL_RES *result;
  MYSQL_ROW row;
  
  gfloat tel_ra, tel_dec, tel_eq;
  gdouble tel_ra_fk5, tel_dec_fk5;
  ccd_img_get_tel_pos(img, &tel_ra, &tel_dec);
  tel_eq = ccd_img_get_start_datetime(img);
  precess_fk5(tel_ra, tel_dec, tel_eq, &tel_ra_fk5, &tel_dec_fk5);
  
  gdouble ra_radius_d = radius_d / cos(tel_dec_fk5*ONEPI/180.0);
  if (tel_dec_fk5 + radius_d >= 90.0)
    sprintf(constr_str, "dec_min<=%f AND dec_max>=90.0", tel_dec_fk5-radius_d);
  else if (tel_dec_fk5 - radius_d <= -90.0)
    sprintf(constr_str, "dec_min<=-90.0 AND dec_max>=%f", tel_dec_fk5+radius_d);
  else if (tel_ra_fk5 - ra_radius_d < 0.0)
    sprintf(constr_str, "dec_max>=%f AND dec_min<=%f AND ((ra_max>=360.0 AND ra_min<=%f) OR (ra_max>=%f AND ra_min<=0.0))", tel_dec_fk5-radius_d, tel_dec_fk5+radius_d, tel_ra_fk5-ra_radius_d+360.0, tel_ra_fk5+ra_radius_d);
  else if (tel_ra_fk5 + ra_radius_d >= 360.0)
    sprintf(constr_str, "dec_max>=%f AND dec_min<=%f AND ((ra_max>=360.0 AND ra_min<=%f) OR (ra_max>=%f AND ra_min<=0.0))", tel_dec_fk5-radius_d, tel_dec_fk5+radius_d, tel_ra_fk5-ra_radius_d, tel_ra_fk5+ra_radius_d-360.0);
  else 
    sprintf(constr_str, "dec_max>=%f AND dec_min<=%f AND ra_max>=%f AND ra_min<=%f", tel_dec_fk5-radius_d, tel_dec_fk5+radius_d, tel_ra_fk5-ra_radius_d, tel_ra_fk5+ra_radius_d);
  sprintf(qrystr, "SELECT id FROM gsc1_reg WHERE %s;", constr_str);
  
  mysql_query(conn, qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    fprintf(stderr, "Could not retrieve catalog star regions - %s.\n", mysql_error(conn));
    return point_list_new();
  }
  gint rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 1))
  {
    fprintf(stderr, "Could not retrieve star catalog region entries - Invalid number of rows/columns returned (%d rows, %d columns).\n", rowcount, mysql_num_fields(result));
    mysql_free_result(result);
    return point_list_new();
  }
  gint reg_id, ret, len=0;
  while ((row = mysql_fetch_row(result)) != NULL)
  {
    ret = sscanf(row[0], "%d", &reg_id);
    if (ret != 1)
    {
      fprintf(stderr, "Failed to extract region ID.\n");
      break;
    }
    ret = sprintf(&reg_list[len], "%d,", reg_id);
    if (ret <= 0)
    {
      fprintf(stderr, "Failed to write region ID to region list.\n");
      break;
    }
    len += ret;
  }
  mysql_free_result(result);
  if ((ret <= 0) || (len == 0))
    return point_list_new();
  reg_list[len-1] = '\0';
  
  if (tel_dec_fk5 + radius_d >= 90.0)
    sprintf(constr_str, "dec_d_fk5>%lf AND dec_d_fk5<90.0", tel_dec_fk5-radius_d);
  else if (tel_dec_fk5 - radius_d <= -90.0)
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>-90.0", tel_dec_fk5+radius_d);
  else if (tel_ra_fk5 - ra_radius_d < 0.0)
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>%lf AND (ra_d_fk5<%lf OR ra_d_fk5>%lf)", tel_dec_fk5+radius_d, tel_dec_fk5-radius_d, tel_ra_fk5+ra_radius_d, tel_ra_fk5-ra_radius_d+360.0);
  else if (tel_ra_fk5 + ra_radius_d >= 360.0)
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>%lf AND (ra_d_fk5<%lf OR ra_d_fk5>%lf)", tel_dec_fk5+radius_d, tel_dec_fk5-radius_d, tel_ra_fk5+ra_radius_d-360.0, tel_ra_fk5-ra_radius_d);
  else 
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>%lf AND ra_d_fk5<%lf AND ra_d_fk5>%lf", tel_dec_fk5+radius_d, tel_dec_fk5-radius_d, tel_ra_fk5+ra_radius_d, tel_ra_fk5-ra_radius_d);
  
  sprintf(qrystr, "SELECT ra_d_fk5, dec_d_fk5 FROM gsc1 WHERE reg_id IN (%s) AND (%s)", reg_list, constr_str);
  act_log_debug(act_log_msg(qrystr));
  mysql_query(conn, qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    fprintf(stderr, "Could not retrieve catalog stars - %s.\n", mysql_error(conn));
    return point_list_new();
  }
  rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 2))
  {
    fprintf(stderr, "Could not retrieve star catalog entries - Invalid number of rows/columns returned (%d rows, %d columns).\n", rowcount, mysql_num_fields(result));
    fprintf(stderr, "\t%f %f %f %f\n", tel_ra_fk5, tel_dec_fk5, radius_d, ra_radius_d);
    mysql_free_result(result);
    return point_list_new();
  }
  
  PointList *list = point_list_new_with_length(rowcount);
  if (list == NULL)
  {
    fprintf(stderr, "Failed to create point list for star catalog entries.\n");
    mysql_free_result(result);
    return point_list_new();
  }
  gdouble point_ra, point_dec;
  gchar OK = 1;
  while ((row = mysql_fetch_row(result)) != NULL)
  {
    if (sscanf(row[0], "%lf", &point_ra) != 1)
      OK = 0;
    if (sscanf(row[1], "%lf", &point_dec) != 1)
      OK = 0;
    if (!OK)
    {
      fprintf(stderr, "Failed to extract all parameters for point from database.\n");
      break;
    }
    point_list_append(list, point_ra, point_dec);
  }
  mysql_free_result(result);
  if (!OK)
  {
    fprintf(stderr, "Failed to retrieve all catalog stars from database.\n");
    point_list_clear(list);
    return point_list_new();
  }
  if (point_list_get_num_used(list) != (guint)rowcount)
    fprintf(stderr, "Not all catalog stars extracted from database (%u should be %d).\n", point_list_get_num_used(list), rowcount);
  return list;
}

void precess_fk5(gdouble ra_in, gdouble dec_in, gdouble eq_in, gdouble *ra_d_fk5, gdouble *dec_d_fk5)
{
  gdouble ra_rad = ra_in*ONEPI/180.0;
  gdouble dec_rad = dec_in*ONEPI/180.0;
  gdouble T, xa, za, ta, sacd, cacd, sd;
  
  T = 0.01*(2000.0 - eq_in);
  xa = (0.6406161 + 0.0000839*T + 0.0000050*T*T)*T*ONEPI/180.0;
  za = (0.6406161 + 0.0003041*T + 0.0000051*T*T)*T*ONEPI/180.0;
  ta = (0.5567530 - 0.0001185*T - 0.0000116*T*T)*T*ONEPI/180.0;
  sacd = sin(ra_rad+xa)*cos(dec_rad);
  cacd = cos(ra_rad+xa)*cos(ta)*cos(dec_rad) - sin(ta)*sin(dec_rad);
  sd = cos(ra_rad + xa)*sin(ta)*cos(dec_rad) + cos(ta)*sin(dec_rad);
  dec_rad = asin(sd);
  ra_rad = atan2(sacd, cacd) + za;
  if (ra_rad < 0.0)
    ra_rad += 2*ONEPI;
  else if (ra_rad > 2*ONEPI)
    ra_rad -= 2*ONEPI;
  if (dec_rad < -1*ONEPI)
    dec_rad += 2*ONEPI;
  else if (dec_rad > ONEPI)
    dec_rad -= 2*ONEPI;
  *ra_d_fk5 = ra_rad * 180.0 / ONEPI;
  *dec_d_fk5 = dec_rad * 180.0 / ONEPI;
}

void print_image(gint img_id, CcdImg *img)
{
  if (img == NULL)
    return;
  gchar fname[256];
  sprintf(fname, "img_%04d.dat", img_id);
  FILE *fout = fopen(fname, "w");
  gushort x,y;
  for (x=0; x<ccd_img_get_img_width(img); x++)
    for (y=0; y<ccd_img_get_img_height(img); y++)
      fprintf(fout, "%4d\t%4d\t%10.6f\n", x, y, ccd_img_get_pixel(img, x, y));
  fclose(fout);
}

void print_stars(gint img_id, PointList *list)
{
  if (list == NULL)
    return;
  gint i, len = point_list_get_num_used(list);
  if (len <= 0)
    return;
  gchar fname[256];
  sprintf(fname, "obj_%04d.dat", img_id);
  FILE *fout = fopen(fname, "w");
  gdouble x, y;
  for (i=0; i<len; i++)
  {
    if (!point_list_get_coord(list, i, &x, &y))
      continue;
    fprintf(fout, "%12.6f  %12.6f\n", x, y);
  }
  fclose(fout);
}

void print_pat(gint img_id, PointList *list)
{
  if (list == NULL)
    return;
  gint i, len = point_list_get_num_used(list);
  if (len <= 0)
    return;
  gchar fname[256];
  sprintf(fname, "pat_%04d.dat", img_id);
  FILE *fout = fopen(fname, "w");
  gdouble x, y;
  for (i=0; i<len; i++)
  {
    if (!point_list_get_coord(list, i, &x, &y))
      continue;
    fprintf(fout, "%12.6f  %12.6f\n", x, y);
  }
  fclose(fout);
}

void print_map(gint img_id, GSList *map)
{
  if (map == NULL)
    return;
  gchar fname[256];
  sprintf(fname, "map_%04d.dat", img_id);
  FILE *fout = fopen(fname, "w");
  GSList *node = map;
  point_map_t *tmp_map=NULL;
  while (node != NULL)
  {
    tmp_map = (point_map_t *)node->data;
    if (tmp_map == NULL)
      continue;
    fprintf(fout, "%6d  %10.6f  %10.6f\t%6d  %10.6f  %10.6f\n", tmp_map->idx1, tmp_map->x1, tmp_map->y1, tmp_map->idx2, tmp_map->x2, tmp_map->y2);
    node = g_slist_next(node);
  }
  fclose(fout);
}

