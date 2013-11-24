#ifndef __DOMEMOVE_H__
#define __DOMEMOVE_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define DOMEMOVE_TYPE              (domemove_get_type())
#define DOMEMOVE(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), DOMEMOVE_TYPE, Domemove))
#define DOMEMOVE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DOMEMOVE_TYPE, DomemoveClass))
#define IS_DOMEMOVE(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DOMEMOVE_TYPE))
#define IS_DOMEMOVE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DOMEMOVE_TYPE))

typedef struct _Domemove       Domemove;
typedef struct _DomemoveClass  DomemoveClass;

struct _Domemove
{
  GtkFrame frame;
  GtkWidget *box, *btn_left, *btn_right, *btn_auto, *evb_azm, *lbl_azm;
  gfloat azm_goal, azm_cur;
  gboolean moving, azm_auto;
  gint fail_to_id;
  DtiMsg *pending_msg;
};

struct _DomemoveClass
{
  GtkFrameClass parent_class;
};

GType domemove_get_type (void);
GtkWidget *domemove_new (gboolean dome_moving, gfloat dome_azm);
void domemove_update_moving (GtkWidget *domemove, gboolean new_dome_moving);
void domemove_update_azm (GtkWidget *domemove, gfloat new_azm);
void domemove_process_msg(GtkWidget *domemove, DtiMsg *msg);
void domemove_park(GtkWidget *domemove);

G_END_DECLS

#endif   /* __DOMEMOVE_H__ */
