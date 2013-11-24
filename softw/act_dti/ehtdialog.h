#ifndef __EHTDIALOG_H__
#define __EHTDIALOG_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkdialog.h>
#include <act_ipc.h>

G_BEGIN_DECLS

#define EHTDIALOG_TYPE              (ehtdialog_get_type())
#define EHTDIALOG(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), EHTDIALOG_TYPE, EHTdialog))
#define EHTDIALOG_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), EHTDIALOG_TYPE, EHTdialogClass))
#define IS_EHTDIALOG(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), EHTDIALOG_TYPE))
#define IS_EHTDIALOG_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), EHTDIALOG_TYPE))

typedef struct _EHTdialog           EHTdialog;
typedef struct _EHTdialogClass      EHTdialogClass;

struct _EHTdialog
{
  GtkDialog parent;
  
  GtkWidget *box, *btn_off, *btn_high, *lbl_stab_time;
  guchar eht_stat;
  gint stab_time;
};

struct _EHTdialogClass
{
  GtkDialogClass parent_class;
};

GType ehtdialog_get_type (void);
GtkWidget *ehtdialog_new (GtkWidget *parent, guchar eht_stat, gint stab_time);
void ehtdialog_update (GtkWidget *ehtdialog, guchar eht_stat, gint stab_time);

G_END_DECLS

#endif   /* __EHTDIALOG_H__ */
