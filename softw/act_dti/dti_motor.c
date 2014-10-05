#include <gtk/gtk.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <motor_driver.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include <act_log.h>
#include "dti_motor.h"
#include "pointing_model.h"
#include "tracking_model.h"
#include "dti_marshallers.h"

#define DTI_MOTOR_WARN_N        0x10
#define DTI_MOTOR_WARN_S        0x20
#define DTI_MOTOR_WARN_E        0x40
#define DTI_MOTOR_WARN_W        0x80

static void dti_motor_class_init (DtimotorClass *klass);
static void dti_motor_instance_init(GObject *dti_motor);
static void dti_motor_instance_dispose(GObject *dti_motor);
static gboolean motor_read_ready(GIOChannel *motor_chan, GIOCondition cond, gpointer dti_motor);
static void check_coord_poll(Dtimotor *objs);
static gboolean coord_poll(gpointer dti_motor);
static GActTelcoord *update_coord(Dtimotor *objs);
//static void calc_track_adj(struct hastruct *ha, struct decstruct *dec, struct hastruct *adj_ha, struct decstruct *adj_dec);
static guchar check_warn(Dtimotor *objs);
static void pointing_model_sky_tel(struct hastruct *ha, struct decstruct *dec);
static void pointing_model_tel_sky(struct hastruct *ha, struct decstruct *dec);
static gint read_motor_stat(gint motor_fd, guchar *motor_stat);
static gint read_motor_limits(gint motor_fd, guchar *limits_stat);
static void read_motor_coord(gint motor_fd, struct hastruct *ha, struct decstruct *dec);
static gint send_motor_card(gint motor_fd, guchar motor_dir, guchar motor_speed);
static gint send_motor_goto(gint motor_fd, struct hastruct *ha, struct decstruct *dec, guchar motor_speed, gboolean is_sidereal);
static void send_motor_stop(gint motor_fd);
static void send_motor_emgny_stop(gint motor_fd, gboolean stop_on);
static gint send_motor_tracking(gint motor_fd, gboolean tracking_on);
static gint send_motor_track_adj(gint motor_fd, gdouble adj_ha_h, gdouble adj_dec_d);

static void gact_telcoord_init(GObject *gactmotortelcoord);
static void gact_telgoto_init(GObject *gactmotortelgoto);

enum
{
  STAT_UPDATE,
  LIMITS_UPDATE,
  WARN_UPDATE,
  COORD_UPDATE,
  MOVE_FINISH,
  LAST_SIGNAL
};

/// Table for translating between DTI_MOTOR speed modes and motor driver speed modes
const guchar motor_speed_tbl[] = { 0, MOTOR_SPEED_GUIDE, MOTOR_SPEED_SET, MOTOR_SPEED_SLEW };
/// Table for translating between DTI_MOTOR direction modes and motor driver direction modes
const guchar motor_dir_tbl[] = 
{
  0,
  MOTOR_DIR_NORTH,
  MOTOR_DIR_NORTHWEST,
  MOTOR_DIR_WEST,
  MOTOR_DIR_SOUTHWEST,
  MOTOR_DIR_SOUTH,
  MOTOR_DIR_SOUTHEAST,
  MOTOR_DIR_EAST,
  MOTOR_DIR_NORTHEAST,
  MOTOR_DIR_INVAL
};
static guint dti_motor_signals[LAST_SIGNAL] = { 0 };

GType dti_motor_get_type (void)
{
  static GType dti_motor_type = 0;
  
  if (!dti_motor_type)
  {
    const GTypeInfo dti_motor_info =
    {
      sizeof (DtimotorClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) dti_motor_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Dtimotor),
      0,
      (GInstanceInitFunc) dti_motor_instance_init,
      NULL
    };
    
    dti_motor_type = g_type_register_static (G_TYPE_OBJECT, "Dtimotor", &dti_motor_info, 0);
  }
  
  return dti_motor_type;
}

Dtimotor *dti_motor_new (void)
{  
  gint motor_fd = open("/dev/" MOTOR_DEVICE_NAME, O_RDWR|O_NONBLOCK);
  if (motor_fd < 0)
  {
    act_log_error(act_log_msg("Failed to open motor driver character device - %s.", strerror(errno)));
    return NULL;
  }
  guchar tmp_stat;
  gint ret = read_motor_stat(motor_fd, &tmp_stat);
  if (ret != 0)
  {
    close(motor_fd);
    return NULL;
  }
  guchar tmp_limits;
  ret = read_motor_limits(motor_fd, &tmp_limits);
  if (ret != 0)
  {
    close(motor_fd);
    return NULL;
  }
  struct hastruct tmp_ha;
  struct decstruct tmp_dec;
  read_motor_coord(motor_fd, &tmp_ha, &tmp_dec);
  
  Dtimotor *objs = DTI_MOTOR(g_object_new (dti_motor_get_type (), NULL));
  objs->motor_chan = g_io_channel_unix_new(motor_fd);
  objs->motor_watch_id = g_io_add_watch (objs->motor_chan, G_IO_IN|G_IO_PRI, motor_read_ready, objs);
  check_coord_poll(objs);
  objs->cur_stat = tmp_stat;
  gact_telcoord_set(objs->cur_coord, &tmp_ha, &tmp_dec);
  objs->lim_W_h = 5.0;
  objs->lim_E_h = -5.0;
  objs->lim_N_d = 35.0;
  objs->lim_S_d = -100.0;
  objs->lim_alt_d = 20.0;
  objs->cur_limits = tmp_limits;
  objs->cur_warn = check_warn(objs);
/*  gchar pointing_model_str[256];
  PRINT_MODEL(pointing_model_str);
  act_log_debug(act_log_msg("Using pointing model: %s", pointing_model_str));*/
  return objs;
}

void dti_motor_set_soft_limits (Dtimotor *objs, struct hastruct *lim_W, struct hastruct *lim_E, struct decstruct *lim_N, struct decstruct *lim_S, struct altstruct *lim_alt)
{
  if (lim_W != NULL)
    objs->lim_W_h = convert_HMSMS_H_ha(lim_W);
  if (lim_E != NULL)
    objs->lim_E_h = convert_HMSMS_H_ha(lim_E);
  if (lim_N != NULL)
    objs->lim_N_d = convert_DMS_D_dec(lim_N);
  if (lim_S != NULL)
    objs->lim_S_d = convert_DMS_D_dec(lim_S);
  if (lim_alt != NULL)
    objs->lim_alt_d = convert_DMS_D_alt(lim_alt);
}

guchar dti_motor_get_stat (Dtimotor *objs)
{
  return objs->cur_stat;
}

gboolean dti_motor_stat_init (guchar stat)
{
  return (stat & MOTOR_STAT_HA_INIT) && (stat & MOTOR_STAT_DEC_INIT);
}

gboolean dti_motor_stat_tracking (guchar stat)
{
  return (stat & MOTOR_STAT_TRACKING) > 0;
}

gboolean dti_motor_stat_moving (guchar stat)
{
  return (stat & MOTOR_STAT_MOVING) > 0;
}

gboolean dti_motor_stat_goto (guchar stat)
{
  return (stat & MOTOR_STAT_GOTO) > 0;
}

gboolean dti_motor_stat_card (guchar stat)
{
  return (stat & MOTOR_STAT_CARD) > 0;
}

gboolean dti_motor_stat_emgny_stop (guchar stat)
{
  return (stat & MOTOR_STAT_ALLSTOP) > 0;
}

gboolean dti_motor_limit_reached (guchar stat)
{
  return (stat & MOTOR_STAT_ERR_LIMS) > 0;
}

guchar dti_motor_get_limits (Dtimotor *objs)
{
  return objs->cur_limits;
}

gboolean dti_motor_lim_N (guchar limits)
{
  return MOTOR_LIM_N(limits);
}

gboolean dti_motor_lim_S (guchar limits)
{
  return MOTOR_LIM_S(limits);
}

gboolean dti_motor_lim_E (guchar limits)
{
  return MOTOR_LIM_E(limits);
}

gboolean dti_motor_lim_W (guchar limits)
{
  return MOTOR_LIM_W(limits);
}

guchar dti_motor_get_warn (Dtimotor *objs)
{
  return objs->cur_warn;
}

gboolean dti_motor_warn_N (guchar warn)
{
  return (warn & DTI_MOTOR_WARN_N) > 0;
}

gboolean dti_motor_warn_S (guchar warn)
{
  return (warn & DTI_MOTOR_WARN_S) > 0;
}

gboolean dti_motor_warn_E (guchar warn)
{
  return (warn & DTI_MOTOR_WARN_E) > 0;
}

gboolean dti_motor_warn_W (guchar warn)
{
  return (warn & DTI_MOTOR_WARN_W) > 0;
}

GActTelcoord *dti_motor_get_coord(Dtimotor *objs)
{
  return gact_telcoord_new(&objs->cur_coord->ha, &objs->cur_coord->dec);
}

void dti_motor_apply_pointing_tel_sky(GActTelcoord *coord)
{
  if (IS_GACT_TELCOORD(coord))
    pointing_model_tel_sky(&coord->ha, &coord->dec);
  else
    act_log_error(act_log_msg("Invalid input parameters."));
}

void dti_motor_apply_pointing_sky_tel(GActTelcoord *coord)
{
  if (IS_GACT_TELCOORD(coord))
    pointing_model_sky_tel(&coord->ha, &coord->dec);
  else
    act_log_error(act_log_msg("Invalid input parameters."));
}

gint dti_motor_move_card (Dtimotor *objs, guchar dir, guchar speed)
{
  guchar motor_dir = motor_dir_tbl[dir];
  guchar motor_speed = motor_speed_tbl[speed];
  return send_motor_card(g_io_channel_unix_get_fd(objs->motor_chan), motor_dir, motor_speed);
}

gint dti_motor_goto(Dtimotor *objs, GActTelgoto *gotocmd)
{
  struct hastruct tmpha;
  struct decstruct tmpdec;
  memcpy(&tmpha, &gotocmd->ha, sizeof(struct hastruct));
  memcpy(&tmpdec, &gotocmd->dec, sizeof(struct decstruct));
//  pointing_model_forward(&tmpha, &tmpdec);
  struct altstruct tmpalt;
  convert_EQUI_ALTAZ(&tmpha, &tmpdec, &tmpalt, NULL);
  if ((convert_HMSMS_H_ha(&tmpha) > objs->lim_W_h) || (convert_HMSMS_H_ha(&tmpha) < objs->lim_E_h) || (convert_DMS_D_dec(&tmpdec) > objs->lim_N_d) || (convert_DMS_D_dec(&tmpdec) < objs->lim_S_d) || (convert_DMS_D_alt(&tmpalt) < objs->lim_alt_d))
    return EINVAL;
  act_log_debug(act_log_msg("Starting goto. Current coord: %f %f . Target coord: %f %f", convert_HMSMS_H_ha(&objs->cur_coord->ha), convert_DMS_D_dec(&objs->cur_coord->dec), convert_HMSMS_H_ha(&tmpha), convert_DMS_D_dec(&tmpdec)));
  guchar motor_speed = motor_speed_tbl[gotocmd->speed];
  return send_motor_goto(g_io_channel_unix_get_fd(objs->motor_chan), &tmpha, &tmpdec, motor_speed, gotocmd->is_sidereal);
}

void dti_motor_stop(Dtimotor *objs)
{
  send_motor_stop(g_io_channel_unix_get_fd(objs->motor_chan));
}

void dti_motor_emgncy_stop(Dtimotor *objs, gboolean stop_on)
{
  send_motor_emgny_stop(g_io_channel_unix_get_fd(objs->motor_chan), stop_on);
}

gint dti_motor_init(Dtimotor *objs)
{
  return send_motor_card(g_io_channel_unix_get_fd(objs->motor_chan), MOTOR_DIR_SOUTHWEST, MOTOR_SPEED_SET);
}

gint dti_motor_set_tracking(Dtimotor *objs, gboolean tracking_on)
{
  return send_motor_tracking(g_io_channel_unix_get_fd(objs->motor_chan), tracking_on);
}

gint dti_motor_track_adj(Dtimotor *objs, gdouble ha_adj_h, gdouble dec_adj_d)
{
  return send_motor_track_adj(g_io_channel_unix_get_fd(objs->motor_chan), ha_adj_h, dec_adj_d);
}


void dti_motor_get_raw_coord(Dtimotor *objs, glong *ha_steps, glong *dec_steps)
{
  struct motor_tel_coord coord;
  gint ret = ioctl(g_io_channel_unix_get_fd(objs->motor_chan), IOCTL_MOTOR_GET_MOTOR_POS, &coord);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read telescope coordinates from motor driver - %s.", strerror(errno)));
    return;
  }
  *ha_steps = coord.tel_ha;
  *dec_steps = coord.tel_dec;
}


static void dti_motor_class_init (DtimotorClass *klass)
{
  dti_motor_signals[STAT_UPDATE] = g_signal_new("stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_motor_signals[LIMITS_UPDATE] = g_signal_new("limits-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_motor_signals[WARN_UPDATE] = g_signal_new("warn-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_motor_signals[COORD_UPDATE] = g_signal_new("coord-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  dti_motor_signals[MOVE_FINISH] = g_signal_new("move-finish", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__BOOLEAN_POINTER, G_TYPE_NONE, 2, G_TYPE_BOOLEAN, G_TYPE_POINTER);
  G_OBJECT_CLASS(klass)->dispose = dti_motor_instance_dispose;
}

static void dti_motor_instance_init(GObject *dti_motor)
{
  Dtimotor *objs = DTI_MOTOR(dti_motor);
  objs->motor_chan = NULL;
  objs->coord_to_id = objs->motor_watch_id = 0;
  objs->lim_W_h = objs->lim_E_h = objs->lim_N_d = objs->lim_S_d = objs->lim_alt_d = 0.0;
  objs->cur_stat = objs->cur_limits = objs->cur_warn = 0;
  objs->cur_coord = gact_telcoord_new(NULL, NULL);
  if (g_object_is_floating (objs->cur_coord))
    g_object_ref_sink(objs->cur_coord);
}

static void dti_motor_instance_dispose(GObject *dti_motor)
{
  Dtimotor *objs = DTI_MOTOR(dti_motor);
  if (objs->motor_watch_id > 0)
  {
    g_source_remove(objs->motor_watch_id);
    objs->motor_watch_id = 0;
  }
  if (objs->coord_to_id > 0)
  {
    g_source_remove(objs->coord_to_id);
    objs->coord_to_id = 0;
  }
  if (objs->motor_chan != NULL)
  {
    g_io_channel_unref(objs->motor_chan);
    objs->motor_chan = NULL;
  }
  if (objs->cur_coord != NULL)
  {
    g_object_unref(objs->cur_coord);
    objs->cur_coord = NULL;
  }
  G_OBJECT_CLASS(dti_motor)->dispose(dti_motor);
}

static gboolean motor_read_ready(GIOChannel *motor_chan, GIOCondition cond, gpointer dti_motor)
{
  (void) cond;
  Dtimotor *objs = DTI_MOTOR(dti_motor);
  guchar tmp_stat;
  gint motor_fd = g_io_channel_unix_get_fd(motor_chan);
  gint ret = read_motor_stat(motor_fd, &tmp_stat);
  if (ret < 0)
    return TRUE;
  if (tmp_stat == objs->cur_stat)
    return TRUE;
  g_signal_emit(G_OBJECT(objs), dti_motor_signals[STAT_UPDATE], 0, tmp_stat);
  if (((objs->cur_stat & MOTOR_STAT_MOVING) > 0) && ((tmp_stat & MOTOR_STAT_MOVING) == 0))
  {
    GActTelcoord *coord = update_coord(objs);
    g_object_force_floating (G_OBJECT(coord));
    if ((tmp_stat & (MOTOR_STAT_ALLSTOP | MOTOR_STAT_ERR_LIMS)) > 0)
      g_signal_emit(dti_motor, dti_motor_signals[MOVE_FINISH], 0, FALSE, coord);
    else
      g_signal_emit(dti_motor, dti_motor_signals[MOVE_FINISH], 0, TRUE, coord);
  }
  objs->cur_stat = tmp_stat;
  check_coord_poll(objs);
  if ((tmp_stat & MOTOR_STAT_ERR_LIMS) == 0)
  {
    if (objs->cur_limits != 0)
    {
      objs->cur_limits = 0;
      g_signal_emit(G_OBJECT(objs), dti_motor_signals[LIMITS_UPDATE], 0, 0);
    }
    return TRUE;
  }
  guchar tmp_limits;
  ret = read_motor_limits(motor_fd, &tmp_limits);
  if (ret != 0)
    return TRUE;
  if (tmp_limits != objs->cur_limits)
  {
    objs->cur_limits = tmp_limits;
    g_signal_emit(G_OBJECT(objs), dti_motor_signals[LIMITS_UPDATE], 0, tmp_limits);
  }
  return TRUE;
}

static void check_coord_poll(Dtimotor *objs)
{
  if (objs->coord_to_id != 0)
    g_source_remove(objs->coord_to_id);
  if ((objs->cur_stat & MOTOR_STAT_MOVING) > 0)
    objs->coord_to_id = g_timeout_add(300, coord_poll, objs);
  else
    objs->coord_to_id = g_timeout_add(1000, coord_poll, objs);
}

static gboolean coord_poll(gpointer dti_motor)
{
  Dtimotor *objs = DTI_MOTOR(dti_motor);
  GActTelcoord *tmp_coord = update_coord(objs);
  
  guchar tmp_warn = check_warn(objs);
  if (tmp_warn != objs->cur_warn)
  {
    g_signal_emit(G_OBJECT(objs), dti_motor_signals[WARN_UPDATE], 0, tmp_warn);
    objs->cur_warn = tmp_warn;
  }
  
  if ((objs->cur_stat & MOTOR_STAT_TRACKING) && (MOTOR_LIM_W(objs->cur_limits) || (tmp_warn & DTI_MOTOR_WARN_W)))
    send_motor_stop(g_io_channel_unix_get_fd(objs->motor_chan));
//   else if (objs->cur_stat & MOTOR_STAT_TRACKING)
//   {
//     struct hastruct adj_ha;
//     struct decstruct adj_dec;
//     calc_track_adj(&tmp_ha, &tmp_dec, &adj_ha, &adj_dec);
//     send_motor_track_adj(motor_fd, convert_HMSMS_H_ha(&adj_ha), convert_DMS_D_dec(&adj_dec));
//   }
  g_object_force_floating (G_OBJECT(tmp_coord));
  g_signal_emit(G_OBJECT(objs), dti_motor_signals[COORD_UPDATE], 0, tmp_coord);
  return TRUE;
}

static GActTelcoord *update_coord(Dtimotor *objs)
{
  struct hastruct tmp_ha;
  struct decstruct tmp_dec;
  gint motor_fd = g_io_channel_unix_get_fd(objs->motor_chan);
  read_motor_coord(motor_fd, &tmp_ha, &tmp_dec);
  gact_telcoord_set(objs->cur_coord, &tmp_ha, &tmp_dec);
  return gact_telcoord_new(&tmp_ha, &tmp_dec);
}

/*
static void calc_track_adj(struct hastruct *ha, struct decstruct *dec, struct hastruct *adj_ha, struct decstruct *adj_dec)
{
//   act_log_debug(act_log_msg("Not implemented yet."));
  (void) ha;
  (void) dec;
  (void) adj_ha;
  (void) adj_dec;
}
*/

static guchar check_warn(Dtimotor *objs)
{
  guchar tmp_warn = 0;
  if (convert_HMSMS_H_ha(&objs->cur_coord->ha) > objs->lim_W_h)
    tmp_warn |= DTI_MOTOR_WARN_W;
  if (convert_HMSMS_H_ha(&objs->cur_coord->ha) < objs->lim_E_h)
    tmp_warn |= DTI_MOTOR_WARN_E;
  if (convert_DMS_D_dec(&objs->cur_coord->dec) > objs->lim_N_d)
    tmp_warn |= DTI_MOTOR_WARN_N;
  if (convert_DMS_D_dec(&objs->cur_coord->dec) < objs->lim_S_d)
    tmp_warn |= DTI_MOTOR_WARN_S;
  
  struct altstruct tmpalt;
  struct azmstruct tmpazm;
  convert_EQUI_ALTAZ(&objs->cur_coord->ha, &objs->cur_coord->dec, &tmpalt, &tmpazm);
  if (convert_DMS_D_alt(&tmpalt) < objs->lim_alt_d)
  {
    double tmpazm_d = convert_DMS_D_azm(&tmpazm);
    if ((tmpazm_d > -90.0) && (tmpazm_d < 90.0))
      tmp_warn |= DTI_MOTOR_WARN_N;
    if ((tmpazm_d > 0.0) && (tmpazm_d < 180.0))
      tmp_warn |= DTI_MOTOR_WARN_E;
    if ((tmpazm_d > 90.0) && (tmpazm_d < 270.0))
    tmp_warn |= DTI_MOTOR_WARN_S;
    if ((tmpazm_d > 180.0) && (tmpazm_d < 360.0))
      tmp_warn |= DTI_MOTOR_WARN_W;
  }
  
  return tmp_warn;
}

static void pointing_model_sky_tel(struct hastruct *ha, struct decstruct *dec)
{
/*  corr_atm_refract_sky_tel_equat(ha, dec);
  double ha_h = convert_HMSMS_H_ha(ha), dec_d = convert_DMS_D_dec(dec);
//   POINTING_MODEL_ST(ha_h,dec_d);
  convert_H_HMSMS_ha(ha_h, ha);
  convert_D_DMS_dec(dec_d, dec);*/
}

static void pointing_model_tel_sky(struct hastruct *ha, struct decstruct *dec)
{
/*  double ha_h = convert_HMSMS_H_ha(ha), dec_d = convert_DMS_D_dec(dec);
//   POINTING_MODEL_TS(ha_h,dec_d);
  convert_H_HMSMS_ha(ha_h, ha);
  convert_D_DMS_dec(dec_d, dec);
  corr_atm_refract_tel_sky_equat(ha, dec);*/
}

static gint read_motor_stat(gint motor_fd, guchar *motor_stat)
{
  guchar tmp_stat;
  gint ret = read(motor_fd, &tmp_stat, 1);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read from motor driver character device - %s.", strerror(errno)));
    return errno;
  }
  *motor_stat = tmp_stat;
  return 0;
}

static gint read_motor_limits(gint motor_fd, guchar *limits_stat)
{
  unsigned char motor_limits;
  gint ret = ioctl(motor_fd, IOCTL_MOTOR_GET_LIMITS, &motor_limits);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read limit switch status from motor driver character device - %s.", strerror(errno)));
    return errno;
  }
  *limits_stat = motor_limits;
  return 0;
}

static void read_motor_coord(gint motor_fd, struct hastruct *ha, struct decstruct *dec)
{
  struct motor_tel_coord coord;
  gint ret;
  double range_ha, range_dec;
#ifdef MOTOR_USE_ENCOD
  ret = ioctl(motor_fd, IOCTL_MOTOR_GET_ENCOD_POS, &coord);
  range_ha = MOTOR_ENCOD_E_LIM;
  range_dec = MOTOR_ENCOD_N_LIM;
#else
  ret = ioctl(motor_fd, IOCTL_MOTOR_GET_MOTOR_POS, &coord);
  range_ha = MOTOR_STEPS_E_LIM;
  range_dec = MOTOR_STEPS_N_LIM;
#endif
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read telescope coordinates from motor driver - %s.", strerror(errno)));
    return;
  }
  double ha_h = ((double) (MOTOR_LIM_E_MSEC - MOTOR_LIM_W_MSEC) * coord.tel_ha / range_ha + MOTOR_LIM_W_MSEC) / 3600000.0;
  double dec_d = ((double) (MOTOR_LIM_N_ASEC - MOTOR_LIM_S_ASEC) * coord.tel_dec / range_dec + MOTOR_LIM_S_ASEC) / 3600.0;
  convert_H_HMSMS_ha(ha_h, ha);
  convert_D_DMS_dec(dec_d, dec);
}

static gint send_motor_card(gint motor_fd, guchar motor_dir, guchar motor_speed)
{
  struct motor_card_cmd cmd = {.dir = motor_dir, .speed = motor_speed };
  gint ret = ioctl(motor_fd, IOCTL_MOTOR_CARD, &cmd);
  if (ret < 0)
    return errno;
  return 0;
}

static gint send_motor_goto(gint motor_fd, struct hastruct *ha, struct decstruct *dec, guchar motor_speed, gboolean is_sidereal)
{
  double ha_h = convert_HMSMS_H_ha(ha);
  double dec_d = convert_DMS_D_dec(dec);
  
  struct motor_goto_cmd cmd;
  #ifdef MOTOR_USE_ENCOD
  cmd.targ_ha = (ha_h*3600000.0 - MOTOR_LIM_W_MSEC) * MOTOR_ENCOD_E_LIM / (double)(MOTOR_LIM_E_MSEC-MOTOR_LIM_W_MSEC);
  cmd.targ_dec = (dec_d*3600.0 - MOTOR_LIM_S_ASEC) * MOTOR_ENCOD_N_LIM / (double)(MOTOR_LIM_N_ASEC-MOTOR_LIM_S_ASEC);
  cmd.use_encod = TRUE;
  #else
  cmd.targ_ha = (ha_h*3600000.0 - MOTOR_LIM_W_MSEC) * MOTOR_STEPS_E_LIM / (double)(MOTOR_LIM_E_MSEC-MOTOR_LIM_W_MSEC);
  cmd.targ_dec = (dec_d*3600.0 - MOTOR_LIM_S_ASEC) * MOTOR_STEPS_N_LIM / (double)(MOTOR_LIM_N_ASEC-MOTOR_LIM_S_ASEC);
  cmd.use_encod = FALSE;
  #endif
  cmd.speed = motor_speed;
  cmd.tracking_on = is_sidereal;
  
  gint ret = ioctl(motor_fd, IOCTL_MOTOR_GOTO, &cmd);
  if (ret < 0)
    return errno;
  return 0;
}

static void send_motor_stop(gint motor_fd)
{
  ioctl(motor_fd, IOCTL_MOTOR_GOTO, NULL);
  ioctl(motor_fd, IOCTL_MOTOR_CARD, NULL);
}

static void send_motor_emgny_stop(gint motor_fd, gboolean stop_on)
{
  glong stop_val = stop_on > 0;
  ioctl(motor_fd, IOCTL_MOTOR_EMERGENCY_STOP, stop_val);
}

static gint send_motor_tracking(gint motor_fd, gboolean tracking_on)
{
  guchar tmp_on = tracking_on;
  gint ret = ioctl(motor_fd, IOCTL_MOTOR_SET_TRACKING, tmp_on);
  if (ret < 0)
  {
//    act_log_error(act_log_msg("Failed to set telescope tracking rate - %s.", strerror(errno)));
    return errno;
  }
  return 0;
}

static gint send_motor_track_adj(gint motor_fd, gdouble adj_ha_h, gdouble adj_dec_d)
{
  struct motor_track_adj cmd;
  cmd.adj_ha_steps = adj_ha_h * 3600000.0 * MOTOR_STEPS_E_LIM / (double)(MOTOR_LIM_E_MSEC-MOTOR_LIM_W_MSEC);
  cmd.adj_dec_steps = adj_dec_d * 3600.0 * MOTOR_STEPS_N_LIM / (double)(MOTOR_LIM_N_ASEC-MOTOR_LIM_S_ASEC);
  
  gint ret = ioctl(motor_fd, IOCTL_MOTOR_TRACKING_ADJ, &cmd);
  if (ret < 0)
    return errno;
  return 0;
}

GType gact_telcoord_get_type (void)
{
  static GType gacttelcoord_type = 0;
  
  if (!gacttelcoord_type)
  {
    const GTypeInfo gact_telcoord_info =
    {
      sizeof (GActTelcoordClass),
      NULL, // base_init
      NULL, // base_finalize
      NULL, // class init
      NULL, // class_finalize 
      NULL, // class_data
      sizeof (GActTelcoord),
      0,
      (GInstanceInitFunc) gact_telcoord_init,
      NULL
    };
    
    gacttelcoord_type = g_type_register_static (G_TYPE_OBJECT, "GActTelcoord", &gact_telcoord_info, 0);
  }
  
  return gacttelcoord_type;
}

GActTelcoord *gact_telcoord_new (struct hastruct *tel_ha, struct decstruct *tel_dec)
{
  GObject *gacttelcoord = g_object_new (gact_telcoord_get_type(), NULL);
  GActTelcoord *objs = GACT_TELCOORD(gacttelcoord);
  if (tel_ha != NULL)
    memcpy(&objs->ha, tel_ha, sizeof(struct hastruct));
  else
    memset(&objs->ha, 0, sizeof(struct hastruct));
  if (tel_dec != NULL)
    memcpy(&objs->dec, tel_dec, sizeof(struct decstruct));
  else
    memset(&objs->dec, 0, sizeof(struct decstruct));
  return objs;
}

GActTelcoord *gact_telcoord_new_telcoord (GActTelcoord *copy)
{
  GObject *gacttelcoord = g_object_new (gact_telcoord_get_type(), NULL);
  GActTelcoord *objs = GACT_TELCOORD(gacttelcoord);
  memcpy(&objs->ha, &copy->ha, sizeof(struct hastruct));
  memcpy(&objs->dec, &copy->dec, sizeof(struct decstruct));
  return objs;
}


void gact_telcoord_set (GActTelcoord *objs, struct hastruct *tel_ha, struct decstruct *tel_dec)
{
  if (tel_ha != NULL)
    memcpy(&objs->ha, tel_ha, sizeof(struct hastruct));
  if (tel_dec != NULL)
    memcpy(&objs->dec, tel_dec, sizeof(struct decstruct));
}

void gact_telcoord_set_telcoord(GActTelcoord *objs, GActTelcoord *copy)
{
  memcpy(&objs->ha, &copy->ha, sizeof(struct hastruct));
  memcpy(&objs->dec, &copy->dec, sizeof(struct decstruct));
}

GType gact_telgoto_get_type (void)
{
  static GType gacttelgoto_type = 0;
  
  if (!gacttelgoto_type)
  {
    const GTypeInfo gacttelgoto_info =
    {
      sizeof (GActTelgotoClass),
      NULL, // base_init
      NULL, // base_finalize
      NULL, // class init
      NULL, // class_finalize
      NULL, // class_data
      sizeof (GActTelgoto),
      0,
      (GInstanceInitFunc) gact_telgoto_init,
      NULL
    };
    
    gacttelgoto_type = g_type_register_static (G_TYPE_OBJECT, "GActTelgoto", &gacttelgoto_info, 0);
  }
  
  return gacttelgoto_type;
}

GActTelgoto *gact_telgoto_new (struct hastruct *ha, struct decstruct *dec, guchar speed, gboolean is_sidereal)
{
  GObject *gacttelgoto = g_object_new (gact_telgoto_get_type(), NULL);
  GActTelgoto *objs = GACT_TELGOTO(gacttelgoto);
  if ((ha != NULL) && (dec != NULL))
  {
    memcpy(&objs->ha, ha, sizeof(struct hastruct));
    memcpy(&objs->dec, dec, sizeof(struct decstruct));
    objs->is_sidereal = is_sidereal;
    objs->speed = speed;
  }
  return objs;
}

static void gact_telcoord_init(GObject *gacttelcoord)
{
  GActTelcoord *objs = GACT_TELCOORD(gacttelcoord);
  memset(&objs->ha, 0, sizeof(struct hastruct));
  memset(&objs->dec, 0, sizeof(struct decstruct));
}

static void gact_telgoto_init(GObject *gacttelgoto)
{
  GActTelgoto *objs = GACT_TELGOTO(gacttelgoto);
  memset(&objs->ha, 0, sizeof(struct hastruct));
  memset(&objs->dec, 0, sizeof(struct decstruct));
  objs->is_sidereal = FALSE;
}

