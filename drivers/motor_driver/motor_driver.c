// This driver is intended to communicate with the timecard installed on the Alan Cousins Telescope
//  The code is VERY loosely based on a code segment found in the Linux Kernel Module Programming Guide by Peter Jay Salzman, although no longer bears any notable similarity with the original code - available at http://tldp.org/LDP/lkmpg/2.6/html/
//  The original code was provided free of charge and without any conditions as to attribution, remuneration or distribution of derivative works

/**
 * TODO:
 * - make provision for goto when already at target coordinates
 * - implement new treatment of soft altitude limits (in get_limits())
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <asm/div64.h>
#include "motor_driver.h"
#include "motor_intfce.h"
#include "motor_defs.h"
#include "motor_funcs.h"

#define MON_PERIOD_MSEC     200
#define RAMPMON_PERIOD_MSEC 50

#define MSEC_TO_HOURS(msec) (msec/3600000)
#define MSEC_TO_MIN(msec) (msec/60000)%60
#define MSEC_TO_SEC(msec) (msec/1000)%60
#define ASEC_TO_DEG(asec) (asec/3600)
#define ASEC_TO_AMIN(asec) (asec/60)%60
#define HMSMS_TO_MSEC(hrs,min,sec,msec) (hrs*3600000)+(min*60000)+(sec*1000)+msec
#define DMS_TO_ASEC(deg,amin,asec) (deg*3600)+(amin*60)+asec

static int __init device_init(void);
static void __exit device_cleanup(void);
static ssize_t device_read(struct file *filp, char __user * buffer, size_t length, loff_t * offset);
static long device_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param);
static int device_open(struct inode *inode, struct file *file);
static int device_release(struct inode *inode, struct file *file);
static unsigned int device_poll(struct file *filp, poll_table *wait);
char user_start_goto(struct tel_goto_cmd *cmd);
char user_start_init(struct tel_limits *new_lims);
void user_adj_pointing(struct tel_coord *new_coord);
static void gotomon(struct work_struct *work);
static void rampmon(struct work_struct *work);
static void trackmon(struct work_struct *work);
static void initmon(struct work_struct *work);

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
/// Device class structure
static struct class *G_class_motor;
/// Device structure
static struct device *G_dev_motor;
static unsigned char G_status=0;
static unsigned char G_num_open=0;
static unsigned char G_dev_busy=0;
static wait_queue_head_t G_readq;
static struct workqueue_struct *G_motordrv_workq;
static struct delayed_work G_trackmon_work;
static struct delayed_work G_gotomon_work;
static struct delayed_work G_initmon_work;
static struct delayed_work G_rampmon_work;

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
  G_status = 0;
  G_motordrv_workq = create_singlethread_workqueue("act_motors");
  INIT_DELAYED_WORK(&G_trackmon_work, trackmon);
  INIT_DELAYED_WORK(&G_gotomon_work, gotomon);
  INIT_DELAYED_WORK(&G_rampmon_work, rampmon);
  INIT_DELAYED_WORK(&G_initmon_work, initmon);

  return 0;
}

// Cleanup − unregister the appropriate file from /proc
static void __exit device_cleanup(void)
{
  // Initialise the controller
  cancel_delayed_work(&G_trackmon_work);
  cancel_delayed_work(&G_gotomon_work);
  cancel_delayed_work(&G_rampmon_work);
  cancel_delayed_work(&G_initmon_work);
  stop_move();
  destroy_workqueue(G_motordrv_workq);
  device_destroy(G_class_motor, MKDEV(G_major, 0));
  class_destroy(G_class_motor);
  unregister_chrdev(G_major, MOTOR_DEVICE_NAME);
}

// This function is called whenever a process which has already opened the device file attempts to read from it.
static ssize_t device_read(struct file *filp, char __user * buffer, size_t length, loff_t * offset)
{
  int ret;
  if (filp->f_flags & O_NONBLOCK)
  {
    ret = copy_to_user(buffer, &G_status, 1);
    if (ret >= 0)
      G_status &= ~MOTOR_STAT_UPDATE;
    return ret;
  }
  while ((G_status & MOTOR_STAT_UPDATE) == 0)
  {
    if (wait_event_interruptible(G_readq, (G_status & MOTOR_STAT_UPDATE) == 0))
      return -ERESTARTSYS;
  }
  ret = copy_to_user(buffer, &G_status, 1);
  if (ret >= 0)
    G_status &= ~MOTOR_STAT_UPDATE;
  return ret;
}

// This function is called whenever a process tries to do an ioctl on our device file. 
static long device_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
  long value;
  void *user_data = NULL;

  switch (ioctl_num)
  {
    /// Get the telescope's current position - saves struct tel_coord to ioctl parameter
    case IOCTL_GET_TEL_POS:
    {
      long int ha_msec, dec_asec;
      struct tel_coord user_coord;
      get_coords(&ha_msec, &dec_asec);
      user_coord.ha_hrs = MSEC_TO_HOURS(ha_msec);
      user_coord.ha_min = MSEC_TO_MIN(ha_msec);
      user_coord.ha_sec = MSEC_TO_SEC(ha_msec);
      user_coord.ha_msec = ha_msec % 1000;
      user_coord.dec_deg = ASEC_TO_DEG(dec_asec);
      user_coord.dec_amin = ASEC_TO_AMIN(dec_asec);
      user_coord.dec_asec = dec_asec % 60;
      value = copy_to_user((void*)ioctl_param, &user_coord, sizeof(struct tel_coord));
      break;
    }

    /// IOCTL to start telescope initialisation
    case IOCTL_START_INIT:
    {
      if (G_status & (MOTOR_STAT_INITIALISING | MOTOR_STAT_GOING | MOTOR_STAT_TRACKING | MOTOR_STAT_ALLSTOP | MOTOR_STAT_GOTO_CANCEL))
      {
        printk(KERN_INFO PRINTK_PREFIX "Received init command, but motors are currently busy. Ignoring init command.\n");
        value = -EPERM;
        break;
      }
      G_status = MOTOR_STAT_UPDATE;
      if ((void *)ioctl_param == NULL)
      {
        printk(KERN_INFO PRINTK_PREFIX "Command to initialise received with a NULL pointer. Not initialising.\n");
        value = 0;
        break;
      }
      user_data = kmalloc(sizeof(struct tel_limits), GFP_ATOMIC);
      if (user_data == NULL)
      {
        printk(KERN_INFO PRINTK_PREFIX "Could not allocate memory for initialisation parameters. Not initialising.\n");
        value = -ENOMEM;
        break;
      }
      value = copy_from_user(user_data, (void*)ioctl_param, sizeof(struct tel_limits));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Received init command. Could not copy init parameters from user.\n");
        kfree(user_data);
        user_data = NULL;
        value = -EIO;
        break;
      }
      value = user_start_init((struct tel_limits*)user_data);
      kfree(user_data);
      user_data = NULL;
      if (value == 0)
      {
        value = -EPERM;
        G_status |= MOTOR_STAT_ERR_LIMS;
        break;
      }
      G_status |= MOTOR_STAT_INITIALISING;
      initmon(NULL);
      if (get_is_ramp_reqd())
        rampmon(NULL);
      value = 0;
      break;
    }

    /// Start a telescope goto - receives struct tel_goto as ioctl parameter, cancels command if ioctl parameter is NULL.
    case IOCTL_TEL_GOTO:
    {
      if (ioctl_param == 0)
      {
        if ((G_status & MOTOR_STAT_GOING) == 0)
        {
          printk(KERN_DEBUG PRINTK_PREFIX "Received goto cancel command, but no goto is underway. Ignoring.\n");
          value = 0;
          break;
        }
        end_goto();
        value = 0;
        break;
      }
      if (G_status & (MOTOR_STAT_INITIALISING | MOTOR_STAT_GOING | MOTOR_STAT_ALLSTOP | MOTOR_STAT_GOTO_CANCEL))
      {
        printk(KERN_INFO PRINTK_PREFIX "Received goto command, but motors are currently busy. Ignoring.\n");
        value = -EAGAIN;
        break;
      }
      if ((G_status & MOTOR_STAT_INITIALISED) == 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Received goto command, but telescope has not been initialised.\n");
        value = -EPERM;
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
      printk(KERN_INFO PRINTK_PREFIX "Goto requested.\n");
      if (G_status & MOTOR_STAT_TRACKING)
      {
        G_status &= ~MOTOR_STAT_TRACKING;
        G_status |= MOTOR_STAT_UPDATE;
        cancel_delayed_work(&G_trackmon_work);
        stop_move();
      }
      value = user_start_goto((struct tel_goto_cmd*)user_data);
      kfree(user_data);
      user_data = NULL;
      if (value == 0)
      {
        value = -EPERM;
        G_status |= MOTOR_STAT_ERR_LIMS;
        break;
      }
      G_status |= MOTOR_STAT_GOING | MOTOR_STAT_UPDATE;
      gotomon(NULL);
      if (get_is_ramp_reqd())
        rampmon(NULL);
      value = 0;
      break;
    }

    /// Set telescope tracking - 0 means disable tracking, everything else means track at the specified speed (usually you want SID_RATE)
    // ??? Allow to disable tracking if if motors not initialised and allstop active
    case IOCTL_TEL_SET_TRACKING:
    {
      if (G_status & (MOTOR_STAT_INITIALISING | MOTOR_STAT_ALLSTOP))
      {
        printk(KERN_INFO PRINTK_PREFIX "Received track command, but emergency stop is active. Ignoring.\n");
        value = -EPERM;
        break;
      }
      if ((G_status & MOTOR_STAT_INITIALISED) == 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Received track command, but telescope has not been initialised.\n");
        value = -EPERM;
        break;
      }
      get_user(value, (unsigned long*)ioctl_param);
      set_track_rate(value);
      if (value == 0)
      {
        G_status &= ~MOTOR_STAT_TRACKING;
        G_status |= MOTOR_STAT_UPDATE;
        cancel_delayed_work(&G_trackmon_work);
        stop_move();
      }
      else if ((G_status & MOTOR_STAT_GOING) == 0)
      {
        G_status &= ~MOTOR_STAT_ERR_LIMS;
        G_status |= MOTOR_STAT_TRACKING | MOTOR_STAT_UPDATE;
        trackmon(NULL);
      }
      value = 0;
      break;
    }

    case IOCTL_GET_LIMITS:
    {
      value = get_limits();
      put_user(value, (unsigned long*)ioctl_param);
      value = 0;
      break;
    }

    case IOCTL_TEL_EMERGENCY_STOP:
    {
      if (get_user(value, (long*)ioctl_param) < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Error: Could not copy emergency stop ioctl parameter from user space. Assuming emergency stop must be activated.\n");
        value = 1;
      }
      if (value)
      {
        printk(KERN_INFO PRINTK_PREFIX "Emergency stop!\n");
        set_track_rate(0);
        stop_move();
        cancel_delayed_work(&G_trackmon_work);
        cancel_delayed_work(&G_gotomon_work);
        cancel_delayed_work(&G_initmon_work);
        cancel_delayed_work(&G_rampmon_work);
        G_status &= ~(MOTOR_STAT_INITIALISING | MOTOR_STAT_GOING | MOTOR_STAT_GOTO_CANCEL | MOTOR_STAT_TRACKING);
        G_status |= MOTOR_STAT_ALLSTOP | MOTOR_STAT_UPDATE;
      }
      else
      {
        printk(KERN_INFO PRINTK_PREFIX "Cancelling emergency stop.\n");
        G_status &= ~MOTOR_STAT_ALLSTOP;
        G_status |= MOTOR_STAT_UPDATE;
      }
      value = 0;
      break;
    }

    case IOCTL_TEL_ADJ_POINTING:
    {
      if ((void *)ioctl_param == NULL)
      {
        printk(KERN_INFO PRINTK_PREFIX "Telescope pointing adjust command received, but NULL parameter specified.\n");
        value = -EINVAL;
        break;
      }
      user_data = kmalloc(sizeof(struct tel_coord), GFP_ATOMIC);
      if (user_data == NULL)
      {
        printk(KERN_INFO PRINTK_PREFIX "Telescope pointing adjust commdand received, but could not allocate memory for parameters.\n");
        value = -ENOMEM;
        break;
      }
      value = copy_from_user(user_data, (void*)ioctl_param, sizeof(struct tel_coord));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Telescope pointing adjust command received, but could not copy parameters from user.\n");
        value = -EIO;
        break;
      }
      user_adj_pointing((struct tel_coord*)user_data);

      kfree(user_data);
      user_data = NULL;
      value = 0;
      break;
    }

    #if defined(ENCODER_DIAG)
    /// Get the current position according to the motors - saves struct tel_coord to ioctl parameter.
    case IOCTL_GET_MOTOR_POS:
    {
      long int ha_msec, dec_asec;
      struct tel_coord user_coord;
      get_motor_coords(&ha_msec, &dec_asec);
      user_coord.ha_hrs = MSEC_TO_HOURS(ha_msec);
      user_coord.ha_min = MSEC_TO_MIN(ha_msec);
      user_coord.ha_sec = MSEC_TO_SEC(ha_msec);
      user_coord.ha_msec = ha_msec % 1000;
      user_coord.dec_deg = ASEC_TO_DEG(dec_asec);
      user_coord.dec_amin = ASEC_TO_AMIN(dec_asec);
      user_coord.dec_asec = dec_asec % 60;
      value = copy_to_user((void*)ioctl_param, &user_coord, sizeof(struct tel_coord));
      break;
    }

    /// Get the current position according to the encoders - saves struct tel_coord to ioctl parameter
    case IOCTL_GET_ENCODER_POS:
    {
      long int ha_msec, dec_asec;
      struct tel_coord user_coord;
      get_encod_coords(&ha_msec, &dec_asec);
      user_coord.ha_hrs = MSEC_TO_HOURS(ha_msec);
      user_coord.ha_min = MSEC_TO_MIN(ha_msec);
      user_coord.ha_sec = MSEC_TO_SEC(ha_msec);
      user_coord.ha_msec = ha_msec % 1000;
      user_coord.dec_deg = ASEC_TO_DEG(dec_asec);
      user_coord.dec_amin = ASEC_TO_AMIN(dec_asec);
      user_coord.dec_asec = dec_asec % 60;
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
}

// This is called whenever a process attempts to open the device file
static int device_open(struct inode *inode, struct file *file)
{
  G_num_open++;
  G_status |= MOTOR_STAT_UPDATE;
  if (G_num_open > 1)
    return 0;

  stop_move();
  try_module_get(THIS_MODULE);
  return 0;
}

// This is called whenever a process closes the device file
static int device_release(struct inode *inode, struct file *file)
{
  G_num_open--;
  if (G_num_open > 0)
    return 0;

  G_status &= ~(MOTOR_STAT_INITIALISING | MOTOR_STAT_TRACKING | MOTOR_STAT_GOING | MOTOR_STAT_GOTO_CANCEL);
  cancel_delayed_work(&G_trackmon_work);
  cancel_delayed_work(&G_gotomon_work);
  cancel_delayed_work(&G_rampmon_work);
  cancel_delayed_work(&G_initmon_work);
  set_track_rate(0);
  stop_move();

  G_dev_busy = 0;
  module_put(THIS_MODULE);
  return 0;
}

static unsigned int device_poll(struct file *filp, poll_table *wait)
{
  unsigned int mask = 0;
  poll_wait(filp, &G_readq,  wait);
  if ((G_status & MOTOR_STAT_UPDATE) != 0)
    mask |= POLLIN | POLLRDNORM;
  return mask;
}

char user_start_goto(struct tel_goto_cmd *cmd)
{
  long int targ_ha_msec, targ_dec_asec;
  if (cmd == NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Input parameters to user_start_goto not specified - not starting goto.\n");
    return 0;
  }
  targ_ha_msec = HMSMS_TO_MSEC(cmd->ha_hrs, cmd->ha_min, cmd->ha_sec, cmd->ha_msec);
  targ_dec_asec = DMS_TO_ASEC(cmd->dec_deg, cmd->dec_amin, cmd->dec_asec);
  return start_goto(targ_ha_msec, targ_dec_asec, cmd->max_speed);
}

char user_start_init(struct tel_limits *new_lims)
{
  long lim_W_msec, lim_E_msec, lim_N_asec, lim_S_asec;
  if (new_lims == NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Input parameters to user_start_init not specified - not starting initialisation.\n");
    return 0;
  }
  
  lim_W_msec = HMSMS_TO_MSEC(new_lims->W_ha_hrs, new_lims->W_ha_min, new_lims->W_ha_sec, new_lims->W_ha_msec);
  lim_E_msec = HMSMS_TO_MSEC(new_lims->E_ha_hrs, new_lims->E_ha_min, new_lims->E_ha_sec, new_lims->E_ha_msec);
  lim_N_asec = DMS_TO_ASEC(new_lims->N_dec_deg, new_lims->N_dec_amin, new_lims->N_dec_asec);
  lim_S_asec = DMS_TO_ASEC(new_lims->S_dec_deg, new_lims->S_dec_amin, new_lims->S_dec_asec);
  printk(KERN_INFO PRINTK_PREFIX "Received init command: %ld %ld %ld %ld\n", lim_W_msec, lim_E_msec, lim_N_asec, lim_S_asec);
  return start_init(lim_W_msec, lim_E_msec, lim_N_asec, lim_S_asec);
}

void user_adj_pointing(struct tel_coord *new_coord)
{
  long int new_ha_msec, new_dec_asec;
  if (new_coord == NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Input parameters to user_adj_pointing not specified - not adjusting pointing.\n");
    return;
  }
  
  new_ha_msec = HMSMS_TO_MSEC(new_coord->ha_hrs, new_coord->ha_min, new_coord->ha_sec, new_coord->ha_msec);
  new_dec_asec = DMS_TO_ASEC(new_coord->dec_deg, new_coord->dec_amin, new_coord->dec_asec);
  adj_pointing(new_ha_msec, new_dec_asec);
}

static void gotomon(struct work_struct *work)
{
  int ret;
  G_dev_busy++;
  if ((G_status & MOTOR_STAT_GOING) == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "gotomon called, but goto does not seem to be in progress.\n");
    G_dev_busy--;
    return;
  }
  ret = check_goto(MON_PERIOD_MSEC);
  if (ret < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error encountered during goto (probably limits). Cancelling.\n");
    G_status |= MOTOR_STAT_ERR_LIMS | MOTOR_STAT_UPDATE;
    wake_up_interruptible(&G_readq);
  }
  else if (ret == 0)
  {
    queue_delayed_work(G_motordrv_workq, &G_gotomon_work, MON_PERIOD_MSEC * HZ / 1000);
    if (get_is_ramp_reqd())
      rampmon(NULL);
  }
  else
  {
    unsigned long tmp_track_rate = get_track_rate();
    G_status &= ~MOTOR_STAT_GOING;
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
  G_dev_busy--;
}

static void rampmon(struct work_struct *work)
{
  int ret;
  G_dev_busy++;
  ret = motor_ramp();
  if (ret < 0)
    printk(KERN_INFO PRINTK_PREFIX "Error encountered while ramping motor up/down. Cancelling ramp.\n");
  else if (ret != 0)
    queue_delayed_work(G_motordrv_workq, &G_rampmon_work, RAMPMON_PERIOD_MSEC * HZ / 1000);
  G_dev_busy--;
}

static void trackmon(struct work_struct *work)
{
  int ret;
  G_dev_busy++;
  if ((G_status & MOTOR_STAT_TRACKING) == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "trackmon called, but telescope should not be tracking.\n");
    G_status &= ~MOTOR_STAT_TRACKING;
    G_status |= MOTOR_STAT_UPDATE;
    wake_up_interruptible(&G_readq);
    G_dev_busy--;
    return;
  }
  ret = check_track();
  if (ret < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error encountered while tracking (probably limits). Cancelling track.\n");
    stop_move();
    G_status &= ~MOTOR_STAT_TRACKING;
    G_status |= MOTOR_STAT_ERR_LIMS | MOTOR_STAT_UPDATE;
    wake_up_interruptible(&G_readq);
  }
  else if (ret > 0)
    queue_delayed_work(G_motordrv_workq, &G_trackmon_work, MON_PERIOD_MSEC * HZ / 1000);
  G_dev_busy--;
}

static void initmon(struct work_struct *work)
{
  int ret;
  G_dev_busy++;
  if ((G_status & MOTOR_STAT_INITIALISING) == 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "initmon called, but telescope is not initialising.\n");
    G_status &= ~MOTOR_STAT_INITIALISING;
    G_dev_busy--;
    return;
  }
  ret = check_init();
  if (ret < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error encountered while initialising (probably limits). Cancelling initialisation.\n");
    G_status &= ~MOTOR_STAT_INITIALISING;
    G_status |= MOTOR_STAT_ERR_LIMS | MOTOR_STAT_UPDATE;
    wake_up_interruptible(&G_readq);
  }
  else if (ret == 0)
  {
    queue_delayed_work(G_motordrv_workq, &G_initmon_work, MON_PERIOD_MSEC * HZ / 1000);
    if (get_is_ramp_reqd())
      rampmon(NULL);
  }
  else
  {
    printk(KERN_INFO PRINTK_PREFIX "Initialisation complete\n");
    G_status &= ~MOTOR_STAT_INITIALISING;
    G_status |= MOTOR_STAT_INITIALISED | MOTOR_STAT_UPDATE;
    wake_up_interruptible(&G_readq);
  }
  G_dev_busy--;
}

module_init(device_init);
module_exit(device_cleanup);
// Some work_queue related functions are just available to GPL licensed Modules
MODULE_LICENSE("GPL");
