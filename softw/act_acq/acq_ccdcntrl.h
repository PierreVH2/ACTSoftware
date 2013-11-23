#ifndef ACQ_CCDCNTRL_H
#define ACQ_CCDCNTRL_H

#include <gtk/gtk.h>
#include <mysql/mysql.h>
#include <act_ipc.h>
#include <merlin_driver.h>

/// X centre of aperture on acquisition image in pixels
#define XAPERTURE 172
/// Y centre of aperture on acquisition image in pixels
#define YAPERTURE 110

/// An internal error on the CCD occurred
#define   ccdcntrl_error(objs)    (objs->merlin_stat & CCD_ERROR)
/// An exposure of the CCD has been ordered
#define   ccdcntrl_exp_ord(objs)  (objs->merlin_stat & EXP_ORDERED)
/// The CCD is being exposed
#define   ccdcntrl_integ(objs)    (objs->merlin_stat & CCD_INTEGRATING)
/// The CCD is being read out
#define   ccdcntrl_readout(objs)  (objs->merlin_stat & CCD_READING_OUT)
/// An image is ready to be read
#define   ccdcntrl_imgready(objs) (objs->merlin_stat & IMG_READY)

struct ccdcntrl_objects
{
  GIOChannel *ccd_chan;
  int ccd_watch_id;
  char merlin_stat;
  MYSQL **mysql_conn;
  char *data_dir;

  GtkWidget *evb_ccdstat, *lbl_ccdstat;
  GtkWidget *cmb_window, *cmb_prebin;
  GtkWidget *spn_expt, *spn_repeat;
  GtkWidget *chk_phot_exp, *chk_save;
  GtkWidget *btn_expose;
};

struct ccdcntrl_objects *ccdcntrl_create_objs(MYSQL **conn, GtkWidget *container);
void ccdcntrl_finalise_objs(struct ccdcntrl_objects *ccdcntrl_objs);
void ccdcntrl_get_img_datetime(struct datestruct *unidate, struct timestruct *unitime);
char ccdcntrl_save_image(struct ccdcntrl_objects *ccdcntrl_objs);
char ccdcntrl_write_fits(const char *filename);
unsigned short ccdcntrl_get_max_width();
unsigned short ccdcntrl_get_max_height();
unsigned short ccdcntrl_get_ra_width();
unsigned short ccdcntrl_get_dec_height();
void ccdcntrl_set_coords(struct act_msg_coord *msg);
void ccdcntrl_set_time(struct act_msg_time *msg);
void ccdcntrl_get_ccdcaps(struct act_msg_ccdcap *msg_ccdcap);
int ccdcntrl_start_targset_exp(struct ccdcntrl_objects *ccdcntrl_objs, struct act_msg_targset *msg);
int ccdcntrl_check_targset_exp(struct ccdcntrl_objects *ccdcntrl_objs, struct rastruct *adj_ra, struct decstruct *adj_dec, unsigned char *targ_cent);
int ccdcntrl_start_phot_exp(struct ccdcntrl_objects *ccdcntrl_objs, struct act_msg_dataccd *msg);
int ccdcntrl_check_phot_exp(struct ccdcntrl_objects *ccdcntrl_objs);
void ccdcntrl_cancel_exp(struct ccdcntrl_objects *ccdcntrl_objs);
#endif
