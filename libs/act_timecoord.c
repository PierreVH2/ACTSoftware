#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include "act_timecoord.h"

#ifndef ONEPI
  #define ONEPI           3.141592653589793116
#endif
#ifndef TWOPI
  #define TWOPI           6.28318530717958623200
#endif

/** \brief Converts fractional radians to fractional degrees.
 * \param radians Angle in fractional radians.
 * \return Angle in fractional degrees.
 * 
 * WARNING: This function DOES NOT check for invalid angles.
 */
double convert_RAD_DEG (double radians)
{  return radians * 180.0 / ONEPI;  }


/** \brief Converts fractional degrees to fractional radians.
 * \param degrees Angle in fractional degrees.
 * \return Angle in fractional radians.
 * 
 * WARNING: This function DOES NOT check for invalid angles.
 */
double convert_DEG_RAD (double degrees)
{  return degrees * ONEPI / 180.0;  }

/** \brief Converts fractional hours to fractional radians.
 * \param hours Angle in fractional hours.
 * \return Angle in fractional radians.
 * 
 * WARNING: This function DOES NOT check for invalid angles.
 */
double convert_H_RAD (double hours)
{  return hours * ONEPI / 12.0;  }

/** \brief Converts fractional radians to fractional hours.
 * \param radians Angle in fractional radians.
 * \return Angle in fractional hours.
 * 
 * WARNING: This function DOES NOT check for invalid angles.
 */
double convert_RAD_H (double radians)
{  return radians * 12.0 / ONEPI;  }

/** \brief Converts fractional degrees to fractional hours.
 * \param degrees Angle in fractional degrees.
 * \return Angle in fractional hours.
 * 
 * WARNING: This function DOES NOT check for invalid angles.
 */
double convert_DEG_H (double degrees)
{  return degrees * 12.0 / 180.0;  }

/** \brief Converts fractional hours to fractional degrees.
 * \param hours Angle in fractional hours.
 * \return Angle in fractional degrees.
 * 
 * WARNING: This function DOES NOT check for invalid angles.
 */
double convert_H_DEG (double hours)
{  return hours * 180.0 / 12.0;  }

/** \brief Converts declination angle from integral degrees, arcminutes, arcseconds to fractional degrees.
 * \param dec Declination structure
 * \return Declination angle in fractional degrees.
 * 
 * WARNING: For angles <-90 and >90, both RA/HA and dec will need an appropriate adjustment to keep
 *          within the definitions of RA/HA and dec.
 */
double convert_DMS_D_dec(struct decstruct *dec)
{
  if (dec == NULL)
    return 0.0;
  double tmp_dec = (double)(dec->degrees) + (dec->amin)/60.0 + (dec->asec)/3600.0;
  while (tmp_dec > 180.0)
    tmp_dec -= 360.0;
  while (tmp_dec <= -180.0)
    tmp_dec += 360.0;
  return tmp_dec;
}

/** \brief Converts altitude angle from integral degrees, arcminutes, arcseconds to fractional degrees.
 * \param alt Altitude structure
 * \return Altitude angle in fractional degrees.
 *
 * WARNING: For angles <-90 and >90, both alt and azm will need an appropriate adjustment to keep
 *          within the definitions of alt and azm.
 */
double convert_DMS_D_alt(struct altstruct *alt)
{
  if (alt == NULL)
    return 0.0;
  double tmp_alt = (double)(alt->degrees) + (alt->amin)/60.0 + (alt->asec)/3600.0;
  while (tmp_alt > 180.0)
    tmp_alt -= 360.0;
  while (tmp_alt <= -180.0)
    tmp_alt += 360.0;
  return tmp_alt;
}

/** \brief Converts azimuth angle from integral degrees, arcminutes, arcseconds to fractional degrees.
 * \param azm Azimuth structure
 * \return Azimuth angle in fractional degrees.
 */
double convert_DMS_D_azm(struct azmstruct *azm)
{
  if (azm == NULL)
    return 0.0;
  double tmp_azm = (double)(azm->degrees) + (azm->amin)/60.0 + (azm->asec)/3600.0;
  while (tmp_azm > 360.0)
    tmp_azm -= 360.0;
  while (tmp_azm <= 0.0)
    tmp_azm += 360.0;
  return tmp_azm;
}

long int convert_DMS_ASEC_dec(struct decstruct *dec)
{
  long int dec_asec = dec->degrees*3600 + dec->amin*60 + dec->asec;
  while (dec_asec > 648000)
    dec_asec -= 1296000;
  while (dec_asec < -648000)
    dec_asec += 1296000;
  return dec_asec;
}

long int convert_DMS_ASEC_alt(struct altstruct *alt)
{
  long int alt_asec = alt->degrees*3600 + alt->amin*60 + alt->asec;
  while (alt_asec > 648000)
    alt_asec -= 1296000;
  while (alt_asec < -648000)
    alt_asec += 1296000;
  return alt_asec;
}

long int convert_DMS_ASEC_azm(struct azmstruct *azm)
{
  long int azm_asec = azm->degrees*3600 + azm->amin*60 + azm->asec;
  while (azm_asec > 1296000)
    azm_asec -= 1296000;
  while (azm_asec < 0)
    azm_asec += 1296000;
  return azm_asec;
}

/** \brief Converts declination fractional degrees to angle integral degrees, arcminutes, arcseconds.
 * \param frac_deg Declination fractional degrees.
 * \param dec Declination structure
 * \return (void)
 * 
 * WARNING: For angles <-90 and >90, both RA/HA and dec will need an appropriate adjustment to keep
 *          within the definitions of RA/HA and dec.
 */
void convert_D_DMS_dec(double frac_deg, struct decstruct *dec)
{
  convert_ASEC_DMS_dec((int)(frac_deg*3600), dec);
}

/** \brief Converts altitude fractional degrees to angle integral degrees, arcminutes, arcseconds.
 * \param frac_deg Altitude fractional degrees.
 * \param alt Altitude structure
 * \return (void)
 * 
 * WARNING: For angles <-90 and >90, both alt and azm will need an appropriate adjustment to keep
 *          within the definitions of alt and azm.
 */
void convert_D_DMS_alt(double frac_deg, struct altstruct *alt)
{
  convert_ASEC_DMS_alt((int)(frac_deg * 3600), alt);
}

/** \brief Converts azimuth fractional degrees to angle integral degrees, arcminutes, arcseconds.
 * \param frac_deg Azimuth fractional degrees.
 * \param azm Azimuth structure
 * \return (void)
 */
void convert_D_DMS_azm(double frac_deg, struct azmstruct *azm)
{
  convert_ASEC_DMS_azm((int)(frac_deg * 3600), azm);
}

void convert_ASEC_DMS_dec(long integ_asec, struct decstruct *dec)
{
  if (dec == NULL)
    return;
  while (integ_asec < -648000)
    integ_asec += 1296000;
  while (integ_asec > 648000)
    integ_asec -= 1296000;
  dec->degrees = (integ_asec / 3600) % 180;
  dec->amin = (integ_asec / 60) % 60;
  dec->asec = integ_asec % 60;
}

void convert_ASEC_DMS_alt(long integ_asec, struct altstruct *alt)
{
  if (alt == NULL)
    return;
  while (integ_asec < -648000)
    integ_asec += 1296000;
  while (integ_asec > 648000)
    integ_asec -= 1296000;
  alt->degrees = (integ_asec / 3600) % 180;
  alt->amin = (integ_asec / 60) % 60;
  alt->asec = integ_asec % 60;
}

void convert_ASEC_DMS_azm(long integ_asec, struct azmstruct *azm)
{
  if (azm == NULL)
    return;
  while (integ_asec < 0)
    integ_asec += 1296000;
  while (integ_asec > 1296000)
    integ_asec -= 1296000;
  azm->degrees = (integ_asec / 3600) % 360;
  azm->amin = (integ_asec / 60) % 60;
  azm->asec = integ_asec % 60;
}

/** \brief Converts integral hours, minutes, seconds, milliseconds to fractional hours.
 * \param time Time structure.
 * \return Fractional hours
 */
double convert_HMSMS_H_time(struct timestruct *time)
{
  if (time == NULL)
    return 0.0;
  double tmp_time = (double)(time->hours) + (time->minutes)/60.0 + (time->seconds)/3600.0 + (time->milliseconds)/3600000.0;
  while (tmp_time > 24.0)
    tmp_time -= 24.0;
  while (tmp_time <= 0.0)
    tmp_time += 24.0;
  return tmp_time; 
}

/** \brief Converts integral hours, minutes, seconds, milliseconds to fractional hours.
 * \param ra Right ascension structure.
 * \return Fractional hours
 */
double convert_HMSMS_H_ra(struct rastruct *ra)
{
  if (ra == NULL)
    return 0.0;
  double tmp_ra = (double)(ra->hours) + (ra->minutes)/60.0 + (ra->seconds)/3600.0 + (ra->milliseconds)/3600000.0;
  while (tmp_ra > 24.0)
    tmp_ra -= 24.0;
  while (tmp_ra < 0.0)
    tmp_ra += 24.0;
  return tmp_ra;
}

/** \brief Converts integral hours, minutes, seconds, milliseconds to fractional hours.
 * \param ha Hour angle structure.
 * \return Fractional hours
 */
double convert_HMSMS_H_ha(struct hastruct *ha)
{
  if (ha == NULL)
    return 0.0;
  double tmp_ha = (double)(ha->hours) + (ha->minutes)/60.0 + (ha->seconds)/3600.0 + (ha->milliseconds)/3600000.0;
  while (tmp_ha > 12.0)
    tmp_ha -= 24.0;
  while (tmp_ha <= -12.0)
    tmp_ha += 24.0;
  return tmp_ha;
}

long int convert_HMSMS_MS_time(struct timestruct *time)
{
  long int tmp_time = time->hours*3600000 + time->minutes*60000 + time->seconds*1000 + time->milliseconds;
  while (tmp_time > 86400000)
    tmp_time -= 86400000;
  while (tmp_time < 0)
    tmp_time += 86400000;
  return tmp_time;
}

long int convert_HMSMS_MS_ra(struct rastruct *ra)
{
  long int tmp_ra = ra->hours*3600000 + ra->minutes*60000 + ra->seconds*1000 + ra->milliseconds;
  while (tmp_ra > 86400000)
    tmp_ra -= 86400000;
  while (tmp_ra < 0)
    tmp_ra += 86400000;
  return tmp_ra;
}

long int convert_HMSMS_MS_ha(struct hastruct *ha)
{
  long int tmp_ha = ha->hours*3600000 + ha->minutes*60000 + ha->seconds*1000 + ha->milliseconds;
  while (tmp_ha > 43200000)
    tmp_ha -= 86400000;
  while (tmp_ha < -43200000)
    tmp_ha += 86400000;
  return tmp_ha;
}

/** \brief Converts fractional hours to integral hours, minutes, seconds, milliseconds.
 * \param frac_hours Fractional hours.
 * \param time Time structure.
 * \return (void)
 */
void convert_H_HMSMS_time(double frac_hours, struct timestruct *time)
{
  convert_MS_HMSMS_time((long int)(frac_hours * 3600000), time);
}

/** \brief Converts fractional hours to integral hours, minutes, seconds, milliseconds.
 * \param frac_hours Fractional hours.
 * \param ra Right ascension structure.
 * \return (void)
 */
void convert_H_HMSMS_ra(double frac_hours, struct rastruct *ra)
{
  convert_MS_HMSMS_ra((long int)(frac_hours * 3600000), ra);
}

/** \brief Converts fractional hours to integral hours, minutes, seconds, milliseconds.
 * \param frac_hours Fractional hours.
 * \param ha Hour angle structure.
 * \return (void)
 */
void convert_H_HMSMS_ha(double frac_hours, struct hastruct *ha)
{
  convert_MS_HMSMS_ha((long int)(frac_hours * 3600000), ha);
}

void convert_MS_HMSMS_time(long int integ_millisec, struct timestruct *time)
{
  if (time == NULL)
    return;
  while (integ_millisec < 0)
    integ_millisec += 86400000;
  while (integ_millisec > 86400000)
    integ_millisec -= 86400000;
  time->hours = (integ_millisec / 3600000) % 24;
  time->minutes = (integ_millisec / 60000) % 60;
  time->seconds = (integ_millisec / 1000) % 60;
  time->milliseconds = integ_millisec % 1000;
}

void convert_MS_HMSMS_ra(long int integ_millisec, struct rastruct *ra)
{
  if (ra == NULL)
    return;
  while (integ_millisec < 0)
    integ_millisec += 86400000;
  while (integ_millisec > 86400000)
    integ_millisec -= 86400000;
  ra->hours = (integ_millisec / 3600000) % 24;
  ra->minutes = (integ_millisec / 60000) % 60;
  ra->seconds = (integ_millisec / 1000) % 60;
  ra->milliseconds = integ_millisec % 1000;
}

void convert_MS_HMSMS_ha(long int integ_millisec, struct hastruct *ha)
{
  if (ha == NULL)
    return;
  while (integ_millisec < -43200000)
    integ_millisec += 86400000;
  while (integ_millisec > 43200000)
    integ_millisec -= 86400000;
  ha->hours = (integ_millisec / 3600000) % 24;
  ha->minutes = (integ_millisec / 60000) % 60;
  ha->seconds = (integ_millisec / 1000) % 60;
  ha->milliseconds = integ_millisec % 1000;
}

char *time_to_str(struct timestruct *time)
{
  char *ret = malloc(15*sizeof(char));
  snprintf(ret, 15*sizeof(char), "%2hhu:%02hhu:%02hhu.%03hu", time->hours, time->minutes, time->seconds, time->milliseconds);
  return ret;
}

char *date_to_str(struct datestruct *date)
{
  char *ret = malloc(12*sizeof(char));
  snprintf(ret, 12*sizeof(char), "%4hd/%02hhu/%02hhu", date->year, date->month+1, date->day+1);
  return ret;
}

char *ra_to_str(struct rastruct *ra)
{
  char *ret = malloc(16*sizeof(char));
  snprintf(ret, 16*sizeof(char), "%2hhuh%02hhum%02hhu.%01hus", ra->hours, ra->minutes, ra->seconds, ra->milliseconds/100);
  return ret;
}

char *ha_to_str(struct hastruct *ha)
{
  char *ret = malloc(17*sizeof(char));
  if (ha->hours + ha->minutes + ha->seconds + ha->milliseconds < 0)
    snprintf(ret, 17*sizeof(char), "-%2hhdh%02hhdm%02hhd.%01hds", -ha->hours, -ha->minutes, -ha->seconds, -ha->milliseconds/100);
  else
    snprintf(ret, 17*sizeof(char), " %2hhdh%02hhdm%02hhd.%01hds", ha->hours, ha->minutes, ha->seconds, ha->milliseconds/100);
  return ret;
}

char *dec_to_str(struct decstruct *dec)
{
  char *ret = malloc(13*sizeof(char));
  if (dec->degrees + dec->amin + dec->asec < 0)
    snprintf(ret, 13*sizeof(char), "-%3hhd°%02hhd\'%02hhd\"", -dec->degrees, -dec->amin, -dec->asec);
  else
    snprintf(ret, 13*sizeof(char), " %3hhd°%02hhd\'%02hhd\"", dec->degrees, dec->amin, dec->asec);
  return ret;
}

char *alt_to_str(struct altstruct *alt)
{
  char *ret = malloc(13*sizeof(char));
  if (alt->degrees + alt->amin + alt->asec < 0)
    snprintf(ret, 13*sizeof(char), "-%2hhd°%02hhd\'%02hhd\"", -alt->degrees, -alt->amin, -alt->asec);
  else
    snprintf(ret, 13*sizeof(char), " %2hhd°%02hhd\'%02hhd\"", alt->degrees, alt->amin, alt->asec);
  return ret;
}

char *azm_to_str(struct azmstruct *azm)
{
  char *ret = malloc(13*sizeof(char));
  snprintf(ret, 13*sizeof(char), "%3hu°%02hhd\'%02hhd\"", azm->degrees, azm->amin, azm->asec);
  return ret;
}

