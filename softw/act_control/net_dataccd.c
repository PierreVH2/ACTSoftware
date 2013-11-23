#include <string.h>
#include <act_log.h>
#include "net_dataccd.h"
#include "net_basic.h"

static void merge_ccdcaps(struct act_msg_ccdcap *msg_ccdcap);
static void send_ccdcap_resp_all(struct act_prog *prog_array, int num_progs);

struct act_msg_ccdcap G_msg_ccdcap;

void init_ccdcap()
{
  memset(&G_msg_ccdcap, 0, sizeof(struct act_msg_ccdcap));
  G_msg_ccdcap.prebin_x = G_msg_ccdcap.prebin_y = 0;
  int i;
  for (i=0; i<IPC_MAX_NUM_FILTAPERS; i++)
    G_msg_ccdcap.filters[i].slot = IPC_MAX_NUM_FILTAPERS;
}

void request_ccdcap(struct act_prog *prog)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (prog->caps.dataccd_prov == 0)
    return;
  
  struct act_msg msg;
  msg.mtype = MT_CCD_CAP;
  memset(&(msg.content.msg_ccdcap), 0, sizeof(msg.content.msg_ccdcap));
  if (!act_send(prog, &msg))
    act_log_error(act_log_msg("Failed to send DATACCD capabilities request to %s.", prog->name));
}

void process_ccdcap(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  merge_ccdcaps(&msg->content.msg_ccdcap);
  send_ccdcap_resp_all(prog_array, num_progs);
}

void process_dataccd(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  int i;
  char found_prog = FALSE;
  while ((msg->content.msg_dataccd.dataccd_stage & DATACCD_NONE) == 0)
  {
    msg->content.msg_dataccd.dataccd_stage = msg->content.msg_dataccd.dataccd_stage << 1;
    for (i=0; i<num_progs; i++)
    {
      if ((prog_array[i].caps.dataccd_prov & msg->content.msg_dataccd.dataccd_stage) == 0)
        continue;
      found_prog = TRUE;
      if (!act_send(&(prog_array[i]), msg))
        act_log_error(act_log_msg("Failed to send DATACCD request to %s.", prog_array[i].name));
    }
    if (found_prog)
      break;
  }
}

static void merge_ccdcaps(struct act_msg_ccdcap *msg_ccdcap)
{
  if (strlen(msg_ccdcap->ccd_id) != 0)
  {
    if (strlen(G_msg_ccdcap.ccd_id) == 0)
      snprintf(G_msg_ccdcap.ccd_id, IPC_MAX_INSTRID_LEN, "%s", msg_ccdcap->ccd_id);
    else
      act_log_error(act_log_msg("Two identifiers for CCD reported (current: %s ; new: %s). Keeping current.", G_msg_ccdcap.ccd_id, msg_ccdcap->ccd_id));
  }
  
  if (msg_ccdcap->min_exp_t_msec > 0)
  {
    if (G_msg_ccdcap.min_exp_t_msec == 0)
      G_msg_ccdcap.min_exp_t_msec = msg_ccdcap->min_exp_t_msec;
    else if (msg_ccdcap->min_exp_t_msec > G_msg_ccdcap.min_exp_t_msec)
      G_msg_ccdcap.min_exp_t_msec = msg_ccdcap->min_exp_t_msec;
  }
  
  if (msg_ccdcap->max_exp_t_msec > 0)
  {
    if (msg_ccdcap->max_exp_t_msec < G_msg_ccdcap.max_exp_t_msec)
      G_msg_ccdcap.max_exp_t_msec = msg_ccdcap->max_exp_t_msec;
  }
  
  int i, j, new_pos;
  for (i=0; (i<IPC_MAX_NUM_FILTAPERS) && (msg_ccdcap->filters[i].db_id>0); i++)
  {
    new_pos = -1;
    for (j=0; (j<IPC_MAX_NUM_FILTAPERS) && (new_pos<0); j++)
    {
      if (G_msg_ccdcap.filters[j].db_id == msg_ccdcap->filters[i].db_id)
      {
        act_log_error(act_log_msg("Duplicate CCD filter DB identifier found (current %d - %s ; new %d - %s). Ignoring new filter.", G_msg_ccdcap.filters[j].db_id, G_msg_ccdcap.filters[j].name, msg_ccdcap->filters[i].db_id, msg_ccdcap->filters[i].name));
        break;
      }
      if (G_msg_ccdcap.filters[j].slot == msg_ccdcap->filters[i].slot)
      {
        act_log_error(act_log_msg("Duplicate CCD filter slot found (current %d - %s ; new %d - %s). Ignoring new filter.", G_msg_ccdcap.filters[j].slot, G_msg_ccdcap.filters[j].name, msg_ccdcap->filters[i].slot, msg_ccdcap->filters[i].name));
        break;
      }
      if (G_msg_ccdcap.filters[j].db_id<=0)
      {
        new_pos = j;
        break;
      }
    }
    if (new_pos >= IPC_MAX_NUM_FILTAPERS)
    {
      act_log_error(act_log_msg("New CCD filter(s) received, but no space left in CCD capabilities structure."));
      break;
    }
    if (new_pos < 0)
      continue;
    memcpy(&G_msg_ccdcap.filters[new_pos], &msg_ccdcap->filters[i], sizeof(struct filtaper));
  }
  
  if (msg_ccdcap->prebin_x != 0)
  {
    if (G_msg_ccdcap.prebin_x == 0)
      G_msg_ccdcap.prebin_x = msg_ccdcap->prebin_x;
    else
      G_msg_ccdcap.prebin_x &= msg_ccdcap->prebin_x;
  }
  if (msg_ccdcap->prebin_y != 0)
  {
    if (G_msg_ccdcap.prebin_y == 0)
      G_msg_ccdcap.prebin_y = msg_ccdcap->prebin_y;
    else
      G_msg_ccdcap.prebin_y &= msg_ccdcap->prebin_y;
  }
  
  for (i=0; i<IPC_CCD_MAX_NUM_WINDOW_MODES; i++)
  {
    if ((msg_ccdcap->windows[i].width_px == 0) || (msg_ccdcap->windows[i].height_px == 0))
      break;
    new_pos = -1;
    for (j=0; j<IPC_CCD_MAX_NUM_WINDOW_MODES; j++)
    {
      if ((G_msg_ccdcap.windows[j].width_px == msg_ccdcap->windows[i].width_px) && (G_msg_ccdcap.windows[j].height_px == msg_ccdcap->windows[i].height_px))
      {
        act_log_debug(act_log_msg("Duplicate CCD window mode received."));
        new_pos = IPC_CCD_MAX_NUM_WINDOW_MODES;
        break;
      }
      if ((G_msg_ccdcap.windows[j].width_px == 0) || (G_msg_ccdcap.windows[j].height_px == 0))
      {
        new_pos = j;
        break;
      }
    }
    if (new_pos >= IPC_CCD_MAX_NUM_WINDOW_MODES)
      continue;
    if (new_pos < 0)
    {
      act_log_error(act_log_msg("New CCD window mode(s) received, but no space left in CCD capabilities structure. Ignoring new mode."));
      break;
    }
    memcpy(&G_msg_ccdcap.windows[new_pos], &msg_ccdcap->windows[i], sizeof(struct ipc_ccd_window_mode));
  }
}

static void send_ccdcap_resp_all(struct act_prog *prog_array, int num_progs)
{
  if (prog_array == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct act_msg msg;
  msg.mtype = MT_CCD_CAP;
  memcpy(&msg.content.msg_ccdcap, &G_msg_ccdcap, sizeof(struct act_msg_ccdcap));
  int i;
  for (i=0; i<num_progs; i++)
  {
    if ((prog_array[i].status != PROGSTAT_STARTUP) && (prog_array[i].status != PROGSTAT_RUNNING))
      continue;
    if (prog_array[i].caps.dataccd_prov == 0)
      continue;
    msg.content.msg_ccdcap.dataccd_stage = prog_array[i].caps.dataccd_prov;
    if (!act_send(&(prog_array[i]), &msg))
      act_log_error(act_log_msg("Failed to send CCDCAP response to %s.", prog_array[i].name));
  }
}
