#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <asm/div64.h>
#include <act_plc/act_plc.h>
#include "motor_intfce.h"
#include "motor_defs.h"
#include "motor_driver.h"

#ifdef MOTOR_SIM
 #define MOTORSIM_PFX  "[MOTOR_SIM] "
#endif

/** Motor controller card IO ports
 * \{ */
#define    TEL_CONTROLLER   0x200      /*  base address */
#define    SLEW_RATE_LS     0x208      /*  write  */
#define    SLEW_RATE_MS     0x209      /*  write  */
#define    SID_RATE_LS      0x20a      /*  write  */
#define    SID_RATE_MS      0x20b      /*  write  */
#define    DIR_CONTROL      0x20c      /*  write  */

#define    LIMITS           0x20c      /*  read   */

#define    STEP_CTR_LS      0x20d      /*  read & write  */
#define    STEP_CTR_MI      0x20e      /*  read & write  */
#define    STEP_CTR_MS      0x20f      /*  read & write  */
/** \} */

/** Motor direction bits
 * \{ */
#define    DIR_WEST_MASK    0x01
#define    DIR_EAST_MASK    0x02
#define    DIR_NORTH_MASK   0x04
#define    DIR_SOUTH_MASK   0x08
#define    DIR_HA_MASK      (DIR_EAST_MASK|DIR_WEST_MASK)
#define    DIR_DEC_MASK     (DIR_NORTH_MASK|DIR_SOUTH_MASK)
#define    DIR_MASK         (DIR_HA_MASK|DIR_DEC_MASK)
#define    TRK_OFF_MASK     0x40
#define    POWER_OFF_MASK   0x80
/** \} */

/** Motor step rates
 * \{ */
#define    RATE_SID              57381U    /* previously 57388, 60000U autoscope value 27890U */
#define    RATE_SLEW             200U
#define    RATE_SET              5578U
#define    RATE_SET_TRACK_W      5084U
#define    RATE_SET_TRACK_E      6179U
#define    RATE_GUIDE            55780U
#define    RATE_GUIDE_TRACK_W    28284U
#define    RATE_GUIDE_TRACK_E    1999196U
#define    RATE_MIN              RATE_GUIDE_TRACK_W
/** \} */

/** Stop goto if within these distances to the target coordinates
 * \{ */
#define TOLERANCE_ENCOD_HA_PULSES   2
#define TOLERANCE_ENCOD_DEC_PULSES  1
#define TOLERANCE_MOTOR_HA_STEPS    3
#define TOLERANCE_MOTOR_DEC_STEPS   2
/** \} */

/** Slow down if this distance from target coordinates
 * \{ */
#define FLOP_ENCOD_HA_PULSES        842
#define FLOP_ENCOD_DEC_PULSES       1683
#define FLOP_MOTOR_HA_STEPS         2334
#define FLOP_MOTOR_DEC_STEPS        3177
/** \} */

/// Period (in milliseconds) for motor monitoring function
#define MON_PERIOD_MSEC             50

/// Hour angle motor steps increment per millisecond (used to adjust target ha during move if tracking)
#define HA_INCR_MOTOR_STEPS         31/1000
/// Hour angle encoder pulses increment per millisecond (used to adjust target ha move goto if tracking)
#define HA_INCR_ENCOD_PULSES        11/1000

struct gotomove_params
{
  int targ_ha, targ_dec;
  unsigned char dir_cur;
  unsigned int rate_req, rate_cur;
  unsigned int timer_ms;
  unsigned char use_encod;
  unsigned char cancelled;
};

struct cardmove_params
{
  unsigned char dir_cur, dir_req;
  unsigned int rate_req, rate_cur;
  unsigned int timer_ms;
  unsigned char handset_move;
  int start_ha;
};

struct tracking_params
{
  int adj_ha_steps, adj_dec_steps;
  int last_steps_ha, last_steps_dec;
  unsigned char dir_cur;
};

union move_params
{
  struct gotomove_params gotomove;
  struct cardmove_params cardmove;
  struct tracking_params tracking;
};

static void update_status(void);
static void update_motor_coords(int reset_motor_steps);
static void check_motors(struct work_struct *work);
unsigned char check_gotomove(void);
unsigned char check_cardmove(void);
unsigned char check_tracking(void);
static unsigned char calc_direction(int targ_ha, int targ_dec, unsigned char use_encod);
static unsigned char get_dir_mask(unsigned char dir_mode);
static unsigned char calc_is_near_target(unsigned char direction, int targ_ha, int targ_dec, unsigned char use_encod);
static unsigned int calc_rate(unsigned char dir, unsigned char speed, unsigned char tracking_on);
static unsigned int calc_ramp_rate(unsigned int rate_cur, unsigned int rate_req);
static void start_move(unsigned char dir, unsigned long rate_req);
static void stop_move(void);
static unsigned char check_soft_lims(void);
static void send_direction(unsigned char dir);
static unsigned char read_limits(void);
static void send_steps(unsigned long steps);
static unsigned long read_steps(void);
static void send_rate(unsigned long rate);

/** Motor status variables
 * \{ */
static unsigned char G_status;
static unsigned char G_hard_limits;
static unsigned char G_alt_limits;
/** \} */
/** Motor position variables
 * \{ */
static int G_motor_steps_ha;
static int G_motor_steps_dec;
static int G_last_motor_steps;
static int G_encod_pulses_ha;
static int G_encod_pulses_dec;
/** Motor motion variable
 * \{ */
static union move_params G_move_params;
/** \} */
/** Miscellaneous variables
 * \{ */
static struct workqueue_struct *G_motordrv_workq;
static struct delayed_work G_motor_work;
static void (*G_status_update) (void) = NULL;
/** \} */
#ifdef MOTOR_SIM
 /** Offline motor simulation variables
  * \{ */
 unsigned long G_sim_motor_steps, G_sim_rate;
 unsigned char G_sim_limits, G_sim_dir;
 /** \} */
#endif

/** Additional telescope limits
 * \{ */
#define TEL_ALT_LIM_MIN_DEC_STEPS 365634
#define TEL_ALT_LIM_DEC_INC_STEPS 19068
#define TEL_ALT_LIM_NUM_DIVS      11
// tel_alt_limits specifies the maximum allowable hour-angle in minutes for a declination within the range corresponding to {(-10)-(-5), (-5)-0, 0-5, 5-10, 10-15, 15-20, 20-25, 25-30, 30-35, 35-40, 40-45} in degrees
// in order for the telescope to remain above 10 degrees in altitude
// static const int G_tel_alt_lim_dec_steps[TEL_ALT_LIM_NUM_DIVS] = {365634, 384702, 403770, 422838, 441906, 460974, 480042, 499110, 518178, 537246, 556314};
static const int G_tel_alt_lim_W_steps[TEL_ALT_LIM_NUM_DIVS] = {-3520, 19677, 43880, 69600, 97474, 128323, 163334, 204335, 254504, 320569, 424189};
static const int G_tel_alt_lim_E_steps[TEL_ALT_LIM_NUM_DIVS] = {1168912, 1145715, 1121512, 1095792, 1067917, 1037069, 1002058, 961057, 910888, 844823, 741203};
/** \} */

void motordrv_init(void (*stat_update)(void))
{
  G_status = 0;
  G_hard_limits = 0;
  G_alt_limits = 0;
  G_motor_steps_ha = 0;
  G_motor_steps_dec = 0;
  G_last_motor_steps = 0;
  G_encod_pulses_ha = 0;
  G_encod_pulses_dec = 0;
  #ifdef MOTOR_SIM
  G_sim_motor_steps = 0;
  G_sim_speed = 0;
  G_sim_limits = 0;
  G_sim_dir = 0;
  #endif
  
  G_motordrv_workq = create_singlethread_workqueue("act_motors");
  INIT_DELAYED_WORK(&G_motor_work, check_motors);
  check_motors(NULL);
  if (G_hard_limits || G_alt_limits)
    G_status |= MOTOR_STAT_ERR_LIMS;
  G_status_update = stat_update;
  set_handset_handler(&handset_handler);
  printk(KERN_CRIT PRINTK_PREFIX "Motor driver loaded. Please initialise the driver's coordinate system by moving the telescope to the Southern and Western electronic limits with the handset.\n");
}

void motordrv_finalise(void)
{
  end_all();
  set_handset_handler(NULL);
  cancel_delayed_work(&G_motor_work);
  G_status_update = NULL;
}

unsigned char get_motor_stat(void)
{
  return G_status;
}

unsigned char get_motor_limits(void)
{
  return G_hard_limits | G_alt_limits;
}

int start_goto(struct motor_goto_cmd *cmd)
{
  unsigned char dir;
  unsigned int rate;
  struct gotomove_params *params = &G_move_params.gotomove;
  
  // Check if goto currently possible
  if (G_status & (MOTOR_STAT_MOVING | MOTOR_STAT_ALLSTOP))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Received goto command, but motors are currently busy. Ignoring.\n");
    return -EAGAIN;
  }
  if ((G_status & (MOTOR_STAT_HA_INIT | MOTOR_STAT_DEC_INIT)) == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Received goto command, but telescope has not been initialised.\n");
    return -EPERM;
  }
  
  // Check if target coordinates valid
  if (cmd->use_encod)
  {
    if ((cmd->targ_dec < 0) || (cmd->targ_ha < 0) || (cmd->targ_dec > MOTOR_ENCOD_N_LIM) || (cmd->targ_ha > MOTOR_ENCOD_E_LIM))
    {
      printk(KERN_ERR PRINTK_PREFIX "Target coordinates beyond limit.\n");
      return -EINVAL;
    }
  }
  else
  {
    if ((cmd->targ_dec < 0) || (cmd->targ_ha < 0) || (cmd->targ_dec > MOTOR_STEPS_N_LIM) || (cmd->targ_ha > MOTOR_STEPS_E_LIM))
    {
      printk(KERN_ERR PRINTK_PREFIX "Target coordinates beyond limit.\n");
      return -EINVAL;
    }
  }
  dir = calc_direction(cmd->targ_ha, cmd->targ_dec, cmd->use_encod);
  if (dir == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Already at target coordinates.\n");
    return -EINVAL;
  }
  if ((dir & (G_alt_limits | G_hard_limits)) > 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Telescope limit reached in requested direction.");
    return -EINVAL;
  }
  
  rate = calc_rate(dir, cmd->speed, 0);
  if (rate == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Invalid speed setting (%hhu).\n", cmd->speed);
    return -EINVAL;
  }
  
  // Set global variables appropriately
  params->targ_ha = cmd->targ_ha;
  params->targ_dec = cmd->targ_dec;
  params->dir_cur = dir;
  params->rate_req = rate;
  params->rate_cur = (rate > RATE_MIN) ? rate : RATE_MIN;
  params->timer_ms = 0;
  params->use_encod = cmd->use_encod;
  params->cancelled = FALSE;
  
  start_move(dir, params->rate_cur);
  G_status |= MOTOR_STAT_GOTO;
  update_status();
  return 0;
}

void end_goto(void)
{
  struct gotomove_params *params = &G_move_params.gotomove;
  if ((G_status & MOTOR_STAT_GOTO) == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "No goto motion is currently underway. Not cancelling goto.\n");
    return;
  }
//   printk(KERN_DEBUG PRINTK_PREFIX "Goto end requested.\n");
  params->rate_req = RATE_MIN;
  params->cancelled = TRUE;
  if (params->rate_cur >= RATE_MIN)
  {
    stop_move();
    G_status &= ~MOTOR_STAT_MOVING;
    update_status();
    if (G_status & MOTOR_STAT_TRACKING)
      toggle_tracking(TRUE);
  }
}

int start_card(struct motor_card_cmd *cmd)
{
  unsigned char dir;
  unsigned int rate;
  struct cardmove_params *params = &G_move_params.cardmove;
  
  // Check if goto currently possible
  if (G_status & (MOTOR_STAT_MOVING | MOTOR_STAT_ALLSTOP))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Received goto command, but motors are currently busy. Ignoring.\n");
    return -EAGAIN;
  }
  if ((G_status & (MOTOR_STAT_HA_INIT | MOTOR_STAT_DEC_INIT)) == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Received goto command, but telescope has not been initialised.\n");
    return -EPERM;
  }

  // Check if requested direction valid
//  printk(KERN_DEBUG PRINTK_PREFIX "start_card direction: %hhu\n", cmd->dir);
  dir = get_dir_mask(cmd->dir);
  if (dir == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Start card: 0 direction specified.\n");
    return -EINVAL;
  }
  if ((dir & (G_alt_limits | G_hard_limits)) > 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Start card: invalid direction specified (0x%x 0x%x 0x%x).\n", dir, G_alt_limits, G_hard_limits);
    return -EINVAL;
  }
  
  rate = calc_rate(dir, cmd->speed, (G_status & MOTOR_STAT_TRACKING) > 0);
  if (rate == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Invalid speed setting (%hhu).\n", cmd->speed);
    return -EINVAL;
  }
  
  params->dir_req = dir;
  params->rate_req = rate;
  if (((dir & DIR_HA_MASK) == 0) && ((G_status & MOTOR_STAT_TRACKING) > 0))
    params->start_ha = G_motor_steps_ha;
  else
    params->start_ha = 0;
  // If cardinal move is already being done, only change requested direction and rate and let check_cardmove do the rest
  if (G_status & MOTOR_STAT_CARD)
    return 0;
  params->dir_cur = params->dir_req;
//  printk(KERN_DEBUG PRINTK_PREFIX "start_card dir_cur, dir_req: %hhu, %hhu\n", params->dir_cur, params->dir_req);
  params->rate_cur = rate > RATE_MIN ? rate : RATE_MIN;
  params->timer_ms = 0;
  params->handset_move = FALSE;
  
  start_move(params->dir_cur, params->rate_cur);
  G_status |= MOTOR_STAT_CARD;
  update_status();
  return 0;
}

void end_card(void)
{
  struct cardmove_params *params = &G_move_params.cardmove;
  if (((G_status & MOTOR_STAT_CARD) == 0) || (params->handset_move))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Not moving in cardinal direction or motion initiated by handset. Not ending motion.\n");
    return;
  }
  params->rate_req = RATE_MIN;
  params->dir_req = 0;
  if ((params->dir_cur == 0) || (params->rate_cur >= RATE_MIN))
  {
    stop_move();
    G_status &= ~MOTOR_STAT_MOVING;
    if (G_status & MOTOR_STAT_TRACKING)
      toggle_tracking(TRUE);
    update_status();
  }
}

void end_all(void)
{
  stop_move();
  G_status &= ~(MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING);
  update_status();
}

void toggle_all_stop(unsigned char stop_on)
{
  if (stop_on)
  {
    stop_move();
    G_status &= ~(MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING);
    G_status |= MOTOR_STAT_ALLSTOP;
  }
  else
    G_status &= ~MOTOR_STAT_ALLSTOP;
  update_status();
}

void toggle_tracking(unsigned char tracking_on)
{
  if (G_status & MOTOR_STAT_ALLSTOP)
  {
    if (tracking_on)
      printk(KERN_INFO PRINTK_PREFIX "Cannot activate tracking because all-stop is enabled.\n");
    return;
  }
  if ((G_status & MOTOR_STAT_HA_INIT) == 0)
  {
    if (tracking_on)
      printk(KERN_INFO PRINTK_PREFIX "Cannot activate tracking because HA not initialised.\n");
    return;
  }
  if (tracking_on)
    G_status |= MOTOR_STAT_TRACKING;
  else
    G_status &= ~MOTOR_STAT_TRACKING;
  update_status();
  if (G_status & MOTOR_STAT_MOVING)
    return;
  if (tracking_on)
  {
    struct tracking_params *params = &G_move_params.tracking;
    params->adj_ha_steps = params->adj_dec_steps = 0;
    params->dir_cur = 0;
    params->last_steps_ha = G_motor_steps_ha;
    params->last_steps_dec = G_motor_steps_dec;
    start_move(DIR_WEST_MASK, RATE_SID);
  }
  else
    stop_move();
}

void adjust_tracking(int adj_ha, int adj_dec)
{
  if ((G_status & MOTOR_STAT_MOVING) || ((G_status & MOTOR_STAT_TRACKING) == 0))
    return;
  if ((abs(adj_ha) < TOLERANCE_MOTOR_HA_STEPS) && (abs(adj_dec) < TOLERANCE_MOTOR_DEC_STEPS))
    return;
  G_move_params.tracking.adj_ha_steps = adj_ha;
  G_move_params.tracking.adj_dec_steps = adj_dec;
}

void get_coord_motor(struct motor_tel_coord *coord)
{
  if (G_status & MOTOR_STAT_HA_INIT)
    coord->tel_ha = G_motor_steps_ha;
  else
    coord->tel_ha = 0;
  if (G_status & MOTOR_STAT_DEC_INIT)
    coord->tel_dec = G_motor_steps_dec;
  else
    coord->tel_dec = 0;
}

void get_coord_encod(struct motor_tel_coord *coord)
{
  if (G_status & MOTOR_STAT_HA_INIT)
    coord->tel_ha = G_encod_pulses_ha;
  else
    coord->tel_ha = 0;
  if (G_status & MOTOR_STAT_DEC_INIT)
    coord->tel_dec = G_encod_pulses_dec;
  else
    coord->tel_dec = 0;
}

void handset_handler(unsigned char old_hs, unsigned char new_hs)
{
  unsigned char dir, speed;
  unsigned int rate;
  struct cardmove_params *params = &G_move_params.cardmove;
  
  // Check if all stop toggled
  if (((old_hs & HS_DIR_DEC_MASK) != HS_DIR_DEC_MASK) && ((new_hs & HS_DIR_DEC_MASK) == HS_DIR_DEC_MASK))
  {
    if (G_status & MOTOR_STAT_ALLSTOP)
    {
      printk(KERN_INFO PRINTK_PREFIX "Disabling emergency stop (by handset).\n");
      toggle_all_stop(FALSE);
    }
    else
    {
      printk(KERN_INFO PRINTK_PREFIX "Enabling emergency stop (by handset).\n");
      toggle_all_stop(TRUE);
    }
    return;
  }
  
  // Check for allstop
  if ((G_status & MOTOR_STAT_ALLSTOP) && ((new_hs & HS_DIR_MASK) > 0))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Handset move requested, but all-stop is currently active.\n");
    return;
  }

  // Check if tracking toggled
  if (((old_hs & HS_DIR_HA_MASK) != HS_DIR_HA_MASK) && ((new_hs & HS_DIR_HA_MASK) == HS_DIR_HA_MASK))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Toggling tracking handset (%hhu %hhu)\n", old_hs, new_hs);
    if (G_status & MOTOR_STAT_TRACKING)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Disabling tracking (by handset).\n");
      toggle_tracking(FALSE);
    }
    else
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Enabling tracking (by handset).\n");
      toggle_tracking(TRUE);
    }
    return;
  }
  
  // Double-check for handset change (first check should be in act_plc)
  if (old_hs == new_hs)
    return;
  // Check for move command not initiated by handset
  if (((G_status & MOTOR_STAT_CARD) && (!params->handset_move)) || (G_status & MOTOR_STAT_GOTO))
  {
    printk(KERN_INFO PRINTK_PREFIX "Telescope is busy, cannot perform handset move.\n");
    return;
  }
  
  // No direction buttons pressed, end move
  if (((new_hs & HS_DIR_MASK) == 0) && (G_status & MOTOR_STAT_CARD) && (params->handset_move))
  {
    params->dir_req = 0;
    return;
  }
  
  // Find requested speed, direction and rate
  if (new_hs & HS_SPEED_GUIDE_MASK)
    speed = MOTOR_SPEED_GUIDE;
  else if (new_hs & HS_SPEED_SLEW_MASK)
    speed = MOTOR_SPEED_SLEW;
  else
    speed = MOTOR_SPEED_SET;
  dir = 0;
  if (new_hs & HS_DIR_NORTH_MASK)
    dir |= DIR_NORTH_MASK;
  else if (new_hs & HS_DIR_SOUTH_MASK)
    dir |= DIR_SOUTH_MASK;
  if (new_hs & HS_DIR_EAST_MASK)
    dir |= DIR_EAST_MASK;
  else if (new_hs & HS_DIR_WEST_MASK)
    dir |= DIR_WEST_MASK;
  rate = calc_rate(dir, speed, (G_status & MOTOR_STAT_TRACKING) > 0);
  
  params->dir_req = dir;
  params->rate_req = rate;
  if (((dir & DIR_HA_MASK) == 0) && ((G_status & MOTOR_STAT_TRACKING) > 0))
    params->start_ha = G_motor_steps_ha;
  else
    params->start_ha = 0;
  // If handset cardinal move is already being done, only change requested direction and rate and let check_cardmove do the rest
  if (G_status & MOTOR_STAT_CARD)
    return;
  // Otherwise start cardinal move
  params->dir_cur = dir;
  params->rate_cur = rate > RATE_MIN ? rate : RATE_MIN;
  params->timer_ms = 0;
  params->handset_move = TRUE;
  
  if (dir != 0)
  {
    start_move(dir, params->rate_cur);
    G_status |= MOTOR_STAT_CARD;
    update_status();
  }
}

#ifdef MOTOR_SIM
void set_sim_steps(unsigned long steps)
{
  G_sim_motor_steps = steps;
}

void set_sim_limits(unsigned char limits)
{
  G_sim_limits = limits;
}

unsigned long get_sim_steps(void)
{
//   return G_last_motor_steps;
  /// TODO: Unbreak this
  return 0;
}

unsigned char get_sim_dir(void)
{
  return G_sim_dir;
}

unsigned long get_sim_speed(void)
{
  return G_sim_speed;
}
#endif

void set_motor_steps_ha(int new_steps)
{
  if ((G_status & MOTOR_STAT_HA_INIT) == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Setting HA motor steps to %d. HA not initialised.\n", new_steps);
    G_status |= MOTOR_STAT_HA_INIT;
    update_status();
  }
  else
    printk(KERN_DEBUG PRINTK_PREFIX "Setting HA motor steps to %d. HA initialised, currently at %d steps.\n", new_steps, G_motor_steps_ha);
  G_motor_steps_ha = new_steps;
}

void set_motor_steps_dec(int new_steps)
{
  if ((G_status & MOTOR_STAT_DEC_INIT) == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Setting Dec motor steps to %d. Dec not initialised.\n", new_steps);
    G_status |= MOTOR_STAT_DEC_INIT;
    update_status();
  }
  else
    printk(KERN_DEBUG PRINTK_PREFIX "Setting Dec motor steps to %d. Dec initialised, currently at %d steps.\n", new_steps, G_motor_steps_dec);
  G_motor_steps_dec = new_steps;
}

static void update_status(void)
{
  if (G_status_update != NULL)
    (*G_status_update)();
}

static void update_motor_coords(int reset_motor_steps)
{
  static int last_motor_steps = 0;
  int new_motor_steps = read_steps();
  unsigned char dir = 0;
  if (G_status & MOTOR_STAT_GOTO)
    dir = G_move_params.gotomove.dir_cur;
  else if (G_status & MOTOR_STAT_CARD)
    dir = G_move_params.cardmove.dir_cur;
  else if (G_status & MOTOR_STAT_TRACKING)
    dir = G_move_params.tracking.dir_cur != 0 ? G_move_params.tracking.dir_cur : DIR_WEST_MASK;
  if (dir & DIR_WEST_MASK)
    G_motor_steps_ha -= last_motor_steps - new_motor_steps;
  else if (dir & DIR_EAST_MASK)
    G_motor_steps_ha += last_motor_steps - new_motor_steps;
  if (dir & DIR_NORTH_MASK)
    G_motor_steps_dec += last_motor_steps - new_motor_steps;
  else if (dir & DIR_SOUTH_MASK)
    G_motor_steps_dec -= last_motor_steps - new_motor_steps;
  if (reset_motor_steps >= 0)
    last_motor_steps = reset_motor_steps;
  else
    last_motor_steps = new_motor_steps;
}

void check_motors(struct work_struct *work)
{
  unsigned char new_limits, req_status_update = 0;
  
  if ((G_status & (MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING)) == 0)
  {
    queue_delayed_work(G_motordrv_workq, &G_motor_work, MON_PERIOD_MSEC * HZ / 1000);
    return;
  }

  // Check hard limits first, just in case
  new_limits = read_limits();
  if (G_hard_limits != new_limits)
  {
    if (new_limits != 0)
      printk(KERN_INFO PRINTK_PREFIX "Hard telescope limit reached (0x%x).\n", new_limits);
    if (new_limits & DIR_SOUTH_MASK)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Setting declination zero point at Southern limit.\n");
      G_motor_steps_dec = 0;
      if ((G_status & MOTOR_STAT_DEC_INIT) == 0)
      {
        G_status |= MOTOR_STAT_DEC_INIT;
        req_status_update = TRUE;
      }
    }
    if (new_limits & DIR_WEST_MASK)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Setting hour angle zero point at Western limit.\n");
      G_motor_steps_ha = 0;
      if ((G_status & MOTOR_STAT_HA_INIT) == 0)
      {
        G_status |= MOTOR_STAT_HA_INIT;
        req_status_update = TRUE;
      }
    }
    G_hard_limits = new_limits;
  }
  new_limits = check_soft_lims();
  if (new_limits != G_alt_limits)
  {
    printk(KERN_INFO PRINTK_PREFIX "Soft telescope limit reached (0x%x).\n", new_limits);
    G_alt_limits = new_limits;
  }
  new_limits |= G_hard_limits;
  if ((new_limits) && ((G_status & MOTOR_STAT_ERR_LIMS) == 0))
  {
    G_status |= MOTOR_STAT_ERR_LIMS;
    req_status_update = TRUE;
  }
  else if ((new_limits == 0) && (G_status & MOTOR_STAT_ERR_LIMS))
  {
    G_status &= ~MOTOR_STAT_ERR_LIMS;
    req_status_update = TRUE;
  }
  
  update_motor_coords(-1);
  
  if (G_status & MOTOR_STAT_GOTO)
  {
    if (check_gotomove())
      req_status_update = TRUE;
  }
  else if (G_status & MOTOR_STAT_CARD)
  {
    if (check_cardmove())
      req_status_update = TRUE;
  }
  else if (G_status & MOTOR_STAT_TRACKING)
  {
    if (check_tracking())
      req_status_update = TRUE;
  }
  
  if (req_status_update)
    update_status();
  queue_delayed_work(G_motordrv_workq, &G_motor_work, MON_PERIOD_MSEC * HZ / 1000);
}

unsigned char check_gotomove(void)
{
  unsigned int rate_req, rate_new, new_targ_ha;
  unsigned char dir_new;
  struct gotomove_params *params = &G_move_params.gotomove;
  
//   printk(KERN_DEBUG PRINTK_PREFIX "Checking goto.\n");
  if ((G_hard_limits | G_alt_limits) & params->dir_cur)
  {
    printk(KERN_INFO PRINTK_PREFIX "Limit reached in move direction. Cancelling goto.\n");
    stop_move();
    G_status &= ~(MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING);
    update_status();
    return TRUE;
  }
  
  if (params->cancelled)
  {
//     printk(KERN_DEBUG PRINTK_PREFIX "Goto cancelled\n");
    if (params->rate_cur < RATE_MIN)
    {
      rate_new = calc_ramp_rate(params->rate_cur, RATE_MIN);
//       printk(KERN_DEBUG PRINTK_PREFIX "Ramping down: %u\n", rate_new);
      send_rate(rate_new);
      params->rate_cur = rate_new;
      return FALSE;
    }
    stop_move();
    G_status &= ~MOTOR_STAT_GOTO;
    if ((G_status & MOTOR_STAT_TRACKING) != 0)
      toggle_tracking(TRUE);
    return TRUE;
  }
  
  params->timer_ms += MON_PERIOD_MSEC;
  if ((G_status & MOTOR_STAT_TRACKING) == 0)
    new_targ_ha = params->targ_ha;
  else if (params->use_encod)
    new_targ_ha = params->targ_ha - (params->timer_ms*HA_INCR_ENCOD_PULSES);
  else
    new_targ_ha = params->targ_ha - (params->timer_ms*HA_INCR_MOTOR_STEPS);
  
  dir_new = calc_direction(new_targ_ha, params->targ_dec, params->use_encod);
//   printk(KERN_DEBUG PRINTK_PREFIX "Goto track-adjusted coordinates,dir: %u %u 0x%x\n", new_targ_ha, params->targ_dec, dir_new);
  if ((dir_new == 0) && (params->rate_cur >= RATE_MIN))
  {
//     printk(KERN_DEBUG PRINTK_PREFIX "Goto complete (HA %d; Dec %d).\n", params->use_encod ? G_encod_pulses_ha : G_motor_steps_ha, params->use_encod ? G_encod_pulses_dec : G_motor_steps_dec);
    G_status &= ~MOTOR_STAT_GOTO;
    stop_move();
    if ((G_status & MOTOR_STAT_TRACKING) != 0)
      toggle_tracking(TRUE);
    return TRUE;
  }
  if ((dir_new != params->dir_cur) && (params->rate_cur >= RATE_MIN))
  {
    start_move(dir_new, params->rate_cur);
    params->dir_cur = dir_new;
  }
  
  if (calc_is_near_target(params->dir_cur, new_targ_ha, params->targ_dec, params->use_encod))
    rate_req = RATE_MIN;
  else
    rate_req = params->rate_req;
  rate_new = calc_ramp_rate(params->rate_cur, rate_req);
  if (rate_new != params->rate_cur)
  {
    send_rate(rate_new);
    params->rate_cur = rate_new;
  }
  return FALSE;
}

unsigned char check_cardmove(void)
{
  struct cardmove_params *params = &G_move_params.cardmove;
  unsigned int rate_req;
  
  // Check limits - either hard limits or soft limits while not under handset control
  if ((G_hard_limits & params->dir_cur) || ((G_alt_limits & params->dir_cur) && (!params->handset_move)))
  {
    printk(KERN_INFO PRINTK_PREFIX "Limit reached in move direction. Cancelling cardinal move.\n");
    stop_move();
    G_status &= ~(MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING);
    return FALSE;
  }

  // If no motion in HA, increment timer so we can catch up later (if moving in HA, even if also moving in Dec, move rate will account for sidereal motion)
/*  if ((params->start_ha == 0) && ((params->dir_cur & DIR_HA_MASK) == 0) && (G_status | MOTOR_STAT_TRACKING))
  {
//     printk(KERN_DEBUG PRINTK_PREFIX "Setting card move start hour angle.\n");
    params->start_ha = G_motor_steps_ha;
  }
  else if ((params->start_ha != 0) && (((params->dir_cur & DIR_HA_MASK) != 0) || ((G_status | MOTOR_STAT_TRACKING) == 0)))
  {
//     printk(KERN_DEBUG PRINTK_PREFIX "Unsetting card move start hour angle.\n");
    params->start_ha = 0;
  }*/
  if (params->start_ha != 0)
    params->timer_ms += MON_PERIOD_MSEC;
  
  // Calculate (and set) required rate
  rate_req = params->rate_req;
  if (params->dir_cur != params->dir_req)
    rate_req = RATE_MIN;
  else if (params->rate_cur != params->rate_req)
    rate_req = params->rate_req;
  if (rate_req != params->rate_cur)
  {
    unsigned int rate_new = calc_ramp_rate(params->rate_cur, rate_req);
//     printk(KERN_DEBUG PRINTK_PREFIX "Ramping down for direction change (0x%x 0x%x %u)\n", params->dir_cur, params->dir_req, rate_req);
    send_rate(rate_new);
    params->rate_cur = rate_new;
  }
  
  // Only direction changes and stops remain, nothing more to do if telescope moving too fast
  if (params->rate_cur < RATE_MIN)
    return FALSE;
  
  // If move cancelled
  if (params->dir_req == 0)
  {
    unsigned char dir_new = 0;
    // If tracking is enabled, check if we need to catch up with sidereal motion (Western direction only)
    if (params->start_ha != 0)
    {
      int new_targ_ha = params->start_ha - (params->timer_ms*HA_INCR_MOTOR_STEPS);
      dir_new = calc_direction(new_targ_ha, G_motor_steps_dec, FALSE) & DIR_WEST_MASK;
//       printk(KERN_DEBUG PRINTK_PREFIX "Tracking adjusted ha: %d (%d %d %d)\n", new_targ_ha, G_motor_steps_ha, params->timer_ms*HA_INCR_MOTOR_STEPS, params->start_ha);
    }
//     printk(KERN_DEBUG PRINTK_PREFIX "Slow enough to stop (new dir: 0x%x 0x%x)\n", dir_new, params->dir_cur);
    if (dir_new == params->dir_cur)
      return FALSE;
    stop_move();
    if (dir_new != 0)
    {
      params->dir_cur = dir_new;
      start_move(dir_new, RATE_MIN);
      return FALSE;
    }
    G_status &= ~MOTOR_STAT_CARD;
    if ((G_status & MOTOR_STAT_TRACKING) != 0)
      toggle_tracking(TRUE);
    return TRUE;
  }
  
  // Change of direction required
  if ((params->dir_req != params->dir_cur) && (params->rate_cur >= RATE_MIN))
  {
    stop_move();
    params->dir_cur = params->dir_req;
    start_move(params->dir_req, RATE_MIN);
  }
  return FALSE;
}

unsigned char check_tracking(void)
{
  struct tracking_params *params = &G_move_params.tracking;
  int ha_steps, dec_steps;
  unsigned char dir_new;
  unsigned int rate_new;
  
  if ((G_hard_limits | G_alt_limits) & (params->dir_cur | DIR_WEST_MASK))
  {
    printk(KERN_INFO PRINTK_PREFIX "Limit reached in tracking (or track adjusting) direction. Disabling tracking.\n");
    toggle_all_stop(TRUE);
    return TRUE;
  }

  ha_steps = params->last_steps_ha - G_motor_steps_ha - MON_PERIOD_MSEC*HA_INCR_MOTOR_STEPS;
  dec_steps = G_motor_steps_dec - params->last_steps_dec;
  if ((params->dir_cur & DIR_HA_MASK) != 0)
    params->adj_ha_steps += ha_steps;
/*  if (((params->dir_cur & DIR_WEST_MASK) && (ha_steps <= 0)) || ((params->dir_cur & DIR_EAST_MASK) && (ha_steps >= 0)))
    params->adj_ha_steps -= ha_steps;
  else if (params->dir_cur & DIR_HA_MASK)
    printk(KERN_INFO PRINTK_PREFIX "HA tracking adjustment: number of steps do not agree with adjustment direction (dir %hhu, adj %d).\n", (params->dir_cur & DIR_HA_MASK), ha_steps);*/
  if ((params->dir_cur & DIR_DEC_MASK) != 0)
    params->adj_dec_steps += dec_steps;
/*  if (((params->dir_cur & DIR_NORTH_MASK) && (dec_steps >= 0)) || ((params->dir_cur & DIR_SOUTH_MASK) && (dec_steps <= 0)))
    params->adj_dec_steps -= dec_steps;
  else if (params->dir_cur & DIR_DEC_MASK)
    printk(KERN_INFO PRINTK_PREFIX "Dec tracking adjustment: number of steps do not agree with adjustment direction (dir %hhu, adj %d).\n", params->dir_cur & DIR_DEC_MASK), dec_steps);*/
  
  // Update steps stored in tracking parameters
  params->last_steps_ha = G_motor_steps_ha;
  params->last_steps_dec = G_motor_steps_dec;
  if ((params->adj_ha_steps == 0) && (params->adj_dec_steps == 0) && (params->dir_cur == 0))
    return FALSE;
  // In order to prevent cumulative errors in the adjustments, zero them when we're close to 0
  if (abs(params->adj_ha_steps) <= TOLERANCE_MOTOR_HA_STEPS)
    params->adj_ha_steps = 0;
  if (abs(params->adj_dec_steps) <= TOLERANCE_MOTOR_DEC_STEPS)
    params->adj_dec_steps = 0;
  
  ha_steps = G_motor_steps_ha + params->adj_ha_steps;
  dec_steps = G_motor_steps_dec + params->adj_dec_steps;
  dir_new = calc_direction(ha_steps, dec_steps, FALSE);
  if (dir_new == params->dir_cur)
    return FALSE;
  params->dir_cur = dir_new;
  if (dir_new == 0)
  {
    start_move(DIR_WEST_MASK, RATE_SID);
    return FALSE;
  }
  if ((dir_new & DIR_HA_MASK) == 0)
  {
    dir_new |= DIR_WEST_MASK;
    rate_new = RATE_SID;
  }
  else
    rate_new = calc_rate(dir_new, MOTOR_SPEED_GUIDE, TRUE);
  start_move(dir_new, rate_new);
  return FALSE;
}

static unsigned char calc_direction(int targ_ha, int targ_dec, unsigned char use_encod)
{
  unsigned char dir = 0;
  if (use_encod)
  {
    if (abs(targ_dec-G_encod_pulses_dec) > TOLERANCE_ENCOD_DEC_PULSES)
    {
      if (targ_dec > G_encod_pulses_dec)
        dir |= DIR_SOUTH_MASK;
      else
        dir |= DIR_NORTH_MASK;
    }
    if (abs(targ_ha-G_encod_pulses_ha) > TOLERANCE_ENCOD_HA_PULSES)
    {
      if (targ_ha > G_encod_pulses_ha)
        dir |= DIR_EAST_MASK;
      else
        dir |= DIR_WEST_MASK;
    }
  }
  else
  {
    if (abs(targ_dec-G_motor_steps_dec) > TOLERANCE_MOTOR_DEC_STEPS)
    {
      if (targ_dec < G_motor_steps_dec)
        dir |= DIR_SOUTH_MASK;
      else
        dir |= DIR_NORTH_MASK;
    }
    if (abs(targ_ha-G_motor_steps_ha) > TOLERANCE_MOTOR_HA_STEPS)
    {
      if (targ_ha > G_motor_steps_ha)
        dir |= DIR_EAST_MASK;
      else
        dir |= DIR_WEST_MASK;
    }
  }
  return dir;
}

static unsigned char get_dir_mask(unsigned char dir_mode)
{
  unsigned char ret = 0;
  switch(dir_mode)
  {
    case MOTOR_DIR_NORTH:
      ret = DIR_NORTH_MASK;
      break;
    case MOTOR_DIR_NORTHWEST:
      ret = DIR_NORTH_MASK | DIR_WEST_MASK;
      break;
    case MOTOR_DIR_WEST:
      ret = DIR_WEST_MASK;
      break;
    case MOTOR_DIR_SOUTHWEST:
      ret = DIR_SOUTH_MASK | DIR_WEST_MASK;
      break;
    case MOTOR_DIR_SOUTH:
      ret = DIR_SOUTH_MASK;
      break;
    case MOTOR_DIR_SOUTHEAST:
      ret = DIR_SOUTH_MASK | DIR_EAST_MASK;
      break;
    case MOTOR_DIR_EAST:
      ret = DIR_EAST_MASK;
      break;
    case MOTOR_DIR_NORTHEAST:
      ret = DIR_NORTH_MASK | DIR_EAST_MASK;
      break;
    default:
      ret = 0;
  }
  return ret;
}

static unsigned char calc_is_near_target(unsigned char direction, int targ_ha, int targ_dec, unsigned char use_encod)
{
  if (use_encod)
  {
    if ((direction & (DIR_SOUTH_MASK | DIR_NORTH_MASK)) && (abs(targ_dec - G_encod_pulses_dec) < FLOP_ENCOD_DEC_PULSES))
      return TRUE;
    if ((direction & (DIR_WEST_MASK | DIR_EAST_MASK)) && (abs(targ_ha - G_encod_pulses_ha) < FLOP_ENCOD_HA_PULSES))
      return TRUE;
  }
  else
  {
    if ((direction & (DIR_SOUTH_MASK | DIR_NORTH_MASK)) && (abs(targ_dec - G_motor_steps_dec) < FLOP_MOTOR_DEC_STEPS))
      return TRUE;
    if ((direction & (DIR_WEST_MASK | DIR_EAST_MASK)) && (abs(targ_ha - G_motor_steps_ha) < FLOP_MOTOR_HA_STEPS))
      return TRUE;
  }
  return FALSE;
}

static unsigned int calc_rate(unsigned char dir, unsigned char speed, unsigned char tracking_on)
{
  unsigned int rate;
  switch(speed)
  {
    case (MOTOR_SPEED_SLEW):
      rate = RATE_SLEW;
      break;
    case (MOTOR_SPEED_SET):
      if (!tracking_on)
        rate = RATE_SET;
      else if (dir & DIR_WEST_MASK)
        rate = RATE_SET_TRACK_W;
      else if (dir & DIR_EAST_MASK)
        rate = RATE_SET_TRACK_E;
      else
        rate = RATE_SET;
      break;
    case (MOTOR_SPEED_GUIDE):
      if (!tracking_on)
        rate = RATE_GUIDE;
      else if (dir & DIR_WEST_MASK)
        rate = RATE_GUIDE_TRACK_W;
      else if (dir & DIR_EAST_MASK)
        rate = RATE_GUIDE_TRACK_E;
      else
        rate = RATE_GUIDE;
      break;
    default:
      rate = 0;
  }
  return rate;
}

static unsigned int calc_ramp_rate(unsigned int rate_cur, unsigned int rate_req)
{
  unsigned int rate_new;
  if (rate_req < rate_cur)
  {
    rate_new = (rate_cur)*3/4;
    if (rate_new < rate_req)
      rate_new = rate_req;
    if (rate_new < RATE_SLEW)
      rate_new = RATE_SLEW;
  }
  else if (rate_cur == rate_req)
    rate_new = rate_req;
  else
  {
    rate_new = rate_cur*4/3;
    if (rate_new > rate_req)
      rate_new = rate_req;
    if (rate_new > RATE_MIN)
      rate_new = RATE_MIN;
  }
  return rate_new;
}

static void start_move(unsigned char dir, unsigned long rate_req)
{
  update_motor_coords(0xFFFFFF);
  send_steps(~((unsigned long)0));
  send_rate(rate_req > RATE_MIN ? rate_req : RATE_MIN);
  send_direction(dir);
}

static void stop_move(void)
{
  update_motor_coords(0);
  send_steps(0);
  send_direction(0);
}

static unsigned char check_soft_lims(void)
{
  unsigned char idx=TEL_ALT_LIM_NUM_DIVS+1, lim_dir = 0;
  int64_t grad_u, zerop, dec_l, tmp_div;
  int tel_ha_lim_E = 0, tel_ha_lim_W = 0;
  
  if (G_motor_steps_dec < TEL_ALT_LIM_MIN_DEC_STEPS)
    return 0;
  idx = (G_motor_steps_dec - TEL_ALT_LIM_MIN_DEC_STEPS) / TEL_ALT_LIM_DEC_INC_STEPS;
  if (idx >= TEL_ALT_LIM_NUM_DIVS-1)
    return DIR_NORTH_MASK | DIR_WEST_MASK | DIR_EAST_MASK;
  dec_l = TEL_ALT_LIM_MIN_DEC_STEPS + idx*TEL_ALT_LIM_DEC_INC_STEPS;
  grad_u = (G_tel_alt_lim_W_steps[idx+1] - G_tel_alt_lim_W_steps[idx]);
  tmp_div = grad_u * dec_l;
  do_div(tmp_div,TEL_ALT_LIM_DEC_INC_STEPS);
  zerop = G_tel_alt_lim_W_steps[idx] - tmp_div;
  tmp_div = grad_u * G_motor_steps_dec;
  do_div(tmp_div, TEL_ALT_LIM_DEC_INC_STEPS);
  tel_ha_lim_W = tmp_div + zerop;
  grad_u *= -1;
  tmp_div = grad_u*dec_l;
  do_div(tmp_div, TEL_ALT_LIM_DEC_INC_STEPS);
  zerop = G_tel_alt_lim_E_steps[idx] - tmp_div;
  tmp_div = grad_u * G_motor_steps_dec;
  do_div(tmp_div, TEL_ALT_LIM_DEC_INC_STEPS);
  tel_ha_lim_E = tmp_div + zerop;
  if (G_motor_steps_ha < tel_ha_lim_W)
    lim_dir |= DIR_NORTH_MASK | DIR_WEST_MASK;
  if (G_motor_steps_ha > tel_ha_lim_E)
    lim_dir |= DIR_NORTH_MASK | DIR_EAST_MASK;
  return lim_dir;
}

static void send_direction(unsigned char dir)
{
  #ifndef MOTOR_SIM
   if (dir == 0)
     outb_p(POWER_OFF_MASK | TRK_OFF_MASK, DIR_CONTROL);
   else
    outb_p(dir | TRK_OFF_MASK, DIR_CONTROL);
  #else
   G_sim_dir = dir;
  #endif
}

static unsigned char read_limits(void)
{
  #ifndef MOTOR_SIM
   return inb_p(LIMITS);
  #else
   return G_sim_limits;
  #endif
}

static void send_steps(unsigned long steps)
{
  #ifndef MOTOR_SIM
   outb_p(steps, STEP_CTR_LS);
   outb_p(steps >> 8, STEP_CTR_MI);
   outb_p(steps >> 16, STEP_CTR_MS);
  #else
   G_sim_motor_steps = steps & 0xFFFFFF;
  #endif
}

static unsigned long read_steps(void)
{
  #ifndef MOTOR_SIM
   return inb_p(STEP_CTR_LS) + inb_p( STEP_CTR_MI ) * 256L + inb_p( STEP_CTR_MS ) * 256L * 256L;
  #else
   return G_sim_motor_steps;
  #endif
}

static void send_rate(unsigned long speed)
{
  #ifndef MOTOR_SIM
   outb_p(speed, SLEW_RATE_LS);
   outb_p(speed >> 8, SLEW_RATE_MS);
  #else
   G_sim_speed = speed;
  #endif
}

