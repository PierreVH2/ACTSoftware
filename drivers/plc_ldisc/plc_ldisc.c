/*!
 * \file plc_ldisc.h
 * \brief PLC line discipline source file.
 * \author Pierre van Heerden
 *
 * Contains the implementation for the plc_ldisc driver to be used by external programmes that use a Programmable
 * Logic converter connected to the computer via serial (RS232) cable. The plc_ldisc driver is in essence
 * a layer between the Linux kernel serial/tty layer and another programme (in practise this is restricted
 * to another Linux kernel driver) that interprets feedback from the PLC and sends commands to it. All
 * messages sent to or received from the PLC are in the form of text strings. The plc_ldisc driver assembles
 * chunks of the messages from the PLC, starting from a designated start-of-message delimiter and ending at
 * a designated end-of-message delimiter, and sends the complete messages to a registered external programme.
 * The plc_ldisc driver also accepts strings from an external programme and sends them to the PLC. After being
 * loaded, the plc_ldisc driver only becomes active after the line discipline of the tty device (usually
 * /dev/ttyS0, /dev/ttyS1 or the like) corresponding to the serial port to which the PLC is connected to the
 * computer is set to the N_PLC line discipline (as defined below).
 *
 * \note The plc_ldisc conflicts with the Linux kernel serial mouse driver - DO NOT have them both loaded
 *       in a running Linux kernel at the same time.
 *
 * \note This code is very loosely based on the code for the ppp_sync Linux kernel module found within the
 *       Linux kernel source tree.
 *
 * \note At the moment, this driver supports only one PLC device connected to the computer. If more than one
 *       tty device is registered as obeying the N_PLC line discipline, only the last one will be used. It should
 *       be fairly simple to extend the driver to support an arbitrary number of PLCs - see the sync_ppp module,
 *       which manages an arbitrary number of serial modems, for clues.
 *
 * \note At the moment, this driver supports only one external programme (i.e. separate kernel driver) being
 *       registered with this driver. In practise, this means that only one function within the Linux kernel
 *       can be set to handle responses from the PLC. Seeing as the driver only supports one PLC, more than
 *       one registered handler function is unnecessary. If more than one handler function is registered with
 *       this driver, only the last registered one will be used. This could cause a headache later, since
 *       this driver doesn't check whether a handler function is already registered before registering a new
 *       handler function. Also note that any number of programmes may write to the PLC through this driver and
 *       when installing this driver, administrator needs to make sure that only one driver will try to write
 *       messages to the PLC, otherwise bad things might happen. The driver can be extended to support more
 *       handler functions and prevent multiple writes to the same PLC, see the ppp_sync module for clues. 
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include "plc_ldisc.h"

#define PRINTK_PREFIX "[ACT_LDISC] "

/// Message receiver buffer - used to store incoming PLC response strings, which will come in chunks, during assembly.
char G_recv_buf[256];
/// Counter for number of bytes received from the PLC.
unsigned char G_bytes_recvd=0;
/// TTY device that this driver manages - set when a tty device is set to the N_PLC line discipline.
struct tty_struct *G_plc_tty = NULL;
/// Pointer to a registered PLC response string handler function.
void (*plc_handler)(const char*, int) = NULL;

/*! \brief Called when a tty is put into PLC line discipline.
 * \param tty Pointer to the TTY structure corresponding to the TTY device that was set to the N_PLC line discipline.
 */
static int plc_open(struct tty_struct *tty)
{
  if (tty->ops->write == NULL)
    return -EOPNOTSUPP;
  printk(KERN_DEBUG PRINTK_PREFIX "PLC link opened.\n");
  G_plc_tty = tty;
  return 0;
}

/*! \brief Called when the tty is put into a differnt line discipline or PLC hangs up.
 * \param tty Pointer to the TTY structure corresponding to the TTY device that was set from the N_PLC line discipline to
 *            something else.
 */
static void plc_close(struct tty_struct *tty)
{
  printk(KERN_DEBUG PRINTK_PREFIX "PLC link closed.\n");
  G_plc_tty = NULL;
  return;
}


/*! \brief Reading from tty device - does nothing, no data is ever available this way.
 */
static ssize_t plc_read(struct tty_struct *tty, struct file *file, unsigned char __user *buf, size_t count)
{
  return -EAGAIN;
}

/*! \brief Writing to tty device - does nothing, no data can be sent this way.
 */
static ssize_t plc_write(struct tty_struct *tty, struct file *file, const unsigned char *buf, size_t count)
{
  return -EAGAIN;
}

/*! \brief Write a string to PLC.
 * \param buf String to be written.
 * \param count Length of string in bytes.
 * \note This function (symbol) is exported so it can be accessed by other Linux kernel drivers.
 */
ssize_t write_to_plc(char *buf, size_t count)
{
  if (G_plc_tty == NULL)
    return -EAGAIN;
  return G_plc_tty->ops->write(G_plc_tty, buf, count);
}
EXPORT_SYMBOL(write_to_plc);

/*! \brief Register a PLC response string handler function.
 * \param handler Pointer to PLC response string handler function.
 * \note The new handler function is registered regardless of whether a function has already been registered.
 * \note This function (symbol) is exported so it can be accessed by other Linux kernel drivers.
 */
void register_plc_handler(void (*handler)(const char*, int))
{
  plc_handler = handler;
}
EXPORT_SYMBOL(register_plc_handler);

/*! \brief Unregister a PLC response string handler function.
 * \note This function MUST be called before a driver that registered a function with the plc_ldisc driver is
 *       removed from the kernel - bad things will happen if you don't!
 * \note This function (symbol) is exported so it can be accessed by other Linux kernel drivers.
 */
void unregister_plc_handler(void)
{
  plc_handler = NULL;
}
EXPORT_SYMBOL(unregister_plc_handler);

/*! \brief IOCTL on tty device - does nothing. All global tty/serial layer IOCTLs are caught by corresponding drivers.
 */
static int plc_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
  return -ENOTTY;
}

/*! \brief Called when a string is received on an N_PLC tty device.
 * \param tty Pointer to TTY structure.
 * \param buf Message chunk
 * \param cflags N/A
 * \param count Length of message chunk in bytes.
 *
 * Algorithm:
 *  -# Find positions of message start (@) and message end (*) characters within message chunk, if present
 *  -# If complete message contained within chunk (@ and * both present, @ comes before *):
 *    - Call message handler function (if one is registered) with complete message.
 *    - If message continues after * character, process the rest recursively.
 *    - Return
 *  -# If end of message (*) present:
 *    - Copy message chunk up to * into message buffer
 *    - Call message handler function (if one is registered) with complete message.
 *    - If message continues after * character, process the rest recursively.
 *    - Return
 *  -# If @ present in message chunk, reset message buffer counter to 0
 *  -# Copy message chunk to receiver buffer, starting at the message buffer counter.
 *  -# Increase message buffer counter accordingly.
 */
static void plc_receive(struct tty_struct *tty, const unsigned char *buf, char *cflags, int count)
{
  int start_pos=-1, end_pos=-1, i;
  for (i=0; i<count; i++)
  {
    if (buf[i] == '*')
    {
      end_pos = i;
      break;
    }
  }
  for (i=0; i<count; i++)
  {
    if (buf[i] == '@')
    {
      start_pos = i;
      break;
    }
  }
  // Check if complete message (from @ to *) contained within buf
  if ((start_pos >= 0) && (end_pos >= 0) && (start_pos < end_pos))
  {
    G_bytes_recvd = end_pos-start_pos+1;
    if (G_bytes_recvd > sizeof(G_recv_buf)-1)
    {
      printk(KERN_INFO PRINTK_PREFIX "Strange - received message that is larger than %d. This must be an invalid message. Ignoring.\n", sizeof(G_recv_buf));
      G_bytes_recvd = 0;
      return;
    }
    memcpy(G_recv_buf, &buf[start_pos], G_bytes_recvd);
    G_recv_buf[G_bytes_recvd] = '\0';
    if (plc_handler != NULL)
      (*plc_handler)(G_recv_buf, G_bytes_recvd);
    if (end_pos < count-1)
      plc_receive(tty, &buf[end_pos+1], cflags, count-end_pos-1);
    return;
  }
  // Check if end of message (*) in buf - process only up to *, then handle the rest recursively
  if (end_pos >= 0)
  {
    if (end_pos+G_bytes_recvd > sizeof(G_recv_buf)-1)
    {
      printk(KERN_INFO PRINTK_PREFIX "Receive buffer overflow!\n");
      G_bytes_recvd = 0;
    }
    memcpy(&G_recv_buf[G_bytes_recvd], buf, end_pos+1);
    G_bytes_recvd += end_pos+1;
    G_recv_buf[G_bytes_recvd] = '\0';
    if (plc_handler != NULL)
      (*plc_handler)(G_recv_buf, G_bytes_recvd);
    if (end_pos < count-1)
      plc_receive(tty, &buf[end_pos+1], cflags, count-end_pos-1);
    return;
  }
  // Check if start of message (@) in buf, if so overwrite buffer from beginning, otherwise just write where previous chunk stopped.
  if (start_pos >= 0)
    G_bytes_recvd = 0;
  else
    start_pos = 0;
  memcpy(&G_recv_buf[G_bytes_recvd], &buf[start_pos], count-start_pos);
  G_bytes_recvd += count-start_pos;
}

/*! \brief Called when a write to the PLC is interrupted and then woken up later - it should be unnecessary.
 */
static void plc_write_wakeup(struct tty_struct *tty)
{
  return;
}

/*! \brief TTY line discipline structure, contains pointers to functions that are called by the Linux tty driver.
 */
static struct tty_ldisc_ops G_plc_ldisc = {
  .owner  = THIS_MODULE,
  .magic  = TTY_LDISC_MAGIC,
  .name = "plc",
  .open = plc_open,
  .close  = plc_close,
  .read = plc_read,
  .write  = plc_write,
  .ioctl  = plc_ioctl,
  .receive_buf = plc_receive,
  .write_wakeup = plc_write_wakeup,
};

/*! \brief Called when driver inserted into running Linux kernel.
 */
static int __init plc_init(void)
{
  int err;

  err = tty_register_ldisc(N_PLC, &G_plc_ldisc);
  if (err != 0)
  {
    printk(KERN_ERR PRINTK_PREFIX "Error %d registering line discipline.\n", err);
    return err;
  }

  return err;
}

/*! \brief Called when driver removed from running Linux kernel.
 */
static void __exit plc_cleanup(void)
{
  if (tty_unregister_ldisc(N_PLC) != 0)
    printk(KERN_ERR PRINTK_PREFIX "Failed to unregister PLC line discipline\n");
}

module_init(plc_init);
module_exit(plc_cleanup);
MODULE_LICENSE("GPL");
MODULE_ALIAS_LDISC(N_PLC);
