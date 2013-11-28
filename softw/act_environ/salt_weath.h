#ifndef __SALT_WEATH_H__
#define __SALT_WEATH_H__

#include <glib.h>
#include <glib-object.h>
#include <libsoup/soup.h>
#include <act_ipc.h>

G_BEGIN_DECLS

#define SALT_WEATH_TYPE            (salt_weath_get_type())
#define SALT_WEATH(objs)           (G_TYPE_CHECK_INSTANCE_CAST ((objs), SALT_WEATH_TYPE, SaltWeath))
#define SALT_WEATH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SALT_WEATH_TYPE, SaltWeathClass))
#define IS_SALT_WEATH(objs)        (G_TYPE_CHECK_INSTANCE_TYPE ((objs), SALT_WEATH_TYPE))
#define IS_SALT_WEATH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SALT_WEATH_TYPE))

typedef struct _SaltWeath       SaltWeath;
typedef struct _SaltWeathClass  SaltWeathClass;

struct _SaltWeath
{
  GObject parent;
  double cur_jd;
  gint fetch_to_id, time_to_id;
  SoupSession *fetch_session;
  SoupMessage *fetch_msg;
  
  /// Julian date (fractional days)
  double weath_jd;
  /// Air pressure (millibar)
  float air_press;
  /// Dew point temperature (fractional degrees Celcius)
  float dew_point_T;
  /// Relative humidity (percentage)
  float rel_hum;
  /// Wind speed - 30m mast (km/h)
  float wind_speed_30;
  /// Wind direction - 30m mast (fractional degrees in azimuth)
  float wind_dir_30;
  /// Wind speed - 10m mast (km/h)
  float wind_speed_10;
  /// Wind direction - 10m mast (fractional degrees in azimuth)
  float wind_dir_10;
  /// Temperature - 2m mast (fractional degrees Celcius)
  float temp_2;
  /// Temperature - 5m mast (fractional degrees Celcius)
  float temp_5;
  /// Temperature - 10m mast (fractional degrees Celcius)
  float temp_10;
  /// Temperature - 15m mast (fractional degrees Celcius)
  float temp_15;
  /// Temperature - 20m mast (fractional degrees Celcius)
  float temp_20;
  /// Temperature - 25m mast (fractional degrees Celcius)
  float temp_25;
  /// Temperature - 30m mast (fractional degrees Celcius)
  float temp_30;
  /// Rain (yes/no)
  char rain;
};

struct _SaltWeathClass
{
  GObjectClass parent_class;
};

GType salt_weath_get_type (void);
SaltWeath *salt_weath_new (void);
void salt_weath_set_time(SaltWeath * objs, double jd);
gboolean salt_weath_get_env_data (SaltWeath * objs, struct act_msg_environ *env_data);

G_END_DECLS

#endif