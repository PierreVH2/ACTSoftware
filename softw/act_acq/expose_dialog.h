#ifndef __EXPOSE_DIALOG_H__
#define __EXPOSE_DIALOG_H__

#include <glib.h>
#include <glib-object.h>
#include "ccd_img.h"

G_BEGIN_DECLS

#define EXPOSE_DIALOG_TYPE              (expose_dialog_type())
#define EXPOSE_DIALOG(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), EXPOSE_DIALOG_TYPE, ExposeDialog))
#define EXPOSE_DIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), EXPOSE_DIALOG_TYPE, ExposeDialogClass))
#define IS_EXPOSE_DIALOG(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), EXPOSE_DIALOG_TYPE))
#define IS_EXPOSE_DIALOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), EXPOSE_DIALOG_TYPE))

typedef struct _ExposeDialog       ExposeDialog;
typedef struct _ExposeDialogClass  ExposeDialogClass;

struct _ExposeDialog
{
  GtkDialog parent;
  
  GtkWidget *cmb_img_type;
  GtkWidget *spn_win_start_x, *spn_win_start_y;
  GtkWidget *spn_win_width, *spn_win_height;
  GtkWidget *spn_prebin_x, *spn_prebin_y;
  GtkWidget *spn_exp_t_s, *spn_repetitions;
};

struct _ExposeDialogClass
{
  GtkDialogClass parent_class;
};

GType expose_dialog_type(void);
GtkWidget *expose_dialog_new(GtkWidget *parent, CcdCntrl *cntrl);
GtkWidget *expose_dialog_new_init(GtkWidget *parent, guchar img_type, guint start_x, guint start_y, guint win_width, guint win_height, guint prebin_x, guint prebin_y, gfloat exp_t, guint rpt);
guchar expose_dialog_get_image_type(GtkWidget *expose_dialog);
guint expose_dialog_get_win_start_x(GtkWidget *expose_dialog);
guint expose_dialog_get_win_start_y(GtkWidget *expose_dialog);
guint expose_dialog_get_win_width(GtkWidget *expose_dialog);
guint expose_dialog_get_win_height(GtkWidget *expose_dialog);
guint expose_dialog_get_prebin_x(GtkWidget *expose_dialog);
guint expose_dialog_get_prebin_y(GtkWidget *expose_dialog);
gfloat expose_dialog_get_exp_t(GtkWidget *expose_dialog);
guint expose_dialog_get_repetitions(GtkWidget *expose_dialog);
gboolean expose_dialog_set_image_type(GtkWidget *expose_dialog, guchar img_type);
void expose_dialog_set_win_start_x(GtkWidget *expose_dialog, guint start_x);
void expose_dialog_set_win_start_y(GtkWidget *expose_dialog, guint start_y);
void expose_dialog_set_win_width(GtkWidget *expose_dialog, guint win_width);
void expose_dialog_set_win_height(GtkWidget *expose_dialog, guint win_height);
void expose_dialog_set_prebin_x(GtkWidget *expose_dialog, guint prebin_x);
void expose_dialog_set_prebin_y(GtkWidget *expose_dialog, guint prebin_y);
void expose_dialog_set_exp_t(GtkWidget *expose_dialog, gfloat exp_t);
void expose_dialog_set_repetitions(GtkWidget *expose_dialog, guint rpt);

#endif   /* __EXPOSE_DIALOG_H__ */