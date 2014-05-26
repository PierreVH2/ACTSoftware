#include <gtk/gtk.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include "ccd_img.h"

static void ccd_img_instance_init(GObject *ccd_img);
static void ccd_img_class_init(CcdImgClass *klass);
static void ccd_img_instance_dispose(GObject *ccd_img);

// CCD Image implementation
GType ccd_img_get_type(void)
{
  static GType ccd_img_type = 0;
  
  if (!ccd_img_type)
  {
    const GTypeInfo ccd_img_info =
    {
      sizeof (CcdImgClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) ccd_img_class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (CcdImg),
      0,
      (GInstanceInitFunc) ccd_img_instance_init,
      NULL
    };
    
    ccd_img_type = g_type_register_static (G_TYPE_OBJECT, "CcdImg", &ccd_img_info, 0);
  }
  
  return ccd_img_type;
}

guchar ccd_img_get_img_type(CcdImg const *objs)
{
  return objs->img_type;
}

void ccd_img_set_img_type(CcdImg *objs, guchar img_type)
{
  objs->img_type = img_type;
}

gfloat ccd_img_get_exp_t(CcdImg const *objs)
{
  return objs->exp_t_s;
}

void ccd_img_set_exp_t(CcdImg *objs, gfloat exp_t_s)
{
  objs->exp_t_s = exp_t_s;
}

gushort ccd_img_get_img_width(CcdImg const *objs)
{
  return floor ((float)objs->win_width/objs->prebin_x);
}

gushort ccd_img_get_img_height(CcdImg const *objs)
{
  return floor((float)objs->win_height/objs->prebin_y);
}

gushort ccd_img_get_win_start_x(CcdImg const *objs)
{
  return objs->win_start_x;
}

gushort ccd_img_get_win_start_y(CcdImg const *objs)
{
  return objs->win_start_y;
}

gushort ccd_img_get_win_width(CcdImg const *objs)
{
  return objs->win_width;
}

gushort ccd_img_get_win_height(CcdImg const *objs)
{
  return objs->win_height;
}

gushort ccd_img_get_prebin_x(CcdImg const *objs)
{
  return objs->prebin_x;
}

gushort ccd_img_get_prebin_y(CcdImg const *objs)
{
  return objs->prebin_y;
}

void ccd_img_set_window(CcdImg *objs, gushort win_start_x, gushort win_start_y, gushort win_width, gushort win_height, gushort prebin_x, gushort prebin_y)
{
  objs->win_start_x = win_start_x;
  objs->win_start_y = win_start_y;
  objs->win_width = win_width;
  objs->win_height = win_height;
  objs->prebin_x = prebin_x;
  objs->prebin_y = prebin_y;
}

void ccd_img_set_pixel_size(CcdImg *objs, gfloat size_ra_asec, gfloat size_dec_asec)
{
  objs->pix_size_ra = size_ra_asec;
  objs->pix_size_dec = size_dec_asec;
}

gfloat ccd_img_get_pixel_size_ra(CcdImg const *objs)
{
  return objs->pix_size_ra;
}

gfloat ccd_img_get_pixel_size_dec(CcdImg const *objs)
{
  return objs->pix_size_dec;
}

void ccd_img_get_start_datetime(CcdImg const *objs, struct datestruct *start_unid, struct timestruct *start_unit)
{
  if (start_unid != NULL)
    memcpy(start_unid, &objs->start_unid, sizeof(struct datestruct));
  if (start_unit != NULL)
    memcpy(start_unit, &objs->start_unit, sizeof(struct timestruct));
}

void ccd_img_set_start_datetime(CcdImg *objs, struct datestruct const *start_unid, struct timestruct const *start_unit)
{
  memcpy(&objs->start_unid, start_unid, sizeof(struct datestruct));
  memcpy(&objs->start_unit, start_unit, sizeof(struct timestruct));
}

gchar const *ccd_img_get_targ_name(CcdImg const *objs)
{
  return objs->targ_name;
}

gulong ccd_img_get_targ_id(CcdImg const *objs)
{
  return objs->targ_id;
}

void ccd_img_set_target(CcdImg *objs, gulong targ_id, gchar const *targ_name)
{
  objs->targ_id = targ_id;
  if (objs->targ_name != NULL)
    g_free(objs->targ_name);
  objs->targ_name = g_strdup(targ_name);
}

gchar const *ccd_img_get_user_name(CcdImg const *objs)
{
  return objs->user_name;
}

gulong ccd_img_get_user_id(CcdImg const *objs)
{
  return objs->user_id;
}

void ccd_img_set_user(CcdImg *objs, gulong user_id, gchar const *user_name)
{
  objs->user_id = user_id;
  if (objs->user_name != NULL)
    g_free(objs->user_name);
  objs->user_name = g_strdup(user_name);
}

void ccd_img_get_tel_pos(CcdImg const *objs, struct rastruct *tel_ra, struct decstruct *tel_dec)
{
  if (tel_ra != NULL)
    memcpy(tel_ra, &objs->tel_ra, sizeof(struct rastruct));
  if (tel_dec != NULL)
    memcpy(tel_dec, &objs->tel_dec, sizeof(struct decstruct));
}

void ccd_img_set_tel_pos(CcdImg *objs, struct rastruct const *tel_ra, struct decstruct const *tel_dec)
{
  memcpy(&objs->tel_ra, tel_ra, sizeof(struct rastruct));
  memcpy(&objs->tel_dec, tel_dec, sizeof(struct decstruct));
}

gulong ccd_img_get_img_len(CcdImg const *objs)
{
  return objs->img_len;
}

gfloat const *ccd_img_get_img_data(CcdImg const *objs)
{
  return objs->img_data;
}

void ccd_img_set_img_data(CcdImg *objs, gulong img_len, gfloat const *img_data)
{
  objs->img_len = img_len;
  if (objs->img_data != NULL)
    g_free(objs->img_data);
  objs->img_data = malloc(img_len*sizeof(gfloat));
  memcpy(objs->img_data, img_data, img_len*sizeof(gfloat));
}

static void ccd_img_instance_init(GObject *ccd_img)
{
  CcdImg *objs = CCD_IMG(ccd_img);
  objs->img_type = IMGT_NONE;
  objs->win_start_x = objs->win_start_y = 0;
  objs->win_width = objs->win_height = 0;
  objs->exp_t_s = 0.0;
  memset(&objs->start_unid, 0, sizeof(struct datestruct));
  memset(&objs->start_unit, 0, sizeof(struct timestruct));
  objs->targ_name = NULL;
  objs->targ_id = 0;
  objs->user_name = NULL;
  objs->user_id = 0;
  memset(&objs->tel_ra, 0, sizeof(struct rastruct));
  memset(&objs->tel_dec, 0, sizeof(struct decstruct));
  objs->pix_size_ra = objs->pix_size_dec = 0.0;
  objs->img_len = 0;
  objs->img_data = NULL;
}

static void ccd_img_class_init(CcdImgClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = ccd_img_instance_dispose;
}

static void ccd_img_instance_dispose(GObject *ccd_img)
{
  CcdImg *objs = CCD_IMG(ccd_img);
  if (objs->targ_name != NULL)
  {
    g_free(objs->targ_name);
    objs->targ_name = NULL;
  }
  if (objs->img_data != NULL)
  {
    g_free(objs->img_data);
    objs->img_data = NULL;
  }
  objs->img_len = 0;
  objs->img_type = IMGT_NONE;
}

