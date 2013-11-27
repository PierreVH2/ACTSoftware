#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include <act_ipc.h>
#include "dti_marshallers.h"
#include "domemove.h"

#define DOME_AZM_FLOP          5.0
#define DOMEMOVE_FAIL_TIME_S   10

static void domemove_class_init (DomemoveClass *klass);
static void domemove_init(GtkWidget *domemove);
static void domemove_destroy(gpointer domemove);
static void domemove_update (Domemove *objs);
static gboolean left_press(gpointer domemove);
static gboolean right_press(gpointer domemove);
static gboolean leftright_release(gpointer domemove);
static void toggle_auto(gpointer domemove);
static guchar process_quit(Domemove *objs, gboolean mode_auto);
static void set_azm_auto(Domemove *objs, gboolean azm_auto);
static void set_azm_goal(Domemove *objs, gfloat new_azm);
static void process_complete(Domemove *objs, guchar status);
static gboolean fail_timeout(gpointer domemove);

enum
{
  SEND_START_MOVE_LEFT_SIGNAL,
  SEND_START_MOVE_RIGHT_SIGNAL,
  SEND_STOP_MOVE_SIGNAL,
  SEND_AUTO_DOME_AZM_SIGNAL,
  PROC_COMPLETE_SIGNAL,
  LAST_SIGNAL
};

static guint domemove_signals[LAST_SIGNAL] = { 0 };

GType domemove_get_type (void)
{
  static GType domemove_type = 0;
  
  if (!domemove_type)
  {
    const GTypeInfo domemove_info =
    {
      sizeof (DomemoveClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) domemove_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Domemove),
      0,
      (GInstanceInitFunc) domemove_init,
      NULL
    };
    
    domemove_type = g_type_register_static (GTK_TYPE_FRAME, "Domemove", &domemove_info, 0);
  }
  
  return domemove_type;
}

GtkWidget *domemove_new (gboolean dome_moving, gfloat dome_azm)
{
  GtkWidget *domemove = g_object_new (domemove_get_type (), NULL);
  Domemove *objs = DOMEMOVE(domemove);
  objs->azm_cur = dome_azm;
  objs->azm_goal = dome_azm;
  objs->moving = dome_moving;
  domemove_update (objs);
  
  g_signal_connect_swapped(G_OBJECT(domemove), "destroy", G_CALLBACK(domemove_destroy), domemove);
  g_signal_connect_swapped(G_OBJECT(objs->btn_left), "button-press-event", G_CALLBACK(left_press), (void *)domemove);
  g_signal_connect_swapped(G_OBJECT(objs->btn_left), "button-release-event", G_CALLBACK(leftright_release), (void *)domemove);
  g_signal_connect_swapped(G_OBJECT(objs->btn_right), "button-press-event", G_CALLBACK(right_press), (void *)domemove);
  g_signal_connect_swapped(G_OBJECT(objs->btn_right), "button-release-event", G_CALLBACK(leftright_release), (void *)domemove);
  g_signal_connect_swapped(G_OBJECT(objs->btn_auto), "toggled", G_CALLBACK(toggle_auto), domemove);
  
  return domemove;
}

void domemove_update_moving (GtkWidget *domemove, gboolean new_dome_moving)
{
  Domemove *objs = DOMEMOVE(domemove);
  objs->moving = new_dome_moving;
  domemove_update(objs);
}

void domemove_update_azm (GtkWidget *domemove, gfloat new_azm)
{
  Domemove *objs = DOMEMOVE(domemove);
  objs->azm_cur = new_azm;
  domemove_update(objs);
}

void domemove_process_msg(GtkWidget *domemove, DtiMsg *msg)
{
  guchar ret = OBSNSTAT_GOOD;
  Domemove *objs = DOMEMOVE(domemove);
  switch(dti_msg_get_mtype(msg))
  {
    case MT_QUIT:
      ret = process_quit(objs,dti_msg_get_quit(msg)->mode_auto);
      break;
    case MT_TARG_SET:
      set_azm_auto(objs, dti_msg_get_targset(msg)->mode_auto);
      break;
    case MT_DATA_PMT:
      set_azm_auto(objs, dti_msg_get_datapmt(msg)->mode_auto);
      break;
    case MT_DATA_CCD:
      set_azm_auto(objs, dti_msg_get_dataccd(msg)->mode_auto);
      break;
    case MT_COORD:
      set_azm_goal(objs, convert_DMS_D_azm(&(dti_msg_get_coord(msg)->azm)));
      break;
  }
  if (ret != 0)
  {
    g_signal_emit(domemove, domemove_signals[PROC_COMPLETE_SIGNAL], 0, ret, msg);
    return;
  }
  g_object_ref(msg);
  objs->pending_msg = msg;
}

static void domemove_class_init (DomemoveClass *klass)
{
  domemove_signals[SEND_START_MOVE_LEFT_SIGNAL] = g_signal_new("start-move-left", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  domemove_signals[SEND_START_MOVE_RIGHT_SIGNAL] = g_signal_new("start-move-right", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  domemove_signals[SEND_STOP_MOVE_SIGNAL] = g_signal_new("stop-move", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  domemove_signals[SEND_AUTO_DOME_AZM_SIGNAL] = g_signal_new("send-azm", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__FLOAT, G_TYPE_NONE, 1, G_TYPE_FLOAT);
  domemove_signals[PROC_COMPLETE_SIGNAL] = g_signal_new("proc-complete", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_POINTER, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_POINTER);
}

static void domemove_init(GtkWidget *domemove)
{
  Domemove *objs = DOMEMOVE(domemove);
  gtk_frame_set_label(GTK_FRAME(domemove), "Dome Rotation");
  objs->azm_goal = objs->azm_cur = 0.0;
  objs->moving = objs->azm_auto = FALSE;
  objs->fail_to_id = 0;
  objs->pending_msg = NULL;
  objs->box = gtk_table_new(2,2,TRUE);
  gtk_container_add(GTK_CONTAINER(domemove), objs->box);
  
  objs->btn_left = gtk_button_new_with_label("Left");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_left, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_right = gtk_button_new_with_label("Right");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_right, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->btn_auto = gtk_toggle_button_new_with_label("Auto");
  gtk_table_attach(GTK_TABLE(objs->box), objs->btn_auto, 0, 1, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->evb_azm = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(objs->box), objs->evb_azm, 1, 2, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 5, 5);
  objs->lbl_azm = gtk_label_new("N/A");
  gtk_container_add(GTK_CONTAINER(objs->evb_azm), objs->lbl_azm);
}

static void domemove_destroy(gpointer domemove)
{
  Domemove *objs = DOMEMOVE(domemove);
  if (objs->fail_to_id != 0)
    g_source_remove(objs->fail_to_id);
  process_complete(objs, OBSNSTAT_CANCEL);
}

static void domemove_update (Domemove *objs)
{
  // If dome auto is activated for observing procedure and dome stops moving and dome at goal azimuth
  if ((objs->fail_to_id > 0) && (!objs->moving) && ((fabs(objs->azm_goal - objs->azm_cur) < DOME_AZM_FLOP) || (fabs(objs->azm_goal - objs->azm_cur - 360.0) < DOME_AZM_FLOP)))
  {
    if (objs->fail_to_id > 0)
    {
      g_source_remove(objs->fail_to_id);
      objs->fail_to_id = 0;
    }
    process_complete(objs, OBSNSTAT_GOOD);
  }
  
  GdkColor new_col;
  if (objs->moving)
    gdk_color_parse("#AAAA00", &new_col);
  else if ((fabs(objs->azm_cur - fabs(objs->azm_goal)) < 5) || (fabs(objs->azm_cur - objs->azm_goal - 360.0) < 5))
    gdk_color_parse("#00AA00", &new_col);
  else if (objs->azm_auto)
  {
    gdk_color_parse("#AA0000", &new_col);
    act_log_error(act_log_msg("Dome not aligned with telescope."));
  }
  else
    gdk_color_parse("#0000AA", &new_col);
  gtk_widget_modify_bg(objs->evb_azm, GTK_STATE_NORMAL, &new_col);
  
  char tmpstr[20];
  sprintf(tmpstr, "Azm: %5.1f\302\260", objs->azm_cur);
  gtk_label_set_text(GTK_LABEL(objs->lbl_azm), tmpstr);
}

static gboolean left_press(gpointer domemove)
{
  g_signal_emit(G_OBJECT(domemove), domemove_signals[SEND_START_MOVE_LEFT_SIGNAL], 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(DOMEMOVE(domemove)->btn_auto), FALSE);
  return FALSE;
}

static gboolean right_press(gpointer domemove)
{
  g_signal_emit(G_OBJECT(domemove), domemove_signals[SEND_START_MOVE_RIGHT_SIGNAL], 0);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(DOMEMOVE(domemove)->btn_auto), FALSE);
  return FALSE;
}

static gboolean leftright_release(gpointer domemove)
{
  g_signal_emit(G_OBJECT(domemove), domemove_signals[SEND_STOP_MOVE_SIGNAL], 0);
  return FALSE;
}

static void toggle_auto(gpointer domemove)
{
  Domemove *objs = DOMEMOVE(domemove);
  set_azm_auto(objs, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_auto)));
}

static guchar process_quit(Domemove *objs, gboolean mode_auto)
{
  set_azm_auto(objs, FALSE);
  if (!mode_auto)
    return OBSNSTAT_GOOD;
  set_azm_goal(objs, 0.0);
  return 0;
}

static void set_azm_auto(Domemove *objs, gboolean azm_auto)
{
  if (azm_auto)
    set_azm_goal(objs, objs->azm_goal);
  else
  {
    if (objs->fail_to_id > 0)
    {
      g_source_remove(objs->fail_to_id);
      objs->fail_to_id = 0;
    }
    process_complete(objs, OBSNSTAT_CANCEL);
  }
  objs->azm_auto = azm_auto;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_auto)) != azm_auto)
  {
    glong handler = g_signal_handler_find(G_OBJECT(objs->btn_auto), G_SIGNAL_MATCH_FUNC, 0, 0, NULL, G_CALLBACK(toggle_auto), NULL);
    g_signal_handler_block(G_OBJECT(objs->btn_auto), handler);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_auto), azm_auto);
    g_signal_handler_unblock(G_OBJECT(objs->btn_auto), handler);
  }
}

static void set_azm_goal(Domemove *objs, gfloat new_azm)
{
  objs->azm_goal = new_azm;
  if ((objs->azm_auto) && (fabs(new_azm - objs->azm_cur) > DOME_AZM_FLOP) && (fabs(new_azm - objs->azm_cur + 360.0) > DOME_AZM_FLOP))
  {
    g_signal_emit(G_OBJECT(objs), domemove_signals[SEND_AUTO_DOME_AZM_SIGNAL], 0, objs->azm_goal);
    if (objs->fail_to_id > 0)
      g_source_remove(objs->fail_to_id);
    objs->fail_to_id = g_timeout_add_seconds(DOMEMOVE_FAIL_TIME_S, fail_timeout, objs);
  }
}

static void process_complete(Domemove *objs, guchar status)
{
  if (objs->pending_msg == NULL)
    return;
  act_log_debug(act_log_msg("Domemove process complete: status %hhu - %hhu %hhu %f %f", status, objs->moving, objs->azm_auto, objs->azm_cur, objs->azm_goal));
  g_signal_emit(G_OBJECT(objs), domemove_signals[PROC_COMPLETE_SIGNAL], 0, status, objs->pending_msg);
  g_object_unref(objs->pending_msg);
  objs->pending_msg = NULL;
}

static gboolean fail_timeout(gpointer domemove)
{
  Domemove *objs = DOMEMOVE(domemove);
  process_complete(objs, OBSNSTAT_ERR_RETRY);
  objs->fail_to_id = 0;
  GdkColor new_col;
  gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_azm, GTK_STATE_NORMAL, &new_col);
  return FALSE;
}
