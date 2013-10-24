#ifndef MOTOR_FUNCS_H
#define MOTOR_FUNCS_H

#include "motor_defs.h"

char start_goto(long int targ_ha_msec, long int targ_dec_asec, unsigned long max_speed);
char end_goto(void);
char check_goto(unsigned long track_incr_msec);
char start_init(long int lim_W_msec, long int lim_E_msec, long int lim_N_asec, long int lim_S_asec);
char check_init(void);
char get_is_ramp_reqd(void);
char set_track_rate(unsigned long new_rate);
unsigned long get_track_rate(void);
char check_track(void);
void get_coords(long int *ha_msec, long int *dec_asec);
#if defined(ENCODER_DIAG)
 void get_encod_coords(long int *ha_msec, long int *dec_asec);
 void get_motor_coords(long int *ha_msec, long int *dec_asec);
#endif
void adj_pointing(long int new_ha_msec, long int new_dec_asec);

#endif

