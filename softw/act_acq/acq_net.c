#include <gtk/gtk.h>
#include <string.h>
#include <stdlib.h>
#include <act_ipc.h>
#include <act_log.h>
#include "acq_net.h"
#include "ccd_img.h"
#include "ccd_cntrl.h"
#include "marshallers.h"

#define PENDING_MSG_TARGSET(objs)  (&((struct act_msg *)objs->pending_msg)->content.msg_targset)
#define PENDING_MSG_DATACCD(objs)  (&((struct act_msg *)objs->pending_msg)->content.msg_dataccd)
#define OBJS_CCDCAP_MSG(objs)      (&((struct act_msg *)objs->ccdcap_msg)->content.msg_ccdcap)

enum
{
  SIG_TELCOORD = 0,
  SIG_GUISOCK,
  SIG_CHANGE_USER,
  SIG_CHANGE_TARG,
  SIG_TARGSET_START,
  SIG_TARGSET_STOP,
  SIG_CCDCAP,
  SIG_DATACCD_START,
  SIG_DATACCD_STOP,
  LAST_SIGNAL
};

static guint acq_net_signals[LAST_SIGNAL] = { 0 };


static void acq_net_class_init(AcqNetClass *klass);
static void acq_net_instance_init(GObject *acq_net);
static void acq_net_instance_dispose(GObject *acq_net);
static gboolean net_read_ready(GIOChannel *net_chan, GIOCondition cond, gpointer acq_net);
static gint acq_net_send(GIOChannel *channel, struct act_msg *msg);
static gboolean net_read_ready(GIOChannel *net_chan, GIOCondition cond, gpointer acq_net);
static void process_msg_targset(AcqNet *objs, struct act_msg *msgbuf);
static void process_msg_dataccd(AcqNet *objs, struct act_msg *msgbuf);

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

gboolean acq_net_request_guisocket(AcqNet *objs)
{
  struct act_msg msgbuf;
  memset(&msgbuf, 0, sizeof(struct act_msg));
  msgbuf.mtype = MT_GUISOCK;
  if (acq_net_send(objs->net_chan, &msgbuf) < 0)
  {
    act_log_error(act_log_msg("Failed to send GUI socket request message."));
    return FALSE;
  }
  return TRUE;
}

gboolean acq_net_targset_pending(AcqNet *objs)
{
  return ((struct act_msg *)objs->pending_msg)->mtype == MT_TARG_SET;
}

gint acq_net_send_targset_response(AcqNet *objs, gchar status, gdouble adj_ra_d, gdouble adj_dec_d, gboolean targ_cent)
{
  if (!acq_net_targset_pending(objs))
  {
    act_log_error(act_log_msg("Target set message response requested, but no target set message is pending."));
    return -1;
  }
  PENDING_MSG_TARGSET(objs)->status = status;
  PENDING_MSG_TARGSET(objs)->adj_ra_h = convert_DEG_H(adj_ra_d/15.0);
  PENDING_MSG_TARGSET(objs)->adj_dec_d = adj_dec_d;
  PENDING_MSG_TARGSET(objs)->targ_cent = targ_cent;
  int ret = acq_net_send(objs->net_chan, (struct act_msg *)objs->pending_msg);
  if (ret < 0)
    act_log_error(act_log_msg("Failed to send target set response."));
  else
    ((struct act_msg *)objs->pending_msg)->mtype = 0;
  return ret;
}

void acq_net_set_status(AcqNet *objs, gchar new_stat)
{
  objs->status = new_stat;
}

gchar acq_net_get_status(AcqNet *objs)
{
  return objs->status;
}

void acq_net_set_ccdcap_ready(AcqNet *objs, gboolean ready)
{
  struct act_msg *msg = (struct act_msg *)objs->ccdcap_msg;
  msg->mtype = ready ? MT_CCD_CAP : 0;
  if (objs->ccdcap_pending)
  {
    if (acq_net_send(objs->net_chan, msg) < 0)
      act_log_error(act_log_msg("Failed to send delayed CCD capabilities message."));
  }
}

void acq_net_set_min_integ_t_s(AcqNet *objs, gfloat integ_t)
{
  OBJS_CCDCAP_MSG(objs)->min_exp_t_s = integ_t;
}

void acq_net_set_max_integ_t_s(AcqNet *objs, gfloat integ_t)
{
  OBJS_CCDCAP_MSG(objs)->max_exp_t_s = integ_t;
}

void acq_net_set_ccd_id(AcqNet *objs, const gchar *ccd_id)
{
  snprintf(OBJS_CCDCAP_MSG(objs)->ccd_id, IPC_MAX_INSTRID_LEN-1, "%s", ccd_id);
}

gboolean acq_net_add_filter(AcqNet *objs, const gchar *name, guchar slot, gint db_id)
{
  int i;
  gboolean ret = FALSE;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    struct filtaper *filt = &OBJS_CCDCAP_MSG(objs)->filters[i];
    if (filt->slot != 0)
      continue;
    filt->slot = slot;
    filt->db_id = db_id;
    snprintf(filt->name, IPC_MAX_FILTAPER_NAME_LEN-1, "%s", name);
    ret = TRUE;
    break;
  }
  if (!ret)
    act_log_debug(act_log_msg("Failed to add filter %s to filters list - no free space in array.", name));
  return ret;
}

gboolean acq_net_dataccd_pending(AcqNet *objs)
{
  return ((struct act_msg *)objs->pending_msg)->mtype == MT_DATA_CCD;
}

gint acq_net_send_dataccd_response(AcqNet *objs, gchar status)
{
  if (!acq_net_dataccd_pending(objs))
  {
    act_log_error(act_log_msg("Data CCD message response requested, but no data CCD message is pending."));
    return -1;
  }
  struct act_msg *msg = (struct act_msg *)objs->pending_msg;
  PENDING_MSG_TARGSET(objs)->status = status;
  int ret = acq_net_send(objs->net_chan, msg);
  if (ret < 0)
    act_log_error(act_log_msg("Failed to send data CCD response."));
  else
    msg->mtype = 0;
  return ret;
}

static void acq_net_class_init(AcqNetClass *klass)
{
  acq_net_signals[SIG_TELCOORD] = g_signal_new("coord-received", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__DOUBLE_DOUBLE, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  acq_net_signals[SIG_GUISOCK] = g_signal_new("gui-socket", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__ULONG, G_TYPE_NONE, 1, G_TYPE_ULONG);
  acq_net_signals[SIG_CHANGE_USER] = g_signal_new("change-user", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__ULONG, G_TYPE_NONE, 1, G_TYPE_ULONG);
  acq_net_signals[SIG_CHANGE_TARG] = g_signal_new("change-target", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__ULONG_STRING, G_TYPE_NONE, 2, G_TYPE_ULONG, G_TYPE_STRING);
  acq_net_signals[SIG_TARGSET_START] = g_signal_new("targset-start", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_user_marshal_VOID__DOUBLE_DOUBLE, G_TYPE_NONE, 2, G_TYPE_DOUBLE, G_TYPE_DOUBLE);
  acq_net_signals[SIG_TARGSET_STOP] = g_signal_new("targset-stop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  acq_net_signals[SIG_DATACCD_START] = g_signal_new("data-ccd-start", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__POINTER, G_TYPE_NONE, 1, G_TYPE_POINTER);
  acq_net_signals[SIG_DATACCD_STOP] = g_signal_new("data-ccd-stop", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_FIRST|G_SIGNAL_ACTION, 0, NULL, NULL, g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
  
  G_OBJECT_CLASS(klass)->dispose = acq_net_instance_dispose;
}

static void acq_net_instance_init(GObject *acq_net)
{
  AcqNet *objs = ACQ_NET(acq_net);
  objs->net_chan = NULL;
  objs->net_watch_id = 0;
  objs->pending_msg = malloc(sizeof(struct act_msg));
  ((struct act_msg *)objs->pending_msg)->mtype = 0;
  objs->status = PROGSTAT_STARTUP;
  objs->ccdcap_msg = malloc(sizeof(struct act_msg));
  ((struct act_msg *)objs->ccdcap_msg)->mtype = 0;
  OBJS_CCDCAP_MSG(objs)->dataccd_stage = DATACCD_PHOTOM;
  objs->ccdcap_pending = FALSE;
}

static void acq_net_instance_dispose(GObject *acq_net)
{
  AcqNet *objs = ACQ_NET(acq_net);
  if (objs->ccdcap_msg != NULL)
  {
    free(objs->ccdcap_msg);
    objs->ccdcap_msg = NULL;
  }
  if (objs->pending_msg != NULL)
  {
    gboolean pending_response = FALSE;
    if ((acq_net_targset_pending(objs) && (objs->net_chan != NULL)))
    {
      pending_response = TRUE;
      PENDING_MSG_TARGSET(objs)->status = OBSNSTAT_CANCEL;
    }
    else if ((acq_net_dataccd_pending(objs) && (objs->net_chan != NULL)))
    {
      pending_response = TRUE;
      PENDING_MSG_DATACCD(objs)->status = OBSNSTAT_CANCEL;
    }
    if (pending_response)
    {
      if (acq_net_send(objs->net_chan, (struct act_msg *)objs->pending_msg) < 0)
        act_log_error(act_log_msg("Object being destroyed while messages pending, but failed to send response."));
    }
    free(objs->pending_msg);
    objs->pending_msg = NULL;
  }
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
}

static gint acq_net_send(GIOChannel *channel, struct act_msg *msg)
{
  gsize num_bytes;
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
  AcqNet *objs = ACQ_NET(acq_net);
  struct act_msg msgbuf;
  gsize num_bytes;
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
      // Quit the main programme loop
      gtk_main_quit();
      break;
    case MT_CAP:
      msgbuf.content.msg_cap.service_provides = 0;
      msgbuf.content.msg_cap.service_needs = SERVICE_TIME | SERVICE_COORD;
      msgbuf.content.msg_cap.targset_prov = TARGSET_ACQUIRE;
      msgbuf.content.msg_cap.datapmt_prov = 0;
      msgbuf.content.msg_cap.dataccd_prov = DATACCD_PHOTOM;
      snprintf(msgbuf.content.msg_cap.version_str, MAX_VERSION_LEN-1, "%d.%d", MAJOR_VER, MINOR_VER);
      if (acq_net_send(net_chan, &msgbuf) < 0)
        act_log_error(act_log_msg("Failed to send programme capabilities response.\n"));
      break;
    case MT_STAT:
      msgbuf.content.msg_stat.status = objs->status;
      if (acq_net_send(net_chan, &msgbuf) < 0)
        act_log_error(act_log_msg("Failed to send programme status response.\n"));
      break;
    case MT_GUISOCK:
      g_signal_emit(G_OBJECT(acq_net), acq_net_signals[SIG_GUISOCK], 0, msgbuf.content.msg_guisock.gui_socket);
      break;
    case MT_COORD:
      g_signal_emit(G_OBJECT(acq_net), acq_net_signals[SIG_TELCOORD], 0, convert_H_DEG(convert_HMSMS_H_ra(&msgbuf.content.msg_coord.ra)), convert_DMS_D_dec(&msgbuf.content.msg_coord.dec));
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
      process_msg_targset(objs, &msgbuf);
      break;
    case MT_PMT_CAP:
      // ignore
      break;
    case MT_DATA_PMT:
      // ignore
      break;
    case MT_CCD_CAP:
      if (msgbuf.content.msg_ccdcap.dataccd_stage != 0)
        break; // Ignore
      if (((struct act_msg *)objs->ccdcap_msg)->mtype == MT_CCD_CAP)
      {
        act_log_debug(act_log_msg("Sending DATACCD capabilities response."));
        if (acq_net_send(net_chan, (struct act_msg *)objs->ccdcap_msg) < 0)
          act_log_error(act_log_msg("Failed to send CCD capabilities response message."));
      }
      else
        objs->ccdcap_pending = TRUE;
      break;
    case MT_DATA_CCD:
      process_msg_dataccd(objs, &msgbuf);
      break;
    default:
      act_log_normal(act_log_msg("Invalid message type received (%d)", msgbuf.mtype));
  }
  
  return TRUE;
}

static void process_msg_targset(AcqNet *objs, struct act_msg *msgbuf)
{
  static char cur_targ_name[MAX_TARGID_LEN];
  struct act_msg_targset *msg = &msgbuf->content.msg_targset;
  snprintf(cur_targ_name, MAX_TARGID_LEN-1, "%s", msg->targ_name);
  g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_CHANGE_TARG], 0, msg->targ_id, cur_targ_name);
  if (!msg->mode_auto)
  {
    if (acq_net_send(objs->net_chan, msgbuf) < 0)
      act_log_error(act_log_msg("Failed to send manual target set response message."));
    return;
  }
  if ((msg->status == OBSNSTAT_CANCEL) || 
      (msg->status == OBSNSTAT_ERR_RETRY) || 
      (msg->status == OBSNSTAT_ERR_WAIT) || 
      (msg->status == OBSNSTAT_ERR_NEXT))
  {
    g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_TARGSET_STOP], 0);
    if (acq_net_targset_pending(objs))
      ((struct act_msg *)objs->pending_msg)->mtype = 0;
    if (acq_net_send(objs->net_chan, msgbuf) < 0)
      act_log_error(act_log_msg("Failed to send target set cancel/error message response."));
    return;
  }
  if (((struct act_msg *)objs->pending_msg)->mtype != 0)
  {
    act_log_error(act_log_msg("Target set message received, but a message is currently being processed."));
    msg->status = OBSNSTAT_ERR_RETRY;
    if (acq_net_send(objs->net_chan, msgbuf) < 0)
      act_log_error(act_log_msg("Failed to send target set error message response."));
    return;
  }
  g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_TARGSET_START], 0, convert_H_DEG(convert_HMSMS_H_ra(&msg->targ_ra)), convert_DMS_D_dec(&msg->targ_dec));
  memcpy(objs->pending_msg, msgbuf, sizeof(struct act_msg));
}

static void process_msg_dataccd(AcqNet *objs, struct act_msg *msgbuf)
{
  static char cur_targ_name[MAX_TARGID_LEN];
  struct act_msg_dataccd *msg = &msgbuf->content.msg_dataccd;
  snprintf(cur_targ_name, MAX_TARGID_LEN-1, "%s", msg->targ_name);
  g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_CHANGE_TARG], 0, msg->targ_id, cur_targ_name);
  g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_CHANGE_USER], 0, msg->user_id, cur_targ_name);
  if (!msg->mode_auto)
  {
    if (acq_net_send(objs->net_chan, msgbuf) < 0)
      act_log_error(act_log_msg("Failed to send manual data CCD response message."));
    return;
  }
  if ((msg->status == OBSNSTAT_CANCEL) || 
      (msg->status == OBSNSTAT_ERR_RETRY) || 
      (msg->status == OBSNSTAT_ERR_WAIT) || 
      (msg->status == OBSNSTAT_ERR_NEXT))
  {
    g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_DATACCD_STOP], 0);
    if (acq_net_dataccd_pending(objs))
      ((struct act_msg *)objs->pending_msg)->mtype = 0;
    if (acq_net_send(objs->net_chan, msgbuf) < 0)
      act_log_error(act_log_msg("Failed to send data CCD cancel/error message response."));
    return;
  }
  if (((struct act_msg *)objs->pending_msg)->mtype != 0)
  {
    act_log_error(act_log_msg("Data CCD message received, but a message is currently being processed."));
    msg->status = OBSNSTAT_ERR_RETRY;
    if (acq_net_send(objs->net_chan, msgbuf) < 0)
      act_log_error(act_log_msg("Failed to send data CCD error message response."));
    return;
  }
  
  CcdCmd *cmd = ccd_cmd_new(IMGT_OBJECT, msg->win_start_x, msg->win_start_y, msg->win_width, msg->win_height, msg->prebin_x, msg->prebin_y, msg->exp_t_s, msg->repetitions, msg->targ_id, msg->targ_name);
  g_signal_emit(G_OBJECT(objs), acq_net_signals[SIG_DATACCD_START], 0, cmd);
  memcpy(objs->pending_msg, msgbuf, sizeof(struct act_msg));
}
