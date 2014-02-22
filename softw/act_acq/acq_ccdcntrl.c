#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <math.h>
#include <merlin_driver.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include <fitsio.h>
#include <mysql/mysql.h>
#include <string.h>
#include <act_log.h>
#include <act_ipc.h>
#include "acq_imgdisp.h"
#include "acq_ccdcntrl.h"
#include "pattern_match.h"

/// Mininum number of stars to match during pattern matching for the match to be considered accurate
#define MIN_PAT_STARS_FRAC  0.5
/// ?
#define STARINT_MINIMUM 10.0

/// Maximum allowable deviation in RA for positive identification
#define RA_TOL_H    9.259259259259259e-05
#define DEC_TOL_D   0.001388888888888889

enum
{
  WINDOWSTORE_MODENUM = 0,
  WINDOWSTORE_NAME,
  WINDOWSTORE_NUM_COLS
};

enum
{
  PREBINSTORE_X = 0,
  PREBINSTORE_Y,
  PREBINSTORE_NAME,
  PREBINSTORE_NUM_COLS
};


struct exposure_information
{
  struct datestruct unidate;
  struct timestruct unitime;
  struct rastruct ra;
  struct hastruct ha;
  struct decstruct dec;
  char targ_name[MAX_TARGID_LEN];
  int targ_id;
  int user_id;
  char sky;
};

gboolean ccd_stat_update (GIOChannel *ccd_chan, GIOCondition condition, gpointer user_data);
void update_statdisp(struct ccdcntrl_objects *ccdcntrl_objs);
char start_exposure(struct ccdcntrl_objects *ccdcntrl_objs);
void expose_toggled(GtkWidget *btn_expose, gpointer user_data);
void expose_clicked();
char get_data_dir(struct ccdcntrl_objects *ccdcntrl_objs);
float get_pat_exp_t_sec(MYSQL *conn, int targ_id);
int load_pattern(MYSQL *conn, int targ_id, char sky, int ccd_offset_x, int ccd_offset_y, struct point **pattern_points);
char save_acq_image(MYSQL *conn, int num_acq_points, int num_points_matched);
void save_fits_fallback();
char *fallback_data_dir();
char create_data_dir(const char *data_dir);

// ??? Should actually search through combo boxes to find the right window number and prebin mode number

static struct ccd_modes G_modes;
static struct merlin_img G_targ_img;
static struct exposure_information G_cur_info;
static struct exposure_information G_targ_info;

struct ccdcntrl_objects *ccdcntrl_create_objs(MYSQL **conn, GtkWidget *container)
{
  if ((conn == NULL) || (container == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  struct ccdcntrl_objects *objs = malloc(sizeof(struct ccdcntrl_objects));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Failed to allocate space for CCD control objects."));
    return NULL;
  }
  memset(objs, 0, sizeof(struct ccdcntrl_objects));
  
  int fd = open("/dev/" MERLIN_DEVICE_NAME, O_RDWR);
  if (fd < 0)
  {
    act_log_error(act_log_msg("Can't open device file: /dev/%s", MERLIN_DEVICE_NAME));
    return NULL;
  }
  
  GError *error = NULL;
  objs->ccd_chan = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref (objs->ccd_chan, TRUE);
  g_io_channel_set_encoding (objs->ccd_chan, NULL, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to set encoding type for CCD driver channel (%d - %s).", error->code, error->message));
    g_error_free(error);
    return NULL;
  }
  g_io_channel_set_buffered (objs->ccd_chan, FALSE);
  objs->ccd_watch_id = g_io_add_watch(objs->ccd_chan, G_IO_IN, ccd_stat_update, objs);
  
/*  unsigned int num_bytes;
  int ret;
  char tmp_ccd_stat;
  ret = g_io_channel_read_chars (objs->ccd_chan, &tmp_ccd_stat, 1, &num_bytes, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("An error occurred while attempting to read CCD driver status (%d - %s)", error->code, error->message));
    g_error_free(error);
    return NULL;
  }
  if (ret != G_IO_STATUS_NORMAL)
  {
    act_log_error(act_log_msg("An unknown error occurred while attempting to read the CCD driver status."));
    return NULL;
  }*/
  
  objs->mysql_conn = conn;
  memset(&G_cur_info, 0, sizeof(struct exposure_information));
  snprintf(G_cur_info.targ_name, sizeof(G_cur_info.targ_name), "ACT_ANY");
  G_cur_info.targ_id = 1;
  G_cur_info.user_id = 1;
  G_cur_info.sky = 0;

  struct ccd_modes modes;
  int ret = ioctl(fd, IOCTL_GET_MODES, &modes);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Error retrieving modes supported by MERLIN CCD driver - %s.", strerror(ret)));
    return NULL;
  }
  memcpy(&G_modes, &modes, sizeof(struct ccd_modes));
  
  get_data_dir(objs);
  
  GtkWidget *box_ccdcntrl = gtk_vbox_new(0, FALSE);
  gtk_container_add(GTK_CONTAINER(container),box_ccdcntrl);
  
  GtkWidget *lbl_ccdid = gtk_label_new(G_modes.ccd_id);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),lbl_ccdid,FALSE,TRUE,3);
  
  objs->evb_ccdstat = gtk_event_box_new();
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->evb_ccdstat,FALSE,TRUE,3);
  objs->lbl_ccdstat = gtk_label_new("N/A");
  gtk_label_set_width_chars(GTK_LABEL(objs->lbl_ccdstat),12);
  gtk_container_add(GTK_CONTAINER(objs->evb_ccdstat), objs->lbl_ccdstat);
  
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_hseparator_new(),FALSE,TRUE,3);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_label_new("Exp. time (s)"),FALSE,TRUE,3);
  if ((modes.min_exp_t_msec == 0) || (modes.max_exp_t_msec == 0))
  {
    objs->spn_expt = gtk_spin_button_new_with_range(0.01,1000.0,0.01);
    act_log_normal(act_log_msg("WARNING: Invalid exposure times returned by CCD driver. Setting assumed defaults."));
  }
  else
    objs->spn_expt = gtk_spin_button_new_with_range(modes.min_exp_t_msec/1000.0, modes.max_exp_t_msec/1000.0, modes.min_exp_t_msec/1000.0);
  gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(objs->spn_expt),TRUE);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->spn_expt,FALSE,TRUE,3);

  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_hseparator_new(),FALSE,TRUE,3);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_label_new("Repetitions"),FALSE,TRUE,3);
  objs->spn_repeat = gtk_spin_button_new_with_range(0,~((unsigned long)0),1);
  gtk_spin_button_set_snap_to_ticks(GTK_SPIN_BUTTON(objs->spn_repeat),TRUE);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->spn_repeat,FALSE,TRUE,3);
  
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_hseparator_new(),FALSE,TRUE,3);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_label_new("Window"),FALSE,TRUE,3);
  GtkListStore *window_store = gtk_list_store_new(WINDOWSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_STRING);
  unsigned int i;
  char tmpstr[256];
  GtkTreeIter iter;
  for (i=0; i<CCD_MAX_NUM_WINDOW_MODES; i++)
  {
    if ((modes.windows[i].width_px == 0) || (modes.windows[i].height_px == 0))
      continue;
    sprintf(tmpstr, "%hux%hu", modes.windows[i].width_px, modes.windows[i].height_px);
    gtk_list_store_append(GTK_LIST_STORE(window_store), &iter);
    gtk_list_store_set(GTK_LIST_STORE(window_store), &iter, WINDOWSTORE_MODENUM, i, WINDOWSTORE_NAME, tmpstr, -1);
  }
  objs->cmb_window = gtk_combo_box_new_with_model(GTK_TREE_MODEL(window_store));
  gtk_combo_box_set_active(GTK_COMBO_BOX(objs->cmb_window),0);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->cmb_window,FALSE,TRUE,3);
  GtkCellRenderer *cel_window = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_window), cel_window, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(objs->cmb_window), cel_window, "text", WINDOWSTORE_NAME, NULL);
  
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_hseparator_new(),FALSE,TRUE,3);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_label_new("Prebin"),FALSE,TRUE,3);
  GtkListStore *prebin_store = gtk_list_store_new(PREBINSTORE_NUM_COLS, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING);
  unsigned int j;
  for (i=0; i<sizeof(modes.prebin_x)*8; i++)
  {
    char prebin_str[20];
    if ((modes.prebin_x & (0x00000001 << i)) == 0)
      continue;
    for (j=0; j<sizeof(modes.prebin_y)*8; j++)
    {
      if ((modes.prebin_y & (0x00000001 << j)) == 0)
	continue;
      sprintf(prebin_str, "%dx%d", i, j);
      gtk_list_store_append(GTK_LIST_STORE(prebin_store), &iter);
      gtk_list_store_set(GTK_LIST_STORE(prebin_store), &iter, PREBINSTORE_X, 0x00000001 << i, PREBINSTORE_Y, 0x00000001 << j, PREBINSTORE_NAME, prebin_str, -1);
    }
  }
  objs->cmb_prebin = gtk_combo_box_new_with_model(GTK_TREE_MODEL(prebin_store));
  gtk_combo_box_set_active(GTK_COMBO_BOX(objs->cmb_prebin),0);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->cmb_prebin,FALSE,TRUE,3);
  GtkCellRenderer *cel_prebin = gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(objs->cmb_prebin), cel_prebin, TRUE);
  gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(objs->cmb_prebin), cel_prebin, "text", PREBINSTORE_NAME, NULL);

  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_hseparator_new(),FALSE,TRUE,3);
  objs->chk_phot_exp = gtk_check_button_new_with_label("Start at sec.");
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->chk_phot_exp,FALSE,TRUE,3);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),gtk_hseparator_new(),FALSE,TRUE,3);
  objs->chk_save = gtk_check_button_new_with_label("Save all");
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->chk_save,FALSE,TRUE,3);
  objs->btn_expose = gtk_toggle_button_new_with_label("Expose");
  g_signal_connect(G_OBJECT(objs->btn_expose), "toggled", G_CALLBACK(expose_toggled), objs);
//   g_signal_connect(G_OBJECT(objs->btn_expose), "toggled", G_CALLBACK(expose_clicked), NULL);
  gtk_box_pack_start(GTK_BOX(box_ccdcntrl),objs->btn_expose,FALSE,TRUE,3);
  return objs;
}

void ccdcntrl_finalise_objs(struct ccdcntrl_objects *ccdcntrl_objs)
{
  if (ccdcntrl_objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  g_object_unref(ccdcntrl_objs->ccd_chan);
  ccdcntrl_objs->ccd_chan = NULL;
  g_source_remove(ccdcntrl_objs->ccd_watch_id);
  ccdcntrl_objs->ccd_watch_id = 0;
  ccdcntrl_objs->mysql_conn = NULL;
  free(ccdcntrl_objs->data_dir);
  ccdcntrl_objs->data_dir = NULL;
}

void ccdcntrl_get_img_datetime(struct datestruct *unidate, struct timestruct *unitime)
{
  if ((unidate == NULL) || (unitime == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return;
  }
  memcpy(unidate, &G_targ_info.unidate, sizeof(struct datestruct));
  memcpy(unitime, &G_targ_info.unitime, sizeof(struct timestruct));
}

char ccdcntrl_save_image(struct ccdcntrl_objects *ccdcntrl_objs)
{
  if (ccdcntrl_objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return FALSE;
  }
  MYSQL *conn = *ccdcntrl_objs->mysql_conn;
  int ret = mysql_query(conn, "START TRANSACTION;");
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to start DB transaction for uploading image data - %s", mysql_error(conn)));
    return FALSE;
  }
  
  char qrystr[70+35*G_targ_img.img_params.img_height];
  
  sprintf(qrystr, "INSERT INTO merlin_img (targ_id, user_id, type, exp_t_s, start_date, start_time_h, width, height, bits_per_pix, tel_ha_h, tel_dec_d) VALUES (%d, %d, %d, %f, \"%hu-%hhu-%hhu\", %f, %u, %u, %hhu, %f, %f);", G_targ_info.targ_id, G_targ_info.user_id, G_targ_info.sky ? 2 : 1, G_targ_img.img_params.exp_t_msec/1000.0, G_targ_info.unidate.year, G_targ_info.unidate.month+1, G_targ_info.unidate.day+1, convert_HMSMS_H_time(&G_targ_info.unitime), G_targ_img.img_params.img_width, G_targ_img.img_params.img_height, sizeof(ccd_pixel_type)*8, convert_HMSMS_H_ha(&G_targ_info.ha), convert_DMS_D_dec(&G_targ_info.dec));
  if (mysql_query(conn, qrystr))
  {
    act_log_error(act_log_msg("Failed to save CCD image header to database - %s.", mysql_error(conn)));
    act_log_debug(act_log_msg("SQL query: %s", qrystr));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  act_log_debug(act_log_msg("Inserted image header."));
  
  unsigned long img_id = mysql_insert_id(conn);
  if (img_id == 0)
  {
    act_log_error(act_log_msg("Could not retrieve unique ID for saved CCD photometry image."));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  act_log_debug(act_log_msg("Image ID: %lu", img_id));
  
  MYSQL_STMT  *stmt;
  stmt = mysql_stmt_init(conn);
  if (!stmt)
  {
    act_log_error(act_log_msg("Failed to initialise MySQL prepared statement object - out of memory."));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  sprintf(qrystr, "INSERT INTO merlin_img_data (ccd_img_id, x, y, value) VALUES (%lu, ?, ?, ?);", img_id);
  if (mysql_stmt_prepare(stmt, qrystr, strlen(qrystr)))
  {
    act_log_error(act_log_msg("Failed to prepare statement for inserting image pixel data - %s", mysql_stmt_error(stmt)));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  if (mysql_stmt_param_count(stmt) != 3)
  {
    act_log_error(act_log_msg("Invalid number of parameters in prepared statement - %d",  mysql_stmt_param_count(stmt)));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  
  gushort cur_x, cur_y;
  guchar cur_val;
  MYSQL_BIND  bind[3];
  memset(bind, 0, sizeof(bind));
  bind[0].buffer_type = MYSQL_TYPE_SHORT;
  bind[0].buffer = (char *)&cur_x;
  bind[0].is_null = 0;
  bind[0].length = 0;
  bind[0].is_unsigned = TRUE;
  bind[1].buffer_type = MYSQL_TYPE_SHORT;
  bind[1].buffer = (char *)&cur_y;
  bind[1].is_null = 0;
  bind[1].length = 0;
  bind[1].is_unsigned = TRUE;
  bind[2].buffer_type = MYSQL_TYPE_TINY;
  bind[2].buffer = (char *)&cur_val;
  bind[2].is_null = 0;
  bind[2].length = 0;
  bind[2].is_unsigned = TRUE;
  if (mysql_stmt_bind_param(stmt, bind))
  {
    act_log_error(act_log_msg("Failed to bind parameters for prepared statement - %s", mysql_stmt_error(stmt)));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  
  int i,j;
  for (i=0; i < G_targ_img.img_params.img_width; i++)
  {
    for (j=0; j < G_targ_img.img_params.img_height; j++)
    {
      cur_x = i;
      cur_y = j;
      cur_val = G_targ_img.img_data[i*G_targ_img.img_params.img_height + j];
      if (mysql_stmt_execute(stmt))
      {
        act_log_error(act_log_msg("Failed to execute prepared statement to insert pixel datum into database - %s", mysql_stmt_error(stmt)));
        ret = FALSE;
        break;
      }
    }
  }
  if (!ret)
  {
    act_log_error(act_log_msg("Entire image not saved. Rolling back database transactions."));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  mysql_query(conn, "COMMIT;");
  return TRUE;  
}

char ccdcntrl_write_fits(const char *filename)
{
  if (filename == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  fitsfile *fptr;
  
  int bitpix = BYTE_IMG;
  long naxis = 2;                    // 2-dimensional image
  long naxes[2] = {G_targ_img.img_params.img_width, G_targ_img.img_params.img_height};
  int fits_stat = 0;                     // initialize status before calling fitsio routines 
  
  if (fits_create_file(&fptr, filename, &fits_stat)) // create new FITS file
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Error creating FITS file (%s) - %s", filename, err_str));
    return -1;
  }

  // Write the required keywords for the primary array image
  if (fits_create_img(fptr,  bitpix, naxis, naxes, &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Error creating FITS image (%s) - %s", filename, err_str));
    return -1;
  }

  // Write the array of long integers (after converting them to short)
  if (fits_write_img(fptr, TBYTE, 1, naxes[0] * naxes[1], (void *)G_targ_img.img_data, &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write image data to %s - %s", filename, err_str));
    return -1;
  }

  char ret_val = 1;
  // Write another optional keyword; must pass the ADDRESS of the value
  float exposure = (float)G_targ_img.img_params.exp_t_msec / 1000.0;
  if (fits_update_key(fptr, TFLOAT, "EXPOSURE", &exposure, "Exposure Time (s)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write exposure time to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  double ra_h = convert_HMSMS_H_ra(&G_targ_info.ra);
  double dec_d = convert_DMS_D_dec(&G_targ_info.dec);
  double ha_h = convert_HMSMS_H_ha(&G_targ_info.ha);
  if (fits_update_key(fptr, TDOUBLE, "RA_h", &ra_h, "Right Ascension (hours)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write right ascension (hours) to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  if (fits_update_key(fptr, TDOUBLE, "HA_h", &ha_h, "Hour Angle (hours)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write hour angle (hours) to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  if (fits_update_key(fptr, TDOUBLE, "DEC_d", &dec_d, "Declination (degrees)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write declination (degrees) to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  char *tmpstr = ra_to_str(&G_targ_info.ra);
  if (fits_update_key(fptr, TSTRING, "RA", tmpstr, "Right Ascension", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write right ascension (hours) to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  free(tmpstr);
  tmpstr = malloc(20);
  if (convert_DMS_ASEC_dec(&G_targ_info.dec) < 0)
    snprintf(tmpstr, 20, "-%3hhdd%02hhd\'%02hhd\"", -G_targ_info.dec.degrees, -G_targ_info.dec.amin, -G_targ_info.dec.asec);
  else
    snprintf(tmpstr, 20, " %3hhdd%02hhd\'%02hhd\"", G_targ_info.dec.degrees, G_targ_info.dec.amin, G_targ_info.dec.asec);
  if (fits_update_key(fptr, TSTRING, "DEC", tmpstr, "Declination", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write declination to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  free(tmpstr);
  tmpstr = date_to_str(&G_targ_info.unidate);
  if (fits_update_key(fptr, TSTRING, "START_DATE", tmpstr, "Date at exposure start (Universal Time)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not exposure start date to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  free(tmpstr);
  tmpstr = time_to_str(&G_targ_info.unitime);
  if (fits_update_key(fptr, TSTRING, "START_TIME", tmpstr, "Time at exposure start (Universal Time)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write exposure start time to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  free(tmpstr);
  
  double jd = calc_GJD (&G_targ_info.unidate, &G_targ_info.unitime);
  if (fits_update_key(fptr, TDOUBLE, "START_JD", &jd, "Julian Day at exposure start", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write exposure start JD to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  
  double hjd;
  calc_sun (jd, &G_targ_info.ra, &G_targ_info.dec, NULL, NULL, &hjd);
  if (fits_update_key(fptr, TDOUBLE, "START_HJD", &hjd, "Heliocentric Julian day at exposure start", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write exposure start HJD to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  
  double sidt = calc_SidT (jd);
  if (fits_update_key(fptr, TDOUBLE, "START_SIDT_h", &sidt, "Sidereal Time at exposure start (hours)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write exposure start sidereal time (hours) to %s  - %s", filename, err_str));
    ret_val = 0;
  }

  if (strlen(G_targ_info.targ_name) > 0)
  {
    if (fits_update_key(fptr, TSTRING, "TARG", G_targ_info.targ_name, "Target Identifier", &fits_stat))
    {
      char err_str[35];
      fits_get_errstatus (fits_stat, err_str);
      act_log_error(act_log_msg("Could not write target identifier to %s  - %s", filename, err_str));
      ret_val = 0;
    }
  }
  if (fits_update_key(fptr, TINT, "TARG_INT", &G_targ_info.targ_id, "Internal Target Identifier", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write internal target identifier to %s  - %s", filename, err_str));
    ret_val = 0;
  }
  if (fits_update_key(fptr, TBYTE, "STAR_SKY", &G_targ_info.sky, "Star (FALSE) or Sky (TRUE)", &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not write star/sky to %s - %s", filename, err_str));
    ret_val = 0;
  }
  
  if (fits_close_file(fptr, &fits_stat))
  {
    char err_str[35];
    fits_get_errstatus (fits_stat, err_str);
    act_log_error(act_log_msg("Could not close FITS file  to %s - %s", filename, err_str));
    return -1;
  }
  
  return ret_val;
}

unsigned short ccdcntrl_get_max_width()
{
  return G_modes.max_width_px;
}

unsigned short ccdcntrl_get_max_height()
{
  return G_modes.max_height_px;
}

unsigned short ccdcntrl_get_ra_width()
{
  return G_modes.ra_width_asec;
}

unsigned short ccdcntrl_get_dec_height()
{
  return G_modes.dec_height_asec;
}

void ccdcntrl_set_coords(struct act_msg_coord *msg)
{
  if (msg == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  memcpy(&G_cur_info.ra, &msg->ra, sizeof(struct rastruct));
  memcpy(&G_cur_info.ha, &msg->ha, sizeof(struct hastruct));
  memcpy(&G_cur_info.dec, &msg->dec, sizeof(struct decstruct));
}

void ccdcntrl_set_time(struct act_msg_time *msg)
{
  if (msg == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  memcpy(&G_cur_info.unidate, &msg->unid, sizeof(struct datestruct));
  memcpy(&G_cur_info.unitime, &msg->unit, sizeof(struct timestruct));
}

void ccdcntrl_get_ccdcaps(struct act_msg_ccdcap *msg_ccdcap)
{
  if (msg_ccdcap == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  msg_ccdcap->min_exp_t_msec = G_modes.min_exp_t_msec;
  msg_ccdcap->max_exp_t_msec = G_modes.max_exp_t_msec;
  snprintf(msg_ccdcap->ccd_id, MAX_CCD_ID_LEN-1, "%s", G_modes.ccd_id);
  memset(msg_ccdcap->filters, 0, sizeof(msg_ccdcap->filters));
  /// TODO: Implement proper translation from CCD prebin modes to IPC prebin modes
  msg_ccdcap->prebin_x = G_modes.prebin_x;
  msg_ccdcap->prebin_y = G_modes.prebin_y;
  memcpy(msg_ccdcap->windows, G_modes.windows, CCD_MAX_NUM_WINDOW_MODES*sizeof(struct ccd_window_mode));
}

int ccdcntrl_start_targset_exp(struct ccdcntrl_objects *ccdcntrl_objs, struct act_msg_targset *msg)
{
  if ((ccdcntrl_objs == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  memcpy(G_cur_info.targ_name, msg->targ_name, MAX_TARGID_LEN);
  act_log_debug(act_log_msg("Starting target set exposure for target %d", msg->targ_id));
  G_cur_info.targ_id = msg->targ_id;
  G_cur_info.user_id = 1;
  G_cur_info.sky = msg->sky;
  if (!msg->mode_auto)
    return 0;
  if (ccdcntrl_objs->merlin_stat & (CCD_ERROR | EXP_ORDERED | CCD_INTEGRATING | CCD_READING_OUT))
  {
    act_log_error(act_log_msg("CCD is currently busy (status %hhd). Cannot start a target set exposure."));
    return -OBSNSTAT_ERR_RETRY;
  }
  
  float exp_t_sec = get_pat_exp_t_sec(*(ccdcntrl_objs->mysql_conn), msg->targ_id);
  if (exp_t_sec < 0)
  {
    act_log_error(act_log_msg("Could not retrieve pattern exposure time from SQL database. Recommending the next target."));
    return -OBSNSTAT_ERR_NEXT;
  }
  if (exp_t_sec > 30)
  {
    act_log_debug(act_log_msg("Very long exposure time selected (%f sec), not doing automatic target set.", exp_t_sec));
    return -OBSNSTAT_ERR_NEXT;
  }
  
  gtk_combo_box_set_active(GTK_COMBO_BOX(ccdcntrl_objs->cmb_window), 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ccdcntrl_objs->cmb_prebin), 0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_expt), exp_t_sec);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_repeat), 1);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->chk_phot_exp), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->chk_save), FALSE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose), TRUE);
  
  return 0;
}

int ccdcntrl_check_targset_exp(struct ccdcntrl_objects *ccdcntrl_objs, double *adj_ra, double *adj_dec, unsigned char *targ_cent)
{
  act_log_debug(act_log_msg("Checking target set exposure"));
  if ((ccdcntrl_objs == NULL) || (adj_ra == NULL) || (adj_dec == NULL) || (targ_cent == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -OBSNSTAT_ERR_RETRY;
  }
    
  if ((ccdcntrl_objs->merlin_stat & IMG_READY) == 0)
  {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose)))
    {
      act_log_error(act_log_msg("CCD does not appear to be exposing. This probably means it was cancelled by the user."));
      return -OBSNSTAT_ERR_RETRY;
    }
    act_log_normal(act_log_msg("Image not ready yet."));
    return 0;
  }
  
  struct point *pattern_points = NULL;
  /// TODO: Implement proper treatment of prebin and window
  int ccd_offset_x = XAPERTURE - G_modes.max_width_px/2, ccd_offset_y = YAPERTURE - G_modes.max_height_px/2;
  int num_pat_points = load_pattern(*(ccdcntrl_objs->mysql_conn), G_targ_info.targ_id, G_targ_info.sky, ccd_offset_x, ccd_offset_y, &pattern_points);
  if (num_pat_points < 0)
  {
    act_log_error(act_log_msg("No pattern found."));
    return -OBSNSTAT_ERR_NEXT;
  }
  
  int num_pix = G_targ_img.img_params.img_width * G_targ_img.img_params.img_height;
  float image_float[num_pix];
  int i;
  for (i=0; i<num_pix; i++)
    image_float[i] = G_targ_img.img_data[i];
  struct blob *bloblst;
  int num_blobs = 0;
  if (FindBlobs2 (image_float, G_targ_img.img_params.img_height, G_targ_img.img_params.img_width, &bloblst, &num_blobs) < 0)
  {
    act_log_error(act_log_msg("Error finding blobs in image"));
    if (bloblst != NULL)
      free(bloblst);
    return -OBSNSTAT_ERR_RETRY;
  }
  act_log_debug(act_log_msg("%d blobs found", num_blobs));
  
  struct point* acq_points;
  int num_acq_points;
  Blobs2Points (bloblst, num_blobs, STARINT_MINIMUM, &acq_points, &num_acq_points);
  if (num_acq_points <= 0)
  {
    act_log_error(act_log_msg("Error converting blobs to points\n"));
    if (bloblst != NULL)
      free(bloblst);
    return -OBSNSTAT_ERR_RETRY;
  }
  free(bloblst);
  bloblst = NULL;
  num_blobs = 0;
  act_log_debug(act_log_msg("%d points found.", num_acq_points));
  
  int *nMap, nMatch;
  FindPointMapping (pattern_points, num_pat_points, acq_points, num_acq_points, &nMap, &nMatch);
  if (nMatch <= 0)
  {
    if (pattern_points != NULL)
      free(pattern_points);
    if (acq_points != NULL)
      free(acq_points);
    return -OBSNSTAT_ERR_RETRY;
  }
  act_log_debug(act_log_msg("%d points matched", nMatch));
  
  double xshift=0.0, yshift=0.0;
  for (i=0; i<num_pat_points; i++)
  {
    if (nMap[i] >= 0)
    {
      xshift += pattern_points[i].x - acq_points[nMap[i]].x;
      yshift += pattern_points[i].y - acq_points[nMap[i]].y;
    }
  }
  xshift /= (float)nMatch;
  yshift /= (float)nMatch;
  if (acq_points != NULL)
    free(acq_points);
  acq_points = NULL;
  if (pattern_points != NULL)
    free(pattern_points);
  pattern_points = NULL;
  if (nMap != NULL)
    free(nMap);
  nMap = NULL;
  
  if (((float)num_pat_points / (float)nMatch) < MIN_PAT_STARS_FRAC)
  {
    act_log_error(act_log_msg("Sub-minimum number of stars matched."));
    return -OBSNSTAT_ERR_RETRY;
  }
  
  double dec_d = convert_DMS_D_dec(&G_targ_info.dec);
  if (fabs(dec_d) > 89.0)
    dec_d = 89.0 * dec_d/fabs(dec_d);
  act_log_debug(act_log_msg("RA adjustment: %f   %d   %d   %f", -xshift, G_modes.ra_width_asec, G_modes.max_width_px, cos(convert_DEG_RAD(dec_d))));
  double ra_h = convert_DEG_H (-xshift * G_modes.ra_width_asec / G_modes.max_width_px / cos(convert_DEG_RAD(dec_d)) / 3600.0);
  dec_d = yshift * G_modes.dec_height_asec / G_modes.max_height_px / 3600.0;
  act_log_debug(act_log_msg("Dec adjustment: %f   %d   %d", yshift, G_modes.dec_height_asec, G_modes.max_height_px));
  *adj_ra = *adj_ra + ra_h;
  *adj_dec = *adj_dec + dec_d;
  if ((ra_h < RA_TOL_H) && (dec_d < DEC_TOL_D))
  {
    act_log_normal(act_log_msg("Target is centred."));
    *targ_cent = TRUE;
//     ccdcntrl_save_image(ccdcntrl_objs);
  }
  else
    *targ_cent = FALSE;
  act_log_debug(act_log_msg("RA, Dec adjustment: %f s   %f \"", ra_h*3600.0, dec_d*3600.0));
  return 1;
}

int ccdcntrl_start_phot_exp(struct ccdcntrl_objects *ccdcntrl_objs, struct act_msg_dataccd *msg)
{
  if ((ccdcntrl_objs == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -OBSNSTAT_ERR_RETRY;
  }
  if (ccdcntrl_objs->merlin_stat & (CCD_ERROR | EXP_ORDERED | CCD_INTEGRATING | CCD_READING_OUT))
  {
    act_log_error(act_log_msg("CCD is currently busy (status %hhd). Cannot start a target set exposure."));
    return -OBSNSTAT_ERR_RETRY;
  }
  
  memcpy(G_cur_info.targ_name, msg->targ_name, MAX_TARGID_LEN);
  G_cur_info.targ_id = msg->targ_id;
  G_cur_info.user_id = msg->user_id;
  G_cur_info.sky = FALSE;
  
  unsigned long exp_t_msec = msg->exp_t_s*1000.0;
  unsigned long win_num = msg->window_mode_num;
  if (win_num >= CCD_MAX_NUM_WINDOW_MODES)
  {
    act_log_error(act_log_msg("Invalid window mode requested. Selecting full-frame"));
    win_num = 0;
  }
  else if ((G_modes.windows[win_num].width_px == 0) || (G_modes.windows[win_num].height_px == 0))
  {
    act_log_error(act_log_msg("Unavailalbe window mode requested. Selecting full-frame"));
    win_num = 0;
  }
  /// TODO: Implement prebinning
//   unsigned long prebin_num = msg->prebin;
//   if (prebin_num != 0)
//   {
//     act_log_error(act_log_msg("Requested prebinning mode not yet implemented."));
//     prebin_num = 0;
//   }
  
  gtk_combo_box_set_active(GTK_COMBO_BOX(ccdcntrl_objs->cmb_window), win_num);
//   gtk_combo_box_set_active(GTK_COMBO_BOX(ccdcntrl_objs->cmb_prebin), prebin_num);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_expt), exp_t_msec);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_repeat), msg->repetitions);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->chk_phot_exp), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->chk_save), TRUE);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose), TRUE);
  
  return 0;
}

int ccdcntrl_check_phot_exp(struct ccdcntrl_objects *ccdcntrl_objs)
{
  if (ccdcntrl_objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -OBSNSTAT_ERR_RETRY;
  }
  if ((ccdcntrl_objs->merlin_stat & CCD_ERROR) != 0)
    return -OBSNSTAT_ERR_RETRY;
  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose)))
    return 0;
  return 1;
}

void ccdcntrl_cancel_exp(struct ccdcntrl_objects *ccdcntrl_objs)
{
  if (ccdcntrl_objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose), FALSE);
}


gboolean ccd_stat_update (GIOChannel *ccd_chan, GIOCondition condition, gpointer user_data)
{
  (void) condition;
  struct ccdcntrl_objects *ccdcntrl_objs = (struct ccdcntrl_objects *)user_data;
  unsigned int num_bytes;
  int ret;
  char tmp_ccd_stat;
  GError *error = NULL;
  ret = g_io_channel_read_chars (ccd_chan, &tmp_ccd_stat, 1, &num_bytes, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("An error occurred while attempting to read CCD driver status (%d - %s)", error->code, error->message));
    g_error_free(error);
    return TRUE;
  }
  if (ret != G_IO_STATUS_NORMAL)
  {
    act_log_error(act_log_msg("An unknown error occurred while attempting to read the CCD driver status."));
    return TRUE;
  }
  ccdcntrl_objs->merlin_stat = tmp_ccd_stat;
  
  if ((ccdcntrl_objs->merlin_stat & CCD_STAT_UPDATE) == 0)
    return TRUE;
  update_statdisp(ccdcntrl_objs);
  if ((ccdcntrl_objs->merlin_stat & IMG_READY) == 0)
    return TRUE;
  
  struct merlin_img newimg;
  int merlin_fd = g_io_channel_unix_get_fd (ccd_chan);
  ret = ioctl(merlin_fd, IOCTL_GET_IMAGE, &newimg);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Error reading new image from MERLIN CCD driver - %s.", strerror(ret)));
    return TRUE;
  }
  struct timestruct tmp_time = {.hours = newimg.img_params.start_hrs, .minutes = newimg.img_params.start_min, .seconds = newimg.img_params.start_sec, .milliseconds = newimg.img_params.start_msec };
  check_systime_discrep(&G_targ_info.unidate, &G_targ_info.unitime, &tmp_time);
  memcpy(&G_targ_img, &newimg, sizeof(struct merlin_img));
  update_imgdisp(&newimg, &G_targ_info.ra, &G_targ_info.dec);

  if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->chk_save)))
  {
    if (!ccdcntrl_save_image(ccdcntrl_objs))
      save_fits_fallback();
  }
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose)))
    return TRUE;
  int tmp_rpt = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_repeat));
  if (tmp_rpt == 0)
    start_exposure(ccdcntrl_objs);
  else if (tmp_rpt > 0)
  {
    tmp_rpt--;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_repeat), tmp_rpt);
    if (tmp_rpt > 0)
    {
      ret = start_exposure(ccdcntrl_objs);
      if (ret < 0)
        act_log_error(act_log_msg("Error ordering next exposure - %s.", strerror(ret)));
    }
    else
    {
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->btn_expose), FALSE);
    }
  }
  return TRUE;
}

void update_statdisp(struct ccdcntrl_objects *ccdcntrl_objs)
{
  if (ccdcntrl_objs->merlin_stat & CCD_ERROR)
  {
    GdkColor newcol;
    gdk_color_parse("#AA0000", &newcol);
    gtk_widget_modify_bg(ccdcntrl_objs->evb_ccdstat, GTK_STATE_NORMAL, &newcol);
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "ERROR");
    return;
  }
  GdkColor newcol;
  gdk_color_parse("#00AA00", &newcol);
  gtk_widget_modify_bg(ccdcntrl_objs->evb_ccdstat, GTK_STATE_NORMAL, &newcol);
  if (ccdcntrl_objs->merlin_stat == 0)
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "IDLE");
  else if (ccdcntrl_objs->merlin_stat & EXP_ORDERED)
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "EXP ORDERED");
  else if (ccdcntrl_objs->merlin_stat & CCD_INTEGRATING)
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "INTEGRATING");
  else if (ccdcntrl_objs->merlin_stat & CCD_READING_OUT)
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "READOUT");
  else if (ccdcntrl_objs->merlin_stat & IMG_READY)
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "IMG READY");
  else
  {
    gtk_label_set_text(GTK_LABEL(ccdcntrl_objs->lbl_ccdstat), "UNKNOWN");
    gdk_color_parse("#0000AA", &newcol);
    gtk_widget_modify_bg(ccdcntrl_objs->evb_ccdstat, GTK_STATE_NORMAL, &newcol);
  }
}

char start_exposure(struct ccdcntrl_objects *ccdcntrl_objs)
{
  act_log_debug(act_log_msg("Starting exposure."));
  struct ccd_cmd cmd;
  memset(&cmd, 0, sizeof(struct ccd_cmd));
  
  GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(ccdcntrl_objs->cmb_prebin));
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(ccdcntrl_objs->cmb_prebin), &iter))
  {
    act_log_error(act_log_msg("Could not retrieve user-selected prebinning mode. Using first available mode."));
    cmd.prebin_x = CCD_PREBIN_1;
    cmd.prebin_y = CCD_PREBIN_1;
  }
  else
  {
    int tmp_prebin;
    gtk_tree_model_get (model, &iter, PREBINSTORE_X, &tmp_prebin, -1);
    cmd.prebin_x = tmp_prebin;
    gtk_tree_model_get (model, &iter, PREBINSTORE_Y, &tmp_prebin, -1);
    cmd.prebin_y = tmp_prebin;
  }
  
  model = gtk_combo_box_get_model(GTK_COMBO_BOX(ccdcntrl_objs->cmb_window));
  if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(ccdcntrl_objs->cmb_window), &iter))
  {
    act_log_error(act_log_msg("Could not retrieve user-selected window mode. Using first available mode."));
    cmd.window = 0;
  }
  else
  {
    int tmp_window;
    gtk_tree_model_get (model, &iter, WINDOWSTORE_MODENUM, &tmp_window, -1);
    cmd.window = tmp_window;
  }
  
  cmd.start_at_sec = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ccdcntrl_objs->chk_phot_exp));
  float exp_t_sec = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ccdcntrl_objs->spn_expt));
  cmd.exp_t_msec = (unsigned long)(round(exp_t_sec * 1000.0));
  
  int merlin_fd = g_io_channel_unix_get_fd (ccdcntrl_objs->ccd_chan);
  long ret = ioctl(merlin_fd, IOCTL_ORDER_EXP, &cmd);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Error sending expose command to MERLIN CCD driver - %s.", strerror(ret)));
    return -1;
  }
  
  memcpy(&G_targ_info, &G_cur_info, sizeof(struct exposure_information));
  act_log_debug(act_log_msg("Success."));
  return 0;
}

void expose_toggled(GtkWidget *btn_expose, gpointer user_data)
{
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn_expose)))
    return;
  struct ccdcntrl_objects *ccdcntrl_objs = (struct ccdcntrl_objects *)user_data;
  if (start_exposure(ccdcntrl_objs) != 0)
  {
    act_log_error(act_log_msg("Could not start exposure on user request."));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn_expose),FALSE);
  }
}

void expose_clicked()
{
  G_cur_info.targ_id = 1;
  G_cur_info.user_id = 1;
  G_cur_info.sky = FALSE;
}

char get_data_dir(struct ccdcntrl_objects *ccdcntrl_objs)
{
  MYSQL *conn = *(ccdcntrl_objs->mysql_conn);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(conn,"SELECT conf_value FROM global_config WHERE conf_key IS img_data_dir;");
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Error occurred while attempting to retrieve image data directory name from SQL database - %s.", mysql_error(conn)));
    mysql_free_result(result);
    char *data_dir = fallback_data_dir();
    if (data_dir == NULL)
    {
      act_log_error(act_log_msg("Failed to set the fallback data directory."));
      return FALSE;
    }
    ccdcntrl_objs->data_dir = data_dir;
    return TRUE;
  }
  if (mysql_num_fields(result) != 1)
  {
    act_log_error(act_log_msg("Invalid data returned while attempting to retrieve image data directory from SQL database (result has %d fields instead of 1)", mysql_num_fields(result)));
    mysql_free_result(result);
    char *data_dir = fallback_data_dir();
    if (data_dir == NULL)
    {
      act_log_error(act_log_msg("Failed to set the fallback data directory."));
      return FALSE;
    }
    ccdcntrl_objs->data_dir = data_dir;
    return TRUE;
  }
  int rowcount = mysql_num_rows(result);
  if (rowcount <= 0)
  {
    act_log_error(act_log_msg("No data returned while attempting to retrieve image data directory from SQL database."));
    mysql_free_result(result);
    char *data_dir = fallback_data_dir();
    if (data_dir == NULL)
    {
      act_log_error(act_log_msg("Failed to set the fallback data directory."));
      return FALSE;
    }
    ccdcntrl_objs->data_dir = data_dir;
    return TRUE;
  }
  if (rowcount > 1)
    act_log_error(act_log_msg("WARNING: More than one image data directory specified in SQL database. Choosing the first and hoping for the best."));

  row = mysql_fetch_row(result);
  if (ccdcntrl_objs->data_dir != NULL)
    free(ccdcntrl_objs->data_dir);
  ccdcntrl_objs->data_dir = malloc(strlen(row[0])+2);
  sprintf(ccdcntrl_objs->data_dir, "%s", row[0]);
  if (ccdcntrl_objs->data_dir[strlen(ccdcntrl_objs->data_dir)-1] != '/')
  {
    int len = strlen(ccdcntrl_objs->data_dir);
    ccdcntrl_objs->data_dir[len-1] = '/';
    ccdcntrl_objs->data_dir[len] = '\0';
  }
  struct stat tmp_fstat;
  if (stat(ccdcntrl_objs->data_dir,&tmp_fstat) != 0)
  {
    act_log_normal(act_log_msg("Image data directory (%s) does not exist.", ccdcntrl_objs->data_dir));
    return create_data_dir(ccdcntrl_objs->data_dir);
  }

  act_log_normal(act_log_msg("Backup image data directory - %s.", ccdcntrl_objs->data_dir));
  mysql_free_result(result);
  return TRUE;
}

float get_pat_exp_t_sec(MYSQL *conn, int targ_id)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  char sqlqry[256];
  snprintf(sqlqry, sizeof(sqlqry), "SELECT exp_t_s FROM acq_patterns WHERE targ_id=%d",targ_id);
  mysql_query(conn, sqlqry);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Error occurred while attempting to retrieve target set pattern exposure time from SQL database - %s.", mysql_error(conn)));
    mysql_free_result(result);
    return -1;
  }
  if (mysql_num_fields(result) != 1)
  {
    act_log_error(act_log_msg("Invalid data returned while attempting to retrieve target set pattern exposure time from SQL database (result has %d fields instead of 1)", mysql_num_fields(result)));
    mysql_free_result(result);
    return -1;
  }
  int rowcount = mysql_num_rows(result);
  if (rowcount <= 0)
  {
    act_log_error(act_log_msg("No data returned while attempting to retrieve target set pattern exposure time from SQL database."));
    mysql_free_result(result);
    return -1;
  }
  if (rowcount > 1)
    act_log_error(act_log_msg("WARNING: More than one pattern matching target specified in SQL database. Choosing the first and hoping for the best."));
  
  row = mysql_fetch_row(result);
  double exp_t_s;
  if (sscanf(row[0], "%lf", &exp_t_s) != 1)
  {
    act_log_error(act_log_msg("Could not extract exposure time from SQL database."));
    return -1;
  }
  mysql_free_result(result);
  return exp_t_s;
}

int load_pattern(MYSQL *conn, int targ_id, char sky, int ccd_offset_x, int ccd_offset_y, struct point **pattern_points)
{
  MYSQL_RES *result;
  MYSQL_ROW row;
  char qrystr[256];
  if (sky)
    sprintf(qrystr, "SELECT id,sky_offset_x_pix, sky_offset_y_pix FROM acq_patterns WHERE targ_id=%d", targ_id);
  else
    sprintf(qrystr, "SELECT id,star_offset_x_pix, star_offset_y_pix FROM acq_patterns WHERE targ_id=%d", targ_id);
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Error occurred while attempting to retrieve pixel offset for target %d from SQL database - %s.", targ_id, mysql_error(conn)));
    return -1;
  }
  if (mysql_num_fields(result) != 3)
  {
    act_log_error(act_log_msg("Invalid data returned while attempting to retrieve pixel offset for target %d from SQL database (result has %d fields instead of 3)", targ_id, mysql_num_fields(result)));
    mysql_free_result(result);
    return -1;
  }
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    act_log_error(act_log_msg("Invalid number of rows retrieved from SQL database while attempting to retrieve pixel offset for target %d (%d lins)", targ_id, rowcount));
    mysql_free_result(result);
    return -1;
  }
  unsigned long pat_id;
  float x_offset, y_offset;
  row = mysql_fetch_row(result);
  if ((sscanf(row[0], "%lu", &pat_id) != 1) || (sscanf(row[1], "%f", &x_offset) != 1) || (sscanf(row[2], "%f", &y_offset) != 1))
  {
    act_log_error(act_log_msg("Failed to extract pattern id or x or y pixel offset from SQL database (%s %s %s; %u %f %f).", row[0], row[1], row[2], pat_id, x_offset, y_offset));
    mysql_free_result(result);
    return -1;
  }
  x_offset += ccd_offset_x;
  y_offset += ccd_offset_y;
  mysql_free_result(result);
  
  snprintf(qrystr, sizeof(qrystr), "SELECT x_pix,y_pix FROM acq_pattern_points WHERE pattern_id=%lu", pat_id);
  mysql_query(conn,qrystr);
  result = mysql_store_result(conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Error occurred while attempting to retrieve target set pattern for target %d, pattern %u from SQL database - %s.", targ_id, pat_id, mysql_error(conn)));
    return -1;
  }
  if (mysql_num_fields(result) != 2)
  {
    act_log_error(act_log_msg("Invalid data returned while attempting to retrieve target set pattern for target %d, pattern %u from SQL database (result has %d fields instead of 2)", targ_id, pat_id, mysql_num_fields(result)));
    mysql_free_result(result);
    return -1;
  }
  rowcount = mysql_num_rows(result);
  if (rowcount <= 0)
  {
    act_log_error(act_log_msg("No data returned while attempting to retrieve target set pattern for target %d, pattern %u from SQL database.", targ_id, pat_id));
    mysql_free_result(result);
    return -1;
  }
  
  struct point *tmp_points = malloc(sizeof(struct point)*rowcount);
  int used_rows = 0;
  int tmp_x_pix, tmp_y_pix;
  while ((row = mysql_fetch_row(result)) != NULL)
  {
    if ((sscanf(row[0], "%d", &tmp_x_pix) != 1) || (sscanf(row[1], "%d", &tmp_y_pix) != 1))
    {
      act_log_error(act_log_msg("Could not extract pattern point %d X,Y coordinates from string retrieved from SQL database.", used_rows));
      continue;
    }
    tmp_points[used_rows].x = tmp_x_pix + x_offset;
    tmp_points[used_rows].y = tmp_y_pix + y_offset;
    used_rows++;
  }
  if (used_rows <= 0)
  {
    act_log_error(act_log_msg("Could not retrieve pattern point coordinates from SQL database."));
    free(tmp_points);
    return -1;
  }
  *pattern_points = malloc(sizeof(struct point)*used_rows);
  memcpy(*pattern_points, tmp_points,sizeof(struct point)*used_rows);
  free(tmp_points);
  mysql_free_result(result);
  return used_rows;
}

void save_fits_fallback(const char *data_dir)
{
  const char *dir;
  if (data_dir == NULL)
  {
    char *fallback_dir = fallback_data_dir();
    if (fallback_dir == NULL)
    {
      act_log_error(act_log_msg("Failed to set the fallback data directory. Images will not be saved."));
      return;
    }
    dir = fallback_dir;
  }
  else
    dir = data_dir;
  char img_filepath[100];
  sprintf(img_filepath, "%s%04hu%02hhu%02hhu_%02hhu%02hhu%02hhu.fits", dir, G_targ_info.unidate.year, G_targ_info.unidate.month, G_targ_info.unidate.day, G_targ_info.unitime.hours, G_targ_info.unitime.minutes, G_targ_info.unitime.seconds);
  if (ccdcntrl_write_fits(img_filepath) <= 0)
  {
    act_log_error(act_log_msg("Error encountered while falling back to saving the image as a FITS file. Image will be dumped in the log."));
    act_log_normal(act_log_msg("Image header begins"));
    act_log_normal(act_log_msg("Exposure time (s) : %f", (float)G_targ_img.img_params.exp_t_msec / 1000.0));
    char *tmpstr = ra_to_str(&G_targ_info.ra);
    act_log_normal(act_log_msg("Right Ascension : %s", tmpstr));
    free(tmpstr);
    tmpstr = dec_to_str(&G_targ_info.dec);
    act_log_normal(act_log_msg("Declination : %s", tmpstr));
    free(tmpstr);
    tmpstr = date_to_str(&G_targ_info.unidate);
    act_log_normal(act_log_msg("Universal Date at exposure start : %s", tmpstr));
    free(tmpstr);
    tmpstr = time_to_str(&G_targ_info.unitime);
    act_log_normal(act_log_msg("Universal Time at exposure start : %s", tmpstr));
    free(tmpstr);
    act_log_normal(act_log_msg("ACT target identifier : %d", G_targ_info.targ_id));
    act_log_normal(act_log_msg("%s", G_targ_info.sky ? "SKY" : "STAR"));
    act_log_normal(act_log_msg("Image header ends"));
    act_log_normal(act_log_msg("Image data starts"));
    unsigned long img_size = G_targ_img.img_params.img_len * sizeof(ccd_pixel_type) / sizeof(char);
    char img_data[img_size+1];
    memcpy(img_data, G_targ_img.img_data, img_size);
    img_data[img_size] = '\0';
    act_log_normal(act_log_msg("%s", img_data));
    act_log_normal(act_log_msg("Image data ends"));
  }
  return;
}

char *fallback_data_dir()
{
  act_log_normal(act_log_msg("Attempting to set fallback data directory."));
  char *homedir = getenv ("HOME");
  if (strlen(homedir) == 0)
  {
    act_log_error(act_log_msg("Could not determine user's home directory path. Cannot create fallback data directory."));
    return NULL;
  }
  char *data_dir = malloc(strlen(homedir)+10);
  if (homedir[strlen(homedir)-1] == '/')
    sprintf(data_dir, "%simg_data/", homedir);
  else
    sprintf(data_dir, "%s/img_data/", homedir);

  struct stat tmp_fstat;
  if (stat(data_dir,&tmp_fstat) != 0)
  {
    act_log_normal(act_log_msg("Image data directory (%s) does not exist.", data_dir));
    if (!create_data_dir(data_dir))
    {
      act_log_error(act_log_msg("Failed to create fallback data directory."));
      free(data_dir);
      return NULL;
    }
  }
  return data_dir;
}

char create_data_dir(const char *data_dir)
{
  return mkdir(data_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
}
