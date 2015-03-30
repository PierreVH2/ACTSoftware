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
#include <errno.h>
#include <math.h>
#include <argtable2.h>
#include <gtk/gtk.h>
#include <act_ipc.h>
#include <act_site.h>
#include <act_log.h>
#include <act_positastro.h>
#include "env_weather.h"

#define TIMEOUT_CHECK_GUI_PERIOD  5000

struct guicheck_to_objs
{
  GtkWidget *env_weather;
  GIOChannel *net_chan;
};

/** \brief Callback when plug (parent of all GTK objects) destroyed, adds a reference to plug's child so it (and it's
 *  \brief children) won't be deleted.
 * \param plug GTK plug containing all on-screen objects.
 * \param user_data The parent of all objects contained within plug.
 * \return (void)
 */
void destroy_plug(GtkWidget *plug, gpointer env_weather)
{
  gtk_container_remove(GTK_CONTAINER(plug), GTK_WIDGET(env_weather));
}

int net_send(GIOChannel *channel, struct act_msg *msg)
{
  gsize num_bytes;
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
gboolean read_net_message (GIOChannel *source, GIOCondition condition, gpointer env_weather)
{
  (void)condition;
//   struct formobjects *objs = (struct formobjects *)net_read_data;
  struct act_msg msgbuf;
  gsize num_bytes;
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
      cap_msg->service_needs = SERVICE_TIME | SERVICE_COORD;
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
      msgbuf.content.msg_stat.status = PROGSTAT_RUNNING;
      if (net_send(source, &msgbuf) != sizeof(msgbuf))
        act_log_error(act_log_msg("Failed to send status response message."));
      break;
    }
    case MT_GUISOCK:
    {
      if (msgbuf.content.msg_guisock.gui_socket <= 0)
      {
        act_log_debug(act_log_msg("Received invalid GUI socket ID (%d)", msgbuf.content.msg_guisock.gui_socket));
        break;
      }
      GtkWidget *plug = gtk_widget_get_parent(GTK_WIDGET(env_weather));
      if (plug != NULL)
      {
        gtk_container_remove(GTK_CONTAINER(plug), GTK_WIDGET(env_weather));
        gtk_widget_destroy(plug);
      }
      plug = gtk_plug_new(msgbuf.content.msg_guisock.gui_socket);
      act_log_normal(act_log_msg("Received GUI socket (%d).", msgbuf.content.msg_guisock.gui_socket));
      gtk_container_add(GTK_CONTAINER(plug),GTK_WIDGET(env_weather));
      g_signal_connect(G_OBJECT(plug),"destroy",G_CALLBACK(destroy_plug),GTK_WIDGET(env_weather));
      gtk_widget_show_all(plug);
      break;
    }
    case MT_COORD:
    {
      env_weather_process_msg(GTK_WIDGET(env_weather), &msgbuf);
      break;
    }
    case MT_TIME:
    {
      env_weather_process_msg(GTK_WIDGET(env_weather), &msgbuf);
      break;
    }
    case MT_ENVIRON:
    {
      act_log_debug(act_log_msg("Strange: Received an ENVIRONMENT message. Responding with latest environment message."));
      env_weather_process_msg(GTK_WIDGET(env_weather), &msgbuf);
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
      env_weather_process_msg(GTK_WIDGET(env_weather), &msgbuf);
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
      env_weather_process_msg(GTK_WIDGET(env_weather), &msgbuf);
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
      env_weather_process_msg(GTK_WIDGET(env_weather), &msgbuf);
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
    return read_net_message(source, new_cond, env_weather);
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

/** \brief Main programme loop to check for messages.
 * \param user_data Pointer to struct formobjects which contains all data relevant to this function.
 * \return Return FALSE if main loop should not be called again, otherwise always return TRUE.
 *
 * Tasks:
 *  -# Process all incoming messages and request GUI socket if necessary.
 */
gboolean timeout_check_gui(gpointer check_gui_objs)
{
  GtkWidget *env_weather = ((struct guicheck_to_objs *)check_gui_objs)->env_weather;
  GIOChannel *net_chan = ((struct guicheck_to_objs *)check_gui_objs)->net_chan;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(env_weather)) != NULL;
  if (main_embedded)
    return TRUE;
  struct act_msg guimsg;
  memset(&guimsg, 0, sizeof(struct act_msg));
  guimsg.mtype = MT_GUISOCK;
  if (net_send(net_chan, &guimsg) != sizeof(struct act_msg))
    act_log_error(act_log_msg("Error sending GUI socket request message."));
  return TRUE;
}

void update_weather(GtkWidget *env_weather, gpointer net_chan)
{
  struct act_msg envmsg;
  memset(&envmsg, 0, sizeof(struct act_msg));
  envmsg.mtype = MT_ENVIRON;
  env_weather_process_msg(env_weather, &envmsg);
  if (net_send(net_chan, &envmsg) != sizeof(struct act_msg))
    act_log_error(act_log_msg("Error sending environment message."));
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

  GIOChannel *net_chan = setup_net(host, port);
  if (net_chan == NULL)
  {
    act_log_error(act_log_msg("Error setting up network connection."));
    return 1;
  }
  
  GtkWidget *env_weather = env_weather_new();
  g_object_ref(G_OBJECT(env_weather));
  struct guicheck_to_objs guicheck_objs = { .env_weather=env_weather, .net_chan=net_chan};
  g_object_ref(env_weather);
  g_io_channel_ref(net_chan);

  g_signal_connect(G_OBJECT(env_weather), "env-weather-update", G_CALLBACK(update_weather), net_chan);
  int net_watch_id = g_io_add_watch(net_chan, G_IO_IN, read_net_message, env_weather);
  int gui_check_to_id = g_timeout_add_seconds(TIMEOUT_CHECK_GUI_PERIOD/1000, timeout_check_gui, &guicheck_objs);
  
  act_log_normal(act_log_msg("Entering main loop."));
//   gtk_container_add(GTK_CONTAINER(ENV_WEATHER(env_weather)->evb_swasp_stat), gtk_label_new("TEST"));
//   gtk_label_set_text(GTK_LABEL(ENV_WEATHER(env_weather)->lbl_salt_stat), "TEST");
  gtk_main();
  g_source_remove(net_watch_id);
  g_source_remove(gui_check_to_id);
  g_io_channel_unref(guicheck_objs.net_chan);
  g_object_unref(G_OBJECT(guicheck_objs.env_weather));
  guicheck_objs.net_chan = NULL;
  guicheck_objs.env_weather = NULL;
  g_io_channel_unref(net_chan);
  g_object_unref(G_OBJECT(env_weather));
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}
