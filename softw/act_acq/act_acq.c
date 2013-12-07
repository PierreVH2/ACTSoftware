#include <stdio.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <math.h>
#include <argtable2.h>
#include <gtk/gtk.h>
#include <mysql/mysql.h>
#include <merlin_driver.h>
#include <act_log.h>
#include <act_ipc.h>
#include <act_positastro.h>
#include "acq_imgdisp.h"
#include "acq_ccdcntrl.h"

#define CCDCHECK_TIMEOUT_PERIOD 1000
#define GUICHECK_TIMEOUT_PERIOD 3000

struct formobjects
{
  GtkWidget *box_main, *dra_ccdimg;
  
  struct ccdcntrl_objects *ccdcntrl_objs;
};

int G_netsock_fd;
int G_signal_pipe[2];
struct formobjects G_formobjs;
struct act_msg *G_obsn_msg = NULL;

/** \brief Callback when main plug containing all GUI objects is destroyed.
 * \param plug GTK plug object.
 * \param user_data Contents of GTK plug.
 * \return (void)
 * \todo Access contents of plug using get_child instead.
 *
 * References objects contained within plug so they don't get destroyed.
 */
void destroy_plug(GtkWidget *plug, gpointer user_data)
{
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(user_data));
}

/** \brief Callback for when user clicks "Save" from FITS save dialog.
 * \param dialog GTK file save dialog.
 * \param response_id Flag indicating the user's response to the file save dialog
 * \return (void)
 */
void save_fits_dialog_response(GtkWidget* dialog, gint response_id, gpointer user_data)
{
  if (response_id != GTK_RESPONSE_ACCEPT)
  {
    gtk_widget_destroy(dialog);
    return;
  }

  char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  struct stat filestat;
  if ((stat (filename, &filestat)) != -1)
  {
    if (remove(filename) == -1)
    {
      GtkWidget *save_fail_dialog = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(user_data),GTK_TYPE_WINDOW)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "Could not remove existing file %s - %s", filename, strerror(errno));
      g_signal_connect_swapped(G_OBJECT(save_fail_dialog), "response", G_CALLBACK(gtk_widget_destroy), dialog);
      gtk_widget_show_all(save_fail_dialog);
      g_free(filename);
      return;
    }
  }
  char ret_val = ccdcntrl_write_fits(filename);
  if (ret_val <= 0)
  {
    GtkWidget *save_fail_dialog = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(user_data),GTK_TYPE_WINDOW)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "An error occurred while writing FITS data to %s. Check the system logs for more information.", filename);
    g_signal_connect(G_OBJECT(save_fail_dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show_all(save_fail_dialog);
  }
  g_free (filename);
  gtk_widget_destroy(dialog);
}

/** \brief Callback for when user clicks "Save FITS" from main window. Displays "Save File" dialog.
 * \return (void)
 * \todo Read default directory from global config
 */
void save_fits_clicked(GtkWidget *btn_save_fits)
{
  GtkWidget *dialog;
  dialog = gtk_file_chooser_dialog_new ("Save FITS Image", GTK_WINDOW(gtk_widget_get_ancestor(btn_save_fits,GTK_TYPE_WINDOW)), GTK_FILE_CHOOSER_ACTION_SAVE, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), "/home/actdevel/acq_images");
  struct datestruct unidate;
  struct timestruct unitime;
  ccdcntrl_get_img_datetime(&unidate, &unitime);
  char tmp_filename[50];
  sprintf(tmp_filename, "%04hu%02hhu%02hhu_%02hhu%02hhu%02hhu.fits", unidate.year, unidate.month+1, unidate.day+1, unitime.hours, unitime.minutes, unitime.seconds);
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (dialog), tmp_filename);  
  gtk_widget_show_all(dialog);
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(save_fits_dialog_response), btn_save_fits);
}

void save_db_clicked(GtkWidget *btn_save_db, gpointer user_data)
{
  if (ccdcntrl_save_image((struct ccdcntrl_objects *)user_data))
    return;
  GtkWidget *save_fail_dialog = gtk_message_dialog_new (GTK_WINDOW (gtk_widget_get_ancestor (GTK_WIDGET (btn_save_db), GTK_TYPE_WINDOW)), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "An error occurred while image to database. Check the system logs for more information.");
  g_signal_connect(G_OBJECT(save_fail_dialog), "response", G_CALLBACK(gtk_widget_destroy), NULL);
  gtk_widget_show_all(save_fail_dialog);
}

/** \brief Setup network socket with ACT control.
 * \param host Hostname (dns name) or IP address of the computer running ACT control.
 * \param port Port number/name that ACT control is listening on.
 */
int setup_net(const char* host, const char* port)
{
  struct addrinfo hints, *servinfo, *p;
  int sockfd, retval;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if ((retval = getaddrinfo(host, port, &hints, &servinfo)) != 0)
  {
    act_log_error(act_log_msg("Failed to get address info - %s.", gai_strerror(retval)));
    return -1;
  }

  for(p = servinfo; p != NULL; p = p->ai_next)
  {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
      continue;

    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1)
    {
      close(sockfd);
      continue;
    }
    break;
  }
  if (p == NULL)
  {
    act_log_error(act_log_msg("Failed to connect - %s.", gai_strerror(retval)));
    return -1;
  }
  freeaddrinfo(servinfo);

  return sockfd;
}

/** \brief Check for incoming messages over network connection.
 * \param box_main GTK box containing all graphical objects.
 * \return TRUE if a message was received, FALSE if incoming queue is empty, <0 upon error.
 *
 * Recognises the following message types:
 *  - MT_QUIT: Exit main programme loop.
 *  - MT_CAP: Respond with capabilities and requirements.
 *  - MT_STAT: Respond with current status.
 *  - MT_GUISOCK: Construct GtkPlug for given socket and embed box_main within plug.
 *  - MT_COORD: Update internally stored right ascension, hour angle, declination, altitude, azimuth.
 *
 * \todo For MT_OBSN - ignore messages with error flag set.
 */
int check_net_messages()
{
  int ret = 0;
  struct act_msg msgbuf;
  memset(&msgbuf, 0, sizeof(struct act_msg));
  int numbytes = recv(G_netsock_fd, &msgbuf, sizeof(struct act_msg), MSG_DONTWAIT);
  if ((numbytes == -1) && (errno == EAGAIN))
    return 0;
  if (numbytes == -1)
  {
    act_log_error(act_log_msg("Failed to receive message - %s.", strerror(errno)));
    return -1;
  }
  switch (msgbuf.mtype)
  {
    case MT_QUIT:
    {
      act_log_normal(act_log_msg("Received QUIT signal"));
      gtk_main_quit();
      break;
    }
    case MT_CAP:
    {
      struct act_msg_cap *cap_msg = &msgbuf.content.msg_cap;
      cap_msg->service_provides = 0;
      cap_msg->service_needs = SERVICE_TIME | SERVICE_COORD;
      cap_msg->targset_prov = TARGSET_ACQUIRE;
      cap_msg->datapmt_prov = 0;
      cap_msg->dataccd_prov = DATACCD_PHOTOM;
      snprintf(cap_msg->version_str, MAX_VERSION_LEN, "%d.%d", MAJOR_VER, MINOR_VER);
      if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
      {
        act_log_error(act_log_msg("Failed to send capabilities message - %s.", strerror(errno)));
        ret = -1;
      }
      else 
        ret = 1;
      break;
    }
    case MT_STAT:
    {
      msgbuf.content.msg_stat.status = PROGSTAT_RUNNING;
      if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
      {
        act_log_error(act_log_msg("Failed to send status message - %s.", strerror(errno)));
        ret = -1;
      }
      else
        ret = 1;
      break;
    }
    case MT_GUISOCK:
    {
      struct act_msg_guisock *guimsg = &msgbuf.content.msg_guisock;
      ret = 1;
      act_log_normal(act_log_msg("Processing GUISOCK (%d)", guimsg->gui_socket));
      if (guimsg->gui_socket > 0)
      {
        if (gtk_widget_get_parent(G_formobjs.box_main) != NULL)
        {
          act_log_normal(act_log_msg("Received GUI socket (%d), but GUI elements are already embedded. Ignoring the latter."));
          break;
        }
        GtkWidget *plg_new = gtk_plug_new(guimsg->gui_socket);
        act_log_normal(act_log_msg("Received GUI socket %d.", guimsg->gui_socket));
        gtk_container_add(GTK_CONTAINER(plg_new),G_formobjs.box_main);
        g_signal_connect(G_OBJECT(plg_new),"destroy",G_CALLBACK(destroy_plug),G_formobjs.box_main);
        gtk_widget_show_all(plg_new);
      }
      break;
    }
    case MT_TIME:
    {
      ccdcntrl_set_time(&msgbuf.content.msg_time);
      ret = 1;
      break;
    }
    case MT_COORD:
    {
      ccdcntrl_set_coords(&msgbuf.content.msg_coord);
      ret = 1;
      break;
    }
    case MT_TARG_CAP:
    {
      struct act_msg_targcap *msg_targcap = &msgbuf.content.msg_targcap;
      if (msg_targcap->targset_stage == TARGSET_ACQUIRE)
      {
        ret = 1;
        break;
      }
      msg_targcap->autoguide = 0;
      msg_targcap->targset_stage = TARGSET_ACQUIRE;
      if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
      {
        act_log_error(act_log_msg("Failed to send target set capabilities - %s.", strerror(errno)));
        ret = -1;
      }
      else
        ret = 1;
      break;
    }
    case MT_TARG_SET:
    {
      struct act_msg_targset *msg_targset = &msgbuf.content.msg_targset;
      if (msg_targset->targset_stage != TARGSET_ACQUIRE)
      {
        act_log_error(act_log_msg("Target set message with incorrect targset_stage received. Ignoring."));
        ret = 1;
        break;
      }
      if ((msg_targset->status == OBSNSTAT_CANCEL) || 
          (msg_targset->status == OBSNSTAT_ERR_RETRY) || 
          (msg_targset->status == OBSNSTAT_ERR_WAIT) || 
          (msg_targset->status == OBSNSTAT_ERR_NEXT))
      {
        ccdcntrl_cancel_exp(G_formobjs.ccdcntrl_objs);
        if (G_obsn_msg != NULL)
        {
          free(G_obsn_msg);
          G_obsn_msg = NULL;
        }
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
        {
          act_log_error(act_log_msg("Failed to send target set error notification response - %s.", strerror(errno)));
          ret = -1;
        }
        else
          ret = 1;
        break;
      }
      if (G_obsn_msg != NULL)
      {
        act_log_error(act_log_msg("Target set message received, but already busy with an observation. Flagging error and responding."));
        msg_targset->status = OBSNSTAT_ERR_RETRY;
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
        {
          act_log_error(act_log_msg("Failed to send target set cancellation/error response - %s.", strerror(errno)));
          ret = -1;
        }
        else
          ret = 1;
        break;
      }
      
      act_log_normal(act_log_msg("Starting target acquisition."));
      ret = ccdcntrl_start_targset_exp(G_formobjs.ccdcntrl_objs, msg_targset);
      if (ret < 0)
      {
        act_log_error(act_log_msg("An error occurred while attempting to start target acquisition."));
        msg_targset->status = abs(ret);
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
          act_log_error(act_log_msg("Failed to send target set error notification - %s.", strerror(errno)));
        ret = -1;
        break;
      }
      G_obsn_msg = malloc(sizeof(struct act_msg));
      if (G_obsn_msg == NULL)
      {
        act_log_error(act_log_msg("Could not allocate space for target acquisition message."));
        msg_targset->status = OBSNSTAT_ERR_RETRY;
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
        {
          act_log_error(act_log_msg("Failed to send target set error notification - %s.", strerror(errno)));
          ret = -1;
        }
        else
          ret = 1;
        break;
      }
      memcpy(G_obsn_msg, &msgbuf, sizeof(struct act_msg));
      ret = 1;
      break;
    }
    case MT_CCD_CAP:
    {
      struct act_msg_ccdcap *msg_ccdcap = &msgbuf.content.msg_ccdcap;
      if (msg_ccdcap->dataccd_stage == DATACCD_PHOTOM)
      {
        ret = 1;
        break;
      }
      ccdcntrl_get_ccdcaps(msg_ccdcap);
      msg_ccdcap->dataccd_stage = DATACCD_PHOTOM;
      if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
      {
        act_log_error(act_log_msg("Failed to send CCD DATA capabilities - %s.", strerror(errno)));
        ret = -1;
      }
      else
        ret = 1;
      break;
    }
    case MT_DATA_CCD:
    {
      struct act_msg_dataccd *msg_dataccd = &msgbuf.content.msg_dataccd;
      if (msg_dataccd->dataccd_stage != DATACCD_PHOTOM)
      {
        act_log_error(act_log_msg("DATA CCD message with incorrect stage received. Ignoring."));
        ret = 1;
        break;
      }
      if ((msg_dataccd->status == OBSNSTAT_CANCEL) || 
          (msg_dataccd->status == OBSNSTAT_ERR_RETRY) || 
          (msg_dataccd->status == OBSNSTAT_ERR_WAIT) || 
          (msg_dataccd->status == OBSNSTAT_ERR_NEXT))
      {
        ccdcntrl_cancel_exp(G_formobjs.ccdcntrl_objs);
        if (G_obsn_msg != NULL)
        {
          free(G_obsn_msg);
          G_obsn_msg = NULL;
        }
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
        {
          act_log_error(act_log_msg("Failed to send DATA CCD cancel/error notification response - %s.", strerror(errno)));
          ret = -1;
        }
        else
          ret = 1;
        break;
      }
      if (G_obsn_msg != NULL)
      {
        act_log_error(act_log_msg("DATA CCD message received, but already busy with an observation. Flagging error and responding."));
        msg_dataccd->status = OBSNSTAT_ERR_RETRY;
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
        {
          act_log_error(act_log_msg("Failed to send DATA CCD error response - %s.", strerror(errno)));
          ret = -1;
        }
        else
          ret = 1;
        break;
      }
      
      act_log_normal(act_log_msg("Starting CCD photometry collection."));
      ret = ccdcntrl_start_phot_exp(G_formobjs.ccdcntrl_objs, msg_dataccd);
      if (ret < 0)
      {
        act_log_error(act_log_msg("An error occurred while attempting to start CCD photometry collection."));
        msg_dataccd->status = abs(ret);
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
          act_log_error(act_log_msg("Failed to send CCD DATA error notification - %s.", strerror(errno)));
        ret = -1;
        break;
      }
      G_obsn_msg = malloc(sizeof(struct act_msg));
      if (G_obsn_msg == NULL)
      {
        act_log_error(act_log_msg("Could not allocate space for CCD DATA message."));
        msg_dataccd->status = OBSNSTAT_ERR_RETRY;
        if (send(G_netsock_fd, &msgbuf, sizeof(struct act_msg), 0) == -1)
        {
          act_log_error(act_log_msg("Failed to send CCD DATA error notification - %s.", strerror(errno)));
          ret = -1;
        }
        else
          ret = 1;
        break;
      }
      memcpy(G_obsn_msg, &msgbuf, sizeof(struct act_msg));
      ret = 1;
      break;
    }
    default:
      if (msgbuf.mtype >= MT_INVAL)
        act_log_error(act_log_msg("Invalid message received (type %d).", msgbuf.mtype));
      ret = 1;
  }
  return ret;
}

int request_guisock()
{
  struct act_msg guimsg;
  memset(&guimsg, 0, sizeof(struct act_msg));
  guimsg.mtype = MT_GUISOCK;
  if (send(G_netsock_fd, &guimsg, sizeof(struct act_msg), 0) == -1)
  {
    act_log_error(act_log_msg("Failed to send GUI socket request - %s.", strerror(errno)));
    return -1;
  }
  return 1;
}

void pipe_signals(int signum)
{
  if (signum != SIGIO)
  {
    act_log_error(act_log_msg("Invalid signal number (%d).", signum));
    return;
  }  
  if (write(G_signal_pipe[1], &signal, sizeof(int)) != sizeof(int))
    act_log_error(act_log_msg("Could not send signal %d to GTK handler.", signum));
}

char acq_check_targset(struct act_msg *obsn_msg, int status)
{
  if (status == 0)
    return FALSE;
  if (status < 0)
  {
    act_log_error(act_log_msg("Flagging error and returning target set message."));
    if (send(G_netsock_fd, obsn_msg, sizeof(struct act_msg), 0) == -1)
      act_log_error(act_log_msg("Failed to send target set error response - %s.", strerror(errno)));
    return TRUE;
  }
  struct act_msg_targset *msg_targset = &obsn_msg->content.msg_targset;
  int ret = ccdcntrl_check_targset_exp(G_formobjs.ccdcntrl_objs, &msg_targset->adj_ra_h, &msg_targset->adj_dec_d, &msg_targset->targ_cent);
  if (ret < 0)
  {
    act_log_error(act_log_msg("An error occurred while checking target set exposure status. Flagging error and returning target set message."));
    msg_targset->status = abs(ret);
    if (send(G_netsock_fd, obsn_msg, sizeof(struct act_msg), 0) == -1)
      act_log_error(act_log_msg("Failed to send target set error response - %s.", strerror(errno)));
    return TRUE;
  }
  if (ret == 0)
  {
    act_log_normal(act_log_msg("check_targset_exp return 0, image not ready yet."));
    return FALSE;
  }
  if (send(G_netsock_fd, obsn_msg, sizeof(struct act_msg), 0) == -1)
    act_log_error(act_log_msg("Failed to send target set response - %s.", strerror(errno)));
  return TRUE;
}

char acq_check_dataccd(struct act_msg *obsn_msg, int status)
{
  if (status == 0)
    return FALSE;
  if (status < 0)
  {
    act_log_error(act_log_msg("Flagging error and returning CCD photometry message."));
    if (send(G_netsock_fd, obsn_msg, sizeof(struct act_msg), 0) == -1)
      act_log_error(act_log_msg("Failed to send CCD photometry error message - %s.", strerror(errno)));
    return TRUE;
  }
  struct act_msg_dataccd *msg_dataccd = &obsn_msg->content.msg_dataccd;
  int ret = ccdcntrl_check_phot_exp(G_formobjs.ccdcntrl_objs);
  if (ret == 0)
    return FALSE;
  if (ret < 0)
  {
    act_log_error(act_log_msg("An error occurred while checking CCD photometric exposure status. Flagging error and returning CCD photometry message."));
    msg_dataccd->status = abs(ret);
    if (send(G_netsock_fd, obsn_msg, sizeof(struct act_msg), 0) == -1)
      act_log_error(act_log_msg("Failed to send CCD photometry error response - %s.", strerror(errno)));
    return TRUE;
  }
  act_log_normal(act_log_msg("CCD photometry collection complete/cancelled by user."));
  if (send(G_netsock_fd, obsn_msg, sizeof(struct act_msg), 0) == -1)
    act_log_error(act_log_msg("Failed to send target set response - %s.", strerror(errno)));
  return TRUE;
}

gboolean guicheck_timeout(gpointer user_data)
{
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(user_data)) != NULL;
  if (!main_embedded)
  {
    act_log_normal(act_log_msg("Requesting GUI socket"));
    request_guisock();
  }
  return TRUE;
}

gboolean ccdcheck_timeout(gpointer user_data)
{
  if (G_obsn_msg != NULL)
  {
    int ret = 0;
    if (G_obsn_msg->mtype == MT_TARG_SET)
      ret = acq_check_targset(G_obsn_msg, 1);
    else if (G_obsn_msg->mtype == MT_DATA_CCD)
      ret = acq_check_dataccd(G_obsn_msg, 1);
    else
    {
      act_log_error(act_log_msg("An observation message with an unknown/invalid type (%d) is buffered. Clearing buffer."));
      ret = 1;
    }
    if (ret)
    {
      free(G_obsn_msg);
      G_obsn_msg = NULL;
    }
  }

  return TRUE;
}

gboolean process_signal(GIOChannel *source, GIOCondition cond, gpointer user_data)
{
  (void)cond;
  (void)user_data;
  
  GError *error = NULL;
  union 
  {
    gchar chars[sizeof(int)];
    int signum;
  } buf;
  GIOStatus status;
  gsize bytes_read;

  while((status = g_io_channel_read_chars(source, buf.chars, sizeof(int), &bytes_read, &error)) == G_IO_STATUS_NORMAL)
  {
    if (error != NULL)
      break;
    int ret;
/*    if (G_obsn_msg != NULL)
    {
      if (G_obsn_msg->mtype == MT_TARG_SET)
        ret = acq_check_targset(G_obsn_msg, ret);
      else if (G_obsn_msg->mtype == MT_DATA_CCD)
        ret = acq_check_dataccd(G_obsn_msg, ret);
      else
      {
        act_log_error(act_log_msg("An observation message with an unknown/invalid type (%d) is buffered. Clearing buffer."));
        ret = 1;
      }
      if (ret)
      {
        free(G_obsn_msg);
        G_obsn_msg = NULL;
      }
    }*/

    ret = check_net_messages();
    if (ret < 0)
      act_log_error(act_log_msg("Error reading messages."));
  }
  
  if(error != NULL)
  {
    act_log_error(act_log_msg("Error reading signal pipe: %s", error->message));
    return TRUE;
  }
  if(status == G_IO_STATUS_EOF)
  {
    act_log_error(act_log_msg("Signal pipe has been closed."));
    return FALSE;
  }
  if (status != G_IO_STATUS_AGAIN)
  {
    act_log_error(act_log_msg("status != G_IO_STATUS_AGAIN."));
    return FALSE;
  }
  return TRUE;
}

int main(int argc, char **argv)
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
    exit(EXIT_FAILURE);
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  sqlconfig = sqlconfigarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));

  if ((G_netsock_fd = setup_net(host, port)) < 0)
  {
    act_log_error(act_log_msg("Error setting up network connection."));
    return 1;
  }
  fcntl(G_netsock_fd, F_SETOWN, getpid());
  int oflags = fcntl(G_netsock_fd, F_GETFL);
  fcntl(G_netsock_fd, F_SETFL, oflags | FASYNC);
  
  MYSQL *conn;
  conn = mysql_init(NULL);
  if (conn == NULL)
    act_log_error(act_log_msg("Error initialising MySQL connection handler - %s.", mysql_error(conn)));
  else if (mysql_real_connect(conn, sqlconfig, "act_acq", NULL, "actnew", 0, NULL, 0) == NULL)
  {
    act_log_error(act_log_msg("Error connecting to MySQL database - %s.", mysql_error(conn)));
    conn = NULL;
    return 1;
  }
  
  GtkWidget* box_main = gtk_table_new(3,3,FALSE);
  G_formobjs.box_main = box_main;
  g_object_ref(box_main);
  
  GtkWidget *evb_ccdcmd = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(box_main),evb_ccdcmd, 0,1,0,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  G_formobjs.ccdcntrl_objs = ccdcntrl_create_objs(&conn, evb_ccdcmd);
  if (G_formobjs.ccdcntrl_objs == NULL)
  {
    act_log_error(act_log_msg("Failed to create CCD control objects."));
    act_log_error(act_log_msg("Exiting"));
    close(G_netsock_fd);
    return 1;
  }
  
  gtk_table_attach(GTK_TABLE(box_main),gtk_vseparator_new(), 1,2,0,3, GTK_SHRINK, GTK_FILL|GTK_EXPAND, 3,3);

  GtkWidget *evb_imgdisp = gtk_event_box_new();
  gtk_table_attach(GTK_TABLE(box_main),evb_imgdisp, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  if (create_imgdisp_objs(conn, evb_imgdisp) < 0)
  {
    act_log_error(act_log_msg("Failed to create CCD image display objects."));
    act_log_error(act_log_msg("Exiting"));
    close(G_netsock_fd);
    return 1;
  }
  
  gtk_table_attach(GTK_TABLE(box_main),gtk_hseparator_new(), 1,3,1,2, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 3,3);
  
  GtkWidget *box_misc_cntrl = gtk_hbox_new(TRUE,3);
  gtk_table_attach(GTK_TABLE(box_main),box_misc_cntrl, 2,3,2,3, GTK_FILL|GTK_EXPAND, GTK_SHRINK, 3,3);
  GtkWidget *btn_save_db = gtk_button_new_with_label("Save DB");
  gtk_box_pack_start(GTK_BOX(box_misc_cntrl),btn_save_db,TRUE,TRUE,3);
  g_signal_connect (G_OBJECT(btn_save_db), "clicked", G_CALLBACK(save_db_clicked), G_formobjs.ccdcntrl_objs);
  GtkWidget *btn_save_fits = gtk_button_new_with_label("Save FITS");
  gtk_box_pack_start(GTK_BOX(box_misc_cntrl),btn_save_fits,TRUE,TRUE,3);
  g_signal_connect (G_OBJECT(btn_save_fits), "clicked", G_CALLBACK(save_fits_clicked), NULL);
  
  sigset_t sigset;
  sigemptyset(&sigset);
  struct sigaction sa = { .sa_handler=pipe_signals, .sa_mask=sigset, .sa_flags=0, .sa_restorer=NULL };
  if (sigaction(SIGIO, &sa, NULL) != 0)
  {
    act_log_error(act_log_msg("Could not attach signal handler to SIGIO."));
    act_log_error(act_log_msg("Exiting"));
    return 1;
  }
  if (pipe(G_signal_pipe)) 
  {
    act_log_error(act_log_msg("Error creating UNIX pipe for signal processing pipeline"));
    return 1;
  }
  oflags = fcntl(G_signal_pipe[1], F_GETFL);
  fcntl(G_signal_pipe[1], F_SETFL, oflags | O_NONBLOCK);
  GIOChannel *gio_signal_in = g_io_channel_unix_new(G_signal_pipe[0]);
  GError *error = NULL;
  g_io_channel_set_encoding(gio_signal_in, NULL, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to set g_io_channel encoding"));
    return 1;
  }
  g_io_channel_set_flags(gio_signal_in, g_io_channel_get_flags(gio_signal_in) | G_IO_FLAG_NONBLOCK, &error);
  if (error != NULL)
  {
    act_log_error(act_log_msg("Failed to set IO flags for g_io_channel - %s", error->message));
    return 1;
  }
  int iowatch_id = g_io_add_watch_full (gio_signal_in, G_PRIORITY_HIGH, G_IO_IN | G_IO_PRI, process_signal, NULL, NULL);
  
  act_log_normal(act_log_msg("Starting main loop."));
  int ccdcheck_to_id = g_timeout_add_seconds (CCDCHECK_TIMEOUT_PERIOD/1000, ccdcheck_timeout, NULL);
  int guicheck_to_id = g_timeout_add_seconds (GUICHECK_TIMEOUT_PERIOD/1000, guicheck_timeout, box_main);
  gtk_main();
  g_source_remove(ccdcheck_to_id);
  g_source_remove(guicheck_to_id);
  g_source_remove(iowatch_id);
  close(G_netsock_fd);
  ccdcntrl_finalise_objs(G_formobjs.ccdcntrl_objs);
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}
