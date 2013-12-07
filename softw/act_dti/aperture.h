#ifndef __APERTURE_H__
#define __APERTURE_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define APERTURE_TYPE              (aperture_get_type())
#define APERTURE(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), APERTURE_TYPE, Aperture))
#define APERTURE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), APERTURE_TYPE, ApertureClass))
#define IS_APERTURE(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), APERTURE_TYPE))
#define IS_APERTURE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), APERTURE_TYPE))

typedef struct _Aperture       Aperture;
typedef struct _ApertureClass  ApertureClass;

struct _Aperture
{
  GtkFrame frame;
  
  GtkWidget *box, *cmb_apersel, *evb_aperdisp, *lbl_aperdisp;
  guchar aper_stat, aper_slot;
  gchar aper_goal;
  gint fail_to_id;
  DtiMsg *pending_msg;
};

struct _ApertureClass
{
  GtkFrameClass parent_class;
};

GType aperture_get_type (void);
GtkWidget *aperture_new (guchar aper_stat, guchar aper_slot);
void aperture_update_stat (GtkWidget *aperture, guchar new_aper_stat);
void aperture_update_slot (GtkWidget *aperture, guchar new_aper_slot);
void aperture_process_msg(GtkWidget *aperture, DtiMsg *msg);

G_END_DECLS

#endif   /* __APERTURE_H__ */
