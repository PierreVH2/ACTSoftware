#ifndef MOTOR_DRIVER_H
#define MOTOR_DRIVER_H

#include <linux/ioctl.h>
#include "motor_defs.h"

#define MOTOR_IOCTL_NUM          0xE2
#define MOTOR_DEVICE_NAME        "act_motors"

#define MOTOR_STAT_UPDATE       0x01
#define MOTOR_STAT_INITIALISED  0x02
#define MOTOR_STAT_INITIALISING 0x04
#define MOTOR_STAT_TRACKING     0x08
#define MOTOR_STAT_GOING        0x10
#define MOTOR_STAT_GOTO_CANCEL  0x20
#define MOTOR_STAT_ERR_LIMS     0x40
#define MOTOR_STAT_ALLSTOP      0x80

#define MOTOR_LIM_W(limits) ((limits & 0x01) > 0)
#define MOTOR_LIM_E(limits) ((limits & 0x02) > 0)
#define MOTOR_LIM_N(limits) ((limits & 0x04) > 0)
#define MOTOR_LIM_S(limits) ((limits & 0x08) > 0)

struct tel_coord
{
  char ha_hrs, ha_min, ha_sec;
  short ha_msec;
  char dec_deg, dec_amin, dec_asec;
};

struct tel_goto_cmd
{
  char ha_hrs, ha_min, ha_sec;
  short ha_msec;
  char dec_deg, dec_amin, dec_asec;
  unsigned long track_rate;
  unsigned long max_speed;
};

struct tel_limits
{
  char E_ha_hrs, E_ha_min, E_ha_sec;
  short E_ha_msec;
  char W_ha_hrs, W_ha_min, W_ha_sec;
  short W_ha_msec;
  char N_dec_deg, N_dec_amin, N_dec_asec;
  char S_dec_deg, S_dec_amin, S_dec_asec;
};

/// IOCTL to get the telescope's current position - saves struct tel_coord to ioctl parameter.
#define IOCTL_GET_TEL_POS _IOR(MOTOR_IOCTL_NUM, 0, unsigned long*)

/// IOCTL to start telescope initialisation
#define IOCTL_START_INIT _IOW(MOTOR_IOCTL_NUM, 1, unsigned long*)

/// IOCTL to start a telescope goto - receives struct tel_goto as ioctl parameter, cancels command if ioctl parameter is NULL.
#define IOCTL_TEL_GOTO _IOW(MOTOR_IOCTL_NUM, 2, unsigned long*)

/// IOCTL to set telescope tracking - 0 means disable tracking, everything else means track at the specified speed (usually you want SID_RATE).
#define IOCTL_TEL_SET_TRACKING _IOW(MOTOR_IOCTL_NUM, 3, unsigned long*)

/// IOCTL to read the status of the electronic limit switches
#define IOCTL_GET_LIMITS _IOR(MOTOR_IOCTL_NUM, 4, unsigned long*)

/// IOCTL to do an emergency stop - set 1 for an emergency stop, set to 0 to enable motion again
#define IOCTL_TEL_EMERGENCY_STOP _IOW(MOTOR_IOCTL_NUM, 5, unsigned long*)

/// IOCTL to adjust pointing - receives struct tel_coord, which indicates the actual hour-angle and declination the telescope is pointing at
#define IOCTL_TEL_ADJ_POINTING _IOW(MOTOR_IOCTL_NUM, 6, unsigned long*)

/** \brief Additional IOCTLs useful for diagnosing pointing issues/encoder slips.
 * \{
 */
#ifdef ENCODER_DIAG
/// IOCTL to get the current position according to the motors - saves struct tel_coord to ioctl parameter
#define IOCTL_GET_MOTOR_POS _IOR(MOTOR_IOCTL_NUM, 7, unsigned long*)

/// IOCTL to get the current position according to the encoders - saves struct tel_coord to ioctl parameter
#define IOCTL_GET_ENCODER_POS _IOR(MOTOR_IOCTL_NUM, 8, unsigned long*)
#endif
/** \} */

/** \brief Additional IOCTLs for simulating the motors - these IOCTLs get and set values that would normally be written to/read from the motor controller
 * \{
 */
#ifdef MOTOR_SIM
/// IOCTL to set the number of motor/encoder steps
#define IOCTL_SET_SIM_STEPS _IOW(MOTOR_IOCTL_NUM, 9, unsigned long*)

/// IOCTL to set the eletronic limit switch flags
#define IOCTL_SET_SIM_LIMITS _IOW(MOTOR_IOCTL_NUM, 10, unsigned long*)

/// IOCTL to get the number of steps (as requested by the driver) that the "motors" should move by
#define IOCTL_GET_SIM_STEPS _IOR(MOTOR_IOCTL_NUM, 11, unsigned long*)

/// IOCTL to get the direction in which the "motors" should move
#define IOCTL_GET_SIM_DIR _IOR(MOTOR_IOCTL_NUM, 12, unsigned long*)

/// IOCTL to get the speed with which the "motors" should move
#define IOCTL_GET_SIM_SPEED _IOR(MOTOR_IOCTL_NUM, 13, unsigned long*)
#endif
/** \} */

#endif //MOTOR_DRIVER_H
