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
#define MOTOR_STAT_MOVING       0x10
#define MOTOR_STAT_ERR_LIMS     0x40
#define MOTOR_STAT_ALLSTOP      0x80

#define MOTOR_LIM_W(limits) ((limits & 0x01) > 0)
#define MOTOR_LIM_E(limits) ((limits & 0x02) > 0)
#define MOTOR_LIM_N(limits) ((limits & 0x04) > 0)
#define MOTOR_LIM_S(limits) ((limits & 0x08) > 0)

struct tel_coord
{
  int ha_steps, dec_steps;
};

struct tel_goto_cmd
{
  int targ_ha, targ_dec;
  unsigned char use_encod;
  unsigned int track_rate;
  unsigned int max_speed;
  
};

/// IOCTL to get the telescope's current position according to telescope motors - saves struct tel_coord to ioctl parameter.
#define IOCTL_MOTOR_GET_MOTOR_POS _IOR(MOTOR_IOCTL_NUM, 0, unsigned long*)

/// IOCTL to get the telescope's current position according to telescope encoders - saves struct tel_coord to ioctl parameter.
#define IOCTL_MOTOR_GET_ENCOD_POS _IOR(MOTOR_IOCTL_NUM, 1, unsigned long*)

/// IOCTL to start a telescope goto - receives struct tel_goto as ioctl parameter, cancels command if ioctl parameter is NULL.
#define IOCTL_MOTOR_GOTO _IOW(MOTOR_IOCTL_NUM, 2, unsigned long*)

/// IOCTL to set telescope tracking - 0 means disable tracking, everything else means track at the specified speed (usually you want SID_RATE).
#define IOCTL_MOTOR_SET_TRACKING _IOW(MOTOR_IOCTL_NUM, 3, unsigned long*)

/// IOCTL to read the status of the electronic limit switches
#define IOCTL_MOTOR_GET_LIMITS _IOR(MOTOR_IOCTL_NUM, 4, unsigned long*)

/// IOCTL to do an emergency stop - set 1 for an emergency stop, set to 0 to enable motion again
#define IOCTL_MOTOR_EMERGENCY_STOP _IOW(MOTOR_IOCTL_NUM, 5, unsigned long*)

/** \brief Additional IOCTLs for simulating the motors - these IOCTLs get and set values that would normally be written to/read from the motor controller
 * \{
 */
#ifdef MOTOR_SIM
/// IOCTL to set the number of motor/encoder steps
#define IOCTL_MOTOR_SET_SIM_STEPS _IOW(MOTOR_IOCTL_NUM, 6, unsigned long*)

/// IOCTL to set the eletronic limit switch flags
#define IOCTL_MOTOR_SET_SIM_LIMITS _IOW(MOTOR_IOCTL_NUM, 7, unsigned long*)

/// IOCTL to get the number of steps (as requested by the driver) that the "motors" should move by
#define IOCTL_MOTOR_GET_SIM_STEPS _IOR(MOTOR_IOCTL_NUM, 8, unsigned long*)

/// IOCTL to get the direction in which the "motors" should move
#define IOCTL_MOTOR_GET_SIM_DIR _IOR(MOTOR_IOCTL_NUM, 9, unsigned long*)

/// IOCTL to get the speed with which the "motors" should move
#define IOCTL_MOTOR_GET_SIM_SPEED _IOR(MOTOR_IOCTL_NUM, 10, unsigned long*)
#endif
/** \} */

#endif //MOTOR_DRIVER_H
