#ifndef __ENV_WEATHER_H__
#define __ENV_WEATHER_H__

#include <glib.h>
#include <glib-object.h>
#include <gtk/gtkframe.h>
#include <libsoup/soup.h>
#include <act_ipc.h>
#include "swasp_weath.h"
#include "salt_weath.h"

G_BEGIN_DECLS

#define ENV_WEATHER_TYPE            (env_weather_get_type())
#define ENV_WEATHER(objs)           (G_TYPE_CHECK_INSTANCE_CAST ((objs), ENV_WEATHER_TYPE, EnvWeather))
#define ENV_WEATHER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), ENV_WEATHER_TYPE, EnvWeatherClass))
#define IS_ENV_WEATHER(objs)        (G_TYPE_CHECK_INSTANCE_TYPE ((objs), ENV_WEATHER_TYPE))
#define IS_ENV_WEATHER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), ENV_WEATHER_TYPE))

typedef struct _EnvWeather       EnvWeather;
typedef struct _EnvWeatherClass  EnvWeatherClass;

struct _EnvWeather
{
  GtkFrame parent;
  gint update_to_id, weath_change_to_id, active_change_to_id;
  struct act_msg_environ all_env;
  gboolean salt_ok, swasp_ok;
  SaltWeath *salt_weath;
  SwaspWeath *swasp_weath;

  GtkWidget *box_main;
  GtkWidget *evb_swasp_stat, *evb_salt_stat;
  GtkWidget *evb_humidity, *lbl_humidity;
  GtkWidget *evb_cloud, *lbl_cloud;
  GtkWidget *evb_rain, *lbl_rain;
  GtkWidget *evb_wind, *lbl_wind;
  GtkWidget *evb_sunalt, *lbl_sunalt;
  GtkWidget *evb_moonpos, *lbl_moonpos;
  GtkWidget *evb_active_mode, *lbl_active_mode;
};

struct _EnvWeatherClass
{
  GtkFrameClass parent_class;
};

GType env_weather_get_type (void);
GtkWidget *env_weather_new (void);
void env_weather_process_msg (GtkWidget *env_weather, struct act_msg *msg);

G_END_DECLS

#endif
