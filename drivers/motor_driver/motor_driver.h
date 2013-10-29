#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <linux/ioctl.h>
#include "motor_defs.h"

#define MOTOR_IOCTL_NUM          0xE2
#define MOTOR_DEVICE_NAME        "act_motors"

#define MOTOR_STAT_UPDATE       0x01
#define MOTOR_STAT_HA_INIT      0x02
#define MOTOR_STAT_DEC_INIT     0x04
#define MOTOR_STAT_TRACKING     0x08
#define MOTOR_STAT_GOTO         0x10
#define MOTOR_STAT_CARD         0x20
#define MOTOR_STAT_MOVING       (MOTOR_STAT_GOTO | MOTOR_STAT_CARD)
#define MOTOR_STAT_ERR_LIMS     0x40
#define MOTOR_STAT_ALLSTOP      0x80

#define MOTOR_LIM_W(limits) ((limits & 0x01) > 0)
#define MOTOR_LIM_E(limits) ((limits & 0x02) > 0)
#define MOTOR_LIM_N(limits) ((limits & 0x04) > 0)
#define MOTOR_LIM_S(limits) ((limits & 0x08) > 0)

enum
{
  MOTOR_SPEED_GUIDE = 1,
  MOTOR_SPEED_SET,
  MOTOR_SPEED_SLEW
};

struct motor_tel_coord
{
  int tel_ha, tel_dec;
};

struct motor_goto_cmd
{
  int targ_ha, targ_dec;
  unsigned char speed, tracking_on, use_encod;
};

struct motor_card_cmd
{
  unsigned char dir;
  unsigned char speed;
};

struct motor_track_adj
{
  int adj_ha_steps, adj_dec_steps;
};

/// IOCTL to get the telescope's current position according to telescope motors - saves struct motor_tel_coord to ioctl parameter.
#define IOCTL_MOTOR_GET_MOTOR_POS _IOR(MOTOR_IOCTL_NUM, 0, unsigned long*)

/// IOCTL to get the telescope's current position according to telescope encoders - saves struct motor_tel_coord to ioctl parameter.
#define IOCTL_MOTOR_GET_ENCOD_POS _IOR(MOTOR_IOCTL_NUM, 1, unsigned long*)

/// IOCTL to start or end a telescope goto - takes pointer to motor_goto struct as ioctl parameter, if NULL end goto.
#define IOCTL_MOTOR_GOTO _IOW(MOTOR_IOCTL_NUM, 2, unsigned long*)

/// IOCTL to start or end movement in cardinal direction (NSEW) - takes pointer to motor_card_cmd struct as ioctl parameter, if NULL end movement.
#define IOCTL_MOTOR_CARD _IOW(MOTOR_IOCTL_NUM, 3, unsigned long*)

/// IOCTL to set telescope tracking - 0 disables, everything else enables.
#define IOCTL_MOTOR_SET_TRACKING _IOW(MOTOR_IOCTL_NUM, 4, unsigned long*)

/// IOCTL to adjust telescope tracking - takes pointer to motor_track_adj struct as ioctl parameter, if NULL set adjustment to zero.
#define IOCTL_MOTOR_TRACKING_ADJ _IOW(MOTOR_IOCTL_NUM, 5, unsigned long*)

/// IOCTL to read the status of the electronic limit switches
#define IOCTL_MOTOR_GET_LIMITS _IOR(MOTOR_IOCTL_NUM, 6, unsigned long*)

/// IOCTL to do an emergency stop - set 1 for an emergency stop, set to 0 to enable motion again
#define IOCTL_MOTOR_EMERGENCY_STOP _IOW(MOTOR_IOCTL_NUM, 7, unsigned long*)

/** \brief Additional IOCTLs for simulating the motors - these IOCTLs get and set values that would normally be written to/read from the motor controller
 * \{
 */
#ifdef MOTOR_SIM
/// IOCTL to set the number of motor/encoder steps
#define IOCTL_MOTOR_SET_SIM_STEPS _IOW(MOTOR_IOCTL_NUM, 8, unsigned long*)

/// IOCTL to set the eletronic limit switch flags
#define IOCTL_MOTOR_SET_SIM_LIMITS _IOW(MOTOR_IOCTL_NUM, 9, unsigned long*)

/// IOCTL to get the number of steps (as requested by the driver) that the "motors" should move by
#define IOCTL_MOTOR_GET_SIM_STEPS _IOR(MOTOR_IOCTL_NUM, 10, unsigned long*)

/// IOCTL to get the direction in which the "motors" should move
#define IOCTL_MOTOR_GET_SIM_DIR _IOR(MOTOR_IOCTL_NUM, 11, unsigned long*)

/// IOCTL to get the speed with which the "motors" should move
#define IOCTL_MOTOR_GET_SIM_RATE _IOR(MOTOR_IOCTL_NUM, 12, unsigned long*)
#endif
/** \} */

#endif //MOTOR_DRIVER_H
