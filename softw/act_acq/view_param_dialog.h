#ifndef __VIEW_PARAM_DIALOG_H__
#define __VIEW_PARAM_DIALOG_H__

/** TODO: Re-implement this widget as a derivative of GtkDialog. This needs a bit more work than simply deriving from GObject, so the less-clean GObject route was selected instead */

#include <glib.h>
#include <glib-object.h>
#include "imgdisp.h"

G_BEGIN_DECLS

#define VIEW_PARAM_DIALOG_TYPE              (view_param_dialog_type())
#define VIEW_PARAM_DIALOG(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), VIEW_PARAM_DIALOG_TYPE, ViewParamDialog))
#define VIEW_PARAM_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), VIEW_PARAM_DIALOG_TYPE, ViewParamDialogClass))
#define IS_VIEW_PARAM_DIALOG(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), VIEW_PARAM_DIALOG_TYPE))
#define IS_VIEW_PARAM_DIALOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), VIEW_PARAM_DIALOG_TYPE))

typedef struct _ViewParamDialog       ViewParamDialog;
typedef struct _ViewParamDialogClass  ViewParamDialogClass;

struct _ViewParamDialog
{
  GtkDialog parent;
  GtkWidget *imgdisp;
  
  gboolean orig_flip_ns, orig_flip_ew;
  gfloat orig_bright, orig_faint;
  Imglut *orig_lut;
  guchar orig_grid_type;
  gfloat orig_grid_spacing_x, orig_grid_spacing_y;
  
  GtkWidget* btn_flip_ns, *btn_flip_ew;
  GtkWidget *cmb_grid_type;
  GtkWidget *scl_grid_x, *scl_grid_y;
  GtkWidget *cmb_lut;
  GtkWidget *scl_faint, *scl_bright
};

struct _ViewParamDialogClass
{
  GtkDialogClass parent_class;
};

GType view_param_dialog_type(void);
GtkWidget *view_param_dialog_new(GtkWidget *parent, GtkWidget *imgdisp);
void view_param_dialog_revert(GtkWidget *view_param_dialog);

#endif   /* __VIEW_PARAM_DIALOG_H__ */