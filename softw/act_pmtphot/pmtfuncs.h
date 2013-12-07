#ifndef PMTFUNCS_H
#define PMTFUNCS_H

#include <stdio.h>
#include <mysql/mysql.h>
#include <pmt_driver.h>
#include <time_driver.h>
#include <act_ipc.h>
#include <gtk/gtk.h>

#define pmt_zero_counts(error)  ((error & PMT_ERR_ZERO) > 0)
#define pmt_high_counts(error)  ((error & PMT_ERR_WARN) > 0)
#define pmt_overillum(error)    ((error & PMT_ERR_OVERILLUM) > 0)
#define pmt_overflow(error)     ((error & PMT_ERR_OVERFLOW) > 0)
#define pmt_time_desync(error)  ((error & PMT_ERR_TIME_SYNC) > 0)
#define pmt_crit_err(error)     ((error & PMT_CRIT_ERR_MASK) > 0)
#define pmt_noncrit_err(error)  ((error & (~PMT_CRIT_ERR_MASK)) > 0)

#define pmt_probing(status)     ((status & PMT_STAT_PROBE) > 0)
#define pmt_integrating(status) ((status & PMT_STAT_BUSY) > 0)
#define pmt_data_ready(status)  ((status & PMT_STAT_DATA_READY) > 0)
#define pmt_error(status)       ((status & PMT_STAT_ERR) > 0)
#define pmt_new_stat(status)    ((status & PMT_STAT_UPDATE) > 0)

struct pmtdetailstruct
{
  GtkWidget *evb_pmt_stat, *lbl_pmt_stat;
  
  int pmtdrv_fd;
  char pmt_stat;
  struct pmt_information pmt_info;

  struct act_msg_pmtcap pmtcaps;
  struct datestruct cur_unidate;
  struct timestruct cur_unitime;
  unsigned long est_counts_s;
  unsigned char error;
};

struct pmtintegstruct
{
  int targid;
  char sky;
  int userid;
  struct filtaper filter;
  struct filtaper aperture;
  struct datestruct start_unidate;
  struct timestruct start_unitime;
  double sample_period_s;
  unsigned long prebin;
  double integt_s;
  double dead_time_s;
  unsigned long repetitions;
  unsigned long counts;
  unsigned char error;
  
  char done;
  void *next;
};

struct pmtdetailstruct *init_pmtdetail(GtkWidget *container);
void finalise_pmtdetail(struct pmtdetailstruct *pmtinfo);
void pmt_reg_checks(struct pmtdetailstruct *pmtdetail);
int check_integ_params(struct pmtdetailstruct *pmtdetail, struct pmtintegstruct *pmtinteg, char *reason);
int pmt_start_integ(struct pmtdetailstruct *pmtdetail, struct pmtintegstruct *pmtinteg);
int pmt_integ_get_data(struct pmtdetailstruct *pmtdetail, struct pmtintegstruct *pmtinteg);
struct pmtintegstruct *free_integ_data(struct pmtintegstruct *pmtinteg);
void pmt_integ_clear_data(struct pmtdetailstruct *pmtdetail);
void pmt_cancel_integ(struct pmtdetailstruct *pmtdetail);
void pmt_set_datetime(struct pmtdetailstruct *pmtdetail, struct datestruct *unidate, struct timestruct *unitime);
void pmt_get_datetime(struct pmtdetailstruct *pmtdetail, struct datestruct *unidate, struct timestruct *unitime);
const char *pmt_get_id(struct pmtdetailstruct *pmtdetail);
unsigned char pmt_get_modes(struct pmtdetailstruct *pmtdetail);
void pmt_get_caps(struct pmtdetailstruct *pmtdetail, struct act_msg_pmtcap *msg_pmtcap);

#endif