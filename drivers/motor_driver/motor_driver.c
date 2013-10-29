// This driver is intended to communicate with the timecard installed on the Alan Cousins Telescope
//  The code is VERY loosely based on a code segment found in the Linux Kernel Module Programming Guide by Peter Jay Salzman, although no longer bears any notable similarity with the original code - available at http://tldp.org/LDP/lkmpg/2.6/html/
//  The original code was provided free of charge and without any conditions as to attribution, remuneration or distribution of derivative works

/**
 * TODO:
 * - make provision for goto when already at target coordinates
 * - restructure
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include "motor_driver.h"
#include "motor_intfce.h"
#include "motor_defs.h"

#define MON_PERIOD_MSEC     200
#define RAMPMON_PERIOD_MSEC 50

static int __init device_init(void);
static void __exit device_cleanup(void);
static ssize_t device_read(struct file *filp, char __user * buffer, size_t length, loff_t * offset);
static long device_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param);
static int device_open(struct inode *inode, struct file *file);
static int device_release(struct inode *inode, struct file *file);
static unsigned int device_poll(struct file *filp, poll_table *wait);
static void update_status(void);

// Module Declarations
struct file_operations Fops =
{
  .read = device_read,
  .unlocked_ioctl = device_ioctl,
  .open = device_open,
  .release = device_release,
  .poll = device_poll
};

/// Device major number
static int G_major = 0;
/// Device class structure for creating /dev character device on load
static struct class *G_class_motor;
/// Device structure for creating /dev character device on load
static struct device *G_dev_motor;
/// If new status since last status read
static unsigned char G_stat_pending=0;
/// Number of times character device has been opened
static unsigned char G_num_open=0;
/// List of notifications for asynchronous notification ("poll")
static wait_queue_head_t G_readq;

// Initialize the module and register the character device
static int __init device_init(void)
{
  G_major = register_chrdev(0, MOTOR_DEVICE_NAME, &Fops);
  if (G_major < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Can't get major number\n");
    return(G_major);
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Module inserted. Assigned major: %d\n", G_major);
  
  G_class_motor = class_create(THIS_MODULE, MOTOR_DEVICE_NAME);
  if (G_class_motor == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device class.\n" );
    unregister_chrdev(G_major, MOTOR_DEVICE_NAME);
    return -ENODEV;
  }
  
  G_dev_motor = device_create(G_class_motor, NULL, MKDEV(G_major, 0), NULL, MOTOR_DEVICE_NAME);
  if (G_dev_motor == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device.\n" );
    unregister_chrdev(G_major, MOTOR_DEVICE_NAME);
    class_destroy(G_class_motor);
    return -ENODEV;
  }
  
  init_waitqueue_head(&G_readq);
  
  motordrv_init(&update_status);

  return 0;
}

// Cleanup − unregister the appropriate file from /proc
static void __exit device_cleanup(void)
{
  motordrv_finalise();
  device_destroy(G_class_motor, MKDEV(G_major, 0));
  class_destroy(G_class_motor);
  unregister_chrdev(G_major, MOTOR_DEVICE_NAME);
}

// This function is called whenever a process which has already opened the device file attempts to read from it.
static ssize_t device_read(struct file *filp, char __user * buffer, size_t length, loff_t * offset)
{
  int ret;
  unsigned char stat = get_motor_stat();
  if (filp->f_flags & O_NONBLOCK)
  {
    ret = copy_to_user(buffer, &stat, 1);
    if (ret >= 0)
      G_stat_pending = 0;
    return ret;
  }
  while (G_stat_pending == 0)
  {
    if (wait_event_interruptible(G_readq, (stat & MOTOR_STAT_UPDATE) == 0))
      return -ERESTARTSYS;
  }
  ret = copy_to_user(buffer, &stat, 1);
  G_stat_pending = 0;
  return ret;
}

static long device_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
  union user_data_t
  {
    struct motor_tel_coord coord;
    struct motor_goto_cmd goto_cmd;
    struct motor_card_cmd card_cmd;
    struct motor_track_adj track_adj;
    unsigned char limits_stat;
  } user_data;
  long value;
  
  switch (ioctl_num)
  {
    case IOCTL_MOTOR_GET_MOTOR_POS:
      get_coord_motor(&user_data.coord);
      value = copy_to_user((void*)ioctl_param, &user_data.coord, sizeof(struct motor_tel_coord));
      if (value != 0)
        value = -EFAULT;
      break;
      
    case IOCTL_MOTOR_GET_ENCOD_POS:
      get_coord_encod(&user_data.coord);
      value = copy_to_user((void*)ioctl_param, &user_data.coord, sizeof(struct motor_tel_coord));
      if (value != 0)
        value = -EFAULT;
      break;
      
    case IOCTL_MOTOR_GOTO:
      if (ioctl_param == 0)
      {
        end_goto();
        value = 0;
        break;
      }
      value = copy_from_user(&user_data.goto_cmd, (void*)ioctl_param, sizeof(struct motor_goto_cmd));
      if (value != 0)
      {
        value = -EFAULT;
        break;
      }
      value = start_goto(&user_data.goto_cmd);
      break;
    
    case IOCTL_MOTOR_CARD:
      if (ioctl_param == 0)
      {
        end_card();
        value = 0;
        break;
      }
      value = copy_from_user(&user_data.card_cmd, (void*)ioctl_param, sizeof(struct motor_card_cmd));
      if (value != 0)
      {
        value = -EFAULT;
        break;
      }
      value = start_card(&user_data.card_cmd);
      break;
    
    case IOCTL_MOTOR_SET_TRACKING:
      toggle_tracking(ioctl_param != 0);
      value = 0;
      break;
    
    case IOCTL_MOTOR_TRACKING_ADJ:
      value = 0;
      if (ioctl_param == 0)
      {
        adjust_tracking(0, 0);
        break;
      }
      value = copy_from_user(&user_data.track_adj, (void*)ioctl_param, sizeof(struct motor_track_adj));
      if (value != 0)
      {
        value = -EFAULT;
        break;
      }
      adjust_tracking(user_data.track_adj.adj_ha_steps, user_data.track_adj.adj_dec_steps);
      break;
    
    case IOCTL_MOTOR_GET_LIMITS:
      user_data.limits_stat = get_motor_limits();
      value = copy_to_user(&user_data.limits_stat, (void *)ioctl_param, sizeof(unsigned char));
      if (value != 0)
        value = -EFAULT;
      break;
    
    case IOCTL_MOTOR_EMERGENCY_STOP:
      toggle_all_stop(ioctl_param != 0);
      value = 0;
      break;
    
    #ifdef MOTOR_SIM
    case IOCTL_SET_SIM_STEPS:
    {
      unsigned long sim_steps;
      value = sizeof(unsigned long);
      while (value > 0)
        value = copy_from_user((void*)ioctl_param+value, &sim_steps, sizeof(unsigned long)-value);
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to read simulated steps from user-space.\n");
        value = -EIO;
        break;
      }
      set_sim_steps(sim_steps);
      break;
    }
    
    case IOCTL_SET_SIM_LIMITS:
    {
      unsigned long sim_limits;
      value = copy_from_user((void*)ioctl_param, &sim_limits, sizeof(unsigned long));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to read simulated limits from user-space.\n");
        value = -EIO;
        break;
      }
      set_sim_limits(sim_limits);
      break;
    }
    
    case IOCTL_GET_SIM_STEPS:
    {
      unsigned long sim_steps = get_sim_steps();
      value = copy_to_user((void*)ioctl_param, &sim_steps, sizeof(unsigned long));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated limits to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }
    
    case IOCTL_GET_SIM_DIR:
    {
      unsigned char sim_dir = get_sim_dir();
      value = copy_to_user((void*)ioctl_param, &sim_dir, sizeof(unsigned char));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated direction to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }
    
    case IOCTL_GET_SIM_RATE:
    {
      unsigned long sim_speed = get_sim_speed();
      value = copy_to_user((void*)ioctl_param, &sim_speed, sizeof(unsigned long));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated speed to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }
    #endif
    default:
      printk(KERN_DEBUG PRINTK_PREFIX "Invalid IOCTL number (%d).\n", ioctl_num);
      value = -ENODEV;
  }
  return value;
}

// This function is called whenever a process tries to do an ioctl on our device file. 
/*static long device_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
  long value;
  void *user_data = NULL;

  switch (ioctl_num)
  {
    /// Get the telescope's current position - saves struct tel_coord to ioctl parameter
    case IOCTL_MOTOR_GET_TEL_POS:
    {
      int ha_steps, dec_steps;
      get_coords(&ha_steps, &dec_steps);
      struct tel_coord user_coord;
      user_coord.ha_steps = ha_steps;
      user_coord.dec_steps = dec_steps;
      value = copy_to_user((void*)ioctl_param, &user_coord, sizeof(struct tel_coord));
      break;
    }

    /// Start a telescope goto - receives struct tel_goto as ioctl parameter, cancels command if ioctl parameter is NULL.
    case IOCTL_TEL_GOTO:
    {
      if (ioctl_param == 0)
      {
        value = 0;
        if ((G_status & MOTOR_STAT_MOVING) == 0)
        {
          printk(KERN_DEBUG PRINTK_PREFIX "Received goto cancel command, but no goto is underway. Ignoring.\n");
          break;
        }
        user_cancel_goto();
        break;
      }
      user_data = kmalloc(sizeof(struct tel_goto_cmd), GFP_ATOMIC);
      if (user_data == NULL)
      {
        printk(KERN_INFO PRINTK_PREFIX "Could not allocate memory for goto parameters. Ignoring goto command.\n");
        value = -ENOMEM;
        break;
      }
      value = copy_from_user(user_data, (void*)ioctl_param, sizeof(struct tel_goto_cmd));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Received goto command. Could not copy goto parameters from user.\n");
        kfree(user_data);
        user_data = NULL;
        value = -EIO;
        break;
      }
      value = user_start_goto((struct tel_goto_cmd *) user_data);
      break;
    }

    /// Set telescope tracking - 0 means disable tracking, everything else means track at the specified speed (usually you want SID_RATE)
    // ??? Allow to disable tracking if if motors not initialised and allstop active
    case IOCTL_TEL_SET_TRACKING:
    {
      get_user(value, (unsigned long*)ioctl_param);
      value = user_toggle_track(value);
      break;
    }

    case IOCTL_GET_LIMITS:
    {
      value = get_limits();
      put_user(value, (unsigned long*)ioctl_param);
      value = 0;
      break;
    }
/// TODO: check that MOTOR_STAT_ERR_LIMS is being toggled correctly, preferably remove toggles in ioctl handler
    case IOCTL_TEL_EMERGENCY_STOP:
    {
      if (get_user(value, (long*)ioctl_param) < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Error: Could not copy emergency stop ioctl parameter from user space. Assuming emergency stop must be activated.\n");
        value = 1;
      }
      user_toggle_allstop(value);
      value = 0;
      break;
    }

    #if defined(ENCODER_DIAG)
    /// Get the current position according to the motors - saves struct tel_coord to ioctl parameter.
    case IOCTL_GET_MOTOR_POS:
    {
      int ha_steps, dec_steps;
      get_motor_coords(&ha_steps, &dec_steps);
      struct tel_coord user_coord;
      user_coord.ha_steps = ha_steps;
      user_coord.dec_steps = dec_steps;
      value = copy_to_user((void*)ioctl_param, &user_coord, sizeof(struct tel_coord));
      break;
    }

    /// Get the current position according to the encoders - saves struct tel_coord to ioctl parameter
    case IOCTL_GET_ENCODER_POS:
    {
      int ha_steps, dec_steps;
      get_encod_coords(&ha_steps, &dec_steps);
      struct tel_coord user_coord;
      user_coord.ha_steps = ha_steps;
      user_coord.dec_steps = dec_steps;
      value = copy_to_user((void*)ioctl_param, &user_coord, sizeof(struct tel_coord));
      break;
    }
    #endif
    
    #ifdef MOTOR_SIM
    case IOCTL_SET_SIM_STEPS:
    {
      unsigned long sim_steps;
      value = sizeof(unsigned long);
      while (value > 0)
        value = copy_from_user((void*)ioctl_param+value, &sim_steps, sizeof(unsigned long)-value);
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to read simulated steps from user-space.\n");
        value = -EIO;
        break;
      }
      set_sim_steps(sim_steps);
      break;
    }

    case IOCTL_SET_SIM_LIMITS:
    {
      unsigned long sim_limits;
      value = copy_from_user((void*)ioctl_param, &sim_limits, sizeof(unsigned long));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to read simulated limits from user-space.\n");
        value = -EIO;
        break;
      }
      set_sim_limits(sim_limits);
      break;
    }

    case IOCTL_GET_SIM_STEPS:
    {
      unsigned long sim_steps = get_sim_steps();
      value = copy_to_user((void*)ioctl_param, &sim_steps, sizeof(unsigned long));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated limits to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }

    case IOCTL_GET_SIM_DIR:
    {
      unsigned char sim_dir = get_sim_dir();
      value = copy_to_user((void*)ioctl_param, &sim_dir, sizeof(unsigned char));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated direction to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }

    case IOCTL_GET_SIM_SPEED:
    {
      unsigned long sim_speed = get_sim_speed();
      value = copy_to_user((void*)ioctl_param, &sim_speed, sizeof(unsigned long));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated speed to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }
    #endif

    default:
      value = -ENOTTY;
  }

  if (G_status & MOTOR_STAT_UPDATE)
    wake_up_interruptible(&G_readq);
  return value;
}*/

// This is called whenever a process attempts to open the device file
static int device_open(struct inode *inode, struct file *file)
{
  G_num_open++;
  if (G_num_open > 1)
    return 0;

  try_module_get(THIS_MODULE);
  return 0;
}

// This is called whenever a process closes the device file
static int device_release(struct inode *inode, struct file *file)
{
  G_num_open--;
  if (G_num_open > 0)
    return 0;

  module_put(THIS_MODULE);
  return 0;
}

static unsigned int device_poll(struct file *filp, poll_table *wait)
{
  unsigned int mask = 0;
  poll_wait(filp, &G_readq,  wait);
  if (G_stat_pending)
    mask |= POLLIN | POLLRDNORM;
  return mask;
}

static void update_status(void)
{
  G_stat_pending = 1;
  wake_up_interruptible(&G_readq);
}

/*void handset_change(const unsigned char old_hs, const unsigned char new_hs)
{
  if (((old_hs & HS_DIR_DEC_MASK) != HS_DIR_DEC_MASK) && ((new_hs & HS_DIR_DEC_MASK) == HS_DIR_DEC_MASK))
  {
    if (G_status & MOTOR_STAT_ALLSTOP)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Disabling emergency stop (by handset).\n");
      user_toggle_allstop(FALSE);
    }
    else
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Enabling emergency stop (by handset).\n");
      user_toggle_allstop(TRUE);
    }
    return;
  }
  if (((old_hs & HS_DIR_HA_MASK) != HS_DIR_HA_MASK) && ((new_hs & HS_DIR_HA_MASK) == HS_DIR_HA_MASK))
  {
    if (G_status & MOTOR_STAT_TRACKING)
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Disabling tracking (by handset).\n");
      user_toggle_track(FALSE);
    }
    else
    {
      printk(KERN_DEBUG PRINTK_PREFIX "Enabling tracking (by handset).\n");
      user_toggle_track(TRUE);
    }
    return;
  }
  if ((old_hs & HS_SPEED_MASK) != (new_hs & HS_SPEED_MASK))
  {
    unsigned int new_speed = MOTOR_SET_RATE;
    if (new_hs & HS_SLEW_MASK)
      new_speed = MOTOR_SLEW_RATE;
    if (new_hs & HS_GUIDE_MASK)
      new_speed = MOTOR_GUIDE_RATE;
    change_speed(new_speed);
  }
  if ((old_hs & HS_DIR_MASK) != (new_hs & HS_DIR_MASK))
  {
    if (G_status & MOTOR_STAT_MOVING)
    {
      stop_move();
      G_status &= MOTOR_STAT_MOVING;
      G_status |= MOTOR_STAT_UPDATE;
    }
    unsigned char new_dir = 0;
    if (new_hs & HS_DIR_NORTH_MASK)
      new_dir |= MOTOR_DIR_NORTH_MASK;
    if (new_hs & HS_DIR_SOUTH_MASK)
      new_dir |= MOTOR_DIR_SOUTH_MASK;
    if (new_hs & HS_DIR_EAST_MASK)
      new_dir |= MOTOR_DIR_EAST_MASK;
    if (new_hs & HS_DIR_WEST_MASK)
      new_dir |= MOTOR_DIR_WEST_MASK;
    if (new_dir != 0)
    {
      if (!start_move(new_dir, unsigned long speed);
    }
  }
  
  
  
  int targ_ha_steps, targ_dec_steps;
  unsigned int max_speed;
  if ((old_hs & MOTOR_DIR_MASK) && !(new_hs & MOTOR_DIR_MASK))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Ending handset move.\n");
    end_goto();
    return;
  }
  if (!(old_hs & MOTOR_DIR_MASK) || (new_hs & MOTOR_DIR_MASK))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Handset update specifies no new direction, not starting/changing anything.\n");
    return;
  }
  
  
  char start_goto(int targ_ha, int targ_dec, unsigned char use_encod, unsigned long max_speed)

  
  
  
  
}

static int user_start_goto(struct tel_goto_cmd *cmd)
{
  if (G_status & (MOTOR_STAT_MOVING | MOTOR_STAT_ALLSTOP))
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Received goto command, but motors are currently busy. Ignoring.\n");
    return -EAGAIN;
  }
  if ((G_status & (MOTOR_STAT_HA_INIT | MOTOR_STAT_DEC_INIT)) == 0)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Received goto command, but telescope has not been initialised.\n");
    return -EPERM;
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Goto requested.\n");
  if (G_status & MOTOR_STAT_TRACKING)
  {
    printk(KERN_DEBUG PRINTK_PREFIX "Tracking stop.\n");
    G_status &= ~MOTOR_STAT_TRACKING;
    G_status |= MOTOR_STAT_UPDATE;
    cancel_delayed_work(&G_trackmon_work);
    stop_move();
  }
  if (!start_goto(cmd->ha_steps, cmd->dec_steps, cmd->use_encod, cmd->max_speed))
    return -EINVAL;
  G_status |= MOTOR_STAT_MOVING | MOTOR_STAT_UPDATE;
  gotomon(NULL);
  if (get_is_ramp_reqd())
    rampmon(NULL);
  value = 0;
}

static void user_cancel_goto(void)
{
  cancel_goto();
}

static void user_toggle_allstop(char allstop_on)
{
  if (allstop_on)
  {
    printk(KERN_INFO PRINTK_PREFIX "Emergency stop!\n");
    set_track_rate(0);
    stop_move();
    cancel_delayed_work(&G_trackmon_work);
    cancel_delayed_work(&G_gotomon_work);
    cancel_delayed_work(&G_rampmon_work);
    cancel_delayed_work(&G_handsetmove_work);
    G_status &= ~(MOTOR_STAT_MOVING | MOTOR_STAT_TRACKING);
    G_status |= MOTOR_STAT_ALLSTOP | MOTOR_STAT_UPDATE;
  }
  else
  {
    printk(KERN_INFO PRINTK_PREFIX "Cancelling emergency stop.\n");
    G_status &= ~MOTOR_STAT_ALLSTOP;
    G_status |= MOTOR_STAT_UPDATE;
  }
}

static int user_toggle_track(unsigned int track_rate)
{
  if (G_status & MOTOR_STAT_ALLSTOP)
  {
    printk(KERN_INFO PRINTK_PREFIX "Received track command, but emergency stop is active. Ignoring.\n");
    return -EPERM;
  }
  if ((G_status & (MOTOR_STAT_HA_INIT | MOTOR_STAT_DEC_INIT)) == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Received track command, but telescope has not been initialised.\n");
    return -EPERM;
  }
  if (track_rate == 0)
  {
    G_status &= ~MOTOR_STAT_TRACKING;
    G_status |= MOTOR_STAT_UPDATE;
    cancel_delayed_work(&G_trackmon_work);
    if ((G_status & MOTOR_STAT_MOVING) == 0)
      stop_move();
  }
  else if ((G_status & MOTOR_STAT_MOVING) == 0)
  {
    G_status &= ~MOTOR_STAT_ERR_LIMS;
    G_status |= MOTOR_STAT_TRACKING | MOTOR_STAT_UPDATE;
    if (!set_track_rate(track_rate))
      return -EIO;
    trackmon(NULL);
  }
  return 0;
}

static void gotomon(struct work_struct *work)
{
  int ret;
  if ((G_status & MOTOR_STAT_MOVING) == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "gotomon called, but goto does not seem to be in progress.\n");
    return;
  }
  ret = check_goto(MON_PERIOD_MSEC);
  if (ret < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error encountered during goto (probably limits). Cancelling.\n");
    G_status &= MOTOR_STAT_MOVING;
    if (get_limits()
      G_status |= MOTOR_STAT_ERR_LIMS;
    G_status |= MOTOR_STAT_UPDATE;
    wake_up_interruptible(&G_readq);
    return;
  }
  if (ret == 0)
  {
    queue_delayed_work(G_motordrv_workq, &G_gotomon_work, MON_PERIOD_MSEC * HZ / 1000);
    if (get_is_ramp_reqd())
      rampmon(NULL);
    return;
  }
  unsigned long tmp_track_rate = get_track_rate();
  G_status &= ~MOTOR_STAT_MOVING;
  if (tmp_track_rate > 0)
  {
    set_track_rate(0);
    ret = set_track_rate(tmp_track_rate);
    if (!ret)
      printk(KERN_INFO PRINTK_PREFIX "Error encountered while trying to start tracking after goto. Not tracking.\n");
    else
    {
      G_status |= MOTOR_STAT_TRACKING;
      trackmon(NULL);
    }
  }
  G_status |= MOTOR_STAT_UPDATE;
  wake_up_interruptible(&G_readq);
}

static void rampmon(struct work_struct *work)
{
  int ret;
  ret = motor_ramp();
  if (ret < 0)
    printk(KERN_INFO PRINTK_PREFIX "Error encountered while ramping motor up/down. Cancelling ramp.\n");
  else if (ret != 0)
    queue_delayed_work(G_motordrv_workq, &G_rampmon_work, RAMPMON_PERIOD_MSEC * HZ / 1000);
}

static void trackmon(struct work_struct *work)
{
  int ret;
  if ((G_status & MOTOR_STAT_TRACKING) == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "trackmon called, but telescope should not be tracking.\n");
    return;
  }
  ret = check_track();
  if (ret > 0)
  {
    queue_delayed_work(G_motordrv_workq, &G_trackmon_work, MON_PERIOD_MSEC * HZ / 1000);
    return;
  }
  printk(KERN_INFO PRINTK_PREFIX "Error encountered while tracking. Cancelling track.\n");
  stop_move();
  G_status &= ~MOTOR_STAT_TRACKING;
  if (get_limits())
    G_status |= MOTOR_STAT_ERR_LIMS;
  G_status |= MOTOR_STAT_UPDATE;
  wake_up_interruptible(&G_readq);
}*/

module_init(device_init);
module_exit(device_cleanup);
// Some work_queue related functions are just available to GPL licensed Modules
MODULE_LICENSE("GPL");
