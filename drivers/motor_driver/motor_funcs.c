#include <linux/kernel.h>
#include <act_plc/act_plc.h>
#include "motor_funcs.h"
#include "motor_defs.h"
#include "motor_intfce.h"

#define TRUE  1
#define FALSE 0

#define TOLERANCE_ENCOD_HA_PULSES   4
#define TOLERANCE_ENCOD_DEC_PULSES  3
#define TOLERANCE_MOTOR_HA_STEPS    10
#define TOLERANCE_MOTOR_DEC_STEPS   5

#define FLOP_ENCOD_HA_PULSES        842
#define FLOP_ENCOD_DEC_PULSES       1683
#define FLOP_MOTOR_HA_STEPS         2334
#define FLOP_MOTOR_DEC_STEPS        3177

struct tel_goto_params
{
  int targ_ha;
  int targ_dec;
  unsigned char use_encod;
  unsigned int max_speed;
  unsigned char is_near_target;
  unsigned char direction;
  int track_timer;
  unsigned char goto_cancel;
};

static unsigned char calc_direction(int targ_ha, int targ_dec, unsigned char use_encod);
static char calc_is_near_target(unsigned char direction, int targ_ha, int targ_dec, unsigned char use_encod);
static void update_motor_coord(void);
static void update_encod_coord(void);
static void update_coord(void);

static int G_cur_motor_ha_steps;
static int G_cur_motor_dec_steps;
static int G_cur_encod_ha_pulses;
static int G_cur_encod_dec_pulses;
static char G_ramp_reqd = FALSE;
static struct tel_goto_params G_goto_params;
static unsigned long G_track_rate = 0;

char start_goto(int targ_ha, int targ_dec, unsigned char use_encod, unsigned long max_speed)
{
  unsigned char lims;
  update_coord();
  G_goto_params.targ_ha = targ_ha;
  G_goto_params.targ_dec = targ_dec;
  G_goto_params.use_encod = use_encod;
  G_goto_params.direction = calc_direction(targ_ha, targ_dec, use_encod);
  G_goto_params.is_near_target = calc_is_near_target(G_goto_params.direction, targ_ha, targ_dec, use_encod);
  G_goto_params.max_speed = max_speed;
  G_goto_params.goto_cancel = FALSE;
  G_goto_params.track_timer = 0;
  
  if (G_goto_params.direction == 0)
    return FALSE;

  lims = get_limits();
  if ((lims & G_goto_params.direction) != 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Received goto command. Limit switches in desired direction have been triggered.\n");
    return FALSE;
  }
  if (G_goto_params.max_speed < SLEW_RATE)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Attempting slew at unsafe speed. Will slew at SLEW_RATE\n");
    G_goto_params.max_speed = SLEW_RATE;
  }
  if ((G_goto_params.is_near_target) && (G_goto_params.max_speed < MIN_RATE))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Already near target, not ramping up.");
    G_ramp_reqd = FALSE;
    return start_move(G_goto_params.direction, MIN_RATE);
  }
  G_ramp_reqd = G_goto_params.max_speed < MIN_RATE;
  return start_move(G_goto_params.direction, G_goto_params.max_speed);
}

void cancel_goto(void)
{
  printk(KERN_DEBUG PRINTK_PREFIX "Goto cancel requested\n");
  if (G_goto_params.max_speed < MIN_RATE)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Ramp required.\n");
    G_ramp_reqd = TRUE;
    change_speed(MIN_RATE);
  }
  G_goto_params.goto_cancel = TRUE;
  G_goto_params.is_near_target = TRUE;
  return FALSE;
}

char check_goto(unsigned long track_incr_msec)
{
  unsigned char new_dir;

  update_coord();
  if (get_limits() & G_goto_params.direction)
  {
    printk(KERN_INFO PRINTK_PREFIX "Limit switch triggered while performing goto (%d). Stopping.\n", get_limits());
    stop_move();
    return -1;
  }
  
  if (G_goto_params.goto_cancel)
  {
    char move_ended = end_move();
    printk(KERN_DEBUG PRINTK_PREFIX "Goto cancelling: %hd %lu\n", move_ended, G_track_rate);
    if (!move_ended)
    {
      printk(KERN_INFO PRINTK_PREFIX "Move not completed yet.\n");
      return FALSE;
    }
    printk(KERN_INFO PRINTK_PREFIX "Move complete.\n");
    G_goto_params.direction = 0;
    stop_move();
    return TRUE;
  }
  
  if ((G_track_rate > 0) && (G_goto_params.track_timer < 0))
  {
    int track_adj;
    G_goto_params.track_timer -= track_incr_msec;
    track_adj = G_goto_params.track_timer / (G_goto_params.use_encod ? MOTOR_SID_PER_ENCOD_PULSE : MOTOR_SID_PER_MOTOR_STEPS);
    new_dir = calc_direction(G_goto_params.targ_ha - track_adj, G_goto_params.targ_dec, G_goto_params.use_encod);
    printk(KERN_DEBUG PRINTK_PREFIX "Trying to catch up with tracking (%d %s to go, direction %hhu).\n", track_adj, G_goto_params.use_encod ? "pulses" : "steps", new_dir);
    if (new_dir != DIR_WEST_MASK)
    {
      printk(KERN_INFO PRINTK_PREFIX "Target reached, tracking on (HA: %d %d; Dec: %d %d).\n", G_goto_params.targ_ha - track_adj, G_goto_params.use_encod ? G_encod_ha_pulses : G_motor_ha_steps, G_goto_params.targ_dec, G_goto_params.use_encod ? G_encod_dec_pulses : G_motor_dec_steps);
      G_goto_params.direction = 0;
      stop_move();
      return TRUE;
    }
    return FALSE;
  }
  
  if (G_track_rate > 0)
    G_goto_params.track_timer += track_incr_msec;
  
  if ((calc_is_near_target(G_goto_params.direction, G_goto_params.targ_ha, G_goto_params.targ_dec, G_goto_params.use_encod)) && (G_goto_params.is_near_target == 0))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Near target\n");
    G_goto_params.is_near_target = TRUE;
    change_speed(MIN_RATE);
    G_ramp_reqd = TRUE;
  }
    
  new_dir = calc_direction(G_goto_params.targ_ha_msec, G_goto_params.targ_dec_asec);
  if (new_dir == G_goto_params.direction)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "No new direction\n");
    return FALSE;
  }
    
  if (new_dir != 0)
  {
    int ret;
    printk(KERN_DEBUG PRINTK_PREFIX "Need to move in remaining direction\n");
    stop_move();
    G_goto_params.direction = new_dir;
    G_goto_params.is_near_target = calc_is_near_target(G_goto_params.direction, G_goto_params.targ_ha, G_goto_params.targ_dec, G_goto_params.use_encod);
    if (!G_goto_params.is_near_target)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Need to ramp up for remaining direction\n");
      G_ramp_reqd = TRUE;
      ret = start_move(G_goto_params.direction, G_goto_params.max_speed);
    }
    else
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Moving in remaining direction at minimum speed.\n");
      ret = start_move(G_goto_params.direction, MIN_RATE);
    }
    if (!ret)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Failed to start telescope motion.\n");
      return -1;
    }
    printk(KERN_DEBUG PRINTK_PREFIX "Started moving in remaining direction\n");
    return FALSE;
  }
    
  stop_move();
  if (G_track_rate == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Target reached, tracking off (HA: %d %d; Dec: %d %d).\n", G_goto_params.targ_ha, G_goto_params.use_encod ? G_encod_ha_pulses : G_motor_ha_steps, G_goto_params.targ_dec, G_goto_params.use_encod ? G_encod_dec_pulses : G_motor_dec_steps);
    G_goto_params.direction = 0;
    return TRUE;
  }
  
  G_goto_params.track_timer *= -1;
  if (!start_move(DIR_WEST_MASK, SET_RATE))
  {
    printk(KERN_INFO PRINTK_PREFIX "Failed to start telescope motion.\n");
    return -1;
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Move/Goto complete - accounting for tracking loss\n");
  G_ramp_reqd = TRUE;
  return FALSE;
}

char get_is_ramp_reqd(void)
{
  if (G_ramp_reqd != 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Ramp required.\n");
    G_ramp_reqd = FALSE;
    return TRUE;
  }
  return FALSE;
}

char set_track_rate(unsigned long new_rate)
{
  int ret = TRUE;
  printk(KERN_DEBUG PRINTK_PREFIX "set_track_rate %lu %lu\n", new_rate, G_track_rate);
  if (new_rate == G_track_rate)
    return TRUE;

  if (G_goto_params.direction != 0)
  {
    G_track_rate = new_rate;
    return TRUE;
  }
  
  if (new_rate == 0)
  {
    stop_move();
    ret = TRUE;
  }
  else if (G_track_rate > 0)
    change_speed(new_rate);
  else
    ret = start_move(DIR_WEST_MASK, new_rate);
  if (!ret)
  {
    printk(KERN_INFO PRINTK_PREFIX "Failed to start/stop/change tracking.\n");
    return -1;
  }
  G_track_rate = new_rate;
  return TRUE;
}

unsigned long get_track_rate(void)
{
  return G_track_rate;
}

char check_track(void)
{
  if (G_track_rate == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "check_track called, but should not be tracking. Ignoring\n");
    return FALSE;
  }
  if (G_goto_params.direction != 0)
    return FALSE;
  
  update_coord();
  if (get_limits() & DIR_WEST_MASK)
  {
    printk(KERN_INFO PRINTK_PREFIX "Western limit switch triggered while tracking. Stopping.\n");
    stop_move();
    return -1;
  }
  return TRUE;
}

void get_motor_coords(int *ha_steps, int *dec_steps)
{
  update_motor_coord();
  *ha_steps = G_motor_ha_steps;
  *dec_asec = G_motor_dec_steps;
}

void get_encod_coords(int *ha_pulses, int *dec_pulses)
{
  update_motor_coord();
  *ha_pulses = G_encod_ha_pulses;
  *dec_pulses = G_encod_dec_pulses;
}

static unsigned char calc_direction(int targ_ha, int targ_dec, unsigned char use_encod)
{
  unsigned char dir = 0;
  if (use_encod)
  {
    if (abs(targ_dec-G_cur_encod_dec_pulses) > TOLERANCE_ENCOD_DEC_PULSES)
    {
      if (targ_dec > G_cur_encod_dec_pulses)
        dir |= DIR_SOUTH_MASK;
      else
        dir |= DIR_NORTH_MASK;
    }
    if (abs(targ_ha-G_cur_encod_ha_pulses) > TOLERANCE_ENCOD_HA_PULSES)
    {
      if (targ_ha > G_cur_encod_ha_pulses)
        dir |= DIR_EAST_MASK;
      else
        dir |= DIR_WEST_MASK;
    }
  }
  else
  {
    if (abs(targ_dec-G_cur_motor_dec_steps) > TOLERANCE_MOTOR_DEC_STEPS)
    {
      if (targ_dec > G_cur_motor_dec_steps)
        dir |= DIR_SOUTH_MASK;
      else
        dir |= DIR_NORTH_MASK;
    }
    if (abs(targ_ha-G_cur_motor_ha_steps) > TOLERANCE_MOTOR_HA_STEPS)
    {
      if (targ_ha > G_cur_motor_ha_steps)
        dir |= DIR_EAST_MASK;
      else
        dir |= DIR_WEST_MASK;
    }
  }
  return dir;
}

static char calc_is_near_target(unsigned char direction, int targ_ha, int targ_dec, unsigned char use_encod)
{
  if (use_encod)
  {
    if ((direction & (DIR_SOUTH_MASK | DIR_NORTH_MASK)) && (abs(targ_dec - G_cur_encod_dec_pulses) < FLOP_ENCOD_DEC_PULSES))
      return TRUE;
    if ((direction & (DIR_WEST_MASK | DIR_EAST_MASK)) && (abs(targ_ha - G_cur_encod_ha_pulses) < FLOP_ENCOD_HA_PULSES))
      return TRUE;
  }
  else
  {
    if ((direction & (DIR_SOUTH_MASK | DIR_NORTH_MASK)) && (abs(targ_dec - G_cur_motor_dec_steps) < FLOP_MOTOR_DEC_STEPS))
      return TRUE;
    if ((direction & (DIR_WEST_MASK | DIR_EAST_MASK)) && (abs(targ_ha - G_cur_motor_ha_steps) < FLOP_MOTOR_HA_STEPS))
      return TRUE;
  }
  return FALSE;
}

static void update_motor_coord(void)
{
  G_motor_ha_steps = get_steps_ha();
  G_motor_dec_steps = get_steps_dec();
}

static void update_encod_coord(void)
{
  G_encod_ha_pulses = get_enc_ha_pulses();
  G_encod_dec_pulses = get_enc_dec_pulses();
}

static void update_coord(void)
{
  update_motor_coord();
  update_encod_coord();
}
