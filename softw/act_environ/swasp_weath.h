#ifndef __SWASP_WEATH_H__
#define __SWASP_WEATH_H__

#include <glib.h>
#include <glib-object.h>
#include <libsoup/soup.h>
#include <act_ipc.h>

G_BEGIN_DECLS

#define SWASP_WEATH_TYPE            (swasp_weath_get_type())
#define SWASP_WEATH(objs)           (G_TYPE_CHECK_INSTANCE_CAST ((objs), SWASP_WEATH_TYPE, SwaspWeath))
#define SWASP_WEATH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), SWASP_WEATH_TYPE, SwaspWeathClass))
#define IS_SWASP_WEATH(objs)        (G_TYPE_CHECK_INSTANCE_TYPE ((objs), SWASP_WEATH_TYPE))
#define IS_SWASP_WEATH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), SWASP_WEATH_TYPE))

typedef struct _SwaspWeath       SwaspWeath;
typedef struct _SwaspWeathClass  SwaspWeathClass;

struct _SwaspWeath
{
  GObject parent;
  double cur_jd;
  gint fetch_to_id, time_to_id;
  SoupSession *fetch_session;
  SoupMessage *fetch_msg;
  
  /// Julian date (fractional days)
  double weath_jd;
  /// Relative humidity (integer percentage)
  float rel_hum;
  /// Rain (yes/no)
  unsigned char rain;
  /// Wind speed (integer km/h)
  short wind_speed;
  /// Wind direction (fractional degrees in azimuth)
  float wind_dir;
  /// External temperature (fractional degrees Celcius)
  float ext_temp;
  /// External temperature - Dew point temperature (fractional degrees Celcius)
  float ext_dew_temp;
  /// Cloud
  float cloud;
};

struct _SwaspWeathClass
{
  GObjectClass parent_class;
};

GType swasp_weath_get_type (void);
SwaspWeath *swasp_weath_new (void);
void swasp_weath_set_time(SwaspWeath * objs, double jd);
gboolean swasp_weath_get_env_data (SwaspWeath * objs, struct act_msg_environ *env_data);

G_END_DECLS

#endif