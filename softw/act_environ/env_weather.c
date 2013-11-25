#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <act_ipc.h>
#include <act_site.h>
#include <act_log.h>
#include <act_positastro.h>
#include <act_timecoord.h>
#include "env_weather.h"
#include "swasp_scrape.h"
#include "salt_extract.h"

/// Maximum permissible difference between current time and time stamp on weather data
#define WEATHER_TIMEOUT_S      300
#define SALT_WEATHER_TIMEOUT_S 1200
#define MSEC_PER_DAY           86400
#define PROMPT_TIMEOUT         60
#define MAX_PROMPT_TIMEOUT     300

#define SWASP_WEATH_URL "http://swaspgateway.suth/index.php"
#define SALT_WEATH_URL "http://www.salt.ac.za/~saltmet/weather.txt"

#define TABLE_PADDING             3

void update_env_indicators(struct environ_objects *objs);
void update_active_indicator(struct environ_objects *objs);
void process_swasp_resp(SoupSession *sps_swasp, SoupMessage *swasp_msg, gpointer user_data);
void process_salt_resp(SoupSession *sps_salt, SoupMessage *salt_msg, gpointer user_data);
void merge_weath(struct environ_objects *objs);
void show_weath_change_dialog(struct environ_objects *objs);
void weath_change_destroy(gpointer user_data);
void show_active_change_dialog(struct environ_objects *objs);
void active_change_destroy(gpointer user_data);
void prompt_counter_zero(gpointer spn_counter);
void countdown_change_value(GtkWidget *spn_counter);
gboolean prompt_counter_countdown(gpointer user_data);

struct environ_objects *init_environ(GtkWidget *container)
{
  struct environ_objects *objs = malloc(sizeof(struct environ_objects));
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Failed to allocate memory for environment objects."));
    return NULL;
  }
  
  objs->new_status_active = 0;
  objs->new_weath_ok = 0;
  objs->active_change_to = 0;
  objs->weath_change_to = 0;
  objs->time_since_time_ms = WEATHER_TIMEOUT_S*1000 + 1;
  objs->time_since_swasp_ms = WEATHER_TIMEOUT_S*1000 + 1;
  objs->time_since_salt_ms = SALT_WEATHER_TIMEOUT_S*1000 + 1;
  struct datestruct unidate;
  struct timestruct unitime;
  time_t systime_sec = time(NULL);
  struct tm *timedate = gmtime(&systime_sec);
  unidate.year = timedate->tm_year+1900;
  unidate.month = timedate->tm_mon;
  unidate.day = timedate->tm_mday-1;
  unitime.hours = timedate->tm_hour;
  unitime.minutes = timedate->tm_min;
  unitime.seconds = timedate->tm_sec;
  unitime.milliseconds = 0;
  objs->jd = calc_GJD(&unidate, &unitime);
  double tmp_sidt = calc_SidT(objs->jd);
  convert_H_HMSMS_time(tmp_sidt,&objs->sidt);
  
  memset(&objs->swasp_env, 0, sizeof(struct act_msg_environ));
  memset(&objs->salt_env, 0, sizeof(struct act_msg_environ));
  memset(&objs->all_env, 0, sizeof(struct act_msg_environ));
  objs->all_env.wind_vel = WIND_SPEED_LIMIT_KMPH + 1;
  objs->all_env.humidity = REL_HUM_LIMIT_PERC + 1;
  objs->all_env.rain = TRUE;
  objs->all_env.status_active = ACTIVE_TIME_DAY;
  convert_D_DMS_alt(SUN_ALT_WARN_DEG+1.0, &objs->all_env.sun_alt);
  
  GtkWidget *box_indicators = gtk_table_new(0,0,FALSE);
  gtk_container_add(GTK_CONTAINER(container),box_indicators);
  
  GdkColor newcol;
  gdk_color_parse("#0000AA", &newcol);
  GtkWidget *frm_source_stat = gtk_frame_new("Source");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_source_stat, 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
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
  gtk_table_attach(GTK_TABLE(box_indicators), frm_humidity, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_humidity = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_humidity, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_humidity);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_humidity), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_humidity),objs->evb_humidity);
  objs->lbl_humidity = gtk_label_new("N/A");
  g_object_ref(objs->lbl_humidity);
  gtk_container_add(GTK_CONTAINER(objs->evb_humidity),objs->lbl_humidity);
  
  GtkWidget *frm_cloud = gtk_frame_new("Cloud");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_cloud, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_cloud = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_cloud);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_cloud), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_cloud),objs->evb_cloud);
  objs->lbl_cloud = gtk_label_new("N/A");
  g_object_ref(objs->lbl_cloud);
  gtk_container_add(GTK_CONTAINER(objs->evb_cloud),objs->lbl_cloud);
  
  GtkWidget *frm_rain = gtk_frame_new("Rain");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_rain, 3,4,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_rain = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_rain, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_rain);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_rain), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_rain),objs->evb_rain);
  objs->lbl_rain = gtk_label_new("N/A");
  g_object_ref(objs->lbl_rain);
  gtk_container_add(GTK_CONTAINER(objs->evb_rain),objs->lbl_rain);
  
  GtkWidget *frm_wind = gtk_frame_new("Wind");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_wind, 4,5,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_wind = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_wind, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_wind);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_wind), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_wind),objs->evb_wind);
  objs->lbl_wind = gtk_label_new("N/A");
  g_object_ref(objs->lbl_wind);
  gtk_container_add(GTK_CONTAINER(objs->evb_wind),objs->lbl_wind);
  
  GtkWidget *frm_sunalt = gtk_frame_new("Sun Alt.");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_sunalt, 5,6,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_sunalt = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_sunalt, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_sunalt);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_sunalt), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_sunalt),objs->evb_sunalt);
  objs->lbl_sunalt = gtk_label_new("N/A");
  g_object_ref(objs->lbl_sunalt);
  gtk_container_add(GTK_CONTAINER(objs->evb_sunalt),objs->lbl_sunalt);
  
  GtkWidget *frm_moonpos = gtk_frame_new("Moon Pos.");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_moonpos, 6,7,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_moonpos = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_moonpos, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_moonpos);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_moonpos), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_moonpos),objs->evb_moonpos);
  objs->lbl_moonpos = gtk_label_new("N/A");
  g_object_ref(objs->lbl_moonpos);
  gtk_container_add(GTK_CONTAINER(objs->evb_moonpos),objs->lbl_moonpos);
  
  GtkWidget *frm_activemode = gtk_frame_new("Active Mode");
  gtk_table_attach(GTK_TABLE(box_indicators), frm_activemode, 7,8,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL, TABLE_PADDING, TABLE_PADDING);
  objs->evb_active_mode = gtk_event_box_new();
  gtk_widget_modify_bg(objs->evb_active_mode, GTK_STATE_NORMAL, &newcol);
  g_object_ref(objs->evb_active_mode);
  gtk_container_set_border_width(GTK_CONTAINER(objs->evb_active_mode), TABLE_PADDING);
  gtk_container_add(GTK_CONTAINER(frm_activemode),objs->evb_active_mode);
  objs->lbl_active_mode = gtk_label_new("");
  update_active_indicator(objs);
  g_object_ref(objs->lbl_active_mode);
  gtk_container_add(GTK_CONTAINER(objs->evb_active_mode),objs->lbl_active_mode);
  
  gtk_widget_show_all(box_indicators);
  
  objs->sps_swasp = soup_session_async_new();
  objs->sps_salt = soup_session_async_new();
  if ((objs->sps_swasp == NULL) || (objs->sps_salt == NULL))
  {
    act_log_error(act_log_msg("Could not create soup sessions for downloading SALT/SuperWASP weather data."));
    free(objs);
    return NULL;
  }
  SoupMessage *swasp_msg = soup_message_new ("GET", SWASP_WEATH_URL);
  soup_session_queue_message (objs->sps_swasp, swasp_msg, process_swasp_resp, objs);
  SoupMessage *salt_msg = soup_message_new ("GET", SALT_WEATH_URL);
  soup_session_queue_message (objs->sps_salt, salt_msg, process_salt_resp, objs);
  return objs;
}

void finalise_environ(struct environ_objects *objs)
{
  g_object_unref(objs->evb_swasp_stat);
  g_object_unref(objs->evb_salt_stat);
  g_object_unref(objs->evb_humidity);
  g_object_unref(objs->lbl_humidity);
  g_object_unref(objs->evb_cloud);
  g_object_unref(objs->lbl_cloud);
  g_object_unref(objs->evb_rain);
  g_object_unref(objs->lbl_rain);
  g_object_unref(objs->evb_wind);
  g_object_unref(objs->lbl_wind);
  g_object_unref(objs->evb_sunalt);
  g_object_unref(objs->lbl_sunalt);
  g_object_unref(objs->evb_moonpos);
  g_object_unref(objs->lbl_moonpos);
  g_object_unref(objs->evb_active_mode);
  g_object_unref(objs->lbl_active_mode);
  objs->evb_swasp_stat = NULL;
  objs->evb_salt_stat = NULL;
  objs->evb_humidity = NULL;
  objs->lbl_humidity = NULL;
  objs->evb_cloud = NULL;
  objs->lbl_cloud = NULL;
  objs->evb_rain = NULL;
  objs->lbl_rain = NULL;
  objs->evb_wind = NULL;
  objs->lbl_wind = NULL;
  objs->evb_sunalt = NULL;
  objs->lbl_sunalt = NULL;
  objs->evb_moonpos = NULL;
  objs->lbl_moonpos = NULL;
  objs->evb_active_mode = NULL;
  objs->lbl_active_mode = NULL;
}

char get_env_ready(struct environ_objects *objs)
{
  if (objs == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  unsigned long time_since_env_ms = objs->time_since_swasp_ms < objs->time_since_salt_ms ? objs->time_since_swasp_ms : objs->time_since_salt_ms;
  if (time_since_env_ms > WEATHER_TIMEOUT_S*1000)
    return FALSE;
  if (objs->time_since_time_ms > WEATHER_TIMEOUT_S*1000)
    return FALSE;
  return TRUE;
}

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
      act_log_error(act_log_msg("More than %d ms since weather data was last successfully retrieved (%d ms). Setting weather bad flag to be safe.", WEATHER_TIMEOUT_S*1000, time_since_env_ms));
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
  
  if (objs->all_env.clouds >= CLOUD_COVER_LIMIT_PERC)
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colred);
  else if (objs->all_env.clouds >= CLOUD_COVER_WARN_PERC)
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colyellow);
  else
    gtk_widget_modify_bg(objs->evb_cloud, GTK_STATE_NORMAL, &colgreen);
  snprintf(tmpstr, sizeof(tmpstr), "%2d%%", objs->all_env.clouds);
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
    objs->all_env.clouds = objs->swasp_env.clouds > objs->salt_env.clouds ? objs->swasp_env.clouds : objs->salt_env.clouds;
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
