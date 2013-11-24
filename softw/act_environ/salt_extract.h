/*!
 * \file salt_scrape.h
 * \brief Definitions for functions used to scrape weather/environment data from SALT online datafile
 * \author Pierre van Heerden
 */

#ifndef SALT_SCRAPE_H
#define SALT_SCRAPE_H

#include <stdio.h>

/// Structure to contain weather/environment data extracted from SALT website.
struct salt_weath_data
{
  /// Julian date (fractional days)
  double jd;
  /// Air pressure (millibar)
  float air_press;
  /// Dew point temperature (fractional degrees Celcius)
  float dew_point_T;
  /// Relative humidity (percentage)
  float rel_hum;
  /// Wind speed - 30m mast (km/h)
  float wind_speed_30;
  /// Wind direction - 30m mast (fractional degrees in azimuth)
  float wind_dir_30;
  /// Wind speed - 10m mast (km/h)
  float wind_speed_10;
  /// Wind direction - 10m mast (fractional degrees in azimuth)
  float wind_dir_10;
  /// Temperature - 2m mast (fractional degrees Celcius)
  float temp_2;
  /// Temperature - 5m mast (fractional degrees Celcius)
  float temp_5;
  /// Temperature - 10m mast (fractional degrees Celcius)
  float temp_10;
  /// Temperature - 15m mast (fractional degrees Celcius)
  float temp_15;
  /// Temperature - 20m mast (fractional degrees Celcius)
  float temp_20;
  /// Temperature - 25m mast (fractional degrees Celcius)
  float temp_25;
  /// Temperature - 30m mast (fractional degrees Celcius)
  float temp_30;
  /// Rain (yes/no)
  char rain;
};

/// Main function used to scrape all relevant data from SuperWASP website.
char salt_extract_all(const char *salt_dat, int len, struct salt_weath_data *salt_weath);

#endif
