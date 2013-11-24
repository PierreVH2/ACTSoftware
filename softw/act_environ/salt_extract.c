/*!
 * \file salt_scrape.c
 * \brief Functions to extract weather data from online SALT weather datafile
 * \author Pierre van Heerden
 *
 * Contains only salt_extract_all, which extracts all weather data.
 */

#include <stdio.h>
#include <string.h>
#include <act_timecoord.h>
#include <act_positastro.h>
#include <act_log.h>
#include "salt_extract.h"

/** \brief Extract all useful, available data from SALT website and populate weather data structure.
 * \param salt_dat Character string containing SALT weather data.
 * \param len Length of salt_dat
 * \param salt_weath Pointer to salt_weath_data struct, where all weather data will be stored.
 * \return \>0 on success, otherwise \=0
 *
 * Step through the salt_dat string until the last valid line is found. At each step, check the validity
 * of the content of the line. If the line is well-formatted (has the correct number and correct types of columns),
 * save the data to a temporary storage structure. At the end of this loop, the temporary storage structure should
 * contain the latest weather data available (if any data were successfully extracted). Lastly, copy the weather
 * data from the temporary storage structure to the memory location pointed to by salt_weath.
 */
char salt_extract_all(const char *salt_dat, int len, struct salt_weath_data *salt_weath)
{
  if ((salt_dat == NULL) || (salt_weath == NULL) || (len <= 0))
  {
    act_log_error(act_log_msg("Invalid input parameters."));
    return 0;
  }
  
  char found_valid = 0;
  int curchar = 0;
  char tmp_date[20] = "", tmp_time[20] = "", tmp_rain = -1;
  double tmp_floatvals[14] = {-1e6};
  struct salt_weath_data weath_data;
  struct datestruct locd, unid;
  struct timestruct loct, unit;
  char salt_copy[len+1];
  snprintf(salt_copy, len, "%s", salt_dat);
  char *start_char, *end_char;
  while (curchar < len)
  {
    end_char = strrchr(salt_copy, '\n');
    if ((end_char == NULL) || (end_char == salt_copy))
    {
      act_log_error(act_log_msg("Reached the start of the message."));
      break;
    }
    *end_char = '\0';
    end_char--;
    start_char = strrchr(salt_copy, '\n');
    if (start_char == NULL)
      start_char = salt_copy;
    else
      start_char++;
    if (end_char <= start_char)
      continue;
    int ret = sscanf(start_char, "%s %s %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %hhd", tmp_date, tmp_time, &tmp_floatvals[0], &tmp_floatvals[1], &tmp_floatvals[2], &tmp_floatvals[3], &tmp_floatvals[4], &tmp_floatvals[5], &tmp_floatvals[6], &tmp_floatvals[7], &tmp_floatvals[8], &tmp_floatvals[9], &tmp_floatvals[10], &tmp_floatvals[11], &tmp_floatvals[12], &tmp_floatvals[13], &tmp_rain);
    if (ret != 17)
    {
      act_log_normal(act_log_msg("Invalid number of columns: %d", ret));
      continue;
    }
    
    if (sscanf(tmp_date, "%hd-%hhu-%hhu", &locd.year, &locd.month, &locd.day) != 3)
    {
      act_log_normal(act_log_msg("Could not read date"));
      continue;
    }
    locd.month--;
    locd.day--;
    if (sscanf(tmp_time, "%hhu:%hhu:%hhu", &loct.hours, &loct.minutes, &loct.seconds) != 3)
    {
      act_log_normal(act_log_msg("Could not read time"));
      continue;
    }
    
    // This is a valid line
    found_valid = 1;
    loct.milliseconds = 0;
    calc_UniT (&loct, &unit);
    memcpy(&unid, &locd, sizeof(struct datestruct));
    check_systime_discrep(&unid, &loct, &unit);
    weath_data.jd = calc_GJD(&unid, &unit);
    weath_data.air_press = tmp_floatvals[0];
    weath_data.dew_point_T = tmp_floatvals[1];
    weath_data.rel_hum = tmp_floatvals[2];
    weath_data.wind_speed_30 = tmp_floatvals[3] * 3.6;
    weath_data.wind_dir_30 = tmp_floatvals[4];
    weath_data.wind_speed_10 = tmp_floatvals[5] * 3.6;
    weath_data.wind_dir_10 = tmp_floatvals[6];
    weath_data.temp_2 = tmp_floatvals[7];
    weath_data.temp_5 = tmp_floatvals[8];
    weath_data.temp_10 = tmp_floatvals[9];
    weath_data.temp_15 = tmp_floatvals[10];
    weath_data.temp_20 = tmp_floatvals[11];
    weath_data.temp_25 = tmp_floatvals[12];
    weath_data.temp_30 = tmp_floatvals[13];
    weath_data.rain = (tmp_rain != 0);
    break;
  }
  
  if (!found_valid)
  {
    act_log_normal(act_log_msg("No valid line found."));
    return 0;
  }

  memcpy(salt_weath, &weath_data, sizeof(struct salt_weath_data));
  return 1;
}
