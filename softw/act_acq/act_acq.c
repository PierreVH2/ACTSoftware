#include <gtk/gtk.h>
#include "imgdisp.h"
#include "acq_net.h"
#include "acq_store.h"
#include "ccd_cntrl.h"
#include "ccd_img.h"

enum
{
  MODE_IDLE,
  MODE_MANUAL_EXP,
  MODE_TARGSET_EXP,
  MODE_TARGSET_CENT,
  MODE_DATACCD_EXP
};

struct acq_objects
{
  gint mode;
  CcdCntrl *cntrl;
  AcqStore *store;
  AcqNet *net;
  
  GtkWidget *box_main;
  GtkWidget *imgdisp;
  
  GtkWidget *prog_stat;
  GtkWidget *lbl_ccd_stat;
  GtkWidget *lbl_exp_rem;
  
  GtkWidget *lbl_store_stat;
  GtkWidget *btn_expose;
  GtkWidget *btn_cancel;
};

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
   *  if (to_objs->timeout_period != DEFAULT_LOOP_PERIOD)
   *  {
   *    to_objs->timeout_period = DEFAULT_LOOP_PERIOD;
   *    g_timeout_add(to_objs->timeout_period, timeout, user_data);
   *    return FALSE;
}
if ((G_msg_time.loct.milliseconds % (DEFAULT_LOOP_PERIOD-DEFAULT_LOOP_PERIOD/10) > DEFAULT_LOOP_PERIOD/10) && (G_msg_time.loct.milliseconds < DEFAULT_LOOP_PERIOD))
{
to_objs->timeout_period = DEFAULT_LOOP_PERIOD - G_msg_time.loct.milliseconds;
g_timeout_add(to_objs->timeout_period, timeout, user_data);
return FALSE;
}*/
  return TRUE;
}








gboolean imgdisp_mouse_move_equat(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_view)
{
  gfloat ra_h = imgdisp_coord_ra(imgdisp, motdata->x, motdata->y);
  gfloat dec_d = imgdisp_coord_dec(imgdisp, motdata->x, motdata->y);
  
  struct rastruct ra;
  convert_H_HMSMS_ra(ra_h, &ra);
  char *ra_str = ra_to_str(&ra);
  struct decstruct dec;
  convert_D_DMS_dec(dec_d, &dec);
  char *dec_str = dec_to_str(&dec);
  
  char str[256];
  sprintf(str, "RA: %10s\nDec: %10s", ra_str, dec_str);
  gtk_label_set_text(GTK_LABEL(lbl_coord), str);
  free(ra_str);
  free(dec_str);
  
  return FALSE;
}

gboolean imgdisp_mouse_move_view(GtkWidget* imgdisp, GdkEventMotion* motdata, gpointer lbl_mouse_equat)
{
  glong pixel_x = imgdisp_coord_pixel_x(imgdisp, motdata->x, motdata->y);
  glong pixel_y = imgdisp_coord_pixel_y(imgdisp, motdata->x, motdata->y);
  gchar str[256];
  sprintf(str, "X: %10lu\nY: %10lu", pixel_x, pixel_y);
  gtk_label_set_text(GTK_LABEL(mouse_view), str);
  return FALSE;
}

gboolean guicheck_timeout(gpointer user_data)
{
  struct acq_objects *objs = (struct acq_objects *)user_data;
  unsigned char main_embedded = gtk_widget_get_parent(GTK_WIDGET(objs->box_main)) != NULL;
  
  if (!main_embedded)
    request_guisock();
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
  
  CcdCntrl *cntrl = ccd_cntrl_new();
  if (cntrl == NULL)
  {
    act_log_error(act_log_msg("Failed to create CCD control object"));
    return 1;
  }
  
  AcqStore *store = acq_store_new(host);
  if (store == NULL)
  {
    act_log_error(act_log_msg("Failed to create database storage link"));
    g_object_unref(cntrl);
    return 1;
  }
  
  AcqNet *net = acq_net_new(host, port);
  if (net == NULL)
  {
    act_log_error(act_log_msg("Failed to establish network connection."));
    g_object_unref(cntrl);
    g_object_unref(store);
    return 1;
  }
  
  // Create GUI
  GtkWidget *box_main = gtk_vbox_new(FALSE, TABLE_PADDING);
  GtkWidget *imgdisp  = imgdisp_new();
  gtk_box_pack_start(GTK_BOX(box_main), imgdisp, TRUE, TRUE, TABLE_PADDING);
  GtkWidget* box_controls = gtk_table_new(3,3,TRUE);
  gtk_box_pack_start(GTK_BOX(box_main), box_controls, TRUE, TRUE, TABLE_PADDING);
  
  gtk_widget_set_size_request(imgdisp, ccd_cntrl_get_max_width(cntrl), ccd_cntrl_get_max_height(cntrl));
  imgdisp_set_window(imgdisp, 0, 0, ccd_cntrl_get_max_width(cntrl), ccd_cntrl_get_max_height(cntrl));
  
  GtkWidget *lbl_mouse_view = gtk_label_new("X:           \nY:           ");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_mouse_view, 0,1,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *lbl_mouse_equat = gtk_label_new("RA:           \nDec:           ");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_mouse_equat, 1,2,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_view_param = gtk_button_new_with_label("View...");
  gtk_table_attach(GTK_TABLE(box_controls), btn_view_param, 2,3,0,1, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *lbl_prog_stat = gtk_label_new("IDLE");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_prog_stat, 0,1,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *lbl_ccd_stat = gtk_label_new("CCD OK");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_ccd_stat, 1,2,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *lbl_exp_rem = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_exp_trem, 2,3,1,2, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  GtkWidget *lbl_store_stat = gtk_label_new("Store OK");
  gtk_table_attach(GTK_TABLE(box_controls), lbl_auto, 0,1,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_expose = gtk_button_new_with_label("Expose...");
  gtk_table_attach(GTK_TABLE(box_controls), btn_expose, 1,2,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  GtkWidget *btn_cancel = gtk_button_new_with_label("Cancel");
  gtk_table_attach(GTK_TABLE(box_controls), btn_cancel, 2,3,2,3, GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, TABLE_PADDING, TABLE_PADDING);
  
  void acq_net_set_ccdcap_ready(AcqNet *acq_net, gboolean ready);
  void acq_net_set_min_exp_t_s(AcqNet *acq_net, gfloat exp_t);
  void acq_net_set_max_exp_t_s(AcqNe *acq_net, gfloat exp_t);
  void acq_net_set_ccd_id(AcqNet *acq_net, const gchar *ccd_id);
  
  
  struct acq_objects objs = {
    .box_main = box_main,
    .imgdisp = imgdisp,
    .lbl_prog_stat = lbl_prog_stat,
    .lbl_ccd_stat = lbl_ccd_stat,
    .lbl_exp_rem = lbl_exp_rem,
    .lbl_store_stat = lbl_store_stat,
    .btn_expose = btn_expose;
    .btn_cancel = btn_cancel;
  };
  
  // Connect signals
  g_signal_connect (G_OBJECT(btn_view_param), "click", G_CALLBACK (view_param_click), imgdisp);
  g_signal_connect (G_OBJECT(btn_expose), "click", G_CALLBACK(expose_click), &objs);
  g_signal_connect (G_OBJECT(btn_cancel), "click", G_CALLBACK(cancel_click), &objs);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (imgdisp_mouse_move_view), lbl_mouse_view);
  g_signal_connect (G_OBJECT(imgdisp), "motion-notify-event", G_CALLBACK (imgdisp_mouse_move_equat), lbl_mouse_equat);
  g_signal_connect (G_OBJECT(ccd_cntrl), "ccd-stat-update", G_CALLBACK (ccd_stat_update), &objs);
  g_signal_connect (G_OBJECT(ccd_cntrl), "ccd-new-image", G_CALLBACK (ccd_new_image), &objs);
  g_signal_connect (G_OBJECT(acq_store), "store-status-update", G_CALLBACK(store_stat_update), lbl_store_stat);
  g_signal_connect (G_OBJECT(acq_net), "coord-received", G_CALLBACK(coord_received), ccd_cntrl);
  g_signal_connect (G_OBJECT(acq_net), "gui-socket", G_CALLBACK(guisock_received), box_main);
  g_signal_connect (G_OBJECT(acq_net), "change-user", G_CALLBACK(change_user), &objs);
  g_signal_connect (G_OBJECT(acq_net), "change-target", G_CALLBACK(change_target), &objs);
  g_signal_connect (G_OBJECT(acq_net), "targset-start", G_CALLBACK(targset_start), &objs);
  g_signal_connect (G_OBJECT(acq_net), "targset-stop", G_CALLBACK(targset-stop), &objs);
  g_signal_connect (G_OBJECT(acq_net), "data-ccd-start", G_CALLBACK(data_ccd_start), &objs);
  g_signal_connect (G_OBJECT(acq_net), "data-ccd-stop", G_CALLBACK(data_ccd_stop), &objs);
  guicheck_to_id = g_timeout_add(GUICHECK_LOOP_PERIOD, guicheck_timeout, &objs);
  
  act_log_debug(act_log_msg("Entering main loop."));
  gtk_main();
  
  act_log_normal(act_log_msg("Exiting"));
  g_object_unref(net);
  g_object_unref(store);
  g_object_unref(cntrl);
  return 0;
}
