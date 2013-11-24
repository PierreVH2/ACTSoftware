#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include "focusdialog.h"

static void focusdialog_class_init (FocusdialogClass *klass);
static void focusdialog_init(GtkWidget *focusdialog);
static void focus_go(gpointer focusdialog);
static void focus_reset(gpointer focusdialog);

enum
{
  SEND_FOCUS_POS_SIGNAL,
  LAST_SIGNAL
};

static guint focusdialog_signals[LAST_SIGNAL] = { 0 };

GType focusdialog_get_type (void)
{
  static GType focusdialog_type = 0;
  
  if (!focusdialog_type)
  {
    const GTypeInfo focusdialog_info =
    {
      sizeof (FocusdialogClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) focusdialog_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Focusdialog),
      0,
      (GInstanceInitFunc) focusdialog_init,
      NULL
    };
    
    focusdialog_type = g_type_register_static (GTK_TYPE_DIALOG, "Focusdialog", &focusdialog_info, 0);
  }
  
  return focusdialog_type;
}

GtkWidget *focusdialog_new (GtkWidget *parent, guchar focus_stat, gshort focus_pos)
{
  GtkWidget *focusdialog = g_object_new (focusdialog_get_type (), NULL);
  gtk_window_set_transient_for(GTK_WINDOW(focusdialog), GTK_WINDOW(parent));
  Focusdialog *objs = FOCUSDIALOG(focusdialog);
  objs->focus_stat = ~focus_stat;
  objs->focus_pos = focus_pos-1;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_focus_pos), focus_pos);
  focusdialog_update(focusdialog, focus_stat, focus_pos);
  
  return focusdialog;
}

void focusdialog_update (GtkWidget *focusdialog, guchar focus_stat, gshort focus_pos)
{
  Focusdialog *objs = FOCUSDIALOG(focusdialog);
  if ((objs->focus_stat == focus_stat) && (objs->focus_pos == focus_pos))
    return;
  
  char posstr[20];
  sprintf(posstr, "%hd", focus_pos);
  gtk_label_set_text(GTK_LABEL(objs->lbl_focus_pos), posstr);
  objs->focus_pos = focus_pos;
  
  GdkColor new_col;
  if ((focus_stat & FOCUS_STALL_MASK) > 0)
  {
    gdk_color_parse("#AA0000", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_focus_stat), "STALLED");
  }
  else if ((focus_stat & FOCUS_MOVING_MASK) > 0)
  {
    gdk_color_parse("#AAAA00", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_focus_stat), "MOVING");
  }
  else if ((focus_stat & FOCUS_REF_MASK) > 0)
  {
    gdk_color_parse("#00AA00", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_focus_stat), "REF");
  }
  else if ((focus_stat & FOCUS_SLOT_MASK) > 0)
  {
    gdk_color_parse("#00AA00", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_focus_stat), "SLOT");
  }
  else
  {
    gdk_color_parse("#0000AA", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_focus_stat), "N/A");
  }
  
  gtk_widget_modify_bg(objs->evb_focus_stat, GTK_STATE_NORMAL, &new_col);
  objs->focus_stat = focus_stat;
}

static void focusdialog_class_init (FocusdialogClass *klass)
{
  focusdialog_signals[SEND_FOCUS_POS_SIGNAL] = g_signal_new("send-focus-pos", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
}

static void focusdialog_init(GtkWidget *focusdialog)
{
  Focusdialog *objs = FOCUSDIALOG(focusdialog);
  gtk_window_set_title(GTK_WINDOW(focusdialog), "Telescope Focus");
  gtk_window_set_modal(GTK_WINDOW(focusdialog), TRUE);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(focusdialog), TRUE);
  gtk_dialog_add_button(GTK_DIALOG(focusdialog),GTK_STOCK_OK,GTK_RESPONSE_NONE);
  
  objs->focus_stat = 0;
  objs->focus_pos = 0;
  
  objs->box = gtk_table_new(2,3,TRUE);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(focusdialog))), objs->box);
  objs->evb_focus_stat = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(objs->box),objs->evb_focus_stat,0,1,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->lbl_focus_stat = gtk_label_new("");
  gtk_container_add(GTK_CONTAINER(objs->evb_focus_stat), objs->lbl_focus_stat);
  objs->lbl_focus_pos = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(objs->box),objs->lbl_focus_pos,1,3,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->spn_focus_pos = gtk_spin_button_new_with_range(-660,140,1);
  gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(objs->spn_focus_pos),TRUE);
  gtk_table_attach(GTK_TABLE(objs->box),objs->spn_focus_pos,0,1,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->btn_focus_go = gtk_button_new_with_label("Go");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_focus_go,1,2,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->btn_focus_reset = gtk_button_new_with_label("Reset");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_focus_reset,2,3,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  
  g_signal_connect_swapped(G_OBJECT(focusdialog), "response", G_CALLBACK(gtk_widget_destroy), focusdialog);
  g_signal_connect_swapped(G_OBJECT(objs->btn_focus_go), "clicked", G_CALLBACK(focus_go), focusdialog);
  g_signal_connect_swapped(G_OBJECT(objs->btn_focus_reset), "clicked", G_CALLBACK(focus_reset), focusdialog);
}

static void focus_go(gpointer focusdialog)
{
  Focusdialog *objs = FOCUSDIALOG(focusdialog);
  gint pos = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_focus_pos));
  g_signal_emit(G_OBJECT(focusdialog), focusdialog_signals[SEND_FOCUS_POS_SIGNAL], 0, pos);
}

static void focus_reset(gpointer focusdialog)
{
  g_signal_emit(G_OBJECT(focusdialog), focusdialog_signals[SEND_FOCUS_POS_SIGNAL], 0, 0);
}
