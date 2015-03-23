#include <gtk/gtk.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <pwd.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include <act_log.h>
#include <act_ipc.h>
#include <act_positastro.h>
#include "acq_store.h"

#define STAT_STORING      0x01
#define STAT_ERR_RETRY    0x02
#define STAT_ERR_NO_RECOV 0x04

#define STORE_MAX_RETRIES 3

enum
{
  STATUS_UPDATE,
  LAST_SIGNAL
};

enum
{
  DB_TYPE_ANY = 1,
  DB_TYPE_ACQ_OBJ,
  DB_TYPE_ACQ_SKY,
  DB_TYPE_OBJECT,
  DB_TYPE_BIAS,
  DB_TYPE_DARK,
  DB_TYPE_FLAT,
  DB_TYPE_MASTER_BIAS,
  DB_TYPE_MASTER_DARK,
  DB_TYPE_MASTER_FLAT
};


static guint acq_store_signals[LAST_SIGNAL] = { 0 };


static void acq_store_instance_init(GObject *acq_store);
static void acq_store_class_init(AcqStoreClass *klass);
static void acq_store_instance_dispose(GObject *acq_store);
static void *store_pending_img(void *acq_store);
static void store_next_img(AcqStore *objs);
static gboolean store_img(MYSQL *conn, CcdImg *img);
static void store_img_fallback(CcdImg *img);
static gboolean store_reconnect(AcqStore *objs);
static guchar img_type_acq_to_db(guchar acq_img_type);


GType acq_store_get_type (void)
{
  static GType acq_store_type = 0;
  
  if (!acq_store_type)
  {
    const GTypeInfo acq_store_info =
    {
      sizeof (AcqStoreClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) acq_store_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (AcqStore),
      0,
      (GInstanceInitFunc) acq_store_instance_init,
      NULL
    };
    
    acq_store_type = g_type_register_static (G_TYPE_OBJECT, "AcqStore", &acq_store_info, 0);
  }
  
  return acq_store_type;
}

AcqStore *acq_store_new(gchar const *sqlhost)
{
  // check connections
  act_log_debug(act_log_msg("Creating MySQL image storage connection."));
  MYSQL *store_conn;
  store_conn = mysql_init(NULL);
  if (store_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising MySQL image storage connection handler - %s.", mysql_error(store_conn)));
    return NULL;
  }
  if (mysql_real_connect(store_conn, "localhost", "root", "nsW3", "tmp_sep", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(store_conn)));
    mysql_close(store_conn);
    return NULL;
  }
//   if (mysql_real_connect(store_conn, sqlhost, "act_acq", NULL, "actnew", 0, NULL, 0) == NULL)
//   {
//     act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(store_conn)));
//     mysql_close(store_conn);
//     return NULL;
//   }
  
  act_log_debug(act_log_msg("Creating general MySQL connection."));
  MYSQL *genl_conn;
  genl_conn = mysql_init(NULL);
  if (genl_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising general MySQL connection handler - %s.", mysql_error(store_conn)));
    mysql_close(store_conn);
    return NULL;
  }
  if (mysql_real_connect(genl_conn, "localhost", "root", "nsW3", "tmp_sep", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(store_conn)));
    mysql_close(store_conn);
    mysql_close(genl_conn);
    return NULL;
  }
//   if (mysql_real_connect(genl_conn, sqlhost, "act_acq", NULL, "actnew", 0, NULL, 0) == NULL)
//   {
//     act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(genl_conn)));
//     mysql_close(store_conn);
//     mysql_close(genl_conn);
//     return NULL;
//   }
  
  // create object
  AcqStore *objs = g_object_new (acq_store_get_type(), NULL);
  objs->sqlhost = malloc(strlen(sqlhost)+1);
  sprintf(objs->sqlhost, "%s", sqlhost);
  objs->store_conn = store_conn;
  objs->genl_conn = genl_conn;
  pthread_mutex_init (&objs->img_list_mutex, NULL);
  return objs; 
}

/** \brief Search database for given target name pattern
 * \param objs AcqStore object, must have been initialised
 * \param targ_name_pat Target name pattern to search for - SQL rules regarding search strings (with the 'LIKE' key word) apply
 * \return Internal database target identifier on success, <0 if an error occurred and ==0 if no match was found.
 */
glong acq_store_search_targ_id(AcqStore *objs, gchar const *targ_name_pat)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return -1;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT star_id FROM star_names WHERE star_name LIKE \"%s\";", targ_name_pat);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve internal database identifier for target names matching \"%s\" - %s.", targ_name_pat, mysql_error(objs->genl_conn)));
    return -1;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve internal database identifier for targets names matching \"%s\" - Invalid number of rows/columns returned (%d rows, %d columns).", targ_name_pat, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return -1;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No target found matching search pattern \"%s\"", targ_name_pat));
    return 0;
  }
  if (rowcount > 1)
    act_log_debug(act_log_msg("Multiple targets found matching search pattern \"%s\". Choosing first returned result.", targ_name_pat));
  
  row = mysql_fetch_row(result);
  glong tmp_targ_id;
  if (sscanf(row[0], "%ld", &tmp_targ_id) != 1)
  {
    act_log_error(act_log_msg("Error parsing internal database target identifier (%s).", row[0]));
    return -1;
  }
  mysql_free_result(result);
  return tmp_targ_id;
}

/** \brief Search database for name of target matching given DB ID
 * \param objs AcqStore object, must have been initialised
 * \param targ_id Internal database target identifier
 * \return Name of target matching given identifier, or NULL in case of failure
 */
gchar *acq_store_get_targ_name(AcqStore *objs, gulong targ_id)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return NULL;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT star_name FROM star_prim_names WHERE star_id=%lu;", targ_id);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve name for target with internal database identifier %lu - %s.", targ_id, mysql_error(objs->genl_conn)));
    return NULL;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve name for target with internal database identifier %lu - Invalid number of rows/columns returned (%d rows, %d columns).", targ_id, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return NULL;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No target found with idenfitier \"%lu\"", targ_id));
    return NULL;
  }
  
  row = mysql_fetch_row(result);
  gchar *targ_name = malloc(strlen(row[0]+1));
  sprintf(targ_name, "%s", row[0]);
  mysql_free_result(result);
  return targ_name;
}

glong acq_store_search_user_id(AcqStore *objs, gchar const *user_name_pat)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return -1;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT id FROM users WHERE name LIKE \"%s\";", user_name_pat);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve internal database identifier for user names matching \"%s\" - %s.", user_name_pat, mysql_error(objs->genl_conn)));
    return -1;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve internal database identifier for user names matching \"%s\" - Invalid number of rows/columns returned (%d rows, %d columns).", user_name_pat, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return -1;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No user found matching search pattern \"%s\"", user_name_pat));
    return 0;
  }
  if (rowcount > 1)
    act_log_debug(act_log_msg("Multiple users found matching search pattern \"%s\". Choosing first returned result.", user_name_pat));
  
  row = mysql_fetch_row(result);
  glong tmp_user_id;
  if (sscanf(row[0], "%ld", &tmp_user_id) != 1)
  {
    act_log_error(act_log_msg("Error parsing internal database user identifier (%s).", row[0]));
    return -1;
  }
  mysql_free_result(result);
  return tmp_user_id;
}

gchar *acq_store_get_user_name(AcqStore *objs, gulong user_id)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return NULL;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT name FROM users WHERE id=%lu;", user_id);
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve name for user with internal database identifier %lu - %s.", user_id, mysql_error(objs->genl_conn)));
    return NULL;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 1))
  {
    act_log_error(act_log_msg("Could not retrieve name for user with internal database identifier %lu - Invalid number of rows/columns returned (%d rows, %d columns).", user_id, rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return NULL;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No user found with idenfitier \"%lu\"", user_id));
    return NULL;
  }
  
  row = mysql_fetch_row(result);
  gchar *user_name = malloc(strlen(row[0]+1));
  sprintf(user_name, "%s", row[0]);
  mysql_free_result(result);
  return user_name;
}

struct filtaper
{
  //! Name of filter/aperture (really only for user convenience)
  char name[IPC_MAX_FILTAPER_NAME_LEN];
  //! Slot where filter/aperture currently installed
  unsigned char slot;
  //! Internal identifier for filter/aperture (used for storage)
  int db_id;
};

gboolean acq_store_get_filt_list(AcqStore *objs, acq_filters_list_t *ccd_filters)
{
  if (ccd_filters == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return FALSE;
  }
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return FALSE;
  }
  gchar qrystr[256];
  sprintf(qrystr, "SELECT ccd_filters.id, ccd_filters.slot, filter_types.name FROM ccd_filters INNER JOIN filter_types ON filter_types.id=ccd_filters.type WHERE ccd_filters.slot>=0 ORDER BY ccd_filters.slot;");
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve filters list - %s.", mysql_error(objs->genl_conn)));
    return FALSE;
  }
  
  int rowcount = mysql_num_rows(result);
  if ((rowcount < 0) || (mysql_num_fields(result) != 3))
  {
    act_log_error(act_log_msg("Could not retrieve filters list - Invalid number of rows/columns returned (%d rows, %d columns).", rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return FALSE;
  }
  if (rowcount == 0)
  {
    act_log_debug(act_log_msg("No filters found."));
    return FALSE;
  }
  if (rowcount > num_filts)
  {
    act_log_debug(act_log_msg("Too many filters found (%d, max %d)", rowcount, num_filts));
    rowcount = num_filts;
  }
  
  int i;
  for (i=0; i<num_filts; i++)
  {
    row = mysql_fetch_row(result);
    if (row == NULL)
    {
      ccd_filters.filt[i].db_id = 0;
      ccd_filters.filt[i].slot = 0;
      ccd_filters.filt[i].name[0] = '\0';
      continue;
    }
    gint tmp_slot, tmp_db_id, tmp_name_len;
    if (sscanf(row[0], "%d", &tmp_db_id) != 1)
    {
      act_log_error(act_log_msg("Failed to extract database identifier for CCD filter."));
      continue;
    }
    if (sscanf(row[1], "%d", &tmp_slot) != 1)
    {
      act_log_error(act_log_msg("Failed to extract slot number for CCD filter with ID %d.", tmp_db_id));
      continue;
    }
    if (tmp_slot <= 0)
    {
      act_log_error(act_log_msg("Invalid slot number extracted for CCD filter with ID %d.", tmp_db_id));
      continue;
    }
    tmp_name_len = strlen(row[2]);
    if (tmp_name_len >= IPC_MAX_FILTAPER_NAME_LEN)
    {
      act_log_error(act_log_msg("Name for CCD filter with ID %d (%s) is too long (%d). Trimming to %d characters.", tmp_db_id, row[2], tmp_name_len, IPC_MAX_FILTAPER_NAME_LEN));
      tmp_name_len = IPC_MAX_FILTAPER_NAME_LEN-1;
    }
    ccd_filters.filt[i].db_id = tmp_db_id;
    ccd_filters.filt[i].slot = tmp_slot;
    snprintf(ccd_filters.filt[i].name, tmp_name_len, "%s", row[2]);
  }
  return TRUE;
}

PointList *acq_store_get_tycho_pattern(AcqStore *objs, gfloat ra_d, gfloat dec_d, gfloat epoch, gfloat radius_deg)
{
  if (objs->genl_conn == NULL)
  {
    act_log_error(act_log_msg("MySQL connection not available."));
    return FALSE;
  }
  double ra_radius = radius_deg / cos(convert_DEG_RAD(dec_d));
  gchar qrystr[256];
  gchar quad_shift = 0;
  if (dec_d + radius_deg >= 90.0)
  {
    sprintf(qrystr, "SELECT RAmdeg, DECmdeg, pmRA, pmDEC FROM tycho2 WHERE DECmdeg>%lf AND DECmdeg<90.0", ra_d-radius_deg);
    quad_shift = 0;
  }
  else if (dec_d - radius_deg <= -90.0)
  {
    sprintf(qrystr, "SELECT RAmdeg, DECmdeg, pmRA, pmDEC FROM tycho2 WHERE DECmdeg>%lf AND DECmdeg>-90.0", ra_d+radius_deg);
    quad_shift = 0;
  }
  else if (ra_d - ra_radius < 0.0)
  {
    sprintf(qrystr, "SELECT RAmdeg, DECmdeg, pmRA, pmDEC FROM tycho2 WHERE DECmdeg>%lf AND DECmdeg<%lf AND (RAmdeg<%lf OR RAmdeg>%lf)", dec_d-radius_deg, dec_d+radius_deg, ra_d+ra_radius, ra_d-ra_radius+360.0);
    quad_shift = -1;
  }
  else if (ra_d + ra_radius >= 360.0)
  {
    sprintf(qrystr, "SELECT RAmdeg, DECmdeg, pmRA, pmDEC FROM tycho2 WHERE DECmdeg>%lf AND DECmdeg<%lf AND (RAmdeg>%lf OR RAmdeg<%lf)", dec_d-radius_deg, dec_d+radius_deg, ra_d-ra_radius, ra_d+ra_radius-360.0);
    quad_shift = 1;
  }
  else
  {
    sprintf(qrystr, "SELECT RAmdeg, DECmdeg, pmRA, pmDEC FROM tycho2 WHERE DECmdeg>%lf AND DECmdeg<%lf AND RAmdeg>%lf AND RAmdeg<%lf", dec_d-radius_deg, dec_d+radius_deg, ra_d-ra_radius, ra_d+ra_radius);
    quad_shift = 0;
  }
  act_log_debug(act_log_msg("SQL query: %s\n", qrystr));
  
  MYSQL_RES *result;
  MYSQL_ROW row;
  mysql_query(objs->genl_conn,qrystr);
  result = mysql_store_result(objs->genl_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve star catalog entries - %s.", mysql_error(objs->genl_conn)));
    return NULL;
  }
  int rowcount = mysql_num_rows(result);
  if ((rowcount <= 0) || (mysql_num_fields(result) != 4))
  {
    act_log_error(act_log_msg("Could not retrieve star catalog entries - Invalid number of rows/columns returned (%d rows, %d columns).", rowcount, mysql_num_fields(result)));
    mysql_free_result(result);
    return NULL;
  }
  act_log_debug(act_log_msg("%d objects to retrieve.", rowcount));
  
  PointList *list = point_list_new_with_length(rowcount);
  if (list == NULL)
  {
    act_log_error(act_log_msg("Failed to create point list for star catalog entries."));
    mysql_free_result(result);
    return NULL;
  }
  
  double point_ra, point_dec;
  double point_pmra, point_pmdec;
  struct rastruct orig_ra, prec_ra;
  struct decstruct orig_dec, prec_dec;
  while ((row = mysql_fetch_row(result)) != NULL)
  {
    int ret = 0;
    ret += sscanf(row[0], "%lf", &point_ra);
    ret += sscanf(row[1], "%lf", &point_dec);
    ret += sscanf(row[2], "%lf", &point_pmra);
    ret += sscanf(row[3], "%lf", &point_pmdec);
    if (ret != 4)
    {
      act_log_error(act_log_msg("Failed to extract all parameters for point from database."));
      continue;
    }
    point_ra += point_pmra * (epoch-2000.0) / 1000.0 / 3600.0;
    point_dec += point_pmdec * (epoch-2000.0) / 1000.0 / 3600.0;
    convert_H_HMSMS_ra(convert_DEG_H(point_ra), &orig_ra);
    convert_D_DMS_dec(point_dec, &orig_dec);
    precess_coord(&orig_ra, &orig_dec, 2000.0, epoch, &prec_ra, &prec_dec);
    point_ra = convert_H_DEG(convert_HMSMS_H_ra(&prec_ra));
    point_dec = convert_DMS_D_dec(&prec_dec);
    if ((quad_shift < 0) && (point_ra > 360.0))
      point_ra -= 360.0;
    if ((quad_shift > 0) && (point_ra < 360.0))
      point_ra += 360.0;
    point_list_append(list, (point_ra-ra_d)*3600.0, (point_dec-dec_d)*3600.0);
  }
  act_log_debug(act_log_msg("Stars retrieved from database: %u", point_list_get_num_used(list)));
  if (point_list_get_num_used(list) != (guint)rowcount)
    act_log_normal(act_log_msg("Not all catalog stars extracted from database (%u should be %d).", point_list_get_num_used(list), rowcount));
  return list;
}

void acq_store_append_image(AcqStore *objs, CcdImg *new_img)
{
  int ret = pthread_mutex_lock(&objs->img_list_mutex);
  if (ret != 0)
  {
    act_log_error(act_log_msg("Failed to obtain mutex lock on pending images list - %d. New image will be lost", ret));
    return;
  }
  objs->img_pend = g_slist_append(objs->img_pend, G_OBJECT(new_img));
  g_object_ref(G_OBJECT(new_img));
  ret = pthread_mutex_unlock(&objs->img_list_mutex);
  if (ret != 0)
    act_log_error(act_log_msg("Failed to release mutex lock on pending images list - %d", ret));
  if ((objs->status & STAT_STORING) != 0)
    return;
  ret = pthread_create(&objs->store_thr, NULL, store_pending_img, (void *)objs);
  if (ret != 0)
    act_log_error(act_log_msg("Failed to create image store thread - %d", ret));
}

gboolean acq_store_idle(AcqStore *objs)
{
  return objs->status == 0;
}

gboolean acq_store_storing(AcqStore *objs)
{
  return (objs->status & STAT_STORING) > 0;
}

gboolean acq_store_error_retry(AcqStore *objs)
{
  return (objs->status & STAT_ERR_RETRY) > 0;
}

gboolean acq_store_error_no_recov(AcqStore *objs)
{
  return (objs->status & STAT_ERR_NO_RECOV) > 0;
}

static void acq_store_instance_init(GObject *acq_store)
{
  AcqStore *objs = ACQ_STORE(acq_store);
  objs->status = 0;
  objs->sqlhost = NULL;
  objs->store_conn = NULL;
  objs->genl_conn = NULL;
  objs->img_pend = NULL;
}

static void acq_store_class_init(AcqStoreClass *klass)
{
  acq_store_signals[STATUS_UPDATE] = g_signal_new("store-status-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  G_OBJECT_CLASS(klass)->dispose = acq_store_instance_dispose;
}

static void acq_store_instance_dispose(GObject *acq_store)
{
  AcqStore *objs = ACQ_STORE(acq_store);
  if ((objs->status & STAT_STORING) > 0)
  {
    gint ret = pthread_join(objs->store_thr, NULL);
    if (ret != 0)
    {
      act_log_error(act_log_msg("Failed to join storage thread - %s", strerror(ret)));
      objs->status &= ~STAT_STORING;
    }
  }
  if (objs->sqlhost != NULL)
  {
    g_free(objs->sqlhost);
    objs->sqlhost = NULL;
  }
  if (objs->store_conn != NULL)
  {
    if ((objs->status & STAT_STORING) > 0)
      act_log_error(act_log_msg("Need to close MySQL store connection, but images are still being store. Not closing connection."));
    else
    {
      mysql_close(objs->store_conn);
      objs->store_conn = NULL;
    }
  }
  if (objs->genl_conn != NULL)
  {
    mysql_close(objs->genl_conn);
    objs->genl_conn = NULL;
  }
  if (objs->img_pend != NULL)
    act_log_error(act_log_msg("There are images waiting to be stored."));
  /// TODO: Destroy  mutex?
}

static void *store_pending_img(void *acq_store)
{
  AcqStore *objs = ACQ_STORE(acq_store);
  objs->status |= STAT_STORING;
  g_object_ref(G_OBJECT(objs));
  store_next_img(objs);
  g_object_unref(G_OBJECT(objs));
  objs->status &= ~STAT_STORING;
  return 0;
}

static void store_next_img(AcqStore *objs)
{
  if (objs->img_pend == NULL)
  {
    act_log_debug(act_log_msg("Stored all images."));
    return;
  }
  int ret = pthread_mutex_lock(&objs->img_list_mutex);
  if (ret != 0)
  {
    /// TODO: Error message
    act_log_error(act_log_msg("Error returned while trying to obtain mutex lock on pending images list - %d", ret));
  }
  GSList *cur_el = objs->img_pend;
  objs->img_pend = objs->img_pend->next;
  CcdImg *cur_img = CCD_IMG(objs->img_pend->data);
  g_slist_free_1 (cur_el);
  ret = pthread_mutex_unlock(&objs->img_list_mutex);
  if (ret != 0)
  {
    /// TODO: Error message
    act_log_error(act_log_msg("Error returned while trying to release mutex lock on pending images list - %d", ret));
  }
  
  if ((objs->status & STAT_ERR_NO_RECOV) > 0)
  {
    act_log_error(act_log_msg("Need to save an image, but data store is currently unavailable."));
    store_img_fallback(cur_img);
    g_object_unref(cur_img);
    return;
  }
  gboolean img_saved;
  guchar i;
  for (i=0; i<STORE_MAX_RETRIES; i++)
  {
    img_saved = store_img(objs->store_conn, cur_img);
    if (img_saved)
      break;
    act_log_debug(act_log_msg("Failed to save image to database, retrying (try %hhu/%hhu).", i+1, STORE_MAX_RETRIES));
    if ((objs->status & STAT_ERR_RETRY) == 0)
    {
      objs->status |= STAT_ERR_RETRY;
      g_signal_emit(G_OBJECT(objs), acq_store_signals[STATUS_UPDATE], 0);
    }
  }
  if (!img_saved)
  {
    act_log_debug(act_log_msg("Attempting to reconnect to MYSQL server."));
    img_saved = store_reconnect(objs);
    if (img_saved)
      img_saved = store_img(objs->store_conn, cur_img);
    if (!img_saved)
    {
      act_log_crit(act_log_msg("Failed to reconnect to MySQL server and save an image. Please consult IT technician."));
      objs->status &= ~STAT_ERR_RETRY;
      objs->status |= STAT_ERR_NO_RECOV;
      store_img_fallback(cur_img);
      g_object_unref(cur_img);
      return;
    }
  }
  if ((objs->status & (STAT_ERR_NO_RECOV | STAT_ERR_RETRY)) > 0)
  {
    objs->status &= ~(STAT_ERR_NO_RECOV | STAT_ERR_RETRY);
    g_signal_emit(G_OBJECT(objs), acq_store_signals[STATUS_UPDATE], 0);
  }
  g_object_unref(cur_img);
}

static gboolean store_img(MYSQL *conn, CcdImg *img)
{
  struct rastruct tel_ra;
  struct decstruct tel_dec;
  ccd_img_get_tel_pos(img, &tel_ra, &tel_dec);
  struct timestruct start_time;
  struct datestruct start_date;
  ccd_img_get_start_datetime(img, &start_date, &start_time);
  char qrystr[256];
  sprintf(qrystr, "INSERT INTO ccd_img \
  (targ_id, user_id, type, exp_t_s, start_date, start_time_h, win_start_x, win_start_y, win_width, win_height, prebin_x, prebin_y, tel_ra_h, tel_dec_d) VALUES \
  (%lu, %lu, %hhu, %f, \"%hu-%hhu-%hhu\", %f, %hu, %hu, %hu, %hu, %hu, %hu, %f, %f);", 
          ccd_img_get_targ_id(img),
          ccd_img_get_user_id(img),
          img_type_acq_to_db(ccd_img_get_img_type(img)),
          ccd_img_get_exp_t(img),
          start_date.year, start_date.month+1, start_date.day+1,
          convert_HMSMS_H_time(&start_time),
          ccd_img_get_win_start_x(img),
          ccd_img_get_win_start_y(img),
          ccd_img_get_win_width(img),
          ccd_img_get_win_height(img),
          ccd_img_get_prebin_x(img),
          ccd_img_get_prebin_y(img),
          convert_HMSMS_H_ra(&tel_ra),
          convert_DMS_D_dec(&tel_dec));
  if (mysql_query(conn, qrystr))
  {
    act_log_error(act_log_msg("Failed to save CCD image header to database - %s.", mysql_error(conn)));
    act_log_debug(act_log_msg("SQL query: %s", qrystr));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  act_log_debug(act_log_msg("Inserted image header."));
  
  gulong img_id = mysql_insert_id(conn);
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
  sprintf(qrystr, "INSERT INTO ccd_img_data (ccd_img_id, x, y, value) VALUES (%lu, ?, ?, ?);", img_id);
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
  gdouble cur_val;
  MYSQL_BIND  bind[3];
  memset(bind, 0, sizeof(bind));
  /// TODO: Checked signed ints and data types in genl
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
  bind[2].buffer_type = MYSQL_TYPE_DOUBLE;
  bind[2].buffer = (char *)&cur_val;
  bind[2].is_null = 0;
  bind[2].length = 0;
  if (mysql_stmt_bind_param(stmt, bind))
  {
    act_log_error(act_log_msg("Failed to bind parameters for prepared statement - %s", mysql_stmt_error(stmt)));
    mysql_query(conn, "ROLLBACK;");
    return FALSE;
  }
  
  gfloat const *img_data = ccd_img_get_img_data(img);
  gulong i, img_len = ccd_img_get_img_len(img);
  gushort img_width = ccd_img_get_win_width(img)/ccd_img_get_prebin_x(img), img_height = ccd_img_get_win_height(img)/ccd_img_get_prebin_y(img);
  gboolean ret = TRUE;
  if ((gulong)(img_width*img_height) != img_len)
  {
    act_log_debug(act_log_msg("Image width and height parameters contradicts image length (width %hu, height %hu, length %lu). Choosing smallest.", img_width, img_height, img_len));
    img_len = (gulong)(img_width*img_height) < img_len ? (gulong)(img_width*img_height) : img_len;
  }
  for (i=0; i<img_len; i++)
  {
    cur_x = i%img_width;
    cur_y = i/img_width;
    cur_val = img_data[i];
    if (mysql_stmt_execute(stmt))
    {
      act_log_error(act_log_msg("Failed to execute prepared statement to insert pixel datum into database - %s", mysql_stmt_error(stmt)));
      ret = FALSE;
      break;
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

static void store_img_fallback(CcdImg *img)
{
  static char *fallback_path = NULL;
  if (fallback_path == NULL)
  {
    const char* homedir;
    homedir = getenv("HOME");
    if (homedir == NULL)
    {
      struct passwd *pw = getpwuid(getuid());
      homedir = pw->pw_dir;
      if (homedir == NULL)
      {
        act_log_error(act_log_msg("Need to save an image to the fallback directory, but cannot determine software home directory. Image will be lost."));
        return;
      }
    }
    int len = strlen(homedir);
    fallback_path = malloc(len+20);
    if (homedir[len-1] == '/')
      sprintf(fallback_path, "%sacq_img_fallback/", homedir);
    else
      sprintf(fallback_path, "%s/acq_img_fallback/", homedir);
    DIR* dir = opendir(fallback_path);
    if (dir != NULL)
    {
      /* Directory exists. */
      closedir(dir);
    }
    else if (errno == ENOENT)
    {
      /* Directory does not exist. */
      mkdir(fallback_path, 0755);
    }
    else
    {
      /* opendir() failed for some other reason. */
      act_log_error(act_log_msg("Need to save an image to the fallback directory (%s), but an error occurred while trying to open the directory - %s", fallback_path, strerror(errno)));
      return;
    }
  }
  
  struct datestruct start_unid;
  struct timestruct start_unit;
  ccd_img_get_start_datetime(img, &start_unid, &start_unit);
  char filepath[strlen(fallback_path)+50];
  sprintf(filepath, "%s%s%s.dat", fallback_path, date_to_str(&start_unid), time_to_str(&start_unit));
  FILE *fp = fopen(filepath, "ab");
  if (fp == NULL)
  {
    act_log_error(act_log_msg("Failed to open file %s for fallback image storage. Image will be lost. Error - %s", filepath, strerror(errno)));
    return;
  }
  fwrite(img+sizeof(GObject), 1, sizeof(CcdImg)-sizeof(GObject), fp);
  fclose(fp);
}

static gboolean store_reconnect(AcqStore *objs)
{
  if (objs->sqlhost == NULL)
  {
    act_log_error(act_log_msg("Hostname of database server not available."));
    return FALSE;
  }
  act_log_debug(act_log_msg("Recreating MySQL image storage connection."));
  MYSQL *store_conn;
  store_conn = mysql_init(NULL);
  if (store_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising MySQL image storage connection handler - %s.", mysql_error(store_conn)));
    return FALSE;
  }
  if (mysql_real_connect(store_conn, objs->sqlhost, "act_acq", NULL, "actnew", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error establishing image storage connecting to MySQL database - %s.", mysql_error(store_conn)));
    mysql_close(store_conn);
    return FALSE;
  }
  if (objs->store_conn != NULL)
    mysql_close(objs->store_conn);
  objs->store_conn = store_conn;
  
  return TRUE;
}

static guchar img_type_acq_to_db(guchar acq_img_type)
{
  guchar ret;
  switch (acq_img_type)
  {
    case IMGT_ACQ_OBJ:
      ret = DB_TYPE_ACQ_OBJ;
      break;
    case IMGT_ACQ_SKY:
      ret = DB_TYPE_ACQ_SKY;
      break;
    case IMGT_OBJECT:
      ret = DB_TYPE_OBJECT;
      break;
    case IMGT_BIAS:
      ret = DB_TYPE_BIAS;
      break;
    case IMGT_DARK:
      ret = DB_TYPE_DARK;
      break;
    case IMGT_FLAT:
      ret = DB_TYPE_FLAT;
      break;
    default:
      ret = DB_TYPE_ANY;
  }
  return ret;
}
