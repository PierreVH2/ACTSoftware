/*!
 * \file time_coord.c
 * \brief ACT time-and-coordinates display programme.
 * \author Pierre van Heerden
 *
 * Retrieves data from environmental conditions from ACT and external sources and determines whether it is safe and
 * viable to operate the telescope. Issues environmental conditions messages and blocks observation commands that are 
 * issued during unsafe conditions.
 *
 * \todo Do something about timestruct convert_ functions incompatibility.
 * \todo Implement higher-precision formula for sidereal time.
 */

#include <stdio.h>
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
#include <argtable2.h>
#include <math.h>
#include <gtk/gtk.h>
#include <time_driver.h>
#include <act_ipc.h>
#include <act_positastro.h>
#include <act_timecoord.h>
#include <act_site.h>
#include <act_log.h>

/// Period of main programme loop.
#define DEFAULT_LOOP_PERIOD 5000
#define DISPTIMES_LOOP_PERIOD 1000
#define GUICHECK_LOOP_PERIOD 3000
/// Font size for large display boxes.
#define BIG_FONT_SIZE 64000

/**
 * \brief Structure containing pointers to all various frequently used GUI objects.
 */
struct formobjects
{
  /// Main GUI object container.
  GtkWidget *box_main;

  /** \brief Labels on the form
  * \{
  */
  GtkWidget *lbl_loctd, *lbl_unitd, *lbl_sidt, *lbl_gjd, *lbl_hjd;
  GtkWidget *lbl_ra, *lbl_ha, *lbl_dec, *lbl_alt, *lbl_azm;
  /** \}
  */

  /** \brief Labels on the BIG DISPLAY dialogs
  * \{
  */
  GtkWidget *lbl_big_loctd, *lbl_big_unitd, *lbl_big_sidt;
  GtkWidget *lbl_big_gjd, *lbl_big_hjd;
  GtkWidget *lbl_big_ra, *lbl_big_ha, *lbl_big_dec;
  GtkWidget *lbl_big_alt, *lbl_big_azm;
  /** \} */
};

struct timeout_objects
{
  int timeout_period, timeout_id;
};

// ??? remove this when files are reorganised
void disp_coords();

/** \brief Global variables.
 * \{
 */
struct act_msg_time G_msg_time;
struct act_msg_coord *G_msg_coord = NULL;

int G_netsock_fd, G_time_fd;
int G_signal_pipe[2];
struct formobjects G_form_objs;
/** \} */

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
  g_object_ref(G_OBJECT(user_data));
  gtk_container_remove(GTK_CONTAINER(plug),GTK_WIDGET(user_data));
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

/** \brief Callback when dialog containing big display is destroyed. Set internal reference to big label object to NULL.
 * \param dialog Point to GTK dialog object.
 * \param user_data Pointer to internal reference to label contained within dialog.
 * \param (void)
 * \todo Don't need param dialog.
 */
void destroy_big_label(gpointer user_data)
{
  *((void**)user_data) = NULL;
}

void destroy_big_dialog(gpointer user_data)
{
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(user_data),FALSE);
}

/** \brief Callback when user clicks large display button. Create large display dialog and label within.
 * \param button Display button clicked by user.
 * \param user_data Pointer to internal reference to label contained within dialog.
 * \return (void)
 * \todo Don't need button.
 */
void create_big_dialog(GtkWidget *button, gpointer user_data)
{
  if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button)))
  {
    GtkWidget *dialog = gtk_widget_get_ancestor(GTK_WIDGET(*((void**)user_data)), GTK_TYPE_WINDOW);
    if (dialog == NULL)
      act_log_error(act_log_msg("Big time/coordinate display object needs to be destroyed, but cannot be found. Not destroying the display object."));
    else
      gtk_widget_destroy(dialog);
  }
  else
  {
    if (*((void**)user_data) != NULL)
    {
      act_log_error(act_log_msg("Need to create big time/coordinate display, but it seems to exist already. Not creating another one"));
      return;
    }
    GtkWidget *dialog = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(gtk_widget_get_ancestor(GTK_WIDGET(button), GTK_TYPE_WINDOW)));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    GtkWidget *big_label = gtk_label_new("N/A");
    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), big_label, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    g_signal_connect_swapped(G_OBJECT(dialog), "destroy", G_CALLBACK(destroy_big_dialog), button);
    g_signal_connect_swapped(G_OBJECT(big_label), "destroy", G_CALLBACK(destroy_big_label), user_data);
    *((void**)user_data) = big_label;
  }
}

/** \brief Read mean time from photometry-and-time driver and derive local and universal time and date.
 * \param time_fd File descriptor for photometry-and-time driver character device.
 * \param loct Pointer to timestruct where local time will be stored.
 * \param locd Pointer to datestruct where local date will be stored.
 * \param unit Pointer to timestruct where universal time will be stored.
 * \param unid Pointer to datestruct where universal date will be stored.
 *
 * Algorithm:
 *  -# Read local time from time card.
 *  -# Determine local date.
 *  -# Calculate universal time.
 *    -# Add 24 hours (86400000 msec) to local time hours and subtract TIMEZONE in seconds.
 *    -# Subtract integer multiples of 1 day.
 *  -# Calculate universal date.
 */
unsigned char get_meantime(int time_fd, struct timestruct *loct, struct datestruct *locd, struct timestruct *unit, struct datestruct *unid)
{
  unsigned long new_unit, tmp;
  int ret_val;

  ret_val = ioctl(time_fd, IOCTL_GET_UNITIME, (unsigned long)(&new_unit));
  if (ret_val < 0)
  {
    act_log_error(act_log_msg("Error communicating with time driver - %s", strerror(abs(ret_val))));
    return FALSE;
  }
  tmp = new_unit;
  unit->milliseconds = tmp % 1000;
  tmp /= 1000;
  unit->seconds = tmp % 60;
  tmp /= 60;
  unit->minutes = tmp % 60;
  tmp /= 60;
  unit->hours = tmp % 24;

  time_t systime_sec = time(NULL);
  struct tm *timedate = gmtime(&systime_sec);
  unid->year = timedate->tm_year+1900;
  unid->month = timedate->tm_mon;
  unid->day = timedate->tm_mday-1;
  struct timestruct systime;
  systime.hours = timedate->tm_hour;
  systime.minutes = timedate->tm_min;
  systime.seconds = timedate->tm_sec;
  systime.milliseconds = 0;
  check_systime_discrep(unid, &systime, unit);

  tmp = new_unit + 86400000 + (unsigned long)(TIMEZONE*3600000);
  tmp %= 86400000;
  loct->milliseconds = tmp % 1000;
  tmp /= 1000;
  loct->seconds = tmp % 60;
  tmp /= 60;
  loct->minutes = tmp % 60;
  tmp /= 60;
  loct->hours = tmp % 24;

  timedate = localtime(&systime_sec);
  locd->year = timedate->tm_year+1900;
  locd->month = timedate->tm_mon;
  locd->day = timedate->tm_mday-1;
  systime.hours = timedate->tm_hour;
  systime.minutes = timedate->tm_min;
  systime.seconds = timedate->tm_sec;
  check_systime_discrep(locd, &systime, loct);

  return TRUE;
}

// ??? Check for invalid input parameters

/** \brief Calculate local apparent sidereal time
 * \return (void)
 */
void calc_sidtime(double jd, struct timestruct *sidt)
{
  if (sidt == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters"));
    return;
  }
  double sidt_h = calc_SidT (jd);
  while (sidt_h < 0.0)
    sidt_h += 24.0;
  while (sidt_h > 24.0)
    sidt_h -= 24.0;
  convert_H_HMSMS_time(sidt_h, sidt);
}

/** \brief Calculate geocentric Julian date and heliocentric Julian date using calc_GJD and calc_HJD, respectively.
 * \param unid Pointer to datestruct that contains the current UT date.
 * \param unit Pointer to timestruct that contains the current UT time.
 * \param ra Current telescope right ascension.
 * \param dec Current telescope declination.
 * \param gjd Pointer to where GJD will be stored.
 * \param hjd Pointer to hwere HJD will be stored.
 * \return (void)
 */
void calc_juliandays(struct datestruct *unid, struct timestruct *unit, struct rastruct *targ_ra, struct decstruct *targ_dec, double *gjd, double *hjd)
{
  *gjd = calc_GJD (unid, unit);
  calc_sun (*gjd, targ_ra, targ_dec, NULL, NULL, hjd);
}

/** \brief Send time_msg with current times to controller.
 * \return (void)
 */
void send_times()
{
  struct act_msg msg;
  msg.mtype = MT_TIME;
  memcpy(&msg.content.msg_time, &G_msg_time, sizeof(struct act_msg_time));
  if (send(G_netsock_fd, &msg, sizeof(struct act_msg), MSG_DONTWAIT) == -1)
    act_log_error(act_log_msg("Failed to send telescope coordinates - %s", strerror(errno)));
/*  else
  {
    char *tmpstr = time_to_str(&G_msg_time.sidt);
    act_log_normal(act_log_msg("SIDT sent: %s", tmpstr));
    free(tmpstr);
  }*/
}

/** \brief If GUI objects are not embedded within a socket (via a plug), request socket information from controller.
 * \return TRUE if request was successfully sent, otherwise <0.
 */
int request_guisock()
{
  struct act_msg guimsg;
  guimsg.mtype = MT_GUISOCK;
  memset(&guimsg.content.msg_guisock, 0, sizeof(struct act_msg_guisock));
  if (send(G_netsock_fd, &guimsg, sizeof(struct act_msg), 0) == -1)
  {
    act_log_error(act_log_msg("Failed to send GUI socket request - %s.", strerror(errno)));
    return -1;
  }
  return 1;
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
      cap_msg->service_provides = SERVICE_TIME;
      cap_msg->service_needs = SERVICE_COORD;
      cap_msg->targset_prov = 0;
      cap_msg->datapmt_prov = 0;
      cap_msg->dataccd_prov = 0;
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
      if (guimsg->gui_socket > 0)
      {
        if (gtk_widget_get_parent(G_form_objs.box_main) != NULL)
        {
          act_log_normal(act_log_msg("Received GUI socket (%d), but GUI elements are already embedded. Ignoring the latter."));
          break;
        }
        GtkWidget *plg_new = gtk_plug_new(guimsg->gui_socket);
        act_log_normal(act_log_msg("Received GUI socket %d.", guimsg->gui_socket));
        gtk_container_add(GTK_CONTAINER(plg_new),G_form_objs.box_main);
        g_object_unref(G_OBJECT(G_form_objs.box_main));
        g_signal_connect(G_OBJECT(plg_new),"destroy",G_CALLBACK(destroy_plug),G_form_objs.box_main);
        gtk_widget_show_all(plg_new);
      }
      break;
    }
    case MT_COORD:
    {
      if (G_msg_coord == NULL)
        G_msg_coord = malloc(sizeof(struct act_msg_coord));
      memcpy(G_msg_coord, &msgbuf.content.msg_coord, sizeof(struct act_msg_coord));
      disp_coords();
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

/** \brief Update (normal and big) time and coordinate indicators.
 * \return (void)
 * \todo Generalise for other time zones.
 */
void disp_times()
{
  char *timestr = time_to_str(&(G_msg_time.loct));
  char *datestr = date_to_str(&(G_msg_time.locd));
  char tmpstr[256], tmpmarkup[256];
  sprintf(tmpstr, "%s: %s %s", LOCT_NAME, datestr, timestr);
  free(timestr);
  free(datestr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_loctd), tmpstr);
  if (G_form_objs.lbl_big_loctd != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_loctd), tmpmarkup);
  }
  timestr = time_to_str(&(G_msg_time.unit));
  datestr = date_to_str(&(G_msg_time.unid));
  sprintf(tmpstr, "UT: %s %s", datestr, timestr);
  free(timestr);
  free(datestr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_unitd), tmpstr);
  if (G_form_objs.lbl_big_unitd != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_unitd), tmpmarkup);
  }
  timestr = time_to_str(&(G_msg_time.sidt));
  sprintf(tmpstr, "SIDT: %s", timestr);
  free(timestr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_sidt), tmpstr);
  if (G_form_objs.lbl_big_sidt != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_sidt), tmpmarkup);
  }
  sprintf(tmpstr, "GJD: %15.6lf", G_msg_time.gjd);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_gjd), tmpstr);
  if (G_form_objs.lbl_big_gjd != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_gjd), tmpmarkup);
  }
  sprintf(tmpstr, "HJD: %15.6lf", G_msg_time.hjd);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_hjd), tmpstr);
  if (G_form_objs.lbl_big_hjd != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_hjd), tmpmarkup);
  }
}

void disp_coords()
{
  if (G_msg_coord == NULL)
    return;

  char tmpstr[256], tmpmarkup[256];
  char *coordstr = ra_to_str(&(G_msg_coord->ra));
  sprintf(tmpstr, "RA: %s", coordstr);
  free(coordstr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_ra),tmpstr);
  if (G_form_objs.lbl_big_ra != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_ra), tmpmarkup);
  }
  coordstr = ha_to_str(&(G_msg_coord->ha));
  sprintf(tmpstr, "HA: %s", coordstr);
  free(coordstr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_ha),tmpstr);
  if (G_form_objs.lbl_big_ha != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_ha), tmpmarkup);
  }
  coordstr = dec_to_str(&(G_msg_coord->dec));
  sprintf(tmpstr, "Dec: %s", coordstr);
  free(coordstr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_dec),tmpstr);
  if (G_form_objs.lbl_big_dec != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_dec), tmpmarkup);
  }
  coordstr = alt_to_str(&(G_msg_coord->alt));
  sprintf(tmpstr, "Alt: %s", coordstr);
  free(coordstr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_alt),tmpstr);
  if (G_form_objs.lbl_big_alt != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_alt), tmpmarkup);
  }
  coordstr = azm_to_str(&(G_msg_coord->azm));
  sprintf(tmpstr, "Azm: %s", coordstr);
  free(coordstr);
  gtk_label_set_text(GTK_LABEL(G_form_objs.lbl_azm),tmpstr);
  if (G_form_objs.lbl_big_azm != NULL)
  {
    sprintf(tmpmarkup, "<span foreground=\"green\" size=\"%d\">%s</span>", BIG_FONT_SIZE, tmpstr);
    gtk_label_set_markup(GTK_LABEL(G_form_objs.lbl_big_azm), tmpmarkup);
  }
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

gboolean guicheck_timeout(gpointer user_data)
{
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(user_data)) != NULL;
  if (!main_embedded)
    request_guisock();
  return TRUE;
}

/** \brief Main programme loop.
 * \param user_data Pointer to struct formobjects which contains all data relevant to this function.
 * \return Return FALSE if main loop should not be called again, otherwise always return TRUE.
 *
 * Tasks:
 *  - Check for new incoming messages.
 *  - If main GTK box isn't embedded, request X11 socket info from controller.
 *  - Read/calculate times (local and universal time and date, gjd, hjd)
 *  - Send time information to controller.
 *  - Display times and coordinates on screen.
 */
gboolean timeout(gpointer user_data)
{
  (void)user_data;
  send_times();
/*  struct timeout_objects *to_objs = (struct timeout_objects *)user_data;
  if (to_objs->timeout_period != DEFAULT_LOOP_PERIOD)
  {
    to_objs->timeout_period = DEFAULT_LOOP_PERIOD;
    g_timeout_add(to_objs->timeout_period, timeout, user_data);
    return FALSE;
  }
  if ((G_msg_time.loct.milliseconds % (DEFAULT_LOOP_PERIOD-DEFAULT_LOOP_PERIOD/10) > DEFAULT_LOOP_PERIOD/10) && (G_msg_time.loct.milliseconds < DEFAULT_LOOP_PERIOD))
  {
    to_objs->timeout_period = DEFAULT_LOOP_PERIOD - G_msg_time.loct.milliseconds;
    g_timeout_add(to_objs->timeout_period, timeout, user_data);
    return FALSE;
  }*/
  return TRUE;
}

gboolean disptimes_timeout()
{
  if (!get_meantime(G_time_fd, &G_msg_time.loct, &G_msg_time.locd, &G_msg_time.unit, &G_msg_time.unid))
    act_log_error(act_log_msg("Error reading mean date/time."));
  G_msg_time.gjd = calc_GJD (&G_msg_time.unid, &G_msg_time.unit);
  if (G_msg_coord != NULL)
    calc_sun (G_msg_time.gjd, &G_msg_coord->ra, &G_msg_coord->dec, NULL, NULL, &G_msg_time.hjd);
  calc_sidtime(G_msg_time.gjd, &G_msg_time.sidt);
  disp_times();
  return TRUE;
}

/** \brief Main function.
 * \param argc Number of command-line arguments
 * \param argv Command-line arguments.
 * \return 0 upon success, 2 if incorrect command-line arguments are specified, 1 otherwise.
 * \todo Move gtk_init to before argtable command-line parsing.
 *
 * Tasks:
 *  - Parse command-line arguments.
 *  - Establish network connection with controller.
 *  - Open photometry-and-time driver character device.
 *  - Create GUI.
 *  - Start main programme loop.
 *  - Close all open file descriptors and exit.
 */
int main(int argc, char** argv)
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

  if ((G_netsock_fd = setup_net(host, port)) < 0)
  {
    act_log_error(act_log_msg("Error setting up network connection."));
    return 1;
  }
  fcntl(G_netsock_fd, F_SETOWN, getpid());
  int oflags = fcntl(G_netsock_fd, F_GETFL);
  fcntl(G_netsock_fd, F_SETFL, oflags | FASYNC);

  G_time_fd = open("/dev/" TIME_DEVICE_NAME, O_RDWR);
  if (G_time_fd < 0)
  {
    act_log_error(act_log_msg("Can't open device file: /dev/%s", TIME_DEVICE_NAME));
    close(G_netsock_fd);
    return 1;
  }

  memset(&G_form_objs, 0, sizeof(struct formobjects));
  GtkWidget* box_main = gtk_table_new(2,5,TRUE);
  G_form_objs.box_main = box_main;
  g_object_ref(box_main);

  GtkWidget *btn_loctd = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_loctd = gtk_bin_get_child(GTK_BIN(btn_loctd));
  G_form_objs.lbl_big_loctd = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_loctd, 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_loctd), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_loctd);

  GtkWidget *btn_unitd = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_unitd = gtk_bin_get_child(GTK_BIN(btn_unitd));
  G_form_objs.lbl_big_unitd = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_unitd, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_unitd), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_unitd);

  GtkWidget *btn_sidt = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_sidt = gtk_bin_get_child(GTK_BIN(btn_sidt));
  G_form_objs.lbl_big_sidt = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_sidt, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_sidt), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_sidt);

  GtkWidget *btn_gjd = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_gjd = gtk_bin_get_child(GTK_BIN(btn_gjd));
  G_form_objs.lbl_big_gjd = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_gjd, 3,4,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_gjd), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_gjd);

  GtkWidget *btn_hjd = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_hjd = gtk_bin_get_child(GTK_BIN(btn_hjd));
  G_form_objs.lbl_big_hjd = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_hjd, 4,5,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_hjd), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_hjd);

  GtkWidget *btn_ra = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_ra = gtk_bin_get_child(GTK_BIN(btn_ra));
  G_form_objs.lbl_big_ra = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_ra, 0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_ra), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_ra);

  GtkWidget *btn_ha = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_ha = gtk_bin_get_child(GTK_BIN(btn_ha));
  G_form_objs.lbl_big_ha = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_ha, 1,2,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_ha), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_ha);

  GtkWidget *btn_dec = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_dec = gtk_bin_get_child(GTK_BIN(btn_dec));
  G_form_objs.lbl_big_dec = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_dec, 2,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_dec), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_dec);

  GtkWidget *btn_alt = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_alt = gtk_bin_get_child(GTK_BIN(btn_alt));
  G_form_objs.lbl_big_alt = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_alt, 3,4,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_alt), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_alt);

  GtkWidget *btn_azm = gtk_toggle_button_new_with_label("N/A");
  G_form_objs.lbl_azm = gtk_bin_get_child(GTK_BIN(btn_azm));
  G_form_objs.lbl_big_azm = NULL;
  gtk_table_attach(GTK_TABLE(box_main), btn_azm, 4,5,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 3,3);
  g_signal_connect(G_OBJECT(btn_azm), "toggled", G_CALLBACK(create_big_dialog), &G_form_objs.lbl_big_azm);

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
  
  act_log_normal(act_log_msg("Entering main loop."));
  int disptimes_to_id = g_timeout_add(DISPTIMES_LOOP_PERIOD, disptimes_timeout, NULL);
  struct timeout_objects to_objs;
  to_objs.timeout_period=DEFAULT_LOOP_PERIOD;
  to_objs.timeout_id = g_timeout_add (to_objs.timeout_period, timeout, &to_objs);
  int guicheck_to_id;
  if (GUICHECK_LOOP_PERIOD % 1000 == 0)
    guicheck_to_id = g_timeout_add_seconds(GUICHECK_LOOP_PERIOD/1000, guicheck_timeout, box_main);
  else
    guicheck_to_id = g_timeout_add(GUICHECK_LOOP_PERIOD, guicheck_timeout, box_main);
  gtk_main();
  g_source_remove(disptimes_to_id);
  g_source_remove(to_objs.timeout_period);
  g_source_remove(guicheck_to_id);
  g_source_remove(iowatch_id);
  close(G_netsock_fd);
  close(G_time_fd);
  act_log_normal(act_log_msg("Exiting"));
  return 0;
}
