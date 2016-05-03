/* Compile from local directory with:
 * gcc -Wall -Wextra `pkg-config --cflags gtk+-2.0 gtkglext-1.0` -I../ -I../../../libs/ 
 * ./disp_db_img.c ../ccd_img.c ../imgdisp.c ../view_param_dialog.c ../sep/*.c ../point_list.c ../pattern_match.c
 * ../../../libs/act_log.c ../../../libs/act_timecoord.c `pkg-config --libs gtk+-2.0 gtkglext-1.0` -lmysqlclient -lm
 * -o ./disp_db_img
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

MYSQL *conn = NULL;

int main(int argc, char **argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting %s", argv[1]));
  gtk_init(&argc, &argv);
  if (argc != 2)
  {
    act_log_error(act_log_msg("Invalid usage - specify a database image identifier (integer)"));
    return 1;
  }
  gint img_id = 0;

  conn = mysql_init(NULL);
  if (conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising MySQL database connection handler - %s.", mysql_error(conn)));
    return 2;
  }
  if (mysql_real_connect(conn, IMGDB_HOST, IMGDB_UNAME, IMGDB_PASSWD, "act", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error establishing connection to MySQL database - %s.", mysql_error(conn)));
    mysql_close(conn);
    return 2;
  }
  
  MYSQL_RES *img_res;
  MYSQL_ROW img_row;
  mysql_query(conn, "SELECT img_id FROM ccd_img WHERE type=2");
  img_res = mysql_store_result(conn);
  if (img_res == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve image ID list - %s.", mysql_error(conn)));
    mysql_close(conn);
    return 2;
  }
  
  gint img_id;
  while ((img_row = mysql_fetch_row(img_res)) != NULL)
  {
    if (sscanf(res_row[0], "%d", &img_id) != 1)
      continue;
    CcdImg *img = g_object_new (ccd_img_get_type(), NULL);
    ccd_img_set_img_type(img, IMGT_ACQ_OBJ);
    ccd_img_set_pixel_size(img, 2.30680331438202, 2.23796007923311);
    if (get_img_info(img_id, img) < 0)
    {
      act_log_error(act_log_msg("Failed to get image %d info.", img_id));
      g_object_unref(img);
      continue;
    }
    if (get_img_data(img_id, img) != 0)
    {
      act_log_error(act_log_msg("Failed to get image %d data.", img_id));
      g_object_unref(img);
      continue;
    }
    match_image(img);
    g_object_unref(img);
  }
  
  mysql_close(conn);
  return 0;
}

gint get_img_info(gint img_id, CcdImg *img)
{
  string256 qrystr;
  sprintf(qrystr, "SELECT exp_t_s, win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y, tel_ra_h, tel_dec_d, DATEDIFF(start_date,\"2000-01-01\") FROM ccd_img WHERE ccd_img.id=%d", img_id);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve information for image %d - %s.", img_id, mysql_error(conn)));
    mysql_close(conn);
    return -1;
  }
  gint rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    if (rowcount == 0)
      act_log_error(act_log_msg("No image found with identifier %d.", img_id));
    else
      act_log_error(act_log_msg("Too many images found with identifier %d - something is very wrong.", img_id));
    mysql_close(conn);
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
    act_log_error(act_log_msg("Failed to extract all necessary image information for image %d (%d parameters extracted).", img_id, ret));
    return -1;
  }
  
  ccd_img_set_integ_t(img, exp_t_s);
  ccd_img_set_window(img, read_x, read_y, read_width, read_height, read_pbnx, read_pbny);
  gdouble ra_fk5, dec_fk5;
  precess_fk5(read_ra*15.0, read_dec, 2000.0 + (gdouble)read_days / 365.25, &ra_fk5, &dec_fk5);
  ccd_img_set_tel_pos(img, ra_fk5, dec_fk5);
  ccd_img_set_start_datetime(img, 2000.0 + (gdouble)read_days / 365.25);
  
  return 0;
}

gint get_img_data(gint img_id, CcdImg *img)
{
  string256 qrystr;
  sprintf(qrystr, "SELECT y*%hu+x, value FROM ccd_img_data WHERE ccd_img_id=%d", ccd_img_get_img_width(img), img_id);
  MYSQL_RES *result;
  MYSQL_ROW res_row;
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve pixel data for image %d - %s.", img_id, mysql_error(conn)));
    return -1;
  }
  gint rowcount = mysql_num_rows(result);
  if (rowcount != (gint)ccd_img_get_img_width(img) * ccd_img_get_img_height(img))
  {
    act_log_error(act_log_msg("Image size mismatch (database sent %d pixels, should be %d), image %d", (gint)ccd_img_get_img_width(img) * ccd_img_get_img_height(img), rowcount, img_id));
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
    act_log_error(act_log_msg("Not all pixels read (%d should be %d)", ret, rowcount));
    free(img_data);
    return -1;
  }
  ccd_img_set_img_data(img, rowcount, img_data);
  free(img_data);
  return 0;
}

void match_image(CcdImg *img)
{
  (void)btn_match;
  CcdImg *img = imgdisp_get_img(GTK_WIDGET(imgdisp));
  
  // Extract stars from image
  PointList *img_pts = image_extract_stars(img, GTK_WIDGET(imgdisp));
  gint num_stars = point_list_get_num_used(img_pts);
  act_log_debug(act_log_msg("Number of stars extracted from image: %d\n", num_stars));
  if (num_stars < MIN_NUM_STARS)
  {
    act_log_error(act_log_msg("Too few stars in image (%d must be %d)", num_stars, MIN_NUM_STARS));
    return;
  }
  
  // Fetch nearby stars in GSC-1.2 catalog from database
  gfloat ra_d, dec_d, equinox;
  ccd_img_get_tel_pos(img, &ra_d, &dec_d);
  equinox = ccd_img_get_start_datetime(img);
  PointList *pat_pts = get_pat_points(ra_d, dec_d, equinox, PAT_SEARCH_RADIUS);
  gint num_pat = point_list_get_num_used(pat_pts);
  act_log_debug(act_log_msg("Number of catalog stars in vicinity: %d\n", num_pat));
  if (num_pat < MIN_NUM_STARS)
  {
    act_log_error(act_log_msg("Too few catalog stars in vicinity (%d must be %d)", num_pat, MIN_NUM_STARS));
    point_list_clear(img_pts);
    return;
  }
  
  // Match the two lists of points
  GSList *map = find_point_list_map(img_pts, pat_pts, 10./3600.0);
  gint num_match;
  point_list_clear(img_pts);
  point_list_clear(pat_pts);
  if (map == NULL)
  {
    act_log_error(act_log_msg("Failed to find point mapping."));
    return;
  }
  num_match = g_slist_length(map);
  gfloat rashift, decshift;
  if (num_match / (float)num_stars < MIN_MATCH_FRAC)
  {
    act_log_error(act_log_msg("Too few stars mapped to pattern (%d mapped, %d required)", num_match, (int)(MIN_MATCH_FRAC*num_stars)));
    return;
  }
  
  GSList *node = map;
  point_map_t *tmp_map=NULL;
  while (node != NULL)
  {
    tmp_map = (point_map_t *)node->data;
    if (tmp_map == NULL)
      continue;
    printf("%6d  %10.6f  %10.6f        %6d  %10.6f  %10.6f\n", tmp_map->idx1, tmp_map->x1, tmp_map->y1, tmp_map->idx2, tmp_map->x2, tmp_map->y2);
    node = g_slist_next(node);
  }
  
  point_list_map_calc_offset(map, &rashift, &decshift, NULL, NULL);
  point_list_map_free(map);
  g_slist_free(map);
  
  act_log_debug(act_log_msg("Pattern match result:  %12.6f %12.6f  %12.6f %12.6f  %12.6f %12.6f", ra_d, dec_d, ra_d-rashift, dec_d-decshift, rashift, decshift));
}

