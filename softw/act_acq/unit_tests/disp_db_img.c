/* Compile from local directory with:
 * gcc -Wall -Wextra `pkg-config --cflags gtk+-2.0 gtkglext-1.0` -I../ -I../../../libs/ 
 * ./disp_db_img.c ../ccd_img.c ../imgdisp.c ../view_param_dialog.c ../../../libs/act_log.c ../../../libs/act_timecoord.c 
 * `pkg-config --libs gtk+-2.0 gtkglext-1.0` -lmysqlclient -lm -o ./disp_db_img
 */

#include <gtk/gtk.h>
#include <act_log.h>
#include <imgdisp.h>
#include <ccd_img.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <mysql/mysql.h>
#include <view_param_dialog.h>
#include <act_timecoord.h>

#define IMGDB_HOST    "actphot.suth.saao.ac.za"
#define IMGDB_UNAME   "act_acq"
#define IMGDB_PASSWD  NULL

gboolean mouse_move(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_coord)
{
  gulong mouse_x = motdata->x, mouse_y = motdata->y;
  glong pixel_x = imgdisp_coord_pixel_x(imgdisp, mouse_x, mouse_y);
  glong pixel_y = imgdisp_coord_pixel_y(imgdisp, mouse_x, mouse_y);
  gfloat viewp_x = imgdisp_coord_viewport_x(imgdisp, mouse_x, mouse_y);
  gfloat viewp_y = imgdisp_coord_viewport_y(imgdisp, mouse_x, mouse_y);
  gfloat ra_h = imgdisp_coord_ra(imgdisp, mouse_x, mouse_y)/15.0;
  gfloat dec_d = imgdisp_coord_dec(imgdisp, mouse_x, mouse_y);
  
  struct rastruct ra;
  convert_H_HMSMS_ra(ra_h, &ra);
  char *ra_str = ra_to_str(&ra);
  struct decstruct dec;
  convert_D_DMS_dec(dec_d, &dec);
  char *dec_str = dec_to_str(&dec);

  char str[256];
  sprintf(str, "mX: %lu  ;  mY: %lu\nX: %ld  ;  Y: %ld\nvX: %f  ;  vY: %f\nRA: %s  ;  Dec: %s", mouse_x, mouse_y, pixel_x, pixel_y, viewp_x, viewp_y, ra_str, dec_str);
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

  MYSQL *conn;
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
  
  gchar qrystr[1024];
  sprintf(qrystr, "SELECT ccd_img.targ_id, star_prim_names.star_name, ccd_img.type, ccd_img_types.type, ccd_img.filt_id, filter_types.name, exp_t_s, start_date, start_time_h, win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y, tel_ra_h, tel_dec_d FROM ccd_img INNER JOIN star_prim_names ON star_prim_names.star_id=ccd_img.targ_id INNER JOIN ccd_img_types ON ccd_img_types.id=ccd_img.type INNER JOIN ccd_filters ON ccd_filters.id=ccd_img.filt_id INNER JOIN filter_types ON filter_types.id=ccd_filters.type WHERE ccd_img.id=%d", img_id);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve information for image %d - %s.", img_id, mysql_error(conn)));
    mysql_close(conn);
    return 2;
  }
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    if (rowcount == 0)
      act_log_error(act_log_msg("No image found with identifier %d.", img_id));
    else
      act_log_error(act_log_msg("Too many images found with identifier %d - something is very wrong.", img_id));
    mysql_close(conn);
    return 2;
  }
  row = mysql_fetch_row(result);
  glong win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y;
  gfloat tel_ra_h, tel_dec_d;
  gint ret = 0;
  ret += (sscanf(row[ 9], "%ld", &win_start_x) == 1);
  ret += (sscanf(row[10], "%ld", &win_start_y) == 1);
  ret += (sscanf(row[11], "%ld", &win_width  ) == 1);
  ret += (sscanf(row[12], "%ld", &win_height ) == 1);
  ret += (sscanf(row[13], "%ld", &prebin_x   ) == 1);
  ret += (sscanf(row[14], "%ld", &prebin_y   ) == 1);
  ret += (sscanf(row[15], "%f" , &tel_ra_h   ) == 1);
  ret += (sscanf(row[16], "%f" , &tel_dec_d  ) == 1);
  mysql_free_result(result);
  if (ret != 8)
  {
    act_log_error(act_log_msg("Failed to extract all necessary image information for image %d (%d parameters extracted).", img_id, ret));
    mysql_close(conn);
  }
  act_log_debug(act_log_msg("Extracted information for image %d (win_start_x %d, win_start_y %d, win_width %d, win_height %d, prebin_x %d, prebin_y %d, tel_ra_h %f, tel_dec_d %f)", img_id, win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y, tel_ra_h, tel_dec_d));
  
  glong img_width=win_width/prebin_x + (win_width%prebin_x != 0 ? 1 : 0);
  glong img_height=win_height/prebin_y + (win_height%prebin_y != 0 ? 1 : 0);
  gfloat img_data[(win_width/prebin_x) * (win_height/prebin_y)];
  sprintf(qrystr, "SELECT y*%ld+x, value FROM ccd_img_data WHERE ccd_img_id=%d", img_width, img_id);
  MYSQL_ROW res_row;
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Failed to retrieve pixel data for image %d - %s.", img_id, mysql_error(conn)));
    mysql_close(conn);
    return 2;
  }
  rowcount = mysql_num_rows(result);
  if (rowcount != img_width * img_height)
  {
    act_log_error(act_log_msg("Image size mismatch (database sent %d pixels, should be %d), image %d\n", img_width*img_height, rowcount, img_id));
    mysql_free_result(result);
    mysql_close(conn);
    return 2;
  }
  glong pixnum;
  gdouble pixval;
  ret = 0;
  while ((res_row = mysql_fetch_row(result)) != NULL)
  {
    if (sscanf(res_row[0], "%ld", &pixnum) != 1)
    {
      ret = 1;
      break;
    }
    if (sscanf(res_row[1], "%lf", &pixval) != 1)
    {
      ret = 1;
      break;
    }
    img_data[pixnum] = pixval;
  }
  mysql_free_result(result);
  mysql_close(conn);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Error extracting pixel data from results returned by database for image %d", img_id));
    return 2;
  }
  act_log_debug(act_log_msg("Succesfully retrieved image %d (%d pixels)", img_id, rowcount));

  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect_swapped(G_OBJECT(wnd_main), "destroy", G_CALLBACK(gtk_main_quit), NULL);
  GtkWidget *box_main = gtk_vbox_new(0, FALSE);
  gtk_container_add(GTK_CONTAINER(wnd_main), box_main);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_box_pack_start(GTK_BOX(box_main), imgdisp, 0, 0, 0);

  CcdImg *img = g_object_new (ccd_img_get_type(), NULL);
  ccd_img_set_img_type(img, IMGT_ACQ_OBJ);
  ccd_img_set_integ_t(img, 1.0);
  ccd_img_set_window(img, win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y);
  ccd_img_set_tel_pos(img, tel_ra_h*15.0, tel_dec_d);
  // ??? Calculate correct pixel size
  ccd_img_set_pixel_size(img, 2.3170, 2.3264);
  ccd_img_set_img_data(img, img_width*img_height, img_data);

  imgdisp_set_img(imgdisp, img);
  gtk_widget_set_size_request(imgdisp, img_width*1.5, img_height*1.5);
  imgdisp_set_window(imgdisp, 0, 0, img_width, img_height);
  imgdisp_set_flip_ew(imgdisp, TRUE);
  imgdisp_set_grid(imgdisp, IMGDISP_GRID_EQUAT, 1.0, 1.0);
    
  GtkWidget *btn_view_param = gtk_button_new_with_label("View parameters...");
  g_signal_connect(G_OBJECT(btn_view_param), "clicked", G_CALLBACK(show_view_param), imgdisp);
  gtk_box_pack_start(GTK_BOX(box_main), btn_view_param, TRUE, TRUE, 3);
  
  GtkWidget *lbl_coord = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box_main), lbl_coord, TRUE, TRUE, 3);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (mouse_move), lbl_coord);

  gtk_widget_show_all(wnd_main);
  gtk_main();
  return 0;
}