/**
 * \file time_driver.c
 * \author Pierre van Heerden
 * \author Luis Balona
 * \brief ACT Photometry-and-Time Driver
 *
 * Adapted from Luis Balona's MILLYDEV photometry module, which was modified from PARAPIN.
 *
 * \todo Implement setting of system clock.
 * \todo Check time since last 1KHz pulse when status read.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/version.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/stddef.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <linux/delay.h>
#include <linux/parport.h>
#include <linux/parport_pc.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include "parapin.h"
#include "time_driver.h"

#define PRINTK_PREFIX "[ACT_TIME] "

/// Maximum number of handler functions that can be registered
#define MAX_TIME_HANDLERS 5
#define TRUE  1
#define FALSE 0

/** \brief Linux-specific inb and outb.
 * \{
 */
#define INB(register_number) inb(G_lp_base + register_number)
#define OUTB(value, register_number) outb(value, G_lp_base + register_number)
/** \} */

/** \brief Definitions related to parallel port.
 * \{
 */
/// Set PULSE for test signal on pin 14.
#define PULSE  0
/// Output pulse on pin 1 if PULSE nonzero.
#define CLK LP_PIN01
/// Pin 12 used to read seconds pulse.
#define LPT_SEC LP_PIN12
#define CNTBUFSZ  524288
/** \} */

/** \brief Forward definitions of private functions.
 * \{
 */
int insert_module(void);
void remove_module(void);
int time_open(struct inode *inodePtr, struct file *filePtr);
ssize_t time_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
int time_release(struct inode *inodePtr, struct file *filePtr);
long time_ioctl(struct file *filePtr, unsigned int ioctl_num, unsigned long ioctl_param);
void sync_time(struct work_struct *work);
void time_irq_handler(void *dev_id);
void pin_release(void);
void change_pin(int pins, int mode);
void clear_pin(int pins);
static inline void clear_datareg(int pins);
static inline void clear_controlreg(int pins);
void pin_disable_irq(void);
int pin_init_kernel(int lpt, void (*irq_func)(void *));
static void pin_init_internal(void);
int pin_have_irq(void);
void pin_enable_irq(void);
void pin_input_mode(int pins);
int pin_is_set(int pins);
static void dataline_input_mode(void);
static void controlreg_input_mode(int pins);
void set_pin(int pins);
static inline void set_datareg(int pins);
static inline void set_controlreg(int pins);
static inline int read_register(int regnum);
static inline void write_register(int value, int regnum);
char register_second_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec));
void unregister_second_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec));
char register_millisec_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec));
void unregister_millisec_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec));
char register_time_sync_handler(void (*handler)(const char synced));
void unregister_time_sync_handler(void (*handler)(const char synced));
void get_unitime(unsigned long *unit, unsigned short *unit_msec);
/** \} */

/** \brief Global variables.
 * \{
 */
/// Device major number
static int G_major = 0;
/// Device class structure
static struct class *G_class_time;
/// Device structure
static struct device *G_dev_time;
/// Status information
static char G_status = 0;
/// Local time in seconds since midnight (hours, minutes and seconds only).
static unsigned long G_unit=0;
/// Local time milli-seconds.
static unsigned short G_unit_msec = 0;
/// Array of handlers register to be called at the turn of a second.
static void (*G_second_handlers[MAX_TIME_HANDLERS]) (const unsigned long, const unsigned short);
/// Array of handlers register to be called at the turn of a millisecond.
static void (*G_millisec_handlers[MAX_TIME_HANDLERS]) (const unsigned long, const unsigned short);
/// Array of handlers register to be called to warn of time sync loss
static void (*G_time_sync_handlers[MAX_TIME_HANDLERS]) (const char);
/// Flag indicating whether or not minute pulse is present
static int G_sec_pulse=0;
/// Parallel port found
struct parport *G_parapin_port = NULL;
/// Parallel port device registered.
struct pardevice *G_parapin_dev = NULL;
/// Parallel port base.
static int G_lp_base;
/// TRUE if IRQ handler available.
static int G_have_irq_handler = 0;
/// Previous IRQ mode
static int G_old_irq_mode = 0;
/// Mask of pins on which we currently allow input.
static int G_lp_input_pins;
/// Mask of pins on which we currently allow output.
static int G_lp_output_pins;
/** \brief Most recently read or written values of the three parallel port I/O registers.
 * Kept in TRUE logic; i.e. the actual high/low electrical state on the pins, NOT the values of register bits. 
 */
static int G_lp_register[3];
/// Kernel work queue structure for synchronising time (bottom-half of 1 ms interrupt handler).
static struct work_struct G_synctime_work;
/** \} */

/// System calls we're handling
struct file_operations time_fops = {
  .open = time_open,
  .read = time_read,
  .release = time_release,
  .unlocked_ioctl = time_ioctl
};

/** \brief Called when module is inserted into the kernel.
 * \param (void)
 * \return 0 if successfully loaded, \< 0 otherwise.
 */
int insert_module(void)
{
  int i;
  G_major = register_chrdev(0, TIME_DEVICE_NAME, &time_fops);
  if (G_major < 0)
  {
    printk(KERN_INFO PRINTK_PREFIX "Can't get major number\n");
    return(G_major);
  }
  printk(KERN_DEBUG PRINTK_PREFIX "Assigned major: %d\n", G_major);

  G_class_time = class_create(THIS_MODULE, TIME_DEVICE_NAME);
  if (G_class_time == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device class.\n" );
    unregister_chrdev(G_major, TIME_DEVICE_NAME);
    return -ENODEV;
  }
  
  G_dev_time = device_create(G_class_time, NULL, MKDEV(G_major, 0), NULL, TIME_DEVICE_NAME);
  if (G_dev_time == NULL)
  {
    printk (KERN_ALERT PRINTK_PREFIX "Error creating device.\n" );
    unregister_chrdev(G_major, TIME_DEVICE_NAME);
    class_destroy(G_class_time);
    return -ENODEV;
  }
  
  G_status = 0;
  INIT_WORK(&G_synctime_work, sync_time);

  pin_input_mode(LPT_SEC);
  G_sec_pulse = pin_is_set(LPT_SEC);

  if (pin_init_kernel(0, time_irq_handler) < 0)
  {
    printk(KERN_CRIT PRINTK_PREFIX "Cannot load interrupt handler\n") ;
    device_destroy(G_class_time, MKDEV(G_major, 0));
    class_destroy(G_class_time);
    unregister_chrdev(G_major, TIME_DEVICE_NAME);
    return -EFAULT;
  }

  if (!pin_have_irq())
  {
    printk(KERN_CRIT PRINTK_PREFIX "No irq available...");
    device_destroy(G_class_time, MKDEV(G_major, 0));
    class_destroy(G_class_time);
    unregister_chrdev(G_major, TIME_DEVICE_NAME);
    return -EFAULT;
  }
  pin_enable_irq();

  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    G_millisec_handlers[i] = 0;
    G_second_handlers[i] = 0;
    G_time_sync_handlers[i] = 0;
  }

  printk(KERN_INFO PRINTK_PREFIX "Module inserted.\n");
  return(0);
}
module_init(insert_module);

/** \brief Called when module is unloaded from the kernel.
 * \param (void)
 * \return (void)
 */
void remove_module(void)
{
  pin_release();
  pin_disable_irq();

  printk(KERN_INFO PRINTK_PREFIX "Removed\n") ;
  // Unregister our device
  device_destroy(G_class_time, MKDEV(G_major, 0));
  class_destroy(G_class_time);
  unregister_chrdev(G_major, TIME_DEVICE_NAME);
}
module_exit(remove_module);

/** \brief Called when character device opened.
 * \param inodePtr (??? FILL)
 * \param filePtr (??? FILL)
 * \return 0 (success)
 */
int time_open(struct inode *inodePtr, struct file *filePtr)
{
  return 0;
}

/** \brief Return status byte to user.
 * \param filp File pointer structure.
 * \param buf (??? FILL)
 * \param count (??? FILL)
 * \param f_pos (??? FILL)
 * \return Number of bytes written to user-space.
 */
ssize_t time_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
  if (copy_to_user (buf, &G_status, sizeof(G_status)))
    return -EFAULT;

  return(sizeof(G_status));
}

/** \brief called when character device closed.
 * \param inodePtr (??? FILL)
 * \param filePtr (??? FILL)
 * \return 0 (success)
 */
int time_release(struct inode *inodePtr, struct file *filePtr)
{
  return 0;
}

/** \brief Handle ioctl calls.
 * \param inodePtr (??? FILL)
 * \param filePtr (??? FILL)
 * \param ioctl_num IOCTL number.
 * \param ioctl_param IOCTL parameter.
 * \return \>= 0 on success, \< 0 on error.
 */
long time_ioctl(struct file *filePtr, unsigned int ioctl_num, unsigned long ioctl_param)
{
  int ret_val = 0, i;

  switch (ioctl_num)
  {
    case IOCTL_GET_UNITIME:
      ret_val = put_user((G_unit*1000)+G_unit_msec, (unsigned long*)ioctl_param);
      break;

    case IOCTL_TIME_RESYNC:
      G_status &= ~TIME_CLOCK_SYNC;
      for (i=0; i<MAX_TIME_HANDLERS; i++)
      {
        if (G_time_sync_handlers[i] != 0)
          (*G_time_sync_handlers[i])(0);
      }
      ret_val = 0;
      break;

    default:  // Invalid IOCTL number
      ret_val = -ENOTTY;
      break;
  }
  return(ret_val);
}

void sync_time(struct work_struct *work)
{
  struct timeval tv;
  int i;

  G_status = TIME_CLOCK_SYNC;
  do_gettimeofday(&tv);
  G_unit = tv.tv_sec % 86400;
  printk(KERN_INFO PRINTK_PREFIX "Syncing. System has time %lu:%lu:%lu\n", (tv.tv_sec/3600)%24, (tv.tv_sec/60)%60, tv.tv_sec%60);
  if (tv.tv_usec < 500000L)
    G_unit--;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_time_sync_handlers[i] != 0)
      (*G_time_sync_handlers[i])(1);
  }
}

/** \brief Interrupt handler (should occur once per millisecond)
 * \param irq (??? FILL)
 * \param dev_id (??? FILL)
 * \param regs (??? FILL)
 */
void time_irq_handler(void *dev_id)
{
  int tmp_sec_pulse = pin_is_set(LPT_SEC), i;
  G_unit_msec++;

  if ((G_status & TIME_CLOCK_SYNC) == 0)
  {
    if ((tmp_sec_pulse != 0) && (G_sec_pulse == 0))
    {
      G_unit_msec = 0;
      schedule_work(&G_synctime_work);
    }
  }

  if ((G_unit_msec % 1000) == 0)
  {
    if ((G_status & TIME_CLOCK_SYNC) == 0)
      printk(KERN_INFO PRINTK_PREFIX "Something's wrong: 1000 1 KHz pulses counted but still no 1 second pulse.\n");
    else if ((tmp_sec_pulse == 0) || (G_sec_pulse != 0))
    {
      printk(KERN_INFO PRINTK_PREFIX "Clock skew detected. Attempting to re-sync.\n");
      G_status &= ~TIME_CLOCK_SYNC;
      for (i=0; i<MAX_TIME_HANDLERS; i++)
      {
        if (G_time_sync_handlers[i] != 0)
          (*G_time_sync_handlers[i])(0);
      }
    }
    G_unit++;
    if (G_unit % 86400 == 0)
      G_unit = 0;
    G_unit_msec = 0;
    for (i=0; i<MAX_TIME_HANDLERS; i++)
    {
      if (G_second_handlers[i] != 0)
        (*G_second_handlers[i])(G_unit, G_unit_msec);
      if (G_millisec_handlers[i] != 0)
        (*G_millisec_handlers[i])(G_unit, G_unit_msec);
    }
  }
  else
  {
    for (i=0; i<MAX_TIME_HANDLERS; i++)
    {
      if (G_millisec_handlers[i] != 0)
        (*G_millisec_handlers[i])(G_unit, G_unit_msec);
    }
  }
  G_sec_pulse = tmp_sec_pulse;
}

/** \brief Releases LP port so it can be used by other apps.
 * \return (void)
 *
 * This function must be called by the kernel programme when we're done with the parallel port.
 * Tasks:
 *  - Restores interrupts to their former states.
 *  - Releases and unregisters the parallel port.
 */
void pin_release(void)
{
  change_pin(LP_IRQ_MODE, G_old_irq_mode ? LP_SET : LP_CLEAR);

  parport_release(G_parapin_dev);
  parport_unregister_device(G_parapin_dev);
  parport_put_port(G_parapin_port);
}

/** \brief Alternative interface to set_pin and clear_pin.
 * \param pins (??? FILL)
 * \param mode (??? FILL)
 * \return (void)
 *
 * change_pin(X, LP_SET) is the same as set_pin(X).
 * change_pin(X, LP_CLEAR) is the same as clear_pin(X).
 */
void change_pin(int pins, int mode)
{
  if (mode == LP_SET)
    set_pin(pins);
  else if (mode == LP_CLEAR)
    clear_pin(pins);
}

/** \brief Same interface as set_pin, except that it clears instead.
 * \param pins
 * \return (void)
 *
 * Algorithm:
 *  -# Make sure the user is only trying to set an output pin.
 *  -# If user is trying to clear pins that are data-register controlled
 *    - clear data-register controlled pin.
 *  -# If user is trying to clear pins that are control-register controlled
 *    - clear control-register controlled pin.
 */
void clear_pin(int pins)
{
  pins &= G_lp_output_pins;

  if (pins & LPBASE0_MASK)
    clear_datareg(pins & LPBASE0_MASK);

  if (pins & LPBASE2_MASK)
    clear_controlreg((pins & LPBASE2_MASK) >> 16);
}

/** \brief Clear output pins in the data register.
 * \param pins (??? FILL)
 * \return (void)
 *
 * We'll do read-before-write, just to be safe.
 */
static inline void clear_datareg(int pins)
{
  write_register((read_register(0) & (~pins)), 0);
}

/** \brief Clear output pins in the control register.
 * \param pins (??? FILL)
 * \return (void)
 *
 * The control register requires:
 *  - switchable pins that are currently being used as inputs must be 1
 *    all other pins may be either set or cleared.
 *
 * Algorithm:
 *   -# Read existing register into lp_register[2].
 *   -# Clear the requested pins, leaving others unchanged.
 *   -# Set all inputs to one (they may have been read as 0's!) 1.
 *   -# Write it back.
 */
static inline void clear_controlreg(int pins)
{
  read_register(2);
  G_lp_register[2] &= (~pins);
  G_lp_register[2] |= (0x0f & ((G_lp_input_pins & LP_SWITCHABLE_PINS) >> 16));
  write_register(G_lp_register[2], 2);
}

/** \brief Turn interrupts off.
 * \return (void)
 */
void pin_disable_irq(void)
{
  if (G_parapin_port && G_parapin_port->irq >= 0)
    disable_irq(G_parapin_port->irq);
  clear_pin(LP_IRQ_MODE);
}

/** \brief Kernel-mode parallel port initialisation.
 * \param lpt LP number of the port that you want.
 * \param irq_func Pointer to IRQ (1 KHz) handler function.
 * \param pt_regs (??? FILL)
 *
 * This code will EXCLUSIVELY claim the parallel port; i.e., so that other devices connected to the same
 * parallel port will not be able to use it until you call the corresponding pin_release().
 *
 * Algorithm:
 *  -# Get enumeration number for this port.
 *  -# Register the device on the port for exclusive access.
 *  -# Claim the parallel port.
 *  -# Store LP base of parallel port.
 *  -# Enable bidir mode if we have an ECR.
 *  -# Initialize the state of the registers.
 *  -# Store the current state of the interrupt enable flag.
 *
 * NOTE: For a brief time sometime in the middle of the 2.6.x kernel
 *    series, the definition of parport_register_device() was changed
 *    briefly such that the 5th argument (the irq handler) did not have
 *    the "struct pt_regs() *" argument explicitly declared, which was
 *    an error.  It appears that at least one Debian distro went out to
 *    the world with this change in place.  In this case gcc will emit a
 *    warning about incompatible arguments being passed, but everything will
 *    work correctly.  If you get this warning on the 5th argument in the
 *    following call, and it bothers you, take a look at your
 *    include/linux/parport.h kernel header file to see if it has the
 *    incorrect function prototype for parport_register_device().
 */
int pin_init_kernel(int lpt, void (*irq_func)(void *))
{
  G_parapin_port = parport_find_number(lpt);
  if (G_parapin_port == NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Init failed, parallel port lp%d not found\n", lpt);
    return -ENODEV;
  }

  G_parapin_dev = parport_register_device(G_parapin_port,
					"parapin", /* name for debugging */
					     NULL, /* preemption callback */
					     NULL, /* wakeup callback */
					 irq_func, /* interrupt callback */
                                 PARPORT_DEV_EXCL, /* flags */
				    (void *)NULL); /* user data */
  if (G_parapin_dev == NULL)
  {
    printk(KERN_INFO PRINTK_PREFIX "Init failed, could not register device\n");
    return -ENODEV;
  }

  parport_claim(G_parapin_dev);

  G_lp_base = G_parapin_port->base;

  {
    struct parport_pc_private *priv = G_parapin_port->private_data;
    if (priv->ecr)
      outb(0x20, ECONTROL(G_parapin_port));
  }

  pin_init_internal();

  G_old_irq_mode = pin_is_set(LP_IRQ_MODE);
  G_have_irq_handler = (irq_func != NULL);

  printk(KERN_DEBUG PRINTK_PREFIX "claiming %s at 0x%lx, irq %d\n", G_parapin_port->name,
         G_parapin_port->base, G_parapin_port->irq);

  return 0;
}

/** \brief Checks which pins are active.
 * \param pins Set of pins to check.
 * \return Bitvector indicating what subset of pins are active.
 *
 * Takes any number of pins to check, and returns a corresponding bitvector with 
 * 1's set on pins that are electrically high.
 */
int pin_is_set(int pins)
{
  int result = 0;

  pins &= G_lp_input_pins;

  if (pins & LPBASE0_MASK) {
    result |= (read_register(0) & (pins & LPBASE0_MASK));
  }

  if (pins & LPBASE1_MASK) {
    result |= ((read_register(1) & ((pins & LPBASE1_MASK) >> 8)) << 8);
  }

  if (pins & LPBASE2_MASK) {
    result |= ((read_register(2) & ((pins & LPBASE2_MASK) >> 16)) << 16);
  }

  return result;
}


/** \brief Pin initialisation
 * \param (void)
 * \return (void)
 *
 * The library is initialized with a very minimalist approach: instead of trying 
 * to figure out what values are "safe", we just avoid touching the parallel 
 * port registers *at all* until the user tells us to do so.  accordingly, all
 * switchable pins are in neither input nor output mode to start with.  This 
 * enforces the fact that outputs/inputs can not be used until a pin is 
 * explicitly configured as either an input or output pin.  Note that it is 
 * always legal to read from and write to the "pseudo pins" of INPUT_MODE and 
 * IRQ_MODE because these are actually control bits.
 */
static void pin_init_internal(void)
{

  G_lp_output_pins = LP_INPUT_MODE | LP_IRQ_MODE;
  G_lp_input_pins = LP_ALWAYS_INPUT_PINS | LP_INPUT_MODE | LP_IRQ_MODE;
}

/** \brief Are interrupts available?
 * \return TRUE if IRQs are available, FALSE otherwise.
 */
int pin_have_irq(void)
{
  return (G_parapin_port && G_have_irq_handler && (G_parapin_port->irq >= 0));
}

/** \brief Turn interrupts on.
 * \return (void)
 */
void pin_enable_irq(void)
{
  if (pin_have_irq()) 
  {
    set_pin(LP_IRQ_MODE);
    udelay(10); /* wait for the spurious interrupt (if any) to pass */
    enable_irq(G_parapin_port->irq);
  } 
  else 
  {
    printk(PRINTK_PREFIX "Trying to enable interrupts without proper prereqs");
  }
}

/** \brief Function to change pins to input mode.
 * \param pins 
 * \return (void)
 */
void pin_input_mode(int pins)
{
  if (pins & LP_DATA_PINS)
    dataline_input_mode();
  if (pins & LP_SWITCHABLE_PINS)
    controlreg_input_mode(pins & LP_SWITCHABLE_PINS);
}

/** \brief Direction changing: change the data pins (pins 2-9) to input mode.
 * \param (void)
 * \return (void)
 */
static void dataline_input_mode(void)
{
  G_lp_input_pins |= LP_DATA_PINS;
  G_lp_output_pins &= (~LP_DATA_PINS);
  set_pin(LP_INPUT_MODE);
}

/** \brief Used to set any number of pins without disturbing other pins.
 * \param pins (??? FILL)
 * \return (void)
 *
 * Example: set_pin(LP_PIN02 | LP_PIN05 | LP_PIN07)
 *
 * Algorithm:
 *   -# Make sure the user is only trying to set an output pin.
 *   -# If user is trying to set pins high that are data-register controlled, set data register pin.
 *   -# If user is trying to set pins high that are control-register controlled, set control register pin.
 */
void set_pin(int pins)
{
  pins &= G_lp_output_pins;
  if (pins & LPBASE0_MASK)
    set_datareg(pins & LPBASE0_MASK);
  if (pins & LPBASE2_MASK)
    set_controlreg((pins & LPBASE2_MASK) >> 16);
}

/** \brief Set output pins in the data register.  We'll do read-before-write, just to be safe.
 * \param pins (??? FILL)
 * \return (void)
 */
static inline void set_datareg(int pins)
{
  write_register(read_register(0) | pins, 0);
}

/** \brief Set output pins in the control register.
 * \param pins (??? FILL)
 * \return (void)
 *
 * The control register requires:
 *  - switchable pins that are currently being used as inputs must be 1 .
 *  - all other pins may be either set or cleared.
 *
 * Algorithm:
 *  -# read existing register into lp_register[2].
 *  -# set the requested bits to 1, leaving the others unchanged.
 *  -# set all inputs to one (they may have been read as 0's!) 1.
 *  -# write it back.
 */
static inline void set_controlreg(int pins)
{
  read_register(2);
  G_lp_register[2] |= pins;
  G_lp_register[2] |= (0x0f & ((G_lp_input_pins & LP_SWITCHABLE_PINS) >> 16));
  write_register(G_lp_register[2], 2);
}


/** \brief (??? FILL)
 * \param (??? FILL)
 * \return (void)
 */
static void controlreg_input_mode(int pins)
{
  G_lp_input_pins |= pins;
  G_lp_output_pins &= (~pins);
  set_controlreg(0);
}

/** \brief Read a byte from an I/O port (base + regnum) , correct for inverted logic on 
 * \brief some bits, and store in the lp_register variable.  Return the value read.
 * \param regnum (??? FILL)
 * \return Value read from register regnum.
*/
static inline int read_register(int regnum)
{
  return G_lp_register[regnum] = (INB(regnum) ^ lp_invert_masks[regnum]);
}

/** \brief  Store the written value in the lp_register variable, correct for  inverted 
 * \brief logic on some bits, and write a byte to an I/O port (base + regnum).
 * \param value (??? FILL)
 * \param regnum (??? FILL)
 * \return (void)
 */
static inline void write_register(int value, int regnum) 
{
  OUTB((G_lp_register[regnum] = value) ^ lp_invert_masks[regnum], regnum);
}

/** \brief Register an external function to be called at the turn of a second.
 * \param handler Pointer to a function of type void (unsigned long, unsigned short)
 * \return TRUE if handler successfully registered, FALSE otherwise
 *
 * Registers a function pointed to by handler as a function that must be called at the turn of a second.
 */
char register_second_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec))
{
  int i;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_second_handlers[i] != NULL)
      continue;
    G_second_handlers[i] = handler;
    return TRUE;
  }
  printk(KERN_ERR PRINTK_PREFIX "Could not find an open slot for new second handler registration.\n");
  return FALSE;
}
EXPORT_SYMBOL(register_second_handler);

/** \brief Unregister an external function that was called at the turn of a second.
 * \param handler Pointer to a function of type void (unsigned long, unsigned short)
 * \return TRUE if handler successfully registered, FALSE otherwise
 *
 * Unregisters a function pointed to by handler as a function that used to be called at the turn of a second.
 */
void unregister_second_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec))
{
  int i;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_second_handlers[i] != handler)
      continue;
    G_second_handlers[i] = NULL;
    return;
  }
  printk(KERN_ERR PRINTK_PREFIX "Could not find second handler to unregister!.\n");
}
EXPORT_SYMBOL(unregister_second_handler);

/** \brief Register an external function to be called at the turn of a milli-second.
 * \param handler Pointer to a function of type void (unsigned long, unsigned short)
 * \return TRUE if handler successfully registered, FALSE otherwise
 *
 * Registers a function pointed to by handler as a function that must be called at the turn of a milli-second.
 */
char register_millisec_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec))
{
  int i;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_millisec_handlers[i] != NULL)
      continue;
    G_millisec_handlers[i] = handler;
    return TRUE;
  }
  printk(KERN_ERR PRINTK_PREFIX "Could not find an open slot for new millisec handler registration.\n");
  return FALSE;
}
EXPORT_SYMBOL(register_millisec_handler);

/** \brief Unregister an external function that was called at the turn of a milli-second.
 * \param handler Pointer to a function of type void (unsigned long, unsigned short)
 * \return TRUE if handler successfully registered, FALSE otherwise
 *
 * Unregisters a function pointed to by handler as a function that used to be called at the turn of a milli-second.
 */
void unregister_millisec_handler(void (*handler)(const unsigned long unit, const unsigned short unit_msec))
{
  int i;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_millisec_handlers[i] != handler)
      continue;
    G_millisec_handlers[i] = NULL;
    return;
  }
  printk(KERN_ERR PRINTK_PREFIX "Could not find millisec handler to unregister!.\n");
}
EXPORT_SYMBOL(unregister_millisec_handler);

/** \brief Register an external function ti be called when the time system loses/gains sync
 * \param handler Pointer to a function of type void (char)
 * \return TRUE if handler successfully registered, FALSE otherwise
 *
 * Unregisters a function pointed to by handler as a function that used to be called at the turn of a milli-second.
 */
char register_time_sync_handler(void (*handler)(const char synced))
{
  int i;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_time_sync_handlers[i] != NULL)
      continue;
    G_time_sync_handlers[i] = handler;
    (*handler)((G_status & TIME_CLOCK_SYNC) > 0);
    return TRUE;
  }
  printk(KERN_ERR PRINTK_PREFIX "Could not find an open slot for new time sync handler registration.\n");
  return FALSE;
}
EXPORT_SYMBOL(register_time_sync_handler);

void unregister_time_sync_handler(void (*handler)(const char synced))
{
  int i;
  for (i=0; i<MAX_TIME_HANDLERS; i++)
  {
    if (G_time_sync_handlers[i] != handler)
      continue;
    G_time_sync_handlers[i] = NULL;
    return;
  }
  printk(KERN_ERR PRINTK_PREFIX "Could not find time sync handler to unregister!.\n");
}
EXPORT_SYMBOL(unregister_time_sync_handler);

/** \brief Returns the local time (mean time) to the calling function.
 * \param 
 * \param
 * \return (void)
 */
void get_unitime(unsigned long *unit, unsigned short *unit_msec)
{
  *unit = G_unit;
  *unit_msec = G_unit_msec;
}
EXPORT_SYMBOL(get_unitime);

/** \brief Linux kernel module definitions.
 * \{
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Luis Balona <lab@saao.ac.za>, Pierre van Heerden <pierre@saao.ac.za>");
MODULE_DESCRIPTION("Time interrupt handler using printer port.");
/** \} */

