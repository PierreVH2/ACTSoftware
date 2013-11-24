#ifndef __DTI_NET_H__
#define __DTI_NET_H__

#include <glib.h>
#include <glib-object.h>
#include <act_ipc.h>

G_BEGIN_DECLS

#define DTI_MSG_TYPE                           (dti_msg_get_type())
#define DTI_MSG(objs)                          (G_TYPE_CHECK_INSTANCE_CAST ((objs), DTI_MSG_TYPE, DtiMsg))
#define DTI_MSG_CLASS(klass)                   (G_TYPE_CHECK_CLASS_CAST ((klass), DTI_MSG_TYPE, DtiMsgClass))
#define IS_DTI_MSG(objs)                       (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DTI_MSG_TYPE))
#define IS_DTI_MSG_CLASS(klass)                (G_TYPE_CHECK_CLASS_TYPE ((klass), DTI_MSG_TYPE))


#define DTI_NET_TYPE                            (dti_net_get_type())
#define DTI_NET(objs)                           (G_TYPE_CHECK_INSTANCE_CAST ((objs), DTI_NET_TYPE, DtiNet))
#define DTI_NET_CLASS(klass)                    (G_TYPE_CHECK_CLASS_CAST ((klass), DTI_NET_TYPE, DtiNetClass))
#define IS_DTI_NET(objs)                        (G_TYPE_CHECK_INSTANCE_TYPE ((objs), DTI_NET_TYPE))
#define IS_DTI_NET_CLASS(klass)                 (G_TYPE_CHECK_CLASS_TYPE ((klass), DTI_NET_TYPE))


enum
{
  DTISTAGE_NONE = 0,
  DTISTAGE_DTIMISC,
  DTISTAGE_INSTRSHUTT,
  DTISTAGE_ACQMIR,
  DTISTAGE_FILTER,
  DTISTAGE_APERTURE,
  DTISTAGE_DOMEMOVE,
  DTISTAGE_TELMOVE,
  DTISTAGE_DOMESHUTTER,
  DTISTAGE_DROPOUT,
  DTISTAGE_INVAL
};


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


typedef struct _DtiNet       DtiNet;
typedef struct _DtiNetClass  DtiNetClass;

struct _DtiNet
{
  GObject parent;
  GIOChannel *net_chan;
  gint net_watch_id;
};

struct _DtiNetClass
{
  GObjectClass parent_class;
};


GType dti_msg_get_type (void);
DtiMsg *dti_msg_new (struct act_msg *msg, gint stage);
guchar dti_msg_get_mtype (DtiMsg *dti_msg);
void dti_msg_set_mtype (DtiMsg *dti_msg, guchar mtype);
gint dti_msg_get_dtistage (DtiMsg *dti_msg);
void dti_msg_set_dtistage (DtiMsg *dti_msg, guchar new_stage);
void dti_msg_inc_dtistage (DtiMsg *dti_msg);
void dti_msg_set_num_pending (DtiMsg *dti_msg, guint new_num_pending);
void dti_msg_dec_num_pending (DtiMsg *dti_msg);
guint dti_msg_get_num_pending (DtiMsg *dti_msg);
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


GType dti_net_get_type (void);
DtiNet *dti_net_new (const gchar *host, const gchar *port);
gint dti_net_send(DtiNet *dti_net, DtiMsg *msg);

G_END_DECLS

#endif