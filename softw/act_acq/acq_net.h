#ifndef __ACQ_NET_H__
#define __ACQ_NET_H__

#include <glib.h>
#include <glib-object.h>
#include <act_ipc.h>

G_BEGIN_DECLS

#define ACQ_MSG_TYPE                           (acq_msg_get_type())
#define ACQ_MSG(objs)                          (G_TYPE_CHECK_INSTANCE_CAST ((objs), ACQ_MSG_TYPE, DtiMsg))
#define ACQ_MSG_CLASS(klass)                   (G_TYPE_CHECK_CLASS_CAST ((klass), ACQ_MSG_TYPE, DtiMsgClass))
#define IS_ACQ_MSG(objs)                       (G_TYPE_CHECK_INSTANCE_TYPE ((objs), ACQ_MSG_TYPE))
#define IS_ACQ_MSG_CLASS(klass)                (G_TYPE_CHECK_CLASS_TYPE ((klass), ACQ_MSG_TYPE))


typedef struct _DtiMsg       DtiMsg;
typedef struct _DtiMsgClass  DtiMsgClass;

struct _DtiMsg
{
  GObject parent;
  
  struct act_msg msg;
  guchar dti_stage;
  guint num_pending;
};

struct _DtiMsgClass
{
  GObjectClass parent_class;
};


GType dti_msg_get_type (void);
DtiMsg *dti_msg_new (struct act_msg *msg, gint stage);
guchar dti_msg_get_mtype (DtiMsg *dti_msg);


struct act_msg_quit *dti_msg_get_quit (DtiMsg *dti_msg);
struct act_msg_cap *dti_msg_get_cap (DtiMsg *dti_msg);
struct act_msg_stat *dti_msg_get_stat (DtiMsg *dti_msg);
struct act_msg_guisock *dti_msg_get_guisock (DtiMsg *dti_msg);
struct act_msg_coord *dti_msg_get_coord (DtiMsg *dti_msg);
struct act_msg_time *dti_msg_get_time (DtiMsg *dti_msg);
struct act_msg_environ *dti_msg_get_environ (DtiMsg *dti_msg);
struct act_msg_targcap *dti_msg_get_targcap (DtiMsg *dti_msg);
struct act_msg_targset *dti_msg_get_targset (DtiMsg *dti_msg);
struct act_msg_pmtcap *dti_msg_get_pmtcap (DtiMsg *dti_msg);
struct act_msg_datapmt *dti_msg_get_datapmt (DtiMsg *dti_msg);
struct act_msg_ccdcap *dti_msg_get_ccdcap (DtiMsg *dti_msg);
struct act_msg_dataccd *dti_msg_get_dataccd (DtiMsg *dti_msg);


#define ACQ_NET_TYPE                            (acq_net_get_type())
#define ACQ_NET(objs)                           (G_TYPE_CHECK_INSTANCE_CAST ((objs), ACQ_NET_TYPE, AcqNet))
#define ACQ_NET_CLASS(klass)                    (G_TYPE_CHECK_CLASS_CAST ((klass), ACQ_NET_TYPE, AcqNetClass))
#define IS_ACQ_NET(objs)                        (G_TYPE_CHECK_INSTANCE_TYPE ((objs), ACQ_NET_TYPE))
#define IS_ACQ_NET_CLASS(klass)                 (G_TYPE_CHECK_CLASS_TYPE ((klass), ACQ_NET_TYPE))

typedef struct _AcqNet       AcqNet;
typedef struct _AcqNetClass  AcqNetClass;

struct _AcqNet
{
  GObject parent;
  GIOChannel *net_chan;
  gint net_watch_id;
  void *targset_msg;
};

struct _AcqNetClass
{
  GObjectClass parent_class;
};


GType acq_net_get_type (void);
AcqNet *acq_net_new (const gchar *host, const gchar *port);
gboolean acq_net_targset_pending(AcqNet *acq_net);
gint acq_net_send_targset_response(AcqNet *acq_net, gdouble adj_ra_h, gdouble adj_dec_d, gboolean targ_cent);
// gint acq_net_send(AcqNet *acq_net, AcqMsg *msg);

G_END_DECLS

#endif