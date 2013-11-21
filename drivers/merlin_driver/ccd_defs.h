/**
 * \file ccd_defs.h
 * \author Pierre van Heerden
 * \brief Definitions for CCD drivers
 */

#ifndef CCD_DEFS_H
#define CCD_DEFS_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <inttypes.h>
#endif

// ??? Implement CCD frame transfer modes

/** \brief Prebinning modes supported by the CCD/driver
 * \{ */
#define CCD_PREBIN_1   0x00000001
#define CCD_PREBIN_2   0x00000002
#define CCD_PREBIN_3   0x00000004
#define CCD_PREBIN_4   0x00000008
#define CCD_PREBIN_5   0x00000010
#define CCD_PREBIN_6   0x00000020
#define CCD_PREBIN_7   0x00000040
#define CCD_PREBIN_8   0x00000080
#define CCD_PREBIN_9   0x00000100
#define CCD_PREBIN_10  0x00000200
#define CCD_PREBIN_11  0x00000400
#define CCD_PREBIN_12  0x00000800
#define CCD_PREBIN_13  0x00001000
#define CCD_PREBIN_14  0x00002000
#define CCD_PREBIN_15  0x00004000
#define CCD_PREBIN_16  0x00008000
#define CCD_PREBIN_17  0x00010000
#define CCD_PREBIN_18  0x00020000
#define CCD_PREBIN_19  0x00040000
#define CCD_PREBIN_20  0x00080000
#define CCD_PREBIN_21  0x00100000
#define CCD_PREBIN_22  0x00200000
#define CCD_PREBIN_23  0x00400000
#define CCD_PREBIN_24  0x00800000
#define CCD_PREBIN_25  0x01000000
#define CCD_PREBIN_26  0x02000000
#define CCD_PREBIN_27  0x04000000
#define CCD_PREBIN_28  0x08000000
#define CCD_PREBIN_29  0x10000000
#define CCD_PREBIN_30  0x20000000
#define CCD_PREBIN_31  0x40000000
#define CCD_PREBIN_32  0x80000000
/** \} */

/// Definition of maximum number of window modes can be supported
#define CCD_MAX_NUM_WINDOW_MODES 10

/// Definition of maximum length of CCD identifier string
#define MAX_CCD_ID_LEN 20

/** \brief Structure to contain details of a particular window mode
 * \{ */
struct ccd_window_mode
{
  /// Width of image in this mode
  unsigned short width_px;
  /// Height of image in this mode
  unsigned short height_px;
  /// X-origin
  unsigned short origin_x;
  /// Y-origin
  unsigned short origin_y;
};
/** \} */

/** \brief Structure indicating the modes the CCD supports.
 * The driver should fill this structure as appropriate and any programmes that use the driver should
 * retrieve and interpret this structure and its contents.
 * \{ */
struct ccd_modes
{
  /// String identifier of CCD
  char ccd_id[MAX_CCD_ID_LEN];
  /// Prebin modes the CCD supports
  uint64_t prebin_x, prebin_y;
  /// Windowing modes supported by the CCD
  struct ccd_window_mode windows[CCD_MAX_NUM_WINDOW_MODES];
  /// Minimum exposure time supported by the CCD
  unsigned long min_exp_t_msec;
  /// Maximum exposure time supported by the CCD
  unsigned long max_exp_t_msec;
  /// Maximum width of CCD image (disregarding prebin and window modes)
  unsigned short max_width_px;
  /// Maximum height of CCD image (disregarding prebin and window modes)
  unsigned short max_height_px;
  /// On-sky width of CCD image in arcseconds at maximum pixel width
  unsigned short ra_width_asec;
  /// On-sky height of CCD image in arcseconds at maximum pixel height
  unsigned short dec_height_asec;
};
/** \} */

/** \brief Structure containig parameters of CCD exposure.
 * A programme requiring an image from the CCD should fill the fields of this structure as desired and
 * in accordance with the available modes supported by the CCD and CCD driver, which the programme
 * should already have retrieved from the driver.
 * \{ */
struct ccd_cmd
{
  /// True if exposure should only be started at the next turn of a second, False if exposure should start immediately
  unsigned char start_at_sec;
  /// Prebinning mode required - must be one of the above listed bitmasks.
  uint64_t prebin_x, prebin_y;
  /// The number of the window mode desired.
  unsigned char window;
  /// The desired exposure time.
  unsigned long exp_t_msec;
};
/** \} */

/** \brief Structure containing parameters and data of a completed exposure.
 * A CCD driver should implement a structure that contains this structure, including an array to
 * contain the image data, which should be at least as large as the largest image the CCD can
 * provide.
 * \{ */
struct ccd_img_params
{
  /// Width and height of image in pixels, according to prebin and window mode
  unsigned short img_width, img_height;
  /// Total number of pixels retrieved - should be img_width*img_height, but this cannot be assumed.
  unsigned long img_len;
  /// Prebinning mode used - must be one of the above listed bitmasks.
  unsigned int prebin_x, prebin_y;
  /// The number of the window mode used.
  unsigned char window;
  /// The length of the exposure
  unsigned int exp_t_msec;
  /// Starting time of the exposure (hours, minutes, seconds components)
  unsigned char start_hrs, start_min, start_sec;
  /// Starting time of the exposure (milliseconds component)
  unsigned short start_msec;
};
/** \} */

#endif
