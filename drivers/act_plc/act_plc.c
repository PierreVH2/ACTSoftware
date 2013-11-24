// #define PLC_SIM

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/random.h>
#include <linux/device.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <plc_ldisc/plc_ldisc.h>
#include "act_plc.h"
#include "plc_definitions.h"

#define PRINTK_PREFIX "[ACT_PLC] "

#define AWAIT_STAT_RESP     1
#define AWAIT_CMD_RESP      2
#define AWAIT_INIT_STATRESP 3

#define DOME_MIN_FLOP   5
#define DOME_MAX_FLOP  10
#define DOME_AZM_OFFS 105

#define SAFE_FILT_SLOT  9
#define SAFE_APER_SLOT  8

/// Device major number
static int G_major = 0;
/// Device class structure
static struct class *G_class_plc;
/// Device structure
static struct device *G_dev_plc;
static unsigned char G_status = 0;
static unsigned char G_num_open = 0;
static char G_cur_plc_stat_str[PLC_STAT_RESP_LEN+1];
static char G_cur_plc_cmd_str[PLC_CMD_LEN+1];
static struct plc_status G_cur_plc_stat;
static struct fasync_struct *G_async_queue;
static wait_queue_head_t G_readq;
static unsigned long G_ha_pulses=0, G_dec_pulses=0;
static void (*G_handset_handler) (const unsigned char old_hs, const unsigned char new_hs) = NULL;
#ifndef PLC_SIM
static struct workqueue_struct *G_plcdrv_workq;
static struct delayed_work G_plcstat_work;
static struct delayed_work G_plccmd_work;
static struct delayed_work G_plcresp_work;
static unsigned char G_await_resp = 0;
static unsigned char G_cmd_pending = 0;
#endif

static char hexchar2int(char c)
{
  if ((c>='0') && (c<='9'))
    return c - '0';
  if ((c>='A') && (c<='F'))
    return c - 'A' + 10;
  if ((c>='a') && (c<='f'))
    return c - 'a' + 10;
  return -1;
}

static char int2hexchar(char n)
{
  if (n < 0)
    return -1;
  if (n < 0xA)
    return n + '0';
  if (n <= 0xF)
    return n - 10 + 'A';
  return -1;
}

static int calc_fcs(const char *str, int length)
{
  int A=0, l;
  for (l=0; l<length ; l++)           // perform an exclusive or on each command string character in succession
    A = A ^ str[l];
  return A;
}

static int parse_plc_status(char *old_status, const char *new_status)
{
  int ret = 0;
  unsigned char tmp_cmd_pending = 0;
  if ((old_status[STAT_DOME_POS_OFFS] != new_status[STAT_DOME_POS_OFFS]) ||
    (old_status[STAT_DOME_POS_OFFS+1] != new_status[STAT_DOME_POS_OFFS+1]) ||
    (old_status[STAT_DOME_POS_OFFS+2] != new_status[STAT_DOME_POS_OFFS+2]) ||
    (old_status[STAT_DOME_POS_OFFS+3] != new_status[STAT_DOME_POS_OFFS+3]))
  {
    G_cur_plc_stat.dome_pos = (new_status[STAT_DOME_POS_OFFS]-'0')*1000
    + (new_status[STAT_DOME_POS_OFFS+1]-'0')*100
    + (new_status[STAT_DOME_POS_OFFS+2]-'0')*10
    + (new_status[STAT_DOME_POS_OFFS+3]-'0');
    G_cur_plc_stat.dome_pos = 3599 - G_cur_plc_stat.dome_pos;
    old_status[STAT_DOME_POS_OFFS] = new_status[STAT_DOME_POS_OFFS];
    old_status[STAT_DOME_POS_OFFS+1] = new_status[STAT_DOME_POS_OFFS+1];
    old_status[STAT_DOME_POS_OFFS+2] = new_status[STAT_DOME_POS_OFFS+2];
    old_status[STAT_DOME_POS_OFFS+3] = new_status[STAT_DOME_POS_OFFS+3];
    ret = 1;
  }
  if (old_status[STAT_DROPOUT_OFFS] != new_status[STAT_DROPOUT_OFFS])
  {
    G_cur_plc_stat.dropout = hexchar2int(new_status[STAT_DROPOUT_OFFS]);
    if ((G_cur_plc_stat.dropout & STAT_DSHUTT_OPEN_MASK) && (hexchar2int(G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS]) & CNTR_DSHUTT_OPEN_MASK))
    {
      G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = '0';
      tmp_cmd_pending = 1;
    }
    else if ((G_cur_plc_stat.dropout & STAT_DSHUTT_CLOSED_MASK) && (hexchar2int(G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS]) & CNTR_DSHUTT_CLOSE_MASK))
    {
      G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = '0';
      tmp_cmd_pending = 1;
    }
    old_status[STAT_DROPOUT_OFFS] = new_status[STAT_DROPOUT_OFFS];
    ret = 1;
  }
  if (old_status[STAT_SHUTTER_OFFS] != new_status[STAT_SHUTTER_OFFS])
  {
    G_cur_plc_stat.shutter = hexchar2int(new_status[STAT_SHUTTER_OFFS]);
    if ((G_cur_plc_stat.shutter & STAT_DSHUTT_OPEN_MASK) && (hexchar2int(G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS]) & CNTR_DSHUTT_OPEN_MASK))
    {
      G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = '0';
      tmp_cmd_pending = 1;
    }
    else if ((G_cur_plc_stat.shutter & STAT_DSHUTT_CLOSED_MASK) && (hexchar2int(G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS]) & CNTR_DSHUTT_CLOSE_MASK))
    {
      G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = '0';
      tmp_cmd_pending = 1;
    }
    old_status[STAT_SHUTTER_OFFS] = new_status[STAT_SHUTTER_OFFS];
    ret = 1;
  }
  if (old_status[STAT_DOME_STAT_OFFS] != new_status[STAT_DOME_STAT_OFFS])
  {
    G_cur_plc_stat.trapdoor_open = (hexchar2int(new_status[STAT_DOME_STAT_OFFS]) & STAT_TRAPDOOR_OPEN_MASK) > 0;
    if (G_cur_plc_stat.trapdoor_open)
      printk(PRINTK_PREFIX KERN_INFO "Trapdoor opened.");
    G_cur_plc_stat.dome_moving = (hexchar2int(new_status[STAT_DOME_STAT_OFFS]) & STAT_DOME_MOVING_MASK) > 0;
    old_status[STAT_DOME_STAT_OFFS] = new_status[STAT_DOME_STAT_OFFS];
    ret = 1;
  }
  if (old_status[STAT_APER_STAT_OFFS] != new_status[STAT_APER_STAT_OFFS])
  {
    G_cur_plc_stat.aper_stat = hexchar2int(new_status[STAT_APER_STAT_OFFS]);
    old_status[STAT_APER_STAT_OFFS] = new_status[STAT_APER_STAT_OFFS];
    ret = 1;
  }
  if (old_status[STAT_FILT_STAT_OFFS] != new_status[STAT_FILT_STAT_OFFS])
  {
    G_cur_plc_stat.filt_stat = hexchar2int(new_status[STAT_FILT_STAT_OFFS]);
    old_status[STAT_FILT_STAT_OFFS] = new_status[STAT_FILT_STAT_OFFS];
    ret = 1;
  }
  if (old_status[STAT_ACQMIR_OFFS] != new_status[STAT_ACQMIR_OFFS])
  {
    G_cur_plc_stat.acqmir = hexchar2int(new_status[STAT_ACQMIR_OFFS]);
    old_status[STAT_ACQMIR_OFFS] = new_status[STAT_ACQMIR_OFFS];
    ret = 1;
  }
  if (old_status[STAT_INSTR_SHUTT_OFFS] != new_status[STAT_INSTR_SHUTT_OFFS])
  {
    G_cur_plc_stat.instrshutt_open = (hexchar2int(new_status[STAT_INSTR_SHUTT_OFFS]) & STAT_INSTR_SHUTT_OPEN_MASK) > 0;
    if (G_cur_plc_stat.instrshutt_open)
      printk(PRINTK_PREFIX KERN_INFO "PMT shutter opened.\n");
    old_status[STAT_INSTR_SHUTT_OFFS] = new_status[STAT_INSTR_SHUTT_OFFS];
    ret = 1;
  }
  if ((old_status[STAT_FOCUS_REG_OFFS] != new_status[STAT_FOCUS_REG_OFFS]) ||
    (old_status[STAT_FOCUS_POS_OFFS] != new_status[STAT_FOCUS_POS_OFFS]) ||
    (old_status[STAT_FOCUS_POS_OFFS+1] != new_status[STAT_FOCUS_POS_OFFS+1]) ||
    (old_status[STAT_FOCUS_POS_OFFS+2] != new_status[STAT_FOCUS_POS_OFFS+2]))
  {
    if (new_status[STAT_FOCUS_REG_OFFS] == STAT_FOCUS_REG_OUT_VAL)
      G_cur_plc_stat.focus_pos = - (new_status[STAT_FOCUS_POS_OFFS] - '0')*100
      - (new_status[STAT_FOCUS_POS_OFFS+1] - '0')*10
      - (new_status[STAT_FOCUS_POS_OFFS+2] - '0');
    else
      G_cur_plc_stat.focus_pos = + (new_status[STAT_FOCUS_POS_OFFS] - '0')*100
      + (new_status[STAT_FOCUS_POS_OFFS+1] - '0')*10
      + (new_status[STAT_FOCUS_POS_OFFS+2] - '0');
    old_status[STAT_FOCUS_REG_OFFS] = new_status[STAT_FOCUS_REG_OFFS];
    old_status[STAT_FOCUS_POS_OFFS] = new_status[STAT_FOCUS_POS_OFFS];
    old_status[STAT_FOCUS_POS_OFFS+1] = new_status[STAT_FOCUS_POS_OFFS+1];
    old_status[STAT_FOCUS_POS_OFFS+2] = new_status[STAT_FOCUS_POS_OFFS+2];
    ret = 1;
  }
  if ((old_status[STAT_FOC_STAT_OFFS1] != new_status[STAT_FOC_STAT_OFFS1]) ||
    (old_status[STAT_FOC_STAT_OFFS2] != new_status[STAT_FOC_STAT_OFFS2]))
  {
    G_cur_plc_stat.foc_stat = (hexchar2int(new_status[STAT_FOC_STAT_OFFS1]) << 4) | (hexchar2int(new_status[STAT_FOC_STAT_OFFS2]));
    old_status[STAT_FOC_STAT_OFFS1] = new_status[STAT_FOC_STAT_OFFS1];
    old_status[STAT_FOC_STAT_OFFS2] = new_status[STAT_FOC_STAT_OFFS2];
    ret = 1;
  }
  if (old_status[STAT_CRIT_ERR_OFFS] != new_status[STAT_CRIT_ERR_OFFS])
  {
    G_cur_plc_stat.power_fail = (hexchar2int(new_status[STAT_CRIT_ERR_OFFS]) & STAT_POWER_FAIL_MASK) > 0;
    G_cur_plc_stat.watchdog_trip = (hexchar2int(new_status[STAT_CRIT_ERR_OFFS]) & STAT_WATCHDOG_MASK) > 0;
    old_status[STAT_CRIT_ERR_OFFS] = new_status[STAT_CRIT_ERR_OFFS];
    ret = 1;
  }
  if (old_status[STAT_EHT_OFFS] != new_status[STAT_EHT_OFFS])
  {
    G_cur_plc_stat.eht_mode = hexchar2int(new_status[STAT_EHT_OFFS]);
    old_status[STAT_EHT_OFFS] = new_status[STAT_EHT_OFFS];
    ret = 1;
  }
  if ((old_status[STAT_HANDSET_OFFS1] != new_status[STAT_HANDSET_OFFS1]) ||
    (old_status[STAT_HANDSET_OFFS2] != new_status[STAT_HANDSET_OFFS2]))
  {
    unsigned char new_hs = hexchar2int(new_status[STAT_HANDSET_OFFS1]) | (hexchar2int(new_status[STAT_HANDSET_OFFS2]) << 4);
    if (G_handset_handler != NULL)
      G_handset_handler(G_cur_plc_stat.handset, new_hs);
    G_cur_plc_stat.handset = new_hs;
    old_status[STAT_HANDSET_OFFS1] = new_status[STAT_HANDSET_OFFS1];
    old_status[STAT_HANDSET_OFFS2] = new_status[STAT_HANDSET_OFFS2];
    ret = 1;
  }
  if (old_status[STAT_APER_NUM_OFFS] != new_status[STAT_APER_NUM_OFFS])
  {
    G_cur_plc_stat.aper_pos = new_status[STAT_APER_NUM_OFFS] - '0';
    old_status[STAT_APER_NUM_OFFS] = new_status[STAT_APER_NUM_OFFS];
    ret = 1;
  }
  if (old_status[STAT_FILT_NUM_OFFS] != new_status[STAT_FILT_NUM_OFFS])
  {
    G_cur_plc_stat.filt_pos = new_status[STAT_FILT_NUM_OFFS] - '0';
    old_status[STAT_FILT_NUM_OFFS] = new_status[STAT_FILT_NUM_OFFS];
    ret = 1;
  }
  
  G_ha_pulses = new_status[STAT_TEL_RA_HI_OFFS]*10000000
  + new_status[STAT_TEL_RA_HI_OFFS+1]*1000000
  + new_status[STAT_TEL_RA_HI_OFFS+2]*100000
  + new_status[STAT_TEL_RA_HI_OFFS+3]*10000
  + new_status[STAT_TEL_RA_LO_OFFS]*1000
  + new_status[STAT_TEL_RA_LO_OFFS+1]*100
  + new_status[STAT_TEL_RA_LO_OFFS+2]*10
  + new_status[STAT_TEL_RA_LO_OFFS+3];
  G_dec_pulses = new_status[STAT_TEL_DEC_HI_OFFS]*10000000
  + new_status[STAT_TEL_DEC_HI_OFFS+1]*1000000
  + new_status[STAT_TEL_DEC_HI_OFFS+2]*100000
  + new_status[STAT_TEL_DEC_HI_OFFS+3]*10000
  + new_status[STAT_TEL_DEC_LO_OFFS]*1000
  + new_status[STAT_TEL_DEC_LO_OFFS+1]*100
  + new_status[STAT_TEL_DEC_LO_OFFS+2]*10
  + new_status[STAT_TEL_DEC_LO_OFFS+3];
  
  #ifndef PLC_SIM
  if (tmp_cmd_pending)
    G_cmd_pending = 1;
  #endif
  return ret;
}

static void init_cmd_str(void)
{
  char tmpval;
  memset(G_cur_plc_cmd_str, '0', PLC_CMD_LEN);
  strncpy(G_cur_plc_cmd_str, PLC_CMD_HEAD, PLC_CMD_HEAD_LEN);
  snprintf(&G_cur_plc_cmd_str[CNTR_DOME_POS_OFFS], CNTR_DOME_POS_LEN+1, CNTR_DOME_POS_FMT, 0);
  snprintf(&G_cur_plc_cmd_str[CNTR_DOME_MAX_FLOP_OFFS], CNTR_DOME_FLOP_LEN+1, CNTR_DOME_FLOP_FMT, DOME_MAX_FLOP);
  snprintf(&G_cur_plc_cmd_str[CNTR_DOME_MIN_FLOP_OFFS], CNTR_DOME_FLOP_LEN+1, CNTR_DOME_FLOP_FMT, DOME_MIN_FLOP);
  if (hexchar2int(G_cur_plc_stat_str[STAT_INSTR_SHUTT_OFFS]) & STAT_INSTR_SHUTT_OPEN_MASK)
    G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(CNTR_INSTR_SHUTT_MASK | CNTR_WATCHDOG_MASK);
  else
    G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(CNTR_WATCHDOG_MASK);
  G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = '0';
  G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = '0';
  G_cur_plc_cmd_str[CNTR_DOME_STAT_OFFS] = int2hexchar(CNTR_DOME_SET_OFFS_MASK);
  G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = '0';
  G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = '0';
  G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = '0';
  if (hexchar2int(G_cur_plc_stat_str[STAT_EHT_OFFS]) & STAT_EHT_HI_MASK)
    tmpval = CNTR_EHT_HI_MASK;
  else if (hexchar2int(G_cur_plc_stat_str[STAT_EHT_OFFS]) & STAT_EHT_LO_MASK)
    tmpval = CNTR_EHT_LO_MASK;
  else
    tmpval = 0;
  tmpval |= (G_cur_plc_stat_str[STAT_ACQMIR_OFFS] & STAT_ACQMIR_VIEW_MASK) == 0 ? CNTR_ACQMIR_INBEAM_MASK : 0;
  G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(tmpval);
  G_cur_plc_cmd_str[CNTR_FILT_NUM_OFFS] = G_cur_plc_stat_str[STAT_FILT_NUM_OFFS];
  G_cur_plc_cmd_str[CNTR_FILT_NUM_OFFS+1] = '0';
  G_cur_plc_cmd_str[CNTR_APER_NUM_OFFS] = G_cur_plc_stat_str[STAT_APER_NUM_OFFS];
  G_cur_plc_cmd_str[CNTR_APER_NUM_OFFS+1] = '0';
  strncpy(&G_cur_plc_cmd_str[CNTR_FOCUS_REG_OFFS], &G_cur_plc_stat_str[STAT_FOCUS_REG_OFFS], CNTR_FOCUS_POS_LEN+1);
  snprintf(&G_cur_plc_cmd_str[CNTR_DOME_OFFS_OFFS], CNTR_DOME_POS_LEN+1, CNTR_DOME_POS_FMT, DOME_AZM_OFFS);
  G_cur_plc_cmd_str[CNTR_PADDING_OFFS] = '0';
  tmpval = calc_fcs(G_cur_plc_cmd_str, CNTR_FCS_TERM_OFFS);
  snprintf(&G_cur_plc_cmd_str[CNTR_FCS_TERM_OFFS], CNTR_FCS_TERM_LEN+1, CNTR_FCS_TERM_FMT, tmpval);
}

#ifndef PLC_SIM
static int check_fcs(const char *str, int length)
{
  int fcs;
  fcs = hexchar2int(str[length-4])*16 + hexchar2int(str[length-3]);
  return (calc_fcs(str, length-4) == fcs);
}

static int check_endcode(const char *str, int length)
{
  char code, tmp;
  if (length < 7)
    return 1;
  code = 0;
  tmp = hexchar2int(str[5]);
  if (tmp < 0)
    return 1;
  code += tmp*16;
  tmp = hexchar2int(str[6]);
  if (tmp < 0)
    return 1;
  code += tmp;
  return (tmp == 0);
}

static void plc_resp_timeout(struct work_struct *work)
{
  printk(KERN_ERR PRINTK_PREFIX "Timed out while awaiting %s response from PLC.\n", G_await_resp == AWAIT_INIT_STATRESP ? "initial status" : G_await_resp == AWAIT_STAT_RESP ? "status" : G_await_resp == AWAIT_CMD_RESP? "command" : G_await_resp > 0 ? "unknown " : "");
  G_await_resp = 0;
  G_status &= ~PLC_COMM_OK;
  kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  wake_up_interruptible(&G_readq);
}

static void send_plc_statreq(struct work_struct *work)
{
  if (G_await_resp > 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Attempted to send status request, but driver is currently awaiting a response from the PLC.\n");
    queue_delayed_work(G_plcdrv_workq, &G_plcstat_work, HZ * (jiffies%900+100) / 1000);
    return;
  }
  write_to_plc(PLC_STAT_REQ, PLC_STAT_REQ_LEN);
  G_await_resp = AWAIT_STAT_RESP;
  queue_delayed_work(G_plcdrv_workq, &G_plcresp_work, 5*HZ);
}

static void send_plc_cmd(struct work_struct *work)
{
  if (G_await_resp > 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Attempted to send command, but driver is currently awaiting a response from the PLC.\n");
    G_cmd_pending = 1;
    return;
  }
  printk(KERN_INFO PRINTK_PREFIX "Sending PLC command.\n");
  sprintf(&G_cur_plc_cmd_str[CNTR_FCS_TERM_OFFS], CNTR_FCS_TERM_FMT, calc_fcs(G_cur_plc_cmd_str, CNTR_FCS_TERM_OFFS));
  write_to_plc(G_cur_plc_cmd_str, PLC_CMD_LEN);
  G_await_resp = AWAIT_CMD_RESP;
  queue_delayed_work(G_plcdrv_workq, &G_plcresp_work, 5*HZ);
}

static int process_init_statresp(const char *buf, int len)
{
  if (len+1 < PLC_STAT_RESP_LEN)
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to initial status request (invalid length - %d should be %d). Not initialising.\n", len, PLC_STAT_RESP_LEN);
    return -EIO;
  }
  if (strncmp(buf, PLC_STAT_REQ, 5) != 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to initial status request (%s). Not initialising.\n", buf);
    return -EIO;
  }
  if ((!check_fcs(buf,len+1)) || (!check_endcode(buf,len)))
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to status request (invalid FCS/endcode).\n");
    return -EIO;
  }
  G_await_resp = 0;
  cancel_delayed_work(&G_plcresp_work);
  parse_plc_status(G_cur_plc_stat_str, buf);
  init_cmd_str();
  send_plc_cmd(NULL);
  G_cur_plc_cmd_str[CNTR_DOME_STAT_OFFS] = '0';
  queue_delayed_work(G_plcdrv_workq, &G_plccmd_work, 60*HZ);
  queue_delayed_work(G_plcdrv_workq, &G_plcstat_work, HZ/5);
  return 1;
}

static int process_cmdresp(const char *buf, int len)
{
  if (len+1 < PLC_CMD_RESP_LEN)
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to command (invalid length - %d should be %d).\n", len, PLC_CMD_RESP_LEN);
    return -EIO;
  }
  if (strncmp(buf, PLC_CMD_RESP, PLC_CMD_RESP_LEN-1) != 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to command (%s).\n", buf);
    return -EIO;
  }
  if (!G_cmd_pending)
  {
    // Deactivate commands that only need to be issued once
    G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(hexchar2int(G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS]) & (~CNTR_ACQRESET_MASK));
    G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(hexchar2int(G_cur_plc_cmd_str[CNTR_FOCUS_OFFS]) & ~(CNTR_FOC_RESET_MASK | CNTR_FOC_GO_MASK));
    G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = '0';
    G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = '0';
  }
  cancel_delayed_work(&G_plccmd_work);
  queue_delayed_work(G_plcdrv_workq, &G_plccmd_work, 20*HZ);
  return 0;
}

static int process_statresp(const char *buf, int len)
{
  queue_delayed_work(G_plcdrv_workq, &G_plcstat_work, HZ/5);
  if (len+1 < PLC_STAT_RESP_LEN)
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to status request (invalid length - %d should be %d.\n", len, PLC_STAT_RESP_LEN);
    return -EIO;
  }
  if (strncmp(buf, PLC_STAT_REQ, 5) != 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to status request (%s).\n", buf);
    return -EIO;
  }
  if ((!check_fcs(buf,len+1)) || (!check_endcode(buf,len)))
  {
    printk(KERN_ERR PRINTK_PREFIX "Received invalid response to status request (invalid FCS/endcode).\n");
    return -EIO;
  }
  if (parse_plc_status(G_cur_plc_stat_str, buf) > 0)
    return 1;
  return 0;
}

static void process_response(const char *buf, int count)
{
  int ret = 0;
  if ((buf[0] != '@') || (buf[count-1] != '*'))
  {
    printk(KERN_ERR PRINTK_PREFIX "Incomplete message received: %s\n", buf);
    return;
  }
  switch (G_await_resp)
  {
    case AWAIT_INIT_STATRESP:
      ret = process_init_statresp(buf, count);
      // ??? Used to return here, but removed it so error handling is performed (without goto), should work, but check
      break;
    case AWAIT_CMD_RESP:
      ret = process_cmdresp(buf, count);
      break;
    case AWAIT_STAT_RESP:
      ret = process_statresp(buf, count);
      break;
    default:
      printk(KERN_INFO PRINTK_PREFIX "Unexpected message received: %s\n", buf);
      break;
  }
  if (ret < 0)
  {
    G_status &= ~PLC_COMM_OK;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&G_readq);
  }
  else if ((ret == 0) && ((G_status & PLC_COMM_OK) == 0))
  {
    G_status |= PLC_COMM_OK;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&G_readq);
  }
  else if (ret > 0)
  {
    G_status |= NEW_STAT_AVAIL;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&G_readq);
  }
  G_await_resp = 0;
  cancel_delayed_work(&G_plcresp_work);
  if (G_cmd_pending)
  {
    send_plc_cmd(NULL);
    G_cmd_pending = 0;
  }
}
#endif

void reset_acq_merlin(void)
{
  G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(hexchar2int(G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS]) | CNTR_ACQRESET_MASK);
  #ifndef PLC_SIM
  if (G_await_resp == 0)
    send_plc_cmd(NULL);
  else
    G_cmd_pending = 1;
  #endif
}
EXPORT_SYMBOL(reset_acq_merlin);

char reset_acq_pending(void)
{
  return (hexchar2int(G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS]) & CNTR_ACQRESET_MASK) > 0;
}
EXPORT_SYMBOL(reset_acq_pending);

void close_pmt_shutter(void)
{
  int tmp;
  G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(hexchar2int(G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS]) & (~CNTR_INSTR_SHUTT_MASK));
  tmp = hexchar2int(G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS]) & 0x08;
  G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = int2hexchar(CNTR_APER_GO_MASK | tmp);
  G_cur_plc_cmd_str[CNTR_APER_NUM_OFFS+1] = '0' + SAFE_APER_SLOT;
  G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = int2hexchar(CNTR_APER_GO_MASK);
  G_cur_plc_cmd_str[CNTR_FILT_NUM_OFFS+1] = '0' + SAFE_FILT_SLOT;
  tmp = hexchar2int(G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS]) & 0x3;
  G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(tmp);
  
  #ifndef PLC_SIM
  if (G_await_resp == 0)
    send_plc_cmd(NULL);
  else
    G_cmd_pending = 1;
  #endif
}
EXPORT_SYMBOL(close_pmt_shutter);

short dome_azm_d(void)
{
  return G_cur_plc_stat.dome_pos / 10;
}
EXPORT_SYMBOL(dome_azm_d);

unsigned long get_enc_ha_pulses(void)
{
  return G_ha_pulses;
}
EXPORT_SYMBOL(get_enc_ha_pulses);

unsigned long get_enc_dec_pulses(void)
{
  return G_dec_pulses;
}
EXPORT_SYMBOL(get_enc_dec_pulses);

void set_handset_handler(void (*handler)(const unsigned char old_hs, const unsigned char new_hs))
{
  G_handset_handler = handler;
}
EXPORT_SYMBOL(set_handset_handler);

static int actplc_open(struct inode *inode, struct file *filp)
{
  G_num_open++;
  if (G_num_open > 1)
    return 0;
  #ifndef PLC_SIM
  INIT_DELAYED_WORK(&G_plcresp_work, plc_resp_timeout);
  INIT_DELAYED_WORK(&G_plcstat_work, send_plc_statreq);
  INIT_DELAYED_WORK(&G_plccmd_work, send_plc_cmd);
  register_plc_handler(&process_response);
  send_plc_statreq(NULL);
  G_await_resp = AWAIT_INIT_STATRESP;
  #else
  // Set some necessary simulated initial status values so initial command string is set correctly
  G_cur_plc_stat_str[STAT_INSTR_SHUTT_OFFS] = '0';
  G_cur_plc_stat_str[STAT_EHT_OFFS] = '0';
  G_cur_plc_stat_str[STAT_ACQMIR_OFFS] = int2hexchar(STAT_ACQMIR_VIEW_MASK);
  G_cur_plc_stat_str[STAT_FILT_NUM_OFFS] = '0';
  G_cur_plc_stat_str[STAT_APER_NUM_OFFS] = '0';
  snprintf(&G_cur_plc_stat_str[STAT_FOCUS_REG_OFFS], STAT_FOCUS_POS_LEN+1, "0350");
  snprintf(&G_cur_plc_stat_str[STAT_DOME_OFFS_OFFS], STAT_DOME_POS_LEN+1, STAT_DOME_POS_FMT, 0);
  init_cmd_str();
  #endif
  
  return 0;
}

static int actplc_release(struct inode *inode, struct file *filp)
{
  if (G_num_open == 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Strange: Device node is being closed, but no open connections.\n");
    return 0;
  }
  G_num_open--;
  if (G_num_open > 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Device released\n");
    return 0;
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Character device closed by all programmes. Closing PLC connection.\n");
  #ifndef PLC_SIM
  unregister_plc_handler();
  cancel_delayed_work(&G_plcstat_work);
  cancel_delayed_work(&G_plccmd_work);
  cancel_delayed_work(&G_plcresp_work);
  G_await_resp = 0;
  #endif
  G_status = 0;
  return 0;
}

static ssize_t actplc_read(struct file *filp, char *buf, size_t len, loff_t *offs)
{
  int ret;
  while ((G_status & NEW_STAT_AVAIL) == 0)
  {
    if (filp->f_flags & O_NONBLOCK)
      return -EAGAIN;
    if (wait_event_interruptible(G_readq, (G_status & NEW_STAT_AVAIL) == 0))
      return -ERESTARTSYS;
  }
  ret = put_user((char)G_status, buf) == 0 ? 1 : -EFAULT;
  if (ret > 0)
    G_status &= ~NEW_STAT_AVAIL;
  return ret;
}

static ssize_t actplc_write(struct file *filp, const char *buf, size_t len, loff_t *offs)
{
  return -ENOTTY;
}

static long actplc_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
  long value = -ENOTTY, ret = 0;
  char tmpstr[256], *ptr_right, *ptr_left;
  unsigned char i;

  #ifndef PLC_SIM
  if ((G_status & PLC_COMM_OK) == 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "User requested PLC command, but PLC communications currently unavailable.\n");
    return -EIO;
  }
  #endif
  
  switch (ioctl_num)
  {
    /// Retrieve current status
    case IOCTL_GET_STATUS:
    {
      ret = copy_to_user((void*)ioctl_param, &G_cur_plc_stat, sizeof(struct plc_status));
      if (ret < 0)
        break;
      G_status &= ~NEW_STAT_AVAIL;
      ret = 0;
      break;
    }
    
    /// Reset PLC watchdog
    case IOCTL_RESET_WATCHDOG:
    {
      i = hexchar2int(G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS]) & (~CNTR_WATCHDOG_MASK);
      G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(CNTR_WATCHDOG_MASK | i);
      ret = 1;
      break;
    }
    
    /// Set guiding azimuth of dome - activates dome guiding
    case IOCTL_SET_DOME_AZM:
    {
      get_user(value, (unsigned long*)ioctl_param);
      snprintf(tmpstr, sizeof(tmpstr), CNTR_DOME_POS_FMT, (unsigned short)(3599 - (value % 3600)));
      for (i=0, ptr_right=tmpstr, ptr_left=&G_cur_plc_cmd_str[CNTR_DOME_POS_OFFS]; i<CNTR_DOME_POS_LEN; i++, ptr_left++, ptr_right++)
      {
        if (*ptr_right == '\0')
          break;
        *(ptr_left) = *(ptr_right);
      }
      G_cur_plc_cmd_str[CNTR_DOME_STAT_OFFS] = int2hexchar(CNTR_DOME_GUIDE_MASK);
      ret = 1;
      break;
    }
    
    /// Move dome manually - disables dome guiding (>0 moves dome left, <0 moves dome right, 0=stop)
    case IOCTL_DOME_MOVE:
    {
      get_user(value, (unsigned long*)ioctl_param);
      if (value > 0)
        G_cur_plc_cmd_str[CNTR_DOME_STAT_OFFS] = int2hexchar(CNTR_DOME_MOVE_LEFT_MASK | CNTR_DOME_MOVE_MASK);
      else if (value < 0)
        G_cur_plc_cmd_str[CNTR_DOME_STAT_OFFS] = int2hexchar(CNTR_DOME_MOVE_MASK);
      else
        G_cur_plc_cmd_str[CNTR_DOME_STAT_OFFS] = '0';
      ret = 1;
      break;
    }
    
    /// Open/close/stop dome shutter (>0=open, <0=close, 0=stop)
    case IOCTL_DOMESHUTT_OPEN:
    {
      get_user(value, (long*)ioctl_param);
      if (value > 0)
        G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = int2hexchar(CNTR_DSHUTT_OPEN_MASK);
      else if (value < 0)
        G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = int2hexchar(CNTR_DSHUTT_CLOSE_MASK);
      else
        G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = '0';
      ret = 1;
      break;
    }
    
    case IOCTL_DROPOUT_OPEN:
    {
      get_user(value, (unsigned long*)ioctl_param);
      if (value > 0)
        G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = int2hexchar(CNTR_DSHUTT_OPEN_MASK);
      else if (value < 0)
        G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = int2hexchar(CNTR_DSHUTT_CLOSE_MASK);
      else
        G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = '0';
      ret = 1;
      break;
    }
    
    /// Go to focus position (<0 is out region, >0 is in region, 0 is init)
    case IOCTL_FOCUS_GOTO:
    {
      get_user(value, (long*)ioctl_param);
      if (value == 0)
      {
        G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(CNTR_FOC_RESET_MASK);
        ret = 1;
        break;
      }
      G_cur_plc_cmd_str[CNTR_FOCUS_REG_OFFS] = value < 0 ? CNTR_FOCUS_REG_OUT_VAL : CNTR_FOCUS_REG_IN_VAL;
      snprintf(tmpstr, sizeof(tmpstr), CNTR_FOCUS_POS_FMT, (int)(value < 0 ? -value : value));
      for (i=0, ptr_right=tmpstr, ptr_left=&G_cur_plc_cmd_str[CNTR_FOCUS_POS_OFFS]; i<CNTR_FOCUS_POS_LEN; i++, ptr_left++, ptr_right++)
      {
        if (*ptr_right == '\0')
          break;
        *(ptr_left) = *(ptr_right);
      }
      G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(CNTR_FOC_GO_MASK);
      ret = 1;
      break;
    }
    
    /// Move focus (<0 move to out region, >0 move to in region, 0 is reset - stop at next slot)
    case IOCTL_FOCUS_MOVE:
    {
      get_user(value, (long*)ioctl_param);
      if (value > 0)
      {
        G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(CNTR_FOC_IN_MASK);
        G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(CNTR_FOC_IN_MASK);
      }
      else if (value < 0)
        G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(CNTR_FOC_OUT_MASK);
      else
        G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = int2hexchar(CNTR_FOC_RESET_MASK);
      ret = 1;
      break;
    }
    
    /// Open/close instrument shutter (1=Open, everything else=Close)
    case IOCTL_INSTRSHUTT_OPEN:
    {
      get_user(value, (unsigned long*)ioctl_param);
      if (value == 1)
        G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(CNTR_INSTR_SHUTT_MASK | CNTR_WATCHDOG_MASK);
      else
        G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(CNTR_WATCHDOG_MASK);
      ret = 1;
      break;
    }
    
    /// Move the acquisition mirror (1=in beam, 2=out beam, everything else=reset - i.e. stop motor)
    case IOCTL_SET_ACQMIR:
    {
      i = hexchar2int(G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS]) & 0x3;
      get_user(value, (unsigned long*)ioctl_param);
      if (value == 1)
        G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(i);
      else if (value == 2)
        G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(CNTR_ACQMIR_INBEAM_MASK | i);
      else
        G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(CNTR_ACQMIR_RESET_MASK | i);
      ret = 1;
      break;
    }
    
    /// Move aperture wheel (0-9=slots 0-9, >0 initialise, <0 reset/stop)
    case IOCTL_MOVE_APER:
    {
      i = hexchar2int(G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS]) & 0x08;
      get_user(value, (long*)ioctl_param);
      if ((value >= 0) && (value < 10))
      {
        G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = int2hexchar(CNTR_APER_GO_MASK | i);
        G_cur_plc_cmd_str[CNTR_APER_NUM_OFFS+1] = '0' + value;
      }
      else if (value > 0)
        G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = int2hexchar(CNTR_APER_INIT_MASK | i);
      else
        G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = int2hexchar(CNTR_APER_RESET_MASK | i);
      ret = 1;
      break;
    }
    
    /// Move filter wheel (0-9=slots 0-9, >0 initialise, <0 reset/stop)
    case IOCTL_MOVE_FILT:
    {
      get_user(value, (long*)ioctl_param);
      if ((value >= 0) && (value < 10))
      {
        G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = int2hexchar(CNTR_APER_GO_MASK);
        G_cur_plc_cmd_str[CNTR_FILT_NUM_OFFS+1] = '0' + value;
      }
      else if (value > 0)
        G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = int2hexchar(CNTR_APER_INIT_MASK);
      else
        G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = int2hexchar(CNTR_APER_RESET_MASK);
      ret = 1;
      break;
    }
    
    /// Set EHT mode (1=lo, 2=hi, everything else=off)
    case IOCTL_SET_EHT:
    {
      i = hexchar2int(G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS]) & 0xC;
      get_user(value, (unsigned long*)ioctl_param);
      if (value == 1)
        G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(CNTR_EHT_LO_MASK | i);
      else if (value == 2)
        G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(CNTR_EHT_HI_MASK | i);
      else
        G_cur_plc_cmd_str[CNTR_ACQMIR_EHT_OFFS] = int2hexchar(i);
      ret = 1;
      break;
    }
    
    /// Set simulated status
    #ifdef PLC_SIM
    case IOCTL_SET_SIM_STATUS:
    {
      ret = copy_from_user(tmpstr, (void*)ioctl_param, PLC_STAT_RESP_LEN);
      if (ret != 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Could not copy simulated status string from user (%ld)\n", ret);
        break;
      }
      G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS] = int2hexchar(hexchar2int(G_cur_plc_cmd_str[CNTR_INSTR_SHUTT_OFFS]) & (~CNTR_ACQRESET_MASK));
      G_cur_plc_cmd_str[CNTR_SHUTTER_OFFS] = '0';
      G_cur_plc_cmd_str[CNTR_DROPOUT_OFFS] = '0';
      G_cur_plc_cmd_str[CNTR_FOCUS_OFFS] = '0';
      G_cur_plc_cmd_str[CNTR_APER_STAT_OFFS] = '0';
      G_cur_plc_cmd_str[CNTR_FILT_STAT_OFFS] = '0';
      parse_plc_status(G_cur_plc_stat_str, tmpstr);
      G_status |= NEW_STAT_AVAIL;
      kill_fasync(&G_async_queue, SIGIO, POLL_IN);
      wake_up_interruptible(&G_readq);
      ret = 0;
      break;
    }
    #endif
    
    /// Get simulated command
    #ifdef PLC_SIM
    case IOCTL_GET_SIM_CMD:
    {
      ret = copy_to_user((void*)ioctl_param, G_cur_plc_cmd_str, PLC_CMD_LEN);
      if (ret != 0)
        printk(KERN_INFO PRINTK_PREFIX "Could not copy simulated command string to user (%ld)\n", ret);
      break;
    }
    #endif
    
    default:
      ret = -ENOTTY;
  }
  
  if (ret <= 0)
    return ret;
  
  #ifndef PLC_SIM
  if (G_await_resp == 0)
    send_plc_cmd(NULL);
  else
    G_cmd_pending = 1;
  #endif
  return 0;
}

static int actplc_fasync(int fd, struct file *filp, int on)
{
  return fasync_helper(fd, filp, on, &G_async_queue);
}

static unsigned int actplc_poll(struct file *filp, poll_table *wait)
{
  unsigned int mask = 0;
  poll_wait(filp, &G_readq,  wait);
  if ((G_status & NEW_STAT_AVAIL) != 0)
    mask |= POLLIN | POLLRDNORM;
  return mask;
}

// Module Declarations
static const struct file_operations Fops =
{
  .owner = THIS_MODULE,
  .read = actplc_read,
  .write = actplc_write,
  .unlocked_ioctl = actplc_ioctl,
  .open = actplc_open,
  .release = actplc_release,
  .fasync = actplc_fasync,
  .poll = actplc_poll  
};

static int __init act_plc_init(void)
{
  #ifndef PLC_SIM
  G_plcdrv_workq = create_singlethread_workqueue("act_plc");
  if (!G_plcdrv_workq)
    return -ENODEV;
  #endif
  G_major = register_chrdev(0, PLC_DEVICE_NAME, &Fops);
  if (G_major < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Can't get major number\n");
    return(G_major);
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Module inserted. Assigned major: %d\n", G_major);
  
  G_class_plc = class_create(THIS_MODULE, PLC_DEVICE_NAME);
  if (G_class_plc == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device class.\n" );
    unregister_chrdev(G_major, PLC_DEVICE_NAME);
    return -ENODEV;
  }
  
  G_dev_plc = device_create(G_class_plc, NULL, MKDEV(G_major, 0), NULL, PLC_DEVICE_NAME);
  if (G_dev_plc == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device.\n" );
    unregister_chrdev(G_major, PLC_DEVICE_NAME);
    class_destroy(G_class_plc);
    return -ENODEV;
  }
  
  init_waitqueue_head(&G_readq);
  
  #ifndef PLC_SIM
  INIT_DELAYED_WORK(&G_plcresp_work, plc_resp_timeout);
  INIT_DELAYED_WORK(&G_plcstat_work, send_plc_statreq);
  INIT_DELAYED_WORK(&G_plccmd_work, send_plc_cmd);
  #else
  G_status |= PLC_COMM_OK;
  #endif
  return 0;
}

static void __exit act_plc_exit(void)
{
  #ifndef PLC_SIM
  cancel_delayed_work(&G_plcresp_work);
  cancel_delayed_work(&G_plcstat_work);
  cancel_delayed_work(&G_plccmd_work);
  destroy_workqueue(G_plcdrv_workq);
  #endif
  device_destroy(G_class_plc, MKDEV(G_major, 0));
  class_destroy(G_class_plc);
  unregister_chrdev(G_major, PLC_DEVICE_NAME);
}


module_init(act_plc_init);
module_exit(act_plc_exit);
MODULE_LICENSE("GPL");
