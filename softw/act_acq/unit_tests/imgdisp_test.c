#include <gtk/gtk.h>
#include <act_log.h>
#include <imgdisp.h>
#include <ccd_img.h>
#include <stdlib.h>
#include <time.h>

int main(int argc, char **argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  gtk_init(&argc, &argv);
  
  GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  g_signal_connect_swapped(G_OBJECT(wnd_main), "destroy", G_CALLBACK(gtk_main_quit), NULL);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_container_add(GTK_CONTAINER(wnd_main), imgdisp);
  
/*  CcdImg *img = g_object_new (imgdisp_get_type(), NULL);
  ccd_img_set_img_type(img, IMGT_ACQ_OBJ);
  ccd_img_set_exp_t(img, 1.0);
  gulong width=407, height=288;
  ccd_img_set_window(img, 0, 0, width, height, 1, 1);
  gfloat img_data[width * height];
  /// TODO: Check if initialisation can be optimised
  guint i;
  for (i=0; i<width * height; i++)
    img_data[i] = 0.0;
  ccd_img_set_img_data(img, width*height, img_data);
  imgdisp_set_img(imgdisp, img);*/
  
  gtk_widget_show_all(wnd_main);
  gtk_main();
  return 0;
}