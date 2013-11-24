#ifndef __POINTING_MODEL_H__
#define __POINTING_MODEL_H__

#include <math.h>
#include <act_site.h>

#define IH       -817.6941
#define PHD      -338.4163
#define PDD      -209.3593
#define PDD2     -329.7665
#define PDD3     -142.6720
#define NP       -707.6888
#define HDSD6    -102.2632

#define POINTING_APPLY_IH(ha_h,dec_d,coeff) \
  ha_h += coeff/54000.0;
  
#define POINTING_APPLY_ID(ha_h,dec_d,coeff) \
  dec_d += coeff/3600.0;

#define POINTING_APPLY_NP(ha_h,dec_d,coeff) \
  if (((dec_d>89.5) && (dec_d<=90)) && ((dec_d<-90.5) && (dec_d>=-90))) \
    ha_h += coeff*114.58865012931011/54000.0; \
  else if (((dec_d>-89.5) && (dec_d<=-90)) && ((dec_d<90.5) && (dec_d>=90))) \
    ha_h += coeff*-114.58865012931011/54000.0; \
  else \
    ha_h += coeff*tan(dec_d*ONEPI/180.0)/54000.0;

#define POINTING_APPLY_PHH(ha_h,dec_d,coeff) \
  ha_h += coeff*(ha_h*ONEPI/12.0)/54000.0;

#define POINTING_APPLY_PDD(ha_h,dec_d,coeff) \
  dec_d += coeff*(dec_d*ONEPI/180.0)/3600.0;

#define POINTING_APPLY_PDD2(ha_h,dec_d,coeff) \
  dec_d += coeff*pow(dec_d*ONEPI/180.0,2)/3600.0;

#define POINTING_APPLY_PDD3(ha_h,dec_d,coeff) \
  dec_d += coeff*pow(dec_d*ONEPI/180.0,3)/3600.0;

#define POINTING_APPLY_PHD(ha_h,dec_d,coeff) \
  ha_h += coeff*(dec_d*ONEPI/180.0)/54000.0;

#define POINTING_APPLY_HDSD5(ha_h,dec_d,coeff) \
  double dec_rad = dec_d*ONEPI/180.0; \
  dec_d += coeff*sin(5*dec_rad) / 3600.0;

#define POINTING_APPLY_HDSD6(ha_h,dec_d,coeff) \
  double dec_rad = dec_d*ONEPI/180.0; \
  dec_d += coeff*sin(6*dec_rad) / 3600.0;

#define POINTING_APPLY_HHCH(ha_h,dec_d,coeff) \
  double ha_rad = dec_d*ONEPI/12.0; \
  ha_h += coeff*cos(ha_rad) / 54000.0;

#define POINTING_APPLY_HZSZ5(ha_h,dec_d,coeff) \
  double ha_rad = ha_h*ONEPI/12.0; \
  double dec_rad = dec_d*ONEPI/180.0; \
  double lat_rad = LATITUDE*ONEPI/180.0; \
  double alt_rad = asin(sin(lat_rad)*sin(dec_rad) + cos(lat_rad)*cos(dec_rad)*cos(ha_rad)); \
  double azm_rad = atan2(sin(-ha_rad) * cos(dec_rad) / cos(alt_rad), (sin(dec_rad)-sin(lat_rad)*sin(alt_rad)) / cos(lat_rad) / cos(alt_rad)); \
  alt_rad -= coeff*sin(5*(ONEPI/2 - alt_rad)) * ONEPI / 648000; \
  dec_rad = asin(sin(lat_rad)*sin(alt_rad) + cos(lat_rad)*cos(alt_rad)*cos(azm_rad)); \
  ha_rad = atan2(-sin(azm_rad)*cos(alt_rad)/cos(dec_rad), (sin(alt_rad) - sin(dec_rad)*sin(lat_rad))/cos(dec_rad)/cos(lat_rad)); \
  ha_h = ha_rad*12.0/ONEPI; \
  dec_d = dec_rad*180.0/ONEPI;

#define POINTING_APPLY_HHCH7(ha_h,dec_d,coeff) \
  ha_h += coeff*cos(7*(ha_h*ONEPI/12.0)) / 54000.0;


#define POINTING_MODEL_FORWARD(ha_h,dec_d) \
  POINTING_APPLY_IH(ha_h,dec_d,-IH); \
  POINTING_APPLY_PHD(ha_h,dec_d,-PHD); \
  POINTING_APPLY_PDD(ha_h,dec_d,-PDD); \
  POINTING_APPLY_PDD2(ha_h,dec_d,-PDD); \
  POINTING_APPLY_PDD3(ha_h,dec_d,-PDD); \
  POINTING_APPLY_NP(ha_h,dec_d,-NP); \
  POINTING_APPLY_HDSD6(ha_h,dec_d,-HDSD6);

#define POINTING_MODEL_REVERSE(ha_h,dec_d) \
  POINTING_APPLY_HDSD6(ha_h,dec_d,HDSD6); \
  POINTING_APPLY_NP(ha_h,dec_d,NP); \
  POINTING_APPLY_PDD3(ha_h,dec_d,PDD); \
  POINTING_APPLY_PDD2(ha_h,dec_d,PDD); \
  POINTING_APPLY_PDD(ha_h,dec_d,PDD); \
  POINTING_APPLY_PHD(ha_h,dec_d,PHD); \
  POINTING_APPLY_IH(ha_h,dec_d,IH);

#define PRINT_MODEL(str) \
  sprintf(str, "(IH : %.2f) => (PHD : %.2f) => (PDD : %.2f) => (PDD2 : %.2f) => (PDD3 : %.2f) => (NP : %.2f) => (HDSD6 : %.2f)", IH, PHD, PDD, PDD2, PDD3, NP, HDSD6);

#endif
