#include <gtk/gtk.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES
#include <gtk/gtkgl.h>
#include <GL/gl.h>
#include <act_log.h>
#include "acq_marshallers.h"
#include "ccd_img.h"
#include "imgdisp.h"

#include <time.h>
#include <stdlib.h>

// #define DATETIME_TO_SEC 60
// #define TEL_POS_TO_SEC 60

#define IMGDISP_IMG_TEX    0
#define IMGDISP_LUT_TEX    2

static void imglut_instance_init(GObject *imglut);
static void imglut_class_init(ImglutClass *klass);
static void imglut_instance_dispose(GObject *imglut);

static void imgdisp_instance_init(GtkWidget *imgdisp);
static void imgdisp_instance_destroy(GtkWidget *ccd_cntrl);
static void imgdisp_redraw(GtkWidget *imgdisp);
static gboolean imgdisp_configure(GtkWidget *imgdisp);
static gboolean imgdisp_expose (GtkWidget *imgdisp);
static gboolean create_shaders(Imgdisp *objs);



// static guint imgdisp_signals[LAST_SIGNAL] = { 0 };

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
  act_log_debug(act_log_msg("Creating imgdisp object."));
  return GTK_WIDGET(g_object_new (imgdisp_get_type(), NULL));
}

void imgdisp_set_flip_ns(GtkWidget *imgdisp, gboolean flip_ns)
{
  IMGDISP(imgdisp)->flip_ns = flip_ns;
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_flip_ew(GtkWidget *imgdisp, gboolean flip_ew)
{
  IMGDISP(imgdisp)->flip_ew = flip_ew;
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_bright_lim(GtkWidget *imgdisp, gfloat lim)
{
  IMGDISP(imgdisp)->bright_lim = lim;
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_feint_lim(GtkWidget *imgdisp, gfloat lim)
{
  IMGDISP(imgdisp)->feint_lim = lim;
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_lut(GtkWidget *imgdisp, Imglut *lut)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->lut != NULL)
    g_object_unref(objs->lut);
  objs->lut = lut;
  g_object_ref(lut);

  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to upload new CCD image."));
    return;
  }

  glActiveTexture(GL_TEXTURE0+IMGDISP_LUT_TEX);
  glBindTexture(GL_TEXTURE_2D, objs->lut_gl_name);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, imglut_get_num_points(lut), 1, 0, GL_RGB, GL_FLOAT, &imglut_get_points(objs->lut)[0].red);

  gdk_gl_drawable_gl_end (gldrawable);
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_img(GtkWidget *imgdisp, CcdImg const *img)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not realised yet. Image will be updated when object is realised."));
    return;
  }
  gulong cur_width = objs->dra_ccdimg->allocation.width, cur_height = objs->dra_ccdimg->allocation.height;
  gulong img_width = ccd_img_get_img_width(img), img_height = ccd_img_get_img_height(img);
  if ((cur_width < img_width) || (cur_height < img_height))
  {
    if (cur_width < img_width)
      cur_width = img_width;
    if (cur_height < img_height)
      cur_height = img_height;
    gtk_widget_set_size_request(imgdisp, cur_width, cur_height);
  }
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to upload new CCD image."));
    return;
  }

  glActiveTexture(GL_TEXTURE0+IMGDISP_IMG_TEX);
  glBindTexture(GL_TEXTURE_2D, objs->img_gl_name);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, img_width, img_height, 0, GL_RED, GL_FLOAT, ccd_img_get_img_data(img));

  gdk_gl_drawable_gl_end (gldrawable);
  imgdisp_redraw(imgdisp);
}

gulong imgdisp_coord_pixel_x(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  return 0;
}

gulong imgdisp_coord_pixel_y(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  return 0;
}

gfloat imgdisp_coord_viewport_x(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  return 0.0;
}

gfloat imgdisp_coord_viewport_y(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  return 0.0;
}

gfloat imgdisp_coord_ra(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  return 0.0;
}

gfloat imgdisp_coord_dec(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  return 0.0;
}

static void imgdisp_instance_init(GtkWidget *imgdisp)
{
  act_log_debug(act_log_msg("Initialising imgdisp instance"));
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->flip_ns = objs->flip_ew = FALSE;
  objs->bright_lim = 1.0;
  objs->feint_lim = 0.0;
  objs->img_gl_name = objs->lut_gl_name = 0;
  objs->glsl_prog = 0;
  objs->lut = imglut_new (2, NULL);
  imglut_set_point_rgb(objs->lut, 0, 0.0, 0.0, 0.0);
  imglut_set_point_rgb(objs->lut, 1, 1.0, 1.0, 1.0);
  
  objs->img = NULL;
  
  objs->dra_ccdimg = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(imgdisp), objs->dra_ccdimg);
//   gtk_widget_add_events (imgdisp, GDK_CONFIGURE);
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
  act_log_debug(act_log_msg("Destroying imgdisp instance"));
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
  GtkWidget *dra_ccdimg = IMGDISP(imgdisp)->dra_ccdimg;
  gdk_window_invalidate_rect (dra_ccdimg->window, &dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (dra_ccdimg->window, FALSE);
}

static gboolean imgdisp_configure(GtkWidget *imgdisp)
{
  act_log_debug(act_log_msg("Configuring imgdisp object"));
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
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glViewport (0, 0, width, height);
  glOrtho(-1.0,1.0,-1.0,1.0,-1.0,1.0);
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
    
  /// TODO: Check if this be moved until after glUseProgram?
  GLint ccdimg_loc = glGetUniformLocation(objs->glsl_prog, "ccdimg_tex");
  GLint coltbl_loc = glGetUniformLocation(objs->glsl_prog, "coltbl_tex");
  
  glUseProgram(objs->glsl_prog);
  glUniform1i(ccdimg_loc, IMGDISP_IMG_TEX); //Texture unit 0 is for CCD images.
  glUniform1i(coltbl_loc, IMGDISP_LUT_TEX); //Texture unit 2 is for colour table.
  
  //When rendering an object with this program.
  GLuint tmp_tex_name;
  glGenTextures(1,&tmp_tex_name);
  glActiveTexture(GL_TEXTURE0+IMGDISP_IMG_TEX);
  glBindTexture(GL_TEXTURE_2D, tmp_tex_name);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("Generating random test image."));
    objs->img = g_object_new (imgdisp_get_type(), NULL);
    g_object_ref(objs->img);
    ccd_img_set_img_type(objs->img, IMGT_NONE);
    ccd_img_set_exp_t(objs->img, 0.0);
    ccd_img_set_window(objs->img, 0, 0, width, height, 1, 1);
    srand(time(NULL));
    gfloat img_data[width * height];
    /// TODO: Check if initialisation can be optimised
    guint i;
    for (i=0; i<width * height; i++)
      img_data[i] = rand();
    ccd_img_set_img_data(objs->img, width*height, img_data);
  }
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ccd_img_get_img_width(objs->img), ccd_img_get_img_height(objs->img), 0, GL_RED, GL_FLOAT, ccd_img_get_img_data(objs->img));
  glMatrixMode(GL_MODELVIEW);
  objs->img_gl_name = tmp_tex_name;
  
  glGenTextures(1,&tmp_tex_name);
  glActiveTexture(GL_TEXTURE0+IMGDISP_LUT_TEX);
  glBindTexture(GL_TEXTURE_2D, tmp_tex_name);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  GLfloat lut[6] = { 0.0, 0.0, 0.0, 1.0, 0.0, 1.0 };
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGB, GL_FLOAT, &lut[0]);
  objs->lut_gl_name = tmp_tex_name;
  
  gdk_gl_drawable_gl_end (gldrawable);
  
  return TRUE;
}

static gboolean imgdisp_expose (GtkWidget *imgdisp)
{
  act_log_debug(act_log_msg("Imgdisp object exposed."));
  Imgdisp *objs = IMGDISP(imgdisp);
  gulong width = objs->dra_ccdimg->allocation.width, height = objs->dra_ccdimg->allocation.height;
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (objs->dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (objs->dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to draw GL scene."));
    return TRUE;
  }

  glClearColor(0.0, 0.0, 0.0,1.0);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(objs->glsl_prog);

  // Draw the image from the CCD
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
/*  if ((G_flip_img&IMG_FLIP_EW) == IMG_FLIP_EW)
    glScalef(-1.0, 1.0, 1.0);
  if ((G_flip_img&IMG_FLIP_NS) == IMG_FLIP_NS)
    glScalef(1.0, -1.0, 1.0);*/
  glScaled(1.0, -1.0, 1.0);
  glEnable (GL_TEXTURE_2D);
  glActiveTexture(GL_TEXTURE0+IMGDISP_IMG_TEX);
  glBindTexture(GL_TEXTURE_2D, objs->img_gl_name);
  glColor3ub(0,0,0);
  glBegin (GL_QUADS);
  glTexCoord2f(0.0,0.0); glVertex3d(-1.0,-1.0,0.0);
  glTexCoord2f(1.0,0.0); glVertex3d( 1.0,-1.0,0.0);
  glTexCoord2f(1.0,1.0); glVertex3d( 1.0, 1.0,0.0);
  glTexCoord2f(0.0,1.0); glVertex3d(-1.0, 1.0,0.0);
  glEnd();
  glDisable(GL_TEXTURE_2D);

/*  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if ((G_flip_img&IMG_FLIP_EW) == IMG_FLIP_EW)
    glScalef(-1.0, 1.0, 1.0);
  if ((G_flip_img&IMG_FLIP_NS) == IMG_FLIP_NS)
    glScalef(1.0, -1.0, 1.0);
  glScalef(2.0/(float)width, -2.0/(float)height, 1.0);
  glTranslated(-(float)width/2.0, -(float)height/2.0, 1.0);
  double ang, radius_px = 7;
  glTranslated((double)XAPERTURE, (double)YAPERTURE, 0.0);
  glColor3ub(255,0,0);
  glBegin(GL_LINE_LOOP);
  for (ang = 0.0; ang<=2.0*ONEPI; ang+=2.0*ONEPI/fmax((double)(radius_px),10.0))
    glVertex3d(radius_px/2.0*cos(ang), radius_px/2.0*sin(ang), 0.0);
  glEnd();*/
  
/*  switch(G_grid_type)
  {
    case GRID_VIEWPORT:
    {
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      float cur_line;
      glBegin (GL_LINES);
      for (cur_line = -1.0; cur_line < 1.0; cur_line += GRID_SPACING_VIEWPORT)
      {
        glVertex3f(cur_line, -1.0, 0.0);
        glVertex3f(cur_line, 1.0, 0.0);
      }
      for (cur_line = -1.0; cur_line < 1.0; cur_line += GRID_SPACING_VIEWPORT)
      {
        glVertex3f(-1.0, cur_line, 0.0);
        glVertex3f(1.0, cur_line, 0.0);
      }
      glEnd();
      break;
    }
    case GRID_PIXEL:
    {
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      if ((G_flip_img&IMG_FLIP_EW) == IMG_FLIP_EW)
        glScalef(-1.0, 1.0, 1.0);
      if ((G_flip_img&IMG_FLIP_NS) == IMG_FLIP_NS)
        glScalef(1.0, -1.0, 1.0);
      glScalef(2.0/(float)ccdcntrl_get_max_width(), -2.0/(float)ccdcntrl_get_max_height(), 1.0);
      glTranslated(-(float)ccdcntrl_get_max_width()/2.0, -(float)ccdcntrl_get_max_height()/2.0, 1.0);
      float cur_line;
      glBegin (GL_LINES);
      for (cur_line = 0.0; cur_line < (float)ccdcntrl_get_max_width(); cur_line += GRID_SPACING_PIXEL)
      {
        glVertex3f(cur_line, 0.0, 0.0);
        glVertex3f(cur_line, ccdcntrl_get_max_height(), 0.0);
      }
      for (cur_line = 0.0; cur_line < (float)ccdcntrl_get_max_height(); cur_line += GRID_SPACING_PIXEL)
      {
        glVertex3f(0.0, cur_line, 0.0);
        glVertex3f(ccdcntrl_get_max_width(), cur_line, 0.0);
      }
      glEnd();
      break;
    }
    case GRID_EQUAT:
    {
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();
      if ((G_flip_img&IMG_FLIP_EW) == IMG_FLIP_EW)
        glScaled(-1.0, 1.0, 1.0);
      if ((G_flip_img&IMG_FLIP_NS) == 0)
        glScaled(1.0, -1.0, 1.0);
      double dec_height_rad = convert_DEG_RAD(ccdcntrl_get_dec_height()/3600.0);
      double ra_width_rad = convert_DEG_RAD(ccdcntrl_get_ra_width()/3600.0);
      glScaled(2.0/sin(ra_width_rad), 2.0/sin(dec_height_rad), 1.0);
      glRotated(-(convert_DMS_D_dec(&G_img_dec)+DEC_APER_OFFSET_DEG),1.0,0.0,0.0);
      glRotated(-convert_H_DEG(convert_HMSMS_H_ra(&G_img_ra))+RA_APER_OFFSET_DEG,0.0,1.0,0.0);
      double ra_lim_low, ra_lim_high, ra_spacing, dec_lim_low, dec_lim_high;
      double dec_rad = convert_DEG_RAD(convert_DMS_D_dec(&G_img_dec));
      double ra_rad = convert_H_RAD(convert_HMSMS_H_ra(&G_img_ra));
      dec_lim_low = GRID_SPACING_EQUAT*floor(dec_rad / GRID_SPACING_EQUAT) - GRID_SPACING_EQUAT*ceil(dec_height_rad / GRID_SPACING_EQUAT);
      dec_lim_high = GRID_SPACING_EQUAT*ceil(dec_rad / GRID_SPACING_EQUAT) + GRID_SPACING_EQUAT*ceil(dec_height_rad / GRID_SPACING_EQUAT);
      ra_spacing = GRID_SPACING_EQUAT * round(GRID_SPACING_EQUAT / fabs(cos(dec_rad)) / ONEARCMIN_RAD);
      ra_lim_low = GRID_SPACING_EQUAT * floor(ra_rad / GRID_SPACING_EQUAT) - GRID_SPACING_EQUAT * ceil(ra_width_rad/cos(dec_rad) / GRID_SPACING_EQUAT);
      if (ra_lim_low < -ONEPI)
        ra_lim_low = -ONEPI;
      if (ra_lim_low > TWOPI)
        ra_lim_low = 0.0;
      ra_lim_high = GRID_SPACING_EQUAT * ceil(ra_rad / GRID_SPACING_EQUAT) + GRID_SPACING_EQUAT * ceil(ra_width_rad/cos(dec_rad) / GRID_SPACING_EQUAT);
      if ((ra_lim_high < 0.0) || (ra_lim_high > TWOPI))
        ra_lim_high = TWOPI;
      if ((ra_lim_high-ra_lim_low)/ra_spacing > 30.0)
        ra_spacing = (ra_lim_high-ra_lim_low) / 30.0;
      if (ra_spacing > 15.0 * 60.0 * GRID_SPACING_EQUAT)
        ra_spacing = 15.0 * 60.0 * GRID_SPACING_EQUAT;
      double dec, ra;
      for (dec=dec_lim_low; dec<dec_lim_high; dec+=GRID_SPACING_EQUAT)
      {
        glBegin(GL_LINE_STRIP);
        for (ra=ra_lim_low; ra<ra_lim_high; ra+=ra_spacing)
          glVertex3d(sin(dec+0.5*ONEPI)*sin(ra),cos(dec+0.5*ONEPI),sin(dec+0.5*ONEPI)*cos(ra));
        glVertex3d(sin(dec+0.5*ONEPI)*sin(ra),cos(dec+0.5*ONEPI),sin(dec+0.5*ONEPI)*cos(ra));
        glEnd();
      }
      for (ra=ra_lim_low; ra<ra_lim_high; ra+=ra_spacing)
      {
        glBegin(GL_LINE_STRIP);
        for (dec=dec_lim_low; dec<dec_lim_high; dec+=GRID_SPACING_EQUAT)
          glVertex3d(sin(dec+0.5*ONEPI)*sin(ra),cos(dec+0.5*ONEPI),sin(dec+0.5*ONEPI)*cos(ra));
        glVertex3d(sin(dec+0.5*ONEPI)*sin(ra),cos(dec+0.5*ONEPI),sin(dec+0.5*ONEPI)*cos(ra));
        glEnd();
      }
      break;
    }
  }*/
  
  if (gdk_gl_drawable_is_double_buffered (gldrawable))
    gdk_gl_drawable_swap_buffers (gldrawable);
  else
    glFlush ();

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
  
  const char *frag_src = "varying vec2 texture_coordinate; uniform sampler2D ccdimg_tex; uniform sampler2D coltbl_tex; void main() {  float colidx = texture2D(ccdimg_tex, texture_coordinate).r; gl_FragColor = texture2D(coltbl_tex, vec2(colidx,0)) + gl_Color; }";
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
