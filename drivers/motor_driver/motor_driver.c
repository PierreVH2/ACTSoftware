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
      value = copy_to_user((unsigned char *)ioctl_param, &user_data.limits_stat, sizeof(unsigned char));
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
      value = copy_to_user((void*)ioctl_param, &sim_speed, sizeof(unsigned char));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to write simulated speed to user-space.\n");
        value = -EIO;
        break;
      }
      break;
    }
    #endif
    case IOCTL_MOTOR_SET_MOTOR_POS:
      value = copy_from_user(&user_data.coord, (void*)ioctl_param, sizeof(struct motor_tel_coord));
      if (value < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Failed to read motor steps from user-space.\n");
        value = -EIO;
        break;
      }
      set_coord_motor(&user_data.coord);
      break;

    default:
      printk(KERN_DEBUG PRINTK_PREFIX "Invalid IOCTL number (%d).\n", ioctl_num);
      value = -ENODEV;
  }
  return value;
}

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

module_init(device_init);
module_exit(device_cleanup);
// Some work_queue related functions are just available to GPL licensed Modules
MODULE_LICENSE("GPL");
