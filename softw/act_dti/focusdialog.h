#ifndef __FOCUSDIALOG_H__
#define __FOCUSDIALOG_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkdialog.h>
#include <act_ipc.h>

G_BEGIN_DECLS

#define FOCUSDIALOG_TYPE              (focusdialog_get_type())
#define FOCUSDIALOG(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), FOCUSDIALOG_TYPE, Focusdialog))
#define FOCUSDIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), FOCUSDIALOG_TYPE, FocusdialogClass))
#define IS_FOCUSDIALOG(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), FOCUSDIALOG_TYPE))
#define IS_FOCUSDIALOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), FOCUSDIALOG_TYPE))

typedef struct _Focusdialog           Focusdialog;
typedef struct _FocusdialogClass      FocusdialogClass;

struct _Focusdialog
{
  GtkDialog parent;
  
  GtkWidget *box, *lbl_focus_pos, *evb_focus_stat, *lbl_focus_stat;
  GtkWidget *spn_focus_pos, *btn_focus_go, *btn_focus_reset;
  guchar focus_stat;
  gshort focus_pos;
};

struct _FocusdialogClass
{
  GtkDialogClass parent_class;
};

GType focusdialog_get_type (void);
GtkWidget *focusdialog_new (GtkWidget *parent, guchar focus_stat, gshort focus_pos);
void focusdialog_update (GtkWidget *focusdialog, guchar focus_stat, gshort focus_pos);

G_END_DECLS

#endif   /* __FOCUSDIALOG_H__ */
