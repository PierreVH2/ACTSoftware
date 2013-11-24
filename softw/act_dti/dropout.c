#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "dti_marshallers.h"
#include "dropout.h"

#define DROPOUT_FAIL_TIME_S   300
#define DROPOUT_ENV_TIME_S    600

static void dropout_class_init (DropoutClass *klass);
static void dropout_init(GtkWidget *dropout);
static void dropout_destroy(gpointer dropout);
static void buttons_toggled(gpointer dropout);
static void buttons_nand(GtkWidget *button1, gpointer button2);
static void block_button_toggles(Dropout *objs);
static void unblock_button_toggles(Dropout *objs);
static guchar process_environ(Dropout *objs, struct act_msg_environ *msg_environ);
static guchar process_targset(Dropout *objs, struct act_msg_targset *msg_targset);
static guchar process_quit(Dropout *objs, struct act_msg_quit *msg_quit);
static gboolean fail_timeout(gpointer dropout);
static gboolean env_timeout(gpointer dropout);
static guchar environ_change(Dropout *objs, gboolean weath_ok, gboolean sun_alt_ok);
static void process_complete(Dropout *objs, guchar status);
static void send_start_open(Dropout *objs);
static void send_start_close(Dropout *objs);
static void send_stop(Dropout *objs);

enum
{
  IS_CLOSED_SIGNAL,
  SEND_START_OPEN_SIGNAL,
  SEND_START_CLOSE_SIGNAL,
  SEND_STOP_MOVE_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

static guint dropout_signals[LAST_SIGNAL] = { 0 };

GType dropout_get_type (void)
{
  static GType dropout_type = 0;
  
  if (!dropout_type)
  {
    const GTypeInfo dropout_info =
    {
      sizeof (DropoutClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) dropout_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Dropout),
      0,
      (GInstanceInitFunc) dropout_init,
      NULL
    };
    
    dropout_type = g_type_register_static (GTK_TYPE_FRAME, "Dropout", &dropout_info, 0);
  }
  
  return dropout_type;
}

GtkWidget *dropout_new (guchar dropout_stat)
{
  GtkWidget *dropout = g_object_new (dropout_get_type (), NULL);
  Dropout *objs = DROPOUT(dropout);
  objs->weath_ok = objs->sun_alt_ok = FALSE;
  objs->dropout_goal = dropout_stat;
  objs->dropout_cur = dropout_stat;
  dropout_update (dropout, dropout_stat);
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)))
    gtk_widget_set_sensitive(objs->btn_open, FALSE);
  
  g_signal_connect_swapped(G_OBJECT(dropout), "destroy", G_CALLBACK(dropout_destroy), dropout);
  g_signal_connect(G_OBJECT(objs->btn_open), "toggled", G_CALLBACK(buttons_nand), objs->btn_close);
  g_signal_connect(G_OBJECT(objs->btn_close), "toggled", G_CALLBACK(buttons_nand), objs->btn_open);
  g_signal_connect_swapped(G_OBJECT(objs->btn_open), "toggled", G_CALLBACK(buttons_toggled), dropout);
  g_signal_connect_swapped(G_OBJECT(objs->btn_close), "toggled", G_CALLBACK(buttons_toggled), dropout);
  
  return dropout;
}

void dropout_update (GtkWidget *dropout, guchar new_dropout_stat)
{
  Dropout *objs = DROPOUT(dropout);
  GdkColor new_col;
  block_button_toggles(objs);
  if (new_dropout_stat & DSHUTT_MOVING_MASK)
  {
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, &new_col);
  }
  else if (new_dropout_stat & DSHUTT_OPEN_MASK)
  {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), FALSE);
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, NULL);
  }
  else if (new_dropout_stat & DSHUTT_CLOSED_MASK)
  {
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, &new_col);
  }
  else
  {
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_INSENSITIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_INSENSITIVE, NULL);
  }

  if (((new_dropout_stat & DSHUTT_CLOSED_MASK) != 0) && ((objs->dropout_cur & DSHUTT_CLOSED_MASK) == 0))
    g_signal_emit(dropout, dropout_signals[IS_CLOSED_SIGNAL], 0, TRUE);
  else if (((new_dropout_stat & DSHUTT_CLOSED_MASK) == 0) && ((objs->dropout_cur & DSHUTT_CLOSED_MASK) != 0))
    g_signal_emit(dropout, dropout_signals[IS_CLOSED_SIGNAL], 0, FALSE);
  
  if (new_dropout_stat == objs->dropout_goal)
  {
    if (objs->fail_to_id > 0)
    {
      g_source_remove(objs->fail_to_id);
      objs->fail_to_id = 0;
    }
    process_complete(objs, OBSNSTAT_GOOD);
  }
  objs->dropout_cur = new_dropout_stat;
  
  unblock_button_toggles(objs);
}

void dropout_process_msg(GtkWidget *dropout, DtiMsg *msg)
{
  guchar ret = OBSNSTAT_GOOD;
  Dropout *objs = DROPOUT(dropout);
  switch(dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_quit(objs, dti_msg_get_quit(msg));
      break;
    case MT_ENVIRON:
      ret = process_environ(objs, dti_msg_get_environ(msg));
      break;
    case MT_TARG_SET:
      ret = process_targset(objs, dti_msg_get_targset(msg));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(dropout, dropout_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

void dropout_set_lock(GtkWidget *dropout, gboolean lock_on)
{
  Dropout *objs = DROPOUT(dropout);
  objs->locked = lock_on;
  gtk_widget_set_sensitive(objs->btn_open, lock_on && objs->weath_ok && objs->sun_alt_ok);
  gtk_widget_set_sensitive(objs->btn_close, lock_on);
}

static void dropout_class_init (DropoutClass *klass)
{
  dropout_signals[IS_CLOSED_SIGNAL] = g_signal_new("closed", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dropout_signals[SEND_START_OPEN_SIGNAL] = g_signal_new("start-open", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  dropout_signals[SEND_START_CLOSE_SIGNAL] = g_signal_new("start-close", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  dropout_signals[SEND_STOP_MOVE_SIGNAL] = g_signal_new("stop-move", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  dropout_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void dropout_init(GtkWidget *dropout)
{
  Dropout *objs = DROPOUT(dropout);
  gtk_frame_set_label(GTK_FRAME(dropout), "Dome Dropout");
  objs->dropout_cur = objs->dropout_goal = objs->fail_to_id = objs->env_to_id = 0;
  objs->weath_ok = objs->sun_alt_ok = objs->locked = FALSE;
  objs->pending_msg = FALSE;
  objs->box = gtk_table_new(1,2,TRUE);
  gtk_container_add(GTK_CONTAINER(dropout), objs->box);
  
  objs->btn_open = gtk_toggle_button_new_with_label("Open");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_open, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_close = gtk_toggle_button_new_with_label("Close");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_close, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
}

static void dropout_destroy(gpointer dropout)
{
  Dropout *objs = DROPOUT(dropout);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  if (objs->env_to_id != 0)
    g_source_remove(objs->env_to_id);
  process_complete(objs, OBSNSTAT_CANCEL);
}

static void buttons_toggled(gpointer dropout)
{
  Dropout *objs = DROPOUT(dropout);
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_close)))
  {
    act_log_error(act_log_msg("Both dropout open and close buttons simultaneously active. Stopping dropout."));
    send_stop(objs);
  }
  else if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_close)))
    send_stop(objs);
  else if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_open)))
    send_start_open(objs);
  else
    send_start_close(objs);
}

static void buttons_nand(GtkWidget *button1, gpointer button2)
{
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button1)) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button2)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button2), FALSE);
}

static void block_button_toggles(Dropout *objs)
{
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_nand), objs->btn_close);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(buttons_nand), objs->btn_open);
}

static void unblock_button_toggles(Dropout *objs)
{
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_toggled), objs);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(buttons_nand), objs->btn_close);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(buttons_nand), objs->btn_open);
}

static guchar process_quit(Dropout *objs, struct act_msg_quit *msg_quit)
{
  if (!msg_quit->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->dropout_cur & DSHUTT_CLOSED_MASK) > 0)
    return OBSNSTAT_GOOD;
  if (objs->pending_msg)
  {
    act_log_debug(act_log_msg("Quit message received, but another message is being processed. Cancelling earlier."));
    process_complete(objs, OBSNSTAT_CANCEL);
  }
  if (objs->locked)
    act_log_error(act_log_msg("Auto-quit message received, but lock is engaged. Sending close anyway and hoping for the best."));
  else
    act_log_debug(act_log_msg("Closing dome dropout for auto-quit."));
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
  unblock_button_toggles(objs);
  send_start_close(objs);
  return 0;
}

static guchar process_environ(Dropout *objs, struct act_msg_environ *msg_environ)
{
  if (objs->env_to_id)
    g_source_remove(objs->env_to_id);
  objs->env_to_id = g_timeout_add_seconds(DROPOUT_ENV_TIME_S, env_timeout, objs);
  float new_sun_alt = convert_DMS_D_alt(&msg_environ->sun_alt);
  return environ_change(objs, msg_environ->weath_ok>0, new_sun_alt<0.0);
}

static guchar process_targset(Dropout *objs, struct act_msg_targset *msg_targset)
{
  if (msg_targset->status == OBSNSTAT_ERR_CRIT)
  {
    if ((objs->dropout_cur & DSHUTT_CLOSED_MASK) > 0)
    {
      act_log_debug(act_log_msg("Observation message with critical error flag set, dome dropout already closed."));
      return OBSNSTAT_GOOD;
    }
    act_log_debug(act_log_msg("Observation message with critical error flag set. Closing dome dropout."));
    if (objs->pending_msg)
    {
      act_log_debug(act_log_msg("Earlier message still being processed. Cancelling earlier message."));
      process_complete(objs, OBSNSTAT_CANCEL);
    }
    if (objs->locked)
      act_log_error(act_log_msg("Critical target set message received, but lock is engaged. Sending dome dropout close command anyway."));
    else
      act_log_debug(act_log_msg("Closing dome dropout for critical condition."));
    block_button_toggles(objs);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
    unblock_button_toggles(objs);
    send_start_close(objs);
    return 0;
  }
  if (!msg_targset->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->dropout_cur & DSHUTT_OPEN_MASK) > 0)
  {
    act_log_debug(act_log_msg("Dome dropout already open for automatic observation."));
    return OBSNSTAT_GOOD;
  }
  if (!(objs->weath_ok) || !(objs->sun_alt_ok))
  {
    act_log_error(act_log_msg("Cannot open dome dropout for automatic target set - a weather alert is asserted."));
    return OBSNSTAT_ERR_WAIT;
  }
  if (objs->pending_msg)
  {
    act_log_error(act_log_msg("Cannot open dome dropout for automatic target set, another command is being processed."));
    return OBSNSTAT_ERR_WAIT;
  }
  if (objs->locked)
    act_log_debug(act_log_msg("Received auto target set message and need to open dome dropout, but dome dropout is locked. This should be impossible, continuing and hoping for the best."));
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), FALSE);
  unblock_button_toggles(objs);
  send_start_open(objs);
  return 0;
}

static gboolean fail_timeout(gpointer dropout)
{
  act_log_crit(act_log_msg("Timed out while waiting for dropout to open/close."));
  Dropout *objs = DROPOUT(dropout);
  process_complete(objs, OBSNSTAT_ERR_CRIT);
  GdkColor new_col;
  gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
  gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
  send_start_close(objs);
  objs->fail_to_id = 0;
  return FALSE;
}

static gboolean env_timeout(gpointer dropout)
{
  act_log_debug(act_log_msg("Timed out while waiting for environment message."));
  Dropout *objs = DROPOUT(dropout);
  environ_change(objs, FALSE, FALSE);
  objs->env_to_id = 0;
  return FALSE;
}

static guchar environ_change(Dropout *objs, gboolean weath_ok, gboolean sun_alt_ok)
{
  if (((weath_ok>0) == (objs->weath_ok>0)) && ((sun_alt_ok>0) == (objs->sun_alt_ok>0)))
    return OBSNSTAT_GOOD;
  objs->weath_ok = weath_ok > 0;
  objs->sun_alt_ok = sun_alt_ok > 0;
  if ((objs->weath_ok) && (objs->sun_alt_ok))
  {
    if (!objs->locked)
      gtk_widget_set_sensitive(objs->btn_open, TRUE);
    return OBSNSTAT_GOOD;
  }
  act_log_debug(act_log_msg("Weather alert asserted"));
  gtk_widget_set_sensitive(objs->btn_open, FALSE);
  if ((objs->dropout_cur & DSHUTT_CLOSED_MASK) > 0)
  {
    act_log_debug(act_log_msg("Dome dropout already closed."));
    return OBSNSTAT_GOOD;
  }
  if (objs->pending_msg)
    process_complete(objs, OBSNSTAT_CANCEL);
  if (objs->locked)
    act_log_error(act_log_msg("Cannot close dome dropout because lock is engaged. Probably the dome shutter is not completely open. Find a scape goat. Sending close anyway and hoping for the best."));
  else
    act_log_debug(act_log_msg("Closing dome dropout for weather alert."));
  block_button_toggles(objs);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), TRUE);
  unblock_button_toggles(objs);
  send_start_close(objs);
  return 0;
}

static void process_complete(Dropout *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Dome dropout process complete, status - %hhu %hhu %hhd", status, objs->dropout_cur, objs->dropout_goal));
  guchar ret_stat = status;
  if (status == 0)
  {
    if (objs->dropout_goal == objs->dropout_cur)
      ret_stat = OBSNSTAT_GOOD;
    else
      ret_stat = OBSNSTAT_ERR_RETRY;
  }
  g_signal_emit(G_OBJECT(objs), dropout_signals[PROC_COMPLETE_SIGNAL], 0, ret_stat, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static void send_start_open(Dropout *objs)
{
  g_signal_emit(objs, dropout_signals[SEND_START_OPEN_SIGNAL], 0);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = g_timeout_add_seconds(DROPOUT_FAIL_TIME_S, fail_timeout, objs);
  objs->dropout_goal = DSHUTT_OPEN_MASK;
}

static void send_start_close(Dropout *objs)
{
  g_signal_emit(objs, dropout_signals[SEND_START_CLOSE_SIGNAL], 0);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = g_timeout_add_seconds(DROPOUT_FAIL_TIME_S, fail_timeout, objs);
  objs->dropout_goal = DSHUTT_CLOSED_MASK;
}

static void send_stop(Dropout *objs)
{ 
  g_signal_emit(objs, dropout_signals[SEND_STOP_MOVE_SIGNAL], 0);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = 0;
  objs->dropout_goal = 0;
}

