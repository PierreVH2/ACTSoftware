#include "pmtfuncs.h"
#include <act_ipc.h>
#include <act_log.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include <pmt_driver.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>

/**
 * TODO:
 *  - In check_integ_params removed constraint that
 *    integ sample_period_s % pmtcaps min_sample_period_s =0
 *    It was causing perfectly valid sample periods to be rejected, but is necessary
 *    and must be re-introduced
 */

void update_stat_ind(struct pmtdetailstruct *pmtdetail);

struct pmtdetailstruct *init_pmtdetail(GtkWidget *container)
{
  struct pmtdetailstruct *pmtdetail = malloc(sizeof(struct pmtdetailstruct));
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Could not allocate memory for PMT information structure."));
    return NULL;
  }
  
  int pmt_fd = open("/dev/" PMT_DEVICE_NAME, O_RDWR);
  if (pmt_fd <= 0)
  {
    act_log_error(act_log_msg("Could not open PMT device driver character device %s - %s.", "/dev/" PMT_DEVICE_NAME, strerror(pmt_fd)));
    free(pmtdetail);
    return NULL;
  }
  pmtdetail->pmtdrv_fd = pmt_fd;
  char pmt_stat;
  int ret = read(pmt_fd, &pmt_stat, 1);
  if (ret <= 0)
  {
    act_log_error(act_log_msg("Error reading status from PMT driver - %s.", strerror(ret)));
    close(pmt_fd);
    free(pmtdetail);
    return NULL;
  }
  pmtdetail->pmt_stat = pmt_stat;
  ret = ioctl(pmt_fd, IOCTL_GET_INFO, &pmtdetail->pmt_info);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to get PMT information - %s.", strerror(ret)));
    close (pmt_fd);
    free(pmtdetail);
    return NULL;
  }
  if (pmt_data_ready(pmt_stat))
  {
    act_log_debug(act_log_msg("Data found in queue while starting up. Discarding data."));
    ret = 1;
    struct pmt_integ_data new_data;
    while (ret >= 0)
      ret = ioctl(pmtdetail->pmtdrv_fd, IOCTL_GET_INTEG_DATA, &new_data);
  }
  struct pmt_integ_data cur_data;
  ret = ioctl(pmt_fd, IOCTL_GET_CUR_INTEG, &cur_data);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to get PMT current data - %s.", strerror(ret)));
    close (pmt_fd);
    free(pmtdetail);
    return NULL;
  }
  snprintf(pmtdetail->pmtcaps.pmt_id, IPC_MAX_INSTRID_LEN-1, "%s", pmtdetail->pmt_info.pmt_id);
  pmtdetail->pmtcaps.datapmt_stage = DATAPMT_PHOTOM;
  pmtdetail->pmtcaps.pmt_mode = pmtdetail->pmt_info.modes;
  pmtdetail->pmtcaps.min_sample_period_s = pmtdetail->pmt_info.min_sample_period_ns / 1000000000.0;
  pmtdetail->pmtcaps.max_sample_period_s = pmtdetail->pmt_info.max_sample_period_ns / 1000000000.0;
  pmtdetail->est_counts_s = cur_data.counts / cur_data.sample_period_ns;
  pmtdetail->error = cur_data.error;
  
  int i;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    pmtdetail->pmtcaps.filters[i].db_id = -1;
    pmtdetail->pmtcaps.apertures[i].db_id = -1;
  }

  
  time_t systime_sec = time(NULL);
  struct tm *timedate = gmtime(&systime_sec);
  pmtdetail->cur_unidate.year = timedate->tm_year+1900;
  pmtdetail->cur_unidate.month = timedate->tm_mon;
  pmtdetail->cur_unidate.day = timedate->tm_mday-1;
  pmtdetail->cur_unitime.hours = timedate->tm_hour;
  pmtdetail->cur_unitime.minutes = timedate->tm_min;
  pmtdetail->cur_unitime.seconds = timedate->tm_sec;
  
  pmtdetail->evb_pmt_stat = gtk_event_box_new();
  g_object_ref(pmtdetail->evb_pmt_stat);
  gtk_container_add(GTK_CONTAINER(container),pmtdetail->evb_pmt_stat);
  pmtdetail->lbl_pmt_stat = gtk_label_new("");
  g_object_ref(pmtdetail->lbl_pmt_stat);
  gtk_container_add(GTK_CONTAINER(pmtdetail->evb_pmt_stat),pmtdetail->lbl_pmt_stat);
  update_stat_ind(pmtdetail);
  
  return pmtdetail;
}

void finalise_pmtdetail(struct pmtdetailstruct *pmtdetail)
{
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  g_object_unref(pmtdetail->evb_pmt_stat);
  g_object_unref(pmtdetail->lbl_pmt_stat);
  if (pmtdetail->pmtdrv_fd >= 0)
  {
    pmt_cancel_integ(pmtdetail);
    close(pmtdetail->pmtdrv_fd);
  }
}

void pmt_reg_checks(struct pmtdetailstruct *pmtdetail)
{
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }

  unsigned char pmt_stat;
  int ret = read(pmtdetail->pmtdrv_fd, &pmt_stat, 1);
  if (ret <= 0)
  {
    act_log_error(act_log_msg("Error reading status from PMT driver - %s.", strerror(ret)));
    pmt_stat |= PMT_STAT_ERR;
    pmtdetail->pmt_stat = pmt_stat;
    update_stat_ind(pmtdetail);
    return;
  }
  pmtdetail->pmt_stat = pmt_stat;
  act_log_debug(act_log_msg("PMT status: %hhd", pmt_stat));
  
  if (!pmt_integrating(pmt_stat))
  {
    struct pmt_integ_data new_data;
    ret = ioctl(pmtdetail->pmtdrv_fd, IOCTL_GET_CUR_INTEG, &new_data);
    if (ret < 0)
    {
      act_log_error(act_log_msg("Error reading current integration data from PMT driver - %s.", strerror(ret)));
      pmtdetail->pmt_stat |= pmt_stat;
      pmtdetail->error = 0;
    }
    else
    {
      if (new_data.sample_period_ns == 0)
        pmtdetail->est_counts_s = 0;
      else
        pmtdetail->est_counts_s = new_data.counts * (1000000000 / new_data.sample_period_ns);
      pmtdetail->error = new_data.error;
    }
  }
  
  if (pmt_new_stat(pmt_stat))
    update_stat_ind(pmtdetail);
}

int check_integ_params(struct pmtdetailstruct *pmtdetail, struct pmtintegstruct *pmtinteg, char *reason)
{
  if ((pmtdetail == NULL) || (pmtinteg == NULL) || (reason == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  
  int ret = 1, reason_len = 0;
  if (pmt_probing(pmtdetail->pmt_stat) || pmt_integrating(pmtdetail->pmt_stat))
  {
    act_log_error(act_log_msg("PMT is currently busy."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "PMT is currently busy.\n");
    ret = 0;
  }
  if (pmtdetail->pmt_stat & PMT_STAT_ERR)
  {
    if (pmt_crit_err(pmtdetail->error))
    {
      act_log_error(act_log_msg("A critical error has been raised on the PMT (%hhd). Cannot start an integration until it clears.", pmtdetail->error));
      reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "A critical error has been raised on the PMT.\n");
      ret = 0;
    }
    else
      act_log_normal(act_log_msg("An error has been raised on the PMT, but it is not critical (code %hhu). Integration will continue.", pmtdetail->error));
  }
  
  if (pmtinteg->targid < 1)
  {
    act_log_error(act_log_msg("Invalid target number."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid target number.\n");
    ret = 0;
  }
  if (pmtinteg->userid < 1)
  {
    act_log_error(act_log_msg("Invalid user number."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid user number.\n");
    ret = 0;
  }
  
  if (pmtinteg->filter.db_id < 0)
  {
    act_log_error(act_log_msg("Invalid filter number."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid filter number.\n");
    ret = 0;
  }
  if (pmtinteg->aperture.db_id < 0)
  {
    act_log_error(act_log_msg("Invalid aperture number."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid aperture number.\n");
    ret = 0;
  }
  
  unsigned long sample_length = round(pmtinteg->sample_period_s*1000000000.0/pmtdetail->pmt_info.min_sample_period_ns);
//   unsigned long sample_period_ns = 1000000000/((unsigned long)(1./pmtinteg->sample_period_s));
  act_log_debug(act_log_msg("Sample length: %lu (%lf %lu)", sample_length, pmtinteg->sample_period_s, pmtdetail->pmt_info.min_sample_period_ns));
  if (sample_length < 1)
  {
    act_log_error(act_log_msg("Invalid sample length (%lu)", sample_length));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid sample length.\n");
    ret = 0;
  }
//   if ((sample_period_ns > pmtdetail->pmt_info.max_sample_period_ns) || (sample_period_ns < pmtdetail->pmt_info.min_sample_period_ns) || (sample_period_ns%pmtdetail->pmt_info.min_sample_period_ns != 0))
//   {
//     act_log_error(act_log_msg("Invalid sample period (%f %f %lu %lu, %lu, %lu, %lu)", pmtinteg->sample_period_s, 1./pmtinteg->sample_period_s, (unsigned long)(1./pmtinteg->sample_period_s), sample_period_ns, pmtdetail->pmt_info.max_sample_period_ns, pmtdetail->pmt_info.min_sample_period_ns, sample_period_ns%pmtdetail->pmt_info.min_sample_period_ns));
//     reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid sample period.\n");
//     ret = 0;
//   }
  if (pmtinteg->prebin < 1)
  {
    act_log_error(act_log_msg("Invalid prebinning number."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid prebinning number.\n");
    ret = 0;
  }
  if (pmtinteg->repetitions < 1)
  {
    act_log_error(act_log_msg("Invalid number of repetitions."));
    reason_len += snprintf(&reason[reason_len], sizeof(reason)-reason_len-1, "Invalid number of repetitions.\n");
    ret = 0;
  }
  
  return ret;
}

int pmt_start_integ(struct pmtdetailstruct *pmtdetail, struct pmtintegstruct *pmtinteg)
{
  if ((pmtdetail == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }
  
  struct pmt_command cmd = 
  { 
    .mode = PMT_MODE_INTEG,
    .sample_length = round(pmtinteg->sample_period_s*1000000000.0/pmtdetail->pmt_info.min_sample_period_ns),
    .prebin_num = pmtinteg->prebin,
    .repetitions = pmtinteg->repetitions
  };
  int ret = ioctl(pmtdetail->pmtdrv_fd, IOCTL_INTEG_CMD, &cmd);
  if (ret < 0)
  {
    act_log_error(act_log_msg("Failed to start integration. Check the system log."));
    return -1;
  }
  
  pmtinteg->integt_s = 0.0;
  pmtinteg->dead_time_s = pmtdetail->pmt_info.dead_time_ns / 1000000000.0;
  pmtinteg->counts = 0;
  pmtinteg->error = 0;
  pmtinteg->done = 0;
  pmtinteg->next = NULL;
  memcpy(&pmtinteg->start_unidate, &pmtdetail->cur_unidate, sizeof(struct datestruct));
  memcpy(&pmtinteg->start_unitime, &pmtdetail->cur_unitime, sizeof(struct timestruct));
  return 1;
}

int pmt_integ_get_data(struct pmtdetailstruct *pmtdetail, struct pmtintegstruct *pmtinteg)
{
  if ((pmtdetail == NULL) || (pmtinteg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return -1;
  }

  long num_integ = 0, ret;
  struct pmtintegstruct *lastinteg = pmtinteg;
  while (lastinteg->next != NULL)
    lastinteg = (struct pmtintegstruct *)(lastinteg->next);
  struct pmtintegstruct *nextinteg = NULL;
  struct pmt_integ_data new_data;
  struct timestruct start_time;
  while (pmt_data_ready(pmtdetail->pmt_stat))
  {
    ret = ioctl(pmtdetail->pmtdrv_fd, IOCTL_GET_INTEG_DATA, &new_data);
    if (ret < 0)
    {
      if ((ret == -EAGAIN) || (ret == -EINVAL))
      {
        act_log_debug(act_log_msg("No data available yet."));
        break;
      }
      act_log_error(act_log_msg("Error reading integration data from PMT driver (%d - %s).", ret, strerror(abs(ret))));
      pmtdetail->pmt_stat |= PMT_STAT_ERR;
      pmtdetail->error = 0;
      update_stat_ind(pmtdetail);
      return -1;
    }
    lastinteg->integt_s = (double)new_data.sample_period_ns * new_data.prebin_num / 1000000000.0;
    lastinteg->repetitions = new_data.repetitions;
    lastinteg->counts = new_data.counts;
    lastinteg->error = new_data.error;
    convert_MS_HMSMS_time(new_data.start_time_s*1000 + new_data.start_time_ns/1000000, &start_time);
    check_systime_discrep(&lastinteg->start_unidate, &lastinteg->start_unitime, &start_time);
    memcpy(&lastinteg->start_unitime, &start_time, sizeof(struct timestruct));
    lastinteg->done = 1;
    num_integ++;
    
    nextinteg = malloc(sizeof(struct pmtintegstruct));
    if (nextinteg == NULL)
    {
      act_log_error(act_log_msg("Could not allocate space for next integration data."));
      return -1;
    }
    memcpy(nextinteg, lastinteg, sizeof(struct pmtintegstruct));
    nextinteg->next = NULL;
    nextinteg->done = -1;
    nextinteg->integt_s = 0.0;
    nextinteg->repetitions = 0;
    nextinteg->counts = 0;
    nextinteg->error = 0;
    
    lastinteg->next = nextinteg;
    lastinteg = nextinteg;
    
    pmt_reg_checks(pmtdetail);
  }
  if (pmt_integrating(pmtdetail->pmt_stat))
  {
    ret = ioctl(pmtdetail->pmtdrv_fd, IOCTL_GET_CUR_INTEG, &new_data);
    if (ret < 0)
    {
      act_log_error(act_log_msg("Failed to get PMT current data - %s.", strerror(ret)));
      return num_integ;
    }
    lastinteg->counts = new_data.counts;
    lastinteg->error = new_data.error;
    convert_MS_HMSMS_time(new_data.start_time_s*1000 + new_data.start_time_ns/1000000, &start_time);
    check_systime_discrep(&lastinteg->start_unidate, &lastinteg->start_unitime, &start_time);
    memcpy(&lastinteg->start_unitime, &start_time, sizeof(struct timestruct));
    lastinteg->integt_s = (double)new_data.sample_period_ns/1000000000.0 + (double)lastinteg->sample_period_s*new_data.prebin_num;
    lastinteg->done = 0;
  }
  else
    lastinteg->done = -1;

  return num_integ;
}

struct pmtintegstruct *free_integ_data(struct pmtintegstruct *pmtinteg)
{
  if (pmtinteg == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  struct pmtintegstruct *lastinteg = pmtinteg, *nextinteg = pmtinteg->next;
  while (nextinteg != NULL)
  {
    free(lastinteg);
    lastinteg = nextinteg;
    nextinteg = lastinteg->next;
  }
  return lastinteg;
}

void pmt_integ_clear_data(struct pmtdetailstruct *pmtdetail)
{
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }

  char pmt_stat;
  int ret = read(pmtdetail->pmtdrv_fd, &pmt_stat, 1);
  if (ret <= 0)
  {
    act_log_error(act_log_msg("Error reading status from PMT driver - %s.", strerror(ret)));
    return;
  }
  struct pmt_integ_data new_data;
  while (pmt_data_ready(pmt_stat))
  {
    ret = ioctl(pmtdetail->pmtdrv_fd, IOCTL_GET_INTEG_DATA, &new_data);
    if (ret < 0)
    {
      act_log_error(act_log_msg("Error reading integration data from PMT driver (%d - %s).", ret, strerror(abs(ret))));
      return;
    }
    ret = read(pmtdetail->pmtdrv_fd, &pmt_stat, 1);
    if (ret <= 0)
    {
      act_log_error(act_log_msg("Error reading status from PMT driver - %s.", strerror(ret)));
      return;
    }
  }
  return;
}

void pmt_cancel_integ(struct pmtdetailstruct *pmtdetail)
{
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct pmt_command cmd = { .mode = 0 };
  ioctl(pmtdetail->pmtdrv_fd, IOCTL_INTEG_CMD, &cmd);
}

void pmt_set_datetime(struct pmtdetailstruct *pmtdetail, struct datestruct *unidate, struct timestruct *unitime)
{
  memcpy(&pmtdetail->cur_unidate, unidate, sizeof(struct datestruct));
  memcpy(&pmtdetail->cur_unitime, unitime, sizeof(struct timestruct));
}

void pmt_get_datetime(struct pmtdetailstruct *pmtdetail, struct datestruct *unidate, struct timestruct *unitime)
{
  memcpy(unidate, &pmtdetail->cur_unidate, sizeof(struct datestruct));
  memcpy(unitime, &pmtdetail->cur_unitime, sizeof(struct timestruct));
}

const char *pmt_get_id(struct pmtdetailstruct *pmtdetail)
{
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return NULL;
  }
  return pmtdetail->pmtcaps.pmt_id;
}

void update_stat_ind(struct pmtdetailstruct *pmtdetail)
{
  GdkColor new_col;
  char pmt_stat_str[100];
  int stat_str_len = 0;
  if (pmtdetail->pmt_stat & PMT_STAT_ERR)
  {
    if (pmtdetail->error & PMT_CRIT_ERR_MASK)
      gdk_color_parse("#AA0000", &new_col);
    else
      gdk_color_parse("#AAAA00", &new_col);
    if (pmtdetail->error & PMT_ERR_ZERO)
      stat_str_len = snprintf(pmt_stat_str, sizeof(pmt_stat_str)-1, "Zero Counts");
    if (pmtdetail->error & PMT_ERR_WARN)
    {
      if (stat_str_len != 0)
        stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, ", ");
      stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, "High Counts");
    }
    if (pmtdetail->error & PMT_ERR_OVERILLUM)
    {
      if (stat_str_len != 0)
        stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, ", ");
      stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, "Overillumination");
    }
    if (pmtdetail->error & PMT_ERR_OVERFLOW)
    {
      if (stat_str_len != 0)
        stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, ", ");
      stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, "Int Overflow");
    }
    if (pmtdetail->error & PMT_ERR_TIME_SYNC)
    {
      if (stat_str_len != 0)
        stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, ", ");
      stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, "Time Sync");
    }
    if (stat_str_len == 0)
      snprintf(pmt_stat_str, sizeof(pmt_stat_str)-1, "UNKNWN ERR");
  }
  else
  {
    gdk_color_parse("#00AA00", &new_col);
    if (pmtdetail->pmt_stat & PMT_STAT_PROBE)
      stat_str_len = snprintf(pmt_stat_str, sizeof(pmt_stat_str)-1, "Probing");
    if (pmtdetail->pmt_stat & PMT_STAT_BUSY)
    {
      if (stat_str_len != 0)
        stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, ", ");
      stat_str_len += snprintf(&pmt_stat_str[stat_str_len], sizeof(pmt_stat_str)-stat_str_len-1, "Integrating");
    }
    if (stat_str_len == 0)
      snprintf(pmt_stat_str, sizeof(pmt_stat_str)-1, "%s", pmtdetail->pmt_info.pmt_id);
  }
  gtk_widget_modify_bg(pmtdetail->evb_pmt_stat, GTK_STATE_NORMAL, &new_col);
  gtk_label_set_text(GTK_LABEL(pmtdetail->lbl_pmt_stat), pmt_stat_str);
}

unsigned char pmt_get_modes(struct pmtdetailstruct *pmtdetail)
{
  if (pmtdetail == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return 0;
  }
  return pmtdetail->pmt_info.modes;
}

void pmt_get_caps(struct pmtdetailstruct *pmtdetail, struct act_msg_pmtcap *msg_pmtcap)
{
  if ((pmtdetail == NULL) || (msg_pmtcap == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  memcpy(msg_pmtcap, &pmtdetail->pmtcaps, sizeof(struct act_msg_pmtcap));
}
