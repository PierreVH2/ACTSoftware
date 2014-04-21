/*!
 * \addtogroup common
 * \file site.h
 * \brief Header file containing site-specific defines
 */

#ifndef SITE_H
#define SITE_H

/** \name General defines */
/// pi
#define ONEPI           3.141592653589793116
/// 2*pi
#define TWOPI           6.28318530717958623200

/** Time zone in hours, Eastward of GMT is positive */
#define TIMEZONE          2
/** Name of local time zone */
#define LOCT_NAME         "SAST"
/** Latitude of site in decimal degrees */
//#define LATITUDE          -32.3760556
#define LATITUDE  -32.379
/** Longitude of site in decimal degrees */
//#define LONGITUDE         20.8010678
#define LONGITUDE 20.811

/// Average atmospheric temperatuer at site - degrees Celcius
#define AVG_TEMP_degC     15.0
/// Average atmospheric pressure at site - kilopascal
#define AVG_PRESS_kPa     82.8


#endif
