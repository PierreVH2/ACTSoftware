#ifndef __ACQ_NET_H__
#define __ACQ_NET_H__

#include <glib.h>
#include <glib-object.h>

G_BEGIN_DECLS

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
  void *pending_msg;
  
  gchar status;
  void *ccdcap_msg;
  gboolean ccdcap_pending;
};

struct _AcqNetClass
{
  GObjectClass parent_class;
};


GType acq_net_get_type (void);
AcqNet *acq_net_new (const gchar *host, const gchar *port);
gboolean acq_net_targset_pending(AcqNet *acq_net);
gint acq_net_send_targset_response(AcqNet *acq_net, gdouble adj_ra_h, gdouble adj_dec_d, gboolean targ_cent);
void acq_net_set_status(AcqNet *acq_net, gchar new_stat);
gchar acq_net_get_status(AcqNet *acq_net);
void acq_net_set_ccdcap_ready(AcqNet *acq_net, gboolean ready);
void acq_net_set_min_exp_t_s(AcqNet *acq_net, gfloat exp_t);
void acq_net_set_max_exp_t_s(AcqNet *acq_net, gfloat exp_t);
void acq_net_set_ccd_id(AcqNet *acq_net, const gchar *ccd_id);
gboolean acq_net_add_filter(AcqNet *acq_net, const gchar *name, guchar slot, gint db_id);
gboolean acq_net_dataccd_pending(AcqNet *acq_net);
gint acq_net_send_dataccd_response(AcqNet *acq_net, gchar status);
// gint acq_net_send(AcqNet *acq_net, AcqMsg *msg);

G_END_DECLS

#endif
