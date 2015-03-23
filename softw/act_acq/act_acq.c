#include <gtk/gtk.h>
#include "imgdisp.h"
#include "acq_net.h"
#include "acq_store.h"
#include "ccd_cntrl.h"
#include "ccd_img.h"

enum
{
  MODE_IDLE,
  MODE_MANUAL_EXP,
  MODE_TARGSET_EXP,
  MODE_DATACCD_EXP,
  MODE_ERR_RESTART
};

struct acq_objects
{
  gint mode;
  CcdCntrl *cntrl;
  AcqStore *store;
  AcqNet *net;
  
  GtkWidget *box_main;
  GtkWidget *imgdisp;
  
  GtkWidget *prog_stat;
  GtkWidget *lbl_ccd_stat;
  GtkWidget *lbl_exp_rem;
  
  GtkWidget *lbl_store_stat;
  GtkWidget *btn_expose;
  GtkWidget *btn_cancel;
  
  gulong cur_targ_id;
  gchar *cur_targ_name;
  gulong cur_user_id;
  gchar *cur_user_name;
  
  guchar last_imgt = IMGT_NONE;
  gfloat last_exp_t = 1.0;
  guint last_repeat = 1;
};



g_signal_connect (G_OBJECT(acq_net), "targset-start", G_CALLBACK(targset_start), &objs);
g_signal_connect (G_OBJECT(acq_net), "targset-stop", G_CALLBACK(targset-stop), &objs);
g_signal_connect (G_OBJECT(acq_net), "data-ccd-start", G_CALLBACK(data_ccd_start), &objs);
g_signal_connect (G_OBJECT(acq_net), "data-ccd-stop", G_CALLBACK(data_ccd_stop), &objs);


void change_target(GObject *acq_net, gulong new_target_id, gchar *new_targ_name, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->cur_user_id == new_user_id)
    return;
  objs->cur_targ_id = new_targ_id;
  if (objs->cur_targ_name != NULL)
    g_free(objs->cur_targ_name);
  objs->cur_targ_name = new_targ_name;
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

void coord_received(GObject *acq_net, gdouble tel_ra, gdouble tel_dec, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  ccd_cntrl_set_tel_pos(objs->cntrl, tel_ra_d, tel_dec_d);
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
    /// TODO: Show error dialog, indicating that system is busy
    act_log_error(act_log_debug("User attempted to start a manual exposure, but system is currently busy (mode %hhu).", objs->mode));
    GtkWidget *err_dialog = gtk_message_dialog_new (objs->box_main, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "User attempted to start a manual exposure, but system is currently busy (mode %hhu).", objs->mode);
    g_signal_connect_swapped (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), err_dialog);
    gtk_widget_show_all(err_dialog);
    gtk_widget_destroy(dialog);
    return;
  }
  objs->mode = MODE_MANUAL_EXP;
  
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
  
  gint ret = ccd_cntrl_start_exp(CcdCntrl *objs, CcdCmd *cmd);
  if (ret < 0)
  {
    objs->mode = MODE_IDLE;
    act_log_error(act_log_msg("Error occurred while attempting to start a manual exposure - %s.", strerror(abs(ret))));
    GtkWidget *err_dialog = gtk_message_dialog_new (objs->box_main, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Error occurred while attempting to start a manual exposure - %s.", strerror(abs(ret)));
    g_signal_connect_swapped (err_dialog, "response", G_CALLBACK (gtk_widget_destroy), err_dialog);
    gtk_widget_show_all(err_dialog);
  }
  
  gtk_widget_destroy(dialog);
}

void expose_click(GtkWidget btn_expose, gpointer user_data)
{
  // Create and show exposure parameters dialog
  struct acq_objects *objs = (struct acq_objects *)user_data;
  GtkWidget *dialog = expose_dialog_new(objs->box_main, objs->cntrl);
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

void cancel_click(GtkWiget *btn_cancel, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  ccd_cntrl_cancel_exp(objs->cntrl);
  objs->mode = MODE_IDLE;
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
    sprintf(stat_str, "%5.1f s / %lu", ccd_cntrl_get_integ_trem(objs->cntrl), ccd_cntrl_get_rpt_rem(objs->cntrl));
    gtk_label_set_text(GTK_LABEL(objs->lbl_exp_rem), stat_str);
    sprintf(stat_str, "INTEG");
  }
  else if (ccd_cntrl_stat_readout(new_stat))
    sprintf(stat_str, "READ");
  else if (objs->mode == MODE_IDLE)
    sprintf(stat_str, "IDLE");
  
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
      GtkWidget *dialog = gtk_message_dialog_new (objs->box_main, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "CCD raised a recoverable error during a manual integration.");
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
    default:
      act_log_debug(act_log_msg("CCD raised recoverable error, but an invalid ACQ mode is in operation. Ignoring."));
  }
  objs->mode = MODE_IDLE;
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
      GtkWidget *dialog = gtk_message_dialog_new (objs->box_main, GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "CCD raised a fatal error during a manual integration.");
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
    default:
      act_log_debug(act_log_msg("CCD raised recoverable error, but an invalid ACQ mode is in operation. Ignoring."));
  }
  
  // Try to disconnect from camera driver and reconnect
  if (ccd_cntrl_reconnect(objs))
  {
    objs->mode = MODE_IDLE;
    if (reconnect_to != 0)
    {
      g_source_remove(reconnect_to);
      reconnect_to = 0;
    }
    act_log_normal(act_log_msg("Successfully reconnected to CCD."));
  }
  else
  {
    objs->mode = MODE_ERR_RESTART;
    if (reconnect_to == 0)
      reconnect_to = g_timeout_add(RECONNECT_TIMEOUT_PERIOD, reconnect_timeout, objs);
    act_log_normal(act_log_msg("Failed to reconnect to CCD. Will try again in %d seconds.", RECONNECT_TIMEOUT_PERIOD/1000));
  }
}

void reconnect_timeout(gpointer user_data)
{
  gboolean ret = ccd_cntrl((struct acq_objects *)user_data);
  if (ret)
  {
    act_log_normal(act_log_msg("Successfully reconnected to CCD."));
    objs->mode = MODE_IDLE;
  }
  else
    act_log_normal(act_log_mgs("Failed to reconnect to CCD. Will try again in %d seconds.", RECONNECT_TIMEOUT_PERIOD/1000));
  return !ret;
}

void ccd_new_image(GObject *ccd_cntrl, GObject *img, gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  imgdisp_set_img(objs->imgdisp, CCD_IMG(img));
  
  // Check for auto target set, if so extract stars, calculate offset etc
  
  acq_store_append_image(objs->store, CCD_IMG(img));
  
  // If all integrations done (rpt_rem == 0), then change acq mode
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
  else if acq_store_error_no_recov(store))
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

void view_param_response(GtkWidget *view_param_dialog, gint response_id)
{
  if (response_id == GTK_RESPONSE_CANCEL)
    view_param_dialog_revert(dialog);
  else
    gtk_widget_destroy(dialog);
}

void view_param_click(GtkWidget *btn_view_param, gpointer imgdisp)
{
  GtkWidget *dialog = view_param_dialog_new(gtk_widget_get_toplevel(btn_view_param), GTK_WIDGET(imgdisp));
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(view_param_response), NULL);
  gtk_widget_show_all(dialog);
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
  gtk_label_set_text(GTK_LABEL(lbl_coord), str);
  free(ra_str);
  free(dec_str);
  
  return FALSE;
}

gboolean imgdisp_mouse_move_view(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_equat)
{
  glong pixel_x = imgdisp_coord_pixel_x(imgdisp, motdata->x, motdata->y);
  glong pixel_y = imgdisp_coord_pixel_y(imgdisp, motdata->x, motdata->y);
  gchar str[256];
  sprintf(str, "X: %10lu\nY: %10lu", pixel_x, pixel_y);
  gtk_label_set_text(GTK_LABEL(mouse_view), str);
  return FALSE;
}

gboolean guicheck_timeout(gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL;
  
  if (!main_embedded)
    request_guisock();
  return TRUE;
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
    exit(EXIT_FAILURE);
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
    g_object_unref(cntrl);
    return 1;
  }
  
  AcqNet *net = acq_net_new(host, port);
  if (net == NULL)
  {
    act_log_error(act_log_msg("Failed to establish network connection."));
    g_object_unref(cntrl);
    g_object_unref(store);
    return 1;
  }
  acq_net_init(net, cntrl);
  
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
  gtk_table_attach(GTK_TABLE(box_controls), lbl_auto, 0,1,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
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
  g_signal_connect (G_OBJECT(ccd_cntrl), "ccd-stat-update", G_CALLBACK (ccd_stat_update), &objs);
  g_signal_connect (G_OBJECT(ccd_cntrl), "ccd-new-image", G_CALLBACK (ccd_new_image), &objs);
  g_signal_connect (G_OBJECT(acq_store), "store-status-update", G_CALLBACK(store_stat_update), lbl_store_stat);
  g_signal_connect (G_OBJECT(acq_net), "coord-received", G_CALLBACK(coord_received), ccd_cntrl);
  g_signal_connect (G_OBJECT(acq_net), "gui-socket", G_CALLBACK(guisock_received), box_main);
  g_signal_connect (G_OBJECT(acq_net), "change-user", G_CALLBACK(change_user), &objs);
  g_signal_connect (G_OBJECT(acq_net), "change-target", G_CALLBACK(change_target), &objs);
  g_signal_connect (G_OBJECT(acq_net), "targset-start", G_CALLBACK(targset_start), &objs);
  g_signal_connect (G_OBJECT(acq_net), "targset-stop", G_CALLBACK(targset-stop), &objs);
  g_signal_connect (G_OBJECT(acq_net), "data-ccd-start", G_CALLBACK(data_ccd_start), &objs);
  g_signal_connect (G_OBJECT(acq_net), "data-ccd-stop", G_CALLBACK(data_ccd_stop), &objs);
  guicheck_to_id = g_timeout_add(GUICHECK_LOOP_PERIOD, guicheck_timeout, &objs);
  
  act_log_debug(act_log_msg("Entering main loop."));
  gtk_main();
  
  act_log_normal(act_log_msg("Exiting"));
  g_object_unref(net);
  g_object_unref(store);
  g_object_unref(cntrl);
  return 0;
}
