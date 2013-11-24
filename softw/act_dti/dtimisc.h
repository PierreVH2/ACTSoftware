#ifndef __DTIMISC_H__
#define __DTIMISC_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define DTIMISC_TYPE              (dtimisc_get_type())
#define DTIMISC(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), DTIMISC_TYPE, Dtimisc))
#define DTIMISC_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DTIMISC_TYPE, DtimiscClass))
#define IS_DTIMISC(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DTIMISC_TYPE))
#define IS_DTIMISC_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DTIMISC_TYPE))

typedef struct _Dtimisc           Dtimisc;
typedef struct _DtimiscClass      DtimiscClass;

struct _Dtimisc
{
  GtkFrame parent;
  
  GtkWidget *box, *btn_focus, *btn_eht;
  GtkWidget *evb_plccomm, *evb_trapdoor, *evb_power, *evb_watchdog;
  GtkWidget *focusdialog, *ehtdialog;
  gboolean plccomm_ok, trapdoor_open, power_fail, watchdog_trip;
  guchar focus_stat;
  gint focus_pos;
  guchar eht_mode;
  gint focus_fail_to_id, eht_timer_trem, eht_timer_to_id;
};

struct _DtimiscClass
{
  GtkFrameClass parent_class;
};

GType dtimisc_get_type (void);
GtkWidget *dtimisc_new (gboolean plccomm_ok, gboolean watchdog_trip, gboolean power_fail, gboolean trapdoor_open, guchar eht_mode, guchar focus_stat, gint focus_pos);
void dtimisc_update_plccomm(GtkWidget *dtimisc, gboolean new_plccomm_ok);
void dtimisc_update_watchdog(GtkWidget *dtimisc, gboolean new_watchdog_trip);
void dtimisc_update_power(GtkWidget *dtimisc, gboolean new_power_fail);
void dtimisc_update_trapdoor(GtkWidget *dtimisc, gboolean new_trapdoor_open);
void dtimisc_update_eht(GtkWidget *dtimisc, guchar new_eht_mode);
void dtimisc_update_focus_stat(GtkWidget *dtimisc, guchar new_focus_stat);
void dtimisc_update_focus_pos(GtkWidget *dtimisc, gint new_focus_pos);
void dtimisc_process_msg(GtkWidget *dtimisc, DtiMsg *msg);

G_END_DECLS

#endif   /* __DTIMISC_H__ */
