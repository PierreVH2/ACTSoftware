#ifndef PLC_DRIVER_H
#define PLC_DRIVER_H

#include <linux/ioctl.h>

#define PLC_IOCTL_NUM          0xE1
#define PLC_DEVICE_NAME        "act_plc"

#define PLC_COMM_OK    0x1
#define NEW_STAT_AVAIL 0x2

#define DSHUTT_OPEN_MASK     0x1
#define DSHUTT_CLOSED_MASK   0x2
#define DSHUTT_MOVING_MASK   0x4
#define FILTAPER_INIT_MASK   0x1
#define FILTAPER_CENT_MASK   0x2
#define FILTAPER_MOVING_MASK 0x4
#define ACQMIR_VIEW_MASK     0x1
#define ACQMIR_MEAS_MASK     0x2
#define ACQMIR_MOVING_MASK   0x4
#define FOCUS_SLOT_MASK      0x01
#define FOCUS_REF_MASK       0x02
#define FOCUS_OUT_MASK       0x04
#define FOCUS_IN_MASK        0x08
#define FOCUS_MOVING_MASK    0x10
#define FOCUS_INIT_MASK      0x20
#define FOCUS_STALL_MASK     0x40
#define EHT_MAN_OFF_MASK     0x1
#define EHT_LOW_MASK         0x2
#define EHT_HIGH_MASK        0x4
#define HS_DIR_SOUTH_MASK    0x01
#define HS_DIR_NORTH_MASK    0x02
#define HS_DIR_EAST_MASK     0x04
#define HS_DIR_WEST_MASK     0x08
#define HS_FOC_IN_MASK       0x10
#define HS_FOC_OUT_MASK      0x20
#define HS_SPEED_SLEW_MASK   0x40
#define HS_SPEED_GUIDE_MASK  0x80

#define DOME_POS_DEG(status) (status->dome_pos / 10.0)
#define SHUTTER_OPEN(status) ((status->shutter & 0x1) != 0)
#define SHUTTER_CLOSED(status) ((status->shutter & 0x2) != 0)
#define SHUTTER_MOVING(status) ((status->shutter & 0x4) != 0)
#define DROPOUT_OPEN(status) ((status->dropout & 0x1) != 0)
#define DROPOUT_CLOSED(status) ((status->dropout & 0x2) != 0)
#define DROPOUT_MOVING(status) ((status->dropout & 0x4) != 0)
#define APER_INIT(status) ((status->aper_stat & 0x1) != 0)
#define APER_CENT(status) ((status->aper_stat & 0x2) != 0)
#define APER_MOVING(status) ((status->aper_stat & 0x4) != 0)
#define FILT_INIT(status) ((status->filt_stat & 0x1) != 0)
#define FILT_CENT(status) ((status->filt_stat & 0x2) != 0)
#define FILT_MOVING(status) ((status->filt_stat & 0x4) != 0)
#define ACQMIR_VIEW(status) ((status->acqmir & 0x1) != 0)
#define ACQMIR_MEAS(status) ((status->acqmir & 0x2) != 0)
#define ACQMIR_MOVING(status) ((status->acqmir & 0x4) != 0)
#define FOCUS_SLOT(status) ((status->foc_stat & 0x01) != 0)
#define FOCUS_REF(status) ((status->foc_stat & 0x02) != 0)
#define FOCUS_OUT(status) ((status->foc_stat & 0x04) != 0)
#define FOCUS_IN(status) ((status->foc_stat & 0x08) != 0)
#define FOCUS_MOVING(status) ((status->foc_stat & 0x10) != 0)
#define FOCUS_INIT(status) ((status->foc_stat & 0x20) != 0)
#define FOCUS_STALL(status) ((status->foc_stat & 0x40) != 0)
#define EHT_OFF(status) (status->eht_mode == 0)
#define EHT_MAN_OFF(status) ((status->eht_mode & 0x1) != 0)
#define EHT_LOW(status) ((status->eht_mode & 0x2) != 0)
#define EHT_HIGH(status) ((status->eht_mode & 0x4) != 0)
#define HS_DIR_NONE(status) ((status->handeset & 0x0F) == 0)
#define HS_DIR_SOUTH(status) ((status->handset & 0x01) != 0)
#define HS_DIR_NORTH(status) ((status->handset & 0x02) != 0)
#define HS_DIR_EAST(status) ((status->handset & 0x04) != 0)
#define HS_DIR_WEST(status) ((status->handset & 0x08) != 0)
#define HS_FOC_IN(status) ((status->handset & 0x10) != 0)
#define HS_FOC_OUT(status) ((status->handset & 0x20) != 0)
#define HS_SPEED_SLEW(status) ((status->handset & 0x40) != 0)
#define HS_SPEED_SET(status) ((status->handset & 0xC0) == 0)
#define HS_SPEED_GUIDE(status) ((status->handset & 0x80) != 0)

struct plc_status
{
  unsigned short dome_pos;
  short focus_pos;
  unsigned char aper_pos;
  unsigned char filt_pos;

  unsigned char shutter;
  unsigned char dropout;
  unsigned char aper_stat;
  unsigned char filt_stat;
  unsigned char acqmir;
  unsigned char foc_stat;
  unsigned char eht_mode;
  unsigned char handset;

  unsigned char trapdoor_open;
  unsigned char instrshutt_open;
  unsigned char dome_moving;
  unsigned char power_fail;
  unsigned char watchdog_trip;
};

void reset_acq_merlin(void);
char reset_acq_pending(void);
void close_pmt_shutter(void);
short dome_azm_d(void);
unsigned long get_enc_ha_pulses(void);
unsigned long get_enc_dec_pulses(void);

/// IOCTL to retrieve current status
#define IOCTL_GET_STATUS _IOR(PLC_IOCTL_NUM, 0, unsigned long*)

/// IOCTL to set guiding azimuth of dome - activates dome guiding
#define IOCTL_SET_DOME_AZM _IOW(PLC_IOCTL_NUM, 1, unsigned long*)

/// IOCTL to move dome manually - disables dome guiding (>0 moves dome left, <0 moves dome right, 0=stop)
#define IOCTL_DOME_MOVE _IOW(PLC_IOCTL_NUM, 2, long*)

/// IOCTL to open/close/stop dome shutter (>0=open, <0=close, 0=stop)
#define IOCTL_DOMESHUTT_OPEN _IOW(PLC_IOCTL_NUM, 3, long*)

/// IOCTL to open/close/stop dome dropout (>0=open, <0=close, 0=stop)
#define IOCTL_DROPOUT_OPEN _IOW(PLC_IOCTL_NUM, 4, long*)

/// IOCTL to go to focus position (<0 is out region, >0 is in region, 0 is init)
#define IOCTL_FOCUS_GOTO _IOW(PLC_IOCTL_NUM, 5, long*)

/// IOCTL to move focus (<0 move to out region, >0 move to in region, 0 is reset - stop at next slot)
#define IOCTL_FOCUS_MOVE _IOW(PLC_IOCTL_NUM, 6, long*)

/// IOCTL to open/close instrument shutter (1=Open, everything else=Close)
#define IOCTL_INSTRSHUTT_OPEN _IOW(PLC_IOCTL_NUM, 7, unsigned long*)

/// IOCTL to move the acquisition mirror (1=in beam, 2=out beam, everything else=reset - i.e. stop motor)
#define IOCTL_SET_ACQMIR _IOW(PLC_IOCTL_NUM, 8, unsigned long*)

/// IOCTL to move aperture wheel (0=initialise - slot 0, 1-9=slots 1-9, everything else=reset)
#define IOCTL_MOVE_APER _IOW(PLC_IOCTL_NUM, 9, unsigned long*)

/// IOCTL to move filter wheel (0=initialise - slot 0, 1-9=slots 1-9, everything else=reset)
#define IOCTL_MOVE_FILT _IOW(PLC_IOCTL_NUM, 10, unsigned long*)

/// IOCTL to set EHT mode (1=lo, 2=hi, everything else=off)
#define IOCTL_SET_EHT _IOW(PLC_IOCTL_NUM, 11, unsigned long*)

/// IOCTL for simulator programme to set the status
///  - Only available if PLC_SIM flag is active in plc_driver.c
#define IOCTL_SET_SIM_STATUS _IOW(PLC_IOCTL_NUM, 14, unsigned long*)

/// IOCTL for simulator programme to read command sent by PLC programme
///  - Only available if PLC_SIM flag is active in plc_driver.c
#define IOCTL_GET_SIM_CMD _IOR(PLC_IOCTL_NUM, 15, unsigned long*)


#endif
