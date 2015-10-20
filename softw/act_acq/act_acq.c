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
#include "pattern_match.h"
#include "sep/sep.h"

#define TABLE_PADDING 3

#define GUICHECK_LOOP_PERIOD       5000
#define RECONNECT_TIMEOUT_PERIOD  30000

/// X centre of aperture on acquisition image in pixels
#define XAPERTURE 172
/// Y centre of aperture on acquisition image in pixels
#define YAPERTURE 110

/// Minimum number of stars for a positive field identification
/** \brief Definitions related to auto targset/pattern match exposures
 * Exposure time of first exposure is TARGSET_EXP_MIN_T. For each subsequent exposure, the exposure time is
 * multiplied by TARGSET_EXP_RETRY_FACT until either MIN_NUM_STARS stars are identified in the image (in which
 * case pattern matching is done) or the exposure time exceeds TARGSET_EXP_MAX_T (in which case a failure is 
 * reported). MIN_MATCH_FRAC specifies the minimum fraction of identified stars in the field that could be
 * mapped to stars in the Tycho/GSC-1.2 catalog for the map to be deemed a success. PAT_SEARCH_RADIUS sets the 
 * rectangular region about the telescope coordinates within which stars are extracted from the Tycho catalog 
 * for matching the star pattern against.
 * \{ */
#define TARGSET_EXP_MIN_T        0.4
#define TARGSET_EXP_MAX_T        5.0
#define TARGSET_EXP_RETRY_FACT   3
#define MIN_NUM_STARS            6
#define MIN_MATCH_FRAC           0.4
#define PAT_SEARCH_RADIUS        1.0
#define TARGSET_CENT_RADIUS      0.002777777777777778
/** \} */

/// Converts time in seconds since the UNIX epoch to fractional number of years (for coordinates epoch)
#define SEC_TO_YEAR(sec)   (1970 + sec/(float)31556926)

enum
{
  MODE_IDLE=1,
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
  GtkWidget *lbl_integ_rem;
  
  GtkWidget *lbl_store_stat;
  GtkWidget *btn_expose;
  GtkWidget *btn_cancel;
  
  gulong cur_targ_id;
  gchar *cur_targ_name;
  gulong cur_user_id;
  gchar *cur_user_name;
  
  guchar last_imgt;
  gfloat last_integ_t;
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
void ccd_integt_update(GObject *ccd_cntrl, gfloat integt_rem, gulong rpt_rem, gpointer lbl_integ_rem);
void ccd_stat_err_retry(struct acq_objects *objs);
void ccd_stat_err_no_recov(struct acq_objects *objs);
void ccd_new_image(GObject *ccd_cntrl, GObject *img, gpointer user_data);
void manual_pattern_match(struct acq_objects *objs, CcdImg *img);
void manual_pattern_match_msg(GtkWidget *parent, guint type, const char *msg);
void print_point_list(const char *heading, PointList *list);
void image_auto_target_set(struct acq_objects *objs, CcdImg *img);
PointList *image_extract_stars(CcdImg *img, GtkWidget *imgdisp);
guchar targset_integ_retry(CcdCntrl *cntrl, CcdImg *img);
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

int main(int argc, char** argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  
  const char *host, *port, *sqlhost;
  gtk_init(&argc, &argv);
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_str *sqlconfigarg = arg_str1("s", "sqlconfighost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
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
  sqlhost = sqlconfigarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
  
  CcdCntrl *cntrl = ccd_cntrl_new();
  if (cntrl == NULL)
  {
    act_log_error(act_log_msg("Failed to create CCD control object."));
    return 1;
  }
  
  AcqStore *store = acq_store_new(sqlhost);
  if (store == NULL)
  {
    act_log_error(act_log_msg("Failed to create database storage link."));
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
  acq_net_set_status(net, PROGSTAT_STARTUP);
  
  // Create GUI
  GtkWidget *box_main = gtk_vbox_new(FALSE, TABLE_PADDING);
  g_object_ref(box_main);
  GtkWidget *box_imgdisp = gtk_hbox_new(FALSE, TABLE_PADDING);
  gtk_box_pack_start(GTK_BOX(box_main), box_imgdisp, FALSE, FALSE, TABLE_PADDING);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_box_pack_start(GTK_BOX(box_imgdisp), imgdisp, TRUE, FALSE, TABLE_PADDING);
  GtkWidget* box_controls = gtk_table_new(3,3,TRUE);
  gtk_box_pack_start(GTK_BOX(box_main), box_controls, TRUE, TRUE, TABLE_PADDING);
  
  gtk_widget_set_size_request(imgdisp, ccd_cntrl_get_max_width(cntrl), ccd_cntrl_get_max_height(cntrl));
  imgdisp_set_window(imgdisp, 0, 0, ccd_cntrl_get_max_width(cntrl), ccd_cntrl_get_max_height(cntrl));
  imgdisp_set_flip_ew(imgdisp, TRUE);
  imgdisp_set_grid(imgdisp, IMGDISP_GRID_EQUAT, 1.0, 1.0);

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
  GtkWidget *lbl_integ_rem = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_integ_rem, 2,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *lbl_store_stat = gtk_label_new("Store OK");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_store_stat, 0,1,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_expose = gtk_button_new_with_label("Expose...");
  gtk_table_attach(GTK_TABLE(box_controls), btn_expose, 1,2,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
  gtk_table_attach(GTK_TABLE(box_controls), btn_cancel, 2,3,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  struct acq_objects objs = 
  {
    .mode = 0,
    .cntrl = cntrl,
    .store = store,
    .net = net,
    .box_main = box_main,
    .imgdisp = imgdisp,
    .lbl_prog_stat = lbl_prog_stat,
    .lbl_ccd_stat = lbl_ccd_stat,
    .lbl_integ_rem = lbl_integ_rem,
    .lbl_store_stat = lbl_store_stat,
    .btn_expose = btn_expose,
    .btn_cancel = btn_cancel,
    .cur_targ_id = 1,
    .cur_targ_name = malloc(8*sizeof(char)),
    .cur_user_id = 1,
    .cur_user_name = malloc(8*sizeof(char)),
    .last_imgt = IMGT_NONE,
    .last_integ_t = 1.0,
    .last_repeat = 1,
  };
  sprintf(objs.cur_targ_name, "ACT_ANY");
  sprintf(objs.cur_user_name, "ACT_ANY");
  prog_change_mode(&objs, MODE_IDLE);
  
  // Connect signals
  g_signal_connect (G_OBJECT(btn_view_param), "clicked", G_CALLBACK (view_param_click), imgdisp);
  g_signal_connect (G_OBJECT(btn_expose), "clicked", G_CALLBACK(expose_click), &objs);
  g_signal_connect (G_OBJECT(btn_cancel), "clicked", G_CALLBACK(cancel_click), &objs);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (imgdisp_mouse_move_view), lbl_mouse_view);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (imgdisp_mouse_move_equat), lbl_mouse_equat);
  g_signal_connect (G_OBJECT(cntrl), "ccd-stat-update", G_CALLBACK (ccd_stat_update), &objs);
  g_signal_connect (G_OBJECT(cntrl), "ccd-integ-rem", G_CALLBACK (ccd_integt_update), lbl_integ_rem);
  g_signal_connect (G_OBJECT(cntrl), "ccd-new-image", G_CALLBACK (ccd_new_image), &objs);
  g_signal_connect (G_OBJECT(store), "store-status-update", G_CALLBACK(store_stat_update), lbl_store_stat);
  g_signal_connect (G_OBJECT(net), "coord-received", G_CALLBACK(coord_received), &objs);
  g_signal_connect (G_OBJECT(net), "gui-socket", G_CALLBACK(guisock_received), box_main);
  g_signal_connect (G_OBJECT(net), "change-user", G_CALLBACK(change_user), &objs);
  g_signal_connect (G_OBJECT(net), "change-target", G_CALLBACK(change_target), &objs);
  g_signal_connect (G_OBJECT(net), "targset-start", G_CALLBACK(targset_start), &objs);
  g_signal_connect (G_OBJECT(net), "targset-stop", G_CALLBACK(targset_stop), &objs);
  g_signal_connect (G_OBJECT(net), "data-ccd-start", G_CALLBACK(data_ccd_start), &objs);
  g_signal_connect (G_OBJECT(net), "data-ccd-stop", G_CALLBACK(data_ccd_stop), &objs);
  gint guicheck_to_id = g_timeout_add(GUICHECK_LOOP_PERIOD, guicheck_timeout, &objs);
  ccd_cntrl_gen_test_image(cntrl);
  
  act_log_debug(act_log_msg("Entering main loop."));
  gtk_main();
  
  act_log_normal(act_log_msg("Exiting"));
  acq_net_set_status(net, PROGSTAT_STOPPING);
  g_source_remove(guicheck_to_id);
  g_object_unref(G_OBJECT(net));
  g_object_unref(G_OBJECT(store));
  g_object_unref(G_OBJECT(cntrl));
  return 0;
}

void acq_net_init(AcqNet *net, AcqStore *store, CcdCntrl *cntrl)
{
  acq_net_set_min_integ_t_s(net, ccd_cntrl_get_min_integ_t_sec(cntrl));
  acq_net_set_max_integ_t_s(net, ccd_cntrl_get_max_integ_t_sec(cntrl));
  gchar *ccd_id = ccd_cntrl_get_ccd_id(cntrl);
  acq_net_set_ccd_id(net, ccd_id);
  g_free(ccd_id);
  acq_filters_list_t ccd_filters;
  if (!acq_store_get_filt_list(store, &ccd_filters))
  {
    act_log_error(act_log_msg("Failed to get CCD filters list from database."));
    return;
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
  expose_dialog_set_exp_t(dialog, objs->last_integ_t);
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
  ccd_cmd_set_integ_t(cmd, expose_dialog_get_exp_t(dialog));
  ccd_cmd_set_rpt(cmd, expose_dialog_get_repetitions(dialog));
  ccd_cmd_set_target(cmd, objs->cur_targ_id, objs->cur_targ_name);
  ccd_cmd_set_user(cmd, objs->cur_user_id, objs->cur_user_name);
  
  objs->last_imgt = expose_dialog_get_image_type(dialog);
  objs->last_integ_t = expose_dialog_get_exp_t(dialog);
  objs->last_repeat = expose_dialog_get_repetitions(dialog);
  
  gint ret = ccd_cntrl_start_integ(objs->cntrl, cmd);
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
  ccd_cntrl_cancel_integ(objs->cntrl);
  guchar ccd_stat = ccd_cntrl_get_stat(objs->cntrl);
  if ((!ccd_cntrl_stat_readout(ccd_stat)) && (!ccd_cntrl_stat_integrating(ccd_stat)))
    prog_change_mode(objs, MODE_IDLE);
  else
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
  gfloat ra_h = imgdisp_coord_ra(imgdisp, motdata->x, motdata->y) / 15.0;
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
  (void) ccd_cntrl;
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
    sprintf(stat_str, "INTEG");
  else if (ccd_cntrl_stat_readout(new_stat))
    sprintf(stat_str, "READ");
  else 
  {
    if (objs->mode == MODE_CANCEL)
      prog_change_mode(objs, MODE_IDLE);
    sprintf(stat_str, "IDLE");
  }
  
  gtk_label_set_text(GTK_LABEL(objs->lbl_ccd_stat), stat_str);
}

void ccd_integt_update(GObject *ccd_cntrl, gfloat integt_rem, gulong rpt_rem, gpointer lbl_integ_rem)
{
  (void) ccd_cntrl;
  gchar stat_str[100];
  sprintf(stat_str, "%5.1f s / %lu", integt_rem, rpt_rem);
  gtk_label_set_text(GTK_LABEL(lbl_integ_rem), stat_str);
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
  g_object_ref(img);
  
  gulong rpt_rem = ccd_cntrl_get_rpt_rem(CCD_CNTRL(ccd_cntrl));
  switch (objs->mode)
  {
    case MODE_IDLE:
      // This should not happen
      act_log_error(act_log_msg("New CCD image received, but ACQ system should be idle."));
      return;
    case MODE_MANUAL_EXP:
      // Check if there integration repetitions are complete, if so change mode to idle
      act_log_debug(act_log_msg("New manual image received (%d exposures remain).", rpt_rem));
      if ((ccd_img_get_img_type(CCD_IMG(img)) == IMGT_ACQ_OBJ) || (ccd_img_get_img_type(CCD_IMG(img)) == IMGT_ACQ_SKY))
        manual_pattern_match(objs, CCD_IMG(img));
      if (rpt_rem == 0)
        prog_change_mode(objs, MODE_IDLE);
      break;
    case MODE_TARGSET_EXP:
      // Do pattern matching
      image_auto_target_set(objs, CCD_IMG(img));
      break;
    case MODE_DATACCD_EXP:
      // Check if there integration repetitions are complete, if so change mode to idle
      act_log_debug(act_log_msg("New DATA CCD image received."));
      if (rpt_rem == 0)
        prog_change_mode(objs, MODE_IDLE);
      break;
    case MODE_CANCEL:
      // Integration was cancelled, only update programme status, then exit (do not display image, do not save image)
      prog_change_mode(objs, MODE_IDLE);
      return;
    default:
      // Unknown mode. Ignore this image (do not display, do not save)
      act_log_debug(act_log_msg("New CCD iamge received, but an invalid ACQ mode is in operation. Ignoring."));
      return;
  }
  
  gtk_widget_set_size_request(objs->imgdisp, ccd_cntrl_get_max_width(objs->cntrl), ccd_cntrl_get_max_height(objs->cntrl));
  imgdisp_set_window(objs->imgdisp, 0, 0, ccd_img_get_img_width(CCD_IMG(img)), ccd_img_get_img_height(CCD_IMG(img)));
  imgdisp_set_img(objs->imgdisp, CCD_IMG(img));
  acq_store_append_image(objs->store, CCD_IMG(img));
  g_object_unref(G_OBJECT(img));
}

void manual_pattern_match(struct acq_objects *objs, CcdImg *img)
{
  char msg_str[256] = "No error message";
  // Extract stars from image
  PointList *img_pts = image_extract_stars(img, objs->imgdisp);
  gint num_stars = point_list_get_num_used(img_pts);
  act_log_debug(act_log_msg("Manual img - number of stars extracted from image: %d\n", num_stars));
  if (num_stars < MIN_NUM_STARS)
  {
    sprintf(msg_str, "Too few stars in image (%d must be %d)", num_stars, MIN_NUM_STARS);
    manual_pattern_match_msg(gtk_widget_get_toplevel(objs->box_main), GTK_MESSAGE_ERROR, msg_str);
    return;
  }
  print_point_list("Image points", img_pts);
  
  // Fetch nearby stars in GSC-1.2 catalog from database
  gfloat img_ra, img_dec;
  ccd_img_get_tel_pos(img, &img_ra, &img_dec);
  gdouble img_start_sec = ccd_img_get_start_datetime(img);
  PointList *pat_pts = acq_store_get_gsc1_pattern(objs->store, img_ra, img_dec, SEC_TO_YEAR(img_start_sec), PAT_SEARCH_RADIUS);
  if (pat_pts == NULL)
  {
    sprintf(msg_str, "Failed to fetch GSC catalog stars.");
    manual_pattern_match_msg(gtk_widget_get_toplevel(objs->box_main), GTK_MESSAGE_ERROR, msg_str);
    return;
  }
  gint num_pat = point_list_get_num_used(pat_pts);
  act_log_debug(act_log_msg("Manual img - number of catalog stars within search region: %d\n", num_pat));
  if (num_pat < MIN_NUM_STARS)
  {
    sprintf(msg_str, "Failed to fetch GSC catalog stars. (%d retrieved)", num_pat);
    manual_pattern_match_msg(gtk_widget_get_toplevel(objs->box_main), GTK_MESSAGE_ERROR, msg_str);
    return;
  }
  print_point_list("Pattern points", pat_pts);
  
  // Match the two lists of points
  GSList *map = find_point_list_map(img_pts, pat_pts, DEFAULT_RADIUS);
  gint num_match;
  if (map == NULL)
  {
    sprintf(msg_str, "Failed to find point mapping.");
    manual_pattern_match_msg(gtk_widget_get_toplevel(objs->box_main), GTK_MESSAGE_ERROR, msg_str);
    return;
  }
  num_match = g_slist_length(map);
  gfloat rashift, decshift;
  if (num_match / (float)num_stars < MIN_MATCH_FRAC)
  {
    sprintf(msg_str, "Too few stars mapped to pattern (%d mapped, %d required)", num_match, (int)(MIN_MATCH_FRAC*num_stars));
    manual_pattern_match_msg(gtk_widget_get_toplevel(objs->box_main), GTK_MESSAGE_ERROR, msg_str);
    return;
  }
  point_list_map_calc_offset(map, &rashift, &decshift, NULL, NULL);
  point_list_map_free(map);
  g_slist_free(map);
  
  sprintf(msg_str, "Shift: %f\"  %f\"\nTrue: %f d  %f d", rashift, decshift, img_ra+rashift/3600.0, img_dec+decshift/3600.0);
  manual_pattern_match_msg(gtk_widget_get_toplevel(objs->box_main), GTK_MESSAGE_INFO, msg_str);
  act_log_debug(act_log_msg("Pattern match result:  %12.6f %12.6f  %12.6f %12.6f  %6.2f\" %6.2f", img_ra, img_dec, img_ra+rashift/3600.0, img_dec+decshift/3600.0, rashift, decshift));
}

void manual_pattern_match_msg(GtkWidget *parent, guint type, const char *msg)
{
  if (type == GTK_MESSAGE_INFO)
    act_log_normal(act_log_msg("Successfully did manual pattern match - %s", msg));
  else
    act_log_error(act_log_msg("Failed to do manual pattern match - %s", msg));
  GtkWidget *msg_dialog = gtk_message_dialog_new (GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT, type, GTK_BUTTONS_CLOSE, "%s", msg);
  g_signal_connect_swapped (msg_dialog, "response", G_CALLBACK (gtk_widget_destroy), msg_dialog);
  gtk_widget_show_all(msg_dialog);  
}

void print_point_list(const char *heading, PointList *list)
{
  int i, num=point_list_get_num_used(list);
  gdouble x, y;
  act_log_debug(act_log_msg("%s", heading));
  for (i=0; i<num; i++)
  {
    if (!point_list_get_coord(list, i, &x, &y))
      continue;
    act_log_debug(act_log_msg("  %10.5f  %10.5f", x, y));
  }
}

void image_auto_target_set(struct acq_objects *objs, CcdImg *img)
{
  // Extract stars from image
  PointList *img_pts = image_extract_stars(img, objs->imgdisp);
  gint num_stars = point_list_get_num_used(img_pts);
  act_log_debug(act_log_msg("Number of stars extracted from image: %d\n", num_stars));
  if (num_stars < MIN_NUM_STARS)
  {
    guchar obsnstat = targset_integ_retry(objs->cntrl, img);
    if (obsnstat != OBSNSTAT_GOOD)
    {
      act_log_error(act_log_msg("Error occurred while attempting to start an auto target set exposure."));
      acq_net_send_targset_response(objs->net, obsnstat, 0.0, 0.0, FALSE);
      prog_change_mode(objs, MODE_IDLE);
    }
    return;
  }
  
  // Fetch nearby stars in Tycho2 catalog from database
  gfloat img_ra, img_dec;
  ccd_img_get_tel_pos(img, &img_ra, &img_dec);
  gdouble img_start_sec = ccd_img_get_start_datetime(img);
  PointList *pat_pts = acq_store_get_gsc1_pattern(objs->store, img_ra, img_dec, SEC_TO_YEAR(img_start_sec), PAT_SEARCH_RADIUS);
  gint num_pat = point_list_get_num_used(pat_pts);
  act_log_debug(act_log_msg("Number of catalog stars within search region: %d\n", num_pat));
  if (num_pat < MIN_NUM_STARS)
  {
    act_log_error(act_log_msg("Failed to fetch Tycho catalog stars."));
    acq_net_send_targset_response(objs->net, OBSNSTAT_ERR_RETRY, 0.0, 0.0, FALSE);
    prog_change_mode(objs, MODE_IDLE);
    return;
  }
  
  // Match the two lists of points
  GSList *map = find_point_list_map(img_pts, pat_pts, DEFAULT_RADIUS);
  gint num_match;
  if (map == NULL)
  {
    act_log_error(act_log_msg("Failed to find point mapping."));
    num_match = 0;
  }
  else
    num_match = g_slist_length(map);
  guchar obsnstat;
  gfloat rashift, decshift;
  if (num_match / (float)num_stars < MIN_MATCH_FRAC)
  {
    act_log_normal(act_log_msg("Too few stars mapped to pattern (%d mapped, %d required)", num_match, MIN_MATCH_FRAC*num_stars));
    rashift = decshift = 0.0;
    obsnstat = OBSNSTAT_ERR_NEXT;
  }
  else
  {
    point_list_map_calc_offset(map, &rashift, &decshift, NULL, NULL);
    obsnstat = OBSNSTAT_GOOD;
  }
  if (map != NULL)
  {
    point_list_map_free(map);
    g_slist_free(map);
    map = NULL;
  }
  
  // Send response
  if ((rashift < TARGSET_CENT_RADIUS) && (decshift < TARGSET_CENT_RADIUS))
    acq_net_send_targset_response(objs->net, obsnstat, rashift, decshift, TRUE);
  else
    acq_net_send_targset_response(objs->net, obsnstat, rashift, decshift, FALSE);
  prog_change_mode(objs, MODE_IDLE);
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
  ret = sep_extract((void *)img_data, NULL, SEP_TFLOAT, SEP_TFLOAT, 0, ccd_img_get_win_width(img), ccd_img_get_win_height(img), mean+2.0*stddev, 5, conv, 3, 3, 32, 0.005, 1, 1.0, &obj, &num_stars);
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
    act_log_debug(act_log_msg("    %6.2lf  %6.2lf      %10.5f  %10.5f", obj[i].x, obj[i].y, tmp_ra, tmp_dec));
    ret = point_list_append(star_list, tmp_ra, tmp_dec);
    if (!ret)
      act_log_debug(act_log_msg("Failed to add identified star %d to stars list."));
  }

  return star_list;
}

guchar targset_integ_retry(CcdCntrl *cntrl, CcdImg *img)
{
  CcdCmd *cmd = CCD_CMD(g_object_new (ccd_cmd_get_type(), NULL));
  gfloat integ_t_s = ccd_img_get_integ_t(img)*TARGSET_EXP_RETRY_FACT;
  if (integ_t_s > TARGSET_EXP_MAX_T)
  {
    act_log_debug(act_log_msg("Need to retry auto target set exposure with longer exposure time, but exposure time is already too long (%f s, max %f s). Rejecting this target.", integ_t_s, TARGSET_EXP_MAX_T));
    return OBSNSTAT_ERR_NEXT;
  }
  act_log_debug(act_log_msg("Retrying auto target set exposure with longer exposure time (%f s)"));
  ccd_cmd_set_img_type(cmd, IMGT_ACQ_OBJ);
  ccd_cmd_set_win_start_x(cmd, ccd_img_get_win_start_x(img));
  ccd_cmd_set_win_start_y(cmd, ccd_img_get_win_start_y(img));
  ccd_cmd_set_win_width(cmd, ccd_img_get_win_width(img));
  ccd_cmd_set_win_height(cmd, ccd_img_get_win_height(img));
  ccd_cmd_set_prebin_x(cmd, ccd_img_get_prebin_x(img));
  ccd_cmd_set_prebin_y(cmd, ccd_img_get_prebin_y(img));
  ccd_cmd_set_integ_t(cmd, integ_t_s);
  ccd_cmd_set_rpt(cmd, 1);
  ccd_cmd_set_user(cmd, ccd_img_get_user_id(img), ccd_img_get_user_name(img));
  ccd_cmd_set_target(cmd, ccd_img_get_targ_id(img), ccd_img_get_targ_name(img));
  gint ret = ccd_cntrl_start_integ(cntrl, cmd);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to start auto targset retry exposure."));
    g_object_unref(G_OBJECT(cmd));
    return OBSNSTAT_ERR_WAIT;
  }
  g_object_unref(G_OBJECT(cmd));
  return OBSNSTAT_GOOD;
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
//  act_log_debug(act_log_msg("Updated coordinates received: %f %f", tel_ra, tel_dec));
  (void) acq_net;
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
  acq_net_set_status(ACQ_NET(acq_net), PROGSTAT_RUNNING);
}

void destroy_gui_plug(GtkWidget *plug, gpointer box_main)
{
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(box_main));
}

void change_user(GObject *acq_net, gulong new_user_id, gpointer user_data)
{
  (void) acq_net;
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
  (void) acq_net;
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->cur_targ_id == new_targ_id)
    return;
  objs->cur_targ_id = new_targ_id;
  if (objs->cur_targ_name != NULL)
    g_free(objs->cur_targ_name);
  objs->cur_targ_name = new_targ_name;
}

void targset_start(GObject *acq_net, gdouble targ_ra, gdouble targ_dec, gpointer user_data)
{
  (void) targ_ra;
  (void) targ_dec;
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if ((objs->mode != MODE_IDLE) && (objs->mode != MODE_TARGSET_EXP))
  {
    act_log_error(act_log_msg("Auto target set message received, but the ACQ system is not idle (mode %hhu).", objs->mode));
    if (acq_net_send_targset_response(ACQ_NET(acq_net), OBSNSTAT_ERR_WAIT, 0.0, 0.0, FALSE) < 0)
      act_log_error(act_log_msg("Failed to send auto target set message response."));
    return;
  }
  CcdCmd *cmd = ccd_cmd_new(IMGT_ACQ_OBJ, 1, 1, ccd_cntrl_get_max_width(objs->cntrl), ccd_cntrl_get_max_height(objs->cntrl), 1, 1, 0.4, 1, objs->cur_targ_id, objs->cur_targ_name);
  gint ret = ccd_cntrl_start_integ(objs->cntrl, cmd);
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
  (void) acq_net;
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->mode != MODE_TARGSET_EXP)
  {
    act_log_error(act_log_msg("Auto target set cancel message received, but the ACQ system is not in auto target set mode (mode %hhu).", objs->mode));
    return;
  }
  ccd_cntrl_cancel_integ(objs->cntrl);
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
  gint ret = ccd_cntrl_start_integ(objs->cntrl, CCD_CMD(ccd_cmd));
  if (ret < 0)
  {
    act_log_error(act_log_msg("Error occurred while attempting to start an auto CCD data exposure - %s.", strerror(abs(ret))));
    acq_net_send_dataccd_response(ACQ_NET(acq_net), OBSNSTAT_ERR_RETRY);
    g_object_unref(G_OBJECT(ccd_cmd));
    return;
  }
  prog_change_mode(objs, MODE_DATACCD_EXP);
  g_object_unref(G_OBJECT(ccd_cmd));
}

void data_ccd_stop(GObject *acq_net, gpointer user_data)
{
  (void) acq_net;
  struct acq_objects *objs = (struct acq_objects *)user_data;
  if (objs->mode != MODE_DATACCD_EXP)
  {
    act_log_error(act_log_msg("Auto CCD data cancel message received, but the ACQ system is not in auto CCD data mode (mode %hhu).", objs->mode));
    return;
  }
  ccd_cntrl_cancel_integ(objs->cntrl);
  prog_change_mode(objs, MODE_CANCEL);
}

void prog_change_mode(struct acq_objects *objs, guchar new_mode)
{
  if (objs->mode == new_mode)
    return;
  char stat_str[100];
  gboolean idle = TRUE;
  switch (new_mode)
  {
    case MODE_IDLE:
      sprintf(stat_str, "IDLE");
      idle = TRUE;
      break;
    case MODE_MANUAL_EXP:
      sprintf(stat_str, "MANUAL");
      idle = FALSE;
      break;
    case MODE_TARGSET_EXP:
      sprintf(stat_str, "AUTO ACQ");
      idle = FALSE;
      break;
    case MODE_DATACCD_EXP:
      sprintf(stat_str, "AUTO DATA");
      idle = FALSE;
      break;
    case MODE_CANCEL:
      sprintf(stat_str, "CANCELLING");
      idle = FALSE;
      break;
    default:
      act_log_debug(act_log_msg("Unknown programme status %hhu", new_mode));
      return;
  }
  gtk_label_set_text(GTK_LABEL(objs->lbl_prog_stat), stat_str);
  gtk_widget_set_sensitive(objs->btn_expose, idle);
  gtk_widget_set_sensitive(objs->btn_cancel, !idle);
  objs->mode = new_mode;
}

