/**
 * \file merlin_driver.h
 * \author Pierre van Heerden
 * \brief Definitions for MERLIN CCD driver
 */

#ifndef MERLIN_DRIVER_H
#define MERLIN_DRIVER_H

#include <linux/ioctl.h>
#include "ccd_defs.h"

/** \brief Driver interface definitions
 * \{
 */
/// Device name of driver's character device.
#define   MERLIN_DEVICE_NAME   "act_merlin"
/// Driver IOCTL number
#define   MERLIN_IOCTL_NUM     0xE3
/// Name of driver (mostly for internal purposes)
#define   MERLIN_NAME          "ACT_MERLIN"
/// Maximum size image this driver can handle.
#define   MERLIN_MAX_IMG_LEN   117216
/** \} */

/// Data type for pixels returned by CCD
typedef unsigned char ccd_pixel_type;
/// Maximum value of ccd_pixel_type
#define CCDPIX_MAX 255

/** \brief Structure to contain image data and parameters.
 * /{ */
struct merlin_img
{
  /// Structure containing image parameters
  struct ccd_img_params img_params;
  /// Structure containing pixel data of image
  ccd_pixel_type img_data[MERLIN_MAX_IMG_LEN];
};
/** \} */

/** \brief Driver status definitions
 * \{
 */
/// Status update available
#define   CCD_STAT_UPDATE  0x01
/// An internal error on the CCD occurred
#define   CCD_ERROR        0x02
/// An exposure of the CCD has been ordered
#define   EXP_ORDERED      0x04
/// The CCD is being exposed
#define   CCD_INTEGRATING  0x08
/// The CCD is being read out
#define   CCD_READING_OUT  0x10
/// An image is ready to be read
#define   IMG_READY        0x20
/// The driver is about to exit - for internal use only
#define   MERLIN_EXIT      0x40
/** \} */

/// IOCTL to read CCD image from driver
#define IOCTL_GET_IMAGE _IOR(MERLIN_IOCTL_NUM, 0, unsigned long*)

///IOCTL to start a new exposure
#define IOCTL_ORDER_EXP _IOW(MERLIN_IOCTL_NUM, 1, unsigned long *)

///IOCTL to read modes supported by CCD
#define IOCTL_GET_MODES _IOR(MERLIN_IOCTL_NUM, 2, unsigned long *)

#ifdef ACQSIM
 /// IOCTL to write simulated CCD image to driver
 #define IOCTL_SET_IMAGE _IOW(MERLIN_IOCTL_NUM, 3, unsigned long*)
 
 /// IOCTL to read parameters of ordered exposer from driver
 #define IOCTL_GET_CMD _IOR(MERLIN_IOCTL_NUM, 4, unsigned long*)

 /// IOCTL to write simulated CCD available modes to driver
 #define IOCTL_SET_MODES _IOW(MERLIN_IOCTL_NUM, 5, unsigned long*)
#endif


#endif
