#ifndef __CCD_IMG_H__
#define __CCD_IMG_H__

#include <glib.h>
#include <glib-object.h>
#include <act_timecoord.h>

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
  /// Telescope coordinates at exposure start
  struct rastruct tel_ra;
  struct decstruct tel_dec;
  /// Size of pixels in arcseconds
  gfloat pix_size_ra;
  gfloat pix_size_dec;
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
guchar ccd_img_get_img_type(CcdImg const *objs);
void ccd_img_set_img_type(CcdImg *objs, guchar img_type);
gfloat ccd_img_get_exp_t(CcdImg const *objs);
void ccd_img_set_exp_t(CcdImg *objs, gfloat exp_t_s);
gushort ccd_img_get_img_width(CcdImg const *objs);
gushort ccd_img_get_img_height(CcdImg const *objs);
gushort ccd_img_get_win_start_x(CcdImg const *objs);
gushort ccd_img_get_win_start_y(CcdImg const *objs);
gushort ccd_img_get_win_width(CcdImg const *objs);
gushort ccd_img_get_win_height(CcdImg const *objs);
gushort ccd_img_get_prebin_x(CcdImg const *objs);
gushort ccd_img_get_prebin_y(CcdImg const *objs);
gfloat ccd_img_get_pixel_size_ra(CcdImg const *objs);
gfloat ccd_img_get_pixel_size_dec(CcdImg const *objs);
void ccd_img_set_window(CcdImg *objs, gushort win_start_x, gushort win_start_y, gushort win_width, gushort win_height, gushort prebin_x, gushort prebin_y);
void ccd_img_get_start_datetime(CcdImg const *objs, struct datestruct *start_unid, struct timestruct *start_unit);
void ccd_img_set_start_datetime(CcdImg *objs, struct datestruct const *start_unid, struct timestruct const *start_unit);
gchar const *ccd_img_get_targ_name(CcdImg const *objs);
gulong ccd_img_get_targ_id(CcdImg const *objs);
void ccd_img_set_target(CcdImg *objs, gulong targ_id, gchar const *targ_name);
gchar const *ccd_img_get_user_name(CcdImg const *objs);
gulong ccd_img_get_user_id(CcdImg const *objs);
void ccd_img_set_user(CcdImg *objs, gulong user_id, gchar const *user_name);
void ccd_img_get_tel_pos(CcdImg const *objs, struct rastruct *tel_ra, struct decstruct *tel_dec);
void ccd_img_set_tel_pos(CcdImg *objs, struct rastruct const *tel_ra, struct decstruct const *tel_dec);
gulong ccd_img_get_img_len(CcdImg const *objs);
gfloat const *ccd_img_get_img_data(CcdImg const *objs);
void ccd_img_set_img_data(CcdImg *objs, gulong img_len, gfloat const *img_data);

G_END_DECLS

#endif   /* __CCD_IMG_H__ */
