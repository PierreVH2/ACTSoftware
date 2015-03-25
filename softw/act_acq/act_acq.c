#include <gtk/gtk.h>
#include <argtable2.h>
#include <act_log.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include "imgdisp.h"
#include "acq_net.h"
#include "acq_store.h"
#include "ccd_cntrl.h"
#include "ccd_img.h"
#include "view_param_dialog.h"
#include "expose_dialog.h"
#include "point_list.h"
#include "sep/sep.h"

#define TABLE_PADDING 3

#define GUICHECK_LOOP_PERIOD       5000
#define RECONNECT_TIMEOUT_PERIOD  30000

/// X centre of aperture on acquisition image in pixels
#define XAPERTURE 172
/// Y centre of aperture on acquisition image in pixels
#define YAPERTURE 110

/// Minimum number of stars for a positive field identification
#define MIN_NUM_STARS    6
/// If during a target set procedure an insufficient number of stars are extracted from the image, retry with an exposure time that is EXP_RETRY_FACT than for the previous attempt
#define EXP_RETRY_FACT   3
/// Search radius for pattern matching in degrees
#define PAT_SEARCH_RAD   0.5

#define SEC_TO_YEAR(sec)   (1970 + sec/(float)31556926)

enum
{
  MODE_IDLE,
  MODE_MANUAL_EXP,
  MODE_TARGSET_EXP,
  MODE_DATACCD_EXP,
  MODE_ERR_RESTART,
  MODE_CANCEL
};

struct acq_objects
{
  gint mode;
  CcdCntrl *cntrl;
  AcqStore *store;
  AcqNet *net;
  
  GtkWidget *box_main;
  GtkWidget *imgdisp;
  
  GtkWidget *lbl_prog_stat;
  GtkWidget *lbl_ccd_stat;
  GtkWidget *lbl_exp_rem;
  
  GtkWidget *lbl_store_stat;
  GtkWidget *btn_expose;
  GtkWidget *btn_cancel;
  
  gulong cur_targ_id;
  gchar *cur_targ_name;
  gulong cur_user_id;
  gchar *cur_user_name;
  
  guchar last_imgt;
  gfloat last_exp_t;
  guint last_repeat;
};

void acq_net_init(AcqNet *net, AcqStore *store, CcdCntrl *cntrl);
gboolean guicheck_timeout(gpointer user_data);
void view_param_click(GtkWidget *btn_view_param, gpointer imgdisp);
void view_param_response(GtkWidget *view_param_dialog, gint response_id);
void expose_click(GtkWidget *btn_expose, gpointer user_data);
void expose_response(GtkWidget *dialog, gint response_id, gpointer user_data);
void cancel_click(GtkWidget *btn_cancel, gpointer user_data);
gboolean imgdisp_mouse_move_view(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_equat);
gboolean imgdisp_mouse_move_equat(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_view);
void ccd_stat_update(GObject *ccd_cntrl, guchar new_stat, gpointer user_data);
void ccd_stat_err_retry(struct acq_objects *objs);
void ccd_stat_err_no_recov(struct acq_objects *objs);
void ccd_new_image(GObject *ccd_cntrl, GObject *img, gpointer user_data);
PointList *image_extract_stars(CcdImg *img);
gint targset_exp_retry(CcdCntrl *cntrl, CcdImg *img);
gboolean reconnect_timeout(gpointer user_data);
void store_stat_update(GObject *acq_store, gpointer lbl_store_stat);
void coord_received(GObject *acq_net, gdouble tel_ra, gdouble tel_dec, gpointer user_data);
void guisock_received(GObject *acq_net, gulong win_id, gpointer box_main);
void destroy_gui_plug(GtkWidget *plug, gpointer box_main);
void change_user(GObject *acq_net, gulong new_user_id, gpointer user_data);
void change_target(GObject *acq_net, gulong new_targ_id, gchar *new_targ_name, gpointer user_data);
void targset_start(GObject *acq_net, gdouble targ_ra, gdouble targ_dec, gpointer user_data);
void targset_stop(GObject *acq_net, gpointer user_data);
void data_ccd_start(GObject *acq_net, gpointer ccd_cmd, gpointer user_data);
void data_ccd_stop(GObject *acq_net, gpointer user_data);
void prog_change_mode(struct acq_objects *objs, guchar new_mode);

/** \brief Main function.
 * \param argc Number of command-line arguments
 * \param argv Command-line arguments.
 * \return 0 upon success, 2 if incorrect command-line arguments are specified, 1 otherwise.
 * \todo Move gtk_init to before argtable command-line parsing.
 *
 * Tasks:
 *  - Parse command-line arguments.
 *  - Establish network connection with controller.
 *  - Open photometry-and-time driver character device.
 *  - Create GUI.
 *  - Start main programme loop.
 *  - Close all open file descriptors and exit.
 */
int main(int argc, char** argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  
  const char *host, *port;
  gtk_init(&argc, &argv);
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_str *sqlconfigarg = arg_str0("s", "sqlconfighost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
  struct arg_end *endargs = arg_end(10);
  void* argtable[] = {addrarg, portarg, sqlconfigarg, endargs};
  if (arg_nullcheck(argtable) != 0)
    act_log_error(act_log_msg("Argument parsing error: insufficient memory."));
  int argparse_errors = arg_parse(argc,argv,argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr,endargs,argv[0]);
    return 2;
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
  
  CcdCntrl *cntrl = ccd_cntrl_new();
  if (cntrl == NULL)
  {
    act_log_error(act_log_msg("Failed to create CCD control object"));
    return 1;
  }
  
  AcqStore *store = acq_store_new(host);
  if (store == NULL)
  {
    act_log_error(act_log_msg("Failed to create database storage link"));
    g_object_unref(G_OBJECT(cntrl));
    return 1;
  }
  
  AcqNet *net = acq_net_new(host, port);
  if (net == NULL)
  {
    act_log_error(act_log_msg("Failed to establish network connection."));
    g_object_unref(G_OBJECT(cntrl));
    g_object_unref(G_OBJECT(store));
    return 1;
  }
  acq_net_init(net, store, cntrl);
  
  // Create GUI
  GtkWidget *box_main = gtk_vbox_new(FALSE, TABLE_PADDING);
  g_object_ref(box_main);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_box_pack_start(GTK_BOX(box_main), imgdisp, TRUE, TRUE, TABLE_PADDING);
  GtkWidget* box_controls = gtk_table_new(3,3,TRUE);
  gtk_box_pack_start(GTK_BOX(box_main), box_controls, TRUE, TRUE, TABLE_PADDING);
  
  gtk_widget_set_size_request(imgdisp, ccd_cntrl_get_max_width(cntrl), ccd_cntrl_get_max_height(cntrl));
  imgdisp_set_window(imgdisp, 0, 0, ccd_cntrl_get_max_width(cntrl), ccd_cntrl_get_max_height(cntrl));
  
  GtkWidget *lbl_mouse_view = gtk_label_new("X:           \nY:           ");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_mouse_view, 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *lbl_mouse_equat = gtk_label_new("RA:           \nDec:           ");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_mouse_equat, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_view_param = gtk_button_new_with_label("View...");
  gtk_table_attach(GTK_TABLE(box_controls), btn_view_param, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *lbl_prog_stat = gtk_label_new("IDLE");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_prog_stat, 0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *lbl_ccd_stat = gtk_label_new("CCD OK");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_ccd_stat, 1,2,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *lbl_exp_rem = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_exp_rem, 2,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *lbl_store_stat = gtk_label_new("Store OK");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_store_stat, 0,1,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_expose = gtk_button_new_with_label("Expose...");
  gtk_table_attach(GTK_TABLE(box_controls), btn_expose, 1,2,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
  gtk_table_attach(GTK_TABLE(box_controls), btn_cancel, 2,3,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  struct acq_objects objs = 
  {
    .mode = MODE_IDLE,
    .cntrl = cntrl,
    .store = store,
    .net = net,
    .box_main = box_main,
    .imgdisp = imgdisp,
    .lbl_prog_stat = lbl_prog_stat,
    .lbl_ccd_stat = lbl_ccd_stat,
    .lbl_exp_rem = lbl_exp_rem,
    .lbl_store_stat = lbl_store_stat,
    .btn_expose = btn_expose,
    .btn_cancel = btn_cancel,
  };
  
  // Connect signals
  g_signal_connect (G_OBJECT(btn_view_param), "click", G_CALLBACK (view_param_click), imgdisp);
  g_signal_connect (G_OBJECT(btn_expose), "click", G_CALLBACK(expose_click), &objs);
  g_signal_connect (G_OBJECT(btn_cancel), "click", G_CALLBACK(cancel_click), &objs);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (imgdisp_mouse_move_view), lbl_mouse_view);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (imgdisp_mouse_move_equat), lbl_mouse_equat);
  g_signal_connect (G_OBJECT(cntrl), "ccd-stat-update", G_CALLBACK (ccd_stat_update), &objs);
  g_signal_connect (G_OBJECT(cntrl), "ccd-new-image", G_CALLBACK (ccd_new_image), &objs);
  g_signal_connect (G_OBJECT(store), "store-status-update", G_CALLBACK(store_stat_update), lbl_store_stat);
  g_signal_connect (G_OBJECT(net), "coord-received", G_CALLBACK(coord_received), cntrl);
  g_signal_connect (G_OBJECT(net), "gui-socket", G_CALLBACK(guisock_received), box_main);
  g_signal_connect (G_OBJECT(net), "change-user", G_CALLBACK(change_user), &objs);
  g_signal_connect (G_OBJECT(net), "change-target", G_CALLBACK(change_target), &objs);
  g_signal_connect (G_OBJECT(net), "targset-start", G_CALLBACK(targset_start), &objs);
  g_signal_connect (G_OBJECT(net), "targset-stop", G_CALLBACK(targset_stop), &objs);
  g_signal_connect (G_OBJECT(net), "data-ccd-start", G_CALLBACK(data_ccd_start), &objs);
  g_signal_connect (G_OBJECT(net), "data-ccd-stop", G_CALLBACK(data_ccd_stop), &objs);
  gint guicheck_to_id = g_timeout_add(GUICHECK_LOOP_PERIOD, guicheck_timeout, &objs);
  
  act_log_debug(act_log_msg("Entering main loop."));
  gtk_main();
  
  act_log_normal(act_log_msg("Exiting"));
  g_source_remove(guicheck_to_id);
  g_object_unref(G_OBJECT(net));
  g_object_unref(G_OBJECT(store));
  g_object_unref(G_OBJECT(cntrl));
  return 0;
}

void acq_net_init(AcqNet *net, AcqStore *store, CcdCntrl *cntrl)
{
  acq_net_set_min_exp_t_s(net, ccd_cntrl_get_min_exp_t_sec(cntrl));
  acq_net_set_max_exp_t_s(net, ccd_cntrl_get_max_exp_t_sec(cntrl));
  gchar *ccd_id = ccd_cntrl_get_ccd_id(cntrl);
  acq_net_set_ccd_id(net, ccd_id);
  g_free(ccd_id);
  acq_filters_list_t ccd_filters;
  if (!acq_store_get_filt_list(store, &ccd_filters))
  {
    act_log_error(act_log_msg("Failed to get CCD filters list from database."));
    return;;
  }
  gint i, num_added=0;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    if (ccd_filters.filt[i].db_id <= 0)
      continue;
    if (!acq_net_add_filter(net, ccd_filters.filt[i].name, ccd_filters.filt[i].slot, ccd_filters.filt[i].db_id))
      act_log_error(act_log_msg("Failed to add filter with database ID %d to the internal list of available filters.", ccd_filters.filt[i].db_id));
    else
      num_added++;
  }
  if (num_added == 0)
    return;
  acq_net_set_ccdcap_ready(net, TRUE);
}

gboolean guicheck_timeout(gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL;
  
  if (!main_embedded)
    acq_net_request_guisocket(objs->net);
  return TRUE;
}

void view_param_click(GtkWidget *btn_view_param, gpointer imgdisp)
{
  GtkWidget *dialog = view_param_dialog_new(gtk_widget_get_toplevel(btn_view_param), GTK_WIDGET(imgdisp));
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(view_param_response), NULL);
  gtk_widget_show_all(dialog);
}

void view_param_response(GtkWidget *dialog, gint response_id)
{
  if (response_id == GTK_RESPONSE_CANCEL)
    view_param_dialog_revert(dialog);
  else
    gtk_widget_destroy(dialog);
}

void expose_click(GtkWidget *btn_expose, gpointer user_data)
{
  // Create and show exposure parameters dialog
  struct acq_objects *objs = (struct acq_objects *)user_data;
  GtkWidget *dialog = expose_dialog_new(gtk_widget_get_toplevel(btn_expose), objs->cntrl);
  expose_dialog_set_image_type(dialog, objs->last_imgt);
  expose_dialog_set_win_start_x(dialog, 1);
  expose_dialog_set_win_start_y(dialog, 1);
  expose_dialog_set_win_width(dialog, ccd_cntrl_get_max_width(objs->cntrl));
  expose_dialog_set_win_height(dialog, ccd_cntrl_get_max_height(objs->cntrl));
  expose_dialog_set_prebin_x(dialog, 1);
  expose_dialog_set_prebin_y(dialog, 1);
  expose_dialog_set_exp_t(dialog, objs->last_exp_t);
  expose_dialog_set_repetitions(dialog, objs->last_repeat);
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(expose_response), user_data);
  gtk_widget_show_all(dialog);
}

void expose_response(GtkWidget *dialog, gint response_id, gpointer user_data)
{
  // User cancelled
  if (response_id != GTK_RESPONSE_OK)
  {
    gtk_widget_destroy(dialog);
    return;
  }
  struct acq_objects *objs = (struct acq_objects *)user_data;
  // This is actually just precautionary, the user should not be able to click the expose button when there is an exposure underway
  if (objs->mode != MODE_IDLE)
  {
    act_log_error(act_log_msg("User attempted to start a manual exposure, but system is currently busy (mode %hhu).", objs->mode));
    GtkWidget *err_dialog = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_toplevel(objs->box_main)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "User attempted to start a manual exposure, but system is currently busy (mode %hhu).", objs->mode);
    g_signal_connect_swapped (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), err_dialog);
    gtk_widget_show_all(err_dialog);
    gtk_widget_destroy(dialog);
    return;
  }
  prog_change_mode(objs, MODE_MANUAL_EXP);
  
  // Create CCD command structure and populate
  CcdCmd *cmd = CCD_CMD(g_object_new (ccd_cmd_get_type(), NULL));
  ccd_cmd_set_img_type(cmd, expose_dialog_get_image_type(dialog));
  ccd_cmd_set_win_start_x(cmd, expose_dialog_get_win_start_x(dialog));
  ccd_cmd_set_win_start_y(cmd, expose_dialog_get_win_start_y(dialog));
  ccd_cmd_set_win_width(cmd, expose_dialog_get_win_width(dialog));
  ccd_cmd_set_win_height(cmd, expose_dialog_get_win_height(dialog));
  ccd_cmd_set_prebin_x(cmd, expose_dialog_get_prebin_x(dialog));
  ccd_cmd_set_prebin_y(cmd, expose_dialog_get_prebin_y(dialog));
  ccd_cmd_set_exp_t(cmd, expose_dialog_get_exp_t(dialog));
  ccd_cmd_set_rpt(cmd, expose_dialog_get_repetitions(dialog));
  ccd_cmd_set_target(cmd, objs->cur_targ_id, objs->cur_targ_name);
  ccd_cmd_set_user(cmd, objs->cur_user_id, objs->cur_user_name);
  
  objs->last_imgt = expose_dialog_get_image_type(dialog);
  objs->last_exp_t = expose_dialog_get_exp_t(dialog);
  objs->last_repeat = expose_dialog_get_repetitions(dialog);
  
  gint ret = ccd_cntrl_start_exp(objs->cntrl, cmd);
  if (ret < 0)
  {
    prog_change_mode(objs, MODE_IDLE);
    act_log_error(act_log_msg("Error occurred while attempting to start a manual exposure - %s.", strerror(abs(ret))));
    GtkWidget *err_dialog = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_toplevel(objs->box_main)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Error occurred while attempting to start a manual exposure - %s.", strerror(abs(ret)));
    g_signal_connect_swapped (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), err_dialog);
    gtk_widget_show_all(err_dialog);
  }
  g_object_unref(G_OBJECT(cmd));
  
  gtk_widget_destroy(dialog);
}

void cancel_click(GtkWidget *btn_cancel, gpointer user_data)
{
  (void) btn_cancel;
  struct acq_objects *objs = (struct acq_objects *)user_data;
  ccd_cntrl_cancel_exp(objs->cntrl);
  prog_change_mode(objs, MODE_CANCEL);
}

gboolean imgdisp_mouse_move_view(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_equat)
{
  glong pixel_x = imgdisp_coord_pixel_x(imgdisp, motdata->x, motdata->y);
  glong pixel_y = imgdisp_coord_pixel_y(imgdisp, motdata->x, motdata->y);
  gchar str[256];
  sprintf(str, "X: %10lu\nY: %10lu", pixel_x, pixel_y);
  gtk_label_set_text(GTK_LABEL(lbl_mouse_equat), str);
  return FALSE;
}

gboolean imgdisp_mouse_move_equat(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_view)
{
  gfloat ra_h = imgdisp_coord_ra(imgdisp, motdata->x, motdata->y);
  gfloat dec_d = imgdisp_coord_dec(imgdisp, motdata->x, motdata->y);
  
  struct rastruct ra;
  convert_H_HMSMS_ra(ra_h, &ra);
  char *ra_str = ra_to_str(&ra);
  struct decstruct dec;
  convert_D_DMS_dec(dec_d, &dec);
  char *dec_str = dec_to_str(&dec);
  
  char str[256];
  sprintf(str, "RA: %10s\nDec: %10s", ra_str, dec_str);
  gtk_label_set_text(GTK_LABEL(lbl_mouse_view), str);
  free(ra_str);
  free(dec_str);
  
  return FALSE;
}

void ccd_stat_update(GObject *ccd_cntrl, guchar new_stat, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  gchar stat_str[100];
  if (ccd_cntrl_stat_err_retry(new_stat))
  {
    sprintf(stat_str, "RETRY");
    ccd_stat_err_retry(objs);
  }
  else if (ccd_cntrl_stat_err_no_recov(new_stat))
  {
    sprintf(stat_str, "ERR");
    ccd_stat_err_no_recov(objs);
  }
  else if (ccd_cntrl_stat_integrating(new_stat))
  {
    sprintf(stat_str, "%5.1f s / %lu", ccd_cntrl_get_integ_trem(CCD_CNTRL(ccd_cntrl)), ccd_cntrl_get_rpt_rem(CCD_CNTRL(ccd_cntrl)));
    gtk_label_set_text(GTK_LABEL(objs->lbl_exp_rem), stat_str);
    sprintf(stat_str, "INTEG");
  }
  else if (ccd_cntrl_stat_readout(new_stat))
    sprintf(stat_str, "READ");
  else 
  {
    if (objs->mode == MODE_CANCEL)
      prog_change_mode(objs, MODE_IDLE);
    sprintf(stat_str, "IDLE");
  }
  
  gtk_label_set_text(GTK_LABEL(objs->lbl_ccd_stat), stat_str);
  gtk_widget_set_sensitive(GTK_WIDGET(objs->btn_expose), objs->mode == MODE_IDLE);
}

void ccd_stat_err_retry(struct acq_objects *objs)
{
  switch (objs->mode)
  {
    case MODE_IDLE:
      // do nothing
      act_log_error(act_log_msg("CCD raised a recoverable error, but the system is currently idle. This should not have happened."));
      break;
    case MODE_MANUAL_EXP:
      // show retry error dialog
      act_log_error(act_log_msg("CCD raised a recoverable error during a manual integration."));
      GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_toplevel(objs->box_main)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "CCD raised a recoverable error during a manual integration.");
      g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
      gtk_widget_show_all(dialog);
      break;
    case MODE_TARGSET_EXP:
      // Send error message
      acq_net_send_targset_response(objs->net, OBSNSTAT_ERR_RETRY, 0.0, 0.0, FALSE);
      break;
    case MODE_DATACCD_EXP:
      // Send retry error message
      act_log_error(act_log_msg("CCD raised a recoverable error during an auto CCD data integration."));
      acq_net_send_dataccd_response(objs->net, OBSNSTAT_ERR_RETRY);
      break;
    case MODE_CANCEL:
      act_log_error(act_log_msg("CCD raised a recoverable error and the integration has been cancelled."));
      break;
    default:
      act_log_debug(act_log_msg("CCD raised recoverable error, but an invalid ACQ mode is in operation. Ignoring."));
  }
  prog_change_mode(objs, MODE_IDLE);
}

void ccd_stat_err_no_recov(struct acq_objects *objs)
{
  static gint reconnect_to = 0;
  switch (objs->mode)
  {
    case MODE_IDLE:
      // do nothing
      act_log_error(act_log_msg("CCD raised a fatal error, but the system is currently idle. This should not have happened."));
      break;
    case MODE_MANUAL_EXP:
      // show error dialog
      act_log_error(act_log_msg("CCD raised a fatal error during a manual integration."));
      GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_toplevel(objs->box_main)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "CCD raised a fatal error during a manual integration.");
      g_signal_connect_swapped (dialog, "response", G_CALLBACK (gtk_widget_destroy), dialog);
      gtk_widget_show_all(dialog);
      break;
    case MODE_TARGSET_EXP:
      // Send error message
      act_log_error(act_log_msg("CCD raised a fatal error during an auto target set integration."));
      acq_net_send_targset_response(objs->net, OBSNSTAT_ERR_WAIT, 0.0, 0.0, FALSE);
      break;
    case MODE_DATACCD_EXP:
      // Send error message
      act_log_error(act_log_msg("CCD raised a fatal error during an auto CCD data integration."));
      acq_net_send_dataccd_response(objs->net, OBSNSTAT_ERR_WAIT);
      break;
    case MODE_CANCEL:
      act_log_error(act_log_msg("CCD raised a fatal error and the integration has been cancelled."));
      break;
    default:
      act_log_debug(act_log_msg("CCD raised fatal error, but an invalid ACQ mode is in operation. Ignoring."));
  }
  
  // Try to disconnect from camera driver and reconnect
  if (ccd_cntrl_reconnect(objs->cntrl))
  {
    prog_change_mode(objs, MODE_IDLE);
    if (reconnect_to != 0)
    {
      g_source_remove(reconnect_to);
      reconnect_to = 0;
    }
    act_log_normal(act_log_msg("Successfully reconnected to CCD."));
  }
  else
  {
    prog_change_mode(objs, MODE_ERR_RESTART);
    if (reconnect_to == 0)
      reconnect_to = g_timeout_add(RECONNECT_TIMEOUT_PERIOD, reconnect_timeout, objs);
    act_log_normal(act_log_msg("Failed to reconnect to CCD. Will try again in %d seconds.", RECONNECT_TIMEOUT_PERIOD/1000));
  }
}

void ccd_new_image(GObject *ccd_cntrl, GObject *img, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  imgdisp_set_img(objs->imgdisp, CCD_IMG(img));
  
  gulong rpt_rem = ccd_cntrl_get_rpt_rem(CCD_CNTRL(ccd_cntrl));
  switch (objs->mode)
  {
    case MODE_IDLE:
      // This should not happen
      act_log_error(act_log_msg("New CCD image received, but ACQ system should be idle."));
      break;
    case MODE_MANUAL_EXP:
      // Check if there integration repetitions are complete, if so change mode to idle
      act_log_debug(act_log_msg("New manual image received."));
      if (rpt_rem == 0)
        prog_change_mode(objs, MODE_IDLE);
      break;
    case MODE_TARGSET_EXP:
      // Do pattern matching
      // Extract stars from image
      PointList *img_pts = image_extract_stars(CCD_IMG(img));
      if (point_list_get_num_used(img_pts) < MIN_NUM_STARS)
      {
        if (targset_exp_retry(objs->cntrl, CCD_IMG(img)) < 0)
        {
          act_log_error(act_log_msg("Error occurred while attempting to start an auto target set exposure - %s.", strerror(abs(ret))));
          acq_net_send_targset_response(objs->net, OBSNSTAT_ERR_RETRY, 0.0, 0.0, FALSE);
        }
        break;
      }
      
      // Fetch nearby stars in Tycho2 catalog from database
      gfloat img_ra, img_dec;
      ccd_img_get_tel_pos(CCD_IMG(img), &img_ra, &img_dec);
      glong img_start_sec, img_start_nanosec;
      ccd_img_get_start_datetime(CCD_IMG(img), &img_start_sec, &img_start_nanosec);
      PointList *pat_pts = acq_store_get_tycho_pattern(objs->store, img_ra, img_dec, SEC_TO_YEAR(img_start_sec), PAT_SEARCH_RAD);
      if (point_list_get_num_used(pat_pts) < MIN_NUM_STARS)
      {
        act_log_error(act_log_msg("Failed to fetch Tycho catalog stars."));
        break;
      }
      
      // TODO: Continue implementing here
      
      
      // Match the two lists of points
      FindPointMapping (img_points, num_stars, pat_points, num_pat, &map, &num_match);
      fprintf(stderr,"%d points matched.\n", num_match);
      if (num_match / (float)num_stars < MIN_MATCH_FRAC)
      {
        fprintf(stderr,"Too few points matched.\n");
        prog = 1;
        goto cleanup;
      }
      print_map("map", img_id, num_stars, map, img_points, pat_points);
      
      
      // Send response
//       acq_net_send_targset_response(objs->net, , ra_offs, dec_offs, FALSE);
      prog_change_mode(objs, MODE_IDLE);
      break;
    case MODE_DATACCD_EXP:
      // Check if there integration repetitions are complete, if so change mode to idle
      act_log_debug(act_log_msg("New DATA CCD image received."));
      if (rpt_rem == 0)
        prog_change_mode(objs, MODE_IDLE);
      break;
    default:
      act_log_debug(act_log_msg("New CCD iamge received, but an invalid ACQ mode is in operation. Ignoring."));
  }
  
  acq_store_append_image(objs->store, CCD_IMG(img));
  g_object_unref(G_OBJECT(img));
}

PointList *image_extract_stars(CcdImg *img)
{
  PointList *star_list = point_list_new_with_length(num_stars);
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
  
  sepobj *obj = NULL;
  int ret, num_stars;
  ret = sep_extract((void *)img_data, NULL, SEP_TFLOAT, SEP_TFLOAT, 0, ccd_img_get_win_width(img), ccd_img_get_win_height(img), mean+2.0*stddev, 5, conv, 3, 3, 32, 0.005, 1, 1.0, &obj, &num_stars);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to extract stars from image - SEP error code %d", ret));
    return star_list;
  }
  
  gfloat tmp_ra, tmp_dec;
  ccd_img_get_tel_pos(img, &tmp_ra, &tmp_dec);
  for (i=0; i<num_stars; i++)
  {
    static double x, y;
    x = (obj[i].x-XAPERTURE)*ccd_img_get_pixel_size_ra(img)/cos(convert_DEG_RAD(tmp_dec));
    y = (-obj[i].y+YAPERTURE)*ccd_img_get_pixel_size_dec(img);
    if (!point_list_append(star_list, x, y))
      act_log_debug(act_log_msg("Failed to add identified star %d to stars list."));
  }
  return star_list;
}

gint targset_exp_retry(CcdCntrl *cntrl, CcdImg *img)
{
  CcdCmd *cmd = CCD_CMD(g_object_new (ccd_cmd_get_type(), NULL));
  ccd_cmd_set_img_type(cmd, IMGT_ACQ_OBJ);
  ccd_cmd_set_win_start_x(cmd, ccd_img_get_win_start_x(CCD_IMG(img)));
  ccd_cmd_set_win_start_y(cmd, ccd_img_get_win_start_y(CCD_IMG(img)));
  ccd_cmd_set_win_width(cmd, ccd_img_get_win_width(CCD_IMG(img)));
  ccd_cmd_set_win_height(cmd, ccd_img_get_win_height(CCD_IMG(img)));
  ccd_cmd_set_prebin_x(cmd, ccd_img_get_prebin_x(CCD_IMG(img)));
  ccd_cmd_set_prebin_y(cmd, ccd_img_get_prebin_y(CCD_IMG(img)));
  ccd_cmd_set_exp_t(cmd, ccd_img_get_exp_t(CCD_IMG(img))*EXP_RETRY_FACT);
  ccd_cmd_set_rpt(cmd, 1);
  ccd_cmd_set_target(cmd, objs->cur_targ_id, objs->cur_targ_name);
  ccd_cmd_set_user(cmd, objs->cur_user_id, objs->cur_user_name);
  gint ret = ccd_cntrl_start_exp(objs->cntrl, cmd);
  g_object_unref(G_OBJECT(cmd));
  return ret;
}

gboolean reconnect_timeout(gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  gboolean ret = ccd_cntrl_reconnect(objs->cntrl);
  if (ret)
  {
    act_log_normal(act_log_msg("Successfully reconnected to CCD."));
    prog_change_mode(objs, MODE_IDLE);
  }
  else
    act_log_normal(act_log_msg("Failed to reconnect to CCD. Will try again in %d seconds.", RECONNECT_TIMEOUT_PERIOD/1000));
  return !ret;
}

void store_stat_update(GObject *acq_store, gpointer lbl_store_stat)
{
  AcqStore *store = ACQ_STORE(acq_store);
  gchar stat_str[100];
  if (acq_store_idle(store))
    sprintf(stat_str, "IDLE");
  else if (acq_store_storing(store))
    sprintf(stat_str, "BUSY");
  else if (acq_store_error_retry(store))
  {
    act_log_error(act_log_msg("An error occurred in the database storage system. Retrying."));
    sprintf(stat_str, "RETRY");
  }
  else if (acq_store_error_no_recov(store))
  {
    act_log_error(act_log_msg("An unrecoverable error occurred in the database storage system."));
    sprintf(stat_str, "ERROR");
  }
  else
  {
    act_log_debug(act_log_msg("Unknown error occurred on database storage system."));
    sprintf(stat_str, "UNKNWON");
  }
  gtk_label_set_text(GTK_LABEL(lbl_store_stat), stat_str);
}

void coord_received(GObject *acq_net, gdouble tel_ra, gdouble tel_dec, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  ccd_cntrl_set_tel_pos(objs->cntrl, tel_ra, tel_dec);
}

void guisock_received(GObject *acq_net, gulong win_id, gpointer box_main)
{
  act_log_debug(act_log_msg("Received GUI socket message."));
  if (gtk_widget_get_parent(GTK_WIDGET(box_main)) != NULL)
  {
    act_log_normal(act_log_msg("Strange: Received GUI socket message from act_control, but GUI components already embedded. Ignoring this message."));
    return;
  }
  GtkWidget *plg_new = gtk_plug_new(win_id);
  gtk_container_add(GTK_CONTAINER(plg_new), box_main);
  g_signal_connect(G_OBJECT(plg_new),"destroy",G_CALLBACK(destroy_gui_plug), box_main);
  gtk_widget_show_all(plg_new);
}

void destroy_gui_plug(GtkWidget *plug, gpointer box_main)
{
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(box_main));
}

void change_user(GObject *acq_net, gulong new_user_id, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->cur_user_id == new_user_id)
    return;
  objs->cur_user_id = new_user_id;
  if (objs->cur_user_name != NULL)
    g_free(objs->cur_user_name);
  objs->cur_user_name = acq_store_get_user_name(objs->store, new_user_id);
}

void change_target(GObject *acq_net, gulong new_targ_id, gchar *new_targ_name, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->cur_user_id == new_targ_id)
    return;
  objs->cur_targ_id = new_targ_id;
  if (objs->cur_targ_name != NULL)
    g_free(objs->cur_targ_name);
  objs->cur_targ_name = new_targ_name;
}

void targset_start(GObject *acq_net, gdouble targ_ra, gdouble targ_dec, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if ((objs->mode != MODE_IDLE) && (objs->mode != MODE_TARGSET_EXP))
  {
    act_log_error(act_log_msg("Auto target set message received, but the ACQ system is not idle (mode %hhu).", objs->mode));
    if (acq_net_send_targset_response(ACQ_NET(acq_net), OBSNSTAT_ERR_WAIT, 0.0, 0.0, FALSE) < 0)
      act_log_error(act_log_msg("Failed to send auto target set message response."));
    return;
  }
  CcdCmd *cmd = ccd_cmd_new(IMGT_ACQ_OBJ, 1, 1, ccd_cntrl_get_max_width(objs->cntrl), ccd_cntrl_get_max_height(objs->cntrl), 1, 1, 0.4, 1, objs->cur_targ_id, objs->cur_targ_name);
  gint ret = ccd_cntrl_start_exp(objs->cntrl, cmd);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Error occurred while attempting to start an auto target set exposure - %s.", strerror(abs(ret))));
    acq_net_send_targset_response(objs->net, OBSNSTAT_ERR_RETRY, 0.0, 0.0, FALSE);
    g_object_unref(G_OBJECT(cmd));
    return;
  }
  prog_change_mode(objs, MODE_TARGSET_EXP);
  g_object_unref(G_OBJECT(cmd));
}

void targset_stop(GObject *acq_net, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->mode != MODE_TARGSET_EXP)
  {
    act_log_error(act_log_msg("Auto target set cancel message received, but the ACQ system is not in auto target set mode (mode %hhu).", objs->mode));
    return;
  }
  ccd_cntrl_cancel_exp(objs->cntrl);
  prog_change_mode(objs, MODE_CANCEL);
}

void data_ccd_start(GObject *acq_net, gpointer ccd_cmd, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->mode != MODE_IDLE)
  {
    act_log_error(act_log_msg("Auto CCD data message received, but the ACQ system is not idle (mode %hhu).", objs->mode));
    if (acq_net_send_dataccd_response(ACQ_NET(acq_net), OBSNSTAT_ERR_WAIT) < 0)
      act_log_error(act_log_msg("Failed to send auto CCD data message response."));
    return;
  }
  gint ret = ccd_cntrl_start_exp(objs->cntrl, cmd);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Error occurred while attempting to start an auto CCD data exposure - %s.", strerror(abs(ret))));
    acq_net_send_dataccd_response(ACQ_NET(acq_net), OBSNSTAT_ERR_RETRY);
    g_object_unref(G_OBJECT(cmd));
    return;
  }
  prog_change_mode(objs, MODE_DATACCD_EXP);
  g_object_unref(G_OBJECT(cmd));
}

void data_ccd_stop(GObject *acq_net, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->mode != MODE_DATACCD_EXP)
  {
    act_log_error(act_log_msg("Auto CCD data cancel message received, but the ACQ system is not in auto CCD data mode (mode %hhu).", objs->mode));
    return;
  }
  ccd_cntrl_cancel_exp(objs->cntrl);
  prog_change_mode(objs, MODE_CANCEL);
}

void prog_change_mode(struct acq_objects *objs, guchar new_mode)
{
  if (objs->mode == new_mode)
    return;
  char stat_str[100];
  switch (new_mode)
  {
    case MODE_IDLE:
      sprintf(stat_str, "IDLE");
      break;
    case MODE_MANUAL_EXP:
      sprintf(stat_str, "MANUAL");
      break;
    case MODE_TARGSET_EXP:
      sprintf(stat_str, "AUTO ACQ");
      break;
    case MODE_DATACCD_EXP:
      sprintf(stat_str, "AUTO DATA");
      break;
    default:
      act_log_debug(act_log_msg("UNKNWON"));
  }
  gtk_label_set_text(GTK_LABEL(objs->lbl_prog_stat), stat_str);
}

