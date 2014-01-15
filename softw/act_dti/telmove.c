#include <gtk/gtk.h>
#include <stdio.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <errno.h>
#include <act_site.h>
#include <act_positastro.h>
#include <act_timecoord.h>
#include <act_ipc.h>
#include <act_log.h>
#include "telmove.h"
#include "dti_motor.h"
#include "telmove_coorddialog.h"
#include "dti_marshallers.h"

/** TODO:
 * - implement fail timeout
 * - incorporate more of duplicate code in goto and confirm coord in coorddialog
 */

#define TABLE_PADDING   5
#define SID_PER_MEAN_T  1.0027379056597505
#define SIDT_TIMEOUT_S  600

#define TELMOVE_FAIL_TIME_S   300
#define COORD_TIMEOUT_MS      300

#define TELHA_TOL_H     9.25925925925926e-05
#define TELDEC_TOL_D    0.001388888888888889

#define TELPARK_HA_H    0.0
#define TELPARK_DEC_D   -80.0
#define TELPARK_TOL_D   5.0

#define SOFT_LIM_ALT_MASK  0x10
#define HARD_LIM_MASK      0x0F

#define TRACK_ADJ_MIN_HA_H   5.555555555555556e-05
#define TRACK_ADJ_MIN_DEC_D  8.333333333333334e-04

static void telmove_class_init (TelmoveClass *klass);
static void telmove_init(GtkWidget *telmove);
static void telmove_destroy(gpointer telmove);
static void stat_update(gpointer telmove, guchar stat);
static void limits_update(gpointer telmove, guchar limits);
static void warn_update(gpointer telmove, guchar warn);
static void coord_update(gpointer telmove, GActTelcoord *new_coord);
static void goto_finish(gpointer telmove, gboolean success);
// static void update_statdisp(Telmove *objs);
// static void update_limitdisp(Telmove *objs);
static void check_button_sens(Telmove *objs);
static void update_coorddisp_nonsid(Telmove *objs, struct hastruct *ha, struct decstruct *dec);
static void update_coorddisp_sid(Telmove *objs, struct rastruct *ra, struct decstruct *dec);
static guchar process_quit(Telmove *objs, struct act_msg_quit *msg_quit);
static guchar process_time(Telmove *objs, struct act_msg_time *msg_time);
static guchar process_targcap(Telmove *objs, struct act_msg_targcap *msg_targcap);
static guchar process_targset(Telmove *objs, struct act_msg_targset *msg_targset);
static void process_complete(Telmove *objs, guchar status);
static guchar park_telescope(Telmove *objs);
static gboolean check_prequit_stop(gpointer telmove);
static gboolean start_moveN(GtkWidget *button, GdkEventButton *event, gpointer telmove);
static gboolean start_moveS(GtkWidget *button, GdkEventButton *event, gpointer telmove);
static gboolean start_moveE(GtkWidget *button, GdkEventButton *event, gpointer telmove);
static gboolean start_moveW(GtkWidget *button, GdkEventButton *event, gpointer telmove);
static gboolean end_moveNSEW(GtkWidget *button, GdkEventButton *event, gpointer telmove);
static guchar cardmove_get_speed(Telmove *objs);
static guchar start_goto_sid(Telmove *objs, struct rastruct *ra, struct decstruct *dec);
static guchar start_goto(Telmove *objs, struct hastruct *ha, struct decstruct *dec, gboolean is_sidereal);
static void user_emgny_stop(GtkWidget *btn_emgny_stop, gpointer telmove);
static void user_track(GtkWidget *btn_track, gpointer telmove);
static void user_tel_goto(GtkWidget *btn_goto, gpointer telmove);
static void tel_goto_response(GtkWidget *coorddialog, int response_id, gpointer telmove);
static void user_cancel_goto(gpointer telmove);
static void check_sidt(Telmove *objs);
static void error_dialog(GtkWidget *top_parent, const char *message, unsigned int errcode);


enum
{
  SEND_COORD_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

static guint telmove_signals[LAST_SIGNAL] = { 0 };

GType telmove_get_type (void)
{
  static GType telmove_type = 0;
  
  if (!telmove_type)
  {
    const GTypeInfo telmove_info =
    {
      sizeof (TelmoveClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) telmove_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Telmove),
      0,
      (GInstanceInitFunc) telmove_init,
      NULL
    };
    
    telmove_type = g_type_register_static (GTK_TYPE_FRAME, "Telmove", &telmove_info, 0);
  }
  
  return telmove_type;
}

GtkWidget *telmove_new (void)
{
  GtkWidget *telmove = g_object_new (telmove_get_type (), NULL);
  Telmove *objs = TELMOVE(telmove);
  objs->dti_motor = dti_motor_new();
  if (g_object_is_floating(G_OBJECT(objs->dti_motor)))
    g_object_ref_sink(G_OBJECT(objs->dti_motor));
  
  objs->motor_stat = dti_motor_get_stat(objs->dti_motor);
  objs->motor_limits = dti_motor_get_limits(objs->dti_motor);
  objs->motor_warn = dti_motor_get_warn(objs->dti_motor);
  stat_update(objs, objs->motor_stat);
  limits_update(objs, objs->motor_limits);
  warn_update(objs, objs->motor_warn);
  g_signal_connect_swapped(G_OBJECT(objs->dti_motor), "stat-update", G_CALLBACK(stat_update), objs);
  g_signal_connect_swapped(G_OBJECT(objs->dti_motor), "limits-update", G_CALLBACK(limits_update), objs);
  g_signal_connect_swapped(G_OBJECT(objs->dti_motor), "warn-update", G_CALLBACK(warn_update), objs);
  g_signal_connect_swapped(G_OBJECT(objs->dti_motor), "coord-update", G_CALLBACK(coord_update), objs);
  g_signal_connect_swapped(G_OBJECT(objs->dti_motor), "goto-finish", G_CALLBACK(goto_finish), objs);
  
  g_signal_connect_swapped(G_OBJECT(telmove), "destroy", G_CALLBACK(telmove_destroy), telmove);
  g_signal_connect(G_OBJECT(objs->btn_goto), "clicked", G_CALLBACK(user_tel_goto), telmove);
  g_signal_connect_swapped(G_OBJECT(objs->btn_cancel), "clicked", G_CALLBACK(user_cancel_goto), telmove);
  g_signal_connect(G_OBJECT(objs->btn_emgny_stop), "toggled", G_CALLBACK(user_emgny_stop), telmove);
  g_signal_connect(G_OBJECT(objs->btn_track), "toggled", G_CALLBACK(user_track), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveN), "button-press-event", G_CALLBACK(start_moveN), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveS), "button-press-event", G_CALLBACK(start_moveS), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveE), "button-press-event", G_CALLBACK(start_moveE), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveW), "button-press-event", G_CALLBACK(start_moveW), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveN), "button-release-event", G_CALLBACK(end_moveNSEW), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveS), "button-release-event", G_CALLBACK(end_moveNSEW), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveE), "button-release-event", G_CALLBACK(end_moveNSEW), telmove);
  g_signal_connect(G_OBJECT(objs->btn_moveW), "button-release-event", G_CALLBACK(end_moveNSEW), telmove);

  return telmove;
}

void telmove_process_msg (GtkWidget *telmove, DtiMsg *msg)
{
  gchar ret = OBSNSTAT_GOOD;
  Telmove *objs = TELMOVE(telmove);
  switch (dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_quit(objs, dti_msg_get_quit(msg));
      break;
    case MT_TIME:
      ret = process_time(objs, dti_msg_get_time(msg));
      break;
    case MT_TARG_CAP:
      ret = process_targcap(objs, dti_msg_get_targcap(msg));
      break;
    case MT_TARG_SET:
      ret = process_targset(objs, dti_msg_get_targset(msg));
      act_log_debug(act_log_msg("Processed target set: %hhd", ret));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(telmove, telmove_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

static void telmove_class_init (TelmoveClass *klass)
{
  telmove_signals[SEND_COORD_SIGNAL] = g_signal_new("send-coord", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
  telmove_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void telmove_init(GtkWidget *telmove)
{
  Telmove *objs = TELMOVE(telmove);
  gtk_frame_set_label(GTK_FRAME(telmove), "Move Telescope");
  objs->fail_to_id = 0;
  objs->dti_motor = NULL;
  objs->sidt_h = -1;
  objs->sidt_timer = g_timer_new();
  
  objs->reset_targ_coord = FALSE;
  memset(&objs->targ_ra, 0, sizeof(struct rastruct));
  memset(&objs->targ_dec, 0, sizeof(struct decstruct));

  objs->box = gtk_table_new(6,7,FALSE);
  gtk_container_add(GTK_CONTAINER(telmove), objs->box);
  
  objs->btn_goto = gtk_button_new_with_label("Go to");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_goto,0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_cancel = gtk_button_new_with_label("Cancel");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_cancel,0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_track = gtk_toggle_button_new_with_label("Track");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_track, 0,1,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GdkColor new_col;
  gdk_color_parse("#00AA00", &new_col);
  gtk_table_attach(GTK_TABLE(objs->box), gtk_vseparator_new(), 1,2,0,3, GTK_SHRINK, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_speed_slew = gtk_radio_button_new_with_label(NULL, "SLEW");
  gtk_widget_modify_bg (objs->btn_speed_slew, GTK_STATE_ACTIVE, &new_col);
  g_object_set(objs->btn_speed_slew,"draw-indicator",FALSE,NULL);
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_speed_slew,2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_speed_set = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(objs->btn_speed_slew),"SET");
  gtk_widget_modify_bg (objs->btn_speed_set, GTK_STATE_ACTIVE, &new_col);
  g_object_set(objs->btn_speed_set,"draw-indicator",FALSE,NULL);
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_speed_set,2,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_speed_guide = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(objs->btn_speed_slew),"GUIDE");
  gtk_widget_modify_bg (objs->btn_speed_guide, GTK_STATE_ACTIVE, &new_col);
  g_object_set(objs->btn_speed_guide,"draw-indicator",FALSE,NULL);
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_speed_guide,2,3,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(objs->box),gtk_vseparator_new(), 3,4, 0,3, GTK_SHRINK, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_moveN = gtk_button_new_with_label("N");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_moveN,5,6,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_moveS = gtk_button_new_with_label("S");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_moveS,5,6,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_moveE = gtk_button_new_with_label("E");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_moveE,6,7,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_moveW = gtk_button_new_with_label("W");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_moveW,4,5,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_emgny_stop = gtk_toggle_button_new();
  GtkWidget *img_tel_stop_icon = gtk_image_new_from_stock(GTK_STOCK_STOP, GTK_ICON_SIZE_MENU);
  gtk_container_add(GTK_CONTAINER(objs->btn_emgny_stop), img_tel_stop_icon);
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_emgny_stop,5,6,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);

  gtk_table_attach(GTK_TABLE(objs->box), gtk_hseparator_new(), 0,7,3,4, GTK_FILL|GTK_EXPAND, GTK_SHRINK, TABLE_PADDING, TABLE_PADDING);
  objs->lbl_hara_label= gtk_label_new("HA");
  gtk_table_attach(GTK_TABLE(objs->box), objs->lbl_hara_label, 0,1,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->lbl_dec_label = gtk_label_new("Dec");
  gtk_table_attach(GTK_TABLE(objs->box), objs->lbl_dec_label, 0,1,5,6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->lbl_hara = gtk_label_new("");
  gtk_label_set_width_chars(GTK_LABEL(objs->lbl_hara), 12);
  gtk_table_attach(GTK_TABLE(objs->box),objs->lbl_hara,1,3,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->lbl_dec = gtk_label_new("");
  gtk_label_set_width_chars(GTK_LABEL(objs->lbl_dec), 12);
  gtk_table_attach(GTK_TABLE(objs->box),objs->lbl_dec,1,3,5,6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box),gtk_vseparator_new(),3,4,4,6,GTK_SHRINK, GTK_FILL|GTK_EXPAND, TABLE_PADDING,TABLE_PADDING);
  objs->evb_stat = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(objs->box),objs->evb_stat,4,7,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->lbl_stat = gtk_label_new("");
  gtk_label_set_width_chars(GTK_LABEL(objs->lbl_stat), 20);
  gtk_container_add(GTK_CONTAINER(objs->evb_stat),objs->lbl_stat);
}

static void telmove_destroy(gpointer telmove)
{
  Telmove *objs = TELMOVE(telmove);
  if (objs->pending_msg != NULL)
    process_complete(objs,OBSNSTAT_CANCEL);
  if (objs->fail_to_id > 0)
  {
    g_source_remove(objs->fail_to_id);
    objs->fail_to_id = 0;
  }
  g_object_unref(objs->dti_motor);
  objs->dti_motor = NULL;
}

static void stat_update(gpointer telmove, guchar stat)
{
  Telmove *objs = TELMOVE(telmove);

  if ((dti_motor_stat_tracking(stat)) && (dti_motor_stat_moving(objs->motor_stat)) && (!dti_motor_stat_tracking(objs->motor_stat)))
    objs->reset_targ_coord = TRUE;
  
  if ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_track)) > 0) != (dti_motor_stat_tracking(stat)))
  {
    g_signal_handlers_block_by_func(G_OBJECT(objs->btn_track), G_CALLBACK(user_track), objs);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_track), dti_motor_stat_tracking(stat));
    g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_track), G_CALLBACK(user_track), objs);
  }
  if ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_emgny_stop)) > 0) != (dti_motor_stat_emgny_stop(stat) > 0))
  {
    g_signal_handlers_block_by_func(G_OBJECT(objs->btn_emgny_stop), G_CALLBACK(user_emgny_stop), objs);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_emgny_stop), dti_motor_stat_emgny_stop(stat));
    g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_emgny_stop), G_CALLBACK(user_emgny_stop), objs);
  }
  
  GdkColor new_col;
  
  if (dti_motor_stat_emgny_stop(stat))
  {
    gtk_label_set_text(GTK_LABEL(objs->lbl_stat), "EMRG. STOP");
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->evb_stat, GTK_STATE_NORMAL, &new_col);
    return;
  }
  if (dti_motor_limit_reached(stat))
  {
    gtk_label_set_text(GTK_LABEL(objs->lbl_stat), "HARD LIMIT");
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->evb_stat, GTK_STATE_NORMAL, &new_col);
    return;
  }
  if (!dti_motor_stat_init(stat))
  {
    gtk_label_set_text(GTK_LABEL(objs->lbl_stat), "NOT INIT'D");
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->evb_stat, GTK_STATE_NORMAL, &new_col);
    return;
  }
  
  char tmplabel[100];
  if (dti_motor_stat_moving(stat))
    sprintf(tmplabel, "MOVE");
  else if (dti_motor_stat_tracking(stat))
    sprintf(tmplabel, "TRACK");
  else
    sprintf(tmplabel, "IDLE");

  if (objs->sidt_h < 0)
  {
    gdk_color_parse("#AAAA00", &new_col);
    strcat(tmplabel, " (no SIDT)");
  }
  else
    gdk_color_parse("#00AA00", &new_col);
  gtk_widget_modify_bg(objs->evb_stat, GTK_STATE_NORMAL, &new_col);
  gtk_label_set_text(GTK_LABEL(objs->lbl_stat), tmplabel);

  objs->motor_stat = stat;
  check_button_sens(objs);
}

static void limits_update(gpointer telmove, guchar limits)
{
  Telmove *objs = TELMOVE(telmove);
  objs->motor_limits = limits;
  check_button_sens(objs);
}

static void warn_update(gpointer telmove, guchar warn)
{
  Telmove *objs = TELMOVE(telmove);
  GdkColor new_col;
  gdk_color_parse("#AAAA00", &new_col);
  if (dti_motor_warn_N (warn))
  {
    gtk_widget_modify_bg(objs->btn_moveN, GTK_STATE_NORMAL, &new_col);
    gtk_widget_modify_bg(objs->btn_moveN, GTK_STATE_ACTIVE, &new_col);
  }
  else
  {
    gtk_widget_modify_bg(objs->btn_moveN, GTK_STATE_NORMAL, NULL);
    gtk_widget_modify_bg(objs->btn_moveN, GTK_STATE_ACTIVE, NULL);
  }
  if (dti_motor_warn_S (warn))
  {
    gtk_widget_modify_bg(objs->btn_moveS, GTK_STATE_NORMAL, &new_col);
    gtk_widget_modify_bg(objs->btn_moveS, GTK_STATE_ACTIVE, &new_col);
  }
  else
  {
    gtk_widget_modify_bg(objs->btn_moveS, GTK_STATE_NORMAL, NULL);
    gtk_widget_modify_bg(objs->btn_moveS, GTK_STATE_ACTIVE, NULL);
  }
  if (dti_motor_warn_E (warn))
  {
    gtk_widget_modify_bg(objs->btn_moveE, GTK_STATE_NORMAL, &new_col);
    gtk_widget_modify_bg(objs->btn_moveE, GTK_STATE_ACTIVE, &new_col);
  }
  else
  {
    gtk_widget_modify_bg(objs->btn_moveE, GTK_STATE_NORMAL, NULL);
    gtk_widget_modify_bg(objs->btn_moveE, GTK_STATE_ACTIVE, NULL);
  }
  if (dti_motor_warn_W (warn))
  {
    gtk_widget_modify_bg(objs->btn_moveW, GTK_STATE_NORMAL, &new_col);
    gtk_widget_modify_bg(objs->btn_moveW, GTK_STATE_ACTIVE, &new_col);
  }
  else
  {
    gtk_widget_modify_bg(objs->btn_moveW, GTK_STATE_NORMAL, NULL);
    gtk_widget_modify_bg(objs->btn_moveW, GTK_STATE_ACTIVE, NULL);
  }
  objs->motor_warn = warn;
}

static void coord_update(gpointer telmove, GActTelcoord *new_coord)
{
  Telmove *objs = TELMOVE(telmove);
  if (g_object_is_floating(G_OBJECT(new_coord)))
    g_object_ref_sink(G_OBJECT(new_coord));
  GActTelcoord *raw_coord = gact_telcoord_new(&new_coord->ha, &new_coord->dec);
  if (g_object_is_floating(G_OBJECT(raw_coord)))
    g_object_ref_sink(G_OBJECT(raw_coord));
  dti_motor_apply_pointing(new_coord);
  GActTelcoord *undone_coord = gact_telcoord_new(&new_coord->ha, &new_coord->dec);
  if (g_object_is_floating(G_OBJECT(undone_coord)))
    g_object_ref_sink(G_OBJECT(undone_coord));
  dti_motor_apply_pointing_forward(undone_coord);
  act_log_debug(act_log_msg("New coordinates %f %f\t%f %f\t%f %f\t%f %f", convert_HMSMS_H_ha(&raw_coord->ha), convert_DMS_D_dec(&raw_coord->dec), convert_HMSMS_H_ha(&new_coord->ha), convert_DMS_D_dec(&new_coord->dec),
  convert_HMSMS_H_ha(&undone_coord->ha), convert_DMS_D_dec(&undone_coord->dec), convert_HMSMS_H_ha(&raw_coord->ha)-convert_HMSMS_H_ha(&undone_coord->ha), convert_DMS_D_dec(&raw_coord->dec)-convert_DMS_D_dec(&undone_coord->dec)));
  
  struct act_msg msg;
  memset(&msg, 0, sizeof(struct act_msg));
  msg.mtype = MT_COORD;
  struct act_msg_coord *msg_coord = &msg.content.msg_coord;
  memcpy(&msg_coord->ha, &new_coord->ha, sizeof(struct hastruct));
  memcpy(&msg_coord->dec, &new_coord->dec, sizeof(struct decstruct));
  g_object_unref(G_OBJECT(new_coord));
  convert_EQUI_ALTAZ (&msg_coord->ha, &msg_coord->dec, &msg_coord->alt, &msg_coord->azm);
  check_sidt(telmove);
  if (objs->sidt_h < 0.0)
  {
//     act_log_debug(act_log_msg("Sidereal time not available, not calculating telescope RA."));
    memset(&msg_coord->ra, 0, sizeof(struct rastruct));
  }
  else
  {
    struct timestruct sidt;
    gulong sidtdelta_us;
    gdouble sidtdelta_s = g_timer_elapsed(objs->sidt_timer, &sidtdelta_us);
    sidtdelta_s += (gdouble)sidtdelta_us/1e6;
    convert_H_HMSMS_time(objs->sidt_h + sidtdelta_s*SID_PER_MEAN_T/3600.0, &sidt);
    calc_RA(&msg_coord->ha, &sidt, &msg_coord->ra);
    if (objs->reset_targ_coord)
    {
      memcpy(&objs->targ_ra, &msg_coord->ra, sizeof(struct rastruct));
      memcpy(&objs->targ_dec, &msg_coord->dec, sizeof(struct decstruct));
      objs->reset_targ_coord = FALSE;
    }
  }
  if ((gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_track))) && (objs->sidt_h >= 0.0))
    update_coorddisp_sid(objs, &msg_coord->ra, &msg_coord->dec);
  else
    update_coorddisp_nonsid(objs, &msg_coord->ha, &msg_coord->dec);
  g_signal_emit(G_OBJECT(telmove), telmove_signals[SEND_COORD_SIGNAL], 0, dti_msg_new (&msg, 0));
  
  if ((dti_motor_stat_tracking(objs->motor_stat)) && (!dti_motor_stat_moving(objs->motor_stat)))
  {
    gdouble hadiff_h = convert_HMSMS_H_ra(&msg_coord->ra) - convert_HMSMS_H_ra(&objs->targ_ra);
    gdouble decdiff_d = convert_DMS_D_dec(&objs->targ_dec) - convert_DMS_D_dec(&msg_coord->dec);
    if ((fabs(hadiff_h) > TRACK_ADJ_MIN_HA_H) || (fabs(decdiff_d) > TRACK_ADJ_MIN_DEC_D))
    {
      act_log_debug(act_log_msg("Sending adjustment %f h, %f d.", hadiff_h, decdiff_d));
      dti_motor_track_adj(objs->dti_motor, hadiff_h, decdiff_d);
    }
  }
}

static void goto_finish(gpointer telmove, gboolean success)
{
  Telmove *objs = TELMOVE(telmove);
  if (objs->pending_msg == NULL)
  {
    act_log_debug(act_log_msg("Goto finished, but no auto goto underway. Ignoring"));
    return;
  }
  if (!success)
  {
    act_log_debug(act_log_msg("Goto finished, unsuccessful."));
    process_complete(objs, OBSNSTAT_ERR_NEXT);
    return;
  }
  // ??? Check coordinates?
  act_log_debug(act_log_msg("Goto finished, success!"));
  process_complete(objs, OBSNSTAT_GOOD);
}

static void check_button_sens(Telmove *objs)
{
  gboolean card_disable = FALSE, track_disable = dti_motor_lim_W(objs->motor_limits);
  if ((dti_motor_stat_emgny_stop(objs->motor_stat)) || (!dti_motor_stat_init(objs->motor_stat)))
  {
    gtk_widget_set_sensitive(objs->btn_goto, FALSE);
    gtk_widget_set_sensitive(objs->btn_cancel, FALSE);
    track_disable = TRUE;
    card_disable = TRUE;
  }
  else if (dti_motor_stat_goto(objs->motor_stat))
  {
    gtk_widget_set_sensitive(objs->btn_goto, FALSE);
    gtk_widget_set_sensitive(objs->btn_cancel, TRUE);
    card_disable = TRUE;
  }
  else if (dti_motor_stat_card(objs->motor_stat))
  {
    gtk_widget_set_sensitive(objs->btn_goto, FALSE);
    gtk_widget_set_sensitive(objs->btn_cancel, FALSE);
    card_disable = FALSE;
  }
  else
  {
    gtk_widget_set_sensitive(objs->btn_cancel, FALSE);
    gtk_widget_set_sensitive(objs->btn_goto, TRUE);
    card_disable = FALSE;
  }
  
  gtk_widget_set_sensitive(objs->btn_moveN, !(dti_motor_lim_N(objs->motor_limits) || card_disable));
  gtk_widget_set_sensitive(objs->btn_moveS, !(dti_motor_lim_S(objs->motor_limits) || card_disable));
  gtk_widget_set_sensitive(objs->btn_moveE, !(dti_motor_lim_E(objs->motor_limits) || card_disable));
  gtk_widget_set_sensitive(objs->btn_moveW, !(dti_motor_lim_W(objs->motor_limits) || card_disable));
  gtk_widget_set_sensitive(objs->btn_track, !track_disable);
}

static void update_coorddisp_nonsid(Telmove *objs, struct hastruct *ha, struct decstruct *dec)
{
  gtk_label_set_text(GTK_LABEL(objs->lbl_hara_label), "HA");
  char *tmp = ha_to_str(ha);
  gtk_label_set_text(GTK_LABEL(objs->lbl_hara), tmp);
  free(tmp);
  tmp = dec_to_str(dec);
  gtk_label_set_text(GTK_LABEL(objs->lbl_dec), tmp);
  free(tmp);
}

static void update_coorddisp_sid(Telmove *objs, struct rastruct *ra, struct decstruct *dec)
{
  gtk_label_set_text(GTK_LABEL(objs->lbl_hara_label), "RA");
  char *tmp = ra_to_str(ra);
  gtk_label_set_text(GTK_LABEL(objs->lbl_hara), tmp);
  free(tmp);
  tmp = dec_to_str(dec);
  gtk_label_set_text(GTK_LABEL(objs->lbl_dec), tmp);
  free(tmp);
}

static guchar process_quit(Telmove *objs, struct act_msg_quit *msg_quit)
{
  if (!msg_quit->mode_auto)
    return OBSNSTAT_GOOD;
  
  if (objs->pending_msg != NULL)
  {
    act_log_error(act_log_msg("A task is currently being processed. Cancelling in-progress task."));
    process_complete(objs, OBSNSTAT_CANCEL);
  }
  if ((dti_motor_stat_emgny_stop(objs->motor_stat)) || (!dti_motor_stat_init(objs->motor_stat)))
  {
    act_log_debug(act_log_msg("Cannot park telescope because all-stop is active or telescope has not been initialised."));
    return OBSNSTAT_CANCEL;
  }
  if (dti_motor_stat_moving(objs->motor_stat))
  {
    act_log_debug(act_log_msg("Motor is moving. Cancelling motion."));
    dti_motor_stop (objs->dti_motor);
    g_timeout_add_seconds(1, check_prequit_stop, objs);
    return 0;
  }
  return park_telescope(objs);
}

static guchar process_time(Telmove *objs, struct act_msg_time *msg_time)
{
  objs->sidt_h = convert_HMSMS_H_time(&msg_time->sidt);
  g_timer_start(objs->sidt_timer);
  return OBSNSTAT_GOOD;
}

static guchar process_targcap(Telmove *objs, struct act_msg_targcap *msg_targcap)
{
  dti_motor_set_soft_limits (objs->dti_motor, &msg_targcap->ha_lim_W, &msg_targcap->ha_lim_E, &msg_targcap->dec_lim_N, &msg_targcap->dec_lim_S, &msg_targcap->alt_lim);
  return OBSNSTAT_GOOD;
}

static guchar process_targset(Telmove *objs, struct act_msg_targset *msg_targset)
{
  if (!msg_targset->mode_auto)
    return OBSNSTAT_GOOD;
  if (msg_targset->status == OBSNSTAT_CANCEL)
  {
    dti_motor_stop (objs->dti_motor);
    process_complete(objs, 0);
    return OBSNSTAT_GOOD;
  }
  if (objs->pending_msg != NULL)
  {
    act_log_debug(act_log_msg("Busy processing a message, cannot process automatic target set."));
    return OBSNSTAT_ERR_WAIT;
  }
  if (!dti_motor_stat_init(objs->motor_stat))
  {
    act_log_crit(act_log_msg("Telescope has not been initialised."));
    return OBSNSTAT_ERR_CRIT;
  }
  if (msg_targset->targ_cent)
  {
    act_log_debug(act_log_msg("Target %s (%d, %s) centred.", msg_targset->targ_name, msg_targset->targ_id, msg_targset->sky ? "sky" : "object"));
    return OBSNSTAT_GOOD;
  }
  struct rastruct ra;
  struct decstruct dec;
  convert_H_HMSMS_ra(convert_HMSMS_H_ra(&msg_targset->targ_ra) + msg_targset->adj_ra_h, &ra);
  convert_D_DMS_dec(convert_DMS_D_dec(&msg_targset->targ_dec) + msg_targset->adj_dec_d, &dec);
  act_log_debug(act_log_msg("Initiating automatic target set: %s (%d) %s - %s %s adjustment %f %f (resultant %s %s)", msg_targset->targ_name, msg_targset->targ_id, msg_targset->sky ? "sky" : "object", ra_to_str(&msg_targset->targ_ra), dec_to_str(&msg_targset->targ_dec), msg_targset->adj_ra_h, msg_targset->adj_dec_d, ra_to_str(&ra), dec_to_str(&dec)));
  
  guchar ret = start_goto_sid(objs, &ra, &dec);
  act_log_debug(act_log_msg("Start goto result: %hhu", ret));
  
  return ret;
}

static void process_complete(Telmove *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Telmove process complete: status %hhu", status));
  g_signal_emit(G_OBJECT(objs), telmove_signals[PROC_COMPLETE_SIGNAL], 0, status, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static guchar park_telescope(Telmove *objs)
{
  if (dti_motor_stat_tracking(objs->motor_stat))
  {
    act_log_debug(act_log_msg("Parking telescope; disabling tracking."));
    gint ret = dti_motor_set_tracking (objs->dti_motor, FALSE);
    if (ret != 0)
    {
      act_log_error(act_log_msg("Failed to stop tracking - %s", strerror(ret)));
      return OBSNSTAT_CANCEL;
    }
  }
  
  struct hastruct park_ha;
  struct decstruct park_dec;
  convert_H_HMSMS_ha(TELPARK_HA_H, &park_ha);
  convert_D_DMS_dec(TELPARK_DEC_D, &park_dec);
  GActTelgoto *gotocmd = gact_telgoto_new (&park_ha, &park_dec, DTI_MOTOR_SPEED_SLEW, FALSE);
  if (g_object_is_floating(G_OBJECT(gotocmd)))
    g_object_ref_sink(G_OBJECT(gotocmd));
  gint ret = dti_motor_goto (objs->dti_motor, gotocmd);
  g_object_unref(G_OBJECT(gotocmd));
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to start moving telescope to park position - %s", strerror(ret)));
    return OBSNSTAT_CANCEL;
  }
  return 0;
}

static gboolean check_prequit_stop(gpointer telmove)
{
  act_log_debug(act_log_msg("Checking if telescope ready to be parked."));
  Telmove *objs = TELMOVE(telmove);
  if (dti_motor_stat_emgny_stop(objs->motor_stat))
  {
    act_log_debug(act_log_msg("Cannot park telescope because all-stop is active."));
    return FALSE;
  }
  if (dti_motor_stat_moving(objs->motor_stat))
    return TRUE;
  park_telescope(objs);
  return FALSE;
}

static gboolean start_moveN(GtkWidget *button, GdkEventButton *event, gpointer telmove)
{
  (void)button;
  if (event->button != 1) // If this isn't the left mouse button, ignore
    return FALSE;
  Telmove *objs = TELMOVE(telmove);
  guchar speed = cardmove_get_speed(objs);
  gint ret = dti_motor_move_card(objs->dti_motor, DTI_MOTOR_DIR_NORTH, speed);
  if (ret != 0)
    error_dialog(gtk_widget_get_toplevel(button), "Failed to start moving North", ret);
  return FALSE;
}

static gboolean start_moveS(GtkWidget *button, GdkEventButton *event, gpointer telmove)
{
  (void)button;
  if (event->button != 1) // If this isn't the left mouse button, ignore
    return FALSE;
  Telmove *objs = TELMOVE(telmove);
  guchar speed = cardmove_get_speed(objs);
  gint ret = dti_motor_move_card(objs->dti_motor, DTI_MOTOR_DIR_SOUTH, speed);
  if (ret != 0)
    error_dialog(gtk_widget_get_toplevel(button), "Failed to start moving South", ret);
  return FALSE;
}

static gboolean start_moveE(GtkWidget *button, GdkEventButton *event, gpointer telmove)
{
  (void)button;
  if (event->button != 1) // If this isn't the left mouse button, ignore
    return FALSE;
  Telmove *objs = TELMOVE(telmove);
  guchar speed = cardmove_get_speed(objs);
  gint ret = dti_motor_move_card(objs->dti_motor, DTI_MOTOR_DIR_EAST, speed);
  if (ret != 0)
    error_dialog(gtk_widget_get_toplevel(button), "Failed to start moving East", ret);
  return FALSE;
}

static gboolean start_moveW(GtkWidget *button, GdkEventButton *event, gpointer telmove)
{
  (void)button;
  if (event->button != 1) // If this isn't the left mouse button, ignore
    return FALSE;
  Telmove *objs = TELMOVE(telmove);
  guchar speed = cardmove_get_speed(objs);
  gint ret = dti_motor_move_card(objs->dti_motor, DTI_MOTOR_DIR_WEST, speed);
  if (ret != 0)
    error_dialog(gtk_widget_get_toplevel(button), "Failed to start moving West", ret);
  return FALSE;
}

static gboolean end_moveNSEW(GtkWidget *button, GdkEventButton *event, gpointer telmove)
{
  (void)button;
  if (event->button != 1) // If this isn't the left mouse button, ignore
    return FALSE;
  dti_motor_stop (TELMOVE(telmove)->dti_motor);
  return FALSE;
}

static guchar cardmove_get_speed(Telmove *objs)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_speed_slew)))
    return DTI_MOTOR_SPEED_SLEW;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_speed_set)))
    return DTI_MOTOR_SPEED_SET;
  return DTI_MOTOR_SPEED_GUIDE;
}

static guchar start_goto_sid(Telmove *objs, struct rastruct *ra, struct decstruct *dec)
{
  check_sidt(objs);
  struct timestruct tmp_sidt;
  struct hastruct tmp_ha;
  unsigned long sidtdelta_us;
  double sidtdelta_s = g_timer_elapsed(objs->sidt_timer, &sidtdelta_us);
  sidtdelta_s += (double)sidtdelta_us/1e6;
  convert_H_HMSMS_time(objs->sidt_h + sidtdelta_s*SID_PER_MEAN_T/3600.0, &tmp_sidt);
  calc_HAngle(ra, &tmp_sidt, &tmp_ha);
  guchar ret = start_goto(objs, &tmp_ha, dec, TRUE);
  act_log_debug(act_log_msg("Start goto result: %hhu", ret));
  return ret;
}

static guchar start_goto(Telmove *objs, struct hastruct *ha, struct decstruct *dec, gboolean is_sidereal)
{
  GActTelgoto *gotocmd = gact_telgoto_new (ha, dec, DTI_MOTOR_SPEED_SLEW, is_sidereal);
  if (g_object_is_floating(G_OBJECT(gotocmd)))
    g_object_ref_sink(G_OBJECT(gotocmd));
  gint ret = dti_motor_goto (objs->dti_motor, gotocmd);
  g_object_unref(gotocmd);
  if (ret == 0)
  {
    act_log_debug(act_log_msg("Successfully started goto."));
    return 0;
  }
  if (ret == EINVAL)
  {
    act_log_error(act_log_msg("Goto coordinates fall beyond software limits."));
    return OBSNSTAT_ERR_NEXT;
  }
  if (ret == EAGAIN)
  {
    act_log_error(act_log_msg("Motors currently unavailable."));
    return OBSNSTAT_ERR_WAIT;
  }
  return OBSNSTAT_ERR_RETRY;
}

static void user_emgny_stop(GtkWidget *btn_emgny_stop, gpointer telmove)
{
  Telmove *objs = TELMOVE(telmove);
  dti_motor_emgncy_stop (objs->dti_motor, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_emgny_stop)));
  process_complete(objs, OBSNSTAT_ERR_RETRY);
}

static void user_track(GtkWidget *btn_track, gpointer telmove)
{
  Telmove *objs = TELMOVE(telmove);
  gint ret = dti_motor_set_tracking (objs->dti_motor, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_track)));
  if (ret == 0)
    return;
  error_dialog(gtk_widget_get_toplevel(btn_track), "Failed to enable/disable tracking. Check that the telescope is initialised and that the all-stop (emergency stop) is disabled", ret);
}

static void user_tel_goto(GtkWidget *btn_goto, gpointer telmove)
{
  Telmove *objs = TELMOVE(telmove);
  gdouble sidt_h = -1.0;
  check_sidt(objs);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_track)) && (objs->sidt_h >= 0.0))
  {
    unsigned long sidtdelta_us;
    double sidtdelta_s = g_timer_elapsed(objs->sidt_timer, &sidtdelta_us);
    sidtdelta_s += (double)sidtdelta_us/1e6;
    sidt_h = objs->sidt_h + sidtdelta_s*SID_PER_MEAN_T/3600.0;
  }
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_track)))
    act_log_normal(act_log_msg("Telescope is tracking, but no sidereal time available. User will not be able to specify RA."));
  GActTelcoord *tmp_coord = dti_motor_get_coord (objs->dti_motor);
  if (g_object_is_floating(tmp_coord))
    g_object_ref_sink(tmp_coord);
  GtkWidget *coorddialog = telmove_coorddialog_new("Go To", gtk_widget_get_toplevel(btn_goto), sidt_h, tmp_coord);
  g_signal_connect(G_OBJECT(coorddialog), "response", G_CALLBACK(tel_goto_response), telmove);
  g_object_unref(tmp_coord);
  gtk_widget_show_all(coorddialog);
}

static void tel_goto_response(GtkWidget *coorddialog, int response_id, gpointer telmove)
{
  if (response_id != GTK_RESPONSE_OK)
  {
    gtk_widget_destroy(coorddialog);
    return;
  }
  Telmove *objs = TELMOVE(telmove);
  gboolean is_sidereal;
  GActTelcoord *tmp_coord;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_track)))
  {
    is_sidereal = TRUE;
    unsigned long sidtdelta_us;
    double sidtdelta_s = g_timer_elapsed(objs->sidt_timer, &sidtdelta_us);
    sidtdelta_s += (double)sidtdelta_us/1e6;
    gdouble sidt_h = objs->sidt_h + sidtdelta_s*SID_PER_MEAN_T/3600.0;
    tmp_coord = telmove_coorddialog_get_coord(coorddialog, sidt_h);
  }
  else
  {
    is_sidereal = FALSE;
    tmp_coord = telmove_coorddialog_get_coord(coorddialog, -1.0);
  }

  if (g_object_is_floating(G_OBJECT(tmp_coord)))
    g_object_ref_sink(G_OBJECT(tmp_coord));
  gtk_widget_destroy(coorddialog);

  GActTelgoto *tmp_cmd = gact_telgoto_new (&tmp_coord->ha, &tmp_coord->dec, DTI_MOTOR_SPEED_SLEW, is_sidereal);
  g_object_unref(tmp_coord);
  if (g_object_is_floating(tmp_cmd))
    g_object_ref_sink(tmp_cmd);

  gint ret = dti_motor_goto(objs->dti_motor, tmp_cmd);
  g_object_unref(tmp_cmd);
  if (ret == 0)
    return;
  if (ret == EINVAL)
  {
    act_log_error(act_log_msg("Invalid coordinates."));
    error_dialog(gtk_widget_get_toplevel(GTK_WIDGET(telmove)), "The specified coordinates fall beyond the telescope's safe operational range.", 0);
  }
  else
  {
    act_log_error(act_log_msg("Failed to initiate goto: %s", strerror(ret)));
    error_dialog(gtk_widget_get_toplevel(GTK_WIDGET(telmove)), "The specified coordinates fall beyond the telescope's safe operational range.", ret);
  }
}

static void user_cancel_goto(gpointer telmove)
{
  Telmove *objs = TELMOVE(telmove);
  dti_motor_stop(objs->dti_motor);
}

static void check_sidt(Telmove *objs)
{
  if ((g_timer_elapsed(objs->sidt_timer, NULL) > SIDT_TIMEOUT_S) && (objs->sidt_h >= 0.0))
    objs->sidt_h *= -1;
}

static void error_dialog(GtkWidget *top_parent, const char *message, unsigned int errcode)
{
  GtkWidget *err_dialog;
  if (errcode > 0)
    err_dialog = gtk_message_dialog_new (GTK_WINDOW(top_parent), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s.\n\nError was:\n%s", message, strerror(errcode));
  else
    err_dialog = gtk_message_dialog_new (GTK_WINDOW(top_parent), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, "%s.", message);
  g_signal_connect_swapped(G_OBJECT(err_dialog), "response", G_CALLBACK(gtk_widget_destroy), err_dialog);
  gtk_widget_show_all(err_dialog);
}
