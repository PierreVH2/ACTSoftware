#include <gtk/gtk.h>
#include <mysql/mysql.h>
#include <argtable2.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <act_log.h>
#include <act_ipc.h>

#define GUICHECK_TIMEOUT_PERIOD    3
#define OBSN_WAIT_TIMEOUT          60
#define SCHED_IDLE_TIMEOUT         10

struct formobjects
{
  GIOChannel *net_chan;
  MYSQL *mysql_conn;
  GtkWidget *box_main, *lbl_schedline,*evb_schedstat, *lbl_schedstat;
  struct act_msg *cur_msg;
  gulong cur_obsnid, cur_blockid;
  guchar cur_block_stat;
};

GIOChannel *setup_net(const char* host, const char* port);
gboolean read_net_message (GIOChannel *source, GIOCondition condition, gpointer net_read_data);
char net_send(GIOChannel *channel, struct act_msg *msg);
gboolean timeout_check_gui(gpointer user_data);
void destroy_plug(GtkWidget *plug, gpointer user_data);
void request_guisock(GIOChannel *channel);
void cancel_cur(gpointer user_data);
void targset_finish(struct formobjects *objs, struct act_msg_targset *msg_targset);
void datapmt_finish(struct formobjects *objs, struct act_msg_datapmt *msg_datapmt);
void dataccd_finish(struct formobjects *objs, struct act_msg_dataccd *msg_dataccd);
void update_block(struct formobjects *objs);
void sched_next(struct formobjects *objs);
unsigned char sched_next_block(struct formobjects *objs);
unsigned char sched_next_obsn(struct formobjects *objs);
unsigned char sched_next_targset(struct formobjects *objs, int targset_id);
unsigned char sched_next_datapmt(struct formobjects *objs, int datapmt_id);
unsigned char sched_next_dataccd(struct formobjects *objs, int dataccd_id);
gboolean sched_idle(gpointer user_data);
gboolean delayed_obsn_retry(gpointer user_data);
void update_schedstat(struct formobjects *objs, unsigned char stat);
void update_schedline(struct formobjects *objs);


int main(int argc, char** argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  
  const char *host, *port, *sqlconfig;
  gtk_init(&argc, &argv);
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_str *sqlconfigarg = arg_str1("s", "sqlconfighost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
  struct arg_end *endargs = arg_end(10);
  void* argtable[] = {addrarg, portarg, sqlconfigarg, endargs};
  if (arg_nullcheck(argtable) != 0)
    act_log_error(act_log_msg("Argument parsing error: insufficient memory."));
  int argparse_errors = arg_parse(argc,argv,argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr,endargs,argv[0]);
    return 1;
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  sqlconfig = sqlconfigarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));
  
  struct formobjects formobjs;
  memset(&formobjs, 0, sizeof(struct formobjects));
  
  formobjs.net_chan = setup_net(host, port);
  if (formobjs.net_chan == NULL)
  {
    act_log_error(act_log_msg("Error setting up network connection."));
    return 1;
  }
  
  formobjs.mysql_conn = mysql_init(NULL);
  if (formobjs.mysql_conn == NULL)
  {
    act_log_error(act_log_msg("Error initialising MySQL connection handler."));
    g_io_channel_unref(formobjs.net_chan);
    return 1;
  }
  else if (mysql_real_connect(formobjs.mysql_conn, sqlconfig, "act_sched", NULL, "act", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error connecting to MySQL database - %s.", mysql_error(formobjs.mysql_conn)));
    g_io_channel_unref(formobjs.net_chan);
    return 1;
  }

  formobjs.box_main = gtk_table_new(1,3,FALSE);
  formobjs.lbl_schedline = gtk_label_new("");
  g_object_ref(G_OBJECT(formobjs.box_main));
  gtk_table_attach(GTK_TABLE(formobjs.box_main),formobjs.lbl_schedline, 0, 1, 0, 1, GTK_FILL|GTK_EXPAND, GTK_FILL, 3, 3);
  gtk_table_attach(GTK_TABLE(formobjs.box_main),gtk_vseparator_new(), 1, 2, 0, 1, GTK_SHRINK, GTK_FILL, 3, 3);
  formobjs.evb_schedstat = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(formobjs.box_main),formobjs.evb_schedstat, 1, 2, 0, 1, GTK_SHRINK, GTK_FILL, 3, 3);
  formobjs.lbl_schedstat = gtk_label_new("");
  gtk_label_set_width_chars(GTK_LABEL(formobjs.lbl_schedstat), 10);
  gtk_container_add(GTK_CONTAINER(formobjs.evb_schedstat), formobjs.lbl_schedstat);
  GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
  gtk_table_attach(GTK_TABLE(formobjs.box_main),btn_cancel, 2, 3, 0, 1, GTK_SHRINK, GTK_FILL, 3, 3);
  g_signal_connect_swapped(G_OBJECT(btn_cancel), "clicked", G_CALLBACK(cancel_cur), &formobjs);
  
  act_log_normal(act_log_msg("Entering main loop."));
  int guicheck_to_id = g_timeout_add_seconds(GUICHECK_TIMEOUT_PERIOD, timeout_check_gui, &formobjs);
  int net_watch_id = g_io_add_watch(formobjs.net_chan, G_IO_IN, read_net_message, &formobjs);
  g_timeout_add_seconds(SCHED_IDLE_TIMEOUT, sched_idle, &formobjs);
  update_schedstat(&formobjs, 0);
  gtk_main();
  g_object_unref(G_OBJECT(formobjs.box_main));
  g_source_remove(net_watch_id);
  g_source_remove(guicheck_to_id);
  g_io_channel_unref(formobjs.net_chan);
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}

/** \brief Setup network socket with ACT control
 * \param host 
 *   C string containing hostname (dns name) or IP address of the computer running ACT control
 * \param port
 *   C string containing port number or port name on which server is listening for connections.
 * \return File descriptor for socket representing connection to controller (>0), or <=0 if an error occurred.
 */
GIOChannel *setup_net(const char* host, const char* port)
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
    act_log_error(act_log_msg("Failed to connect to server %s", hostport));
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
    act_log_error(act_log_msg("Failed to set encoding type to binary for network communications channel."));
    g_error_free(error);
    return NULL;
  }
  g_io_channel_set_buffered (channel, FALSE);
  
  return channel;
}

/** \brief Check for incoming messages over network connection.
 * \param box_main GTK box containing all graphical objects.
 * \param main_embedded Flag indicating whether box_main is embedded in a plug.
 * \return TRUE if a message was received, FALSE if incoming queue is empty, <0 upon error.
 *
 * Recognises the following message types:
 *  - MT_QUIT: Exit main programme loop.
 *  - MT_CAP: Respond with capabilities and requirements.
 *  - MT_STAT: Respond with current status.
 *  - MT_GUISOCK: Construct GtkPlug for given socket and embed box_main within plug.
 *  - MT_OBSN:
 *    -# Collect photometry with the PMT as indicated in the observation command message.
 */
gboolean read_net_message (GIOChannel *source, GIOCondition condition, gpointer net_read_data)
{
  (void)condition;
  struct formobjects *objs = (struct formobjects *)net_read_data;
  struct act_msg msgbuf;
  unsigned int num_bytes;
  int status;
  GError *error = NULL;
  status = g_io_channel_read_chars (source, (gchar *)&msgbuf, sizeof(msgbuf), &num_bytes, &error);
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
  switch (msgbuf.mtype)
  {
    case MT_QUIT:
    {
      gtk_main_quit();
      break;
    }
    case MT_CAP:
    {
      struct act_msg_cap *cap_msg = &msgbuf.content.msg_cap;
      cap_msg->service_provides = 0;
      cap_msg->service_needs = 0;
      cap_msg->targset_prov = TARGSET_SCHED_PRE | TARGSET_SCHED_POST;
      cap_msg->datapmt_prov = DATAPMT_SCHED_PRE | DATAPMT_SCHED_POST;
      cap_msg->dataccd_prov = DATACCD_SCHED_PRE | DATACCD_SCHED_POST;
      snprintf(cap_msg->version_str,MAX_VERSION_LEN, "%d.%d", MAJOR_VER, MINOR_VER);
      msgbuf.mtype = MT_CAP;
      if (net_send(source, &msgbuf) < 0)
        act_log_error(act_log_msg("Failed to send capabilities response message."));
      break;
    }
    case MT_STAT:
    {
      if (msgbuf.content.msg_stat.status != 0)
        break;
      if (gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) == NULL)
        msgbuf.content.msg_stat.status = PROGSTAT_STARTUP;
      else
        msgbuf.content.msg_stat.status = PROGSTAT_RUNNING;
      if (net_send(source, &msgbuf) < 0)
        act_log_error(act_log_msg("Failed to send status response message."));
      break;
    }
    case MT_GUISOCK:
    {
      if (gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL)
      {
        act_log_debug(act_log_msg("GUI already embedded."));
        break;
      }
      if (msgbuf.content.msg_guisock.gui_socket == 0)
      {
        act_log_error(act_log_msg("Received 0 GUI socket."));
        break;
      }
      GtkWidget *plug = gtk_widget_get_parent(objs->box_main);
      if (plug != NULL)
      {
        gtk_container_remove(GTK_CONTAINER(plug), objs->box_main);
        gtk_widget_destroy(plug);
      }
      plug = gtk_plug_new(msgbuf.content.msg_guisock.gui_socket);
      act_log_normal(act_log_msg("Received GUI socket (%d).", msgbuf.content.msg_guisock.gui_socket));
      gtk_container_add(GTK_CONTAINER(plug),objs->box_main);
      g_signal_connect(G_OBJECT(plug),"destroy",G_CALLBACK(destroy_plug),objs->box_main);
      gtk_widget_show_all(plug);
      break;
    }
    case MT_TARG_SET:
    {
      struct act_msg_targset *msg_targset = (struct act_msg_targset *)&msgbuf.content.msg_targset;
      if (msg_targset->targset_stage == TARGSET_SCHED_PRE)
      {
        act_log_debug(act_log_msg("Received a TARGSET_SCHED_PRE message. Another programme probably requested this. Passing message along."));
        if (net_send(objs->net_chan, &msgbuf) < 0)
          act_log_error(act_log_msg("Failed to pass along TARGSET_SCHED_PRE message."));
        break;
      }
      if (msg_targset->targset_stage == TARGSET_SCHED_POST)
      {
        act_log_debug(act_log_msg("Target set complete message received."));
        targset_finish(objs, msg_targset);
        break;
      }
      act_log_error(act_log_msg("Invalid TARGSET stage: %hhu. Ignoring.", msg_targset->targset_stage));
      break;
    }
    case MT_DATA_PMT:
    {
      struct act_msg_datapmt *msg_datapmt = (struct act_msg_datapmt *)&msgbuf.content.msg_datapmt;
      if (msg_datapmt->datapmt_stage == DATAPMT_SCHED_PRE)
      {
        act_log_debug(act_log_msg("Received a DATAPMT_SCHED_PRE message. Another programme probably requested this. Passing message along."));
        if (net_send(objs->net_chan, &msgbuf) < 0)
          act_log_error(act_log_msg("Failed to pass along DATAPMT_SCHED_PRE message."));
        break;
      }
      if (msg_datapmt->datapmt_stage == DATAPMT_SCHED_POST)
      {
        act_log_debug(act_log_msg("Observation complete message received."));
        datapmt_finish(objs, msg_datapmt);
        break;
      }
      act_log_error(act_log_msg("Invalid DATAPMT stage: %hhu. Ignoring.", msg_datapmt->datapmt_stage));
      break;
    }
    case MT_DATA_CCD:
    {
      struct act_msg_dataccd *msg_dataccd = (struct act_msg_dataccd *)&msgbuf.content.msg_dataccd;
      if (msg_dataccd->dataccd_stage == DATACCD_SCHED_PRE)
      {
        act_log_debug(act_log_msg("Received a DATACCD_SCHED_PRE message. Another programme probably requested this. Passing message along."));
        if (net_send(objs->net_chan, &msgbuf) < 0)
          act_log_error(act_log_msg("Failed to pass along DATACCD_SCHED_PRE message."));
        break;
      }
      if (msg_dataccd->dataccd_stage == DATACCD_SCHED_POST)
      {
        act_log_debug(act_log_msg("Observation complete message received."));
        dataccd_finish(objs, msg_dataccd);
        break;
      }
      act_log_error(act_log_msg("Invalid DATACCD stage: %hhu. Ignoring.", msg_dataccd->dataccd_stage));
      break;
    }
    default:
      if (msgbuf.mtype >= MT_INVAL)
        act_log_error(act_log_msg("Invalid message type received."));
  }
  int new_cond = g_io_channel_get_buffer_condition (source);
  if ((new_cond & G_IO_IN) != 0)
    return read_net_message(source, new_cond, net_read_data);
  return TRUE;
}

char net_send(GIOChannel *channel, struct act_msg *msg)
{
  unsigned int num_bytes;
  int status;
  GError *error = NULL;
  status = g_io_channel_write_chars (channel, (gchar *)msg, sizeof(struct act_msg), &num_bytes, &error);
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

gboolean timeout_check_gui(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL;
  if (!main_embedded)
    request_guisock(objs->net_chan);
  return TRUE;
}

/** \brief Callback when plug (parent of all GTK objects) destroyed, adds a reference to plug's child so it (and it's
 *  \brief children) won't be deleted.
 * \param plug GTK plug containing all on-screen objects.
 * \param user_data The parent of all objects contained within plug.
 * \return (void)
 */
void destroy_plug(GtkWidget *plug, gpointer user_data)
{
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(user_data));
}

/** \brief Request socket information from controller (if GUI objects are not embedded within a socket).
 * \return void
 */
void request_guisock(GIOChannel *channel)
{
  struct act_msg guimsg;
  guimsg.mtype = MT_GUISOCK;
  memset(&guimsg.content.msg_guisock, 0, sizeof(struct act_msg_guisock));
  if (net_send(channel, &guimsg) < 0)
    act_log_error(act_log_msg("Error sending GUI socket request message."));
}

void cancel_cur(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  struct act_msg msg;
  msg.mtype = MT_TARG_SET;
  msg.content.msg_targset.status = OBSNSTAT_CANCEL;
  if (net_send(objs->net_chan, &msg) < 0)
    act_log_error(act_log_msg("Failed to send targset cancel mesaage."));
  msg.mtype = MT_DATA_PMT;
  msg.content.msg_datapmt.status = OBSNSTAT_CANCEL;
  if (net_send(objs->net_chan, &msg) < 0)
    act_log_error(act_log_msg("Failed to send datapmt cancel mesaage."));
  msg.mtype = MT_DATA_CCD;
  msg.content.msg_dataccd.status = OBSNSTAT_CANCEL;
  if (net_send(objs->net_chan, &msg) < 0)
    act_log_error(act_log_msg("Failed to send dataccd cancel mesaage."));
  update_schedstat(objs, OBSNSTAT_CANCEL);
}

void targset_finish(struct formobjects *objs, struct act_msg_targset *msg_targset)
{
  if (objs->cur_msg == NULL)
  {
    act_log_error(act_log_msg("Target set message received, but no observation message is currently in progress. Selecting next queue item."));
    sched_next(objs);
    return;
  }
  if (objs->cur_msg->mtype != MT_TARG_SET)
  {
    act_log_debug(act_log_msg("Target set complete message received, but current item is not a target set. Ignoring message."));
    return;
  }
  update_schedstat(objs, msg_targset->status);
  if (objs->cur_block_stat < msg_targset->status)
    objs->cur_block_stat = msg_targset->status;
  if (msg_targset->status == OBSNSTAT_ERR_RETRY)
  {
    act_log_debug(act_log_msg("Retrying last target set message."));
    msg_targset->status = OBSNSTAT_GOOD;
    // Copy message just in case - it should already be identical (besides the status)
    memcpy(&objs->cur_msg->content.msg_targset, msg_targset, sizeof(struct act_msg_targset));
    if (net_send(objs->net_chan, objs->cur_msg) < 0)
      act_log_error(act_log_msg("Failed to send target set retry message."));
    return;
  }
  if (msg_targset->status == OBSNSTAT_ERR_WAIT)
  {
    msg_targset->status = OBSNSTAT_GOOD;
    // Copy message just in case - it should already be identical (besides the status)
    memcpy(&objs->cur_msg->content.msg_targset, msg_targset, sizeof(struct act_msg_targset));
    g_timeout_add_seconds(OBSN_WAIT_TIMEOUT, delayed_obsn_retry, objs);
    return;
  }
  if (msg_targset->status == OBSNSTAT_ERR_CRIT)
  {
    act_log_debug(act_log_msg("Received target set message with critical error."));
    return;
  }
  if (msg_targset->status != OBSNSTAT_COMPLETE)
    act_log_debug(act_log_msg("Received target set message, but DONE flag not set (status %hhu). Updating DB and selecting next queue item.", msg_targset->status));
  free(objs->cur_msg);
  objs->cur_msg = NULL;
  char qrystr[256];
  sprintf(qrystr, "UPDATE sched_targset SET status=%hhu WHERE id=%lu;", msg_targset->status, objs->cur_obsnid);
  if (mysql_query(objs->mysql_conn, qrystr) != 0)
  {
    act_log_error(act_log_msg("Failed to set db status for completed target set."));
    return;
  }
  sched_next(objs);
}

void datapmt_finish(struct formobjects *objs, struct act_msg_datapmt *msg_datapmt)
{
  if (objs->cur_msg == NULL)
  {
    act_log_error(act_log_msg("Data PMT message received, but no observation message is currently in progress. Selecting next queue item."));
    sched_next(objs);
    return;
  }
  if (objs->cur_msg->mtype != MT_DATA_PMT)
  {
    act_log_debug(act_log_msg("Data PMT complete message received, but current queue item is not a Data PMT item. Ignoring message."));
    return;
  }
  update_schedstat(objs, msg_datapmt->status);
  if (objs->cur_block_stat < msg_datapmt->status)
    objs->cur_block_stat = msg_datapmt->status;
  if (msg_datapmt->status == OBSNSTAT_ERR_RETRY)
  {
    act_log_debug(act_log_msg("Retrying last Data PMT message."));
    msg_datapmt->status = OBSNSTAT_GOOD;
    // Copy message just in case - it should already be identical (besides the status)
    memcpy(&objs->cur_msg->content.msg_datapmt, msg_datapmt, sizeof(struct act_msg_datapmt));
    if (net_send(objs->net_chan, objs->cur_msg) < 0)
      act_log_error(act_log_msg("Failed to send Data PMT retry message."));
    return;
  }
  if (msg_datapmt->status == OBSNSTAT_ERR_WAIT)
  {
    msg_datapmt->status = OBSNSTAT_GOOD;
    // Copy message just in case - it should already be identical (besides the status)
    memcpy(&objs->cur_msg->content.msg_datapmt, msg_datapmt, sizeof(struct act_msg_datapmt));
    g_timeout_add_seconds(OBSN_WAIT_TIMEOUT, delayed_obsn_retry, objs);
    return;
  }
  if (msg_datapmt->status == OBSNSTAT_ERR_CRIT)
  {
    act_log_debug(act_log_msg("Received Data PMT message with critical error."));
    return;
  }
  
  if (msg_datapmt->status != OBSNSTAT_COMPLETE)
    act_log_debug(act_log_msg("Received Data PMT message, but DONE flag not set (status %hhu). Updating DB and selecting next queue item."));
  free(objs->cur_msg);
  msg_datapmt->status = OBSNSTAT_GOOD;
  objs->cur_msg = NULL;
  char qrystr[256];
  sprintf(qrystr, "UPDATE sched_datapmt SET status=%hhu WHERE id=%lu;", msg_datapmt->status, objs->cur_obsnid);
  if (mysql_query(objs->mysql_conn, qrystr) != 0)
  {
    act_log_error(act_log_msg("Failed to set db status for completed Data PMT."));
    return;
  }
  sched_next(objs);
}

void dataccd_finish(struct formobjects *objs, struct act_msg_dataccd *msg_dataccd)
{
  if (objs->cur_msg == NULL)
  {
    act_log_error(act_log_msg("Data CCD message received, but no observation message is currently in progress. Selecting next queue item."));
    sched_next(objs);
    return;
  }
  if (objs->cur_msg->mtype != MT_TARG_SET)
  {
    act_log_debug(act_log_msg("Data CCD complete message received, but current queue item is not a Data CCD item. Ignoring message."));
    return;
  }
  update_schedstat(objs, msg_dataccd->status);
  if (objs->cur_block_stat < msg_dataccd->status)
    objs->cur_block_stat = msg_dataccd->status;
  if (msg_dataccd->status == OBSNSTAT_ERR_RETRY)
  {
    act_log_debug(act_log_msg("Retrying last Data CCD message."));
    struct act_msg msg;
    msg.mtype = MT_DATA_CCD;
    memcpy(&msg.content.msg_dataccd, msg_dataccd, sizeof(struct act_msg_dataccd));
    if (net_send(objs->net_chan,  &msg) < 0)
      act_log_error(act_log_msg("Failed to send Data CCD retry message."));
    return;
  }
  if (msg_dataccd->status == OBSNSTAT_ERR_WAIT)
  {
    msg_dataccd->status = OBSNSTAT_GOOD;
    // Copy message just in case - it should already be identical (besides the status)
    memcpy(&objs->cur_msg->content.msg_dataccd, msg_dataccd, sizeof(struct act_msg_dataccd));
    g_timeout_add_seconds(OBSN_WAIT_TIMEOUT, delayed_obsn_retry, objs);
    return;
  }
  if (msg_dataccd->status == OBSNSTAT_ERR_CRIT)
  {
    act_log_debug(act_log_msg("Received Data CCD complete message with critical error."));
    return;
  }
  if (msg_dataccd->status != OBSNSTAT_COMPLETE)
    act_log_debug(act_log_msg("Received Data CCD message, but DONE flag not set (status %hhu). Updating DB and selecting next queue item."));
  free(objs->cur_msg);
  objs->cur_msg = NULL;
  char qrystr[256];
  sprintf(qrystr, "UPDATE sched_dataccd SET status=%hhu WHERE id=%lu;", msg_dataccd->status, objs->cur_obsnid);
  if (mysql_query(objs->mysql_conn, qrystr) != 0)
  {
    act_log_error(act_log_msg("Failed to set db status for completed Data CCD."));
    return;
  }
  sched_next(objs);
}

void update_block(struct formobjects *objs)
{
  if (objs->cur_blockid <= 0)
  {
    act_log_error(act_log_msg("Invalid block ID (%lu). Cannot update block status.", objs->cur_blockid));
    return;
  }
  char qrystr[256];
  sprintf(qrystr, "UPDATE sched_blocks SET status=%hhu WHERE id=%lu;", objs->cur_block_stat, objs->cur_blockid);
  if (mysql_query(objs->mysql_conn, qrystr) != 0)
  {
    act_log_error(act_log_msg("Failed to set db status for completed block %lu.", objs->cur_blockid));
    return;
  }
}

void sched_next(struct formobjects *objs)
{
  if (objs->cur_msg != NULL)
  {
    act_log_error(act_log_msg("An observation is currently being processed, cannot start a new one."));
    return;
  }
  
  act_log_debug(act_log_msg("cur_msg: %p\tcur_obsnid: %lu\tcur_blockid: %lu\tcur_block_stat: %hhu", objs->cur_msg, objs->cur_obsnid, objs->cur_blockid, objs->cur_block_stat));
  unsigned char ret = 0;
  if (objs->cur_blockid == 0)
  {
    act_log_debug(act_log_msg("No current block."));
    ret = sched_next_block(objs);
    if (!ret)
    {
      act_log_debug(act_log_msg("No new block."));
      g_timeout_add_seconds(SCHED_IDLE_TIMEOUT, sched_idle, objs);
      update_schedline(objs);
      update_schedstat(objs, 0);
      return;
    }
    act_log_debug(act_log_msg("New block."));
  }
  ret = sched_next_obsn(objs);
  if (!ret)
  {
    update_block(objs);
    objs->cur_blockid = 0;
    objs->cur_block_stat = 0;
    if (objs->cur_obsnid != 0)
      g_timeout_add_seconds(SCHED_IDLE_TIMEOUT, sched_idle, objs);
    else
      sched_next(objs);
    return;
  }
  ret = net_send(objs->net_chan, objs->cur_msg);
  if (!ret)
  {
    free(objs->cur_msg);
    objs->cur_msg = NULL;
    objs->cur_obsnid = 0;
    g_timeout_add_seconds(SCHED_IDLE_TIMEOUT, sched_idle, objs);
    update_schedstat(objs, 0);
    return;
  }
  update_schedstat(objs, OBSNSTAT_GOOD);
  update_schedline(objs);
}

unsigned char sched_next_block(struct formobjects *objs)
{
  act_log_debug(act_log_msg("Scheduling next block."));
  MYSQL_RES *result;
  mysql_query(objs->mysql_conn,"SELECT sched_blocks.id FROM sched_blocks INNER JOIN sched_block_seq ON sched_block_seq.block_id=sched_blocks.id INNER JOIN (SELECT block_seq_id FROM sched_targset UNION SELECT block_seq_id FROM sched_datapmt UNION SELECT block_seq_id FROM sched_dataccd) AS obsn_list ON obsn_list.block_seq_id=sched_block_seq.id WHERE sched_blocks.status=0 ORDER BY sched_blocks.priority ASC LIMIT 1;");
  result = mysql_store_result(objs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve next schedule block identifier - %s.", mysql_error(objs->mysql_conn)));
    return 0;
  }
  act_log_debug(act_log_msg("Results retrieved"));
  
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    act_log_debug(act_log_msg("Observing queue is empty."));
    return 0;
  }
  act_log_debug(act_log_msg("1 row retrieved"));
  
  MYSQL_ROW row = mysql_fetch_row(result);
  int blockid;
  if (sscanf(row[0], "%d", &blockid) != 1)
  {
    act_log_error(act_log_msg("Failed to parse identifier for next schedule block (%s).", row[0]));
    mysql_free_result(result);
    return 0;
  }
  act_log_debug(act_log_msg("Next block id: %d\n", blockid));
  mysql_free_result(result);
  objs->cur_blockid = blockid;
  objs->cur_block_stat = 0;
  return 1;
}

unsigned char sched_next_obsn(struct formobjects *objs)
{
  act_log_debug(act_log_msg("Selecting new observation."));  
  char qrystr[512];
  sprintf(qrystr, "SELECT * FROM (SELECT id,block_seq_id,status,1 FROM sched_targset UNION SELECT id,block_seq_id,status,2 FROM sched_dataccd UNION SELECT id,block_seq_id,status,3 FROM sched_datapmt) AS obsn_queue INNER JOIN sched_block_seq ON sched_block_seq.id=obsn_queue.block_seq_id WHERE sched_block_seq.block_id=1 AND obsn_queue.status=%ld;", objs->cur_blockid);
  MYSQL_RES *result;
  mysql_query(objs->mysql_conn,qrystr);
  result = mysql_store_result(objs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve next schedule item identifier - %s.", mysql_error(objs->mysql_conn)));
    return 0;
  }
  act_log_debug(act_log_msg("Results retrieved"));
  
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    act_log_debug(act_log_msg("Queue empty for observing block %d", objs->cur_blockid));
    return 0;
  }
  act_log_debug(act_log_msg("1 new observation"));
  
  MYSQL_ROW row = mysql_fetch_row(result);
  int obsntype, obsnid;
  if (sscanf(row[2], "%d", &obsntype) != 1)
  {
    act_log_error(act_log_msg("Failed to parse observation type information for next schedule item (%s).", row[2]));
    mysql_free_result(result);
    return 0;
  }
  if (sscanf(row[0], "%d", &obsnid) != 1)
  {
    act_log_error(act_log_msg("Failed to parse observation identifier for next schedule item (%s).", row[0]));
    mysql_free_result(result);
    return 0;
  }
  act_log_debug(act_log_msg("New observation type: %d ID: %d", obsntype, obsnid));
  mysql_free_result(result);
  unsigned char ret = 0;
  switch(obsntype)
  {
    case 1:
      ret = sched_next_targset(objs, obsnid);
      break;
    case 2:
      ret = sched_next_dataccd(objs, obsnid);
      break;
    case 3:
      ret = sched_next_datapmt(objs, obsnid);
      break;
    default:
      act_log_error(act_log_msg("Next schedule item has invalid observation type (%d). Skipping this item.", obsntype));
      ret = 0;
  }
  return ret;
}

unsigned char sched_next_targset(struct formobjects *objs, int targset_id)
{
  act_log_debug(act_log_msg("Selecting next target set."));
  MYSQL_RES *result;
  char qrystr[512];
  sprintf(qrystr, "SELECT mode_auto, sched_blocks.targ_id, star_names.star_name, star_info.ra_h_fk5, star_info.dec_d_fk5, sky FROM sched_targset INNER JOIN sched_block_seq ON sched_block_seq.id=sched_targset.block_seq_id INNER JOIN sched_blocks ON sched_blocks.id=sched_block_seq.block_id INNER JOIN star_info ON star_info.id=sched_blocks.targ_id INNER JOIN star_names ON sched_blocks.targ_id=star_names.star_name WHERE sched_targset.id=%d", targset_id);
  mysql_query(objs->mysql_conn,qrystr);
  result = mysql_store_result(objs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve next target set observation information - %s.", mysql_error(objs->mysql_conn)));
    return 0;
  }
  
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    act_log_debug(act_log_msg("Invalid number of rows retrieved (%d)", rowcount));
    return 0;
  }
  
  MYSQL_ROW row = mysql_fetch_row(result);
  unsigned char mode_auto, sky;
  int targ_id;
  char targ_name[MAX_TARGID_LEN];
  double ra_h, dec_d;
  int num_retrieved = 0;
  if (sscanf(row[0], "%hhu", &mode_auto) != 1)
    act_log_error(act_log_msg("Failed to parse mode auto parameter for target set queue item %d (%s).", targset_id, row[0]));
  else
    num_retrieved++;
  if (sscanf(row[1], "%d", &targ_id) != 1)
    act_log_error(act_log_msg("Failed to parse target identifier parameter for target set queue item %d (%s).", targset_id, row[1]));
  else
    num_retrieved++;
  snprintf(targ_name, sizeof(targ_name)-1, "%s", row[2]);
  num_retrieved++;
  if (sscanf(row[3], "%lf", &ra_h) != 1)
    act_log_error(act_log_msg("Failed to parse RA parameter for target set queue item %d (%s).", targset_id, row[3]));
  else
    num_retrieved++;
  if (sscanf(row[4], "%lf", &dec_d) != 1)
    act_log_error(act_log_msg("Failed to parse Dec parameter for target set queue item %d (%s).", targset_id, row[4]));
  else
    num_retrieved++;
  if (sscanf(row[5], "%hhu", &sky) != 1)
    act_log_error(act_log_msg("Failed to parse star/sky parameter for target set queue item %d (%s).", targset_id, row[5]));
  else
    num_retrieved++;
  mysql_free_result(result);
  if (num_retrieved != 6)
  {
    act_log_error(act_log_msg("Failed to retrieve all parameters for next target set schedule item."));
    return 0;
  }

  objs->cur_obsnid = targset_id;
  objs->cur_msg = malloc(sizeof(struct act_msg));
  objs->cur_msg->mtype = MT_TARG_SET;
  struct act_msg_targset *msg_targset = &objs->cur_msg->content.msg_targset;

  msg_targset->status = OBSNSTAT_GOOD;
  msg_targset->targset_stage = TARGSET_SCHED_PRE;
  msg_targset->targ_cent = FALSE;
  msg_targset->focus_pos = 0;
  msg_targset->autoguide= FALSE;
  msg_targset->targ_epoch = 2000.0;

  msg_targset->mode_auto = mode_auto;
  msg_targset->targ_id = targ_id;
  memcpy(msg_targset->targ_name, targ_name, MAX_TARGID_LEN);
  msg_targset->sky = sky;
  convert_H_HMSMS_ra(ra_h, &msg_targset->targ_ra);
  convert_D_DMS_dec(dec_d, &msg_targset->targ_dec);
  
  return 1;
}

unsigned char sched_next_datapmt(struct formobjects *objs, int datapmt_id)
{
  act_log_debug(act_log_msg("Selecting next data pmt."));
  MYSQL_RES *result;
  char qrystr[1024];
  
  sprintf(qrystr, "SELECT mode_auto, sched_blocks.targ_id, star_names.star_name, star_info.ra_h_fk5, star_info.dec_d_fk5, sky, sample_period_s, prebin, repetitions, pmt_filt_id, filter_types.name, pmt_filters.slot, pmt_aper_id, pmt_apertures.name, pmt_apertures.slot, sched_blocks.user_id FROM sched_datapmt INNER JOIN sched_block_seq ON sched_datapmt.block_seq_id=sched_block_seq.id INNER JOIN sched_blocks ON sched_blocks.id=sched_block_seq.block_id INNER JOIN star_info ON star_info.id=sched_blocks.targ_id INNER JOIN star_names ON star_names.star_id=star_info.id INNER JOIN pmt_filters ON pmt_filters.id=sched_datapmt.pmt_filt_id INNER JOIN filter_types ON filter_types.id=pmt_filters.type INNER JOIN pmt_apertures ON pmt_apertures.id=sched_datapmt.pmt_aper_id WHERE sched_datapmt.id=%d;", datapmt_id);
  mysql_query(objs->mysql_conn,qrystr);
  result = mysql_store_result(objs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve next Data PMT observation information - %s.", mysql_error(objs->mysql_conn)));
    return 0;
  }
  act_log_debug(act_log_msg("Results retrieved."));
  
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    act_log_debug(act_log_msg("Invalid number of rows retrieved (%d)", rowcount));
    return 0;
  }
  act_log_debug(act_log_msg("1 row returned"));
  
  MYSQL_ROW row = mysql_fetch_row(result);
  unsigned char mode_auto, sky, filt_slot, aper_slot;
  int targ_id, user_id, prebin_num, repetitions, filt_id, aper_id;
  char targ_name[MAX_TARGID_LEN], filt_name[IPC_MAX_NUM_FILTAPERS], aper_name[IPC_MAX_NUM_FILTAPERS];
  double ra_h, dec_d, sample_period_s;
  int num_retrieved = 0;
  if (sscanf(row[0], "%hhu", &mode_auto) != 1)
    act_log_error(act_log_msg("Failed to parse mode auto parameter for Data PMT queue item %d (%s).", datapmt_id, row[0]));
  else
    num_retrieved++;
  if (sscanf(row[1], "%d", &targ_id) != 1)
    act_log_error(act_log_msg("Failed to parse target identifier parameter for Data PMT queue item %d (%s).", datapmt_id, row[1]));
  else
    num_retrieved++;
  snprintf(targ_name, sizeof(targ_name)-1, "%s", row[2]);
  num_retrieved++;
  if (sscanf(row[3], "%lf", &ra_h) != 1)
    act_log_error(act_log_msg("Failed to parse RA parameter for Data PMT queue item %d (%s).", datapmt_id, row[3]));
  else
    num_retrieved++;
  if (sscanf(row[4], "%lf", &dec_d) != 1)
    act_log_error(act_log_msg("Failed to parse Dec parameter for Data PMT queue item %d (%s).", datapmt_id, row[4]));
  else
    num_retrieved++;
  if (sscanf(row[5], "%hhu", &sky) != 1)
    act_log_error(act_log_msg("Failed to parse star/sky parameter for Data PMT queue item %d (%s).", datapmt_id, row[5]));
  else
    num_retrieved++;
  if (sscanf(row[6], "%lf", &sample_period_s) != 1)
    act_log_error(act_log_msg("Failed to parse sample period parameter for Data PMT queue item %d (%s).", datapmt_id, row[6]));
  else
    num_retrieved++;
  if (sscanf(row[7], "%d", &prebin_num) != 1)
    act_log_error(act_log_msg("Failed to parse prebin parameter for Data PMT queue item %d (%s).", datapmt_id, row[7]));
  else
    num_retrieved++;
  if (sscanf(row[8], "%d", &repetitions) != 1)
    act_log_error(act_log_msg("Failed to parse repetitions parameter for Data PMT queue item %d (%s).", datapmt_id, row[8]));
  else
    num_retrieved++;
  if (sscanf(row[9], "%d", &filt_id) != 1)
    act_log_error(act_log_msg("Failed to parse filter ID parameter for Data PMT queue item %d (%s).", datapmt_id, row[9]));
  else
    num_retrieved++;
  snprintf(filt_name, sizeof(filt_name)-1, "%s", row[10]);
  num_retrieved++;
  if (sscanf(row[11], "%hhu", &filt_slot) != 1)
    act_log_error(act_log_msg("Failed to parse filter slot parameter for Data PMT queue item %d (%s).", datapmt_id, row[11]));
  else
    num_retrieved++;
  if (sscanf(row[12], "%d", &aper_id) != 1)
    act_log_error(act_log_msg("Failed to parse aperture ID parameter for Data PMT queue item %d (%s).", datapmt_id, row[12]));
  else
    num_retrieved++;
  snprintf(aper_name, sizeof(aper_name)-1, "%s", row[13]);
  num_retrieved++;
  if (sscanf(row[14], "%hhu", &aper_slot) != 1)
    act_log_error(act_log_msg("Failed to parse aperture slot parameter for Data PMT queue item %d (%s).", datapmt_id, row[14]));
  else
    num_retrieved++;
  if (sscanf(row[15], "%d", &user_id) != 1)
    act_log_error(act_log_msg("Failed to parse user ID parameter for Data PMT queue item %d (%s).", datapmt_id, row[15]));
  else
    num_retrieved++;
  mysql_free_result(result);
  if (num_retrieved != 16)
  {
    act_log_error(act_log_msg("Failed to retrieve all parameters for Data PMT schedule item."));
    return 0;
  }
  act_log_debug(act_log_msg("Parameters extracted"));
  
  objs->cur_obsnid = datapmt_id;
  objs->cur_msg = malloc(sizeof(struct act_msg));
  objs->cur_msg->mtype = MT_DATA_PMT;
  struct act_msg_datapmt *msg_datapmt = &objs->cur_msg->content.msg_datapmt;
  
  msg_datapmt->status = OBSNSTAT_GOOD;
  msg_datapmt->datapmt_stage = DATAPMT_SCHED_PRE;
  msg_datapmt->targ_epoch = 2000.0;
  msg_datapmt->pmt_mode = 0;
  
  msg_datapmt->mode_auto = mode_auto;
  msg_datapmt->targ_id = targ_id;
  memcpy(msg_datapmt->targ_name, targ_name, MAX_TARGID_LEN);
  msg_datapmt->sky = sky;
  convert_H_HMSMS_ra(ra_h, &msg_datapmt->targ_ra);
  convert_D_DMS_dec(dec_d, &msg_datapmt->targ_dec);
  msg_datapmt->user_id = user_id;
  msg_datapmt->sample_period_s = sample_period_s;
  msg_datapmt->prebin_num = prebin_num;
  msg_datapmt->repetitions = repetitions;
  msg_datapmt->filter.db_id = filt_id;
  memcpy(msg_datapmt->filter.name, filt_name, IPC_MAX_FILTAPER_NAME_LEN);
  msg_datapmt->filter.slot = filt_slot;
  msg_datapmt->aperture.db_id = aper_id;
  memcpy(msg_datapmt->aperture.name, aper_name, IPC_MAX_FILTAPER_NAME_LEN);
  msg_datapmt->aperture.slot = aper_slot;
  
  return 1;
}

unsigned char sched_next_dataccd(struct formobjects *objs, int dataccd_id)
{
  act_log_debug(act_log_msg("Selecting next data CCD."));
  MYSQL_RES *result;
  char qrystr[1024];
  
  sprintf(qrystr, "SELECT mode_auto, sched_blocks.targ_id, star_names.star_name, star_info.ra_h_fk5, star_info.dec_d_fk5, exp_t_s, repetitions, frame_transfer, prebin_x, prebin_y, ccd_filt_id, filter_types.name, ccd_filters.slot, sched_blocks.user_id FROM sched_dataccd INNER JOIN sched_block_seq ON sched_block_seq.id=sched_dataccd.block_seq_id INNER JOIN sched_blocks ON sched_blocks.id=sched_block_seq.block_id INNER JOIN star_info ON sched_blocks.targ_id=star_info.id INNER JOIN star_names ON star_names.star_id=sched_blocks.targ_id INNER JOIN ccd_filters ON ccd_filters.id=sched_dataccd.ccd_filt_id INNER JOIN filter_types ON filter_types.id=ccd_filters.type WHERE sched_dataccd.id=%d", dataccd_id);
  mysql_query(objs->mysql_conn,qrystr);
  result = mysql_store_result(objs->mysql_conn);
  if (result == NULL)
  {
    act_log_error(act_log_msg("Could not retrieve next Data CCD observation information - %s.", mysql_error(objs->mysql_conn)));
    return 0;
  }
  
  int rowcount = mysql_num_rows(result);
  if (rowcount != 1)
  {
    act_log_debug(act_log_msg("Invalid number of rows retrieved (%d)", rowcount));
    return 0;
  }
  
  MYSQL_ROW row = mysql_fetch_row(result);
  unsigned char mode_auto, filt_slot;
  int targ_id, user_id, repetitions, filt_id;
  char targ_name[MAX_TARGID_LEN], filt_name[IPC_MAX_NUM_FILTAPERS];
  double ra_h, dec_d, exp_t_s;
  int num_retrieved = 0;
  if (sscanf(row[0], "%hhu", &mode_auto) != 1)
    act_log_error(act_log_msg("Failed to parse mode auto parameter for Data CCD queue item %d (%s).", dataccd_id, row[0]));
  else
    num_retrieved++;
  if (sscanf(row[1], "%d", &targ_id) != 1)
    act_log_error(act_log_msg("Failed to parse target identifier parameter for Data CCD queue item %d (%s).", dataccd_id, row[1]));
  else
    num_retrieved++;
  snprintf(targ_name, sizeof(targ_name)-1, "%s", row[2]);
  num_retrieved++;
  if (sscanf(row[3], "%lf", &ra_h) != 1)
    act_log_error(act_log_msg("Failed to parse RA parameter for Data CCD queue item %d (%s).", dataccd_id, row[3]));
  else
    num_retrieved++;
  if (sscanf(row[4], "%lf", &dec_d) != 1)
    act_log_error(act_log_msg("Failed to parse Dec parameter for Data CCD queue item %d (%s).", dataccd_id, row[4]));
  else
    num_retrieved++;
  if (sscanf(row[5], "%lf", &exp_t_s) != 1)
    act_log_error(act_log_msg("Failed to parse exposure time parameter for Data CCD queue item %d (%s).", dataccd_id, row[5]));
  else
    num_retrieved++;
  if (sscanf(row[6], "%d", &repetitions) != 1)
    act_log_error(act_log_msg("Failed to parse repetitions parameter for Data CCD queue item %d (%s).", dataccd_id, row[6]));
  else
    num_retrieved++;
  if (sscanf(row[10], "%d", &filt_id) != 1)
    act_log_error(act_log_msg("Failed to parse filter ID parameter for Data CCD queue item %d (%s).", dataccd_id, row[10]));
  else
    num_retrieved++;
  snprintf(filt_name, sizeof(filt_name)-1, "%s", row[11]);
  num_retrieved++;
  if (sscanf(row[12], "%hhu", &filt_slot) != 1)
    act_log_error(act_log_msg("Failed to parse filter slot parameter for Data CCD queue item %d (%s).", dataccd_id, row[12]));
  else
    num_retrieved++;
  if (sscanf(row[13], "%d", &user_id) != 1)
    act_log_error(act_log_msg("Failed to parse user ID parameter for Data CCD queue item %d (%s).", dataccd_id, row[13]));
  else
    num_retrieved++;
  mysql_free_result(result);
  if (num_retrieved != 14)
  {
    act_log_error(act_log_msg("Failed to retrieve all parameters for Data CCD schedule item."));
    return 0;
  }
  
  objs->cur_obsnid = dataccd_id;
  objs->cur_msg = malloc(sizeof(struct act_msg));
  objs->cur_msg->mtype = MT_DATA_CCD;
  struct act_msg_dataccd *msg_dataccd = &objs->cur_msg->content.msg_dataccd;
  
  msg_dataccd->status = OBSNSTAT_GOOD;
  msg_dataccd->dataccd_stage = DATACCD_SCHED_PRE;
  msg_dataccd->targ_epoch = 2000.0;
  msg_dataccd->frame_transfer = TRUE;
  msg_dataccd->prebin_x = msg_dataccd->prebin_y = 1;
  msg_dataccd->window_mode_num = 0;
  
  msg_dataccd->mode_auto = mode_auto;
  msg_dataccd->targ_id = targ_id;
  memcpy(msg_dataccd->targ_name, targ_name, MAX_TARGID_LEN);
  convert_H_HMSMS_ra(ra_h, &msg_dataccd->targ_ra);
  convert_D_DMS_dec(dec_d, &msg_dataccd->targ_dec);
  msg_dataccd->user_id = user_id;
  msg_dataccd->exp_t_s = exp_t_s;
  msg_dataccd->repetitions = repetitions;
  msg_dataccd->filter.db_id = filt_id;
  memcpy(msg_dataccd->filter.name, filt_name, IPC_MAX_FILTAPER_NAME_LEN);
  msg_dataccd->filter.slot = filt_slot;
  
  return 1;
}

gboolean sched_idle(gpointer user_data)
{
  act_log_debug(act_log_msg("Testing for new schedule item."));
  sched_next((struct formobjects *)user_data);
  return  FALSE;
}

gboolean delayed_obsn_retry(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  if (objs->cur_msg == NULL)
  {
    act_log_error(act_log_msg("Need to process a delayed observation retry message, but there is no message currently stored. Selecting next queue item."));
    sched_next(objs);
    return FALSE;
  }
  net_send(objs->net_chan, objs->cur_msg);
  return FALSE;
}

void update_schedstat(struct formobjects *objs, unsigned char stat)
{
  GdkColor new_col;
  switch(stat)
  {
    case 0:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "IDLE");
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, NULL);
      break;
    case OBSNSTAT_GOOD:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "GOOD");
      gdk_color_parse("#00AA00", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    case OBSNSTAT_CANCEL:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "CANCELLED");
      gdk_color_parse("#AAAA00", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    case OBSNSTAT_COMPLETE:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "COMPLETE");
      gdk_color_parse("#00AA00", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    case OBSNSTAT_ERR_RETRY:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "RETRY");
      gdk_color_parse("#AAAA00", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    case OBSNSTAT_ERR_WAIT:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "WAIT");
      gdk_color_parse("#AAAA00", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    case OBSNSTAT_ERR_NEXT:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "NEXT");
      gdk_color_parse("#AAAA00", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    case OBSNSTAT_ERR_CRIT:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "CRITICAL");
      gdk_color_parse("#AA0000", &new_col);
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, &new_col);
      break;
    default:
      gtk_label_set_text(GTK_LABEL(objs->lbl_schedstat), "UNKNOWN");
      gtk_widget_modify_bg(objs->evb_schedstat, GTK_STATE_NORMAL, NULL);
  }
}

void update_schedline(struct formobjects *objs)
{
  if (objs->cur_msg == NULL)
  {
    gtk_label_set_text(GTK_LABEL(objs->lbl_schedline), "");
    return;
  }
  char schedline[256];
  switch (objs->cur_msg->mtype)
  {
    case MT_TARG_SET:
    {
      struct act_msg_targset *msg_targset = &objs->cur_msg->content.msg_targset;
      if (msg_targset->targ_cent)
        sprintf(schedline, "SET TARGET:  %s %s (%d %s %s J%6.1f) %s CENTRED", msg_targset->mode_auto ? "AUTO" : "MANUAL", msg_targset->targ_name, msg_targset->targ_id, ra_to_str(&msg_targset->targ_ra), dec_to_str(&msg_targset->targ_dec), msg_targset->targ_epoch, msg_targset->sky ? "SKY" : "STAR");
      else
        sprintf(schedline, "SET TARGET:  %s %s (%d %s %s J%6.1f) %s ADJUST RA %fs %f\"", msg_targset->mode_auto ? "AUTO" : "MANUAL", msg_targset->targ_name, msg_targset->targ_id, ra_to_str(&msg_targset->targ_ra), dec_to_str(&msg_targset->targ_dec), msg_targset->targ_epoch, msg_targset->sky ? "SKY" : "STAR", convert_HMSMS_H_ra(&msg_targset->adj_ra)*3600.0, convert_DMS_D_dec(&msg_targset->adj_dec)*3600.0);
      break;
    }
    case MT_DATA_PMT:
    {
      struct act_msg_datapmt *msg_datapmt = &objs->cur_msg->content.msg_datapmt;
      sprintf(schedline, "DATA PMT:  %s %s (%d %s %s J%6.1f) %s %lu x %lu x %fs %s %s", msg_datapmt->mode_auto ? "AUTO" : "MANUAL", msg_datapmt->targ_name, msg_datapmt->targ_id, ra_to_str(&msg_datapmt->targ_ra), dec_to_str(&msg_datapmt->targ_dec), msg_datapmt->targ_epoch, msg_datapmt->sky ? "SKY" : "STAR", msg_datapmt->repetitions, msg_datapmt->prebin_num, msg_datapmt->sample_period_s, msg_datapmt->filter.name, msg_datapmt->aperture.name);
      break;
    }
    case MT_DATA_CCD:
    {
      struct act_msg_dataccd *msg_dataccd = &objs->cur_msg->content.msg_dataccd;
      sprintf(schedline, "DATA PMT:  %s %s (%d %s %s J%6.1f) %lu x %fs %s", msg_dataccd->mode_auto ? "AUTO" : "MANUAL", msg_dataccd->targ_name, msg_dataccd->targ_id, ra_to_str(&msg_dataccd->targ_ra), dec_to_str(&msg_dataccd->targ_dec), msg_dataccd->targ_epoch, msg_dataccd->repetitions, msg_dataccd->exp_t_s, msg_dataccd->filter.name);
      break;
    }
    default:
      sprintf(schedline, "INVALID");
  }
  gtk_label_set_text(GTK_LABEL(objs->lbl_schedline), schedline);
}
