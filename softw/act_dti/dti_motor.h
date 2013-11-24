#ifndef __DTI_MOTOR_H__
#define __DTI_MOTOR_H__

#include <glib.h>
#include <glib-object.h>
#include <act_timecoord.h>

G_BEGIN_DECLS

#define GACT_TELCOORD_TYPE                (gact_telcoord_get_type())
#define GACT_TELCOORD(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), GACT_TELCOORD_TYPE, GActTelcoord))
#define GACT_TELCOORD_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), GACT_TELCOORD_TYPE, GActTelcoordClass))
#define IS_GACT_TELCOORD(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), GACT_TELCOORD_TYPE))
#define IS_GACT_TELCOORD_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), GACT_TELCOORD_TYPE))

#define GACT_TELGOTO_TYPE                 (gact_telgoto_get_type())
#define GACT_TELGOTO(objs)                (G_TYPE_CHECK_INSTANCE_CAST ((objs), GACT_TELGOTO_TYPE, GActTelgoto))
#define GACT_TELGOTO_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GACT_TELGOTO_TYPE, GActTelgotoClass))
#define IS_GACT_TELGOTO(objs)             (G_TYPE_CHECK_INSTANCE_TYPE ((objs), GACT_TELGOTO_TYPE))
#define IS_GACT_TELGOTO_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GACT_TELGOTO_TYPE))

#define DTIMOTOR_TYPE              (dti_motor_get_type())
#define DTIMOTOR(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), DTIMOTOR_TYPE, Dtimotor))
#define DTIMOTOR_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), DTIMOTOR_TYPE, DtimotorClass))
#define IS_DTIMOTOR(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DTIMOTOR_TYPE))
#define IS_DTIMOTOR_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), DTIMOTOR_TYPE))

enum
{
  DTIMOTOR_SPEED_GUIDE = 1,
  DTIMOTOR_SPEED_SET,
  DTIMOTOR_SPEED_SLEW,
  DTIMOTOR_SPEED_INVAL
};

enum
{
  DTIMOTOR_DIR_NORTH = 1,
  DTIMOTOR_DIR_NORTHWEST,
  DTIMOTOR_DIR_WEST,
  DTIMOTOR_DIR_SOUTHWEST,
  DTIMOTOR_DIR_SOUTH,
  DTIMOTOR_DIR_SOUTHEAST,
  DTIMOTOR_DIR_EAST,
  DTIMOTOR_DIR_NORTHEAST,
  DTIMOTOR_DIR_INVAL
};

typedef struct _GActTelcoord       GActTelcoord;
typedef struct _GActTelcoordClass  GActTelcoordClass;

struct _GActTelcoord
{
  GObject parent;
  struct hastruct ha;
  struct decstruct dec;
};

struct _GActTelcoordClass
{
  GObjectClass parent_class;
};

typedef struct _GActTelgoto       GActTelgoto;
typedef struct _GActTelgotoClass  GActTelgotoClass;

struct _GActTelgoto
{
  GObject parent;
  struct hastruct ha;
  struct decstruct dec;
  gboolean is_sidereal;
  guchar speed;
};

struct _GActTelgotoClass
{
  GObjectClass parent_class;
};

typedef struct _Dtimotor       Dtimotor;
typedef struct _DtimotorClass  DtimotorClass;

struct _Dtimotor
{
  GObject parent;
  GIOChannel *motor_chan;
  guint coord_to_id, motor_watch_id;
  guchar cur_stat, cur_limits, cur_warn;
  gdouble lim_W_h, lim_E_h, lim_N_d, lim_S_d, lim_alt_d;
  GActTelcoord *cur_coord;
};

struct _DtimotorClass
{
  GObjectClass parent_class;
};

GType gact_telcoord_get_type (void);
GActTelcoord *gact_telcoord_new (struct hastruct *tel_ha, struct decstruct *tel_dec);
void gact_telcoord_set (GActTelcoord *objs, struct hastruct *tel_ha, struct decstruct *tel_dec);

GType gact_telgoto_get_type (void);
GActTelgoto *gact_telgoto_new (struct hastruct *ha, struct decstruct *dec, guchar speed, gboolean is_sidereal);

GType dti_motor_get_type (void);
Dtimotor *dti_motor_new (void);
void dti_motor_set_soft_limits (Dtimotor *objs, struct hastruct *lim_W, struct hastruct *lim_E, struct decstruct *lim_N, struct decstruct *lim_S, struct altstruct *lim_alt);
gboolean dti_motor_stat_init (Dtimotor *objs);
gboolean dti_motor_stat_tracking (Dtimotor *objs);
gboolean dti_motor_stat_moving (Dtimotor *objs);
gboolean dti_motor_stat_emgny_stop (Dtimotor *objs);
gboolean dti_motor_limit_reached (Dtimotor *objs);
gboolean dti_motor_lim_N (Dtimotor *objs);
gboolean dti_motor_lim_S (Dtimotor *objs);
gboolean dti_motor_lim_E (Dtimotor *objs);
gboolean dti_motor_lim_W (Dtimotor *objs);
gboolean dti_motor_warn_N (Dtimotor *objs);
gboolean dti_motor_warn_S (Dtimotor *objs);
gboolean dti_motor_warn_E (Dtimotor *objs);
gboolean dti_motor_warn_W (Dtimotor *objs);
GActTelcoord *dti_motor_get_coord (Dtimotor *objs);
GActTelcoord *dti_motor_get_coord_raw (Dtimotor *objs);
gint dti_motor_move_card (Dtimotor *objs, guchar dir, guchar speed);
gint dti_motor_goto (Dtimotor *objs, GActTelgoto *gotocmd);
void dti_motor_stop (Dtimotor *objs);
void dti_motor_emgncy_stop (Dtimotor *objs, gboolean stop_on);
gint dti_motor_init (Dtimotor *objs);
gint dti_motor_set_tracking (Dtimotor *objs, gboolean tracking_on);

G_END_DECLS

#endif   /* __DTI_MOTOR_H__ */
