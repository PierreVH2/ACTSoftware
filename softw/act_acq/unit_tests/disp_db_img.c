/* Compile from local directory with:
 * gcc -Wall -Wextra `pkg-config --cflags gtk+-2.0 gtkglext-1.0` -I../ -I../../../libs/ 
 * ./disp_db_img.c ../ccd_img.c ../imgdisp.c ../view_param_dialog.c ../../../libs/act_log.c ../../../libs/act_timecoord.c 
 * `pkg-config --libs gtk+-2.0 gtkglext-1.0` -lmysqlclient -lm -o ./disp_db_img
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

#define MIN_NUM_STARS            4
#define MIN_MATCH_FRAC           0.4
#define PAT_SEARCH_RADIUS        1.0

typedef gchar string256[256];

gint get_img_info(gint img_id, CcdImg *img);
gint get_img_data(gint img_id, CcdImg *img);
PointList *image_extract_stars(CcdImg *img, GtkWidget *imgdisp);
PointList *get_pat_points(gdouble ra_d, gdouble dec_d, gdouble equinox, gdouble radius_d);
void precess_fk5(gdouble ra_in, gdouble dec_in, gdouble eq_in, gdouble *ra_d_fk5, gdouble *dec_d_fk5);
gboolean mouse_move(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_coord);
void view_param_response(GtkWidget *dialog, gint response_id);
void show_view_param(GtkWidget *btn_view_param, gpointer imgdisp);
void match_pattern(GtkWidget *btn_match, gpointer imgdisp);

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
  if (sscanf(argv[1], "%d", &img_id) != 1)
  {
    act_log_error(act_log_msg("Failed to extract database image identifier from command line - please specify an integer identifier."));
    return 1;
  }
  if (img_id <= 0)
  {
    act_log_error(act_log_msg("Invalid database image identifier specified (%d). Must be a positive integer."));
    return 1;
  }

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
  
  CcdImg *img = g_object_new (ccd_img_get_type(), NULL);
  ccd_img_set_img_type(img, IMGT_ACQ_OBJ);
  ccd_img_set_pixel_size(img, 2.3170, 2.3264);
  if (get_img_info(img_id, img) < 0)
  {
    act_log_error(act_log_msg("Failed to get image %d info.", img_id));
    mysql_close(conn);
    return 2;
  }
  gfloat ra_d, dec_d;
  ccd_img_get_tel_pos(img, &ra_d, &dec_d);
  act_log_debug(act_log_msg("Extracted information for image %d (img_width %d, img_height %d, tel_ra_d %f, tel_dec_d %f, equinox %f)", img_id, ccd_img_get_img_width(img), ccd_img_get_img_height(img), ra_d, dec_d, ccd_img_get_start_datetime(img)));
  
  if (get_img_data(img_id, img) != 0)
  {
    act_log_error(act_log_msg("Failed to get image %d data.", img_id));
    mysql_close(conn);
    return 2;
  }
  
  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect_swapped(G_OBJECT(wnd_main), "destroy", G_CALLBACK(gtk_main_quit), NULL);
  GtkWidget *box_main = gtk_vbox_new(0, FALSE);
  gtk_container_add(GTK_CONTAINER(wnd_main), box_main);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_box_pack_start(GTK_BOX(box_main), imgdisp, 0, 0, 0);

  imgdisp_set_img(imgdisp, img);
  gtk_widget_set_size_request(imgdisp, ccd_img_get_img_width(img)*1.5, ccd_img_get_img_height(img)*1.5);
  imgdisp_set_window(imgdisp, 0, 0, ccd_img_get_img_width(img), ccd_img_get_img_height(img));
  imgdisp_set_flip_ew(imgdisp, TRUE);
  imgdisp_set_grid(imgdisp, IMGDISP_GRID_EQUAT, 1.0, 1.0);
    
  GtkWidget *btn_view_param = gtk_button_new_with_label("View parameters...");
  g_signal_connect(G_OBJECT(btn_view_param), "clicked", G_CALLBACK(show_view_param), imgdisp);
  gtk_box_pack_start(GTK_BOX(box_main), btn_view_param, TRUE, TRUE, 3);
  
  GtkWidget *btn_match = gtk_button_new_with_label("Match pattern");
  g_signal_connect(G_OBJECT(btn_match), "clicked", G_CALLBACK(match_pattern), imgdisp);
  gtk_box_pack_start(GTK_BOX(box_main), btn_match, TRUE, TRUE, 3);

  GtkWidget *lbl_coord = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box_main), lbl_coord, TRUE, TRUE, 3);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (mouse_move), lbl_coord);

  gtk_widget_show_all(wnd_main);
  gtk_main();
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
  ccd_img_set_tel_pos(img, read_ra*15.0, read_dec);
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

gboolean mouse_move(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_coord)
{
  gulong mouse_x = motdata->x, mouse_y = motdata->y;
  glong pixel_x = imgdisp_coord_pixel_x(imgdisp, mouse_x, mouse_y);
  glong pixel_y = imgdisp_coord_pixel_y(imgdisp, mouse_x, mouse_y);
  gfloat viewp_x = imgdisp_coord_viewport_x(imgdisp, mouse_x, mouse_y);
  gfloat viewp_y = imgdisp_coord_viewport_y(imgdisp, mouse_x, mouse_y);
  gfloat ra_h = imgdisp_coord_ra(imgdisp, mouse_x, mouse_y)/15.0;
  gfloat dec_d = imgdisp_coord_dec(imgdisp, mouse_x, mouse_y);
  gfloat val = imgdisp_get_img_value(imgdisp, pixel_x, pixel_y);
  
  struct rastruct ra;
  convert_H_HMSMS_ra(ra_h, &ra);
  char *ra_str = ra_to_str(&ra);
  struct decstruct dec;
  convert_D_DMS_dec(dec_d, &dec);
  char *dec_str = dec_to_str(&dec);
  
  char str[256];
  sprintf(str, "mX: %lu  ;  mY: %lu\nX: %ld  ;  Y: %ld  ;  val  %5.3f\nvX: %f  ;  vY: %f\nRA: %s  ;  Dec: %s", mouse_x, mouse_y, pixel_x, pixel_y, val, viewp_x, viewp_y, ra_str, dec_str);
  gtk_label_set_text(GTK_LABEL(lbl_coord), str);
  free(ra_str);
  free(dec_str);
  
  return FALSE;
}

void view_param_response(GtkWidget *dialog, gint response_id)
{
  if (response_id == GTK_RESPONSE_CANCEL)
    view_param_dialog_revert(dialog);
  else
    gtk_widget_destroy(dialog);
}

void show_view_param(GtkWidget *btn_view_param, gpointer imgdisp)
{
  GtkWidget *dialog = view_param_dialog_new(gtk_widget_get_toplevel(btn_view_param), GTK_WIDGET(imgdisp));
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(view_param_response), NULL);
  gtk_widget_show_all(dialog);
}

void match_pattern(GtkWidget *btn_match, gpointer imgdisp)
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
  act_log_debug(act_log_msg("number of catalog stars in vicinity: %d\n", num_pat));
  if (num_pat < MIN_NUM_STARS)
  {
    act_log_error(act_log_msg("Too few catalog stars in vicinity (%d must be %d)", num_pat, MIN_NUM_STARS));
    point_list_clear(img_pts);
    return;
  }
  
  // Match the two lists of points
  GSList *map = find_point_list_map(img_pts, pat_pts, DEFAULT_RADIUS);
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
  
  act_log_debug(act_log_msg("Pattern match result:  %12.6f %12.6f  %12.6f %12.6f  %6.2f\" %6.2f", ra_d, dec_d, ra_d+rashift/3600.0, dec_d+decshift/3600.0, rashift, decshift));
}

PointList *image_extract_stars(CcdImg *img, GtkWidget *imgdisp)
{
  float conv[9] = {1, 2, 1, 2, 4, 2, 1, 2, 1};
  float mean=0.0, stddev=0.0;
  int i, num_pix=ccd_img_get_img_len(img);
  float const *img_data = ccd_img_get_img_data(img);
  for (i=0; i<num_pix; i++)
    mean += img_data[i];
  mean /= num_pix;
  for (i=0; i<num_pix; i++)
    stddev += pow(mean-img_data[i],2.0);
  stddev /= num_pix;
  stddev = pow(stddev, 0.5);
  
  sepobj *obj = NULL;
  int ret, num_stars;
  ret = sep_extract((void *)img_data, NULL, SEP_TFLOAT, SEP_TFLOAT, 0, ccd_img_get_win_width(img), ccd_img_get_win_height(img), mean+3*stddev, 5, conv, 3, 3, 32, 0.005, 1, 1.0, &obj, &num_stars);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to extract stars from image - SEP error code %d", ret));
    return point_list_new();
  }
  act_log_debug(act_log_msg("  %d stars in image", num_stars));
  
  PointList *star_list = point_list_new_with_length(num_stars);
  gfloat tmp_ra, tmp_dec;
  for (i=0; i<num_stars; i++)
  {
    ret = imgdisp_coord_equat(imgdisp, obj[i].x, obj[i].y, &tmp_ra, &tmp_dec);
    if (ret < 0)
    {
      act_log_error(act_log_msg("Failed to calculate RA and Dec of star %d in star list."));
      continue;
    }
    if (tmp_ra < 0.0)
      tmp_ra += 360.0;
    else if (tmp_ra >= 360.0)
      tmp_ra -= 360.0;
    act_log_debug(act_log_msg("    %6.2lf  %6.2lf      %10.5f  %10.5f", obj[i].x, obj[i].y, tmp_ra, tmp_dec));
    ret = point_list_append(star_list, tmp_ra, tmp_dec);
    if (!ret)
      act_log_debug(act_log_msg("Failed to add identified star %d to stars list."));
  }
  
  return star_list;
}

PointList *get_pat_points(gdouble ra_d, gdouble dec_d, gdouble equinox, gdouble radius_d)
{
  gdouble ra_d_fk5, dec_d_fk5;
  string256 qrystr, constr_str, reg_list;
  MYSQL_RES *result;
  MYSQL_ROW row;
  
  precess_fk5(ra_d, dec_d, equinox, &ra_d_fk5, &dec_d_fk5);
  gdouble ra_radius_d = radius_d / cos(dec_d_fk5*ONEPI/180.0);
  
  if (dec_d_fk5 + radius_d >= 90.0)
    sprintf(constr_str, "dec_min<=%f AND dec_max>=90.0", dec_d_fk5-radius_d);
  else if (dec_d_fk5 - radius_d <= -90.0)
    sprintf(constr_str, "dec_min<=-90.0 AND dec_max>=%f", dec_d_fk5+radius_d);
  else if (ra_d_fk5 - ra_radius_d < 0.0)
    sprintf(constr_str, "dec_max>=%f AND dec_min<=%f AND ((ra_max>=360.0 AND ra_min<=%f) OR (ra_max>=%f AND ra_min<=0.0))", dec_d_fk5-radius_d, dec_d_fk5+radius_d, ra_d_fk5-ra_radius_d+360.0, ra_d_fk5+ra_radius_d);
  else if (ra_d_fk5 + ra_radius_d >= 360.0)
    sprintf(constr_str, "dec_max>=%f AND dec_min<=%f AND ((ra_max>=360.0 AND ra_min<=%f) OR (ra_max>=%f AND ra_min<=0.0))", dec_d_fk5-radius_d, dec_d_fk5+radius_d, ra_d_fk5-ra_radius_d, ra_d_fk5+ra_radius_d-360.0);
  else 
    sprintf(constr_str, "dec_max>=%f AND dec_min<=%f AND ra_max>=%f AND ra_min<=%f", dec_d_fk5-radius_d, dec_d_fk5+radius_d, ra_d_fk5-ra_radius_d, ra_d_fk5+ra_radius_d);
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
  
  if (dec_d_fk5 + radius_d >= 90.0)
    sprintf(constr_str, "dec_d_fk5>%lf AND dec_d_fk5<90.0", dec_d_fk5-radius_d);
  else if (dec_d_fk5 - radius_d <= -90.0)
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>-90.0", dec_d_fk5+radius_d);
  else if (ra_d_fk5 - ra_radius_d < 0.0)
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>%lf AND (ra_d_fk5<%lf OR ra_d_fk5>%lf)", dec_d_fk5+radius_d, dec_d_fk5-radius_d, ra_d_fk5+ra_radius_d, ra_d_fk5-ra_radius_d+360.0);
  else if (ra_d_fk5 + ra_radius_d >= 360.0)
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>%lf AND (ra_d_fk5<%lf OR ra_d_fk5>%lf)", dec_d_fk5+radius_d, dec_d_fk5-radius_d, ra_d_fk5+ra_radius_d-360.0, ra_d_fk5-ra_radius_d);
  else 
    sprintf(constr_str, "dec_d_fk5<%lf AND dec_d_fk5>%lf AND ra_d_fk5<%lf AND ra_d_fk5>%lf", dec_d_fk5+radius_d, dec_d_fk5-radius_d, ra_d_fk5+ra_radius_d, ra_d_fk5-ra_radius_d);
  
  sprintf(qrystr, "SELECT ra_d_fk5, dec_d_fk5 FROM gsc1 WHERE reg_id IN (%s) AND (%s)", reg_list, constr_str);
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
  *ra_d_fk5 = ra_rad * 180.0 / ONEPI;
  *dec_d_fk5 = dec_rad * 180.0 / ONEPI;
}
