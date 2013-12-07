#ifndef __ACQMIR_H__
#define __ACQMIR_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define ACQMIR_TYPE                (acqmir_get_type())
#define ACQMIR(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), ACQMIR_TYPE, Acqmir))
#define ACQMIR_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), ACQMIR_TYPE, AcqmirClass))
#define IS_ACQMIR(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), ACQMIR_TYPE))
#define IS_ACQMIR_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), ACQMIR_TYPE))

typedef struct _Acqmir       Acqmir;
typedef struct _AcqmirClass  AcqmirClass;

struct _Acqmir
{
  GtkFrame parent;
  
  GtkWidget *box, *btn_view, *btn_meas;
  DtiMsg *pending_msg;
  guchar acqmir_cur, acqmir_goal;
  gint fail_to_id;
};

struct _AcqmirClass
{
  GtkFrameClass parent_class;
};

GType acqmir_get_type (void);
GtkWidget *acqmir_new (guchar cur_acqmir);
void acqmir_update (GtkWidget *acqmir, guchar new_acqmir);
void acqmir_process_msg (GtkWidget *acqmir, DtiMsg *msg);

G_END_DECLS

#endif   /* __ACQMIR_H__ */
