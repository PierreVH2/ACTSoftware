/*!
 * \file swasp_scrape.h
 * \brief Definitions for functions used to scrape weather/environment data from SuperWASP website (swaspgateway.suth)
 * \author Pierre van Heerden
 */

#ifndef SWASP_SCRAPE_H
#define SWASP_SCRAPE_H

#include <stdio.h>

/*! \name SWasp scrape Errors
 * \brief Error codes for each datum that's extracted from the ACT website.
 */
/*! \{ */
// enum
// {
  /// Universal Date/Time
//   ERR_SCRAPE_UNIDT = 1,
  /// Relative Humidity
//   ERR_SCRAPE_REL_HUM,
  /// Rain
//   ERR_SCRAPE_IS_DRY,
  /// Wind Speed
//   ERR_SCRAPE_WIND_SPEED,
  /// Wind Direction
//   ERR_SCRAPE_WIND_DIR,
  /// Exterior Temperature
//   ERR_SCRAPE_EXT_TEMP,
  /// Exterior Temperature - Dew Point Temperature
//   ERR_SCRAPE_EXT_DEW_TEMP,
  /// Cloud Coverage
//   ERR_SCRAPE_CLOUD
// };
/** \} */

/// Structure to contain weather/environment data extracted from SuperWASP website.
struct swasp_weath_data
{
  /// Julian date (fractional days)
  double jd;
  /// Relative humidity (integer percentage)
  char rel_hum;
  /// Rain (yes/no)
  char rain;
  /// Wind speed (integer km/h)
  short wind_speed;
  /// Wind direction (fractional degrees in azimuth)
  float wind_dir;
  /// External temperature (fractional degrees Celcius)
  float ext_temp;
  /// External temperature - Dew point temperature (fractional degrees Celcius)
  float ext_dew_temp;
  /// Cloud
  float cloud;
};

/// Main function used to scrape all relevant data from SuperWASP website.
char swasp_scrape_all(const char *swasp_dat, int len, struct swasp_weath_data *swasp_weath);

#endif
