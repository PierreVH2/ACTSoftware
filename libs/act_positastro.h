/*!
 * \file act_common.h
 * \brief Definitions for functions (mostly astronomical/mathematical) that are used by a number of ACT programmes.
 * \author Pierre van Heerden
 */

#ifndef ACT_POSITASTRO_H
#define ACT_POSITASTRO_H

#include "act_ipc.h"
#include "act_timecoord.h"

#ifdef __cplusplus
extern "C"{
#endif

char isLeapYear(short year);
unsigned char daysInMonth(struct datestruct *date);
void check_date_time_ranges(struct timestruct *time, struct datestruct *date);
void check_systime_discrep(struct datestruct *date, struct timestruct *sys_time, struct timestruct *real_time);
void check_ra_ha_dec_ranges(struct rastruct *ra, struct hastruct *ha, struct decstruct *dec);
void check_alt_azm_ranges(struct altstruct *alt, struct azmstruct *azm);
void convert_EQUI_ALTAZ (struct hastruct *ha, struct decstruct *dec, struct altstruct *alt, struct azmstruct *azm);
void convert_ALTAZ_EQUI (struct altstruct *alt, struct azmstruct *azm, struct hastruct *ha, struct decstruct *dec);
void calc_RA (struct hastruct *ha, struct timestruct *sidt, struct rastruct *ra);
void calc_HAngle (struct rastruct *ra, struct timestruct *sidt, struct hastruct *ha);
void calc_UniT (struct timestruct *loct, struct timestruct *unit);
void calc_LocT (struct timestruct *unit, struct timestruct *loct);
double calc_GJD (struct datestruct *unid, struct timestruct *unit);
double calc_SidT (double jd);
void calc_sun (double jd, struct rastruct *targ_ra, struct decstruct *targ_dec, struct rastruct *sun_ra, struct decstruct *sun_dec, double *hjd);
void calc_moon_pos (double JulCen, struct rastruct *moon_ra, struct decstruct *moon_dec);
double calc_moon_illum (struct rastruct *moon_ra, struct decstruct *moon_dec, struct rastruct *sun_ra, struct decstruct *sun_dec);
double calc_airmass(struct altstruct *alt);
void precess_coord(struct rastruct *ra_in, struct decstruct *dec_in, float epoch_in, float epoch_out, struct rastruct *ra_out, struct decstruct *dec_out);
void corr_atm_refract_tel_sky(struct altstruct *alt);
void corr_atm_refract_sky_tel(struct altstruct *alt);
void corr_atm_refract_tel_sky_equat(struct hastruct *ha, struct decstruct *dec);
void corr_atm_refract_sky_tel_equat(struct hastruct *ha, struct decstruct *dec);
double calc_atm_refract_deg(double alt_deg, double press_kpa, double temp_c);

#ifdef __cplusplus
}
#endif
#endif
