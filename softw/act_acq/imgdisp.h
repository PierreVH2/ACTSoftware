#ifndef __ACQ_IMGDISP_H__
#define __ACQ_IMGDISP_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkeventbox.h>
#include <act_ipc.h>
#include "acq_ccdcntrl.h"

typedef struct _LutPoint LutPoint;

struct LutPoint
{
  gfloat value, red, green, blue;
};

G_BEGIN_DECLS

#define ACQ_IMGDISP_TYPE              (acq_imgdisp_get_type())
#define ACQ_IMGDISP(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), ACQ_IMGDISP_TYPE, AcqImgdisp))
#define ACQ_IMGDISP_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), ACQ_IMGDISP_TYPE, AcqImgdispClass))
#define IS_ACQ_IMGDISP(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), ACQ_IMGDISP_TYPE))
#define IS_ACQ_IMGDISP_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), ACQ_IMGDISP_TYPE))

typedef struct _AcqImgdisp       AcqImgdisp;
typedef struct _AcqImgdispClass  AcqImgdispClass;

struct _AcqImgdisp
{
  GtkEventBox parent;
  GtkWidget *dra_ccdimg
  
  gboolean flip_ns, flip_ew;
  gfloat bright_lim, feint_lim;
  AcqImgLut *lut;
  AcqImg *img;
};

struct _AcqImgdispClass
{
  GtkEventBoxClass parent_class;
};

GType acq_imgdisp_get_type (void);
GtkWidget *acq_imgdisp_new (void);
void acq_imgdisp_set_flip_ns(GtkWidget *acq_imgdisp, gboolean flip_ns);
void acq_imgdisp_set_flip_ew(GtkWidget *acq_imgdisp, gboolean flip_ew);
void acq_imgdisp_set_bright_lim(GtkWidget *acq_imgdisp, gfloat lim);
void acq_imgdisp_set_feint_lim(GtkWidget *acq_imgdisp, gfloat lim);
void acq_imgdisp_set_lut(GtkWidget *acq_imgdisp, AcqImgLut *lut);
void acq_imgdisp_set_img(GtkWidget *acq_imgdisp, AcqImg *img);

G_END_DECLS

#endif   /* __ACQ_IMGDISP_H__ */



#ifndef ACQ_IMGDISP_H
#define ACQ_IMGDISP_H

#include <gtk/gtk.h>
#include <act_timecoord.h>

int create_imgdisp_objs(MYSQL *conn, GtkWidget *evb_imgdisp);
void update_imgdisp(struct merlin_img *newimg, struct rastruct *ra, struct decstruct *dec);

#endif
