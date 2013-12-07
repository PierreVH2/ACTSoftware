#include <gtk/gtk.h>
#include <math.h>
#include <stdlib.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include "telmove_coorddialog.h"

#define TABLE_PADDING   5

static void telmove_coorddialog_init(GtkWidget *coorddialog);
static void change_haradec_sign(GtkWidget *button, gpointer haradec_sign);

GType telmove_coorddialog_get_type (void)
{
  static GType telmove_coorddialog_type = 0;
  
  if (!telmove_coorddialog_type)
  {
    const GTypeInfo telmove_coorddialog_info =
    {
      sizeof (TelmoveCoorddialogClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      NULL,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (TelmoveCoorddialog),
      0,
      (GInstanceInitFunc) telmove_coorddialog_init,
      NULL
    };
    
    telmove_coorddialog_type = g_type_register_static (GTK_TYPE_DIALOG, "TelmoveCoorddialog", &telmove_coorddialog_info, 0);
  }
  
  return telmove_coorddialog_type;
}

GtkWidget *telmove_coorddialog_new(const gchar *title, GtkWidget *parent, gdouble sidt_h, GActTelcoord *cur_coord)
{
  GtkWidget *coorddialog = g_object_new (telmove_coorddialog_get_type (), NULL);
  gtk_window_set_title(GTK_WINDOW(coorddialog), title);
  gtk_window_set_transient_for(GTK_WINDOW(coorddialog), GTK_WINDOW(parent));
  
  g_object_ref(G_OBJECT(cur_coord));
  TelmoveCoorddialog *objs = TELMOVE_COORDDIALOG(coorddialog);
  objs->sidt_h = sidt_h;
  if (sidt_h >= 0.0)
  {
    struct timestruct tmp_sidt;
    struct rastruct tmp_ra;
    convert_H_HMSMS_time(sidt_h, &tmp_sidt);
    calc_RA(&cur_coord->ha, &tmp_sidt, &tmp_ra);
    
    objs->hara_sign_pos = TRUE;
    gtk_button_set_label(GTK_BUTTON(objs->btn_harasign), "+");
    gtk_widget_set_sensitive(objs->btn_harasign, FALSE);
    gtk_label_set_text(GTK_LABEL(objs->lbl_haralabel), "Right Ascension");
    gtk_spin_button_set_range (GTK_SPIN_BUTTON(objs->spn_hara_h), 0, 23);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_hara_h), tmp_ra.hours);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_hara_m), tmp_ra.minutes);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_hara_s), tmp_ra.seconds + tmp_ra.milliseconds/1000.0);
  }
  else
  {
    if (convert_HMSMS_H_ha(&cur_coord->ha) < 0.0)
    {
      objs->hara_sign_pos = FALSE;
      gtk_button_set_label(GTK_BUTTON(objs->btn_harasign), "-");
    }
    else
    {
      objs->hara_sign_pos = TRUE;
      gtk_button_set_label(GTK_BUTTON(objs->btn_harasign), "+");
    }
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_hara_h), abs(cur_coord->ha.hours));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_hara_m), abs(cur_coord->ha.minutes));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_hara_s), abs(cur_coord->ha.seconds) + abs(cur_coord->ha.milliseconds)/1000.0);
  }
  if (convert_DMS_D_dec(&cur_coord->dec) < 0.0)
  {
    objs->dec_sign_pos = FALSE;
    gtk_button_set_label(GTK_BUTTON(objs->btn_decsign), "-");
  }
  else
  {
    objs->dec_sign_pos = TRUE;
    gtk_button_set_label(GTK_BUTTON(objs->btn_decsign), "+");
  }
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_dec_d), abs(cur_coord->dec.degrees));
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_dec_am), abs(cur_coord->dec.amin));
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_dec_as), abs(cur_coord->dec.asec));
  
  g_signal_connect(G_OBJECT(objs->btn_harasign), "clicked", G_CALLBACK(change_haradec_sign), (void*)&objs->hara_sign_pos);
  g_signal_connect(G_OBJECT(objs->btn_decsign), "clicked", G_CALLBACK(change_haradec_sign), (void*)&objs->dec_sign_pos);

  g_object_unref(G_OBJECT(cur_coord));
  return coorddialog;
}

GActTelcoord *telmove_coorddialog_get_coord(GtkWidget *coorddialog, gdouble sidt_h)
{
  TelmoveCoorddialog *objs = TELMOVE_COORDDIALOG(coorddialog);
  gboolean is_sidereal = objs->sidt_h >= 0.0;
  // If dialog created for sidereal coordinates, use newest available sidereal time (i.e. if sidereal coordinates requested with sidt_h>=0, use that sidt, otherwise fallback to using the sidt stored when the dialog was created - this is may lead to deviations if a sidereal-type coordinates dialog is created and non-sidereal coordinates requested, which could happen if telmove loses sidereal time synchronisation while the dialog is running).
  if ((is_sidereal) && (sidt_h >= 0.0))
    objs->sidt_h = sidt_h;
  
  char sign_mult;
  
  struct hastruct tmp_ha;
  struct decstruct tmp_dec;
  if (is_sidereal)
  {
    struct timestruct tmp_sidt;
    convert_H_HMSMS_time(objs->sidt_h, &tmp_sidt);
    struct rastruct tmp_ra;
    tmp_ra.hours = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_hara_h));
    tmp_ra.minutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_hara_m));
    tmp_ra.seconds = trunc(gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_hara_s)));
    tmp_ra.milliseconds = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_hara_s)) - tmp_ra.seconds;
    calc_HAngle(&tmp_ra, &tmp_sidt, &tmp_ha);
  }
  else
  {
    sign_mult = objs->hara_sign_pos > 0 ? 1 : -1;
    tmp_ha.hours = sign_mult*gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_hara_h));
    tmp_ha.minutes = sign_mult*gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_hara_m));
    tmp_ha.seconds = sign_mult*trunc(gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_hara_s)));
    tmp_ha.milliseconds = sign_mult*gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_hara_s)) - tmp_ha.seconds;
  }
  
  sign_mult = objs->dec_sign_pos > 0 ? 1 : -1;
  tmp_dec.degrees = sign_mult*gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_dec_d));
  tmp_dec.amin = sign_mult*gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_dec_am));
  tmp_dec.asec = sign_mult*gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(objs->spn_dec_as));
  
  return gact_telcoord_new(&tmp_ha, &tmp_dec);
}

static void telmove_coorddialog_init(GtkWidget *coorddialog)
{
  TelmoveCoorddialog *objs = TELMOVE_COORDDIALOG(coorddialog);
  gtk_dialog_add_buttons (GTK_DIALOG(coorddialog), GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT, NULL);
  objs->box_content = gtk_table_new(2,8,FALSE);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(coorddialog))), objs->box_content);
  
  objs->sidt_h = -1.0;
  objs->lbl_haralabel = gtk_label_new("Hour Angle");
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->lbl_haralabel, 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_harasign = gtk_button_new_with_label("+");
  objs->hara_sign_pos = TRUE;
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->btn_harasign, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_hara_h = gtk_spin_button_new_with_range(-12,12,1);
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->spn_hara_h, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box_content), gtk_label_new("h"), 3,4,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_hara_m = gtk_spin_button_new_with_range(0,59,1);
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->spn_hara_m, 4,5,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box_content), gtk_label_new("m"), 5,6,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_hara_s = gtk_spin_button_new_with_range(0,59.9,0.1);
  gtk_spin_button_set_increments (GTK_SPIN_BUTTON(objs->spn_hara_s), 0.1, 10.0);
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->spn_hara_s, 6,7,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box_content), gtk_label_new("s"), 7,8,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->lbl_declabel = gtk_label_new("Declination");
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->lbl_declabel, 0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_decsign = gtk_button_new_with_label("-");
  objs->dec_sign_pos = TRUE;
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->btn_decsign, 1,2,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_dec_d = gtk_spin_button_new_with_range(0,180,1);
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->spn_dec_d, 2,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box_content), gtk_label_new("\302\260"), 3,4,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_dec_am = gtk_spin_button_new_with_range(0,59,1);
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->spn_dec_am, 4,5,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box_content), gtk_label_new("'"), 5,6,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_dec_as = gtk_spin_button_new_with_range(0,59,1);
  gtk_table_attach(GTK_TABLE(objs->box_content), objs->spn_dec_as, 6,7,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(objs->box_content), gtk_label_new("\""), 7,8,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
}

static void change_haradec_sign(GtkWidget *button, gpointer haradec_sign)
{
  unsigned char * tmp_haradec_sign = (unsigned char *)haradec_sign;
  *tmp_haradec_sign = !(*tmp_haradec_sign);
  if (*tmp_haradec_sign)
    gtk_button_set_label(GTK_BUTTON(button), "+");
  else
    gtk_button_set_label(GTK_BUTTON(button), "-");
}
