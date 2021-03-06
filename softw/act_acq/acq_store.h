#ifndef __ACQ_STORE_H__
#define __ACQ_STORE_H__

#include <glib.h>
#include <glib-object.h>
#include <pthread.h>
#include <mysql/mysql.h>
#include "ccd_cntrl.h"
#include "ccd_img.h"
#include "point_list.h"
#include "act_ipc.h"

typedef struct _acq_filters_list_t{ struct filtaper filt[IPC_MAX_NUM_FILTAPERS]; } acq_filters_list_t;

G_BEGIN_DECLS

#define ACQ_STORE_TYPE              (acq_store_get_type())
#define ACQ_STORE(objs)             (G_TYPE_CHECK_INSTANCE_CAST ((objs), ACQ_STORE_TYPE, AcqStore))
#define ACQ_STORE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), ACQ_STORE_TYPE, AcqStoreClass))
#define IS_ACQ_STORE(objs)          (G_TYPE_CHECK_INSTANCE_TYPE ((objs), ACQ_STORE_TYPE))
#define IS_ACQ_STORE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), ACQ_STORE_TYPE))

typedef struct _AcqStore       AcqStore;
typedef struct _AcqStoreClass  AcqStoreClass;

struct _AcqStore
{
  GObject parent;
  
  guchar status;
  pthread_t store_thr;
  gchar *sqlhost;
  MYSQL *store_conn;
  MYSQL *genl_conn;
  GSList *img_pend;
  pthread_mutex_t img_list_mutex;
};

struct _AcqStoreClass
{
  GObjectClass parent_class;
};

GType acq_store_get_type (void);
AcqStore *acq_store_new(gchar const *sqlhost);
glong acq_store_search_targ_id(AcqStore *objs, gchar const *targ_name_pat);
gchar *acq_store_get_targ_name(AcqStore *objs, gulong targ_id);
glong acq_store_search_user_id(AcqStore *objs, gchar const *user_name_pat);
gchar *acq_store_get_user_name(AcqStore *objs, gulong user_id);
gboolean acq_store_get_filt_list(AcqStore *objs, acq_filters_list_t *ccd_filters);
PointList *acq_store_get_tycho_pattern(AcqStore *objs, gfloat ra_d, gfloat dec_d, gfloat equinox, gfloat radius_d);
PointList *acq_store_get_gsc1_pattern(AcqStore *objs, gfloat ra_d, gfloat dec_d, gfloat equinox, gfloat radius_d);
void acq_store_append_image(AcqStore *objs, CcdImg *new_img);
gboolean acq_store_idle(AcqStore *objs);
gboolean acq_store_storing(AcqStore *objs);
gboolean acq_store_error_retry(AcqStore *objs);
gboolean acq_store_error_no_recov(AcqStore *objs);

G_END_DECLS

#endif   /* __ACQ_STORE_H__ */
