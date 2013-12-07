#include <string.h>
#include <act_log.h>
#include "net_datapmt.h"
#include "net_basic.h"

static void merge_pmtcaps(struct act_msg_pmtcap *msg_pmtcap);
static void send_pmtcap_resp_all(struct act_prog *prog_array, int num_progs);

static struct act_msg_pmtcap G_msg_pmtcap;

void init_pmtcap()
{
  memset(&G_msg_pmtcap, 0, sizeof(struct act_msg_pmtcap));
  G_msg_pmtcap.pmt_mode = ~((unsigned char)0);
  int i;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
  {
    G_msg_pmtcap.filters[i].slot = IPC_MAX_NUM_FILTAPERS;
    G_msg_pmtcap.apertures[i].slot = IPC_MAX_NUM_FILTAPERS;
  }
}

void request_pmtcap(struct act_prog *prog)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (prog->caps.datapmt_prov == 0)
    return;
  
  struct act_msg msg;
  msg.mtype = MT_PMT_CAP;
  memset(&(msg.content.msg_pmtcap), 0, sizeof(msg.content.msg_pmtcap));
  if (!act_send(prog, &msg))
    act_log_error(act_log_msg("Failed to send DATAPMT capabilities request to %s.", prog->name));
}

void process_pmtcap(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  merge_pmtcaps(&msg->content.msg_pmtcap);
  send_pmtcap_resp_all(prog_array, num_progs);
}

void process_datapmt(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  int i;
  char found_prog = FALSE;
  for (;;)
  {
    msg->content.msg_datapmt.datapmt_stage = msg->content.msg_datapmt.datapmt_stage << 1;
    if ((msg->content.msg_datapmt.datapmt_stage & DATAPMT_NONE) != 0)
      break;
    act_log_debug(act_log_msg("New DATAPMT stage: %hhu", msg->content.msg_datapmt.datapmt_stage));
    for (i=0; i<num_progs; i++)
    {
      if ((prog_array[i].caps.datapmt_prov & msg->content.msg_datapmt.datapmt_stage) == 0)
        continue;
      found_prog = TRUE;
      act_log_debug(act_log_msg("Sending message to %s", prog_array[i].name));
      if (!act_send(&(prog_array[i]), msg))
        act_log_error(act_log_msg("Failed to send DATAPMT request to %s.", prog_array[i].name));
    }
    if (found_prog)
      break;
  }
  if (!found_prog)
    act_log_debug(act_log_msg("Did not find programme."));
}

static void merge_pmtcaps(struct act_msg_pmtcap *msg_pmtcap)
{
  if (strlen(msg_pmtcap->pmt_id) != 0)
  {
    if (strlen(G_msg_pmtcap.pmt_id) == 0)
      snprintf(G_msg_pmtcap.pmt_id, IPC_MAX_INSTRID_LEN, "%s", msg_pmtcap->pmt_id);
    else
      act_log_error(act_log_msg("Two identifiers for PMT reported (current: %s ; new: %s). Keeping current.", G_msg_pmtcap.pmt_id, msg_pmtcap->pmt_id));
  }
  
  if (msg_pmtcap->pmt_mode != 0)
    G_msg_pmtcap.pmt_mode &= msg_pmtcap->pmt_mode;
  
  if (msg_pmtcap->min_sample_period_s > 0.0)
  {
    if (G_msg_pmtcap.min_sample_period_s > 1e-9)
      G_msg_pmtcap.min_sample_period_s = msg_pmtcap->min_sample_period_s;
    else if (msg_pmtcap->min_sample_period_s > G_msg_pmtcap.min_sample_period_s)
      G_msg_pmtcap.min_sample_period_s = msg_pmtcap->min_sample_period_s;
  }
  
  if (msg_pmtcap->max_sample_period_s > 0.0)
  {
    if (msg_pmtcap->max_sample_period_s < G_msg_pmtcap.max_sample_period_s)
      G_msg_pmtcap.max_sample_period_s = msg_pmtcap->max_sample_period_s;
  }
  
  int i, j, new_pos;
  for (i=0; (i<IPC_MAX_NUM_FILTAPERS) && (msg_pmtcap->filters[i].db_id>0); i++)
  {
    new_pos = -1;
    for (j=0; (j<IPC_MAX_NUM_FILTAPERS) && (new_pos<0); j++)
    {
      if (G_msg_pmtcap.filters[j].db_id == msg_pmtcap->filters[i].db_id)
      {
        act_log_error(act_log_msg("Duplicate PMT filter DB identifier found (current %d - %s ; new %d - %s). Ignoring new filter.", G_msg_pmtcap.filters[j].db_id, G_msg_pmtcap.filters[j].name, msg_pmtcap->filters[i].db_id, msg_pmtcap->filters[i].name));
        break;
      }
      if (G_msg_pmtcap.filters[j].slot == msg_pmtcap->filters[i].slot)
      {
        act_log_error(act_log_msg("Duplicate PMT filter slot found (current %d - %s ; new %d - %s). Ignoring new filter.", G_msg_pmtcap.filters[j].slot, G_msg_pmtcap.filters[j].name, msg_pmtcap->filters[i].slot, msg_pmtcap->filters[i].name));
        break;
      }
      if (G_msg_pmtcap.filters[j].db_id<=0)
      {
        new_pos = j;
        break;
      }
    }
    if (new_pos >= IPC_MAX_NUM_FILTAPERS)
    {
      act_log_error(act_log_msg("New PMT filter(s) received, but no space left in PMT capabilities structure."));
      break;
    }
    if (new_pos < 0)
      continue;
    memcpy(&G_msg_pmtcap.filters[new_pos], &msg_pmtcap->filters[i], sizeof(struct filtaper));
  }
  
  for (i=0; (i<IPC_MAX_NUM_FILTAPERS) && (msg_pmtcap->apertures[i].db_id>0); i++)
  {
    new_pos = -1;
    for (j=0; (j<IPC_MAX_NUM_FILTAPERS) && (new_pos<0); j++)
    {
      if (G_msg_pmtcap.apertures[j].db_id == msg_pmtcap->apertures[i].db_id)
      {
        act_log_error(act_log_msg("Duplicate PMT aperture DB identifier found (current %d - %s ; new %d - %s). Ignoring new aperture.", G_msg_pmtcap.apertures[j].db_id, G_msg_pmtcap.apertures[j].name, msg_pmtcap->apertures[i].db_id, msg_pmtcap->apertures[i].name));
        break;
      }
      if (G_msg_pmtcap.apertures[j].slot == msg_pmtcap->apertures[i].slot)
      {
        act_log_error(act_log_msg("Duplicate PMT aperture slot found (current %d - %s ; new %d - %s). Ignoring new aperture.", G_msg_pmtcap.apertures[j].slot, G_msg_pmtcap.apertures[j].name, msg_pmtcap->apertures[i].slot, msg_pmtcap->apertures[i].name));
        break;
      }
      if (G_msg_pmtcap.apertures[j].db_id<=0)
      {
        new_pos = j;
        break;
      }
    }
    if (new_pos >= IPC_MAX_NUM_FILTAPERS)
    {
      act_log_error(act_log_msg("New PMT aperture(s) received, but no space left in PMT capabilities structure."));
      break;
    }
    if (new_pos < 0)
      continue;
    memcpy(&G_msg_pmtcap.apertures[new_pos], &msg_pmtcap->apertures[i], sizeof(struct filtaper));
  }
}

static void send_pmtcap_resp_all(struct act_prog *prog_array, int num_progs)
{
  if (prog_array == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct act_msg msg;
  msg.mtype = MT_PMT_CAP;
  memcpy(&msg.content.msg_pmtcap, &G_msg_pmtcap, sizeof(struct act_msg_pmtcap));
  int i;
  for (i=0; i<num_progs; i++)
  {
    if ((prog_array[i].status != PROGSTAT_STARTUP) && (prog_array[i].status != PROGSTAT_RUNNING))
      continue;
    if (prog_array[i].caps.datapmt_prov == 0)
      continue;
    msg.content.msg_pmtcap.datapmt_stage = prog_array[i].caps.datapmt_prov;
    if (!act_send(&(prog_array[i]), &msg))
      act_log_error(act_log_msg("Failed to send PMTCAP response to %s.", prog_array[i].name));
  }
}
