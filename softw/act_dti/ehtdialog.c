#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <act_plc.h>
#include <act_log.h>
#include "ehtdialog.h"

static void ehtdialog_class_init (EHTdialogClass *klass);
static void ehtdialog_init(GtkWidget *ehtdialog);
static void off_toggled(gpointer ehtdialog);
static void high_toggled(gpointer ehtdialog);

enum
{
  SEND_EHT_HIGH_SIGNAL,
  LAST_SIGNAL
};

static guint ehtdialog_signals[LAST_SIGNAL] = { 0 };

GType ehtdialog_get_type (void)
{
  static GType ehtdialog_type = 0;
  
  if (!ehtdialog_type)
  {
    const GTypeInfo ehtdialog_info =
    {
      sizeof (EHTdialogClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) ehtdialog_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (EHTdialog),
      0,
      (GInstanceInitFunc) ehtdialog_init,
      NULL
    };
    
    ehtdialog_type = g_type_register_static (GTK_TYPE_DIALOG, "EHTdialog", &ehtdialog_info, 0);
  }
  
  return ehtdialog_type;
}

GtkWidget *ehtdialog_new (GtkWidget *parent, guchar eht_stat, gint stab_time)
{
  GtkWidget *ehtdialog = g_object_new (ehtdialog_get_type (), NULL);
  gtk_window_set_transient_for(GTK_WINDOW(ehtdialog), GTK_WINDOW(parent));
  EHTdialog *objs = EHTDIALOG(ehtdialog);
  objs->eht_stat = ~eht_stat;
  objs->stab_time = stab_time +1;
  ehtdialog_update(ehtdialog, eht_stat, stab_time);
  
  return ehtdialog;
}

void ehtdialog_update (GtkWidget *ehtdialog, guchar eht_stat, gint stab_time)
{
  EHTdialog *objs = EHTDIALOG(ehtdialog);
  printf("EHT dialog update: %hhu, %hhu %d %d\n", objs->eht_stat, eht_stat, objs->stab_time, stab_time);
  if ((objs->eht_stat == eht_stat) && (objs->stab_time == stab_time))
    return;
  
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_off), G_CALLBACK(off_toggled), ehtdialog);
  g_signal_handlers_block_by_func(G_OBJECT(objs->btn_high), G_CALLBACK(high_toggled), ehtdialog);
  
  GdkColor new_col;
  
  if ((eht_stat & EHT_HIGH_MASK) == 0)
  {
    gdk_color_parse("#AA0000", &new_col);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_off), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_high), FALSE);
    gtk_widget_modify_bg(objs->btn_off, GTK_STATE_INSENSITIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_off, GTK_STATE_ACTIVE, &new_col);
    gtk_widget_modify_bg(objs->btn_high, GTK_STATE_INSENSITIVE, NULL);
    gtk_widget_modify_bg(objs->btn_high, GTK_STATE_ACTIVE, NULL);
  }
  else
  {
    if (objs->stab_time <= 0)
      gdk_color_parse("#00AA00", &new_col);
    else
      gdk_color_parse("#AAAA00", &new_col);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_off), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_high), TRUE);
    gtk_widget_modify_bg(objs->btn_off, GTK_STATE_ACTIVE, NULL);
    gtk_widget_modify_bg(objs->btn_high, GTK_STATE_ACTIVE, &new_col);
  }
  
  if (((eht_stat & EHT_MAN_OFF_MASK) > 0) && ((objs->eht_stat & EHT_MAN_OFF_MASK) == 0))
  {
    gtk_widget_set_sensitive(objs->btn_off, FALSE);
    gtk_widget_set_sensitive(objs->btn_high, FALSE);
  }
  else if (((eht_stat & EHT_MAN_OFF_MASK) == 0) && ((objs->eht_stat & EHT_MAN_OFF_MASK) > 0))
  {
    gtk_widget_set_sensitive(objs->btn_off, TRUE);
    gtk_widget_set_sensitive(objs->btn_high, TRUE);
  }
  
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_off), G_CALLBACK(off_toggled), ehtdialog);
  g_signal_handlers_unblock_by_func(G_OBJECT(objs->btn_high), G_CALLBACK(high_toggled), ehtdialog);
  
  if (objs->stab_time != stab_time)
  {
    char tmpstr[100];
    if ((eht_stat & EHT_HIGH_MASK) == 0)
      sprintf(tmpstr, "HT off");
    else if (stab_time <= 0)
      sprintf(tmpstr, "HT stable");
    else
      sprintf(tmpstr, "Time until HT stabilises: %d seconds", stab_time);
    gtk_label_set_text(GTK_LABEL(objs->lbl_stab_time), tmpstr);
    objs->stab_time = stab_time;
  }

  objs->eht_stat = eht_stat;
}

static void ehtdialog_class_init (EHTdialogClass *klass)
{
  ehtdialog_signals[SEND_EHT_HIGH_SIGNAL] = g_signal_new("send-eht-high", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void ehtdialog_init(GtkWidget *ehtdialog)
{
  EHTdialog *objs = EHTDIALOG(ehtdialog);
  gtk_window_set_title(GTK_WINDOW(ehtdialog), "EHT");
  gtk_window_set_modal(GTK_WINDOW(ehtdialog), TRUE);
  gtk_window_set_destroy_with_parent(GTK_WINDOW(ehtdialog), TRUE);
  gtk_dialog_add_button(GTK_DIALOG(ehtdialog),GTK_STOCK_OK,GTK_RESPONSE_NONE);
  
  objs->eht_stat = 0;
  objs->stab_time = 0;
  
  objs->box = gtk_table_new(2,2,TRUE);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(ehtdialog))), objs->box);
  objs->btn_off = gtk_toggle_button_new_with_label("Off");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_off,0,1,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->btn_high = gtk_toggle_button_new_with_label("High");
  gtk_table_attach(GTK_TABLE(objs->box),objs->btn_high,1,2,0,1, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  objs->lbl_stab_time = gtk_label_new("");
  gtk_label_set_width_chars(GTK_LABEL(objs->lbl_stab_time), 45);
  gtk_table_attach(GTK_TABLE(objs->box),objs->lbl_stab_time,0,2,1,2, GTK_FILL|GTK_EXPAND,GTK_FILL|GTK_EXPAND,5,5);
  
  g_signal_connect_swapped(G_OBJECT(ehtdialog), "response", G_CALLBACK(gtk_widget_destroy), ehtdialog);
  g_signal_connect_swapped(G_OBJECT(objs->btn_off), "toggled", G_CALLBACK(off_toggled), ehtdialog);
  g_signal_connect_swapped(G_OBJECT(objs->btn_high), "toggled", G_CALLBACK(high_toggled), ehtdialog);
}

static void off_toggled(gpointer ehtdialog)
{
  EHTdialog *objs = EHTDIALOG(ehtdialog);
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_off)) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_high)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_off), TRUE);
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_off)))
    return;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_off)) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_high)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_high), FALSE);
  g_signal_emit(G_OBJECT(ehtdialog), ehtdialog_signals[SEND_EHT_HIGH_SIGNAL], FALSE);
}

static void high_toggled(gpointer ehtdialog)
{
  EHTdialog *objs = EHTDIALOG(ehtdialog);
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_off)) && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_high)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_high), TRUE);
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_high)))
    return;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_off)) && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(objs->btn_high)))
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_off), FALSE);
  g_signal_emit(G_OBJECT(ehtdialog), ehtdialog_signals[SEND_EHT_HIGH_SIGNAL], TRUE);
}
