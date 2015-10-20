#include <gtk/gtk.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES
#include <gtk/gtkgl.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <act_log.h>
#include <act_site.h>
#include "marshallers.h"
#include "ccd_img.h"
#include "imgdisp.h"

#include <time.h>
#include <stdlib.h>

// #define DATETIME_TO_SEC 60
// #define TEL_POS_TO_SEC 60

#define IMGDISP_IMG_TEX    0
#define IMGDISP_LUT_TEX    2

/// Number of arcminutes per radian
#define ONEARCMIN_RAD 0.0002908882086657216
/// Default spacing between Viewport grid lines
#define GRID_SPACING_VIEWPORT  0.2
/// Default spacing between Pixel grid lines
#define GRID_SPACING_PIXEL     50
/// Default spacing between Equitorial grid lines at equator in degrees
#define GRID_SPACING_EQUAT     (1./60.)


static void imglut_instance_init(GObject *imglut);
static void imglut_class_init(ImglutClass *klass);
static void imglut_instance_dispose(GObject *imglut);

static void imgdisp_instance_init(GtkWidget *imgdisp);
static void imgdisp_instance_destroy(GtkWidget *ccd_cntrl);
static void imgdisp_redraw(GtkWidget *imgdisp);
static gboolean imgdisp_configure(GtkWidget *imgdisp);
static gboolean imgdisp_expose (GtkWidget *imgdisp);
static gboolean create_shaders(Imgdisp *objs);
static void update_colour_transl(Imgdisp *objs);

// Imglut function implementation
GType imglut_get_type (void)
{
  static GType imglut_type = 0;
  
  if (!imglut_type)
  {
    const GTypeInfo imglut_info =
    {
      sizeof (ImglutClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) imglut_class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Imglut),
      0,
      (GInstanceInitFunc) imglut_instance_init,
      NULL
    };
    
    imglut_type = g_type_register_static (G_TYPE_OBJECT, "Imglut", &imglut_info, 0);
  }
  
  return imglut_type;
}

Imglut *imglut_new (gulong num_points, LutPoint const *points)
{
  if ((num_points < 2) || (num_points & (num_points - 1)))
  {
    act_log_error(act_log_msg("Invalid number of points for colour lookup table. Number of points must be >=2 and must be an integer power of 2."));
    return NULL;
  }
  Imglut *objs = IMGLUT(g_object_new (imglut_get_type(), NULL));
  objs->num_points = num_points;
  objs->points = malloc(num_points*sizeof(LutPoint));
  if (points != NULL)
    memcpy(objs->points, points, num_points*sizeof(LutPoint));
  return objs;
}

void imglut_set_points(Imglut *objs, gulong num_points, LutPoint const *points)
{
  if ((num_points < 2) || (num_points & (num_points - 1)))
  {
    act_log_error(act_log_msg("Invalid number of points for colour lookup table. Number of points must be >=2 and must be an integer power of 2."));
    return;
  }
  objs->num_points = num_points;
  if (objs->points != NULL)
    free(objs->points);
  objs->points = malloc(num_points*sizeof(LutPoint));
  if (points != NULL)
    memcpy(objs->points, points, num_points*sizeof(LutPoint));
}

void imglut_set_point(Imglut *objs, gulong idx, LutPoint const *point)
{
  if (point == NULL)
    act_log_error(act_log_msg("Invalid input parameters."));
  else
    imglut_set_point_rgb(objs, idx, point->red, point->green, point->blue);
}

void imglut_set_point_rgb(Imglut *objs, gulong idx, gfloat red, gfloat green, gfloat blue)
{
  if (idx >= objs->num_points)
  {
    act_log_debug(act_log_msg("Invalid lookup table index - %d.", idx));
    return;
  }
  objs->points[idx].red = red;
  objs->points[idx].green = green;
  objs->points[idx].blue = blue;
}

void imglut_set_point_value(Imglut *objs, gfloat value, LutPoint const *point)
{
  if ((value < 0.0) || (value > 1.0))
  {
    act_log_debug(act_log_msg("Invalid lookup table value - %f.", value));
    return;
  }
  glong idx = round(value*(objs->num_points-1));
  if (point == NULL)
    act_log_error(act_log_msg("Invalid input parameters."));
  else
    imglut_set_point_value_rgb(objs, idx, point->red, point->green, point->blue);
}

void imglut_set_point_value_rgb(Imglut *objs, gfloat value, gfloat red, gfloat green, gfloat blue)
{
  if ((value < 0.0) || (value > 1.0))
  {
    act_log_debug(act_log_msg("Invalid lookup table value - %f.", value));
    return;
  }
  glong idx = round(value*(objs->num_points-1));
  imglut_set_point_rgb(objs, idx, red, green, blue);
}

gulong imglut_get_num_points(Imglut const *objs)
{
  return objs->num_points;
}

LutPoint const *imglut_get_points(Imglut const *objs)
{
  return objs->points;
}

gfloat const *imglut_get_points_float(Imglut const *objs)
{
  return (gfloat const *)&objs->points[0].red;
}

LutPoint const *imglut_get_point(Imglut const *objs, gulong index)
{
  if (index >= objs->num_points)
  {
    act_log_error(act_log_msg("Invalid lookup table index - %u", index));
    return NULL;
  }
  return &objs->points[index];
}

static void imglut_instance_init(GObject *imglut)
{
  Imglut *objs = IMGLUT(imglut);
  objs->num_points = 0;
  objs->points = NULL;
}

static void imglut_class_init(ImglutClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = imglut_instance_dispose;
}

static void imglut_instance_dispose(GObject *imglut)
{
  Imglut *objs = IMGLUT(imglut);
  objs->num_points = 0;
  if (objs->points != NULL)
  {
    free(objs->points);
    objs->points = NULL;
  }
}


// Imgdisp function implementation
GType imgdisp_get_type (void)
{
  static GType imgdisp_type = 0;
  
  if (!imgdisp_type)
  {
    const GTypeInfo imgdisp_info =
    {
      sizeof (ImgdispClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      NULL, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (Imgdisp),
      0,
      (GInstanceInitFunc) imgdisp_instance_init,
      NULL
    };
    
    imgdisp_type = g_type_register_static (GTK_TYPE_EVENT_BOX, "Imgdisp", &imgdisp_info, 0);
  }
  
  return imgdisp_type;
}

GtkWidget *imgdisp_new (void)
{
//   act_log_debug(act_log_msg("Creating imgdisp object."));
  return GTK_WIDGET(g_object_new (imgdisp_get_type(), NULL));
}

gboolean imgdisp_get_flip_ns(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->flip_ns;
}

void imgdisp_set_flip_ns(GtkWidget *imgdisp, gboolean flip_ns)
{
  IMGDISP(imgdisp)->flip_ns = flip_ns;
  imgdisp_redraw(imgdisp);
}

gboolean imgdisp_get_flip_ew(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->flip_ew;
}

void imgdisp_set_flip_ew(GtkWidget *imgdisp, gboolean flip_ew)
{
  IMGDISP(imgdisp)->flip_ew = flip_ew;
  imgdisp_redraw(imgdisp);
}

gfloat imgdisp_get_bright_lim(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->bright_lim;
}

void imgdisp_set_bright_lim(GtkWidget *imgdisp, gfloat lim)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->bright_lim = lim;

  update_colour_transl(objs);
  imgdisp_redraw(imgdisp);
}

gfloat imgdisp_get_faint_lim(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->faint_lim;
}

void imgdisp_set_faint_lim(GtkWidget *imgdisp, gfloat lim)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->faint_lim = lim;
  
  update_colour_transl(objs);
  imgdisp_redraw(imgdisp);
}

Imglut * imgdisp_get_lut(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->lut;
}

void imgdisp_set_lut(GtkWidget *imgdisp, Imglut *lut)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->lut != NULL)
    g_object_unref(objs->lut);
  objs->lut = lut;
  g_object_ref(lut);

  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not configured yet. LUT will be updated when object is realised."));
    return;
  }
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to upload new CCD image."));
    return;
  }

  glActiveTexture(GL_TEXTURE0+IMGDISP_LUT_TEX);
  glBindTexture(GL_TEXTURE_2D, objs->lut_gl_name);
  gulong lut_num_points = imglut_get_num_points(objs->lut);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, lut_num_points, 1, 0, GL_RGB, GL_FLOAT, imglut_get_points_float(objs->lut));
  
  update_colour_transl(objs);
  gdk_gl_drawable_gl_end (gldrawable);
  imgdisp_redraw(imgdisp);
}

guchar imgdisp_get_grid_type(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->grid_type;
}

gfloat imgdisp_get_grid_spacing_x(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->grid_spacing_x;
}

gfloat imgdisp_get_grid_spacing_y(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->grid_spacing_y;
}

void imgdisp_set_grid(GtkWidget *imgdisp, guchar new_grid, gfloat spacing_x, gfloat spacing_y)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->grid_type = new_grid;
  objs->grid_spacing_x = spacing_x;
  objs->grid_spacing_y = spacing_y;
  imgdisp_redraw(imgdisp);
}

CcdImg * imgdisp_get_img(GtkWidget *imgdisp)
{
  return IMGDISP(imgdisp)->img;
}

void imgdisp_set_img(GtkWidget *imgdisp, CcdImg *img)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->img != NULL)
  {
    g_object_unref(objs->img);
    objs->img = NULL;
  }
  objs->img = img;
  g_object_ref(img);
  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not configured yet. Image will be updated when object is realised."));
    return;
  }
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to upload new CCD image."));
    return;
  }

  act_log_debug(act_log_msg("Updating image texture."));
  glActiveTexture(GL_TEXTURE0+IMGDISP_IMG_TEX);
  glBindTexture(GL_TEXTURE_2D, objs->img_gl_name);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gulong img_width = ccd_img_get_img_width(img), img_height = ccd_img_get_img_height(img);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img_width, img_height, 0, GL_ALPHA, GL_FLOAT, ccd_img_get_img_data(img));

  gdk_gl_drawable_gl_end (gldrawable);
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_window(GtkWidget *imgdisp, glong start_x, glong start_y, gulong width, gulong height)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if ((width == 0) || (height == 0))
    objs->win_start_x = objs->win_start_y = objs->win_width = objs->win_height = 0;
  else
  {
    objs->win_start_x = start_x;
    objs->win_start_y = start_y;
    objs->win_width = width;
    objs->win_height = height;
  }
  imgdisp_redraw(imgdisp);
}

gfloat imgdisp_coord_viewport_x(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  (void)mouse_y;
  GtkAllocation alloc;
  gtk_widget_get_allocation(imgdisp, &alloc);
  return (((gfloat)mouse_x)*2/alloc.width-1.0)*(IMGDISP(imgdisp)->flip_ew ? 1 : -1);
}

gfloat imgdisp_coord_viewport_y(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  (void)mouse_x;
  GtkAllocation alloc;
  gtk_widget_get_allocation(imgdisp, &alloc);
  return (((gfloat)mouse_y)*2/alloc.height-1.0)*(IMGDISP(imgdisp)->flip_ns ? 1 : -1);
}

glong imgdisp_coord_pixel_x(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  return round((imgdisp_coord_viewport_x(imgdisp, mouse_x, mouse_y)/2.0+0.5)*objs->win_width) - objs->win_start_x;
}

glong imgdisp_coord_pixel_y(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  return round((imgdisp_coord_viewport_y(imgdisp, mouse_x, mouse_y)/-2.0+0.5)*objs->win_height) - objs->win_start_y;
}

gfloat imgdisp_coord_ra(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
//   Imgdisp *objs = IMGDISP(imgdisp);
//   if (objs->img == NULL)
//   {
//     act_log_debug(act_log_msg("No image available, cannot calculate mouse RA."));
//     return 0.0;
//   }
// 
//   GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
//   GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);
// 
//   if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
//   {
//     act_log_error(act_log_msg("Could not access GTK drawable GL context to draw GL scene."));
//     return TRUE;
//   }
// 
//   GLint viewport[4];
//   GLdouble modelview[16];
//   GLdouble projection[16];
//   GLfloat winX, winY, winZ;
//   GLdouble posX, posY, posZ;
//   
//   glMatrixMode(GL_MODELVIEW_MATRIX);
//   glPopMatrix();
//   glGetDoublev (GL_MODELVIEW_MATRIX, modelview);
//   glGetDoublev (GL_PROJECTION_MATRIX, projection);
//   glGetIntegerv (GL_VIEWPORT, viewport);
//  
//   winX = (float) imgdisp_coord_pixel_x(imgdisp, mouse_x, mouse_y);
//   winY = (float) viewport[3] - (float)imgdisp_coord_pixel_y(imgdisp, mouse_x, mouse_y);
//   glReadPixels ((int)winX, (int)winY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &winZ);
//  
//   gluUnProject (winX, winY, winZ, modelview, projection, viewport, &posX, &posY, &posZ);
//   glPushMatrix();
//  
//   gdk_gl_drawable_gl_end (gldrawable);
//   
//   return posX;
  gfloat mouse_ra_d;
  gint ret = imgdisp_coord_equat(imgdisp, mouse_x, mouse_y, &mouse_ra_d, NULL);
  if (ret < 0)
    return 0.0;
  return mouse_ra_d;

}

gfloat imgdisp_coord_dec(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
//   Imgdisp *objs = IMGDISP(imgdisp);
//   if (objs->img == NULL)
//   {
//     act_log_debug(act_log_msg("No image available, cannot calculate mouse RA."));
//     return 0.0;
//   }
//   
//   long img_y = imgdisp_coord_pixel_y(imgdisp, mouse_x, mouse_y);
//   gfloat dec;
//   ccd_img_get_tel_pos(objs->img, NULL, &dec);
//   return dec - (img_y - ccd_img_get_img_height(objs->img)/2.0) * ccd_img_get_pixel_size_dec(objs->img) / 3600.0;
  gfloat mouse_dec_d;
  gint ret = imgdisp_coord_equat(imgdisp, mouse_x, mouse_y, NULL, &mouse_dec_d);
  if (ret < 0)
    return 0.0;
  return mouse_dec_d;
}

gint imgdisp_coord_equat(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y, gfloat *mouse_ra_d, gfloat *mouse_dec_d)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("No image available, cannot calculate mouse RA."));
    return -1;
  }

  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to draw GL scene."));
    return -1;
  }

  GLint viewport[4];
  GLdouble modelview[16];
  GLdouble projection[16];
  GLfloat winX, winY;
  GLdouble posX, posY, posZ;
  
  glMatrixMode(GL_MODELVIEW_MATRIX);
  
  glGetDoublev (GL_MODELVIEW_MATRIX, modelview);
  glGetDoublev (GL_PROJECTION_MATRIX, projection);
  glGetIntegerv (GL_VIEWPORT, viewport);
  
//   int i;
//   for (i=0; i<16; i++)
//   {
//     act_log_debug(act_log_msg("%10.5f    %10.5f", modelview[i], projection[i]));
//   }
  
  winX = (float) viewport[2] - (float) mouse_x;
  winY = (float) viewport[3] - (float) mouse_y;
  
  gluUnProject (winX, winY, 1.0, modelview, projection, viewport, &posX, &posY, &posZ);
//   act_log_debug(act_log_msg("Unproject (%f %f ; %lf %lf %lf)", winX, winY, posX, posY, posZ));
 
  gdk_gl_drawable_gl_end (gldrawable);
  
  gfloat ret_ra, ret_dec;
  if (abs(posY) < 0.99999)
  {
    ret_ra = atan2(posX, posZ);
    ret_dec = atan2(posY, pow(pow(posX,2.0)+pow(posZ,2.0),0.5));
  }
  else
  {
    ret_ra = 0.0;
    ret_dec = posY > 0.0 ? ONEPI/2.0 : ONEPI/-2.0;
  }
  
  if (mouse_ra_d != NULL)
    *mouse_ra_d = convert_RAD_DEG(ret_ra);
  if (mouse_dec_d != NULL)
    *mouse_dec_d = convert_RAD_DEG(ret_dec);
  return 0;
}

gfloat imgdisp_get_img_value(GtkWidget *imgdisp, gulong pixel_x, gulong pixel_y)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->img == NULL)
    return -1.0;
  return ccd_img_get_pixel(CCD_IMG(objs->img), pixel_x, pixel_y);
}

static void imgdisp_instance_init(GtkWidget *imgdisp)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->flip_ns = objs->flip_ew = FALSE;
  objs->win_start_x = objs->win_start_y = 0;
  objs->win_width = objs->win_height = 0;
  
  objs->bright_lim = 1.0;
  objs->faint_lim = 0.0;
  objs->grid_type = 0;
  objs->grid_spacing_x = objs->grid_spacing_y = 10.0;
  objs->img_gl_name = objs->lut_gl_name = 0;
  objs->glsl_prog = 0;
  objs->lut = imglut_new (2, NULL);
  imglut_set_point_rgb(objs->lut, 0, 0.0, 0.0, 0.0);
  imglut_set_point_rgb(objs->lut, 1, 1.0, 1.0, 1.0);
  
  objs->img = NULL;
  gtk_widget_add_events (imgdisp, GDK_POINTER_MOTION_MASK);
  
  objs->dra_ccdimg = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(imgdisp), objs->dra_ccdimg);
  gtk_widget_add_events (objs->dra_ccdimg, GDK_EXPOSURE_MASK);
  g_object_ref(objs->dra_ccdimg);
  GdkGLConfig *glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGBA | GDK_GL_MODE_DOUBLE);
  if (glconfig == NULL)
  {
    act_log_error(act_log_msg("Could not find suitable OpenGL configuration."));
    g_object_unref(objs);
    return;
  }
  if (!gtk_widget_set_gl_capability (objs->dra_ccdimg, glconfig, FALSE, TRUE, GDK_GL_RGBA_TYPE))
  {
    act_log_error(act_log_msg("Could not find suitable OpenGL capability."));
    g_object_unref(objs);
    return;
  }
  g_signal_connect_swapped (objs->dra_ccdimg, "configure-event", G_CALLBACK (imgdisp_configure), objs);
  g_signal_connect_swapped (objs->dra_ccdimg, "expose-event", G_CALLBACK (imgdisp_expose), objs);
  g_signal_connect (objs, "destroy", G_CALLBACK(imgdisp_instance_destroy), NULL);
}

static void imgdisp_instance_destroy(GtkWidget *imgdisp)
{
//   act_log_debug(act_log_msg("Destroying imgdisp instance"));
  Imgdisp *objs = IMGDISP(imgdisp);
  
  if (objs->dra_ccdimg != NULL)
  {
    g_object_unref(objs->dra_ccdimg);
    objs->dra_ccdimg = NULL;
  }
  
  if (objs->lut != NULL)
  {
    g_object_unref(objs->lut);
    objs->lut = NULL;
  }
  
  if (objs->img != NULL)
  {
    g_object_unref(objs->img);
    objs->img = NULL;
  }
}

static void imgdisp_redraw(GtkWidget *imgdisp)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not configured yet. Cannot redraw now."));
    return;
  }
  GtkWidget *dra_ccdimg = objs->dra_ccdimg;
  gdk_window_invalidate_rect (dra_ccdimg->window, &dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (dra_ccdimg->window, FALSE);
}

static gboolean imgdisp_configure(GtkWidget *imgdisp)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  gulong width = objs->dra_ccdimg->allocation.width, height = objs->dra_ccdimg->allocation.height;
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);
  
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    printf("Failed to initiate drawing on the OpenGL widget\n");
    return TRUE;
  }
  
  glClearColor(0.0,0.0,0.0,0.0);
  glShadeModel(GL_FLAT);
  glEnable(GL_TEXTURE_2D);
  
  if (objs->glsl_prog == 0)
  {
    if (!create_shaders(objs))
    {
      act_log_error(act_log_msg("Failed to create shader programme - images will not be displayed."));
      gdk_gl_drawable_gl_end (gldrawable);
      return TRUE;
    }
  }
    
  glUseProgram(objs->glsl_prog);
  GLint ccdimg_loc = glGetUniformLocation(objs->glsl_prog, "ccdimg_tex");
  GLint coltbl_loc = glGetUniformLocation(objs->glsl_prog, "coltbl_tex");
  glUniform1i(ccdimg_loc, IMGDISP_IMG_TEX); //Texture unit 0 is for CCD images.
  glUniform1i(coltbl_loc, IMGDISP_LUT_TEX); //Texture unit 2 is for colour table.
  update_colour_transl(objs);
  
  //When rendering an object with this program.
  if (objs->img == NULL)
  {
    objs->img = g_object_new (imgdisp_get_type(), NULL);
    g_object_ref(objs->img);
    ccd_img_set_img_type(objs->img, IMGT_NONE);
    ccd_img_set_integ_t(objs->img, 0.0);
    ccd_img_set_window(objs->img, 0, 0, width, height, 1, 1);
    srand(time(NULL));
    gfloat img_data[width * height];
    memset(img_data, 0, sizeof(gfloat)*width*height);
    ccd_img_set_img_data(objs->img, width*height, img_data);
  }
  GLuint tmp_tex_name;
  glGenTextures(1,&tmp_tex_name);
  glActiveTexture(GL_TEXTURE0+IMGDISP_IMG_TEX);
  glBindTexture(GL_TEXTURE_2D, tmp_tex_name);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ccd_img_get_img_width(objs->img), ccd_img_get_img_height(objs->img), 0, GL_ALPHA, GL_FLOAT, ccd_img_get_img_data(objs->img));
  objs->img_gl_name = tmp_tex_name;
  
  glGenTextures(1,&tmp_tex_name);
  glActiveTexture(GL_TEXTURE0+IMGDISP_LUT_TEX);
  glBindTexture(GL_TEXTURE_2D, tmp_tex_name);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  if (objs->lut == NULL)
  {
    act_log_debug(act_log_msg("Loading default LUT."));
    LutPoint *points = malloc(2*sizeof(LutPoint));
    points[0].red = points[0].green = points[0].blue = 0.0;
    points[1].red = points[1].green = points[1].blue = 1.0;
    objs->lut = imglut_new (2, points);
    g_object_ref(objs->lut);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  gulong lut_num_points = imglut_get_num_points(objs->lut);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, lut_num_points, 1, 0, GL_RGB, GL_FLOAT, imglut_get_points_float(objs->lut));
  GLint coltbl_size_loc = glGetUniformLocation(objs->glsl_prog, "coltbl_size");
  glUniform1f(coltbl_size_loc, lut_num_points); //Size of colour table
  
  objs->lut_gl_name = tmp_tex_name;
  
  gdk_gl_drawable_gl_end (gldrawable);
  
  return TRUE;
}

static gboolean imgdisp_expose (GtkWidget *imgdisp)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not configured yet. Imgdisp expose event will be ignored."));
    return TRUE;
  }
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("No image available."));
    return TRUE;
  }
  gulong dra_width = objs->dra_ccdimg->allocation.width, dra_height = objs->dra_ccdimg->allocation.height;
  gulong img_width = ccd_img_get_img_width(objs->img), img_height = ccd_img_get_img_height(objs->img);
  
  gfloat ra_rad, dec_rad;
  ccd_img_get_tel_pos(objs->img, &ra_rad, &dec_rad);
//   act_log_debug(act_log_msg("Tel RA, Dec:  %f %f", ra_rad, dec_rad));
  ra_rad = convert_DEG_RAD(ra_rad);
  dec_rad = convert_DEG_RAD(dec_rad);
  gdouble img_height_rad = convert_DEG_RAD(img_height*ccd_img_get_pixel_size_dec(objs->img)/3600.0);
  gdouble img_width_rad = convert_DEG_RAD(img_width*ccd_img_get_pixel_size_ra(objs->img)/3600.0);
//   act_log_debug(act_log_msg("Image RA, Dec, Height, Width:  %f %f   %f (%f)  %f (%f)", ra_rad, dec_rad, img_height_rad, img_height*ccd_img_get_pixel_size_dec(objs->img), img_width_rad, img_width*ccd_img_get_pixel_size_ra(objs->img)));
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to draw GL scene."));
    return TRUE;
  }

  glClearColor(0.0, 0.0, 0.0, 1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(objs->glsl_prog);
  
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glViewport (0, 0, dra_width, dra_height);
  glOrtho(1.0,-1.0,1.0,-1.0,1.0,-1.0);
  if (objs->flip_ns)
    glScalef(1.0, -1.0, 1.0);
  if (objs->flip_ew)
    glScalef(-1.0, 1.0, 1.0);
  
  glMatrixMode(GL_MODELVIEW);
  // Set up the matrix for drawing in spherical coordinates and push it for later use (drawing equatorial grid, picking RA and Dec coordinates) 
  glLoadIdentity();
  glScaled(2.0/sin(img_width_rad), -2.0/sin(img_height_rad), 1.0);
  glRotated(convert_RAD_DEG(dec_rad), 1.0, 0.0, 0.0);
  glRotated(-convert_RAD_DEG(ra_rad), 0.0, 1.0, 0.0);
  glPushMatrix();

  // Draw the image from the CCD
  glLoadIdentity();
  glTranslated(-1.0,-1.0,0.0);
  glScalef(2./dra_width, 2./dra_height, 1.0);
  glTranslated(objs->win_start_x,objs->win_start_y,0.0);
  glScalef(dra_width/(float)objs->win_width, dra_height/(float)objs->win_height, 1.0);
  glEnable (GL_TEXTURE_2D);
  glActiveTexture(GL_TEXTURE0+IMGDISP_IMG_TEX);
  glBindTexture(GL_TEXTURE_2D, objs->img_gl_name);
  glColor3f(0.0,0.0,0.0);
  glBegin (GL_QUADS);
  glTexCoord2f(0.0,0.0); glVertex3d(0.0,0.0,0.0);
  glTexCoord2f(1.0,0.0); glVertex3d(img_width,0.0,0.0);
  glTexCoord2f(1.0,1.0); glVertex3d(img_width, img_height,0.0);
  glTexCoord2f(0.0,1.0); glVertex3d(0.0, img_height,0.0);
  glEnd();
  glDisable(GL_TEXTURE_2D);

  glPushMatrix();
  glTranslated(img_width/2.0, img_height/2.0, 0.0);
  double ang, radius_px = 7;
  glColor3f(1.0,0,0);
  glBegin(GL_LINE_LOOP);
  for (ang = 0.0; ang<=2.0*ONEPI; ang+=2.0*ONEPI/fmax((double)(radius_px),10.0))
    glVertex3d(radius_px/2.0*cos(ang), radius_px/2.0*sin(ang), 0.0);
  glEnd();
  glPopMatrix();
  
  switch(objs->grid_type)
  {
    case IMGDISP_GRID_IMAGE:
    {
      if (objs->img == NULL)
      {
        act_log_debug(act_log_msg("Image grid type selected, but no image available."));
        break;
      }
      glMatrixMode(GL_MODELVIEW);
      float cur_line, spacing_x=GRID_SPACING_PIXEL*objs->grid_spacing_x, spacing_y=GRID_SPACING_PIXEL*objs->grid_spacing_y;
      glBegin (GL_LINES);
      for (cur_line = 0.0; cur_line < (float)img_width; cur_line += spacing_x)
      {
        glVertex3f(cur_line, 0.0, 0.0);
        glVertex3f(cur_line, img_height, 0.0);
      }
      for (cur_line = 0.0; cur_line < (float)img_height; cur_line += spacing_y)
      {
        glVertex3f(0.0, cur_line, 0.0);
        glVertex3f(img_width, cur_line, 0.0);
      }
      glEnd();
      break;
    }
    case IMGDISP_GRID_VIEW:
    {
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      float cur_line, spacing_x=GRID_SPACING_VIEWPORT*objs->grid_spacing_x, spacing_y=GRID_SPACING_VIEWPORT*objs->grid_spacing_y;
      glBegin (GL_LINES);
      for (cur_line = -1.0; cur_line < 1.0; cur_line += spacing_x)
      {
        glVertex3f(cur_line, -1.0, 0.0);
        glVertex3f(cur_line, 1.0, 0.0);
      }
      for (cur_line = -1.0; cur_line < 1.0; cur_line += spacing_y)
      {
        glVertex3f(-1.0, cur_line, 0.0);
        glVertex3f(1.0, cur_line, 0.0);
      }
      glEnd();
      break;
    }
    case IMGDISP_GRID_EQUAT:
    {
      double ra_cent, dec_cent;
      double ra_low, ra_high, dec_low, dec_high;
      double dec_inc, ra_inc, num_divs;
      
      dec_inc = convert_DEG_RAD(objs->grid_spacing_y*GRID_SPACING_EQUAT);
      num_divs = ceil(img_height_rad / dec_inc) + 1.0;
      dec_cent = dec_rad - fmod(dec_rad, dec_inc);
      dec_low = dec_cent - num_divs*dec_inc;
      dec_high = dec_cent + num_divs*dec_inc;
      // Check if we're straddling the pole
      if ((fabs(dec_low) >= ONEPI/2) != (fabs(dec_high) >= ONEPI/2))
      {
        // Set the dec that exceeds the pole to the dec of the pole.
        if (fabs(dec_high) >= ONEPI/2)
          dec_high = ONEPI/2 * (dec_high < 0.0 ? -1.0 : 1.0);
        else
          dec_low = ONEPI/2 * (dec_low < 0.0 ? -1.0 : 1.0);
        num_divs = ceil(img_width_rad / convert_DEG_RAD(objs->grid_spacing_x*GRID_SPACING_EQUAT)) * 2.0;
        ra_inc = TWOPI / num_divs;
        ra_low = 0.0;
        ra_high = TWOPI + ra_inc/10.0;
      }
      else
      {
        ra_inc = objs->grid_spacing_x*GRID_SPACING_EQUAT / cos(dec_rad);
        ra_inc -= fmod(ra_inc, objs->grid_spacing_x*GRID_SPACING_EQUAT);
        ra_inc = convert_DEG_RAD(ra_inc);
        if (fabs(dec_low) < fabs(dec_high))
          num_divs = ceil(img_width_rad / ra_inc / cos(dec_high)) + 1.0;
        else
          num_divs = ceil(img_width_rad / ra_inc / cos(dec_low)) + 1.0;
        ra_cent = ra_rad - fmod(ra_rad, ra_inc);
        ra_low = ra_cent - num_divs*ra_inc;
        ra_high = ra_cent + num_divs*ra_inc;
      }
      
      glMatrixMode(GL_MODELVIEW);
      glPopMatrix();

      double line_ra, line_dec;
      for (line_ra=ra_low; line_ra<=ra_high; line_ra+=ra_inc)
      {
        glBegin(GL_LINE_STRIP);
        glVertex3d(sin(line_ra)*cos(dec_low),sin(dec_low),cos(line_ra)*cos(dec_low));
        glVertex3d(sin(line_ra)*cos(dec_high),sin(dec_high),cos(line_ra)*cos(dec_high));
        glEnd();
      }
      
      for (line_dec=dec_low; line_dec<=dec_high; line_dec+=dec_inc)
      {
        glBegin(GL_LINE_STRIP);
        for (line_ra=ra_low; line_ra<=ra_high; line_ra+=ra_inc/10.0)
          glVertex3d(sin(line_ra)*cos(line_dec),sin(line_dec),cos(line_ra)*cos(line_dec));
        glEnd();
      }
      
      glPushMatrix();

      break;
    }
  }
  
  if (gdk_gl_drawable_is_double_buffered (gldrawable))
    gdk_gl_drawable_swap_buffers (gldrawable);
  else
    glFlush ();

  // Pop last modelview matrix so equatorial matrix is on top of stack
  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  gdk_gl_drawable_gl_end (gldrawable);

  return TRUE;
}

static gboolean create_shaders(Imgdisp *objs)
{
  GLuint glsl_prog = glCreateProgram();
  if (glsl_prog == 0)
  {
    act_log_error(act_log_msg("Unable to create GL programme."));
    return FALSE;
  }
  GLuint shader;
  GLint length, result;
  
  const char *vert_src = "varying vec2 texture_coordinate; void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;  texture_coordinate = vec2(gl_MultiTexCoord0); gl_FrontColor = gl_Color; }";
  shader = glCreateShader(GL_VERTEX_SHADER);
  if (shader == 0)
  {
    act_log_error(act_log_msg("Unable to create vertex shader object."));
    glDeleteProgram(glsl_prog);
    return FALSE;
  }
  length = strlen(vert_src);
  glShaderSource(shader, 1, &vert_src, &length);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
  if (result == GL_FALSE) 
  {
    char *log;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    log = malloc(length);
    glGetShaderInfoLog(shader, length, &result, log);
    act_log_error(act_log_msg("Unable to compile vertex shader: %s", log));
    free(log);
    glDeleteShader(shader);
    glDeleteProgram(glsl_prog);
    return FALSE;
  }
  glAttachShader(glsl_prog, shader);
  glDeleteShader(shader);
  
  const char *frag_src = "varying vec2 texture_coordinate; uniform sampler2D ccdimg_tex; uniform sampler2D coltbl_tex; uniform float scale; uniform float offset; void main() {  float colidx = texture2D(ccdimg_tex, texture_coordinate).a * scale + offset; vec4 texcol = texture2D(coltbl_tex, vec2(colidx,0)); gl_FragColor = texcol + gl_Color; }";
  shader = glCreateShader(GL_FRAGMENT_SHADER);
  if (shader == 0)
  {
    act_log_error(act_log_msg("Unable to create fragment shader object."));
    glDeleteProgram(glsl_prog);
    return FALSE;
  }
  length = strlen(frag_src);
  glShaderSource(shader, 1, &frag_src, &length);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
  if (result == GL_FALSE) 
  {
    char *log;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    log = malloc(length);
    glGetShaderInfoLog(shader, length, &result, log);
    act_log_error(act_log_msg("Unable to compile fragmet shader: %s", log));
    free(log);
    glDeleteShader(shader);
    glDeleteProgram(glsl_prog);
    return FALSE;
  }
  glAttachShader(glsl_prog, shader);
  glDeleteShader(shader);
  
  glLinkProgram(glsl_prog);
  glGetProgramiv(glsl_prog, GL_LINK_STATUS, &result);
  if (result == GL_FALSE) 
  {
    GLint length;
    char *log;
    glGetProgramiv(glsl_prog, GL_INFO_LOG_LENGTH, &length);
    log = malloc(length);
    glGetProgramInfoLog(glsl_prog, length, &result, log);
    act_log_error(act_log_msg("Unable to link GL shader programme: %s", log));
    free(log);
    glDeleteProgram(glsl_prog);
    return FALSE;
  }
  objs->glsl_prog = glsl_prog;
  return TRUE;
}

static void update_colour_transl(Imgdisp *objs)
{
  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not configured yet. Bright limit will be set when object is realised."));
    return;
  }
  if (objs->lut == NULL)
  {
    act_log_debug(act_log_msg("No colour lookup table available yet. Cannot calculate colour translation parameters."));
    return;
  }
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to upload new CCD image."));
    return;
  }
  glUseProgram(objs->glsl_prog);
  
  float num, scale, offset;
  num = 1.0 / imglut_get_num_points(objs->lut);
  if (fabs(objs->bright_lim - objs->faint_lim) > 0.0001)
    scale = (1.0 - num) / (objs->bright_lim - objs->faint_lim);
  else
    scale = (1.0 - num) * 10000;
  offset = 0.5*num - objs->faint_lim*scale;
  
  GLint scale_loc = glGetUniformLocation(objs->glsl_prog, "scale");
  GLint offset_loc = glGetUniformLocation(objs->glsl_prog, "offset");
  glUniform1f(scale_loc, scale);
  glUniform1f(offset_loc, offset);
  gdk_gl_drawable_gl_end (gldrawable);
}
