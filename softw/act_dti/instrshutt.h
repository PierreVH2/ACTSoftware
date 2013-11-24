#ifndef __INSTRSHUTT_H__
#define __INSTRSHUTT_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include "dti_net.h"

G_BEGIN_DECLS

#define INSTRSHUTT_TYPE            (instrshutt_get_type ())
#define INSTRSHUTT(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), INSTRSHUTT_TYPE, Instrshutt))
#define INSTRSHUTT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), INSTRSHUTT_TYPE, InstrshuttClass))
#define IS_INSTRSHUTT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), INSTRSHUTT_TYPE))
#define IS_INSTRSHUTT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), INSTRSHUTT_TYPE))


typedef struct _Instrshutt       Instrshutt;
typedef struct _InstrshuttClass  InstrshuttClass;

struct _Instrshutt
{
  /// Base widget for this composite widget
  GtkFrame frame;
  
  /// Widgets contained within this widget
  GtkWidget *box, *btn_open, *btn_close;

  /// Current status of instrument shutter
  gboolean instrshutt_open, instrshutt_goal;
  /// Message that is currently being processed
  DtiMsg *pending_msg;
  /// Countdown timer to allow instrument shutter to power up after closing
  guint timer_sec;
  /// Source ID for shutter timer timeout function
  gint powerup_to_id;
  /// Source ID for shutter open/close timer
  gint fail_to_id;
};

struct _InstrshuttClass
{
  GtkFrameClass parent_class;
};

GType instrshutt_get_type (void);
GtkWidget* instrshutt_new (gboolean instrshutt_open);
void instrshutt_update (GtkWidget *instrshutt, gboolean new_instrshutt_open);
void instrshutt_process_msg(GtkWidget *instrshutt, DtiMsg *msg);

G_END_DECLS

#endif /* __INSTRSHUTT_H__ */
