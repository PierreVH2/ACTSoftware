// #include <sys/stat.h>
// #include <sys/types.h>
#include <sys/socket.h>
// #include <netinet/in.h>
// #include <netdb.h>
// #include <arpa/inet.h>
// #include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <act_log.h>

#include "net_basic.h"
#include "subprogrammes.h"

void progopts_start(gpointer user_data);
void progopts_close(gpointer user_data);
void progopts_kill(gpointer user_data);

/** \brief Starts specified subprogramme
 *
 * \param sockfd
 *   File descriptor for UNIX socket on which controller is listening for incoming network connections.
 *
 * \param programe
 *   Point to <struct act_prog> that contains all information regarding the corresponding subprogramme.
 *
 * \param port_str
 *   C string containing the port number or port name on which controller is listening for incoming connections.
 *
 * \param config_file
 *   C string containing filename of global configuration file, preferably the global filesystem path to
 *   the file.
 *
 * \return TRUE on success, FALSE on failure.
 *
 * Algorithm:
 *   -# Fork process (duplicates controller, yields child and parent).
 *     - Child (fork() returns non-zero):
 *       -# Exec sub-programme (replaces duplicate of controller with specified subprogramme)
 *         - Child executable can be in user's $PATH environment variable, in which case just call the executable
 *         - Path the child executable is specified, in which case concatenate "path" and "executable" and execute.
 *     - Parent (frok() return 0):
 *       -# Wait for network connection from child
 *       -# Start handshake with child by sending "client capabilities request" message.
 *
 * \todo Make the listen fd blocking before accept and non-blocking after
 */
char start_prog(struct act_prog* prog)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return FALSE;
  }
  act_log_normal(act_log_msg("Test"));
  if ((prog->status != PROGSTAT_STOPPED) && (prog->status != PROGSTAT_KILLED))
  {
    act_log_normal(act_log_msg("%s is already running."));
    return FALSE;
  }
    
  act_log_normal(act_log_msg("Starting programme %s.", prog->name));
  socklen_t sin_size;
  int new_fd = -1;
  struct sockaddr_storage their_addr;
  char i;

  pid_t childpid = fork();
  if (childpid < 0)
  {
    act_log_error(act_log_msg("Error forking process for programme %s - %s", prog->name, strerror(errno)));
    return FALSE;
  }

  if (childpid == 0)
  {
    if (execlp(prog->executable,prog->executable,"-a",prog->hostname,"-p",prog->portname,"-s",prog->sqlconfighost,(char *)0) == -1)
    {
      act_log_error(act_log_msg("Error executing \"%s\" (programme %s) - %s", prog->executable,prog->name, strerror(errno)));
      exit(1);
    }
  }

  prog->pid = childpid;
  sin_size = sizeof(their_addr);
  for (i=0; i<10 && new_fd<0; i++)
  {
    new_fd = accept(*(prog->listen_sockfd), (struct sockaddr *)&their_addr, &sin_size);
    sleep(1);
  }
  if (new_fd == -1)
  {
    act_log_error(act_log_msg("Timed out while waiting to accept network connection from programme %s - %s", prog->name, strerror(errno)));
    kill(childpid, SIGTERM);
    return FALSE;
  }
  act_log_normal(act_log_msg("Child connected on socket %d", new_fd));
  prog->sockfd = new_fd;
  fcntl(new_fd, F_SETOWN, getpid());
  int oflags = fcntl(new_fd, F_GETFL);
  fcntl(new_fd, F_SETFL, oflags | FASYNC);
  prog_set_status(prog, PROGSTAT_STARTUP);

  if ((prog->guicoords[0] != prog->guicoords[1]) && (prog->guicoords[2] != prog->guicoords[3]))
  {
    act_log_normal(act_log_msg("Creating GUI components for %s", prog->name));
    GtkWidget* frm_temp = gtk_frame_new(prog->name);
    gtk_table_attach(GTK_TABLE(prog->box_progs), frm_temp, prog->guicoords[0], prog->guicoords[1], prog->guicoords[2], prog->guicoords[3], GTK_EXPAND|GTK_FILL, GTK_EXPAND|GTK_FILL, 5, 5);
    prog->socket = gtk_socket_new();
    gtk_container_add(GTK_CONTAINER(frm_temp),prog->socket);
    g_signal_connect(G_OBJECT(prog->socket),"plug-removed",G_CALLBACK(plug_removed), prog);
    g_signal_connect(G_OBJECT(prog->socket),"plug-added",G_CALLBACK(plug_added),NULL);
  }
  else
  {
    act_log_normal(act_log_msg("No GUI region specified for %s. Not creating GUI components.", prog->name));
    if (prog->socket != NULL)
    {
      gtk_widget_destroy(prog->socket);
      prog->socket = NULL;
    }
  }

  act_log_normal(act_log_msg("Sending capabilities request message to %s", prog->name));
  struct act_msg msgbuf;
  memset(&msgbuf, 0, sizeof(struct act_msg));
  msgbuf.mtype = MT_CAP;
  if (send(new_fd, &msgbuf, sizeof(struct act_msg), 0) < 0)
    act_log_error(act_log_msg("Could not send capabilities request message to %s - %s", prog->name, strerror(errno)));
  else
    prog->last_stat_timer = 0;
  act_log_normal(act_log_msg("Done starting %s", prog->name));
  
  return TRUE;
}

char close_prog(struct act_prog *prog)
{
  prog_set_status(prog, PROGSTAT_STOPPING);
  struct act_msg buf;
  buf.mtype = MT_QUIT;
  /// TODO: Implement auto quit
  buf.content.msg_quit.mode_auto = FALSE;
  if (!act_send(prog, &buf))
  {
    act_log_error(act_log_msg("Could not send quit signal to %s - setting state to \"RUNNING\".", prog->name));
    prog_set_status(prog, PROGSTAT_RUNNING);
    return FALSE;
  }
  return TRUE;
}

void prog_active_change(struct act_prog *prog, unsigned char status_active)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  act_log_debug(act_log_msg("%s status active: %hhu %hhu %hhu", prog->name, status_active, prog->active_time, prog->status));
  if (status_active == 0)
  {
//     act_log_debug(act_log_msg("Current active status not available. Not doing programme auto start/stop."));
    return;
  }
  if (prog->active_time == 0)
    return;
  if (prog->active_time & status_active)
  {
    if ((prog->status != PROGSTAT_STOPPED) && (prog->status != PROGSTAT_KILLED))
      return;
    if (start_prog(prog) == 0)
      act_log_error(act_log_msg("Error starting programme %s.", prog->name));
    else
      act_log_normal(act_log_msg("Programme %s successfully started.", prog->name));
    return;
  }
  if ((prog->status != PROGSTAT_STARTUP) && (prog->status != PROGSTAT_RUNNING))
    return;
  if (close_prog(prog) == 0)
  {
    act_log_error(act_log_msg("Error closing programme %s - Killing.", prog->name));
    prog_set_status(prog, PROGSTAT_KILLED);
    close(prog->sockfd);
    kill(prog->pid, SIGTERM);
  }
  else
    act_log_normal(act_log_msg("Programme %s successfully closed.", prog->name));
}

/** \brief Display subprogramme information upon user request.
 *
 * \param user_data
 *   Pointer to <struct act_prog> which contains the details about the correspondig subprogramme.
 *
 * \return (void)
 *
 * Algorithm:
 *   -# Create form for popup window which contains labels and buttons displaying information about the
 *      and using which the programme can be started/stopped.
 */
void prog_button(GtkWidget *btn_progopts, gpointer user_data)
{
  // ??? implement log display
  struct act_prog *prog = (struct act_prog *)user_data;
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Program Information", GTK_WINDOW(gtk_widget_get_toplevel(btn_progopts)), 0, GTK_STOCK_OK, GTK_RESPONSE_OK, NULL);
  gtk_window_set_default_size(GTK_WINDOW(dialog),480,240);
  GtkWidget *box_progopts = gtk_table_new(0,6,FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),box_progopts, TRUE, TRUE, 3);
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new("Name"), 0,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new(prog->name), 3,6,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new("Version"), 0,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new(prog->caps.version_str), 3,6,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new("Status"),0,3,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  char stat_str[20];
  switch(prog->status)
  {
    case PROGSTAT_STARTUP:
      snprintf(stat_str, sizeof(stat_str), "Starting up");
      break;
    case PROGSTAT_RUNNING:
      snprintf(stat_str, sizeof(stat_str), "Running");
      break;
    case PROGSTAT_STOPPING:
      snprintf(stat_str, sizeof(stat_str), "Shutting down");
      break;
    case PROGSTAT_STOPPED:
      snprintf(stat_str, sizeof(stat_str), "Not running");
      break;
    case PROGSTAT_KILLED:
      snprintf(stat_str, sizeof(stat_str), "Killed");
      break;
    case PROGSTAT_ERR_RESTART:
      snprintf(stat_str, sizeof(stat_str), "Critical error - restart programme");
      break;
    case PROGSTAT_ERR_CRIT:
      snprintf(stat_str, sizeof(stat_str), "Critical error - all stop");
      break;
    default:
      snprintf(stat_str, sizeof(stat_str), "Unknown - %d", prog->status);
  }
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new(stat_str), 3,6,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_progopts),gtk_label_new("LOG N/A"), 0,6,3,4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_close = gtk_button_new_with_label("Close");
  gtk_table_attach(GTK_TABLE(box_progopts),btn_close, 0,2,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_kill = gtk_button_new_with_label("Kill");
  gtk_table_attach(GTK_TABLE(box_progopts),btn_kill, 2,4,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *btn_start = gtk_button_new_with_label("Start");
  gtk_table_attach(GTK_TABLE(box_progopts),btn_start, 4,6,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);

  g_signal_connect_swapped(G_OBJECT(btn_close),"clicked",G_CALLBACK(progopts_close),prog);
  g_signal_connect_swapped(G_OBJECT(btn_kill),"clicked",G_CALLBACK(progopts_kill),prog);
  g_signal_connect_swapped(G_OBJECT(btn_start),"clicked",G_CALLBACK(progopts_start),prog);
  g_signal_connect_swapped(G_OBJECT(dialog),"response",G_CALLBACK(gtk_widget_destroy),dialog);

  gtk_widget_show_all(dialog);
}

/** \brief Callback for when subprogramme socket is added to a controller plug.
 *
 * \param socket
 *   GtkSocket of controller to which subprogramme's GtkPlug will connect.
 *
 * \return (void)
 */
void plug_added(GtkSocket *socket)
{
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(socket));
  gtk_widget_show_all(parent);
}

/** \brief Callback for when subprogramme plug is removed from its controller socket.
 *
 * \param socket
 *   GtkSocket of controller from which subprogramme's GtkPlug has disconnected
 *
 * \param user_data
 *   Pointer to <struct act_prog> which contains the details about the correspondig subprogramme.
 *
 * \return (void)
 *
 * When a GtkPlug disconnects from its GtkSocket, the GtkSocket gets destroyed. This function recreates the 
 * GtkSocket (owned by the controller) so that when the subprogramme has something to connect to. This mostly
 * happens when a subprogramme exits (either normally or abnormally).
 *
 * GtkSocket is hidden in case programme exited normally. GtkSocket will only be made visible when a GtkPlug
 * connects to it.
 */
gboolean plug_removed(GtkSocket *socket, gpointer user_data)
{
  struct act_prog *prog = (struct act_prog *)user_data;
  
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(socket));
  if (parent == NULL)
  {
    act_log_error(act_log_msg("Plug removed for program %s but cannot find socket's parent. Not recreating socket.", prog->name));
    return FALSE;
  }
  gtk_container_remove(GTK_CONTAINER(parent),GTK_WIDGET(socket));

  if ((prog->status == PROGSTAT_RUNNING) || (prog->status == PROGSTAT_STARTUP))
  {
    prog->socket = gtk_socket_new();
    g_signal_connect(G_OBJECT(prog->socket),"plug-removed",G_CALLBACK(plug_removed), prog);
    g_signal_connect(G_OBJECT(prog->socket),"plug-added",G_CALLBACK(plug_added),NULL);
    gtk_container_add(GTK_CONTAINER(parent),prog->socket);
    gtk_widget_hide_all(parent);
  }
  
  if ((prog->status == PROGSTAT_STOPPING) || (prog->status == PROGSTAT_STOPPED))
  {
    gtk_widget_destroy(parent);
  }
  
  return FALSE;
}

char prog_set_status(struct act_prog *prog, unsigned char new_stat)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  if (prog->status == new_stat)
    return 0;
  
  prog->status = new_stat;
  GdkColor new_col;
  switch (prog->status)
  {
    case PROGSTAT_STARTUP:
      gdk_color_parse("#00AAAA", &new_col);
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
    case PROGSTAT_RUNNING:
      gdk_color_parse("#00AA00", &new_col);
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
    case PROGSTAT_STOPPING:
      gdk_color_parse("#0000AA", &new_col);
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
    case PROGSTAT_STOPPED:
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, NULL);
      break;
    case PROGSTAT_KILLED:
      gdk_color_parse("#AA0000", &new_col);
      act_log_normal(act_log_msg("Strange, %s reported status \"KILLED\", but if it has been killed it cannot send messages.", prog->name));
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
    case PROGSTAT_ERR_RESTART:
      gdk_color_parse("#AA0000", &new_col);
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
    case PROGSTAT_ERR_CRIT:
      gdk_color_parse("#AA0000", &new_col);
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
    default:
      act_log_error(act_log_msg("Programme %s has invalid status."));
      gdk_color_parse("#AAAA00", &new_col);
      gtk_widget_modify_bg(prog->button, GTK_STATE_NORMAL, &new_col);
      break;
  }
  return 1;
}

/** \brief Start subprogramme upon user request.
 * 
 * \param user_data
 *   Pointer to <struct act_prog> which contains the details about the subprogramme that must be stopped.
 *
 * \return (void)
 *
 * Algorithm:
 *   -# If the programme is already, display message and exit.
 *   -# Start programme, setting all internal variables and handling errors as necessary.
 */
void progopts_start(gpointer user_data)
{
  struct act_prog *prog = (struct act_prog *)user_data;
  if ((prog->status == PROGSTAT_STARTUP) || (prog->status == PROGSTAT_RUNNING))
  {
    act_log_normal(act_log_msg("User requested program %s to be started, but it seems to be already running.", prog->name));
    return;
  }
  
  if (!start_prog(prog))
  {
    act_log_error(act_log_msg("Failed to start %s on user request.", prog->name));
    return;
  }
}

/** \brief Close subprogramme upon user request.
 * 
 * \param user_data
 *   Pointer to <struct act_prog> which contains the details about the subprogramme that must be stopped.
 *
 * \return (void)
 *
 * Algorithm:
 *   -# If the programme isn't running, display error message and return.
 *   -# Send quit message to subprogramme.
 */
void progopts_close(gpointer user_data)
{
  struct act_prog *prog = (struct act_prog *)user_data;
  if ((prog->status != PROGSTAT_RUNNING) && (prog->status != PROGSTAT_STARTUP))
  {
    act_log_normal(act_log_msg("User attempted to close subprogram %s, but it isn't running.", prog->name));
    return;
  }
  if (!close_prog(prog))
    act_log_error(act_log_msg("Faild to close %s on user request.", prog->name));
}

/** \brief Kill (force exit) subprogramme upon user request.
 * 
 * \param user_data
 *   Pointer to <struct act_prog> which contains the details about the subprogramme that must be stopped.
 *
 * \return (void)
 *
 * Algorithm:
 *   -# Set internal variables accordingly, close net connection to subprogramme.
 *   -# Kill subprogramme.
 */
void progopts_kill(gpointer user_data)
{
  struct act_prog *prog = (struct act_prog *)user_data;
  prog_set_status(prog, PROGSTAT_KILLED);
  close(prog->sockfd);
  prog->sockfd = 0;
  kill(prog->pid, SIGTERM);
}
