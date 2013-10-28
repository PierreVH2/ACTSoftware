#ifndef MOTOR_FUNCS_H
#define MOTOR_FUNCS_H

#include "motor_defs.h"

char start_goto(int targ_ha, int targ_dec, unsigned char use_encod, unsigned int max_speed);
void cancel_goto(void);
char check_goto(unsigned long track_incr_msec);
char get_is_ramp_reqd(void);
char set_track_rate(unsigned long new_rate);
unsigned long get_track_rate(void);
char check_track(void);
void get_encod_coords(int *ha_pulses, int *dec_pulses);
void get_motor_coords(int *ha_steps, int *dec_steps);

#endif

