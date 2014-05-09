#include <gtk/gtk.h>
#include <act_log.h>
#include <imgdisp.h>
#include <ccd_img.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

void faint_changed(GtkWidget *scl_faint, gpointer imgdisp)
{
  imgdisp_set_faint_lim(GTK_WIDGET(imgdisp), gtk_range_get_value(GTK_RANGE(scl_faint)));
}

void bright_changed(GtkWidget *scl_bright, gpointer imgdisp)
{
  imgdisp_set_bright_lim(GTK_WIDGET(imgdisp), gtk_range_get_value(GTK_RANGE(scl_bright)));
}

void toggle_flip_ns(GtkWidget *btn_flip_ns, gpointer imgdisp)
{
  static gboolean flip_ns = FALSE;
  flip_ns = !flip_ns;
  imgdisp_set_flip_ns(imgdisp, flip_ns);
  if (flip_ns)
    gtk_button_set_image(GTK_BUTTON(btn_flip_ns), gtk_image_new_from_stock(GTK_STOCK_GO_DOWN,GTK_ICON_SIZE_BUTTON));
  else
    gtk_button_set_image(GTK_BUTTON(btn_flip_ns), gtk_image_new_from_stock(GTK_STOCK_GO_UP,GTK_ICON_SIZE_BUTTON));
}

void toggle_flip_ew(GtkWidget *btn_flip_ew, gpointer imgdisp)
{
  static gboolean flip_ew = FALSE;
  flip_ew = !flip_ew;
  imgdisp_set_flip_ew(imgdisp, flip_ew);
  if (flip_ew)
    gtk_button_set_image(GTK_BUTTON(btn_flip_ew), gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD,GTK_ICON_SIZE_BUTTON));
  else
    gtk_button_set_image(GTK_BUTTON(btn_flip_ew), gtk_image_new_from_stock(GTK_STOCK_GO_BACK,GTK_ICON_SIZE_BUTTON));
}

gboolean mouse_move(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_coord)
{
  gulong mouse_x = motdata->x, mouse_y = motdata->y;
  gulong pixel_x = imgdisp_coord_pixel_x(imgdisp, mouse_x, mouse_y);
  gulong pixel_y = imgdisp_coord_pixel_y(imgdisp, mouse_x, mouse_y);
  gfloat viewp_x = imgdisp_coord_viewport_x(imgdisp, mouse_x, mouse_y);
  gfloat viewp_y = imgdisp_coord_viewport_y(imgdisp, mouse_x, mouse_y);
  gfloat ra_h = imgdisp_coord_ra(imgdisp, mouse_x, mouse_y);
  gfloat dec_d = imgdisp_coord_dec(imgdisp, mouse_x, mouse_y);
  
  char str[256];
  sprintf(str, "mX: %lu  ;  mY: %lu\nX: %lu  ;  Y: %lu\nvX: %f  ;  vY: %f\nRA: %f  ;  Dec: %f", mouse_x, mouse_y, pixel_x, pixel_y, viewp_x, viewp_y, ra_h, dec_d);
  gtk_label_set_text(GTK_LABEL(lbl_coord), str);
  
  return FALSE;
}

int main(int argc, char **argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  gtk_init(&argc, &argv);
  
  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect_swapped(G_OBJECT(wnd_main), "destroy", G_CALLBACK(gtk_main_quit), NULL);
  GtkWidget *box_main = gtk_vbox_new(0, FALSE);
  gtk_container_add(GTK_CONTAINER(wnd_main), box_main);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_box_pack_start(GTK_BOX(box_main), imgdisp, 0, 0, 0);
    
  CcdImg *img = g_object_new (imgdisp_get_type(), NULL);
  ccd_img_set_img_type(img, IMGT_ACQ_OBJ);
  ccd_img_set_exp_t(img, 1.0);
  gulong width=407, height=288;
  ccd_img_set_window(img, 0, 0, width, height, 1, 1);
  gfloat img_data[width * height];
  srand(time(NULL));
  guint i;
  for (i=0; i<width * height; i++)
  {
//     double tmp = (double)rand()/RAND_MAX;
    unsigned long row=i%width, col=i/width;
    double tmp = pow((pow(row,2)+pow(col,2))/(pow(width,2)+pow(height,2)),0.5);
    img_data[i] = tmp;
  }
  ccd_img_set_img_data(img, width*height, img_data);
  imgdisp_set_img(imgdisp, img);
  gtk_widget_set_size_request(imgdisp, width, height);
  
  GtkWidget *scl_faint = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_range_set_value(GTK_RANGE(scl_faint), 0.0);
  g_signal_connect(G_OBJECT(scl_faint), "value-changed", G_CALLBACK(faint_changed), imgdisp);
  gtk_box_pack_start(GTK_BOX(box_main), scl_faint, 0, 0, 0);
  GtkWidget *scl_bright = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_range_set_value(GTK_RANGE(scl_bright), 1.0);
  g_signal_connect(G_OBJECT(scl_bright), "value-changed", G_CALLBACK(bright_changed), imgdisp);
  gtk_box_pack_start(GTK_BOX(box_main), scl_bright, 0, 0, 0);
  
  GtkWidget* btn_flip_ns = gtk_button_new_with_label("N");
  gtk_button_set_image(GTK_BUTTON(btn_flip_ns), gtk_image_new_from_stock(GTK_STOCK_GO_UP,GTK_ICON_SIZE_BUTTON));
  gtk_box_pack_start(GTK_BOX(box_main), btn_flip_ns, TRUE, TRUE, 3);
  g_signal_connect(G_OBJECT(btn_flip_ns),"clicked",G_CALLBACK(toggle_flip_ns),imgdisp);
  GtkWidget* btn_flip_EW = gtk_button_new_with_label("E");
  gtk_button_set_image(GTK_BUTTON(btn_flip_EW), gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD,GTK_ICON_SIZE_BUTTON));
  gtk_box_pack_start(GTK_BOX(box_main), btn_flip_EW, TRUE, TRUE, 3);
  g_signal_connect(G_OBJECT(btn_flip_EW),"clicked",G_CALLBACK(toggle_flip_ew),imgdisp);
  
  GtkWidget *lbl_coord = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box_main), lbl_coord, TRUE, TRUE, 3);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (mouse_move), lbl_coord);

  gtk_widget_show_all(wnd_main);
  gtk_main();
  return 0;
}