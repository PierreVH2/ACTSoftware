#ifndef __TELMOVE_H__
#define __TELMOVE_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include <act_timecoord.h>
#include <act_plc.h>
#include "dti_net.h"
#include "dti_motor.h"

G_BEGIN_DECLS

#define TELMOVE_TYPE              (telmove_get_type())
#define TELMOVE(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), TELMOVE_TYPE, Telmove))
#define TELMOVE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), TELMOVE_TYPE, TelmoveClass))
#define IS_TELMOVE(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), TELMOVE_TYPE))
#define IS_TELMOVE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), TELMOVE_TYPE))

typedef struct _Telmove       Telmove;
typedef struct _TelmoveClass  TelmoveClass;

struct _Telmove
{
  GtkFrame frame;
  
  Dtimotor *dti_motor;
  
  GtkWidget *box;
  GtkWidget *btn_goto, *btn_cancel, *btn_track, *btn_confirm_coord;
  GtkWidget *btn_speed_slew, *btn_speed_set, *btn_speed_guide;
  GtkWidget *btn_moveN, *btn_moveS, *btn_moveE, *btn_moveW, *btn_emgny_stop;
  GtkWidget *lbl_hara_label, *lbl_dec_label, *lbl_hara, *lbl_dec;
  GtkWidget *evb_stat, *lbl_stat;
  
  gdouble sidt_h;
  GTimer *sidt_timer;
  gint fail_to_id;
  DtiMsg *pending_msg;
};

struct _TelmoveClass
{
  GtkFrameClass parent_class;
};

GType telmove_get_type (void);
GtkWidget *telmove_new (void);
void telmove_process_msg (GtkWidget *telmove, DtiMsg *msg);

G_END_DECLS

#endif   /* __TELMOVE_H__ */
