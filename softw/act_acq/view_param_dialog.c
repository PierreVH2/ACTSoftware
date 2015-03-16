#include <gtk/gtk.h>
#include "view_param_dialog.h"
#include "imgdisp.h"

#ifndef TABLE_PADDING
#define TABLE_PADDING 3
#endif

enum
{
  GRIDSTORE_TYPE,
  GRIDSTORE_NAME,
  GRIDSTORE_NUM_COLS
};

static void instance_init(GObject *view_param_dialog);
static void class_init(ViewParamDialogClass *klass);
static void instance_dispose(GObject *view_param_dialog);
static void view_param_dialog_cancel(GtkWidget *imgdisp);

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
      (GClassInitFunc) view_param_class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (ViewParamDialog),
      0,
      (GInstanceInitFunc) view_param_instance_init,
      NULL
    };
    
    view_param_type = g_type_register_static (G_TYPE_OBJECT, "ViewParamDialog", &view_param_info, 0);
  }
  
  return view_param_type;
}

GObject *view_param_dialog_new(GtkWidget *parent, GtkWidget *imgdisp)
{
  if (!IS_IMGDISP(imgdisp))
  {
    act_log_error(act_log_msg("Invalid image display object specified."));
    return NULL;
  }
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(g_object_new(view_param_dialog_type(), NULL));
  
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
  
  objs->dialog = gtk_dialog_new_with_buttons("Change View Parameters", parent, GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
  GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(objs->dialog));
  GtkWidget *box_content = gtk_table_new(7,6,TRUE);
  
  GtkWidget* btn_flip_ns = gtk_toggle_button_new_with_label("Flip N/S");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_flip_ns), objs->orig_flip_ns);
  gtk_table_attach(GTK_TABLE(box_content), btn_flip_ns, 0, 3, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect(G_OBJECT(btn_flip_ns),"toggled",G_CALLBACK(toggle_flip_ns),imgdisp);
  GtkWidget* btn_flip_ew = gtk_toggle_button_new_with_label("Flip E/W");
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_flip_ew), objs->orig_flip_ew);
  gtk_table_attach(GTK_TABLE(box_content), btn_flip_ns, 3, 6, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect(G_OBJECT(btn_flip_ew),"toggled",G_CALLBACK(toggle_flip_ew),imgdisp);
  
  gtk_table_attach(GTK_TABLE(box_content), gtk_hseparator_new(), 0, 6, 1, 2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *cmb_grid_type = gtk_combo_box_new();
  GtkListStore *gridstore = gtk_list_store_new(GRIDSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_STRING);
  GtkTreeIter iter;
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_NONE, GRIDSTORE_NAME, "None");
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_IMAGE, GRIDSTORE_NAME, "Image");
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_VIEW, GRIDSTORE_NAME, "View");
  gtk_list_store_append(gridstore, &iter);
  gtk_list_store_set(gridstore, &iter, GRIDSTORE_TYPE, IMGDISP_GRID_EQUAT, GRIDSTORE_NAME, "Equatorial");
  GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cmb_grid_type), renderer, TRUE);
  gtk_cell_layout_add_attribute(GTK_CELL_LAYOUT(cmb_grid_type), renderer, "text", GRIDSTORE_NAME);
  gtk_combo_box_set_model(GTK_COMBO_BOX(cmb_grid_type), gridstore);
  gtk_table_attach(GTK_TABLE(box_content), cmb_grid_type, 0, 2, 2, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  g_signal_connect(G_OBJECT(cmb_grid_type),"changed",G_CALLBACK(grid_type_change),imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("X scale"), 2, 3, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_grid_x = gtk_hscale_new_with_range(0.0, 100.0, 1.0);
  gtk_range_set_value(GTK_RANGE(scl_grid_x), objs->orig_grid_spacing_x);
  gtk_scale_set_draw_value(GTK_SCALE(scl_grid_x), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_grid_x), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_grid_x),"value-changed",G_CALLBACK(grid_scale_x_changed),imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_grid_x, 3, 6, 2, 3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Y scale"), 2, 3, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_grid_y = gtk_hscale_new_with_range(0.0, 100.0, 1.0);
  gtk_range_set_value(GTK_RANGE(scl_grid_y), objs->orig_grid_spacing_y);
  gtk_scale_set_draw_value(GTK_SCALE(scl_grid_y), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_grid_y), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_grid_y),"value-changed",G_CALLBACK(grid_scale_y_changed),imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_grid_x, 3, 6, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
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
  gtk_table_attach(GTK_TABLE(box_content), scl_faint, 3, 6, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  gtk_table_attach(GTK_TABLE(box_content), gtk_label_new("Bright"), 2, 3, 6, 7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *scl_bright = gtk_hscale_new_with_range(0.0, 1.0, 1./255.);
  gtk_range_set_value(GTK_RANGE(scl_bright), objs->orig_bright);
  gtk_scale_set_draw_value(GTK_SCALE(scl_bright), TRUE);
  gtk_scale_set_value_pos (GTK_SCALE(scl_bright), GTK_POS_RIGHT);
  g_signal_connect(G_OBJECT(scl_bright), "value-changed", G_CALLBACK(bright_changed), imgdisp);
  gtk_table_attach(GTK_TABLE(box_content), scl_bright, 3, 6, 3, 4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  return G_OBJECT(objs);
}

void view_param_dialog_cancel(GObject *view_param_dialog)
{
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(view_param_dialog);
  imgdisp_set_flip_ns(objs->imgdisp, objs->orig_flip_ns);
  imgdisp_set_flip_ew(objs->imgdisp, objs->orig_flip_ew);
  imgdisp_set_bright_lim(objs->imgdisp, objs->orig_bright);
  imgdisp_set_faint_lim(objs->imgdisp, objs->orig_faint);
  imgdisp_set_grid(objs->imgdisp, objs->orig_grid_type, objs->orig_grid_spacing_x, objs->orig_grid_spacing_y);
}

GtkWidget *view_param_dialog_get_dialog(GObject *view_param_dialog)
{
  return VIEW_PARAM_DIALOG(view_param_dialog)->dialog;
}

static void instance_init(GObject *view_param_dialog)
{
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(view_param_dialog);
  objs->dialog = NULL;
  objs->imgdisp = NULL;
  objs->orig_flip_ns = objs->orig_flip_ew = FALSE;
  objs->orig_faint = objs->orig_bright = 0.0;
  objs->orig_lut = NULL;
  objs->orig_grid_type = 0;
  objs->orig_grid_spacing_x = objs->orig_grid_spacing_y = 0.0;
}

static void class_init(ImglutClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = imglut_instance_dispose;
}

static void instance_dispose(GObject *view_param_dialog)
{
  ViewParamDialog *objs = VIEW_PARAM_DIALOG(view_param_dialog);
  if (objs->dialog != NULL)
  {
    g_object_unref(objs->dialog);
    objs->dialog = NULL;
  }
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
}

void toggle_flip_ns(GtkWidget *btn_flip_ns, gpointer imgdisp)
{
  imgdisp_set_flip_ns(imgdisp, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_flip_ns)));
}

void toggle_flip_ew(GtkWidget *btn_flip_ew, gpointer imgdisp)
{
  imgdisp_set_flip_ew(imgdisp, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_flip_ew)));
}

void grid_type_change(GtkWidget *cmb_grid_type, gpointer imgdisp)
{
  imgdisp_set_grid(GTK_WIDGET(imgdisp), guchar new_grid, imgdisp_get_grid_spacing_x(imgdisp), imgdisp_get_grid_spacing_y(imgdisp));
}

void grid_scale_x_change(GtkWidget *scl_grid_x, gpointer imgdisp)
{
  imgdisp_set_grid(GTK_WIDGET(imgdisp), imgdisp_get_grid_type(GtkWidget *imgdisp), gtk_range_get_value(GTK_RANGE(scl_grid_x)), imgdisp_get_grid_spacing_y(imgdisp));
}

void grid_get_params(GtkWidget *imgdisp, guchar *cur_type, gfloat *cur_spacing_x, gfloat *cur_spacing_y)
{
}


void faint_changed(GtkWidget *scl_faint, gpointer imgdisp)
{
  imgdisp_set_faint_lim(GTK_WIDGET(imgdisp), gtk_range_get_value(GTK_RANGE(scl_faint)));
}

void bright_changed(GtkWidget *scl_bright, gpointer imgdisp)
{
  imgdisp_set_bright_lim(GTK_WIDGET(imgdisp), gtk_range_get_value(GTK_RANGE(scl_bright)));
}





g_signal_connect(G_OBJECT(cmb_grid_type),"changed",G_CALLBACK(grid_type_change),imgdisp);
g_signal_connect(G_OBJECT(scl_grid_x),"value-changed",G_CALLBACK(grid_scale_x_change),imgdisp);
g_signal_connect(G_OBJECT(scl_grid_y),"value-changed",G_CALLBACK(grid_scale_y_change),imgdisp);




