#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <gtk/gtk.h>
#include <act_log.h>
#include "pmtphot_plot.h"
#include "pmtfuncs.h"

void plot_all_toggled(gpointer user_data);
void plot_star_toggled(gpointer user_data);
void plot_box_show(gpointer user_data);
void plot_box_hide(gpointer user_data);
void plot_socket_realized(GtkWidget *sck_plot, gpointer user_data);
void save_plot_pdf(GtkWidget *btn_save_plot_pdf, gpointer user_data);
void save_plot_dialog_response(GtkWidget* dialog, gint response_id, gpointer user_data);
void disp_plot_help(GtkWidget *btn_plothelp);
// gboolean plot_box_keypress(GtkWidget *widget, GdkEvent *event, gpointer user_data);

struct plotobjects *create_plotobjs(GtkWidget *container)
{
  if (container == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  struct plotobjects *objs = malloc(sizeof(struct plotobjects));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Could not allocate memory for plot storage and display objects."));
    return NULL;
  }
  
  objs->cur_targ_id = 0;
  sprintf(objs->plotdata_filename,"/tmp/act_plotXXXXXX");
  int tmp_plot_fd = mkstemp(objs->plotdata_filename);
  if (tmp_plot_fd <= 0)
  {
    act_log_error(act_log_msg("Could not get file descriptor for temporary file to store plot data."));
    free(objs);
    return NULL;
  }
  act_log_debug(act_log_msg("Saving plot data to %s", objs->plotdata_filename));
  objs->plotdata_fp = fdopen(tmp_plot_fd, "a");
  if (objs->plotdata_fp == NULL)
  {
    act_log_error(act_log_msg("Could not get file pointer for temporary file to store plot data."));
    free(objs);
    return NULL;
  }
  
  objs->gnuplot_fp = popen("gnuplot -background 'black' 2> /dev/null", "w");
  if (objs->gnuplot_fp == NULL)
  {
    act_log_error(act_log_msg("Could not start GNUplot with pipe."));
    fclose(objs->plotdata_fp);
    free(objs);
    return NULL;
  }
  fprintf(objs->gnuplot_fp, "set term dumb\n");
  fprintf(objs->gnuplot_fp, "set nokey\n");
  fprintf(objs->gnuplot_fp, "set border linecolor rgb 'green'\n");
  fprintf(objs->gnuplot_fp, "set style line 1 pt 13\n");
  fprintf(objs->gnuplot_fp, "set grid lc 2\n");
  fprintf(objs->gnuplot_fp, "set xlabel 'Frac. JD' textcolor rgb 'green' font ',15'\n");
  fprintf(objs->gnuplot_fp, "set ylabel 'Counts' textcolor rgb 'green' font ',15'\n");
  fflush(objs->gnuplot_fp);
  
  GtkWidget *box_plot = gtk_table_new(3,2,FALSE);
  gtk_container_add(GTK_CONTAINER(container),box_plot);
  
  objs->btn_plot_all = gtk_toggle_button_new_with_label("Plot All");
  g_object_ref(objs->btn_plot_all);
  g_signal_connect_swapped(G_OBJECT(objs->btn_plot_all),"toggled",G_CALLBACK(plot_all_toggled),objs);
  gtk_table_attach(GTK_TABLE(box_plot),objs->btn_plot_all,0,1,0,1,GTK_FILL|GTK_EXPAND,GTK_FILL,3,3);
  objs->btn_plot_star = gtk_toggle_button_new_with_label("Plot Star");
  g_object_ref(objs->btn_plot_star);
  g_signal_connect_swapped(G_OBJECT(objs->btn_plot_star),"toggled",G_CALLBACK(plot_star_toggled),objs);
  gtk_table_attach(GTK_TABLE(box_plot),objs->btn_plot_star,1,2,0,1,GTK_FILL|GTK_EXPAND,GTK_FILL,3,3);
  
  objs->evb_plot_box = gtk_event_box_new();
  g_object_ref(objs->evb_plot_box);
  gtk_table_attach(GTK_TABLE(box_plot),objs->evb_plot_box,0,2,1,2,GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,3,3);
  g_signal_connect_swapped(G_OBJECT(objs->evb_plot_box),"show",G_CALLBACK(plot_box_show),objs);
  g_signal_connect_swapped(G_OBJECT(objs->evb_plot_box),"hide",G_CALLBACK(plot_box_hide),objs);
//   g_signal_connect(G_OBJECT(objs->evb_plot_box), "key-press-event", G_CALLBACK(plot_box_keypress), objs);
//   int plot_events = gtk_widget_get_events(objs->evb_plot_box);
//   gtk_widget_set_events(objs->evb_plot_box, plot_events & (~(GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK)));
  
  objs->btn_save_plot_pdf = gtk_button_new_with_label("Save PDF");
  g_object_ref(objs->btn_save_plot_pdf);
  g_signal_connect(G_OBJECT(objs->btn_save_plot_pdf),"clicked",G_CALLBACK(save_plot_pdf),objs);
  gtk_table_attach(GTK_TABLE(box_plot),objs->btn_save_plot_pdf,0,1,2,3,GTK_FILL|GTK_EXPAND,GTK_FILL,3,3);
  objs->btn_plothelp = gtk_button_new_with_label("Help");
  g_object_ref(objs->btn_plothelp);
  g_signal_connect(G_OBJECT(objs->btn_plothelp),"clicked",G_CALLBACK(disp_plot_help),NULL);
  gtk_table_attach(GTK_TABLE(box_plot),objs->btn_plothelp,1,2,2,3,GTK_FILL|GTK_EXPAND,GTK_FILL,3,3);
  
  return objs;
}

void finalise_plotobjs(struct plotobjects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  gtk_widget_hide_all(objs->evb_plot_box);
  fclose(objs->plotdata_fp);
  objs->plotdata_fp = NULL;
  fprintf(objs->gnuplot_fp,"exit\n");
  fflush(objs->gnuplot_fp);
  pclose(objs->gnuplot_fp);
  objs->gnuplot_fp = NULL;
  g_object_unref(objs->btn_plot_all);
  g_object_unref(objs->btn_plot_star);
}

void plot_set_pmtdetail(struct plotobjects *objs, struct pmtdetailstruct *pmtdetail)
{
  if ((objs == NULL) || (pmtdetail == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (objs->plotdata_fp == NULL)
  {
    act_log_error(act_log_msg("Plot data file not available."));
    return;
  }
  if (pmt_integrating(pmtdetail->pmt_stat))
  {
    act_log_debug(act_log_msg("Currently integrating, so will not plot estimated count rate."));
    return;
  }
  objs->cur_targ_id = 0;
  double tmp_time = convert_HMSMS_H_time(&pmtdetail->cur_unitime) / 24.0;
  if (tmp_time < 0.5)
    tmp_time += 0.5;
  fprintf(objs->plotdata_fp, "%10.7f\t%15lu\t%d\n", tmp_time, pmtdetail->est_counts_s, 0);
  fflush(objs->plotdata_fp);
  if ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_all))) || (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_star))))
  {
    fprintf(objs->gnuplot_fp,"replot\n");
    fflush(objs->gnuplot_fp);
  }
}

void plot_set_targid(struct plotobjects *objs, int targ_id)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  objs->cur_targ_id = targ_id;
}

void plot_add_data(struct plotobjects *objs, struct pmtintegstruct *pmtinteg)
{
  if ((objs == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if ((objs->gnuplot_fp == NULL) || (objs->plotdata_fp == NULL))
  {
    act_log_error(act_log_msg("GNUplot pipe pointer or plot data file pointer invalid."));
    return;
  }
  
  struct pmtintegstruct *lastinteg = pmtinteg;
  char done = lastinteg->done;
  double tmp_time;
  while (done > 0)
  {
    tmp_time = convert_HMSMS_H_time(&lastinteg->start_unitime) / 24.0;
    if (tmp_time < 0.5)
      tmp_time += 0.5;
    fprintf(objs->plotdata_fp, "%10.7f\t%15lu\t%d\n", tmp_time, pmtinteg->counts, pmtinteg->targid);
    if (lastinteg->next != NULL)
    {
      lastinteg = (struct pmtintegstruct *)lastinteg->next;
      done = lastinteg->done;
    }
    else
      done = 0;
  }
  
  fflush(objs->plotdata_fp);
  objs->cur_targ_id = lastinteg->targid;
  fprintf(objs->gnuplot_fp,"replot\n");
  fflush(objs->gnuplot_fp);
}

void plot_all_toggled(gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct plotobjects *objs = (struct plotobjects *)user_data;
  if ((!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_all))) && (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_star))))
  {
    gtk_widget_hide_all(objs->evb_plot_box);
    gtk_widget_hide(objs->btn_save_plot_pdf);
    gtk_widget_hide(objs->btn_plothelp);
    return;
  }
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_all)))
    return;
  act_log_debug(act_log_msg("Plot all toggled"));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_plot_star),FALSE);
  if (!gtk_widget_get_visible(objs->evb_plot_box))
    gtk_widget_show(objs->evb_plot_box);
  gtk_widget_show(objs->btn_save_plot_pdf);
  gtk_widget_show(objs->btn_plothelp);
  fprintf(objs->gnuplot_fp,"plot '%s' w lp ls 1\n", objs->plotdata_filename);
  fflush(objs->gnuplot_fp);
}

void plot_star_toggled(gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct plotobjects *objs = (struct plotobjects *)user_data;
  if ((!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_all))) && (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_star))))
  {
    gtk_widget_hide_all(objs->evb_plot_box);
    gtk_widget_hide(objs->btn_save_plot_pdf);
    gtk_widget_hide(objs->btn_plothelp);
    return;
  }
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_star)))
    return;
  act_log_debug(act_log_msg("Plot star toggled."));
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_plot_all),FALSE);
  if (!gtk_widget_get_visible(objs->evb_plot_box))
    gtk_widget_show(objs->evb_plot_box);
  gtk_widget_show(objs->btn_save_plot_pdf);
  gtk_widget_show(objs->btn_plothelp);
  fprintf(objs->gnuplot_fp,"plot '%s' using 1:($3==%d?$2:1/0) w lp ls 1\n", objs->plotdata_filename, objs->cur_targ_id);
  fflush(objs->gnuplot_fp);
}

void plot_box_show(gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct plotobjects *objs = (struct plotobjects *)user_data;
  if ((!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_all))) && (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_plot_star))))
  {
    act_log_debug(act_log_msg("SHOW event triggered, but neither plot buttons are active. Probably the programme is starting up. Ignoring."));
    gtk_widget_hide_all(objs->evb_plot_box);
    gtk_widget_hide(objs->btn_save_plot_pdf);
    gtk_widget_hide(objs->btn_plothelp);
    return;
  }
  act_log_debug(act_log_msg("Creating plot box"));
  GtkWidget *sck_plot = gtk_bin_get_child(GTK_BIN(objs->evb_plot_box));
  if (sck_plot != NULL)
  {
    act_log_error(act_log_msg("Strange - plot socket container already contains a plot socket. Removing old socket and creating a new one."));
    gtk_widget_destroy(sck_plot);
  }
  sck_plot = gtk_socket_new();
  g_signal_connect(G_OBJECT(sck_plot),"realize",G_CALLBACK(plot_socket_realized),user_data);
  gtk_container_add(GTK_CONTAINER(objs->evb_plot_box),sck_plot);
  gtk_widget_set_size_request(sck_plot, 400, 300);
  gtk_widget_show_all(objs->evb_plot_box);
}

void plot_box_hide(gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  act_log_debug(act_log_msg("Hiding plot box"));
  struct plotobjects *objs = (struct plotobjects *)user_data;
  fprintf(objs->gnuplot_fp, "set term dumb\n");
  fflush(objs->gnuplot_fp);
  GtkWidget *sck_plot = gtk_bin_get_child(GTK_BIN(objs->evb_plot_box));
  if (sck_plot != NULL)
    gtk_widget_destroy(sck_plot);
}

void plot_socket_realized(GtkWidget *sck_plot, gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  act_log_debug(act_log_msg("Plot box realized"));
  struct plotobjects *objs = (struct plotobjects *)user_data;
  gtk_widget_set_can_focus (sck_plot, TRUE);
  gtk_widget_grab_focus(sck_plot);
  int width, height;
  width = sck_plot->allocation.width;
  height = sck_plot->allocation.height;
  int sock_id = gtk_socket_get_id(GTK_SOCKET(sck_plot));
  if (sock_id <= 0)
  {
    act_log_error(act_log_msg("Could not get identifier for plot socket."));
    return;
  }
  act_log_debug(act_log_msg("Plot window ID: %d (%x)", sock_id, sock_id));
  fprintf(objs->gnuplot_fp,"set term x11 window \"%x\" noraise size %d,%d\n", sock_id, width, height);
  fprintf(objs->gnuplot_fp,"replot\n");
  fflush(objs->gnuplot_fp);
}

void save_plot_pdf(GtkWidget *btn_save_plot_pdf, gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct passwd *pw = getpwuid(getuid());
  GtkWidget *dialog;
  dialog = gtk_file_chooser_dialog_new ("Save plot as PDF", GTK_WINDOW(gtk_widget_get_toplevel(btn_save_plot_pdf)), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), pw->pw_dir);
  gtk_widget_show_all(dialog);
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(save_plot_dialog_response), user_data);
}

void save_plot_dialog_response(GtkWidget* dialog, gint response_id, gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (response_id != GTK_RESPONSE_ACCEPT)
  {
    gtk_widget_destroy(dialog);
    return;
  }
  struct plotobjects *objs = (struct plotobjects *)user_data;
  char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  
  fprintf(objs->gnuplot_fp, "set term pdf enhanced size 29.7cm,21cm\n");
  fprintf(objs->gnuplot_fp, "set output '%s'\n", filename);
  fprintf(objs->gnuplot_fp, "set border\n");
  fprintf(objs->gnuplot_fp, "set style line 1 pt 13\n");
  fprintf(objs->gnuplot_fp, "set xlabel 'Frac. JD' textcolor rgb 'black' font ',12'\n");
  fprintf(objs->gnuplot_fp, "set ylabel 'Counts' textcolor rgb 'black' font ',12'\n");
  fprintf(objs->gnuplot_fp, "replot\n");
  if (gtk_widget_get_visible(objs->evb_plot_box))
  {
    gtk_widget_hide_all(objs->evb_plot_box);
    gtk_widget_show_all(objs->evb_plot_box);
    fprintf(objs->gnuplot_fp, "set output\n");
  }
  else
    fprintf(objs->gnuplot_fp, "set term dumb\n");
  fprintf(objs->gnuplot_fp, "set border linecolor rgb 'green'\n");
  fprintf(objs->gnuplot_fp, "set style line 1 pt 13\n");
  fprintf(objs->gnuplot_fp, "set xlabel 'Frac. JD' textcolor rgb 'green' font ',15'\n");
  fprintf(objs->gnuplot_fp, "set ylabel 'Counts' textcolor rgb 'green' font ',15'\n");
  fflush(objs->gnuplot_fp);
  
  g_free (filename);
  gtk_widget_destroy(dialog);
}

void disp_plot_help(GtkWidget *btn_plothelp)
{
  GtkWidget *plot_help_dialog = gtk_message_dialog_new(GTK_WINDOW(gtk_widget_get_toplevel(btn_plothelp)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "Interactive Plot Window Help");
  gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(plot_help_dialog),
  "Plotting is done with the assistance of GNUplot (www.gnuplot.info).\n\n\
  All the interactive features provided by GNUplot are available in the plot \
  window. A brief description of the most commonly used features follows here.\n\n\
  When moving the cursor over the plot window, the X and Y coordinates of the \
  pointer are displayed in the bottom-left corner.\n\n\
  Zoom in and out by holding the Ctrl key while scrolling (with the mouse wheel) \
  up and down, respectively.\n\n\
  Alternatively, select a zooming region by right-clicking in one corner of the \
  desired region, moving to the opposite corner and left-clicking.\n\n\
  The plot can be panned up and down (Y-direction) with the mouse wheel \
  and panned left and right (X-direction) with the mouse wheel while holding Shift.\n\n\
  'a' key : Activate autoscale (undo zoom/pan).\n\n\
  'b' key : Toggle border visibility.\n\n\
  'g' key : Toggle grid visibility.\n\n\
  'r' key : Toggle ruler.\n\n\
  'l' key : Toggle logarithmic Y-axis scale.");
  gtk_widget_show_all(plot_help_dialog);
  g_signal_connect_swapped(G_OBJECT(plot_help_dialog), "response", G_CALLBACK(gtk_widget_destroy), plot_help_dialog);
}

/*gboolean plot_box_keypress(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
  struct plotobjects *objs = (struct plotobjects *)user_data;
  switch(event->keyval)
  {
    case GDK_KEY_a:
      fprintf(objs->gnuplot_fp, "set border linecolor rgb 'green'\n");
      fprintf(objs->gnuplot_fp, "set style line 1 pt 13 lc 2\n");
      break;
    case GDK_KEY_g:
      break;
    case GDK_KEY_Left:
      break;
    case GDK_KEY_Right:
      break;
    case GDK_KEY_Up:
      break;
    case GDK_KEY_Down:
      break;
    default:
      act_log_debug(act_log_msg("Invalid key pressed."));
      break;
  }
  return TRUE;
}*/
