/**
 * \file pmt_driver.c
 * \author Pierre van Heerden
 * \author Luis Balona
 * \brief Photomultiplier Tube Driver
 *
 * Adapted from Luis Balona's MILLYDEV photometry module, which was modified from PARAPIN.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/device.h>
#include <linux/stddef.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/sched.h>

#include <time_driver/time_driver.h>
#include <act_plc/act_plc.h>

#include "pmt_driver.h"
#include "pmt_defs.h"

#define PRINTK_PREFIX "[ACT_PMT] "

/** \brief PHOTOMETRY CARD port numbers
 * \{
 */
/// counter 0 port
#define COUNTER0   0X2A0
/// counter 1 port
#define COUNTER1   0X2A2
/// counter 2 port
#define COUNTER2   0X2A4
/// counter 3 port
#define COUNTER3   0X2A6
/// counter currently used
#define DEFCOUNTER COUNTER2
/// 82C55A port B - TTL inputs
#define TTLIN      0X2AA
/// 82C55A port C - TTL outputs
#define TTLOUT     0X2AC
/// 82C55A control word
#define PHOTIO     0X2AE
/// Program word for 82C55A
#define MODE0      0X9292
/** \} */

/// Length of buffer for average counts
#define INTEG_BUFF_LEN                5000

/// Minimum number of counts to be counted as non-zero
#define MIN_COUNTS_NONZERO            5

/// Maximum allowable counts per second
#define PMT_MAX_COUNTRATE             700000

/// Maximum value that can be stored on photometry card counter - DO NOT EXCEED!
#define PMT_COUNTER_MAX               60000

/// Number of seconds to probe PMT at high speed before integration is started
#define PMT_PROBE_TIME_S              3

/** \brief Linux kernel module definitions.
 * \{
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luis Balona <lab@saao.ac.za>, Pierre van Heerden <pierre@saao.ac.za>");
MODULE_DESCRIPTION("PMT photometry driver using external timing.");
/** \} */

/** \brief Forward definitions of private functions.
 * \{
 */
int insert_module(void);
void remove_module(void);
int pmt_open(struct inode *inodePtr, struct file *filePtr);
ssize_t pmt_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
int pmt_release(struct inode *inodePtr, struct file *filePtr);
long pmt_ioctl(struct file *filePtr, unsigned int ioctl_num, unsigned long ioctl_param);
int pmt_fasync(int fd, struct file *filp, int mode);
void pmt_millisec_handler(unsigned long unit, unsigned short unit_msec);
void pmt_time_sync_handler(char synced);
unsigned char check_counts(unsigned char cur_err, unsigned long counts, unsigned long sample_p_ns);
void check_probe(unsigned long check_period_ns, unsigned long unit_s, unsigned long unit_ns);
int order_integ(struct pmt_command *cmd);
void start_integ(void);
void cancel_integ(void);
void check_integ(unsigned long check_period_ns, unsigned long unit_s, unsigned long unit_ns);
void finish_integ(void);
/** \} */

/** \brief Global variables.
 * \{
 */
/// Device major number
static int G_major = 0;
/// Device class structure
static struct class *G_class_pmt;
/// Device structure
static struct device *G_dev_pmt;
/// Status information
static char G_status = 0;
/// Number of times the device has been opened
static unsigned char G_num_open=0;
/// Counter (photometry card channel) to use
static unsigned long G_counter_chan = DEFCOUNTER;
/// Structure to contain information about PMT
static struct pmt_information G_pmt_info;
/// Structure to contain information on current PMT command.
static struct pmt_command G_pmt_cmd;
/// Structure with data for integration currently in progress
static struct pmt_integ_data G_cur_integ;
/// Data buffer
static struct pmt_integ_data G_integ_buffer[INTEG_BUFF_LEN];
/// Reference index to last user-read photometric count in G_integ_buffer.
static unsigned long G_user_idx=0;
/// Reference index to current photometric count in G_integ_buffer.
static unsigned long G_cur_idx=0;
/// Photometer countrate above which over-illumination is triggered.
static unsigned long G_overillum_counts=PMT_WARN_COUNTRATE;
/// Counter for probing the PMT for an overillumination/overflow before integrating.
static char G_probe_counter=0;
/// Queue for asynchronous communication with user-space programmes
static struct fasync_struct *G_async_queue;
/** \} */

/// System calls we're handling
struct file_operations pmt_fops = {
  .owner = THIS_MODULE,
  .open = pmt_open,
  .read = pmt_read,
  .release = pmt_release,
  .unlocked_ioctl = pmt_ioctl,
  .fasync = pmt_fasync
};

/** \brief Called when module is inserted into the kernel.
 * \param (void)
 * \return 0 if successfully loaded, \< 0 otherwise.
 */
int insert_module(void)
{
  int ret;
  G_major = register_chrdev(0, PMT_DEVICE_NAME, &pmt_fops);
  if (G_major < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Can't get major number\n");
    return(G_major);
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Module inserted. Assigned major: %d\n", G_major);
  
  G_class_pmt = class_create(THIS_MODULE, PMT_DEVICE_NAME);
  if (G_class_pmt == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device class.\n" );
    unregister_chrdev(G_major, PMT_DEVICE_NAME);
    return -ENODEV;
  }

  G_dev_pmt = device_create(G_class_pmt, NULL, MKDEV(G_major, 0), NULL, PMT_DEVICE_NAME);
  if (G_dev_pmt == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device.\n" );
    unregister_chrdev(G_major, PMT_DEVICE_NAME);
    class_destroy(G_class_pmt);
    return -ENODEV;
  }

  ret = register_millisec_handler(&pmt_millisec_handler);
  if (!ret)
  {
    printk(KERN_ERR PRINTK_PREFIX "Could not register millisecond handler function with time system.\n");
    device_destroy(G_class_pmt, MKDEV(G_major, 0));
    class_destroy(G_class_pmt);
    unregister_chrdev(G_major, PMT_DEVICE_NAME);
    return -ENODEV;
  }
  ret = register_time_sync_handler(&pmt_time_sync_handler);
  if (!ret)
  {
    printk(KERN_ERR PRINTK_PREFIX "Could not register time sync alert handeler function with time system.\n");
    unregister_millisec_handler(&pmt_millisec_handler);
    device_destroy(G_class_pmt, MKDEV(G_major, 0));
    class_destroy(G_class_pmt);
    unregister_chrdev(G_major, PMT_DEVICE_NAME);
    return -ENODEV;
  }

  outw_p (MODE0, PHOTIO);
  outw_p (0, TTLOUT);

  snprintf(G_pmt_info.pmt_id, sizeof(G_pmt_info.pmt_id), "Hamamatsu R943");
  G_pmt_info.modes = PMT_MODE_INTEG;
  G_pmt_info.timetag_time_res_ns = 0;
  G_pmt_info.max_sample_period_ns = 1000000000;
  G_pmt_info.min_sample_period_ns = 1000000;
  // ??? Just for testing - determine value empirically
  G_pmt_info.dead_time_ns = 1000;

  G_cur_integ.sample_period_ns = 1000000;
  G_cur_integ.counts = inw_p(G_counter_chan);
  
  printk(KERN_DEBUG PRINTK_PREFIX "Using counter %lu (0x%lx)", (G_counter_chan - COUNTER0) / 2, G_counter_chan);

  return 0;
}
module_init(insert_module);

/** \brief Called when module is unloaded from the kernel.
 * \param (void)
 * \return (void)
 */
void remove_module(void)
{
  printk(KERN_DEBUG PRINTK_PREFIX "Module removed\n");
  unregister_millisec_handler(&pmt_millisec_handler);
  unregister_time_sync_handler(&pmt_time_sync_handler);
  device_destroy(G_class_pmt, MKDEV(G_major, 0));
  class_destroy(G_class_pmt);
  unregister_chrdev(G_major, PMT_DEVICE_NAME);
}
module_exit(remove_module);

/** \brief Called when character device opened.
 * \param inodePtr (??? FILL)
 * \param filePtr (??? FILL)
 * \return 0 (success)
 */
int pmt_open(struct inode *inodePtr, struct file *filePtr)
{
  G_num_open++;
  return 0;
}

/** \brief Return status byte to user.
 * \param filp File pointer structure.
 * \param buf (??? FILL)
 * \param count (??? FILL)
 * \param f_pos (??? FILL)
 * \return Number of bytes written to user-space.
 */
ssize_t pmt_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
  if (copy_to_user (buf, &G_status, sizeof(G_status)))
    return -EFAULT;
  G_status &= ~PMT_STAT_UPDATE;
  return (sizeof(G_status));
}

/** \brief called when character device closed.
 * \param inodePtr (??? FILL)
 * \param filePtr (??? FILL)
 *\return 0 (success).
 */
int pmt_release(struct inode *inodePtr, struct file *filePtr)
{
  G_num_open--;
  pmt_fasync(-1, filePtr, 0);
  if (G_num_open > 0)
    return 0;
  // Reset PMT shutter forced closure countrate to the default.
  G_overillum_counts = PMT_WARN_COUNTRATE;
  cancel_integ();
  return 0;
}

/** \brief Handle ioctl calls.
 * \param inodePtr (??? FILL)
 * \param filePtr (??? FILL)
 * \param ioctl_num IOCTL number.
 * \param ioctl_param IOCTL parameter.
 * \return \>= 0 on success, \< 0 on error.
 */
long pmt_ioctl(struct file *filePtr, unsigned int ioctl_num, unsigned long ioctl_param)
{
  int ret_val = 0;
  unsigned long value = 0;
  struct pmt_command tmp_cmd;

  switch (ioctl_num)
  {
    case IOCTL_GET_INFO:
      ret_val = copy_to_user((void *)ioctl_param, &G_pmt_info, sizeof(struct pmt_information));
      if (ret_val < 0)
        printk(KERN_ALERT PRINTK_PREFIX "Failed to copy PMT information to user space.\n");
      break;

    case IOCTL_GET_CUR_INTEG:
      ret_val = copy_to_user((void *)ioctl_param, &G_cur_integ, sizeof(struct pmt_integ_data));
      if (ret_val < 0)
        printk(KERN_ALERT PRINTK_PREFIX "Failed to copy current integration data/information to user space.\n");
      G_status &= ~PMT_STAT_ERR;
      break;

    case IOCTL_INTEG_CMD:
      ret_val = copy_from_user(&tmp_cmd, (void*)ioctl_param, sizeof(struct pmt_command));
      if (ret_val < 0)
      {
        printk(KERN_ALERT PRINTK_PREFIX "Failed to copy PMT command structure from user space.\n");
        break;
      }
      ret_val = order_integ(&tmp_cmd);
      if (ret_val < 0)
        printk(KERN_ALERT PRINTK_PREFIX "Failed to start integration.\n");
      break;

    case IOCTL_GET_INTEG_DATA:
      if ((G_status & PMT_STAT_DATA_READY) == 0)
      {
        printk(KERN_ALERT PRINTK_PREFIX "No data is available.\n");
        ret_val = -EINVAL;
        break;
      }
      else
        printk(KERN_DEBUG PRINTK_PREFIX "Data ready.\n");
      if (G_user_idx >= G_cur_idx)
      {
        printk(KERN_DEBUG PRINTK_PREFIX "Integration has not started (yet?)");
        ret_val = -EAGAIN;
        break;
      }
      else
        printk(KERN_DEBUG PRINTK_PREFIX "Indexes OK (integration has started.\n");
      ret_val = copy_to_user((void *)ioctl_param, &G_integ_buffer[G_user_idx%INTEG_BUFF_LEN], sizeof(struct pmt_integ_data));
      if (ret_val < 0)
      {
        printk(KERN_ALERT PRINTK_PREFIX "Failed to copy integration data to user space.\n");
        break;
      }
      else
        printk(KERN_DEBUG PRINTK_PREFIX "Copied integration data to user space.\n");
      G_user_idx++;
      if (G_user_idx >= G_cur_idx)
      {
        G_status &= ~PMT_STAT_DATA_READY;
        if ((G_status & PMT_STAT_UPDATE) == 0)
        {
          G_status |= PMT_STAT_UPDATE;
          kill_fasync(&G_async_queue, SIGIO, POLL_IN);
        }
      }
      break;

    case IOCTL_SET_OVERILLUM_RATE:
      ret_val = copy_from_user(&value, (void*)ioctl_param, sizeof(unsigned long));
      if (ret_val != 0)
      {
        printk(KERN_ERR PRINTK_PREFIX "Could not copy new overillumination count rate from user (%ld)\n", value);
        G_overillum_counts=PMT_WARN_COUNTRATE;
        break;
      }
      if (value > PMT_MAX_COUNTRATE)
      {
        printk(KERN_ERR PRINTK_PREFIX "Requested overillumination rate is too high (%ld)\n", value);
        G_overillum_counts=PMT_WARN_COUNTRATE;
        ret_val = -EINVAL;
        break;
      }
      if (value > PMT_WARN_COUNTRATE)
        printk(KERN_INFO PRINTK_PREFIX "Requested overillumination rate is higher than recommended maximum (%ld)\n", value);
      G_overillum_counts = value;
      ret_val = sizeof(unsigned long);
      break;
    
    case IOCTL_SET_CHANNEL:
      if ((G_status & (PMT_STAT_BUSY | PMT_STAT_PROBE)) > 0)
      {
        printk(KERN_ERR PRINTK_PREFIX "Cannot set counter while integration is in progress.\n");
        ret_val = -EAGAIN;
        break;
      }
      ret_val = copy_from_user(&value, (void*)ioctl_param, sizeof(unsigned long));
      if (ret_val < 0)
      {
        printk(KERN_ERR PRINTK_PREFIX "Error reading counter channel number from user space.\n");
        break;
      }
      if (value >= 4)
      {
        printk(KERN_ERR PRINTK_PREFIX "Invalid counter channel specified (%ld > 3)\n", value);
        ret_val = -EINVAL;
        break;
      }
      G_counter_chan = COUNTER0 + (value*2);
      G_cur_integ.counts = inw_p(G_counter_chan);
      printk(KERN_DEBUG PRINTK_PREFIX "Using counter %lu (0x%lx)", (G_counter_chan - COUNTER0) / 2, G_counter_chan);
      break;

    default:  // Invalid IOCTL number
      ret_val = -ENOTTY;
      break;
  }
  return(ret_val);
}

/** \brief Called when a programme registers for asynchronous notifications.
 * \return (check documentation for fasync_helper, should be >= 0 on success).
 *
 * A programme can register for asynchronous notifications, which means that whenever
 * kill_fasync(&G_async_queue, SIGIO, POLL_IN) is called from within the driver, the programme will
 * receive a SIGIO signal.
 */
int pmt_fasync(int fd, struct file *filp, int mode)
{
  return fasync_helper(fd, filp, mode, &G_async_queue);
}

/** \brief Handler for millisecond interrupt (from time_driver)
 * \param unit Universal time (seconds component)
 * \param unit_msec Universal time (milliseconds component)
 *
 * Responds in one of three ways, depending on what the driver is currently doing:
 * -# If driver is performing pre-integration probe:
 *   - Call check_probe
 * -# If driver is integrating:
 *   - Call check_integ
 * -# If driver is idle:
 *   - Set internal time to input parameters
 *   - Read counts from PMT hardware
 *   - Check for error/warning, depending on current counts
 *   - Handle any error/warning condition appropriately
 *     - If driver is not open, set error code and enable/disable error status flag
 *     - If driver is open and an error has already been raised, append error code
 *     - If driver is open and no error condition is active, set error coe and status flag as appropriate
 */
void pmt_millisec_handler(unsigned long unit, unsigned short unit_msec)
{
  unsigned char new_err;
  if ((G_status & PMT_STAT_PROBE) != 0)
  {
    check_probe(1000000L, unit, unit_msec*1000000L);
    return;
  }
  if ((G_status & PMT_STAT_BUSY) != 0)
  {
    check_integ(1000000L, unit, unit_msec*1000000L);
    return;
  }
  G_cur_integ.start_time_s = unit;
  G_cur_integ.start_time_ns = unit_msec * 1000000L;
  G_cur_integ.counts = inw_p(G_counter_chan);
  new_err = check_counts(G_cur_integ.error, G_cur_integ.counts, 1000000);

  if (G_num_open == 0)
  {
    // Driver is closed
    G_cur_integ.error = new_err;
    if (new_err)
      G_status |= PMT_STAT_ERR;
    else
      G_status &= ~PMT_STAT_ERR;
  }
  else if (G_status & PMT_STAT_ERR)
    // Driver is open, an error status is already active
    G_cur_integ.error |= new_err;
  else
  {
    // Driver is open, no error status is active (either because no error has occurred yet or because the last reported error has already been read by user-space)
    if (new_err)
    {
      G_status |= PMT_STAT_ERR;
      if ((G_status & PMT_STAT_UPDATE) == 0)
      {
        G_status |= PMT_STAT_UPDATE;
        kill_fasync(&G_async_queue, SIGIO, POLL_IN);
      }
    }
    G_cur_integ.error = new_err;
  }
}

/** \brief Called when time driver loses/gains time synchronization
 * \param synced True if time synchronized
 *
 * Sets error codes and error status flag as appropriate
 */
void pmt_time_sync_handler(char synced)
{
  if (synced)
    G_cur_integ.error &= ~PMT_ERR_TIME_SYNC;
  else
    G_cur_integ.error |= PMT_ERR_TIME_SYNC;
  G_status |= PMT_STAT_ERR;
  if ((G_status & PMT_STAT_UPDATE) == 0)
  {
    G_status |= PMT_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  }
}

/** \brief Checks what errors (if any) to raise given latest count rate
 * \param cur_err Last error code
 * \param counts Last counts read from PMT hardware
 * \param sample_p_ns Period with which counts are sampled in current configuration
 * \return New error code
 *
 * Checks for:
 * - Overillumination (count rate greater than user-specified/default maximum allowable count rate)
 * - High count rate (count rate greater than recommended maximum)
 * - Zero counts (counts practically zero)
 * In each case it checks if an error should be raised, prints a message if a new error is raised 
 * and prints a message if an old error has cleared.
 */
unsigned char check_counts(unsigned char cur_err, unsigned long counts, unsigned long sample_p_ns)
{
  unsigned char new_err = cur_err & PMT_EXT_ERR_MASK;
  unsigned long count_rate_s = counts * (1000000000 / sample_p_ns);

  if (count_rate_s >= G_overillum_counts)
  {
    new_err |= PMT_ERR_OVERILLUM;
    if ((cur_err & PMT_ERR_OVERILLUM) == 0)
    {
      printk(KERN_INFO PRINTK_PREFIX "CRITICAL: Count rate beyond specified limit (%lu counts/s - %lu). Closure forced.\n", G_overillum_counts, count_rate_s);
      close_pmt_shutter();
    }
  }
  else if ((count_rate_s < G_overillum_counts) && ((cur_err & PMT_ERR_OVERILLUM) != 0))
    printk(KERN_INFO PRINTK_PREFIX "Count rate below specified limit (%lu counts/s - %lu).\n", G_overillum_counts, count_rate_s);

  if (count_rate_s >= PMT_WARN_COUNTRATE)
  {
    new_err |= PMT_ERR_WARN;
    if ((cur_err & PMT_ERR_WARN) == 0)
      printk(KERN_INFO PRINTK_PREFIX "WARNING: Dangerously high count rate (%lu counts/s). Close the PMT shutter!\n", count_rate_s);
  }
  else if ((count_rate_s < PMT_WARN_COUNTRATE) && ((cur_err & PMT_ERR_WARN) != 0))
    printk(KERN_INFO PRINTK_PREFIX "Count rate down to safe levels (%lu counts/s)\n", count_rate_s);

/*  if (counts <= MIN_COUNTS_NONZERO)
  {
    new_err |= PMT_ERR_ZERO;
    if ((cur_err & PMT_ERR_ZERO) == 0)
      printk(KERN_INFO PRINTK_PREFIX "WARNING: Zero counts. Is the PMT plugged in?\n");
  }
  else if ((counts > MIN_COUNTS_NONZERO) && ((cur_err & PMT_ERR_ZERO) != 0))
    printk(KERN_INFO PRINTK_PREFIX "Counts non-zero (%lu).\n", counts);*/
  
  return new_err;
}

/** \brief Performs pre-integration probe checks
 * \param check_period_ns Period with which function is called (should be 1 ms, i.e. 1000000 ns)
 * \param unit_s Universal time (seconds component)
 * \param unit_ns Universal time (nanoseconds component)
 *
 * Sets internal time to parameters.
 * Checks for errors
 * In addition to errors checked in check_counts, check if counter overflow will occur with
 * desired integration time.
 * If an error has been raised, cancel the integration.
 * If probe timer has expired and no error was raised, start integration.
 */
void check_probe(unsigned long check_period_ns, unsigned long unit_s, unsigned long unit_ns)
{
  unsigned long tmp_overflow_counts;
  unsigned short new_err;
  
  G_cur_integ.start_time_s = unit_s;
  G_cur_integ.start_time_ns = unit_ns;
  G_cur_integ.counts = inw_p(G_counter_chan);
  new_err = check_counts(G_cur_integ.error, G_cur_integ.counts, check_period_ns);

  tmp_overflow_counts = G_cur_integ.counts * (G_pmt_cmd.sample_length*G_pmt_info.min_sample_period_ns / check_period_ns);
  if (tmp_overflow_counts >= PMT_COUNTER_MAX)
    new_err |= PMT_ERR_OVERFLOW;
  
  if (G_status & PMT_STAT_ERR)
    G_cur_integ.error |= new_err;
  else
  {
    if (new_err)
    {
      G_status |= PMT_STAT_ERR;
      if ((G_status & PMT_STAT_UPDATE) == 0)
      {
        G_status |= PMT_STAT_UPDATE;
        kill_fasync(&G_async_queue, SIGIO, POLL_IN);
      }
    }
    G_cur_integ.error = new_err;
  }
  
  if (G_cur_integ.error & PMT_CRIT_ERR_MASK)
  {
    cancel_integ();
    G_cur_integ.error = new_err & (~PMT_ERR_OVERFLOW);
    return;
  }
  if (unit_ns == 0)
    G_probe_counter--;
  if (G_probe_counter == 0)
    start_integ();
}

/** \brief Start a user-requested integration
 * \param cmd PMT integration parameters.
 * \return <0 for error, 0 for success.
 *
 * If mode is 0, cancel integration.
 * Check if hardware available
 * Check parameters of integration are valid
 * Start pre-integration probe
 */
int order_integ(struct pmt_command *cmd)
{
  if (cmd->mode == 0)
  {
    cancel_integ();
    return 0;
  }
  if ((G_status & PMT_STAT_BUSY) || (G_status & PMT_STAT_PROBE))
  {
    printk(KERN_ERR PRINTK_PREFIX "An integration is currently underway. Cannot start another.\n");
    return -EPERM;
  }
  if (cmd->mode != PMT_MODE_INTEG)
  {
    printk(KERN_ERR PRINTK_PREFIX "Invalid mode specified.\n");
    return -EINVAL;
  }
  if (cmd->sample_length < 1)
  {
    printk(KERN_ERR PRINTK_PREFIX "Invalid sample length specified.\n");
    return -EINVAL;
  }
  if (cmd->prebin_num < 1)
  {
    printk(KERN_ERR PRINTK_PREFIX "Invalid prebin number specified.\n");
    return -EINVAL;
  }
  if (cmd->repetitions < 1)
  {
    printk(KERN_ERR PRINTK_PREFIX "Invalid number of repetitions specified.\n");
    return -EINVAL;
  }
  memcpy(&G_pmt_cmd, cmd, sizeof(struct pmt_command));
  G_probe_counter = PMT_PROBE_TIME_S;
  G_status |= PMT_STAT_PROBE;
  if ((G_status & PMT_STAT_UPDATE) == 0)
  {
    G_status |= PMT_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Starting probe.\n");
  return 0;
}

/** \brief Starts a user-requested integration.
 *
 * Sets all internal counters and other variables as appropriate.
 * The integration will only REALLY start when check_integ is next called.
 */
void start_integ(void)
{
  printk(KERN_DEBUG PRINTK_PREFIX "Starting integration.\n");
  G_cur_idx = 0;
  G_user_idx = 0;
  G_cur_integ.sample_period_ns = 0;
  G_cur_integ.prebin_num = 0;
  G_cur_integ.repetitions = 0;
  G_cur_integ.counts = 0;
  G_cur_integ.error = 0;
  // Time will already have been set by millisecond handler.
  G_status |= PMT_STAT_BUSY;
  G_status &= ~(PMT_STAT_PROBE | PMT_STAT_DATA_READY);
  if ((G_status & PMT_STAT_UPDATE) == 0)
  {
    G_status |= PMT_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  }
}

/** \brief Cancel current integration
 *
 * Save whatever part of the integration that is being cancelled to the data buffer.
 * Set all internal counters and variables as appropriate.
 * The function doesn't actually cancel an integration, but merely stops check_integ from being called.
 */
void cancel_integ(void)
{
  printk(KERN_DEBUG PRINTK_PREFIX "Cancelling integration.\n");
  memcpy(&G_integ_buffer[G_cur_idx%INTEG_BUFF_LEN], &G_cur_integ, sizeof(struct pmt_integ_data));
  G_cur_idx++;
  G_status &= ~(PMT_STAT_BUSY | PMT_STAT_PROBE);
  G_cur_integ.counts = 0;
  G_cur_integ.sample_period_ns = G_pmt_info.min_sample_period_ns;
  G_cur_integ.prebin_num = 0;
  if ((G_status & PMT_STAT_UPDATE) == 0)
  {
    G_status |= PMT_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  }
}

/** \brief Checks on an integration and performs any necessary tasks (reads hardware, error checking, storing integration, etc.)
 * \param check_period_ns Period with which function is called (should be 1 ms, i.e. 1000000 ns)
 * \param unit_s Universal time (seconds component)
 * \param unit_ns Universal time (milliseconds component)
 *
 * Checks if it is time to read hardware (given requested sample period) - returns if not
 * Reads photometry hardware.
 * Checks for error condition on latest reading, handles appropriately.
 * Bins counts as requested (returns if requested number of bins not reached).
 * Copies photometry to data buffer.
 * Continues with next repetition if necessary.
 */
void check_integ(unsigned long check_period_ns, unsigned long unit_s, unsigned long unit_ns)
{
  unsigned long tmp_counts;
  unsigned short new_err;
  G_cur_integ.sample_period_ns += check_period_ns;
  if (G_cur_integ.sample_period_ns < G_pmt_cmd.sample_length*G_pmt_info.min_sample_period_ns)
    return;
  if (G_cur_integ.sample_period_ns > G_pmt_cmd.sample_length*G_pmt_info.min_sample_period_ns)
    printk(KERN_ERR PRINTK_PREFIX "Error: Requested sample period is not an integer multiple of the minimum sampling period. The sample period should have been rejected when the integration was requested (%lu %lu %lu). Sampling now.\n", check_period_ns, G_cur_integ.sample_period_ns, G_pmt_cmd.sample_length*G_pmt_info.min_sample_period_ns);
  tmp_counts = inw_p(G_counter_chan);
  new_err = check_counts(G_cur_integ.error, tmp_counts, G_cur_integ.sample_period_ns);
  G_cur_integ.counts += tmp_counts;
  G_cur_integ.prebin_num++;
  if (G_cur_integ.error != new_err)
  {
    if ((new_err) && ((G_status & PMT_STAT_ERR) == 0))
    {
      G_status |= PMT_STAT_ERR;
      if ((G_status & PMT_STAT_UPDATE) == 0)
      {
        G_status |= PMT_STAT_UPDATE;
        kill_fasync(&G_async_queue, SIGIO, POLL_IN);
      }
    }
    G_cur_integ.error |= new_err;
  }
  if (G_cur_integ.error & PMT_CRIT_ERR_MASK)
  {
    cancel_integ();
    return;
  }
  if (G_cur_integ.prebin_num < G_pmt_cmd.prebin_num)
  {
    G_cur_integ.sample_period_ns = 0;
    return;
  }
  if (G_cur_integ.prebin_num > G_pmt_cmd.prebin_num)
    printk(KERN_ERR PRINTK_PREFIX "Error: Requested prebinning number exceeded. This should not have happend.\n");
  memcpy(&G_integ_buffer[G_cur_idx%INTEG_BUFF_LEN], &G_cur_integ, sizeof(struct pmt_integ_data));
  G_cur_idx++;
  G_status |= PMT_STAT_DATA_READY;
  G_cur_integ.repetitions++;
  G_cur_integ.start_time_s = unit_s;
  G_cur_integ.start_time_ns = unit_ns;
  G_cur_integ.sample_period_ns = 0;
  G_cur_integ.prebin_num = 0;
  G_cur_integ.counts = 0;
  G_cur_integ.error = new_err;
  if (G_cur_integ.repetitions >= G_pmt_cmd.repetitions)
    finish_integ();
  if ((G_status & PMT_STAT_UPDATE) == 0)
  {
    // Only signal user-space if more than 1 second has elapsed since last data read.
    if (((G_integ_buffer[G_cur_idx%INTEG_BUFF_LEN].start_time_s - G_integ_buffer[G_user_idx%INTEG_BUFF_LEN].start_time_s)*1000000000L + G_integ_buffer[G_cur_idx%INTEG_BUFF_LEN].start_time_ns - G_integ_buffer[G_user_idx%INTEG_BUFF_LEN].start_time_ns) > 1000000000L)
    {
      G_status |= PMT_STAT_UPDATE;
      kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    }
  }
}

/** \brief Finalises all internal counters and variables after completion of a series of integrations.
 */
void finish_integ(void)
{
  G_status &= ~(PMT_STAT_BUSY | PMT_STAT_PROBE);
  G_cur_integ.counts = 0;
  G_cur_integ.sample_period_ns = G_pmt_info.min_sample_period_ns;
  G_cur_integ.prebin_num = 0;
  if ((G_status & PMT_STAT_UPDATE) == 0)
  {
    G_status |= PMT_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  }
}
