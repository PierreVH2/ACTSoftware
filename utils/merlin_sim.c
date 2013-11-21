#include <gtk/gtk.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <string.h>
#include <argtable2.h>
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <fitsio.h>
#include <act_ipc.h>
#include <merlin_driver.h>

#define ONEPI 3.141592653589793

struct fitsload_objects
{
  GtkWidget *spn_coordDec, *spn_coordRA, *spn_raoffset, *spn_decoffset, *lbl_filename, *img_acq;
};

struct sendimg_objects
{
  GtkWidget *spn_coordDec, *spn_coordRA, *spn_raoffset, *spn_decoffset;
  int fd_ccddev;
};

struct sendobsn_objects
{
  GtkWidget *ent_targname, *spn_coordDec, *spn_coordRA;
  int sockfd;
};

struct timeout_objects
{
  GtkWidget *spn_coordDec, *spn_coordRA, *lbl_corrRA, *lbl_corrDec, *box_main;
  int sockfd;
};


enum
{
  ENOERR = 0,
  EGETADDRINF,
  ECONNECT,
  ENONE
};

unsigned char G_image[IMG_LEN];
const char *G_progname;

void convert_D_DMS(double frac_deg, unsigned short *deg, unsigned short *amin, unsigned short *asec)
{
  double tmp_deg = frac_deg;
  while (tmp_deg >= 360.0)
    tmp_deg -= 360.0;
  while (tmp_deg < 0.0)
    tmp_deg += 360.0;
  if (deg != NULL)
    *deg = (unsigned short)floor(tmp_deg);
  tmp_deg = (tmp_deg-floor(tmp_deg)) * 60.0;
  if (amin != NULL)
    *amin = (unsigned short)floor(tmp_deg);
  tmp_deg = (tmp_deg-floor(tmp_deg)) * 60.0;
  if (asec != NULL)
    *asec = (unsigned short)floor(tmp_deg);
}

void convert_H_HMSMS(double frac_hours, unsigned short *hours, unsigned short *minutes, unsigned short *seconds, unsigned short *millisec)
{
  double tmp_hours = fmod(frac_hours, 24.0);
  tmp_hours += tmp_hours < 0.0 ? 24.0 : 0.0;
  if (hours != NULL)
    *hours = (unsigned short)floor(tmp_hours);
  tmp_hours = (tmp_hours-floor(tmp_hours)) * 60.0;
  if (minutes != NULL)
    *minutes = (unsigned short)floor(tmp_hours);
  tmp_hours = (tmp_hours-floor(tmp_hours)) * 60.0;
  if (seconds != NULL)
    *seconds = (unsigned short)floor(tmp_hours);
  tmp_hours = (tmp_hours-floor(tmp_hours)) * 1000.0;
  if (millisec != NULL)
    *millisec = (unsigned short)floor(tmp_hours);
}

int setup_net(const char* host, const char* port)
{
  struct addrinfo hints, *servinfo, *p;
  int sockfd, retval;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  if ((retval = getaddrinfo(host, port, &hints, &servinfo)) != 0) 
    return -EGETADDRINF;

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
    return -ECONNECT;
  freeaddrinfo(servinfo);

  return sockfd;
}

void destroy_plug(GtkWidget *plg_main, gpointer user_data)
{
  g_object_ref(G_OBJECT(user_data));
  gtk_container_remove(GTK_CONTAINER(plg_main),GTK_WIDGET(user_data));
}

unsigned int pixel_from_gray(GdkVisual *vis, unsigned char g)
{
  return (g << vis->red_shift) | (g << vis->green_shift) | (g << vis->blue_shift);
}

void set_image(GtkWidget *img_acq)
{
  GdkVisual *vis = gdk_visual_get_system();
  if (vis == NULL)
  {
    fprintf(stderr, "No valid visual could be found.\n");
    return;
  }
  GdkImage *image_object = gdk_image_new(GDK_IMAGE_FASTEST,vis,CCD_WIDTH_PX,CCD_HEIGHT_PX);
  int i,j;
  for (i=0; i<CCD_HEIGHT_PX; i++)
  {
    for (j=0; j<CCD_WIDTH_PX; j++)
    {
      gdk_image_put_pixel(image_object,j,i,pixel_from_gray(vis,G_image[i*CCD_WIDTH_PX + j]));
    }
  }
  gtk_image_set_from_image(GTK_IMAGE(img_acq),image_object, NULL);
}

unsigned char send_coord(int sockfd, GtkWidget *spn_coordRA, GtkWidget *spn_coordDec)
{
  struct act_msg_coord coord_msg;
  coord_msg.mtype = MT_COORD;
  coord_msg.ra_h = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spn_coordRA));
  coord_msg.dec_d = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spn_coordDec));
  if (send(sockfd, &coord_msg, sizeof(struct act_msg), 0) == -1)
  {
    fprintf(stderr, "[%s] Failed to send coordinates: %s\n", G_progname, strerror(errno));
    return FALSE;
  }
  return TRUE;
}

unsigned char send_time(int sockfd)
{
  time_t rawtime;
  struct tm *syst;
  time(&rawtime);
  syst = localtime(&rawtime);
 
  struct act_msg_time time_msg;
  time_msg.loct.hours = syst->tm_hour;
  time_msg.loct.minutes = syst->tm_min;
  time_msg.loct.seconds = syst->tm_sec;
  time_msg.locd.day = syst->tm_mday-1;
  time_msg.locd.month = syst->tm_mon;
  time_msg.locd.year = syst->tm_year + 1900;
  
  if (send(sockfd, &time_msg, sizeof(struct act_msg), 0) == -1)
  {
    fprintf(stderr, "[%s] Failed to send time: %s\n", G_progname, strerror(errno));
    return FALSE;
  }
  return TRUE;
}

int request_guisock(int sockfd)
{
  struct act_msg_guisock guimsg;
  memset(&guimsg, 0, sizeof(struct act_msg));
  guimsg.mtype = MT_GUISOCK;
  if (send(sockfd, &guimsg, sizeof(struct act_msg), 0) == -1)
  {
    fprintf(stderr, "[%s] send failed: %s\n", G_progname, strerror(errno));
    return -1;
  }
  return 1;
}

int check_messages(struct timeout_objects *objs, unsigned char *main_embedded)
{
  struct act_msg msgbuf;
  memset(&msgbuf, 0, sizeof(struct act_msg));
  int numbytes = recv(objs->sockfd, &msgbuf, sizeof(struct act_msg), MSG_DONTWAIT);
  if ((numbytes == -1) && (errno == EAGAIN))
    return 0;
  if (numbytes == -1)
  {
    fprintf(stderr, "[%s] recv: %s.\n", G_progname, strerror(errno));
    return -1;
  }
  switch (msgbuf.mtype)
  {
    case MT_QUIT:
    {
      fprintf(stdout, "[%s] Received QUIT signal.\n", G_progname);
      gtk_main_quit();
      return 2;
    }
    case MT_CAP:
    {
      struct act_msg_cap *cap_msg = (struct act_msg_cap*)(&msgbuf);
      cap_msg->provides = CAP_COORD_RA | CAP_COORD_DEC | CAP_TIME_LOCT;
      cap_msg->needs = 0;
      cap_msg->obsn_prov = OBSNSTAGE_SEL;
      if (send(objs->sockfd, &msgbuf, sizeof(struct act_msg), 0) == -1)
      {
        fprintf(stderr, "[%s] send failed: %s\n", G_progname, strerror(errno));
        return -1;
      }
      break;
    }
    case MT_STAT:
    {
      memset(&msgbuf, 0, sizeof(struct act_msg));
      msgbuf.mtype = MT_STAT;
      ((struct act_msg_stat *)(&msgbuf))->status = *main_embedded ? STAT_GOOD : STAT_STARTUP;
      if (send(objs->sockfd, &msgbuf, sizeof(struct act_msg), 0) == -1)
      {
        fprintf(stderr, "[%s] send failed: %s\n", G_progname, strerror(errno));
        return -1;
      }
      break;
    }
    case MT_GUISOCK:
    {
      struct act_msg_guisock *guimsg = (struct act_msg_guisock*)(&msgbuf);
      if ((guimsg->gui_socket > 0) && (!(*main_embedded)))
      {  
        GtkWidget *plg_new = gtk_plug_new(guimsg->gui_socket);
        printf("[%s] Received gui socket %d\n", G_progname, guimsg->gui_socket);
        gtk_container_add(GTK_CONTAINER(plg_new),objs->box_main);
        g_object_unref(G_OBJECT(objs->box_main));
        g_signal_connect(G_OBJECT(plg_new),"destroy",G_CALLBACK(destroy_plug),objs->box_main);
        gtk_widget_show_all(plg_new);
        *main_embedded = TRUE;
      }
      break;
    }
    case MT_OBSN:
    {
      struct act_msg_obsn *obsn_msg = (struct act_msg_obsn *)(&msgbuf);
      char tmpstr[50];
      unsigned short new_ra_hmsms[4], new_dec_dms[3];
      convert_H_HMSMS(obsn_msg->ra_h, &new_ra_hmsms[0], &new_ra_hmsms[1], &new_ra_hmsms[2], &new_ra_hmsms[3]);
      convert_D_DMS(obsn_msg->dec_d, &new_dec_dms[0], &new_dec_dms[1], &new_dec_dms[2]);
      sprintf(tmpstr,"%02huh%02hum%02hu.%01hus", new_ra_hmsms[0], new_ra_hmsms[1], new_ra_hmsms[2], new_ra_hmsms[3]/100);
      gtk_label_set_text(GTK_LABEL(objs->lbl_corrRA),tmpstr);
      if (obsn_msg->dec_d < 0.0)
        sprintf(tmpstr, "-%02hd\302\260%02hd\'%02hd\"", 359 - new_dec_dms[0], 59 - new_dec_dms[1], 59 - new_dec_dms[2]);
      else
        sprintf(tmpstr, "Dec  %02hd\302\260%02hd\'%02hd\"", new_dec_dms[0], new_dec_dms[1], new_dec_dms[2]);
      gtk_label_set_text(GTK_LABEL(objs->lbl_corrDec),tmpstr);
      break;
    }
    default:
      if (msgbuf.mtype >= MT_INVAL)
        fprintf(stderr, "[%s] Invalid signal received.\n", G_progname);
  }
  return 1;
}

unsigned char load_fits(const char *filename, double *ra, double *dec)
{
  fitsfile *fp;
  unsigned char *tmpimg;
  long int naxes[2], fpixel[2]={1,1};
  int status=0, bitpix, naxis;
  if (fits_open_file(&fp, filename, READONLY, &status) != 0)
  {
    fprintf(stderr, "[%s] Error opening FITS file \"%s\"\n", G_progname, filename);
    fits_report_error(stderr, status);
    return FALSE;
  }
  if (fits_read_key(fp, TDOUBLE, "RA", ra, NULL, &status) != 0)
  {
    fprintf(stderr, "[%s] Error reading RA FITS file \"%s\"\n", G_progname, filename);
    fits_report_error(stderr, status);
    status=0;
    *ra = 0.0;
  }
  if (fits_read_key(fp, TDOUBLE, "DEC", dec, NULL, &status) != 0)
  {
    fprintf(stderr, "[%s] Error reading RA FITS file \"%s\"\n", G_progname, filename);
    fits_report_error(stderr, status);
    status=0;
    *dec = 0.0;
  }
  if (fits_get_img_param(fp, 2, &bitpix, &naxis, naxes, &status) != 0)
  {
    fprintf(stderr, "[%s] Error reading from FITS file \"%s\"\n", G_progname, filename);
    fits_report_error(stderr, status);
    fits_close_file(fp, &status);
    return FALSE;
  }
  if ((naxis != 2) || (bitpix != BYTE_IMG))
  {
    fprintf(stderr, "[%s] Invalid image in FITS file. The image should be in the primary HDU and should have the type BYTE_IMG\n", G_progname);
    fits_report_error(stderr, status);
    fits_close_file(fp, &status);
    return FALSE;
  }
  if ((naxes[0] != CCD_WIDTH_PX) || (naxes[1] != CCD_HEIGHT_PX))
  {
    fprintf(stderr, "[%s] Image in FITS file has incorrect dimensions (%ldx%ld should be %dx%d)\n", G_progname, naxes[0], naxes[1], CCD_WIDTH_PX, CCD_HEIGHT_PX);
    fits_report_error(stderr, status);
    fits_close_file(fp, &status);
    return FALSE;
  }
  tmpimg = (unsigned char *) malloc(naxes[0]*naxes[1]*sizeof(unsigned char));
  for (fpixel[1] = naxes[1]; fpixel[1] >= 1; fpixel[1]--)
  {
    if (fits_read_pix(fp, TBYTE, fpixel, naxes[0], NULL, &tmpimg[(fpixel[1]-1)*CCD_WIDTH_PX], NULL, &status))
    {
      fprintf(stderr, "[%s] Error reading image from \"%s\" (row %ld)\n", G_progname, filename, naxes[1]-fpixel[1]);
      fits_report_error(stderr, status);
      fits_close_file(fp, &status);
      return FALSE;
    }
  }
  memcpy(G_image, tmpimg, sizeof(G_image));
  free(tmpimg);
  fits_close_file(fp, &status);

  return TRUE;
}

void load_dialog_response(GtkWidget* dialog, gint response_id, gpointer user_data)
{
  if (response_id != GTK_RESPONSE_ACCEPT)
  {
    gtk_widget_destroy(dialog);
    return;
  }
  
  struct fitsload_objects *objs = (struct fitsload_objects *)user_data;
  char *filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (dialog));
  double ra=0.0, dec=0.0;
  unsigned char ret = load_fits(filename, &ra, &dec);
  if (!ret)
  {
    char errstr[50+strlen(filename)];
    sprintf(errstr, "Error loading fits file %s", filename);
    GtkWidget *err_dialog = gtk_message_dialog_new(GTK_WINDOW(dialog), GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL, GTK_MESSAGE_ERROR, GTK_BUTTONS_CANCEL, errstr);
    gtk_window_set_keep_above(GTK_WINDOW(err_dialog), TRUE);
    gtk_widget_show_all(err_dialog);
    gtk_dialog_run(GTK_DIALOG(err_dialog));
    gtk_widget_destroy(err_dialog);
  }
  else
  {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_raoffset),0.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_decoffset),0.0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_coordRA),ra);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(objs->spn_coordDec),dec);
    const char *image_name = strrchr(filename, '/');
    image_name++;
    gtk_label_set_text(GTK_LABEL(objs->lbl_filename), image_name);
    set_image(GTK_WIDGET(objs->img_acq));
  }
  g_free (filename);
  gtk_widget_destroy(dialog);
}

void load_fits_clicked(gpointer user_data)
{
  GtkWidget *dialog;
  dialog = gtk_file_chooser_dialog_new ("Load FITS Image", NULL, GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
//   gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (dialog), "/home/actuser/act_control/acq_images"); // ??? read from global config
  gtk_widget_show_all(dialog);
  g_signal_connect(G_OBJECT(dialog), "response", G_CALLBACK(load_dialog_response), user_data);
}

void offsetRA_changed(GtkWidget *spn_raoffset, gpointer user_data)
{
  char rastr[100];
  double ra_offset = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spn_raoffset));
  char ra_h = floor(fabs(ra_offset)/60.0);
  char ra_m = floor(fmod(fabs(ra_offset)/60.0,1.0) * 60);
  double ra_s = fmod(fabs(ra_offset),1.0) * 60.0;
  if (ra_offset < 0)
    sprintf(rastr, "-%02hhdh%02hhdm%04.1fs", ra_h, ra_m, ra_s);
  else
    sprintf(rastr, "%02hhdh%02hhdm%04.1fs", ra_h, ra_m, ra_s);
  gtk_label_set_text(GTK_LABEL(user_data), rastr);
}

void offsetDec_changed(GtkWidget *spn_decoffset, gpointer user_data)
{
  char decstr[100];
  double dec_offset = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spn_decoffset));
  char dec_d = floor(fabs(dec_offset)/60.0);
  char dec_am = floor(fmod(fabs(dec_offset)/60.0,1.0) * 60);
  char dec_as = floor(fmod(fabs(dec_offset),1.0) * 60.0);
  if (dec_offset < 0)
    sprintf(decstr, "-%02hhd\302\260%02hhd\'%02hhd\"", dec_d, dec_am, dec_as);
  else
    sprintf(decstr, "%02hhd\302\260%02hhd\'%02hhd\"", dec_d, dec_am, dec_as);
  gtk_label_set_text(GTK_LABEL(user_data), decstr);
}

void coordRA_changed(GtkWidget *spn_coordRA, gpointer user_data)
{
  char rastr[100];
  double coord_ra = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spn_coordRA));
  char ra_h = floor(fabs(coord_ra));
  char ra_m = floor(fmod(fabs(coord_ra),1.0) * 60);
  double ra_s = fmod(fabs(coord_ra)*60.0,1.0) * 60.0;
  sprintf(rastr, "%02hhdh%02hhdm%04.1fs", ra_h, ra_m, ra_s);
  gtk_label_set_text(GTK_LABEL(user_data), rastr);
}

void coordDec_changed(GtkWidget *spn_coordDec, gpointer user_data)
{
  char decstr[100];
  double coord_dec = gtk_spin_button_get_value(GTK_SPIN_BUTTON(spn_coordDec));
  char dec_d = floor(fabs(coord_dec));
  char dec_am = floor(fmod(fabs(coord_dec),1.0) * 60);
  char dec_as = floor(fmod(fabs(coord_dec)*60.0,1.0) * 60.0);
  if (coord_dec < 0.0)
    sprintf(decstr, "-%02hhd\302\260%02hhd\'%02hhd\"", dec_d, dec_am, dec_as);
  else
    sprintf(decstr, "%02hhd\302\260%02hhd\'%02hhd\"", dec_d, dec_am, dec_as);
  gtk_label_set_text(GTK_LABEL(user_data), decstr);
}

void send_image(gpointer user_data)
{
  if (user_data == NULL)
    return;
  
  double ccd_res_ra_m = (double)CCD_RA_WIDTH * 12.0 * 60.0 / ONEPI / (double)CCD_WIDTH_PX;
  double ccd_res_dec_am = (double)CCD_DEC_HEIGHT * 180.0 * 60.0 / ONEPI / (double)CCD_HEIGHT_PX;
  
  struct sendimg_objects *objs = (struct sendimg_objects *)user_data;
  double cur_dec = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_coordDec));
  double shift_ra_m = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_raoffset)) * cos(cur_dec * ONEPI / 180.0);
  double shift_dec_am = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_decoffset));
  
  int shift_x = shift_ra_m / ccd_res_ra_m;
  int shift_y = shift_dec_am / ccd_res_dec_am;
  
  unsigned char shifted_image[CCD_WIDTH_PX * CCD_HEIGHT_PX];
  memset(shifted_image, 0, sizeof(shifted_image));
  int row, col;
  for (row=0; row<CCD_HEIGHT_PX; row++)
  {
    for (col=0; col<CCD_WIDTH_PX; col++)
    {
      if ((row+shift_y<CCD_HEIGHT_PX) && (row+shift_y>0) && (col+shift_x<CCD_WIDTH_PX) && (col+shift_x>0))
        shifted_image[(row+shift_y)*CCD_WIDTH_PX+(col+shift_x)] = G_image[row*CCD_WIDTH_PX+col];
    }
  }
  
  if (ioctl(objs->fd_ccddev, IOCTL_SET_IMAGE, (unsigned long)shifted_image) <= 0)
    fprintf(stderr, "[%s] Error sending simulated image to CCD driver.\n", G_progname);
}

void send_obsn(gpointer user_data)
{
  if (user_data == NULL)
    return;
  
  struct sendobsn_objects *objs = (struct sendobsn_objects *)user_data;
  struct act_msg_obsn obsn_msg;
  memset(&obsn_msg, 0, sizeof(obsn_msg));
  obsn_msg.mtype = MT_OBSN;
  obsn_msg.status = OBSNSTAT_MODEAUTO;
  obsn_msg.obsn_stage = OBSNSTAGE_SEL;
  obsn_msg.ra_h = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_coordRA));
  obsn_msg.dec_d = gtk_spin_button_get_value(GTK_SPIN_BUTTON(objs->spn_coordDec));
  strncpy(obsn_msg.targ_name, gtk_entry_get_text(GTK_ENTRY(objs->ent_targname)), sizeof(obsn_msg.targ_name));
  if (send(objs->sockfd, &obsn_msg, sizeof(struct act_msg), 0) == -1)
    fprintf(stderr, "[%s] send failed: %s\n", G_progname, strerror(errno));
}

gboolean timeout(gpointer user_data)
{
  struct timeout_objects *objs = (struct timeout_objects *)user_data;
  
  int ret;
  unsigned char main_embedded = gtk_widget_get_parent(objs->box_main) != NULL;
  while ((ret = check_messages(objs,&main_embedded)) > 0)
  {
    if (ret == 2)  //QUIT signal recieved
      return FALSE;
  }
  if (!main_embedded)
    request_guisock(objs->sockfd);
  
  send_time(objs->sockfd);
  send_coord(objs->sockfd, objs->spn_coordRA, objs->spn_coordDec);
  return TRUE;
}

int main(int argc, char **argv)
{
  // Parse command-line options
  gtk_init (&argc, &argv);
  
  // Parse command-line options
  const char *host, *port;
  G_progname = argv[0];
  struct arg_str *addrarg = arg_str1("a", "addr", "<str>", "The host to connect to. May be a hostname, IP4 address or IP6 address.");
  struct arg_str *portarg = arg_str1("p", "port", "<str>", "The port to connect to. Must be an unsigned short integer.");
  struct arg_file *configarg = arg_file0("c", "config", "<filename>", "The master configuration file (unused).");
  struct arg_end *endargs = arg_end(10);
  void* argtable[] = {addrarg, portarg, configarg, endargs};
  if (arg_nullcheck(argtable) != 0)
    fprintf(stderr, "[%s] Argument parsing error: insufficient memory\n", G_progname);
  int argparse_errors = arg_parse(argc,argv,argtable);
  if (argparse_errors != 0)
  {
    arg_print_errors(stderr,endargs,G_progname);
    exit(EXIT_FAILURE);
  }
  host = addrarg->sval[0];
  port = portarg->sval[0];
  arg_freetable(argtable,sizeof(argtable)/sizeof(argtable[0]));

  int netsockfd;
  if ((netsockfd = setup_net(host, port)) < 0)
  {
    fprintf(stderr, "[%s] Error setting up network connection: %d. Exiting\n", G_progname, netsockfd);
    return 1;
  }

  int fd_ccddev = open(CCD_DEVICE_NAME, O_RDWR);
  if (fd_ccddev < 0)
  {
    fprintf(stderr, "[%s] Error: Can't open character device %s (code %d)\n", G_progname, CCD_DEVICE_NAME, fd_ccddev);
    exit(EXIT_FAILURE);
  }
  
  memset(G_image, 0, sizeof(G_image));

  // Do all GTK-related widget creation and -initialisation
  GtkWidget* box_main = gtk_vbox_new(0,FALSE);
  g_object_ref(box_main);
  
  GtkWidget *img_acq = gtk_image_new();
  gtk_box_pack_start(GTK_BOX(box_main),img_acq,TRUE,TRUE,0);
  
  GtkWidget *box_acqcntrl = gtk_table_new(3,7,FALSE);
  gtk_box_pack_start(GTK_BOX(box_main),box_acqcntrl,TRUE,TRUE,2);
  GtkWidget *btn_fitsload = gtk_button_new_with_label("Load FITS");
  gtk_table_attach(GTK_TABLE(box_acqcntrl), btn_fitsload, 0,3,0,1,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *lbl_filename = gtk_label_new("NONE");
  gtk_table_attach(GTK_TABLE(box_acqcntrl), lbl_filename, 4,6,0,1,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_table_attach(GTK_TABLE(box_acqcntrl), gtk_label_new("RA offset m"), 0,2,1,2,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *spn_raoffset = gtk_spin_button_new_with_range(-1440,1440,0.00166666666667);
  gtk_table_attach(GTK_TABLE(box_acqcntrl), spn_raoffset, 2,4,1,2,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *lbl_raoffset = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_acqcntrl), lbl_raoffset, 4,6,1,2,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_table_attach(GTK_TABLE(box_acqcntrl), gtk_label_new("Dec offset \'"), 0,2,2,3,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *spn_decoffset = gtk_spin_button_new_with_range(-10800,10800,0.01666666666666667);
  gtk_table_attach(GTK_TABLE(box_acqcntrl), spn_decoffset, 2,4,2,3,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *lbl_decoffset = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_acqcntrl), lbl_decoffset, 4,6,2,3,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *btn_sendimg = gtk_button_new_with_label("Send Img");
  gtk_table_attach(GTK_TABLE(box_acqcntrl), btn_sendimg, 6,7,0,3,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  
  gtk_box_pack_start(GTK_BOX(box_main),gtk_hseparator_new(),TRUE,TRUE,2);
  
  GtkWidget *box_coord = gtk_table_new(2,3,FALSE);
  gtk_box_pack_start(GTK_BOX(box_main),box_coord,TRUE,TRUE,2);
  gtk_table_attach(GTK_TABLE(box_coord), gtk_label_new("RA h"), 0,1,0,1,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *spn_coordRA = gtk_spin_button_new_with_range(0.0,24.0,2.7e-5);
  // So the value-changed event is properly triggered later
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_coordRA),1.0);
  //
  gtk_table_attach(GTK_TABLE(box_coord), spn_coordRA, 1,2,0,1,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *lbl_coordRA = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_coord), lbl_coordRA, 2,3,0,1,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  gtk_table_attach(GTK_TABLE(box_coord), gtk_label_new("Dec \302\260"), 0,1,1,2,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *spn_coordDec = gtk_spin_button_new_with_range(-90.0,90.0,2.7e-4);
  gtk_table_attach(GTK_TABLE(box_coord), spn_coordDec, 1,2,1,2,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  GtkWidget *lbl_coordDec = gtk_label_new("");
  gtk_table_attach(GTK_TABLE(box_coord), lbl_coordDec, 2,3,1,2,GTK_FILL|GTK_EXPAND, GTK_FILL|GTK_EXPAND, 0, 0);
  
  gtk_box_pack_start(GTK_BOX(box_main),gtk_hseparator_new(),TRUE,TRUE,2);
  
  GtkWidget *box_obsn = gtk_hbox_new(0,FALSE);
  gtk_box_pack_start(GTK_BOX(box_main),box_obsn,TRUE,TRUE,2);
  gtk_box_pack_start(GTK_BOX(box_obsn),gtk_label_new("Target name"),TRUE,TRUE,2);
  GtkWidget *ent_targname = gtk_entry_new();
  gtk_box_pack_start(GTK_BOX(box_obsn),ent_targname,TRUE,TRUE,2);
  GtkWidget *btn_sendobsn = gtk_button_new_with_label("Send Observation");
  gtk_box_pack_start(GTK_BOX(box_obsn),btn_sendobsn,TRUE,TRUE,2);
  
  GtkWidget *box_coordcorr = gtk_hbox_new(0,FALSE);
  gtk_box_pack_start(GTK_BOX(box_main),box_coordcorr,TRUE,TRUE,0);
  gtk_box_pack_start(GTK_BOX(box_coordcorr), gtk_label_new("Correction:"),TRUE,TRUE,0);
  GtkWidget *lbl_corrRA = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box_coordcorr), lbl_corrRA, TRUE, TRUE, 0);
  GtkWidget *lbl_corrDec = gtk_label_new("");
  gtk_box_pack_start(GTK_BOX(box_coordcorr), lbl_corrDec, TRUE, TRUE, 0);

  struct fitsload_objects fitsload_objs;
  fitsload_objs.spn_coordRA = spn_coordRA;
  fitsload_objs.spn_coordDec = spn_coordDec;
  fitsload_objs.spn_raoffset = spn_raoffset;
  fitsload_objs.spn_decoffset = spn_decoffset;
  fitsload_objs.lbl_filename = lbl_filename;
  fitsload_objs.img_acq = img_acq;
  
  struct sendimg_objects sendimg_objs;
  sendimg_objs.spn_coordRA = spn_coordRA;
  sendimg_objs.spn_coordDec = spn_coordDec;
  sendimg_objs.spn_raoffset = spn_raoffset;
  sendimg_objs.spn_decoffset = spn_decoffset;
  sendimg_objs.fd_ccddev = fd_ccddev;
  
  struct sendobsn_objects sendobsn_objs;
  sendobsn_objs.ent_targname = ent_targname;
  sendobsn_objs.spn_coordRA = spn_coordRA;
  sendobsn_objs.spn_coordDec = spn_coordDec;
  sendobsn_objs.sockfd = netsockfd;
  
  struct timeout_objects timeout_objs;
  timeout_objs.spn_coordRA = spn_coordRA;
  timeout_objs.spn_coordDec = spn_coordDec;
  timeout_objs.lbl_corrRA = lbl_corrRA;
  timeout_objs.lbl_corrDec = lbl_corrDec;
  timeout_objs.box_main = box_main;
  timeout_objs.sockfd = netsockfd;
  
//   GtkWidget *wnd_main = gtk_window_new(GTK_WINDOW_TOPLEVEL);
//   gtk_container_add(GTK_CONTAINER(wnd_main),box_main);
//   g_signal_connect(G_OBJECT(wnd_main),"destroy",G_CALLBACK(gtk_main_quit),NULL);
  g_signal_connect_swapped(G_OBJECT(btn_fitsload),"clicked",G_CALLBACK(load_fits_clicked),&fitsload_objs);
  g_signal_connect_swapped(G_OBJECT(btn_sendimg),"clicked",G_CALLBACK(send_image),&sendimg_objs);
  g_signal_connect_swapped(G_OBJECT(btn_sendobsn),"clicked",G_CALLBACK(send_obsn),&sendobsn_objs);
  g_signal_connect(G_OBJECT(spn_raoffset),"value-changed",G_CALLBACK(offsetRA_changed),lbl_raoffset);
  g_signal_connect(G_OBJECT(spn_decoffset),"value-changed",G_CALLBACK(offsetDec_changed),lbl_decoffset);
  g_signal_connect(G_OBJECT(spn_coordRA),"value-changed",G_CALLBACK(coordRA_changed),lbl_coordRA);
  g_signal_connect(G_OBJECT(spn_coordDec),"value-changed",G_CALLBACK(coordDec_changed),lbl_coordDec);
  g_timeout_add_seconds(1, timeout, &timeout_objs);
  
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_raoffset),0.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_decoffset),0.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_coordRA),0.0);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(spn_coordDec),0.0);
  
  gtk_widget_show_all(box_main);
  set_image(img_acq);
  
  gtk_main();
  close(fd_ccddev);
  close(netsockfd);
  return 0;
}

