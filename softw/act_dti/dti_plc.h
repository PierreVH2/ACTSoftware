#ifndef __DTI_PLC_H__
#define __DTI_PLC_H__

#include <glib.h>
#include <glib-object.h>
#include <act_plc.h>

#define dti_plc_domeshutt_open(domeshutt_stat)     ((domeshutt_stat & DSHUTT_OPEN_MASK) > 0)
#define dti_plc_domeshutt_closed(domeshutt_stat)   ((domeshutt_stat & DSHUTT_CLOSED_MASK) > 0)
#define dti_plc_domeshutt_moving(domeshutt_stat)   ((domeshutt_stat & DSHUTT_MOVING_MASK) > 0)
#define dti_plc_dropout_open(dropout_stat)         ((dropout_stat & DSHUTT_OPEN_MASK) > 0)
#define dti_plc_dropout_closed(dropout_stat)       ((dropout_stat & DSHUTT_CLOSED_MASK) > 0)
#define dti_plc_dropout_moving(dropout_stat)       ((dropout_stat & DSHUTT_MOVING_MASK) > 0)
#define dti_plc_handset_move_N(handset_stat)       ((handset_stat & HS_DIR_NORTH_MASK) > 0)
#define dti_plc_handset_move_S(handset_stat)       ((handset_stat & HS_DIR_SOUTH_MASK) > 0)
#define dti_plc_handset_move_E(handset_stat)       ((handset_stat & HS_DIR_EAST_MASK) > 0)
#define dti_plc_handset_move_W(handset_stat)       ((handset_stat & HS_DIR_WEST_MASK) > 0)
#define dti_plc_handset_speed_slew(handset_stat)   ((handset_stat & HS_SPEED_SLEW_MASK) > 0)
#define dti_plc_handset_speed_set(handset_stat)    (((handset_stat & HS_SPEED_SLEW_MASK) == 0) && ((handset_stat & HS_SPEED_GUIDE_MASK) == 0))
#define dti_plc_handset_speed_guide(handset_stat)  ((handset_stat & HS_SPEED_GUIDE_MASK) > 0)
#define dti_plc_handset_focus_in(handset_stat)     ((handset_stat & HS_FOC_IN_MASK) > 0)
#define dti_plc_handset_focus_out(handset_stat)    ((handset_stat & HS_FOC_OUT_MASK) > 0)
#define dti_plc_focus_slot(focus_stat)             ((focus_stat & FOCUS_SLOT_MASK) > 0)
#define dti_plc_focus_ref(focus_stat)              ((focus_stat & FOCUS_REF_MASK) > 0)
#define dti_plc_focus_moving(focus_stat)           ((focus_stat & FOCUS_MOVING_MASK) > 0)
#define dti_plc_focus_stall(focus_stat)            ((focus_stat & FOCUS_STALL_MASK) > 0)
#define dti_plc_acqmir_view(acqmir_stat)           ((acqmir_stat & ACQMIR_VIEW_MASK) > 0)
#define dti_plc_acqmir_meas(acqmir_stat)           ((acqmir_stat & ACQMIR_MEAS_MASK) > 0)
#define dti_plc_acqmir_moving(acqmir_stat)         ((acqmir_stat & ACQMIR_MOVING_MASK) > 0)
#define dti_plc_aper_cent(aper_stat)               ((aper_stat & FILTAPER_CENT_MASK) > 0)
#define dti_plc_aper_init(aper_stat)               ((aper_stat & FILTAPER_INIT_MASK) > 0)
#define dti_plc_aper_moving(aper_stat)             ((aper_stat & FILTAPER_MOVING_MASK) > 0)
#define dti_plc_filt_cent(filt_stat)               ((filt_stat & FILTAPER_CENT_MASK) > 0)
#define dti_plc_filt_init(filt_stat)               ((filt_stat & FILTAPER_INIT_MASK) > 0)
#define dti_plc_filt_moving(filt_stat)             ((filt_stat & FILTAPER_MOVING_MASK) > 0)
#define dti_plc_eht_man_off(eht_stat)              ((eht_stat & EHT_MAN_OFF_MASK) > 0)
#define dti_plc_eht_low(eht_stat)                  ((eht_stat & EHT_LOW_MASK) > 0)
#define dti_plc_eht_high(eht_stat)                 ((eht_stat & EHT_HIGH_MASK) > 0)


G_BEGIN_DECLS

#define DTI_PLC_TYPE                            (dti_plc_get_type())
#define DTI_PLC(objs)                           (G_TYPE_CHECK_INSTANCE_CAST ((objs), DTI_PLC_TYPE, DtiPlc))
#define DTI_PLC_CLASS(klass)                    (G_TYPE_CHECK_CLASS_CAST ((klass), DTI_PLC_TYPE, DtiPlcClass))
#define IS_DTI_PLC(objs)                        (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DTI_PLC_TYPE))
#define IS_DTI_PLC_CLASS(klass)                 (G_TYPE_CHECK_CLASS_TYPE ((klass), DTI_PLC_TYPE))


typedef struct _DtiPlc       DtiPlc;
typedef struct _DtiPlcClass  DtiPlcClass;

struct _DtiPlc
{
  GObject parent;
  GIOChannel *plc_chan;
  gint plc_watch_id;
  gboolean plc_comm_ok;
  struct plc_status plc_stat;
};

struct _DtiPlcClass
{
  GObjectClass parent_class;
};


GType dti_plc_get_type (void);
DtiPlc *dti_plc_new (void);

gfloat dti_plc_get_dome_azm(gpointer dti_plc);
gboolean dti_plc_get_dome_moving(gpointer dti_plc);
guchar dti_plc_get_domeshutt_stat(gpointer dti_plc);
guchar dti_plc_get_dropout_stat(gpointer dti_plc);
guchar dti_plc_get_handset_stat(gpointer dti_plc);
gint dti_plc_get_focus_pos(gpointer dti_plc);
guchar dti_plc_get_focus_stat(gpointer dti_plc);
guchar dti_plc_get_acqmir_stat(gpointer dti_plc);
guchar dti_plc_get_aper_slot(gpointer dti_plc);
guchar dti_plc_get_aper_stat(gpointer dti_plc);
guchar dti_plc_get_filt_slot(gpointer dti_plc);
guchar dti_plc_get_filt_stat(gpointer dti_plc);
guchar dti_plc_get_eht_stat(gpointer dti_plc);
gboolean dti_plc_get_trapdoor_open(gpointer dti_plc);
gboolean dti_plc_get_instrshutt_open(gpointer dti_plc);
gboolean dti_plc_get_power_failed(gpointer dti_plc);
gboolean dti_plc_get_watchdog_tripped(gpointer dti_plc);

void dti_plc_send_domemove_start_left(gpointer dti_plc);
void dti_plc_send_domemove_start_right(gpointer dti_plc);
void dti_plc_send_domemove_stop(gpointer dti_plc);
void dti_plc_send_domemove_azm(gpointer dti_plc, gfloat azm);
void dti_plc_send_domeshutter_open(gpointer dti_plc);
void dti_plc_send_domeshutter_close(gpointer dti_plc);
void dti_plc_send_domeshutter_stop(gpointer dti_plc);
void dti_plc_send_dropout_open(gpointer dti_plc);
void dti_plc_send_dropout_close(gpointer dti_plc);
void dti_plc_send_dropout_stop(gpointer dti_plc);
void dti_plc_send_focus_pos(gpointer dti_plc, gint focus_pos);
void dti_plc_send_acqmir_view(gpointer dti_plc);
void dti_plc_send_acqmir_meas(gpointer dti_plc);
void dti_plc_send_acqmir_stop(gpointer dti_plc);
void dti_plc_send_change_aperture(gpointer dti_plc, guchar aper_slot);
void dti_plc_send_change_filter(gpointer dti_plc, guchar filt_slot);
void dti_plc_send_eht_high(gpointer dti_plc, gboolean eht_on);
void dti_plc_send_instrshutt_toggle(gpointer dti_plc, gboolean instrshutt_open);
void dti_plc_send_watchdog_reset(gpointer dti_plc);

G_END_DECLS

#endif