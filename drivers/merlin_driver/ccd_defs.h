/**
 * \file ccd_defs.h
 * \author Pierre van Heerden
 * \brief Definitions for CCD drivers
 */

#ifndef CCD_DEFS_H
#define CCD_DEFS_H

// ??? Implement CCD frame transfer modes
// ??? Change prebin to use individual x and y prebinning as integers

/** \brief Prebinning modes supported by the CCD/driver
 * \{ */
/// 1x1 Prebinning
#define CCD_MODE_PREBIN_1x1   0x0001
/// 1x2 Prebinning
#define CCD_MODE_PREBIN_1x2   0x0002
/// 1x4 Prebinning
#define CCD_MODE_PREBIN_1x4   0x0004
/// 1x8 Prebinning
#define CCD_MODE_PREBIN_1x8   0x0008
/// 2x1 Prebinning
#define CCD_MODE_PREBIN_2x1   0x0010
/// 2x2 Prebinning
#define CCD_MODE_PREBIN_2x2   0x0020
/// 2x4 Prebinning
#define CCD_MODE_PREBIN_2x4   0x0040
/// 2x8 Prebinning
#define CCD_MODE_PREBIN_2x8   0x0080
/// 4x1 Prebinning
#define CCD_MODE_PREBIN_4x1   0x0100
/// 4x2 Prebinning
#define CCD_MODE_PREBIN_4x2   0x0200
/// 4x4 Prebinning
#define CCD_MODE_PREBIN_4x4   0x0400
/// 4x8 Prebinning
#define CCD_MODE_PREBIN_4x8   0x0800
/// 8x1 Prebinning
#define CCD_MODE_PREBIN_8x1   0x1000
/// 8x2 Prebinning
#define CCD_MODE_PREBIN_8x2   0x2000
/// 8x4 Prebinning
#define CCD_MODE_PREBIN_8x4   0x4000
/// 8x8 Prebinning
#define CCD_MODE_PREBIN_8x8   0x8000
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
  /// The frame transfer modes the CCD supports
  unsigned int prebin;
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
  unsigned int prebin;
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
  unsigned int prebin;
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
