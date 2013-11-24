#ifndef __FORM_MAIN_H__
#define __FORM_MAIN_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtktable.h>
#include <act_ipc.h>
#include "dti_plc.h"

G_BEGIN_DECLS

#define FORM_MAIN_TYPE              (form_main_get_type())
#define FORM_MAIN(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), FORM_MAIN_TYPE, FormMain))
#define FORM_MAIN_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), FORM_MAIN_TYPE, FormMainClass))
#define IS_FORM_MAIN(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), FORM_MAIN_TYPE))
#define IS_FORM_MAIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), FORM_MAIN_TYPE))

typedef struct _FormMain       FormMain;
typedef struct _FormMainClass  FormMainClass;

struct _FormMain
{
  GtkTable parent;
  
  GtkWidget *domeshutter, *dropout, *domemove;
  GtkWidget *telmove, *dtimisc;
  GtkWidget *acqmir, *filter, *aperture, *instrshutt;
  
  gpointer dti_plc;
  
  guint proc_seq_num;
  struct act_msg cur_msg;
};

struct _FormMainClass
{
  GtkTableClass parent_class;
};

GType form_main_get_type (void);
GtkWidget *form_main_new (void);
void form_main_process_msg (GtkWidget *form_main, struct act_msg *msg);

G_END_DECLS

#endif   /* __FORM_MAIN_H__ */
