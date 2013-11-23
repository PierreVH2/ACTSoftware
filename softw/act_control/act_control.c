/*!
 * \file act_control.c
 * \brief ACT Control source file.
 * \author Pierre van Heerden
 * \todo Add/remove programmes on-the-fly
 * \todo Remote access
 * \todo Implement proper treatment of progstat
 *
 * ACT main controller. Manages all programmes that form part of the ACT control software suite.
 */

#if !defined(MAJOR_VER)
 #pragma message "WARNING: MAJOR_VER not defined for " __FILE__
 #define MAJOR_VER 2
#endif

#if !defined(MINOR_VER)
 #pragma message "WARNING: MINOR_VER not defined for " __FILE__
 #define MINOR_VER -1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <gtk/gtk.h>
#include <argtable2.h>
#include <act_ipc.h>
#include <act_log.h>

#include "act_control_config.h"
#include "subprogrammes.h"
#include "net_dataccd.h"
#include "net_datapmt.h"
#include "net_targset.h"
#include "net_genl.h"
#include "net_basic.h"


//! Main programme loop period.
#define TIMEOUT_PERIOD_MS     1000
//! Time between consecutive status requests to programmes
#define STAT_REQ_TIMEOUT_MS   10000
//! Timeout for status responses - used to see if programmes are still responding
#define STAT_RESP_TIMEOUT_MS  60000
//! Time between consecutive checks to see whether programme has closed gracefully
#define QUIT_CHECK_TIMEOUT_S  1
//! Number of times to check whether a programme has exited gracefully
#define QUIT_CHECK_NUM        10

struct addprog_objects
{
  GtkWidget *box_progs, *box_stat;
};

/** \name Global variables */
//! Hostname string
char G_hostname[30];
//! Name of port to listen on
const char *G_portname;
//! Hostname of SQL server
const char *G_sqlserver;
//! Number of children managed by act_control.
short G_num_progs;
//! File descriptor for socket on which act_control listens for incoming connections.
int G_listen_sockfd;
//! System's current active time (ACTIVE_TIME_DAY, ACTIVE_TIME_NIGHT, neither or both)
unsigned char G_status_active;
//! Array of children managed by act_control.
struct act_prog* G_progs;


gboolean reg_check_close(gpointer user_data)
{
  int i;
  char all_closed = TRUE;
  int ret;
  for (i=0; i<G_num_progs; i++)
  {
    if ((G_progs[i].status == PROGSTAT_STOPPED) || (G_progs[i].status == PROGSTAT_KILLED))
      continue;
    ret = kill(G_progs[i].pid,0);
    if (ret != 0)
      prog_set_status(&G_progs[i], PROGSTAT_STOPPED);
    else
      all_closed = FALSE;
  }
  if (all_closed)
  {
    gtk_widget_destroy(user_data);
    gtk_main_quit();
    return FALSE;
  }
  return TRUE;
}

void force_exit(gpointer wnd_main)
{
  int i;
  for (i=0; i<G_num_progs; i++)
  {
    act_log_normal(act_log_msg("Killing %s.", G_progs[i].name));
    kill(G_progs[i].pid, SIGTERM);
  }
  gtk_widget_destroy(wnd_main);
  gtk_main_quit();
}

void confirm_quit(GtkWidget *wnd_main, int timeout_id)
{
  GtkWidget* confirm_exit = gtk_message_dialog_new (GTK_WINDOW(wnd_main), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO, "Really quit?");
  if (gtk_dialog_run (GTK_DIALOG (confirm_exit)) == GTK_RESPONSE_NO)
  {
    gtk_widget_destroy (GTK_WIDGET(confirm_exit));
    return;
  }
  gtk_widget_destroy (GTK_WIDGET(confirm_exit));
  g_source_remove(timeout_id);
  
  GtkWidget* wait_exit = gtk_dialog_new_with_buttons ("Closing", GTK_WINDOW(wnd_main), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Force quit", GTK_RESPONSE_CLOSE, NULL);
  gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(wait_exit))), gtk_label_new("Attempting to close programmes gracefully"));
  g_signal_connect_swapped(G_OBJECT(wait_exit), "response", G_CALLBACK(force_exit), wnd_main);
  if (TIMEOUT_PERIOD_MS % 1000 == 0)
    g_timeout_add_seconds(TIMEOUT_PERIOD_MS/1000, reg_check_close, wnd_main);
  else
    g_timeout_add(TIMEOUT_PERIOD_MS, reg_check_close, wnd_main);
  gtk_widget_show_all(wait_exit);
  
  act_log_normal(act_log_msg("Attempting to close programmes gracefully."));
  signal(SIGCHLD, SIG_IGN);
  close(G_listen_sockfd);
  G_listen_sockfd = 0;
  int i;
  for (i=0; i<G_num_progs; i++)
  {
    if ((G_progs[i].status == PROGSTAT_STOPPED) || (G_progs[i].status == PROGSTAT_KILLED))
      continue;
    if (G_progs[i].status == PROGSTAT_ERR_RESTART)
    {
      act_log_normal(act_log_msg("Programme %s encountered an error which requires a restart. Killing instead.", G_progs[i].name));
      kill(G_progs[i].pid, SIGTERM);
      continue;
    }
    if (G_progs[i].status == PROGSTAT_ERR_CRIT)
    {
      act_log_normal(act_log_msg("Programme %s encountered a critical error. Will not stop this programme and await assistance instead.", G_progs[i].name));
      continue;
    }
    if (!close_prog(&G_progs[i]))
    {
      act_log_error(act_log_msg("Error sending quit command to %s; attempting to kill it.", G_progs[i].name));
      kill(G_progs[i].pid, SIGTERM);
    }
  }
}

/** \brief Signal handler for Quit button
  *
  * \param btn_quit
  *   "Quit" button on form.
  * \param wnd_main
  *   Pointer to main window.
 */
void quit_pressed(GtkWidget *btn_quit, gpointer user_data)
{
  GtkWidget *wnd_main = gtk_widget_get_toplevel(btn_quit);
  confirm_quit(wnd_main, (int)user_data);
}

gboolean window_close(GtkWidget *wnd_main, GdkEvent *event, gpointer user_data)
{
  (void)event;
  confirm_quit(wnd_main, (int)user_data);
  return TRUE;
}

void addprog_response(GtkWidget *dialog, int response, gpointer user_data)
{
  if (user_data == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    gtk_widget_destroy(dialog);
    return;
  }
  struct act_prog *new_prog = (struct act_prog*)user_data;
  if (response != GTK_RESPONSE_OK)
  {
    gtk_widget_destroy(new_prog->button);
    free(new_prog);
    gtk_widget_destroy(dialog);
    return;
  }
  
  // ??? Check GUI coords
  
  struct act_prog *new_prog_array = malloc((G_num_progs+1)*sizeof(struct act_prog));
  memcpy(new_prog_array,G_progs,G_num_progs*sizeof(struct act_prog));
  memcpy(&new_prog_array[G_num_progs], new_prog, sizeof(struct act_prog));
  free(G_progs);
  free(new_prog);
  G_progs = new_prog_array;

  gtk_button_set_label(GTK_BUTTON(G_progs[G_num_progs].button), G_progs[G_num_progs].name);
  gtk_widget_show(G_progs[G_num_progs].button);
  g_signal_connect(G_OBJECT(G_progs[G_num_progs].button),"clicked",G_CALLBACK(prog_button), &G_progs[G_num_progs]);
  G_num_progs++;
  
  gtk_widget_destroy(dialog);
}

void addprog_entry_change(GtkWidget *entry, gpointer user_data)
{
  char *value = (char *)user_data;
  sprintf(value, "%s", gtk_entry_get_text(GTK_ENTRY(entry)));
}

void addprog_guicoord_change(GtkWidget *spn_guicoord, gpointer user_data)
{
  unsigned char *guicoord = (unsigned char *)user_data;
  *guicoord = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spn_guicoord));
}

void addprog_active_change(GtkWidget *cmb_active, gpointer user_data)
{
  unsigned char *active_time = (unsigned char *)user_data;
  *active_time = gtk_combo_box_get_active(GTK_COMBO_BOX(cmb_active));
}

void add_prog(GtkWidget *btn_add_prog, gpointer user_data)
{
  struct addprog_objects *addprog_objs = (struct addprog_objects*)user_data;
  struct act_prog *new_prog = malloc(sizeof(struct act_prog));
  memset(new_prog, 0, sizeof(struct act_prog));
  new_prog->listen_sockfd = &G_listen_sockfd;
  new_prog->hostname = G_hostname;
  new_prog->portname = G_portname;
  new_prog->sqlconfighost = G_sqlserver;
  new_prog->status_active = &G_status_active;
  new_prog->box_progs = addprog_objs->box_progs;
  new_prog->button = gtk_button_new_with_label("");
  gtk_box_pack_start(GTK_BOX(addprog_objs->box_stat),new_prog->button,TRUE,TRUE,0);
  
  GtkWidget *dialog = gtk_dialog_new_with_buttons("Program Information", GTK_WINDOW(gtk_widget_get_toplevel(btn_add_prog)), 0, GTK_STOCK_OK, GTK_RESPONSE_OK, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
//   gtk_window_set_default_size(GTK_WINDOW(dialog),480,240);
  GtkWidget *box_addprog = gtk_table_new(0,2,FALSE);
  gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox),box_addprog, TRUE, TRUE, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("Name"), 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *ent_progname = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(ent_progname),sizeof(new_prog->name)-1);
  g_signal_connect(G_OBJECT(ent_progname),"changed",G_CALLBACK(addprog_entry_change),new_prog->name);
  gtk_table_attach(GTK_TABLE(box_addprog),ent_progname, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("Executable"), 0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *ent_progexec = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(ent_progexec),sizeof(new_prog->executable)-1);
  g_signal_connect(G_OBJECT(ent_progexec),"changed",G_CALLBACK(addprog_entry_change),new_prog->executable);
  gtk_table_attach(GTK_TABLE(box_addprog),ent_progexec, 1,2,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("Host"), 0,1,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *ent_proghost = gtk_entry_new();
  gtk_entry_set_max_length(GTK_ENTRY(ent_proghost),sizeof(new_prog->host)-1);
  g_signal_connect(G_OBJECT(ent_proghost),"changed",G_CALLBACK(addprog_entry_change),new_prog->host);
  gtk_table_attach(GTK_TABLE(box_addprog),ent_proghost, 1,2,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("GUI left"), 0,1,3,4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *spn_progguileft = gtk_spin_button_new_with_range(0,100,1);
  g_signal_connect(G_OBJECT(spn_progguileft),"value-changed",G_CALLBACK(addprog_guicoord_change),&new_prog->guicoords[0]);
  gtk_table_attach(GTK_TABLE(box_addprog),spn_progguileft, 1,2,3,4, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("GUI right"), 0,1,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *spn_progguiright = gtk_spin_button_new_with_range(0,100,1);
  g_signal_connect(G_OBJECT(spn_progguiright),"value-changed",G_CALLBACK(addprog_guicoord_change),&new_prog->guicoords[1]);
  gtk_table_attach(GTK_TABLE(box_addprog),spn_progguiright, 1,2,4,5, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("GUI top"), 0,1,5,6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *spn_progguitop = gtk_spin_button_new_with_range(0,100,1);
  g_signal_connect(G_OBJECT(spn_progguitop),"value-changed",G_CALLBACK(addprog_guicoord_change),&new_prog->guicoords[2]);
  gtk_table_attach(GTK_TABLE(box_addprog),spn_progguitop, 1,2,5,6, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("GUI bottom"), 0,1,6,7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *spn_progguibottom = gtk_spin_button_new_with_range(0,100,1);
  g_signal_connect(G_OBJECT(spn_progguibottom),"value-changed",G_CALLBACK(addprog_guicoord_change),&new_prog->guicoords[3]);
  gtk_table_attach(GTK_TABLE(box_addprog),spn_progguibottom, 1,2,6,7, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  gtk_table_attach(GTK_TABLE(box_addprog),gtk_label_new("Active during"), 0,1,7,8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  GtkWidget *cmb_progactive = gtk_combo_box_new_text();
  gtk_combo_box_append_text(GTK_COMBO_BOX(cmb_progactive), "Neither");
  gtk_combo_box_append_text(GTK_COMBO_BOX(cmb_progactive), "Day");
  gtk_combo_box_append_text(GTK_COMBO_BOX(cmb_progactive), "Night");
  gtk_combo_box_append_text(GTK_COMBO_BOX(cmb_progactive), "Both");
  gtk_combo_box_set_active(GTK_COMBO_BOX(cmb_progactive),2);
  new_prog->active_time = ACTIVE_TIME_NIGHT;
  g_signal_connect(G_OBJECT(cmb_progactive),"changed",G_CALLBACK(addprog_active_change),&new_prog->active_time);
  gtk_table_attach(GTK_TABLE(box_addprog),cmb_progactive, 1,2,7,8, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3, 3);
  
  g_signal_connect(G_OBJECT(dialog),"response",G_CALLBACK(addprog_response),new_prog);
  gtk_widget_show_all(dialog);
}

/** \brief Handle system signal (specifically, SIGCHLD - child exit)
 *
 * \param signum
 *   Signal number of received system signal
 *
 * \return (void)
 *
 * Algorithm:
 *   -# Process only SIGCHLD, return otherwise.
 *   -# Step through global list of subprogrammes.
 *     -# Ignore subprogrammes that exited normally.
 *     -# Ignore subprogrammes that are still running.
 *     -# All programmes that remain must be restarted.
 *     -# If the restarted subprogramme was processing a stage of an observation, re-send the
 *        observation message.
 */
void process_sigchld(int signum)
{
  if (signum != SIGCHLD)
    return;
  int i;

  act_log_debug(act_log_msg("Processing SIGCHLD"));
  for (i=0; i<G_num_progs; i++)
  {
    if ((G_progs[i].status == PROGSTAT_STOPPED) || (G_progs[i].status == PROGSTAT_KILLED))
      continue;
    if (waitpid(G_progs[i].pid, NULL, WNOHANG) == 0)
      continue;
    if (G_progs[i].status == PROGSTAT_STOPPING)
    {
      prog_set_status(&G_progs[i], PROGSTAT_STOPPED);
      return;
    }

    act_log_error(act_log_msg("Programme %s has died. Attempting to restart", G_progs[i].name));
    prog_set_status(&G_progs[i], PROGSTAT_KILLED);
    G_progs[i].sockfd = 0;
    G_progs[i].pid = 0;
    if ((!start_prog(&G_progs[i])) < 0)
      act_log_error(act_log_msg("Failed to start %s.", G_progs[i].name));
    return;
  }
}

void process_sigio(int signum)
{
  if (signum != SIGIO)
    return;
  check_prog_messages(G_progs, G_num_progs);
}

void check_status(struct act_prog *prog)
{
  if ((prog->status != PROGSTAT_RUNNING) && (prog->status != PROGSTAT_STARTUP))
    return;

  prog->last_stat_timer += TIMEOUT_PERIOD_MS;
  if (prog->last_stat_timer > STAT_RESP_TIMEOUT_MS)
  {
    act_log_normal(act_log_msg("No messages received from %s in %d seconds. Restarting %s.", prog->name, prog->last_stat_timer/1000, prog->name));
    prog_set_status(prog, PROGSTAT_KILLED);
    close(prog->sockfd);
    kill(prog->pid, SIGTERM);
    start_prog(prog);
  }
  else if (prog->last_stat_timer > STAT_REQ_TIMEOUT_MS)
    send_statreq(prog);
}

gboolean reg_checks()
{
  int i;
  for (i=0; i<G_num_progs; i++)
  {
//     check_active_change(&G_progs[i], G_status_active);
    check_status(&G_progs[i]);
  }
  return TRUE;
}

/** \brief Main function.
 *
 * \param argc
 *   Command-line argument count
 *
 * \param argv
 *   Command-line arguments
 *
 * Algorithm:
 *  -# Parse command line arguments
 *  -# Parse global configuration file
 *  -# Set up a socket on which to listen for incoming network connections
 *  -# Parse subprogrammes configuration file
 *  -# Create GUI elements
 *  -# Start subprogrammes (see start_prog)
 *  -# MAIN PROGRAMME LOOP
 *  -# Close subprogrammes gracefully
 *  -# Close open file descriptors (incl. network connections)
 *
 * \todo This function is unnecessary and should be removed.
 */
int main(int argc, char** argv)
{
  int i;

  act_log_open();
  act_log_normal(act_log_msg("ACT control %d.%d starting up.", MAJOR_VER, MINOR_VER));

  gtk_init(&argc, &argv);
  struct arg_str *sqlarg = arg_str1("s", "sqlconfighost", "<server ip/hostname>", "The hostname or IP address of the SQL server than contains act_control's configuration information");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The TCP port to listen on for incoming connections (from child applications).");
  struct arg_lit *fullscrarg = arg_lit0("f", "fullscreen", "Activate to have the software run in fullscreen mode.");
  struct arg_end *endargs = arg_end(5);
  void* argtable[] = {sqlarg, portarg, fullscrarg, endargs};
  if (arg_nullcheck(argtable) != 0)
    act_log_error(act_log_msg("Error parsing command-line arguemtns - insufficient memory."));
  int argparse_errors = arg_parse(argc, argv, argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr, endargs, argv[0]);
    exit(2);
  }

  G_sqlserver = sqlarg->sval[0];
  G_portname = portarg->sval[0];
  char fullscr = fullscrarg->count > 0;
  if (gethostname(G_hostname, sizeof(G_hostname)) < 0)
  {
    act_log_error(act_log_msg("Could not get hostname - %s.", strerror(errno)));
    return 1;
  }
  G_listen_sockfd = net_setup(G_portname);
  if (G_listen_sockfd < 0)
  {
    act_log_error(act_log_msg("Could establish a network socket for listening. Exiting."));
    return 1;
  }
  G_status_active = 0;
  act_log_normal(act_log_msg("ACT control listening on port %s (host %s).", G_portname, G_hostname));

  init_targcap();
  init_pmtcap();
  init_ccdcap();

  // GUI stuff
  GtkWidget* wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title(GTK_WINDOW(wnd_main),"ACT CONTROL");
  if (fullscr)
    gtk_window_fullscreen(GTK_WINDOW(wnd_main));
  else
    gtk_window_maximize(GTK_WINDOW(wnd_main));
  gtk_widget_show_all(wnd_main);

  GtkWidget *scr_main = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scr_main),GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(wnd_main),scr_main);
  GtkWidget* box_main = gtk_vbox_new(FALSE,3);
  gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scr_main),box_main);
  GtkWidget* frm_stat = gtk_frame_new("Programme Status");
  gtk_box_pack_end(GTK_BOX(box_main),frm_stat,FALSE,TRUE,0);
  GtkWidget* box_stat = gtk_hbox_new(FALSE,3);
  gtk_container_add(GTK_CONTAINER(frm_stat),box_stat);
  GtkWidget* btn_quit = gtk_button_new_with_label("Quit");
  gtk_box_pack_end(GTK_BOX(box_stat),btn_quit,TRUE,TRUE,0);
  GtkWidget *btn_add_prog = gtk_button_new_with_label("Add Prog.");
  gtk_box_pack_end(GTK_BOX(box_stat),btn_add_prog,TRUE,TRUE,0);

  GtkWidget* box_progs = gtk_table_new(0, 0, FALSE);
  gtk_box_pack_start(GTK_BOX(box_main), box_progs, TRUE, TRUE, 0);
  struct addprog_objects addprog_objs = {.box_progs = box_progs, .box_stat = box_stat};

  G_progs = NULL;
  G_num_progs = control_config_programmes(G_sqlserver, &G_progs);
  if (G_num_progs < 0)
  {
    act_log_error(act_log_msg("Error reading programmes configuration from database."));
    G_num_progs = 0;
    if (G_progs != NULL)
    {
      free(G_progs);
      G_progs = NULL;
    }
  }
  else for (i=0; i<G_num_progs; i++)
  {
    G_progs[i].listen_sockfd = &G_listen_sockfd;
    G_progs[i].hostname = G_hostname;
    G_progs[i].portname = G_portname;
    G_progs[i].sqlconfighost = G_sqlserver;
    G_progs[i].status_active = &G_status_active;
    G_progs[i].box_progs = box_progs;
    G_progs[i].button = gtk_button_new_with_label(G_progs[i].name);
    gtk_box_pack_start(GTK_BOX(box_stat),G_progs[i].button,TRUE,TRUE,0);
    g_signal_connect(G_OBJECT(G_progs[i].button),"clicked",G_CALLBACK(prog_button), &G_progs[i]);

    G_progs[i].sockfd = 0;
    G_progs[i].pid = 0;
    G_progs[i].status = PROGSTAT_STOPPED;
    memset(&G_progs[i].caps, 0, sizeof(struct act_msg_cap));
  }

  signal(SIGCHLD, process_sigchld);
  signal(SIGIO, process_sigio);
  signal(SIGPIPE, SIG_IGN);

  // Start all sub-programs
  for (i=0; i<G_num_progs; i++)
  {
    if (G_progs[i].active_time != (ACTIVE_TIME_DAY | ACTIVE_TIME_NIGHT))
      continue;
    if (!start_prog(&G_progs[i]) < 0)
    {
      act_log_error(act_log_msg("Error starting programme %s.", G_progs[i].name));
      continue;
    }
    act_log_normal(act_log_msg("Programme %s successfully started.", G_progs[i].name));
  }

  act_log_normal(act_log_msg("Ready. Starting main loop."));
  int timeout_id = 0;
  if (TIMEOUT_PERIOD_MS % 1000 == 0)
    timeout_id = g_timeout_add_seconds(TIMEOUT_PERIOD_MS/1000, reg_checks, NULL);
  else
    timeout_id = g_timeout_add(TIMEOUT_PERIOD_MS, reg_checks, NULL);
  g_signal_connect(G_OBJECT(btn_add_prog),"clicked",G_CALLBACK(add_prog), &addprog_objs);
  g_signal_connect(G_OBJECT(btn_quit),"clicked",G_CALLBACK(quit_pressed), (void *)timeout_id);
  g_signal_connect(G_OBJECT(wnd_main),"delete-event",G_CALLBACK(window_close), (void *)timeout_id);
  gtk_widget_show_all(wnd_main);
  gtk_main();

  act_log_normal(act_log_msg("Done. Exiting."));
  act_log_close();
  return 0;
}
