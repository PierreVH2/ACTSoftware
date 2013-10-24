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
static void send_steps(unsigned long steps);
static unsigned long read_steps(void);
static void send_speed(unsigned long speed);
static void update_step_count(void);

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

void zero_ha_steps(void)
{
  G_motor_steps_ha = 0;
}

void zero_dec_steps(void)
{
  G_motor_steps_dec = 0;
}

unsigned char get_limits(void)
{
  return read_limits();
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

