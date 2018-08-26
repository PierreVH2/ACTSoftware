#include <gtk/gtk.h>
#define GL_GLEXT_PROTOTYPES
#include <gtk/gtkgl.h>
#include <GL/gl.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <merlin_driver.h>
#include <ccd_defs.h>
#include <act_log.h>
#include <act_site.h>
#include <act_timecoord.h>
#include "acq_ccdcntrl.h"
#include "acq_imgdisp.h"

// ??? Try embedded PNG images for brightness and contrast icons

/// Number of arcminutes per radian
#define ONEARCMIN_RAD 0.0002908882086657216
/// Spacing between Viewport grid lines
#define GRID_SPACING_VIEWPORT  0.2
/// Spacing between Pixel grid lines
#define GRID_SPACING_PIXEL     50
/// Spacing between Equitorial grid lines at equator
#define GRID_SPACING_EQUAT     ONEARCMIN_RAD
/// Flip image North-South
#define IMG_FLIP_NS            0x1
/// Flip image East-West
#define IMG_FLIP_EW            0x2
/// RA offset of aperture from RA of centre of image, in degrees
#define RA_APER_OFFSET_DEG (XAPERTURE - ccdcntrl_get_max_width()/2.0) * ccdcntrl_get_ra_width() / ccdcntrl_get_max_width() / 3600
/// Dec offset of aperture from Dec of centre of image, in degrees
#define DEC_APER_OFFSET_DEG (YAPERTURE - ccdcntrl_get_max_height()/2.0) * ccdcntrl_get_dec_height() / ccdcntrl_get_max_height() / 3600

#ifndef ACT_FILES_PATH
#pragma message("ACT_FILES_PATH not defined, setting default (""/usr/local/share/act_files"" - this is where the brightness and contrast icons should be saved)")
#define ACT_FILES_PATH "/usr/local/share/act_files"
#endif

struct img_lut_colours
{
  float red, green, blue;
//   float blue, green, red, alpha;
//   float alpha;
};

struct img_lut
{
  struct img_lut_colours colours[CCDPIX_MAX+1];
};

struct coord_disp_labels
{
  GtkWidget *lbl_disp_X, *lbl_disp_Y, *lbl_disp_RA, *lbl_disp_Dec;
};

gboolean dra_configure(GtkWidget *dra_ccdimg);
gboolean dra_expose (GtkWidget *dra_ccdimg);
void load_lutlist(GtkWidget *cmb_imglut, MYSQL *conn);
void clear_lutlist(GtkWidget *cmb_imglut);
void load_lut(GtkWidget *cmb_imglut, gpointer user_data);
void set_brightness(GtkWidget *scl_imgbrt, gpointer user_data);
void set_contrast(GtkWidget *scl_imgcnt, gpointer user_data);
void calc_new_lut(GtkWidget *dra_ccdimg);
void change_grid(GtkWidget *cmb_dispgrid, gpointer user_data);
gboolean mouse_move(GtkWidget* owner, GdkEventMotion* motdata, gpointer user_data);
void ccdimg_destroy(gpointer user_data);
void flip_view_ns(GtkButton* btn_flipNS);
void flip_view_ew(GtkToggleButton* btn_flipEW);


enum
{
  IMGLUT_NAME = 0,
  IMGLUT_IMGLUTPTR,
  IMGLUT_NUM_COLS
};

enum
{
  DISPGRID_NAME = 0,
  DISPGRID_NUM,
  DISPGRID_NUM_COLS
};

enum
{
  GRID_NONE = 0,
  GRID_VIEWPORT,
  GRID_PIXEL,
  GRID_EQUAT
};

static GLuint G_glsl_prog, G_ccdimgname, G_coltblname;
static GtkWidget *G_dra_ccdimg = NULL;
static float G_contrast = 0.0;
static short G_brightness = 0;
static struct img_lut G_coltbl;
static unsigned char G_grid_type = GRID_NONE;
static unsigned char G_flip_img = 0;
static struct rastruct G_img_ra;
static struct decstruct G_img_dec;

int create_imgdisp_objs(MYSQL *conn, GtkWidget *evb_imgdisp)
{
  if ((conn == NULL) || (evb_imgdisp == NULL))
  {
    act_log_normal(act_log_msg("Invalid input parameters"));
    return -1;
  }
  
  // This should be unnecessary, but it's done here in case not even the default LUT can be loaded.
  // Otherwise the user would just see a blank screen.
  G_coltbl.colours[0].red = 0.0;
  G_coltbl.colours[0].green = 1.0;
  G_coltbl.colours[0].blue = 0.0;
  G_coltbl.colours[255].red = 1.0;
  G_coltbl.colours[255].green = 0.0;
  G_coltbl.colours[255].blue = 0.0;
  int i;
  for (i=1; i<CCDPIX_MAX; i++)
  {
    G_coltbl.colours[i].red = (float)i/CCDPIX_MAX;
    G_coltbl.colours[i].green = (float)i/CCDPIX_MAX;
    G_coltbl.colours[i].blue = (float)i/CCDPIX_MAX;
  }
  //
  
  memset(&G_img_ra, 0, sizeof(struct rastruct));
  memset(&G_img_dec, 0, sizeof(struct decstruct));
  
  GtkWidget *box_imgdisp = gtk_table_new(3,2,FALSE);
  gtk_container_add(GTK_CONTAINER(evb_imgdisp),box_imgdisp);
  
  GtkWidget *evb_ccdimg = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(box_imgdisp),evb_ccdimg,0,1,0,1, GTK_EXPAND, GTK_EXPAND, 3, 3);
  GtkWidget *dra_ccdimg = gtk_drawing_area_new();
  G_dra_ccdimg = dra_ccdimg;
  gtk_container_add(GTK_CONTAINER(evb_ccdimg), dra_ccdimg);
  gtk_widget_set_size_request (dra_ccdimg, ccdcntrl_get_max_width(), ccdcntrl_get_max_height());
  gtk_widget_add_events (dra_ccdimg, GDK_EXPOSURE_MASK);
  GdkGLConfig *glconfig = gdk_gl_config_new_by_mode (GDK_GL_MODE_RGBA | GDK_GL_MODE_DOUBLE);
  if (!glconfig)
    act_log_error(act_log_msg("Could not find suitable OpenGL configuration."));
  else if (!gtk_widget_set_gl_capability (dra_ccdimg, glconfig, FALSE, TRUE, GDK_GL_RGBA_TYPE))
    act_log_error(act_log_msg("Could not find suitable OpenGL capability."));
  else
  {
    g_signal_connect (dra_ccdimg, "configure-event", G_CALLBACK (dra_configure), NULL);
    g_signal_connect (dra_ccdimg, "expose-event", G_CALLBACK (dra_expose), NULL);
  }
  
  GtkWidget *box_dispparams = gtk_table_new(5,2,FALSE);
  gtk_table_attach(GTK_TABLE(box_imgdisp),box_dispparams,1,2,0,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_dispparams),gtk_label_new("LUT"),0,2,0,1, GTK_FILL, GTK_SHRINK, 3, 3);
  GtkWidget *cmb_imglut = gtk_combo_box_new();
  load_lutlist(cmb_imglut, conn);
  gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_imglut), 0);
  gtk_table_attach(GTK_TABLE(box_dispparams),cmb_imglut,0,2,1,2, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 3, 3);
  g_signal_connect (G_OBJECT(cmb_imglut), "changed", G_CALLBACK(load_lut), dra_ccdimg);
  g_signal_connect (G_OBJECT(cmb_imglut), "destroy", G_CALLBACK(clear_lutlist), NULL);
  GdkPixbuf *pxb_imgbrt = gdk_pixbuf_new_from_file(ACT_FILES_PATH "/brightness_icon.png", NULL);
  if (pxb_imgbrt == NULL)
  {
    act_log_error(act_log_msg("Could not load brightness icon file %s.", ACT_FILES_PATH "/brightness_icon.png"));
    gtk_table_attach(GTK_TABLE(box_dispparams),gtk_label_new("Brt"),0,1,2,3, GTK_FILL, GTK_SHRINK, 3, 3);
  }
  else
    gtk_table_attach(GTK_TABLE(box_dispparams),gtk_image_new_from_pixbuf(pxb_imgbrt),0,1,2,3, GTK_FILL, GTK_SHRINK, 3, 3);
  GtkWidget* scl_imgbrt = gtk_vscale_new_with_range(-255,255,1);
  gtk_scale_add_mark (GTK_SCALE(scl_imgbrt), 0, GTK_POS_LEFT, NULL);
  gtk_range_set_value(GTK_RANGE(scl_imgbrt),0);
  g_signal_connect (G_OBJECT(scl_imgbrt), "value-changed", G_CALLBACK(set_brightness), dra_ccdimg);
  gtk_table_attach(GTK_TABLE(box_dispparams),scl_imgbrt,0,1,3,4, GTK_FILL, GTK_FILL|GTK_EXPAND, 3, 3);
  GdkPixbuf *pxb_imgcnt = gdk_pixbuf_new_from_file(ACT_FILES_PATH "/contrast_icon.png", NULL);
  if (pxb_imgcnt == NULL)
  {
    act_log_error(act_log_msg("Could not load contrast icon file %s.", ACT_FILES_PATH "/brightness_icon.png"));
    gtk_table_attach(GTK_TABLE(box_dispparams),gtk_label_new("Cnt"),1,2,2,3, GTK_FILL, GTK_SHRINK, 3, 3);
  }
  else
    gtk_table_attach(GTK_TABLE(box_dispparams),gtk_image_new_from_pixbuf(pxb_imgcnt),1,2,2,3, GTK_FILL, GTK_SHRINK, 3, 3);
  GtkWidget* scl_imgcnt = gtk_vscale_new_with_range(-1,1,0.01);
  gtk_scale_add_mark (GTK_SCALE(scl_imgcnt), 0, GTK_POS_LEFT, NULL);
  gtk_range_set_value(GTK_RANGE(scl_imgcnt),0);
  g_signal_connect (G_OBJECT(scl_imgcnt), "value-changed", G_CALLBACK(set_contrast), dra_ccdimg);
  gtk_table_attach(GTK_TABLE(box_dispparams),scl_imgcnt,1,2,3,4, GTK_FILL, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkListStore *grid_store = gtk_list_store_new(DISPGRID_NUM_COLS, G_TYPE_STRING, G_TYPE_INT);
  GtkTreeIter iter;
  gtk_list_store_append(GTK_LIST_STORE(grid_store), &iter);
  gtk_list_store_set(GTK_LIST_STORE(grid_store), &iter, DISPGRID_NAME, "No grid", DISPGRID_NUM, GRID_NONE, -1);
  gtk_list_store_append(GTK_LIST_STORE(grid_store), &iter);
  gtk_list_store_set(GTK_LIST_STORE(grid_store), &iter, DISPGRID_NAME, "Viewport", DISPGRID_NUM, GRID_VIEWPORT, -1);
  gtk_list_store_append(GTK_LIST_STORE(grid_store), &iter);
  gtk_list_store_set(GTK_LIST_STORE(grid_store), &iter, DISPGRID_NAME, "Pixel", DISPGRID_NUM, GRID_PIXEL, -1);
  gtk_list_store_append(GTK_LIST_STORE(grid_store), &iter);
  gtk_list_store_set(GTK_LIST_STORE(grid_store), &iter, DISPGRID_NAME, "Equatorial", DISPGRID_NUM, GRID_EQUAT, -1);
  GtkWidget *cmb_dispgrid = gtk_combo_box_new_with_model(GTK_TREE_MODEL(grid_store));
  GtkCellRenderer *cel_dispgrid = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cmb_dispgrid), cel_dispgrid, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(cmb_dispgrid), cel_dispgrid, "text", DISPGRID_NAME, NULL);
  gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_dispgrid),0);
  g_signal_connect(G_OBJECT(cmb_dispgrid),"changed",G_CALLBACK(change_grid),dra_ccdimg);
  gtk_table_attach(GTK_TABLE(box_dispparams),cmb_dispgrid,0,2,4,5,GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  
  GtkWidget *box_imginfo = gtk_hbox_new(FALSE,3);
  gtk_table_attach(GTK_TABLE(box_imgdisp),box_imginfo,0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  GtkWidget *box_coorddisp = gtk_table_new(2,4,FALSE);
  gtk_box_pack_start(GTK_BOX(box_imginfo), box_coorddisp, TRUE, TRUE, 3);
  gtk_table_attach(GTK_TABLE(box_coorddisp),gtk_label_new("X:"),0,1,0,1, GTK_FILL, GTK_FILL, 3, 3);
  gtk_table_attach(GTK_TABLE(box_coorddisp),gtk_label_new("Y:"),0,1,1,2, GTK_FILL, GTK_FILL, 3, 3);
  gtk_table_attach(GTK_TABLE(box_coorddisp),gtk_label_new("RA: "),2,3,0,1, GTK_FILL, GTK_FILL, 3, 3);
  gtk_table_attach(GTK_TABLE(box_coorddisp),gtk_label_new("Dec:"),2,3,1,2, GTK_FILL, GTK_FILL, 3, 3);
  struct coord_disp_labels *disp_labels = malloc(sizeof(struct coord_disp_labels));
  if (disp_labels != NULL)
  {
    disp_labels->lbl_disp_X = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(disp_labels->lbl_disp_X),3);
    gtk_misc_set_alignment(GTK_MISC(disp_labels->lbl_disp_X), 0.0f, 0.0f);
    gtk_table_attach(GTK_TABLE(box_coorddisp),disp_labels->lbl_disp_X,1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
    disp_labels->lbl_disp_Y = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(disp_labels->lbl_disp_Y),3);
    gtk_misc_set_alignment(GTK_MISC(disp_labels->lbl_disp_Y), 0.0f, 0.0f);
    gtk_table_attach(GTK_TABLE(box_coorddisp),disp_labels->lbl_disp_Y,1,2,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
    disp_labels->lbl_disp_RA = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(disp_labels->lbl_disp_RA),11);
    gtk_misc_set_alignment(GTK_MISC(disp_labels->lbl_disp_RA), 0.0f, 0.0f);
    gtk_table_attach(GTK_TABLE(box_coorddisp),disp_labels->lbl_disp_RA,3,4,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
    disp_labels->lbl_disp_Dec = gtk_label_new("");
    gtk_label_set_width_chars(GTK_LABEL(disp_labels->lbl_disp_Dec),11);
    gtk_misc_set_alignment(GTK_MISC(disp_labels->lbl_disp_Dec), 0.0f, 0.0f);
    gtk_table_attach(GTK_TABLE(box_coorddisp),disp_labels->lbl_disp_Dec,3,4,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
    gtk_widget_add_events (evb_ccdimg, GDK_POINTER_MOTION_MASK);
    g_signal_connect (evb_ccdimg, "motion-notify-event", G_CALLBACK (mouse_move), disp_labels);
    g_signal_connect (evb_ccdimg, "destroy", G_CALLBACK(ccdimg_destroy), disp_labels);
  }
  else
    act_log_error(act_log_msg("Could not allocate space for coordinate display labels."));
  GtkWidget* btn_flipNS = gtk_button_new_with_label("N");
  gtk_button_set_image(GTK_BUTTON(btn_flipNS), gtk_image_new_from_stock(GTK_STOCK_GO_UP,GTK_ICON_SIZE_BUTTON));
  gtk_box_pack_start(GTK_BOX(box_imginfo), btn_flipNS, TRUE, TRUE, 3);
  g_signal_connect(G_OBJECT(btn_flipNS),"clicked",G_CALLBACK(flip_view_ns),NULL);
  GtkWidget* btn_flipEW = gtk_button_new_with_label("E");
  gtk_button_set_image(GTK_BUTTON(btn_flipEW), gtk_image_new_from_stock(GTK_STOCK_GO_FORWARD,GTK_ICON_SIZE_BUTTON));
  gtk_box_pack_start(GTK_BOX(box_imginfo), btn_flipEW, TRUE, TRUE, 3);
  g_signal_connect(G_OBJECT(btn_flipEW),"clicked",G_CALLBACK(flip_view_ew),NULL);
  return 1;
}

void update_imgdisp(struct merlin_img *newimg, struct rastruct *ra, struct decstruct *dec)
{
  if (newimg == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  
  if ((ra != NULL) && (dec != NULL))
  {
    memcpy(&G_img_ra, ra, sizeof(struct rastruct));
    memcpy(&G_img_dec, dec, sizeof(struct decstruct));
  }
  else
  {
    memset(&G_img_ra, 0, sizeof(struct rastruct));
    memset(&G_img_dec, 0, sizeof(struct decstruct));
  }
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (G_dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (G_dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to upload new CCD image."));
    return;
  }

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, G_ccdimgname);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, newimg->img_params.img_width, newimg->img_params.img_height, 0, GL_RED, GL_UNSIGNED_BYTE, newimg->img_data);

  gdk_window_invalidate_rect (G_dra_ccdimg->window, &G_dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (G_dra_ccdimg->window, FALSE);
  
  gdk_gl_drawable_gl_end (gldrawable);
}

gboolean dra_configure(GtkWidget *dra_ccdimg)
{
  GdkGLContext *glcontext = gtk_widget_get_gl_context (dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (dra_ccdimg);
  
  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    printf("Failed to initiate drawing on the OpenGL widget\n");
    return TRUE;
  }
  
  glClearColor(0.0,0.0,0.0,0.0);
  glShadeModel(GL_FLAT);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glViewport (0, 0, dra_ccdimg->allocation.width, dra_ccdimg->allocation.height);
  glOrtho(-1.0,1.0,-1.0,1.0,-1.0,1.0);
  glEnable(GL_TEXTURE_2D);
  
  G_glsl_prog = glCreateProgram();
  GLuint shader;
  GLint length, result;
  
  const char *vert_src = "varying vec2 texture_coordinate; void main() { gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;  texture_coordinate = vec2(gl_MultiTexCoord0); gl_FrontColor = gl_Color; }";
  shader = glCreateShader(GL_VERTEX_SHADER);
  length = strlen(vert_src);
  glShaderSource(shader, 1, &vert_src, &length);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
  if(result == GL_FALSE) 
  {
    char *log;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    log = malloc(length);
    glGetShaderInfoLog(shader, length, &result, log);
    fprintf(stderr, "Unable to compile vertex shader: %s\n", log);
    free(log);
    glDeleteShader(shader);
  }
  if(shader != 0) 
  {
    glAttachShader(G_glsl_prog, shader);
    glDeleteShader(shader);
  }
  
  const char *frag_src = "varying vec2 texture_coordinate; uniform sampler2D ccdimg_tex; uniform sampler2D coltbl_tex; void main() {  float colidx = texture2D(ccdimg_tex, texture_coordinate).r; gl_FragColor = texture2D(coltbl_tex, vec2(colidx,0)) + gl_Color; }";
  shader = glCreateShader(GL_FRAGMENT_SHADER);
  length = strlen(frag_src);
  glShaderSource(shader, 1, &frag_src, &length);
  glCompileShader(shader);
  glGetShaderiv(shader, GL_COMPILE_STATUS, &result);
  if(result == GL_FALSE) 
  {
    char *log;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    log = malloc(length);
    glGetShaderInfoLog(shader, length, &result, log);
    fprintf(stderr, "Unable to compile vertex shader: %s\n", log);
    free(log);
    glDeleteShader(shader);
  }
  if(shader != 0)
  {
    glAttachShader(G_glsl_prog, shader);
    glDeleteShader(shader);
  }
  
  glLinkProgram(G_glsl_prog);
  glGetProgramiv(G_glsl_prog, GL_LINK_STATUS, &result);
  if(result == GL_FALSE) 
  {
    GLint length;
    char *log;
    glGetProgramiv(G_glsl_prog, GL_INFO_LOG_LENGTH, &length);
    log = malloc(length);
    glGetProgramInfoLog(G_glsl_prog, length, &result, log);
    fprintf(stderr, "sceneInit(): Program linking failed: %s\n", log);
    free(log);
    glDeleteProgram(G_glsl_prog);
    G_glsl_prog = 0;
  }
  
  GLint ccdimg_loc = glGetUniformLocation(G_glsl_prog, "ccdimg_tex");
  GLint coltbl_loc = glGetUniformLocation(G_glsl_prog, "coltbl_tex");
  
  glUseProgram(G_glsl_prog);
  glUniform1i(ccdimg_loc, 0); //Texture unit 0 is for CCD images.
  glUniform1i(coltbl_loc, 2); //Texture unit 2 is for colour table.
  
  //When rendering an objectwith this program.
  glGenTextures(1,&G_ccdimgname);
  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, G_ccdimgname);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  unsigned char img[ccdcntrl_get_max_width()*ccdcntrl_get_max_height()];
  int i;
  for (i=0; i<ccdcntrl_get_max_width()*ccdcntrl_get_max_height(); i++)
    img[i] = i%256;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, ccdcntrl_get_max_width(), ccdcntrl_get_max_height(), 0, GL_RED, GL_UNSIGNED_BYTE, &img[0]);
  glMatrixMode(GL_MODELVIEW);
  
  glActiveTexture(GL_TEXTURE0 + 2);
  glGenTextures(1,&G_coltblname);
  glBindTexture(GL_TEXTURE_2D, G_coltblname);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 1, 0, GL_RGB, GL_FLOAT, &G_coltbl.colours[0].red);
  gdk_gl_drawable_gl_end (gldrawable);
  
  return TRUE;
}

gboolean dra_expose (GtkWidget *dra_ccdimg)
{
  GdkGLContext *glcontext = gtk_widget_get_gl_context (dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to draw GL scene."));
    return TRUE;
  }

  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glUseProgram(G_glsl_prog);

  // Draw the image from the CCD
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if ((G_flip_img&IMG_FLIP_EW) == IMG_FLIP_EW)
    glScalef(-1.0, 1.0, 1.0);
  if ((G_flip_img&IMG_FLIP_NS) == IMG_FLIP_NS)
    glScalef(1.0, -1.0, 1.0);
  glScaled(1.0, -1.0, 1.0);
  glEnable (GL_TEXTURE_2D);
  glActiveTexture(GL_TEXTURE0 + 0);
  glBindTexture(GL_TEXTURE_2D, G_ccdimgname);
  glColor3ub(0,0,0);
  glBegin (GL_QUADS);
  glTexCoord2f(0.0,0.0); glVertex3d(-1.0,-1.0,0.0);
  glTexCoord2f(1.0,0.0); glVertex3d( 1.0,-1.0,0.0);
  glTexCoord2f(1.0,1.0); glVertex3d( 1.0, 1.0,0.0);
  glTexCoord2f(0.0,1.0); glVertex3d(-1.0, 1.0,0.0);
  glEnd();
  glDisable(GL_TEXTURE_2D);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  if ((G_flip_img&IMG_FLIP_EW) == IMG_FLIP_EW)
    glScalef(-1.0, 1.0, 1.0);
  if ((G_flip_img&IMG_FLIP_NS) == IMG_FLIP_NS)
    glScalef(1.0, -1.0, 1.0);
  glScalef(2.0/(float)ccdcntrl_get_max_width(), -2.0/(float)ccdcntrl_get_max_height(), 1.0);
  glTranslated(-(float)ccdcntrl_get_max_width()/2.0, -(float)ccdcntrl_get_max_height()/2.0, 1.0);
  double ang, radius_px = 7 /* pixels */;
  glTranslated((double)XAPERTURE, (double)YAPERTURE, 0.0);
  glColor3ub(255,0,0);
  glBegin(GL_LINE_LOOP);
  for (ang = 0.0; ang<=2.0*ONEPI; ang+=2.0*ONEPI/fmax((double)(radius_px),10.0))
    glVertex3d(radius_px/2.0*cos(ang), radius_px/2.0*sin(ang), 0.0);
  glEnd();
  
  switch(G_grid_type)
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
  }
  
  if (gdk_gl_drawable_is_double_buffered (gldrawable))
    gdk_gl_drawable_swap_buffers (gldrawable);
  else
    glFlush ();

  gdk_gl_drawable_gl_end (gldrawable);

  return TRUE;
}

void load_lutlist(GtkWidget *cmb_imglut, MYSQL *conn)
{
  (void) conn;
  GtkListStore *lut_store = gtk_list_store_new(IMGLUT_NUM_COLS, G_TYPE_STRING, G_TYPE_POINTER);
  GtkTreeIter iter;
  
  struct img_lut *lut = malloc(sizeof(struct img_lut));
  lut->colours[0].red = 0.0;
  lut->colours[0].green = 1.0;
  lut->colours[0].blue = 0.0;
//   lut->colours[0].alpha = 1.0;
  lut->colours[CCDPIX_MAX].red = 1.0;
  lut->colours[CCDPIX_MAX].green = 0.0;
  lut->colours[CCDPIX_MAX].blue = 0.0;
//   lut->colours[CCDPIX_MAX].alpha = 1.0;
  int i;
  for (i=1; i<CCDPIX_MAX; i++)
  {
    lut->colours[i].red = (float)i/CCDPIX_MAX;
    lut->colours[i].green = (float)i/CCDPIX_MAX;
    lut->colours[i].blue = (float)i/CCDPIX_MAX;
//     lut->colours[i].alpha = 1.0;
  }
  gtk_list_store_append(GTK_LIST_STORE(lut_store), &iter);
  gtk_list_store_set(GTK_LIST_STORE(lut_store), &iter, IMGLUT_NAME, "Default", IMGLUT_IMGLUTPTR, lut, -1);
  
  gtk_combo_box_set_model(GTK_COMBO_BOX(cmb_imglut), GTK_TREE_MODEL(lut_store));
  gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_imglut),0);
  GtkCellRenderer *cel_imglut = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(cmb_imglut), cel_imglut, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(cmb_imglut), cel_imglut, "text", IMGLUT_NAME, NULL);
}

void clear_lutlist(GtkWidget *cmb_imglut)
{
  GtkListStore *lut_store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_imglut)));
  GtkTreeIter iter;
  struct img_lut *lut;
  char valid = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(lut_store), &iter);
  while (valid)
  {
    lut = NULL;
    gtk_tree_model_get_value(GTK_TREE_MODEL(lut_store), &iter, IMGLUT_IMGLUTPTR, (void *)&lut);
    if (lut != NULL)
    {
      free(lut);
      gtk_list_store_set_value(GTK_LIST_STORE(lut_store), &iter, IMGLUT_IMGLUTPTR, NULL);
    }
    valid = gtk_tree_model_iter_next(GTK_TREE_MODEL(lut_store), &iter);
  }
  gtk_combo_box_set_model(GTK_COMBO_BOX(cmb_imglut), NULL);
}

void load_lut(GtkWidget *cmb_imglut, gpointer user_data)
{
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(cmb_imglut), &iter))
  {
    act_log_error(act_log_msg("LUT combobox has no active iterator. Cannot set LUT."));
    return;
  }
  GtkListStore *lut_store = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_imglut)));
  struct img_lut *lut = NULL;
  gtk_tree_model_get_value(GTK_TREE_MODEL(lut_store), &iter, IMGLUT_IMGLUTPTR, (void *)&lut);
  if (lut == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve LUT from combobox store."));
    return;
  }
  memcpy(&G_coltbl, lut, sizeof(struct img_lut));
  calc_new_lut(GTK_WIDGET(user_data));
}

void set_brightness(GtkWidget *scl_imgbrt, gpointer user_data)
{
  G_brightness = (short) gtk_range_get_value(GTK_RANGE(scl_imgbrt));
  calc_new_lut(GTK_WIDGET(user_data));
}

void set_contrast(GtkWidget *scl_imgcnt, gpointer user_data)
{
  G_contrast = (float) gtk_range_get_value(GTK_RANGE(scl_imgcnt));
  calc_new_lut(GTK_WIDGET(user_data));
}

void calc_new_lut(GtkWidget *dra_ccdimg)
{
//   float tmp_adjtbl[CCDPIX_MAX+1][3];
/*  int i;
  for (i=0; i<CCDPIX_MAX+1; i++)
  {
    tmp_adjtbl[i][0] = (G_coltbl.colours[i].red*pow((float)CCDPIX_MAX,G_contrast)) + ((float)G_brightness/CCDPIX_MAX);
    tmp_adjtbl[i][1] = (G_coltbl.colours[i].green*pow((float)CCDPIX_MAX,G_contrast)) + ((float)G_brightness/CCDPIX_MAX);
    tmp_adjtbl[i][2] = (G_coltbl.colours[i].blue*pow((float)CCDPIX_MAX,G_contrast)) + ((float)G_brightness/CCDPIX_MAX);
  }*/
  
  GdkGLContext *glcontext = gtk_widget_get_gl_context (dra_ccdimg);
  GdkGLDrawable *gldrawable = gtk_widget_get_gl_drawable (dra_ccdimg);

  if (!gdk_gl_drawable_gl_begin (gldrawable, glcontext))
  {
    act_log_error(act_log_msg("Could not access GTK drawable GL context to draw GL scene."));
    return;
  }

  glActiveTexture(GL_TEXTURE0 + 2);
  glGenTextures(1,&G_coltblname);
  glBindTexture(GL_TEXTURE_2D, G_coltblname);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 1, 0, GL_RGB, GL_FLOAT, &G_coltbl.colours[0].red);
  
  gdk_gl_drawable_gl_end (gldrawable);
  
  gdk_window_invalidate_rect (dra_ccdimg->window, &dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (dra_ccdimg->window, FALSE);
}

void change_grid(GtkWidget *cmb_dispgrid, gpointer user_data)
{
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(cmb_dispgrid),&iter))
  {
    act_log_error(act_log_msg("Could not find the selected grid type."));
    return;
  }
  GtkTreeModel *grid_store = gtk_combo_box_get_model(GTK_COMBO_BOX(cmb_dispgrid));
  gint tmp_grid_type = -1;
  gtk_tree_model_get(grid_store, &iter, DISPGRID_NUM, &tmp_grid_type, -1);
  if ((tmp_grid_type < GRID_NONE) || (tmp_grid_type > GRID_EQUAT))
  {
    act_log_error(act_log_msg("Invalid grid type selected. This should not be possible, but is not a serious problem."));
    return;
  }
  G_grid_type = tmp_grid_type;
  
  GtkWidget *dra_ccdimg = GTK_WIDGET(user_data);
  gdk_window_invalidate_rect (dra_ccdimg->window, &dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (dra_ccdimg->window, FALSE);
}

/** \brief Callback when the mouse is moved over the OpenGL widget.
 * \param evb_ccdimg GtkWidget over which mouse was moved (should always be the OpenGL widget).
 * \param motdata GDK object containing data on mouse and keyboard state.
 * \param user_data GtkLabel to which X,Y, RA, Dec information is to be written.
 * \return Always return TRUE.
 *
 * Algorithm:
 *  -# Determine mouse X,Y coordinates on image, taking image flips into account.
 *  -# If left mouse button is pressed (i.e. click and drag):
 *    - If mouse is over a starbox (find_starbox returns != NULL), move starbox to current mouse x,y
 *  -# Determine equitorial coordinates of mouse pointer (conv_PIXCOORD_EQUIT) and print to user_data.
 */
gboolean mouse_move(GtkWidget* evb_ccdimg, GdkEventMotion* motdata, gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return TRUE;
  }
  (void) evb_ccdimg;
  
  unsigned short x, y;
  if ((G_flip_img & IMG_FLIP_EW) == IMG_FLIP_EW)
    x = ccdcntrl_get_max_width() - motdata->x;
  else
    x = motdata->x;
  if ((G_flip_img & IMG_FLIP_NS) == IMG_FLIP_NS)
    y = ccdcntrl_get_max_height() - motdata->y;
  else
    y = motdata->y;
  
  struct coord_disp_labels *labels = (struct coord_disp_labels *)user_data;
  
  char *tmpstr = malloc(20);
  snprintf(tmpstr, 20, "%hu", x);
  gtk_label_set_text(GTK_LABEL(labels->lbl_disp_X), tmpstr);
  snprintf(tmpstr, 20, "%hu", y);
  gtk_label_set_text(GTK_LABEL(labels->lbl_disp_Y), tmpstr);
  free(tmpstr);
  
  double img_dec = convert_DMS_D_dec(&G_img_dec);
  if (fabs(img_dec) > 89.0)
    img_dec = 89.0;
  double ra_offset_deg = (XAPERTURE - motdata->x) * ccdcntrl_get_ra_width() / ccdcntrl_get_max_width() / cos(convert_DEG_RAD(img_dec)) / 3600.0;
  if ((G_flip_img & IMG_FLIP_EW) != IMG_FLIP_EW)
    ra_offset_deg *= -1.0;
  struct rastruct mouse_ra;
  convert_H_HMSMS_ra(convert_HMSMS_H_ra(&G_img_ra) + convert_DEG_H(ra_offset_deg), &mouse_ra);
  tmpstr = ra_to_str(&mouse_ra);
  gtk_label_set_text(GTK_LABEL(labels->lbl_disp_RA), tmpstr);
  free(tmpstr);
  
  double dec_offset_deg = (YAPERTURE - motdata->y) * ccdcntrl_get_dec_height() / ccdcntrl_get_max_height() / 3600.0;
  if ((G_flip_img & IMG_FLIP_NS) == IMG_FLIP_NS)
    dec_offset_deg *= -1.0;
  struct decstruct mouse_dec;
  convert_D_DMS_dec(convert_DMS_D_dec(&G_img_dec) + dec_offset_deg, &mouse_dec);
  tmpstr = dec_to_str(&mouse_dec);
  gtk_label_set_text(GTK_LABEL(labels->lbl_disp_Dec), tmpstr);
  free(tmpstr);

  return TRUE;
}

void ccdimg_destroy(gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct coord_disp_labels *disp_labels = (struct coord_disp_labels *)user_data;
  free(disp_labels);
}

/** \brief Callback for when user presses North-South flip toggle button. Set internal value appropriately and update
 *  \brief OpenGL widget.
 * \param btn_flipNS GTK button the user pressed.
 * \return (void)
 */
void flip_view_ns(GtkButton* btn_flipNS)
{
  GtkWidget* img_btnimage = gtk_button_get_image(btn_flipNS);
  if ((G_flip_img & IMG_FLIP_NS) == IMG_FLIP_NS)
  {
    gtk_image_set_from_stock(GTK_IMAGE(img_btnimage), GTK_STOCK_GO_UP,GTK_ICON_SIZE_BUTTON);
    G_flip_img &= ~IMG_FLIP_NS;
  }
  else
  {
    gtk_image_set_from_stock(GTK_IMAGE(img_btnimage), GTK_STOCK_GO_DOWN,GTK_ICON_SIZE_BUTTON);
    G_flip_img |= IMG_FLIP_NS;
  }
  gdk_window_invalidate_rect (G_dra_ccdimg->window, &G_dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (G_dra_ccdimg->window, FALSE);
}

/** \brief Callback for when user presses East-West flip toggle button. Set internal value appropriately and update
 *  \brief OpenGL widget.
 * \param btn_flipEW GTK button the user pressed.
 * \return (void)
 */
void flip_view_ew(GtkToggleButton* btn_flipEW)
{
  GtkWidget* img_btnimage = gtk_button_get_image(GTK_BUTTON(btn_flipEW));
  if ((G_flip_img & IMG_FLIP_EW) == IMG_FLIP_EW)
  {
    gtk_image_set_from_stock(GTK_IMAGE(img_btnimage), GTK_STOCK_GO_FORWARD, GTK_ICON_SIZE_BUTTON);
    G_flip_img &= ~IMG_FLIP_EW;
  }
  else
  {
    gtk_image_set_from_stock(GTK_IMAGE(img_btnimage), GTK_STOCK_GO_BACK, GTK_ICON_SIZE_BUTTON);
    G_flip_img |= IMG_FLIP_EW;
  }
  gdk_window_invalidate_rect (G_dra_ccdimg->window, &G_dra_ccdimg->allocation, FALSE);
  gdk_window_process_updates (G_dra_ccdimg->window, FALSE);
}

