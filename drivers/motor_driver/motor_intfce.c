#include <linux/kernel.h>
#include <linux/io.h>
#include "motor_intfce.h"
#include "motor_defs.h"

#ifdef MOTOR_SIM
 #define MOTORSIM_PFX  "[MOTOR_SIM] "
#endif

// Ports used by motor controller
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

enum
{
  MOVESTAT_IDLE = 0,
  MOVESTAT_MOVING,
  MOVESTAT_ENDING
};

#ifdef MOTOR_SIM
 unsigned long G_sim_motor_steps, G_sim_speed;
 unsigned char G_sim_limits, G_sim_dir;
#endif

static void send_direction(unsigned char dir);
static unsigned char read_limits(void);
static unsigned char check_soft_limits(void);
static void send_steps(unsigned long steps);
static unsigned long read_steps(void);
static void send_speed(unsigned long speed);
static void update_step_count(void);

// Addtional telescope limits
#define TEL_ALT_LIM_MIN_DEC_STEPS 365634
#define TEL_ALT_LIM_DEC_INC_STEPS 19068
#define TEL_ALT_LIM_NUM_DIVS      11
// tel_alt_limits specifies the maximum allowable hour-angle in minutes for a declination within the range corresponding to {(-10)-(-5), (-5)-0, 0-5, 5-10, 10-15, 15-20, 20-25, 25-30, 30-35, 35-40, 40-45} in degrees
// in order for the telescope to remain above 10 degrees in altitude
// static const int G_tel_alt_lim_dec_steps[TEL_ALT_LIM_NUM_DIVS] = {365634, 384702, 403770, 422838, 441906, 460974, 480042, 499110, 518178, 537246, 556314};
static const int G_tel_alt_lim_W_steps[TEL_ALT_LIM_NUM_DIVS] = {-3520, 19677, 43880, 69600, 97474, 128323, 163334, 204335, 254504, 320569, 424189};
static const int G_tel_alt_lim_E_steps[TEL_ALT_LIM_NUM_DIVS] = {1168912, 1145715, 1121512, 1095792, 1067917, 1037069, 1002058, 961057, 910888, 844823, 741203};

static long int G_motor_steps_ha = 0;
static long int G_motor_steps_dec = 0;
static long int G_last_motor_steps = 0;
static long int G_move_dir = 0;
static unsigned long G_max_speed = 0, G_cur_speed = 0;
static char G_move_ending = 0;

char start_move(unsigned char dir, unsigned long speed)
{
  if (G_cur_speed != 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Cannot start motion because motors currently moving.\n");
    return 0;
  }

  G_cur_speed = speed >= MIN_RATE ? speed : MIN_RATE;
  G_max_speed = speed;
  G_move_dir = dir & DIR_MASK;
  G_move_ending = 0;
  send_steps(~((unsigned long)0));
  send_speed(G_cur_speed);
  send_direction(G_move_dir);
  return 1;
}

void change_speed(unsigned long new_speed)
{
  if (G_cur_speed == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Cannot change rate of motion, as telescope is not moving.\n");
    return;
  }
  
  if (G_cur_speed < MIN_RATE)
    G_max_speed = new_speed;
  else if (new_speed < MIN_RATE)
  {
    send_speed(MIN_RATE);
    G_max_speed = new_speed;
    G_cur_speed = MIN_RATE;
  }
  else
  {
    send_speed(new_speed);
    G_max_speed = new_speed;
    G_cur_speed = new_speed;
  }
  return;
}

char end_move(void)
{
  if (G_cur_speed == 0)
    return 1;
  if (G_cur_speed >= MIN_RATE)
  {
    stop_move();
    return 1;
  }
  if (G_move_ending != 0)
    return 0;
  change_speed(MIN_RATE);
  G_move_ending = 1;
  return 0;
}

void stop_move(void)
{
  update_step_count();
  send_steps(0);
  send_direction(0);
  G_cur_speed = 0;
  G_move_ending = 0;
}

unsigned long get_steps_ha(void)
{
  update_step_count();
  return G_motor_steps_ha;
}

unsigned long get_steps_dec(void)
{
  update_step_count();
  return G_motor_steps_dec;
}

unsigned char get_limits(void)
{
  unsigned char lim = read_limits();
  if (((lim & DIR_WEST_MASK) && (lim & DIR_EAST_MASK)) || ((lim & DIR_NORTH_MASK) && (lim & DIR_SOUTH_MASK)))
  {
    printk(KERN_CRIT PRINTK_PREFIX "East and West and/or North and South limits simultaneously active. Limit states unavailable (check limit switch plug is properly plugged in at limit switch).\n");
    return DIR_NORTH_MASK | DIR_SOUTH_MASK | DIR_EAST_MASK | DIR_WEST_MASK;
  }
  if (lim & DIR_SOUTH_MASK)
    G_motor_steps_dec = 0;
  if (lim & DIR_WEST_MASK)
    G_motor_steps_ha = 0;
  return lim | check_soft_limits();
}

char motor_ramp(void)
{
  if (G_max_speed == G_cur_speed)
    return 0;
  if (G_max_speed < G_cur_speed)
  {
    unsigned long new_speed;
    new_speed = (G_cur_speed)*4/5;
    if (new_speed < G_max_speed)
      new_speed = G_max_speed;
    G_cur_speed = new_speed;
    send_speed(new_speed);
  }
  else if (G_max_speed > G_cur_speed)
  {
    unsigned long new_speed;
    new_speed = G_cur_speed*5/4;
    if (new_speed > G_max_speed)
      new_speed = G_max_speed;
    G_cur_speed = new_speed;
    send_speed(new_speed);
  }
  return 1;
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
   return G_last_motor_steps;
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

static void send_direction(unsigned char dir)
{
  #ifndef MOTOR_SIM
   outb(dir | TRK_OFF_MASK, DIR_CONTROL);
  #else
   G_sim_dir = dir;
  #endif
}

static unsigned char read_limits(void)
{
  #ifndef MOTOR_SIM
   return inb(LIMITS);
  #else
   return G_sim_limits;
  #endif
}

static unsigned char check_soft_lims(void)
{
  unsigned char idx=TEL_ALT_LIM_NUM_DIVS+1, lim_dir = 0;
  int64_t grad_u, zerop, dec_l;
  int tel_ha_lim_E = 0, tel_ha_lim_W = 0;
  
  if (G_motor_steps_dec < TEL_ALT_LIM_MIN_DEC_STEPS)
    return 0;
  idx = (G_motor_steps_dec - TEL_ALT_LIM_MIN_DEC_STEPS) / TEL_ALT_LIM_DEC_INC_STEPS;
  if (idx >= TEL_ALT_LIM_NUM_DIVS-1)
    return DIR_NORTH_MASK | DIR_WEST_MASK | DIR_EAST_MASK;
  dec_l = TEL_ALT_LIM_MIN_DEC_STEPS + idx*TEL_ALT_LIM_DEC_INC_STEPS;
  grad_u = (G_tel_alt_lim_W_steps[idx+1] - G_tel_alt_lim_W_steps[idx]);
  zerop = G_tel_alt_lim_W_steps[idx] - grad_u*dec_l/TEL_ALT_LIM_DEC_INC_STEPS;
  tel_ha_lim_W = grad_u * G_motor_steps_dec / TEL_ALT_LIM_DEC_INC_STEPS + zerop;
  grad_u *= -1;
  zerop = G_tel_alt_lim_E_steps[idx] - grad_u*dec_l/TEL_ALT_LIM_DEC_INC_STEPS;
  tel_ha_lim_E = grad_u * G_motor_steps_dec / TEL_ALT_LIM_DEC_INC_STEPS + zerop;
  if (G_motor_steps_ha < tel_ha_lim_W)
    lim_dir |= DIR_NORTH_MASK | DIR_WEST_MASK;
  if (G_motor_steps_ha > tel_ha_lim_E)
    lim_dir |= DIR_NORTH_MASK | DIR_EAST_MASK;
  return lim_dir;
}

static void send_steps(unsigned long steps)
{
  #ifndef MOTOR_SIM
   outb(steps, STEP_CTR_LS);
   outb(steps >> 8, STEP_CTR_MI);
   outb(steps >> 16, STEP_CTR_MS);
  #else
   G_sim_motor_steps = steps & 0xFFFFFF;
  #endif
  G_last_motor_steps = steps & 0xFFFFFF;
}

static unsigned long read_steps(void)
{
  #ifndef MOTOR_SIM
   return inb(STEP_CTR_LS) + inb( STEP_CTR_MI ) * 256L + inb( STEP_CTR_MS ) * 256L * 256L;
  #else
   return G_sim_motor_steps;
  #endif
}

static void send_speed(unsigned long speed)
{
  #ifndef MOTOR_SIM
   outb(speed, SLEW_RATE_LS);
   outb(speed >> 8, SLEW_RATE_MS);
  #else
   G_sim_speed = speed;
  #endif
}

static void update_step_count(void)
{
  long int new_motor_steps;
  
  if (G_cur_speed == 0)
    return;
  if (G_move_dir == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Direction of motion not available. Cannot update motor coordinates.\n");
    return;
  }
  
  new_motor_steps = read_steps();
  
  if (G_move_dir & DIR_WEST_MASK)
    G_motor_steps_ha += new_motor_steps - G_last_motor_steps;
  else if (G_move_dir & DIR_EAST_MASK)
    G_motor_steps_ha -= new_motor_steps - G_last_motor_steps;
  
  if (G_move_dir & DIR_NORTH_MASK)
    G_motor_steps_dec += new_motor_steps - G_last_motor_steps;
  else if (G_move_dir & DIR_SOUTH_MASK)
    G_motor_steps_dec -= new_motor_steps - G_last_motor_steps;
  
  G_last_motor_steps = new_motor_steps;  
}

