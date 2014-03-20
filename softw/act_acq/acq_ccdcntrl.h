#ifndef __CCDCNTRL_H__
#define __CCDCNTRL_H__

#include <glib.h>
#include <glib-object.h>
#include <act_timecoord.h>
#include <act_ipc.h>

G_BEGIN_DECLS

/// Image type enum
enum
{
  IMGT_NONE = 0,
  IMGT_ACQ_OBJ,
  IMGT_ACQ_SKY,
  IMGT_OBJECT,
  IMGT_BIAS,
  IMGT_DARK,
  IMGT_FLAT
};

#define CCD_CMD_TYPE                (ccd_cmd_get_type())
#define CCD_CMD(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), CCD_CMD_TYPE, CcdCmd))
#define CCD_CMD_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CCD_CMD_TYPE, CcdCmdClass))
#define IS_CCD_CMD(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), CCD_CMD_TYPE))
#define IS_CCD_CMD_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CCD_CMD_TYPE))

typedef struct _CcdCmd       CcdCmd;
typedef struct _CcdCmdClass  CcdCmdClass;

struct _CcdCmd
{
  GObject parent;
  /// Image type
  guchar img_type;
  /// Start x,y of window
  gushort win_start_x, win_start_y;
  /// Width and height of window
  gushort win_width, win_height;
  /// Prebinning mode
  gushort prebin_x, prebin_y;
  /// The length of the exposure
  gfloat exp_t_s;
  /// Number of repetitions
  gulong repetitions;
  /// Target name and DB id
  gchar *targ_name;
  gulong targ_id;
  /// User name and DB id
  gchar *user_name;
  gulong user_id;
};

struct _CcdCmdClass
{
  GObjectClass parent_class;
};

GType ccd_cmd_get_type(void);
CcdCmd *ccd_cmd_new(guchar img_type, gushort win_start_x, gushort win_start_y, gushort win_width, gushort win_height, gushort prebin_x, gushort prebin_y, gfloat exp_t_s, gulong repetitions, gulong targ_id, gchar const *targ_name);
guchar ccd_cmd_get_img_type(CcdCmd *objs);
void ccd_cmd_set_img_type(CcdCmd *objs, guchar img_type);
gushort ccd_cmd_get_win_start_x(CcdCmd *objs);
void ccd_cmd_set_win_start_x(CcdCmd *objs, gushort win_start_x);
gushort ccd_cmd_get_win_start_y(CcdCmd *objs);
void ccd_cmd_set_win_start_y(CcdCmd *objs, gushort win_start_y);
gushort ccd_cmd_get_win_width(CcdCmd *objs);
void ccd_cmd_set_win_width(CcdCmd *objs, gushort win_width);
gushort ccd_cmd_get_win_height(CcdCmd *objs);
void ccd_cmd_set_win_height(CcdCmd *objs, gushort win_height);
gushort ccd_cmd_get_prebin_x(CcdCmd *objs);
void ccd_cmd_set_prebin_x(CcdCmd *objs, gushort prebin_x);
gushort ccd_cmd_get_prebin_y(CcdCmd *objs);
void ccd_cmd_set_prebin_y(CcdCmd *objs, gushort prebin_y);
gfloat ccd_cmd_get_exp_t(CcdCmd *objs);
void ccd_cmd_set_exp_t(CcdCmd *objs, gfloat exp_t_s);
gulong ccd_cmd_get_rpt(CcdCmd *objs);
void ccd_cmd_set_rpt(CcdCmd *objs, gulong repetitions);
gulong ccd_cmd_get_targ_id(CcdCmd *objs);
gchar const * ccd_cmd_get_targ_name(CcdCmd *objs);
void ccd_cmd_set_target(CcdCmd *objs, gulong targ_id, gchar const *targ_name);
gulong ccd_cmd_get_user_id(CcdCmd *objs);
gchar const * ccd_cmd_get_user_name(CcdCmd *objs);
void ccd_cmd_set_user(CcdCmd *objs, gulong user_id, gchar const *user_name);


#define CCD_IMG_TYPE                (ccd_img_get_type())
#define CCD_IMG(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), CCD_IMG_TYPE, CcdImg))
#define CCD_IMG_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CCD_IMG_TYPE, CcdImgClass))
#define IS_CCD_IMG(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), CCD_IMG_TYPE))
#define IS_CCD_IMG_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CCD_IMG_TYPE))

/// TODO: Implement CCD windowing and prebinning

typedef struct _CcdImg       CcdImg;
typedef struct _CcdImgClass  CcdImgClass;

struct _CcdImg
{
  GObject parent;
  /// Image type
  guchar img_type;
  /// Start x,y of window
  gushort win_start_x, win_start_y;
  /// Width and height of window
  gushort win_width, win_height;
  /// Prebinning mode
  gushort prebin_x, prebin_y;
  /// The length of the exposure
  gfloat exp_t_s;
  /// Starting date and time of exposure
  struct datestruct start_unid;
  struct timestruct start_unit;
  /// Target name and DB id
  gchar *targ_name;
  gulong targ_id;
  /// User name and DB id
  gchar *user_name;
  gulong user_id;
  /// Starting telescope coordinates
  struct rastruct tel_ra;
  struct decstruct tel_dec;
  /// Length of image
  gulong img_len;
  /// Image data
  gfloat *img_data;
};

struct _CcdImgClass
{
  GObjectClass parent_class;
};

GType ccd_img_get_type(void);
guchar ccd_img_get_img_type(CcdImg *objs);
gfloat ccd_img_get_exp_t(CcdImg *objs);
gushort ccd_img_get_win_start_x(CcdImg *objs);
gushort ccd_img_get_win_start_y(CcdImg *objs);
gushort ccd_img_get_win_width(CcdImg *objs);
gushort ccd_img_get_win_height(CcdImg *objs);
gushort ccd_img_get_prebin_x(CcdImg *objs);
gushort ccd_img_get_prebin_y(CcdImg *objs);
void ccd_img_get_start_datetime(CcdImg *objs, struct datestruct *start_unid, struct timestruct *start_unit);
gchar const *ccd_img_get_targ_name(CcdImg *objs);
gulong ccd_img_get_targ_id(CcdImg *objs);
gchar const *ccd_img_get_user_name(CcdImg *objs);
gulong ccd_img_get_user_id(CcdImg *objs);
void ccd_img_get_tel_pos(CcdImg *objs, struct rastruct *tel_ra, struct decstruct *tel_dec);
gulong ccd_img_get_img_len(CcdImg *objs);
gfloat const *ccd_img_get_img_data(CcdImg *objs);


#define CCD_CNTRL_TYPE                (ccd_cntrl_get_type())
#define CCD_CNTRL(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), CCD_CNTRL_TYPE, CcdCntrl))
#define CCD_CNTRL_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CCD_CNTRL_TYPE, CcdCntrlClass))
#define IS_CCD_CNTRL(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), CCD_CNTRL_TYPE))
#define IS_CCD_CNTRL_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CCD_CNTRL_TYPE))

typedef struct _CcdCntrl       CcdCntrl;
typedef struct _CcdCntrlClass  CcdCntrlClass;

struct _CcdCntrl
{
  GObject parent;
  GIOChannel *drv_chan;
  gint drv_watch_id;
  guchar drv_stat;
  
  gchar *ccd_id;
  gfloat min_exp_t_s, max_exp_t_s;
  gushort max_width_px, max_height_px;
  gushort ra_width_asec, dec_height_asec;

  struct datestruct unid;
  struct timestruct unit;
  gint datetime_to_id;
  
  struct rastruct tel_ra;
  struct decstruct tel_dec;
  gint tel_pos_to_id;
  
  CcdImg *cur_img;
  gulong rpt_rem;
  GTimer *exp_timer;
  gint exp_trem_to_id;
  
  /// Start x,y of window
  gushort win_start_x, win_start_y;
  /// Width and height of window
  gushort win_width, win_height;
  /// Prebinning mode
  gushort prebin_x, prebin_y;
};

struct _CcdCntrlClass
{
  GObjectClass parent_class;
};

GType ccd_cntrl_get_type (void);
CcdCntrl *ccd_cntrl_new (void);
// gint ccd_cntrl_set_window(CcdCntrl *objs, gushort win_start_x, gushort win_start_y, gushort win_width, gushort win_height, gushort prebin_x, gushort prebin_y);
gint ccd_cntrl_start_exp(CcdCntrl *objs, CcdCmd *cmd);
void ccd_cntrl_cancel_exp(CcdCntrl *objs);
gfloat ccd_cntrl_get_integ_trem(CcdCntrl *objs);
gulong ccd_cntrl_get_rpt_rem(CcdCntrl *objs);
guchar ccd_cntrl_get_stat(CcdCntrl *objs);
gboolean ccd_cntrl_stat_err_retry(guchar status);
gboolean ccd_cntrl_stat_err_no_recov(guchar status);
gboolean ccd_cntrl_stat_integrating(guchar status);
gboolean ccd_cntrl_stat_readout(guchar status);
void ccd_cntrl_set_datetime(CcdCntrl *objs, struct datestruct const *unid, struct timestruct const *unit);

G_END_DECLS

#endif   /* __ACQMIR_H__ */
