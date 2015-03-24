/*!
 * \file act_common.c
 * \brief Implementations for functions (mostly astronomical/mathematical) that are used by a number of ACT programmes.
 * \author Pierre van Heerden
 * \todo Check coord limits
 */

#include <stdio.h>

#include <math.h>
#include <string.h>
#include "act_site.h"
#include "act_positastro.h"

/// Altitude of horizon
#define HORIZON_ALT     5.0

/** \brief Calculates whether the specified year is a leap year.
 * \param year Years since 0 AD
 * \return TRUE if 'year' is a leap year, otherwise returns FALSE
 */
char isLeapYear(short year)
{  return ((year%4 == 0) && (year%100 != 0)) || ((year%4 == 0) && (year%100 == 0) && (year%400 == 0));  }

/** \brief Calculates the number of days in a month.
 * \param date Date structure containing the desired year and month
 * \return Number of days in that month, taking leap years into account
 */
unsigned char daysInMonth(struct datestruct *date)
{
  if (date == NULL)
    return 0;
  if (date->month >= 12)
    return 0;
  if ((date->month == 1) && isLeapYear(date->year))
    return 29;
  short Months[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  return Months[date->month];
}

/** \brief Checks the ranges of each element of the given date and time structures.
 * \param date Date structure for the desired date.
 * \param time Time structure for the desired time.
 * \return (void)
 * 
 * Ensures that time->milliseconds is in [0-999]
 * Ensures that time->seconds and time->minutes are in [0-59]
 * Ensures that time->hours is in [0-23]
 * Ensures that date->day is valid for the given date->month and date->year.
 * Ensures that date->month is in [0-11]
 * 
 * All incorrect values are corrected in-place.
 */
void check_date_time_ranges(struct timestruct *time, struct datestruct *date)
{
  if ((date == NULL) || (time == NULL))
    return;
  unsigned char tmp_days;
  while (time->milliseconds >= 1000)
  {
    time->milliseconds -= 1000;
    time->seconds += 1;
  }
  while (time->seconds >= 60)
  {
    time->seconds -= 60;
    time->minutes += 1;
  }
  while (time->minutes >= 60)
  {
    time->minutes -= 60;
    time->hours += 1;
  }
  while (time->hours >= 24)
  {
    time->hours -= 24;  
    date->day += 1;
  }
  while (date->month >= 12)
  {
    date->month -= 12;
    date->year += 1;
  }
  while (date->day >= (tmp_days = daysInMonth(date)))
  {
    date->day -= tmp_days;
    date->month += 1;
  }
  while (date->month >= 12)
  {
    date->month -= 12;
    date->year += 1;
  }
}

/** \brief Checks for discrepencies between system time and real time and corrects date if necessary
 * \param date Date structure for the desired date.
 * \param sys_time System time structure for the desired time.
 * \param real_time Real time structure for the desired time.
 * \return (void)
 * 
 * Incorrect date/time stamps can be produced near midnight if the system time differs from the 
 * real time. If, for instance, the system clock is 1 second slow on January 1 2011, at 00:00:00
 * on January 2 2011 (real time), the software's time will be 00:00:00 but the date will still be
 * Januray 1 2011, seeing as the date is read from the system.
 * 
 * If there is a discrepency between the system time and the real time, this function calculates
 * the "real date" from the system date, system time and real time.
 * 
 * The elements of date are modified in-place.
 */
void check_systime_discrep(struct datestruct *date, struct timestruct *sys_time, struct timestruct *real_time)
{
  if ((date == NULL) || (sys_time == NULL) || (real_time == NULL))
    return;
  if (memcmp(sys_time, real_time, sizeof(struct timestruct)) == 0)
    return;
  double sys_hours = convert_HMSMS_H_time(sys_time);
  double real_hours = convert_HMSMS_H_time(real_time);
  int tmp_day = date->day, tmp_month = date->month;
  if (real_hours - sys_hours > 12)
  {
    // Real day is one day earlier than system day
    tmp_day--;
    if (tmp_day > 0)  // Same month
    {
      date->day = tmp_day;
      return;
    }
    // Last day of previous month
    tmp_month--;
    if (tmp_month > 0)
    {
      date->month = tmp_month;
      date->day = daysInMonth(date)-1;
      return;
    }
    // Last month (December) of the previous year
    (date->year)--;
    date->month = 11;
    date->day = daysInMonth(date)-1;
    return;
  }
  if (sys_hours - real_hours > 12)
  {
    // Real day is one day later than system day
    tmp_day++;
    if (tmp_day <= daysInMonth(date))  // Same month
    {
      date->day = tmp_day;
      return;
    }
    // First day of next month
    tmp_day = 0;
    tmp_month++;
    if (tmp_month < 12)  // Same year
    {
      date->day = 0;
      date->month = tmp_month;
      return;
    }
    date->day = 0;
    date->month = 0;
    (date->year)++;
    return;
  }
}

/** \brief Checks Hour Angle, Right Ascension and Declination ranges.
 * \param ra Right Ascension structure - may not be NULL if ha is NULL.
 * \param ha Hour Angle structure - may not be NULL if ra is NULL.
 * \param dec Declination structure - may not be NULL.
 * 
 * Values are modified in-place.
 * Either ra or ha may be NULL (but not both), in which case that parameter will not be used/updated.
 * Ensures that dec is in [-90,90], ha is in [-12,12] and ra is in [0,24].
 * If dec is in [-180,-90] or [90,180], ha and ra is in the wrong demisphere and is corrected.
 */
void check_ra_ha_dec_ranges(struct rastruct *ra, struct hastruct *ha, struct decstruct *dec)
{
  if ((dec == NULL) || ((ra == NULL) && (ha == NULL)))
    return;
  double tmp_ra = convert_HMSMS_H_ra(ra);
  double tmp_ha = convert_HMSMS_H_ha(ha);
  double tmp_dec = convert_DMS_D_dec(dec);
  while (tmp_dec <= -180.0)
    tmp_dec += 360.0;
  while (tmp_dec > 180.0)
    tmp_dec -= 360.0;
  if ((tmp_dec > 90.0) || (tmp_dec < -90.0))
  {
    tmp_ra += 12.0;
    tmp_ha += 12.0;
    tmp_dec = tmp_dec < 0.0 ? -180.0 - tmp_dec : 180.0 - tmp_dec;
  }
  convert_H_HMSMS_ra(tmp_ra, ra);
  convert_H_HMSMS_ha(tmp_ha, ha);
  convert_D_DMS_dec(tmp_dec, dec);
}

/** \brief Checks Altitude and Azimuth ranges.
 * \param alt Altitude structure - may not be NULL.
 * \param azm Azimuth structure - may not be NULL.
 * 
 * Values are modified in-place.
 * Ensures that alt is in [-90,90] and azm is in [0,360].
 * If alt is in [-180,-90] or [90,180], alt is in the wrong demisphere and is corrected.
 */
void check_alt_azm_ranges(struct altstruct *alt, struct azmstruct *azm)
{
  if ((alt == NULL) || (azm == NULL))
    return;
  double tmp_alt = convert_DMS_D_alt(alt);
  double tmp_azm = convert_DMS_D_azm(azm);
  while (tmp_alt <= -180.0)
    tmp_alt += 360.0;
  while (tmp_alt > 180.0)
    tmp_alt -= 360.0;
  if ((tmp_alt < -90.0) || (tmp_alt > 90.0))
  {
    tmp_azm += 180.0;
    tmp_alt = tmp_alt < 0.0 ? -180.0 - tmp_alt : 180.0 - tmp_alt;
  }
  convert_D_DMS_alt(tmp_alt, alt);
  convert_D_DMS_azm(tmp_azm, azm);
}

/** \brief Convert equitorial coordinates (hour-angle and declination) to horizontal coordinates (altitude, azimuth).
 * \param alt Where altitude will be stored.
 * \param azm Where azimuth will be stored.
 * \param hangle Hour-angle in fractional hours.
 * \param dec Declination in fractional degrees.
 * \return (void)
 * 
 * Any NULL pointers will be ignored. The other values will be returned anyway.
 */
void convert_EQUI_ALTAZ (struct hastruct *ha, struct decstruct *dec, struct altstruct *alt, struct azmstruct *azm)
{
  if ((ha == NULL) || (dec == NULL))
    return;
  double ha_rad = convert_H_RAD(convert_HMSMS_H_ha(ha));
  double dec_rad = convert_DEG_RAD(convert_DMS_D_dec(dec));
  double lat_rad = convert_DEG_RAD(LATITUDE);
  double alt_rad = asin(sin(lat_rad)*sin(dec_rad) + cos(lat_rad)*cos(dec_rad)*cos(ha_rad));
  double azm_rad = atan2(sin(-ha_rad) * cos(dec_rad) / cos(alt_rad), (sin(dec_rad)-sin(lat_rad)*sin(alt_rad)) / cos(lat_rad) / cos(alt_rad));
  convert_D_DMS_alt(convert_RAD_DEG(alt_rad), alt);
  convert_D_DMS_azm(convert_RAD_DEG(azm_rad), azm);
  check_alt_azm_ranges(alt, azm);
}

/** \brief Convert horizontal coordinates (altitude, azimuth) to equitorial coordinates (hour-angle and declination).
 * \param hangle Hour-angle in fractional hours.
 * \param dec Declination in fractional degrees.
 * \param alt Where altitude will be stored.
 * \param azm Where azimuth will be stored.
 * \return (void)
 * 
 * Any NULL pointers will be ignored. The other values will be returned anyway.
 * Algorithm from http://star-www.st-and.ac.uk/~fv/webnotes/chapter7.htm .
 */
void convert_ALTAZ_EQUI (struct altstruct *alt, struct azmstruct *azm, struct hastruct *ha, struct decstruct *dec)
{
  if ((alt == NULL) || (azm == NULL))
    return;
  double alt_rad = convert_DEG_RAD(convert_DMS_D_alt(alt));
  double azm_rad = convert_DEG_RAD(convert_DMS_D_azm(azm));
  double lat_rad = convert_DEG_RAD(LATITUDE);
  double dec_rad = asin(sin(lat_rad)*sin(alt_rad) + cos(lat_rad)*cos(alt_rad)*cos(azm_rad));
  double ha_rad = atan2(-sin(azm_rad)*cos(alt_rad)/cos(dec_rad), (sin(alt_rad) - sin(dec_rad)*sin(lat_rad))/cos(dec_rad)/cos(lat_rad));
  convert_H_HMSMS_ha(convert_RAD_H(ha_rad), ha);
  convert_D_DMS_dec(convert_RAD_DEG(dec_rad), dec);
  check_ra_ha_dec_ranges(NULL, ha, dec);
}

/** \brief Calculates Right Ascention from Hour angle.
 * \param hangle Hour angle structure.
 * \param sidt Sidereal time structure.
 * \param ra Right ascension structure.
 */
void calc_RA (struct hastruct *ha, struct timestruct *sidt, struct rastruct *ra)
{
  if ((ha == NULL) || (sidt == NULL))
    return;
  double frac_ra = convert_HMSMS_H_time(sidt) - convert_HMSMS_H_ha(ha);
  convert_H_HMSMS_ra(frac_ra, ra);
}

/** \brief Calculates Hour-angle from Right-Ascention.
 * \param ra Right ascension structure.
 * \param sidt Sidereal time structure.
 * \param hangle Hour angle structure.
 */
void calc_HAngle (struct rastruct *ra, struct timestruct *sidt, struct hastruct *ha)
{ 
  if ((ra == NULL) || (sidt == NULL))
    return;
  double frac_ha = convert_HMSMS_H_time(sidt) - convert_HMSMS_H_ra(ra);
  convert_H_HMSMS_ha(frac_ha, ha);
}

/** \brief Calculates Universal Time (UT) from Local Time.
 * \param loct Local time structure.
 * \param unit Time structure where Universal time will be stored.
 * \return (void).
 */
void calc_UniT (struct timestruct *loct, struct timestruct *unit)
{
  if (loct == NULL)
    return;
  double tmp_ut = convert_HMSMS_H_time(loct) - (double)TIMEZONE;
  convert_H_HMSMS_time(tmp_ut, unit);
}

/** \brief Calculates Local Time from Universal Time (UT).
 * \param unit Universal time structure.
 * \param loct Time structure where local time will be stored.
 * \return (void)
 */
void calc_LocT (struct timestruct *unit, struct timestruct *loct)
{
  if (unit == NULL)
    return;
  double tmp_lt = convert_HMSMS_H_time(unit) + (double)TIMEZONE;
  convert_H_HMSMS_time(tmp_lt, loct);
}

/** \brief Calculate geocentric Julian date.
 * \param date Universal date structure.
 * \param unit Universal time structure.
 * \return Geocentric Julian date in fractional days.
 *
 * \todo Reference algorithm.
 * \todo Check that results are still correct with new structure definitions.
 */
double calc_GJD (struct datestruct *unid, struct timestruct *unit)
{
  if ((unid == NULL) || (unit == NULL))
    return 0.0;
  
  long int jd12h;
  long year = unid->year;
  long month = unid->month+1;
  long day = unid->day+1;
  
  jd12h = day - 32075L + 1461L * (year + 4800L + (month - 14L) / 12L) / 4L
        + 367L * (month - 2L - (month - 14L) / 12L * 12L)
        / 12L - 3L * ((year + 4900L + (day - 14L) / 12L)
        / 100L) / 4L;
  return (double) jd12h - 0.5 + convert_HMSMS_H_time(unit) / 24.0;
}

/** \brief Calculate Local Apparent Sidereal Time from UT time and date and longitude.
 * \param date Universal date structure.
 * \param unit Universal time structure.
 * \return Local mean sidereal time in fractional hours.
 * 
 * Algorithm from Astronomical Almanac 2012.
 *
 * \todo Check that results are still correct with new structure definitions.
 * \todo Introduce correction to calculate apparent sidereal time.
 */
double calc_SidT (double jd)
{
  double Du, Du_frac, tmp_Duint, T, GMST;
  Du = jd - 2451545.0;
  Du_frac = modf(Du, &tmp_Duint);
  T = Du / 36525.0;
  GMST = 86400*(0.7790572732640 + 0.00273781191135448*Du + Du_frac)
       + 0.0096707
       + 307.47710227*T
       + 0.092772113*pow(T,2)
       - 0.0000000293*pow(T,3)
       - 0.00000199707*pow(T,4)
       - 2.453E-9 * pow(T,5);
  return fmod(GMST/3600.0 + LONGITUDE*12.0/180.0,24.0);
}

/** \brief Calculates several parameters of the Sun (ra, dec) as well as heliocentric Julian date.
 * \param jd Geocentric Julian date in fractional days.
 * \param targ_ra Right-ascension of target structure.
 * \param targ_dec Declination of target structure.
 * \param sun_ra Structure where right-ascension of Sun will be stored.
 * \param sun_dec Structure where declination of Sun will be stored.
 * \param hjd Heliocentric Julian date.
 *
 * \todo Check unit of sun_dist.
 *
 * Any NULL pointers will be ignored. The other values will be returned anyway.
 * Algorithm from page D22 of Astronomical Almanac 2010.
 */
void calc_sun (double jd, struct rastruct *targ_ra, struct decstruct *targ_dec, struct rastruct *sun_ra, struct decstruct *sun_dec, double *hjd)
{
  double n = jd - 2451545.0;
  double L = convert_DEG_RAD(280.460 + 0.9856474*n);
  double g = convert_DEG_RAD(357.528 + 0.9856003*n);
  double lam = fmod(L + convert_DEG_RAD(1.915*sin(g) + 0.020*sin(2.0*g)), TWOPI);
  lam += lam < 0.0 ? TWOPI : 0.0;
  double eps = convert_DEG_RAD(23.439 - 0.0000004*n);
  double ra_rad = atan(cos(eps)*tan(lam));
  ra_rad += (floor(lam/(ONEPI/2.0)) - floor(ra_rad/(ONEPI/2.0))) * (ONEPI/2.0);
  ra_rad = fmod(ra_rad, TWOPI);
  ra_rad += ra_rad < 0.0 ? TWOPI : 0.0;
  convert_H_HMSMS_ra(convert_RAD_H(ra_rad), sun_ra);
  double dec_rad = asin(sin(eps)*sin(lam));
  convert_D_DMS_dec(convert_RAD_DEG(dec_rad), sun_dec);
  check_ra_ha_dec_ranges(sun_ra, NULL, sun_dec);
  double r = 1.00014 - 0.01671*cos(g) - 0.00014*cos(2.0*g);
  if ((targ_ra == NULL) || (targ_dec == NULL))
    return;
  ra_rad = convert_H_RAD(convert_HMSMS_H_ra(targ_ra));
  dec_rad = convert_DEG_RAD(convert_DMS_D_dec(targ_dec));
  double hc = -0.0057755*r*(cos(lam)*cos(ra_rad)*cos(dec_rad) + sin(lam)*(sin(eps)*sin(dec_rad) + cos(eps)*cos(dec_rad)*sin(ra_rad)) ) ;
  if (hjd != NULL)
    *hjd = jd + hc;
}

/** \brief Calculate RA and Dec of the Moon
 * \param moon_ra Structure where right ascension of Moon will be stored.
 * \param moon_dec Structure where declination of Moon will be stored.
 * \param JulCen Julian century in fractional centuries.
 *
 * \todo Check results.
 *
 * Any NULL pointers will be ignored. The other values will be returned anyway.
 */
void calc_moon_pos (double JulCen, struct rastruct *moon_ra, struct decstruct *moon_dec)
{
  double lam  =  218.32 + 481267.881*JulCen;
  lam +=  6.29 * sin (convert_DEG_RAD(fmod(135.0 + 477198.87*JulCen,360.0)));
  lam += -1.27 * sin (convert_DEG_RAD(fmod(259.3 - 413335.36*JulCen,360.0)));
  lam +=  0.66 * sin (convert_DEG_RAD(fmod(235.7 + 890534.22*JulCen,360.0)));
  lam +=  0.21 * sin (convert_DEG_RAD(fmod(269.9 + 954397.74*JulCen,360.0)));
  lam += -0.19 * sin (convert_DEG_RAD(fmod(357.5 +  35999.05*JulCen,360.0)));
  lam += -0.11 * sin (convert_DEG_RAD(fmod(186.5 + 966404.03*JulCen,360.0)));
  lam = convert_DEG_RAD(fmod(lam,180.0));
  
  double bet = 5.13 * sin (convert_DEG_RAD(fmod(93.3 + 483202.02*JulCen,360.0)));
  bet +=  0.28 * sin (convert_DEG_RAD(fmod(228.2 + 960400.89*JulCen,360.0)));
  bet += -0.28 * sin (convert_DEG_RAD(fmod(318.3 +   6003.15*JulCen,360.0)));
  bet += -0.17 * sin (convert_DEG_RAD(fmod(217.6 - 407332.21*JulCen,360.0)));
  bet = convert_DEG_RAD(fmod(bet,360.0));

  double l = cos(bet)*cos(lam);
  double m = 0.9175*cos(bet)*sin(lam) - 0.3978*sin(bet);
  double n = 0.3978*cos(bet)*sin(lam) + 0.9175*sin(bet);

  double phi = convert_DEG_RAD(LATITUDE);
  double theta = 0.0;
  
  double par =  0.9508 + 0.0518 * cos(convert_DEG_RAD(135.0 + 477198.87*JulCen))  +  0.0095 * cos(convert_DEG_RAD(259.3 - 413335.36*JulCen)) + 0.0078 * cos(convert_DEG_RAD(235.7 + 890534.22*JulCen))  +  0.0028 * cos(convert_DEG_RAD(269.9 + 954397.74*JulCen));

  double rr = 1.0 / sin(convert_DEG_RAD(par));
  double x = rr*l - cos (phi)*cos (theta);
  double y = rr*m - cos (phi)*sin (theta);
  double z = rr*n - sin (phi);
  double r = sqrt (x*x + y*y + z*z);
  convert_D_DMS_dec(convert_RAD_DEG(asin(z/r)), moon_dec);
  
  if (fabs (x) < 1.0e-5)
    z = 1.0E+5;
  else
    z = y/x;
  r = atan (z);
  convert_H_HMSMS_ra(convert_RAD_H(r), moon_ra);
}

/** \brief Calculate the fraction of illumination of the Moon.
 * \param moon_ra Right ascention of the Moon (structure).
 * \param moon_dec Declination of the Moon (structure).
 * \param sun_ra Right ascention of the Sun (structure).
 * \param sun_dec Declination of the Sun (structure).
 * \return Illuminated fraction of the Moon ([0..1]).
 * 
 * From eq (48.2) of Astronomical Algorithms by J. Meeus
 */
double calc_moon_illum (struct rastruct *moon_ra, struct decstruct *moon_dec, struct rastruct *sun_ra, struct decstruct *sun_dec)
{
  if ((moon_ra == NULL) || (moon_dec == NULL) || (sun_ra == NULL) || (sun_dec == NULL))
    return 0.0;
  double moon_ra_rad = convert_H_RAD(convert_HMSMS_H_ra(moon_ra));
  double moon_dec_rad = convert_DEG_RAD(convert_DMS_D_dec(moon_dec));
  double sun_ra_rad = convert_H_RAD(convert_HMSMS_H_ra(sun_ra));
  double sun_dec_rad = convert_DEG_RAD(convert_DMS_D_dec(sun_dec));

  double cos_psi = ((sin(sun_dec_rad) * sin(moon_dec_rad)) + (cos(sun_dec_rad) * cos(moon_dec_rad) * cos(sun_ra_rad-moon_ra_rad)));
  return (1.0-cos_psi) / 2.0;
}

/** \brief Calculates and returns the airmass at the specifified altitude.
 * \param alt Altitude structure.
 * \return Airmass as a unitless fraction.
 *
 * \note Implement proper treatment of airmass.
 */
double calc_airmass(struct altstruct *alt)
{
  double alt_rad = convert_DEG_RAD(convert_DMS_D_alt(alt));
  // Prevents division by 0
  if (alt_rad < convert_DEG_RAD(HORIZON_ALT))
    return 200.0;
  if (alt_rad > ONEPI / 2.0)
    alt_rad = ONEPI - alt_rad;

  return 1.0 / cos((ONEPI/2.0) - alt_rad);
}

/** \brief Function to precess input coordinates at input epoch to output coordinates in output epoch.
 * \param ra_in Input Right ascension
 * \param dec_in Input Declinatoin
 * \param epoch_in Input Epoch
 * \param epoch_out Output Epoch
 * \param ra_out Output Right ascension
 * \param dec_out Output Declinatoin
 * 
 * Code liberated from the Linus 160 programme - originally written by Luis Balona
 */
void precess_coord(struct rastruct *ra_in, struct decstruct *dec_in, float epoch_in, float epoch_out, struct rastruct *ra_out, struct decstruct *dec_out)
{
  if ((ra_in == NULL) || (dec_in == NULL) || (ra_out == NULL) || (dec_out == NULL))
    return;
  double ra_rad = convert_H_RAD(convert_HMSMS_H_ra(ra_in));
  double dec_rad = convert_DEG_RAD(convert_DMS_D_dec(dec_in));
  double T, xa, za, ta, sacd, cacd, sd ;
  
  T = 0.01*(epoch_out - epoch_in) ;
  xa = convert_DEG_RAD((0.6406161 + 0.0000839*T + 0.0000050*T*T)*T);
  za = convert_DEG_RAD((0.6406161 + 0.0003041*T + 0.0000051*T*T)*T);
  ta = convert_DEG_RAD((0.5567530 - 0.0001185*T - 0.0000116*T*T)*T);
  sacd = sin(ra_rad+xa)*cos(dec_rad);
  cacd = cos(ra_rad+xa)*cos(ta)*cos(dec_rad) - sin(ta)*sin(dec_rad);
  sd = cos(ra_rad + xa)*sin(ta)*cos(dec_rad) + cos(ta)*sin(dec_rad);
  dec_rad = asin(sd);
  ra_rad = atan2(sacd, cacd) + za;
  convert_H_HMSMS_ra(convert_RAD_H(ra_rad),ra_out);
  convert_D_DMS_dec(convert_RAD_DEG(dec_rad),dec_out);
}

/** \brief Calculates unrefracted altitude from observed altitude
 * \param alt Altitude structure - modified in-place.
 * \return (void)
 */
void corr_atm_refract_tel_sky(struct altstruct *alt)
{
  double alt_deg = convert_DMS_D_alt(alt);
  alt_deg -= calc_atm_refract_deg(alt_deg, AVG_PRESS_kPa, AVG_TEMP_degC);
  convert_D_DMS_alt(alt_deg, alt);
}

/** \brief Calculates observed (refracted) altitude from unrefracted altitude
 * \param alt Altitude structure - modified in-place.
 * \return (void)
 *
 * Formulae from "Astronomical Algorithms (2nd ed.), Jean Meeus, Willmann-Bell, Inc., 1998, pg. 105-108
 */
void corr_atm_refract_sky_tel(struct altstruct *alt)
{
  double alt_deg = convert_DMS_D_alt(alt);
  alt_deg += calc_atm_refract_deg(alt_deg, AVG_PRESS_kPa, AVG_TEMP_degC);
  convert_D_DMS_alt(alt_deg, alt);
}

void corr_atm_refract_tel_sky_equat(struct hastruct *ha, struct decstruct *dec)
{
  double ha_rad = convert_H_RAD(convert_HMSMS_H_ha(ha));
  double dec_rad = convert_DEG_RAD(convert_DMS_D_dec(dec));
  double lat_rad = convert_DEG_RAD(LATITUDE);
  double alt_rad = asin(sin(lat_rad)*sin(dec_rad) + cos(lat_rad)*cos(dec_rad)*cos(ha_rad));
  double azm_rad = atan2(sin(-ha_rad) * cos(dec_rad) / cos(alt_rad), (sin(dec_rad)-sin(lat_rad)*sin(alt_rad)) / cos(lat_rad) / cos(alt_rad));
  alt_rad -= convert_DEG_RAD(calc_atm_refract_deg(convert_RAD_DEG(alt_rad), AVG_PRESS_kPa, AVG_TEMP_degC));
  dec_rad = asin(sin(lat_rad)*sin(alt_rad) + cos(lat_rad)*cos(alt_rad)*cos(azm_rad));
  ha_rad = atan2(-sin(azm_rad)*cos(alt_rad)/cos(dec_rad), (sin(alt_rad) - sin(dec_rad)*sin(lat_rad))/cos(dec_rad)/cos(lat_rad));
  convert_H_HMSMS_ha(convert_RAD_H(ha_rad), ha);
  convert_D_DMS_dec(convert_RAD_DEG(dec_rad), dec);
  check_ra_ha_dec_ranges(NULL, ha, dec);
}

void corr_atm_refract_sky_tel_equat(struct hastruct *ha, struct decstruct *dec)
{
  double ha_rad = convert_H_RAD(convert_HMSMS_H_ha(ha));
  double dec_rad = convert_DEG_RAD(convert_DMS_D_dec(dec));
  double lat_rad = convert_DEG_RAD(LATITUDE);
  double alt_rad = asin(sin(lat_rad)*sin(dec_rad) + cos(lat_rad)*cos(dec_rad)*cos(ha_rad));
  double azm_rad = atan2(sin(-ha_rad) * cos(dec_rad) / cos(alt_rad), (sin(dec_rad)-sin(lat_rad)*sin(alt_rad)) / cos(lat_rad) / cos(alt_rad));
  alt_rad += convert_DEG_RAD(calc_atm_refract_deg(convert_RAD_DEG(alt_rad), AVG_PRESS_kPa, AVG_TEMP_degC));
  dec_rad = asin(sin(lat_rad)*sin(alt_rad) + cos(lat_rad)*cos(alt_rad)*cos(azm_rad));
  ha_rad = atan2(-sin(azm_rad)*cos(alt_rad)/cos(dec_rad), (sin(alt_rad) - sin(dec_rad)*sin(lat_rad))/cos(dec_rad)/cos(lat_rad));
  convert_H_HMSMS_ha(convert_RAD_H(ha_rad), ha);
  convert_D_DMS_dec(convert_RAD_DEG(dec_rad), dec);
  check_ra_ha_dec_ranges(NULL, ha, dec);
}

/** \brief Calculates refraction at given altitude
 * \param alt Altitude structure - not modified.
 * \param press_kpa Atmospheric pressure in kPa
 * \param temp_c Atmospheric temperature in degrees Celcius
 * \return Atmospheric refraction angle in degrees
 *
 * Formula from "Astronomical Algorithms (2nd ed.), Jean Meeus, Willmann-Bell, Inc., 1998, pg. 105-108
 * Observed or calculated altitude may be used - difference in refraction should be negligible (see Meeus)
 * Add returned angle to ideal (unrefracted) altitude to find predicted true altitude ("sky to tel")
 * Subtract returned angle from observed altitude to find ideal (unrefracted) altitude ("tel to sky")
 */
double calc_atm_refract_deg(double alt_deg, double press_kpa, double temp_c)
{
  double coeff = alt_deg + 10.3/(alt_deg+5.11);
  double R = (press_kpa/101.0) * (283.0/(273.0+temp_c)) * 1.02 / tan(coeff*ONEPI/180.0);
  if (R < 0.0)
    return 0.0;
  return R / 60.0;
}
