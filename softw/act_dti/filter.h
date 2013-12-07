#ifndef __FILTER_H__
#define __FILTER_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define FILTER_TYPE              (filter_get_type())
#define FILTER(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), FILTER_TYPE, Filter))
#define FILTER_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), FILTER_TYPE, FilterClass))
#define IS_FILTER(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), FILTER_TYPE))
#define IS_FILTER_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), FILTER_TYPE))

typedef struct _Filter       Filter;
typedef struct _FilterClass  FilterClass;

struct _Filter
{
  GtkFrame frame;
  
  GtkWidget *box, *cmb_filtsel, *evb_filtdisp, *lbl_filtdisp;
  guchar filt_stat, filt_slot;
  gchar filt_goal;
  gint fail_to_id;
  DtiMsg *pending_msg;
};

struct _FilterClass
{
  GtkFrameClass parent_class;
};

GType filter_get_type (void);
GtkWidget *filter_new (guchar filt_stat, guchar filt_slot);
void filter_update_stat (GtkWidget *filter, guchar new_filt_stat);
void filter_update_slot (GtkWidget *filter, guchar new_filt_slot);
void filter_process_msg(GtkWidget *filter, DtiMsg *msg);

G_END_DECLS

#endif   /* __FILTER_H__ */
