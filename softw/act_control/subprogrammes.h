#ifndef SUBPROGRAMMES_H
#define SUBPROGRAMMES_H

#include <gtk/gtk.h>
#include <signal.h>
#include <time.h>
#include <act_ipc.h>

/** At startup, the main controller populates an array of act_prog structures
  * from the sub-programmes configuration file, as listed in the global 
  * configuration file. 
  *
  * \brief Structure to hold information relevant to each child programme. 
  */
struct act_prog
{
  //! Child name
  char name[10];
  //! Child executable
  char executable[20];
  //! Hostname of computer on which child is to be run
  char host[50];
  //! Coordinates (left, right, top, bottom) of programme GUI within act_control GtkTable GUI layout component
  unsigned char guicoords[4];
  //! Times when programme should be active - ACTIVE_TIME_DAY, ACTIVE_TIME_NIGHT, neither or both
  unsigned char active_time;

  //! Network socket on which child is accepted
  int sockfd;
  //! Process identifier of child process
  int pid;
  //! State of execution of child
  unsigned char status;
  //! Time last status was received or quit command was sent
  time_t last_stat_timer;
  //! Message structure that describes the clients capabilities and requirements.
  struct act_msg_cap caps;
  //! GtkButton on act_control form which creates a popup window with options for the child when pressed
  GtkWidget *button;
  //! GtkSocket that contains the GUI for the child on the act_control main window
  GtkWidget *socket;
  
  //! Socket on which ACT control listens for incoming connections
  const int *listen_sockfd;
  //! Hostname of computer on which ACT control is running
  const char *hostname;
  //! Name of port on which ACT control listens of incoming connections
  const char *portname;
  //! Hostname of computer hosting ACT configuration SQL database
  const char *sqlconfighost;
  //! System's current active time (ACTIVE_TIME_DAY, ACTIVE_TIME_NIGHT, neither or both)
  unsigned char *status_active;
  //! GtkTable containing GUI sockets for programmes.
  GtkWidget *box_progs;
};

char start_prog(struct act_prog* prog);
char close_prog(struct act_prog *prog);
void prog_active_change(struct act_prog *prog, unsigned char status_active);
void prog_button(GtkWidget *btn_progopts, gpointer user_data);
void plug_added(GtkSocket *socket);
gboolean plug_removed(GtkSocket *socket, gpointer user_data);
char prog_set_status(struct act_prog *prog, unsigned char new_stat);

#endif
