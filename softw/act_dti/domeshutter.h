#ifndef __DOMESHUTTER_H__
#define __DOMESHUTTER_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include <act_ipc.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define DOMESHUTTER_TYPE              (domeshutter_get_type())
#define DOMESHUTTER(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), DOMESHUTTER_TYPE, Domeshutter))
#define DOMESHUTTER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DOMESHUTTER_TYPE, DomeshutterClass))
#define IS_DOMESHUTTER(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DOMESHUTTER_TYPE))
#define IS_DOMESHUTTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DOMESHUTTER_TYPE))

typedef struct _Domeshutter       Domeshutter;
typedef struct _DomeshutterClass  DomeshutterClass;

struct _Domeshutter
{
  GtkFrame parent;
  
  GtkWidget *box, *btn_open, *btn_close;
  guchar dshutt_cur, dshutt_goal;
  gboolean weath_ok, sun_alt_ok, locked;
  gint fail_to_id, env_to_id;
  DtiMsg *pending_msg;
};

struct _DomeshutterClass
{
  GtkFrameClass parent_class;
};

GType domeshutter_get_type (void);
GtkWidget *domeshutter_new (guchar dshutt_stat);
void domeshutter_update (GtkWidget *domeshutter, guchar new_dshutt_stat);
void domeshutter_process_msg (GtkWidget *domeshutter, DtiMsg *msg);
void domeshutter_set_lock (GtkWidget *domeshutter, gboolean lock_on);

G_END_DECLS

#endif   /* __DOMESHUTTER_H__ */
