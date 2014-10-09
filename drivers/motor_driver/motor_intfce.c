#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/workqueue.h>
#include <asm/div64.h>
#include <linux/jiffies.h>
#include <act_plc/act_plc.h>
#include "motor_intfce.h"
#include "motor_defs.h"
#include "motor_driver.h"
#include "soft_limits.h"

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

/// Maximum number of steps that can be passed to motor controller
#define    MOTOR_MAX_STEPS  0xFFFFFF

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
#define TOLERANCE_MOTOR_HA_STEPS    3
#define TOLERANCE_MOTOR_DEC_STEPS   2
/** \} */

/** Slow down if this distance from target coordinates
 * \{ */
#define FLOP_MOTOR_HA_STEPS         2334
#define FLOP_MOTOR_DEC_STEPS        3177
/** \} */

/** Allow the telescope to be initialised if telescope is this close to where the limit switch SHOULD be (or if telescope is not yet initialised)
* \{ */
#define HA_INIT_LIM_STEPS 45020
#define DEC_INIT_LIM_STEPS 22416
/** \} */

/// Period (in milliseconds) for motor monitoring function
#define MON_PERIOD_MSEC             50

/// Hour angle motor steps increment per millisecond (used to adjust target ha during move if tracking)
#define HA_INCR_MOTOR_STEPS         31/1000

/// Number of motor steps in a second of Hour Angle (1000 * MOTOR_STEPS_E_LIM / (MOTOR_LIM_W_MSEC - MOTOR_LIM_E_MSEC))
#define HA_MOTOR_STEPS_PER_SEC      31 

/// Number of motor steps to catch up if tracking during goto / card move
#define ha_track_time(start_time) (((long)jiffies - (long)start_time) * HA_MOTOR_STEPS_PER_SEC / HZ)

struct gotomove_params
{
  int targ_ha, targ_dec;
  unsigned char dir_cur;
  unsigned int rate_req, rate_cur;
  unsigned char cancelled;
  long start_time;
};

struct cardmove_params
{
  unsigned char dir_cur, dir_req;
  unsigned int rate_req, rate_cur;
  unsigned char handset_move;
  int start_ha;
  long start_time;
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
unsigned char check_gotomove(struct gotomove_params *params);
unsigned char check_cardmove(struct cardmove_params *params);
unsigned char check_tracking(struct tracking_params *params);
static char calc_direction(int targ_ha, int targ_dec);
static char calc_direction_goto(int targ_ha, int targ_dec);
static unsigned char get_dir_mask(unsigned char dir_mode);
static unsigned char calc_is_near_target(unsigned char direction, int targ_ha, int targ_dec);
static unsigned int calc_rate(unsigned char dir, unsigned char speed, unsigned char tracking_on);
static unsigned int calc_ramp_rate(unsigned int rate_cur, unsigned int rate_req);
static void start_move(unsigned char dir, unsigned long rate_req);
static void stop_move(void);
static unsigned char check_soft_lims(int steps_ha, int steps_dec);
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

void motordrv_init(void (*stat_update)(void))
{
  G_status = 0;
  G_hard_limits = 0;
  G_alt_limits = 0;
  G_motor_steps_ha = 0;
  G_motor_steps_dec = 0;
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

/// TODO: Change return value when already at target coordinates
int start_goto(struct motor_goto_cmd *cmd)
{
  char dir;
  unsigned int rate;
  struct gotomove_params *params = NULL;
  
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
  dir = calc_direction_goto(cmd->targ_ha, cmd->targ_dec);
  if (dir < 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Target coordinates beyond limit.\n");
    return -EINVAL;
  }
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
  
  start_move(dir, rate);

  // Set global variables appropriately
  params = &G_move_params.gotomove;
  params->targ_ha = cmd->targ_ha;
  params->targ_dec = cmd->targ_dec;
  params->dir_cur = dir;
  params->rate_req = rate;
  params->rate_cur = (rate > RATE_MIN) ? rate : RATE_MIN;
  params->cancelled = FALSE;
  params->start_time = jiffies;
  
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
  struct cardmove_params *params = NULL;
  
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
  
  // If cardinal move is already being done, only change requested direction and rate and let check_cardmove do the rest
  if (G_status & MOTOR_STAT_CARD)
  {
    params = &G_move_params.cardmove;
    params->dir_req = dir;
    params->rate_req = rate;
    return 0;
  }

  start_move(dir, rate);

  params = &G_move_params.cardmove;
  params->dir_req = dir;
  params->dir_cur = params->dir_req;
  params->rate_req = rate;
  if (((dir & DIR_HA_MASK) == 0) && ((G_status & MOTOR_STAT_TRACKING) > 0))
    params->start_ha = G_motor_steps_ha;
  else
    params->start_ha = 0;
  params->rate_cur = (rate > RATE_MIN) ? rate : RATE_MIN;;
  params->handset_move = FALSE;
  params->start_time = jiffies;
  
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
  unsigned char new_stat;
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
    new_stat = G_status | MOTOR_STAT_TRACKING;
  else
    new_stat = G_status & ~(MOTOR_STAT_TRACKING);
  if (G_status & MOTOR_STAT_MOVING)
  {
    G_status = new_stat;
    update_status();
    return;
  }
  if (tracking_on)
  {
    struct tracking_params *params = &G_move_params.tracking;
    start_move(DIR_WEST_MASK, RATE_SID);
    params->adj_ha_steps = params->adj_dec_steps = 0;
    params->dir_cur = 0;
    params->last_steps_ha = G_motor_steps_ha;
    params->last_steps_dec = G_motor_steps_dec;
  }
  else
    stop_move();
  G_status = new_stat;
  update_status();
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

void set_coord_motor(struct motor_tel_coord *coord)
{
  if (G_status & MOTOR_STAT_HA_INIT)
    printk(KERN_INFO PRINTK_PREFIX "Resetting HA motor steps to %d, currently %d.\n", coord->tel_ha, G_motor_steps_ha);
  else
  {
    G_status |= MOTOR_STAT_HA_INIT;
    printk(KERN_INFO PRINTK_PREFIX "Setting HA motor steps to %d and flagging HA initialised.\n", coord->tel_ha);
  }
  G_motor_steps_ha = coord->tel_ha;
  if (G_status & MOTOR_STAT_DEC_INIT)
    printk(KERN_INFO PRINTK_PREFIX "Resetting DEC motor steps to %d, currently %d.\n", coord->tel_dec, G_motor_steps_dec);
  else
  {
    G_status |= MOTOR_STAT_DEC_INIT;
    printk(KERN_INFO PRINTK_PREFIX "Setting HA motor steps to %d and flagging DEC initialised.\n", coord->tel_dec);
  }
  G_motor_steps_dec = coord->tel_dec;
}

void set_init_motor(unsigned char init_stat)
{
  if (init_stat & MOTOR_STAT_HA_INIT)
    G_status |= MOTOR_STAT_HA_INIT;
  else
    G_status &= ~MOTOR_STAT_HA_INIT;
  if (init_stat & MOTOR_STAT_DEC_INIT)
    G_status |= MOTOR_STAT_DEC_INIT;
  else
    G_status &= ~MOTOR_STAT_DEC_INIT;
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
  
  // If handset cardinal move is already being done, only change requested direction and rate and let check_cardmove do the rest
  if (G_status & MOTOR_STAT_CARD)
  {
    params->dir_req = dir;
    params->rate_req = rate;
    return;
  }

  start_move(dir, rate);

  // Otherwise start cardinal move
  params->dir_req = dir;
  params->rate_req = rate;
  if (((dir & DIR_HA_MASK) == 0) && ((G_status & MOTOR_STAT_TRACKING) > 0))
    params->start_ha = G_motor_steps_ha;
  else
    params->start_ha = 0;
  params->dir_cur = dir;
  params->rate_cur = rate > RATE_MIN ? rate : RATE_MIN;
  params->handset_move = TRUE;
  params->start_time = jiffies;
  
  G_status |= MOTOR_STAT_CARD;
  update_status();
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
    dir = G_move_params.tracking.dir_cur | DIR_WEST_MASK;
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

/// NOTE: Additional criteria were introduced for setting the telescope zero points. Before, the zero points were only being set when
/// the corresponding electronic limit switch was triggered. However, this caused some problems due to the Western limit switch
/// being triggered spontaneously. To overcome this, additional criteria were introduced in order for the zero points to be set;
/// now the telescope MUST either be near the limit switch (according to the current coordinate system) or the telescope must
/// not yet be initialised according to G_status.
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
    // Only set zero point if near Southern limit or telescope not initialised
    if ((new_limits & DIR_SOUTH_MASK) &&
        ((G_motor_steps_dec < DEC_INIT_LIM_STEPS) ||
         ((G_status & MOTOR_STAT_DEC_INIT) == 0)))
    {
      printk(KERN_INFO PRINTK_PREFIX "Setting declination zero point at Southern limit (currently at %d steps).\n", G_motor_steps_dec);
      G_motor_steps_dec = 0;
      if ((G_status & MOTOR_STAT_DEC_INIT) == 0)
      {
        G_status |= MOTOR_STAT_DEC_INIT;
        req_status_update = TRUE;
      }
    }
    else if (new_limits & DIR_SOUTH_MASK)
      printk(KERN_INFO PRINTK_PREFIX "Southern limit reached, but not setting zero point (currently at %d steps).\n", G_motor_steps_dec);
    // Only set zero point if near Western limit or telescope not initialised
    if ((new_limits & DIR_WEST_MASK) &&
        ((G_motor_steps_ha < HA_INIT_LIM_STEPS) ||
         ((G_status & MOTOR_STAT_HA_INIT) == 0)))
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Setting hour angle zero point at Western limit (currently at %d steps).\n", G_motor_steps_ha);
      G_motor_steps_ha = 0;
      if ((G_status & MOTOR_STAT_HA_INIT) == 0)
      {
        G_status |= MOTOR_STAT_HA_INIT;
        req_status_update = TRUE;
      }
    }
    else
      printk(KERN_INFO PRINTK_PREFIX "Western limit reached, but not setting zero point (currently at %d steps).\n", G_motor_steps_ha);
    G_hard_limits = new_limits;
  }
  new_limits = check_soft_lims(G_motor_steps_ha, G_motor_steps_dec);
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
    req_status_update = req_status_update || check_gotomove(&G_move_params.gotomove);
  else if (G_status & MOTOR_STAT_CARD)
    req_status_update = req_status_update || check_cardmove(&G_move_params.cardmove);
  else if (G_status & MOTOR_STAT_TRACKING)
    req_status_update = req_status_update || check_tracking(&G_move_params.tracking);
  
  if (req_status_update)
    update_status();
  queue_delayed_work(G_motordrv_workq, &G_motor_work, MON_PERIOD_MSEC * HZ / 1000);
}

unsigned char check_gotomove(struct gotomove_params *params)
{
  unsigned int rate_req, rate_new, new_targ_ha;
  unsigned char dir_new;
  
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
    if (params->rate_cur < RATE_MIN)
    {
      rate_new = calc_ramp_rate(params->rate_cur, RATE_MIN);
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
  
  if ((G_status & MOTOR_STAT_TRACKING) == 0)
    new_targ_ha = params->targ_ha;
  else
  {
    long tmpjif = jiffies, start_time=params->start_time;
    printk(KERN_DEBUG PRINTK_PREFIX "Tracking catch-up %ld steps (%ld %ld %ld %ld %ld %ld %ld)\n", ha_track_time(params->start_time), tmpjif, start_time, tmpjif-start_time, (tmpjif-start_time)*31, (tmpjif-start_time)*31/HZ, (tmpjif-start_time)*(1000/HZ)*MOTOR_STEPS_E_LIM, (tmpjif-start_time)*(1000/HZ)*(MOTOR_STEPS_E_LIM/(MOTOR_LIM_W_MSEC - MOTOR_LIM_E_MSEC)));
    new_targ_ha = params->targ_ha - ha_track_time(params->start_time);
  }
  
  dir_new = calc_direction_goto(new_targ_ha, params->targ_dec);
  if ((dir_new == 0) && (params->rate_cur >= RATE_MIN))
  {
    stop_move();
    G_status &= ~MOTOR_STAT_GOTO;
    if ((G_status & MOTOR_STAT_TRACKING) != 0)
      toggle_tracking(TRUE);
    return TRUE;
  }
  if ((dir_new != params->dir_cur) && (params->rate_cur >= RATE_MIN))
  {
    start_move(dir_new, params->rate_cur);
    params->dir_cur = dir_new;
  }
  
  if (calc_is_near_target(params->dir_cur, new_targ_ha, params->targ_dec))
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

unsigned char check_cardmove(struct cardmove_params *params)
{
  unsigned int rate_req;
  
  // Check limits - either hard limits or soft limits while not under handset control
  if ((G_hard_limits & params->dir_cur) || ((G_alt_limits & params->dir_cur) && (!params->handset_move)))
  {
    printk(KERN_INFO PRINTK_PREFIX "Limit reached in move direction. Cancelling cardinal move.\n");
    stop_move();
    G_status &= ~(MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING);
    return FALSE;
  }

  // Calculate (and set) required rate
  rate_req = params->rate_req;
  if (params->dir_cur != params->dir_req)
    rate_req = RATE_MIN;
  else if (params->rate_cur != params->rate_req)
    rate_req = params->rate_req;
  if (rate_req != params->rate_cur)
  {
    unsigned int rate_new = calc_ramp_rate(params->rate_cur, rate_req);
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
      int new_targ_ha = params->start_ha - ha_track_time(params->start_time);
      dir_new = calc_direction(new_targ_ha, G_motor_steps_dec) & DIR_WEST_MASK;
    }
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
    start_move(params->dir_req, RATE_MIN);
    params->dir_cur = params->dir_req;
  }
  return FALSE;
}

unsigned char check_tracking(struct tracking_params *params)
{
  int ha_steps, dec_steps;
  unsigned char dir_adj, dir_move;
  unsigned int rate;
  
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
  if ((params->dir_cur & DIR_DEC_MASK) != 0)
    params->adj_dec_steps += dec_steps;
  
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
  dir_adj = calc_direction(ha_steps, dec_steps);
  if (dir_adj == params->dir_cur)
    return FALSE;
  if ((dir_adj & DIR_HA_MASK) == 0)
  {
    dir_move = dir_adj | DIR_WEST_MASK;
    rate = RATE_SID;
  }
  else
  {
    dir_move = dir_adj;
    rate = calc_rate(dir_adj, MOTOR_SPEED_GUIDE, TRUE);
  }
  start_move(dir_move, rate);
  params->dir_cur = dir_adj;
  return FALSE;
}

static char calc_direction(int targ_ha, int targ_dec)
{
  unsigned char dir = 0;
  
  if ((targ_dec < 0) || (targ_ha < 0))
    return -1;
  if ((targ_dec > MOTOR_STEPS_N_LIM) || (targ_ha > MOTOR_STEPS_E_LIM))
    return -1;
  dir = check_soft_lims(G_motor_steps_ha, G_motor_steps_dec);
  if (dir != 0)
    return -1;

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
  return dir;
}

static char calc_direction_goto(int targ_ha, int targ_dec)
{
  char dir;
  
  // Check if coordinates valid and we are not already at coordinates
  dir = calc_direction(targ_ha, targ_dec);
  if (dir <= 0)
    return dir;
  // If we only need to move in HA
  if ((dir & (DIR_NORTH_MASK | DIR_SOUTH_MASK)) == 0)
    return dir;
  // If we only need to move in Dec
  if ((dir & (DIR_EAST_MASK | DIR_WEST_MASK)) == 0)
    return dir;
  
  // Which direction should we move in firt (in order to avoid soft limits)?
  // Is Dec first safe?
  if (check_soft_lims(G_motor_steps_ha, targ_dec) == 0)
    return dir & (DIR_NORTH_MASK | DIR_SOUTH_MASK);
  // Is HA first safe?
  if (check_soft_lims(targ_ha, G_motor_steps_dec) == 0)
    return dir & (DIR_EAST_MASK | DIR_WEST_MASK);
  // Neither first is safe - this should never happen
  return -1;
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

static unsigned char calc_is_near_target(unsigned char direction, int targ_ha, int targ_dec)
{
  if ((direction & (DIR_SOUTH_MASK | DIR_NORTH_MASK)) && (abs(targ_dec - G_motor_steps_dec) < FLOP_MOTOR_DEC_STEPS))
    return TRUE;
  if ((direction & (DIR_WEST_MASK | DIR_EAST_MASK)) && (abs(targ_ha - G_motor_steps_ha) < FLOP_MOTOR_HA_STEPS))
    return TRUE;
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
  update_motor_coords(MOTOR_MAX_STEPS);
  send_steps(MOTOR_MAX_STEPS);
  send_rate(rate_req > RATE_MIN ? rate_req : RATE_MIN);
  send_direction(dir);
}

static void stop_move(void)
{
  update_motor_coords(0);
  send_steps(0);
  send_direction(0);
}

static unsigned char check_soft_lims(int steps_ha, int steps_dec)
{
  int idx, lim_W, lim_E;
  unsigned char lim_dir = 0;
  if (steps_dec < TEL_ALT_LIM_MIN_DEC_STEPS)
    return 0;
  if (steps_dec >= MOTOR_STEPS_N_LIM)
    return DIR_NORTH_MASK | DIR_WEST_MASK | DIR_EAST_MASK;
  idx = steps_dec-TEL_ALT_LIM_MIN_DEC_STEPS;
  lim_W  = G_tel_alt_lim_W_steps[idx], lim_E=G_tel_alt_lim_E_steps[idx];
  if (steps_ha < lim_W)
    lim_dir |= DIR_NORTH_MASK | DIR_WEST_MASK;
  if (steps_ha > lim_E)
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

