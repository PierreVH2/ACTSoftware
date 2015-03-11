#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <merlin_driver.h>
#include <ccd_defs.h>
#include <act_log.h>
#include <act_positastro.h>
#include "ccdcntrl.h"
#include "marshallers.h"

#define DATETIME_TO_SEC 60
#define TEL_POS_TO_SEC 60

static void ccd_cmd_instance_init(GObject *ccd_cmd);
static void ccd_cmd_class_init(CcdCmdClass *klass);
static void ccd_cmd_instance_dispose(GObject *ccd_cmd);

static void ccd_cntrl_instance_init(GObject *ccd_cntrl);
static void ccd_cntrl_class_init(CcdImgClass *klass);
static void ccd_cntrl_instance_dispose(GObject *ccd_cntrl);
static gboolean drv_watch(GIOChannel *drv_chan, GIOCondition cond, gpointer ccd_cntrl);
static gboolean integ_timer(gpointer ccd_cntrl);
static gboolean datetime_timeout(gpointer ccd_cntrl);
static gboolean tel_pos_timeout(gpointer ccd_cntrl);


enum
{
  SIG_STAT_UPDATE = 0,
  SIG_NEW_IMG,
  LAST_SIGNAL
};

static guint cntrl_signals[LAST_SIGNAL] = { 0 };


/// CCD Command implementation
GType ccd_cmd_get_type(void)
{
  static GType ccd_cmd_type = 0;
  
  if (!ccd_cmd_type)
  {
    const GTypeInfo ccd_cmd_info =
    {
      sizeof (CcdCmdClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) ccd_cmd_class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (CcdCmd),
      0,
      (GInstanceInitFunc) ccd_cmd_instance_init,
      NULL
    };
    
    ccd_cmd_type = g_type_register_static (G_TYPE_OBJECT, "CcdCmd", &ccd_cmd_info, 0);
  }
  
  return ccd_cmd_type;
}

CcdCmd *ccd_cmd_new(guchar img_type, gushort win_start_x, gushort win_start_y, gushort win_width, gushort win_height, gushort prebin_x, gushort prebin_y, gfloat exp_t_s, gulong repetitions, gulong targ_id, gchar const *targ_name)
{
  CcdCmd *objs = CCD_CMD(g_object_new (ccd_cmd_get_type(), NULL));
  objs->img_type = img_type;
  objs->win_start_x = win_start_x;
  objs->win_start_y = win_start_y;
  objs->win_width = win_width;
  objs->win_height = win_height;
  objs->prebin_x = prebin_x;
  objs->prebin_y = prebin_y;
  objs->exp_t_s = exp_t_s;
  objs->repetitions = repetitions;
  objs->targ_id = targ_id;
  if (objs->targ_name != NULL)
    g_free(objs->targ_name);
  objs->targ_name = g_strdup(targ_name);
  return objs;
}

guchar ccd_cmd_get_img_type(CcdCmd *objs)
{
  return objs->img_type;
}

void ccd_cmd_set_img_type(CcdCmd *objs, guchar img_type)
{
  objs->img_type = img_type;
}

gushort ccd_cmd_get_win_start_x(CcdCmd *objs)
{
  return objs->win_start_x;
}

void ccd_cmd_set_win_start_x(CcdCmd *objs, gushort win_start_x)
{
  objs->win_start_x = win_start_x;
}

gushort ccd_cmd_get_win_start_y(CcdCmd *objs)
{
  return objs->win_start_y;
}

void ccd_cmd_set_win_start_y(CcdCmd *objs, gushort win_start_y)
{
  objs->win_start_y = win_start_y;
}

gushort ccd_cmd_get_win_width(CcdCmd *objs)
{
  return objs->win_width;
}

void ccd_cmd_set_win_width(CcdCmd *objs, gushort win_width)
{
  objs->win_width = win_width;
}

gushort ccd_cmd_get_win_height(CcdCmd *objs)
{
  return objs->win_height;
}

void ccd_cmd_set_win_height(CcdCmd *objs, gushort win_height)
{
  objs->win_height = win_height;
}

gushort ccd_cmd_get_prebin_x(CcdCmd *objs)
{
  return objs->prebin_x;
}

void ccd_cmd_set_prebin_x(CcdCmd *objs, gushort prebin_x)
{
  objs->prebin_x = prebin_x;
}

gushort ccd_cmd_get_prebin_y(CcdCmd *objs)
{
  return objs->prebin_y;
}

void ccd_cmd_set_prebin_y(CcdCmd *objs, gushort prebin_y)
{
  objs->prebin_y = prebin_y;
}

gfloat ccd_cmd_get_exp_t(CcdCmd *objs)
{
  return objs->exp_t_s;
}

void ccd_cmd_set_exp_t(CcdCmd *objs, gfloat exp_t_s)
{
  objs->exp_t_s = exp_t_s;
}

gulong ccd_cmd_get_rpt(CcdCmd *objs)
{
  return objs->repetitions;
}

void ccd_cmd_set_rpt(CcdCmd *objs, gulong repetitions)
{
  objs->repetitions = repetitions;
}

gulong ccd_cmd_get_targ_id(CcdCmd *objs)
{
  return objs->targ_id;
}

gchar const * ccd_cmd_get_targ_name(CcdCmd *objs)
{
  return objs->targ_name;
}

void ccd_cmd_set_target(CcdCmd *objs, gulong targ_id, gchar const *targ_name)
{
  objs->targ_id = targ_id;
  if (objs->targ_name != NULL)
    g_free(objs->targ_name);
  objs->targ_name = g_strdup(targ_name);
}

gulong ccd_cmd_get_user_id(CcdCmd *objs)
{
  return objs->user_id;
}

gchar const * ccd_cmd_get_user_name(CcdCmd *objs)
{
  return objs->user_name;
}

void ccd_cmd_set_user(CcdCmd *objs, gulong user_id, gchar const *user_name)
{
  objs->user_id = user_id;
  if (objs->user_name != NULL)
    g_free(objs->user_name);
  objs->user_name = g_strdup(user_name);
}

static void ccd_cmd_instance_init(GObject *ccd_cmd)
{
  CcdCmd *objs = CCD_CMD(ccd_cmd);
  objs->img_type = IMGT_NONE;
  objs->win_start_x = objs->win_start_y = 0;
  objs->win_width = objs->win_height = 0;
  objs->prebin_x = objs->prebin_y = 0;
  objs->exp_t_s = 0.0;
  objs->repetitions = 0;
  objs->targ_id = 0;
  objs->targ_name = NULL;
}

static void ccd_cmd_class_init(CcdCmdClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = ccd_cmd_instance_dispose;
}

static void ccd_cmd_instance_dispose(GObject *ccd_cmd)
{
  CcdCmd *objs = CCD_CMD(ccd_cmd);
  if (objs->targ_name != NULL)
  {
    g_free(objs->targ_name);
    objs->targ_name = NULL;
  }
}

// CCD Control implementation
GType ccd_cntrl_get_type (void)
{
  static GType ccd_cntrl_type = 0;
  
  if (!ccd_cntrl_type)
  {
    const GTypeInfo ccd_cntrl_info =
    {
      sizeof (CcdCntrlClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) ccd_cntrl_class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (CcdCntrl),
      0,
      (GInstanceInitFunc) ccd_cntrl_instance_init,
      NULL
    };
    
    ccd_cntrl_type = g_type_register_static (G_TYPE_OBJECT, "CcdCntrl", &ccd_cntrl_info, 0);
  }
  
  return ccd_cntrl_type;
}

CcdCntrl *ccd_cntrl_new (void)
{
  gint drv_fd = open("/dev/" MERLIN_DEVICE_NAME, O_RDWR|O_NONBLOCK);
  if (drv_fd < 0)
  {
    act_log_error(act_log_msg("Failed to open camera driver character device - %s.", strerror(errno)));
    return NULL;
  }
  guchar tmp_stat;
  gint ret = read(drv_fd, &tmp_stat, 1);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read from camera driver character device - %s.", strerror(errno)));
    close(drv_fd);
    return NULL;
  }
  struct ccd_modes tmp_modes;
  ret = ioctl(drv_fd, IOCTL_GET_MODES, &tmp_modes);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to read camera driver capabilities - %s.", strerror(errno)));
    return NULL;
  }
  
  CcdCntrl *objs = CCD_CNTRL(g_object_new (ccd_cntrl_get_type(), NULL));
  objs->drv_stat = tmp_stat;
  objs->drv_chan = g_io_channel_unix_new(drv_fd);
  g_io_channel_set_close_on_unref(objs->drv_chan, TRUE);
  objs->drv_watch_id = g_io_add_watch(objs->drv_chan, G_IO_IN|G_IO_PRI, drv_watch, objs);
  if (objs->ccd_id != NULL)
    g_free(objs->ccd_id);
  objs->ccd_id = malloc(strlen(tmp_modes.ccd_id+1));
  sprintf(objs->ccd_id, "%s", tmp_modes.ccd_id);
  objs->min_exp_t_s = tmp_modes.min_exp_t_sec + tmp_modes.min_exp_t_nanosec/1000000000.0;
  objs->max_exp_t_s = tmp_modes.max_exp_t_sec + tmp_modes.max_exp_t_nanosec/1000000000.0;
  objs->max_width_px = tmp_modes.max_width_px;
  objs->max_height_px = tmp_modes.max_height_px;
  objs->ra_width_asec = tmp_modes.ra_width_asec;
  objs->dec_height_asec = tmp_modes.dec_height_asec;
  /// TODO: When windowing implemented, implement proper treatment of initial window - i.e. either select a default starting window mode and send that to the driver here or read the last used window from the driver and set it in the cntrl structure here.
  objs->win_start_x = 0;
  objs->win_start_y = 0;
  objs->win_width = tmp_modes.max_width_px;
  objs->win_height = tmp_modes.max_height_px;
  objs->prebin_x = 1;
  objs->prebin_y = 1;
  
  return objs;
}

gfloat ccd_cntrl_get_min_exp_t_sec(CcdCntrl *objs)
{
  return objs->min_exp_t_s;
}

gfloat ccd_cntrl_get_max_exp_t_sec(CcdCntrl *objs)
{
  return objs->max_exp_t_s;
}

gushort ccd_cntrl_get_max_width(CcdCntrl *objs)
{
  return objs->max_width_px;
}

gushort ccd_cntrl_get_max_height(CcdCntrl *objs)
{
  return objs->max_height_px;
}

gint ccd_cntrl_start_exp(CcdCntrl *objs, CcdCmd *cmd)
{
  if (objs->max_exp_t_s == objs->min_exp_t_s)
  {
    act_log_error(act_log_msg("CCD parameters not yet established, cannot start integration."));
    return EAGAIN;
  }
  if (objs->drv_stat & (CCD_INTEGRATING | CCD_READING_OUT))
  {
    act_log_error(act_log_msg("CCD is busy, cannot order exposure."));
    return EBUSY;
  }
  struct ccd_cmd drv_cmd;
  ccd_cmd_exp_t(ccd_cmd_get_exp_t(cmd), drv_cmd);
  drv_cmd.prebin_x = ccd_cmd_get_prebin_x(cmd);
  drv_cmd.prebin_y = ccd_cmd_get_prebin_y(cmd);
  drv_cmd.win_start_x = ccd_cmd_get_win_start_x(cmd);
  drv_cmd.win_start_y = ccd_cmd_get_win_start_y(cmd);
  drv_cmd.win_width = ccd_cmd_get_win_width(cmd);
  drv_cmd.win_height = ccd_cmd_get_win_height(cmd);
  int ret = ioctl(g_io_channel_unix_get_fd(objs->drv_chan), IOCTL_ORDER_EXP, &drv_cmd);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to order CCD exposure - %s", strerror(ret)));
    return EIO;
  }

  CcdImg *new_img = CCD_IMG(g_object_new (ccd_img_get_type(), NULL));
  if (objs->datetime_to_id == 0)
  {
    act_log_error(act_log_msg("Date and time information not available. Reading from system clock and hoping for the best."));
    time_t syst = time(NULL);
    struct tm *sys_gmt = gmtime(&syst);
    struct datestruct start_unid;
    struct timestruct start_unit;
    start_unid.year = sys_gmt->tm_year;
    start_unid.month = sys_gmt->tm_mon;
    start_unid.day = sys_gmt->tm_mday-1;
    start_unit.hours = sys_gmt->tm_hour;
    start_unit.minutes = sys_gmt->tm_min;
    start_unit.seconds = sys_gmt->tm_sec;
    start_unit.milliseconds = 0;
    ccd_img_set_start_datetime(new_img, &start_unid, &start_unit);
  }
  else
    ccd_img_set_start_datetime(new_img, &objs->unid, &objs->unit);
  if (objs->tel_pos_to_id == 0)
  {
    act_log_error(act_log_msg("Telescope RA and Dec not available. Storing dummy RA and Dec in image header."));
    ccd_img_set_tel_pos(new_img, 0.0, 0.0);
  }
  else
    ccd_img_set_tel_pos(new_img, objs->ra_d, objs->dec_d);
  ccd_img_set_img_type(new_img, cmd->img_type);
  ccd_img_set_exp_t(new_img, cmd->exp_t_s);
  ccd_img_set_window(new_img, cmd->win_start_x, cmd->win_start_y, cmd->win_width, cmd->win_height, cmd->prebin_x, cmd->prebin_y);
  ccd_img_set_target(new_img, cmd->targ_id, cmd->targ_name);
  ccd_img_set_user(new_img, cmd->user_id, cmd->user_name);
  
  objs->rpt_rem = cmd->repetitions;
  if (objs->exp_trem_to_id)
  {
    act_log_debug(act_log_msg("Strange: Starting new exposure and timeout for previous exposure still active. Removing old timeout."));
    g_source_remove(objs->exp_trem_to_id);
  }
  g_timer_start(objs->exp_timer);
  if (cmd->exp_t_s > 1.0)
    objs->exp_trem_to_id = g_timeout_add(1, integ_timer, objs);
  if (objs->cur_img != 0)
  {
    g_object_unref(objs->cur_img);
    objs->cur_img = new_img;
  }
  return 0;
}

void ccd_cntrl_cancel_exp(CcdCntrl *objs)
{
  act_log_debug(act_log_msg("Not fully implemented yet. Not cancelling current integration, but will cancel future integrations in this series."));
  objs->rpt_rem = 1;
}

guchar ccd_cntrl_get_stat(CcdCntrl *objs)
{
  return objs->drv_stat;
}

gfloat ccd_cntrl_get_integ_trem(CcdCntrl *objs)
{
  if (!ccd_cntrl_stat_integrating(objs->drv_stat))
    return 0;
  if (objs->cur_img == NULL)
    return 0;
  gulong tmp_usec;
  gfloat exp_elapsed = g_timer_elapsed (objs->exp_timer, &tmp_usec);
  exp_elapsed += tmp_usec/1000000.0;
  return ccd_img_get_exp_t(objs->cur_img) - exp_elapsed;
}

gulong ccd_cntrl_get_rpt_rem(CcdCntrl *objs)
{
  return objs->rpt_rem;
}

gboolean ccd_cntrl_stat_err_retry(guchar status)
{
  return (status & CCD_ERR_RETRY) > 0;
}

gboolean ccd_cntrl_stat_error_no_recov(guchar status)
{
  return (status & CCD_ERR_NO_RECOV) > 0;
}

gboolean ccd_cntrl_stat_integrating(guchar status)
{
  return (status & CCD_INTEGRATING) > 0;
}

gboolean ccd_cntrl_stat_readout(guchar status)
{
  return (status & CCD_READING_OUT) > 0;
}

void ccd_cntrl_set_tel_pos(CcdCntrl *objs, gfloat tel_ra_d, gfloat tel_dec_d)
{
  objs->ra_d = tel_ra_d;
  objs->dec_d = tel_dec_d;
  if (objs->tel_pos_to_id != 0)
  {
    g_source_remove(objs->tel_pos_to_id);
    objs->tel_pos_to_id = g_timeout_add_seconds(TEL_POS_TO_SEC, tel_pos_timeout, objs);
  }
}

static void ccd_cntrl_instance_init(GObject *ccd_cntrl)
{
  CcdCntrl *objs = CCD_CNTRL(ccd_cntrl);
  objs->drv_chan = NULL;
  objs->drv_watch_id = 0;
  objs->drv_stat = 0;
  objs->win_start_x = objs->win_start_y = 0;
  objs->win_width = objs->win_height = 0;
  objs->prebin_x = objs->prebin_y = 0;
  
  objs->ra_d = 0.0;
  objs->dec_d = 0.0;
  objs->tel_pos_to_id = 0;
  
  objs->ccd_id = NULL;
  objs->min_exp_t_s = objs->max_exp_t_s = 0.0;
  objs->max_width_px = objs->max_height_px = 0;
  objs->ra_width_asec = objs->dec_height_asec = 0;
  
  objs->cur_img = NULL;
  objs->rpt_rem = 0;
  objs->exp_trem_to_id = 0;
  objs->exp_timer = g_timer_new();
}

static void ccd_cntrl_class_init(CcdImgClass *klass)
{
  cntrl_signals[SIG_STAT_UPDATE] = g_signal_new("ccd-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__UCHAR_FLOAT, G_TYPE_NONE, 2, G_TYPE_UCHAR, G_TYPE_FLOAT);
  cntrl_signals[SIG_NEW_IMG] = g_signal_new("ccd-new-image", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__OBJECT, G_TYPE_NONE, 1, G_TYPE_OBJECT);
  G_OBJECT_CLASS(klass)->dispose = ccd_cntrl_instance_dispose;
}

static void ccd_cntrl_instance_dispose(GObject *ccd_cntrl)
{
  CcdCntrl *objs = CCD_CNTRL(ccd_cntrl);
  if (objs->drv_watch_id != 0)
  {
    g_source_remove(objs->drv_watch_id);
    objs->drv_watch_id = 0;
  }
  if (objs->drv_chan != NULL)
  {
    g_object_unref(objs->drv_chan);
    objs->drv_chan = NULL;
  }
  if (objs->ccd_id != NULL)
  {
    g_free(objs->ccd_id);
    objs->ccd_id = NULL;
  }
  if (objs->cur_img != NULL)
  {
    g_object_unref(objs->cur_img);
    objs->cur_img = NULL;
  }
  if (objs->exp_trem_to_id != 0)
  {
    g_source_remove(objs->exp_trem_to_id);
    objs->exp_trem_to_id = 0;
  }
  if (objs->exp_timer != NULL)
  {
    g_object_unref(objs->exp_timer);
    objs->exp_timer = NULL;
  }
  if (objs->datetime_to_id != 0)
  {
    g_source_remove(objs->datetime_to_id);
    objs->datetime_to_id = 0;
  }
  if (objs->tel_pos_to_id != 0)
  {
    g_source_remove(objs->tel_pos_to_id);
    objs->tel_pos_to_id = 0;
  }
}

static gboolean drv_watch(GIOChannel *drv_chan, GIOCondition cond, gpointer ccd_cntrl)
{
  (void)cond;
  CcdCntrl *objs = CCD_CNTRL(ccd_cntrl);
  guchar tmp_stat;
  gint ret = read(g_io_channel_unix_get_fd(drv_chan), &tmp_stat, 1);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read from camera driver character device - %s.", strerror(errno)));
    return TRUE;
  }
  if (tmp_stat == objs->drv_stat)
    return TRUE;
  
  g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_STAT_UPDATE], 0,  tmp_stat, ccd_cntrl_get_integ_trem(objs));
  
  if ((tmp_stat & CCD_IMG_READY) != 0)
  {
    if (objs->exp_trem_to_id != 0)
    {
      g_source_remove(objs->exp_trem_to_id);
      objs->exp_trem_to_id = 0;
    }
    struct merlin_img tmp_img;
    ret = ioctl(g_io_channel_unix_get_fd(objs->drv_chan), IOCTL_GET_IMAGE, &tmp_img);
    struct ccd_img_params *tmp_params = &tmp_img.img_params;
    
    gfloat *tmp_data = malloc(tmp_params->img_len*sizeof(gfloat));
    gulong i;
    for (i=0; i<tmp_params->img_len; i++)
      tmp_data[i] = (gfloat)tmp_img.img_data[i]/CCDPIX_MAX;
    ccd_img_set_img_data(CCD_IMG(objs->cur_img), tmp_params->img_len, tmp_data);
    
    ccd_img_set_window(CCD_IMG(objs->cur_img), tmp_params->win_start_x, tmp_params->win_start_y, tmp_params->win_width, tmp_params->win_height, tmp_params->prebin_x, tmp_params->prebin_y);
    ccd_img_set_exp_t(CCD_IMG(objs->cur_img), ccd_img_exp_t((*tmp_params)));
    struct datestruct sys_start_unid;
    struct timestruct real_start_unit, sys_start_unit;
    convert_H_HMSMS_time(ccd_img_start_t((*tmp_params))/3600.0, &real_start_unit);
    ccd_img_get_start_datetime(CCD_IMG(objs->cur_img), &sys_start_unid, &sys_start_unit);
    check_systime_discrep(&sys_start_unid, &sys_start_unit, &real_start_unit);
    ccd_img_set_start_datetime(CCD_IMG(objs->cur_img), &sys_start_unid, &real_start_unit);
    
    g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_NEW_IMG], 0,  objs->cur_img);
  }
  
  return TRUE;
}

static gboolean integ_timer(gpointer ccd_cntrl)
{
  CcdCntrl *objs = CCD_CNTRL(ccd_cntrl);
  if ((objs->drv_stat & CCD_INTEGRATING) == 0)
  {
    act_log_debug(act_log_msg("CCD not integrating. Cannot time integration."));
    objs->exp_trem_to_id = 0;
    return FALSE;
  }
  g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_STAT_UPDATE], 0,  objs->drv_stat, ccd_cntrl_get_integ_trem(objs));
  return TRUE;
}

static gboolean datetime_timeout(gpointer ccd_cntrl)
{
  act_log_debug(act_log_msg("Timed out while waiting for new date and time information."));
  CCD_CNTRL(ccd_cntrl)->datetime_to_id = 0;
  return FALSE;
}

static gboolean tel_pos_timeout(gpointer ccd_cntrl)
{
  act_log_debug(act_log_msg("Timed out while waiting for new telescope coordinates."));
  CCD_CNTRL(ccd_cntrl)->tel_pos_to_id = 0;
  return FALSE;
}


