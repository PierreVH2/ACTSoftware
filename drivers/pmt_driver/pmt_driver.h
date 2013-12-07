#ifndef PMT_DRIVER_H
#define PMT_DRIVER_H

#include <linux/ioctl.h>
#include "pmt_defs.h"
// #include <linux/signal.h>

#define PMT_IOCTL_NUM          0xE4
#define PMT_DEVICE_NAME    "act_pmt"

/// Maximum allowable counts per second
#define PMT_WARN_COUNTRATE            200000

/// IOCTL to read supported modes from driver
#define IOCTL_GET_INFO _IOR(PMT_IOCTL_NUM, 1, unsigned long*)

/// IOCTL to read data and information on current integration - can be used to get current countrate when not integrating
#define IOCTL_GET_CUR_INTEG _IOR(PMT_IOCTL_NUM, 2, unsigned long*)

/// IOCTL to write an integration command structure to the driver.
#define IOCTL_INTEG_CMD _IOR(PMT_IOCTL_NUM, 3, unsigned long*)

/// IOCTL to read integration data since last data read.
#define IOCTL_GET_INTEG_DATA _IOR(PMT_IOCTL_NUM, 4, unsigned long*)

/// IOCTL to set photometer count rate (per second) above which over-illumination condition is triggered
#define IOCTL_SET_OVERILLUM_RATE _IOW(PMT_IOCTL_NUM, 5, unsigned long*)

/// IOCTL to set photometer count rate (per second) above which over-illumination condition is triggered
#define IOCTL_SET_CHANNEL _IOW(PMT_IOCTL_NUM, 6, unsigned long*)

#endif
