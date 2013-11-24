#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "dti_marshallers.h"
#include "instrshutt.h"

#define SHUTTER_POWERUP_TIME_S  15
#define SHUTTER_FAIL_TIME_S     3

static void instrshutt_init(GtkWidget *instrshutt);
static void instrshutt_class_init (InstrshuttClass *klass);
static void instrshutt_destroy(gpointer instrshutt);
static void open_toggled(gpointer instrshutt);
static void instrshutt_xor(GtkWidget *button1, gpointer button2);
static void set_buttons(Instrshutt *objs, gboolean open);
static guchar process_quit(Instrshutt *objs, struct act_msg_quit *msg_quit);
static guchar process_datapmt(Instrshutt *objs, struct act_msg_datapmt *msg_datapmt);
static void process_complete(Instrshutt *objs, guchar status);
static void send_open(Instrshutt *objs, gboolean open);
static gboolean powerup_timeout(gpointer instrshutt);
static gboolean fail_timeout(gpointer instrshutt);

enum
{
  SEND_INSTRSHUTT_OPEN_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

static guint instrshutt_signals[LAST_SIGNAL] = { 0 };

GType instrshutt_get_type (void)
{
  static GType instrshutt_type = 0;
  
  if (!instrshutt_type)
  {
    const GTypeInfo instrshutt_info =
    {
      sizeof (InstrshuttClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) instrshutt_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Instrshutt),
      0,
      (GInstanceInitFunc) instrshutt_init,
      NULL
    };
    
    instrshutt_type = g_type_register_static (GTK_TYPE_FRAME, "Instrshutt", &instrshutt_info, 0);
  }
  
  return instrshutt_type;
}

GtkWidget* instrshutt_new (gboolean instrshutt_open)
{
  GtkWidget *instrshutt = g_object_new (instrshutt_get_type (), NULL);
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  instrshutt_update (instrshutt, instrshutt_open);
  g_signal_connect_swapped(G_OBJECT(instrshutt), "destroy", G_CALLBACK(instrshutt_destroy), instrshutt);
  g_signal_connect_swapped(G_OBJECT(objs->btn_open), "toggled", G_CALLBACK(open_toggled), instrshutt);
  g_signal_connect(G_OBJECT(objs->btn_open), "toggled", G_CALLBACK(instrshutt_xor), objs->btn_close);
  g_signal_connect(G_OBJECT(objs->btn_close), "toggled", G_CALLBACK(instrshutt_xor), objs->btn_open);
  
  return instrshutt;
}

void instrshutt_update (GtkWidget *instrshutt, gboolean new_instrshutt_open)
{
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  objs->instrshutt_open = new_instrshutt_open;
  if (objs->fail_to_id)
  {
    g_source_remove(objs->fail_to_id);
    objs->fail_to_id = 0;
  }
  GdkColor new_col;
  if (objs->powerup_to_id > 0)
  {
    g_source_remove(objs->powerup_to_id);
    objs->powerup_to_id = 0;
    objs->timer_sec = 0;
    gtk_button_set_label(GTK_BUTTON(objs->btn_open), "Open");
  }
  set_buttons(objs, new_instrshutt_open);
  if (new_instrshutt_open)
  {
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, NULL);
    if (objs->instrshutt_goal)
      process_complete(objs, OBSNSTAT_GOOD);
    else
    {
      act_log_crit(act_log_msg("Instrument shutter opened when it should be closed."));
      process_complete(objs, OBSNSTAT_ERR_RETRY);
      send_open(objs, FALSE);
    }
  }
  else
  {
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
    if (!objs->instrshutt_goal)
      process_complete(objs, OBSNSTAT_GOOD);
    else
    {
      act_log_crit(act_log_msg("Instrument shutter closed when it should be open."));
      process_complete(objs, OBSNSTAT_ERR_RETRY);
    }
    objs->powerup_to_id = g_timeout_add_seconds(1, powerup_timeout, objs);
    objs->timer_sec = SHUTTER_POWERUP_TIME_S;
    GdkColor new_col;
    gdk_color_parse("#AAAA00", &new_col);
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_NORMAL, &new_col);
    char tmpstr[20];
    snprintf(tmpstr, sizeof(tmpstr)-1, "Open (%d s)", SHUTTER_POWERUP_TIME_S);
    gtk_button_set_label(GTK_BUTTON(objs->btn_open), tmpstr);
  }
}

void instrshutt_process_msg(GtkWidget *instrshutt, DtiMsg *msg)
{
  gchar ret = OBSNSTAT_GOOD;
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  switch (dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_quit(objs, dti_msg_get_quit(msg));
      break;
    case MT_DATA_PMT:
      ret = process_datapmt(objs, dti_msg_get_datapmt(msg));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(instrshutt, instrshutt_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

static void instrshutt_class_init (InstrshuttClass *klass)
{
  instrshutt_signals[SEND_INSTRSHUTT_OPEN_SIGNAL] = g_signal_new ("send-instrshutt-open", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  instrshutt_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void instrshutt_init(GtkWidget *instrshutt)
{
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  gtk_frame_set_label(GTK_FRAME(instrshutt), "Instrument Shutter");
  objs->instrshutt_open = objs->instrshutt_goal = FALSE;
  objs->timer_sec = 0;
  objs->powerup_to_id = objs->fail_to_id = 0;
  objs->pending_msg = NULL;

  objs->box = gtk_table_new(1,2,TRUE);
  gtk_container_add(GTK_CONTAINER(instrshutt), objs->box);
  objs->btn_open = gtk_toggle_button_new_with_label("Open");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_open,0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_close = gtk_toggle_button_new_with_label("Close");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_close,1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
}

static void instrshutt_destroy(gpointer instrshutt)
{
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  if (objs->powerup_to_id != 0)
    g_source_remove(objs->powerup_to_id);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
}

static void open_toggled(gpointer instrshutt)
{
  send_open(INSTRSHUTT(instrshutt), gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(INSTRSHUTT(instrshutt)->btn_open)));
}

static void instrshutt_xor(GtkWidget *button1, gpointer button2)
{
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button1)))
  {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button2)))
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button1), TRUE);
    return;
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button2), FALSE);
}

static void set_buttons(Instrshutt *objs, gboolean open)
{
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(open_toggled), objs);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(instrshutt_xor), objs->btn_close);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(instrshutt_xor), objs->btn_open);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_open), open);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_close), !open);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(open_toggled), objs);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_open), G_CALLBACK(instrshutt_xor), objs->btn_close);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_close), G_CALLBACK(instrshutt_xor), objs->btn_open);
}

static guchar process_quit(Instrshutt *objs, struct act_msg_quit *msg_quit)
{
  if (msg_quit->mode_auto == 0)
    return OBSNSTAT_GOOD;
  if (objs->instrshutt_open == 0)
    return OBSNSTAT_GOOD;
  set_buttons(objs, FALSE);
  send_open(objs, FALSE);
  return 0;
}

static guchar process_datapmt(Instrshutt *objs, struct act_msg_datapmt *msg_datapmt)
{
  if (msg_datapmt->status == OBSNSTAT_ERR_CRIT)
  {
    if (objs->instrshutt_open == 0)
      return OBSNSTAT_GOOD;
    set_buttons(objs, FALSE);
    send_open(objs, FALSE);
    return 0;
  }
  if (msg_datapmt->mode_auto == 0)
    return OBSNSTAT_GOOD;
  if (objs->instrshutt_open > 0)
    return OBSNSTAT_GOOD;
  if (objs->powerup_to_id > 0)
  {
    act_log_normal(act_log_msg("Automatic MT_DATA_PMT received, but instrument shutter has not yet powered up."));
    return OBSNSTAT_ERR_RETRY;
  }
  set_buttons(objs, TRUE);
  send_open(objs, TRUE);
  return 0;
}

static void process_complete(Instrshutt *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Instrument process complete: status %hhu - %hhu %hhu", status, objs->instrshutt_open, objs->instrshutt_goal));
  g_signal_emit(G_OBJECT(objs), instrshutt_signals[PROC_COMPLETE_SIGNAL], 0, status, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static void send_open(Instrshutt *objs, gboolean open)
{
  objs->instrshutt_goal = open;
  g_signal_emit(G_OBJECT(objs), instrshutt_signals[SEND_INSTRSHUTT_OPEN_SIGNAL], 0, open);
  if (objs->fail_to_id > 0)
    g_source_remove(objs->fail_to_id);
  objs->fail_to_id = g_timeout_add_seconds(SHUTTER_FAIL_TIME_S, fail_timeout, objs);
  if ((open) && (objs->powerup_to_id > 0))
  {
    act_log_normal(act_log_msg("Shutter re-opened before power-up cycle complete."));
    g_source_remove(objs->powerup_to_id);
    objs->powerup_to_id = 0;
  }
}

static gboolean powerup_timeout(gpointer instrshutt)
{
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  objs->timer_sec--;
  if (objs->timer_sec <= 0)
  {
    gtk_widget_modify_bg(objs->btn_open, GTK_STATE_NORMAL, NULL);
    gtk_button_set_label(GTK_BUTTON(objs->btn_open), "Open");
    objs->powerup_to_id = 0;
    return FALSE;
  }
  char tmpstr[20];
  snprintf(tmpstr, sizeof(tmpstr)-1, "Open (%d s)", objs->timer_sec);
  gtk_button_set_label(GTK_BUTTON(objs->btn_open), tmpstr);
  return TRUE;
}

static gboolean fail_timeout(gpointer instrshutt)
{
  Instrshutt *objs = INSTRSHUTT(instrshutt);
  objs->fail_to_id = 0;
  GdkColor new_col;
  gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->btn_open, GTK_STATE_ACTIVE, &new_col);
  gtk_widget_modify_bg(objs->btn_close, GTK_STATE_ACTIVE, &new_col);
  act_log_crit(act_log_msg("Timed out while waiting for instrument shutter to %s.", objs->instrshutt_open ? "open":"close"));
  send_open(objs, FALSE);
  process_complete(objs, OBSNSTAT_ERR_CRIT);
  return FALSE;
}

