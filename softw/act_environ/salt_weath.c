#include <math.h>
#include <string.h>
#include <act_log.h>
#include <act_ipc.h>
#include "salt_weath.h"

#define FETCH_TIMEOUT_S     60
#define INVAL_TIMEOUT_D     0.013888888888888888
#define TIME_TIMEOUT_S      60
#define SALT_WEATH_URL      "http://www.salt.ac.za/~saltmet/weather.txt"

static void class_init(SaltWeathClass *klass);
static void instance_init(GObject *salt_weath);
static void instance_dispose(GObject *salt_weath);
static gboolean time_timeout(gpointer salt_weath);
static gboolean fetch_salt(gpointer salt_weath);
void process_resp(SoupSession *soup_session, SoupMessage *msg, gpointer salt_weath);

enum
{
  SALT_WEATH_UPDATE,
  LAST_SIGNAL
};

static guint salt_weath_signals[LAST_SIGNAL] = { 0 };

GType salt_weath_get_type (void)
{
  static GType salt_weath_type = 0;
  
  if (!salt_weath_type)
  {
    const GTypeInfo salt_weath_info =
    {
      sizeof (SaltWeathClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (SaltWeath),
      0,
      (GInstanceInitFunc) instance_init,
      NULL
    };
    
    salt_weath_type = g_type_register_static (G_TYPE_OBJECT, "SaltWeath", &salt_weath_info, 0);
  }
  
  return salt_weath_type;
}

SaltWeath *salt_weath_new (void)
{
  SaltWeath *objs = SALT_WEATH(g_object_new (salt_weath_get_type(), NULL));
  objs->fetch_session = soup_session_async_new();
  fetch_salt((void *)objs);
  objs->fetch_to_id = g_timeout_add_seconds(FETCH_TIMEOUT_S, fetch_salt, objs);
  objs->time_to_id = g_timeout_add_seconds(TIME_TIMEOUT_S, time_timeout, objs);
  return objs;
}

void salt_weath_set_time(SaltWeath * objs, double jd)
{
  objs->cur_jd = jd;
  if (objs->time_to_id != 0)
  {
    g_source_remove(objs->time_to_id);
    objs->time_to_id = 0;
  }
  objs->time_to_id = g_timeout_add_seconds(TIME_TIMEOUT_S, time_timeout, objs);
}

gboolean salt_weath_get_env_data (SaltWeath * objs, struct act_msg_environ *env_data)
{
  if (fabs(objs->weath_jd - objs->cur_jd) > INVAL_TIMEOUT_D)
  {
    act_log_normal(act_log_msg("No recent weather data available."));
    return FALSE;
  }
  
  memset(env_data, 0, sizeof(struct act_msg_environ));
  env_data->humidity = objs->rel_hum;
  env_data->wind_vel = objs->wind_speed_10;
  convert_D_DMS_azm(objs->wind_dir_10, &env_data->wind_azm);
  env_data->rain = objs->rain;
  return TRUE;
}

static void class_init(SaltWeathClass *klass)
{
  salt_weath_signals[SALT_WEATH_UPDATE] = g_signal_new("salt-weath-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
  G_OBJECT_CLASS(klass)->dispose = instance_dispose;
}

static void instance_init(GObject *salt_weath)
{
  SaltWeath *objs = SALT_WEATH(salt_weath);
  
  objs->cur_jd = 1.0;
  objs->fetch_to_id = 0;
  objs->fetch_session = NULL;
  objs->fetch_msg = NULL;
  
  objs->weath_jd = 0.0;
  objs->air_press = 0.0;
  objs->dew_point_T = 0.0;
  objs->rel_hum = 0.0;
  objs->wind_speed_30 = 0.0;
  objs->wind_dir_30 = 0.0;
  objs->wind_speed_10 = 0.0;
  objs->wind_dir_10 = 0.0;
  objs->temp_2 = 0;
  objs->temp_5 = 0;
  objs->temp_10 = 0;
  objs->temp_15 = 0;
  objs->temp_20 = 0;
  objs->temp_25 = 0;
  objs->temp_30 = 0;
  objs->rain = FALSE;
}

static void instance_dispose(GObject *salt_weath)
{
  SaltWeath *objs = SALT_WEATH(salt_weath);
  if (objs->fetch_to_id > 0)
  {
    g_source_remove(objs->fetch_to_id);
    objs->fetch_to_id = 0;
  }
  if (objs->time_to_id != 0)
  {
    g_source_remove(objs->time_to_id);
    objs->time_to_id = 0;
  }
  if ((objs->fetch_session != NULL) && (objs->fetch_msg != NULL))
  {
    soup_session_cancel_message (objs->fetch_session, objs->fetch_msg, SOUP_STATUS_CANCELLED);
    objs->fetch_msg = NULL;
  }
  G_OBJECT_CLASS(salt_weath)->dispose(salt_weath);
}

static gboolean time_timeout(gpointer salt_weath)
{
  SaltWeath *objs = SALT_WEATH(salt_weath);
  objs->cur_jd = 1.0;
  objs->time_to_id = 0;
  return FALSE;
}

static gboolean fetch_salt(gpointer salt_weath)
{
  SaltWeath *objs = SALT_WEATH(salt_weath);
  objs->fetch_msg = soup_message_new ("GET", SALT_WEATH_URL);
  soup_session_queue_message (objs->fetch_session, objs->fetch_msg, process_resp, salt_weath);
  return TRUE;
}

void process_resp(SoupSession *soup_session, SoupMessage *msg, gpointer salt_weath)
{
  (void)soup_session;
  SaltWeath *objs = SALT_WEATH(salt_weath);
  objs->fetch_msg = NULL;
  act_log_debug(act_log_msg("Processing SALT weather data."));
  if (msg->status_code != SOUP_STATUS_OK)
  {
    act_log_error(act_log_msg("An error occurred while retrieving SALT weather data. Reason: %s", msg->reason_phrase));
    g_signal_emit(G_OBJECT(salt_weath), salt_weath_signals[SALT_WEATH_UPDATE], 0, FALSE);
    return;
  }
  
  int len = msg->response_body->length;
  char found_valid = 0;
  int curchar = 0;
  char tmp_date[20] = "", tmp_time[20] = "", tmp_rain = -1;
  double tmp_floatvals[14] = {-1e6}, last_floatvals[14], last_jd;
  struct datestruct locd, unid;
  struct timestruct loct, unit;
  char msg_copy[len+1];
  snprintf(msg_copy, len, "%s", msg->response_body->data);
  char *start_char, *end_char;
  while (curchar < len)
  {
    end_char = strrchr(msg_copy, '\n');
    if ((end_char == NULL) || (end_char == msg_copy))
    {
      act_log_error(act_log_msg("Reached the start of the message."));
      break;
    }
    *end_char = '\0';
    end_char--;
    start_char = strrchr(msg_copy, '\n');
    if (start_char == NULL)
      start_char = msg_copy;
    else
      start_char++;
    if (end_char <= start_char)
      continue;
    int ret = sscanf(start_char, "%s %s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %hhd", tmp_date, tmp_time, &tmp_floatvals[0], &tmp_floatvals[1], &tmp_floatvals[2], &tmp_floatvals[3], &tmp_floatvals[4], &tmp_floatvals[5], &tmp_floatvals[6], &tmp_floatvals[7], &tmp_floatvals[8], &tmp_floatvals[9], &tmp_floatvals[10], &tmp_floatvals[11], &tmp_floatvals[12], &tmp_floatvals[13], &tmp_rain);
    if (ret != 17)
    {
      act_log_normal(act_log_msg("Invalid number of columns: %d", ret));
      continue;
    }
    
    if (sscanf(tmp_date, "%hd-%hhu-%hhu", &locd.year, &locd.month, &locd.day) != 3)
    {
      act_log_normal(act_log_msg("Could not read date"));
      continue;
    }
    locd.month--;
    locd.day--;
    if (sscanf(tmp_time, "%hhu:%hhu:%hhu", &loct.hours, &loct.minutes, &loct.seconds) != 3)
    {
      act_log_normal(act_log_msg("Could not read time"));
      continue;
    }
    loct.milliseconds = 0;
    
    // This is a valid line
    found_valid = 1;
    calc_UniT (&loct, &unit);
    memcpy(&unid, &locd, sizeof(struct datestruct));
    check_systime_discrep(&unid, &loct, &unit);
    objs->weath_jd = calc_GJD(&unid, &unit);
    objs->air_press = tmp_floatvals[0];
    objs->dew_point_T = tmp_floatvals[1];
    objs->rel_hum = tmp_floatvals[2];
    objs->wind_speed_30 = tmp_floatvals[3] * 3.6;
    objs->wind_dir_30 = tmp_floatvals[4];
    objs->wind_speed_10 = tmp_floatvals[5] * 3.6;
    objs->wind_dir_10 = tmp_floatvals[6];
    objs->temp_2 = tmp_floatvals[7];
    objs->temp_5 = tmp_floatvals[8];
    objs->temp_10 = tmp_floatvals[9];
    objs->temp_15 = tmp_floatvals[10];
    objs->temp_20 = tmp_floatvals[11];
    objs->temp_25 = tmp_floatvals[12];
    objs->temp_30 = tmp_floatvals[13];
    objs->rain = (tmp_rain != 0);
    break;
  }
  
  if (!found_valid)
  {
    act_log_normal(act_log_msg("No valid line found."));
    g_signal_emit(G_OBJECT(salt_weath), salt_weath_signals[SALT_WEATH_UPDATE], 0, FALSE);
    return;
  }
  g_signal_emit(G_OBJECT(salt_weath), salt_weath_signals[SALT_WEATH_UPDATE], 0, TRUE);
}
