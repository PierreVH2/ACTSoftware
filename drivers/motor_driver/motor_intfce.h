#ifndef MOTOR_INTFCE_H
#define MOTOR_INTFCE_H

#include "motor_defs.h"
#include "motor_driver.h"

#define MIN_RATE GUIDE_RATE

void motordrv_init(void (*stat_update)(void));
void motordrv_finalise(void);
unsigned char get_motor_stat(void);
unsigned char get_motor_limits(void);
int start_goto(struct motor_goto_cmd *cmd);
void end_goto(void);
int start_card(struct motor_card_cmd *cmd);
void end_card(void);
void end_all(void);
void toggle_all_stop(unsigned char stop_on);
void toggle_tracking(unsigned char tracking_on);
void adjust_tracking(int adj_ha, int adj_dec);
void get_coord_motor(struct motor_tel_coord *coord);
void get_coord_encod(struct motor_tel_coord *coord);
void handset_handler(unsigned char old_hs, unsigned char new_hs);
#ifdef MOTOR_SIM
void set_sim_steps(unsigned long steps);
void set_sim_limits(unsigned char limits);
unsigned long get_sim_steps(void);
unsigned char get_sim_dir(void);
unsigned long get_sim_speed(void);
#endif
void set_motor_steps_ha(unsigned long new_steps);
void set_motor_steps_dec(unsigned long new_steps);

#endif
