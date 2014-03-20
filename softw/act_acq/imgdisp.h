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
  gfloat value;
  gfloat red, green, blue, alpha;
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


#define IMGDISP_TYPE              (imgdisp_get_type())
#define IMGDISP(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), IMGDISP_TYPE, Imgdisp))
#define IMGDISP_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), IMGDISP_TYPE, ImgdispClass))
#define IS_IMGDISP(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), IMGDISP_TYPE))
#define IS_IMGDISP_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), IMGDISP_TYPE))

typedef struct _Imgdisp       Imgdisp;
typedef struct _ImgdispClass  ImgdispClass;

struct _Imgdisp
{
  GtkEventBox parent;
  GtkWidget *dra_ccdimg
  
  gboolean flip_ns, flip_ew;
  gfloat bright_lim, feint_lim;
  ImgLut *lut;
  CcdImg *img;
};

struct _ImgdispClass
{
  GtkEventBoxClass parent_class;
};

GType imgdisp_get_type (void);
GtkWidget *imgdisp_new (void);
void imgdisp_set_flip_ns(GtkWidget *imgdisp, gboolean flip_ns);
void imgdisp_set_flip_ew(GtkWidget *imgdisp, gboolean flip_ew);
void imgdisp_set_bright_lim(GtkWidget *imgdisp, gfloat lim);
void imgdisp_set_feint_lim(GtkWidget *imgdisp, gfloat lim);
void imgdisp_set_lut(GtkWidget *imgdisp, ImgLut const *lut);
void imgdisp_set_img(GtkWidget *imgdisp, CcdImg const *img);

G_END_DECLS

#endif   /* __IMGDISP_H__ */
