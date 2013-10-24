/**
 * \file merlin_driver.c
 * \author Pierre van Heerden
 * \brief Implementation of MERLIN CCD driver
 *
 * This driver provides a simple interface for programmes that need to use a MERLIN acquisition CCD.
 *
 * The driver and CCD's status is determined by reading from the driver's character device.
 * The modes the CCD and driver support are read via IOCTL.
 * Exposure commands are sent to the CCD via IOCTL.
 * Images are retrieved from the driver (after being read out from the CCD) via IOCTL.
 *
 * The driver was also designed to be able to function without a MERLIN CCD present and to work with an
 * outside programem to simulate the presence of a CCD. From the point of view of a programme using the
 * driver, there is virtually no detectable difference between the two modes. Three additional IOCTLs
 * are provided for this purpose
 *
 * \todo User proper kernel log levels
 * \todo Implement the window modes supported by the MERLIN CCD.
 */

/// Simulator compile flag
// #define ACQSIM

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/workqueue.h>
#include <linux/sched.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kthread.h>

#ifndef ACQSIM
 #include <act_plc/act_plc.h>
 #include <time_driver/time_driver.h>
#endif
#include "merlin_driver.h"
#include "ccd_defs.h"

#define PRINTK_PREFIX "[ACT_MERLIN] "

#define TRUE  1
#define FALSE 0

//==========================
//  CCD definitions
//==========================
/** \brief Definitions for low-level interaction with CCD controller.
 * \{ */
/// Port through which data is read from the CCD
#define   READ_DATA        0x2B8
/// Port through which data is written to the CCD
#define   WRITE_DATA       0x2B9
/// Port that indicates whether the CCD is ready to receive input
#define   INPUT_STATUS     0x2BA
/// Port that indicates whether the CCD is ready to send output
#define   OUTPUT_STATUS    0x2BB
/// Port 8212 ?
#define   PORT_8212        0x2BC

/// Number of times to retry sending to/receiving from CCD
#define   COMM_NUM_RETRIES      100000
/// Delay after the INPUT_STATUS/OUTPUT_STATUS flags are activated before data is read/written
#define   COMM_RETRY_DELAY_uS   10

/// Low divisor of integration time as understood by the CCD, in milli-seconds
#define   EXPT_LO_DIV   40
/// High divisor of integration time as understood by the CCD, in milli-seconds
#define   EXPT_HI_DIV   10260
/// Highest possible multiplier.
#define   EXPT_MAX_MULT 255

/// Minimum possible exposure time.
#define   MIN_EXP_TIME EXPT_LO_DIV
/// Maximum possible exposure time.
#define   MAX_EXP_TIME (EXPT_HI_DIV*EXPT_MAX_MULT)+(EXPT_LO_DIV*EXPT_MAX_MULT)

/// Width of CCD in pixels in full-frame mode (no prebinning, no windowing)
#define   WIDTH_PX     407
/// Height of CCD in pixels in full-frame mode (no prebinning, no windowing)
#define   HEIGHT_PX    288
/// On-sky width of CCD in full-frame mode (no prebinning, no windowing), in arcminutes
#define   RA_WIDTH     900
/// On-sky height of CCD in full-frame mode (no prebinning, no windowing), in arcminutes
#define   DEC_HEIGHT   656
/** \} */

/** \name Gloval variables
 * \{ */
/// Driver/CCD status
static unsigned char G_status = 0;
/// Driver major number
static int G_major;
/// Device class structure
static struct class *G_class_merlin;
/// Device structure
static struct device *G_dev_merlin;
/// Modes suppored by CCD/driver
static struct ccd_modes G_modes;
/// Last command sent to driver
static struct ccd_cmd G_cmd;
/// Last image retrieved by driver
static struct merlin_img G_img;
/// Queue for asynchronous communication with user-space programmes
static struct fasync_struct *G_async_queue;
static wait_queue_head_t inq;
#ifndef ACQSIM
 /// Driver work queue
 struct workqueue_struct *ccd_workq;
 /// Work structure for reading out the CCD.
 struct delayed_work readout_work;
 /// Work structure for checking that the turn-of-a-second handler is being received.
 struct delayed_work sec_handler_check_work;
 /// Task structure for sending exposure commands to CCD.
 struct task_struct *G_send_exp_ts = NULL;
#endif
/** \} */

/** \name Function definitions
 ( \{ */
#ifndef ACQSIM
 /// Send character to CCD
 static short ccd_send_char(unsigned char c);
 /// Receive character from CCD
 static short ccd_recv_char(void);
 /// Retrieve ID string from CCD
 static char get_ccd_id(void);
 /// Retrieve image from CCD
 static void ccd_readout(struct work_struct *work);
 /// Send exposure command to CCD (runs in a parallel thread).
 static int start_exp(void *data);
 /// Starts an exposure immediately
 static void start_exp_now(void);
 /// Starts an exposure at the turn of a second.
 static void sec_turn_handler(const unsigned long loct, const unsigned short loct_msec);
 /// Checks whether the driver is receiving notifications at the turn of a second.
 static void sec_handler_check(struct work_struct *work);
#endif
/// Module initialisation function
int init_module(void);
/// Module cleanup/exit function
void cleanup_module(void);
/// Character device opened
static int device_open(struct inode *, struct file *);
/// Character device released
static int device_release(struct inode *, struct file *);
/// IOCTL called on character device
static long device_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param);
/// Read from character device
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
/// Write to character device
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
/// Register asynchronous notification
static int device_fasync(int fd, struct file *filp, int mode);
static unsigned int device_poll(struct file *filp, poll_table *wait);
/** \} */

/** \brief Structure containing file operations (on character device) supported by driver.
 * \{ */
static struct file_operations fops =
{
  .owner = THIS_MODULE,
  .read = device_read,
  .write = device_write,
  .unlocked_ioctl = device_ioctl,
  .open = device_open,
  .release = device_release,
  .fasync = device_fasync,
  .poll = device_poll
};
/** \} */

/** \brief Called when the modules is loaded.
 * \return 0 on success
 *
 * Initialises all variables of the driver.
 * Algorithm:
 * - Registers the driver's character device.
 * - If not in simulation mode:
 *   - Initialises CCD.
 *   - Retrieves CCD identifier.
 *   - Sets available CCD modes.
 *   - Initialises kernel work queue and task structures.
 *   - Registers the turn-of-second handler function with external timing providor.
 * - Initialises command and image structures.
 */
int init_module(void)
{
  G_major = register_chrdev(0, MERLIN_DEVICE_NAME, &fops);
  if (G_major < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Can't get major number\n");
    return(G_major);
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Module inserted. Assigned major: %d\n", G_major);
  
  G_class_merlin = class_create(THIS_MODULE, MERLIN_DEVICE_NAME);
  if (G_class_merlin == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device class.\n" );
    unregister_chrdev(G_major, MERLIN_DEVICE_NAME);
    return -ENODEV;
  }
  
  G_dev_merlin = device_create(G_class_merlin, NULL, MKDEV(G_major, 0), NULL, MERLIN_DEVICE_NAME);
  if (G_dev_merlin == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device.\n" );
    unregister_chrdev(G_major, MERLIN_DEVICE_NAME);
    class_destroy(G_class_merlin);
    return -ENODEV;
  }
  
  #ifndef ACQSIM
    outb(0x08, PORT_8212);
    outb(0x00, PORT_8212);
  #endif

  #ifndef ACQSIM
    if (get_ccd_id() == 0)
    {
      printk(KERN_ALERT PRINTK_PREFIX "Failed to get identifier for CCD. Exiting.\n");
      device_destroy(G_class_merlin, MKDEV(G_major, 0));
      class_destroy(G_class_merlin);
      unregister_chrdev(G_major, MERLIN_DEVICE_NAME);
      return 1;
    }
  #endif
  G_modes.prebin = CCD_MODE_PREBIN_1x1;
  memset(G_modes.windows, 0, sizeof(G_modes.windows));
  G_modes.windows[0].width_px = WIDTH_PX;
  G_modes.windows[0].height_px = HEIGHT_PX;
  G_modes.min_exp_t_msec = MIN_EXP_TIME;
  G_modes.max_exp_t_msec = MAX_EXP_TIME;
  G_modes.max_width_px = WIDTH_PX;
  G_modes.max_height_px = HEIGHT_PX;
  G_modes.ra_width_asec = RA_WIDTH;
  G_modes.dec_height_asec = DEC_HEIGHT;
  #ifndef ACQSIM
    ccd_workq = create_singlethread_workqueue("MERLIN_workq");
    INIT_DELAYED_WORK(&readout_work, ccd_readout);
    INIT_DELAYED_WORK(&sec_handler_check_work, sec_handler_check);
    register_second_handler(&sec_turn_handler);
  #endif

  init_waitqueue_head(&inq);

  G_cmd.exp_t_msec = 0;
  memset(&G_img.img_data, 0, sizeof(G_img.img_data));

  return 0;
}

/** \brief Called when the modules is removed.
 * \return (void)
 *
 * Finalises all operations before driver is removed.
 * Algorithm:
 * - Signals parallel threads of execution that the module will exit.
 * - If not in simulation mode:
 *   - Unregisters the turn-of-second handler function with external timing providor.
 *   - Cancels all pending operations (esp. readouts)
 * - Unregisters driver's character device.
 */
void cleanup_module(void)
{
  G_status |= MERLIN_EXIT;
  #ifndef ACQSIM
  unregister_second_handler(&sec_turn_handler);
  if (cancel_delayed_work(&readout_work) == 0)
    flush_workqueue(ccd_workq);
  if (G_status & (CCD_INTEGRATING | CCD_READING_OUT))
    printk(KERN_INFO PRINTK_PREFIX "CCD is currently integrating/reading out. It may be necessary to restart the MERLIN crate after module is unloaded\n");
  destroy_workqueue(ccd_workq);
  #endif
  device_destroy(G_class_merlin, MKDEV(G_major, 0));
  class_destroy(G_class_merlin);
  unregister_chrdev(G_major, MERLIN_DEVICE_NAME);
  printk(KERN_INFO PRINTK_PREFIX "MERLIN CCD driver unloaded.\n");
}

#ifndef ACQSIM
/** \brief Send a character to the CCD.
 * \param c The character to be sent.
 * \return 1 on success, 0 on failure.
 *
 * Algorithm:
 * - Retry for a time more or less equivalent to COMM_NUM_RETRIES*COMM_RETRY_DELAY_uS:
 *   - If the OUTPUT_STATUS port is high, the CCD is ready to receive the character, so break out of the 
 *     loop.
 *   - If OUTPUT_STATUS is low, yield the CPU to other processes, then try again.
 * - If number of retries reached, return failure.
 * - If CCD is ready, delay for a short period, then send the character.
 *
 * \note The short period between breaking out of the loop when OUTPUT_STATUS is high before the character
 *       is sent was introduced because of timing issues with I/O with the CCD controller. The correct
 *       length of the delay was determined by trial and error
 */
static short ccd_send_char(unsigned char c)
{
  unsigned int i;
  for (i=0; i<=COMM_NUM_RETRIES*COMM_RETRY_DELAY_uS/5; i++)
  {
    if (inb_p(OUTPUT_STATUS))
      break;
    yield();
  }
  if (i == COMM_NUM_RETRIES)
    return 0;
  udelay(COMM_RETRY_DELAY_uS);
  outb_p(c,WRITE_DATA);
  return 1;
}

/** \brief Read a character from the CCD.
 * \return The character on success, -1 on failure.
 *
 * Algorithm:
 * - Retry for a time more or less equivalent to COMM_NUM_RETRIES*COMM_RETRY_DELAY_uS:
 *   - If the INPUT_STATUS port is high, the CCD is ready
 *     - Delay for a short period, then read and return the character
 *   - If INPUT_STATUS is low, yield the CPU to other processes, then try again.
 * - If number of retries reached, return failure.
 *
 * \note The short period between breaking out of the loop when INPUT_STATUS is high before the character
 *       is read was introduced because of timing issues with I/O with the CCD controller. The correct
 *       length of the delay was determined by trial and error
 */
static short ccd_recv_char(void)
{
  unsigned int i;
//   for (i=0; i<COMM_NUM_RETRIES*COMM_RETRY_DELAY_uS/5; i++)
  for (i=0; i<COMM_NUM_RETRIES*COMM_RETRY_DELAY_uS/2; i++)
  {
    if (inb(INPUT_STATUS))
    {
//       udelay(COMM_RETRY_DELAY_uS);
      return inb_p(READ_DATA);
    }
    yield();
  }
  return -1;
}

/** \brief Reads CCD identifier from CCD controller and saves it in G_modes structure.
 * \return (void)
 *
 * Algorithm:
 * - Send the 'Y' (identifier request) character to the CCD.
 *   - If send was unsuccessful, report error and return.
 * - Read characters of identifier string one by one from CCD into a temporary buffer.
 *   - When no more characters can be read, terminate the buffered string and break out of the loop.
 * - If no characters were read, report an error and exit.
 * - If characters were read, copy them to the G_modes.ccd_id variable.
 */
static char get_ccd_id(void)
{
  int i;
  char buf[51], lastchar=0, len=0;
  if (ccd_send_char('Y') < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error: Could not get status information from CCD.\n");
    G_status |= CCD_ERROR;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&inq);
    return 0;
  }
  for (i=0; i<50; i++)
  {
    lastchar = ccd_recv_char();
    if (lastchar >= 0)
      break;
    if (G_status & MERLIN_EXIT)
      return 0;
  }
  if (lastchar < 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Failed get get identifier for CCD. Check CCD connection.\n");
    return 0;
  }

  for (i=0; i<50; i++)
  {
    lastchar = ccd_recv_char();
    if (lastchar < 0)
    {
      buf[i] = '\0';
      len = i;
      break;
    }
    if (G_status & MERLIN_EXIT)
      return 0;
    buf[i] = lastchar;
    udelay(COMM_RETRY_DELAY_uS);
  }
  if (len <= 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "No characters received after trying to retrieve CCD ID.\n");
    snprintf(G_modes.ccd_id, sizeof(G_modes.ccd_id)-1, "UNKWN");
    return 0;
  }
  if (lastchar >= 0)
    printk(KERN_INFO PRINTK_PREFIX "Too many characters (>%d) returned by CCD as identifier string.\n", sizeof(buf));
  snprintf(G_modes.ccd_id, sizeof(G_modes.ccd_id)-1, "%s", buf);
  printk(KERN_INFO PRINTK_PREFIX "Received CCD identifier: %s\n", G_modes.ccd_id);
  return 1;
}

/** \brief Function to read out the CCD in parallel with other threads in the driver.
 * \param work Details of work item on driver's work queue - unused.
 *
 * Algorithm:
 * - If an internal CCD error has occurred or the CCD is not integrating, report the situation and return.
 * - Read first pixel from CCD.
 *    - If this step fails, something's gone wrong. Report and return.
 * - Read the pixels one by one from the CCD, stopping when no more pixels can be read.
 *   - The loop continues for twice the number of pixels than is expected. Experience has taught that
 *     the CCD does not always return the expected number of pixels (see the notes). This unfortunately
 *     means that this function wastes a short (less than 1 sec) length of time trying to read a pixel 
 *     when none can be read.
 *   - Return from the function if the module is about to exit.
 *   - If a pixel was successfully read, the pixel read in the previous iteration was also a pixel.
 *     - Save the previous pixel to the G_img.img_data array.
 *   - If a pixel was not successfully read, the previous pixel was the CCD status, which should be 0.
 * - Determine if the CCD is OK to continue (the last "pixel" returned is 0). If not, report an error.
 * - Set the image parameters of the G_img structure as appropriate.
 * - Signal that the CCD is finished reading out.
 *
 * \note An obscene amount of development and testing has gone into this driver and probably more is
 *       necessary before the driver will be stable. The biggest problem is interacting with the CCD
 *       controller, the parameters of the interaction needed much fine-tuning. It also seems that the
 *       controller is subject to interference from anything more powerful than a common household
 *       electric toaster. This is also why there are so many debugging messages in this function, which
 *       the reader can identify by the KERN_DEBUG log level macro in the printk statements. These
 *       messages clutter up this code as well as the system logs, but are, for the moment, a necessary
 *       evil. Once it has been demonstrated that the driver works well and is stable, these messages
 *       can be removed, provided someone with a pacemaker doesn't get too near the dome.
 */
static void ccd_readout(struct work_struct *work)
{
  unsigned int i;
  short tmpchar;
  ccd_pixel_type lastchar;

  if (G_status & CCD_ERROR)
  {
    printk(KERN_ERR PRINTK_PREFIX "CCD currently unavailable. Please see the system log.\n");
    return;
  }
  if (!(G_status & CCD_INTEGRATING))
  {
    printk(KERN_INFO PRINTK_PREFIX "CCD not currently integrating. Order an integration first.\n");
    return;
  }
  for (i=0; i<50; i++)
  {
    tmpchar = ccd_recv_char();
    if (tmpchar >= 0)
      break;
    if (G_status & MERLIN_EXIT)
      return;
  }
  if (tmpchar < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error: No pixels received from CCD.\n");
    G_status |= CCD_ERROR | CCD_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&inq);
    printk(KERN_INFO PRINTK_PREFIX "Requesting camera reset.\n");
    reset_acq_merlin();
    return;
  }
  lastchar = (ccd_pixel_type)tmpchar;
  printk(KERN_DEBUG PRINTK_PREFIX "Reading out CCD\n");
  G_status &= ~CCD_INTEGRATING;
  G_status |= CCD_READING_OUT | CCD_STAT_UPDATE;
  kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  wake_up_interruptible(&inq);
  for (i=1; i<MERLIN_MAX_IMG_LEN*2; i++)
  {
    udelay(COMM_RETRY_DELAY_uS);
    tmpchar = ccd_recv_char();
    if (tmpchar < 0)
      break;
    if (G_status & MERLIN_EXIT)
      return;
    G_img.img_data[(i-1)%MERLIN_MAX_IMG_LEN] = lastchar;
    lastchar = (ccd_pixel_type)tmpchar;
  }

  G_status &= ~CCD_READING_OUT;
  if (lastchar != 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "CCD reported error %hhu while reading out.\n", lastchar);
    G_status |= CCD_ERROR | CCD_STAT_UPDATE;
    printk(KERN_INFO PRINTK_PREFIX "Requesting camera reset.\n");
    reset_acq_merlin();
  }
  else
  {
    if (i != MERLIN_MAX_IMG_LEN)
      printk(KERN_DEBUG PRINTK_PREFIX "Read %u pixels (should be %d)\n", i, MERLIN_MAX_IMG_LEN);
    G_status |= IMG_READY | CCD_STAT_UPDATE;
  }
  kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  wake_up_interruptible(&inq);
  G_img.img_params.img_len = MERLIN_MAX_IMG_LEN;
  G_img.img_params.img_width = WIDTH_PX;
  G_img.img_params.img_height = HEIGHT_PX;
}

/** \brief Send CCD expose command to CCD.
 * \param data Data sent to this function by the calling function - unused.
 * \return 0 - check the kernel documenation for the meaning of a non-zero return value.
 * 
 * This function is called in a parallel thread of execution to the thread of the function that calls it.
 * This is because one of the functions that calls this function is the turn-of-the-second handler
 * function, which is called by an external timing providor and sending the CCD expose command to the 
 * CCD can take a long time (a large fraction of a second). If the function in the external timing
 * providor were held up while the CCD expose command is sent to the CCD (waiting for this function to
 * return), it would introduce an unacceptably large error in the timing it was providing.
 *
 * Algorithm:
 * - Check that the driver status indicates that a new exposure can be started.
 * - If the exposure time is 0 (specifically, less than the minimum exposure time of the CCD), this 
 *   function was probably called in error. Ignore.
 * - Set the parameters of the G_img and G_cmd structures as appropriate.
 * - Send the expose command to the CCD. For each character, if the character could not be sent,
 *   report an error and return.
 *   - Send the 'R' (readout) character.
 *   - Send the exposure time low multiplier (integer multiple of EXPT_LO_DIV) character.
 *   - Send the exposure time high multiplier (integer multiple of EXPT_HI_DIV) character.
 * - Enqueue the CCD readout function, which should be started at the time when the readout is about 
 *   to start.
 * - Change the driver status and signal the user-space programme.
 * 
 * \note Check what should really happen with G_send_exp_ts when the programme returns.
 */
static int start_exp(void *data)
{
  struct timespec tmpts;
  if (G_status & (CCD_ERROR | EXP_ORDERED | CCD_INTEGRATING | CCD_READING_OUT))
  {
    printk(KERN_INFO PRINTK_PREFIX "Driver status indicates that CCD is currently busy (%hu).\n", G_status);
    return 0;
  }
  if (G_cmd.exp_t_msec < MIN_EXP_TIME)
  {
    printk(KERN_INFO PRINTK_PREFIX "Strange, start_exp was called, but exposure time is 0. Ignoring\n");
    G_send_exp_ts = NULL;
    return 0;
  }
  G_img.img_params.exp_t_msec = ((G_cmd.exp_t_msec%EXPT_HI_DIV)/EXPT_LO_DIV)*EXPT_LO_DIV + (G_cmd.exp_t_msec/EXPT_HI_DIV)*EXPT_HI_DIV;
  G_img.img_params.prebin = CCD_MODE_PREBIN_1x1;
  G_img.img_params.window = 0;
  
  G_cmd.exp_t_msec = 0;
  if (!ccd_send_char('R'))
  {
    printk (KERN_ERR PRINTK_PREFIX "Error sending exposure command to CCD. Maximum number of retries reached.\n");
    G_send_exp_ts = NULL;
    G_status |= CCD_ERROR | CCD_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&inq);
    printk(KERN_ERR PRINTK_PREFIX "Requesting ACQ reset.\n");
    reset_acq_merlin();
    return 0;
  }
  if (!ccd_send_char((G_img.img_params.exp_t_msec%EXPT_HI_DIV)/EXPT_LO_DIV))
  {
    printk (KERN_ERR PRINTK_PREFIX "Error sending exposure command to CCD. Maximum number of retries reached.\n");
    G_send_exp_ts = NULL;
    G_status |= CCD_ERROR | CCD_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&inq);
    printk(KERN_ERR PRINTK_PREFIX "Requesting ACQ reset.\n");
    reset_acq_merlin();
    return 0;
  }
  if (!ccd_send_char(G_img.img_params.exp_t_msec/EXPT_HI_DIV))
  {
    printk (KERN_ERR PRINTK_PREFIX "Error sending exposure command to CCD. Maximum number of retries reached.\n");
    G_send_exp_ts = NULL;
    G_status |= CCD_ERROR | CCD_STAT_UPDATE;
    kill_fasync(&G_async_queue, SIGIO, POLL_IN);
    wake_up_interruptible(&inq);
    printk(KERN_ERR PRINTK_PREFIX "Requesting ACQ reset.\n");
    reset_acq_merlin();
    return 0;
  }
  tmpts.tv_sec = G_img.img_params.exp_t_msec / 1000;
  tmpts.tv_nsec = (G_img.img_params.exp_t_msec % 1000) * 1000000;
  queue_delayed_work(ccd_workq, &readout_work, timespec_to_jiffies(&tmpts));
  G_status |= CCD_INTEGRATING | CCD_STAT_UPDATE;
  kill_fasync(&G_async_queue, SIGIO, POLL_IN);
  wake_up_interruptible(&inq);
  G_send_exp_ts = NULL;
  return 0;
}

/** \brief Start an exposure immediately.
 * \return (void)
 * 
 * Algorithm:
 * - Check that the start_exp function is not already running in a separate thread.
 * - Read the time from the external timing providor and set the integration start time in G_img.
 * - Call the start_exp function (in a separate thread).
 */
static void start_exp_now(void)
{
  unsigned long loct;
  unsigned short loct_msec;
  if (G_send_exp_ts != NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error: Start of exposure has already been scheduled, cannot schedule a second.\n");
    return;
  }
  get_unitime(&loct, &loct_msec);
  G_img.img_params.start_hrs = loct / 3600;
  G_img.img_params.start_min = (loct / 60) % 60;
  G_img.img_params.start_sec = loct % 60;
  G_img.img_params.start_msec = loct_msec;
  kthread_run(start_exp,NULL,"start_exp_thread");
}

/** \brief Turn-of-the-second handler function.
 * \param loct Local time in integer seconds.
 * \param loct_msec Fractional seconds as integer milli-seconds.
 * \return (void)
 *
 * For science exposures, one would expect that the exposure be started at the turn of a second. However,
 * this driver does not have the ability to accurately determine when is the turn of a second. It
 * therefore requires a signal from another kernel module that can "tell" it when is the turn of a
 * second. This is why this driver registers this function as a turn-of-the-second handler function
 * with the time_driver. When the time_driver detects the turn of a second, it calls this function. So,
 * when a science exposure is requested by a programme (which can be identified by the start_at_sec field
 * of the given ccd_cmd structure), this driver knows that an exposure should be started the next time
 * this function is called.
 * 
 * The start_at_sec field of the G_cmd structure is 0 when no new exposure has been ordered by a programme
 * in the last second or when a new exposure had been ordered, but it was started immediately (i.e. a
 * non-science exposure). 
 * 
 * It is also possible (however unlikely) that the external timing providor is suffering from an internal
 * error, which prevents it from calling this function. In this case, there is a function
 * (sec_handler_check) that is called to take appropriate action 2 seconds on the system clock after
 * the exposure was ordered by the user-space programme.
 *
 * Algorithm:
 * - If an exposure should not be started at the turn of a second, return.
 * - Remove the sec_handler_check function from the driver's work queue.
 * - Check that the start_exp function is not already running in a separate thread.
 * - Unset the start_at_sec field (i.e. don't start a new exposure at the next turn of a second).
 * - Use the time from the external timing providor (via this function's parameters) and set the
 *   integration start time in G_img.
 * - Call the start_exp function (in a separate thread).
 */
static void sec_turn_handler(const unsigned long loct, const unsigned short loct_msec)
{
  if (G_status & CCD_ERROR)
  {
    if (reset_acq_pending() == 0)
    {
      G_status &= (~CCD_ERROR);
      kill_fasync(&G_async_queue, SIGIO, POLL_IN);
      wake_up_interruptible(&inq);
    }
  }
  if (!G_cmd.start_at_sec)
    return;
  cancel_delayed_work(&sec_handler_check_work);
  if (G_send_exp_ts != NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Error: Start of exposure has already been scheduled, cannot schedule a second.\n");
    return;
  }
  G_cmd.start_at_sec = FALSE;
  G_img.img_params.start_hrs = loct / 3600;
  G_img.img_params.start_min = (loct / 60) % 60;
  G_img.img_params.start_sec = loct % 60;
  G_img.img_params.start_msec = loct_msec;
  kthread_run(start_exp,NULL,"start_exp_thread");
}

/** \brief Check that an exposure was started at the last turn of a second.
 * \param work Details of work item on driver's work queue - unused.
 *
 * When an exposure is ordered that needs to be started at the turn of a second, this function is put
 * on the driver's work queue to be executed about 2 seconds after the exposure is ordered. So, if the
 * driver is waiting for the turn-of-the-second signal, but it doesn't come in a reasonable length of
 * time, this function starts the exposure anyway.
 * 
 * This function should ideally never be executed, however it was implemented to keep the driver from
 * endlessly waiting for a timing signal that never comes.
 * 
 * Algorithm:
 * - Report the error.
 * - Get the system time and set the exposure start time fields.
 *   - Make sure the start_msec field is non-zero, to indicate to the programme that will read the image
 *     that the exposure was not necessarily started at the turn of a second.
 * - Call the start_exp function (in a separate thread).
 */
static void sec_handler_check(struct work_struct *work)
{
  struct timeval tv;
  extern struct timezone sys_tz;
  unsigned long loct_ms;

  printk(KERN_ERR PRINTK_PREFIX "Error: No signal from time driver. Starting exposure now.\n");
  do_gettimeofday(&tv);
  loct_ms = ((tv.tv_sec - sys_tz.tz_minuteswest*60) % 86400)*1000 + tv.tv_usec / 1000;
  G_cmd.start_at_sec = FALSE;
  G_img.img_params.start_hrs = loct_ms / 3600000;
  G_img.img_params.start_min = (loct_ms / 60000) % 60;
  G_img.img_params.start_sec = (loct_ms / 1000) % 60;
  G_img.img_params.start_msec = loct_ms % 1000;
  if (G_img.img_params.start_msec == 0)
    G_img.img_params.start_msec = 1;
  kthread_run(start_exp,NULL,"start_exp_thread");
}
#endif

/** \brief Called when a programme tries to open the driver's character device.
 * \return 0 (success)
 */
static int device_open(struct inode *inode, struct file *file)
{
  G_status |= CCD_STAT_UPDATE;
  wake_up_interruptible(&inq);
  return 0;
}

/** \brief Called when a programme that previously opened the driver's character device, closes it again.
 * \return 0 (success)
 */
static int device_release(struct inode *inode, struct file *file)
{
  device_fasync(-1, file, 0);
  return 0;
}

/** \brief Called when a programme does an IOCTL call on the driver's character device.
 * \return 0 on success, <0 on failure.
 * 
 * IOCTL_GET_IMAGE, IOCTL_ORDER_EXP and IOCTL_GET_MODES are always supported.
 * IOCTL_SET_IMAGE, IOCTL_GET_CMD and IOCTL_SET_MODES are only available if the ACQSIM compiler flag was
 * active at compile time.
 * If an invalid IOCTL number is supplied, -ENOTTY is returned.
 * 
 * IOCTL_GET_IMAGE:
 * - Send the image to the calling programme
 * - Unset the IMG_READY status flag.
 * IOCTL_ORDER_EXP:
 * - Check that the CCD is ready for an expose command.
 * - Copy the exposure parameters (ccd_cmd struct) from the calling programme.
 * - Check that the requested mode and integration time are supported by the driver and CCD.
 * - If the ACQSIM compiler flag was enabled at compile-time, only set the driver status.
 * - If the ACQSIM compiler flag was disable at compile-time:
 *   - If the exposure needs to be started immediately (G_cmd.start_at_sec is disabled), order the
 *     immediately.
 *   - If the exposure needs to be started at the next turn of a second, the sec_turn_handler function
 *     will take care of starting the exposure. Only enqueue the function that checks that the 
 *     sec_turn_handler function was called to be executed approximately 2 seconds in the future.
 * IOCTL_GET_MODES:
 * - Copy the G_modes structure (which describes all the modes supported by the CCD and driver to the
 *   calling programme.
 * IOCTL_SET_IMAGE:
 * - Copy a new simulated image from the calling programme.
 * IOCTL_GET_CMD:
 * - Copy the last received exposure command to the calling programme.
 * IOCTL_SET_MODES:
 * - Copy the modes that the driver can support from the calling programme.
 */
long device_ioctl(struct file *file, unsigned int ioctl_num, unsigned long ioctl_param)
{
  int ret_val;

  switch (ioctl_num)
  {
    case IOCTL_GET_IMAGE:
      ret_val = copy_to_user((void *)ioctl_param, &G_img, sizeof(struct merlin_img));
      if (ret_val < 0)
        printk(KERN_DEBUG PRINTK_PREFIX "Error writing image data to user-space\n");
      G_status &= ~IMG_READY;
      break;
    case IOCTL_ORDER_EXP:
      if ((G_status & CCD_ERROR) != 0)
      {
        printk(KERN_ERR PRINTK_PREFIX "CCD is currently unavailable. See the system log.\n");
        ret_val = -ECOMM;
        break;
      }
      if ((G_status & (CCD_READING_OUT | CCD_INTEGRATING)) != 0)
      {
        printk(KERN_DEBUG PRINTK_PREFIX "Cannot send new expose command, CCD is still being exposed/read out.\n");
        ret_val = -EBUSY;
        break;
      }
      ret_val = copy_from_user(&G_cmd, (void*)ioctl_param, sizeof(struct ccd_cmd));
      if (ret_val < 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Could not copy exposure parameters from user (%d)\n", ret_val);
        break;
      }
      if ((G_cmd.prebin & G_modes.prebin) == 0)
      {
        printk(KERN_INFO PRINTK_PREFIX "Invalid prebinning mode selected: %hhu\n", G_cmd.prebin);
        ret_val = -EINVAL;
        break;
      }
      if (G_cmd.window >= CCD_MAX_NUM_WINDOW_MODES)
      {
        printk(KERN_INFO PRINTK_PREFIX "Inavlid window mode specified: %hhu\n", G_cmd.window);
        ret_val = -EINVAL;
        break;
      }
      if ((G_modes.windows[G_cmd.window].width_px == 0) || (G_modes.windows[G_cmd.window].height_px == 0))
      {
        printk(KERN_INFO PRINTK_PREFIX "Inavlid window mode specified: %hhu\n", G_cmd.window);
        ret_val = -EINVAL;
        break;
      }
      if ((G_cmd.exp_t_msec < G_modes.min_exp_t_msec) || (G_cmd.exp_t_msec > G_modes.max_exp_t_msec))
      {
        printk(KERN_NOTICE PRINTK_PREFIX "Invalid exposure time given: %lu ms\n", G_cmd.exp_t_msec);
        ret_val = -EINVAL;
        break;
      }
      printk(KERN_DEBUG PRINTK_PREFIX "Exposure ordered (%lu ms).\n", G_cmd.exp_t_msec);
      #ifdef ACQSIM
       G_status |= CCD_INTEGRATING;
      #else
      if (!G_cmd.start_at_sec)
        start_exp_now();
      else
        queue_delayed_work(ccd_workq, &sec_handler_check_work, HZ*2);
      #endif
      ret_val = 0;
      break;
    case IOCTL_GET_MODES:
      ret_val = copy_to_user((void *)ioctl_param, &G_modes, sizeof(struct ccd_modes));
      if (ret_val < 0)
        printk(KERN_INFO PRINTK_PREFIX "Could not copy MERLIN CCD modes to user (%d)\n", ret_val);
      break;
#ifdef ACQSIM
    case IOCTL_SET_IMAGE:
      ret_val = copy_from_user(&G_img, (void *)ioctl_param, sizeof(struct merlin_img));
      if (ret_val < 0)
        printk(KERN_DEBUG PRINTK_PREFIX "Error reading image data from user-space\n");
      G_status = IMG_READY | CCD_STAT_UPDATE;
      kill_fasync(&G_async_queue, SIGIO, POLL_IN);
      wake_up_interruptible(&inq);
      ret_val = 0;
      break;
    case IOCTL_GET_CMD:
      ret_val = copy_to_user((void *)ioctl_param, &G_cmd, sizeof(struct ccd_cmd));
      if (ret_val < 0)
        printk(KERN_DEBUG PRINTK_PREFIX "Error writing exposure command to user-space\n");
      break;
    case IOCTL_SET_MODES:
      ret_val = copy_from_user(&G_modes, (void *)ioctl_param, sizeof(struct ccd_modes));
      if (ret_val < 0)
        printk(KERN_DEBUG PRINTK_PREFIX "Error reading ccd modes from user-space\n");
      break;
#endif
    default:
      printk(KERN_DEBUG PRINTK_PREFIX "Invalid IOCTL number\n");
      ret_val = -ENOTTY;
      break;
  }

  return ret_val;
}

/** \brief Called when a programme tries to read from the driver's character device.
 * \return 1 on success, -EAGAIN on failure.
 *
 * Sends the current CCD/driver status character to the calling programme.
 */
static ssize_t device_read(struct file *filp, char *buffer, size_t length, loff_t * offset)
{
  while ((G_status & CCD_STAT_UPDATE) == 0)
  {
    if (filp->f_flags & O_NONBLOCK)
      return -EAGAIN;
    if (wait_event_interruptible(inq, ((G_status & CCD_STAT_UPDATE) != 0)))
      return -ERESTARTSYS;
  }
  if (put_user(G_status, buffer) == 0)
  {
    G_status &= ~CCD_STAT_UPDATE;
    return 1;
  }
  return -EIO;
}

/** \brief Called when a programme tries to write to the driver's character device.
 * \return -ENOTTY
 * This feature is not supported by the driver.
 */
static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
  return -ENOTTY;
}

/** \brief Called when a programme registers for asynchronous notifications.
 * \return (check documentation for fasync_helper, should be >= 0 on success).
 *
 * A programme can register for asynchronous notifications, which means that whenever
 * kill_fasync(&G_async_queue, SIGIO, POLL_IN) is called from within the driver, the programme will
 * receive a SIGIO signal.
 */
static int device_fasync(int fd, struct file *filp, int mode)
{
  return fasync_helper(fd, filp, mode, &G_async_queue);
}

static unsigned int device_poll(struct file *filp, poll_table *wait)
{
  unsigned int mask = 0;
  poll_wait(filp, &inq,  wait);
  if ((G_status & CCD_STAT_UPDATE) != 0)
    mask |= POLLIN | POLLRDNORM;
  return mask;
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR("PIERRE VAN HEERDEN");
MODULE_DESCRIPTION("DRIVER TO INTERFACE WITH A MERLIN ASTRONOMICAL ACQUISITION CCD CAMERA");
