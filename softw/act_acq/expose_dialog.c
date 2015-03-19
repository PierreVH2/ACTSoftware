#include <gtk/gtk.h>
#include <act_log.h>
#include "expose_dialog.h"

#ifndef TABLE_PADDING
#define TABLE_PADDING 3
#endif

enum
{
  TYPESTORE_TYPE=0,
  TYPESTORE_NAME,
  TYPESTORE_NUM_COLS
};

static void instance_init(GtkWidget *expose_dialog);

GType expose_dialog_type(void)
{
  static GType expose_type = 0;
  
  if (!expose_type)
  {
    const GTypeInfo expose_info =
    {
      sizeof (ExposeDialogClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      NULL, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (ExposeDialog),
      0,
      (GInstanceInitFunc) instance_init,
      NULL
    };
    
    expose_type = g_type_register_static (GTK_TYPE_DIALOG, "ExposeDialog", &expose_info, 0);
  }
  
  return expose_type;
}

GtkWidget *expose_dialog_new(GtkWidget *parent, CcdCntrl *cntrl)
{
  ExposeDialog *objs = EXPOSE_DIALOG(g_object_new(expose_dialog_type(), NULL));
  gtk_window_set_transient_for(GTK_WINDOW(objs), GTK_WINDOW(parent));
  gtk_dialog_add_buttons (GTK_DIALOG(objs), GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
  gtk_window_set_title(GTK_WINDOW(objs), "Exposure parameters");
  
  // Set limits of necessary fields based on info from cntrl
  
  return GTK_WIDGET(objs);
}

guchar expose_dialog_get_image_type(GtkWidget *expose_dialog)
{
  GtkWidget *cmb_img_type = EXPOSE_DIALOG(expose_dialog)->cmb_img_type;
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX(cmb_img_type),&iter))
  {
    act_log_error(act_log_msg("No valid grid selected."));
    return;
  }
  gint imgt_active = -1;
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_img_type));
  gtk_tree_model_get(model, &iter, TYPESTORE_TYPE, &imgt_active, -1);
  return (guchar)imgt_active;
}

guint expose_dialog_get_win_start_x(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_start_x));
}

guint expose_dialog_get_win_start_y(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_start_y));
}

guint expose_dialog_get_win_width(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_width));
}

guint expose_dialog_get_win_height(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_height));
}

guint expose_dialog_get_prebin_x(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_prebin_x));
}

guint expose_dialog_get_previn_y(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_prebin_y));
}

gfloat expose_dialog_get_exp_t(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_exp_t_s));
}

guint expose_dialog_get_repetitions(GtkWidget *expose_dialog)
{
  return gtk_spin_button_get_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_repetitions));
}

gboolean expose_dialog_set_image_type(GtkWidget *expose_dialog, guchar img_type)
{
  GtkWidget *cmb_img_type = EXPOSE_DIALOG(expose_dialog)->cmb_img_type;
  GtkTreeIter iter;
  gint tmp_imgt;
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_img_type));
  if (!gtk_tree_model_get_iter_first(model, &iter))
    return FALSE;
  gtk_tree_model_get(model, &iter, TYPESTORE_TYPE, &tmp_imgt, -1);
  while (img_type != tmp_imgt)
  {
    if (!gtk_tree_model_iter_next(model, &iter))
      return FALSE;
    gtk_tree_model_get(model, &iter, TYPESTORE_TYPE, &tmp_imgt, -1);
  }
  gtk_combo_box_set_active_iter (GTK_COMBO_BOX(cmb_img_type), &iter);
  return TRUE;
}

void expose_dialog_set_win_start_x(GtkWidget *expose_dialog, guint start_x)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_start_x),start_x);
}

void expose_dialog_set_win_start_y(GtkWidget *expose_dialog, guint start_y)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_start_y),start_y);
}

void expose_dialog_set_win_width(GtkWidget *expose_dialog, guint win_width)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_width),win_width);
}

void expose_dialog_set_win_height(GtkWidget *expose_dialog, guint win_height)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_win_height),win_height);
}

void expose_dialog_set_prebin_x(GtkWidget *expose_dialog, guint prebin_x)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->prebin_x),prebin_x);
}

void expose_dialog_set_prebin_y(GtkWidget *expose_dialog, guint prebin_y)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_prebin_y),prebin_y);
}

void expose_dialog_set_exp_t(GtkWidget *expose_dialog, gfloat exp_t)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_exp_t_s),exp_t);
}

void expose_dialog_set_repetitions(GtkWidget *expose_dialog, guint rpt)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_repetitions),rpt);
}

static void instance_init(GtkWidget *expose_dialog)
{
  ExposeDialog *objs = EXPOSE_DIALOG(expose_dialog);
  
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(objs));
  GtkWidget *box_content = gtk_table_new(7,6,TRUE);
  gtk_container_add(GTK_CONTAINER(content), box_content);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Type"), 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->cmb_img_type = gtk_combo_box_new();
  /// TODO: Initialise combo box
  gtk_table_attach(GTK_TABLE(box_content), objs->cmb_img_type, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 2, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);

  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win X"), 0, 1, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_start_x = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_start_x, 1, 2, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win Y"), 0, 1, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_start_y = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_start_y, 1, 2, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win Width"), 0, 1, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_width = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_width, 1, 2, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win Height"), 0, 1, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_height = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_height, 1, 2, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Prebin X"), 0, 1, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_prebin_x = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_prebin_x, 1, 2, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);

  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Prebin Y"), 0, 1, 7, 8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_prebin_y = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_prebin_y, 1, 2, 7, 8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 2, 8, 9, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Integ T (s)"), 0, 1, 9, 10, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_exp_t_s = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_exp_t_s, 1, 2, 9, 10, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Repeat"), 0, 1, 10, 11, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_repetitions = gtk_spin_button_new();
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_repetitions, 1, 2, 10, 11, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
}

