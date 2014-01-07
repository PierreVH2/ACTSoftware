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
  CCDIMGT_ACQ_OBJ = 1,
  CCDIMGT_ACQ_SKY,
  CCDIMGT_OBJECT,
  CCDIMGT_BIAS,
  CCDIMGT_DARK,
  CCDIMGT_FLAT
};

#define CCDCMD_TYPE                (ccdcmd_get_type())
#define CCDCMD(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), CCDCMD_TYPE, CcdCmd))
#define CCDCMD_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CCDCMD_TYPE, CcdCmdClass))
#define IS_CCDCMD(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), CCDCMD_TYPE))
#define IS_CCDCMD_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CCDCMD_TYPE))

typedef struct _CcdCmd       CcdCmd;
typedef struct _CcdCmdClass  CcdCmdClass;

struct _CcdCmd
{
  GObject parent;
  /// Image type
  unsigned char img_type;
  /// Start x,y of window
  unsigned short win_start_x, win_start_y;
  /// Width and height of window
  unsigned short win_width, win_height;
  /// Prebinning mode
  unsigned short prebin_x, prebin_y;
  /// The length of the exposure (in seconds)
  float exp_t_sec;
  /// Target name and DB id
  char targ_name[MAX_TARGID_LEN];
  unsigned long targ_id;
};

struct _CcdCmdClass
{
  GObjectClass parent_class;
};


#define CCDIMG_TYPE                (ccdimg_get_type())
#define CCDIMG(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), CCDIMG_TYPE, CcdImg))
#define CCDIMG_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CCDIMG_TYPE, CcdImgClass))
#define IS_CCDIMG(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), CCDIMG_TYPE))
#define IS_CCDIMG_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CCDIMG_TYPE))

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
  gfloat exp_t_sec;
  /// Starting date and time of exposure
  struct datestruct start_date;
  struct timestruct start_time;
  /// Target name and DB id
  gchar targ_name[MAX_TARGID_LEN];
  gulong targ_id;
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

GType ccdimg_get_type(void);
CcdImg *ccdimg_new(void);
CcdImg *ccdimg_copy(CcdImg *orig);
void ccdimg_set_img_t(CcdImg *img, guchar new_img_t);
guchar ccdimg_get_img_t(CcdImg *img);
void ccdimg_set_window(CcdImg *img,  gushort new_win_width, gushort new_win_height, gushort new_win_start_x, gushort new_win_start_y);
void ccdimg_get_window(CcdImg *img,  gushort *new_win_width, gushort *new_win_height, gushort *new_win_start_x, gushort *new_win_start_y);


void ccdimg_set_win_width(CcdImg *img, gushort new_win_width);
guchar ccdimg_get_win_width(CcdImg *img);
void ccdimg_set_win_height(CcdImg *img, gushort new_win_height);
guchar ccdimg_get_win_height(CcdImg*img);
void ccdimg_set_win_start
void ccdimg_set_exp_t(CcdImg *img, gfloat new_exp_t_sec);
gfloat ccdimg_get_exp_t(CcdImg *img);
void ccdimg_set_start_date(CcdImg *img, const struct datestruct *new_date);
void ccdimg_get_start_date(CcdImg *img, struct datestruct *cur_date);
void ccdimg_set_start_time(CcdImg *img, const struct timestruct *new_time);
void ccdimg_get_start_time(CcdImg *img, struct timesturct *cur_time);
void ccdimg_set_targ_name(CcdImg *img, const gchar *new_targ_name);
const gchar *ccdimg_get_targ_name(CcdImg *img);
void ccdimg_set_targ_id(CcdImg *img, gulong new_targ_id);
gulong ccdimg_get_targ_id(CcdImg *img);
void ccdimg_set_tel_ra(CcdImg *img, const struct rastruct *new_ra);
const struct rastruct *ccdimg_get_tel_ra(CcdImg *img);
void ccdimg_set_tel_dec(CcdImg *img, const struct decstruct *new_dec);
const struct decstruct *ccdimg_get_tel_dec(CcdImg *img);
void ccdimg_set_image(CcdImg *img, gulong new_img_len, const gfloat *new_image);


#define CCDCNTRL_TYPE                (ccdcntrl_get_type())
#define CCDCNTRL(objs)               (G_TYPE_CHECK_INSTANCE_CAST ((objs), CCDCNTRL_TYPE, CcdCntrl))
#define CCDCNTRL_CLASS(klass)        (G_TYPE_CHECK_CLASS_CAST ((klass), CCDCNTRL_TYPE, CcdCntrlClass))
#define IS_CCDCNTRL(objs)            (G_TYPE_CHECK_INSTANCE_TYPE ((objs), CCDCNTRL_TYPE))
#define IS_CCDCNTRL_CLASS(klass)     (G_TYPE_CHECK_CLASS_TYPE ((klass), CCDCNTRL_TYPE))

typedef struct _CcdCntrl       CcdCntrl;
typedef struct _CcdCntrlClass  CcdCntrlClass;

struct _CcdCntrl
{
  GObject parent;
  GIOChannel *merlin_chan;
  gint merlin_watch_id;
  struct ccd_modes modes;
  gulong rpt_rem;
  GTimer *exp_timer;
};

struct _CcdCntrlClass
{
  GObjectClass parent_class;
};

GType ccdcntrl_get_type (void);
CcdCntrl *ccdcntrl_new (void);
gint ccdcntrl_start_exp(CcdCntrl *objs, guchar img_type, gfloat exp_t_sec, gulong repetitions, const guchar *targ_name, gulong targ_id);
void ccdcntrl_cancel_exp(CcdCntrl *objs);
guchar ccdcntrl_get_stat(CcdCntrl *objs);
gboolean ccdcntrl_stat_error(guchar status);
gboolean ccdcntrl_stat_exp_ordered(guchar status);
gboolean ccdcntrl_stat_integrating(guchar status);
gboolean ccdcntrl_stat_readout(guchar status);

G_END_DECLS

#endif   /* __ACQMIR_H__ */
