#include <gtk/gtk.h>
#include <string.h>
#include <act_ipc.h>
#include <act_log.h>
#include "acq_net.h"

/// TODO: Change targset_msg so coordinates are reported with signal

enum
{
  STATREQ = 0,
  TELCOORD,
  GUISOCK,
  CHANGE_USER,
  CHANGE_TARG,
  TARGSET_MSG_RECV,
  CCDCAP,
  DATACCD,
  LAST_SIGNAL
};

static guint acq_net_signals[LAST_SIGNAL] = { 0 };


static void acq_net_class_init(DtiNetClass *klass);
static void acq_net_instance_init(GObject *dti_net);
static void acq_net_instance_dispose(GObject *dti_net);
static gboolean net_read_ready(GIOChannel *net_chan, GIOCondition cond, gpointer dti_net);


GType acq_net_get_type (void)
{
  static GType acq_net_type = 0;
  
  if (!acq_net_type)
  {
    const GTypeInfo acq_net_info =
    {
      sizeof (AcqNetClass),
      NULL, /* base_init */
      NULL, /* base_finalize */
      (GClassInitFunc) acq_net_class_init,
      NULL, /* class_finalize */
      NULL, /* class_data */
      sizeof (AcqNet),
      0,
      (GInstanceInitFunc) acq_net_instance_init,
      NULL
    };
    
    acq_net_type = g_type_register_static (G_TYPE_OBJECT, "AcqNet", &acq_net_info, 0);
  }
  
  return acq_net_type;
}

AcqNet *acq_net_new (const gchar *host, const gchar *port)
{
  char hostport[256];
  if (snprintf(hostport, sizeof(hostport), "%s:%s", host, port) != (int)(strlen(host)+strlen(port)+1))
  {
    act_log_error(act_log_msg("Failed to create host-and-port string."));
    return NULL;
  }
  
  GError *error = NULL;
  GSocketClient *socket_client;
  GSocketConnection *socket_connection;
  GSocket *socket;
  int fd;
  GIOChannel *channel;
  
  socket_client = g_socket_client_new();
  socket_connection = g_socket_client_connect_to_host(socket_client, hostport, 0, NULL, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to connect to server %s - %s", hostport, error->message));
    g_error_free(error);
    return NULL;
  }
  
  socket = g_socket_connection_get_socket(socket_connection);
  fd = g_socket_get_fd(socket);
  act_log_debug(act_log_msg("Connected on socket %d", fd));
  channel = g_io_channel_unix_new(fd);
  g_io_channel_set_close_on_unref (channel, TRUE);
  g_io_channel_set_encoding (channel, NULL, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to set encoding type to binary for network communications channel - %s", error->message));
    g_error_free(error);
    return NULL;
  }
  g_io_channel_set_buffered (channel, FALSE);
  
  AcqNet *objs = ACQ_NET(g_object_new (acq_net_get_type(), NULL));
  objs->net_watch_id = g_io_add_watch (channel, G_IO_IN|G_IO_PRI, net_read_ready, objs);
  objs->net_chan = channel;
  return objs;
}

gboolean acq_net_targset_pending(AcqNet *acq_net)
{
  return acq_net->targset_msg != NULL;
}

gint acq_net_send_targset_response(AcqNet *acq_net, gdouble adj_ra_d, gdouble adj_dec_d, gboolean targ_cent)
{
  if (!acq_net_targset_pending(acq_net))
  {
    act_log_error(act_log_msg("Target set message response requested, but no message is pending."));
    return -1;
  }
  struct act_msg_targset *targset_msg = &((struct act_msg *)acq_net->targset_msg).msg_targset;
  targset_msg->adj_ra_h = convert_DEG_H(adj_ra_d/15.0);
  targset_msg->adj_dec_d = adj_dec_d;
  targset_msg->targ_cent = targ_cent;
  int ret = acq_net_send(acq_net->net_chan, (struct act_msg *)acq_net->targset_msg);
  if (ret < 0)
    act_log_error(act_log_msg("Failed to send target set response.");
  else
  {
    free(acq_net->targset_msg);
    acq_net->targset_msg = NULL;
  }
  return ret;
}

static void acq_net_class_init(AcqNetClass *klass)
{
  acq_net_signals[TELCOORD] = g_signal_new("coord-received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__DOUBLE_DOUBLE, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  acq_net_signals[GUISOCK] = g_signal_new("gui-socket", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__ULONG_STRING, G_TYPE_NONE, 2, G_TYPE_ULONG, G_TYPE_STRING);
  acq_net_signals[CHANGE_USER] = g_signal_new("change-user", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__ULONG_STRING, G_TYPE_NONE, 2, G_TYPE_ULONG, G_TYPE_DOUBLE);
  acq_net_signals[CHANGE_TARG] = g_signal_new("change-target", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__ULONG_STRING, G_TYPE_NONE, 2, G_TYPE_ULONG, G_TYPE_DOUBLE);
  acq_net_signals[TARGSET_MSG_RECV] = g_signal_new("targset-msg", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  acq_net_signals[DATACCD] = g_signal_new("data-ccd", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__DOUBLE_DOUBLE, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  
  G_OBJECT_CLASS(klass)->dispose = acq_net_instance_dispose;
  
  
  
  STATREQ = 0,
  TELCOORD,
  GUISOCK,
  CHANGE_USER,
  CHANGE_TARG,
  TARGSET_MSG_RECV,
  CCDCAP,
  DATACCD,
  
}

static void acq_net_instance_init(GObject *acq_net)
{
  AcqNet *objs = ACQ_NET(acq_net);
  objs->net_chan = NULL;
  objs->net_watch_id = 0;
  objs->targset_msg = NULL;
}

static void acq_net_instance_dispose(GObject *acq_net)
{
  AcqNet *objs = ACQ_NET(acq_net);
  if (objs->net_watch_id != 0)
  {
    g_source_remove(objs->net_watch_id);
    objs->net_watch_id = 0;
  }
  if (objs->net_chan != NULL)
  {
    g_io_channel_unref(objs->net_chan);
    objs->net_chan = NULL;
  }
  G_OBJECT_CLASS(acq_net)->dispose(acq_net);
}

static gint acq_net_send(GIOChannel *channel, struct act_msg *msg)
{
  unsigned int num_bytes;
  int status;
  GError *error = NULL;
  status = g_io_channel_write_chars (channel, (gchar *)(msg), sizeof(struct act_msg), &num_bytes, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Error sending message - %s", error->message));
    g_error_free(error);
    return -1;
  }
  if (status != G_IO_STATUS_NORMAL)
  {
    act_log_error(act_log_msg("Incorrect status returned while attempting to send message over network."));
    return -1;
  }
  if (num_bytes != sizeof(struct act_msg))
  {
    act_log_error(act_log_msg("Entire message was not transmitted (%d bytes)", num_bytes));
    return -1;
  }
  return num_bytes;
}

static gboolean net_read_ready(GIOChannel *net_chan, GIOCondition cond, gpointer acq_net)
{
  (void) cond;
  (void) acq_net;
  struct act_msg msgbuf;
  unsigned int num_bytes;
  int status;
  GError *error = NULL;
  status = g_io_channel_read_chars (net_chan, (gchar *)&msgbuf, sizeof(msgbuf), &num_bytes, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("An error occurred while attempting to read message from network - ", error->message));
    g_error_free(error);
    return TRUE;
  }
  if (status != G_IO_STATUS_NORMAL)
  {
    act_log_error(act_log_msg("Incorrect status returned while attempting to read message from network."));
    return TRUE;
  }
  if (num_bytes != sizeof(msgbuf))
  {
    act_log_error(act_log_msg("Received message has invalid length: %d", num_bytes));
    return TRUE;
  }
  
  switch(msgbuf.mtype)
  {
    case MT_QUIT:
      // call gtk quit
      break;
    case MT_CAP:
      // fill in, respond
      break;
    case MT_STAT:
      // signal?
      break;
    case MT_COORD:
      // signal, no response
      break;
    case MT_GUISOCK:
      // signal, no response
      break;
    case MT_TIME:
      // ignore
      break;
    case MT_ENVIRON:
      // ignore
      break;
    case MT_TARG_CAP:
      // ignore
      break;
    case MT_TARG_SET:
      // signal
      break;
    case MT_PMT_CAP:
      // ignore
      break;
    case MT_DATA_PMT:
      // ignore
      break;
    case MT_CCD_CAP:
      // signal
      break;
    case MT_DATA_CCD:
      // signal
      break;
    default:
  }
  
  
  
  DtiMsg *msg = dti_msg_new(&msgbuf, 0);
  g_signal_emit(G_OBJECT(dti_net), dti_net_signals[MSG_RECV_SIGNAL], 0, msg);
  return TRUE;
}


// 
// static void dti_msg_instance_init(GObject *dti_msg);
// 
// static void dti_net_class_init(DtiNetClass *klass);
// static void dti_net_instance_init(GObject *dti_net);
// static void dti_net_instance_dispose(GObject *dti_net);
// static gboolean net_read_ready(GIOChannel *net_chan, GIOCondition cond, gpointer dti_net);
// 
// enum
// {
//   MSG_RECV_SIGNAL,
//   LAST_SIGNAL
// };
// 
// static guint dti_net_signals[LAST_SIGNAL] = { 0 };
// 
// GType dti_net_get_type (void);
// DtiNet *dti_net_new (const gchar *host, const gchar *port);
// gint dti_net_send(DtiNet *dti_net, DtiMsg *msg);
// 
// 
// GType dti_msg_get_type (void)
// {
//   static GType dti_msg_type = 0;
//   
//   if (!dti_msg_type)
//   {
//     const GTypeInfo dti_msg_info =
//     {
//       sizeof (DtiMsgClass),
//       NULL, /* base_init */
//       NULL, /* base_finalize */
//       NULL, /* class init */
//       NULL, /* class_finalize */
//       NULL, /* class_data */
//       sizeof (DtiMsg),
//       0,
//       (GInstanceInitFunc) dti_msg_instance_init,
//       NULL
//     };
//     
//     dti_msg_type = g_type_register_static (G_TYPE_OBJECT, "DtiMsg", &dti_msg_info, 0);
//   }
//   
//   return dti_msg_type;
// }
// 
// DtiMsg *dti_msg_new (struct act_msg *msg, gint stage)
// {
//   DtiMsg *objs = DTI_MSG(g_object_new (dti_msg_get_type(), NULL));
//   if (msg == NULL)
//     memset(&objs->msg, 0, sizeof(struct act_msg));
//   else
//     memcpy(&objs->msg, msg, sizeof(struct act_msg));
//   objs->dti_stage = stage;
//   return objs;
// }
// 
// guchar dti_msg_get_mtype (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return -1;
//   }
//   return dti_msg->msg.mtype;
// }
// 
// void dti_msg_set_mtype (DtiMsg *dti_msg, guchar mtype)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return;
//   }
//   dti_msg->msg.mtype = mtype;
// }
// 
// gint dti_msg_get_dtistage (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return -1;
//   }
//   return dti_msg->dti_stage;
// }
// 
// void dti_msg_set_dtistage (DtiMsg *dti_msg, guchar new_stage)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return;
//   }
//   dti_msg->dti_stage = new_stage;
// }
// 
// void dti_msg_inc_dtistage (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return;
//   }
//   dti_msg->dti_stage++;
// }
// 
// void dti_msg_set_num_pending (DtiMsg *dti_msg, guint new_num_pending)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return;
//   }
//   dti_msg->num_pending = new_num_pending;
// }
// 
// void dti_msg_dec_num_pending (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return;
//   }
//   dti_msg->num_pending--;
// }
// 
// guint dti_msg_get_num_pending (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return 0;
//   }
//   return dti_msg->num_pending;
// }
// 
// struct act_msg_quit *dti_msg_get_quit (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_QUIT)
//   {
//     act_log_error(act_log_msg("This is not a quit message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_QUIT));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_quit;
// }
// 
// struct act_msg_cap *dti_msg_get_cap (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_CAP)
//   {
//     act_log_error(act_log_msg("This is not a capabilities message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_CAP));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_cap;
// }
// 
// struct act_msg_stat *dti_msg_get_stat (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_STAT)
//   {
//     act_log_error(act_log_msg("This is not a status message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_STAT));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_stat;
// }
// 
// struct act_msg_guisock *dti_msg_get_guisock (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_GUISOCK)
//   {
//     act_log_error(act_log_msg("This is not a GUI socket message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_GUISOCK));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_guisock;
// }
// 
// struct act_msg_coord *dti_msg_get_coord (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_COORD)
//   {
//     act_log_error(act_log_msg("This is not a coordinates message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_COORD));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_coord;
// }
// 
// struct act_msg_time *dti_msg_get_time (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_TIME)
//   {
//     act_log_error(act_log_msg("This is not a time message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_TIME));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_time;
// }
// 
// struct act_msg_environ *dti_msg_get_environ (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_ENVIRON)
//   {
//     act_log_error(act_log_msg("This is not a environment message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_ENVIRON));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_environ;
// }
// 
// struct act_msg_targcap *dti_msg_get_targcap (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_TARG_CAP)
//   {
//     act_log_error(act_log_msg("This is not a target set capabilities message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_TARG_CAP));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_targcap;
// }
// 
// struct act_msg_targset *dti_msg_get_targset (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_TARG_SET)
//   {
//     act_log_error(act_log_msg("This is not a target set message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_TARG_SET));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_targset;
// }
// 
// struct act_msg_pmtcap *dti_msg_get_pmtcap (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_PMT_CAP)
//   {
//     act_log_error(act_log_msg("This is not a PMT capabilities message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_PMT_CAP));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_pmtcap;
// }
// 
// struct act_msg_datapmt *dti_msg_get_datapmt (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_DATA_PMT)
//   {
//     act_log_error(act_log_msg("This is not a PMT data message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_DATA_PMT));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_datapmt;
// }
// 
// struct act_msg_ccdcap *dti_msg_get_ccdcap (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_CCD_CAP)
//   {
//     act_log_error(act_log_msg("This is not a CCD capabilities message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_CCD_CAP));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_ccdcap;
// }
// 
// struct act_msg_dataccd *dti_msg_get_dataccd (DtiMsg *dti_msg)
// {
//   if (!IS_DTI_MSG(dti_msg))
//   {
//     act_log_error(act_log_msg("Invalid input parameters."));
//     return NULL;
//   }
//   if (dti_msg_get_mtype(dti_msg) != MT_DATA_CCD)
//   {
//     act_log_error(act_log_msg("This is not a CCD data message (%d should be %d).", dti_msg_get_mtype(dti_msg), MT_DATA_CCD));
//     return NULL;
//   }
//   return &dti_msg->msg.content.msg_dataccd;
// }
// 
// GType dti_net_get_type (void)
// {
//   static GType dti_net_type = 0;
//   
//   if (!dti_net_type)
//   {
//     const GTypeInfo dti_net_info =
//     {
//       sizeof (DtiNetClass),
//       NULL, /* base_init */
//       NULL, /* base_finalize */
//       (GClassInitFunc) dti_net_class_init,
//       NULL, /* class_finalize */
//       NULL, /* class_data */
//       sizeof (DtiNet),
//       0,
//       (GInstanceInitFunc) dti_net_instance_init,
//       NULL
//     };
//     
//     dti_net_type = g_type_register_static (G_TYPE_OBJECT, "DtiNet", &dti_net_info, 0);
//   }
//   
//   return dti_net_type;
// }
// 
// DtiNet *dti_net_new (const gchar *host, const gchar *port)
// {
//   char hostport[256];
//   if (snprintf(hostport, sizeof(hostport), "%s:%s", host, port) != (int)(strlen(host)+strlen(port)+1))
//   {
//     act_log_error(act_log_msg("Failed to create host-and-port string."));
//     return NULL;
//   }
//   
//   GError *error = NULL;
//   GSocketClient *socket_client;
//   GSocketConnection *socket_connection;
//   GSocket *socket;
//   int fd;
//   GIOChannel *channel;
//   
//   socket_client = g_socket_client_new();
//   socket_connection = g_socket_client_connect_to_host(socket_client, hostport, 0, NULL, &error);
//   if (error != NULL)
//   {
//     act_log_error(act_log_msg("Failed to connect to server %s - %s", hostport, error->message));
//     g_error_free(error);
//     return NULL;
//   }
//   
//   socket = g_socket_connection_get_socket(socket_connection);
//   fd = g_socket_get_fd(socket);
//   act_log_debug(act_log_msg("Connected on socket %d", fd));
//   channel = g_io_channel_unix_new(fd);
//   g_io_channel_set_close_on_unref (channel, TRUE);
//   g_io_channel_set_encoding (channel, NULL, &error);
//   if (error != NULL)
//   {
//     act_log_error(act_log_msg("Failed to set encoding type to binary for network communications channel - %s", error->message));
//     g_error_free(error);
//     return NULL;
//   }
//   g_io_channel_set_buffered (channel, FALSE);
//   
//   DtiNet *objs = DTI_NET(g_object_new (dti_net_get_type(), NULL));
//   objs->net_watch_id = g_io_add_watch (channel, G_IO_IN|G_IO_PRI, net_read_ready, objs);
//   objs->net_chan = channel;
//   return objs;
// }
// 
// gint dti_net_send(DtiNet *dti_net, DtiMsg *msg)
// {
//   unsigned int num_bytes;
//   int status;
//   GError *error = NULL;
//   status = g_io_channel_write_chars (dti_net->net_chan, (gchar *)(&msg->msg), sizeof(struct act_msg), &num_bytes, &error);
//   if (error != NULL)
//   {
//     act_log_error(act_log_msg("Error sending message - %s", error->message));
//     g_error_free(error);
//     return -1;
//   }
//   if (status != G_IO_STATUS_NORMAL)
//   {
//     act_log_error(act_log_msg("Incorrect status returned while attempting to send message over network."));
//     return -1;
//   }
//   if (num_bytes != sizeof(struct act_msg))
//   {
//     act_log_error(act_log_msg("Entire message was not transmitted (%d bytes)", num_bytes));
//     return -1;
//   }
//   return num_bytes;
// }
// 
// static void dti_msg_instance_init(GObject *dti_msg)
// {
//   DtiMsg *objs = DTI_MSG(dti_msg);
//   memset(&objs->msg, 0, sizeof(struct act_msg));
//   objs->dti_stage = objs->num_pending = 0;
// }
// 
// static void dti_net_class_init(DtiNetClass *klass)
// {
//   dti_net_signals[MSG_RECV_SIGNAL] = g_signal_new("message-received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
//   G_OBJECT_CLASS(klass)->dispose = dti_net_instance_dispose;
// }
// 
// static void dti_net_instance_init(GObject *dti_net)
// {
//   DtiNet *objs = DTI_NET(dti_net);
//   objs->net_chan = NULL;
//   objs->net_watch_id = 0;
// }
// 
// static void dti_net_instance_dispose(GObject *dti_net)
// {
//   DtiNet *objs = DTI_NET(dti_net);
//   if (objs->net_watch_id != 0)
//   {
//     g_source_remove(objs->net_watch_id);
//     objs->net_watch_id = 0;
//   }
//   if (objs->net_chan != NULL)
//   {
//     g_io_channel_unref(objs->net_chan);
//     objs->net_chan = NULL;
//   }
//   G_OBJECT_CLASS(dti_net)->dispose(dti_net);
// }
// 
// static gboolean net_read_ready(GIOChannel *net_chan, GIOCondition cond, gpointer dti_net)
// {
//   (void) cond;
//   (void) dti_net;
// //   DtiNet *objs = DTI_NET(dti_net);
//   struct act_msg msgbuf;
//   unsigned int num_bytes;
//   int status;
//   GError *error = NULL;
//   status = g_io_channel_read_chars (net_chan, (gchar *)&msgbuf, sizeof(msgbuf), &num_bytes, &error);
//   if (error != NULL)
//   {
//     act_log_error(act_log_msg("An error occurred while attempting to read message from network - ", error->message));
//     g_error_free(error);
//     return TRUE;
//   }
//   if (status != G_IO_STATUS_NORMAL)
//   {
//     act_log_error(act_log_msg("Incorrect status returned while attempting to read message from network."));
//     return TRUE;
//   }
//   if (num_bytes != sizeof(msgbuf))
//   {
//     act_log_error(act_log_msg("Received message has invalid length: %d", num_bytes));
//     return TRUE;
//   }
//   DtiMsg *msg = dti_msg_new(&msgbuf, 0);
//   g_signal_emit(G_OBJECT(dti_net), dti_net_signals[MSG_RECV_SIGNAL], 0, msg);
//   return TRUE;
// }
