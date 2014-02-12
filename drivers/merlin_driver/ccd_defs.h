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

/// Definition of maximum length of CCD identifier string
#define MAX_CCD_ID_LEN 20

/** \brief Structure indicating the modes the CCD supports.
 * The driver should fill this structure as appropriate and any programmes that use the driver should
 * retrieve and interpret this structure and its contents.
 * \{ */
struct ccd_modes
{
  /// String identifier of CCD
  char ccd_id[MAX_CCD_ID_LEN];
  /// Minimum exposure time supported by the CCD (integral seconds and nanoseconds)
  unsigned long min_exp_t_sec, min_exp_t_nanosec;
  /// Maximum exposure time supported by the CCD (integral seconds and nanoseconds)
  unsigned long max_exp_t_sec, max_exp_t_nanosec;
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
  /// Prebinning mode required
  unsigned short prebin_x, prebin_y;
  /// The start x,y and width, height of desired window.
  unsigned short win_start_x, win_start_y;
  unsigned short win_width, win_height; 
  /// The length of the exposure (integral seconds and nanoseconds)
  unsigned long exp_t_sec, exp_t_nanosec;
};
/** \} */

/** \brief Structure containing parameters and data of a completed exposure.
 * A CCD driver should implement a structure that contains this structure, including an array to
 * contain the image data, which should be at least as large as the largest image the CCD can
 * provide.
 * \{ */
struct ccd_img_params
{
  /// Total number of pixels retrieved - the driver must guarantee that the number of elements in the image data array is always at least as large as this number
  unsigned long img_len;
  /// Prebinning mode used.
  uint64_t prebin_x, prebin_y;
  /// The start x,y and width, height of window used.
  unsigned short win_start_x, win_start_y;
  unsigned short win_width, win_height; 
  /// The length of the exposure (integral seconds and nanoseconds)
  unsigned long exp_t_sec, exp_t_nanosec;
  /// Starting time of the exposure (integral seconds and nanoseconds)
  unsigned long start_sec, start_nanosec;
};
/** \} */

#ifndef __KERNEL__
#include <math.h>
/// Convenience function for extracting exposure time as fractional seconds from integral seconds and nanoseconds stored in the img_params structure
#define ccd_img_exp_t(img_params)    img_params.exp_t_sec + img_params.exp_t_nanosec/1000000000.0
/// Convenience function for exporesure start time as fractional seconds from integral seconds and nanoseconds stored in the img_params structure
#define ccd_img_start_t(img_params)  img_params.start_sec + img_params.start_nanosec/1000000000.0
/// Convenience function for saving the exposure time as integral seconds and nanoseconds from fractional seconds in the ccd_cmd structure
#define ccd_cmd_exp_t(exp_t_s, cmd)  cmd.exp_t_sec = trunc(exp_t_s) ; cmd.exp_t_nanosec = fmod(exp_t_s,1.0)*1000000000
#endif

#endif
