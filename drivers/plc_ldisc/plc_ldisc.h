/*!
 * \file plc_ldisc.h
 * \brief PLC line discipline header file.
 * \author Pierre van Heerden
 *
 * Contains definitions for the plc_ldisc driver to be used by external programmes that use a Programmable
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
 * \note You may notice that the N_PLC line discipline has the same value as the N_MOUSE line discipline. This
 *       is because a new line discipline must be defined as the same number as another, unused line discipline
 *       within the kernel tree. The N_MOUSE line discipline is only used for serial port mice, which (due to
 *       this definition) may not be used on the same computer as a PLC. In fact, if the plc_ldisc driver and
 *       the driver providing the N_MOUSE line discipline is loaded at the same time, the output is undefined.
 *       This shouldn't be a problem, seeing as serial mice are horrendously outdated and virtually no-one uses
 *       them anymore.
 */

#ifndef PLC_LDISC_H
#define PLC_LDISC_H

/// Define the PLC line discipline number. A user-space programme passes this number to the linux kernel
/// (via the TIOCSETD ioctl issued on a tty/serial device such as /dev/ttyS0) in order to set that serial
/// device as a serial device connected to a PLC.
#define N_PLC 2
/// Definition of function that an external programme can call to send a string to the PLC.
ssize_t write_to_plc(char *buf, size_t count);
/// Definition of function that an external programme can call to register a function that interprets a response
/// from the PLC.
void register_plc_handler(void (*handler)(const char*, int));
/// Unregistered a previously register PLC handler function.
void unregister_plc_handler(void);

#endif

