#ifndef ACT_TIMECOORD_H
#define ACT_TIMECOORD_H

#ifdef __cplusplus
extern "C"{
#endif
  

/*! \name Time, Date and Coordinate structures
 *  \brief Structures to consolidate time, date and coordinate information and to enforce a particular format.
 *  \{
 */
//! Time struct
struct timestruct
{
  //! Hours since midnight (0-23).
  unsigned char hours;
  //! Minutes (0-59).
  unsigned char minutes;
  //! Seconds (0-60).
  unsigned char seconds;
  //! Milli-seconds (0-999).
  unsigned short milliseconds;
};

//! Date struct
struct datestruct
{
  //! Day of the month. 0 is the first day of the month, 1 is the second day of the month, etc.
  unsigned char day;
  //! Month of the year. 0 is January, 1 is February, etc.
  unsigned char month;
  //! Years since 0 AD.
  short year;
};

//! Right Ascension struct
struct rastruct
{
  //! Hours since 0h (0-23).
  unsigned char hours;
  //! Minutes (0-59).
  unsigned char minutes;
  //! Seconds (0-60).
  unsigned char seconds;
  //! Milli-seconds (0-999).
  unsigned short milliseconds;  
};

//! Hour angle struct
struct hastruct
{
  //! Hours West of meridian (-12-12) - the other elements must have the same sign as hours.
  char hours;
  //! Minutes (0-59).
  char minutes;
  //! Seconds (0-59).
  char seconds;
  //! Milli-seconds (0-999).
  short milliseconds;  
};

//! Declination struct
struct decstruct
{
  //! Degrees North of equator (-90-90) - the other elements must have the same sign as degrees.
  char degrees;
  //! Arcminutes (0-59)
  char amin;
  //! Arcseconds (0-59)
  char asec;
};

//! Altitude struct
struct altstruct
{
  //! Degrees higher than horizon (-90-90) - the other elements must have the same sign as degrees.
  short degrees;
  //! Arcminutes (0-59)
  char amin;
  //! Arcseconds (0-59)
  char asec;
};

//! Azimuth struct
struct azmstruct
{
  //! Degrees East of due North (0-360).
  unsigned short degrees;
  //! Arcminutes (0-59)
  unsigned char amin;
  //! Arcseconds (0-59)
  unsigned char asec;
};
/*! \} */

double convert_RAD_DEG (double radians);
double convert_DEG_RAD (double degrees);
double convert_H_RAD (double hours);
double convert_RAD_H (double radians);
double convert_DEG_H (double degrees);
double convert_H_DEG (double hours);
double convert_DMS_D_dec(struct decstruct *dec);
double convert_DMS_D_alt(struct altstruct *alt);
double convert_DMS_D_azm(struct azmstruct *azm);
long int convert_DMS_ASEC_dec(struct decstruct *dec);
long int convert_DMS_ASEC_alt(struct altstruct *alt);
long int convert_DMS_ASEC_azm(struct azmstruct *azm);
void convert_D_DMS_dec(double frac_deg, struct decstruct *dec);
void convert_D_DMS_alt(double frac_deg, struct altstruct *alt);
void convert_D_DMS_azm(double frac_deg, struct azmstruct *azm);
void convert_ASEC_DMS_dec(long integ_asec, struct decstruct *dec);
void convert_ASEC_DMS_alt(long integ_asec, struct altstruct *alt);
void convert_ASEC_DMS_azm(long integ_asec, struct azmstruct *azm);
double convert_HMSMS_H_time(struct timestruct *time);
double convert_HMSMS_H_ra(struct rastruct *ra);
double convert_HMSMS_H_ha(struct hastruct *ha);
long int convert_HMSMS_MS_time(struct timestruct *time);
long int convert_HMSMS_MS_ra(struct rastruct *ra);
long int convert_HMSMS_MS_ha(struct hastruct *ha);
void convert_H_HMSMS_time(double frac_hours, struct timestruct *time);
void convert_H_HMSMS_ra(double frac_hours, struct rastruct *ra);
void convert_H_HMSMS_ha(double frac_hours, struct hastruct *ha);
void convert_MS_HMSMS_time(long int integ_millisec, struct timestruct *time);
void convert_MS_HMSMS_ra(long int integ_millisec, struct rastruct *ra);
void convert_MS_HMSMS_ha(long int integ_millisec, struct hastruct *ha);
char *time_to_str(struct timestruct *time);
char *date_to_str(struct datestruct *date);
char *ra_to_str(struct rastruct *ra);
char *ha_to_str(struct hastruct *ha);
char *dec_to_str(struct decstruct *dec);
char *alt_to_str(struct altstruct *alt);
char *azm_to_str(struct azmstruct *azm);

#ifdef __cplusplus
}
#endif

#endif
