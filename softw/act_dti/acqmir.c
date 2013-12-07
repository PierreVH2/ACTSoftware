#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "acqmir.h"
#include "dti_marshallers.h"
#include "dti_plc.h"

#define ACQMIR_FAIL_TIME_S  5

static void acqmir_class_init (AcqmirClass *klass);
static void acqmir_init(GtkWidget *acqmir);
static void acqmir_destroy(gpointer acqmir);
static void view_toggled(gpointer acqmir);
static void meas_toggled(gpointer acqmir);
static void acqmir_nand(GtkWidget *button1, gpointer button2);
static void set_buttons_meas(Acqmir *objs);
static void set_buttons_view(Acqmir *objs);
static void block_button_toggles(Acqmir *objs);
static void unblock_button_toggles(Acqmir *objs);
static guchar process_quit(Acqmir *objs, struct act_msg_quit *msg_quit);
static guchar process_datapmt(Acqmir *objs, struct act_msg_datapmt *msg_datapmt);
static guchar process_dataccd(Acqmir *objs, struct act_msg_dataccd *msg_dataccd);
static guchar process_targset(Acqmir *objs, struct act_msg_targset *msg_targset);
static guchar process_nonpmt(Acqmir *objs, gboolean mode_auto, guchar status);
static void process_complete(Acqmir *objs, guchar status);
static void send_stop(Acqmir *objs);
static void send_view(Acqmir *objs);
static void send_meas(Acqmir *objs);
static void send_acqmir(Acqmir *objs, guchar acqmir_goal, guint signum);
static gboolean fail_timeout(gpointer acqmir);

enum
{
  SEND_ACQMIR_VIEW_SIGNAL,
  SEND_ACQMIR_MEAS_SIGNAL,
  SEND_ACQMIR_STOP_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

static guint acqmir_signals[LAST_SIGNAL] = { 0 };

GType acqmir_get_type (void)
{
  static GType acqmir_type = 0;
  
  if (!acqmir_type)
  {
    const GTypeInfo acqmir_info =
    {
      sizeof (AcqmirClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) acqmir_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Acqmir),
      0,
      (GInstanceInitFunc) acqmir_init,
      NULL
    };
    
    acqmir_type = g_type_register_static (GTK_TYPE_FRAME, "Acqmir", &acqmir_info, 0);
  }
  
  return acqmir_type;
}

GtkWidget *acqmir_new (guchar cur_acqmir)
{
  GtkWidget *acqmir = g_object_new (acqmir_get_type (), NULL);
  Acqmir *objs = ACQMIR(acqmir);
  objs->acqmir_cur = ~cur_acqmir;
  objs->acqmir_goal = cur_acqmir & (ACQMIR_VIEW_MASK | ACQMIR_MEAS_MASK);
  acqmir_update (acqmir, cur_acqmir);
  
  g_signal_connect_swapped(G_OBJECT(acqmir), "destroy", G_CALLBACK(acqmir_destroy), acqmir);
  g_signal_connect_swapped(G_OBJECT(objs->btn_meas), "toggled", G_CALLBACK(meas_toggled), acqmir);
  g_signal_connect_swapped(G_OBJECT(objs->btn_view), "toggled", G_CALLBACK(view_toggled), acqmir);
  g_signal_connect(G_OBJECT(objs->btn_meas), "toggled", G_CALLBACK(acqmir_nand), objs->btn_view);
  g_signal_connect(G_OBJECT(objs->btn_view), "toggled", G_CALLBACK(acqmir_nand), objs->btn_meas);
  
  return acqmir;
}

void acqmir_update (GtkWidget *acqmir, guchar new_acqmir)
{
  Acqmir *objs = ACQMIR(acqmir);
  objs->acqmir_cur = new_acqmir & (ACQMIR_MEAS_MASK | ACQMIR_VIEW_MASK | ACQMIR_MOVING_MASK);
  GdkColor new_col;
  if ((new_acqmir & ACQMIR_MOVING_MASK) || (objs->acqmir_cur != objs->acqmir_goal))
  {
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->btn_view, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_meas, GTK_STATE_ACTIVE, &new_col);
    return;
  }
  if (((new_acqmir & ACQMIR_VIEW_MASK) > 0) && ((new_acqmir & ACQMIR_MEAS_MASK) > 0))
  {
    g_signal_emit(acqmir, acqmir_signals[SEND_ACQMIR_STOP_SIGNAL], 0);
    act_log_crit(act_log_msg("Acquisition mirror simultaneously in view and measure positions. Something has gone wrong."));
    gdk_color_parse("#AA0000", &new_col);
    gtk_widget_modify_bg(objs->btn_view, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_meas, GTK_STATE_ACTIVE, &new_col);
    process_complete(objs, OBSNSTAT_ERR_CRIT);
    return;
  }
  if (((new_acqmir & ACQMIR_VIEW_MASK) == 0) && ((new_acqmir & ACQMIR_MEAS_MASK) == 0))
  {
    gdk_color_parse("#AA0000", &new_col);
    act_log_error(act_log_msg("Acquisition mirror neither in view position nor in measure position and mirror not moving. Not sure what to do."));
    gtk_widget_modify_bg(objs->btn_view, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_meas, GTK_STATE_ACTIVE, &new_col);
    process_complete(objs, OBSNSTAT_ERR_RETRY);
    return;
  }
  if (new_acqmir & ACQMIR_VIEW_MASK)
  {
    act_log_debug(act_log_msg("Acquisition mirror in view position."));
    set_buttons_view(objs);
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_view, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_meas, GTK_STATE_ACTIVE, NULL);
  }
  else if (new_acqmir & ACQMIR_MEAS_MASK)
  {
    act_log_debug(act_log_msg("Acquisition mirror in view position."));
    set_buttons_meas(objs);
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_meas, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_view, GTK_STATE_ACTIVE, NULL);
  }
  if (objs->fail_to_id > 0)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = 0;
  if (objs->acqmir_goal == objs->acqmir_cur)
    process_complete(objs, OBSNSTAT_GOOD);
  else
    process_complete(objs, OBSNSTAT_ERR_RETRY);
}

void acqmir_process_msg (GtkWidget *acqmir, DtiMsg *msg)
{
  Acqmir *objs = ACQMIR(acqmir);
  guchar ret = OBSNSTAT_GOOD;
  switch(dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_quit(objs, dti_msg_get_quit (msg));
      break;
    case MT_TARG_SET:
      ret = process_targset(objs, dti_msg_get_targset (msg));
      break;
    case MT_DATA_PMT:
      ret = process_datapmt(objs, dti_msg_get_datapmt (msg));
      break;
    case MT_DATA_CCD:
      ret = process_dataccd(objs, dti_msg_get_dataccd (msg));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(acqmir, acqmir_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

static void acqmir_class_init (AcqmirClass *klass)
{
  acqmir_signals[SEND_ACQMIR_VIEW_SIGNAL] = g_signal_new("send-acqmir-view", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  acqmir_signals[SEND_ACQMIR_MEAS_SIGNAL] = g_signal_new("send-acqmir-meas", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  acqmir_signals[SEND_ACQMIR_STOP_SIGNAL] = g_signal_new("send-acqmir-stop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  acqmir_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void acqmir_init(GtkWidget *acqmir)
{
  Acqmir *objs = ACQMIR(acqmir);
  gtk_frame_set_label(GTK_FRAME(acqmir), "Acquisition Mirror");
  objs->pending_msg = NULL;
  objs->acqmir_cur = objs->acqmir_goal = 0;
  objs->fail_to_id = 0;
  objs->box = gtk_table_new(1,2,TRUE);
  gtk_container_add(GTK_CONTAINER(acqmir), objs->box);
  
  objs->btn_view = gtk_toggle_button_new_with_label("View");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_view,0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_meas = gtk_toggle_button_new_with_label("Measure");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_meas,1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
}

static void acqmir_destroy(gpointer acqmir)
{
  Acqmir *objs = ACQMIR(acqmir);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  process_complete(objs, OBSNSTAT_CANCEL);
}

static void view_toggled(gpointer acqmir)
{
  Acqmir *objs = ACQMIR(acqmir);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_view)))
    send_view(objs);
  else if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_meas)))
    send_stop(objs);
}

static void meas_toggled(gpointer acqmir)
{
  Acqmir *objs = ACQMIR(acqmir);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_meas)))
    send_meas(objs);
  else if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_view)))
    send_stop(objs);
}

static void acqmir_nand(GtkWidget *button1, gpointer button2)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button1)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button2), FALSE);
}

static void set_buttons_meas(Acqmir *objs)
{
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_view), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_meas), TRUE);
  unblock_button_toggles(objs);
}

static void set_buttons_view(Acqmir *objs)
{
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_view), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_meas), FALSE);
  unblock_button_toggles(objs);
}

static void block_button_toggles(Acqmir *objs)
{
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_meas), G_CALLBACK(meas_toggled), objs);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_view), G_CALLBACK(acqmir_nand), objs->btn_meas);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_meas), G_CALLBACK(acqmir_nand), objs->btn_view);
}

static void unblock_button_toggles(Acqmir *objs)
{
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_meas), G_CALLBACK(meas_toggled), objs);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_view), G_CALLBACK(acqmir_nand), objs->btn_meas);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_meas), G_CALLBACK(acqmir_nand), objs->btn_view);
}

static guchar process_quit(Acqmir *objs, struct act_msg_quit *msg_quit)
{
  process_complete(objs, OBSNSTAT_CANCEL);
  if (!msg_quit->mode_auto)
    return OBSNSTAT_GOOD;
  set_buttons_view(objs);
  send_view(objs); 
  return 0;
}

static guchar process_datapmt(Acqmir *objs, struct act_msg_datapmt *msg_datapmt)
{
  if (msg_datapmt->status == OBSNSTAT_ERR_CRIT)
    return process_nonpmt(objs, TRUE, OBSNSTAT_ERR_CRIT);
  if (!(msg_datapmt->mode_auto))
    return OBSNSTAT_GOOD;
  if (objs->pending_msg != NULL)
    return OBSNSTAT_ERR_WAIT;
  if (msg_datapmt->status != OBSNSTAT_GOOD)
    return OBSNSTAT_GOOD;
  if ((objs->acqmir_cur & ACQMIR_MEAS_MASK) > 0)
    return OBSNSTAT_GOOD;
  set_buttons_meas(objs);
  send_meas(objs);
  return 0;
}

static guchar process_dataccd(Acqmir *objs, struct act_msg_dataccd *msg_dataccd)
{
  return process_nonpmt(objs, msg_dataccd->mode_auto > 0, (guchar)msg_dataccd->status);
}

static guchar process_targset(Acqmir *objs, struct act_msg_targset *msg_targset)
{
  return process_nonpmt(objs, msg_targset->mode_auto > 0, (guchar)msg_targset->status);
}

static guchar process_nonpmt(Acqmir *objs, gboolean mode_auto, guchar status)
{
  if (!(mode_auto) && (status != OBSNSTAT_ERR_CRIT))
    return OBSNSTAT_GOOD;
  if (objs->pending_msg != NULL)
    process_complete(objs, OBSNSTAT_CANCEL);
  if ((objs->acqmir_cur & ACQMIR_VIEW_MASK) > 0)
    return OBSNSTAT_GOOD;
  set_buttons_view(objs);
  send_view(objs);
  return 0;
}

static void process_complete(Acqmir *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Acquisition mirror process complete, status - %hhu %hhu %hhd", status, objs->acqmir_cur, objs->acqmir_goal));
  guchar ret_stat = status;
  g_signal_emit(G_OBJECT(objs), acqmir_signals[PROC_COMPLETE_SIGNAL], 0, ret_stat, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static void send_stop(Acqmir *objs)
{
  act_log_debug(act_log_msg("Emitting stop."));
  send_acqmir(objs, 0, acqmir_signals[SEND_ACQMIR_STOP_SIGNAL]);
}

static void send_view(Acqmir *objs)
{
  act_log_debug(act_log_msg("Emitting send view."));
  send_acqmir(objs, ACQMIR_VIEW_MASK, acqmir_signals[SEND_ACQMIR_VIEW_SIGNAL]);
}

static void send_meas(Acqmir *objs)
{
  act_log_debug(act_log_msg("Emitting send meas."));
  send_acqmir(objs, ACQMIR_MEAS_MASK, acqmir_signals[SEND_ACQMIR_MEAS_SIGNAL]);
}

static void send_acqmir(Acqmir *objs, guchar acqmir_goal, guint signum)
{
  g_signal_emit(G_OBJECT(objs), signum, 0);
  if (objs->fail_to_id > 0)
  {
    act_log_debug(act_log_msg("Cancelling fail timeout."));
    g_source_remove(objs->fail_to_id);
    objs->fail_to_id = 0;
  }
  if (acqmir_goal == 0)
    objs->acqmir_goal = objs->acqmir_cur;
  else
    objs->acqmir_goal = acqmir_goal;
  if (objs->acqmir_goal != objs->acqmir_cur)
  {
    act_log_debug(act_log_msg("Starting fail timeout (%hhu %hhu).", objs->acqmir_goal, objs->acqmir_cur));
    objs->fail_to_id = g_timeout_add_seconds(ACQMIR_FAIL_TIME_S, fail_timeout, G_OBJECT(objs));
  }
}

static gboolean fail_timeout(gpointer acqmir)
{
  Acqmir *objs = ACQMIR(acqmir);
  act_log_crit(act_log_msg("Timed out while waiting for acquisition mirror to move to required position (%hhd)", objs->acqmir_goal));
  process_complete(objs, OBSNSTAT_ERR_CRIT);
  objs->fail_to_id = 0;
  GdkColor new_col;
  gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->btn_view, GTK_STATE_ACTIVE, &new_col);
  gtk_widget_modify_bg(objs->btn_meas, GTK_STATE_ACTIVE, &new_col);
  send_stop(objs);
  return FALSE;
}

