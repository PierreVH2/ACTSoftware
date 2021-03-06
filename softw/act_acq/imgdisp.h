#ifndef __IMGDISP_H__
#define __IMGDISP_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkeventbox.h>
#include "ccd_img.h"

G_BEGIN_DECLS

typedef struct _LutPoint  LutPoint;

struct _LutPoint
{
  gfloat red, green, blue;
};

#define IMGLUT_TYPE              (imglut_get_type())
#define IMGLUT(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), IMGLUT_TYPE, Imglut))
#define IMGLUT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), IMGLUT_TYPE, ImglutClass))
#define IS_IMGLUT(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), IMGLUT_TYPE))
#define IS_IMGLUT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), IMGLUT_TYPE))

typedef struct _Imglut       Imglut;
typedef struct _ImglutClass  ImglutClass;

struct _Imglut
{
  GObject parent;
  
  gulong num_points;
  LutPoint *points;
};

struct _ImglutClass
{
  GObjectClass parent_class;
};

GType imglut_get_type (void);
Imglut *imglut_new (gulong num_points, LutPoint const *points);
void imglut_set_points(Imglut *objs, gulong num_points, LutPoint const *points);
void imglut_set_point(Imglut *objs, gulong idx, LutPoint const *point);
void imglut_set_point_rgb(Imglut *objs, gulong idx, gfloat red, gfloat green, gfloat blue);
void imglut_set_point_value(Imglut *objs, gfloat value, LutPoint const *point);
void imglut_set_point_value_rgb(Imglut *objs, gfloat value, gfloat red, gfloat green, gfloat blue);
gulong imglut_get_num_points(Imglut const *objs);
LutPoint const *imglut_get_points(Imglut const *objs);
gfloat const *imglut_get_points_float(Imglut const *objs);
LutPoint const *imglut_get_point(Imglut const *objs, gulong index);


#define IMGDISP_TYPE              (imgdisp_get_type())
#define IMGDISP(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), IMGDISP_TYPE, Imgdisp))
#define IMGDISP_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), IMGDISP_TYPE, ImgdispClass))
#define IS_IMGDISP(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), IMGDISP_TYPE))
#define IS_IMGDISP_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), IMGDISP_TYPE))

enum
{
  IMGDISP_GRID_NONE = 0,
  IMGDISP_GRID_IMAGE,
  IMGDISP_GRID_VIEW,
  IMGDISP_GRID_EQUAT,
};

typedef struct _Imgdisp       Imgdisp;
typedef struct _ImgdispClass  ImgdispClass;

struct _Imgdisp
{
  GtkEventBox parent;
  GtkWidget *dra_ccdimg;
  
  glong win_start_x, win_start_y;
  gulong win_width, win_height;
  gboolean flip_ns, flip_ew;
  gfloat bright_lim, faint_lim;
  guchar grid_type;
  gfloat grid_spacing_x, grid_spacing_y;
  Imglut *lut;
  CcdImg *img;
  guint img_gl_name, lut_gl_name;
  guint glsl_prog;
};

struct _ImgdispClass
{
  GtkEventBoxClass parent_class;
};

GType imgdisp_get_type (void);
GtkWidget *imgdisp_new (void);
gboolean imgdisp_get_flip_ns(GtkWidget *imgdisp);
void imgdisp_set_flip_ns(GtkWidget *imgdisp, gboolean flip_ns);
gboolean imgdisp_get_flip_ew(GtkWidget *imgdisp);
void imgdisp_set_flip_ew(GtkWidget *imgdisp, gboolean flip_ew);
gfloat imgdisp_get_bright_lim(GtkWidget *imgdisp);
void imgdisp_set_bright_lim(GtkWidget *imgdisp, gfloat lim);
gfloat imgdisp_get_faint_lim(GtkWidget *imgdisp);
void imgdisp_set_faint_lim(GtkWidget *imgdisp, gfloat lim);
Imglut * imgdisp_get_lut(GtkWidget *imgdisp);
void imgdisp_set_lut(GtkWidget *imgdisp, Imglut *lut);
guchar imgdisp_get_grid_type(GtkWidget *imgdisp);
gfloat imgdisp_get_grid_spacing_x(GtkWidget *imgdisp);
gfloat imgdisp_get_grid_spacing_y(GtkWidget *imgdisp);
void imgdisp_set_grid(GtkWidget *imgdisp, guchar new_grid, gfloat spacing_x, gfloat spacing_y);
CcdImg * imgdisp_get_img(GtkWidget *imgdisp);
void imgdisp_set_img(GtkWidget *imgdisp, CcdImg *img);
void imgdisp_set_window(GtkWidget *imgdisp, glong start_x, glong start_y, gulong width, gulong height);
gfloat imgdisp_coord_viewport_x(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y);
gfloat imgdisp_coord_viewport_y(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y);
glong imgdisp_coord_pixel_x(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y);
glong imgdisp_coord_pixel_y(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y);
gfloat imgdisp_coord_ra(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y);
gfloat imgdisp_coord_dec(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y);
gint imgdisp_coord_equat(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y, gfloat *mouse_ra_d, gfloat *mouse_dec_d);
gfloat imgdisp_get_img_value(GtkWidget *imgdisp, gulong pixel_x, gulong pixel_y);

G_END_DECLS

#endif   /* __IMGDISP_H__ */
