#include <gtk/gtk.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#define GL_GLEXT_PROTOTYPES
#include <gtk/gtkgl.h>
#include <GL/gl.h>
#include <act_log.h>
#include <act_site.h>
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
static void update_colour_transl(Imgdisp *objs);
// static glong img_x(Imgdisp *objs, gulong x);
// static glong img_y(Imgdisp *objs, gulong y);

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
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->bright_lim = lim;

  update_colour_transl(objs);
  imgdisp_redraw(imgdisp);
}

void imgdisp_set_faint_lim(GtkWidget *imgdisp, gfloat lim)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->faint_lim = lim;
  
  update_colour_transl(objs);
  imgdisp_redraw(imgdisp);
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

void imgdisp_set_grid(GtkWidget *imgdisp, guchar new_grid)
{
  IMGDISP(imgdisp)->grid_type = grid_type;
  imgdisp_redraw(imgdisp);
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
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("No image available, cannot calculate mouse RA."));
    return 0.0;
  }
  
  long img_x = imgdisp_coord_pixel_x(imgdisp, mouse_x, mouse_y);
  double img_dec = imgdisp_coord_dec(imgdisp, mouse_x, mouse_y);
  if (fabs(img_dec) > 89.99)
    img_dec = 89.99;
  struct rastruct ra;
  ccd_img_get_tel_pos(objs->img, &ra, NULL);
  return convert_HMSMS_H_ra(&ra) + (img_x - ccd_img_get_img_width(objs->img)/2.0) * ccd_img_get_pixel_size_ra(objs->img) / cos(convert_DEG_RAD(img_dec)) / 3600.0;
}

gfloat imgdisp_coord_dec(GtkWidget *imgdisp, gulong mouse_x, gulong mouse_y)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("No image available, cannot calculate mouse RA."));
    return 0.0;
  }
  
  long img_y = imgdisp_coord_pixel_y(imgdisp, mouse_x, mouse_y);
  struct decstruct dec;
  ccd_img_get_tel_pos(objs->img, NULL, &dec);
  return convert_DMS_D_dec(&dec) - (img_y - ccd_img_get_img_height(objs->img)/2.0) * ccd_img_get_pixel_size_dec(objs->img) / 3600.0;
}

static void imgdisp_instance_init(GtkWidget *imgdisp)
{
  Imgdisp *objs = IMGDISP(imgdisp);
  objs->flip_ns = objs->flip_ew = FALSE;
  objs->win_start_x = objs->win_start_y = 0;
  objs->win_width = objs->win_height = 0;
  
  objs->bright_lim = 1.0;
  objs->faint_lim = 0.0;
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
  Imgdisp *objs = IMGDISP(imgdisp);
  if (objs->glsl_prog == 0)
  {
    act_log_debug(act_log_msg("Imgdisp widget not configured yet. Bright limit will be set when object is realised."));
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
    ccd_img_set_exp_t(objs->img, 0.0);
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
  
  // Draw the image from the CCD
  glMatrixMode(GL_MODELVIEW);
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
  
  switch(G_grid_type)
  {
    case GRID_IMAGE:
    {
      if (objs->img == NULL)
      {
        act_log_debug(act_log_msg("Image grid type selected, but no image available."));
        break;
      }
      glMatrixMode(GL_MODELVIEW);
//       glLoadIdentity();
//       glScalef(2.0/(float)ccdcntrl_get_max_width(), -2.0/(float)ccdcntrl_get_max_height(), 1.0);
//       glTranslated(-(float)ccdcntrl_get_max_width()/2.0, -(float)ccdcntrl_get_max_height()/2.0, 1.0);
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
    case GRID_VIEW:
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
  }
  
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
  
  float coltbl_size = imglut_get_num_points(objs->lut);
  float scale = (coltbl_size - 1.0) / coltbl_size * (objs->bright_lim - objs->faint_lim);
  float offset = (coltbl_size - 1.0) / coltbl_size * objs->faint_lim + 1.0 / (2.0 * coltbl_size); 
  
  GLint scale_loc = glGetUniformLocation(objs->glsl_prog, "scale");
  GLint offset_loc = glGetUniformLocation(objs->glsl_prog, "offset");
  glUniform1f(scale_loc, scale);
  glUniform1f(offset_loc, offset);
  gdk_gl_drawable_gl_end (gldrawable);
}

/*
static glong img_x(Imgdisp *objs, gulong x)
{
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("No image available, cannot pixel X coordinate."));
    return 0;
  }
  return objs->flip_ew ? ccd_img_get_img_width(objs->img)-x : x;
}

static glong img_y(Imgdisp *objs, gulong y)
{
  if (objs->img == NULL)
  {
    act_log_debug(act_log_msg("No image available, cannot pixel Y coordinate."));
    return 0;
  }
  return objs->flip_ns ? ccd_img_get_img_height(objs->img)-y : y;
}
*/

