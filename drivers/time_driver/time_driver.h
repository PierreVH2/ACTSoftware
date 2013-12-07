#ifndef TIME_DRIVER_H
#define TIME_DRIVER_H

#include <linux/ioctl.h>

#define TIME_IOCTL_NUM          0xE0
#define TIME_DEVICE_NAME        "act_time"

/** \brief Driver status definitions
 * \{
 */
/// Clock synchronised
#define TIME_CLOCK_SYNC   0x01
/** \} */

/// IOCTL to read mean time (local time)
#define IOCTL_GET_UNITIME _IOR(TIME_IOCTL_NUM, 0, unsigned long*)

/// IOCTL to force driver to resynchronise
#define IOCTL_TIME_RESYNC _IOW(TIME_IOCTL_NUM, 1, unsigned long*)

char register_second_handler(void (*handler)(const unsigned long loct, const unsigned short loct_msec));
void unregister_second_handler(void (*handler)(const unsigned long loct, const unsigned short loct_msec));
char register_millisec_handler(void (*handler)(const unsigned long loct, const unsigned short loct_msec));
void unregister_millisec_handler(void (*handler)(const unsigned long loct, const unsigned short loct_msec));
char register_time_sync_handler(void (*handler)(const char synced));
void unregister_time_sync_handler(void (*handler)(const char synced));
void get_unitime(unsigned long *loct, unsigned short *loct_msec);

#endif
