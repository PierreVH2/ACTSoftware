#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "dti_marshallers.h"
#include "dtimisc.h"
#include "focusdialog.h"
#include "ehtdialog.h"

#define FOCUS_FAIL_TIME_S     60
#define EHT_FAIL_TIME_S       10
#define EHT_STAB_TIME_S       60
#define PROC_FAIL_TIME_S      600

static void dtimisc_class_init (DtimiscClass *klass);
static void dtimisc_init(GtkWidget *dtimisc);
static void dtimisc_destroy(gpointer dtimisc);
static void focus_clicked(GtkWidget *btn_focus, gpointer dtimisc);
static void focusdialog_destroyed(gpointer dtimisc);
static void focusdialog_send_focus(gpointer dtimisc, gint focus_pos);
static void eht_clicked(GtkWidget *btn_eht, gpointer dtimisc);
static void ehtdialog_destroyed(gpointer dtimisc);
static void ehtdialog_send_eht(gpointer dtimisc, gboolean eht_high);
static guchar process_targset(Dtimisc *objs, struct act_msg_targset *msg_targset);
static guchar process_dataccd(Dtimisc *objs, struct act_msg_dataccd *msg_dataccd);
static guchar process_datapmt(Dtimisc *objs, struct act_msg_datapmt *msg_datapmt);
static gboolean focus_fail_timeout(gpointer dtimisc);
static gboolean eht_timer_func(gpointer dtimisc);

enum
{
  SEND_FOCUS_POS_SIGNAL,
  SEND_EHT_HIGH_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

static guint dtimisc_signals[LAST_SIGNAL] = { 0 };

GType dtimisc_get_type (void)
{
  static GType dtimisc_type = 0;
  
  if (!dtimisc_type)
  {
    const GTypeInfo dtimisc_info =
    {
      sizeof (DtimiscClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) dtimisc_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Dtimisc),
      0,
      (GInstanceInitFunc) dtimisc_init,
      NULL
    };
    
    dtimisc_type = g_type_register_static (GTK_TYPE_FRAME, "Dtimisc", &dtimisc_info, 0);
  }
  
  return dtimisc_type;
}

GtkWidget *dtimisc_new (gboolean plccomm_ok, gboolean watchdog_trip, gboolean power_fail, gboolean trapdoor_open, guchar eht_mode, guchar focus_stat, gint focus_pos)
{
  GtkWidget *dtimisc = g_object_new (dtimisc_get_type (), NULL);
  Dtimisc *objs = DTIMISC(dtimisc);
  objs->plccomm_ok = plccomm_ok;
  dtimisc_update_plccomm(dtimisc, plccomm_ok);
  objs->watchdog_trip = watchdog_trip;
  dtimisc_update_watchdog(dtimisc, watchdog_trip);
  objs->trapdoor_open = trapdoor_open;
  dtimisc_update_trapdoor(dtimisc, trapdoor_open);
  objs->power_fail = power_fail;
  dtimisc_update_power(dtimisc, power_fail);
  objs->focus_pos = focus_stat;
  dtimisc_update_focus_stat(dtimisc, focus_stat);
  objs->focus_stat = focus_pos;
  dtimisc_update_focus_pos(dtimisc, focus_pos);
  objs->eht_mode = eht_mode;
  dtimisc_update_eht(dtimisc, eht_mode);
  
  g_signal_connect_swapped(G_OBJECT(dtimisc), "destroy", G_CALLBACK(dtimisc_destroy), dtimisc);
  g_signal_connect(G_OBJECT(objs->btn_focus), "clicked", G_CALLBACK(focus_clicked), dtimisc);
  g_signal_connect(G_OBJECT(objs->btn_eht), "clicked", G_CALLBACK(eht_clicked), dtimisc);
  
  return dtimisc;
}

void dtimisc_update_plccomm(GtkWidget *dtimisc, gboolean new_plccomm_ok)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  if (new_plccomm_ok)
    gdk_color_parse("#00AA00", &new_col);
  else
    gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_plccomm, GTK_STATE_NORMAL, &new_col);
  objs->plccomm_ok = new_plccomm_ok;
}

void dtimisc_update_watchdog(GtkWidget *dtimisc, gboolean new_watchdog_trip)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  if (new_watchdog_trip)
    gdk_color_parse("#AA0000", &new_col);
  else
    gdk_color_parse("#00AA00", &new_col);
  gtk_widget_modify_bg(objs->evb_watchdog, GTK_STATE_NORMAL, &new_col);
  objs->watchdog_trip = new_watchdog_trip;
}

void dtimisc_update_power(GtkWidget *dtimisc, gboolean new_power_fail)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  if (new_power_fail)
    gdk_color_parse("#AA0000", &new_col);
  else
    gdk_color_parse("#00AA00", &new_col);
  gtk_widget_modify_bg(objs->evb_power, GTK_STATE_NORMAL, &new_col);
  objs->power_fail = new_power_fail;
}

void dtimisc_update_trapdoor(GtkWidget *dtimisc, gboolean new_trapdoor_open)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  if (new_trapdoor_open)
    gdk_color_parse("#AA0000", &new_col);
  else
    gdk_color_parse("#00AA00", &new_col);
  gtk_widget_modify_bg(objs->evb_trapdoor, GTK_STATE_NORMAL, &new_col);
  objs->trapdoor_open = new_trapdoor_open;
}

void dtimisc_update_eht(GtkWidget *dtimisc, guchar new_eht_mode)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  if (((new_eht_mode & EHT_HIGH_MASK) > 0) && ((objs->eht_mode & EHT_HIGH_MASK) == 0))
  {
    if (objs->eht_timer_to_id)
      g_source_remove(objs->eht_timer_to_id);
    objs->eht_timer_trem = EHT_STAB_TIME_S;
    objs->eht_timer_to_id = g_timeout_add_seconds(1, eht_timer_func, dtimisc);
  }
  if ((new_eht_mode & EHT_HIGH_MASK) == 0)
    gdk_color_parse("#AA0000", &new_col);
  else if (objs->eht_timer_trem > 0)
    gdk_color_parse("#AAAA00", &new_col);
  else
    gdk_color_parse("#00AA00", &new_col);
  gtk_widget_modify_bg(objs->btn_eht, GTK_STATE_NORMAL, &new_col);
  
  objs->eht_mode = new_eht_mode;
  if (objs->ehtdialog)
    ehtdialog_update(objs->ehtdialog, new_eht_mode, objs->eht_timer_trem);
}

void dtimisc_update_focus_stat(GtkWidget *dtimisc, guchar new_focus_stat)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  if ((new_focus_stat & FOCUS_STALL_MASK) > 0)
    gdk_color_parse("#AA0000", &new_col);
  else if ((new_focus_stat & FOCUS_MOVING_MASK) > 0)
    gdk_color_parse("#AAAA00", &new_col);
  else if ((new_focus_stat & FOCUS_SLOT_MASK) > 0)
    gdk_color_parse("#00AA00", &new_col);
  else
    gdk_color_parse("#0000AA", &new_col);
  gtk_widget_modify_bg(objs->btn_focus, GTK_STATE_NORMAL, &new_col);

  if ((objs->focus_stat & FOCUS_STALL_MASK) != (new_focus_stat & FOCUS_STALL_MASK))
  {
    if ((new_focus_stat & FOCUS_STALL_MASK) > 0)
    {
      if (objs->focus_fail_to_id)
        g_source_remove(objs->focus_fail_to_id);
      objs->focus_fail_to_id = g_timeout_add_seconds(FOCUS_FAIL_TIME_S, focus_fail_timeout, dtimisc);
    }
    else if (objs->focus_fail_to_id)
    {
      g_source_remove(objs->focus_fail_to_id);
      objs->focus_fail_to_id = 0;
    }
  }
  
  objs->focus_stat = new_focus_stat;
  if (objs->focusdialog)
    focusdialog_update(objs->focusdialog, objs->focus_stat, objs->focus_pos);
}

void dtimisc_update_focus_pos(GtkWidget *dtimisc, gint new_focus_pos)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  GdkColor new_col;
  
  char tmpstr[50];
  sprintf(tmpstr, "Focus... (%hd)", new_focus_pos);
  gtk_button_set_label(GTK_BUTTON(objs->btn_focus), tmpstr);
  objs->focus_pos = new_focus_pos;
  if (objs->focusdialog)
    focusdialog_update(objs->focusdialog, objs->focus_stat, objs->focus_pos);
}

void dtimisc_process_msg(GtkWidget *dtimisc, DtiMsg *msg)
{
  guchar ret = OBSNSTAT_GOOD;
  Dtimisc *objs = DTIMISC(dtimisc);
  switch(dti_msg_get_mtype(msg))
  {
    case MT_TARG_SET:
      ret = process_targset(objs, dti_msg_get_targset(msg));
      break;
    case MT_DATA_PMT:
      ret = process_datapmt(objs, dti_msg_get_datapmt(msg));
      break;
    case MT_DATA_CCD:
      ret= process_dataccd(objs, dti_msg_get_dataccd(msg));
      break;
  }
  if (ret == 0)
  {
    act_log_debug(act_log_msg("Message of type %hhu produced a zero return value.", dti_msg_get_mtype(msg)));
    ret = OBSNSTAT_GOOD;
  }
  g_signal_emit(dtimisc, dtimisc_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
}

static void dtimisc_class_init (DtimiscClass *klass)
{
  dtimisc_signals[SEND_FOCUS_POS_SIGNAL] = g_signal_new("send-focus-pos", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  dtimisc_signals[SEND_EHT_HIGH_SIGNAL] = g_signal_new("send-eht-high", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dtimisc_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void dtimisc_init(GtkWidget *dtimisc)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  gtk_frame_set_label(GTK_FRAME(dtimisc), "Miscellaneous");
  objs->plccomm_ok = objs->trapdoor_open = objs->power_fail = objs->watchdog_trip = FALSE;
  objs->focus_stat = objs->focus_pos = objs->eht_mode = 0;
  objs->focus_fail_to_id = objs->eht_timer_trem = objs->eht_timer_to_id  = 0;
  
  objs->box = gtk_table_new(2,4,TRUE);
  gtk_container_add(GTK_CONTAINER(dtimisc), objs->box);
  objs->evb_plccomm = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(objs->evb_plccomm), gtk_label_new("PLC Comm."));
  gtk_table_attach(GTK_TABLE(objs->box),objs->evb_plccomm,0,1,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->evb_watchdog = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(objs->evb_watchdog), gtk_label_new("Watchdog"));
  gtk_table_attach(GTK_TABLE(objs->box),objs->evb_watchdog,1,2,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->evb_trapdoor = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(objs->evb_trapdoor), gtk_label_new("Trapdoor"));
  gtk_table_attach(GTK_TABLE(objs->box),objs->evb_trapdoor,0,1,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->evb_power = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(objs->evb_power), gtk_label_new("Main Power"));
  gtk_table_attach(GTK_TABLE(objs->box),objs->evb_power,1,2,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->btn_focus = gtk_button_new_with_label("Focus...");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_focus, 2, 3, 0, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_eht = gtk_button_new_with_label("EHT...");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_eht, 3, 4, 0, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->focusdialog = objs->ehtdialog = NULL;
}

static void dtimisc_destroy(gpointer dtimisc)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  if (objs->focus_fail_to_id)
    g_source_remove(objs->focus_fail_to_id);
  if (objs->eht_timer_to_id)
    g_source_remove(objs->eht_timer_to_id);
  objs->eht_timer_trem = 0;
}

static void focus_clicked(GtkWidget *btn_focus, gpointer dtimisc)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  if (objs->focusdialog)
  {
    act_log_debug(act_log_msg("Focus dialog already displayed"));
    return;
  }
  objs->focusdialog = focusdialog_new(gtk_widget_get_toplevel(btn_focus), objs->focus_stat, objs->focus_pos);
  if (!objs->focusdialog)
  {
    act_log_error(act_log_msg("Error creating focus dialog."));
    return;
  }
  g_signal_connect_swapped(G_OBJECT(objs->focusdialog), "destroy", G_CALLBACK(focusdialog_destroyed), dtimisc);
  g_signal_connect_swapped(G_OBJECT(objs->focusdialog), "send-focus-pos", G_CALLBACK(focusdialog_send_focus), dtimisc);
  gtk_widget_show_all(objs->focusdialog);
}

static void focusdialog_destroyed(gpointer dtimisc)
{
  DTIMISC(dtimisc)->focusdialog = NULL;
}

static void focusdialog_send_focus(gpointer dtimisc, gint focus_pos)
{
  g_signal_emit(G_OBJECT(dtimisc), dtimisc_signals[SEND_FOCUS_POS_SIGNAL], 0, focus_pos);
}

static void eht_clicked(GtkWidget *btn_eht, gpointer dtimisc)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  if (objs->ehtdialog)
  {
    act_log_debug(act_log_msg("EHT dialog already displayed"));
    return;
  }
  objs->ehtdialog = ehtdialog_new(gtk_widget_get_toplevel(btn_eht), objs->eht_mode, objs->eht_timer_trem);
  if (!objs->ehtdialog)
  {
    act_log_error(act_log_msg("Error creating EHT dialog."));
    return;
  }
  g_signal_connect_swapped(G_OBJECT(objs->ehtdialog), "destroy", G_CALLBACK(ehtdialog_destroyed), dtimisc);
  g_signal_connect_swapped(G_OBJECT(objs->ehtdialog), "send-eht-high", G_CALLBACK(ehtdialog_send_eht), dtimisc);
  gtk_widget_show_all(objs->ehtdialog);
}

static void ehtdialog_destroyed(gpointer dtimisc)
{
  DTIMISC(dtimisc)->ehtdialog = NULL;
}

static void ehtdialog_send_eht(gpointer dtimisc, gboolean eht_high)
{
  g_signal_emit(G_OBJECT(dtimisc), dtimisc_signals[SEND_EHT_HIGH_SIGNAL], 0, eht_high);
}

static guchar process_targset(Dtimisc *objs, struct act_msg_targset *msg_targset)
{
  if (!msg_targset->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->power_fail) || (objs->trapdoor_open) || (objs->watchdog_trip) || ((objs->focus_stat & FOCUS_MOVING_MASK) > 0))
    return OBSNSTAT_ERR_WAIT;
  return OBSNSTAT_GOOD;
}

static guchar process_dataccd(Dtimisc *objs, struct act_msg_dataccd *msg_dataccd)
{
  if (!msg_dataccd->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->power_fail) || (objs->trapdoor_open) || (objs->watchdog_trip) || ((objs->focus_stat & FOCUS_MOVING_MASK) > 0))
    return OBSNSTAT_ERR_WAIT;
  return OBSNSTAT_GOOD;
}

static guchar process_datapmt(Dtimisc *objs, struct act_msg_datapmt *msg_datapmt)
{
  if (!msg_datapmt->mode_auto)
    return OBSNSTAT_GOOD;
  if ((objs->power_fail) || (objs->trapdoor_open) || (objs->watchdog_trip) || ((objs->focus_stat & FOCUS_MOVING_MASK) > 0))
    return OBSNSTAT_ERR_WAIT;
  if ((objs->eht_mode & EHT_HIGH_MASK) == 0)
  {
    g_signal_emit(G_OBJECT(objs), dtimisc_signals[SEND_EHT_HIGH_SIGNAL], 0, TRUE);
    return OBSNSTAT_ERR_WAIT;
  }
  if (objs->eht_timer_trem > 0)
    return OBSNSTAT_ERR_WAIT;
  return OBSNSTAT_GOOD;
}

static gboolean focus_fail_timeout(gpointer dtimisc)
{ 
  act_log_error(act_log_msg("Timed out while waiting for focus stall condition to clear."));
  Dtimisc *objs = DTIMISC(dtimisc);
  g_signal_emit(G_OBJECT(dtimisc), dtimisc_signals[SEND_FOCUS_POS_SIGNAL], 0, 0);
  objs->focus_fail_to_id = 0;
  return FALSE;
}

static gboolean eht_timer_func(gpointer dtimisc)
{
  Dtimisc *objs = DTIMISC(dtimisc);
  objs->eht_timer_trem--;
  if (objs->eht_timer_trem <= 0)
  {
    GdkColor new_col;
    gdk_color_parse("#00AA00", &new_col);
    gtk_widget_modify_bg(objs->btn_eht, GTK_STATE_NORMAL, &new_col);
    gtk_button_set_label(GTK_BUTTON(objs->btn_eht), "EHT...");
    objs->eht_timer_trem = 0;
    objs->eht_timer_to_id = 0;
    if (objs->ehtdialog)
      ehtdialog_update(objs->ehtdialog, objs->eht_mode, 0);
    return FALSE;
  }
  char tmpstr[20];
  snprintf(tmpstr, sizeof(tmpstr)-1, "EHT... (%d s)", objs->eht_timer_trem);
  gtk_button_set_label(GTK_BUTTON(objs->btn_eht), tmpstr);
  if (objs->ehtdialog)
    ehtdialog_update(objs->ehtdialog, objs->eht_mode, objs->eht_timer_trem);
  return TRUE;
}
