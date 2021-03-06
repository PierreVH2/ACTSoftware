#include <gtk/gtk.h>
#include <act_log.h>
#include "expose_dialog.h"

#ifndef TABLE_PADDING
#define TABLE_PADDING 3
#endif

#define DEFAULT_MAX_CCD_SIZE    4096

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
  guint ccd_width = ccd_cntrl_get_max_width(cntrl), ccd_height = ccd_cntrl_get_max_height(cntrl);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_win_start_x), 1, ccd_width);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_win_start_y), 1, ccd_height);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_win_width), 1, ccd_width);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_win_height), 1, ccd_height);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_prebin_x), 1, ccd_width);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_prebin_y), 1, ccd_height);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_exp_t_s), 0.0, ccd_cntrl_get_max_integ_t_sec(cntrl));
  gtk_spin_button_set_increments(GTK_SPIN_BUTTON(objs->spn_exp_t_s), ccd_cntrl_get_min_integ_t_sec(cntrl), 1.0);
  gtk_spin_button_set_digits(GTK_SPIN_BUTTON(objs->spn_exp_t_s), 3);
  gtk_spin_button_set_range(GTK_SPIN_BUTTON(objs->spn_repetitions), 1, 1000000);
  
  return GTK_WIDGET(objs);
}

guchar expose_dialog_get_image_type(GtkWidget *expose_dialog)
{
  GtkWidget *cmb_img_type = EXPOSE_DIALOG(expose_dialog)->cmb_img_type;
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX(cmb_img_type),&iter))
  {
    act_log_error(act_log_msg("No valid grid selected."));
    return IMGT_NONE;
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

guint expose_dialog_get_prebin_y(GtkWidget *expose_dialog)
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
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(EXPOSE_DIALOG(expose_dialog)->spn_prebin_x),prebin_x);
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
  GtkWidget *box_content = gtk_table_new(11,2,TRUE);
  gtk_container_add(GTK_CONTAINER(content), box_content);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Type"), 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->cmb_img_type = gtk_combo_box_new();
  GtkListStore *imgtstore = gtk_list_store_new(TYPESTORE_NUM_COLS, G_TYPE_INT, G_TYPE_STRING);
  GtkTreeIter iter;
  gtk_list_store_append(imgtstore, &iter);
  gtk_list_store_set(imgtstore, &iter, TYPESTORE_TYPE, IMGT_ACQ_OBJ, TYPESTORE_NAME, "Acq star", -1);
  gtk_list_store_append(imgtstore, &iter);
  gtk_list_store_set(imgtstore, &iter, TYPESTORE_TYPE, IMGT_ACQ_SKY, TYPESTORE_NAME, "Acq sky", -1);
  gtk_list_store_append(imgtstore, &iter);
  gtk_list_store_set(imgtstore, &iter, TYPESTORE_TYPE, IMGT_OBJECT, TYPESTORE_NAME, "Object", -1);
  gtk_list_store_append(imgtstore, &iter);
  gtk_list_store_set(imgtstore, &iter, TYPESTORE_TYPE, IMGT_BIAS, TYPESTORE_NAME, "Bias", -1);
  gtk_list_store_append(imgtstore, &iter);
  gtk_list_store_set(imgtstore, &iter, TYPESTORE_TYPE, IMGT_DARK, TYPESTORE_NAME, "Dark", -1);
  gtk_list_store_append(imgtstore, &iter);
  gtk_list_store_set(imgtstore, &iter, TYPESTORE_TYPE, IMGT_FLAT, TYPESTORE_NAME, "Flat", -1);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_img_type), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(objs->cmb_img_type), renderer, "text", TYPESTORE_NAME);
  gtk_combo_box_set_model(GTK_COMBO_BOX(objs->cmb_img_type), GTK_TREE_MODEL(imgtstore));
  gtk_table_attach(GTK_TABLE(box_content), objs->cmb_img_type, 1, 2, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 2, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);

  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win X"), 0, 1, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_start_x = gtk_spin_button_new_with_range(1, DEFAULT_MAX_CCD_SIZE, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_start_x, 1, 2, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win Y"), 0, 1, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_start_y = gtk_spin_button_new_with_range(1, DEFAULT_MAX_CCD_SIZE, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_start_y, 1, 2, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win Width"), 0, 1, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_width = gtk_spin_button_new_with_range(1, DEFAULT_MAX_CCD_SIZE, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_width, 1, 2, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Win Height"), 0, 1, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_win_height = gtk_spin_button_new_with_range(1, DEFAULT_MAX_CCD_SIZE, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_win_height, 1, 2, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Prebin X"), 0, 1, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_prebin_x = gtk_spin_button_new_with_range(1, DEFAULT_MAX_CCD_SIZE, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_prebin_x, 1, 2, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);

  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Prebin Y"), 0, 1, 7, 8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_prebin_y = gtk_spin_button_new_with_range(1, DEFAULT_MAX_CCD_SIZE, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_prebin_y, 1, 2, 7, 8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 2, 8, 9, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Integ T (s)"), 0, 1, 9, 10, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_exp_t_s = gtk_spin_button_new_with_range(0.0, 1000.0, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_exp_t_s, 1, 2, 9, 10, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Repeat"), 0, 1, 10, 11, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->spn_repetitions = gtk_spin_button_new_with_range(1, 1000000, 1);
  gtk_table_attach(GTK_TABLE(box_content), objs->spn_repetitions, 1, 2, 10, 11, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
}

