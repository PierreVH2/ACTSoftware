#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <act_log.h>
#include <act_plc.h>
#include "dti_plc.h"

static void class_init(DtiPlcClass *klass);
static void instance_init(GObject *dti_plc);
static void instance_dispose(GObject *dti_plc);
static gboolean plc_watch(GIOChannel *plc_chan, GIOCondition cond, gpointer dti_plc);
static void plc_send(GIOChannel *plc_chan, glong ioctl_num, glong param);


enum
{
  PLC_COMM_STAT_UPDATE,
  POWER_FAIL_UPDATE,
  WATCHDOG_TRIP_UPDATE,
  DOME_AZM_UPDATE,
  DOME_STAT_UPDATE,
  DOMESHUTT_STAT_UPDATE,
  DROPOUT_STAT_UPDATE,
  FOCUS_POS_UPDATE,
  FOCUS_STAT_UPDATE,
  APER_POS_UPDATE,
  APER_STAT_UPDATE,
  FILT_POS_UPDATE,
  FILT_STAT_UPDATE,
  ACQMIR_STAT_UPDATE,
  EHT_STAT_UPDATE,
  INSTRSHUTT_OPEN_UPDATE,
  HANDSET_STAT_UPDATE,
  TRAPDOOR_OPEN_UPDATE,
  LAST_SIGNAL
};

static guint dti_plc_signals[LAST_SIGNAL] = { 0 };


GType dti_plc_get_type (void)
{
  static GType dti_plc_type = 0;
  
  if (!dti_plc_type)
  {
    const GTypeInfo dti_plc_info =
    {
      sizeof (DtiPlcClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (DtiPlc),
      0,
      (GInstanceInitFunc) instance_init,
      NULL
    };
    
    dti_plc_type = g_type_register_static (G_TYPE_OBJECT, "DtiPlc", &dti_plc_info, 0);
  }
  
  return dti_plc_type;
}

DtiPlc *dti_plc_new (void)
{
  gint plc_fd = open("/dev/" PLC_DEVICE_NAME, O_RDWR|O_NONBLOCK);
  if (plc_fd < 0)
  {
    act_log_error(act_log_msg("Failed to open PLC driver character device - %s.", strerror(errno)));
    return NULL;
  }
  guchar tmp_stat;
  gint ret = read(plc_fd, &tmp_stat, 1);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read from PLC driver character device - %s.", strerror(errno)));
    return NULL;
  }

  struct plc_status tmp_plc_stat;
  ret = ioctl(plc_fd, IOCTL_GET_STATUS, &tmp_plc_stat);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to read PLC status - %s.", strerror(errno)));
    close(plc_fd);
    return NULL;
  }
  
  GObject *dti_plc = g_object_new (dti_plc_get_type(), NULL);
  DtiPlc *objs = DTI_PLC(dti_plc);
  objs->plc_chan = g_io_channel_unix_new(plc_fd);
  g_io_channel_set_close_on_unref(objs->plc_chan, TRUE);
  objs->plc_watch_id = g_io_add_watch (objs->plc_chan, G_IO_IN|G_IO_PRI, plc_watch, objs);
  objs->plc_comm_ok = TRUE;
  memcpy(&objs->plc_stat, &tmp_plc_stat, sizeof(struct plc_status));
  
  return objs;
}

gfloat dti_plc_get_dome_azm(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.dome_pos/10.0;
}

gboolean dti_plc_get_dome_moving(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.dome_moving;
}

guchar dti_plc_get_domeshutt_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.shutter;
}

guchar dti_plc_get_dropout_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.dropout;
}

guchar dti_plc_get_handset_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.handset;
}

gint dti_plc_get_focus_pos(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.focus_pos;
}

guchar dti_plc_get_focus_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.foc_stat;
}

guchar dti_plc_get_acqmir_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.acqmir;
}

guchar dti_plc_get_aper_slot(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.aper_pos;
}

guchar dti_plc_get_aper_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.aper_stat;
}

guchar dti_plc_get_filt_slot(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.filt_pos;
}

guchar dti_plc_get_filt_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.filt_stat;
}

guchar dti_plc_get_eht_stat(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.eht_mode;
}

gboolean dti_plc_get_trapdoor_open(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.trapdoor_open;
}

gboolean dti_plc_get_instrshutt_open(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.instrshutt_open;
}

gboolean dti_plc_get_power_failed(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.power_fail;
}

gboolean dti_plc_get_watchdog_tripped(gpointer dti_plc)
{
  return DTI_PLC(dti_plc)->plc_stat.watchdog_trip;
}

void dti_plc_send_domemove_start_left(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DOME_MOVE, 1);
}

void dti_plc_send_domemove_start_right(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DOME_MOVE, -1);
}

void dti_plc_send_domemove_stop(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DOME_MOVE, 0);
}

void dti_plc_send_domemove_azm(gpointer dti_plc, gfloat azm)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_SET_DOME_AZM, (unsigned long)(azm*10));
}

void dti_plc_send_domeshutter_open(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DOMESHUTT_OPEN, 1);
}

void dti_plc_send_domeshutter_close(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DOMESHUTT_OPEN, -1);
}

void dti_plc_send_domeshutter_stop(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DOMESHUTT_OPEN, 0);
}

void dti_plc_send_dropout_open(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DROPOUT_OPEN, 1);
}

void dti_plc_send_dropout_close(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DROPOUT_OPEN, -1);
}

void dti_plc_send_dropout_stop(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_DROPOUT_OPEN, 0);
}

void dti_plc_send_focus_pos(gpointer dti_plc, gint focus_pos)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_FOCUS_GOTO, focus_pos);
}

void dti_plc_send_acqmir_view(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_SET_ACQMIR, 1);
}

void dti_plc_send_acqmir_meas(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_SET_ACQMIR, 2);
}

void dti_plc_send_acqmir_stop(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_SET_ACQMIR, 0);
}

void dti_plc_send_change_aperture(gpointer dti_plc, guchar aper_slot)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_MOVE_APER, aper_slot);
}

void dti_plc_send_change_filter(gpointer dti_plc, guchar filt_slot)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_MOVE_FILT, filt_slot);
}

void dti_plc_send_eht_high(gpointer dti_plc, gboolean eht_on)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_SET_EHT, eht_on ? 2 : 0);
}

void dti_plc_send_instrshutt_toggle(gpointer dti_plc, gboolean instrshutt_open)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_INSTRSHUTT_OPEN, instrshutt_open ? 1 : 0);
}

void dti_plc_send_watchdog_reset(gpointer dti_plc)
{
  plc_send(DTI_PLC(dti_plc)->plc_chan, IOCTL_RESET_WATCHDOG, 0);
}

static void class_init (DtiPlcClass *klass)
{
  dti_plc_signals[PLC_COMM_STAT_UPDATE] = g_signal_new("plc-comm-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[POWER_FAIL_UPDATE] = g_signal_new("power-fail-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[WATCHDOG_TRIP_UPDATE] = g_signal_new("watchdog-trip-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[DOME_AZM_UPDATE] = g_signal_new("dome-azm-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__FLOAT, G_TYPE_NONE, 1, G_TYPE_FLOAT);
  dti_plc_signals[DOME_STAT_UPDATE] = g_signal_new("dome-moving-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[TRAPDOOR_OPEN_UPDATE] = g_signal_new("trapdoor-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[DOMESHUTT_STAT_UPDATE] = g_signal_new("domeshutt-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[DROPOUT_STAT_UPDATE] = g_signal_new("dropout-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[FOCUS_POS_UPDATE] = g_signal_new("focus-pos-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);
  dti_plc_signals[FOCUS_STAT_UPDATE] = g_signal_new("focus-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[APER_POS_UPDATE] = g_signal_new("aper-pos-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[APER_STAT_UPDATE] = g_signal_new("aper-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[FILT_POS_UPDATE] = g_signal_new("filt-pos-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[FILT_STAT_UPDATE] = g_signal_new("filt-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[ACQMIR_STAT_UPDATE] = g_signal_new("acqmir-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  dti_plc_signals[EHT_STAT_UPDATE] = g_signal_new("eht-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[INSTRSHUTT_OPEN_UPDATE] = g_signal_new("instrshutt-open-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  dti_plc_signals[HANDSET_STAT_UPDATE] = g_signal_new("handset-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  G_OBJECT_CLASS(klass)->dispose = instance_dispose;
}

static void instance_init(GObject *dti_plc)
{
  DtiPlc *objs = DTI_PLC(dti_plc);
  memset(&objs->plc_stat, 0, sizeof(struct plc_status));
  objs->plc_chan = NULL;
  objs->plc_watch_id = 0;
  objs->plc_comm_ok = FALSE;
}

static void instance_dispose(GObject *dti_plc)
{
  DtiPlc *objs = DTI_PLC(dti_plc);
  if (objs->plc_watch_id > 0)
  {
    g_source_remove(objs->plc_watch_id);
    objs->plc_watch_id = 0;
  }
  if (objs->plc_chan != NULL)
  {
    g_io_channel_unref(objs->plc_chan);
    objs->plc_chan = NULL;
  }
  G_OBJECT_CLASS(dti_plc)->dispose(dti_plc);
}

static gboolean plc_watch(GIOChannel *plc_chan, GIOCondition cond, gpointer dti_plc)
{
  (void)cond;
  int plc_fd = g_io_channel_unix_get_fd(plc_chan);
  
  guchar tmp_stat;
  gint ret = read(plc_fd, &tmp_stat, 1);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read from PLC driver character device - %s.", strerror(errno)));
    return TRUE;
  }

  DtiPlc *objs = DTI_PLC(dti_plc);
  if (((tmp_stat & PLC_COMM_OK) > 0) != (objs->plc_comm_ok))
  {
    objs->plc_comm_ok = (tmp_stat & PLC_COMM_OK) > 0;
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[PLC_COMM_STAT_UPDATE], 0, objs->plc_comm_ok);
    if (objs->plc_comm_ok)
      act_log_normal(act_log_msg("Communications with PLC restored."));
    else
    {
      act_log_error(act_log_msg("Driver cannot communicate with PLC."));
      return TRUE;
    }
  }
  if ((tmp_stat & NEW_STAT_AVAIL) == 0)
  {
    act_log_debug(act_log_msg("Reader called, but nothing to update."));
    return TRUE;
  }
  
  struct plc_status tmp_plc_stat;
  ret = ioctl(plc_fd, IOCTL_GET_STATUS, &tmp_plc_stat);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to read PLC status - %s.", strerror(errno)));
    return TRUE;
  }

  if (tmp_plc_stat.power_fail != objs->plc_stat.power_fail)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[POWER_FAIL_UPDATE], 0, tmp_plc_stat.power_fail);
  if (tmp_plc_stat.watchdog_trip != objs->plc_stat.watchdog_trip)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[WATCHDOG_TRIP_UPDATE], 0, tmp_plc_stat.watchdog_trip);
  if (tmp_plc_stat.dome_pos != objs->plc_stat.dome_pos)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[DOME_AZM_UPDATE], 0, tmp_plc_stat.dome_pos/10.0);
  if (tmp_plc_stat.dome_moving != objs->plc_stat.dome_moving)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[DOME_STAT_UPDATE], 0, tmp_plc_stat.dome_moving);
  if (tmp_plc_stat.shutter != objs->plc_stat.shutter)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[DOMESHUTT_STAT_UPDATE], 0, tmp_plc_stat.shutter);
  if (tmp_plc_stat.dropout != objs->plc_stat.dropout)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[DROPOUT_STAT_UPDATE], 0, tmp_plc_stat.dropout);
  if (tmp_plc_stat.focus_pos != objs->plc_stat.focus_pos)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[FOCUS_POS_UPDATE], 0, tmp_plc_stat.focus_pos);
  if (tmp_plc_stat.foc_stat != objs->plc_stat.foc_stat)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[FOCUS_STAT_UPDATE], 0, tmp_plc_stat.foc_stat);
  if (tmp_plc_stat.aper_pos != objs->plc_stat.aper_pos)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[APER_POS_UPDATE], 0, tmp_plc_stat.aper_pos);
  if (tmp_plc_stat.aper_stat != objs->plc_stat.aper_stat)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[APER_STAT_UPDATE], 0, tmp_plc_stat.aper_stat);
  if (tmp_plc_stat.filt_pos != objs->plc_stat.filt_pos)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[FILT_POS_UPDATE], 0, tmp_plc_stat.filt_pos);
  if (tmp_plc_stat.filt_stat != objs->plc_stat.filt_stat)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[FILT_STAT_UPDATE], 0, tmp_plc_stat.filt_stat);
  if (tmp_plc_stat.acqmir != objs->plc_stat.acqmir)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[ACQMIR_STAT_UPDATE], 0, tmp_plc_stat.acqmir);
  if (tmp_plc_stat.eht_mode != objs->plc_stat.eht_mode)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[EHT_STAT_UPDATE], 0, tmp_plc_stat.eht_mode);
  if (tmp_plc_stat.handset != objs->plc_stat.handset)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[HANDSET_STAT_UPDATE], 0, tmp_plc_stat.handset);
  if (tmp_plc_stat.trapdoor_open != objs->plc_stat.trapdoor_open)
    g_signal_emit(G_OBJECT(dti_plc), dti_plc_signals[TRAPDOOR_OPEN_UPDATE], 0, tmp_plc_stat.trapdoor_open);
  
  memcpy(&objs->plc_stat, &tmp_plc_stat, sizeof(struct plc_status));
  return TRUE;
}

static void plc_send(GIOChannel *plc_chan, glong ioctl_num, glong param)
{
  long tmp = param;
  long ret = ioctl(g_io_channel_unix_get_fd(plc_chan), ioctl_num, &tmp);
  if (ret != 0)
    act_log_error(act_log_msg("Failed to send PLC command (this should be impossible) - %s.", strerror(errno)));
}

