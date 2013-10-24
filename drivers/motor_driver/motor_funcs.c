#include <linux/kernel.h>
#include <act_plc/act_plc.h>
#include "motor_funcs.h"
#include "motor_defs.h"
#include "motor_intfce.h"

#define TRUE  1
#define FALSE 0

#define TOLERANCE_DEC_ASEC 5
#define TOLERANCE_RA_MSEC  333
#define TEL_FLOP_RA_MSEC   75000
#define TEL_FLOP_DEC_ASEC  3000

struct tel_goto_params
{
  unsigned long int max_speed;
  unsigned char is_near_target;
  unsigned char direction;
  long int targ_ha_msec;
  long int targ_dec_asec;
  long int track_timer;
  char goto_end;
};

struct tel_init_params
{
  unsigned long max_speed;
  unsigned char direction;
  long int targ_ha_msec;
  long int targ_dec_asec;
  unsigned char stage;
};

static unsigned char calc_direction(long int targ_ha_msec, long int targ_dec_asec);
static char calc_is_near_target(unsigned char direction, long int targ_ha_msec, long int targ_dec_asec);
static char check_soft_lims(unsigned char dir);
#if defined(ENCODER_DIAG) || defined(POINT_W_MOTORS)
static void update_motor_coord(void);
#endif
#if defined(ENCODER_DIAG) || !defined(POINT_W_MOTORS)
static void update_encod_coord(void);
#endif
static void update_coord(void);
static char check_preinit_to_S_lim(void);
static char check_init_W(void);
static char check_init_E(void);
static char check_midinit_merid(void);
static char check_init_N(void);
static char check_init_S(void);

static char G_ramp_reqd = FALSE;
static long int G_lim_W_msec = DEF_TEL_LIM_W_MSEC;
static long int G_lim_E_msec = DEF_TEL_LIM_E_MSEC;
static long int G_lim_N_asec = DEF_TEL_LIM_N_ASEC;
static long int G_lim_S_asec = DEF_TEL_LIM_S_ASEC;
#if defined(ENCODER_DIAG)
  static long int G_cur_motor_ha_msec;
  static long int G_cur_motor_dec_asec;
  static long int G_motor_ref_S = 563131;
  static long int G_motor_ref_E = 1155788;
  static long int G_cur_encod_ha_msec;
  static long int G_cur_encod_dec_asec;
  static long int G_encod_ref_S = 298207;
  static long int G_encod_ref_E = 417151;
  #if defined(POINT_W_MOTORS)
    #define CUR_HA_MSEC G_cur_motor_ha_msec
    #define CUR_DEC_ASEC G_cur_motor_dec_asec
  #else
    #define CUR_HA_MSEC G_cur_encod_ha_msec
    #define CUR_DEC_ASEC G_cur_encod_dec_asec
  #endif
#elif defined(POINT_W_MOTORS)
  static long int G_motor_ref_S = 563131;
  static long int G_motor_ref_E = 1155788;
  static long int G_cur_motor_ha_msec;
  static long int G_cur_motor_dec_asec;
  #define CUR_HA_MSEC G_cur_motor_ha_msec
  #define CUR_DEC_ASEC G_cur_motor_dec_asec
#else
  static long int G_encod_ref_S = 298207;
  static long int G_encod_ref_E = 417151;
  static long int G_cur_encod_ha_msec;
  static long int G_cur_encod_dec_asec;
  #define CUR_HA_MSEC G_cur_encod_ha_msec
  #define CUR_DEC_ASEC G_cur_encod_dec_asec
#endif

// Addtional telescope limits
// tel_alt_limits specifies the maximum allowable hour-angle in minutes for a declination within the range corresponding to {-10-0, 0-10, 10-20, 20-30, 30-40, 40-50} in degrees
// in order for the telescope to remain above 10 degrees in altitude
static const int tel_alt_limits[6] = {301, 275, 244, 204, 143, 53};
#define TEL_ALT_LIM_MIN_DEC -10
#define TEL_ALT_LIM_DEC_INC 10

static struct tel_goto_params G_goto_params;
static struct tel_init_params G_init_params;
static unsigned long G_track_rate = 0;

char start_goto(long int targ_ha_msec, long int targ_dec_asec, unsigned long max_speed)
{
  unsigned char lims;
  G_goto_params.targ_ha_msec = targ_ha_msec;
  G_goto_params.targ_dec_asec = targ_dec_asec;
  G_goto_params.direction = calc_direction(G_goto_params.targ_ha_msec, G_goto_params.targ_dec_asec);
  G_goto_params.is_near_target = calc_is_near_target(G_goto_params.direction, G_goto_params.targ_ha_msec, G_goto_params.targ_dec_asec);
  G_goto_params.max_speed = max_speed;
  G_goto_params.goto_end = FALSE;
  G_goto_params.track_timer = 0;
  
  if (G_goto_params.direction == 0)
    return FALSE;

  lims = get_limits();
  if ((lims & G_goto_params.direction) != 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Received goto command. Limit switches in desired direction have been triggered.\n");
    return FALSE;
  }
  if (G_goto_params.max_speed < SLEW_RATE)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Attempting slew at unsafe speed. Will slew at SLEW_RATE\n");
    G_goto_params.max_speed = SLEW_RATE;
  }
  G_ramp_reqd = G_goto_params.max_speed < MIN_RATE;
  return start_move(G_goto_params.direction, G_goto_params.max_speed);
}

char end_goto(void)
{
  printk(KERN_INFO PRINTK_PREFIX "Goto end requested\n");
  if (G_goto_params.max_speed < MIN_RATE)
  {
    printk(KERN_INFO PRINTK_PREFIX "Activating ramp required.\n");
    G_ramp_reqd = TRUE;
    change_speed(MIN_RATE);
  }
  G_goto_params.goto_end = TRUE;
  G_goto_params.is_near_target = TRUE;
  return FALSE;
}

char check_goto(unsigned long track_incr_msec)
{
  unsigned char new_dir;

  update_coord();
  if (get_limits() & G_goto_params.direction)
  {
    printk(KERN_INFO PRINTK_PREFIX "Limit switch triggered while performing goto (%d). Stopping.\n", get_limits());
    stop_move();
    return -1;
  }
  if (check_soft_lims(G_goto_params.direction))
  {
    printk(KERN_INFO PRINTK_PREFIX "Attempted slew beyond safe limits (%ld %ld)\n", (CUR_HA_MSEC/360000)%24, (CUR_DEC_ASEC/3600)%180);
    stop_move();
    return -1;
  }

  if (G_track_rate > 0)
  {
    if (G_goto_params.track_timer >= 0)
      G_goto_params.track_timer += track_incr_msec;
    else
    {
      printk(KERN_INFO PRINTK_PREFIX "Trying to catch up with tracking.\n");
      G_goto_params.track_timer -= track_incr_msec;
      new_dir = calc_direction(G_goto_params.targ_ha_msec - G_goto_params.track_timer, CUR_DEC_ASEC);
      if (new_dir != DIR_WEST_MASK)
      {
        printk(KERN_INFO PRINTK_PREFIX "Done catching up with tracking.\n");
        G_goto_params.direction = 0;
        stop_move();
        return TRUE;
      }
      printk(KERN_INFO PRINTK_PREFIX "Haven't caught up with tracking yet (%ld %ld).\n", G_goto_params.targ_ha_msec - G_goto_params.track_timer, CUR_HA_MSEC);
      return FALSE;
    }
  }
  
  if (G_goto_params.goto_end)
  {
    char move_ended = end_move();
    printk(KERN_INFO PRINTK_PREFIX "Goto end active: %hd %lu\n", move_ended, G_track_rate);
    if (!move_ended)
    {
      printk(KERN_INFO PRINTK_PREFIX "Move not completed yet.\n");
      return FALSE;
    }
    if ((G_track_rate == 0) || ((G_goto_params.direction & DIR_HA_MASK) != 0))
    {
      printk(KERN_INFO PRINTK_PREFIX "Move complete.\n");
      G_goto_params.direction = 0;
      stop_move();
      return TRUE;
    }
    printk(KERN_INFO PRINTK_PREFIX "Move complete, need to catch up with tracking\n");
    G_goto_params.goto_end = FALSE;
    G_goto_params.targ_ha_msec = CUR_HA_MSEC;
    G_goto_params.targ_dec_asec = CUR_DEC_ASEC;
  }
  
  if (!G_goto_params.goto_end)
  {
    if ((calc_is_near_target(G_goto_params.direction, G_goto_params.targ_ha_msec, G_goto_params.targ_dec_asec)) && (G_goto_params.is_near_target == 0))
    {
      printk(KERN_INFO PRINTK_PREFIX "near target\n");
      G_goto_params.is_near_target = TRUE;
      change_speed(MIN_RATE);
      G_ramp_reqd = TRUE;
    }
    
    new_dir = calc_direction(G_goto_params.targ_ha_msec, G_goto_params.targ_dec_asec);
    printk(KERN_INFO PRINTK_PREFIX "New direction: %hhu\n", new_dir);
    if (new_dir == G_goto_params.direction)
    {
      printk(KERN_INFO PRINTK_PREFIX "No new direction\n");
      return FALSE;
    }
    
    if (new_dir != 0)
    {
      int ret;
      printk(KERN_INFO PRINTK_PREFIX "Need to move in remaining direction\n");
      stop_move();
      G_goto_params.direction = new_dir;
      G_goto_params.is_near_target = calc_is_near_target(G_goto_params.direction, G_goto_params.targ_ha_msec, G_goto_params.targ_dec_asec);
      if (!G_goto_params.is_near_target)
      {
	printk(KERN_INFO PRINTK_PREFIX "Need to ramp up for remaining direction\n");
	G_ramp_reqd = TRUE;
	ret = start_move(G_goto_params.direction, G_goto_params.max_speed);
      }
      else
      {
	printk(KERN_INFO PRINTK_PREFIX "Moving in remaining direction at minimum speed.\n");
	ret = start_move(G_goto_params.direction, MIN_RATE);
      }
      if (!ret)
      {
	printk(KERN_INFO PRINTK_PREFIX "Failed to start telescope motion.\n");
	return -1;
      }
      else
      {
	printk(KERN_INFO PRINTK_PREFIX "Started moving in remaining direction\n");
	return FALSE;
      }
    }
    
    stop_move();
    if (G_track_rate == 0)
    {
      printk(KERN_INFO PRINTK_PREFIX "Move complete.\n");
      G_goto_params.direction = 0;
      return TRUE;
    }
    
    G_goto_params.track_timer *= -1;
    if (!start_move(DIR_WEST_MASK, SET_RATE))
    {
      printk(KERN_INFO PRINTK_PREFIX "Failed to start telescope motion.\n");
      return -1;
    }
    else
    {
      printk(KERN_INFO PRINTK_PREFIX "Move/Goto complete - accounting for tracking loss\n");
      G_ramp_reqd = TRUE;
      return FALSE;
    }
  }
  printk(KERN_INFO PRINTK_PREFIX "check goto called but there's nothing to do.\n");
  return TRUE;
}

char start_init(long int lim_W_msec, long int lim_E_msec, long int lim_N_asec, long int lim_S_asec)
{
  G_lim_W_msec = lim_W_msec;
  G_lim_E_msec = lim_E_msec;
  G_lim_N_asec = lim_N_asec;
  G_lim_S_asec = lim_S_asec;
  G_init_params.max_speed = SLEW_RATE;
  G_init_params.direction = DIR_SOUTH_MASK;
  G_init_params.stage = 1;
  G_ramp_reqd = TRUE;
  return start_move(DIR_SOUTH_MASK, SLEW_RATE);
}

char check_init(void)
{
  char ret;
  switch (G_init_params.stage)
  {
    case 1:
    {
      ret = check_preinit_to_S_lim();
      if (ret > 0)
      {
	G_init_params.stage++;
	ret = 0;
      }
      break;
    }
    case 2:
    {
      ret = check_init_W();
      if (ret > 0)
      {
	G_init_params.stage++;
	ret = 0;
      }
      break;
    }
    case 3:
    {
      ret = check_init_E();
      if (ret > 0)
      {
	G_init_params.stage++;
        ret = 0;
      }
      break;
    }
    case 4:
    {
      ret = check_midinit_merid();
      if (ret > 0)
      {
	G_init_params.stage++;
        ret = 0;
      }
      break;
    }
    case 5:
    {
      ret = check_init_N();
      if (ret > 0)
      {
	G_init_params.stage++;
        ret = 0;
      }
      break;
    }
    case 6:
    {
      ret = check_init_S();
      if (ret > 0)
	G_init_params.stage = 0;
      break;
    }
    default:
    {
      printk(KERN_INFO PRINTK_PREFIX "Invalid initialisation stage number (%d). Cancelling initialisation.\n", G_init_params.stage);
      ret = -1;
    }
  }
  return ret;
}

char get_is_ramp_reqd(void)
{
  if (G_ramp_reqd != 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Ramp required.\n");
    G_ramp_reqd = FALSE;
    return TRUE;
  }
  return FALSE;
}

char set_track_rate(unsigned long new_rate)
{
  int ret = TRUE;
  printk(KERN_INFO PRINTK_PREFIX "set_track_rate %lu %lu\n", new_rate, G_track_rate);
  if (new_rate == G_track_rate)
    return TRUE;

  if ((G_goto_params.direction != 0) || (G_init_params.stage != 0))
  {
    G_track_rate = new_rate;
    return TRUE;
  }
  
  if (new_rate == 0)
  {
    stop_move();
    ret = TRUE;
  }
  else if (G_track_rate > 0)
    change_speed(new_rate);
  else
    ret = start_move(DIR_WEST_MASK, new_rate);
  if (!ret)
  {
    printk(KERN_INFO PRINTK_PREFIX "Failed to start/stop/change tracking.\n");
    return -1;
  }
  G_track_rate = new_rate;
  return TRUE;
}

unsigned long get_track_rate(void)
{
  return G_track_rate;
}

char check_track(void)
{
  if (G_track_rate == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "check_track called, but should not be tracking. Ignoring\n");
    return FALSE;
  }
  if ((G_init_params.stage != 0) || (G_goto_params.direction != 0))
    return FALSE;
  
  update_coord();
  if (get_limits() & DIR_WEST_MASK)
  {
    printk(KERN_INFO PRINTK_PREFIX "Western limit switch triggered while tracking. Stopping.\n");
    stop_move();
    return -1;
  }
  if (check_soft_lims(DIR_WEST_MASK))
  {
    printk(KERN_INFO PRINTK_PREFIX "Attempted track beyond safe limits (%ld %ld)\n", (CUR_HA_MSEC/3600000)%24, (CUR_DEC_ASEC/3600)%180);
    stop_move();
    return -1;
  }
  return TRUE;
}

void get_coords(long int *ha_msec, long int *dec_asec)
{
  update_coord();
  *ha_msec = CUR_HA_MSEC;
  *dec_asec = CUR_DEC_ASEC;
}

#if defined(ENCODER_DIAG)
void get_motor_coords(long int *ha_msec, long int *dec_asec)
{
  *ha_msec = G_motor_ha_msec;
  *dec_asec = G_motor_dec_asec;
}

void get_encod_coords(long int *ha_msec, long int *dec_asec)
{
  *ha_msec = G_encod_ha_msec;
  *dec_asec = G_encod_dec_asec;
}
#endif

void adj_pointing(long int new_ha_msec, long int new_dec_asec)
{
  G_lim_W_msec += new_ha_msec - CUR_HA_MSEC;
  G_lim_E_msec += new_ha_msec - CUR_HA_MSEC;

  G_lim_N_asec += new_dec_asec - CUR_DEC_ASEC;
  G_lim_S_asec += new_dec_asec - CUR_DEC_ASEC;
  update_coord();
}

static unsigned char calc_direction(long int targ_ha_msec, long int targ_dec_asec)
{
  unsigned char dir = 0;
  if ((targ_dec_asec-CUR_DEC_ASEC) * (targ_dec_asec < CUR_DEC_ASEC ? -1 : 1) > TOLERANCE_DEC_ASEC)
  {
    if (targ_dec_asec > CUR_DEC_ASEC)
      dir |= DIR_NORTH_MASK;
    else
      dir |= DIR_SOUTH_MASK;
  }
  if ((targ_ha_msec-CUR_HA_MSEC) * (targ_ha_msec < CUR_HA_MSEC ? -1 : 1) > TOLERANCE_RA_MSEC)
  {
    if (targ_ha_msec > CUR_HA_MSEC)
      dir |= DIR_WEST_MASK;
    else
      dir |= DIR_EAST_MASK;
  }
  return dir;
}

static char calc_is_near_target(unsigned char direction, long int targ_ha_msec, long int targ_dec_asec)
{
  if ((direction & (DIR_SOUTH_MASK | DIR_NORTH_MASK)) && ((targ_dec_asec - CUR_DEC_ASEC)*(targ_dec_asec < CUR_DEC_ASEC ? -1 : 1) < TEL_FLOP_DEC_ASEC))
    return TRUE;
  if ((direction & (DIR_WEST_MASK | DIR_EAST_MASK)) && ((targ_ha_msec-CUR_HA_MSEC)*(targ_ha_msec<CUR_HA_MSEC ? -1 : 1) < TEL_FLOP_RA_MSEC))
    return TRUE;
  return FALSE;
}

static char check_soft_lims(unsigned char dir)
{
  unsigned char idx_l, idx_u;
  int tel_ha_lim = 0;

  if (CUR_DEC_ASEC / 3600 > TEL_ALT_LIM_MIN_DEC)
  {
    idx_l = (CUR_DEC_ASEC - TEL_ALT_LIM_MIN_DEC*3600) / 36000;
    if (idx_l >= 5)
      tel_ha_lim = tel_alt_limits[5];
    else
    {
      idx_u = idx_l+1;
      tel_ha_lim = (tel_alt_limits[idx_u] - tel_alt_limits[idx_l]) * (CUR_DEC_ASEC - idx_l*36000+36000) / 36000 + tel_alt_limits[idx_l];
    }
    if (tel_ha_lim < abs(CUR_HA_MSEC/60000))
    {
      if (dir & DIR_NORTH_MASK)
        return TRUE;
      if ((dir & DIR_EAST_MASK) && (CUR_HA_MSEC < 0))
        return TRUE;
      if ((dir & DIR_WEST_MASK) && (CUR_HA_MSEC > 0))
        return TRUE;
    }
    return FALSE;
  }
  return FALSE;
}

#if defined(ENCODER_DIAG) || defined(POINT_W_MOTORS)
static void update_motor_coord(void)
{
  uint64_t tmp_div;

  tmp_div = (uint64_t)(G_lim_W_msec-G_lim_E_msec) * get_steps_ha();
  do_div(tmp_div, (long unsigned int)G_motor_ref_E);
  G_cur_motor_ha_msec = -(long int)tmp_div + G_lim_W_msec;

  tmp_div = (uint64_t)(G_lim_N_asec-G_lim_S_asec) * get_steps_dec();
  do_div(tmp_div, (long unsigned int)G_motor_ref_S);
  G_cur_motor_dec_asec = -(long int)tmp_div + G_lim_N_asec;
}
#endif

#if defined(ENCODER_DIAG) || !defined(POINT_W_MOTORS)
static void update_encod_coord(void)
{
  uint64_t tmp_div;
  
  tmp_div = (uint64_t)(G_lim_W_msec-G_lim_E_msec) * get_enc_ha_pulses();
  do_div(tmp_div, (long unsigned int)G_encod_ref_E);
  G_cur_encod_ha_msec = -(long int)tmp_div + G_lim_W_msec;
  
  tmp_div = (uint64_t)(G_lim_N_asec-G_lim_S_asec) * get_enc_ha_pulses();
  do_div(tmp_div, (long unsigned int)G_encod_ref_S);
  G_cur_encod_dec_asec = -(long int)tmp_div + G_lim_N_asec;
}
#endif

static void update_coord(void)
{
  #if defined(ENCODER_DIAG)
  update_motor_coord();
  update_encod_coord();
  #elif defined(POINT_W_MOTORS)
  update_motor_coord();
  #else
  update_encod_coord();
  #endif
}

// ??? get proper kernel message levels

static char check_preinit_to_S_lim(void)
{
  unsigned int dome_azm;

  if ((get_limits() & DIR_SOUTH_MASK) == 0)
    return 0;

  dome_azm = dome_azm_d();
  if ((dome_azm > 45) && (dome_azm < 315))
  {
    printk(KERN_INFO PRINTK_PREFIX "Cannot complete initialisation because dome is possibly in an unsafe azimuth (%d). Move dome due North.\n", dome_azm);
    return -1;
  }

  stop_move();
  G_ramp_reqd = TRUE;
  G_init_params.direction = DIR_WEST_MASK;
  return start_move(DIR_WEST_MASK, SLEW_RATE);
}

static char check_init_W(void)
{
  if ((get_limits() & DIR_WEST_MASK) == 0)
    return 0;

  stop_move();
  G_ramp_reqd = TRUE;
  G_init_params.direction = DIR_EAST_MASK;
  G_init_params.max_speed = SLEW_RATE;
  G_init_params.targ_ha_msec = G_lim_E_msec;
  #if defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   zero_ha_steps();
   G_cur_motor_ha_msec=G_lim_W_msec;
  #endif
  #if !defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   G_cur_encod_ha_msec=G_lim_W_msec;
  #endif
  return start_move(DIR_EAST_MASK, SLEW_RATE);
}

static char check_init_E(void)
{
  if ((get_limits() & DIR_EAST_MASK) == 0)
  {
    long int ha_msec, dec_asec;
    update_coord();
    get_coords(&ha_msec, &dec_asec);
    if ((calc_is_near_target(G_init_params.direction, G_init_params.targ_ha_msec, G_init_params.targ_dec_asec)) && (G_init_params.max_speed < MIN_RATE))
    {
      G_init_params.max_speed = MIN_RATE;
      G_ramp_reqd = TRUE;
      change_speed(MIN_RATE);
    }
    return 0;
  }

  stop_move();
  G_ramp_reqd = TRUE;
  G_init_params.direction = DIR_WEST_MASK;
  G_init_params.max_speed = SLEW_RATE;
  G_init_params.targ_ha_msec = 0;
  G_init_params.targ_dec_asec = CUR_DEC_ASEC;
  #if defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   G_motor_ref_E = get_steps_ha();
   G_cur_motor_ha_msec=G_lim_E_msec;
   printk(KERN_INFO PRINTK_PREFIX "Found Eastern limit at %ld motor steps\n", G_motor_ref_E);
  #endif
  #if !defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   G_encod_ref_E = get_enc_ha_pulses();
   G_cur_encod_ha_msec=G_lim_E_msec;
   printk(KERN_INFO PRINTK_PREFIX "Found Eastern limit at %ld encoder pulses\n", G_encod_ref_E);
  #endif
  return start_move(DIR_WEST_MASK, SLEW_RATE);
}

static char check_midinit_merid(void)
{
  if ((get_limits() & DIR_WEST_MASK) != 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error: Western limit switch triggered while parking telescope on meridian for declination calibration. Initialisation cancelled.\n");
    return -1;
  }

  update_coord();
  if (calc_is_near_target(G_init_params.direction, G_init_params.targ_ha_msec, G_init_params.targ_dec_asec))
  {
    if (G_init_params.max_speed < MIN_RATE)
    {
      G_ramp_reqd = TRUE;
      G_init_params.max_speed = MIN_RATE;
    }
    if (!end_move())
      return 0;
    else
    {
      G_ramp_reqd = TRUE;
      G_init_params.direction = DIR_NORTH_MASK;
      G_init_params.max_speed = SLEW_RATE;
      return start_move(DIR_NORTH_MASK, SLEW_RATE);
    }
  }
  return 0;
}

static char check_init_N(void)
{
  if ((get_limits() & DIR_NORTH_MASK) == 0)
    return 0;

  stop_move();
  G_ramp_reqd = TRUE;
  G_init_params.direction = DIR_SOUTH_MASK;
  G_init_params.max_speed = SLEW_RATE;
  G_init_params.targ_dec_asec = G_lim_S_asec;
  #if defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   zero_dec_steps();
   G_cur_motor_dec_asec=G_lim_N_asec;
  #endif
  #if !defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   G_cur_encod_dec_asec=G_lim_N_asec;
  #endif
  return start_move(DIR_SOUTH_MASK, SLEW_RATE);
}

static char check_init_S(void)
{
  if ((get_limits() & DIR_SOUTH_MASK) == 0)
  {
    update_coord();
    if ((calc_is_near_target(G_init_params.direction, G_init_params.targ_ha_msec, G_init_params.targ_dec_asec)) && (G_init_params.max_speed != MIN_RATE))
    {
      G_init_params.max_speed = MIN_RATE;
      G_ramp_reqd = TRUE;
      change_speed(MIN_RATE);
    }
    return 0;
  }

  #if defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   G_motor_ref_S = get_steps_dec();
   G_cur_motor_dec_asec = G_lim_S_asec;
   printk(KERN_INFO PRINTK_PREFIX "Found Southern limit at %ld motor steps\n", G_motor_ref_S);
  #endif
  #if !defined(POINT_W_MOTORS) || defined(ENCODER_DIAG)
   G_encod_ref_S = get_enc_dec_pulses();
   G_cur_encod_dec_asec = G_lim_S_asec;
   printk(KERN_INFO PRINTK_PREFIX "Found Southern limit at %ld encoder pulses\n", G_encod_ref_S);
  #endif
  stop_move();
  return 1;
}

