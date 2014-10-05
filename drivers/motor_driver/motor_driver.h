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

enum
{
  MOTOR_DIR_NORTH = 1,
  MOTOR_DIR_NORTHWEST,
  MOTOR_DIR_WEST,
  MOTOR_DIR_SOUTHWEST,
  MOTOR_DIR_SOUTH,
  MOTOR_DIR_SOUTHEAST,
  MOTOR_DIR_EAST,
  MOTOR_DIR_NORTHEAST,
  MOTOR_DIR_INVAL
};

struct motor_tel_coord
{
  int tel_ha, tel_dec;
};

struct motor_goto_cmd
{
  int targ_ha, targ_dec;
  unsigned char speed, tracking_on;
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
#define IOCTL_MOTOR_GET_MOTOR_POS _IOR(MOTOR_IOCTL_NUM, 0, void *)

/// IOCTL to start or end a telescope goto - takes pointer to motor_goto struct as ioctl parameter, if NULL end goto.
#define IOCTL_MOTOR_GOTO _IOW(MOTOR_IOCTL_NUM, 2, void *)

/// IOCTL to start or end movement in cardinal direction (NSEW) - takes pointer to motor_card_cmd struct as ioctl parameter, if NULL end movement.
#define IOCTL_MOTOR_CARD _IOW(MOTOR_IOCTL_NUM, 3, void*)

/// IOCTL to set telescope tracking - 0 disables, everything else enables.
#define IOCTL_MOTOR_SET_TRACKING _IOW(MOTOR_IOCTL_NUM, 4, unsigned char)

/// IOCTL to adjust telescope tracking - takes pointer to motor_track_adj struct as ioctl parameter, if NULL set adjustment to zero.
#define IOCTL_MOTOR_TRACKING_ADJ _IOW(MOTOR_IOCTL_NUM, 5, void *)

/// IOCTL to read the status of the electronic limit switches
#define IOCTL_MOTOR_GET_LIMITS _IOR(MOTOR_IOCTL_NUM, 6, unsigned char *)

/// IOCTL to do an emergency stop - set 1 for an emergency stop, set to 0 to enable motion again
#define IOCTL_MOTOR_EMERGENCY_STOP _IOW(MOTOR_IOCTL_NUM, 7, unsigned char)

/** \brief Additional IOCTLs for simulating the motors - these IOCTLs get and set values that would normally be written to/read from the motor controller
 * \{
 */
#ifdef MOTOR_SIM
/// IOCTL to set the number of motor steps
#define IOCTL_MOTOR_SET_SIM_STEPS _IOW(MOTOR_IOCTL_NUM, 8, unsigned long)

/// IOCTL to set the eletronic limit switch flags
#define IOCTL_MOTOR_SET_SIM_LIMITS _IOW(MOTOR_IOCTL_NUM, 9, unsigned char)

/// IOCTL to get the number of steps (as requested by the driver) that the "motors" should move by
#define IOCTL_MOTOR_GET_SIM_STEPS _IOR(MOTOR_IOCTL_NUM, 10, unsigned long)

/// IOCTL to get the direction in which the "motors" should move
#define IOCTL_MOTOR_GET_SIM_DIR _IOR(MOTOR_IOCTL_NUM, 11, unsigned char)

/// IOCTL to get the speed with which the "motors" should move
#define IOCTL_MOTOR_GET_SIM_RATE _IOR(MOTOR_IOCTL_NUM, 12,unsigned char)
#endif
/** \} */

/** \brief Additional IOCTL - FOR TESTING PURPOSES ONLY
 * \{
 */
/// IOCTL to SET the telescope's current position according to telescope motors - parameter must be a struct motor_tel_coord containing HA, DEC motor steps
/// NOTE: This feature can be dangerous and may only be used if the user is ABSOLUTELY certain what position the telescope is pointing in
#define IOCTL_MOTOR_SET_MOTOR_POS _IOW(MOTOR_IOCTL_NUM, 13, void *)

/** \} */

#endif //MOTOR_DRIVER_H
