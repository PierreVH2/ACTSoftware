#ifndef MOTOR_DEFS_H
#define MOTOR_DEFS_H

// #define MOTOR_SIM

#define PRINTK_PREFIX "[ACT_MOTOR] "

#ifndef TRUE
#define TRUE    1
#endif
#ifndef FALSE
#define FALSE   0
#endif

#define MOTOR_LIM_W_MSEC 18726882
#define MOTOR_LIM_E_MSEC -18424269
#define MOTOR_LIM_N_ASEC 150368
#define MOTOR_LIM_S_ASEC -381161

#define MOTOR_STEPS_E_LIM  1155976
#define MOTOR_STEPS_N_LIM  563056
// #define MOTOR_ENCOD_E_LIM  417151
// #define MOTOR_ENCOD_N_LIM  298207

// #define MOTOR_SID_PER_MOTOR_STEP     32
// #define MOTOR_SID_PER_ENCOD_PULSE    89

#endif
