#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <act_ipc.h>
#include <act_log.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include "env_weather.h"
#include "salt_weath.h"
#include "swasp_weath.h"

/// Maximum permissible difference between current time and time stamp on weather data
#define UPDATE_TIMEOUT_S       60

/// Maximum permissible wind speed (km/h)
#define WIND_SPEED_WARN_KMPH   50.0
#define WIND_SPEED_LIMIT_KMPH  60.0
/// Maximum permissible relative humidity (percentage)
#define REL_HUM_WARN_PERC      85.0
#define REL_HUM_LIMIT_PERC     90.0
/// Maximum permissible altitude of the Sun (degrees)
#define SUN_ALT_WARN_DEG       -10.0
#define SUN_ALT_LIMIT_DEG      -5.0
/// Maximum permissible cloud levels
#define CLOUD_COVER_WARN       40.0
#define CLOUD_COVER_LIMIT      30.0
/// Maximum permissible proximity to Moon
#define MOON_PROX_LIMIT_DEG     5.0
#define MOON_PROX_WARN_DEG     10.0

#define TABLE_PADDING          3


static void class_init(EnvWeatherClass *klass);
static void instance_init(GtkWidget *env_weather);
static void instance_dispose(GObject *env_weather);
static gboolean update_weather(gpointer env_weather);
static void update_indicators(EnvWeather *objs);
static void update_salt_weath(gpointer env_weather, gboolean salt_ok);
static void update_swasp_weath(gpointer env_weather, gboolean swasp_ok);
static void process_msg_time(EnvWeather *objs, struct act_msg_time *msg_time);
static void process_msg_coord(EnvWeather *objs, struct act_msg_coord *msg_coord);
static void process_msg_environ(EnvWeather *objs, struct act_msg_environ *msg_environ);
static void process_msg_targset(EnvWeather *objs, struct act_msg_targset *msg_targset);
static void process_msg_datapmt(EnvWeather *objs, struct act_msg_datapmt *msg_datapmt);
static void process_msg_dataccd(EnvWeather *objs, struct act_msg_dataccd *msg_dataccd);
static unsigned char process_obsn(EnvWeather *objs, struct rastruct *targ_ra, struct decstruct *targ_dec);
static double calc_moon_dist(struct rastruct *moon_ra, struct decstruct *moon_dec, struct rastruct *targ_ra, struct decstruct *targ_dec);


enum
{
  ENV_WEATHER_UPDATE,
  LAST_SIGNAL
};

static guint env_weather_signals[LAST_SIGNAL] = { 0 };

GType env_weather_get_type (void)
{
  static GType env_weather_type = 0;
  
  if (!env_weather_type)
  {
    const GTypeInfo env_weather_info =
    {
      sizeof (EnvWeatherClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) class_init, /* class init */
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (EnvWeather),
      0,
      (GInstanceInitFunc) instance_init,
      NULL
    };
    
    env_weather_type = g_type_register_static (GTK_TYPE_FRAME, "EnvWeather", &env_weather_info, 0);
  }
  
  return env_weather_type;
}

GtkWidget *env_weather_new (void)
{
  act_log_debug(act_log_msg("Creating env_weather object."));
//   EnvWeather *objs = ENV_WEATHER(g_object_new(env_weather_get_type(), NULL));
//   gtk_container_add(GTK_CONTAINER(objs), gtk_label_new("TEST"));
//   return GTK_WIDGET(objs);
  return GTK_WIDGET(g_object_new(env_weather_get_type(), NULL));
}

void env_weather_process_msg (GtkWidget *env_weather, struct act_msg *msg)
{
  act_log_debug(act_log_msg("Processing message (type %d)", msg->mtype));
  EnvWeather *objs = ENV_WEATHER(env_weather);
  switch(msg->mtype)
  {
    case MT_TIME:
      process_msg_time(objs, &msg->content.msg_time);
      break;
    case MT_COORD:
      process_msg_coord(objs, &msg->content.msg_coord);
      break;
    case MT_ENVIRON:
      process_msg_environ(objs, &msg->content.msg_environ);
      break;
    case MT_TARG_SET:
      process_msg_targset(objs, &msg->content.msg_targset);
      break;
    case MT_DATA_PMT:
      process_msg_datapmt(objs, &msg->content.msg_datapmt);
      break;
    case MT_DATA_CCD:
      process_msg_dataccd(objs, &msg->content.msg_dataccd);
      break;
  }
}

static void class_init(EnvWeatherClass *klass)
{
  env_weather_signals[ENV_WEATHER_UPDATE] = g_signal_new("env-weather-update", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  G_OBJECT_CLASS(klass)->dispose = instance_dispose;
}

static void instance_init(GtkWidget *env_weather)
{
  EnvWeather *objs = ENV_WEATHER(env_weather);
  objs->all_env.status_active = ACTIVE_TIME_DAY;
  objs->all_env.weath_ok = FALSE;
  objs->all_env.humidity = 100.0;
  objs->all_env.clouds = 100.0;
  objs->all_env.rain = TRUE;
  objs->all_env.wind_vel = WIND_SPEED_LIMIT_KMPH+1;
  convert_D_DMS_azm(0.0, &objs->all_env.wind_azm);
  objs->all_env.psf_asec = 0;
  convert_D_DMS_alt(SUN_ALT_LIMIT_DEG+1, &objs->all_env.sun_alt);
  convert_H_HMSMS_ra(0.0, &objs->all_env.moon_ra);
  convert_D_DMS_dec(0.0, &objs->all_env.moon_dec);
  
  objs->box_main = gtk_table_new(8, 1, FALSE);
  gtk_container_add(GTK_CONTAINER(objs), objs->box_main);
  
  GdkColor newcol;
  gdk_color_parse("#0000AA", &newcol);
  GtkWidget *frm_source_stat = gtk_frame_new("Source");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_source_stat, 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *box_source_stat = gtk_hbox_new(TRUE, 0);
  gtk_container_add(GTK_CONTAINER(frm_source_stat),box_source_stat);
  objs->evb_swasp_stat = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_swasp_stat);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_swasp_stat), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(objs->evb_swasp_stat),gtk_label_new("SuperWASP"));
  gtk_box_pack_start(GTK_BOX(box_source_stat),objs->evb_swasp_stat,TRUE,TRUE,0);
  objs->evb_salt_stat = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_salt_stat);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_salt_stat), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(objs->evb_salt_stat),gtk_label_new("SALT"));
  gtk_box_pack_start(GTK_BOX(box_source_stat),objs->evb_salt_stat,TRUE,TRUE,0);
  
  GtkWidget *frm_humidity = gtk_frame_new("Humidity");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_humidity, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_humidity = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_humidity);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_humidity), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_humidity),objs->evb_humidity);
  objs->lbl_humidity = gtk_label_new("N/A");
  g_object_ref(objs->lbl_humidity);
  gtk_container_add(GTK_CONTAINER(objs->evb_humidity),objs->lbl_humidity);
  
  GtkWidget *frm_cloud = gtk_frame_new("Cloud");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_cloud, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_cloud = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_cloud);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_cloud), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_cloud),objs->evb_cloud);
  objs->lbl_cloud = gtk_label_new("N/A");
  g_object_ref(objs->lbl_cloud);
  gtk_container_add(GTK_CONTAINER(objs->evb_cloud),objs->lbl_cloud);
  
  GtkWidget *frm_rain = gtk_frame_new("Rain");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_rain, 3,4,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_rain = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_rain, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_rain);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_rain), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_rain),objs->evb_rain);
  objs->lbl_rain = gtk_label_new("N/A");
  g_object_ref(objs->lbl_rain);
  gtk_container_add(GTK_CONTAINER(objs->evb_rain),objs->lbl_rain);
  
  GtkWidget *frm_wind = gtk_frame_new("Wind");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_wind, 4,5,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_wind = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_wind);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_wind), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_wind),objs->evb_wind);
  objs->lbl_wind = gtk_label_new("N/A");
  g_object_ref(objs->lbl_wind);
  gtk_container_add(GTK_CONTAINER(objs->evb_wind),objs->lbl_wind);
  
  GtkWidget *frm_sunalt = gtk_frame_new("Sun Alt.");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_sunalt, 5,6,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_sunalt = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_sunalt, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_sunalt);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_sunalt), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_sunalt),objs->evb_sunalt);
  objs->lbl_sunalt = gtk_label_new("N/A");
  g_object_ref(objs->lbl_sunalt);
  gtk_container_add(GTK_CONTAINER(objs->evb_sunalt),objs->lbl_sunalt);
  
  GtkWidget *frm_moonpos = gtk_frame_new("Moon RA Dec");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_moonpos, 6,7,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_moonpos = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_moonpos, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_moonpos);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_moonpos), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_moonpos),objs->evb_moonpos);
  objs->lbl_moonpos = gtk_label_new("N/A");
  g_object_ref(objs->lbl_moonpos);
  gtk_container_add(GTK_CONTAINER(objs->evb_moonpos),objs->lbl_moonpos);
  
  GtkWidget *frm_activemode = gtk_frame_new("Active Mode");
  gtk_table_attach(GTK_TABLE(objs->box_main), frm_activemode, 7,8,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_active_mode = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_active_mode, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_active_mode);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_active_mode), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_activemode),objs->evb_active_mode);
  objs->lbl_active_mode = gtk_label_new("N/A");
  gtk_widget_modify_bg(objs->evb_active_mode, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->lbl_active_mode);
  gtk_container_add(GTK_CONTAINER(objs->evb_active_mode),objs->lbl_active_mode);
  
  objs->salt_ok = objs->swasp_ok = FALSE;
  objs->salt_weath = salt_weath_new();
  objs->swasp_weath = swasp_weath_new();
  g_signal_connect_swapped(G_OBJECT(objs->salt_weath), "salt-weath-update", G_CALLBACK(update_salt_weath), objs);
  g_signal_connect_swapped(G_OBJECT(objs->swasp_weath), "swasp-weath-update", G_CALLBACK(update_swasp_weath), objs);
  
  objs->weath_change_to_id = objs->active_change_to_id = 0;
  objs->update_to_id = g_timeout_add_seconds(UPDATE_TIMEOUT_S, update_weather, objs);
}

static void instance_dispose(GObject *env_weather)
{
  EnvWeather *objs = ENV_WEATHER(env_weather);
  
  if (objs->salt_weath != NULL)
  {
    g_object_unref(objs->salt_weath);
    objs->salt_weath = NULL;
  }
  objs->salt_ok = FALSE;
  if (objs->swasp_weath != NULL)
  {
    g_object_unref(objs->swasp_weath);
    objs->swasp_weath = NULL;
  }
  objs->swasp_ok = FALSE;
  
  if (objs->update_to_id != 0)
  {
    g_source_remove(objs->update_to_id);
    objs->update_to_id = 0;
  }
  if (objs->weath_change_to_id != 0)
  {
    g_source_remove(objs->weath_change_to_id);
    objs->weath_change_to_id = 0;
  }
  if (objs->active_change_to_id != 0)
  {
    g_source_remove(objs->active_change_to_id);
    objs->active_change_to_id = 0;
  }
  
  if (objs->evb_swasp_stat != NULL)
  {
    g_object_unref(objs->evb_swasp_stat);
    objs->evb_swasp_stat = NULL;
  }
  if (objs->evb_salt_stat != NULL)
  {
    g_object_unref(objs->evb_salt_stat);
    objs->evb_salt_stat = NULL;
  }
  if (objs->evb_humidity != NULL)
  {
    g_object_unref(objs->evb_humidity);
    objs->evb_humidity = NULL;
  }
  if (objs->lbl_humidity != NULL)
  {
    g_object_unref(objs->lbl_humidity);
    objs->lbl_humidity = NULL;
  }
  if (objs->evb_cloud != NULL)
  {
    g_object_unref(objs->evb_cloud);
    objs->evb_cloud = NULL;
  }
  if (objs->lbl_cloud != NULL)
  {
    g_object_unref(objs->lbl_cloud);
    objs->lbl_cloud = NULL;
  }
  if (objs->evb_rain != NULL)
  {
    g_object_unref(objs->evb_rain);
    objs->evb_rain = NULL;
  }
  if (objs->lbl_rain != NULL)
  {
    g_object_unref(objs->lbl_rain);
    objs->lbl_rain = NULL;
  }
  if (objs->evb_wind != NULL)
  {
    g_object_unref(objs->evb_wind);
    objs->evb_wind = NULL;
  }
  if (objs->lbl_wind != NULL)
  {
    g_object_unref(objs->lbl_wind);
    objs->lbl_wind = NULL;
  }
  if (objs->evb_sunalt != NULL)
  {
    g_object_unref(objs->evb_sunalt);
    objs->evb_sunalt = NULL;
  }
  if (objs->lbl_sunalt != NULL)
  {
    g_object_unref(objs->lbl_sunalt);
    objs->lbl_sunalt = NULL;
  }
  if (objs->evb_moonpos != NULL)
  {
    g_object_unref(objs->evb_moonpos);
    objs->evb_moonpos = NULL;
  }
  if (objs->lbl_moonpos != NULL)
  {
    g_object_unref(objs->lbl_moonpos);
    objs->lbl_moonpos = NULL;
  }
  if (objs->evb_active_mode != NULL)
  {
    g_object_unref(objs->evb_active_mode);
    objs->evb_active_mode = NULL;
  }
  if (objs->lbl_active_mode != NULL)
  {
    g_object_unref(objs->lbl_active_mode);
    objs->lbl_active_mode = NULL;
  }
  
  G_OBJECT_CLASS(env_weather)->dispose(env_weather);
}

static gboolean update_weather(gpointer env_weather)
{
  EnvWeather *objs = ENV_WEATHER(env_weather);
  struct act_msg_environ salt_env, swasp_env;
  gboolean salt_actual, swasp_actual;
  salt_actual = salt_weath_get_env_data(objs->salt_weath, &salt_env);
  swasp_actual = swasp_weath_get_env_data(objs->swasp_weath, &swasp_env);
  
  GdkColor colred;
  GdkColor colyellow;
  GdkColor colgreen;
  gdk_color_parse("#AA0000", &colred);
  gdk_color_parse("#AAAA00", &colyellow);
  gdk_color_parse("#00AA00", &colgreen);
  
  if ((!swasp_actual) && (!salt_actual))
  {
    act_log_error(act_log_msg("No valid weather data available."));
    gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &colred);
    gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &colred);
    objs->all_env.humidity = 100.0;
    objs->all_env.clouds = 100.0;
    objs->all_env.rain = TRUE;
    objs->all_env.wind_vel = WIND_SPEED_LIMIT_KMPH+1;    
  }
  else if (swasp_actual)
  {
    if (objs->swasp_ok)
      gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &colgreen);
    else
      gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &colyellow);
    gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &colred);
    objs->all_env.humidity = swasp_env.humidity;
    objs->all_env.clouds = swasp_env.clouds;
    objs->all_env.rain = swasp_env.rain;
    objs->all_env.wind_vel = swasp_env.wind_vel;
    memcpy(&objs->all_env.wind_azm, &swasp_env.wind_azm, sizeof(struct azmstruct));
  }
  else if (salt_actual)
  {
    if (objs->salt_ok)
      gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &colgreen);
    else
      gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &colyellow);
    gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &colred);
    objs->all_env.humidity = salt_env.humidity;
    objs->all_env.clouds = -1.0;
    objs->all_env.rain = salt_env.rain;
    objs->all_env.wind_vel = salt_env.wind_vel;
    memcpy(&objs->all_env.wind_azm, &salt_env.wind_azm, sizeof(struct azmstruct));
  }
  else
  {
    if (objs->salt_ok)
      gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &colgreen);
    else
      gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &colyellow);
    if (objs->swasp_ok)
      gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &colgreen);
    else
      gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &colyellow);
    
    if (salt_env.humidity > swasp_env.humidity)
      objs->all_env.humidity = salt_env.humidity;
    else
      objs->all_env.humidity = swasp_env.humidity;
    objs->all_env.clouds = swasp_env.clouds;
    objs->all_env.rain = salt_env.rain || swasp_env.rain;
    if (salt_env.wind_vel > swasp_env.wind_vel)
    {
      objs->all_env.wind_vel = salt_env.wind_vel;
      memcpy(&objs->all_env.wind_azm, &salt_env.wind_azm, sizeof(struct azmstruct));
    }
    else
    {
      objs->all_env.wind_vel = swasp_env.wind_vel;
      memcpy(&objs->all_env.wind_azm, &swasp_env.wind_azm, sizeof(struct azmstruct));
    }
  }
  
  objs->all_env.weath_ok = (
    (objs->all_env.wind_vel < WIND_SPEED_LIMIT_KMPH) &&
    (objs->all_env.humidity < REL_HUM_LIMIT_PERC) &&
    (!objs->all_env.rain)
  );
  
  update_indicators(objs);
  
  g_signal_emit(G_OBJECT(env_weather), env_weather_signals[ENV_WEATHER_UPDATE], 0);
  return TRUE;
}

static void update_indicators(EnvWeather *objs)
{
  GdkColor colred;
  GdkColor colyellow;
  GdkColor colgreen;
  GdkColor colblue;
  gdk_color_parse("#AA0000", &colred);
  gdk_color_parse("#AAAA00", &colyellow);
  gdk_color_parse("#00AA00", &colgreen);
  gdk_color_parse("#0000AA", &colblue);
  char tmpstr[50];
  
  if (objs->all_env.humidity >= REL_HUM_LIMIT_PERC)
    gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.humidity >= REL_HUM_WARN_PERC)
    gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &colgreen);
  snprintf(tmpstr, sizeof(tmpstr), "%5.1f%%", objs->all_env.humidity);
  gtk_label_set_text(GTK_LABEL(objs->lbl_humidity), tmpstr);
  
  if (objs->all_env.clouds <= CLOUD_COVER_LIMIT)
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.clouds <= CLOUD_COVER_WARN)
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colgreen);
  snprintf(tmpstr, sizeof(tmpstr), "%5.1f", objs->all_env.clouds);
  gtk_label_set_text(GTK_LABEL(objs->lbl_cloud), tmpstr);
  gtk_widget_show(objs->lbl_cloud);
  
  if (objs->all_env.rain)
  {
    gtk_widget_modify_bg(objs->evb_rain, GTK_STATE_NORMAL, &colred);
    gtk_label_set_text(GTK_LABEL(objs->lbl_rain), "YES");
  }
  else
  {
    gtk_widget_modify_bg(objs->evb_rain, GTK_STATE_NORMAL, &colgreen);
    gtk_label_set_text(GTK_LABEL(objs->lbl_rain), "NO");
  }
  
  if (objs->all_env.wind_vel >= WIND_SPEED_LIMIT_KMPH)
    gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.wind_vel >= WIND_SPEED_WARN_KMPH)
    gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &colgreen);
  char *wind_azm_str = azm_to_str(&objs->all_env.wind_azm);
  snprintf(tmpstr, sizeof(tmpstr), "%5.1f km/h, %s", objs->all_env.wind_vel, wind_azm_str);
  free(wind_azm_str);
  gtk_label_set_text(GTK_LABEL(objs->lbl_wind), tmpstr);
}

static void update_salt_weath(gpointer env_weather, gboolean salt_ok)
{
  EnvWeather *objs = ENV_WEATHER(env_weather);
  objs->salt_ok = salt_ok;
  GdkColor new_col;
  if (salt_ok)
    gdk_color_parse("#00AA00", &new_col);
  else
    gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &new_col);
}

static void update_swasp_weath(gpointer env_weather, gboolean swasp_ok)
{
  EnvWeather *objs = ENV_WEATHER(env_weather);
  objs->swasp_ok = swasp_ok;
  GdkColor new_col;
  if (swasp_ok)
    gdk_color_parse("#00AA00", &new_col);
  else
    gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &new_col);
}

static void process_msg_time(EnvWeather *objs, struct act_msg_time *msg_time)
{
  act_log_debug(act_log_msg("Processing time."));
  swasp_weath_set_time(objs->swasp_weath, msg_time->gjd);
  salt_weath_set_time(objs->salt_weath, msg_time->gjd);
  
  struct rastruct tmp_sun_ra;
  struct decstruct tmp_sun_dec;
  calc_sun (msg_time->gjd, NULL, NULL, &tmp_sun_ra, &tmp_sun_dec, NULL);
  struct hastruct tmp_sun_ha;
  calc_HAngle (&tmp_sun_ra, &msg_time->sidt, &tmp_sun_ha);
  struct altstruct tmp_sun_alt;
  convert_EQUI_ALTAZ (&tmp_sun_ha, &tmp_sun_dec, &tmp_sun_alt, NULL);
  memcpy(&objs->all_env.sun_alt, &tmp_sun_alt, sizeof(struct altstruct));
  double sun_alt_d = convert_DMS_D_alt(&tmp_sun_alt);
  char sun_alt_str[20];
  sprintf(sun_alt_str, "%6.2f", sun_alt_d);
  gtk_label_set_text(GTK_LABEL(objs->lbl_sunalt), sun_alt_str);
  GdkColor new_col;
  if (sun_alt_d < SUN_ALT_WARN_DEG)
    gdk_color_parse("#00AA00", &new_col);
  else if (sun_alt_d < SUN_ALT_LIMIT_DEG)
    gdk_color_parse("#AAAA00", &new_col);
  else
    gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_sunalt, GTK_STATE_NORMAL, &new_col);
  if (sun_alt_d < SUN_ALT_WARN_DEG)
  {
    objs->all_env.status_active = ACTIVE_TIME_NIGHT;
    gdk_color_parse("#00AA00", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_active_mode), "NIGHT");
  }
  else
  {
    objs->all_env.status_active = ACTIVE_TIME_DAY;
    gdk_color_parse("#AA0000", &new_col);
    gtk_label_set_text(GTK_LABEL(objs->lbl_active_mode), "DAY");
  }
  gtk_widget_modify_bg(objs->evb_active_mode, GTK_STATE_NORMAL, &new_col);
  
  calc_moon_pos (msg_time->gjd/36525.0, &objs->all_env.moon_ra, &objs->all_env.moon_dec);
  char moon_pos_str[40];
  sprintf(moon_pos_str, "%6.2f %6.2f", convert_HMSMS_H_ra(&objs->all_env.moon_ra), convert_DMS_D_dec(&objs->all_env.moon_dec));
  gtk_label_set_text(GTK_LABEL(objs->lbl_moonpos), moon_pos_str);
}

static void process_msg_coord(EnvWeather *objs, struct act_msg_coord *msg_coord)
{
  double dist = calc_moon_dist(&objs->all_env.moon_ra, &objs->all_env.moon_dec, &msg_coord->ra, &msg_coord->dec);
  GdkColor new_col;
  if (dist > MOON_PROX_WARN_DEG)
    gdk_color_parse("#00AA00", &new_col);
  else if (dist > MOON_PROX_LIMIT_DEG)
    gdk_color_parse("#AAAA00", &new_col);
  else
    gdk_color_parse("#AA0000", &new_col);
  gtk_widget_modify_bg(objs->evb_moonpos, GTK_STATE_NORMAL, &new_col);
}

static void process_msg_environ(EnvWeather *objs, struct act_msg_environ *msg_environ)
{
  memcpy(msg_environ, &objs->all_env, sizeof(struct act_msg_environ));
}

static void process_msg_targset(EnvWeather *objs, struct act_msg_targset *msg_targset)
{
  if (!msg_targset->mode_auto)
    return;
  if (msg_targset->status != OBSNSTAT_GOOD)
    return;
  msg_targset->status = process_obsn(objs, &msg_targset->targ_ra, &msg_targset->targ_dec);
}

static void process_msg_datapmt(EnvWeather *objs, struct act_msg_datapmt *msg_datapmt)
{
  if (!msg_datapmt->mode_auto)
    return;
  if (msg_datapmt->status != OBSNSTAT_GOOD)
    return;
  msg_datapmt->status = process_obsn(objs, &msg_datapmt->targ_ra, &msg_datapmt->targ_dec);
}

static void process_msg_dataccd(EnvWeather *objs, struct act_msg_dataccd *msg_dataccd)
{
  if (!msg_dataccd->mode_auto)
    return;
  if (msg_dataccd->status != OBSNSTAT_GOOD)
    return;
  msg_dataccd->status = process_obsn(objs, &msg_dataccd->targ_ra, &msg_dataccd->targ_dec);
}

static unsigned char process_obsn(EnvWeather *objs, struct rastruct *targ_ra, struct decstruct *targ_dec)
{
  if (!objs->all_env.weath_ok)
    return OBSNSTAT_ERR_WAIT;
  if (calc_moon_dist(&objs->all_env.moon_ra, &objs->all_env.moon_dec, targ_ra, targ_dec) < MOON_PROX_LIMIT_DEG)
    return OBSNSTAT_ERR_NEXT;
  return OBSNSTAT_GOOD;
}

static double calc_moon_dist(struct rastruct *moon_ra, struct decstruct *moon_dec, struct rastruct *targ_ra, struct decstruct *targ_dec)
{
  double targ_ra_rad = convert_H_RAD(convert_HMSMS_H_ra(targ_ra));
  double targ_dec_rad = convert_DEG_RAD(convert_DMS_D_dec(targ_dec));
  double moon_ra_rad = convert_H_RAD(convert_HMSMS_H_ra(moon_ra));
  double moon_dec_rad = convert_DEG_RAD(convert_DMS_D_dec(moon_dec));
  double dist_rad = acos(sin(targ_dec_rad)*sin(moon_dec_rad) + cos(targ_dec_rad)*cos(moon_dec_rad)*cos(targ_ra_rad-moon_ra_rad));
  return convert_RAD_DEG(dist_rad);
}








/*

void update_environ(struct environ_objects *objs, unsigned int update_period_ms)
{
  act_log_debug(act_log_msg("Updating environment."));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  objs->time_since_swasp_ms += update_period_ms;
  objs->time_since_salt_ms += update_period_ms;
  objs->time_since_time_ms += update_period_ms;
  unsigned long time_since_env_ms = objs->time_since_swasp_ms < objs->time_since_salt_ms ? objs->time_since_swasp_ms : objs->time_since_salt_ms;
  if (((time_since_env_ms > WEATHER_TIMEOUT_S*1000) || (objs->time_since_time_ms > WEATHER_TIMEOUT_S*1000)) && (objs->all_env.weath_ok))
  {
    if (time_since_env_ms > WEATHER_TIMEOUT_S*1000)
      act_log_error(act_log_msg("More than %d ms since time data was last successfully retrieved (%d ms). Setting weather bad flag to be safe.", WEATHER_TIMEOUT_S*1000, objs->time_since_time_ms));
    if (objs->time_since_time_ms > WEATHER_TIMEOUT_S*1000)
      act_log_error(act_log_msg("More than %d ms since time data was last successfully retrieved (%d ms). Setting weather bad flag to be safe.", WEATHER_TIMEOUT_S*1000, time_since_env_ms));
    objs->new_weath_ok = FALSE;
    show_weath_change_dialog(objs);
  }
  
  SoupMessage *swasp_msg = soup_message_new ("GET", SWASP_WEATH_URL);
  soup_session_queue_message (objs->sps_swasp, swasp_msg, process_swasp_resp, objs);
  SoupMessage *salt_msg = soup_message_new ("GET", SALT_WEATH_URL);
  soup_session_queue_message (objs->sps_salt, salt_msg, process_salt_resp, objs);
}

void update_time(struct environ_objects *objs, struct act_msg_time *msg_time)
{
  if ((objs == NULL) || (msg_time == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  act_log_debug(act_log_msg("Receied new time: JD %f\n", objs->jd));
  objs->jd = msg_time->gjd;
  memcpy(&objs->sidt, &msg_time->sidt, sizeof(struct timestruct));
  objs->time_since_time_ms = 0;
  calc_moon_pos (objs->jd/36525.0, &objs->all_env.moon_ra, &objs->all_env.moon_dec);
  struct rastruct tmp_sun_ra;
  struct decstruct tmp_sun_dec;
  calc_sun (objs->jd, NULL, NULL, &tmp_sun_ra, &tmp_sun_dec, NULL);
  struct hastruct tmp_sun_ha;
  calc_HAngle (&tmp_sun_ra, &objs->sidt, &tmp_sun_ha);
  convert_EQUI_ALTAZ (&tmp_sun_ha, &tmp_sun_dec, &objs->all_env.sun_alt, NULL);
  
  objs->new_status_active = convert_DMS_D_alt(&objs->all_env.sun_alt) < SUN_ALT_WARN_DEG ? ACTIVE_TIME_NIGHT : ACTIVE_TIME_DAY;
  if (objs->new_status_active != objs->all_env.status_active)
    show_active_change_dialog(objs);
}

char check_env_obsn_auto(struct environ_objects *objs, struct rastruct *targ_ra, struct decstruct *targ_dec)
{
  if ((objs == NULL) || (targ_ra == NULL) || (targ_dec == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  unsigned long time_since_env_ms = objs->time_since_swasp_ms < objs->time_since_salt_ms ? objs->time_since_swasp_ms : objs->time_since_salt_ms;
  if ((time_since_env_ms > WEATHER_TIMEOUT_S*1000) || (objs->time_since_time_ms > WEATHER_TIMEOUT_S*1000))
  {
    act_log_debug(act_log_msg("Need to check weather for automatic observation, but weather/time data outdated."));
    return -1;
  }
  char good = TRUE;
  if (convert_DMS_D_alt(&objs->all_env.sun_alt) >= SUN_ALT_WARN_DEG)
  {
    act_log_normal(act_log_msg("The Sun is up. Cannot observe (alt: %hhd deg).", objs->all_env.sun_alt.degrees));
    good = FALSE;
  }
  if (objs->all_env.wind_vel >= WIND_SPEED_LIMIT_KMPH)
  {
    act_log_normal(act_log_msg("Wind velocity is too high to observe (%f km/h).", objs->all_env.wind_vel));
    good = FALSE;
  }
  if (objs->all_env.humidity >= REL_HUM_LIMIT_PERC)
  {
    act_log_normal(act_log_msg("Humidity is too high to observe (%hhu%%).", objs->all_env.humidity));
    good = FALSE;
  }
  if (objs->all_env.rain)
  {
    act_log_normal(act_log_msg("It is raining. Cannot observe."));
    good = FALSE;
  }
  if (!good)
    return OBSNSTAT_ERR_WAIT;
  if ((fabs(convert_HMSMS_H_ra(targ_ra) - convert_HMSMS_H_ra(&objs->all_env.moon_ra)) * 15.0 < MOON_PROX_WARN_DEG) && (fabs(convert_DMS_D_dec(targ_dec) - convert_DMS_D_dec(&objs->all_env.moon_dec)) < MOON_PROX_WARN_DEG))
  {
    act_log_normal(act_log_msg("The selected target is too close to the Moon. Select a new target."));
    return OBSNSTAT_ERR_NEXT;
  }
  return OBSNSTAT_GOOD;
}

char check_env_obsn_manual(struct environ_objects *objs, struct rastruct *targ_ra, struct decstruct *targ_dec)
{
  if ((objs == NULL) || (targ_ra == NULL) || (targ_dec == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  unsigned long time_since_env_ms = objs->time_since_swasp_ms < objs->time_since_salt_ms ? objs->time_since_swasp_ms : objs->time_since_salt_ms;
  if ((time_since_env_ms > WEATHER_TIMEOUT_S*1000) || (objs->time_since_time_ms > WEATHER_TIMEOUT_S*1000))
  {
    act_log_debug(act_log_msg("Need to check weather for manual observation, but weather/time data outdated."));
    return -1;
  }
  
  if (check_env_obsn_auto(objs, targ_ra, targ_dec) == OBSNSTAT_GOOD)
    return OBSNSTAT_GOOD;
  
  GtkWidget *wnd_main = gtk_widget_get_toplevel(objs->evb_active_mode);
  if ((wnd_main == NULL) || (!GTK_IS_WINDOW(wnd_main)))
  {
    act_log_error(act_log_msg("Need to show dialog with information on unsuitable weather conditions for manual observation, but main window not available."));
    return OBSNSTAT_CANCEL;
  }
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Bad Weather/Conditions", GTK_WINDOW(wnd_main), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new("The conditions are not currently suitable to start an observation."), TRUE, TRUE, 3);
  char tmpstr[256];
  if (convert_DMS_D_alt(&objs->all_env.sun_alt) >= SUN_ALT_WARN_DEG)
  {
    snprintf(tmpstr, sizeof(tmpstr), "The Sun is up. Cannot observe (alt: %hhd deg).", objs->all_env.sun_alt.degrees);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new(tmpstr), TRUE, TRUE, 3);
  }
  if (objs->all_env.wind_vel >= WIND_SPEED_LIMIT_KMPH)
  {
    snprintf(tmpstr, sizeof(tmpstr), "Wind velocity is too high to observe (%f km/h).", objs->all_env.wind_vel);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new(tmpstr), TRUE, TRUE, 3);
  }
  if (objs->all_env.humidity >= REL_HUM_LIMIT_PERC)
  {
    snprintf(tmpstr, sizeof(tmpstr), "Humidity is too high to observe (%hhu%%).", objs->all_env.humidity);
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new(tmpstr), TRUE, TRUE, 3);
  }
  if (objs->all_env.rain)
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new("It is raining. Cannot observe."), TRUE, TRUE, 3);
  if ((fabs(convert_HMSMS_H_ra(targ_ra) - convert_HMSMS_H_ra(&objs->all_env.moon_ra)) * 15.0 < MOON_PROX_WARN_DEG) && (fabs(convert_DMS_D_dec(targ_dec) - convert_DMS_D_dec(&objs->all_env.moon_dec)) < MOON_PROX_WARN_DEG))
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new("The selected target is too close to the Moon. Select a new target."), TRUE, TRUE, 3);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),gtk_label_new("Continue with the observation?"), TRUE, TRUE, 3);
  gtk_widget_show_all(dialog);
  int response_id = gtk_dialog_run(GTK_DIALOG(dialog));
  char ret_val = OBSNSTAT_CANCEL;
  if (response_id == GTK_RESPONSE_OK)
  {
    ret_val = OBSNSTAT_CANCEL;
    act_log_normal(act_log_msg("User has chosen to continue with the observation."));
  }
  else
    act_log_normal(act_log_msg("User has chosen to cancel the observation."));
  gtk_widget_destroy(dialog);
  return ret_val;
}

void update_env_indicators(struct environ_objects *objs)
{
  GdkColor colred;
  GdkColor colyellow;
  GdkColor colgreen;
  GdkColor colblue;
  gdk_color_parse("#AA0000", &colred);
  gdk_color_parse("#AAAA00", &colyellow);
  gdk_color_parse("#00AA00", &colgreen);
  gdk_color_parse("#0000AA", &colblue);
  char tmpstr[50];
  
  if (objs->all_env.humidity >= REL_HUM_LIMIT_PERC)
    gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.humidity >= REL_HUM_WARN_PERC)
    gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &colgreen);
  snprintf(tmpstr, sizeof(tmpstr), "%2d%%", objs->all_env.humidity);
  gtk_label_set_text(GTK_LABEL(objs->lbl_humidity), tmpstr);
  
  if (objs->all_env.clouds <= CLOUD_COVER_LIMIT)
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.clouds <= CLOUD_COVER_WARN)
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colgreen);
  snprintf(tmpstr, sizeof(tmpstr), "%3d", objs->all_env.clouds);
  gtk_label_set_text(GTK_LABEL(objs->lbl_cloud), tmpstr);
  
  if (objs->all_env.rain)
  {
    gtk_widget_modify_bg(objs->evb_rain, GTK_STATE_NORMAL, &colred);
    gtk_label_set_text(GTK_LABEL(objs->lbl_rain), "YES");
  }
  else
  {
    gtk_widget_modify_bg(objs->evb_rain, GTK_STATE_NORMAL, &colgreen);
    gtk_label_set_text(GTK_LABEL(objs->lbl_rain), "NO");
  }
  
  if (objs->all_env.wind_vel >= WIND_SPEED_LIMIT_KMPH)
    gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.wind_vel >= WIND_SPEED_WARN_KMPH)
    gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &colgreen);
  char *wind_azm_str = azm_to_str(&objs->all_env.wind_azm);
  snprintf(tmpstr, sizeof(tmpstr), "%5.1f km/h, %s", objs->all_env.wind_vel, wind_azm_str);
  free(wind_azm_str);
  gtk_label_set_text(GTK_LABEL(objs->lbl_wind), tmpstr);
  
  float new_sun_alt = convert_DMS_D_alt(&objs->all_env.sun_alt);
  if (new_sun_alt >= SUN_ALT_LIMIT_DEG)
    gtk_widget_modify_bg(objs->evb_sunalt, GTK_STATE_NORMAL, &colred);
  else if (new_sun_alt >= SUN_ALT_WARN_DEG)
    gtk_widget_modify_bg(objs->evb_sunalt, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_sunalt, GTK_STATE_NORMAL, &colgreen);
  char *sun_alt_str = alt_to_str(&objs->all_env.sun_alt);
  snprintf(tmpstr, sizeof(tmpstr), "%s", sun_alt_str);
  free(sun_alt_str);
  gtk_label_set_text(GTK_LABEL(objs->lbl_sunalt), tmpstr);
  
  gtk_widget_modify_bg(objs->evb_moonpos, GTK_STATE_NORMAL, &colgreen);
  char *moon_ra_str = ra_to_str(&objs->all_env.moon_ra);
  char *moon_dec_str = dec_to_str(&objs->all_env.moon_dec);
  snprintf(tmpstr, sizeof(tmpstr), "%s %s", moon_ra_str, moon_dec_str);
  free(moon_ra_str);
  free(moon_dec_str);
  gtk_label_set_text(GTK_LABEL(objs->lbl_moonpos), tmpstr);
}

void update_active_indicator(struct environ_objects *objs)
{
  GdkColor new_col;
  char tmpstr[50];
  if ((objs->all_env.status_active & ACTIVE_TIME_NIGHT) == 0)
  {
    gdk_color_parse("#AA0000", &new_col);
    snprintf(tmpstr, sizeof(tmpstr), "DAY");
  }
  else if (objs->all_env.weath_ok > 0)
  {
    gdk_color_parse("#00AA00", &new_col);
    snprintf(tmpstr, sizeof(tmpstr), "NIGHT");
  }
  else
  {
    gdk_color_parse("#AAAA00", &new_col);
    snprintf(tmpstr, sizeof(tmpstr), "BAD WEATH");
  }
  gtk_label_set_text(GTK_LABEL(objs->lbl_active_mode), tmpstr);
  gtk_widget_modify_bg(objs->evb_active_mode, GTK_STATE_NORMAL, &new_col);
}

void process_swasp_resp(SoupSession *sps_swasp, SoupMessage *swasp_msg, gpointer user_data)
{
  act_log_debug(act_log_msg("Processing SuperWASP weather data."));
  (void)sps_swasp;
  if (swasp_msg->status_code != SOUP_STATUS_OK)
  {
    act_log_error(act_log_msg("An error occurred while retrieving SuperWASP weather data. Reason: %s", swasp_msg->reason_phrase));
    return;
  }
  struct environ_objects *objs = (struct environ_objects *)user_data;
  struct swasp_weath_data new_swasp;
  if (!swasp_scrape_all(swasp_msg->response_body->data, swasp_msg->response_body->length, &new_swasp))
  {
    act_log_error(act_log_msg("Failed to extract all SuperWASP weather data. Not updating SuperWASP weather."));
    GdkColor newcol;
    gdk_color_parse("#AA0000", &newcol);
    gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &newcol);
    return;
  }
  GdkColor newcol;
  act_log_debug(act_log_msg("Time (SWASP/ACT): %f %f\n", new_swasp.jd, objs->jd));
  objs->time_since_swasp_ms = abs(new_swasp.jd - objs->jd)*MSEC_PER_DAY;
  if (objs->time_since_swasp_ms > WEATHER_TIMEOUT_S*1000)
  {
    act_log_error(act_log_msg("SuperWASP weather data is outdated (%f sec).", objs->time_since_swasp_ms / 1000.0));
    gdk_color_parse("#AA0000", &newcol);
  }
  else
    gdk_color_parse("#00AA00", &newcol);
  gtk_widget_modify_bg(objs->evb_swasp_stat, GTK_STATE_NORMAL, &newcol);
  objs->swasp_env.humidity = new_swasp.rel_hum;
  objs->swasp_env.clouds = new_swasp.cloud;
  objs->swasp_env.wind_vel = new_swasp.wind_speed;
  convert_D_DMS_azm(new_swasp.wind_dir, &objs->swasp_env.wind_azm);
  objs->swasp_env.rain = new_swasp.rain;
  objs->swasp_env.weath_ok = (
    (objs->swasp_env.wind_vel < WIND_SPEED_LIMIT_KMPH) &&
    (objs->swasp_env.humidity < REL_HUM_LIMIT_PERC) &&
    (!objs->swasp_env.rain)
  );
  
  merge_weath(objs);
  update_env_indicators(objs);
}

void process_salt_resp(SoupSession *sps_salt, SoupMessage *salt_msg, gpointer user_data)
{
  act_log_debug(act_log_msg("Processing SALT weather data."));
  (void)sps_salt;
  if (salt_msg->status_code != SOUP_STATUS_OK)
  {
    act_log_error(act_log_msg("An error occurred while retrieving SuperWASP weather data. Reason: %s", salt_msg->reason_phrase));
    return;
  }
  struct environ_objects *objs = (struct environ_objects *)user_data;
  struct salt_weath_data new_salt;
  if (!salt_extract_all(salt_msg->response_body->data, salt_msg->response_body->length, &new_salt))
  {
    act_log_error(act_log_msg("Failed to extract all SALT weather data. Not updating SALT weather."));
    GdkColor newcol;
    gdk_color_parse("#AA0000", &newcol);
    gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &newcol);
    return;
  }
  GdkColor newcol;
  act_log_debug(act_log_msg("Time (SALT/ACT): %f %f\n", new_salt.jd, objs->jd));
  objs->time_since_salt_ms = abs(new_salt.jd - objs->jd)*MSEC_PER_DAY;
  if (objs->time_since_salt_ms > SALT_WEATHER_TIMEOUT_S*3000)
  {
    act_log_error(act_log_msg("SALT weather data is outdated (%f sec).", objs->time_since_salt_ms / 1000.0));
    gdk_color_parse("#AA0000", &newcol);
  }
  else
    gdk_color_parse("#00AA00", &newcol);
  gtk_widget_modify_bg(objs->evb_salt_stat, GTK_STATE_NORMAL, &newcol);
  objs->salt_env.humidity = new_salt.rel_hum;
  objs->salt_env.clouds = 0;
  objs->salt_env.wind_vel = new_salt.wind_speed_10;
  convert_D_DMS_azm(new_salt.wind_dir_10, &objs->salt_env.wind_azm);
  objs->salt_env.rain = new_salt.rain;
  objs->salt_env.weath_ok = (
    (objs->salt_env.wind_vel < WIND_SPEED_LIMIT_KMPH) &&
    (objs->salt_env.humidity < REL_HUM_LIMIT_PERC) &&
    (!objs->salt_env.rain)
  );
  merge_weath(objs);
  update_env_indicators(objs);
}

void merge_weath(struct environ_objects *objs)
{
  if ((objs->time_since_swasp_ms < WEATHER_TIMEOUT_S*1000) && (objs->time_since_salt_ms < SALT_WEATHER_TIMEOUT_S*1000))
  {
    objs->all_env.wind_vel = objs->swasp_env.wind_vel > objs->salt_env.wind_vel ? objs->swasp_env.wind_vel : objs->salt_env.wind_vel;
    float tmp_wind_dir = (convert_DMS_D_azm(&objs->swasp_env.wind_azm) + convert_DMS_D_azm(&objs->salt_env.wind_azm)) / 2.0;
    convert_D_DMS_azm(tmp_wind_dir / 2.0, &objs->all_env.wind_azm);
    objs->all_env.humidity = objs->swasp_env.humidity > objs->salt_env.humidity ? objs->swasp_env.humidity : objs->salt_env.humidity;
    objs->all_env.clouds = objs->swasp_env.clouds < objs->salt_env.clouds ? objs->swasp_env.clouds : objs->salt_env.clouds;
    objs->all_env.rain = (objs->swasp_env.rain || objs->salt_env.rain);
    objs->new_weath_ok = (objs->swasp_env.weath_ok != 0) && (objs->salt_env.weath_ok != 0);
  }
  else if (objs->time_since_swasp_ms < WEATHER_TIMEOUT_S*1000)
  {
    objs->all_env.wind_vel = objs->swasp_env.wind_vel;
    objs->all_env.wind_azm = objs->swasp_env.wind_azm;
    objs->all_env.humidity = objs->swasp_env.humidity;
    objs->all_env.clouds = objs->swasp_env.clouds;
    objs->all_env.rain = objs->swasp_env.rain;
    objs->new_weath_ok = objs->swasp_env.weath_ok;
  }
  else if (objs->time_since_salt_ms < SALT_WEATHER_TIMEOUT_S*1000)
  {
    objs->all_env.wind_vel = objs->salt_env.wind_vel;
    objs->all_env.wind_azm = objs->salt_env.wind_azm;
    objs->all_env.humidity = objs->salt_env.humidity;
    objs->all_env.clouds = objs->salt_env.clouds;
    objs->all_env.rain = objs->salt_env.rain;
    objs->new_weath_ok = objs->swasp_env.weath_ok;
  }
  else
  {
    act_log_error(act_log_msg("No up-to-date environment information available."));
    objs->new_weath_ok = FALSE;
  }
  if ((objs->new_weath_ok > 0) != (objs->all_env.weath_ok > 0))
    show_weath_change_dialog(objs);
}

void show_weath_change_dialog(struct environ_objects *objs)
{
  if (objs->weath_change_to > 0)
    return;
  GtkWidget *wnd_main = gtk_widget_get_toplevel(objs->evb_active_mode);
  if ((wnd_main == NULL) || (!GTK_IS_WINDOW(wnd_main)))
  {
    act_log_error(act_log_msg("Need to display weather change dialog, but cannot find toplevel window. Not showing prompt and updating weather_ok flag."));
    objs->all_env.weath_ok = objs->new_weath_ok;
    return;
  }
  act_log_normal(act_log_msg("Prompting weather change"));
  GtkWidget *dlg_weath_change = gtk_dialog_new_with_buttons("Weather Change", GTK_WINDOW(wnd_main), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  if ((objs->new_weath_ok > 0) && (objs->all_env.weath_ok == 0))
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("Weather conditions have changed - it is now safe to observe. The weather alert will be lifted when the counter reaches 0.\n\nTo lift the alert immediately, press OK."), TRUE, TRUE, TABLE_PADDING);
  else
  {
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("Weather conditions have changed - it is no longer safe to observe. The weather alert will be raised when the counter reaches 0.\n\nTo raise the alert immediately, press OK."), TRUE, TRUE, TABLE_PADDING);
    if (objs->time_since_time_ms > WEATHER_TIMEOUT_S * 1000)
      gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("Time outdated."), TRUE, TRUE, TABLE_PADDING);
    unsigned long time_since_env_ms = objs->time_since_swasp_ms < objs->time_since_salt_ms ? objs->time_since_swasp_ms : objs->time_since_salt_ms;
    if (time_since_env_ms > WEATHER_TIMEOUT_S*1000)
      gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("Weather data outdated."), TRUE, TRUE, TABLE_PADDING);
    if (objs->salt_env.wind_vel >= WIND_SPEED_LIMIT_KMPH)
      gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("Wind speed too high."), TRUE, TRUE, TABLE_PADDING);
    if (objs->salt_env.humidity >= REL_HUM_LIMIT_PERC)
      gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("Relative humidity too high."), TRUE, TRUE, TABLE_PADDING);
    if (objs->salt_env.rain)
      gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),gtk_label_new("It is raining."), TRUE, TRUE, TABLE_PADDING);
  }
  GtkWidget *spn_counter = gtk_spin_button_new_with_range(0,MAX_PROMPT_TIMEOUT,1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_counter),PROMPT_TIMEOUT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_weath_change)->vbox),spn_counter, TRUE, TRUE, TABLE_PADDING);
  
  objs->weath_change_to = g_timeout_add_seconds(1, prompt_counter_countdown, spn_counter);
  g_signal_connect(G_OBJECT(spn_counter), "value-changed", G_CALLBACK(countdown_change_value), NULL);
  g_signal_connect_swapped(G_OBJECT(spn_counter), "destroy", G_CALLBACK(gtk_widget_destroy), dlg_weath_change);
  g_signal_connect_swapped(G_OBJECT(dlg_weath_change), "response", G_CALLBACK(prompt_counter_zero), spn_counter);
  g_signal_connect_swapped(G_OBJECT(dlg_weath_change), "destroy", G_CALLBACK(weath_change_destroy), objs);
  
  gtk_widget_show_all(dlg_weath_change);
}

void weath_change_destroy(gpointer user_data)
{
  struct environ_objects *objs = (struct environ_objects *)user_data;
  g_source_remove(objs->weath_change_to);
  objs->weath_change_to = 0;
  objs->all_env.weath_ok = objs->new_weath_ok;
  update_active_indicator(objs);
}

void show_active_change_dialog(struct environ_objects *objs)
{
  if (objs->active_change_to > 0)
    return;

  GtkWidget *wnd_main = gtk_widget_get_toplevel(objs->evb_active_mode);
  if ((wnd_main == NULL) || (!GTK_IS_WINDOW(wnd_main)))
  {
    act_log_error(act_log_msg("Need to display active mode change dialog, but cannot find toplevel window. Not showing prompt and active mode flag."));
    objs->all_env.status_active = objs->new_status_active;
    return;
  }

  act_log_normal(act_log_msg("Prompting active mode change"));
  GtkWidget *dlg_active_change = gtk_dialog_new_with_buttons("Active Mode Change", GTK_WINDOW(wnd_main), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  if ((objs->new_status_active & ACTIVE_TIME_NIGHT) > 0)
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_active_change)->vbox),gtk_label_new("Active mode has changed. When the counter below reaches 0, the system will switch to NIGHT MODE.\n\nTo switch to the new mode immediately, press OK."), TRUE, TRUE, TABLE_PADDING);
  else
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_active_change)->vbox),gtk_label_new("Active mode has changed. When the counter below reaches 0, the system will switch to DAY MODE.\n\nTo switch to the new mode immediately, press OK."), TRUE, TRUE, TABLE_PADDING);
  GtkWidget *spn_counter = gtk_spin_button_new_with_range(0,MAX_PROMPT_TIMEOUT,1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_counter),PROMPT_TIMEOUT);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg_active_change)->vbox),spn_counter, TRUE, TRUE, TABLE_PADDING);
  
  objs->active_change_to = g_timeout_add_seconds(1, prompt_counter_countdown, spn_counter);
  g_signal_connect(G_OBJECT(spn_counter), "value-changed", G_CALLBACK(countdown_change_value), NULL);
  g_signal_connect_swapped(G_OBJECT(spn_counter), "destroy", G_CALLBACK(gtk_widget_destroy), dlg_active_change);
  g_signal_connect_swapped(G_OBJECT(dlg_active_change), "response", G_CALLBACK(prompt_counter_zero), spn_counter);
  g_signal_connect_swapped(G_OBJECT(dlg_active_change), "destroy", G_CALLBACK(active_change_destroy), objs);
  
  gtk_widget_show_all(dlg_active_change);
}

void active_change_destroy(gpointer user_data)
{
  struct environ_objects *objs = (struct environ_objects *)user_data;
  g_source_remove(objs->active_change_to);
  objs->active_change_to = 0;
  objs->all_env.status_active = objs->new_status_active;
  update_active_indicator(objs);
}

void prompt_counter_zero(gpointer spn_counter)
{
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_counter), 1);
}

void countdown_change_value(GtkWidget *spn_counter)
{
  act_log_normal(act_log_msg("User changed time to %d seconds.", gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spn_counter))));
}

gboolean prompt_counter_countdown(gpointer user_data)
{
  int cur_time = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(user_data));
  cur_time--;
  if (cur_time <= 0)
  {
    act_log_normal(act_log_msg("Counter has expired."));
    gtk_widget_destroy(GTK_WIDGET(user_data));
    return FALSE;
  }
  g_signal_handlers_disconnect_by_func(G_OBJECT(user_data), G_CALLBACK(countdown_change_value), NULL);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(user_data),cur_time);
  g_signal_connect(G_OBJECT(user_data), "value-changed", G_CALLBACK(countdown_change_value), NULL);
  return TRUE;
}
*/
