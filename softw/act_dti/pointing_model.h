#ifndef __POINTING_MODEL_H__
#define __POINTING_MODEL_H__

#include <math.h>
#include <act_site.h>

#define POINTING_APPLY_IH(ha_h,dec_d,coeff) \
  ha_h += coeff/54000.0;

#define POINTING_APPLY_ID(ha_h,dec_d,coeff) \
  dec_d += coeff/3600.0;

#define POINTING_APPLY_NP(ha_h,dec_d,coeff) \
  if (((dec_d>89.5) && (dec_d<=90)) || ((dec_d>-90.5) && (dec_d<=-90))) \
    ha_h += coeff*114.58865012931011/54000.0; \
  else if (((dec_d<-89.5) && (dec_d>=-90)) || ((dec_d<90.5) && (dec_d>=90))) \
    ha_h += coeff*-114.58865012931011/54000.0; \
  else \
    ha_h += coeff*tan(dec_d*ONEPI/180.0)/54000.0;

#define POINTING_APPLY_CH(ha_h,dec_d,coeff) \
  if (((dec_d>89.5) && (dec_d<=90)) || ((dec_d>-90.5) && (dec_d<=-90))) \
    ha_h += coeff*114.59301348013082/54000.0; \
  else if (((dec_d<-89.5) && (dec_d>=-90)) || ((dec_d<90.5) && (dec_d>=90))) \
    ha_h += coeff*-114.59301348013082/54000.0; \
  else \
    ha_h += coeff/cos(dec_d*ONEPI/180.0)/54000.0;
  
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

#define POINTING_APPLY_HDCH(ha_h,dec_d,coeff) \
  dec_d += coeff*cos(2*ha_h*ONEPI/12.0) / 3600.0;
  
#define POINTING_APPLY_HDCD2(ha_h,dec_d,coeff) \
  dec_d += coeff*cos(2*dec_d*ONEPI/180.0) / 3600.0;

#define POINTING_APPLY_HDCD4(ha_h,dec_d,coeff) \
  dec_d += coeff*cos(4*dec_d*ONEPI/180.0) / 3600.0;

#define POINTING_APPLY_HDSD5(ha_h,dec_d,coeff) \
  dec_d += coeff*sin(5*dec_d*ONEPI/180.0) / 3600.0;

#define POINTING_APPLY_HDSD5(ha_h,dec_d,coeff) \
  dec_d += coeff*sin(5*dec_d*ONEPI/180.0) / 3600.0;

#define POINTING_APPLY_HDSD6(ha_h,dec_d,coeff) \
  dec_d += coeff*sin(6*dec_d*ONEPI/180.0) / 3600.0;

#define POINTING_APPLY_HHSH5(ha_h,dec_d,coeff) \
  dec_d += coeff*sin(5*ha_h*ONEPI/12.0) / 3600.0;
  
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


/*
model 2013/12/04

#define CH      -720.84
#define NP     -1241.37
#define ID        53.12
#define HDCD2   -132.10
#define HDSD5    -93.92
 
 #define POINTING_MODEL_FORWARD(ha_h,dec_d) \
  POINTING_APPLY_CH(ha_h,dec_d,-CH); \
  POINTING_APPLY_NP(ha_h,dec_d,-NP); \
  POINTING_APPLY_ID(ha_h,dec_d,-ID); \
  POINTING_APPLY_HDCD2(ha_h,dec_d,-HDCD2); \
  POINTING_APPLY_HDSD5(ha_h,dec_d,-HDSD5);

#define POINTING_MODEL_REVERSE(ha_h,dec_d) \
  POINTING_APPLY_HDSD5(ha_h,dec_d,HDSD5); \
  POINTING_APPLY_HDCD2(ha_h,dec_d,HDCD2); \
  POINTING_APPLY_ID(ha_h,dec_d,ID); \
  POINTING_APPLY_NP(ha_h,dec_d,NP); \
  POINTING_APPLY_CH(ha_h,dec_d,CH); \

#define PRINT_MODEL(str) \
  sprintf(str, "(CH : %.2f) => (NP : %.2f) => (ID : %0.2f) => (HDCD2 : %0.2f) => (HDSD5 : %0.2f)", CH, NP, ID, HDCD2, HDSD5);
*/


// model 2013/12/04 (new)

#define ID      -112.9411
#define IH      +737.8942
#define NP      +816.6950
#define PDD     -199.4697

#define POINTING_MODEL_FORWARD(ha_h, dec_d) \
  POINTING_APPLY_ID(ha_h,dec_d,ID); \
  POINTING_APPLY_IH(ha_h,dec_d,IH); \
  POINTING_APPLY_NP(ha_h,dec_d,NP); \
  POINTING_APPLY_PDD(ha_h,dec_d,PDD);

#define POINTING_MODEL_REVERSE(ha_h, dec_d) \
  POINTING_APPLY_PDD(ha_h,dec_d,-PDD); \
  POINTING_APPLY_NP(ha_h,dec_d,-NP); \
  POINTING_APPLY_IH(ha_h,dec_d,-IH); \
  POINTING_APPLY_ID(ha_h,dec_d,-ID);

#define PRINT_MODEL(str) \
  sprintf(str, "(ID : %.2f) => (IH : %.2f) => (NP : %0.2f) => (PDD : %0.2f)", ID, IH, NP, PDD);


/*
// model 2014/01/19

#define IH      774.88
#define ID     -100.44
#define NP      830.60
#define PDD    -279.75
#define HHSH5     7.12

#define POINTING_MODEL_FORWARD(ha_h, dec_d) \
  POINTING_APPLY_IH(ha_h,dec_d,IH); \
  POINTING_APPLY_ID(ha_h,dec_d,ID); \
  POINTING_APPLY_NP(ha_h,dec_d,NP); \
  POINTING_APPLY_PDD(ha_h,dec_d,PDD); \
  POINTING_APPLY_HHSH5(ha_h,dec_d,HHSH5);

#define POINTING_MODEL_REVERSE(ha_h, dec_d) \
  POINTING_APPLY_HHSH5(ha_h,dec_d,-HHSH5); \
  POINTING_APPLY_PDD(ha_h,dec_d,-PDD); \
  POINTING_APPLY_NP(ha_h,dec_d,-NP); \
  POINTING_APPLY_ID(ha_h,dec_d,-ID); \
  POINTING_APPLY_IH(ha_h,dec_d,-IH);
  
#define PRINT_MODEL(str) \
  sprintf(str, "(IH : %.2f) => (ID : %.2f) => (NP : %0.2f) => (PDD : %0.2f) => (HHSH5 : %0.2f)", ID, IH, NP, PDD, HHSH5);
*/

// model 2014/01/20

/*#define ID      466.5248
#define IH      756.2468
#define NP      817.0029
#define HDCH   -476.0393
#define HHSH5   -99.8700

#define POINTING_MODEL_FORWARD(ha_h, dec_d) \
  POINTING_APPLY_ID(ha_h,dec_d,ID); \
  POINTING_APPLY_IH(ha_h,dec_d,IH); \
  POINTING_APPLY_NP(ha_h,dec_d,NP); \
  POINTING_APPLY_HDCH(ha_h,dec_d,HDCH);
  POINTING_APPLY_HHSH5(ha_h,dec_d,HHSH5);

#define POINTING_MODEL_REVERSE(ha_h, dec_d) \
  POINTING_APPLY_HHSH5(ha_h,dec_d,-HHSH5); \
  POINTING_APPLY_HDCH(ha_h,dec_d,-HDCH); \
  POINTING_APPLY_NP(ha_h,dec_d,-NP); \
  POINTING_APPLY_IH(ha_h,dec_d,-IH); \
  POINTING_APPLY_ID(ha_h,dec_d,-ID);

#define PRINT_MODEL(str) \
  sprintf(str, "(ID : %.2f) => (IH : %.2f) => (NP : %0.2f) => (HDCH : %0.2f) => (HHSH5 : %0.2f)", ID, IH, NP, HDCH, HHSH5);
*/

#endif
