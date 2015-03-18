#include <gtk/gtk.h>
#include <act_log.h>
#include "view_param_dialog.h"
#include "imgdisp.h"

#ifndef TABLE_PADDING
#define TABLE_PADDING 3
#endif

enum
{
  GRIDSTORE_TYPE=0,
  GRIDSTORE_NAME,
  GRIDSTORE_NUM_COLS
};

static void instance_init(GtkWidget *view_param_dialog);
static void class_init(ViewParamDialogClass *klass);
static void instance_dispose(GObject *view_param_dialog);
static void toggle_flip_ns(GtkWidget *btn_flip_ns, gpointer imgdisp);
static void toggle_flip_ew(GtkWidget *btn_flip_ew, gpointer imgdisp);
static void grid_type_change(GtkWidget *cmb_grid_type, gpointer imgdisp);
static void grid_scale_x_change(GtkWidget *scl_grid_x, gpointer imgdisp);
static void grid_scale_y_change(GtkWidget *scl_grid_y, gpointer imgdisp);
static void grid_get_cur(GtkWidget *imgdisp, guchar *cur_grid_type, gfloat *cur_spacing_x, gfloat *cur_spacing_y);
static void faint_changed(GtkWidget *scl_faint, gpointer imgdisp);
static void bright_changed(GtkWidget *scl_bright, gpointer imgdisp);

static GObjectClass *parent_class = NULL;

GType view_param_dialog_type(void)
{
  static GType view_param_type = 0;
  
  if (!view_param_type)
  {
    const GTypeInfo view_param_info =
    {
      sizeof (ViewParamDialogClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (ViewParamDialog),
      0,
      (GInstanceInitFunc) instance_init,
      NULL
    };
    
    view_param_type = g_type_register_static (GTK_TYPE_DIALOG, "ViewParamDialog", &view_param_info, 0);
  }
  
  return view_param_type;
}

GtkWidget *view_param_dialog_new(GtkWidget *parent, GtkWidget *imgdisp)
{
  if (!IS_IMGDISP(imgdisp))
  {
    act_log_error(act_log_msg("Invalid image display object specified."));
    return NULL;
  }
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(g_object_new(view_param_dialog_type(), NULL));
  gtk_window_set_transient_for(GTK_WINDOW(objs), GTK_WINDOW(parent));
  gtk_dialog_add_buttons (GTK_DIALOG(objs), GTK_STOCK_REVERT_TO_SAVED, GTK_RESPONSE_CANCEL, GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE, NULL);
  gtk_window_set_title(GTK_WINDOW(objs), "Change View Parameters");
  
  objs->imgdisp = imgdisp;
  g_object_ref(imgdisp);
  objs->orig_flip_ns = imgdisp_get_flip_ns(imgdisp);
  objs->orig_flip_ew = imgdisp_get_flip_ew(imgdisp);
  objs->orig_faint = imgdisp_get_faint_lim(imgdisp);
  objs->orig_bright = imgdisp_get_bright_lim(imgdisp);
  Imglut *tmp_lut = imgdisp_get_lut(imgdisp);
  objs->orig_lut = imglut_new(imglut_get_num_points(tmp_lut), imglut_get_points(tmp_lut));
  objs->orig_grid_type = imgdisp_get_grid_type(imgdisp);
  objs->orig_grid_spacing_x = imgdisp_get_grid_spacing_x(imgdisp);
  objs->orig_grid_spacing_y = imgdisp_get_grid_spacing_y(imgdisp);
  
  GtkWidget* btn_flip_ns = gtk_toggle_button_new_with_label("Flip N/S");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_flip_ns), objs->orig_flip_ns);
  gtk_table_attach(GTK_TABLE(box_content), btn_flip_ns, 0, 3, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect(G_OBJECT(btn_flip_ns),"toggled",G_CALLBACK(toggle_flip_ns),imgdisp);
  GtkWidget* btn_flip_ew = gtk_toggle_button_new_with_label("Flip E/W");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_flip_ew), objs->orig_flip_ew);
  gtk_table_attach(GTK_TABLE(box_content), btn_flip_ew, 3, 6, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect(G_OBJECT(btn_flip_ew),"toggled",G_CALLBACK(toggle_flip_ew),imgdisp);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 6, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *cmb_grid_type = gtk_combo_box_new();
  GtkListStore *gridstore = gtk_list_store_new(GRIDSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_STRING);
  GtkTreeIter iter;
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_NONE, GRIDSTORE_NAME, "None", -1);
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_IMAGE, GRIDSTORE_NAME, "Image", -1);
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_VIEW, GRIDSTORE_NAME, "View", -1);
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_EQUAT, GRIDSTORE_NAME, "Equatorial", -1);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cmb_grid_type), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(cmb_grid_type), renderer, "text", GRIDSTORE_NAME);
  gtk_combo_box_set_model(GTK_COMBO_BOX(cmb_grid_type), GTK_TREE_MODEL(gridstore));
  gtk_table_attach(GTK_TABLE(box_content), cmb_grid_type, 0, 2, 2, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect(G_OBJECT(cmb_grid_type),"changed",G_CALLBACK(grid_type_change),imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("X scale"), 2, 3, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_grid_x = gtk_hscale_new_with_range(0.0, 100.0, 1.0);
  gtk_range_set_value(GTK_RANGE(scl_grid_x), objs->orig_grid_spacing_x);
  gtk_scale_set_draw_value(GTK_SCALE(scl_grid_x), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_grid_x), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_grid_x),"value-changed",G_CALLBACK(grid_scale_x_change),imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_grid_x, 3, 6, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Y scale"), 2, 3, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_grid_y = gtk_hscale_new_with_range(0.0, 100.0, 1.0);
  gtk_range_set_value(GTK_RANGE(scl_grid_y), objs->orig_grid_spacing_y);
  gtk_scale_set_draw_value(GTK_SCALE(scl_grid_y), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_grid_y), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_grid_y),"value-changed",G_CALLBACK(grid_scale_y_change),imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_grid_y, 3, 6, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 6, 4, 5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *cmb_lut = gtk_combo_box_new();
  gtk_widget_set_sensitive(cmb_lut, FALSE);
  gtk_table_attach(GTK_TABLE(box_content), cmb_lut, 0, 2, 5, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Faint"), 2, 3, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_faint = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_range_set_value(GTK_RANGE(scl_faint), objs->orig_faint);
  gtk_scale_set_draw_value(GTK_SCALE(scl_faint), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_faint), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_faint), "value-changed", G_CALLBACK(faint_changed), imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_faint, 3, 6, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Bright"), 2, 3, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_bright = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_range_set_value(GTK_RANGE(scl_bright), objs->orig_bright);
  gtk_scale_set_draw_value(GTK_SCALE(scl_bright), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_bright), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_bright), "value-changed", G_CALLBACK(bright_changed), imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_bright, 3, 6, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  return GTK_WIDGET(objs);
}

void view_param_dialog_revert(GtkWidget *view_param_dialog)
{
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(view_param_dialog);
  imgdisp_set_flip_ns(objs->imgdisp, objs->orig_flip_ns);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_toggle_ns), objs->orig_flip_ns);
  imgdisp_set_flip_ew(objs->imgdisp, objs->orig_flip_ew);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(objs->btn_toggle_ew), objs->orig_flip_ew);
  imgdisp_set_bright_lim(objs->imgdisp, objs->orig_bright);
  gtk_range_set_value(GTK_RANGE(objs->scl_bright), objs->orig_bright);
  imgdisp_set_faint_lim(objs->imgdisp, objs->orig_faint);
  gtk_range_set_value(GTK_RANGE(objs->scl_faint), objs->orig_faint);
  imgdisp_set_grid(objs->imgdisp, objs->orig_grid_type, objs->orig_grid_spacing_x, objs->orig_grid_spacing_y);
  /** TODO:
   * Set grid grid type combo box
   */
  gtk_range_set_value(GTK_RANGE(objs->scl_grid_x), objs->orig_grid_spacing_x);
  gtk_range_set_value(GTK_RANGE(objs->scl_grid_y), objs->orig_grid_spacing_y);
}

static void instance_init(GtkWidget *view_param_dialog)
{
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(view_param_dialog);
  objs->orig_flip_ns = objs->orig_flip_ew = FALSE;
  objs->orig_faint = objs->orig_bright = 0.0;
  objs->orig_lut = NULL;
  objs->orig_grid_type = 0;
  objs->orig_grid_spacing_x = objs->orig_grid_spacing_y = 0.0;
  
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(objs));
  GtkWidget *box_content = gtk_table_new(7,6,TRUE);
  gtk_container_add(GTK_CONTAINER(content), box_content);
  
  objs->btn_flip_ns = gtk_toggle_button_new_with_label("Flip N/S");
  gtk_table_attach(GTK_TABLE(box_content), objs->btn_flip_ns, 0, 3, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->btn_flip_ew = gtk_toggle_button_new_with_label("Flip E/W");
  gtk_table_attach(GTK_TABLE(box_content), objs->btn_flip_ew, 3, 6, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->cmb_grid_type = gtk_combo_box_new();
  GtkListStore *gridstore = gtk_list_store_new(GRIDSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_STRING);
  GtkTreeIter iter;
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_NONE, GRIDSTORE_NAME, "None", -1);
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_IMAGE, GRIDSTORE_NAME, "Image", -1);
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_VIEW, GRIDSTORE_NAME, "View", -1);
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_EQUAT, GRIDSTORE_NAME, "Equatorial", -1);
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cmb_grid_type), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(cmb_grid_type), renderer, "text", GRIDSTORE_NAME);
  gtk_combo_box_set_model(GTK_COMBO_BOX(cmb_grid_type), GTK_TREE_MODEL(gridstore));
  gtk_table_attach(GTK_TABLE(box_content), objs->cmb_grid_type, 0, 2, 2, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("X scale"), 2, 3, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->scl_grid_x = gtk_hscale_new_with_range(0.0, 100.0, 1.0);
  gtk_scale_set_draw_value(GTK_SCALE(scl_grid_x), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_grid_x), GTK_POS_RIGHT);
  gtk_table_attach(GTK_TABLE(box_content), scl_grid_x, 3, 6, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Y scale"), 2, 3, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->scl_grid_y = gtk_hscale_new_with_range(0.0, 100.0, 1.0);
  gtk_scale_set_draw_value(GTK_SCALE(scl_grid_y), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_grid_y), GTK_POS_RIGHT);
  gtk_table_attach(GTK_TABLE(box_content), scl_grid_y, 3, 6, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  objs->cmb_lut = gtk_combo_box_new();
  gtk_widget_set_sensitive(cmb_lut, FALSE);
  gtk_table_attach(GTK_TABLE(box_content), cmb_lut, 0, 2, 5, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Faint"), 2, 3, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->scl_faint = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_scale_set_draw_value(GTK_SCALE(scl_faint), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_faint), GTK_POS_RIGHT);
  gtk_table_attach(GTK_TABLE(box_content), scl_faint, 3, 6, 5, 6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Bright"), 2, 3, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  objs->scl_bright = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_range_set_value(GTK_RANGE(scl_bright), objs->orig_bright);
  gtk_scale_set_draw_value(GTK_SCALE(scl_bright), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_bright), GTK_POS_RIGHT);
  gtk_table_attach(GTK_TABLE(box_content), scl_bright, 3, 6, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
}

static void class_init(ViewParamDialogClass *klass)
{
  parent_class = g_type_class_peek_parent(klass);
  G_OBJECT_CLASS(klass)->dispose = instance_dispose;
}

static void instance_dispose(GObject *view_param_dialog)
{
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(view_param_dialog);
  if (objs->imgdisp != NULL)
  {
    g_object_unref(objs->imgdisp);
    objs->imgdisp = NULL;
  }
  if (objs->orig_lut != NULL)
  {
    g_object_unref(objs->orig_lut);
    objs->orig_lut = NULL;
  }
  if (parent_class != NULL)
    parent_class->dispose(view_param_dialog);
}

static void toggle_flip_ns(GtkWidget *btn_flip_ns, gpointer imgdisp)
{
  imgdisp_set_flip_ns(imgdisp, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_flip_ns)));
}

static void toggle_flip_ew(GtkWidget *btn_flip_ew, gpointer imgdisp)
{
  imgdisp_set_flip_ew(imgdisp, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_flip_ew)));
}

static void grid_type_change(GtkWidget *cmb_grid_type, gpointer imgdisp)
{
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX(cmb_grid_type),&iter))
  {
    act_log_error(act_log_msg("No valid grid selected."));
    return;
  }
  int grid_active = -1;
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_grid_type));
  gtk_tree_model_get(model, &iter, GRIDSTORE_TYPE, &grid_active, -1);
  guchar cur_grid_type;
  gfloat cur_spacing_x, cur_spacing_y;
  grid_get_cur(GTK_WIDGET(imgdisp), &cur_grid_type, &cur_spacing_x, &cur_spacing_y);;
  imgdisp_set_grid(GTK_WIDGET(imgdisp), grid_active, cur_spacing_x, cur_spacing_y);
}

static void grid_scale_x_change(GtkWidget *scl_grid_x, gpointer imgdisp)
{
  guchar cur_grid_type;
  gfloat cur_spacing_x, cur_spacing_y;
  grid_get_cur(GTK_WIDGET(imgdisp), &cur_grid_type, &cur_spacing_x, &cur_spacing_y);;
  imgdisp_set_grid(GTK_WIDGET(imgdisp), cur_grid_type, gtk_range_get_value(GTK_RANGE(scl_grid_x)), cur_spacing_y);
}

static void grid_scale_y_change(GtkWidget *scl_grid_y, gpointer imgdisp)
{
  guchar cur_grid_type;
  gfloat cur_spacing_x, cur_spacing_y;
  grid_get_cur(GTK_WIDGET(imgdisp), &cur_grid_type, &cur_spacing_x, &cur_spacing_y);;
  imgdisp_set_grid(GTK_WIDGET(imgdisp), cur_grid_type, cur_spacing_x, gtk_range_get_value(GTK_RANGE(scl_grid_y)));
}

static void grid_get_cur(GtkWidget *imgdisp, guchar *cur_grid_type, gfloat *cur_spacing_x, gfloat *cur_spacing_y)
{
  *cur_grid_type = imgdisp_get_grid_type(imgdisp);
  *cur_spacing_x = imgdisp_get_grid_spacing_x(imgdisp);
  *cur_spacing_y = imgdisp_get_grid_spacing_y(imgdisp);
}

static void faint_changed(GtkWidget *scl_faint, gpointer imgdisp)
{
  imgdisp_set_faint_lim(GTK_WIDGET(imgdisp), gtk_range_get_value(GTK_RANGE(scl_faint)));
}

static void bright_changed(GtkWidget *scl_bright, gpointer imgdisp)
{
  imgdisp_set_bright_lim(GTK_WIDGET(imgdisp), gtk_range_get_value(GTK_RANGE(scl_bright)));
}
