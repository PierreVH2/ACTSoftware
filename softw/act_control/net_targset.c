#include <string.h>
#include <act_log.h>
#include "net_targset.h"
#include "net_basic.h"

static void merge_targcaps(struct act_msg_targcap *msg_targcap);
static void send_targcap_resp_all(struct act_prog *prog_array, int num_progs);

static struct act_msg_targcap G_msg_targcap;

void init_targcap()
{
  memset(&G_msg_targcap, 0, sizeof(struct act_msg_targcap));
  G_msg_targcap.autoguide = 1;
}

void request_targcap(struct act_prog *prog)
{
  if (prog == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  if (prog->caps.targset_prov == 0)
    return;
  struct act_msg msg;
  msg.mtype = MT_TARG_CAP;
  memset(&(msg.content.msg_targcap), 0, sizeof(msg.content.msg_targcap));
  if (!act_send(prog, &msg))
    act_log_error(act_log_msg("Failed to send TARGSET capabilities request to %s.", prog->name));
}

void process_targcap(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  merge_targcaps(&msg->content.msg_targcap);
  send_targcap_resp_all(prog_array, num_progs);
}

void process_targset(struct act_prog *prog_array, int num_progs, struct act_msg *msg)
{
  if ((prog_array == NULL) || (msg == NULL))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  int i;
  char found_prog = FALSE;
  while ((msg->content.msg_targset.targset_stage & TARGSET_NONE) == 0)
  {
    msg->content.msg_targset.targset_stage = msg->content.msg_targset.targset_stage << 1;
    for (i=0; i<num_progs; i++)
    {
      if ((prog_array[i].caps.targset_prov & msg->content.msg_targset.targset_stage) == 0)
        continue;
      found_prog = TRUE;
      if (!act_send(&(prog_array[i]), msg))
        act_log_error(act_log_msg("Failed to send TARGSET request to %s.", prog_array[i].name));
    }
    if (found_prog)
      break;
  }
}

static void merge_targcaps(struct act_msg_targcap *msg_targcap)
{
  if (!msg_targcap->autoguide)
    G_msg_targcap.autoguide = 0;

  long int tmp_lim_new = convert_HMSMS_MS_ha(&msg_targcap->ha_lim_W);
  long int tmp_lim_cur = convert_HMSMS_MS_ha(&G_msg_targcap.ha_lim_W);
  if ((tmp_lim_new != 0) && (tmp_lim_cur == 0))
    memcpy(&G_msg_targcap.ha_lim_W, &msg_targcap->ha_lim_W, sizeof(struct hastruct));
  else if ((tmp_lim_new != 0) && (tmp_lim_cur > tmp_lim_new))
    memcpy(&G_msg_targcap.ha_lim_W, &msg_targcap->ha_lim_W, sizeof(struct hastruct));

  tmp_lim_new = convert_HMSMS_MS_ha(&msg_targcap->ha_lim_E);
  tmp_lim_cur = convert_HMSMS_MS_ha(&G_msg_targcap.ha_lim_E);
  if ((tmp_lim_new != 0) && (tmp_lim_cur == 0))
    memcpy(&G_msg_targcap.ha_lim_E, &msg_targcap->ha_lim_E, sizeof(struct hastruct));
  else if ((tmp_lim_new != 0) && (tmp_lim_cur < tmp_lim_new))
    memcpy(&G_msg_targcap.ha_lim_E, &msg_targcap->ha_lim_E, sizeof(struct hastruct));

  tmp_lim_new = convert_DMS_ASEC_dec(&msg_targcap->dec_lim_N);
  tmp_lim_cur = convert_DMS_ASEC_dec(&G_msg_targcap.dec_lim_N);
  if ((tmp_lim_new != 0) && (tmp_lim_cur == 0))
    memcpy(&G_msg_targcap.dec_lim_N, &msg_targcap->dec_lim_N, sizeof(struct decstruct));
  else if ((tmp_lim_new != 0) && (tmp_lim_cur > tmp_lim_new))
    memcpy(&G_msg_targcap.dec_lim_N, &msg_targcap->dec_lim_N, sizeof(struct decstruct));

  tmp_lim_new = convert_DMS_ASEC_dec(&msg_targcap->dec_lim_S);
  tmp_lim_cur = convert_DMS_ASEC_dec(&G_msg_targcap.dec_lim_S);
  if ((tmp_lim_new != 0) && (tmp_lim_cur == 0))
    memcpy(&G_msg_targcap.dec_lim_S, &msg_targcap->dec_lim_S, sizeof(struct decstruct));
  else if ((tmp_lim_new != 0) && (tmp_lim_cur < tmp_lim_new))
    memcpy(&G_msg_targcap.dec_lim_S, &msg_targcap->dec_lim_S, sizeof(struct decstruct));

  tmp_lim_new = convert_DMS_ASEC_alt(&msg_targcap->alt_lim);
  tmp_lim_cur = convert_DMS_ASEC_alt(&G_msg_targcap.alt_lim);
  if ((tmp_lim_new != 0) && (tmp_lim_cur == 0))
    memcpy(&G_msg_targcap.alt_lim, &msg_targcap->alt_lim, sizeof(struct altstruct));
  else if ((tmp_lim_new != 0) && (tmp_lim_cur < tmp_lim_new))
    memcpy(&G_msg_targcap.alt_lim, &msg_targcap->alt_lim, sizeof(struct altstruct));
}

static void send_targcap_resp_all(struct act_prog *prog_array, int num_progs)
{
  if (prog_array == NULL)
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return;
  }
  struct act_msg msg;
  msg.mtype = MT_TARG_CAP;
  memcpy(&msg.content.msg_targcap, &G_msg_targcap, sizeof(struct act_msg_targcap));
  int i;
  for (i=0; i<num_progs; i++)
  {
    if ((prog_array[i].status != PROGSTAT_STARTUP) && (prog_array[i].status != PROGSTAT_RUNNING))
      continue;
    if (prog_array[i].caps.targset_prov == 0)
      continue;
    msg.content.msg_targcap.targset_stage = prog_array[i].caps.targset_prov;
    if (!act_send(&(prog_array[i]), &msg))
      act_log_error(act_log_msg("Failed to send TARGCAP response to %s.", prog_array[i].name));
  }
}
