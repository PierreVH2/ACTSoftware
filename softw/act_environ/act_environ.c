/*!
 * \file environ.c
 * \brief Environment monitoring / situational awareness programme.
 * \author Pierre van Heerden
 *
 * Extracts weather/environment data from a variety of internal/external sources (at the moment, this is limited to 
 * SALT and SuperWASP), displays data to user, determines ideal operational state of telescope ("active" when it is
 * safe and viable to observe and "idle" otherwise) and determines whether each observation is safe/viable.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// #include <unistd.h>
// #include <fcntl.h>
#include <errno.h>
#include <math.h>
// #include <sys/socket.h>
// #include <sys/types.h>
// #include <netdb.h>
// #include <sys/stat.h>
// #include <time.h>
#include <argtable2.h>
// #include <curl/curl.h>
#include <gtk/gtk.h>
#include <act_ipc.h>
#include <act_site.h>
#include <act_log.h>
#include <act_positastro.h>
#include "env_weather.h"

#define TIME_TIMEOUT_PERIOD       10000
#define TIMEOUT_ENV_UPDATE_PERIOD 60000
#define TIMEOUT_CHECK_GUI_PERIOD  5000

struct formobjects
{
  GtkWidget *box_main;
  GIOChannel *net_chan;
  struct environ_objects *env_objs;
};

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

int net_send(GIOChannel *channel, struct act_msg *msg)
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
      cap_msg->service_provides = SERVICE_ENVIRON;
      cap_msg->service_needs = SERVICE_TIME;
      cap_msg->targset_prov = TARGSET_ENVIRON;
      cap_msg->datapmt_prov = DATAPMT_ENVIRON;
      cap_msg->dataccd_prov = DATACCD_ENVIRON;
      snprintf(cap_msg->version_str,MAX_VERSION_LEN, "%d.%d", MAJOR_VER, MINOR_VER);
      msgbuf.mtype = MT_CAP;
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send capabilities response message."));
      break;
    }
    case MT_STAT:
    {
      if (msgbuf.content.msg_stat.status != 0)
        break;
      char env_ready = get_env_ready(objs->env_objs);
      if (env_ready < 0)
      {
        act_log_error(act_log_msg("Failed to retrieve act_environ readiness. This shouldn't happen."));
        msgbuf.content.msg_stat.status = PROGSTAT_ERR_RESTART;
      }
      else if (env_ready == 0)
        msgbuf.content.msg_stat.status = PROGSTAT_STARTUP;
      else
        msgbuf.content.msg_stat.status = PROGSTAT_RUNNING;
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send status response message."));
      break;
    }
    case MT_GUISOCK:
    {
      if (msgbuf.content.msg_guisock.gui_socket > 0)
      {
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
      }
      else
        act_log_error(act_log_msg("Received 0 GUI socket."));
      break;
    }
    case MT_TIME:
    {
      update_time(objs->env_objs, &msgbuf.content.msg_time);
      break;
    }
    case MT_ENVIRON:
    {
      act_log_debug(act_log_msg("Strange: Received an ENVIRONMENT message. Responding with latest environment message."));
      memcpy(&msgbuf.content.msg_environ, &objs->env_objs->all_env, sizeof(struct act_msg_environ));
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send (unexpected) ENVIRONMENT message response."));
      break;
    }
    case MT_TARG_CAP:
    {
      if (msgbuf.content.msg_targcap.targset_stage != TARGSET_ENVIRON)
      {
        msgbuf.content.msg_targcap.targset_stage = TARGSET_ENVIRON;
        if (net_send(source, &msgbuf) != sizeof(msgbuf))
          act_log_error(act_log_msg("Failed to send response to TARGCAP request."));
      }
      break;
    }
    case MT_TARG_SET:
    {
      struct act_msg_targset *msg_targset = &msgbuf.content.msg_targset;
      if (msg_targset->status == OBSNSTAT_GOOD)
      {
        char new_stat;
        if (msg_targset->mode_auto)
          new_stat = check_env_obsn_auto(objs->env_objs, &msg_targset->targ_ra, &msg_targset->targ_dec);
        else
          new_stat = check_env_obsn_manual(objs->env_objs, &msg_targset->targ_ra, &msg_targset->targ_dec);
        if (new_stat < 0)
        {
          act_log_error(act_log_msg("An error occurred while checking environment for auto/manual TARGSET viability."));
          new_stat = OBSNSTAT_ERR_WAIT;
        }
        msg_targset->status = new_stat;
      }
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send TARGSET response."));
      break;
    }
    case MT_PMT_CAP:
    {
      if (msgbuf.content.msg_pmtcap.datapmt_stage != DATAPMT_ENVIRON)
      {
        msgbuf.content.msg_pmtcap.datapmt_stage = DATAPMT_ENVIRON;
        if (net_send(source, &msgbuf) != sizeof(msgbuf))
          act_log_error(act_log_msg("Failed to send response to PMTCAP request."));
      }
      break;
    }
    case MT_DATA_PMT:
    {
      struct act_msg_datapmt *msg_datapmt = &msgbuf.content.msg_datapmt;
      if ((msg_datapmt->status == OBSNSTAT_GOOD) && (msg_datapmt->mode_auto))
      {
        char new_status = check_env_obsn_auto(objs->env_objs, &msg_datapmt->targ_ra, &msg_datapmt->targ_dec);
        if (new_status < 0)
        {
          act_log_error(act_log_msg("An error occurred while checking environment for auto DATAPMT viability."));
          new_status = OBSNSTAT_ERR_WAIT;
        }
        msg_datapmt->status = new_status;
      }
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send DATAPMT response."));
      break;
    }
    case MT_CCD_CAP:
    {
      if (msgbuf.content.msg_ccdcap.dataccd_stage != DATACCD_ENVIRON)
      {
        msgbuf.content.msg_ccdcap.dataccd_stage = DATACCD_ENVIRON;
        if (net_send(source, &msgbuf) != sizeof(msgbuf))
          act_log_error(act_log_msg("Failed to send response to CCDCAP request."));
      }
      break;
    }
    case MT_DATA_CCD:
    {
      struct act_msg_dataccd *msg_dataccd = &msgbuf.content.msg_dataccd;
      if ((msg_dataccd->status == OBSNSTAT_GOOD) && (msg_dataccd->mode_auto))
      {
        char new_status = check_env_obsn_auto(objs->env_objs, &msg_dataccd->targ_ra, &msg_dataccd->targ_dec);
        if (new_status < 0)
        {
          act_log_error(act_log_msg("An error occurred while checking environment for auto DATACCD viability."));
          new_status = OBSNSTAT_ERR_WAIT;
        }
        msg_dataccd->status = new_status;
      }
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send DATACCD response."));
      break;
    }
    default:
    {
      if (msgbuf.mtype >= MT_INVAL)
        act_log_error(act_log_msg("Invalid message type received."));
    }
  }
  int new_cond = g_io_channel_get_buffer_condition (source);
  if ((new_cond & G_IO_IN) != 0)
    return read_net_message(source, new_cond, net_read_data);
  return TRUE;
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

/** \brief Main programme loop to update and disseminate weather data.
 * \return return TRUE (call function again).
 */
gboolean timeout_env_update(gpointer user_data)
{
/*  act_log_debug(act_log_msg("Updating environmental information."));
  struct env_indicators *objs = (struct env_indicators *)user_data;
  struct act_msg msgbuf;
  msgbuf.mtype = MT_ENVIRON;
  struct act_msg_environ *tmp_env_msg = &msgbuf.content.msg_environ;
  char src_status = update_environ(tmp_env_msg, G_swasp_http_handle, G_salt_http_handle, G_gjd, G_sidt);
  update_env_indicators(objs, src_status, &G_environ_msg, tmp_env_msg);
  if ((G_environ_msg.status_active != tmp_env_msg->status_active) || (G_environ_msg.weath_ok != tmp_env_msg->weath_ok))
  {
    if (!objs->mode_change_prompted)
    {
      act_log_debug(act_log_msg("Old status active: %hhu %hhu", G_environ_msg.status_active, G_environ_msg.weath_ok));
      prompt_mode_change(objs->evb_active_mode, &objs->mode_change_prompted, tmp_env_msg->status_active, tmp_env_msg->weath_ok, &G_environ_msg, G_netsock_fd);
    }
    tmp_env_msg->status_active = G_environ_msg.status_active;
    tmp_env_msg->weath_ok = G_environ_msg.weath_ok;
    act_log_debug(act_log_msg("New status active: %hhu %hhu", G_environ_msg.status_active, G_environ_msg.weath_ok));
  }
  memcpy(&G_environ_msg, tmp_env_msg, sizeof(struct act_msg_environ));*/
  act_log_debug(act_log_msg("Environment update"));
  struct formobjects *objs = (struct formobjects *)user_data;
  update_environ(objs->env_objs, TIMEOUT_ENV_UPDATE_PERIOD);
  struct act_msg envmsg;
  envmsg.mtype = MT_ENVIRON;
  memcpy(&envmsg.content.msg_environ, &objs->env_objs->all_env, sizeof(struct act_msg_environ));
  if (net_send(objs->net_chan, &envmsg) != sizeof(struct act_msg))
    act_log_error(act_log_msg("Error sending environment service message."));
  return TRUE;
}

/** \brief Main programme loop to check for messages.
 * \param user_data Pointer to struct formobjects which contains all data relevant to this function.
 * \return Return FALSE if main loop should not be called again, otherwise always return TRUE.
 *
 * Tasks:
 *  -# Process all incoming messages and request GUI socket if necessary.
 */
gboolean timeout_check_gui(gpointer user_data)
{
  struct formobjects *objs = (struct formobjects *)user_data;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL;
  if (main_embedded)
    return TRUE;
  struct act_msg guimsg;
  guimsg.mtype = MT_GUISOCK;
  memset(&guimsg.content.msg_guisock, 0, sizeof(struct act_msg_guisock));
  if (net_send(objs->net_chan, &guimsg) != sizeof(struct act_msg))
    act_log_error(act_log_msg("Error sending GUI socket request message."));
  return TRUE;
}

/** \brief Main function.
 * \param argc Number of command-line arguments.
 * \param argv Array of command-line arguments.
 * \return 0 upon success, 2 if command-line arguments incorrect, 1 for other errors.
 * \todo Do gtk_init with arguments first, then own parsing.
 *
 * Tasks:
 *  -# Parse command-line arguments.
 *  -# Parse configuration files.
 *  -# Start weather data grabber scripts.
 *  -# Connect to main controller.
 *  -# Create GUI.
 *  -# Start main programme loop.
 */
int main (int argc, char **argv)
{
  act_log_open();
  act_log_normal(act_log_msg("Starting"));
  
  const char *host, *port;
  gtk_init(&argc, &argv);
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_str *sqlconfigarg = arg_str0("s", "sqlconfighost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
  struct arg_end *endargs = arg_end(10);
  void* argtable[] = {addrarg, portarg, sqlconfigarg, endargs};
  if (arg_nullcheck(argtable) != 0)
    act_log_error(act_log_msg("Argument parsing error: insufficient memory."));
  int argparse_errors = arg_parse(argc,argv,argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr,endargs,argv[0]);
    exit(EXIT_FAILURE);
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));

  struct formobjects objs;
  memset(&objs, 0, sizeof(struct formobjects));
  
  objs.net_chan = setup_net(host, port);
  if (objs.net_chan == NULL)
  {
    act_log_error(act_log_msg("Error setting up network connection."));
    return 1;
  }
  
  objs.box_main = gtk_event_box_new();
  objs.env_objs = init_environ(objs.box_main);
  if (objs.env_objs == NULL)
  {
    act_log_error(act_log_msg("Failed to create environ objects."));
    return 1;
  }
  
  act_log_normal(act_log_msg("Entering main loop."));
  int to_env_upd_id, to_check_gui_id;
  if (TIMEOUT_ENV_UPDATE_PERIOD % 1000 == 0)
    to_env_upd_id = g_timeout_add_seconds(TIMEOUT_ENV_UPDATE_PERIOD/1000, timeout_env_update, &objs);
  else
    to_env_upd_id = g_timeout_add(TIMEOUT_ENV_UPDATE_PERIOD, timeout_env_update, &objs);
  if (TIMEOUT_CHECK_GUI_PERIOD % 1000 == 0)
    to_check_gui_id = g_timeout_add_seconds(TIMEOUT_CHECK_GUI_PERIOD/1000, timeout_check_gui, &objs);
  else
    to_check_gui_id = g_timeout_add(TIMEOUT_CHECK_GUI_PERIOD, timeout_check_gui, &objs);
  int net_watch_id = g_io_add_watch(objs.net_chan, G_IO_IN, read_net_message, &objs);
  gtk_main();
  g_source_remove(to_env_upd_id);
  g_source_remove(to_check_gui_id);
  g_source_remove(net_watch_id);
  g_io_channel_unref(objs.net_chan);
  finalise_environ(objs.env_objs);
  gtk_widget_destroy(objs.box_main);
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}
