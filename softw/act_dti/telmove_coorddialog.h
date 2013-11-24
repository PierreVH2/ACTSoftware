#ifndef __TELMOVE_COORDDIALOG_H__
#define __TELMOVE_COORDDIALOG_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_motor.h"

G_BEGIN_DECLS

#define TELMOVE_COORDDIALOG_TYPE              (telmove_get_type())
#define TELMOVE_COORDDIALOG(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), TELMOVE_COORDDIALOG_TYPE, TelmoveCoorddialog))
#define TELMOVE_COORDDIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TELMOVE_COORDDIALOG_TYPE, TelmoveCoorddialogClass))
#define IS_TELMOVE_COORDDIALOG(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), TELMOVE_COORDDIALOG_TYPE))
#define IS_TELMOVE_COORDDIALOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TELMOVE_COORDDIALOG_TYPE))

typedef struct _TelmoveCoorddialog       TelmoveCoorddialog;
typedef struct _TelmoveCoorddialogClass  TelmoveCoorddialogClass;

struct _TelmoveCoorddialog
{
  GtkDialog dialog;
  
  GtkWidget *box_content;
  GtkWidget *lbl_haralabel, *lbl_declabel;
  GtkWidget *spn_hara_h, *spn_hara_m, *spn_hara_s, *btn_harasign;
  GtkWidget *spn_dec_d, *spn_dec_am, *spn_dec_as, *btn_decsign;
  
  guchar hara_sign_pos, dec_sign_pos;
  gdouble sidt_h;
};

struct _TelmoveCoorddialogClass
{
  GtkDialogClass parent_class;
};

GType telmove_get_type (void);
GtkWidget *telmove_coorddialog_new(const gchar *title, GtkWidget *parent, gdouble sidt_h, GActTelcoord *cur_coord);
GActTelcoord *telmove_coorddialog_get_coord(GtkWidget *coorddialog, gdouble sidt_h);

G_END_DECLS

#endif   /* __TELMOVE_H__ */