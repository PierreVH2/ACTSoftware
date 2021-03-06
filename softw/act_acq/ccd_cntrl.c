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
#include "ccd_cntrl.h"
#include "marshallers.h"

// #define DATETIME_TO_MSEC    60000
// TEL_POS_TO_MSEC gets rounded off to nearest second
#define TEL_POS_TO_MSEC     60000
#define SIG_INTEG_TO_MSEC     100

static void ccd_cmd_instance_init(GObject *ccd_cmd);
static void ccd_cmd_class_init(CcdCmdClass *klass);
static void ccd_cmd_instance_dispose(GObject *ccd_cmd);

static void ccd_cntrl_instance_init(GObject *ccd_cntrl);
static void ccd_cntrl_class_init(CcdImgClass *klass);
static void ccd_cntrl_instance_dispose(GObject *ccd_cntrl);
static gboolean ccd_cntrl_ccd_init(CcdCntrl *ccd_cntrl);
static gboolean drv_watch(GIOChannel *drv_chan, GIOCondition cond, gpointer ccd_cntrl);
static gboolean integ_timer(gpointer ccd_cntrl);
static gboolean tel_pos_timeout(gpointer ccd_cntrl);


enum
{
  SIG_STAT_UPDATE = 0,
  SIG_INTEG_REM,
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

CcdCmd *ccd_cmd_new(guchar img_type, gushort win_start_x, gushort win_start_y, gushort win_width, gushort win_height, gushort prebin_x, gushort prebin_y, gfloat integ_t_s, gulong repetitions, gulong targ_id, gchar const *targ_name)
{
  CcdCmd *objs = CCD_CMD(g_object_new (ccd_cmd_get_type(), NULL));
  objs->img_type = img_type;
  objs->win_start_x = win_start_x;
  objs->win_start_y = win_start_y;
  objs->win_width = win_width;
  objs->win_height = win_height;
  objs->prebin_x = prebin_x;
  objs->prebin_y = prebin_y;
  objs->integ_t_s = integ_t_s;
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

gfloat ccd_cmd_get_integ_t(CcdCmd *objs)
{
  return objs->integ_t_s;
}

void ccd_cmd_set_integ_t(CcdCmd *objs, gfloat integ_t_s)
{
  objs->integ_t_s = integ_t_s;
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
  objs->integ_t_s = 0.0;
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
  CcdCntrl *objs = CCD_CNTRL(g_object_new (ccd_cntrl_get_type(), NULL));
  if (!ccd_cntrl_ccd_init(objs))
  {
    act_log_error(act_log_msg("Failed to initialise CCD interface."));
    g_object_unref(G_OBJECT(objs));
    return NULL;
  }
  return objs;
}

gboolean ccd_cntrl_reconnect(CcdCntrl *objs)
{
  if (objs->drv_chan != NULL)
  {
    gint drv_fd = g_io_channel_unix_get_fd (objs->drv_chan);
    ioctl(drv_fd, IOCTL_ACQ_RESET, 0);
    GError *err = NULL;
    GIOStatus chan_stat = g_io_channel_shutdown (objs->drv_chan, FALSE, &err);
    if ((chan_stat != G_IO_STATUS_NORMAL) || (err != NULL))
    {
      act_log_error(act_log_msg("Error while trying to shut down CCD driver channel (code %d)\n", chan_stat));
      if (err != NULL)
      {
        act_log_error(act_log_msg("Error message was: %s\n", err->message));
        g_error_free (err);
        err = NULL;
      }
      return FALSE;
    }
    objs->drv_chan = NULL;
  }
  return ccd_cntrl_ccd_init(objs);
}

void ccd_cntrl_gen_test_image(CcdCntrl *objs)
{
  int i;
  gushort width=objs->max_width_px, height=objs->max_height_px;
  gfloat tmp_data[width*height];
  for (i=0; i<width*height; i++)
    tmp_data[i] = (i%CCDPIX_MAX)/(gfloat)CCDPIX_MAX;
  CcdImg *img = CCD_IMG(g_object_new (ccd_img_get_type(), NULL));
  ccd_img_set_img_data(img, width*height, tmp_data);
  ccd_img_set_window(img, 0, 0, width, height, 1, 1);
  ccd_img_set_tel_pos(img, 0.0, 0.0);
  ccd_img_set_pixel_size(img, objs->ra_width_asec, objs->dec_height_asec);
  ccd_img_set_integ_t(img, 0.0);
  time_t cur_time = time(NULL); 
  ccd_img_set_start_datetime(img, cur_time);
  g_signal_emit(G_OBJECT(objs), cntrl_signals[SIG_NEW_IMG], 0,  img);
  g_object_unref(G_OBJECT(img));
}

gchar *ccd_cntrl_get_ccd_id(CcdCntrl *objs)
{
  return g_strdup(objs->ccd_id);
}

gfloat ccd_cntrl_get_min_integ_t_sec(CcdCntrl *objs)
{
  return objs->min_integ_t_s;
}

gfloat ccd_cntrl_get_max_integ_t_sec(CcdCntrl *objs)
{
  return objs->max_integ_t_s;
}

gushort ccd_cntrl_get_max_width(CcdCntrl *objs)
{
  return objs->max_width_px;
}

gushort ccd_cntrl_get_max_height(CcdCntrl *objs)
{
  return objs->max_height_px;
}

gint ccd_cntrl_start_integ(CcdCntrl *objs, CcdCmd *cmd)
{
  if (objs->max_integ_t_s == objs->min_integ_t_s)
  {
    act_log_error(act_log_msg("CCD parameters not yet established, cannot start integration."));
    return -EAGAIN;
  }
  if (objs->drv_stat & (CCD_INTEGRATING | CCD_READING_OUT))
  {
    act_log_error(act_log_msg("CCD is busy, cannot order integration."));
    return -EBUSY;
  }
  struct ccd_cmd drv_cmd;
  ccd_cmd_exp_t(ccd_cmd_get_integ_t(cmd), drv_cmd);
  drv_cmd.prebin_x = ccd_cmd_get_prebin_x(cmd);
  drv_cmd.prebin_y = ccd_cmd_get_prebin_y(cmd);
  drv_cmd.win_start_x = ccd_cmd_get_win_start_x(cmd);
  drv_cmd.win_start_y = ccd_cmd_get_win_start_y(cmd);
  drv_cmd.win_width = ccd_cmd_get_win_width(cmd);
  drv_cmd.win_height = ccd_cmd_get_win_height(cmd);
  int ret = ioctl(g_io_channel_unix_get_fd(objs->drv_chan), IOCTL_ORDER_EXP, &drv_cmd);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to order CCD integration - %s", strerror(ret)));
    return -EIO;
  }

  CcdImg *new_img = CCD_IMG(g_object_new (ccd_img_get_type(), NULL));
  if (objs->tel_pos_to_id == 0)
  {
    act_log_error(act_log_msg("Telescope RA and Dec not available. Storing dummy RA and Dec in image header."));
    ccd_img_set_tel_pos(new_img, 0.0, 0.0);
  }
  else
    ccd_img_set_tel_pos(new_img, objs->ra_d, objs->dec_d);
  ccd_img_set_img_type(new_img, cmd->img_type);
  ccd_img_set_integ_t(new_img, cmd->integ_t_s);
  ccd_img_set_window(new_img, cmd->win_start_x, cmd->win_start_y, cmd->win_width, cmd->win_height, cmd->prebin_x, cmd->prebin_y);
  ccd_img_set_target(new_img, cmd->targ_id, cmd->targ_name);
  ccd_img_set_user(new_img, cmd->user_id, cmd->user_name);
  
  objs->rpt_rem = cmd->repetitions;
  if (objs->integ_trem_to_id)
  {
    act_log_debug(act_log_msg("Strange: Starting new integration and timeout for previous integration still active. Removing old timeout."));
    g_source_remove(objs->integ_trem_to_id);
  }
  g_timer_start(objs->integ_timer);
  if (cmd->integ_t_s > 1.0)
    objs->integ_trem_to_id = g_timeout_add(100, integ_timer, objs);
  if (objs->cur_img != NULL)
    g_object_unref(objs->cur_img);
  objs->cur_img = new_img;
  return 0;
}

void ccd_cntrl_cancel_integ(CcdCntrl *objs)
{
  act_log_debug(act_log_msg("Not fully implemented yet. Not cancelling current integration, but will cancel future integrations in this series."));
  if (objs->rpt_rem > 0)
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
  gfloat integ_elapsed = g_timer_elapsed (objs->integ_timer, &tmp_usec);
  integ_elapsed += tmp_usec/1000000.0;
  return ccd_img_get_integ_t(objs->cur_img) - integ_elapsed;
}

gulong ccd_cntrl_get_rpt_rem(CcdCntrl *objs)
{
  return objs->rpt_rem;
}

gboolean ccd_cntrl_stat_err_retry(guchar status)
{
  return (status & CCD_ERR_RETRY) > 0;
}

gboolean ccd_cntrl_stat_err_no_recov(guchar status)
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
//  act_log_debug(act_log_msg("Updated coordinates received - %f %f", tel_ra_d, tel_dec_d));
  objs->ra_d = tel_ra_d;
  objs->dec_d = tel_dec_d;
  if (objs->tel_pos_to_id != 0)
    g_source_remove(objs->tel_pos_to_id);
  objs->tel_pos_to_id = g_timeout_add_seconds(TEL_POS_TO_MSEC/1000, tel_pos_timeout, objs);
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
  objs->min_integ_t_s = objs->max_integ_t_s = 0.0;
  objs->max_width_px = objs->max_height_px = 0;
  objs->ra_width_asec = objs->dec_height_asec = 0;
  
  objs->cur_img = NULL;
  objs->rpt_rem = 0;
  objs->integ_trem_to_id = 0;
  objs->integ_timer = g_timer_new();
}

static void ccd_cntrl_class_init(CcdImgClass *klass)
{
  cntrl_signals[SIG_STAT_UPDATE] = g_signal_new("ccd-stat-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__UCHAR, G_TYPE_NONE, 1, G_TYPE_UCHAR);
  cntrl_signals[SIG_INTEG_REM] = g_signal_new("ccd-integ-rem", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__FLOAT_ULONG, G_TYPE_NONE, 2, G_TYPE_FLOAT, G_TYPE_ULONG);
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
    g_io_channel_unref(objs->drv_chan);
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
  if (objs->integ_trem_to_id != 0)
  {
    g_source_remove(objs->integ_trem_to_id);
    objs->integ_trem_to_id = 0;
  }
  if (objs->integ_timer != NULL)
  {
    g_timer_destroy(objs->integ_timer);
    objs->integ_timer = NULL;
  }
  if (objs->tel_pos_to_id != 0)
  {
    g_source_remove(objs->tel_pos_to_id);
    objs->tel_pos_to_id = 0;
  }
}

static gboolean ccd_cntrl_ccd_init(CcdCntrl *objs)
{
  gint drv_fd = open("/dev/" MERLIN_DEVICE_NAME, O_RDWR|O_NONBLOCK);
  if (drv_fd < 0)
  {
    act_log_error(act_log_msg("Failed to open camera driver character device - %s.", strerror(errno)));
    return FALSE;
  }
  guchar tmp_stat;
  gint ret = read(drv_fd, &tmp_stat, 1);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to read from camera driver character device - %s.", strerror(errno)));
    close(drv_fd);
    return FALSE;
  }
  struct ccd_modes tmp_modes;
  ret = ioctl(drv_fd, IOCTL_GET_MODES, &tmp_modes);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to read camera driver capabilities - %s.", strerror(errno)));
    return FALSE;
  }
  
  objs->drv_stat = tmp_stat;
  objs->drv_chan = g_io_channel_unix_new(drv_fd);
  g_io_channel_set_close_on_unref(objs->drv_chan, TRUE);
  objs->drv_watch_id = g_io_add_watch(objs->drv_chan, G_IO_IN|G_IO_PRI, drv_watch, objs);
  if (objs->ccd_id != NULL)
    g_free(objs->ccd_id);
  objs->ccd_id = malloc(strlen(tmp_modes.ccd_id+1));
  sprintf(objs->ccd_id, "%s", tmp_modes.ccd_id);
  objs->min_integ_t_s = tmp_modes.min_exp_t_sec + tmp_modes.min_exp_t_nanosec/1000000000.0;
  objs->max_integ_t_s = tmp_modes.max_exp_t_sec + tmp_modes.max_exp_t_nanosec/1000000000.0;
  objs->max_width_px = tmp_modes.max_width_px;
  objs->max_height_px = tmp_modes.max_height_px;
  act_log_debug(act_log_msg("Pixel size:  %f  %f  %f    %f  %f  %f", tmp_modes.ra_width_asec, tmp_modes.max_width_px, tmp_modes.ra_width_asec/(gfloat)tmp_modes.max_width_px, tmp_modes.dec_height_asec, tmp_modes.max_height_px, tmp_modes.dec_height_asec/(gfloat)tmp_modes.max_height_px));
  objs->ra_width_asec = tmp_modes.ra_width_asec/(gfloat)tmp_modes.max_width_px;
  objs->dec_height_asec = tmp_modes.dec_height_asec/(gfloat)tmp_modes.max_height_px;
  /// TODO: When windowing implemented, implement proper treatment of initial window - i.e. either select a default starting window mode and send that to the driver here or read the last used window from the driver and set it in the cntrl structure here.
  objs->win_start_x = 0;
  objs->win_start_y = 0;
  objs->win_width = tmp_modes.max_width_px;
  objs->win_height = tmp_modes.max_height_px;
  objs->prebin_x = 1;
  objs->prebin_y = 1;
  
  return TRUE;
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
  
  g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_STAT_UPDATE], 0,  tmp_stat);
  if (((objs->drv_stat & CCD_INTEGRATING) != 0) && ((tmp_stat & CCD_INTEGRATING) == 0))
  {
    g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_INTEG_REM], 0,  0.0, objs->rpt_rem);
    if (objs->integ_trem_to_id != 0)
    {
      g_source_remove(objs->integ_trem_to_id);
      objs->integ_trem_to_id = 0;
    }
  }
  objs->drv_stat = tmp_stat;
  
  if ((tmp_stat & CCD_IMG_READY) == 0)
    return TRUE;
  
  struct merlin_img tmp_img;
  ret = ioctl(g_io_channel_unix_get_fd(objs->drv_chan), IOCTL_GET_IMAGE, &tmp_img);
  struct ccd_img_params *tmp_params = &tmp_img.img_params;
  if (objs->cur_img == NULL)
  {
    act_log_debug(act_log_msg("New image received, but CCD control structure has no reference to a current image - integration was probably cancelled. Ignoring this image."));
    return TRUE;
  }
  
  objs->rpt_rem--;
  gfloat *tmp_data = malloc(tmp_params->img_len*sizeof(gfloat));
  gulong i;
  for (i=0; i<tmp_params->img_len; i++)
    tmp_data[i] = (gfloat)tmp_img.img_data[i]/CCDPIX_MAX;
  CcdImg *img = CCD_IMG(objs->cur_img);
  objs->cur_img = NULL;
  ccd_img_set_img_data(img, tmp_params->img_len, tmp_data);
  ccd_img_set_window(img, tmp_params->win_start_x, tmp_params->win_start_y, tmp_params->win_width, tmp_params->win_height, tmp_params->prebin_x, tmp_params->prebin_y);
  ccd_img_set_integ_t(img, ccd_img_exp_t((*tmp_params)));
  ccd_img_set_start_datetime(img, tmp_params->start_sec + tmp_params->start_nanosec/(double)1e9);
  ccd_img_set_pixel_size(img, objs->ra_width_asec, objs->dec_height_asec);
  g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_NEW_IMG], 0,  img);
  g_object_unref(G_OBJECT(img));

  if (objs->rpt_rem > 0)
  {
    CcdCmd *cmd = CCD_CMD(g_object_new(ccd_cmd_get_type(), NULL));
    ccd_cmd_set_img_type(cmd, ccd_img_get_img_type(img));
    ccd_cmd_set_win_start_x(cmd, ccd_img_get_win_start_x(img));
    ccd_cmd_set_win_start_y(cmd, ccd_img_get_win_start_y(img));
    ccd_cmd_set_win_width(cmd, ccd_img_get_win_width(img));
    ccd_cmd_set_win_height(cmd, ccd_img_get_win_height(img));
    ccd_cmd_set_prebin_x(cmd, ccd_img_get_prebin_x(img));
    ccd_cmd_set_prebin_y(cmd, ccd_img_get_prebin_y(img));
    ccd_cmd_set_integ_t(cmd, ccd_img_get_integ_t(img));
    ccd_cmd_set_rpt(cmd, objs->rpt_rem);
    ccd_cmd_set_user(cmd, ccd_img_get_user_id(img), ccd_img_get_user_name(img));
    ccd_cmd_set_target(cmd, ccd_img_get_targ_id(img), ccd_img_get_targ_name(img));
    ret = ccd_cntrl_start_integ(objs, cmd);
    if (ret < 0)
    {
      act_log_error(act_log_msg("Failed to start next integration repetition (%d - %s)", ret, strerror(abs(ret))));
      objs->rpt_rem = 0;
    }
  }
  else
    g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_INTEG_REM], 0,  0.0, 0);
  
  return TRUE;
}

static gboolean integ_timer(gpointer ccd_cntrl)
{
  CcdCntrl *objs = CCD_CNTRL(ccd_cntrl);
  if (!ccd_cntrl_stat_integrating(objs->drv_stat))
  {
    act_log_debug(act_log_msg("CCD not integrating (%lu). Cannot determine remaining integration time.", objs->drv_stat));
    return TRUE;
  }
  if (objs->cur_img == NULL)
  {
    act_log_debug(act_log_msg("No CCD image object available for current integration. Cannot determine remaining integration time."));
    return TRUE;
  }
  gulong tmp_usec;
  gfloat integ_elapsed = g_timer_elapsed (objs->integ_timer, &tmp_usec);
  integ_elapsed += tmp_usec/1000000.0;
  if (ccd_img_get_integ_t(objs->cur_img) < integ_elapsed)
    g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_INTEG_REM], 0,  0.0);
  else
    g_signal_emit(G_OBJECT(ccd_cntrl), cntrl_signals[SIG_INTEG_REM], 0,  ccd_img_get_integ_t(objs->cur_img) - integ_elapsed, objs->rpt_rem);
  return TRUE;
}

static gboolean tel_pos_timeout(gpointer ccd_cntrl)
{
  act_log_debug(act_log_msg("Timed out while waiting for new telescope coordinates."));
  CCD_CNTRL(ccd_cntrl)->tel_pos_to_id = 0;
  return FALSE;
}


