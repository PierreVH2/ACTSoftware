#ifndef MOTOR_INTFCE_H
#define MOTOR_INTFCE_H

#include "motor_defs.h"

#define MIN_RATE GUIDE_RATE

char start_move(unsigned char dir, unsigned long speed);
void change_speed(unsigned long new_speed);
char end_move(void);
void stop_move(void);
unsigned long get_steps_ha(void);
unsigned long get_steps_dec(void);
unsigned char get_limits(void);
char motor_ramp(void);
#ifdef MOTOR_SIM
 void set_sim_steps(unsigned long steps);
 void set_sim_limits(unsigned char limits);
 unsigned long get_sim_steps(void);
 unsigned char get_sim_dir(void);
 unsigned long get_sim_speed(void);
#endif

#endif
