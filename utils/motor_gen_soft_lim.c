#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <act_site.h>
#include <motor_defs.h>

static int lim_steps_W(long int dec_steps, double lim_alt_d);
static int lim_steps_E(long int dec_steps, double lim_alt_d);
static long int bisect_alt(double dec_rad, long int ha_steps_low, long int ha_steps_hi, double lim_alt_d);
static void motor_coord(long int ha_steps, long int dec_steps, double *ha_rad, double *dec_rad);
static double calc_alt(double ha_rad, double dec_rad);


int main(int argc, char **argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "Invalid usage. Please specify the software altitude limit (in degrees)\n");;
    return 1;
  }
  float alt_lim_d;
  int ret = sscanf(argv[1], "%f", &alt_lim_d);
  if (ret != 1)
    fprintf(stderr, "Error parsing command line arguments: \"%s\" is not a valid number.\n", argv[1]);
  else if ((alt_lim_d > 90.0) || (alt_lim_d < -90.0))
  {
    fprintf(stderr, "Invalid altitude specified: %f\nAltitude must be between -90 and 90 degrees.\n", alt_lim_d);
    ret = 0;
  }
  if (ret != 1)
    return 1;
  static long int lim_W[MOTOR_STEPS_N_LIM], lim_E[MOTOR_STEPS_N_LIM];
  long int i, start_idx = -1;
  for (i=0; i<MOTOR_STEPS_N_LIM; i++)
  {
//    if (i % 1000 == 0)
//      fprintf(stderr, "Processing declination %ld steps.\n", i);
    lim_W[i] = lim_steps_W(i, alt_lim_d);
    lim_E[i] = lim_steps_E(i, alt_lim_d);
    if (((lim_W[i] > 0) || (lim_E[i] < MOTOR_STEPS_E_LIM)) && (start_idx < 0))
    {
      fprintf(stderr, "Starting index %ld\n", i);
      start_idx = i;
    }
  }
  printf("#define TEL_ALT_LIM_DEG           %f\n", alt_lim_d);
  printf("#define TEL_ALT_LIM_MIN_DEC_STEPS %ld\n\n", start_idx);
  printf("static const int G_tel_alt_lim_W_steps[%ld] = { %ld", MOTOR_STEPS_N_LIM-start_idx, lim_W[start_idx]);
  for (i=start_idx+1; i<MOTOR_STEPS_N_LIM; i++)
    printf(", %ld", lim_W[i]);
  printf(" };\n\n");
  printf("static const int G_tel_alt_lim_E_steps[%ld] = { %ld", MOTOR_STEPS_N_LIM-start_idx, lim_E[start_idx]);
  for (i=start_idx+1; i<MOTOR_STEPS_N_LIM; i++)
    printf(", %ld", lim_E[i]);
  printf(" };\n\n");
  return 0;
}

static int lim_steps_W(long int dec_steps, double lim_alt_d)
{
  long int ha_steps_low = 0, ha_steps_hi = MOTOR_STEPS_E_LIM/2;
  double ha_rad_low, ha_rad_hi, dec_rad;
  motor_coord(ha_steps_low, dec_steps, &ha_rad_low, &dec_rad);
  motor_coord(ha_steps_hi, dec_steps, &ha_rad_hi, &dec_rad);
  double alt_low_d = calc_alt(ha_rad_low, dec_rad) * 180.0 / ONEPI;
  double alt_hi_d = calc_alt(ha_rad_hi, dec_rad) * 180.0 / ONEPI;
  if (alt_low_d > lim_alt_d)
    return 0;
  if (alt_hi_d < lim_alt_d)
    return MOTOR_STEPS_E_LIM;
  if (alt_low_d >= alt_hi_d)
  {
    fprintf(stderr, "Error finding limit for dec steps %ld. Low altitude is greater than high altitude.\n", dec_steps);
    return MOTOR_STEPS_E_LIM;
  }
  return bisect_alt(dec_rad, ha_steps_low, ha_steps_hi, lim_alt_d);
}

static int lim_steps_E(long int dec_steps, double lim_alt_d)
{
  long int ha_steps_low = MOTOR_STEPS_E_LIM, ha_steps_hi = MOTOR_STEPS_E_LIM/2;
  double ha_rad_low, ha_rad_hi, dec_rad;
  motor_coord(ha_steps_low, dec_steps, &ha_rad_low, &dec_rad);
  motor_coord(ha_steps_hi, dec_steps, &ha_rad_hi, &dec_rad);
  double alt_low_d = calc_alt(ha_rad_low, dec_rad) * 180.0 / ONEPI;
  double alt_hi_d = calc_alt(ha_rad_hi, dec_rad) * 180.0 / ONEPI;
  if (alt_low_d > lim_alt_d)
    return MOTOR_STEPS_E_LIM;
  if (alt_hi_d < lim_alt_d)
    return 0;
  if (alt_low_d >= alt_hi_d)
  {
    fprintf(stderr, "Error finding limit for dec steps %ld. Low altitude is greater than high altitude.\n", dec_steps);
    return 0;
  }
  return bisect_alt(dec_rad, ha_steps_low, ha_steps_hi, lim_alt_d);
}

static long int bisect_alt(double dec_rad, long int ha_steps_low, long int ha_steps_hi, double lim_alt_d)
{
  if (abs(ha_steps_low - ha_steps_hi) <= 1)
    return ha_steps_low;
  long int mid_steps = (ha_steps_low + ha_steps_hi) / 2;
  double mid_ha_rad;
  motor_coord(mid_steps, 0, &mid_ha_rad, NULL);
  double mid_alt_d = calc_alt(mid_ha_rad, dec_rad) * 180.0 / ONEPI;
  if (mid_alt_d == lim_alt_d)
    return mid_steps;
  if (mid_alt_d < lim_alt_d)
    return bisect_alt(dec_rad, mid_steps, ha_steps_hi, lim_alt_d);
  return bisect_alt(dec_rad, ha_steps_low, mid_steps, lim_alt_d);
}

static void motor_coord(long int ha_steps, long int dec_steps, double *ha_rad, double *dec_rad)
{
  if (ha_rad != NULL)
    (*ha_rad) = ((double) (MOTOR_LIM_E_MSEC - MOTOR_LIM_W_MSEC) * ha_steps / MOTOR_STEPS_E_LIM + MOTOR_LIM_W_MSEC) / 3600000.0 / 12.0 * ONEPI;
  if (dec_rad != NULL)
    (*dec_rad) = ((double) (MOTOR_LIM_N_ASEC - MOTOR_LIM_S_ASEC) * dec_steps / MOTOR_STEPS_N_LIM + MOTOR_LIM_S_ASEC) / 3600.0 / 180.0 * ONEPI;
}

static double calc_alt(double ha_rad, double dec_rad)
{
  static const double lat_rad = LATITUDE * ONEPI /180.0;
  return asin(sin(lat_rad)*sin(dec_rad) + cos(lat_rad)*cos(dec_rad)*cos(ha_rad));
}
