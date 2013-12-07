#ifndef __DROPOUT_H__
#define __DROPOUT_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include <act_ipc.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define DROPOUT_TYPE              (dropout_get_type())
#define DROPOUT(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), DROPOUT_TYPE, Dropout))
#define DROPOUT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DROPOUT_TYPE, DropoutClass))
#define IS_DROPOUT(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DROPOUT_TYPE))
#define IS_DROPOUT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DROPOUT_TYPE))

typedef struct _Dropout       Dropout;
typedef struct _DropoutClass  DropoutClass;

struct _Dropout
{
  GtkFrame parent;
  
  GtkWidget *box, *btn_open, *btn_close;
  guchar dropout_cur, dropout_goal;
  gboolean weath_ok, sun_alt_ok, locked;
  gint fail_to_id, env_to_id;
  DtiMsg *pending_msg;
};

struct _DropoutClass
{
  GtkFrameClass parent_class;
};

GType dropout_get_type (void);
GtkWidget *dropout_new (guchar dropout_stat);
void dropout_update (GtkWidget *dropout, guchar new_dropout_stat);
void dropout_process_msg(GtkWidget *dropout, DtiMsg *msg);
void dropout_set_lock(GtkWidget *dropout, gboolean lock_on);

G_END_DECLS

#endif   /* __DROPOUT_H__ */
