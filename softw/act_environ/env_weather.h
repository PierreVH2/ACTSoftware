#ifndef ENV_WEATHER_H
#define ENV_WEATHER_H

#include <gtk/gtk.h>
#include <libsoup/soup.h>
#include "swasp_scrape.h"
#include "salt_extract.h"

/// Maximum permissible wind speed (km/h)
#define WIND_SPEED_WARN_KMPH   50
#define WIND_SPEED_LIMIT_KMPH  60
/// Maximum permissible relative humidity (percentage)
#define REL_HUM_WARN_PERC      85
#define REL_HUM_LIMIT_PERC     90
/// Maximum permissible altitude of the Sun (degrees)
#define SUN_ALT_WARN_DEG       -10
#define SUN_ALT_LIMIT_DEG      -5
#define CLOUD_COVER_WARN       40
#define CLOUD_COVER_LIMIT      30
#define MOON_PROX_WARN_DEG     5

#define ENV_SRC_SWASP 0x1
#define ENV_SRC_SALT  0x2

/** \name GUI objects
 * \brief Struct containing frequently used form objects.
 * \{
 */
struct environ_objects
{
  GtkWidget *evb_swasp_stat, *evb_salt_stat;
  GtkWidget *evb_humidity, *lbl_humidity;
  GtkWidget *evb_cloud, *lbl_cloud;
  GtkWidget *evb_rain, *lbl_rain;
  GtkWidget *evb_wind, *lbl_wind;
  GtkWidget *evb_sunalt, *lbl_sunalt;
  GtkWidget *evb_moonpos, *lbl_moonpos;
  GtkWidget *evb_active_mode, *lbl_active_mode;
  
  struct act_msg_environ all_env, swasp_env, salt_env;
  struct timestruct sidt;
  double jd;
  unsigned long time_since_time_ms, time_since_swasp_ms, time_since_salt_ms;
  SoupSession *sps_swasp, *sps_salt;

  unsigned char new_status_active, new_weath_ok;
  int weath_change_to, active_change_to;
};
/** \} */

struct environ_objects *init_environ(GtkWidget *container);
void finalise_environ(struct environ_objects *objs);
char get_env_ready(struct environ_objects *objs);
void update_environ(struct environ_objects *objs, unsigned int update_period_ms);
void update_time(struct environ_objects *objs, struct act_msg_time *msg_time);
char check_env_obsn_auto(struct environ_objects *objs, struct rastruct *targ_ra, struct decstruct *targ_dec);
char check_env_obsn_manual(struct environ_objects *objs, struct rastruct *targ_ra, struct decstruct *targ_dec);

// char update_environ(struct act_msg_environ *msg, CURL *swasp_http_handle, CURL *salt_http_handle, double jd, struct timestruct *sidt);
// void update_env_indicators(struct env_indicators *objs, char src_status, struct act_msg_environ *old_env, struct act_msg_environ *new_env);
// void prompt_mode_change(GtkWidget *box_main, unsigned char *prompted, unsigned char new_active_status, unsigned char new_weath_ok, struct act_msg_environ *env_msg, int netsock_fd);
// unsigned char check_env_obsn_auto(struct act_msg_environ *msg, struct rastruct *targ_ra, struct decstruct *targ_dec);
// unsigned char check_env_obsn_manual(struct act_msg_environ *msg, struct rastruct *targ_ra, struct decstruct *targ_dec, GtkWidget *box_main);

#endif
